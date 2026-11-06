/*
 * test_panel.c - Panel core against a simulated drive that uses the real
 * drive-side code (cooler_regs, modbus_slave, fwu + bootloader decision).
 * The link delivers each reply after its 2400 bps transmission time.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "panel.h"
#include "drive_update.h"
#include "json_api.h"
#include "seg7.h"
#include "mb_master.h"
#include "modbus_slave.h"
#include "cooler_regs.h"
#include "fwu.h"
#include "crc32.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL line %d: %s  ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---------------------------------------------------------------- drive */
static cooler_regs_t R;
static mb_slave_t MB;
static fwu_t FW;
static bool drive_silent;
static uint32_t drive_reboot_until;
#define FL_BASE 0x08000000u
static uint8_t flash[128 * 1024];
static bool fe(uint32_t a) { memset(flash + (a - FL_BASE), 0xFF, 2048); return true; }
static bool fp(uint32_t a, const void *s, uint32_t n) { memcpy(flash + (a - FL_BASE), s, n); return true; }
static const uint8_t *fptr(uint32_t a) { return flash + (a - FL_BASE); }
static const flash_if_t FL = { 2048u, fe, fp, fptr };
static bool motor_busy(void *c) { (void)c; return R.ir[IR_STATE] == DS_RUNNING; }
static uint8_t ext_w(uint16_t a, uint16_t v, void *c) { return fwu_write_reg(c, a, v); }
static uint8_t ext_r(uint16_t a, uint16_t *v, bool in, void *c) { return fwu_read_reg(c, a, v, in); }
static const mb_regs_t CB = { cooler_read_holding, cooler_read_input, cooler_write_holding, &R };
static int clear_seen, ident_seen;

static void drive_init(void)
{
    cooler_regs_init(&R);
    R.ir[IR_FW_VER] = 0x0200;
    R.ir[IR_SERIAL_LO] = 4242;
    mb_init(&MB, 1, &CB);
    memset(flash, 0xFF, sizeof flash);
    fwu_init(&FW, &FL, 0x08011800u, 54u * 1024u, motor_busy, 0);
    R.ext_write = ext_w; R.ext_read = ext_r; R.ext_ctx = &FW;
}
static void drive_ms(uint32_t now)
{
    fwu_poll(&FW);
    if (R.clear_req) { R.clear_req = false; clear_seen++; R.ir[IR_ALARM] = 0; }
    if (R.identify_req) { R.identify_req = false; ident_seen++; }
    R.ir[IR_STATE] = R.hr[HR_RUN] ? DS_RUNNING : DS_STANDBY;
    R.ir[IR_SPEED_RPM] = R.hr[HR_RUN] ? (uint16_t)(R.hr[HR_SPEED_PERMILLE] * 450u / 1000u) : 0u;
    R.ir[IR_PUMP_STATE] = R.hr[HR_PUMP];
    if (FW.reboot_req) {                     /* reset -> bootloader copies -> new application */
        FW.reboot_req = false;
        if (boot_decide(fptr(0x08004000u), fptr(0x08011800u), 54u * 1024u) == BOOT_COPY) {
            memcpy(flash + 0x4000, flash + 0x11800, 54u * 1024u);
            img_info_t h;
            img_check(fptr(0x08004000u), 54u * 1024u, &h);
            R.ir[IR_FW_VER] = h.version;
        }
        drive_reboot_until = now + 1200u;
        FW.state = FWU_IDLE;
    }
}

/* ---------------------------------------------------------------- link */
static panel_t P;
static uint32_t now;
static uint8_t rsp[256];
static uint16_t rsp_len;
static uint32_t rsp_at;
static char events[64][160];
static int nev;

