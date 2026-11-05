/*
 * motor_sim.c - Host-side closed-loop test bench for core/sixstep.c
 *
 * Simulates, at 0.25 us resolution:
 *   - 310 V DC bus with 100 Hz rectifier ripple
 *   - 3-phase IGBT bridge (IPM) with freewheel diodes, complementary PWM,
 *     center-aligned 16 kHz carrier, preload/update semantics,
 *     COMP cycle-by-cycle limit and IPM hardware over-current trip
 *   - star-connected BLDC: R, L, trapezoidal BEMF, inertia, fan load
 *   - BEMF dividers + RC filters, 12-bit ADC with noise, shunt + PGA
 *   - TIM2 1 MHz timebase, commutation one-shot, 1 kHz SysTick
 * The control core is linked unchanged. Exit code 0 = all checks passed.
 *
 *   ./motor_sim [scenario] [csv_prefix]
 *   scenario: default | reverse | heavy | inertia | lowbus
 *     default : fan load, 1500 -> 3000 rpm, +30 % load step, -> 1000 rpm
 *     heavy   : +60 % load step at 3000 rpm (demag-limited region)
 *     reverse : same profile, opposite direction
 *     inertia : 8x rotor/load inertia
 *     lowbus  : 250 V bus with 30 V ripple (low mains, weak bulk cap)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_config.h"
#include "sixstep.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---------------- motor / plant parameters ---------------- */
static double       R_PH   = 1.5;      /* ohm per phase                    */
static double       L_PH   = 8e-3;     /* H per phase                      */
static double       KE_LL  = 0.573;    /* V/(rad/s mech), line-line peak   */
static double       J      = 6e-4;     /* kg m^2                           */
static const double T_C    = 0.03;     /* coulomb friction, Nm             */
static double       K_FAN  = 2.0e-5;   /* Nm/(rad/s)^2 -> 2 Nm at 3000 rpm */
static const double VD     = 1.2;      /* diode drop                       */
static double       VBUS0  = 310.0;
static double       VRIPPLE= 12.0;     /* 100 Hz ripple amplitude          */
static const double TAU_BEMF = 7.44e3 * 220e-12;
static const double I_HW_TRIP = 15.0;

static const double DT = 0.25e-6;

/* ---------------- simulated hardware state ---------------- */
enum { LEG_OFF, LEG_HI, LEG_LO };
static struct {
    int    mode;            /* 0 off, 1 precharge, 2 step */
    int    hi, lo;
    double duty_pre, duty;  /* preload / active */
    int    son_pre, son;    /* sample mode preload / active */
    int    cbc;             /* OCREF cleared until next update */
    int    comm_armed;
    uint32_t comm_at;
    int    hw_trip;
} hw;

static double t;                       /* seconds */
static double i_ph[3];
static double omega, theta;            /* mech rad/s, mech rad */
static double vf[3];                   /* filtered divider outputs (V at ADC) */
static double v_term[3];
static double vbus_now;
static double ibus_now;
static ss_t   M;

/* commutation angle statistics (forward runs) */
static double ang_err_sum, ang_err_abs, ang_err_max;
static long   ang_n;
static int    track_angle;

static uint32_t now_us(void) { return (uint32_t)(t * 1e6); }

/* ---------------- HAL implementation ---------------- */
static void h_outputs_off(void) { hw.mode = 0; }
static void h_low_sides_on(void) { hw.mode = 1; }
static void h_apply_step(uint8_t hi, uint8_t lo)
{
    hw.mode = 2; hw.hi = hi; hw.lo = lo;
    if (track_angle && M.state == SS_RUN) {
        double the = fmod(theta * MOTOR_POLE_PAIRS, 2 * M_PI) * 180.0 / M_PI;
        double ideal = 30.0 + 60.0 * M.step;
        double e = fmod(the - ideal + 540.0, 360.0) - 180.0;
        ang_err_sum += e; ang_err_abs += fabs(e);
        if (fabs(e) > ang_err_max) ang_err_max = fabs(e);
        ang_n++;
    }
}
static void h_set_duty(float d) { hw.duty_pre = d; }
static void h_set_sample_on(bool on) { hw.son_pre = on; }
static void h_comm_arm(uint32_t at) { hw.comm_armed = 1; hw.comm_at = at; }
static void h_comm_cancel(void) { hw.comm_armed = 0; }

static const ss_hal_t HAL = {
    h_outputs_off, h_low_sides_on, h_apply_step, h_set_duty,
    h_set_sample_on, h_comm_arm, h_comm_cancel, NULL, NULL
};

