/*
 * foc_sim.c - Closed-loop test bench for foc.c (cooler motors).
 *
 * Plant: PMSM in the rotor dq frame (R, Ld, Lq, psi, p), centrifugal
 * blower load T = kf*w^2 + Tc, rotor + blower inertia J, optional locked
 * rotor and wind-milling; 3-phase inverter averaged per sub-step with the
 * one-period command delay of real hardware and dead-time error; 3-shunt
 * current sampling (noise, 12-bit quantisation, invalid phase when the
 * low-side window is short); PFC-fed DC bus (390 V, 660 uF, cannot sink
 * regenerated energy).
 *
 *   ./foc_sim            run the full test suite, exit code 0 = pass
 *   ./foc_sim csv        also write build/foc_<test>.csv traces
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "foc.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    const char *name;
    int    p;              /* pole pairs */
    double R, Ld, Lq, psi;
    double J, kf, Tc;      /* blower: T = kf*wm^2 + Tc */
    double rpm_max;        /* speed at 100 % */
} motor_t;

static const motor_t MOTORS[] = {
    {"A SIZO-like 36S/40P 1.2kW", 20, 1.6, 15e-3, 15e-3, 0.156, 0.09, 12.7 / (47.12 * 47.12), 0.3, 450},
    {"B ELECTROGEN-like (salient)", 20, 2.4, 18e-3, 22e-3, 0.170, 0.12, 14.0 / (47.12 * 47.12), 0.4, 450},
    {"C old 12S/14P", 7, 3.0, 25e-3, 25e-3, 0.227, 0.06, 4.24 / (94.25 * 94.25), 0.2, 900},
    {"D new design (low R)", 20, 1.0, 10e-3, 10e-3, 0.140, 0.10, 12.7 / (47.12 * 47.12), 0.3, 450},
};

/* ---------------- plant state ---------------- */
static const double TS = 1.0 / 16000.0;
static const int    NSUB = 8;
static const double C_BUS = 660e-6, VBUS_REF = 390.0;
static double T_DEAD = 1.0e-6;
static double MOD_DELAY = 0.005;   /* battery module take-over (hot standby spec) */

typedef struct {
    const motor_t *m;
    double id, iq, wm, th_m, vbus, pfc_int, kf;
    bool   locked;
    double duty_now[3];    /* applied during the current period */
    bool   en_now;
    double t;
    double i_abc[3];
    double energy_in;
    /* source: mains (PFC) and/or 48 V battery module feeding the bus */
    double t_out, t_back, p_mod, mod_int, mod_since, e_mod, vbus_min, p_mod_now;
    bool   mod_active;
} plant_t;

static double gauss(void)
{
    double u = (rand() + 1.0) / (RAND_MAX + 2.0), v = (rand() + 1.0) / (RAND_MAX + 2.0);
    return sqrt(-2 * log(u)) * cos(2 * M_PI * v);
}

static void plant_currents_abc(plant_t *pl)
{
    double th = pl->th_m * pl->m->p;
    double c = cos(th), s = sin(th);
    double ia = pl->id * c - pl->iq * s, ib = pl->id * s + pl->iq * c;
    pl->i_abc[0] = ia;
    pl->i_abc[1] = -0.5 * ia + 0.5 * sqrt(3) * ib;
    pl->i_abc[2] = -0.5 * ia - 0.5 * sqrt(3) * ib;
}

