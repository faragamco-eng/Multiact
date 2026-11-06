/*
 * modbus_slave.h - Minimal Modbus RTU slave for the 2-wire panel bus.
 *
 * Portable C99, no hardware access. The application:
 *   1. feeds received bytes with mb_rx_byte()
 *   2. calls mb_rx_gap() when the line has been silent for >= 3.5 chars
 *      (2400 bps -> ~16 ms; use a timer restarted on every byte)
 *   3. sends the bytes left in mb->tx[0..tx_len) if tx_len > 0
 * Register storage/side effects are supplied through callbacks so the
 * same module serves the drive and the PC test bench simulator.
 *
 * Supported functions: 0x03 read holding, 0x04 read input,
 *                      0x06 write single, 0x10 write multiple.
 */
#ifndef MODBUS_SLAVE_H
#define MODBUS_SLAVE_H

#include <stdbool.h>
#include <stdint.h>

#define MB_BUF 128u

enum {
    MB_EX_ILLEGAL_FUNCTION = 1,
    MB_EX_ILLEGAL_ADDRESS  = 2,
    MB_EX_ILLEGAL_VALUE    = 3,
    MB_EX_DEVICE_FAILURE   = 4,
};

typedef struct {
    /* return 0 or an exception code */
    uint8_t (*read_holding)(uint16_t addr, uint16_t *val, void *ctx);
    uint8_t (*read_input)(uint16_t addr, uint16_t *val, void *ctx);
    uint8_t (*write_holding)(uint16_t addr, uint16_t val, void *ctx);
    void *ctx;
} mb_regs_t;

typedef struct {
    uint8_t  address;
    const mb_regs_t *regs;
    uint8_t  rx[MB_BUF];
    uint16_t rx_len;
    bool     rx_overflow;
    uint8_t  tx[MB_BUF];
    uint16_t tx_len;
    /* statistics */
    uint32_t frames_ok, frames_crc_err, frames_other_addr, exceptions;
} mb_slave_t;

uint16_t mb_crc16(const uint8_t *p, uint16_t n);
void     mb_init(mb_slave_t *mb, uint8_t address, const mb_regs_t *regs);
void     mb_rx_byte(mb_slave_t *mb, uint8_t b);
/* End of frame (3.5 char silence). Returns true if a reply is in tx[]. */
bool     mb_rx_gap(mb_slave_t *mb);

#endif /* MODBUS_SLAVE_H */
