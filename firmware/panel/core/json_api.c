#include "json_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *MODES[] = { "cool", "fan", "pump" };
static const char *SEQ[] = { "off", "prewet", "run", "dryout" };

static const char *find_key(const char *js, const char *key)
{
    size_t kl = strlen(key);
    for (const char *s = js; (s = strchr(s, '"')) != NULL; s++) {
        if (strncmp(s + 1, key, kl) == 0 && s[kl + 1] == '"') {
            const char *c = s + kl + 2;
            while (*c == ' ') c++;
            if (*c == ':') { c++; while (*c == ' ') c++; return c; }
        }
    }
    return NULL;
}
bool json_get_int(const char *js, const char *key, long *v)
{
    const char *c = find_key(js, key);
    if (!c) return false;
    char *end;
    long x = strtol(c, &end, 10);
    if (end == c) return false;
    *v = x;
    return true;
}
bool json_get_str(const char *js, const char *key, char *out, size_t cap)
{
    const char *c = find_key(js, key);
    if (!c || *c != '"' || cap == 0) return false;
    size_t n = 0;
    for (c++; *c && *c != '"' && n + 1 < cap; c++) {
        if (*c == '\\' && c[1]) c++;
        out[n++] = *c;
    }
    out[n] = 0;
    return true;
}
static int mode_of(const char *s) { for (int i = 0; i < 3; i++) if (!strcmp(s, MODES[i])) return i; return -1; }

size_t json_state(const panel_t *p, char *o, size_t cap)
{
    const uint16_t *ir = p->ir;
    uint32_t wh = (uint32_t)ir[IR_ENERGY_WH_HI] << 16 | ir[IR_ENERGY_WH_LO];
    ui_view_t v;
    panel_view(p, &v);
    int n = snprintf(o, cap,
        "{\"type\":\"state\",\"online\":%d,\"power\":%d,\"mode\":\"%s\",\"seq\":\"%s\",\"speed\":%u,"
        "\"seq_left_s\":%u,\"timer_min\":%u,\"alarm\":%u,\"alarm_detail\":%u,\"rpm\":%u,\"power_w\":%u,"
        "\"vin\":%.1f,\"vbus\":%u,\"i_motor\":%.2f,\"t_drive\":%d,\"t_motor\":%d,\"energy_kwh\":%.3f,"
        "\"run_h\":%u,\"starts\":%u,\"outages\":%u,\"drive_fw\":\"%u.%u\",\"motor_id\":%d,\"update_pct\":%u}",
        p->comm_ok, p->power, MODES[p->mode], SEQ[p->seq], p->step, v.seq_left_s, v.timer_left_min,
        v.alarm, ir[IR_ALARM_DETAIL], ir[IR_SPEED_RPM], ir[IR_PIN_W], ir[IR_VIN_DV] / 10.0, ir[IR_VBUS_V],
        ir[IR_IMOT_CA] / 100.0, (int)ir[IR_T_DRIVE_C] - 40, (int)ir[IR_T_MOTOR_C] - 40, wh / 1000.0,
        ir[IR_RUN_HOURS], ir[IR_STARTS], ir[IR_OUTAGES], ir[IR_FW_VER] >> 8, ir[IR_FW_VER] & 0xFFu,
        ir[IR_MOTOR_ID] == 0xFFFFu ? -1 : (int)ir[IR_MOTOR_ID], v.update_pct);
    return n < 0 ? 0 : (size_t)n < cap ? (size_t)n : cap - 1;
}

static size_t log_events(const panel_t *p, char *o, size_t cap)
{
    size_t n = (size_t)snprintf(o, cap, "[");
    unsigned cnt = p->ir[IR_LOG_COUNT] < LOG_LEN ? p->ir[IR_LOG_COUNT] : LOG_LEN;
    for (unsigned k = 0; k < cnt && n < cap; k++) {
        const uint16_t *r = p->log[k];
        n += (size_t)snprintf(o + n, cap - n,
            "%s{\"code\":%u,\"detail\":%u,\"run_min\":%lu,\"vin\":%u,\"vbus\":%u,\"i\":%.2f,\"t\":%d}",
            k ? "," : "", r[0], r[1], (unsigned long)((uint32_t)r[3] << 16 | r[2]), r[4], r[5], r[6] / 100.0, (int)r[7] - 40);
    }
    if (n < cap) n += (size_t)snprintf(o + n, cap - n, "]");
    return n < cap ? n : cap - 1;
}

size_t json_log(const panel_t *p, char *o, size_t cap)
{
    char ev[1400];
    log_events(p, ev, sizeof ev);
    int n = snprintf(o, cap, "{\"type\":\"log\",\"events\":%s}", ev);
    return n < 0 ? 0 : (size_t)n < cap ? (size_t)n : cap - 1;
}

