/*
 * bsp_v2.c - Register-level drivers for the v2 cooler drive (CMSIS only).
 *
 *   TIM1   center-aligned 16 kHz, CH1..3 + CH1N..3N complementary, dead
 *          time 1 us (IRS2186 has no internal dead time), both outputs
 *          active high (IRS2186 HIN/LIN non-inverting, 10k pull-downs).
 *          OC4REF (PWM2, CCR4 = ARR - lead) -> TRGO2 -> injected ADC
 *          trigger just before the counter peak = middle of the low-side
 *          on-time, where the three low-side shunts carry phase current.
 *          BKIN  = PB12 heatsink switch (active high, filtered)
 *          BKIN2 = COMP1/2/4 outputs (shoot-through / short circuit)
 *   OPAMP1/2/3  PGA x4 on PA1/PA7/PB0, outputs internal to ADC1/ADC2
 *   COMP1/2/4   same pins vs VREFINT (1.21 V = ~40 A in a low-side shunt)
 *   ADC1 inj.  I_U (OPAMP1), VBUS, VIN_L, VIN_N
 *   ADC2 inj.  I_V (OPAMP2), I_W (OPAMP3), NTC
 *   USART1 115200 8N1 with receiver timeout (Modbus 3.5 char gap)
 *   IWDG   250 ms
 *
 * Encodings marked VERIFY are from RM0440 rev. 8; check them during
 * bring-up against the reference manual revision you use.
 */
#include "bsp_v2.h"
#include "stm32g4xx.h"
#include <math.h>
#include <string.h>

#define ADC_VREF      3.3f
#define ADC_FS        4095.0f
#define K_VBUS        ((990.0f + 6.65f) / 6.65f)
#define K_VIN         ((3000.0f + 10.0f) / 10.0f)
#define SHUNT_OHM     0.025f
#define SHIFT_GAIN    (10.0f / 11.0f)
#define PGA_GAIN      4.0f
#define ADC_LEAD      ((uint32_t)(SYSCLK_HZ * 1.2e-6f / 2.0f))   /* trigger 1.2 us before the peak */
#define MIN_LOW_S     3.0e-6f                                       /* low-side window for a valid sample */

/* ---------------------------------------------------------------- helpers */
static void gpio_mode(GPIO_TypeDef *g, uint32_t pin, uint32_t mode)
{
    g->MODER = (g->MODER & ~(3u << (2 * pin))) | (mode << (2 * pin));
}
static void gpio_af(GPIO_TypeDef *g, uint32_t pin, uint32_t af)
{
    volatile uint32_t *afr = &g->AFR[pin >> 3];
    *afr = (*afr & ~(0xFu << (4 * (pin & 7)))) | (af << (4 * (pin & 7)));
    g->OSPEEDR |= 3u << (2 * pin);
    gpio_mode(g, pin, 2u);
}
static void gpio_out(GPIO_TypeDef *g, uint32_t pin, bool v) { g->BSRR = v ? (1u << pin) : (1u << (pin + 16)); }

static volatile uint32_t s_ms;
void SysTick_Handler(void) { s_ms++; }
uint32_t bsp_ms(void) { return s_ms; }
static void delay_ms(uint32_t ms) { uint32_t t = s_ms; while (s_ms - t < ms) {} }

/* ---------------------------------------------------------------- clock */
static void clock_init(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1;
    PWR->CR5 &= ~PWR_CR5_R1MODE;                       /* range 1 boost      */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | FLASH_ACR_LATENCY_4WS |
                 FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
    while ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_4WS) {}
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {}
    /* HSI16 / 4 * 85 / 2 = 170 MHz */
    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSI | (3u << RCC_PLLCFGR_PLLM_Pos) |
                   (85u << RCC_PLLCFGR_PLLN_Pos) | RCC_PLLCFGR_PLLREN;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_HPRE) | RCC_CFGR_HPRE_DIV2;     /* >80 MHz step */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
    for (volatile int i = 0; i < 200; i++) {}
    RCC->CFGR &= ~RCC_CFGR_HPRE;
    SystemCoreClock = SYSCLK_HZ;
}

