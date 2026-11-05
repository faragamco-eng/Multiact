/*
 * bsp_stm32g431.c - Register-level drivers (CMSIS only, no HAL).
 *
 * Peripheral plan
 *   TIM1   center-aligned PWM, CH1..3 + CH1N..3N -> IPM HIN/LIN U,V,W
 *          CCPC=1: CCxE/CCxNE/OCxM preloaded, applied by COMG (commutation)
 *          CH4 (PWM1, CCR4 = lead)  -> ADC2 trigger, ON-center (current)
 *          CH6 (PWM1/PWM2)          -> TRGO2 -> ADC1 trigger, ON or OFF center (BEMF)
 *          BKIN  (PB12) <- IPM fault output (active low)
 *          OCREF_CLR <- COMP2 : cycle-by-cycle current limit
 *   TIM2   32-bit 1 MHz timebase, CC1 = commutation one-shot
 *   ADC1   injected: BEMF_U (IN1 PA0), BEMF_V (IN2 PA1), BEMF_W (IN3 PA2), VBUS (IN4 PA3)
 *   ADC2   injected: IBUS (IN16 = OPAMP2 internal), NTC (IN3 PA6), POT (IN13 PA5)
 *   OPAMP2 PGA x4, VINP = PA7 (shunt), output internal to ADC2
 *   COMP2  INP = PA7 (shunt), INM = DAC3_CH2 (limit threshold)
 *   USART1 PB6 TX / PB7 RX, 115200 8N1 (via digital isolator!)
 *
 * Items marked VERIFY are encodings taken from RM0440; check them against
 * the reference manual revision you use during bring-up.
 */
#include "bsp.h"
#include "app_config.h"
#include "stm32g4xx.h"

/* ---------------- IPM input polarity (set from the IPM datasheet) ------- */
#ifndef IPM_HIN_ACTIVE_LOW
#define IPM_HIN_ACTIVE_LOW 0
#endif
#ifndef IPM_LIN_ACTIVE_LOW
#define IPM_LIN_ACTIVE_LOW 0
#endif

/* ---------------- small helpers ---------------- */
static void gpio_mode(GPIO_TypeDef *g, uint32_t pin, uint32_t mode)
{
    g->MODER = (g->MODER & ~(3u << (2 * pin))) | (mode << (2 * pin));
}
static void gpio_af(GPIO_TypeDef *g, uint32_t pin, uint32_t af)
{
    gpio_mode(g, pin, 2u);
    volatile uint32_t *afr = &g->AFR[pin >> 3];
    *afr = (*afr & ~(0xFu << (4 * (pin & 7)))) | (af << (4 * (pin & 7)));
    g->OSPEEDR |= 3u << (2 * pin);
}
static void gpio_out(GPIO_TypeDef *g, uint32_t pin, bool v)
{
    g->BSRR = v ? (1u << pin) : (1u << (pin + 16));
}

static volatile uint32_t s_ms;
static volatile ss_meas_t s_meas;
static volatile uint16_t s_pot;

/* ---------------- clocks: HSI16 -> PLL -> 170 MHz (boost mode) ---------- */
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
    /* 16 MHz / M(4) * N(85) / R(2) = 170 MHz */
    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSI | (3u << RCC_PLLCFGR_PLLM_Pos) |
                   (85u << RCC_PLLCFGR_PLLN_Pos) | RCC_PLLCFGR_PLLREN;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}

    /* >80 MHz switch: go through AHB/2 for >= 1 us (RM0440 6.2.7) */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_HPRE) | RCC_CFGR_HPRE_DIV2;
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
    for (volatile int i = 0; i < 200; i++) {}
    RCC->CFGR &= ~RCC_CFGR_HPRE;                       /* AHB = 170 MHz      */
    SystemCoreClock = SYSCLK_HZ;
}

