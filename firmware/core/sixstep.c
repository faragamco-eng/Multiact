/*
 * sixstep.c - Sensorless six-step BLDC control core (portable C99).
 *
 * Commutation scheme: H_PWM-L_ON with synchronous rectification.
 *   - "high" phase: complementary PWM (high IGBT PWM, low IGBT in the gaps)
 *   - "low"  phase: low IGBT continuously ON
 *   - floating phase: both IGBTs OFF, its terminal voltage is measured.
 *
 * Zero-crossing (ZC) of the floating phase back-EMF:
 *   ON-center sample : V_float = e_float + Vbus/2  -> threshold = Vbus/2
 *   OFF-center sample: V_float = e_float           -> threshold ~ 0 V
 * The sampling mode is chosen from the duty cycle (short ON windows at low
 * duty cannot settle the BEMF filter, short OFF windows at high duty).
 *
 * Demagnetisation after commutation clamps the new floating phase to one
 * rail, always on the *post*-crossing side. The detector therefore requires
 * a blanking time AND one sample on the *pre*-crossing side before a
 * crossing is accepted ("armed"), which rejects the demag pulse.
 *
 * Commutation happens 30 deg electrical (T60/2) after the ZC minus the
 * configured advance. T60 = ZC-to-ZC time, low-pass filtered.
 */
#include "sixstep.h"
#include "app_config.h"
#include <math.h>
#include <stddef.h>

/* Logical step table: phase driven high (PWM), driven low, floating.
 * BEMF of the floating phase falls on even steps and rises on odd steps. */
static const uint8_t HI[6] = {0, 0, 1, 1, 2, 2};
static const uint8_t LO[6] = {1, 2, 2, 0, 0, 1};
static const uint8_t FL[6] = {2, 1, 0, 2, 1, 0};

#define CLAMPF(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

static void lock(ss_t *m)   { if (m->hal->irq_lock)   m->hal->irq_lock(); }
static void unlock(ss_t *m) { if (m->hal->irq_unlock) m->hal->irq_unlock(); }

const char *ss_state_name(ss_state_t s)
{
    static const char *const n[] = {"IDLE", "PRECHARGE", "ALIGN", "RAMP", "RUN", "FAULT"};
    return (unsigned)s < sizeof n / sizeof n[0] ? n[s] : "?";
}

static void set_state(ss_t *m, ss_state_t s)
{
    m->state = s;
    m->state_ms = 0;
}

static void fault(ss_t *m, uint32_t f)
{
    m->hal->comm_cancel();
    m->hal->outputs_off();
    m->faults |= f;
    m->duty = 0.0f;
    set_state(m, SS_FAULT);
}

static void apply_duty(ss_t *m, float d)
{
    m->duty = CLAMPF(d, 0.0f, DUTY_MAX);
    m->hal->set_duty(m->duty);

    /* choose BEMF sampling point with hysteresis */
    bool want_on = m->sample_on ? (m->duty > DUTY_OFF_SAMPLE) : (m->duty >= DUTY_ON_SAMPLE);
    if (want_on != m->sample_on) {
        m->sample_on = want_on;
        m->hal->set_sample_on(want_on);
        m->mode_skip = 2;           /* trigger change lands on next update */
    }
}

static void commutate(ss_t *m, uint32_t now)
{
    /* ramp: rotor step period = interval between two consecutive
     * rotor-synchronised commutations (real ZC or rotor-ahead resync) */
    if (m->state == SS_RAMP) {
        if (m->zc_found && m->last_sync) {
            uint32_t iv = now - m->t_comm;
            m->ramp_iv = m->ramp_iv ? (3u * m->ramp_iv + iv + 2u) / 4u : iv;
        } else if (!m->zc_found) {
            m->ramp_iv = 0;
        }
        m->last_sync = m->zc_found;
    }
    if (!m->zc_found) {
        m->zc_prev_valid = false;
        m->lock_count = 0;          /* forced (unsynchronised) step */
    }
    m->step = (uint8_t)((m->step + 1u) % 6u);
    m->hal->apply_step(m->map[HI[m->step]], m->map[LO[m->step]]);
    m->t_comm = now;
    m->armed = false;
    m->zc_found = false;
    m->late_cnt = 0;
    m->prev_t = 0;
}

