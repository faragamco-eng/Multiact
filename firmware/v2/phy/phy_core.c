#include "phy_core.h"
#include <string.h>

enum { RX_IDLE = 0, RX_START, RX_DATA, RX_STOP };

uint16_t phy_crc16(const uint8_t *d, uint16_t n)
{
    uint16_t c = 0xFFFFu;
    while (n--) {
        c ^= *d++;
        for (int k = 0; k < 8; k++) c = (c & 1u) ? (uint16_t)((c >> 1) ^ 0xA001u) : (uint16_t)(c >> 1);
    }
    return c;
}

void phy_init(phy_t *p)
{
    memset(p, 0, sizeof *p);
    p->thr_ma = 10.0f;
    p->base_ma = 30.0f;
}

static void tx_start(phy_t *p, const uint8_t *d, uint16_t n)
{
    if (n > PHY_FRAME_MAX) n = PHY_FRAME_MAX;
    memcpy(p->tx_buf, d, n);
    p->tx_len = n;
    p->tx_pos = 0;
    p->tx_bit = 0;
    p->tx_sub = 0;
    p->tx_active = true;
}

/* ---------------------------------------------------------- 19.2 kHz */
static void rx_sample(phy_t *p, bool bit)
{
    switch (p->rx_state) {
    case RX_IDLE:
        if (!bit) { p->rx_state = RX_START; p->rx_cnt = 1; }
        break;
    case RX_START:                         /* confirm start bit at its middle */
        if (++p->rx_cnt == PHY_OVERSAMPLE / 2u) {
            if (bit) p->rx_state = RX_IDLE;            /* glitch */
            else { p->rx_state = RX_DATA; p->rx_cnt = 0; p->rx_bit = 0; p->rx_byte = 0; p->ones = 0; }
        }
        break;
    case RX_DATA:
    case RX_STOP: {
        p->rx_cnt++;
        /* majority of samples 3,4,5 of each bit period (counted from mid-start) */
        uint8_t ph = p->rx_cnt;
        if (ph >= PHY_OVERSAMPLE - 1u && ph <= PHY_OVERSAMPLE + 1u) p->ones += bit;
        if (ph < PHY_OVERSAMPLE + 1u) break;
        bool v = p->ones >= 2u;
        p->rx_cnt = 1;                     /* next window measured from here */
        p->ones = 0;
        if (p->rx_state == RX_DATA) {
            p->rx_byte = (uint8_t)((p->rx_byte >> 1) | (v ? 0x80u : 0u));
            if (++p->rx_bit == 8u) p->rx_state = RX_STOP;
        } else {
            if (v) {
                if (p->rx_len < PHY_FRAME_MAX) p->rx_buf[p->rx_len++] = p->rx_byte;
            } else p->framing_err++;
            p->rx_state = RX_IDLE;
            p->idle_ticks = 0;
        }
        break;
    }
    }
}

bool phy_tick(phy_t *p, float i_ma)
{
    p->i_ma = i_ma;
    /* ---- transmitter */
    if (p->tx_active) {
        /* bit index: 0 start, 1..8 data, 9 stop */
        uint8_t b = p->tx_buf[p->tx_pos];
        bool level = (p->tx_bit == 0u) ? false : (p->tx_bit == 9u) ? true : ((b >> (p->tx_bit - 1u)) & 1u);
        p->line_space = !level;
        if (++p->tx_sub == PHY_OVERSAMPLE) {
            p->tx_sub = 0;
            if (++p->tx_bit == 10u) {
                p->tx_bit = 0;
                if (++p->tx_pos >= p->tx_len) { p->tx_active = false; p->line_space = false; }
            }
        }
        p->blank = 2u * PHY_OVERSAMPLE * 10u;       /* 2 character times after the last edge */
        return p->line_space;
    }
    if (p->blank) {
        p->blank--;
        p->rx_state = RX_IDLE;
        return false;
    }
    /* ---- receiver: adaptive baseline, slicing */
    float d = i_ma - p->base_ma;
    bool space = d > p->thr_ma;
    if (p->rx_state == RX_IDLE && !space) p->base_ma += d * (1.0f / 2000.0f);    /* ~0.1 s */
    /* a UART character holds "space" for at most 9 bits: longer means the panel's idle
     * current stepped up (backlight, relay, Wi-Fi) -> re-acquire the baseline */
    if (space) {
        if (++p->space_run > 12u * PHY_OVERSAMPLE) {
            p->base_ma = i_ma;
            p->space_run = 0;
            p->rx_state = RX_IDLE;
            p->baseline_jumps++;
            return false;
        }
    } else p->space_run = 0;
    rx_sample(p, !space);
    if (p->rx_state == RX_IDLE) {
        if (p->idle_ticks < 0xFFFFu) p->idle_ticks++;
        if (p->rx_len && p->idle_ticks == PHY_GAP_TICKS) p->rx_frame_ready = true;
    }
    return false;
}