/* ---------------- GPIO ---------------- */
static void gpio_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN | RCC_AHB2ENR_GPIOCEN;
    (void)RCC->AHB2ENR;

    /* Gate outputs: drive inactive as GPIO before handing them to TIM1.
     * External 10k pull-downs (or pull-ups for active-low inputs) keep
     * the IPM off during reset and while TIM1 releases a channel. */
    gpio_af(GPIOA, 8, 6);  gpio_af(GPIOA, 9, 6);  gpio_af(GPIOA, 10, 6);   /* CH1..3   */
    gpio_af(GPIOB, 13, 6); gpio_af(GPIOB, 14, 6); gpio_af(GPIOB, 15, 4);   /* CH1N..3N */
    gpio_af(GPIOB, 12, 6);                                                 /* BKIN     */
    GPIOB->PUPDR |= 1u << (2 * 12);           /* weak pull-up on FO (ext 10k too) */

    /* analog inputs are analog after reset (MODER=11) - make it explicit */
    const uint8_t apins[] = {0, 1, 2, 3, 5, 6, 7};
    for (unsigned i = 0; i < sizeof apins; i++) gpio_mode(GPIOA, apins[i], 3u);

    /* USART1 */
    gpio_af(GPIOB, 6, 7); gpio_af(GPIOB, 7, 7);

    /* LEDs, relay, button */
    gpio_mode(GPIOB, 4, 1u); gpio_mode(GPIOB, 5, 1u); gpio_mode(GPIOB, 10, 1u);
    gpio_out(GPIOB, 10, false);
    gpio_mode(GPIOB, 11, 0u);
    GPIOB->PUPDR = (GPIOB->PUPDR & ~(3u << 22)) | (1u << 22);   /* pull-up */
}

/* ---------------- TIM1 ---------------- */
#define CCER_POL ((IPM_HIN_ACTIVE_LOW ? (TIM_CCER_CC1P | TIM_CCER_CC2P | TIM_CCER_CC3P) : 0u) | \
                  (IPM_LIN_ACTIVE_LOW ? (TIM_CCER_CC1NP | TIM_CCER_CC2NP | TIM_CCER_CC3NP) : 0u))
#define CCER_ALWAYS (TIM_CCER_CC4E | TIM_CCER_CC6E | CCER_POL)

#define OCM_FROZEN     0u
#define OCM_FORCE_LOW  4u      /* force inactive: OCx off, OCxN on (complementary) */
#define OCM_PWM1       6u
#define OCM_PWM2       7u

static uint32_t ccmr_field(uint32_t ch, uint32_t ocm)
{
    /* OCxM[2:0] at bits 4..6 (+8 for odd channel), OCxM[3] at bit 16 (+8);
     * OCxPE (preload) and OCxCE (OCREF clear enable) always set. */
    uint32_t sh = (ch & 1u) ? 0u : 8u;           /* ch1/3 -> 0, ch2/4 -> 8 */
    uint32_t v = ((ocm & 7u) << 4) | ((ocm >> 3) << 16) | TIM_CCMR1_OC1PE | TIM_CCMR1_OC1CE;
    return v << sh;
}
static uint32_t ccmr_mask(uint32_t ch)
{
    uint32_t sh = (ch & 1u) ? 0u : 8u;
    return (TIM_CCMR1_OC1M | TIM_CCMR1_OC1PE | TIM_CCMR1_OC1CE | TIM_CCMR1_CC1S) << sh;
}

static void tim1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
    (void)RCC->APB2ENR;

    TIM1->CR1  = TIM_CR1_CMS_0 | TIM_CR1_ARPE;        /* center aligned 1   */
    TIM1->PSC  = 0;
    TIM1->ARR  = PWM_ARR;
    TIM1->RCR  = 0;
    TIM1->CCR1 = TIM1->CCR2 = TIM1->CCR3 = 0;

    TIM1->CCMR1 = ccmr_field(1, OCM_FORCE_LOW) | ccmr_field(2, OCM_FORCE_LOW);
    TIM1->CCMR2 = ccmr_field(3, OCM_FORCE_LOW) |
                  (((OCM_PWM1 & 7u) << 12) | TIM_CCMR2_OC4PE);           /* CH4   */
    TIM1->CCR4  = ADC_TRIG_LEAD_TICKS;
    TIM1->CCMR3 = ((OCM_PWM2 & 7u) << TIM_CCMR3_OC6M_Pos) | TIM_CCMR3_OC6PE; /* OFF center */
    TIM1->CCR6  = PWM_ARR - ADC_TRIG_LEAD_TICKS;

    /* TRGO2 = OC6REF (MMS2 = 1001)  VERIFY RM0440 TIMx_CR2 */
    TIM1->CR2 = TIM_CR2_CCPC | (9u << TIM_CR2_MMS2_Pos) |
                (IPM_HIN_ACTIVE_LOW ? (TIM_CR2_OIS1 | TIM_CR2_OIS2 | TIM_CR2_OIS3) : 0u) |
                (IPM_LIN_ACTIVE_LOW ? (TIM_CR2_OIS1N | TIM_CR2_OIS2N | TIM_CR2_OIS3N) : 0u);

    TIM1->CCER = CCER_ALWAYS;

    /* OCREF clear from COMP2 (OCRSEL = 001, VERIFY), source = internal */
    TIM1->SMCR &= ~TIM_SMCR_OCCS;
    TIM1->AF2 = (TIM1->AF2 & ~TIM1_AF2_OCRSEL) | (1u << TIM1_AF2_OCRSEL_Pos);

    /* Break input BKIN pin, active low (IPM FO), digital filter, OSSR/OSSI */
    TIM1->AF1 = (TIM1->AF1 & ~TIM1_AF1_BKINP) | TIM1_AF1_BKINE;
    TIM1->BDTR = PWM_DTG | TIM_BDTR_OSSR | TIM_BDTR_OSSI | TIM_BDTR_BKE |
                 (4u << TIM_BDTR_BKF_Pos);                 /* BKP=0: active low */

    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0;
    TIM1->DIER = TIM_DIER_BIE;
    NVIC_SetPriority(TIM1_BRK_TIM15_IRQn, IRQPRI_BREAK);
    NVIC_EnableIRQ(TIM1_BRK_TIM15_IRQn);
    TIM1->CR1 |= TIM_CR1_CEN;                     /* runs always: ADC triggers */
}

