/*
 * foc.c - Sensorless FOC core (see foc.h).
 */
#include "foc.h"
#include <math.h>
#include <stddef.h>

#define SQRT3      1.7320508f
#define INV_SQRT3  0.5773503f
#define TWO_PI     6.2831853f
#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))

/* identification timing (seconds) and levels */
#define ID_R_SETTLE_S   2.0f      /* rotor alignment settles (heavy blower) */
#define ID_R_PHASE_S    0.40f
#define ID_DAMP_A_PER_V 0.4f      /* alignment damping gain              */
#define ID_L_PHASE_S    0.30f
#define ID_VH_FRAC      0.06f     /* injection amplitude, fraction of Vbus */
#define ID_PSI_HOLD_S   1.0f
#define ID_RUN_FRAC     0.40f     /* speed of the closed-loop ID steps   */
#define ID_J_SETTLE_S   2.0f
#define ID_J_STEP_S     0.15f

const char *foc_state_name(foc_state_t s)
{
    static const char *const n[] = {"IDLE", "ID_R", "ID_L", "CATCH", "START", "RUN", "ID_J", "FAULT"};
    return (unsigned)s < sizeof n / sizeof n[0] ? n[s] : "?";
}

static float wrap(float a)
{
    while (a > 3.14159265f)  a -= TWO_PI;
    while (a < -3.14159265f) a += TWO_PI;
    return a;
}

void foc_default_cfg(foc_cfg_t *c, float ts)
{
    *c = (foc_cfg_t){0};
    c->ts = ts;
    c->v_deadtime = 0.0f;
    c->i_max = 8.0f;
    c->i_trip = 14.0f;
    c->i_start = 3.0f;
    c->w_max = TWO_PI * 150.0f;          /* 450 rpm x 20 pole pairs */
    c->accel = TWO_PI * 150.0f / 20.0f;  /* 0..100 % in 20 s */
    c->w_handover = TWO_PI * 15.0f;
    c->w_min_run = TWO_PI * 6.0f;
    c->vbus_regen = 420.0f;
    c->vbus_ov = 445.0f;
    c->vbus_uv = 200.0f;
    c->cur_bw = TWO_PI * 600.0f;
    c->spd_bw = TWO_PI * 2.0f;
    c->obs_gain = 1500.0f;
    c->pll_bw = TWO_PI * 40.0f;
}

static void set_state(foc_t *f, foc_state_t s)
{
    f->state = s;
    f->t_state = 0;
    f->ms_state = 0;
    f->acc_a = f->acc_b = f->acc_n = 0.0f;
}

static void update_gains(foc_t *f)
{
    const foc_motor_t *m = &f->mot;
    f->kp_d = m->Ld * f->cfg.cur_bw;  f->ki_d = m->R * f->cfg.cur_bw;
    f->kp_q = m->Lq * f->cfg.cur_bw;  f->ki_q = m->R * f->cfg.cur_bw;
    float G = 1.5f * m->psi / m->Je;      /* (rad/s^2) per A, electrical */
    f->kp_s = f->cfg.spd_bw / G;
    f->ki_s = f->kp_s * f->cfg.spd_bw / 5.0f;
}

static void reset_ctrl(foc_t *f)
{
    f->int_d = f->int_q = f->int_s = 0.0f;
    f->id_ref = f->iq_ref = 0.0f;
    f->vd = f->vq = f->valpha = f->vbeta = 0.0f;
    f->xa = f->xb = 0.0f;
    f->va2 = f->vb2 = 0.0f;
    f->inj_sign = f->inj2 = 0;
    f->w_pll = f->th_pll = f->th_obs = 0.0f;
    f->locked_ms = 0;
}

void foc_init(foc_t *f, const foc_cfg_t *cfg, const foc_motor_t *motor)
{
    *f = (foc_t){0};
    f->cfg = *cfg;
    f->mot = *motor;
    if (!(f->mot.vdt > 0.0f)) f->mot.vdt = cfg->v_deadtime;   /* first guess */
    update_gains(f);
    reset_ctrl(f);
    set_state(f, FOC_IDLE);
}

