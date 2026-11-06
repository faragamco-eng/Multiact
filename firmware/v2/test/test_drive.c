/*
 * test_drive.c - Host tests of the v2 drive application layer:
 * mains supervision, power-up sequence, over-voltage, outage, alarms and
 * retries, capacitor imbalance, derating, identification + motor library,
 * parameter storage (power-fail safe) and firmware update over Modbus.
 *
 *   make -C firmware/v2 test
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "drive.h"
#include "mains.h"
#include "params.h"
#include "fwu.h"
#include "crc32.h"
#include "motorlib.h"
#include "modbus_slave.h"
#include "cooler_regs.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #c); \
                           printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ------------------------------------------------------------ flash emulation */
#define FL_BASE 0x08000000u
#define FL_SIZE (128u * 1024u)
static uint8_t flash[FL_SIZE];
static int program_budget = -1;      /* >= 0: fail (power loss) after N bytes */
static bool fe(uint32_t a) { memset(flash + (a - FL_BASE), 0xFF, 2048); return true; }
static bool fp(uint32_t a, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    for (uint32_t i = 0; i < n; i++) {
        if (program_budget == 0) return false;
        if (program_budget > 0) program_budget--;
        if (flash[a - FL_BASE + i] != 0xFF) return false;     /* not erased */
        flash[a - FL_BASE + i] = s[i];
    }
    return true;
}
static const uint8_t *fptr(uint32_t a) { return flash + (a - FL_BASE); }
static const flash_if_t FL = { 2048u, fe, fp, fptr };
#define APP_ADDR 0x08004000u
#define STG_ADDR 0x08011800u
#define SLOT     (54u * 1024u)
#define PAR0     0x0801F000u
#define PAR1     0x0801F800u

/* ------------------------------------------------------------ plant */
typedef struct {
    double t;                 /* s */
    double vin_rms, f;        /* mains source; vin_rms = 0 -> outage */
    double vbus, vcap_low_k;  /* vcap_low = vbus/3 * k */
    double t_hs;
    bool   thermal_open;
    /* fake motor control */
    mc_state_t mc;
    uint32_t faults;
    double w, w_target, t_mc;
    bool   locked, oc;
    bool   id_done;
} plant_t;

static drive_t D;
static drive_in_t IN;
static drive_out_t OUT;
static cooler_regs_t REGS;
static params_t PAR;
static params_store_t PST;
static mains_t M;
static plant_t P;
static uint32_t now_ms;
static bool comm_lost;
static int saves;
static double k1_open_t;      /* time K1 opened last */
static bool last_k1;

static void plant_ms(void)
{
    /* mains samples at 16 kHz through the real supervisor */
    for (int k = 0; k < 16; k++) {
        double ts = P.t + k / 16000.0;
        double v = P.vin_rms * 1.41421356 * sin(2 * M_PI * P.f * ts);
        /* line-to-ground channels: each sees one half wave (bridge to PGND) */
        mains_sample(&M, (float)(v > 0 ? v : 0), (float)(v < 0 ? -v : 0));
    }
    double vpk = P.vin_rms * 1.41421356;
    double dt = 1e-3;
    double p_load = (P.mc == MC_RUN) ? 600.0 * (P.w / 900.0) * (P.w / 900.0) * (P.w / 900.0) : 0.0;
    if (OUT.k1 && P.vin_rms > 0) {
        double target = OUT.pfc_on ? 390.0 : vpk * 0.98;
        if (OUT.pfc_on && vpk > 390.0) target = vpk;
        P.vbus += (target - P.vbus) * (OUT.pfc_on ? 0.02 : 0.2);
    } else if (OUT.k2 && P.vin_rms > 0) {
        if (vpk > P.vbus) P.vbus += (vpk - P.vbus) * dt / 0.4;       /* PTC 120 R x 400 uF ~ 0.05 s, slow PTC */
    } else {
        /* capacitor discharge: load + bleeders, C = 400 uF */
        double e = 0.5 * 400e-6 * P.vbus * P.vbus - (p_load + P.vbus * P.vbus / 150e3) * dt;
        P.vbus = e > 0 ? sqrt(2 * e / 400e-6) : 0;
    }
    /* fake motor control */
    if (OUT.mc_clear && P.mc == MC_FAULT) { P.mc = MC_IDLE; P.faults = 0; }
    if (OUT.mc_identify && P.mc == MC_IDLE) { P.mc = MC_IDENTIFY; P.t_mc = 0; }
    P.t_mc += dt;
    switch (P.mc) {
    case MC_IDLE:
        P.w *= 0.999;
        if (OUT.mc_run) { P.mc = MC_STARTING; P.t_mc = 0; }
        break;
    case MC_IDENTIFY:
        if (P.t_mc > 20.0) { P.mc = MC_IDLE; P.id_done = true; }
        break;
    case MC_STARTING:
        if (!OUT.mc_run) { P.mc = MC_IDLE; break; }
        if (P.locked && P.t_mc > 3.0) { P.mc = MC_FAULT; P.faults = MCF_LOCKED; break; }
        if (P.oc && P.t_mc > 0.5) { P.mc = MC_FAULT; P.faults = MCF_OVERCURRENT; break; }
        if (P.t_mc > 2.0 && !P.locked) P.mc = MC_RUN;
        break;
    case MC_RUN:
        if (!OUT.mc_run) { P.mc = MC_IDLE; break; }
        if (P.vbus < 200.0) { P.mc = MC_FAULT; P.faults = MCF_UNDERVOLT; break; }
        P.w_target = OUT.mc_speed * 942.0;
        P.w += (P.w_target - P.w) * 0.002;
        break;
    default: break;
    }
    P.t += dt;
}

