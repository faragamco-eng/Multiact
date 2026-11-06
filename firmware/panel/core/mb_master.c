#include "mb_master.h"

uint16_t mbm_crc16(const uint8_t *p, uint16_t n)
{
    uint16_t c = 0xFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c & 1u) ? (uint16_t)((c >> 1) ^ 0xA001u) : (uint16_t)(c >> 1);
    }
    return c;
}
static uint16_t fin(uint8_t *f, uint16_t n)
{
    uint16_t c = mbm_crc16(f, n);
    f[n] = (uint8_t)c; f[n + 1] = (uint8_t)(c >> 8);
    return (uint16_t)(n + 2u);
}
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

uint16_t mbm_read(uint8_t *f, uint8_t addr, uint8_t fc, uint16_t start, uint16_t cnt)
{
    f[0] = addr; f[1] = fc; put16(f + 2, start); put16(f + 4, cnt);
    return fin(f, 6);
}
uint16_t mbm_write1(uint8_t *f, uint8_t addr, uint16_t reg, uint16_t val)
{
    f[0] = addr; f[1] = 6; put16(f + 2, reg); put16(f + 4, val);
    return fin(f, 6);
}
uint16_t mbm_writen(uint8_t *f, uint8_t addr, uint16_t start, const uint16_t *v, uint16_t cnt)
{
    f[0] = addr; f[1] = 0x10; put16(f + 2, start); put16(f + 4, cnt); f[6] = (uint8_t)(2u * cnt);
    for (uint16_t i = 0; i < cnt; i++) put16(f + 7 + 2 * i, v[i]);
    return fin(f, (uint16_t)(7u + 2u * cnt));
}
uint16_t mbm_resp_len(const uint8_t *req)
{
    if (req[1] == 3 || req[1] == 4) return (uint16_t)(5u + 2u * get16(req + 4));
    return 8u;
}
int mbm_parse(const uint8_t *req, const uint8_t *rsp, uint16_t n, uint16_t *out)
{
    if (n < 5u) return MBM_ERR_FORMAT;
    if (mbm_crc16(rsp, (uint16_t)(n - 2u)) != (uint16_t)(rsp[n - 2u] | rsp[n - 1u] << 8)) return MBM_ERR_CRC;
    if (rsp[0] != req[0]) return MBM_ERR_FORMAT;
    if (rsp[1] == (uint8_t)(req[1] | 0x80u)) return -(int)rsp[2];
    if (rsp[1] != req[1]) return MBM_ERR_FORMAT;
    if (req[1] == 3 || req[1] == 4) {
        uint16_t cnt = get16(req + 4);
        if (rsp[2] != 2u * cnt || n != 5u + 2u * cnt) return MBM_ERR_FORMAT;
        for (uint16_t i = 0; i < cnt; i++) out[i] = get16(rsp + 3 + 2 * i);
        return MBM_OK;
    }
    if (n != 8u || get16(rsp + 2) != get16(req + 2)) return MBM_ERR_FORMAT;
    return MBM_OK;
}