static void schedule_comm_p(ss_t *m, uint32_t t_zc, uint32_t now, uint32_t period)
{
    /* timing advance grows with current: compensates the demagnetisation
     * time and the phase current lag of the winding inductance */
    float adv = COMM_ADVANCE_DEG + COMM_ADV_DEG_PER_A * (m->i_phase_a > 0.0f ? m->i_phase_a : 0.0f);
    if (adv > COMM_ADVANCE_MAX_DEG) adv = COMM_ADVANCE_MAX_DEG;
    m->advance_deg = adv;
    float delay = (float)period * (30.0f - adv) / 60.0f - (float)COMM_DELAY_COMP_US;
    if (delay < 0.0f)
        delay = 0.0f;
    uint32_t at = t_zc + (uint32_t)delay;
    if ((int32_t)(at - now) <= 2)
        commutate(m, now);
    else
        m->hal->comm_arm(at);
}

static void schedule_comm(ss_t *m, uint32_t t_zc, uint32_t now)
{
    schedule_comm_p(m, t_zc, now, m->t60);
}

static void on_zc(ss_t *m, uint32_t t_zc, uint32_t now)
{
    m->zc_found = true;
    m->zc_count++;

    if (m->state == SS_RAMP) {
        /* lock = consecutive ZCs whose ZC-to-ZC interval is consistent
         * with the previous one (smooth, synchronous rotation). Intervals
         * are measured against the rotor, not the forced period, because
         * above RAMP_REG_MIN_HZ the commutation is already ZC-synced. */
        bool ok = true;
        if (m->zc_prev_valid && m->ramp_iv) {
            float r = (float)(t_zc - m->t_zc) / (float)m->ramp_iv;
            ok = r > 0.75f && r < 1.33f;
        }
        if (ok)
            m->lock_count++;
        else
            m->lock_count = 0;
        m->t_zc = t_zc;
        m->zc_prev_valid = true;

        /* Re-synchronise this commutation to the rotor: ZC + 30 deg instead
         * of the forced instant. A lagging rotor makes the field wait
         * (no pull-out), a leading one gets an earlier step. The forced
         * timer remains the fallback for steps without a ZC. */
        uint32_t period = m->ramp_iv ? m->ramp_iv : m->t60;
        if (m->ramp_f >= RAMP_REG_MIN_HZ) {
            m->hal->comm_cancel();
            schedule_comm_p(m, t_zc, now, period);
        }

        if (m->lock_count >= RAMP_LOCK_STEPS && m->ramp_iv &&
            m->ramp_f >= RAMP_HANDOVER_MIN_HZ) {
            m->t60 = m->ramp_iv;        /* rotor's measured step period */
            m->misses = 0;
            m->integ = m->duty;
            m->speed_rpm = 60.0e6f / (6.0f * (float)m->t60 * MOTOR_POLE_PAIRS);
            m->ref_ramped = m->speed_rpm;
            m->retries = 0;
            set_state(m, SS_RUN);
            if (m->ramp_f < RAMP_REG_MIN_HZ) {
                m->hal->comm_cancel();
                schedule_comm(m, t_zc, now);
            }
        }
        return;
    }

    /* SS_RUN */
    if (m->zc_prev_valid) {
        uint32_t meas = t_zc - m->t_zc;
        /* reject implausible intervals (noise / missed step) */
        if (meas > m->t60 / 2u && meas < m->t60 * 2u)
            m->t60 = (3u * m->t60 + meas + 2u) / 4u;
    }
    m->t_zc = t_zc;
    m->zc_prev_valid = true;
    m->misses = 0;
    schedule_comm(m, t_zc, now);
}

/* ------------------------------------------------------------------ */

void ss_init(ss_t *m, const ss_hal_t *hal)
{
    *m = (ss_t){0};
    m->hal = hal;
    m->map[0] = 0; m->map[1] = 1; m->map[2] = 2;
    m->i_offset = 0;
    m->temp_c = 25.0f;
    m->t60 = 1000000u;
    hal->outputs_off();
    hal->set_sample_on(false);
    set_state(m, SS_IDLE);
}

void ss_calibrate_offset(ss_t *m, uint16_t raw) { m->i_offset = raw; }

void ss_set_speed(ss_t *m, float rpm)
{
    m->speed_ref_rpm = CLAMPF(rpm, 0.0f, MOTOR_MAX_RPM);
}

void ss_set_reverse(ss_t *m, bool rev)
{
    if (m->state == SS_IDLE || m->state == SS_FAULT)
        m->reverse = rev;
}