static void step(void)
{
    plant_ms();
    IN.now_ms = now_ms;
    IN.mains_valid = M.valid; IN.mains_outage = M.outage; IN.mains_ov_fast = M.ov_fast;
    IN.vin_rms = M.rms; IN.vin_freq = M.freq;
    IN.vbus = (float)P.vbus;
    IN.vcap_low = (float)(P.vbus / 3.0 * P.vcap_low_k);
    IN.t_hs = (float)P.t_hs;
    IN.brk_thermal = P.thermal_open;
    IN.brk_oc = false;
    IN.mc_state = P.mc;
    IN.mc_faults = P.faults;
    IN.w_el = (float)P.w;
    IN.i_rms = P.mc == MC_RUN ? 3.0f : 0.0f;
    IN.p_motor = P.mc == MC_RUN ? (float)(600.0 * pow(P.w / 900.0, 3)) : 0.0f;
    IN.id_valid = P.id_done;
    IN.R = 1.62f; IN.Ld = 15.3e-3f; IN.Lq = 14.8e-3f; IN.psi = 0.157f; IN.Je = 0.005f; IN.vdt = 4.0f;
    IN.comm_lost = comm_lost;
    drive_tick_1ms(&D, &IN, &OUT);
    if (OUT.mains_ack_ov) mains_clear_ov(&M);
    if (OUT.save) { saves++; CHECK(params_save(&PST, &PAR), "save"); }
    if (last_k1 && !OUT.k1) k1_open_t = P.t;
    last_k1 = OUT.k1;
    now_ms++;
}

static void run_s(double s) { for (long i = 0; i < (long)(s * 1000); i++) step(); }

static void boot(void)
{
    memset(&P, 0, sizeof P);
    P.vin_rms = 230; P.f = 50; P.vcap_low_k = 1.0; P.t_hs = 45;
    now_ms = 1;
    comm_lost = false;
    mains_init(&M, 16000.0f, 290.0f * 1.4142f);
    cooler_regs_init(&REGS);
    if (!params_load(&PST, &FL, PAR0, PAR1, &PAR)) params_defaults(&PAR);
    drive_cfg_t cfg;
    drive_default_cfg(&cfg);
    drive_init(&D, &cfg, &REGS, &PAR);
    memset(&OUT, 0, sizeof OUT);
}

static void set_hr(uint16_t a, uint16_t v) { CHECK(cooler_write_holding(a, v, &REGS) == 0, "write %u", a); }

