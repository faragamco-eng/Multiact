/*
 * panel.h - Portable core of the cooler panel firmware.
 *
 * Hardware-independent: the ESP32 port (panel/esp32) supplies the bus
 * transport, clock, keys/touch and display; the same core runs in the host
 * test against the real drive-side Modbus code.
 *
 *   - Modbus master on the 2-wire bus: synchronises the drive state, polls
 *     status (1 s) / counters (10 s) / event log (on change), writes
 *     commands only when they differ from the drive's registers
 *   - cooler sequencer: pre-wet the pads before the fan (cool mode), dry
 *     the pads with the fan after the pump stops (against odour / mould),
 *     fan-only and pump-only modes, speed steps 1..10
 *   - off timer, weekly schedule (8 entries), alarm events
 *   - view model for any display (7-segment + LEDs or a touch screen) and
 *     key handling for a 5-key keypad
 *   - drive firmware update (drive_update.c) takes over the bus while active
 */
#ifndef PANEL_H
#define PANEL_H

#include <stdbool.h>
#include <stdint.h>
#include "cooler_regs.h"

#define PANEL_FW_VERSION 0x0100u
#define PANEL_IR_FAST_FIRST 0u
#define PANEL_IR_FAST_N     13u          /* IR 0..12  */
#define PANEL_IR_SLOW_FIRST 13u
#define PANEL_IR_SLOW_N     (IR_COUNT - 13u)
#define PANEL_SCHED_N       8u

typedef enum { MODE_COOL = 0, MODE_FAN = 1, MODE_PUMP = 2 } pmode_t;
typedef enum { SEQ_OFF = 0, SEQ_PREWET, SEQ_RUN, SEQ_DRYOUT } seq_t;
typedef enum { KEY_POWER = 0, KEY_UP, KEY_DOWN, KEY_MODE, KEY_TIMER } pkey_t;

typedef struct {
    bool     enabled;
    uint8_t  days;                   /* bit0 = Saturday ... bit6 = Friday       */
    uint16_t on_min, off_min;        /* minute of the day                      */
    uint8_t  mode, speed;            /* pmode_t, step 1..10                    */
} sched_t;

typedef struct {
    char     seg[4];                 /* 2-character 7-segment text + NUL        */
    bool     blink;
    bool     led_power, led_pump, led_fan, led_timer, led_alarm, led_wifi;
    /* richer data for a touch screen */
    bool     power, comm_ok;
    pmode_t  mode;
    seq_t    seq;
    uint8_t  speed_step;
    uint16_t seq_left_s;             /* pre-wet / dry-out countdown             */
    uint16_t timer_left_min;
    uint16_t alarm;                  /* 0 or En                                 */
    uint16_t rpm, power_w, vin_dv;
    int16_t  t_drive_c;
    uint8_t  update_pct;             /* 0..100 while a drive update runs        */
} ui_view_t;

struct drive_update;

typedef struct {
    /* configuration */
    uint16_t prewet_s, dryout_s, wet_memory_s;
    uint8_t  dry_step;
    /* user state */
    bool     power;
    pmode_t  mode;
    uint8_t  step;
    uint32_t off_at_ms;              /* 0: no off timer                         */
    sched_t  sched[PANEL_SCHED_N];
    bool     wifi_connected;
    /* sequencer */
    seq_t    seq;
    uint32_t seq_t0, pump_off_ms;
    bool     pump_ran;
    uint16_t last_minute;            /* schedule evaluation                     */
    /* drive mirror */
    uint16_t hr[HR_COUNT], ir[IR_COUNT];
    uint16_t log[LOG_LEN][LOG_REC_REGS];
    uint16_t log_seq_read;
    bool     hr_synced, comm_ok;
    uint32_t last_ok_ms;
    uint16_t last_alarm;
    /* wanted drive outputs */
    uint16_t want_run, want_speed, want_pump;
    /* one-shot commands */
    bool     cmd_clear, cmd_identify;
    /* link */
    uint8_t  req[128];
    uint16_t req_len;
    uint8_t  kind;                   /* what the outstanding request is for     */
    uint8_t  log_chunk;
    bool     busy;
    uint32_t req_t0, timeout_ms, next_fast, next_slow;
    uint32_t tx_ok, tx_fail;
    /* callbacks */
    void   (*send)(void *ctx, const uint8_t *f, uint16_t n);
    void   (*event)(void *ctx, const char *json);
    void    *ctx;
    struct drive_update *upd;        /* non-NULL while updating the drive       */
    uint32_t now;
} panel_t;

void panel_init(panel_t *p, void (*send)(void *, const uint8_t *, uint16_t),
                void (*event)(void *, const char *), void *ctx);
/* now_ms: monotonic; minute_of_week: local time (Saturday 00:00 = 0), 0xFFFF if unknown */
void panel_tick(panel_t *p, uint32_t now_ms, uint16_t minute_of_week);
void panel_rx_frame(panel_t *p, const uint8_t *f, uint16_t n);   /* complete response frame */

/* user actions (keys, app, cloud, schedule all end here) */
void panel_set_power(panel_t *p, bool on);
void panel_set_mode(panel_t *p, pmode_t m);
void panel_set_step(panel_t *p, uint8_t step);
void panel_set_timer(panel_t *p, uint16_t minutes);           /* 0 = cancel */
void panel_clear_alarm(panel_t *p);
void panel_identify(panel_t *p);
void panel_key(panel_t *p, pkey_t k, bool long_press);

void panel_view(const panel_t *p, ui_view_t *v);
uint16_t panel_step_to_permille(uint8_t step);
uint8_t  panel_permille_to_step(uint16_t pm);

/* used by drive_update.c */
bool panel_bus_free(const panel_t *p);
void panel_bus_send(panel_t *p, const uint8_t *f, uint16_t n, uint8_t kind);

#endif