/* ---------------------------------------------------------------- GPIO */
static void gpio_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN;
    (void)RCC->AHB2ENR;
    /* outputs low first (relays off, PFC off = PFC_OFF high) */
    GPIOB->BSRR = (1u << (4 + 16)) | (1u << (5 + 16)) | (1u << (10 + 16)) | (1u << (11 + 16));
    GPIOA->BSRR = (1u << 11) | (1u << (12 + 16));
    gpio_mode(GPIOB, 4, 1u); gpio_mode(GPIOB, 5, 1u); gpio_mode(GPIOB, 10, 1u); gpio_mode(GPIOB, 11, 1u);
    gpio_mode(GPIOA, 11, 1u); gpio_mode(GPIOA, 12, 1u);
    /* gate signals: AF6 TIM1 (PB15 CH3N is AF4), BKIN PB12 AF6 */
    gpio_af(GPIOA, 8, 6); gpio_af(GPIOA, 9, 6); gpio_af(GPIOA, 10, 6);
    gpio_af(GPIOB, 13, 6); gpio_af(GPIOB, 14, 6); gpio_af(GPIOB, 15, 4);
    gpio_af(GPIOB, 12, 6);
    /* USART1 PB6/PB7 AF7 */
    gpio_af(GPIOB, 6, 7); gpio_af(GPIOB, 7, 7);
    GPIOB->PUPDR = (GPIOB->PUPDR & ~(3u << 14)) | (1u << 14);          /* RX pull-up */
    /* analog: PA0..PA7, PB0 */
    for (uint32_t p = 0; p <= 7; p++) gpio_mode(GPIOA, p, 3u);
    gpio_mode(GPIOB, 0, 3u);
}

void bsp_relays(bool k1, bool k2, bool pump, bool drain)
{
    gpio_out(GPIOB, 4, k1); gpio_out(GPIOB, 5, k2); gpio_out(GPIOB, 10, pump); gpio_out(GPIOB, 11, drain);
}
void bsp_pfc(bool on) { gpio_out(GPIOA, 11, !on); }
void bsp_led(bool on) { gpio_out(GPIOA, 12, on); }

/* ---------------------------------------------------------------- TIM1 */
/* dead-time generator: 1 us = 170 ticks of 5.88 ns -> DTG[7:6]=10, (64 + x) * 2 */
static uint32_t dtg(uint32_t ns)
{
    uint32_t t = (uint32_t)((uint64_t)ns * SYSCLK_HZ / 1000000000u);
    if (t <= 127u) return t;
    if (t <= 254u) return 0x80u | ((t / 2u - 64u) & 0x3Fu);
    if (t <= 504u) return 0xC0u | ((t / 8u - 32u) & 0x1Fu);
    return 0xFFu;
}

static void tim1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    (void)RCC->APB2ENR;
    TIM1->CR1 = TIM_CR1_CMS_0 | TIM_CR1_ARPE;               /* center aligned mode 1 */
    TIM1->PSC = 0;
    TIM1->ARR = PWM_ARR;
    TIM1->RCR = 1;                                          /* update once per period */
    TIM1->CCR1 = TIM1->CCR2 = TIM1->CCR3 = PWM_ARR / 2u;
    /* CH1..3 PWM1 (active while CNT < CCR: high side on around the valley), preload */
    TIM1->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE | (6u << TIM_CCMR1_OC2M_Pos) | TIM_CCMR1_OC2PE;
    TIM1->CCMR2 = (6u << TIM_CCMR2_OC3M_Pos) | TIM_CCMR2_OC3PE |
                  (7u << TIM_CCMR2_OC4M_Pos) | TIM_CCMR2_OC4PE;     /* CH4 PWM2: rises at CCR4 going up */
    TIM1->CCR4 = PWM_ARR - ADC_LEAD;
    /* TRGO2 = OC4REF (MMS2 = 0111, VERIFY) */
    TIM1->CR2 = (7u << TIM_CR2_MMS2_Pos);                   /* OISx = 0: idle = all low = all off */
    TIM1->CCER = TIM_CCER_CC1E | TIM_CCER_CC1NE | TIM_CCER_CC2E | TIM_CCER_CC2NE |
                 TIM_CCER_CC3E | TIM_CCER_CC3NE;            /* polarity: all active high */
    /* BKIN: pin, active high (switch open), filter; BKIN2: COMP1/2/4, active high */
    TIM1->AF1 = TIM1_AF1_BKINE;                             /* BKINP = 0: not inverted */
    TIM1->AF2 = TIM1_AF2_BK2CMP1E | TIM1_AF2_BK2CMP2E | TIM1_AF2_BK2CMP4E;
    TIM1->BDTR = dtg(DEADTIME_NS) | TIM_BDTR_OSSR | TIM_BDTR_OSSI |
                 TIM_BDTR_BKE | TIM_BDTR_BKP | (6u << TIM_BDTR_BKF_Pos) |
                 TIM_BDTR_BK2E | TIM_BDTR_BK2P | (1u << TIM_BDTR_BK2F_Pos);
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0;
    TIM1->DIER = TIM_DIER_BIE;
    NVIC_SetPriority(TIM1_BRK_TIM15_IRQn, IRQPRI_BREAK);
    NVIC_EnableIRQ(TIM1_BRK_TIM15_IRQn);
    TIM1->CR1 |= TIM_CR1_CEN;                               /* always running: ADC triggers */
}

