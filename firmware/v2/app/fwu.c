#include "fwu.h"
#include "crc32.h"
#include "cooler_regs.h"
#include "modbus_slave.h"
#include <stddef.h>
#include <string.h>

_Static_assert((int)FWU_RECEIVING == (int)FWS_RECEIVING && (int)FWU_ERROR == (int)FWS_ERROR && (int)FWU_READY == (int)FWS_READY,
               "IR_FWU_STATE numbering must match comm/cooler_regs.h");

bool img_check(const uint8_t *img, uint32_t slot, img_info_t *info)
{
    img_info_t h;
    memcpy(&h, img + IMG_INFO_OFS, sizeof h);
    if (h.magic != IMG_MAGIC || h.hw_id != IMG_HW_ID) return false;
    if (h.size < IMG_INFO_OFS + sizeof h || h.size > slot) return false;
    uint32_t cpos = IMG_INFO_OFS + (uint32_t)offsetof(img_info_t, crc);
    uint32_t c = crc32_update(0, img, cpos);
    c = crc32_update(c, img + cpos + 4u, h.size - cpos - 4u);
    if (c != h.crc) return false;
    if (info) *info = h;
    return true;
}

boot_action_t boot_decide(const uint8_t *app, const uint8_t *stg, uint32_t slot)
{
    img_info_t a, s;
    bool app_ok = img_check(app, slot, &a);
    bool stg_ok = img_check(stg, slot, &s);
    if (stg_ok && (!app_ok || a.crc != s.crc || a.size != s.size)) return BOOT_COPY;
    if (app_ok) return BOOT_APP;
    return BOOT_NONE;
}

void fwu_init(fwu_t *u, const flash_if_t *fl, uint32_t stg, uint32_t slot,
              bool (*motor_busy)(void *), void *ctx)
{
    memset(u, 0, sizeof *u);
    u->fl = fl; u->stg = stg; u->slot = slot;
    u->motor_busy = motor_busy; u->ctx = ctx;
}

static void fail(fwu_t *u, uint8_t e) { u->state = FWU_ERROR; u->error = e; }

void fwu_poll(fwu_t *u)
{
    if (u->state != FWU_ERASING) return;
    if (!u->fl->erase(u->stg + u->erase_next)) { fail(u, FWE_FLASH); return; }
    u->erase_next += u->fl->page_size;
    if (u->erase_next >= u->slot) u->state = FWU_RECEIVING;
}

static uint8_t commit_chunk(fwu_t *u)
{
    if (u->state != FWU_RECEIVING) return MB_EX_DEVICE_FAILURE;
    if (u->ofs < u->rx) return 0;                        /* repeated chunk: already stored */
    if (u->ofs != u->rx) { fail(u, FWE_SEQUENCE); return MB_EX_ILLEGAL_VALUE; }
    if (u->ofs + sizeof u->chunk > u->slot) { fail(u, FWE_SIZE); return MB_EX_ILLEGAL_VALUE; }
    if (!u->fl->program(u->stg + u->ofs, u->chunk, sizeof u->chunk)) { fail(u, FWE_FLASH); return MB_EX_DEVICE_FAILURE; }
    u->rx += sizeof u->chunk;
    return 0;
}

uint8_t fwu_write_reg(fwu_t *u, uint16_t a, uint16_t v)
{
    if (a < HR_FWU_BASE || a >= HR_FWU_DATA + FWU_DATA_REGS) return MB_EX_ILLEGAL_ADDRESS;
    if (a >= HR_FWU_DATA) {
        unsigned k = (unsigned)(a - HR_FWU_DATA);
        u->chunk[2 * k] = (uint8_t)(v >> 8);
        u->chunk[2 * k + 1] = (uint8_t)v;
        return (k == FWU_DATA_REGS - 1u) ? commit_chunk(u) : 0;
    }
    unsigned i = (unsigned)(a - HR_FWU_BASE);
    u->hr_shadow[i] = v;
    if (a == HR_FWU_OFS_HI || a == HR_FWU_OFS_LO)
        u->ofs = ((uint32_t)u->hr_shadow[15] << 16) | u->hr_shadow[14];
    if (a != HR_FWU_CMD) return 0;
    switch (v) {
    case FWU_CMD_BEGIN:
        if (u->motor_busy && u->motor_busy(u->ctx)) { fail(u, FWE_BUSY_MOTOR); return MB_EX_DEVICE_FAILURE; }
        u->size = ((uint32_t)u->hr_shadow[2] << 16) | u->hr_shadow[1];
        u->crc = ((uint32_t)u->hr_shadow[4] << 16) | u->hr_shadow[3];
        if (u->size < 1024u || u->size > u->slot) { fail(u, FWE_SIZE); return MB_EX_ILLEGAL_VALUE; }
        u->rx = 0; u->ofs = 0; u->erase_next = 0; u->error = FWE_NONE;
        u->state = FWU_ERASING;
        return 0;
    case FWU_CMD_COMMIT: {
        if (u->state != FWU_RECEIVING || u->rx < u->size) { fail(u, FWE_STATE); return MB_EX_DEVICE_FAILURE; }
        const uint8_t *img = u->fl->ptr(u->stg);
        if (crc32_update(0, img, u->size) != u->crc) { fail(u, FWE_CRC); return MB_EX_DEVICE_FAILURE; }
        img_info_t h;
        if (!img_check(img, u->slot, &h) || h.size != u->size) { fail(u, FWE_HEADER); return MB_EX_DEVICE_FAILURE; }
        u->state = FWU_READY;
        u->reboot_req = true;        /* application resets after the reply is sent */
        return 0;
    }
    case FWU_CMD_ABORT:
        u->state = FWU_IDLE; u->error = FWE_NONE;
        return 0;
    default:
        return MB_EX_ILLEGAL_VALUE;
    }
}

uint8_t fwu_read_reg(fwu_t *u, uint16_t a, uint16_t *v, bool input)
{
    if (!input) {
        if (a < HR_FWU_BASE || a >= HR_FWU_DATA) return MB_EX_ILLEGAL_ADDRESS;
        *v = (a == HR_FWU_CMD) ? 0u : u->hr_shadow[a - HR_FWU_BASE];
        return 0;
    }
    switch (a) {
    case IR_FWU_STATE: *v = (uint16_t)u->state; return 0;
    case IR_FWU_RX_LO: *v = (uint16_t)u->rx; return 0;
    case IR_FWU_RX_HI: *v = (uint16_t)(u->rx >> 16); return 0;
    case IR_FWU_ERROR: *v = u->error; return 0;
    default: return MB_EX_ILLEGAL_ADDRESS;
    }
}