static void plant_period(plant_t *pl)
{
    const motor_t *m = pl->m;
    double h = TS / NSUB;
    for (int n = 0; n < NSUB; n++) {
        plant_currents_abc(pl);
        double th = pl->th_m * m->p, we = pl->wm * m->p;
        double c = cos(th), s = sin(th);
        double va, vb;
        double iinv = 0;
        if (pl->en_now) {
            double ph[3], mean = 0;
            for (int k = 0; k < 3; k++) {
                /* dead time: the output follows the freewheel diode */
                /* dead time: the pole follows the freewheel diode. When the
                 * PWM ripple crosses zero within the period the error is
                 * only partial -> proportional region of half the ripple */
                double d = pl->duty_now[k];
                double half_ripple = 0.5 * pl->vbus * d * (1 - d) * TS / (0.5 * (m->Ld + m->Lq)) + 1e-3;
                double sgn = pl->i_abc[k] / half_ripple;
                if (sgn > 1) sgn = 1;
                if (sgn < -1) sgn = -1;
                double dt_err = -T_DEAD / TS * pl->vbus * sgn;
                ph[k] = pl->duty_now[k] * pl->vbus + dt_err;
                mean += ph[k] / 3;
                iinv += pl->duty_now[k] * pl->i_abc[k];
            }
            double a = ph[0] - mean, b = ph[1] - mean, cc = ph[2] - mean;
            va = a;
            vb = (b - cc) / sqrt(3);
            (void)cc;
        } else {
            /* all switches off: diodes conduct only above the bus -> for the
             * speeds used here (|e| < Vbus) the currents decay to zero */
            pl->id *= 0.5; pl->iq *= 0.5;
            va = vb = 0;
        }
        double vd = va * c + vb * s, vq = -va * s + vb * c;
        if (pl->en_now) {
            /* exponential Euler: exact for the R-L part, stable for any R */
            double ed = vd + we * m->Lq * pl->iq, eq = vq - we * (m->Ld * pl->id + m->psi);
            double ad = exp(-m->R * h / m->Ld), aq = exp(-m->R * h / m->Lq);
            pl->id = ed / m->R + (pl->id - ed / m->R) * ad;
            pl->iq = eq / m->R + (pl->iq - eq / m->R) * aq;
        }
        double te = 1.5 * m->p * (m->psi * pl->iq + (m->Ld - m->Lq) * pl->id * pl->iq);
        double tl = pl->kf * pl->wm * fabs(pl->wm) + m->Tc * tanh(pl->wm / 0.05);
        if (pl->locked) pl->wm = 0;
        else pl->wm += h * (te - tl) / m->J;
        pl->th_m += h * pl->wm;

        /* DC bus: PFC supplies power (cannot sink), inverter draws iinv */
        bool mains = !(pl->t >= pl->t_out && pl->t < pl->t_back);
        double err = VBUS_REF - pl->vbus;
        double p_pfc = 0;
        if (mains) {
            pl->pfc_int += h * 800.0 * err;
            if (pl->pfc_int < 0) pl->pfc_int = 0;
            if (pl->pfc_int > 1500) pl->pfc_int = 1500;
            p_pfc = 60.0 * err + pl->pfc_int;
            if (p_pfc < 0) p_pfc = 0;
            if (p_pfc > 1500) p_pfc = 1500;
        } else {
            pl->pfc_int = 0;
        }
        /* battery module: hot standby, regulates the bus at 370 V (below
         * the PFC set-point, so mains has priority without messages),
         * engages MOD_DELAY after the bus drops below 375 V, hard power cap */
        double p_mod = 0;
        if (pl->p_mod > 0) {
            if (!pl->mod_active && pl->vbus < 375.0) {
                if (pl->mod_since < 0) pl->mod_since = pl->t;
                if (pl->t - pl->mod_since > MOD_DELAY) pl->mod_active = true;
            }
            if (pl->mod_active) {
                double em = 370.0 - pl->vbus;
                pl->mod_int += h * 400.0 * em;
                if (pl->mod_int < 0) pl->mod_int = 0;
                if (pl->mod_int > pl->p_mod) pl->mod_int = pl->p_mod;
                p_mod = 40.0 * em + pl->mod_int;
                if (p_mod < 0) p_mod = 0;
                if (p_mod > pl->p_mod) p_mod = pl->p_mod;
                if (mains && pl->vbus > 380.0) { pl->mod_active = false; pl->mod_since = -1; pl->mod_int = 0; }
            }
        }
        pl->p_mod_now = p_mod;
        /* + 2 W permanent bus load (PFC controller, bleeder) */
        pl->vbus += h * ((p_pfc + p_mod - 2.0) / pl->vbus - iinv) / C_BUS;
        pl->energy_in += h * (p_pfc + p_mod);
        pl->e_mod += h * p_mod;
        if (pl->t > 1.0 && pl->vbus < pl->vbus_min) pl->vbus_min = pl->vbus;
        pl->t += h;
    }
    plant_currents_abc(pl);
}