/* ---------------- TIM2: 1 MHz timebase + commutation compare ---------- */
static void tim2_init(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM2EN;
    (void)RCC->APB1ENR1;
    TIM2->PSC = SYSCLK_HZ / 1000000u - 1u;
    TIM2->ARR = 0xFFFFFFFFu;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0;
    NVIC_SetPriority(TIM2_IRQn, IRQPRI_MOTOR);
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 = TIM_CR1_CEN;
}

uint32_t bsp_now_us(void) { return TIM2->CNT; }

void bsp_delay_ms(uint32_t ms)
{
    uint32_t t0 = bsp_now_us();
    while (bsp_now_us() - t0 < ms * 1000u) {}
}

/* ---------------- analog front end ---------------- */
static void adc_enable(ADC_TypeDef *a)
{
    a->CR &= ~ADC_CR_DEEPPWD;
    a->CR |= ADC_CR_ADVREGEN;
    bsp_delay_ms(1);                                 /* tADCVREG_STUP 20 us */
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

#define SMP_12C5  2u
#define SMP_47C5  4u
#define JEXT_TIM1_CH4    1u    /* VERIFY RM0440 ADC12 injected triggers */
#define JEXT_TIM1_TRGO2  8u

static void analog_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;            /* clocks OPAMP + COMP */
    RCC->AHB2ENR |= RCC_AHB2ENR_ADC12EN | RCC_AHB2ENR_DAC3EN;
    (void)RCC->AHB2ENR;

    /* DAC3 CH2: internal only (MODE=011), HFSEL for AHB > 160 MHz */
    DAC3->MCR = (DAC3->MCR & ~(DAC_MCR_MODE2 | DAC_MCR_HFSEL)) |
                (3u << DAC_MCR_MODE2_Pos) | (2u << DAC_MCR_HFSEL_Pos);
    DAC3->DHR12R2 = (uint32_t)(I_LIMIT_CBC_A * SHUNT_OHM / ADC_VREF * ADC_FS);
    DAC3->CR |= DAC_CR_EN2;
    bsp_delay_ms(1);

    /* COMP2: INP=PA7 (INPSEL=0), INM=DAC3_CH2 (INMSEL=100, VERIFY), low hyst */
    COMP2->CSR = (4u << COMP_CSR_INMSEL_Pos) | (1u << COMP_CSR_HYST_Pos);
    COMP2->CSR |= COMP_CSR_EN;

    /* OPAMP2: PGA x4 (PGGAIN=00001), VINP0=PA7, output to ADC2 internally */
    OPAMP2->CSR = (2u << OPAMP_CSR_VMSEL_Pos) | (0u << OPAMP_CSR_VPSEL_Pos) |
                  (1u << OPAMP_CSR_PGGAIN_Pos) | OPAMP_CSR_OPAMPINTEN | OPAMP_CSR_HIGHSPEEDEN;
    OPAMP2->CSR |= OPAMP_CSR_OPAMPxEN;

    /* ADC clock: synchronous HCLK/4 = 42.5 MHz */
    ADC12_COMMON->CCR = (ADC12_COMMON->CCR & ~ADC_CCR_CKMODE) | (3u << ADC_CCR_CKMODE_Pos);
    adc_enable(ADC1);
    adc_enable(ADC2);

    adc_smp(ADC1, 1, SMP_12C5); adc_smp(ADC1, 2, SMP_12C5);
    adc_smp(ADC1, 3, SMP_12C5); adc_smp(ADC1, 4, SMP_12C5);
    adc_smp(ADC2, 16, SMP_12C5); adc_smp(ADC2, 3, SMP_47C5); adc_smp(ADC2, 13, SMP_47C5);

    ADC1->CFGR |= ADC_CFGR_JQDIS;
    ADC2->CFGR |= ADC_CFGR_JQDIS;
    ADC1->JSQR = (3u << ADC_JSQR_JL_Pos) | (JEXT_TIM1_TRGO2 << ADC_JSQR_JEXTSEL_Pos) |
                 (1u << ADC_JSQR_JEXTEN_Pos) |
                 (1u << ADC_JSQR_JSQ1_Pos) | (2u << ADC_JSQR_JSQ2_Pos) |
                 (3u << ADC_JSQR_JSQ3_Pos) | (4u << ADC_JSQR_JSQ4_Pos);
    ADC2->JSQR = (2u << ADC_JSQR_JL_Pos) | (JEXT_TIM1_CH4 << ADC_JSQR_JEXTSEL_Pos) |
                 (1u << ADC_JSQR_JEXTEN_Pos) |
                 (16u << ADC_JSQR_JSQ1_Pos) | (3u << ADC_JSQR_JSQ2_Pos) |
                 (13u << ADC_JSQR_JSQ3_Pos);

    ADC1->ISR = ADC_ISR_JEOS;
    ADC1->IER = ADC_IER_JEOSIE;
    NVIC_SetPriority(ADC1_2_IRQn, IRQPRI_MOTOR);
    NVIC_EnableIRQ(ADC1_2_IRQn);
    ADC2->CR |= ADC_CR_JADSTART;
    ADC1->CR |= ADC_CR_JADSTART;
}