/* ------------------------------------------------------------ tests */
static void t_mains(void)
{
    printf("mains supervisor\n");
    mains_t m;
    mains_init(&m, 16000.0f, 410.0f);
    for (int i = 0; i < 16000; i++) {
        double v = 230 * 1.41421356 * sin(2 * M_PI * 49.7 * i / 16000.0);
        mains_sample(&m, (float)(v > 0 ? v : 0), (float)(v < 0 ? -v : 0));
    }
    CHECK(fabsf(m.rms - 230) < 1.5f, "rms %.1f", m.rms);
    CHECK(fabsf(m.freq - 49.7f) < 0.1f, "f %.2f", m.freq);
    CHECK(!m.outage && !m.ov_fast, "flags");
    printf("  rms %.1f V, f %.2f Hz\n", m.rms, m.freq);
    /* over-voltage step to 440 V: detection time */
    int n = 0;
    for (; n < 400 && !m.ov_fast; n++) {
        double v = 440 * 1.41421356 * sin(2 * M_PI * 50 * (16000 + n) / 16000.0);
        mains_sample(&m, (float)(v > 0 ? v : 0), (float)(v < 0 ? -v : 0));
    }
    CHECK(m.ov_fast && n <= 160, "OV detected after %d samples (%.1f ms)", n, n / 16.0);
    printf("  440 V detected in %.1f ms\n", n / 16.0);
    for (int i = 0; i < 800; i++) mains_sample(&m, 0, 0);
    CHECK(m.outage, "outage after 50 ms");
}

static void t_powerup(void)
{
    printf("power-up, run, PFC idle-off\n");
    memset(flash, 0xFF, sizeof flash);
    boot();
    run_s(1.0);
    CHECK(!OUT.k1 && !OUT.k2, "relays open while mains is qualified");
    run_s(1.5);
    CHECK(OUT.k2 && !OUT.k1, "precharge after 2 s");
    run_s(3.0);
    CHECK(OUT.k1 && !OUT.k2 && !OUT.pfc_on, "K1 closed, PFC off in standby (k1 %d k2 %d pfc %d, vbus %.0f)", OUT.k1, OUT.k2, OUT.pfc_on, P.vbus);
    set_hr(HR_RUN, 1);
    set_hr(HR_SPEED_PERMILLE, 800);
    run_s(5.0);
    CHECK(OUT.pfc_on && P.vbus > 380, "PFC on while running (vbus %.0f)", P.vbus);
    CHECK(P.mc == MC_RUN && REGS.ir[IR_STATE] == DS_RUNNING, "running (mc %d state %u)", P.mc, REGS.ir[IR_STATE]);
    set_hr(HR_PUMP, 1);
    run_s(0.01);
    CHECK(OUT.pump, "pump relay");
    set_hr(HR_RUN, 0);
    run_s(31.0);
    CHECK(!OUT.pfc_on && OUT.k1, "PFC off 30 s after stop");
    CHECK(REGS.ir[IR_ALARM] == 0, "no alarm (%u)", REGS.ir[IR_ALARM]);
}

static void t_overvoltage(void)
{
    printf("440 VAC neutral loss while running\n");
    boot();
    set_hr(HR_RUN, 1);
    set_hr(HR_SPEED_PERMILLE, 1000);
    run_s(10.0);
    CHECK(P.mc == MC_RUN && OUT.k1, "running before");
    double t0 = P.t;
    P.vin_rms = 440;
    run_s(0.05);
    CHECK(!OUT.k1 && !OUT.k2 && !OUT.pump, "relays open");
    printf("  K1 released %.1f ms after the over-voltage began\n", (k1_open_t - t0) * 1e3);
    CHECK(k1_open_t - t0 < 0.012, "within a half cycle + margin (%.1f ms)", (k1_open_t - t0) * 1e3);
    run_s(2.0);
    CHECK(REGS.ir[IR_ALARM] == AL_VIN_HIGH, "E1 shown (%u)", REGS.ir[IR_ALARM]);
    CHECK(P.mc != MC_RUN, "motor stopped");
    run_s(60.0);
    CHECK(!OUT.k1, "stays open during 60 s of 440 V");
    P.vin_rms = 225;
    run_s(12.0);
    CHECK(OUT.k1, "reconnected after mains returns");
    run_s(10.0);
    CHECK(P.mc == MC_RUN && REGS.ir[IR_ALARM] == 0, "running again automatically (mc %d alarm %u)", P.mc, REGS.ir[IR_ALARM]);
}

