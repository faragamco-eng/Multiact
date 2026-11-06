/*
 * panel.c - Portable core of the cooler panel firmware. See panel.h.
 */
#include "panel.h"
#include "drive_update.h"
#include "mb_master.h"
#include <stdio.h>
#include <string.h>

enum { K_NONE = 0, K_HR_SYNC, K_WRITE, K_CLEAR, K_IDENT, K_FAST, K_SLOW, K_LOG, K_UPD };
#define BYTE_MS      (10000u / 2400u + 1u)      /* 4.2 ms per character at 2400 bps */
#define COMM_LOST_MS 5000u
#define LOG_CHUNK    32u                        /* registers per log read           */

uint16_t panel_step_to_permille(uint8_t step)
{
    if (step < 1u) step = 1u;
    if (step > 10u) step = 10u;
    return (uint16_t)(300u + (step - 1u) * 700u / 9u);       /* 30 % .. 100 % */
}
uint8_t panel_permille_to_step(uint16_t pm)
{
    if (pm <= 300u) return 1u;
    uint32_t s = 1u + ((uint32_t)(pm - 300u) * 9u + 350u) / 700u;
    return (uint8_t)(s > 10u ? 10u : s);
}

static void emit(panel_t *p, const char *json) { if (p->event) p->event(p->ctx, json); }

void panel_init(panel_t *p, void (*send)(void *, const uint8_t *, uint16_t),
                void (*event)(void *, const char *), void *ctx)
{
    memset(p, 0, sizeof *p);
    p->send = send; p->event = event; p->ctx = ctx;
    p->prewet_s = 120; p->dryout_s = 300; p->wet_memory_s = 1200; p->dry_step = 3;
    p->mode = MODE_COOL;
    p->step = 5;
    p->last_minute = 0xFFFFu;
}

/* ------------------------------------------------------------ user actions */
void panel_set_power(panel_t *p, bool on) { p->power = on; if (!on) p->off_at_ms = 0; }
void panel_set_mode(panel_t *p, pmode_t m) { if (m <= MODE_PUMP) p->mode = m; }
void panel_set_step(panel_t *p, uint8_t s) { p->step = s < 1u ? 1u : s > 10u ? 10u : s; }
void panel_set_timer(panel_t *p, uint16_t min) { p->off_at_ms = min ? p->now + (uint32_t)min * 60000u : 0u; if (!p->off_at_ms && min) p->off_at_ms = 1u; }
void panel_clear_alarm(panel_t *p) { p->cmd_clear = true; }
void panel_identify(panel_t *p) { p->cmd_identify = true; }

void panel_key(panel_t *p, pkey_t k, bool long_press)
{
    switch (k) {
    case KEY_POWER:
        if (long_press) { emit(p, "{\"event\":\"pairing\"}"); break; }   /* BLE pairing window */
        panel_set_power(p, !p->power);
        break;
    case KEY_UP:   if (p->power) panel_set_step(p, (uint8_t)(p->step + 1u)); break;
    case KEY_DOWN: if (p->power) panel_set_step(p, (uint8_t)(p->step - 1u)); break;
    case KEY_MODE:
        if (long_press) { panel_clear_alarm(p); break; }
        panel_set_mode(p, (pmode_t)((p->mode + 1u) % 3u));
        break;
    case KEY_TIMER: {
        /* 1 h steps up to 8 h, then off */
        uint32_t left = p->off_at_ms ? (p->off_at_ms - p->now) / 60000u : 0u;
        uint32_t next = (left / 60u + 1u) * 60u;
        if (!p->power) break;
        panel_set_timer(p, next > 480u ? 0u : (uint16_t)next);
        break;
    }
    }
}