/* ---------------------------------------------------------- frames */
static void local_reply(phy_t *p, const uint8_t *f, uint16_t n)
{
    /* FC 04, input registers 0..5: version, vbus [0.1 V], idle current [0.1 mA],
     * frames received, CRC errors, hot-side timeouts */
    if (n != 8u || f[1] != 4u) return;
    uint16_t start = (uint16_t)(f[2] << 8 | f[3]), cnt = (uint16_t)(f[4] << 8 | f[5]);
    uint16_t reg[6] = { PHY_FW_VERSION, (uint16_t)(p->vbus_v * 10.0f), (uint16_t)(p->base_ma * 10.0f),
                        (uint16_t)p->frames_rx, (uint16_t)p->frames_crc, (uint16_t)p->timeouts };
    uint8_t r[32];
    if (start + cnt > 6u || cnt == 0u) {
        r[0] = PHY_LOCAL_ADDR; r[1] = 0x84; r[2] = 2;
        uint16_t c = phy_crc16(r, 3); r[3] = (uint8_t)c; r[4] = (uint8_t)(c >> 8);
        tx_start(p, r, 5);
        return;
    }
    r[0] = PHY_LOCAL_ADDR; r[1] = 4; r[2] = (uint8_t)(2u * cnt);
    for (uint16_t i = 0; i < cnt; i++) { r[3 + 2 * i] = (uint8_t)(reg[start + i] >> 8); r[4 + 2 * i] = (uint8_t)reg[start + i]; }
    uint16_t len = (uint16_t)(3u + 2u * cnt), c = phy_crc16(r, len);
    r[len] = (uint8_t)c; r[len + 1] = (uint8_t)(c >> 8);
    tx_start(p, r, (uint16_t)(len + 2u));
}

void phy_hot_byte(phy_t *p, uint8_t b)
{
    if (p->hot_len < PHY_FRAME_MAX) p->hot_buf[p->hot_len++] = b;
}

void phy_hot_gap(phy_t *p)
{
    if (p->waiting_hot && p->hot_len >= 4u &&
        phy_crc16(p->hot_buf, (uint16_t)(p->hot_len - 2u)) ==
        (uint16_t)(p->hot_buf[p->hot_len - 2u] | p->hot_buf[p->hot_len - 1u] << 8)) {
        tx_start(p, p->hot_buf, p->hot_len);
        p->replies++;
        p->waiting_hot = false;
    }
    p->hot_len = 0;                      /* unsolicited or broken: dropped */
}

void phy_poll(phy_t *p, uint32_t now_ms, void (*send_hot)(const uint8_t *, uint16_t))
{
    if (p->waiting_hot && now_ms - p->wait_ms > 250u) { p->waiting_hot = false; p->timeouts++; }
    if (!p->rx_frame_ready) return;
    /* copy out (the tick may already be collecting the next frame) */
    uint8_t f[PHY_FRAME_MAX];
    uint16_t n = p->rx_len;
    memcpy(f, p->rx_buf, n);
    p->rx_len = 0;
    p->rx_frame_ready = false;
    p->frames_rx++;
    if (n < 4u || phy_crc16(f, (uint16_t)(n - 2u)) != (uint16_t)(f[n - 2u] | f[n - 1u] << 8)) {
        p->frames_crc++;
        return;
    }
    if (f[0] == PHY_LOCAL_ADDR) { local_reply(p, f, n); return; }
    p->hot_len = 0;
    if (f[0] != 0u) { p->waiting_hot = true; p->wait_ms = now_ms; }   /* broadcast: no reply */
    p->frames_fwd++;
    send_hot(f, n);                      /* the reply may arrive before this returns */
}