static void t_outage(void)
{
    printf("outage and automatic restart\n");
    boot();
    set_hr(HR_RUN, 1);
    run_s(10.0);
    uint32_t out0 = PAR.outages;
    int s0 = saves;
    P.vin_rms = 0;
    run_s(0.1);
    CHECK(!OUT.k1 && REGS.ir[IR_POWER_STATE] == PWR_OUTAGE, "outage detected, relays open");
    run_s(3.0);
    CHECK(PAR.outages == out0 + 1 && saves > s0, "counted and saved (outages %u saves %d)", PAR.outages, saves - s0);
    /* real power loss: reboot from flash */
    boot();
    CHECK(PAR.outages == out0 + 1 && PAR.last_run == 1, "state survives reboot");
    CHECK(REGS.hr[HR_RUN] == 1, "resumes (restore-on-power)");
    run_s(10.0);
    CHECK(P.mc == MC_RUN, "running after power return");
}

static void t_locked(void)
{
    printf("locked rotor: retries 10/30/60 s then latched E4\n");
    boot();
    P.locked = true;
    set_hr(HR_RUN, 1);
    double t_fault[5] = {0};
    int nf = 0;
    mc_state_t last = P.mc;
    for (long i = 0; i < 160000 && nf < 5; i++) {
        step();
        if (P.mc == MC_FAULT && last != MC_FAULT) t_fault[nf++] = P.t;
        last = P.mc;
    }
    CHECK(nf == 4, "4 attempts (%d)", nf);
    printf("  attempts at %.1f %.1f %.1f %.1f s\n", t_fault[0], t_fault[1], t_fault[2], t_fault[3]);
    CHECK(fabs(t_fault[1] - t_fault[0] - 13.0) < 1.0 && fabs(t_fault[2] - t_fault[1] - 33.0) < 1.0 &&
          fabs(t_fault[3] - t_fault[2] - 63.0) < 1.0, "schedule");
    CHECK(REGS.ir[IR_ALARM] == AL_LOCKED_ROTOR && REGS.ir[IR_STATE] == DS_ALARM &&
          (REGS.ir[IR_ALARM_DETAIL] & AD_LATCHED), "latched E4");
    run_s(120);
    CHECK(P.mc != MC_STARTING, "no further attempts");
    CHECK(REGS.ir[IR_LOG_COUNT] >= 4 && REGS.log[0][0] == AL_LOCKED_ROTOR, "logged");
    P.locked = false;
    set_hr(HR_CLEAR_ALARM, 1);
    run_s(5);
    CHECK(P.mc == MC_RUN && REGS.ir[IR_ALARM] == 0, "runs after acknowledge");
}

static void t_overcurrent(void)
{
    printf("over-current: 3 retries 10 s, latched E3\n");
    boot();
    P.oc = true;
    set_hr(HR_RUN, 1);
    run_s(60);
    CHECK(REGS.ir[IR_ALARM] == AL_OVERCURRENT && (REGS.ir[IR_ALARM_DETAIL] & AD_LATCHED), "latched E3 (%u/%u)",
          REGS.ir[IR_ALARM], REGS.ir[IR_ALARM_DETAIL]);
}

static void t_cap_imbalance(void)
{
    printf("series capacitor imbalance\n");
    boot();
    set_hr(HR_RUN, 1);
    run_s(10);
    P.vcap_low_k = 1.25;                     /* lowest cap carries 25 % too much */
    run_s(3);
    CHECK(REGS.ir[IR_ALARM] == AL_BUS_PFC && (REGS.ir[IR_ALARM_DETAIL] & 0x7F) == AD_CAP_IMBALANCE, "E9 imbalance");
    CHECK(!OUT.k1 && !OUT.k2 && P.mc != MC_RUN, "relays open, motor stopped");
    run_s(30);
    CHECK(!OUT.k1 && !OUT.k2, "no reconnection while latched");
}

