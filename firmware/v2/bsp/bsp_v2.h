/*
 * bsp_v2.h - Board support for the v2 cooler drive (hardware/v2, sheet 5).
 *
 * Pin map (validated against ST pin data by hardware/v2/check_and_build.py)
 *   PA0  VBUS_SENSE  ADC1_IN1    1:149.9         PA8/9/10  TIM1_CH1..3  -> HIN U/V/W (active high)
 *   PA1  ISENSE_U    OPAMP1/COMP1                 PB13/14/15 TIM1_CH1N..3N -> LIN U/V/W (active high)
 *   PA7  ISENSE_V    OPAMP2/COMP2                 PB12 TIM1_BKIN  heatsink switch (high = open = trip)
 *   PB0  ISENSE_W    OPAMP3/COMP4                 PB6/PB7 USART1 -> ISO7762 -> PHY MCU (115200 8N1)
 *   PA2  VIN_L       ADC1_IN3    1:301           PB4 K1 main, PB5 K2 precharge, PB10 K3 pump, PB11 K4 drain
 *   PA3  VIN_N       ADC1_IN4    1:301           PA11 PFC_OFF (high = PFC off), PA12 LED
 *   PA5  not connected (was the series-capacitor mid-point sense)
 *   PA6  NTC_HS      ADC2_IN3    10k pull-up, NTC 10k B3435
 *   PA4  TP_DAC      DAC1_OUT1   debug signal
 */
#ifndef BSP_V2_H
#define BSP_V2_H

#include <stdbool.h>
#include <stdint.h>

#define SYSCLK_HZ    170000000u
#define PWM_HZ       16000u
#define PWM_ARR      (SYSCLK_HZ / (2u * PWM_HZ))      /* center aligned: 5312 */
#define DEADTIME_NS  1000u                            /* IRS2186 has none of its own */

typedef struct {
    float i[3];           /* phase currents [A], positive into the motor */
    bool  valid[3];       /* low-side window long enough for this phase  */
    float vbus, vin_l, vin_n, vcap_low, t_hs;
} bsp_meas_t;

void bsp_init(void);
uint32_t bsp_ms(void);

/* called by the ADC interrupt (16 kHz) - implemented in main_v2.c */
void bsp_pwm_isr(const bsp_meas_t *m);
/* duties for the next period; enable=false -> all six switches off */
void bsp_pwm_set(const float duty[3], bool enable);
void bsp_current_offset_calibrate(void);   /* outputs off, motor stopped */

bool bsp_brk_thermal(void);        /* live level of the heatsink switch input   */
bool bsp_brk_oc_latched(void);     /* comparator break since last clear          */
void bsp_brk_clear(void);
bool bsp_brk_active(void);         /* outputs were disabled by hardware          */

void bsp_relays(bool k1, bool k2, bool pump, bool drain);
void bsp_pfc(bool on);
void bsp_led(bool on);
void bsp_dac(float v);             /* TP_DAC debug output, volts                 */

/* USART1 Modbus link: bytes in, frame gap (receiver timeout) and transmit */
int  bsp_uart_getc(void);          /* -1 if empty */
bool bsp_uart_gap(void);           /* true once after >= 3.5 char silence        */
void bsp_uart_send(const uint8_t *p, uint16_t n);   /* non-blocking (copied)     */
bool bsp_uart_tx_busy(void);

void bsp_wdg_kick(void);
void bsp_reset(void);

/* flash (params + update staging) */
bool bsp_flash_erase(uint32_t addr);
bool bsp_flash_program(uint32_t addr, const void *src, uint32_t len);
const uint8_t *bsp_flash_ptr(uint32_t addr);

#define IRQPRI_BREAK 0u
#define IRQPRI_PWM   1u
#define IRQPRI_UART  2u
#define IRQPRI_TICK  3u

#endif