static void sample(plant_t *pl, foc_meas_t *me)
{
    for (int k = 0; k < 3; k++) {
        double q = 0.008;   /* A per count */
        double v = pl->i_abc[k] + 0.015 * gauss();
        me->i[k] = (float)(q * floor(v / q + 0.5));
        /* low-side window too short -> garbage */
        /* 3 us low-side window needed for the ADC: duty <= 0.952 */
        me->valid[k] = !(pl->en_now && pl->duty_now[k] > 0.952);
        if (!me->valid[k]) me->i[k] = (float)(5.0 * gauss());
    }
    me->vbus = (float)(pl->vbus + 0.5 * gauss());
}

/* ---------------- one simulation run ---------------- */
typedef struct {
    double t_end;
    double t_lock;          /* lock rotor at this time (<0 never) */
    double wm0;             /* initial mechanical speed (wind-milling) */
    double th0;             /* initial rotor angle (mech rad) */
    double t_speed2, speed2;/* speed change */
    double t_load, load_k;  /* load step factor */
    double t_stop;          /* run=false at this time (<0 never) */
    bool   preidentified;
    bool   fast_ramp;      /* 0..100 %% in 2 s instead of 20 s */
    double t_outage, t_return, p_module;  /* mains outage window, module power (0 = none) */
    double app_plim, app_speed;           /* app reaction 100 ms after outage (0 = none) */
    const char *csv;
} scen_t;

typedef struct {
    foc_motor_t idm;
    uint32_t faults;
    foc_state_t final_state;
    double t_run;           /* first time in RUN after start */
    double rpm_err1, rpm_err2;   /* % error in steady windows */
    double i_peak, vbus_max;
    double t_fault;
    double pin_w;           /* input power in window 1 */
    double vbus_min, rpm_outage, p_mod_avg, rpm_end;
} result_t;

static foc_cfg_t make_cfg(const motor_t *m)
{
    foc_cfg_t c;
    foc_default_cfg(&c, (float)TS);
    c.v_deadtime = (float)(T_DEAD / TS * VBUS_REF);
    c.w_max = (float)(m->rpm_max * 2 * M_PI / 60 * m->p);
    c.accel = c.w_max / 20.0f;                       /* 20 s 0..100 % */
    c.w_handover = 0.10f * c.w_max;
    c.w_min_run = 0.04f * c.w_max;
    return c;
}