/* ------------------------------------------------------------ sequencer */
static void sequencer(panel_t *p)
{
    uint32_t now = p->now;
    uint32_t in_seq = now - p->seq_t0;
    bool pads_wet = p->pump_ran && (now - p->pump_off_ms) < (uint32_t)p->wet_memory_s * 1000u;

    if (p->off_at_ms && (int32_t)(now - p->off_at_ms) >= 0) {
        p->off_at_ms = 0;
        p->power = false;
        emit(p, "{\"event\":\"timer_off\"}");
    }
    seq_t s = p->seq;
    if (!p->power) {
        if (s == SEQ_RUN && p->want_run && p->want_pump) s = SEQ_DRYOUT;   /* cool mode: dry the pads */
        else if (s != SEQ_DRYOUT) s = SEQ_OFF;
        else if (in_seq >= (uint32_t)p->dryout_s * 1000u) s = SEQ_OFF;
    } else {
        switch (p->mode) {
        case MODE_COOL:
            if (s == SEQ_OFF || s == SEQ_DRYOUT) s = pads_wet ? SEQ_RUN : SEQ_PREWET;
            else if (s == SEQ_PREWET && in_seq >= (uint32_t)p->prewet_s * 1000u) s = SEQ_RUN;
            break;
        case MODE_FAN:
        case MODE_PUMP:
            s = SEQ_RUN;
            break;
        }
    }
    if (s != p->seq) { p->seq = s; p->seq_t0 = now; }

    bool fan = (p->seq == SEQ_RUN && p->mode != MODE_PUMP) || p->seq == SEQ_DRYOUT;
    bool pump = p->seq == SEQ_PREWET || (p->seq == SEQ_RUN && p->mode != MODE_FAN);
    if (p->want_pump && !pump) p->pump_off_ms = now;
    if (pump) p->pump_ran = true;
    p->want_run = fan;
    p->want_pump = pump;
    p->want_speed = panel_step_to_permille(p->seq == SEQ_DRYOUT ? p->dry_step : p->step);
}

static void schedule(panel_t *p, uint16_t mow)
{
    if (mow == 0xFFFFu || mow == p->last_minute) return;
    p->last_minute = mow;
    uint8_t day = (uint8_t)(mow / 1440u);
    uint16_t min = (uint16_t)(mow % 1440u);
    for (unsigned i = 0; i < PANEL_SCHED_N; i++) {
        const sched_t *s = &p->sched[i];
        if (!s->enabled || !(s->days & (1u << day))) continue;
        if (min == s->on_min) {
            panel_set_mode(p, (pmode_t)s->mode);
            panel_set_step(p, s->speed);
            panel_set_power(p, true);
            emit(p, "{\"event\":\"schedule_on\"}");
        }
        if (min == s->off_min) { panel_set_power(p, false); emit(p, "{\"event\":\"schedule_off\"}"); }
    }
}

/* ------------------------------------------------------------ bus */
bool panel_bus_free(const panel_t *p) { return !p->busy; }

void panel_bus_send(panel_t *p, const uint8_t *f, uint16_t n, uint8_t kind)
{
    memcpy(p->req, f, n);
    p->req_len = n;
    p->kind = kind;
    p->busy = true;
    p->req_t0 = p->now;
    p->timeout_ms = (uint32_t)(n + mbm_resp_len(f)) * BYTE_MS + 200u;
    p->send(p->ctx, f, n);
}

static void link_ok(panel_t *p)
{
    p->tx_ok++;
    p->last_ok_ms = p->now;
    if (!p->comm_ok) { p->comm_ok = true; emit(p, "{\"event\":\"drive_online\"}"); }
}

static void alarm_check(panel_t *p)
{
    uint16_t a = p->ir[IR_ALARM];
    if (a == p->last_alarm) return;
    char js[96];
    if (a) snprintf(js, sizeof js, "{\"event\":\"alarm\",\"code\":%u,\"name\":\"E%u\",\"detail\":%u}",
                    a, a, p->ir[IR_ALARM_DETAIL]);
    else snprintf(js, sizeof js, "{\"event\":\"alarm_cleared\",\"code\":%u}", p->last_alarm);
    p->last_alarm = a;
    emit(p, js);
}