/* ---------------- plant ---------------- */
static double trap(double th)          /* trapezoid, ZC rising at 0, falling at pi */
{
    th = fmod(th, 2 * M_PI); if (th < 0) th += 2 * M_PI;
    double d = th * 180.0 / M_PI;
    if (d < 30)  return d / 30.0;
    if (d < 150) return 1.0;
    if (d < 210) return 1.0 - (d - 150) / 30.0;
    if (d < 330) return -1.0;
    return -1.0 + (d - 330) / 30.0;
}

static uint16_t adc(double v)
{
    double n = v / ADC_VREF * ADC_FS + ((rand() % 5) - 2);   /* +/-2 LSB noise */
    if (n < 0) n = 0;
    if (n > ADC_FS) n = ADC_FS;
    return (uint16_t)n;
}

static void plant_step(int pwm_on)
{
    const double ke_ph = KE_LL / 2.0;
    double F[3], e[3];
    double the = theta * MOTOR_POLE_PAIRS;
    for (int k = 0; k < 3; k++) {
        F[k] = trap(the - k * 2.0 * M_PI / 3.0);
        e[k] = ke_ph * omega * F[k];
    }

    int leg[3];
    for (int k = 0; k < 3; k++) {
        if (hw.mode == 0 || hw.hw_trip)      leg[k] = LEG_OFF;
        else if (hw.mode == 1)               leg[k] = LEG_LO;
        else if (k == hw.hi)                 leg[k] = (pwm_on && !hw.cbc) ? LEG_HI : LEG_LO;
        else if (k == hw.lo)                 leg[k] = LEG_LO;
        else                                 leg[k] = LEG_OFF;
    }

    /* terminal voltages of connected phases. An OFF leg conducts through a
     * freewheel diode while it carries current, or when the floating
     * terminal voltage would leave the [-Vd, Vbus+Vd] window. */
    int conn[3]; double V[3], ni[3] = {0, 0, 0};
    for (int k = 0; k < 3; k++) {
        conn[k] = 1;
        if (leg[k] == LEG_HI)      V[k] = vbus_now;
        else if (leg[k] == LEG_LO) V[k] = 0.0;
        else if (i_ph[k] > 1e-6)   V[k] = -VD;             /* low diode  */
        else if (i_ph[k] < -1e-6)  V[k] = vbus_now + VD;   /* high diode */
        else { conn[k] = 0; V[k] = 0; i_ph[k] = 0; }
    }
    double vn = 0;
    int nconn = 0;
    for (int pass = 0; pass < 4; pass++) {
        double s = 0; nconn = 0;
        for (int k = 0; k < 3; k++) if (conn[k]) { s += V[k] - e[k]; nconn++; }
        vn = nconn >= 2 ? s / nconn : 0.0;
        int changed = 0;
        for (int k = 0; k < 3; k++) {
            if (!conn[k]) {
                if (nconn < 2) continue;
                double vfl = e[k] + vn;
                if (vfl > vbus_now + VD) { conn[k] = 1; V[k] = vbus_now + VD; changed = 1; }
                else if (vfl < -VD)      { conn[k] = 1; V[k] = -VD;           changed = 1; }
                continue;
            }
            ni[k] = i_ph[k] + DT * (V[k] - vn - R_PH * i_ph[k] - e[k]) / L_PH;
            if (leg[k] == LEG_OFF && ((V[k] < 0 && ni[k] < 0) || (V[k] > vbus_now && ni[k] > 0))) {
                conn[k] = 0; i_ph[k] = 0; changed = 1;     /* diode turns off */
            }
        }
        if (!changed) break;
    }
    if (nconn >= 2) {
        double mean = 0;
        for (int k = 0; k < 3; k++) if (conn[k]) mean += ni[k];
        mean /= nconn;
        for (int k = 0; k < 3; k++) {
            i_ph[k] = conn[k] ? ni[k] - mean : 0.0;
            if (leg[k] == LEG_OFF && ((V[k] < 0 && i_ph[k] < 0) || (V[k] > vbus_now && i_ph[k] > 0)))
                i_ph[k] = 0.0;
        }
    } else {
        i_ph[0] = i_ph[1] = i_ph[2] = 0;
    }

    for (int k = 0; k < 3; k++)
        v_term[k] = conn[k] ? V[k] : e[k] + vn;

    /* DC-link (shunt) current: current drawn from the + rail */
    ibus_now = 0;
    for (int k = 0; k < 3; k++) {
        if (leg[k] == LEG_HI) ibus_now += i_ph[k];
        else if (leg[k] == LEG_OFF && conn[k] && V[k] > vbus_now) ibus_now += i_ph[k];
    }

    /* mechanics */
    double te = ke_ph * (F[0] * i_ph[0] + F[1] * i_ph[1] + F[2] * i_ph[2]);
    double tl = T_C * tanh(omega / 0.5) + K_FAN * omega * fabs(omega);
    omega += DT * (te - tl) / J;
    theta += DT * omega;

    /* BEMF divider RC filters */
    double a = 1.0 - exp(-DT / TAU_BEMF);
    for (int k = 0; k < 3; k++) {
        double vp = v_term[k] < -0.5 ? -0.5 : v_term[k];
        double vadc = vp / VDIV_RATIO;
        if (vadc < 0) vadc = 0;
        vf[k] += a * (vadc - vf[k]);
    }
}