bool ss_start(ss_t *m)
{
    if (m->state != SS_IDLE || m->faults)
        return false;
    if (m->vbus_v < VBUS_START_MIN_V)
        return false;
    lock(m);
    /* reverse = swap physical V and W */
    m->map[0] = 0;
    m->map[1] = m->reverse ? 2 : 1;
    m->map[2] = m->reverse ? 1 : 2;
    m->duty = 0.0f;
    m->hal->set_duty(0.0f);
    m->sample_on = false;
    m->hal->set_sample_on(false);
    m->zc_count = m->forced_count = 0;
    m->hal->low_sides_on();
    set_state(m, SS_PRECHARGE);
    unlock(m);
    return true;
}

void ss_stop(ss_t *m)
{
    lock(m);
    m->hal->comm_cancel();
    m->hal->outputs_off();
    m->duty = 0.0f;
    m->retries = 0;
    if (m->state != SS_FAULT)
        set_state(m, SS_IDLE);
    unlock(m);
}

void ss_clear_faults(ss_t *m)
{
    lock(m);
    if (m->state == SS_FAULT) {
        m->faults = 0;
        m->retries = 0;
        set_state(m, SS_IDLE);
    }
    unlock(m);
}

void ss_on_hw_fault(ss_t *m)
{
    fault(m, SS_F_HW_TRIP);
}

/* ------------------------------------------------------------------ */
/* PWM-rate entry point (ADC injected end of sequence)                 */
/* ------------------------------------------------------------------ */
void ss_on_adc(ss_t *m, const ss_meas_t *me, uint32_t now)
{
    const float vbus = (float)me->vbus * VBUS_V_PER_COUNT;
    m->vbus_v += 0.05f * (vbus - m->vbus_v);
    m->last_meas_vbus = me->vbus;

    if (m->state == SS_IDLE) {
        /* outputs off: track current-sense offset */
        m->i_offset = (uint16_t)((15u * m->i_offset + me->ibus + 8u) / 16u);
        m->i_phase_a = 0.0f;
        return;
    }
    if (m->state == SS_FAULT)
        return;

    const float i = (float)((int32_t)me->ibus - (int32_t)m->i_offset) * I_A_PER_COUNT;
    if (i > I_TRIP_SW_A) { fault(m, SS_F_OVERCURRENT); return; }
    if (vbus > VBUS_OV_V) { fault(m, SS_F_OVERVOLT);   return; }
    m->i_phase_a += 0.02f * (i - m->i_phase_a);

    if (m->state != SS_RAMP && m->state != SS_RUN)
        return;

    uint32_t since = now - m->t_comm;

    /* missed ZC in closed loop: force a commutation, count it */
    if (m->state == SS_RUN && !m->zc_found &&
        (float)since > ZC_TIMEOUT_FRAC * (float)m->t60) {
        m->forced_count++;
        if (++m->misses >= STALL_MISSES) { fault(m, SS_F_STALL); return; }
        commutate(m, now);
        return;
    }

    if (m->zc_found)
        return;
    if (m->mode_skip) {
        m->mode_skip--;
        m->prev_t = 0;              /* no interpolation across a mode change */
        return;
    }

    float blank = ZC_BLANK_FRAC * (float)m->t60;
    if (blank < (float)ZC_BLANK_MIN_US)
        blank = (float)ZC_BLANK_MIN_US;
    if ((float)since < blank)
        return;

    const bool rising = (m->step & 1u) != 0;
    const int32_t v = me->bemf[m->map[FL[m->step]]];
    const int32_t thr = m->sample_on ? (int32_t)me->vbus / 2 : (int32_t)ZC_OFF_THR_COUNTS;
    const int32_t h = (int32_t)ZC_HYST_COUNTS;
    const bool pre  = rising ? (v < thr - h) : (v > thr + h);
    const bool post = rising ? (v >= thr)    : (v <= thr);

    if (!m->armed) {
        if (pre) {
            m->armed = true;
            m->prev_v = (uint16_t)v;
            m->prev_t = now;
        } else if (post && (m->sample_on || rising) &&
                   (m->state == SS_RUN || m->ramp_f >= RAMP_REG_MIN_HZ)) {
            /* Post side without seeing the pre side. If the terminal is
             * pinned to a rail it is still demagnetising; if not, the
             * demag interval hid the real ZC (heavy load) -> it already
             * happened: commutate now rather than waiting for a timeout.
             * In OFF sampling only rising steps can be told apart (demag
             * pins them to Vbus, the real BEMF is far below it). */
            bool clamped = rising ? (v > (int32_t)me->vbus - DEMAG_CLAMP_COUNTS)
                                  : (v < DEMAG_CLAMP_COUNTS);
            /* two consecutive samples: right after demag ends the BEMF RC
             * filter is still rising from the rail and looks "unclamped" */
            if (clamped)
                m->late_cnt = 0;
            else if (++m->late_cnt >= 2) {
                m->late_zc++;
                m->zc_found = true;
                m->zc_prev_valid = false;   /* unknown ZC time: no T60 update */
                m->misses = 0;
                if (m->state == SS_RAMP) {
                    /* rotor ahead of the forced field: step now */
                    m->hal->comm_cancel();
                    m->lock_count++;
                    commutate(m, now);
                    m->hal->comm_arm(now + (m->ramp_iv ? m->ramp_iv : m->t60));
                } else {
                    commutate(m, now);
                }
            }
        }
        return;
    }
    if (!post) {
        m->prev_v = (uint16_t)v;
        m->prev_t = now;
        return;
    }

    /* crossing between previous and this sample: linear interpolation */
    uint32_t t_zc = now;
    if (m->prev_t != 0) {
        int32_t a = (int32_t)m->prev_v - thr;
        int32_t b = (int32_t)m->prev_v - v;
        if (b != 0) {
            float frac = CLAMPF((float)a / (float)b, 0.0f, 1.0f);
            t_zc = m->prev_t + (uint32_t)(frac * (float)(now - m->prev_t));
        }
    }
    on_zc(m, t_zc, now);
}

