/*
 * json_api.h - JSON messages between the panel and the phone app / cloud.
 * One message format for every transport (BLE GATT, local WebSocket, MQTT);
 * see docs/08_panel_and_app_protocol.md.
 *
 *   command  {"cmd":"set","power":1,"mode":"cool","speed":7}
 *            {"cmd":"timer","min":90}       {"cmd":"clear_alarm"}   {"cmd":"identify"}
 *            {"cmd":"schedule","i":0,"en":1,"days":127,"on":450,"off":840,"mode":"cool","speed":6}
 *            {"cmd":"get"}                  {"cmd":"log"}           {"cmd":"warranty","note":"..."}
 *   reply    {"ok":1,...} or {"ok":0,"err":"..."}
 *   state    {"type":"state",...}  (also pushed every 10 s and on change)
 *   event    {"event":"alarm","code":4,"name":"E4","detail":0}
 */
#ifndef JSON_API_H
#define JSON_API_H
#include <stddef.h>
#include "panel.h"

typedef struct {
    const char *panel_serial;       /* factory, panel flash         */
    const char *panel_fw;           /* "1.0.0"                      */
} panel_ident_t;

/* handle one command; writes the reply into out (always NUL terminated) */
size_t json_handle(panel_t *p, const char *cmd, char *out, size_t cap, const panel_ident_t *id);
size_t json_state(const panel_t *p, char *out, size_t cap);
size_t json_log(const panel_t *p, char *out, size_t cap);
/* warranty / service report for the manufacturer server */
size_t json_warranty(const panel_t *p, const panel_ident_t *id, const char *note, char *out, size_t cap);

/* tiny flat-object reader (also used by the ESP32 port) */
bool json_get_int(const char *js, const char *key, long *v);
bool json_get_str(const char *js, const char *key, char *out, size_t cap);
#endif