/* ---------------- scenario ---------------- */
typedef struct { double at; float rpm; double kfan; int stop; } evt_t;

int main(int argc, char **argv)
{
    const char *scen = argc > 1 ? argv[1] : "default";
    const char *prefix = argc > 2 ? argv[2] : NULL;
    int reverse = !strcmp(scen, "reverse");
    int heavy   = !strcmp(scen, "heavy");
    if (!strcmp(scen, "inertia")) J *= 8.0;
    if (!strcmp(scen, "lowbus")) { VBUS0 = 250.0; VRIPPLE = 30.0; }
    /* motor tolerance study: SIM_RK / SIM_LK / SIM_KEK scale R, L, Ke */
    if (getenv("SIM_RK"))  R_PH  *= atof(getenv("SIM_RK"));
    if (getenv("SIM_LK"))  L_PH  *= atof(getenv("SIM_LK"));
    if (getenv("SIM_KEK")) KE_LL *= atof(getenv("SIM_KEK"));
    srand(1);

    evt_t ev[8]; int nev = 0;
    double t_end;
    ev[nev++] = (evt_t){0.05, 1500.0f, K_FAN, 0};
    ev[nev++] = (evt_t){2.6,  3000.0f, K_FAN, 0};
    ev[nev++] = (evt_t){4.2,  3000.0f, K_FAN * (heavy ? 1.6 : 1.3), 0};  /* load step */
    ev[nev++] = (evt_t){5.2,  1000.0f, K_FAN * (heavy ? 1.6 : 1.3), 0};
    ev[nev++] = (evt_t){7.2,  0.0f,    K_FAN, 1};
    t_end = 7.5;

    FILE *fs = NULL, *fh = NULL;
    if (prefix) {
        char fn[256];
        snprintf(fn, sizeof fn, "%s_%s.csv", prefix, scen);
        fs = fopen(fn, "w");
        fprintf(fs, "t,state,ref,ref_ramped,rpm_true,rpm_est,duty,i_est,ia,ib,ic,vbus,sample_on,zc,forced\n");
        snprintf(fn, sizeof fn, "%s_%s_hires.csv", prefix, scen);
        fh = fopen(fn, "w");
        fprintf(fh, "t,va,vb,vc,ia,ib,ic,ibus,step,theta_e\n");
    }

    ss_init(&M, &HAL);
    ss_set_reverse(&M, reverse);
    memset(i_ph, 0, sizeof i_ph);
    omega = 0; theta = getenv("SIM_THETA0") ? atof(getenv("SIM_THETA0")) : 0.3;  /* initial rotor angle (mech rad) */

    const double Tp = 1.0 / PWM_FREQ_HZ;
    int last_half = -1;
    double next_ms = 1e-3;
    int ie = 0;
    long ticks = 0;
    double t_run = -1;
    int pending_adc = 0; double adc_due = 0; ss_meas_t meas = {0};
    uint16_t ibus_latch = 0;
    double rpm_acc[8] = {0}, rpm_signed = 0; int rpm_n[8] = {0};
    int started = 0;
    track_angle = !reverse;
    hw.son = hw.son_pre = 0;

    for (t = 0; t < t_end; t += DT, ticks++) {
        vbus_now = VBUS0 + VRIPPLE * sin(2 * M_PI * 100.0 * t);

        /* carrier */
        double p = fmod(t, Tp) / Tp;
        double tri = p < 0.5 ? 2 * p : 2 - 2 * p;     /* 0 = ON center */
        int half = (int)(t / (Tp / 2));
        if (half != last_half) {                      /* update event   */
            last_half = half;
            hw.duty = hw.duty_pre;
            hw.son = hw.son_pre;
            hw.cbc = 0;
            int at_on_center = (half % 2) == 0;
            if (at_on_center) ibus_latch = adc(ibus_now * SHUNT_OHM * ISENSE_GAIN);
            if (at_on_center == hw.son) {
                for (int k = 0; k < 3; k++) meas.bemf[k] = adc(vf[k]);
                meas.vbus = adc(vbus_now / VDIV_RATIO);
                meas.ntc = 2048;
                pending_adc = 1; adc_due = t + 2.5e-6;  /* conversion + ISR entry */
            }
        }
        int pwm_on = tri < hw.duty;

        plant_step(pwm_on);

        /* comparator cycle-by-cycle limit and IPM hardware trip */
        if (pwm_on && ibus_now > I_LIMIT_CBC_A) hw.cbc = 1;
        if (!hw.hw_trip && fabs(ibus_now) > I_HW_TRIP && hw.mode == 2) {
            hw.hw_trip = 1; hw.mode = 0; ss_on_hw_fault(&M);
        }

        if (pending_adc && t >= adc_due) {
            pending_adc = 0;
            meas.ibus = ibus_latch;
            ss_on_adc(&M, &meas, now_us());
        }
        if (hw.comm_armed && (int32_t)(now_us() - hw.comm_at) >= 0) {
            hw.comm_armed = 0;
            ss_on_comm_timer(&M, now_us());
        }

        if (t >= next_ms) {
            next_ms += 1e-3;
            while (ie < nev && t >= ev[ie].at) {
                K_FAN = ev[ie].kfan;
                if (ev[ie].stop) ss_stop(&M);
                else {
                    ss_set_speed(&M, ev[ie].rpm);
                    if (!started) { started = ss_start(&M); if (!started) printf("start refused (vbus %.1f)\n", M.vbus_v); }
                }
                ie++;
            }
            ss_tick_1ms(&M, now_us());
            if (M.state == SS_RUN && t_run < 0) t_run = t;

            double rpm_true = omega * 60.0 / (2 * M_PI);
            /* steady-state windows: [2.2,2.6] @1500, [3.8,4.2] @3000, [4.8,5.2] @3000 loaded, [6.8,7.2] @1000 */
            const double w0[4] = {2.2, 3.8, 4.8, 6.8}, w1[4] = {2.6, 4.2, 5.2, 7.2};
            for (int k = 0; k < 4; k++) if (t >= w0[k] && t < w1[k]) { rpm_acc[k] += fabs(rpm_true); rpm_signed += rpm_true; rpm_n[k]++; }

            if (fs)
                fprintf(fs, "%.4f,%d,%.1f,%.1f,%.1f,%.1f,%.4f,%.3f,%.3f,%.3f,%.3f,%.1f,%d,%u,%u\n",
                        t, M.state, M.speed_ref_rpm, M.ref_ramped, rpm_true, M.speed_rpm, M.duty, M.i_phase_a,
                        i_ph[0], i_ph[1], i_ph[2], vbus_now, M.sample_on, M.zc_count, M.forced_count);
        }
        /* high-resolution window: one electrical revolution at 1500 rpm + transition */
        if (fh && t >= 2.40 && t < 2.42 && (ticks % 4) == 0)
            fprintf(fh, "%.7f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%d,%.1f\n", t, v_term[0], v_term[1], v_term[2],
                    i_ph[0], i_ph[1], i_ph[2], ibus_now, M.step,
                    fmod(theta * MOTOR_POLE_PAIRS, 2 * M_PI) * 180 / M_PI);
    }
    if (fs) fclose(fs);
    if (fh) fclose(fh);

    /* ---------------- checks ---------------- */
    int fail = 0;
    const float target[4] = {1500, 3000, 3000, 1000};
    printf("scenario: %s\n", scen);
    printf("  closed loop reached at t = %.3f s (start at 0.05 s)\n", t_run);
    if (t_run < 0 || t_run > 3.0) { printf("  FAIL: closed loop not reached\n"); fail = 1; }
    for (int k = 0; k < 4; k++) {
        double avg = rpm_n[k] ? rpm_acc[k] / rpm_n[k] : 0;
        double err = (avg - target[k]) / target[k] * 100.0;
        int ok = fabs(err) < 3.0;
        printf("  speed window %d: ref %4.0f rpm, measured %7.1f rpm (%+.2f %%) %s\n",
               k, target[k], avg, err, ok ? "ok" : "FAIL");
        fail |= !ok;
    }
    printf("  zero crossings %u, forced commutations %u, demag-hidden ZCs %u, last advance %.1f deg\n", M.zc_count, M.forced_count, M.late_zc, (double)M.advance_deg);
    if (M.forced_count * 100u > M.zc_count + 100u) { printf("  FAIL: too many forced commutations\n"); fail = 1; }
    if (M.faults) { printf("  FAIL: faults 0x%02x\n", (unsigned)M.faults); fail = 1; }
    if (ang_n) {
        printf("  commutation angle error vs ideal (RUN, includes intended advance): mean %+.2f deg, mean|.| %.2f deg, max %.2f deg (n=%ld)\n",
               ang_err_sum / ang_n, ang_err_abs / ang_n, ang_err_max, ang_n);
        if (ang_err_abs / ang_n > 12.0) { printf("  FAIL: commutation timing\n"); fail = 1; }
    }
    if ((rpm_signed < 0) != (reverse != 0)) { printf("  FAIL: wrong direction of rotation\n"); fail = 1; }
    else printf("  direction: %s ok\n", reverse ? "reverse" : "forward");
    printf("  result: %s\n", fail ? "FAIL" : "PASS");
    return fail;
}