static void t_comm_and_temp(void)
{
    printf("panel lost, heatsink temperature, thermal switch\n");
    boot();
    set_hr(HR_RUN, 1);
    set_hr(HR_PUMP, 1);
    run_s(10);
    comm_lost = true;
    run_s(0.1);
    CHECK(REGS.ir[IR_ALARM] == AL_COMM_LOST && !OUT.pump && !OUT.mc_run, "E8: fan and pump off");
    comm_lost = false;
    run_s(5);
    CHECK(REGS.ir[IR_ALARM] == 0 && OUT.pump && P.mc == MC_RUN, "E8 clears itself");
    set_hr(HR_SPEED_PERMILLE, 500);
    P.t_hs = 92;
    run_s(30);
    CHECK(fabsf(OUT.mc_speed - 0.5f * (1.0f - 0.6f * 7.0f / 15.0f)) < 0.01f && OUT.mc_run, "derated at 92 C (speed %.2f)", OUT.mc_speed);
    P.t_hs = 101;
    run_s(0.1);
    CHECK(REGS.ir[IR_ALARM] == AL_DRIVE_HOT && !OUT.mc_run, "E6 stop at 101 C");
    P.t_hs = 82;
    run_s(1);
    CHECK(!OUT.mc_run, "hysteresis");
    P.t_hs = 75;
    run_s(5);
    CHECK(OUT.mc_run, "resumes below 80 C");
    P.thermal_open = true;
    run_s(0.1);
    CHECK(REGS.ir[IR_ALARM] == AL_DRIVE_HOT && REGS.ir[IR_ALARM_DETAIL] == AD_HS_SWITCH, "thermal switch");
    P.thermal_open = false;
    run_s(5);
    CHECK(OUT.mc_run, "resumes after switch closes");
}

static void t_identify(void)
{
    printf("identification + motor library + persistence\n");
    boot();
    run_s(6);
    set_hr(HR_IDENTIFY, 1);
    run_s(25);
    CHECK(PAR.motor_valid && PAR.motor_id == 0, "matched library entry 0 (id %u)", PAR.motor_id);
    CHECK(REGS.ir[IR_R_MOHM] == 1620 && REGS.ir[IR_FLUX_UVS] == 1570, "IR R %u psi %u", REGS.ir[IR_R_MOHM], REGS.ir[IR_FLUX_UVS]);
    set_hr(HR_RUN, 1);
    set_hr(HR_SPEED_PERMILLE, 1000);
    run_s(30);
    double rpm_expect = OUT.mc_w_max / (2 * M_PI) * 60 / 20 * (P.w / OUT.mc_w_max);
    CHECK(fabs(OUT.mc_w_max - 450 * 20 * 2 * M_PI / 60) < 1, "w_max from library (%.0f)", OUT.mc_w_max);
    CHECK(fabs(REGS.ir[IR_SPEED_RPM] - rpm_expect) < 2, "rpm shown %u expect %.0f", REGS.ir[IR_SPEED_RPM], rpm_expect);
    run_s(12);
    boot();
    CHECK(PAR.motor_valid && fabsf(PAR.psi - 0.157f) < 1e-6f, "motor data survives reboot");
    motor_match_t u = motorlib_match(0.7f, 30e-3f, 0.30f, 390.0f);
    CHECK(u.index < 0 && u.i_limit > 0 && u.w_max > 0, "unknown motor -> conservative profile");
}

static void t_params(void)
{
    printf("parameter storage (wear levelling, power fail)\n");
    memset(flash, 0xFF, sizeof flash);
    params_store_t s;
    params_t p, q;
    params_defaults(&p);
    CHECK(!params_load(&s, &FL, PAR0, PAR1, &q), "blank");
    for (uint32_t k = 1; k <= 40; k++) {
        p.energy_wh = k;
        CHECK(params_save(&s, &p), "save %u", k);
    }
    params_load(&s, &FL, PAR0, PAR1, &q);
    CHECK(q.energy_wh == 40, "newest after page rollovers (%u)", q.energy_wh);
    p.energy_wh = 41;
    program_budget = 100;                    /* power fails in the middle of the record */
    params_save(&s, &p);
    program_budget = -1;
    params_load(&s, &FL, PAR0, PAR1, &q);
    CHECK(q.energy_wh == 40, "torn write ignored (%u)", q.energy_wh);
    p.energy_wh = 42;
    CHECK(params_save(&s, &p), "save after torn write");
    params_load(&s, &FL, PAR0, PAR1, &q);
    CHECK(q.energy_wh == 42, "continues (%u)", q.energy_wh);
    printf("  record %u bytes, %u per page\n", params_record_size(), 2048u / params_record_size());
}

