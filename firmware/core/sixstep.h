/*
 * sixstep.h - Portable sensorless six-step (trapezoidal) BLDC control core.
 *
 * No hardware access: everything goes through ss_hal_t, so this file is
 * compiled unchanged for the STM32G431 target and for the host simulator
 * (firmware/sim).
 *
 * Call graph (target):
 *   ADC injected end-of-sequence ISR (PWM rate) -> ss_on_adc()
 *   TIM2 CC1 ISR (commutation timer)            -> ss_on_comm_timer()
 *   SysTick (1 kHz)                             -> ss_tick_1ms()
 *   TIM1 break ISR (IPM FO / break input)       -> ss_on_hw_fault()
 * ADC and TIM2 ISRs MUST have the same NVIC priority (no mutual preemption).
 */
#ifndef SIXSTEP_H
#define SIXSTEP_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    SS_IDLE = 0,
    SS_PRECHARGE,   /* all low sides on -> bootstrap caps charge        */
    SS_ALIGN,       /* DC vector, rotor parks at a known position       */
    SS_RAMP,        /* open-loop forced commutation, ZC detection armed */
    SS_RUN,         /* closed-loop sensorless commutation               */
    SS_FAULT
} ss_state_t;

enum {
    SS_F_OVERCURRENT = 1u << 0,
    SS_F_OVERVOLT    = 1u << 1,
    SS_F_UNDERVOLT   = 1u << 2,
    SS_F_OVERTEMP    = 1u << 3,
    SS_F_STALL       = 1u << 4,
    SS_F_STARTUP     = 1u << 5,
    SS_F_HW_TRIP     = 1u << 6,   /* IPM FO / TIM1 break                   */
};

/* Raw measurements, all in ADC counts (12 bit). */
typedef struct {
    uint16_t bemf[3];   /* physical phases U, V, W (same scale as vbus) */
    uint16_t vbus;
    uint16_t ibus;      /* DC-link current, sampled at ON center        */
    uint16_t ntc;
} ss_meas_t;

typedef struct {
    void (*outputs_off)(void);                /* all six switches off       */
    void (*low_sides_on)(void);               /* bootstrap pre-charge       */
    void (*apply_step)(uint8_t hi, uint8_t lo);/* physical phase 0..2, now  */
    void (*set_duty)(float duty);             /* 0..1, takes effect next update */
    void (*set_sample_on)(bool on_center);    /* ADC1 trigger ON/OFF center */
    void (*comm_arm)(uint32_t at_us);         /* one-shot at absolute time  */
    void (*comm_cancel)(void);
    void (*irq_lock)(void);                   /* may be NULL                */
    void (*irq_unlock)(void);
} ss_hal_t;

typedef struct {
    /* --- configuration / commands --- */
    const ss_hal_t *hal;
    float    speed_ref_rpm;     /* user setpoint                         */
    bool     reverse;

    /* --- state --- */
    ss_state_t state;
    uint32_t faults;
    uint32_t state_ms;          /* time in current state                 */
    uint8_t  retries;

    uint8_t  step;              /* logical step 0..5                     */
    uint8_t  map[3];            /* logical -> physical phase             */
    float    duty;
    bool     sample_on;
    uint8_t  mode_skip;         /* samples to ignore after mode change   */

    uint32_t t_comm;            /* time of last commutation (us)         */
    uint32_t t60;               /* step period estimate (us)             */
    uint32_t t_zc;              /* last zero crossing (us)               */
    bool     zc_prev_valid;     /* previous step had a ZC (for T60)      */
    bool     armed;             /* pre-crossing side seen this step       */
    bool     zc_found;
    uint16_t prev_v;
    uint32_t prev_t;
    uint8_t  misses;
    uint8_t  late_cnt;          /* consecutive unclamped post-side samples */
    uint16_t lock_count;

    /* ramp */
    float    ramp_f;
    uint32_t ramp_iv;           /* rotor step period in ramp (us), 0 = unknown */
    bool     last_sync;         /* previous commutation was rotor-synced */
    uint32_t ramp_hold_ms;

    /* speed loop */
    float    ref_ramped;
    float    speed_rpm;
    float    integ;

    /* filtered measurements (engineering units) */
    float    vbus_v;
    float    i_phase_a;         /* low-pass of ON-center bus current     */
    float    temp_c;
    uint16_t i_offset;          /* ADC counts at zero current            */

    /* statistics */
    uint32_t zc_count;
    uint32_t forced_count;
    uint32_t late_zc;           /* ZC hidden by demag, commutated on demag end */
    float    advance_deg;       /* last applied timing advance          */
    uint32_t last_meas_vbus;
} ss_t;

void  ss_init(ss_t *m, const ss_hal_t *hal);
void  ss_calibrate_offset(ss_t *m, uint16_t ibus_raw_at_zero);
bool  ss_start(ss_t *m);                  /* false if faulted / no bus   */
void  ss_stop(ss_t *m);
void  ss_clear_faults(ss_t *m);
void  ss_set_speed(ss_t *m, float rpm);
void  ss_set_reverse(ss_t *m, bool rev);  /* only applied while stopped  */

void  ss_on_adc(ss_t *m, const ss_meas_t *meas, uint32_t now_us);
void  ss_on_comm_timer(ss_t *m, uint32_t now_us);
void  ss_tick_1ms(ss_t *m, uint32_t now_us);
void  ss_on_hw_fault(ss_t *m);
/* Slow path (call ~10 Hz): NTC -> deg C, over-temperature trip. */
float ss_update_temperature(ss_t *m, uint16_t ntc_raw);

const char *ss_state_name(ss_state_t s);

#endif /* SIXSTEP_H */