void foc_set_speed(foc_t *f, float frac) { f->speed_cmd = CLAMP(frac, -1.0f, 1.0f); }

static void fault(foc_t *f, uint32_t code)
{
    f->faults |= code;
    f->id_running = false;
    set_state(f, FOC_FAULT);
}

void foc_hw_fault(foc_t *f) { fault(f, FOC_F_HW); }

void foc_clear_faults(foc_t *f)
{
    if (f->state == FOC_FAULT) { f->faults = 0; set_state(f, FOC_IDLE); }
}

void foc_identify(foc_t *f)
{
    if (f->state != FOC_IDLE) return;
    f->id_req = true;
}

void foc_run(foc_t *f, bool on) { f->run_req = on; }

/* ------------------------------------------------------------------ */
/* building blocks                                                     */
/* ------------------------------------------------------------------ */
static void observer(foc_t *f, float va, float vb, float ia, float ib)
{
    /* Nonlinear flux observer on the ACTIVE FLUX (valid for salient/IPM
     * motors): eta = x - Lq*i lies on the d axis with magnitude
     * psi_af = psi + (Ld - Lq)*id. The constraint |eta| = psi_af pulls the
     * flux estimate onto that circle; id is taken from the current angle. */
    const foc_motor_t *m = &f->mot;
    float ea = f->xa - m->Lq * ia, eb = f->xb - m->Lq * ib;
    float mag2 = ea * ea + eb * eb;
    float inv = mag2 > 1e-12f ? 1.0f / sqrtf(mag2) : 0.0f;
    float id_est = (ia * ea + ib * eb) * inv;
    float paf = m->psi + (m->Ld - m->Lq) * id_est;
    if (paf < 0.2f * m->psi) paf = 0.2f * m->psi;
    float corr = 0.5f * f->cfg.obs_gain * (1.0f - mag2 / (paf * paf));
    f->xa += f->cfg.ts * (va - m->R * ia + corr * ea);
    f->xb += f->cfg.ts * (vb - m->R * ib + corr * eb);
    ea = f->xa - m->Lq * ia; eb = f->xb - m->Lq * ib;
    f->th_obs = atan2f(eb, ea);
    f->psi_obs = sqrtf(ea * ea + eb * eb) - (m->Ld - m->Lq) * id_est;   /* ~psi */

    /* PLL: speed estimate and filtered angle */
    float wn = f->cfg.pll_bw;
    float err = wrap(f->th_obs - f->th_pll);
    f->w_pll += wn * wn * err * f->cfg.ts;
    f->th_pll = wrap(f->th_pll + (f->w_pll + 1.4f * wn * err) * f->cfg.ts);
}

/* dq current control in frame 'th'; bemf_ff enables the flux feed-forward */
static void current_loop(foc_t *f, float th, float w, bool bemf_ff)
{
    const foc_motor_t *m = &f->mot;
    float c = cosf(th), s = sinf(th);
    f->id = f->ialpha * c + f->ibeta * s;
    f->iq = -f->ialpha * s + f->ibeta * c;

    float ed = f->id_ref - f->id, eq = f->iq_ref - f->iq;
    float vd_ff = -w * m->Lq * f->iq;
    float vq_ff = w * m->Ld * f->id + (bemf_ff ? w * m->psi : 0.0f);
    float vd = f->kp_d * ed + f->int_d + vd_ff;
    float vq = f->kp_q * eq + f->int_q + vq_ff;

    float vmax = 0.95f * f->vbus * INV_SQRT3;
    bool sat_d = false, sat_q = false;
    if (vd > vmax)  { vd = vmax;  sat_d = true; }
    if (vd < -vmax) { vd = -vmax; sat_d = true; }
    float vqmax = sqrtf(vmax * vmax - vd * vd);
    if (vq > vqmax)  { vq = vqmax;  sat_q = true; }
    if (vq < -vqmax) { vq = -vqmax; sat_q = true; }
    /* conditional integration (anti-windup) */
    if (!sat_d || (ed * f->int_d < 0.0f)) f->int_d += f->ki_d * f->cfg.ts * ed;
    if (!sat_q || (eq * f->int_q < 0.0f)) f->int_q += f->ki_q * f->cfg.ts * eq;
    f->vd = vd;
    f->vq = vq;
    f->vsat = sat_d || sat_q;
}