/* firmware update through the real Modbus slave */
static mb_slave_t MB;
static fwu_t U;
static bool motor_running;
static bool busy(void *ctx) { (void)ctx; return motor_running; }
static uint8_t ext_w(uint16_t a, uint16_t v, void *ctx) { return fwu_write_reg(ctx, a, v); }
static uint8_t ext_r(uint16_t a, uint16_t *v, bool in, void *ctx) { return fwu_read_reg(ctx, a, v, in); }

static int mb_tx(const uint8_t *req, uint16_t n, uint8_t *rsp)
{
    uint8_t f[MB_BUF];
    memcpy(f, req, n);
    uint16_t c = mb_crc16(f, n);
    f[n] = (uint8_t)c; f[n + 1] = (uint8_t)(c >> 8);
    for (uint16_t i = 0; i < n + 2; i++) mb_rx_byte(&MB, f[i]);
    if (!mb_rx_gap(&MB)) return 0;
    memcpy(rsp, MB.tx, MB.tx_len);
    return MB.tx_len;
}
static bool wr_regs(uint16_t a, const uint16_t *v, uint16_t n)
{
    uint8_t f[MB_BUF], r[MB_BUF];
    f[0] = 1; f[1] = 0x10; f[2] = (uint8_t)(a >> 8); f[3] = (uint8_t)a; f[4] = 0; f[5] = (uint8_t)n; f[6] = (uint8_t)(2 * n);
    for (uint16_t i = 0; i < n; i++) { f[7 + 2 * i] = (uint8_t)(v[i] >> 8); f[8 + 2 * i] = (uint8_t)v[i]; }
    int len = mb_tx(f, (uint16_t)(7 + 2 * n), r);
    return len > 0 && r[1] == 0x10;
}
static uint16_t rd_ir(uint16_t a)
{
    uint8_t f[6] = {1, 4, (uint8_t)(a >> 8), (uint8_t)a, 0, 1}, r[MB_BUF];
    int len = mb_tx(f, 6, r);
    return len >= 5 ? (uint16_t)(r[3] << 8 | r[4]) : 0xFFFF;
}

static void make_image(uint8_t *img, uint32_t size, uint16_t ver)
{
    for (uint32_t i = 0; i < size; i++) img[i] = (uint8_t)(i * 7 + ver);
    img_info_t h = { IMG_MAGIC, IMG_HW_ID, ver, size, 0 };
    memcpy(img + IMG_INFO_OFS, &h, sizeof h);
    uint32_t cpos = IMG_INFO_OFS + 12;
    uint32_t c = crc32_update(0, img, cpos);
    c = crc32_update(c, img + cpos + 4, size - cpos - 4);
    memcpy(img + cpos, &c, 4);
}