void panel_rx_frame(panel_t *p, const uint8_t *f, uint16_t n)
{
    if (!p->busy) return;
    uint16_t regs[64];
    int r = mbm_parse(p->req, f, n, regs);
    uint8_t kind = p->kind;
    p->busy = false;
    if (kind == K_UPD) { if (r != MBM_ERR_CRC) link_ok(p); drive_update_result(p, p->upd, r, regs); return; }
    if (r == MBM_ERR_CRC || r == MBM_ERR_FORMAT) { p->tx_fail++; return; }
    link_ok(p);
    if (r != MBM_OK) return;                         /* exception: drive refused (range, busy) */
    switch (kind) {
    case K_HR_SYNC:
        memcpy(p->hr, regs, sizeof p->hr);
        if (!p->hr_synced) {
            /* adopt the drive's state (it may be running after a power return) */
            p->hr_synced = true;
            p->power = p->hr[HR_RUN] || p->hr[HR_PUMP];
            p->mode = p->hr[HR_PUMP] ? (p->hr[HR_RUN] ? MODE_COOL : MODE_PUMP) : MODE_FAN;
            p->step = panel_permille_to_step(p->hr[HR_SPEED_PERMILLE]);
            p->seq = p->power ? SEQ_RUN : SEQ_OFF;
            p->seq_t0 = p->now;
            sequencer(p);
        }
        break;
    case K_WRITE:
        p->hr[HR_RUN] = p->want_run; p->hr[HR_SPEED_PERMILLE] = p->want_speed; p->hr[HR_PUMP] = p->want_pump;
        break;
    case K_CLEAR: p->cmd_clear = false; break;
    case K_IDENT: p->cmd_identify = false; break;
    case K_FAST:
        memcpy(&p->ir[PANEL_IR_FAST_FIRST], regs, PANEL_IR_FAST_N * 2u);
        alarm_check(p);
        break;
    case K_SLOW:
        memcpy(&p->ir[PANEL_IR_SLOW_FIRST], regs, PANEL_IR_SLOW_N * 2u);
        break;
    case K_LOG:
        memcpy(&p->log[0][0] + p->log_chunk * LOG_CHUNK, regs, LOG_CHUNK * 2u);
        if (++p->log_chunk * LOG_CHUNK >= LOG_LEN * LOG_REC_REGS) {
            p->log_chunk = 0;
            p->log_seq_read = p->ir[IR_LOG_SEQ];
            emit(p, "{\"event\":\"log_updated\"}");
        }
        break;
    default: break;
    }
}

static void bus_schedule(panel_t *p)
{
    uint8_t f[128];
    uint16_t n;
    if (p->busy) {
        if (p->now - p->req_t0 < p->timeout_ms) return;
        p->busy = false;                             /* timeout */
        p->tx_fail++;
        if (p->kind == K_UPD) drive_update_result(p, p->upd, MBM_ERR_FORMAT, 0);
    }
    if (p->comm_ok && p->now - p->last_ok_ms > COMM_LOST_MS) {
        p->comm_ok = false;
        emit(p, "{\"event\":\"drive_offline\"}");
    }
    if (p->upd) { drive_update_tick(p, p->upd); if (p->busy) return; }
    if (!p->hr_synced) { n = mbm_read(f, COOLER_MB_ADDR, 3, 0, HR_COUNT); panel_bus_send(p, f, n, K_HR_SYNC); return; }
    if (p->cmd_clear) { n = mbm_write1(f, COOLER_MB_ADDR, HR_CLEAR_ALARM, 1); panel_bus_send(p, f, n, K_CLEAR); return; }
    if (p->cmd_identify) { n = mbm_write1(f, COOLER_MB_ADDR, HR_IDENTIFY, 1); panel_bus_send(p, f, n, K_IDENT); return; }
    if (p->hr[HR_RUN] != p->want_run || p->hr[HR_SPEED_PERMILLE] != p->want_speed || p->hr[HR_PUMP] != p->want_pump) {
        uint16_t v[3] = { p->want_run, p->want_speed, p->want_pump };
        n = mbm_writen(f, COOLER_MB_ADDR, HR_RUN, v, 3);
        panel_bus_send(p, f, n, K_WRITE);
        return;
    }
    if ((int32_t)(p->now - p->next_fast) >= 0) {
        p->next_fast = p->now + 1000u;
        n = mbm_read(f, COOLER_MB_ADDR, 4, PANEL_IR_FAST_FIRST, PANEL_IR_FAST_N);
        panel_bus_send(p, f, n, K_FAST);
        return;
    }
    if ((int32_t)(p->now - p->next_slow) >= 0) {
        p->next_slow = p->now + 10000u;
        n = mbm_read(f, COOLER_MB_ADDR, 4, PANEL_IR_SLOW_FIRST, PANEL_IR_SLOW_N);
        panel_bus_send(p, f, n, K_SLOW);
        return;
    }
    if (p->ir[IR_LOG_SEQ] != p->log_seq_read && p->ir[IR_LOG_COUNT]) {
        n = mbm_read(f, COOLER_MB_ADDR, 4, (uint16_t)(IR_LOG_BASE + p->log_chunk * LOG_CHUNK), LOG_CHUNK);
        panel_bus_send(p, f, n, K_LOG);
    }
}