/* Called from ADC1_2_IRQHandler: acknowledge and latch all channels. */
void bsp_adc_isr_read(ss_meas_t *m);
void bsp_adc_isr_read(ss_meas_t *m)
{
    ADC1->ISR = ADC_ISR_JEOS;
    m->bemf[0] = (uint16_t)ADC1->JDR1;
    m->bemf[1] = (uint16_t)ADC1->JDR2;
    m->bemf[2] = (uint16_t)ADC1->JDR3;
    m->vbus    = (uint16_t)ADC1->JDR4;
    m->ibus    = (uint16_t)ADC2->JDR1;
    m->ntc     = (uint16_t)ADC2->JDR2;
    s_pot      = (uint16_t)ADC2->JDR3;
    s_meas = *m;
}

void bsp_read_meas(ss_meas_t *m)
{
    __disable_irq();
    *m = s_meas;
    __enable_irq();
}
uint16_t bsp_pot_raw(void) { return s_pot; }

/* ---------------- USART1 ---------------- */
#define RXBUF 64u
static volatile uint8_t rxb[RXBUF];
static volatile uint8_t rx_h, rx_t;

static void uart_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    USART1->BRR = SYSCLK_HZ / 115200u;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE;
    USART1->CR1 |= USART_CR1_UE;
    NVIC_SetPriority(USART1_IRQn, IRQPRI_UART);
    NVIC_EnableIRQ(USART1_IRQn);
}

void USART1_IRQHandler(void)
{
    if (USART1->ISR & USART_ISR_RXNE_RXFNE) {
        uint8_t c = (uint8_t)USART1->RDR;
        uint8_t n = (uint8_t)((rx_h + 1u) % RXBUF);
        if (n != rx_t) { rxb[rx_h] = c; rx_h = n; }
    }
    if (USART1->ISR & USART_ISR_ORE)
        USART1->ICR = USART_ICR_ORECF;
}

int bsp_uart_getc(void)
{
    if (rx_t == rx_h) return -1;
    int c = rxb[rx_t];
    rx_t = (uint8_t)((rx_t + 1u) % RXBUF);
    return c;
}

void bsp_uart_write(const char *s)
{
    while (*s) {
        while (!(USART1->ISR & USART_ISR_TXE_TXFNF)) {}
        USART1->TDR = (uint8_t)*s++;
    }
}

/* ---------------- misc IO ---------------- */
void bsp_led_run(bool on)   { gpio_out(GPIOB, 4, on); }
void bsp_led_fault(bool on) { gpio_out(GPIOB, 5, on); }
void bsp_relay(bool on)     { gpio_out(GPIOB, 10, on); }
bool bsp_button(void)       { return !(GPIOB->IDR & (1u << 11)); }

static void iwdg_init(void)
{
    IWDG->KR = 0x5555u;
    IWDG->PR = 1u;                   /* LSI 32 kHz / 8 = 4 kHz          */
    IWDG->RLR = 400u;                /* 100 ms                          */
    IWDG->KR = 0xCCCCu;
}
void bsp_wdg_kick(void) { IWDG->KR = 0xAAAAu; }