/* ------------------------------------------------------------------ */
/* Commutation timer                                                   */
/* ------------------------------------------------------------------ */
void ss_on_comm_timer(ss_t *m, uint32_t now)
{
    if (m->state == SS_RUN) {
        commutate(m, now);
    } else if (m->state == SS_RAMP) {
        commutate(m, now);
        /* advance the open-loop frequency profile by one step period */
        if (m->ramp_f < RAMP_F1_HZ) {
            m->ramp_f += RAMP_ACC_HZ_S * ((float)m->t60 * 1e-6f);
            if (m->ramp_f > RAMP_F1_HZ)
                m->ramp_f = RAMP_F1_HZ;
        }
        m->t60 = (uint32_t)(1.0e6f / (6.0f * m->ramp_f));
        m->hal->comm_arm(now + m->t60);
    }
}

/* ------------------------------------------------------------------ */
/* 1 kHz: sequencing, slow protections, speed loop                     */
/* ------------------------------------------------------------------ */
static float ntc_to_c(uint16_t raw)
{
    float v = (float)raw * (ADC_VREF / ADC_FS);
    if (v < 0.01f) v = 0.01f;
    if (v > ADC_VREF - 0.01f) v = ADC_VREF - 0.01f;
    float r = NTC_RPULLUP * v / (ADC_VREF - v);
    return 1.0f / (1.0f / 298.15f + logf(r / NTC_R25) / NTC_BETA) - 273.15f;
}

/* Start-up current control (align and ramp): duty = BEMF feed-forward +
 * integrator on the ON-center phase current. Makes the start torque
 * independent of winding resistance, Ke and bus voltage. */
static void start_current_ctrl(ss_t *m, float i_ref, float d_ff)
{
    m->integ += START_I_KI * (i_ref - m->i_phase_a);
    m->integ = CLAMPF(m->integ, 0.0f, START_DUTY_MAX);
    apply_duty(m, CLAMPF(d_ff + m->integ, 0.0f, START_DUTY_MAX));
}

static void startup_failed(ss_t *m)
{
    m->hal->comm_cancel();
    m->hal->outputs_off();
    m->duty = 0.0f;
    if (++m->retries > START_RETRIES) {
        fault(m, SS_F_STARTUP);
    } else {
        /* let the rotor coast to rest, then retry (handled in IDLE) */
        set_state(m, SS_IDLE);
        m->state_ms = 0;
    }
}