static volatile bool s_brk_oc, s_brk_hw;
void TIM1_BRK_TIM15_IRQHandler(void)
{
    uint32_t sr = TIM1->SR;
    if (sr & TIM_SR_B2IF) s_brk_oc = true;
    s_brk_hw = true;
    TIM1->SR = ~(TIM_SR_BIF | TIM_SR_B2IF);
}
bool bsp_brk_thermal(void) { return (GPIOB->IDR & (1u << 12)) != 0u; }
bool bsp_brk_oc_latched(void) { return s_brk_oc; }
bool bsp_brk_active(void) { return s_brk_hw; }
void bsp_brk_clear(void) { s_brk_oc = false; s_brk_hw = false; }

static float s_duty_prev[3] = {0.5f, 0.5f, 0.5f};
void bsp_pwm_set(const float duty[3], bool enable)
{
    if (!enable) { TIM1->BDTR &= ~TIM_BDTR_MOE; return; }
    volatile uint32_t *ccr[3] = { &TIM1->CCR1, &TIM1->CCR2, &TIM1->CCR3 };
    for (int k = 0; k < 3; k++) {
        float d = duty[k] < 0.0f ? 0.0f : duty[k] > 1.0f ? 1.0f : duty[k];
        *ccr[k] = (uint32_t)(d * (float)PWM_ARR);
        s_duty_prev[k] = d;
    }
    /* re-arm only when no break source is active (level inputs) */
    if (!(TIM1->BDTR & TIM_BDTR_MOE) && !s_brk_hw) TIM1->BDTR |= TIM_BDTR_MOE;
}

/* ---------------------------------------------------------------- analog */
static void adc_enable(ADC_TypeDef *a)
{
    a->CR &= ~ADC_CR_DEEPPWD;
    a->CR |= ADC_CR_ADVREGEN;
    delay_ms(1);
    a->CR &= ~ADC_CR_ADCALDIF;
    a->CR |= ADC_CR_ADCAL;
    while (a->CR & ADC_CR_ADCAL) {}
    for (volatile int i = 0; i < 100; i++) {}
    a->ISR = ADC_ISR_ADRDY;
    a->CR |= ADC_CR_ADEN;
    while (!(a->ISR & ADC_ISR_ADRDY)) {}
}
static void adc_smp(ADC_TypeDef *a, uint32_t ch, uint32_t smp)
{
    if (ch < 10) a->SMPR1 = (a->SMPR1 & ~(7u << (3 * ch))) | (smp << (3 * ch));
    else         a->SMPR2 = (a->SMPR2 & ~(7u << (3 * (ch - 10)))) | (smp << (3 * (ch - 10)));
}
#define SMP_6C5   1u
#define SMP_47C5  4u
#define JEXT_TIM1_TRGO2 8u        /* VERIFY RM0440 ADC12 injected trigger table */
#define CH_OPAMP1 13u             /* ADC1_IN13 = OPAMP1 VOUT (VERIFY) */
#define CH_OPAMP2 16u             /* ADC2_IN16 = OPAMP2 VOUT */
#define CH_OPAMP3 18u             /* ADC2_IN18 = OPAMP3 VOUT */