/* ---------------- motor HAL ---------------- */
static void hal_outputs_off(void)
{
    TIM1->BDTR &= ~TIM_BDTR_MOE;     /* immediate: idle state = all off */
    TIM1->CCER = CCER_ALWAYS;        /* preload: nothing enabled        */
    TIM1->EGR = TIM_EGR_COMG;
}

static void hal_low_sides_on(void)
{
    /* all three legs "force inactive": high side off, low side on */
    TIM1->CCMR1 = (TIM1->CCMR1 & ~(ccmr_mask(1) | ccmr_mask(2))) |
                  ccmr_field(1, OCM_FORCE_LOW) | ccmr_field(2, OCM_FORCE_LOW);
    TIM1->CCMR2 = (TIM1->CCMR2 & ~ccmr_mask(3)) | ccmr_field(3, OCM_FORCE_LOW);
    TIM1->CCER = CCER_ALWAYS | TIM_CCER_CC1E | TIM_CCER_CC1NE | TIM_CCER_CC2E |
                 TIM_CCER_CC2NE | TIM_CCER_CC3E | TIM_CCER_CC3NE;
    TIM1->EGR = TIM_EGR_COMG;
    TIM1->BDTR |= TIM_BDTR_MOE;
}

static void hal_apply_step(uint8_t hi, uint8_t lo)
{
    uint32_t ccmr1 = TIM1->CCMR1 & ~(ccmr_mask(1) | ccmr_mask(2));
    uint32_t ccmr2 = TIM1->CCMR2 & ~ccmr_mask(3);
    uint32_t ccer = CCER_ALWAYS;
    for (uint32_t p = 0; p < 3; p++) {
        uint32_t ocm = (p == hi) ? OCM_PWM1 : (p == lo) ? OCM_FORCE_LOW : OCM_FROZEN;
        if (p == hi || p == lo)
            ccer |= (TIM_CCER_CC1E | TIM_CCER_CC1NE) << (4u * p);
        if (p < 2) ccmr1 |= ccmr_field(p + 1u, ocm);
        else       ccmr2 |= ccmr_field(3, ocm);
    }
    TIM1->CCMR1 = ccmr1;             /* preloaded (CCPC=1)              */
    TIM1->CCMR2 = ccmr2;
    TIM1->CCER = ccer;
    TIM1->EGR = TIM_EGR_COMG;        /* apply all three legs at once    */
    TIM1->BDTR |= TIM_BDTR_MOE;
}

static void hal_set_duty(float d)
{
    uint32_t c = (uint32_t)(d * (float)PWM_ARR);
    TIM1->CCR1 = c; TIM1->CCR2 = c; TIM1->CCR3 = c;   /* preloaded */
}

static void hal_set_sample_on(bool on)
{
    uint32_t m = TIM1->CCMR3 & ~TIM_CCMR3_OC6M;
    if (on) { TIM1->CCMR3 = m | ((OCM_PWM1 & 7u) << TIM_CCMR3_OC6M_Pos); TIM1->CCR6 = ADC_TRIG_LEAD_TICKS; }
    else    { TIM1->CCMR3 = m | ((OCM_PWM2 & 7u) << TIM_CCMR3_OC6M_Pos); TIM1->CCR6 = PWM_ARR - ADC_TRIG_LEAD_TICKS; }
}

static void hal_comm_arm(uint32_t at)
{
    TIM2->CCR1 = at;
    TIM2->SR = ~TIM_SR_CC1IF;
    TIM2->DIER |= TIM_DIER_CC1IE;
    if ((int32_t)(at - TIM2->CNT) <= 0)       /* already due */
        TIM2->EGR = TIM_EGR_CC1G;
}

static void hal_comm_cancel(void)
{
    TIM2->DIER &= ~TIM_DIER_CC1IE;
    TIM2->SR = ~TIM_SR_CC1IF;
}

static void hal_lock(void)   { __disable_irq(); }
static void hal_unlock(void) { __enable_irq(); }

const ss_hal_t bsp_hal = {
    hal_outputs_off, hal_low_sides_on, hal_apply_step, hal_set_duty,
    hal_set_sample_on, hal_comm_arm, hal_comm_cancel, hal_lock, hal_unlock
};

/* ---------------- init ---------------- */
void bsp_init(void)
{
    clock_init();
    gpio_init();
    tim2_init();
    tim1_init();
    hal_outputs_off();
    analog_init();
    uart_init();
    SysTick_Config(SYSCLK_HZ / 1000u);
    NVIC_SetPriority(SysTick_IRQn, IRQPRI_TICK);
    iwdg_init();
    (void)s_ms;
}