static void modulate(foc_t *f, float th_apply, float vd, float vq, foc_out_t *o, const float i[3])
{
    float c = cosf(th_apply), s = sinf(th_apply);
    float va = vd * c - vq * s, vb = vd * s + vq * c;
    f->valpha = va; f->vbeta = vb;
    float ph[3] = { va, -0.5f * va + 0.5f * SQRT3 * vb, -0.5f * va - 0.5f * SQRT3 * vb };
    /* dead-time compensation; linear inside half the PWM current ripple
     * (there the ripple crosses zero and the error is only partial) */
    float Lm = 0.5f * (f->mot.Ld + f->mot.Lq);
    float band = 0.5f * f->vbus * 0.25f * f->cfg.ts / Lm;
    if (band < 0.05f) band = 0.05f;
    for (int k = 0; k < 3; k++)
        ph[k] += f->mot.vdt * CLAMP(i[k] / band, -1.0f, 1.0f);
    float mx = fmaxf(ph[0], fmaxf(ph[1], ph[2])), mn = fminf(ph[0], fminf(ph[1], ph[2]));
    float mid = 0.5f * (mx + mn);
    for (int k = 0; k < 3; k++) {
        float d = 0.5f + (ph[k] - mid) / f->vbus;
        o->duty[k] = CLAMP(d, 0.02f, 0.98f);
        f->last_duty[k] = o->duty[k];
    }
    o->enable = true;
}