static void link_send(void *ctx, const uint8_t *f, uint16_t n)
{
    (void)ctx;
    if (drive_silent || now < drive_reboot_until) return;
    for (uint16_t i = 0; i < n; i++) mb_rx_byte(&MB, f[i]);
    if (mb_rx_gap(&MB)) {
        memcpy(rsp, MB.tx, MB.tx_len);
        rsp_len = MB.tx_len;
        rsp_at = now + (uint32_t)(n + MB.tx_len) * 10000u / 2400u + 20u;   /* both frames + turnaround */
    }
}
static void on_event(void *ctx, const char *js) { (void)ctx; if (nev < 64) snprintf(events[nev++], 160, "%s", js); }
static bool has_event(const char *s) { for (int i = 0; i < nev; i++) if (strstr(events[i], s)) return true; return false; }

static uint16_t mow = 0xFFFF;
static void run_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        now++;
        drive_ms(now);
        if (rsp_len && now >= rsp_at) { uint16_t n = rsp_len; rsp_len = 0; panel_rx_frame(&P, rsp, n); }
        panel_tick(&P, now, mow);
    }
}
static void run_s(double s) { run_ms((uint32_t)(s * 1000)); }

/* ---------------------------------------------------------------- tests */
static void t_sync_and_sequences(void)
{
    printf("startup sync, dry-out, pre-wet, keys\n");
    drive_init();
    R.hr[HR_RUN] = 1; R.hr[HR_PUMP] = 1; R.hr[HR_SPEED_PERMILLE] = 845;     /* running after power return */
    panel_init(&P, link_send, on_event, 0);
    run_s(3);
    CHECK(P.power && P.mode == MODE_COOL && P.step == 8, "adopted drive state (power %d mode %d step %u)", P.power, P.mode, P.step);
    CHECK(R.hr[HR_RUN] == 1 && R.hr[HR_PUMP] == 1, "drive undisturbed");
    CHECK(P.ir[IR_SPEED_RPM] > 0, "status polled");

    panel_key(&P, KEY_POWER, false);                         /* off -> dry-out */
    run_s(3);
    CHECK(R.hr[HR_PUMP] == 0 && R.hr[HR_RUN] == 1 && R.hr[HR_SPEED_PERMILLE] == panel_step_to_permille(3), "dry-out: pump off, fan low");
    ui_view_t v;
    panel_view(&P, &v);
    CHECK(v.seg[0] == 'd', "display shows dry-out (%s)", v.seg);
    run_s(300);
    CHECK(R.hr[HR_RUN] == 0 && R.hr[HR_PUMP] == 0, "fan off after 5 min");

    panel_key(&P, KEY_POWER, false);                         /* on within 20 min: pads still wet */
    run_s(3);
    CHECK(R.hr[HR_RUN] == 1 && R.hr[HR_PUMP] == 1, "no pre-wet when pads are wet");
    panel_set_power(&P, false);
    run_s(320);
    run_s(1300);                                             /* pads dry now */
    panel_key(&P, KEY_POWER, false);
    run_s(3);
    CHECK(R.hr[HR_RUN] == 0 && R.hr[HR_PUMP] == 1, "pre-wet: pump only");
    panel_view(&P, &v);
    CHECK(v.seg[0] == 'P' && v.seg[1] == '2', "display P2 (%s)", v.seg);
    char d4[5];
    seg7_text(&v, d4);
    CHECK(!strcmp(d4, "Pr 2") && seg7_char('P') == 0x73 && (seg7_leds(&v) & SEG_B) && !(seg7_leds(&v) & SEG_C),
          "4-digit text and LEDs during pre-wet (%s)", d4);
    run_s(120);
    CHECK(R.hr[HR_RUN] == 1 && R.hr[HR_PUMP] == 1, "fan starts after 2 min");

    panel_key(&P, KEY_UP, false); panel_key(&P, KEY_UP, false); panel_key(&P, KEY_UP, false);
    run_s(2);
    CHECK(P.step == 10 && R.hr[HR_SPEED_PERMILLE] == 1000, "speed keys (step %u, %u)", P.step, R.hr[HR_SPEED_PERMILLE]);
    panel_view(&P, &v);
    seg7_text(&v, d4);
    CHECK(!strcmp(d4, "CO10"), "4-digit text running (%s)", d4);
    panel_key(&P, KEY_MODE, false);                          /* cool -> fan */
    run_s(2);
    CHECK(R.hr[HR_PUMP] == 0 && R.hr[HR_RUN] == 1, "fan-only mode");
    panel_key(&P, KEY_MODE, false);                          /* fan -> pump */
    run_s(2);
    CHECK(R.hr[HR_PUMP] == 1 && R.hr[HR_RUN] == 0, "pump-only mode");
    panel_key(&P, KEY_MODE, false);                          /* back to cool */
    panel_key(&P, KEY_TIMER, false);                         /* 1 h off timer */
    run_s(2);
    panel_view(&P, &v);
    CHECK(v.led_timer && v.timer_left_min == 60, "timer 60 min (%u)", v.timer_left_min);
    run_s(3600);
    CHECK(!P.power && has_event("timer_off"), "timer switched off");
    run_s(310);
    CHECK(R.hr[HR_RUN] == 0, "and dried out");
}