void ss_tick_1ms(ss_t *m, uint32_t now)
{
    m->state_ms++;

    if (m->state == SS_IDLE) {
        if (m->retries && m->state_ms >= 1500u) {
            uint8_t r = m->retries;
            ss_start(m);
            m->retries = r;
        }
        return;
    }
    if (m->state == SS_FAULT)
        return;

    if (m->vbus_v < VBUS_UV_V) { fault(m, SS_F_UNDERVOLT); return; }

    switch (m->state) {
    case SS_PRECHARGE:
        if (m->state_ms >= PRECHARGE_MS) {
            lock(m);
            m->step = 4;
            m->hal->apply_step(m->map[HI[4]], m->map[LO[4]]);
            apply_duty(m, 0.0f);
            m->integ = 0.0f;
            set_state(m, SS_ALIGN);
            unlock(m);
        }
        break;

    case SS_ALIGN: {
        /* Two-stage alignment: first to step 4, then to step 5. A single
         * vector has a dead point when the rotor sits 180 deg away (zero
         * torque); the second vector is 60 deg off that point. */
        float k = (float)m->state_ms / (float)(ALIGN_MS / 4u);
        if (k > 1.0f) k = 1.0f;
        if (m->state_ms == ALIGN_MS / 3u) {
            m->step = 5;
            m->hal->apply_step(m->map[HI[5]], m->map[LO[5]]);
        }
        start_current_ctrl(m, k * ALIGN_I_A, 0.0f);
        if (m->state_ms >= ALIGN_MS) {
            lock(m);
            m->ramp_f = RAMP_F0_HZ;
            m->t60 = (uint32_t)(1.0e6f / (6.0f * m->ramp_f));
            m->lock_count = 0;
            m->ramp_iv = 0;
            m->last_sync = false;
            m->zc_found = true;      /* step 5 had no ZC: don't measure T60 */
            m->zc_prev_valid = false;
            set_state(m, SS_RAMP);
            commutate(m, now);       /* -> step 0, 60 deg ahead of rotor */
            m->hal->comm_arm(now + m->t60);
            unlock(m);
        }
        break;
    }

    case SS_RAMP:
        start_current_ctrl(m, RAMP_I_A, RAMP_V_PER_HZ * m->ramp_f / m->vbus_v);
        if (m->ramp_f >= RAMP_F1_HZ && m->state_ms > 10u) {
            if (++m->ramp_hold_ms > RAMP_HOLD_MS) {
                m->ramp_hold_ms = 0;
                startup_failed(m);
            }
        } else {
            m->ramp_hold_ms = 0;
        }
        break;

    case SS_RUN: {
        /* measured speed from T60 */
        float rpm = 60.0e6f / (6.0f * (float)m->t60 * MOTOR_POLE_PAIRS);
        m->speed_rpm += 0.2f * (rpm - m->speed_rpm);

        /* reference slew, min speed for sensorless operation */
        float ref = m->speed_ref_rpm;
        if (ref < MOTOR_MIN_RPM) ref = MOTOR_MIN_RPM;
        float step = SPEED_SLEW_RPM_S / (float)SPEED_LOOP_HZ;
        if (m->ref_ramped < ref - step)      m->ref_ramped += step;
        else if (m->ref_ramped > ref + step) m->ref_ramped -= step;
        else                                 m->ref_ramped = ref;

        float err = m->ref_ramped - m->speed_rpm;

        /* regenerative braking pumps the bus: don't decelerate harder */
        if (err < 0.0f && m->vbus_v > VBUS_OV_V - 25.0f)
            err = 0.0f;

        float excess = m->i_phase_a - I_LIMIT_RUN_A;
        if (excess > 0.0f) {
            m->integ -= ILIM_KI * excess;
            if (err > 0.0f) err = 0.0f;
        } else {
            m->integ += SPEED_KI * err / (float)SPEED_LOOP_HZ;
        }
        /* per-unit voltage feed-forward bound: duty never far below BEMF */
        m->integ = CLAMPF(m->integ, DUTY_MIN_RUN, DUTY_MAX);
        apply_duty(m, CLAMPF(m->integ + SPEED_KP * err, DUTY_MIN_RUN, DUTY_MAX));

        /* user stop request handled by the application (ss_stop) */
        break;
    }
    default:
        break;
    }
}

/* Temperature is evaluated by the application at a slow rate to keep
 * logf() out of the fast paths. */
float ss_update_temperature(ss_t *m, uint16_t ntc_raw)
{
    m->temp_c = ntc_to_c(ntc_raw);
    if (m->state != SS_IDLE && m->state != SS_FAULT && m->temp_c > T_TRIP_C)
        fault(m, SS_F_OVERTEMP);
    return m->temp_c;
}
