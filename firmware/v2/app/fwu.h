/*
 * fwu.h - Drive firmware update over Modbus (receiver in the application,
 * decision + copy in the bootloader).
 *
 * Flash map (STM32G431CB, 128 KB, 2 KB pages):
 *   0x08000000  16 KB  bootloader
 *   0x08004000  54 KB  application  (APP)
 *   0x08011800  54 KB  staging      (download area)
 *   0x0801F000   4 KB  parameters (2 pages)
 *
 * Image = application binary linked at APP. At offset IMG_INFO_OFS it
 * carries img_info_t; fw_pack.py fills size and CRC-32 (computed over the
 * image with the crc field skipped). The bootloader copies STAGING -> APP
 * when the staging image is valid and differs from the application; an
 * interrupted copy is simply repeated at the next reset.
 *
 * Authenticity: the panel downloads images over TLS and verifies the
 * manufacturer's signature before streaming them here; the drive checks
 * integrity (CRC-32) and hardware id only.
 */
#ifndef FWU_H
#define FWU_H

#include <stdbool.h>
#include <stdint.h>
#include "flash_if.h"

#define IMG_INFO_OFS   0x200u
#define IMG_MAGIC      0x56524443u    /* "CDRV" */
#define IMG_HW_ID      0x0200u        /* board v2 */

typedef struct {
    uint32_t magic;
    uint16_t hw_id;
    uint16_t version;                 /* major<<8 | minor */
    uint32_t size;                    /* bytes, whole image */
    uint32_t crc;                     /* CRC-32 with this field skipped */
} img_info_t;

typedef enum { FWU_IDLE = 0, FWU_ERASING, FWU_RECEIVING, FWU_READY, FWU_ERROR } fwu_state_t;
enum { FWE_NONE = 0, FWE_BUSY_MOTOR, FWE_SIZE, FWE_SEQUENCE, FWE_FLASH, FWE_CRC, FWE_HEADER, FWE_STATE };

typedef struct {
    const flash_if_t *fl;
    uint32_t stg, slot;               /* staging address and slot size */
    fwu_state_t state;
    uint8_t  error;
    uint32_t size, crc, rx, ofs;
    uint32_t erase_next;
    uint16_t hr_shadow[16];
    uint8_t  chunk[96];
    bool     reboot_req;
    bool   (*motor_busy)(void *ctx);
    void    *ctx;
} fwu_t;

void    fwu_init(fwu_t *u, const flash_if_t *fl, uint32_t stg, uint32_t slot,
                 bool (*motor_busy)(void *), void *ctx);
void    fwu_poll(fwu_t *u);                      /* main loop: erases pages step by step */
uint8_t fwu_write_reg(fwu_t *u, uint16_t a, uint16_t v);   /* Modbus holding write */
uint8_t fwu_read_reg(fwu_t *u, uint16_t a, uint16_t *v, bool input);

/* shared with the bootloader */
bool img_check(const uint8_t *img, uint32_t slot, img_info_t *info);
typedef enum { BOOT_APP, BOOT_COPY, BOOT_NONE } boot_action_t;
boot_action_t boot_decide(const uint8_t *app, const uint8_t *stg, uint32_t slot);

#endif