static void opamp_pga4(OPAMP_TypeDef *o)
{
    /* VINP0 (PA1 / PA7 / PB0), VINM = PGA (VMSEL=10), gain 4 (PGGAIN=00001), output internal */
    o->CSR = (2u << OPAMP_CSR_VMSEL_Pos) | (0u << OPAMP_CSR_VPSEL_Pos) | (1u << OPAMP_CSR_PGGAIN_Pos) |
             OPAMP_CSR_OPAMPINTEN | OPAMP_CSR_HIGHSPEEDEN;
    o->CSR |= OPAMP_CSR_OPAMPxEN;
}
static void comp_vrefint(COMP_TypeDef *c)
{
    /* INP = PA1 / PA7 / PB0 (INPSEL=0), INM = VREFINT (INMSEL=011), medium hysteresis */
    c->CSR = (3u << COMP_CSR_INMSEL_Pos) | (2u << COMP_CSR_HYST_Pos);
    c->CSR |= COMP_CSR_EN;
}

static void analog_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;                  /* clocks OPAMP + COMP */
    RCC->AHB2ENR |= RCC_AHB2ENR_ADC12EN | RCC_AHB2ENR_DAC1EN;
    (void)RCC->AHB2ENR;
    opamp_pga4(OPAMP1); opamp_pga4(OPAMP2); opamp_pga4(OPAMP3);
    comp_vrefint(COMP1); comp_vrefint(COMP2); comp_vrefint(COMP4);
    /* DAC1 CH1 -> PA4 debug */
    DAC1->MCR = (DAC1->MCR & ~DAC_MCR_HFSEL) | (2u << DAC_MCR_HFSEL_Pos);
    DAC1->CR |= DAC_CR_EN1;

    ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~ADC_CCR_CKMODE) | (3u << ADC_CCR_CKMODE_Pos) |
                        ADC_CCR_VREFEN;                    /* HCLK/4 = 42.5 MHz */
    adc_enable(ADC1);
    adc_enable(ADC2);
    adc_smp(ADC1, CH_OPAMP1, SMP_6C5); adc_smp(ADC2, CH_OPAMP2, SMP_6C5); adc_smp(ADC2, CH_OPAMP3, SMP_6C5);
    adc_smp(ADC1, 1, SMP_47C5); adc_smp(ADC1, 3, SMP_47C5); adc_smp(ADC1, 4, SMP_47C5);
    adc_smp(ADC2, 3, SMP_47C5);
    ADC1->CFGR |= ADC_CFGR_JQDIS;
    ADC2->CFGR |= ADC_CFGR_JQDIS;
    ADC1->JSQR = (3u << ADC_JSQR_JL_Pos) | (JEXT_TIM1_TRGO2 << ADC_JSQR_JEXTSEL_Pos) | (1u << ADC_JSQR_JEXTEN_Pos) |
                 (CH_OPAMP1 << ADC_JSQR_JSQ1_Pos) | (1u << ADC_JSQR_JSQ2_Pos) |
                 (3u << ADC_JSQR_JSQ3_Pos) | (4u << ADC_JSQR_JSQ4_Pos);
    ADC2->JSQR = (2u << ADC_JSQR_JL_Pos) | (JEXT_TIM1_TRGO2 << ADC_JSQR_JEXTSEL_Pos) | (1u << ADC_JSQR_JEXTEN_Pos) |
                 (CH_OPAMP2 << ADC_JSQR_JSQ1_Pos) | (CH_OPAMP3 << ADC_JSQR_JSQ2_Pos) |
                 (3u << ADC_JSQR_JSQ3_Pos);
    ADC1->ISR = ADC_ISR_JEOS;
    ADC1->IER = ADC_IER_JEOSIE;                             /* ADC1 sequence is the longer one */
    NVIC_SetPriority(ADC1_2_IRQn, IRQPRI_PWM);
    NVIC_EnableIRQ(ADC1_2_IRQn);
    ADC1->CR |= ADC_CR_JADSTART;
    ADC2->CR |= ADC_CR_JADSTART;
}