static result_t run(const motor_t *m, const scen_t *sc, const foc_motor_t *known)
{
    plant_t pl = {0};
    pl.m = m; pl.vbus = VBUS_REF; pl.pfc_int = 0; pl.kf = m->kf;
    pl.wm = sc->wm0; pl.th_m = sc->th0;
    pl.t_out = sc->p_module > 0 || sc->t_return > 0 ? sc->t_outage : 1e9;
    pl.t_back = sc->t_return > 0 ? sc->t_return : 1e9;
    pl.p_mod = sc->p_module; pl.mod_since = -1; pl.vbus_min = 1e9;
    if (pl.t_out <= 0) { pl.vbus = 370.0; pl.mod_active = true; }
    foc_cfg_t cfg = make_cfg(m);
    if (sc->fast_ramp) cfg.accel = cfg.w_max / 2.0f;
    if (getenv("FOC_COMP")) cfg.v_deadtime = (float)atof(getenv("FOC_COMP"));
    if (getenv("FOC_OBS_GAIN")) cfg.obs_gain = (float)atof(getenv("FOC_OBS_GAIN"));
    if (getenv("FOC_HANDOVER")) cfg.w_handover = (float)atof(getenv("FOC_HANDOVER")) * cfg.w_max;
    foc_motor_t guess = {1.0f, 10e-3f, 10e-3f, 0.1f, 2e-4f, 0.0f, false};
    foc_t f;
    foc_init(&f, &cfg, known ? known : &guess);

    result_t r = {0};
    r.t_run = -1; r.t_fault = -1;
    FILE *fc = NULL;
    if (sc->csv) {
        char fn[128]; snprintf(fn, sizeof fn, "build/foc_%s.csv", sc->csv);
        fc = fopen(fn, "w");
        if (fc) fprintf(fc, "t,state,rpm,rpm_est,ref_rpm,id,iq,iq_ref,vbus,theta_err_deg,psi_obs\n");
    }

    foc_set_speed(&f, 1.0f);
    foc_run(&f, true);
    double next_ms = 1e-3, acc1 = 0, acc2 = 0, p1 = 0, acc_o = 0, pm_o = 0; int n1 = 0, n2 = 0, n_o = 0;
    double duty_next[3] = {0.5, 0.5, 0.5}; bool en_next = false;
    long k = 0;
    while (pl.t < sc->t_end) {
        /* hardware pipeline: duties computed last period apply now */
        for (int j = 0; j < 3; j++) pl.duty_now[j] = duty_next[j];
        pl.en_now = en_next;
        foc_meas_t me; sample(&pl, &me);
        foc_out_t o;
        foc_pwm_step(&f, &me, &o);
        for (int j = 0; j < 3; j++) duty_next[j] = o.duty[j];
        en_next = o.enable;
        plant_period(&pl);
        k++;

        if (pl.t >= next_ms) {
            next_ms += 1e-3;
            if (sc->t_speed2 > 0 && pl.t >= sc->t_speed2) foc_set_speed(&f, (float)sc->speed2);
            if (sc->t_load > 0 && pl.t >= sc->t_load) pl.kf = m->kf * sc->load_k;
            if (sc->t_lock >= 0 && pl.t >= sc->t_lock) pl.locked = true;
            if (sc->t_stop >= 0 && pl.t >= sc->t_stop) foc_run(&f, false);
            /* application layer reaction to the power-source change */
            bool on_batt = pl.t >= sc->t_outage + 0.1 && (sc->t_return <= 0 || pl.t < sc->t_return);
            if (sc->p_module > 0 && sc->app_plim > 0) {
                if (on_batt) { foc_set_power_limit(&f, (float)sc->app_plim); if (sc->app_speed > 0) foc_set_speed(&f, (float)sc->app_speed); }
                else if (sc->t_return > 0 && pl.t >= sc->t_return) { foc_set_power_limit(&f, 0); foc_set_speed(&f, 1.0f); }
            }
            foc_tick_1ms(&f);
            if (f.state == FOC_RUN && !f.id_running && r.t_run < 0) r.t_run = pl.t;
            if (f.state == FOC_FAULT && r.t_fault < 0) r.t_fault = pl.t;
            double rpm = pl.wm * 60 / (2 * M_PI);
            double wref = f.speed_cmd * cfg.w_max / m->p * 60 / (2 * M_PI);
            /* steady windows: last 3 s before the speed change and before end */
            if (sc->t_speed2 > 0 && pl.t > sc->t_speed2 - 3 && pl.t < sc->t_speed2) {
                acc1 += rpm; n1++; p1 += 0;
            }
            if (pl.t > sc->t_end - 3) { acc2 += rpm; n2++; }
            if (sc->t_return > 0 && pl.t > sc->t_return - 4 && pl.t < sc->t_return) { acc_o += rpm; pm_o += pl.p_mod_now; n_o++; }
            if (fabs(pl.vbus) > r.vbus_max) r.vbus_max = pl.vbus;
            if (fc && ((long)(pl.t * 1000.0 + 0.5) % 10) == 0) {
                double the = fmod(pl.th_m * m->p, 2 * M_PI);
                double terr = fmod(f.th_obs - the + 5 * M_PI, 2 * M_PI) - M_PI;
                fprintf(fc, "%.3f,%d,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f,%.1f,%.1f,%.4f\n", pl.t, f.state, rpm,
                        f.w_pll / m->p * 60 / (2 * M_PI), wref, f.id, f.iq, f.iq_ref, pl.vbus,
                        terr * 180 / M_PI, f.psi_obs);
            }
            (void)p1;
        }
    }
    if (fc) fclose(fc);
    double target1 = 1.0 * m->rpm_max, target2 = fabs(sc->speed2) * m->rpm_max;
    if (sc->t_speed2 <= 0) target2 = m->rpm_max;
    r.rpm_err1 = n1 ? (acc1 / n1 - target1) / target1 * 100 : NAN;
    r.rpm_err2 = n2 ? (acc2 / n2 - target2) / target2 * 100 : NAN;
    r.idm = f.mot;
    r.faults = f.faults;
    r.final_state = f.state;
    r.i_peak = f.i_peak;
    r.pin_w = pl.energy_in / pl.t;
    r.vbus_min = pl.vbus_min;
    r.rpm_outage = n_o ? acc_o / n_o : NAN;
    r.p_mod_avg = n_o ? pm_o / n_o : NAN;
    r.rpm_end = n2 ? acc2 / n2 : NAN;
    return r;
}

