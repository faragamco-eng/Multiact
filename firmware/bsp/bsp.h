/*
 * bsp.h - STM32G431CBT6 board support for the 310 V BLDC drive.
 * Pin map: see hardware/netlist.md (sheet MCU).
 */
#ifndef BSP_H
#define BSP_H

#include <stdbool.h>
#include <stdint.h>
#include "sixstep.h"

extern const ss_hal_t bsp_hal;

void     bsp_init(void);
uint32_t bsp_now_us(void);           /* TIM2, 1 MHz free running        */
void     bsp_delay_ms(uint32_t ms);  /* busy wait (init only)           */

/* latest raw readings (updated by the ADC ISR) */
void     bsp_read_meas(ss_meas_t *m);
uint16_t bsp_pot_raw(void);

void bsp_led_run(bool on);
void bsp_led_fault(bool on);
void bsp_relay(bool on);             /* inrush NTC bypass relay         */
bool bsp_button(void);               /* START/STOP, true while pressed  */

void bsp_uart_write(const char *s);
int  bsp_uart_getc(void);            /* -1 if empty                     */

void bsp_wdg_kick(void);

/* Interrupt priorities (lower number = higher priority). ADC and the
 * commutation timer share one level so they never preempt each other.  */
#define IRQPRI_BREAK   0u
#define IRQPRI_MOTOR   1u
#define IRQPRI_TICK    2u
#define IRQPRI_UART    3u

#endif /* BSP_H */
