/*
 * mains.h - Mains supervision from the two line-to-ground sense channels.
 *
 * Feed mains_sample() at a fixed rate (the 16 kHz PWM/ADC interrupt) with
 * VIN_L and VIN_N in volts (ADC x 301 divider). The difference is the line
 * voltage. Per full cycle the module produces rms, peak and frequency;
 * a fast peak detector flags over-voltage within ~0.2 ms so the relays can
 * be released in the same half cycle (440 VAC neutral loss), and missing
 * zero crossings flag an outage within one cycle.
 */
#ifndef MAINS_H
#define MAINS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float fs;            /* sample rate [Hz]                                 */
    float ov_peak;       /* instantaneous trip [V], e.g. 290 Vrms * 1.414    */
    float hyst;          /* zero-cross hysteresis [V]                        */
    /* running cycle */
    float acc2, pk_run;
    uint32_t n, n_since_zc, ov_n;
    int8_t sign;
    /* results (valid after the first full cycle) */
    float rms, peak, freq;
    bool  valid;         /* a full cycle has been measured recently          */
    bool  ov_fast;       /* latched until mains_clear_ov()                   */
    bool  outage;        /* no zero crossing for 1.5 nominal periods          */
    uint32_t cycles;
} mains_t;

void mains_init(mains_t *m, float fs, float ov_peak);
void mains_sample(mains_t *m, float v_l, float v_n);   /* ISR context      */
void mains_clear_ov(mains_t *m);

#endif
