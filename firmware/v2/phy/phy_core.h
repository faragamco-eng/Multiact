/*
 * phy_core.h - Panel-bus PHY + Modbus frame bridge (STM32C011, SELV side).
 *
 * Panel bus (2 wires, SELV, 2400 bps 8N1, half duplex):
 *   drive -> panel : bus voltage 24 V (mark, 1) / 16 V (space, 0)
 *   panel -> drive : panel draws +20 mA extra for a space (0)
 * The PHY samples the return current at 8x the bit rate, tracks the
 * panel's idle current (it changes with backlight, Wi-Fi bursts...) and
 * slices at baseline + 10 mA. Reception is blanked while the PHY transmits
 * (the 24 <-> 16 V steps charge the panel's input capacitance).
 *
 * Bridge: a complete panel frame with a good CRC is forwarded to the
 * hot-side MCU (USART, 115200, through the ISO7762) and its reply is sent
 * back on the panel bus. Address PHY_LOCAL_ADDR is answered by the PHY
 * itself (bus diagnostics). Frames with bad CRC are dropped.
 */
#ifndef PHY_CORE_H
#define PHY_CORE_H

#include <stdbool.h>
#include <stdint.h>

#define PHY_OVERSAMPLE   8u
#define PHY_BAUD         2400u
#define PHY_FS           (PHY_BAUD * PHY_OVERSAMPLE)       /* 19200 Hz tick */
#define PHY_GAP_TICKS    (35u * PHY_OVERSAMPLE / 10u * 10u) /* 3.5 chars of 10 bits */
#define PHY_LOCAL_ADDR   247u
#define PHY_FRAME_MAX    128u
#define PHY_FW_VERSION   0x0100u

typedef struct {
    /* receiver */
    float    base_ma, thr_ma;      /* idle current, slicing offset */
    uint8_t  rx_state, rx_bit, rx_byte, rx_cnt, ones;
    uint8_t  rx_buf[PHY_FRAME_MAX];
    uint16_t rx_len;
    uint32_t idle_ticks;
    uint16_t space_run;
    bool     rx_frame_ready;
    /* transmitter */
    uint8_t  tx_buf[PHY_FRAME_MAX];
    uint16_t tx_len, tx_pos;
    uint8_t  tx_bit, tx_sub;
    bool     tx_active;
    uint16_t blank;                /* ticks of RX blanking left */
    bool     line_space;           /* output: true -> drive 16 V (PB_TX high) */
    /* hot-side link */
    uint8_t  hot_buf[PHY_FRAME_MAX];
    uint16_t hot_len;
    bool     waiting_hot;
    uint32_t wait_ms;
    /* statistics / local registers */
    uint32_t frames_rx, frames_crc, frames_fwd, replies, timeouts, framing_err, baseline_jumps;
    float    vbus_v, i_ma;
} phy_t;

void phy_init(phy_t *p);
/* 19.2 kHz: one return-current sample in mA; returns the line level to drive */
bool phy_tick(phy_t *p, float i_ma);
/* main loop: frame handling. send_hot() writes to the hot-side USART. */
void phy_poll(phy_t *p, uint32_t now_ms, void (*send_hot)(const uint8_t *, uint16_t));
/* bytes from the hot-side USART, and its frame end (receiver timeout) */
void phy_hot_byte(phy_t *p, uint8_t b);
void phy_hot_gap(phy_t *p);

uint16_t phy_crc16(const uint8_t *d, uint16_t n);

#endif