static void t_alarm_comm_json(void)
{
    printf("alarms, comm loss, JSON API, schedule, log, warranty\n");
    R.ir[IR_ALARM] = AL_LOCKED_ROTOR;
    R.ir[IR_ALARM_DETAIL] = AD_LATCHED;
    nev = 0;
    run_s(2);
    ui_view_t v;
    panel_view(&P, &v);
    CHECK(has_event("\"name\":\"E4\"") && !strcmp(v.seg, "E4") && v.blink, "alarm event + E4 (%s)", v.seg);
    char d4[5];
    seg7_text(&v, d4);
    CHECK(!strcmp(d4, "E- 4"), "4-digit alarm text (%s)", d4);
    panel_key(&P, KEY_MODE, true);                           /* long MODE = acknowledge */
    run_s(2);
    CHECK(clear_seen == 1 && has_event("alarm_cleared"), "acknowledged");

    drive_silent = true;
    run_s(7);
    panel_view(&P, &v);
    CHECK(!strcmp(v.seg, "--") && has_event("drive_offline"), "offline shown (%s)", v.seg);
    drive_silent = false;
    run_s(3);
    CHECK(P.comm_ok && has_event("drive_online"), "back online");

    char out[2048];
    panel_ident_t id = { "P-000123", "1.0.0" };
    json_handle(&P, "{\"cmd\":\"set\",\"power\":1,\"mode\":\"fan\",\"speed\":4}", out, sizeof out, &id);
    run_s(2);
    CHECK(strstr(out, "\"ok\":1") && R.hr[HR_RUN] == 1 && R.hr[HR_PUMP] == 0 &&
          R.hr[HR_SPEED_PERMILLE] == panel_step_to_permille(4), "JSON set (%s)", out);
    json_handle(&P, "{\"cmd\":\"set\",\"speed\":14}", out, sizeof out, &id);
    CHECK(strstr(out, "\"ok\":0"), "range checked");
    json_handle(&P, "{\"cmd\":\"identify\"}", out, sizeof out, &id);
    CHECK(strstr(out, "off first"), "identify refused while running");
    json_handle(&P, "{\"cmd\":\"get\"}", out, sizeof out, &id);
    CHECK(strstr(out, "\"type\":\"state\"") && strstr(out, "\"mode\":\"fan\"") && strstr(out, "\"speed\":4"), "state (%s)", out);

    /* weekly schedule: Monday (bit 2) 07:30 -> 14:00 cool speed 6 */
    json_handle(&P, "{\"cmd\":\"set\",\"power\":0}", out, sizeof out, &id);
    run_s(2);
    json_handle(&P, "{\"cmd\":\"schedule\",\"i\":0,\"en\":1,\"days\":4,\"on\":450,\"off\":840,\"mode\":\"cool\",\"speed\":6}",
                out, sizeof out, &id);
    CHECK(strstr(out, "\"ok\":1"), "schedule stored");
    mow = 2 * 1440 + 449; run_s(61);
    CHECK(!P.power, "not before 07:30");
    mow = 2 * 1440 + 450; run_s(2);
    CHECK(P.power && P.mode == MODE_COOL && P.step == 6, "on at 07:30");
    mow = 2 * 1440 + 840; run_s(2);
    CHECK(!P.power, "off at 14:00");
    mow = 0xFFFF;

    /* event log: drive logs two events -> panel reads the log */
    uint16_t a[8] = { AL_LOCKED_ROTOR, AD_LATCHED, 600, 0, 228, 390, 310, 72 };
    uint16_t b[8] = { AL_VIN_HIGH, 0, 700, 0, 440, 395, 0, 65 };
    cooler_log(&R, a); cooler_log(&R, b);
    nev = 0;
    run_s(25);
    json_log(&P, out, sizeof out);
    CHECK(has_event("log_updated") && strstr(out, "{\"code\":1,") && strstr(out, "{\"code\":4,"), "log (%s)", out);
    json_handle(&P, "{\"cmd\":\"warranty\",\"note\":\"fan stops after 10 min\"}", out, sizeof out, &id);
    int depth = 0, mind = 0;
    for (char *c = out; *c; c++) { depth += (*c == '{' || *c == '[') - (*c == '}' || *c == ']'); if (depth < mind) mind = depth; }
    CHECK(strstr(out, "\"drive_serial\":4242") && strstr(out, "\"note\":\"fan stops after 10 min\"") &&
          strstr(out, "\"log\":[{") && depth == 0 && mind == 0, "warranty report (%s)", out);
}

