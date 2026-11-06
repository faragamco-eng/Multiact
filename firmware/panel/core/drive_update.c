#include "drive_update.h"
#include "mb_master.h"
#include <string.h>

enum { K_UPD = 8 };                                /* must match panel.c */
#define CHUNK 96u

uint32_t du_crc32(const uint8_t *d, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *d++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

bool drive_update_start(panel_t *p, drive_update_t *u, const uint8_t *img, uint32_t size,
                        bool (*verify)(const uint8_t *, uint32_t, const uint8_t *), const uint8_t *sig)
{
    memset(u, 0, sizeof *u);
    if (!verify || !verify(img, size, sig)) { u->st = DU_FAILED; u->err = DUE_SIGNATURE; return false; }
    /* image information block at 0x200: magic "CDRV", hw id 0x0200, version, size */
    uint32_t magic, isz;
    uint16_t hw, ver;
    if (size < 0x210u) { u->st = DU_FAILED; u->err = DUE_IMAGE; return false; }
    memcpy(&magic, img + 0x200, 4); memcpy(&hw, img + 0x204, 2); memcpy(&ver, img + 0x206, 2); memcpy(&isz, img + 0x208, 4);
    if (magic != 0x56524443u || hw != 0x0200u || isz != size) { u->st = DU_FAILED; u->err = DUE_IMAGE; return false; }
    u->img = img; u->size = size; u->new_ver = ver;
    u->crc = du_crc32(img, size);
    u->st = DU_STOP_MOTOR;
    u->t0 = p->now;
    p->upd = u;
    panel_set_power(p, false);
    p->seq = SEQ_OFF;                                /* no dry-out: the update needs the motor stopped */
    p->want_run = 0; p->want_pump = 0;
    return true;
}

uint8_t drive_update_pct(const drive_update_t *u)
{
    if (u->st == DU_DONE) return 100u;
    if (!u->size) return 0u;
    return (uint8_t)(u->ofs * 99u / u->size);
}

static void fail(panel_t *p, drive_update_t *u, uint8_t e)
{
    u->st = DU_FAILED; u->err = e;
    p->upd = 0;
    p->hr_synced = false;                            /* resynchronise afterwards */
}

void drive_update_tick(panel_t *p, drive_update_t *u)
{
    if (!panel_bus_free(p) || u->pending) return;
    uint8_t f[128];
    uint16_t n = 0;
    switch (u->st) {
    case DU_STOP_MOTOR: {                            /* stop fan + pump, then read the state */
        uint16_t v[3] = {0, p->hr[HR_SPEED_PERMILLE] ? p->hr[HR_SPEED_PERMILLE] : 500u, 0};
        n = mbm_writen(f, COOLER_MB_ADDR, HR_RUN, v, 3);
        break;
    }
    case DU_HEADER: {
        uint16_t v[4] = { (uint16_t)u->size, (uint16_t)(u->size >> 16), (uint16_t)u->crc, (uint16_t)(u->crc >> 16) };
        n = mbm_writen(f, COOLER_MB_ADDR, HR_FWU_SIZE_LO, v, 4);
        break;
    }
    case DU_BEGIN:  n = mbm_write1(f, COOLER_MB_ADDR, HR_FWU_CMD, FWU_CMD_BEGIN); break;
    case DU_ERASE_WAIT: n = mbm_read(f, COOLER_MB_ADDR, 4, IR_FWU_STATE, 4); break;
    case DU_CHUNKS: {
        uint16_t v[2 + FWU_DATA_REGS];
        v[0] = (uint16_t)u->ofs; v[1] = (uint16_t)(u->ofs >> 16);
        for (unsigned k = 0; k < FWU_DATA_REGS; k++) {
            uint32_t i = u->ofs + 2u * k;
            uint8_t b0 = i < u->size ? u->img[i] : 0xFFu, b1 = i + 1u < u->size ? u->img[i + 1u] : 0xFFu;
            v[2 + k] = (uint16_t)(b0 << 8 | b1);
        }
        n = mbm_writen(f, COOLER_MB_ADDR, HR_FWU_OFS_LO, v, 2u + FWU_DATA_REGS);
        break;
    }
    case DU_COMMIT: n = mbm_write1(f, COOLER_MB_ADDR, HR_FWU_CMD, FWU_CMD_COMMIT); break;
    case DU_REBOOT_WAIT:
        if (p->now - u->t0 < 1500u) return;          /* drive resets, bootloader copies (~1 s) */
        n = mbm_read(f, COOLER_MB_ADDR, 4, IR_FW_VER, 2);
        break;
    default: return;
    }
    u->pending = true;
    panel_bus_send(p, f, n, K_UPD);
}

void drive_update_result(panel_t *p, drive_update_t *u, int res, const uint16_t *regs)
{
    u->pending = false;
    if (res != MBM_OK) {
        if (u->st == DU_REBOOT_WAIT) {               /* drive still restarting */
            if (p->now - u->t0 > 30000u) fail(p, u, DUE_VERSION);
            return;
        }
        if (res < 0 && res > -100 && (u->st == DU_BEGIN || u->st == DU_COMMIT)) {
            fail(p, u, u->st == DU_BEGIN ? DUE_REFUSED : DUE_DRIVE);
            return;
        }
        if (++u->tries > 5u) fail(p, u, DUE_LINK);    /* repeated: same chunk/offset is idempotent */
        return;
    }
    u->tries = 0;
    switch (u->st) {
    case DU_STOP_MOTOR: u->st = DU_HEADER; break;
    case DU_HEADER:     u->st = DU_BEGIN; break;
    case DU_BEGIN:      u->st = DU_ERASE_WAIT; break;
    case DU_ERASE_WAIT:
        if (regs[0] == FWS_RECEIVING) { u->st = DU_CHUNKS; u->ofs = 0; }
        else if (regs[0] == FWS_ERROR) fail(p, u, DUE_DRIVE);
        break;
    case DU_CHUNKS:
        u->ofs += CHUNK;
        if (u->ofs >= u->size) u->st = DU_COMMIT;
        break;
    case DU_COMMIT:     u->st = DU_REBOOT_WAIT; u->t0 = p->now; break;
    case DU_REBOOT_WAIT:
        if (regs[0] == u->new_ver) { u->st = DU_DONE; p->upd = 0; p->hr_synced = false; }
        else fail(p, u, DUE_VERSION);
        break;
    default: break;
    }
}
