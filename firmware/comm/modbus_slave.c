/*
 * modbus_slave.c - Minimal Modbus RTU slave (see modbus_slave.h).
 */
#include "modbus_slave.h"

uint16_t mb_crc16(const uint8_t *p, uint16_t n)
{
    uint16_t crc = 0xFFFFu;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
    }
    return crc;
}

void mb_init(mb_slave_t *mb, uint8_t address, const mb_regs_t *regs)
{
    *mb = (mb_slave_t){0};
    mb->address = address;
    mb->regs = regs;
}

void mb_rx_byte(mb_slave_t *mb, uint8_t b)
{
    if (mb->rx_len < MB_BUF)
        mb->rx[mb->rx_len++] = b;
    else
        mb->rx_overflow = true;
}

static uint16_t get16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static void finish(mb_slave_t *mb, uint16_t n)
{
    uint16_t crc = mb_crc16(mb->tx, n);
    mb->tx[n] = (uint8_t)crc;            /* CRC: low byte first */
    mb->tx[n + 1] = (uint8_t)(crc >> 8);
    mb->tx_len = (uint16_t)(n + 2u);
}

static void exception(mb_slave_t *mb, uint8_t fn, uint8_t code)
{
    mb->tx[0] = mb->address;
    mb->tx[1] = (uint8_t)(fn | 0x80u);
    mb->tx[2] = code;
    mb->exceptions++;
    finish(mb, 3);
}

bool mb_rx_gap(mb_slave_t *mb)
{
    const uint8_t *f = mb->rx;
    uint16_t n = mb->rx_len;
    bool overflow = mb->rx_overflow;
    mb->rx_len = 0;
    mb->rx_overflow = false;
    mb->tx_len = 0;

    if (overflow || n < 4u)
        return false;
    if (mb_crc16(f, (uint16_t)(n - 2u)) != (uint16_t)(f[n - 2] | (f[n - 1] << 8))) {
        mb->frames_crc_err++;
        return false;
    }
    uint8_t addr = f[0];
    if (addr != mb->address && addr != 0u) {
        mb->frames_other_addr++;
        return false;
    }
    mb->frames_ok++;
    bool broadcast = addr == 0u;            /* writes only, never answered */
    uint8_t fn = f[1];
    const mb_regs_t *r = mb->regs;

    switch (fn) {
    case 0x03:
    case 0x04: {
        if (n != 8u || broadcast) { if (!broadcast) exception(mb, fn, MB_EX_ILLEGAL_VALUE); break; }
        uint16_t start = get16(f + 2), cnt = get16(f + 4);
        if (cnt == 0u || cnt > 60u) { exception(mb, fn, MB_EX_ILLEGAL_VALUE); break; }
        mb->tx[0] = mb->address;
        mb->tx[1] = fn;
        mb->tx[2] = (uint8_t)(cnt * 2u);
        for (uint16_t i = 0; i < cnt; i++) {
            uint16_t v = 0;
            uint8_t ex = (fn == 0x03) ? r->read_holding((uint16_t)(start + i), &v, r->ctx)
                                      : r->read_input((uint16_t)(start + i), &v, r->ctx);
            if (ex) { exception(mb, fn, ex); return true; }
            put16(mb->tx + 3 + 2 * i, v);
        }
        finish(mb, (uint16_t)(3u + cnt * 2u));
        break;
    }
    case 0x06: {
        if (n != 8u) { if (!broadcast) exception(mb, fn, MB_EX_ILLEGAL_VALUE); break; }
        uint8_t ex = r->write_holding(get16(f + 2), get16(f + 4), r->ctx);
        if (broadcast) break;
        if (ex) { exception(mb, fn, ex); break; }
        for (int i = 0; i < 6; i++) mb->tx[i] = f[i];   /* echo request */
        finish(mb, 6);
        break;
    }
    case 0x10: {
        if (n < 9u) { if (!broadcast) exception(mb, fn, MB_EX_ILLEGAL_VALUE); break; }
        uint16_t start = get16(f + 2), cnt = get16(f + 4);
        uint8_t bytes = f[6];
        if (cnt == 0u || cnt > 60u || bytes != cnt * 2u || n != (uint16_t)(9u + bytes)) {
            if (!broadcast) exception(mb, fn, MB_EX_ILLEGAL_VALUE);
            break;
        }
        for (uint16_t i = 0; i < cnt; i++) {
            uint8_t ex = r->write_holding((uint16_t)(start + i), get16(f + 7 + 2 * i), r->ctx);
            if (ex) { if (!broadcast) exception(mb, fn, ex); return mb->tx_len > 0u; }
        }
        if (broadcast) break;
        mb->tx[0] = mb->address;
        mb->tx[1] = fn;
        put16(mb->tx + 2, start);
        put16(mb->tx + 4, cnt);
        finish(mb, 6);
        break;
    }
    default:
        if (!broadcast) exception(mb, fn, MB_EX_ILLEGAL_FUNCTION);
        break;
    }
    return mb->tx_len > 0u;
}