static bool verify_ok(const uint8_t *img, uint32_t n, const uint8_t *sig) { (void)img; (void)n; return sig && sig[0] == 0xA5; }

static void make_image(uint8_t *img, uint32_t size, uint16_t ver)
{
    for (uint32_t i = 0; i < size; i++) img[i] = (uint8_t)(i * 13 + 1);
    img_info_t h = { IMG_MAGIC, IMG_HW_ID, ver, size, 0 };
    memcpy(img + IMG_INFO_OFS, &h, sizeof h);
    uint32_t cpos = IMG_INFO_OFS + 12;
    uint32_t c = crc32_update(0, img, cpos);
    c = crc32_update(c, img + cpos + 4, size - cpos - 4);
    memcpy(img + cpos, &c, 4);
}

static void t_update(void)
{
    printf("drive firmware update through the panel\n");
    static uint8_t img[30008];
    make_image(img, sizeof img, 0x0201);
    drive_update_t u;
    uint8_t bad_sig[1] = {0}, sig[1] = {0xA5};
    CHECK(!drive_update_start(&P, &u, img, sizeof img, verify_ok, bad_sig) && u.err == DUE_SIGNATURE, "bad signature refused");
    panel_set_power(&P, true);
    panel_set_mode(&P, MODE_FAN);
    run_s(3);
    CHECK(R.hr[HR_RUN] == 1, "running before the update");
    uint32_t t0 = now;
    CHECK(drive_update_start(&P, &u, img, sizeof img, verify_ok, sig), "start");
    ui_view_t v;
    int pct50 = 0;
    while (u.st != DU_DONE && u.st != DU_FAILED && now - t0 < 600000u) {
        run_ms(100);
        panel_view(&P, &v);
        if (v.update_pct >= 50 && !pct50) { pct50 = 1; CHECK(!strcmp(v.seg, "UP"), "display UP"); }
    }
    printf("  result %d (err %u) after %.1f min, drive fw %04x\n", u.st, u.err, (now - t0) / 60000.0, R.ir[IR_FW_VER]);
    CHECK(u.st == DU_DONE && R.ir[IR_FW_VER] == 0x0201, "updated");
    CHECK(R.hr[HR_RUN] == 0, "motor was stopped for the update");
    run_s(5);
    CHECK(P.hr_synced && P.comm_ok && P.ir[IR_FW_VER] == 0x0201, "panel resynchronised");
}

int main(void)
{
    t_sync_and_sequences();
    t_alarm_comm_json();
    t_update();
    printf("panel tests: %s (%d checks, %d failed)\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails != 0;
}