void bsp_dac(float v) { DAC1->DHR12R1 = (uint32_t)(fminf(fmaxf(v, 0.0f), ADC_VREF) / ADC_VREF * ADC_FS); }

/* current offsets (ADC volts at zero current), calibrated with outputs off */
static float s_ioff[3] = {1.2f, 1.2f, 1.2f};
static volatile float s_acc[3];
static volatile uint32_t s_acc_n;
static volatile bool s_calibrating;

static inline float adc_v(uint32_t raw) { return (float)raw * (ADC_VREF / ADC_FS); }
static float ntc_degc(float v)
{
    if (v < 0.05f) return 200.0f;                          /* shorted NTC: treat as hot */
    if (v > 3.25f) return -40.0f;                          /* open NTC */
    float r = 10000.0f * v / (ADC_VREF - v);
    return 1.0f / (1.0f / 298.15f + logf(r / 10000.0f) / 3435.0f) - 273.15f;
}

static float s_tlp = 25.0f;
void ADC1_2_IRQHandler(void)
{
    ADC1->ISR = ADC_ISR_JEOS;
    float v[3] = { adc_v(ADC1->JDR1), adc_v(ADC2->JDR1), adc_v(ADC2->JDR2) };
    if (s_calibrating) {
        for (int k = 0; k < 3; k++) s_acc[k] += v[k];
        s_acc_n++;
    }
    bsp_meas_t m;
    const float k_i = 1.0f / (PGA_GAIN * SHUNT_OHM * SHIFT_GAIN);
    for (int k = 0; k < 3; k++) {
        /* shunt voltage is positive for current flowing OUT of the motor phase */
        m.i[k] = -(v[k] - s_ioff[k]) * k_i;
        m.valid[k] = (1.0f - s_duty_prev[k]) * (1.0f / (float)PWM_HZ) > MIN_LOW_S;
    }
    m.vbus = adc_v(ADC1->JDR2) * K_VBUS;
    m.vin_l = adc_v(ADC1->JDR3) * K_VIN;
    m.vin_n = adc_v(ADC1->JDR4) * K_VIN;
    s_tlp += (ntc_degc(adc_v(ADC2->JDR3)) - s_tlp) * 0.001f;
    m.t_hs = s_tlp;
    m.vcap_low = 0.0f;                                     /* parallel bus capacitors: no mid-point */
    bsp_pwm_isr(&m);
}

void bsp_current_offset_calibrate(void)
{
    TIM1->BDTR &= ~TIM_BDTR_MOE;
    s_acc[0] = s_acc[1] = s_acc[2] = 0.0f;
    s_acc_n = 0;
    s_calibrating = true;
    while (s_acc_n < 512u) {}
    s_calibrating = false;
    for (int k = 0; k < 3; k++) {
        float o = s_acc[k] / (float)s_acc_n;
        if (o > 1.0f && o < 1.4f) s_ioff[k] = o;          /* else keep nominal (hardware fault) */
    }
}

/* ---------------------------------------------------------------- USART1 */
#define RXN 256u
static volatile uint8_t rxb[RXN];
static volatile uint16_t rx_h, rx_t;
static volatile bool s_gap;
static uint8_t txb[256];
static volatile uint16_t tx_n, tx_i;

static void uart_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    USART1->BRR = SYSCLK_HZ / 115200u;
    USART1->RTOR = 35u;                                     /* 3.5 characters of 10 bits */
    USART1->CR2 = USART_CR2_RTOEN;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_RTOIE;
    USART1->CR1 |= USART_CR1_UE;
    NVIC_SetPriority(USART1_IRQn, IRQPRI_UART);
    NVIC_EnableIRQ(USART1_IRQn);
}

