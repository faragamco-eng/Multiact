/*
 * phy_main.c - STM32C011F6P6 panel-bus PHY (hardware/v2 sheet 7, U8).
 *
 *   PA4  PB_TX    out: high = 16 V (space) via Q8 / R92
 *   PA5  PB_ISNS  ADC_IN5: return current, 4.7 R -> 4.7 mV/mA
 *   PA6  PB_VSNS  ADC_IN6: bus voltage / 11
 *   PA0  LED_S    on = bus OK, flickers on traffic, fast blink = bus short
 *   PB6/PB7 USART1 115200 -> ISO7762 -> hot-side MCU
 *   PA1/PA2/PA3   RS-485 (battery module option, not used yet)
 *   TIM14 19.2 kHz: sample + soft UART (phy_core)
 * Items marked VERIFY: AF numbers / bit positions from RM0490, check at bring-up.
 */
#include "stm32c0xx.h"
#include "phy_core.h"

#define SYSCLK 48000000u
static phy_t phy;
static volatile uint32_t s_ms;
static volatile uint16_t s_vsns_raw;
static volatile uint8_t s_tick_div;

static void gpio_mode(GPIO_TypeDef *g, uint32_t pin, uint32_t mode) { g->MODER = (g->MODER & ~(3u << (2 * pin))) | (mode << (2 * pin)); }
static void gpio_af(GPIO_TypeDef *g, uint32_t pin, uint32_t af)
{
    volatile uint32_t *afr = &g->AFR[pin >> 3];
    *afr = (*afr & ~(0xFu << (4 * (pin & 7)))) | (af << (4 * (pin & 7)));
    gpio_mode(g, pin, 2u);
}

void SysTick_Handler(void) { s_ms++; }

/* 19.2 kHz: previous ADC result -> PHY, start the next conversion */
void TIM14_IRQHandler(void)
{
    TIM14->SR = 0;
    uint16_t raw = (uint16_t)ADC1->DR;
    bool vs = (ADC1->CHSELR & ADC_CHSELR_CHSEL6) != 0u;
    if (vs) s_vsns_raw = raw;
    else {
        float i_ma = (float)raw * (3300.0f / 4095.0f / 4.7f);
        bool space = phy_tick(&phy, i_ma);
        if (space) GPIOA->BSRR = 1u << 4; else GPIOA->BRR = 1u << 4;
    }
    /* one bus-voltage sample every 256 ticks (75 Hz), only while not transmitting */
    bool want_vs = (++s_tick_div == 0u) && !phy.tx_active;
    ADC1->CHSELR = want_vs ? ADC_CHSELR_CHSEL6 : ADC_CHSELR_CHSEL5;
    while (!(ADC1->ISR & ADC_ISR_CCRDY)) {}
    ADC1->ISR = ADC_ISR_CCRDY;
    ADC1->CR |= ADC_CR_ADSTART;
}

/* hot-side link */
void USART1_IRQHandler(void)
{
    uint32_t isr = USART1->ISR;
    if (isr & USART_ISR_RXNE_RXFNE) phy_hot_byte(&phy, (uint8_t)USART1->RDR);
    if (isr & USART_ISR_RTOF) { USART1->ICR = USART_ICR_RTOCF; phy_hot_gap(&phy); }
    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) USART1->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
}
static void send_hot(const uint8_t *d, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) {
        while (!(USART1->ISR & USART_ISR_TXE_TXFNF)) {}
        USART1->TDR = d[i];                 /* 128 bytes at 115200 = 11 ms, blocking is fine here */
    }
}

static void init(void)
{
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY) | (1u << FLASH_ACR_LATENCY_Pos);
    RCC->CR &= ~RCC_CR_HSIDIV;                                 /* HSI48 / 1 */
    SystemCoreClock = SYSCLK;
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN;
    RCC->APBENR2 |= RCC_APBENR2_USART1EN | RCC_APBENR2_ADCEN | RCC_APBENR2_TIM14EN;
    (void)RCC->APBENR2;

    GPIOA->BRR = (1u << 4) | (1u << 0) | (1u << 1);           /* mark (24 V), LED off, RS-485 receive */
    gpio_mode(GPIOA, 4, 1u); gpio_mode(GPIOA, 0, 1u); gpio_mode(GPIOA, 1, 1u);
    gpio_mode(GPIOA, 5, 3u); gpio_mode(GPIOA, 6, 3u);
    gpio_af(GPIOB, 6, 0u); gpio_af(GPIOB, 7, 0u);              /* USART1 AF0 (VERIFY) */

    /* ADC: 12 bit, software start, 12.5-cycle sampling, calibrated */
    ADC1->CR |= ADC_CR_ADVREGEN;
    for (volatile int i = 0; i < 2000; i++) {}
    ADC1->CR |= ADC_CR_ADCAL;
    while (ADC1->CR & ADC_CR_ADCAL) {}
    ADC1->SMPR = 2u;                                           /* 12.5 cycles */
    ADC1->CFGR1 = 0;
    ADC1->CFGR2 = (1u << ADC_CFGR2_CKMODE_Pos);               /* PCLK/2 */
    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    while (!(ADC1->ISR & ADC_ISR_ADRDY)) {}
    ADC1->CHSELR = ADC_CHSELR_CHSEL5;
    while (!(ADC1->ISR & ADC_ISR_CCRDY)) {}
    ADC1->CR |= ADC_CR_ADSTART;

    /* USART1 115200 8N1, receiver timeout 3.5 chars */
    USART1->BRR = SYSCLK / 115200u;
    USART1->RTOR = 35u;
    USART1->CR2 = USART_CR2_RTOEN;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_RTOIE | USART_CR1_UE;
    NVIC_SetPriority(USART1_IRQn, 1);
    NVIC_EnableIRQ(USART1_IRQn);

    /* TIM14 19.2 kHz */
    TIM14->PSC = 0;
    TIM14->ARR = SYSCLK / PHY_FS - 1u;
    TIM14->DIER = TIM_DIER_UIE;
    TIM14->CR1 = TIM_CR1_CEN;
    NVIC_SetPriority(TIM14_IRQn, 0);
    NVIC_EnableIRQ(TIM14_IRQn);

    SysTick_Config(SYSCLK / 1000u);
    NVIC_SetPriority(SysTick_IRQn, 2);

    IWDG->KR = 0x5555u; IWDG->PR = 2u; IWDG->RLR = 500u; IWDG->KR = 0xCCCCu;   /* ~250 ms */
}

int main(void)
{
    phy_init(&phy);
    init();
    uint32_t last = 0, low_ms = 0, rx_seen = 0, dark_until = 0;
    for (;;) {
        uint32_t now = s_ms;
        phy_poll(&phy, now, send_hot);
        if (now == last) continue;
        last = now;
        phy.vbus_v = (float)s_vsns_raw * (3.3f / 4095.0f * 11.0f);
        /* LED: steady = bus up, 30 ms dark per received frame, 5 Hz blink = bus short / overload */
        low_ms = phy.vbus_v < 12.0f ? low_ms + 1u : 0u;
        if (phy.frames_rx != rx_seen) { rx_seen = phy.frames_rx; dark_until = now + 30u; }
        bool led = (low_ms > 500u) ? ((now / 100u) & 1u) != 0u : (int32_t)(now - dark_until) >= 0;
        if (led) GPIOA->BSRR = 1u; else GPIOA->BRR = 1u;
        IWDG->KR = 0xAAAAu;
    }
}
