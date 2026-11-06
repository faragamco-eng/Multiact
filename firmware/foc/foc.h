/*
 * foc.h - Portable sensorless FOC core for the cooler motor drive.
 *
 * - dq current control (PI + decoupling + voltage-circle limit), SVPWM
 * - nonlinear flux observer (Ortega et al.) + PLL for angle and speed
 * - I/f start, flying start (catch a wind-milling fan, either direction)
 * - automatic identification: R, Ld, Lq, flux linkage, inertia
 * - speed loop with acceleration ramp and DC-bus (regeneration) limiting
 * - protections: over-current, locked rotor, start failure, phase loss,
 *   bus over/under voltage
 *
 * All quantities are SI, angles/speeds are ELECTRICAL (rad, rad/s). The
 * core never needs the pole-pair count; the application converts to rpm.
 *
 * Call foc_pwm_step() once per PWM period with the currents sampled in
 * that period; it returns the duties for the next period. Call
 * foc_tick_1ms() at 1 kHz (same or lower priority).
 */
#ifndef FOC_H
#define FOC_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float R;        /* phase resistance [ohm]                 */
    float Ld, Lq;   /* phase inductances [H]                  */
    float psi;      /* PM flux linkage, peak phase [V*s/rad]  */
    float Je;       /* "electrical" inertia J/p^2 [kg*m^2]    */
    float vdt;      /* dead-time voltage of the inverter [V] (self-calibrated) */
    bool  valid;    /* identification done                    */
} foc_motor_t;

typedef struct {
    float ts;            /* PWM period [s]                               */
    float v_deadtime;    /* dead-time voltage error to compensate [V]    */
    float i_max;         /* phase current limit, peak [A]                */
    float i_trip;        /* instantaneous trip, peak [A]                 */
    float i_start;       /* I/f start current, peak [A]                  */
    float w_max;         /* speed at 100 % [rad/s electrical]            */
    float accel;         /* ramp [rad/s^2 electrical], 0..w_max          */
    float w_handover;    /* I/f -> observer [rad/s]                      */
    float w_min_run;     /* below this in RUN -> locked rotor check      */
    float vbus_regen;    /* limit regeneration above this bus voltage    */
    float vbus_ov, vbus_uv;
    float vbus_droop;    /* below this: reduce power (weak/overloaded source) */
    float cur_bw;        /* current loop bandwidth [rad/s]               */
    float spd_bw;        /* speed loop bandwidth [rad/s]                 */
    float obs_gain;      /* observer convergence gain [rad/s]            */
    float pll_bw;        /* PLL natural frequency [rad/s]                */
} foc_cfg_t;

typedef enum {
    FOC_IDLE = 0, FOC_ID_R, FOC_ID_L, FOC_CATCH, FOC_START, FOC_RUN, FOC_ID_J, FOC_FAULT
} foc_state_t;

enum {
    FOC_F_OVERCURRENT = 1u << 0,
    FOC_F_LOCKED      = 1u << 1,   /* rotor blocked while running        */
    FOC_F_START       = 1u << 2,   /* rotor did not follow the I/f start */
    FOC_F_PHASE_LOSS  = 1u << 3,
    FOC_F_OVERVOLT    = 1u << 4,
    FOC_F_UNDERVOLT   = 1u << 5,
    FOC_F_HW          = 1u << 6,   /* IPM fault / break input            */
    FOC_F_ID          = 1u << 7,   /* identification result implausible  */
};

typedef struct {
    float i[3];          /* phase currents [A], positive into the motor  */
    bool  valid[3];      /* false when the low-side window was too short */
    float vbus;          /* [V]                                          */
} foc_meas_t;

typedef struct {
    float duty[3];       /* 0..1, high-side on-time fraction             */
    bool  enable;        /* false: all six switches off (coast)          */
} foc_out_t;

typedef struct {
    foc_cfg_t   cfg;
    foc_motor_t mot;
    foc_state_t state;
    uint32_t    faults;
    uint32_t    t_state;     /* PWM periods in this state */
    uint32_t    ms_state;

    /* commands */
    float  speed_cmd;        /* 0..1 of w_max, sign = direction */
    bool   run_req, id_req;

    /* measurements */
    float  ialpha, ibeta, id, iq, vbus;
    /* controller */
    float  theta, w;         /* angle / speed used by the controller   */
    float  id_ref, iq_ref, vd, vq, int_d, int_q;
    float  kp_d, ki_d, kp_q, ki_q;
    float  valpha, vbeta;    /* last voltage command (alpha/beta)      */
    float  va2, vb2;         /* command from two steps back = applied  */
    float  last_duty[3];
    /* observer + PLL */
    float  xa, xb;           /* flux state                             */
    float  th_obs, th_pll, w_pll;
    float  psi_obs;          /* |eta| (observer flux magnitude)        */
    /* I/f start */
    float  th_ol, w_ol, i_ol;
    /* speed loop */
    float  w_ref_ramp, int_s, kp_s, ki_s;
    /* identification */
    float  acc_a, acc_b, acc_n;
    float  id_r1_v, id_i1;
    int8_t inj_sign, inj2;
    float  j_w0, j_w1, j_iq0;
    uint32_t start_hold;
    uint32_t agree_n;
    float    w_obs_lp;
    float    fa, fb;
    bool     vsat, volt_limited;  /* modulator saturated / speed limited by bus voltage */
    uint32_t vsat_ms;
    uint32_t predicted_n;
    float    p_lim, p_lp;      /* power limit (0 = none) / motor power estimate [W] */
    bool     power_limited;    /* periods with predicted (unsampled) current */          /* leaky flux integrator (psi identification) */       /* filtered observer speed during I/f   */        /* periods observer agrees with I/f */     /* periods at I/f target without handover */
    bool   id_running;       /* doing the identification sequence */
    uint8_t psi_pass;
    float   settle_t, vq_lp, eq_lp;     /* R test: rotor settle detection */
    uint32_t quiet_n;        /* closed-loop flux refinement passes   */
    /* statistics / diagnostics */
    float  i_peak;
    uint32_t locked_ms;
} foc_t;

void foc_default_cfg(foc_cfg_t *c, float ts);
void foc_init(foc_t *f, const foc_cfg_t *cfg, const foc_motor_t *motor);
void foc_set_speed(foc_t *f, float frac);          /* -1..1 */
void foc_run(foc_t *f, bool on);
void foc_identify(foc_t *f);                       /* only from IDLE */
void foc_clear_faults(foc_t *f);
void foc_hw_fault(foc_t *f);
void foc_set_power_limit(foc_t *f, float watts);   /* 0 = no limit (battery mode) */

void foc_pwm_step(foc_t *f, const foc_meas_t *m, foc_out_t *o);
void foc_tick_1ms(foc_t *f);

const char *foc_state_name(foc_state_t s);

#endif /* FOC_H */