/* ---------------- test suite ---------------- */
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("    FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static double pct(double est, double tru) { return (est - tru) / tru * 100; }

int main(int argc, char **argv)
{
    bool csv = argc > 1 && !strcmp(argv[1], "csv");
    srand(7);
    if (getenv("FOC_TDEAD")) T_DEAD = atof(getenv("FOC_TDEAD"));
    if (getenv("FOC_MOD_DELAY")) MOD_DELAY = atof(getenv("FOC_MOD_DELAY"));
    const int NM = (int)(sizeof MOTORS / sizeof MOTORS[0]);
    foc_motor_t ident[8];

    if (argc > 1 && !strcmp(argv[1], "random")) {
        /* randomised motors: identify from scratch, then run the profile */
        int n = argc > 2 ? atoi(argv[2]) : 20, ok = 0, nfeas = 0;
        srand(argc > 3 ? (unsigned)atoi(argv[3]) : 12345u);
        printf("== random motors (pole pairs 7..24, R 0.6..4 ohm, L 5..35 mH, saliency 1..1.4, J 0.03..0.15)\n");
        for (int k = 0; k < n; k++) {
            static const int PP[] = {7, 10, 14, 16, 20, 24};
            motor_t m;
            double u = rand() / (double)RAND_MAX;
            m.name = "random";
            m.p = PP[rand() % 6];
            m.R = 0.6 + 3.4 * (rand() / (double)RAND_MAX);
            m.Ld = 5e-3 + 30e-3 * (rand() / (double)RAND_MAX);
            m.Lq = m.Ld * (1.0 + 0.4 * (rand() / (double)RAND_MAX));
            m.rpm_max = 9000.0 / m.p * (0.8 + 0.6 * u);           /* f_e 120..210 Hz */
            double we = m.rpm_max * 2 * M_PI / 60 * m.p;
            m.psi = (0.55 + 0.25 * (rand() / (double)RAND_MAX)) * VBUS_REF / sqrt(3) / we;
            m.J = 0.03 + 0.12 * (rand() / (double)RAND_MAX);
            double wm = m.rpm_max * 2 * M_PI / 60;
            double tmax = 1.5 * m.p * m.psi * 4.0 * (0.4 + 0.5 * (rand() / (double)RAND_MAX));
            m.kf = tmax / (wm * wm); m.Tc = 0.02 * tmax;
            const double load_k_max = 1.2;
            scen_t sc = { .t_end = 75, .t_lock = -1, .th0 = 6.28 * (rand() / (double)RAND_MAX), .t_speed2 = 55,
                          .speed2 = 0.5, .t_load = 45, .load_k = load_k_max, .t_stop = -1,
                          .csv = (getenv("FOC_RAND_CSV") && atoi(getenv("FOC_RAND_CSV")) == k) ? "random" : NULL };
            result_t r = run(&m, &sc, NULL);
            /* can the bus deliver full speed at full load current? */
            double iq_full = (m.kf * wm * wm * load_k_max + m.Tc) / (1.5 * m.p * m.psi);
            double vneed = sqrt(pow(m.psi * we + m.R * iq_full, 2) + pow(we * m.Lq * iq_full, 2));
            bool feasible = vneed < 0.93 * VBUS_REF / sqrt(3);
            bool id_ok = fabs(pct(r.idm.R, m.R)) < 5 && fabs(pct(r.idm.Ld, m.Ld)) < 10 &&
                         fabs(pct(r.idm.Lq, m.Lq)) < 10 && fabs(pct(r.idm.psi, m.psi)) < 5;
            bool good = r.faults == 0 && id_ok &&
                        (feasible ? (fabs(r.rpm_err1) < 2 && fabs(r.rpm_err2) < 2) : true);
            nfeas += !feasible;
            ok += good;
            printf("  %2d p=%2d R=%.2f Ld=%4.1fmH Lq=%4.1fmH psi=%.3f J=%.3f fe=%3.0fHz | R %+5.1f%% Ld %+5.1f%% Lq %+5.1f%% psi %+5.1f%% Je %+6.1f%% | err %+5.2f/%+5.2f%% faults 0x%02x %s%s\n",
                   k, m.p, m.R, m.Ld * 1e3, m.Lq * 1e3, m.psi, m.J, we / 2 / M_PI,
                   pct(r.idm.R, m.R), pct(r.idm.Ld, m.Ld), pct(r.idm.Lq, m.Lq), pct(r.idm.psi, m.psi),
                   pct(r.idm.Je, m.J / (m.p * m.p)), r.rpm_err1, r.rpm_err2, r.faults, good ? "ok" : "FAIL", feasible ? "" : " (bus voltage too low for full speed: speed limited, no trip expected)");
        }
        printf("random motors: %d/%d pass (%d of them voltage-limited)\n", ok, n, nfeas);
        return ok != n;
    }

    printf("== 1. automatic identification + start + speed profile (motor unknown to the drive)\n");
    for (int i = 0; i < NM; i++) {
        const motor_t *m = &MOTORS[i];
        scen_t sc = { .t_end = 75, .t_lock = -1, .th0 = 0.37 * i, .t_speed2 = 55, .speed2 = 0.5,
                      .t_load = 45, .load_k = 1.3, .t_stop = -1, .csv = (csv && i == (getenv("FOC_CSV_MOTOR") ? atoi(getenv("FOC_CSV_MOTOR")) : 0)) ? "id_run_A" : NULL };
        result_t r = run(m, &sc, NULL);
        ident[i] = r.idm;
        double pJe = m->J / (m->p * m->p);
        printf("  %-30s R %+5.1f%%  Ld %+5.1f%%  Lq %+5.1f%%  psi %+5.1f%%  Je %+6.1f%%  | run at %5.1fs  "
               "speed err %+5.2f%% / %+5.2f%%  Ipk %4.1fA  Vbus max %3.0fV  faults 0x%02x  Vdt %.2f V (true %.2f)\n",
               m->name, pct(r.idm.R, m->R), pct(r.idm.Ld, m->Ld), pct(r.idm.Lq, m->Lq), pct(r.idm.psi, m->psi),
               pct(r.idm.Je, pJe), r.t_run, r.rpm_err1, r.rpm_err2, r.i_peak, r.vbus_max, r.faults, r.idm.vdt, T_DEAD / TS * VBUS_REF);
        CHECK(r.faults == 0, "%s faults 0x%x (state %s at %.1fs)", m->name, r.faults, foc_state_name(r.final_state), r.t_fault);
        CHECK(fabs(pct(r.idm.R, m->R)) < 8, "R error");
        CHECK(fabs(pct(r.idm.Ld, m->Ld)) < 15 && fabs(pct(r.idm.Lq, m->Lq)) < 15, "L error");
        CHECK(fabs(pct(r.idm.psi, m->psi)) < 8, "psi error");
        CHECK(fabs(pct(r.idm.Je, pJe)) < 35, "inertia error");
        CHECK(fabs(r.rpm_err1) < 2 && fabs(r.rpm_err2) < 2, "speed error");
        CHECK(r.vbus_max < 440, "bus overvoltage");
    }

    printf("== 2. normal start with identified parameters, 8 initial rotor angles (motor A, B)\n");
    for (int i = 0; i < 2; i++) {
        int ok = 0; double tmax = 0;
        for (int a = 0; a < 8; a++) {
            scen_t sc = { .t_end = 30, .t_lock = -1, .th0 = a * 2 * M_PI / 8 / MOTORS[i].p + 0.01, .t_stop = -1 };
            result_t r = run(&MOTORS[i], &sc, &ident[i]);
            bool good = r.faults == 0 && r.t_run > 0 && fabs(r.rpm_err2) < 2;
            ok += good; if (r.t_run > tmax) tmax = r.t_run;
            if (!good) printf("    angle %d: faults 0x%x t_run %.1f err %.1f%%\n", a, r.faults, r.t_run, r.rpm_err2);
        }
        printf("  %-30s %d/8 started, closed loop within %.1f s\n", MOTORS[i].name, ok, tmax);
        CHECK(ok == 8, "start robustness %s", MOTORS[i].name);
    }

    printf("== 3. locked rotor while running at full speed (motor A)\n");
    {
        scen_t sc = { .t_end = 40, .t_lock = 30, .th0 = 0.1, .t_stop = -1, .csv = csv ? "locked" : NULL };
        result_t r = run(&MOTORS[0], &sc, &ident[0]);
        printf("  fault 0x%02x after %.2f s, peak current %.1f A (limit 8 A, trip 14 A)\n",
               r.faults, r.t_fault - 30, r.i_peak);
        CHECK(r.faults & FOC_F_LOCKED, "locked rotor not detected");
        CHECK(r.t_fault > 0 && r.t_fault - 30 < 3.0, "detection too slow");
        CHECK(r.i_peak < 12, "current not limited");
    }

    printf("== 4. rotor blocked at standstill, start attempt (motor A)\n");
    {
        scen_t sc = { .t_end = 40, .t_lock = 0, .th0 = 0.1, .t_stop = -1 };
        result_t r = run(&MOTORS[0], &sc, &ident[0]);
        printf("  fault 0x%02x at %.1f s, peak current %.1f A\n", r.faults, r.t_fault, r.i_peak);
        CHECK(r.faults & (FOC_F_START | FOC_F_LOCKED), "blocked start not detected");
        CHECK(r.i_peak < 6, "start current too high");
    }

    printf("== 5. flying start: blower wind-milling forward at 30 %% (motor A)\n");
    {
        scen_t sc = { .t_end = 30, .t_lock = -1, .wm0 = 0.3 * 47.12, .th0 = 1.0, .t_stop = -1 };
        result_t r = run(&MOTORS[0], &sc, &ident[0]);
        printf("  closed loop at %.2f s (no I/f), peak %.1f A, faults 0x%02x, final err %+.2f%%\n",
               r.t_run, r.i_peak, r.faults, r.rpm_err2);
        CHECK(r.faults == 0 && r.t_run > 0 && r.t_run < 0.5, "flying start");
    }

    printf("== 6. blower wind-milling backwards at 15 %% (motor A)\n");
    {
        scen_t sc = { .t_end = 45, .t_lock = -1, .wm0 = -0.15 * 47.12, .th0 = 1.0, .t_stop = -1 };
        result_t r = run(&MOTORS[0], &sc, &ident[0]);
        printf("  closed loop at %.2f s, faults 0x%02x, final err %+.2f%%\n", r.t_run, r.faults, r.rpm_err2);
        CHECK(r.faults == 0 && r.t_run > 0 && fabs(r.rpm_err2) < 2, "reverse wind-milling start");
    }

    printf("== 7. fast deceleration 100%% -> 20%% (regeneration into a PFC bus that cannot sink)\n");
    {
        scen_t sc = { .t_end = 50, .t_lock = -1, .th0 = 0.2, .t_speed2 = 30, .speed2 = 0.2, .t_stop = -1 };
        result_t r = run(&MOTORS[1], &sc, &ident[1]);
        printf("  Vbus max %.0f V (OV trip 445 V), faults 0x%02x, final err %+.2f%%\n", r.vbus_max, r.faults, r.rpm_err2);
        CHECK(r.faults == 0 && r.vbus_max < 440, "regeneration");
    }

    printf("== 7b. same with almost no air load (kf x 0.05, decel ramp 2 s): regeneration limiter must act\n");
    {
        foc_motor_t idB = ident[1];
        scen_t sc = { .t_end = 50, .t_lock = -1, .th0 = 0.2, .t_speed2 = 30, .speed2 = 0.2,
                      .t_load = 28, .load_k = 0.05, .t_stop = -1, .fast_ramp = true, .csv = csv ? "regen" : NULL };
        result_t r = run(&MOTORS[1], &sc, &idB);
        printf("  Vbus max %.0f V (regen limit 420 V, OV trip 445 V), faults 0x%02x, final err %+.2f%%\n",
               r.vbus_max, r.faults, r.rpm_err2);
        CHECK(r.faults == 0 && r.vbus_max < 440, "regeneration limiter");
    }

    printf("== 9. mains outage at full speed, 500 W battery module, NO message to the drive (bus droop only)\n");
    {
        scen_t sc = { .t_end = 75, .t_lock = -1, .th0 = 0.2, .t_stop = -1,
                      .t_outage = 40, .t_return = 60, .p_module = 500, .csv = csv ? "outage" : NULL };
        result_t r = run(&MOTORS[0], &sc, &ident[0]);
        printf("  during outage: %.0f rpm (%.0f %%), module %.0f W, Vbus min %.0f V | after return %.0f rpm | faults 0x%02x\n",
               r.rpm_outage, r.rpm_outage / 4.5, r.p_mod_avg, r.vbus_min, r.rpm_end, r.faults);
        CHECK(r.faults == 0, "outage without messages faulted");
        CHECK(r.vbus_min > 300, "bus collapsed");
        CHECK(r.rpm_outage > 0.8 * 450 && r.p_mod_avg <= 500.5, "outage operating point");
        CHECK(fabs(r.rpm_end - 450) < 9, "recovery after mains return");
    }

    printf("== 9b. same, application sets battery mode (speed cap 80 %%, power limit 420 W)\n");
    {
        scen_t sc = { .t_end = 75, .t_lock = -1, .th0 = 0.2, .t_stop = -1,
                      .t_outage = 40, .t_return = 60, .p_module = 500, .app_plim = 420, .app_speed = 0.8 };
        result_t r = run(&MOTORS[0], &sc, &ident[0]);
        printf("  during outage: %.0f rpm (%.0f %%), module %.0f W, Vbus min %.0f V | after return %.0f rpm | faults 0x%02x\n",
               r.rpm_outage, r.rpm_outage / 4.5, r.p_mod_avg, r.vbus_min, r.rpm_end, r.faults);
        CHECK(r.faults == 0 && r.vbus_min > 330, "battery mode");
        CHECK(fabs(r.rpm_outage - 0.8 * 450) < 9, "speed cap in battery mode");
        CHECK(fabs(r.rpm_end - 450) < 9, "recovery after mains return");
    }

    printf("== 10. start on battery only (no mains), 500 W module, full speed requested\n");
    {
        scen_t sc = { .t_end = 50, .t_lock = -1, .th0 = 0.7, .t_stop = -1,
                      .t_outage = 0, .t_return = -1, .p_module = 500 };
        result_t r = run(&MOTORS[0], &sc, &ident[0]);
        printf("  final %.0f rpm (%.0f %%), Vbus min %.0f V, faults 0x%02x\n", r.rpm_end, r.rpm_end / 4.5, r.vbus_min, r.faults);
        CHECK(r.faults == 0 && r.rpm_end > 0.8 * 450 && r.vbus_min > 300, "battery-only start");
    }

    printf("== 8. motor not connected / open windings (motor A) -> identification refuses\n");
    {
        motor_t open = MOTORS[0]; open.R = 1e5;  /* behaves like an open circuit for the DC test */
        scen_t sc = { .t_end = 3, .t_lock = -1, .th0 = 0.1, .t_stop = -1 };
        result_t r = run(&open, &sc, NULL);
        printf("  faults 0x%02x\n", r.faults);
        CHECK(r.faults & FOC_F_PHASE_LOSS, "open motor not detected");
    }

    printf("\nFOC test suite: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