static void t_fwu(void)
{
    printf("firmware update over Modbus + bootloader decision\n");
    memset(flash, 0xFF, sizeof flash);
    static uint8_t img[SLOT];
    uint32_t size = 41234;
    make_image(img, size, 0x0201);
    cooler_regs_init(&REGS);
    fwu_init(&U, &FL, STG_ADDR, SLOT, busy, 0);
    REGS.ext_write = ext_w; REGS.ext_read = ext_r; REGS.ext_ctx = &U;
    static const mb_regs_t CB = { cooler_read_holding, cooler_read_input, cooler_write_holding, &REGS };
    mb_init(&MB, 1, &CB);

    uint32_t crc = crc32_update(0, img, size);
    uint16_t hdr[5] = { 0, (uint16_t)size, (uint16_t)(size >> 16), (uint16_t)crc, (uint16_t)(crc >> 16) };
    motor_running = true;
    CHECK(wr_regs(HR_FWU_SIZE_LO, hdr + 1, 4), "header");
    uint16_t cmd = FWU_CMD_BEGIN;
    CHECK(!wr_regs(HR_FWU_CMD, &cmd, 1), "refused while motor runs");
    motor_running = false;
    CHECK(wr_regs(HR_FWU_CMD, &cmd, 1), "begin");
    while (U.state == FWU_ERASING) fwu_poll(&U);
    CHECK(rd_ir(IR_FWU_STATE) == FWU_RECEIVING, "receiving");
    int frames = 0;
    for (uint32_t ofs = 0; ofs < size; ofs += 96) {
        uint16_t blk[2 + FWU_DATA_REGS];
        blk[0] = (uint16_t)ofs; blk[1] = (uint16_t)(ofs >> 16);
        for (unsigned k = 0; k < FWU_DATA_REGS; k++) {
            uint32_t i = ofs + 2 * k;
            uint8_t b0 = i < size ? img[i] : 0xFF, b1 = i + 1 < size ? img[i + 1] : 0xFF;
            blk[2 + k] = (uint16_t)(b0 << 8 | b1);
        }
        CHECK(wr_regs(HR_FWU_OFS_LO, blk, 2 + FWU_DATA_REGS), "chunk %u", ofs);
        if (ofs == 960) CHECK(wr_regs(HR_FWU_OFS_LO, blk, 2 + FWU_DATA_REGS), "repeated chunk accepted");
        frames++;
    }
    cmd = FWU_CMD_COMMIT;
    CHECK(wr_regs(HR_FWU_CMD, &cmd, 1), "commit (error %u)", U.error);
    CHECK(U.state == FWU_READY && U.reboot_req, "ready, reboot requested");
    printf("  %u bytes in %d frames (%.1f min at 2400 bps)\n", size, frames, frames * (9 + 2 + 100 + 8) * 10 / 2400.0 / 60.0);
    CHECK(boot_decide(fptr(APP_ADDR), fptr(STG_ADDR), SLOT) == BOOT_COPY, "bootloader copies");
    memcpy(flash + (APP_ADDR - FL_BASE), flash + (STG_ADDR - FL_BASE), SLOT);
    CHECK(boot_decide(fptr(APP_ADDR), fptr(STG_ADDR), SLOT) == BOOT_APP, "then starts the application");
    flash[APP_ADDR - FL_BASE + 5000] ^= 1;   /* copy interrupted / corrupted */
    CHECK(boot_decide(fptr(APP_ADDR), fptr(STG_ADDR), SLOT) == BOOT_COPY, "repairs a damaged application");
    flash[STG_ADDR - FL_BASE + 5000] ^= 1;
    CHECK(boot_decide(fptr(APP_ADDR), fptr(STG_ADDR), SLOT) == BOOT_NONE, "both damaged -> stay in bootloader");
    /* wrong CRC is rejected */
    cmd = FWU_CMD_BEGIN;
    hdr[3] ^= 1;
    wr_regs(HR_FWU_SIZE_LO, hdr + 1, 4);
    wr_regs(HR_FWU_CMD, &cmd, 1);
    while (U.state == FWU_ERASING) fwu_poll(&U);
    for (uint32_t ofs = 0; ofs < size; ofs += 96) {
        uint16_t blk[2 + FWU_DATA_REGS];
        blk[0] = (uint16_t)ofs; blk[1] = (uint16_t)(ofs >> 16);
        for (unsigned k = 0; k < FWU_DATA_REGS; k++) blk[2 + k] = 0x1234;
        wr_regs(HR_FWU_OFS_LO, blk, 2 + FWU_DATA_REGS);
    }
    cmd = FWU_CMD_COMMIT;
    CHECK(!wr_regs(HR_FWU_CMD, &cmd, 1) && rd_ir(IR_FWU_ERROR) == FWE_CRC, "bad image rejected");
}

int main(void)
{
    t_mains();
    t_params();
    t_powerup();
    t_overvoltage();
    t_outage();
    t_locked();
    t_overcurrent();
    t_cap_imbalance();
    t_comm_and_temp();
    memset(flash, 0xFF, sizeof flash);
    t_identify();
    t_fwu();
    printf("drive v2 tests: %s (%d checks, %d failed)\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails != 0;
}