size_t json_warranty(const panel_t *p, const panel_ident_t *id, const char *note, char *o, size_t cap)
{
    const uint16_t *ir = p->ir;
    char log[1400];
    log_events(p, log, sizeof log);
    char clean[200];
    size_t k = 0;
    for (const char *s = note ? note : ""; *s && k + 2 < sizeof clean; s++)
        if (*s != '"' && *s != '\\' && (unsigned char)*s >= 0x20u) clean[k++] = *s;     /* keep UTF-8 text */
    clean[k] = 0;
    int n = snprintf(o, cap,
        "{\"type\":\"warranty\",\"drive_serial\":%lu,\"panel_serial\":\"%s\",\"hw_rev\":\"%u.%u\","
        "\"drive_fw\":\"%u.%u\",\"panel_fw\":\"%s\",\"run_h\":%u,\"energy_kwh\":%.1f,\"starts\":%u,"
        "\"outages\":%u,\"alarms_seen\":%u,\"motor\":{\"id\":%d,\"r_mohm\":%u,\"l_10uh\":%u,\"psi\":%u},"
        "\"note\":\"%s\",\"log\":%s}",
        (unsigned long)((uint32_t)ir[IR_SERIAL_HI] << 16 | ir[IR_SERIAL_LO]), id->panel_serial,
        ir[IR_HW_REV] >> 8, ir[IR_HW_REV] & 0xFFu, ir[IR_FW_VER] >> 8, ir[IR_FW_VER] & 0xFFu, id->panel_fw,
        ir[IR_RUN_HOURS], ((uint32_t)ir[IR_ENERGY_WH_HI] << 16 | ir[IR_ENERGY_WH_LO]) / 1000.0, ir[IR_STARTS],
        ir[IR_OUTAGES], ir[IR_ALARM_LATCHED], ir[IR_MOTOR_ID] == 0xFFFFu ? -1 : (int)ir[IR_MOTOR_ID],
        ir[IR_R_MOHM], ir[IR_L_UH_DIV10], ir[IR_FLUX_UVS], clean, log);
    return n < 0 ? 0 : (size_t)n < cap ? (size_t)n : cap - 1;
}

#define REPLY(...) do { int n_ = snprintf(out, cap, __VA_ARGS__); return n_ < 0 ? 0 : (size_t)n_ < cap ? (size_t)n_ : cap - 1; } while (0)

size_t json_handle(panel_t *p, const char *js, char *out, size_t cap, const panel_ident_t *id)
{
    char cmd[16], s[200];
    long v;
    if (!json_get_str(js, "cmd", cmd, sizeof cmd)) REPLY("{\"ok\":0,\"err\":\"no cmd\"}");
    if (!strcmp(cmd, "get")) return json_state(p, out, cap);
    if (!strcmp(cmd, "log")) return json_log(p, out, cap);
    if (p->upd && strcmp(cmd, "get")) REPLY("{\"ok\":0,\"err\":\"drive update running\"}");
    if (!strcmp(cmd, "set")) {
        if (json_get_str(js, "mode", s, sizeof s)) {
            int m = mode_of(s);
            if (m < 0) REPLY("{\"ok\":0,\"err\":\"mode\"}");
            panel_set_mode(p, (pmode_t)m);
        }
        if (json_get_int(js, "speed", &v)) {
            if (v < 1 || v > 10) REPLY("{\"ok\":0,\"err\":\"speed 1..10\"}");
            panel_set_step(p, (uint8_t)v);
        }
        if (json_get_int(js, "power", &v)) panel_set_power(p, v != 0);
        REPLY("{\"ok\":1}");
    }
    if (!strcmp(cmd, "timer")) {
        if (!json_get_int(js, "min", &v) || v < 0 || v > 1440) REPLY("{\"ok\":0,\"err\":\"min 0..1440\"}");
        panel_set_timer(p, (uint16_t)v);
        REPLY("{\"ok\":1}");
    }
    if (!strcmp(cmd, "clear_alarm")) { panel_clear_alarm(p); REPLY("{\"ok\":1}"); }
    if (!strcmp(cmd, "identify")) {
        if (p->power) REPLY("{\"ok\":0,\"err\":\"switch the cooler off first\"}");
        panel_identify(p);
        REPLY("{\"ok\":1}");
    }
    if (!strcmp(cmd, "schedule")) {
        long i, en = 1, days = 127, on = 0, off = 0, sp = 5;
        if (!json_get_int(js, "i", &i) || i < 0 || i >= (long)PANEL_SCHED_N) REPLY("{\"ok\":0,\"err\":\"i\"}");
        json_get_int(js, "en", &en); json_get_int(js, "days", &days);
        json_get_int(js, "on", &on); json_get_int(js, "off", &off); json_get_int(js, "speed", &sp);
        int m = json_get_str(js, "mode", s, sizeof s) ? mode_of(s) : 0;
        if (m < 0 || on < 0 || on > 1439 || off < 0 || off > 1439 || sp < 1 || sp > 10 || days < 0 || days > 127)
            REPLY("{\"ok\":0,\"err\":\"range\"}");
        p->sched[i] = (sched_t){ en != 0, (uint8_t)days, (uint16_t)on, (uint16_t)off, (uint8_t)m, (uint8_t)sp };
        REPLY("{\"ok\":1}");
    }
    if (!strcmp(cmd, "warranty")) {
        json_get_str(js, "note", s, sizeof s);
        return json_warranty(p, id, s, out, cap);
    }
    REPLY("{\"ok\":0,\"err\":\"unknown cmd\"}");
}
