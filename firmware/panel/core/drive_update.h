/*
 * drive_update.h - Streams a drive firmware image through the panel bus
 * (protocol v3 update window, see comm/cooler_regs.h).
 *
 * The image comes from the manufacturer server (HTTPS) or the phone; the
 * signature is checked here by the verify() callback (Ed25519 on the
 * ESP32, see docs/08) BEFORE anything is sent to the drive. The fan is
 * stopped first because the drive refuses an update while the motor runs.
 * At 2400 bps a 30 KB image takes about 3 minutes.
 */
#ifndef DRIVE_UPDATE_H
#define DRIVE_UPDATE_H

#include <stdbool.h>
#include <stdint.h>
#include "panel.h"

typedef enum {
    DU_IDLE = 0, DU_STOP_MOTOR, DU_HEADER, DU_BEGIN, DU_ERASE_WAIT, DU_CHUNKS, DU_COMMIT,
    DU_REBOOT_WAIT, DU_DONE, DU_FAILED
} du_state_t;

enum { DUE_NONE = 0, DUE_SIGNATURE, DUE_IMAGE, DUE_REFUSED, DUE_LINK, DUE_DRIVE, DUE_VERSION };

typedef struct drive_update {
    const uint8_t *img;
    uint32_t size, crc, ofs;
    uint16_t new_ver;
    du_state_t st;
    uint8_t  err, tries;
    uint32_t t0;
    bool     pending;                /* request outstanding                 */
} drive_update_t;

/* verify(img, size, sig) must return true for a genuine image */
bool drive_update_start(panel_t *p, drive_update_t *u, const uint8_t *img, uint32_t size,
                        bool (*verify)(const uint8_t *, uint32_t, const uint8_t *), const uint8_t *sig);
void drive_update_tick(panel_t *p, drive_update_t *u);
void drive_update_result(panel_t *p, drive_update_t *u, int res, const uint16_t *regs);
uint8_t drive_update_pct(const drive_update_t *u);
uint32_t du_crc32(const uint8_t *d, uint32_t n);

#endif