void panel_tick(panel_t *p, uint32_t now_ms, uint16_t minute_of_week)
{
    p->now = now_ms;
    schedule(p, minute_of_week);
    if (p->hr_synced && !p->upd) sequencer(p);
    bus_schedule(p);
}

/* ------------------------------------------------------------ view */
void panel_view(const panel_t *p, ui_view_t *v)
{
    memset(v, 0, sizeof *v);
    v->power = p->power;
    v->comm_ok = p->comm_ok;
    v->mode = p->mode;
    v->seq = p->seq;
    v->speed_step = p->step;
    v->alarm = p->comm_ok ? p->ir[IR_ALARM] : 0u;
    v->rpm = p->ir[IR_SPEED_RPM];
    v->power_w = p->ir[IR_PIN_W];
    v->vin_dv = p->ir[IR_VIN_DV];
    v->t_drive_c = (int16_t)(p->ir[IR_T_DRIVE_C] - 40);
    if (p->off_at_ms) v->timer_left_min = (uint16_t)((p->off_at_ms - p->now + 59999u) / 60000u);
    uint32_t in_seq = (p->now - p->seq_t0) / 1000u;
    if (p->seq == SEQ_PREWET) v->seq_left_s = (uint16_t)(p->prewet_s > in_seq ? p->prewet_s - in_seq : 0u);
    if (p->seq == SEQ_DRYOUT) v->seq_left_s = (uint16_t)(p->dryout_s > in_seq ? p->dryout_s - in_seq : 0u);
    if (p->upd) v->update_pct = drive_update_pct(p->upd);

    v->led_power = p->power;
    v->led_pump = p->want_pump;
    v->led_fan = p->want_run;
    v->led_timer = p->off_at_ms != 0u;
    v->led_alarm = v->alarm != 0u;
    v->led_wifi = p->wifi_connected;
    /* two-character 7-segment text, by priority */
    if (p->upd)                     snprintf(v->seg, sizeof v->seg, "UP");
    else if (!p->comm_ok)           { snprintf(v->seg, sizeof v->seg, "--"); v->blink = true; }
    else if (v->alarm)              { snprintf(v->seg, sizeof v->seg, v->alarm < 10u ? "E%u" : "%u", v->alarm); v->blink = true; }
    else if (p->seq == SEQ_PREWET)  snprintf(v->seg, sizeof v->seg, "P%u", (unsigned)(v->seq_left_s + 59u) / 60u % 10u);
    else if (p->seq == SEQ_DRYOUT)  snprintf(v->seg, sizeof v->seg, "d%u", (unsigned)(v->seq_left_s + 59u) / 60u % 10u);
    else if (p->power && p->mode == MODE_PUMP) snprintf(v->seg, sizeof v->seg, "PU");
    else if (p->power)              snprintf(v->seg, sizeof v->seg, "%2u", p->step);
    else                            snprintf(v->seg, sizeof v->seg, "  ");
}
