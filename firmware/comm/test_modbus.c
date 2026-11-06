/*
 * test_modbus.c - Host tests for the Modbus slave + cooler register map.
 *   make -C firmware test-comm
 */
#include <stdio.h>
#include <string.h>
#include "cooler_regs.h"
#include "modbus_slave.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static cooler_regs_t regs;
static const mb_regs_t CB = { cooler_read_holding, cooler_read_input, cooler_write_holding, &regs };
static mb_slave_t mb;

/* send a request (CRC appended here) and return reply length */
static uint16_t xfer(const uint8_t *req, uint16_t n)
{
    uint8_t buf[MB_BUF];
    memcpy(buf, req, n);
    uint16_t crc = mb_crc16(buf, n);
    buf[n] = (uint8_t)crc; buf[n + 1] = (uint8_t)(crc >> 8);
    for (uint16_t i = 0; i < n + 2u; i++) mb_rx_byte(&mb, buf[i]);
    mb_rx_gap(&mb);
    if (mb.tx_len) {
        uint16_t c2 = mb_crc16(mb.tx, (uint16_t)(mb.tx_len - 2u));
        CHECK(mb.tx[mb.tx_len - 2] == (uint8_t)c2 && mb.tx[mb.tx_len - 1] == (uint8_t)(c2 >> 8));
    }
    return mb.tx_len;
}

int main(void)
{
    /* CRC reference vector from the Modbus spec: 01 03 00 00 00 0A -> C5 CD */
    const uint8_t v[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    uint16_t crc = mb_crc16(v, 6);
    CHECK((crc & 0xFF) == 0xC5 && (crc >> 8) == 0xCD);

    cooler_regs_init(&regs);
    mb_init(&mb, COOLER_MB_ADDR, &CB);
    regs.ir[IR_SPEED_RPM] = 437;
    regs.ir[IR_PIN_W] = 593;

    /* read 3 holding registers from 0: run, speed, pump */
    uint8_t r1[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x03};
    CHECK(xfer(r1, 6) == 3 + 6 + 2);
    CHECK(mb.tx[1] == 0x03 && mb.tx[2] == 6);
    CHECK(mb.tx[5] == 0x01 && mb.tx[6] == 0xF4);      /* default speed 500 */

    /* write speed 750 permille */
    uint8_t w1[] = {0x01, 0x06, 0x00, HR_SPEED_PERMILLE, 0x02, 0xEE};
    CHECK(xfer(w1, 6) == 8);
    CHECK(regs.hr[HR_SPEED_PERMILLE] == 750);

    /* out of range value -> exception 03, register unchanged */
    uint8_t w2[] = {0x01, 0x06, 0x00, HR_SPEED_PERMILLE, 0x04, 0x00};
    CHECK(xfer(w2, 6) == 5 && mb.tx[1] == 0x86 && mb.tx[2] == MB_EX_ILLEGAL_VALUE);
    CHECK(regs.hr[HR_SPEED_PERMILLE] == 750);

    /* write multiple: run=1, speed=900, pump=1 */
    uint8_t w3[] = {0x01, 0x10, 0x00, 0x00, 0x00, 0x03, 0x06, 0x00, 0x01, 0x03, 0x84, 0x00, 0x01};
    CHECK(xfer(w3, sizeof w3) == 8);
    CHECK(regs.hr[HR_RUN] == 1 && regs.hr[HR_SPEED_PERMILLE] == 900 && regs.hr[HR_PUMP] == 1);

    /* read input registers rpm and power */
    uint8_t r2[] = {0x01, 0x04, 0x00, IR_SPEED_RPM, 0x00, 0x05};
    CHECK(xfer(r2, 6) == 3 + 10 + 2);
    CHECK(((mb.tx[3] << 8) | mb.tx[4]) == 437);
    CHECK(((mb.tx[11] << 8) | mb.tx[12]) == 593);

    /* illegal address, illegal function */
    uint8_t r3[] = {0x01, 0x04, 0x00, 0x50, 0x00, 0x01};
    CHECK(xfer(r3, 6) == 5 && mb.tx[2] == MB_EX_ILLEGAL_ADDRESS);
    uint8_t r4[] = {0x01, 0x2B, 0x0E, 0x01, 0x00};
    CHECK(xfer(r4, 5) == 5 && mb.tx[1] == 0xAB && mb.tx[2] == MB_EX_ILLEGAL_FUNCTION);

    /* other address: silent; broadcast write: applied, silent */
    uint8_t r5[] = {0x02, 0x03, 0x00, 0x00, 0x00, 0x01};
    CHECK(xfer(r5, 6) == 0);
    uint8_t w4[] = {0x00, 0x06, 0x00, HR_PUMP, 0x00, 0x00};
    CHECK(xfer(w4, 6) == 0 && regs.hr[HR_PUMP] == 0);

    /* corrupted CRC: ignored */
    uint8_t bad[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
    for (unsigned i = 0; i < sizeof bad; i++) mb_rx_byte(&mb, bad[i]);
    CHECK(!mb_rx_gap(&mb) && mb.frames_crc_err == 1);

    /* command register: identify request latched, reads back 0 */
    uint8_t w5[] = {0x01, 0x06, 0x00, HR_IDENTIFY, 0x00, 0x01};
    CHECK(xfer(w5, 6) == 8 && regs.identify_req && regs.hr[HR_IDENTIFY] == 0);

    /* comm timeout */
    cooler_comm_seen(&regs, 1000);
    CHECK(!cooler_comm_lost(&regs, 1000 + 59000));
    CHECK(cooler_comm_lost(&regs, 1000 + 61000));

    /* protocol v2: battery registers */
    uint8_t r6[] = {0x01, 0x03, 0x00, HR_BATT_SPEED_MAX, 0x00, 0x04};
    CHECK(xfer(r6, 6) == 3 + 8 + 2);
    CHECK(((mb.tx[3] << 8) | mb.tx[4]) == 800 && mb.tx[6] == 1 && mb.tx[8] == 15);
    uint8_t w6[] = {0x01, 0x06, 0x00, HR_BATT_RESERVE, 0x00, 0x03};     /* 3 % < min 5 % */
    CHECK(xfer(w6, 6) == 5 && mb.tx[2] == MB_EX_ILLEGAL_VALUE);
    regs.ir[IR_POWER_SOURCE] = PS_BATTERY; regs.ir[IR_BATT_SOC] = 62;
    uint8_t r7[] = {0x01, 0x04, 0x00, IR_POWER_SOURCE, 0x00, 0x02};
    CHECK(xfer(r7, 6) == 3 + 4 + 2 && mb.tx[4] == PS_BATTERY && mb.tx[6] == 62);
    CHECK(regs.ir[IR_PROTO_VER] == COOLER_PROTO_VER);
    /* runtime: 2.3 kWh usable at 370 W -> 373 min */
    CHECK(cooler_batt_minutes(2300.0f, 370.0f) == 372 || cooler_batt_minutes(2300.0f, 370.0f) == 373);
    CHECK(cooler_batt_minutes(0.0f, 370.0f) == 0 && cooler_batt_minutes(500.0f, 0.0f) == 0xFFFF);

    printf("modbus tests: %s (%u ok frames, %u exceptions)\n", fails ? "FAIL" : "PASS",
           (unsigned)mb.frames_ok, (unsigned)mb.exceptions);
    return fails != 0;
}
