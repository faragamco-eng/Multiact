#include "seg7.h"
#include <stdio.h>
#include <string.h>

uint8_t seg7_char(char c)
{
    static const uint8_t DIG[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    if (c >= '0' && c <= '9') return DIG[c - '0'];
    switch (c) {
    case 'A': return 0x77; case 'C': return 0x39; case 'd': return 0x5E; case 'E': return 0x79;
    case 'F': return 0x71; case 'O': return 0x3F; case 'P': return 0x73; case 'r': return 0x50;
    case 'U': return 0x3E; case 't': return 0x78; case 'n': return 0x54; case 'o': return 0x5C;
    case '-': return SEG_G; default: return 0;
    }
}

void seg7_text(const ui_view_t *v, char out[5])
{
    static const char *MODE[3] = { "CO", "FA", "PU" };
    if (v->update_pct)      snprintf(out, 5, "UP%2u", v->update_pct > 99u ? 99u : v->update_pct);
    else if (!v->comm_ok)   snprintf(out, 5, "----");
    else if (v->alarm)      snprintf(out, 5, "E-%2u", v->alarm % 100u);
    else if (v->seq == SEQ_PREWET) snprintf(out, 5, "Pr%2u", (v->seq_left_s + 59u) / 60u % 100u);
    else if (v->seq == SEQ_DRYOUT) snprintf(out, 5, "dr%2u", (v->seq_left_s + 59u) / 60u % 100u);
    else if (!v->power)     snprintf(out, 5, "    ");
    else if (v->mode == MODE_PUMP) snprintf(out, 5, "PU  ");
    else                    snprintf(out, 5, "%s%2u", MODE[v->mode], v->speed_step % 100u);
}

uint8_t seg7_leds(const ui_view_t *v)
{
    return (uint8_t)((v->led_power ? SEG_A : 0) | (v->led_pump ? SEG_B : 0) | (v->led_fan ? SEG_C : 0) |
                     (v->led_timer ? SEG_D : 0) | (v->led_alarm ? SEG_E : 0) | (v->led_wifi ? SEG_F : 0));
}