/* ------------------------------------------------------------------ */
/* PWM-rate step                                                        */
/* ------------------------------------------------------------------ */
void foc_pwm_step(foc_t *f, const foc_meas_t *me, foc_out_t *o)
{
    const float ts = f->cfg.ts;
    f->vbus = me->vbus;
    f->t_state++;

    /* 3-shunt reconstruction: drop the phase with the highest duty
     * (shortest low-side window) or any invalid sample */
    int skip = 0, nvalid = 0;
    for (int k = 1; k < 3; k++) if (f->last_duty[k] > f->last_duty[skip]) skip = k;
    for (int k = 0; k < 3; k++) { if (!me->valid[k]) skip = k; else nvalid++; }
    float i[3] = { me->i[0], me->i[1], me->i[2] };
    i[skip] = -(i[(skip + 1) % 3] + i[(skip + 2) % 3]);
    if (nvalid < 2) {
        /* near full modulation two low-side windows can be too short to
         * sample: predict the current by rotating the last vector */
        float dth = f->w_pll * ts, c = cosf(dth), sn = sinf(dth);
        float pa = f->ialpha * c - f->ibeta * sn, pb = f->ialpha * sn + f->ibeta * c;
        i[0] = pa;
        i[1] = -0.5f * pa + 0.8660254f * pb;
        i[2] = -0.5f * pa - 0.8660254f * pb;
        f->predicted_n++;
    }

    /* pipeline: the command computed in step k is applied during period
     * k+1, so the voltage that drove the current over the last period is
     * the command from two steps back */
    float va_prev = f->va2, vb_prev = f->vb2;
    f->va2 = f->valpha; f->vb2 = f->vbeta;
    int8_t inj_applied = f->inj2;
    f->inj2 = f->inj_sign;
    float ia_old = f->ialpha, ib_old = f->ibeta;
    f->ialpha = i[0];
    f->ibeta = (i[0] + 2.0f * i[1]) * INV_SQRT3;

    float ipk = sqrtf(f->ialpha * f->ialpha + f->ibeta * f->ibeta);
    if (ipk > f->i_peak) f->i_peak = ipk;

    o->enable = false;
    if (f->state == FOC_IDLE || f->state == FOC_FAULT) {
        reset_ctrl(f);
        o->duty[0] = o->duty[1] = o->duty[2] = 0.5f;
        return;
    }
    if (ipk > f->cfg.i_trip)        { fault(f, FOC_F_OVERCURRENT); return; }
    if (me->vbus > f->cfg.vbus_ov)  { fault(f, FOC_F_OVERVOLT);    return; }

    /* the observer runs in every active state (needs a valid flux) */
    if (f->mot.psi > 0.0f)
        observer(f, va_prev, vb_prev, 0.5f * (f->ialpha + ia_old), 0.5f * (f->ibeta + ib_old));

    const float t = (float)f->t_state * ts;
    switch (f->state) {

    case FOC_ID_R: {
        /* DC current along alpha. The rotor first settles (aligns) at the
         * high level, then V is averaged at the high and at the low level:
         * R = dV/dI (dead-time and offset errors cancel; the rotor stays
         * aligned because the current only decreases). */
        float i1 = 0.5f * f->cfg.i_start, i2 = f->cfg.i_start;
        /* settle detection: a swinging rotor induces back-EMF on the q axis.
         * Wait until |vq| (low-passed) stays small, min ID_R_SETTLE_S, max 8 s */
        if (f->settle_t == 0.0f) {
            f->vq_lp += (fabsf(f->eq_lp) - f->vq_lp) * (ts / 0.3f);
            bool quiet = f->vq_lp < 0.004f * f->vbus;
            f->quiet_n = quiet ? f->quiet_n + 1u : 0u;
            if ((t > ID_R_SETTLE_S && f->quiet_n * ts > 0.3f) || t > 8.0f) f->settle_t = t;
        }
        const float T0 = f->settle_t > 0.0f ? f->settle_t : 1e9f;
        const float T1 = T0 + ID_R_PHASE_S, T2 = T1 + ID_R_PHASE_S;
        float lvl = t < T1 ? i2 : i1;
        f->id_ref = t < 0.3f ? i2 * t / 0.3f : lvl;
        /* active damping of the aligned rotor: a swinging rotor induces
         * back-EMF e_q ~ w*psi on the q axis; feed it back as an opposing
         * q current (torque against the motion). Heavy blowers otherwise
         * keep swinging for many seconds. */
        float eq = f->vq - f->mot.R * f->iq;
        f->eq_lp += (eq - f->eq_lp) * (ts / 0.005f);
        f->iq_ref = CLAMP(-ID_DAMP_A_PER_V * f->eq_lp, -0.5f * i2, 0.5f * i2);
        current_loop(f, 0.0f, 0.0f, false);
        float tt = t < T1 ? t - T0 : t - T1;
        if (t > T0 && tt > 0.5f * ID_R_PHASE_S) { f->acc_a += f->vd; f->acc_b += f->id; f->acc_n += 1.0f; }
        if (t < T1 && t + ts >= T1) {
            if (f->acc_b / f->acc_n < 0.5f * i2) { fault(f, FOC_F_PHASE_LOSS); return; }  /* open motor */
            f->id_r1_v = f->acc_a / f->acc_n; f->id_i1 = f->acc_b / f->acc_n;
            f->acc_a = f->acc_b = f->acc_n = 0.0f;
        }
        if (t >= T2) {
            float v1 = f->acc_a / f->acc_n, i1m = f->acc_b / f->acc_n;
            float R = (f->id_r1_v - v1) / (f->id_i1 - i1m);
            if (!(R > 0.02f && R < 60.0f)) { fault(f, FOC_F_ID); return; }
            f->mot.R = R;
            /* Intercept of V(I): with a DC current along alpha the residual
             * dead-time error (true - compensated) appears as 4/3 of it on
             * the alpha axis at both levels -> self-calibrate the dead-time
             * compensation of this inverter */
            float c = v1 - R * i1m;
            f->mot.vdt = CLAMP(f->mot.vdt + 0.75f * c, 0.0f, 0.05f * f->vbus);
            /* hold the voltage of the high level (rotor stays aligned) */
            f->vd = f->id_r1_v; f->vq = 0.0f;
            set_state(f, FOC_ID_L);
            f->inj_sign = 1; f->inj2 = 0;
        }
        modulate(f, 0.0f, f->vd, f->vq, o, i);
        break;
    }

    case FOC_ID_L: {
        /* keep the DC current (rotor held on alpha), superimpose a +-Vh
         * square wave first on d (alpha), then on q (beta). The current
         * step per period is Vh*Ts/L; demodulate with the sign that was
         * applied during the last period. */
        bool qaxis = t >= ID_L_PHASE_S;
        f->id_ref = f->cfg.i_start; f->iq_ref = 0.0f;
        float vh = ID_VH_FRAC * f->vbus;
        float di = qaxis ? (f->ibeta - ib_old) : (f->ialpha - ia_old);
        float tt = qaxis ? t - ID_L_PHASE_S : t;
        if (tt > 0.05f) {                      /* skip settling */
            f->acc_a += di * (float)inj_applied;
            f->acc_n += 1.0f;
        }
        /* open-loop injection on top of the DC voltage held from the R
         * test: a running current controller would cancel part of the
         * injected voltage with its P term and inflate L */
        f->inj_sign = (int8_t)(f->inj_sign > 0 ? -1 : 1);
        float vd = f->vd + (qaxis ? 0.0f : vh * f->inj_sign);
        float vq = f->vq + (qaxis ? vh * f->inj_sign : 0.0f);
        if (!qaxis && t + ts >= ID_L_PHASE_S) {
            float L = vh * ts / (f->acc_a / f->acc_n);
            f->mot.Ld = L;
#ifdef FOC_DEBUG
            { extern void foc_dbg(const char *, float, float, float); foc_dbg("Ld", L, f->acc_a / f->acc_n, f->acc_n); }
#endif
            f->acc_a = f->acc_n = 0.0f;
        }
        if (t >= 2.0f * ID_L_PHASE_S) {
            float L = vh * ts / (f->acc_a / f->acc_n);
            f->mot.Lq = L;
#ifdef FOC_DEBUG
            { extern void foc_dbg(const char *, float, float, float); foc_dbg("Lq", L, f->acc_a / f->acc_n, f->acc_n); }
#endif
            if (!(f->mot.Ld > 1e-4f && f->mot.Ld < 1.0f && f->mot.Lq > 1e-4f && f->mot.Lq < 1.0f)) {
                fault(f, FOC_F_ID); return;
            }
            update_gains(f);
            f->int_d = f->int_q = 0.0f;
            /* continue with I/f start; flux measured on the way */
            f->mot.psi = 0.0f;
            set_state(f, FOC_START);
            f->th_ol = 0.0f; f->w_ol = 0.0f; f->i_ol = f->cfg.i_start;
            f->fa = f->fb = 0.0f;
        }
        modulate(f, 0.0f, vd, vq, o, i);
        break;
    }

    case FOC_CATCH:
        /* zero-current control: the current-loop output equals the back-
         * EMF, so the observer locks onto a spinning rotor */
        f->id_ref = f->iq_ref = 0.0f;
        current_loop(f, f->th_obs, f->w_pll, false);
        modulate(f, f->th_obs + 1.5f * ts * f->w_pll, f->vd, f->vq, o, i);
        break;

    case FOC_START: {
        /* I/f: current vector of fixed amplitude rotating at w_ol */
        float dir = f->speed_cmd < 0.0f ? -1.0f : 1.0f;
        float ramp_i = CLAMP(t / 0.3f, 0.0f, 1.0f);
        bool measuring = f->id_running && f->mot.psi == 0.0f;
        float w_target = measuring ? 1.2f * f->cfg.w_handover : 2.0f * f->cfg.w_handover;
        if (t > 0.3f && f->w_ol < w_target)
            f->w_ol += f->cfg.accel * 0.5f * ts;            /* gentle I/f ramp */
        f->th_ol = wrap(f->th_ol + dir * f->w_ol * ts);
        f->id_ref = 0.0f;
        f->iq_ref = dir * f->i_ol * ramp_i;
        current_loop(f, f->th_ol, dir * f->w_ol, false);

        if (measuring && f->w_ol >= 0.7f * w_target) {
            /* Flux magnitude from a leaky (DC-blocking) integral of
             * v - R i in the stationary frame, minus L i. |rotor flux| is
             * constant even while the rotor hunts around the forced speed,
             * so no steady-state assumption is needed. */
            const float wc = 0.1f * f->w_ol;                 /* leak: -0.5 % magnitude */
            float L = 0.5f * (f->mot.Ld + f->mot.Lq);       /* rough value, refined in RUN */
            float iam = 0.5f * (f->ialpha + ia_old), ibm = 0.5f * (f->ibeta + ib_old);
            f->fa += ts * (va_prev - f->mot.R * iam - wc * f->fa);
            f->fb += ts * (vb_prev - f->mot.R * ibm - wc * f->fb);
            f->acc_b += 1.0f;                               /* settle counter */
            if (f->acc_b * ts > 5.0f / wc + 0.3f) {
                float pa = f->fa - L * f->ialpha, pb = f->fb - L * f->ibeta;
                f->acc_a += sqrtf(pa * pa + pb * pb) * sqrtf(1.0f + 0.01f);   /* leak gain */
                f->acc_n += 1.0f;
            }
            if (f->acc_n * ts >= ID_PSI_HOLD_S) {
                float psi = f->acc_a / f->acc_n;
                if (!(psi > 0.003f && psi < 3.0f)) { fault(f, FOC_F_ID); return; }
                f->mot.psi = psi;
                update_gains(f);                            /* uses Je guess */
                float pa = f->fa - L * f->ialpha, pb = f->fb - L * f->ibeta;
                f->xa = f->fa; f->xb = f->fb;               /* observer starts on the measured flux */
                f->th_pll = atan2f(pb, pa); f->w_pll = dir * f->w_ol;
                f->acc_a = f->acc_b = f->acc_n = 0.0f;
                f->t_state = (uint32_t)(0.3f / ts);         /* keep current ramp done */
            }
        }

        /* hand over when the observer agrees with the forced rotation */
        if (f->mot.psi > 0.0f && f->w_ol >= f->cfg.w_handover && t > 0.5f) {
            float w_obs = f->w_pll * dir;
            bool flux_ok = f->psi_obs > 0.6f * f->mot.psi && f->psi_obs < 1.4f * f->mot.psi;
            /* I/f has no damping: the rotor hunts around the forced speed,
             * so compare the low-pass filtered observer speed (tau 0.2 s) */
            f->w_obs_lp += (w_obs - f->w_obs_lp) * (ts / 0.2f);
            bool agree = flux_ok && fabsf(f->w_obs_lp - f->w_ol) < 0.15f * f->w_ol;
            f->agree_n = agree ? f->agree_n + 1u : 0u;
            if (f->agree_n > (uint32_t)(0.5f / ts)) {       /* stable for 0.5 s */
                f->agree_n = 0;
                /* torque-producing current seen in the observer frame */
                float c = cosf(f->th_obs), s = sinf(f->th_obs);
                float iq_obs = -f->ialpha * s + f->ibeta * c;
                f->int_s = iq_obs;
                f->int_q = f->vq; f->int_d = 0.0f;
                f->w_ref_ramp = f->w_pll;
                set_state(f, FOC_RUN);
                f->start_hold = 0;
            } else if (f->w_ol >= w_target && ++f->start_hold > (uint32_t)(3.0f / ts)) {
                fault(f, FOC_F_START);                      /* rotor never followed */
                return;
            }
        }
        modulate(f, f->th_ol + 1.5f * ts * dir * f->w_ol, f->vd, f->vq, o, i);
        break;
    }

    case FOC_RUN:
    case FOC_ID_J:
        f->id_ref = 0.0f;
        current_loop(f, f->th_obs, f->w_pll, true);
        modulate(f, f->th_obs + 1.5f * ts * f->w_pll, f->vd, f->vq, o, i);
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* 1 kHz: sequencing, speed loop, slow protections                     */
/* ------------------------------------------------------------------ */
static void speed_loop(foc_t *f, float target)
{
    const float dt = 1e-3f;
    float step = f->cfg.accel * dt;
    bool regen = fabsf(f->vbus) > f->cfg.vbus_regen;

    if (regen) {
        /* bus pumped by regeneration: no braking, the reference follows
         * the coasting rotor, but never past the target speed */
        f->w_ref_ramp = f->w_pll;
        if ((target >= 0.0f && f->w_ref_ramp < target) || (target < 0.0f && f->w_ref_ramp > target))
            f->w_ref_ramp = target;
    } else if (f->w_ref_ramp < target - step) f->w_ref_ramp += step;
    else if (f->w_ref_ramp > target + step)   f->w_ref_ramp -= step;
    else                                      f->w_ref_ramp = target;

    /* voltage-limit governor: if the inverter runs out of voltage (high
     * speed / high inductance / low mains), don't push the reference
     * further; settle slightly below the speed the bus can deliver */
    if (f->vsat) { if (f->vsat_ms < 200u) f->vsat_ms += 2u; } else if (f->vsat_ms) f->vsat_ms--;
    f->volt_limited = f->vsat_ms > 40u;
    if (f->volt_limited) {
        float back = 0.985f * f->w_pll;
        if ((target >= 0.0f && f->w_ref_ramp > back) || (target < 0.0f && f->w_ref_ramp < back))
            f->w_ref_ramp = back;
    }

    float e = f->w_ref_ramp - f->w_pll;
    f->int_s += f->ki_s * e * dt;
    f->int_s = CLAMP(f->int_s, -f->cfg.i_max, f->cfg.i_max);
    float iq = CLAMP(f->kp_s * e + f->int_s, -f->cfg.i_max, f->cfg.i_max);
    if (regen) {                     /* no braking torque */
        if (f->w_pll > 0.0f && iq < 0.0f) iq = 0.0f;
        if (f->w_pll < 0.0f && iq > 0.0f) iq = 0.0f;
    }
    f->iq_ref = iq;
}

void foc_tick_1ms(foc_t *f)
{
    f->ms_state++;
    if (f->state == FOC_FAULT) return;

    if (f->state != FOC_IDLE && f->vbus < f->cfg.vbus_uv) { fault(f, FOC_F_UNDERVOLT); return; }

    switch (f->state) {
    case FOC_IDLE:
        if (f->id_req || (f->run_req && !f->mot.valid)) {
            f->id_req = false;
            f->id_running = true;
            f->psi_pass = 0;
            f->settle_t = 0.0f; f->vq_lp = 0.0f; f->quiet_n = 0; f->eq_lp = 0.0f;
            f->mot.valid = false;
            set_state(f, FOC_ID_R);
        } else if (f->run_req && f->speed_cmd != 0.0f) {
            set_state(f, FOC_CATCH);
        }
        break;

    case FOC_CATCH:
        if (f->ms_state >= 150u) {
            float dir = f->speed_cmd < 0.0f ? -1.0f : 1.0f;
            bool flux_ok = f->psi_obs > 0.6f * f->mot.psi && f->psi_obs < 1.4f * f->mot.psi;
            if (flux_ok && fabsf(f->w_pll) > 0.8f * f->cfg.w_handover) {
                if (f->w_pll * dir > 0.0f) {          /* same direction: take it */
                    f->int_s = 0.0f;
                    f->w_ref_ramp = f->w_pll;
                    set_state(f, FOC_RUN);
                } else {                              /* wind-milling backwards  */
                    set_state(f, FOC_IDLE);           /* coast, re-check later   */
                    f->ms_state = 0;
                }
            } else {
                set_state(f, FOC_START);
                f->th_ol = f->th_obs; f->w_ol = 0.0f; f->i_ol = f->cfg.i_start;
            }
        }
        break;

    case FOC_RUN:
    case FOC_ID_J: {
        if (!f->run_req && !f->id_running) { set_state(f, FOC_IDLE); break; }
        float target = f->speed_cmd * f->cfg.w_max;
        float wmin = 1.2f * f->cfg.w_handover;
        if (fabsf(target) < wmin) target = (target < 0.0f ? -wmin : wmin);
        if (f->id_running) target = ID_RUN_FRAC * f->cfg.w_max;

        if (f->state == FOC_ID_J) {
            /* symmetric torque step +dI then -dI: the change of the load
             * torque (fan law) is nearly the same in both halves and cancels */
            float tj = (float)f->ms_state * 1e-3f;
            /* dI below the load current: torque never reverses, so the
             * test cannot pump regenerated energy into the PFC bus */
            float dI = CLAMP(0.8f * f->j_iq0, 0.1f, 0.5f * f->cfg.i_start);
            if (tj < ID_J_STEP_S) {
                f->iq_ref = f->j_iq0 + dI;
                f->j_w1 = f->w_pll;
            } else {
                f->iq_ref = f->j_iq0 - dI;
            }
            if (tj >= 2.0f * ID_J_STEP_S) {
                float dw = (f->j_w1 - f->j_w0) - (f->w_pll - f->j_w1);
                float Je = 1.5f * f->mot.psi * dI * 2.0f * ID_J_STEP_S / dw;
                if (!(dw > 0.0f && Je > 1e-7f && Je < 10.0f)) { fault(f, FOC_F_ID); return; }
                f->mot.Je = Je;
                f->mot.valid = true;
                f->id_running = false;
                update_gains(f);
                f->int_s = f->j_iq0;
                f->w_ref_ramp = f->w_pll;
                set_state(f, f->run_req ? FOC_RUN : FOC_IDLE);
            }
            break;
        }

        speed_loop(f, target);

        /* identification in closed loop: refine the flux from the q-axis
         * voltage equation (steady state, id = 0): vq = R iq + w psi.
         * Two passes, each 0.5 s, the observer improves after the first. */
        if (f->id_running && f->psi_pass < 2u && fabsf(f->w_ref_ramp - target) < 1.0f &&
            f->ms_state > 1500u) {
            f->acc_a += (f->vq - f->mot.R * f->iq) / f->w_pll;
            f->acc_n += 1.0f;
            if (f->acc_n >= 500.0f) {
                float psi = f->acc_a / f->acc_n;
                if (psi > 0.5f * f->mot.psi && psi < 2.0f * f->mot.psi) f->mot.psi = psi;
                f->acc_a = f->acc_n = 0.0f;
                f->psi_pass++;
                f->ms_state = 1000u;          /* settle again before the next pass */
                update_gains(f);
            }
            break;
        }
        if (f->id_running && f->ms_state > (uint32_t)(ID_J_SETTLE_S * 1000.0f) &&
            fabsf(f->w_ref_ramp - target) < 1.0f) {
            f->j_w0 = f->w_pll; f->j_iq0 = f->iq_ref;
            set_state(f, FOC_ID_J);
            break;
        }

        /* locked rotor: high torque demand but no rotation */
        if (fabsf(f->w_pll) < f->cfg.w_min_run && fabsf(f->iq_ref) > 0.8f * f->cfg.i_max) {
            if (++f->locked_ms > 1500u) { fault(f, FOC_F_LOCKED); return; }
        } else if (f->locked_ms) {
            f->locked_ms--;
        }
        /* observer lost (flux magnitude implausible) while running */
        if (f->psi_obs < 0.4f * f->mot.psi || f->psi_obs > 2.0f * f->mot.psi) {
            if (++f->locked_ms > 1500u) { fault(f, FOC_F_LOCKED); return; }
        }
        break;
    }

    case FOC_START:
        if (!f->run_req && !f->id_running) set_state(f, FOC_IDLE);
        break;

    default:
        break;
    }
}