void USART1_IRQHandler(void)
{
    uint32_t isr = USART1->ISR;
    if (isr & USART_ISR_RXNE_RXFNE) {
        uint8_t c = (uint8_t)USART1->RDR;
        uint16_t n = (uint16_t)((rx_h + 1u) % RXN);
        if (n != rx_t) { rxb[rx_h] = c; rx_h = n; }
    }
    if (isr & USART_ISR_RTOF) { USART1->ICR = USART_ICR_RTOCF; s_gap = true; }
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    if ((USART1->CR1 & USART_CR1_TXEIE_TXFNFIE) && (isr & USART_ISR_TXE_TXFNF)) {
        if (tx_i < tx_n) USART1->TDR = txb[tx_i++];
        else USART1->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
    }
}

int bsp_uart_getc(void)
{
    if (rx_t == rx_h) return -1;
    int c = rxb[rx_t];
    rx_t = (uint16_t)((rx_t + 1u) % RXN);
    return c;
}
bool bsp_uart_gap(void)
{
    if (!s_gap || rx_t != rx_h) return false;              /* deliver the bytes first */
    s_gap = false;
    return true;
}
void bsp_uart_send(const uint8_t *p, uint16_t n)
{
    if (n > sizeof txb) n = sizeof txb;
    while (bsp_uart_tx_busy()) {}
    memcpy(txb, p, n);
    tx_i = 0;
    tx_n = n;
    USART1->CR1 |= USART_CR1_TXEIE_TXFNFIE;
}
bool bsp_uart_tx_busy(void) { return tx_i < tx_n || !(USART1->ISR & USART_ISR_TC); }

/* ---------------------------------------------------------------- flash */
static void flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) { FLASH->KEYR = 0x45670123u; FLASH->KEYR = 0xCDEF89ABu; }
}
static bool flash_wait(void)
{
    while (FLASH->SR & FLASH_SR_BSY) { IWDG->KR = 0xAAAAu; }
    uint32_t err = FLASH->SR & (FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_SIZERR |
                                FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR | FLASH_SR_OPERR);
    FLASH->SR = err | FLASH_SR_EOP;
    return err == 0u;
}
bool bsp_flash_erase(uint32_t addr)
{
    flash_unlock();
    flash_wait();
    uint32_t page = (addr - FLASH_BASE) / 2048u;
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PNB) | FLASH_CR_PER | (page << FLASH_CR_PNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;
    bool ok = flash_wait();
    FLASH->CR &= ~FLASH_CR_PER;
    FLASH->CR |= FLASH_CR_LOCK;
    return ok;
}
bool bsp_flash_program(uint32_t addr, const void *src, uint32_t len)
{
    const uint8_t *s = src;
    bool ok = true;
    flash_unlock();
    flash_wait();
    FLASH->CR |= FLASH_CR_PG;
    for (uint32_t i = 0; i < len && ok; i += 8u) {
        uint32_t w0, w1;
        memcpy(&w0, s + i, 4);
        memcpy(&w1, s + i + 4, 4);
        *(volatile uint32_t *)(addr + i) = w0;
        __ISB();
        *(volatile uint32_t *)(addr + i + 4u) = w1;
        ok = flash_wait();
    }
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;
    return ok;
}
const uint8_t *bsp_flash_ptr(uint32_t addr) { return (const uint8_t *)addr; }

/* ---------------------------------------------------------------- misc */
static void iwdg_init(void)
{
    IWDG->KR = 0x5555u;
    IWDG->PR = 2u;                       /* LSI 32 kHz / 16 = 2 kHz */
    IWDG->RLR = 500u;                    /* 250 ms                  */
    IWDG->KR = 0xCCCCu;
}
void bsp_wdg_kick(void) { IWDG->KR = 0xAAAAu; }
void bsp_reset(void) { NVIC_SystemReset(); }

void bsp_init(void)
{
    clock_init();
    gpio_init();
    SysTick_Config(SYSCLK_HZ / 1000u);
    NVIC_SetPriority(SysTick_IRQn, IRQPRI_TICK);
    tim1_init();
    TIM1->BDTR &= ~TIM_BDTR_MOE;
    analog_init();
    uart_init();
    iwdg_init();
}
