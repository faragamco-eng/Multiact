/*
 * seg7.h - Text for the 4-digit 7-segment display of the basic panel
 * (hardware/panel: DS1 CC56-12, segments a..g,dp on IO4..IO9/IO15/IO16, digits on Q2..Q5).
 *
 *   "CO 7"  cool, speed 7      "FA10"  fan only, speed 10     "PU  "  pump only
 *   "Pr 2"  pre-wet, 2 min     "dr 4"  dry-out, 4 min         "E- 4"  alarm E4
 *   "----"  no drive           "UP45"  drive update 45 %      "    "  off
 */
#ifndef SEG7_H
#define SEG7_H
#include <stdint.h>
#include "panel.h"

#define SEG_A 0x01u
#define SEG_B 0x02u
#define SEG_C 0x04u
#define SEG_D 0x08u
#define SEG_E 0x10u
#define SEG_F 0x20u
#define SEG_G 0x40u
#define SEG_DP 0x80u

uint8_t seg7_char(char c);                                 /* segment bits of one character */
void    seg7_text(const ui_view_t *v, char out[5]);        /* 4 characters + NUL           */
/* LED bits for the 5th multiplex phase: a POWER, b PUMP, c FAN, d TIMER, e ALARM, f WIFI */
uint8_t seg7_leds(const ui_view_t *v);
#endif
