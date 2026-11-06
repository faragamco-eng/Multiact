/*
 * test_phy.c - PHY bridge: panel frames encoded as return-current
 * modulation (with baseline drift, noise, clock error), decoded by the PHY,
 * bridged to the real drive Modbus slave, the reply decoded from the
 * PHY's voltage modulation the way a panel would.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phy_core.h"
#include "modbus_slave.h"
#include "cooler_regs.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL line %d: %s  ", __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

static phy_t P;
static mb_slave_t MB;
static cooler_regs_t REGS;
static const mb_regs_t CB = { cooler_read_holding, cooler_read_input, cooler_write_holding, &REGS };
static double t;                       /* s */
static uint32_t ms;
static double base = 35.0, drift = 0.0, noise = 1.5, clock_err = 0.0;
static uint8_t panel_rx[256];
static int panel_rx_n;

/* fake hot side: answer immediately through the PHY's hot-link API */
static void send_hot(const uint8_t *f, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) mb_rx_byte(&MB, f[i]);
    if (mb_rx_gap(&MB)) { for (uint16_t i = 0; i < MB.tx_len; i++) phy_hot_byte(&P, MB.tx[i]); phy_hot_gap(&P); }
}

/* panel-side UART receiver on the bus voltage (ideal comparator at 20 V) */
static int prx_state, prx_cnt, prx_bit; static uint8_t prx_byte;
static void panel_rx_tick(bool space)
{
    bool bit = !space;
    switch (prx_state) {
    case 0: if (!bit) { prx_state = 1; prx_cnt = 0; } break;
    case 1: if (++prx_cnt == 4) { prx_state = bit ? 0 : 2; prx_cnt = 0; prx_bit = 0; prx_byte = 0; } break;
    case 2: if (++prx_cnt == 8) { prx_cnt = 0; prx_byte = (uint8_t)((prx_byte >> 1) | (bit ? 0x80 : 0)); if (++prx_bit == 8) prx_state = 3; } break;
    case 3: if (++prx_cnt == 8) { if (bit) panel_rx[panel_rx_n++] = prx_byte; prx_state = 0; } break;
    }
}

static double gauss(void) { double u = (rand() + 1.0) / (RAND_MAX + 2.0), v = rand() / (RAND_MAX + 1.0); return sqrt(-2 * log(u)) * cos(2 * M_PI * v); }

static double filt;
static void tick(bool panel_space)
{
    double i = base + drift * sin(2 * M_PI * 0.2 * t) + (panel_space ? 20.0 : 0.0) + noise * gauss();
    filt += (i - filt) * 0.85;                 /* RC 1k x 10 nF + ADC sampling */
    bool space = phy_tick(&P, (float)filt);
    panel_rx_tick(space);
    t += 1.0 / PHY_FS;
    uint32_t nms = (uint32_t)(t * 1000);
    if (nms != ms) { ms = nms; phy_poll(&P, ms, send_hot); }
}

/* panel transmits a frame (its own clock, possibly off by clock_err) */
static void panel_send(const uint8_t *f, int n)
{
    double tb = 1.0 / PHY_BAUD * (1.0 + clock_err), t0 = t;
    int nbits = n * 10;
    while (t - t0 < nbits * tb) {
        int k = (int)((t - t0) / tb), byte = k / 10, b = k % 10;
        bool level = b == 0 ? false : b == 9 ? true : ((f[byte] >> (b - 1)) & 1);
        tick(!level);
    }
}
static void idle(double s) { double t0 = t; while (t - t0 < s) tick(false); }

static int request(const uint8_t *pdu, int n, uint8_t *reply)
{
    uint8_t f[260];
    memcpy(f, pdu, n);
    uint16_t c = phy_crc16(f, n);
    f[n] = (uint8_t)c; f[n + 1] = (uint8_t)(c >> 8);
    panel_rx_n = 0;
    panel_send(f, n + 2);
    idle(0.3);
    memcpy(reply, panel_rx, panel_rx_n);
    return panel_rx_n;
}

static void scenario(const char *name, double b, double dr, double nz, double ce)
{
    base = b; drift = dr; noise = nz; clock_err = ce;
    idle(0.05);
    uint32_t ok = 0, total = 40;
    for (uint32_t k = 0; k < total; k++) {
        REGS.ir[IR_VBUS_V] = (uint16_t)(380 + k);
        uint8_t req[6] = {1, 4, 0, IR_VBUS_V, 0, 3}, r[64];
        int n = request(req, 6, r);
        if (n == 11 && r[0] == 1 && r[1] == 4 && r[2] == 6 && (uint32_t)(r[3] << 8 | r[4]) == 380 + k &&
            phy_crc16(r, 9) == (r[9] | r[10] << 8)) ok++;
    }
    printf("  %-44s %2u/%u transactions ok (baseline %.1f mA)\n", name, ok, total, P.base_ma);
    CHECK(ok == total, "%s", name);
}

int main(void)
{
    srand(5);
    phy_init(&P);
    cooler_regs_init(&REGS);
    mb_init(&MB, COOLER_MB_ADDR, &CB);
    idle(0.5);
    printf("PHY bridge (2400 bps current/voltage modulation)\n");
    scenario("nominal", 35, 0, 1.5, 0);
    scenario("panel clock +2.5 %", 35, 0, 1.5, 0.025);
    scenario("panel clock -2.5 %", 35, 0, 1.5, -0.025);
    scenario("noise 3 mA rms", 35, 0, 3.0, 0);
    scenario("idle current +-8 mA swing, 10 mA/s (Wi-Fi bursts)", 40, 8.0, 1.5, 0.01);
    scenario("idle current step 40 -> 70 mA (no settling)", 70, 0, 1.5, 0);
    drift = 0;

    /* write through the bridge */
    base = 35; noise = 1.5; clock_err = 0;
    idle(2.0);
    uint8_t w[6] = {1, 6, 0, HR_SPEED_PERMILLE, 0x02, 0x58}, r[64];
    int n = request(w, 6, r);
    CHECK(n == 8 && REGS.hr[HR_SPEED_PERMILLE] == 600, "write single (n %d, hr %u)", n, REGS.hr[HR_SPEED_PERMILLE]);
    /* local PHY diagnostics */
    P.vbus_v = 23.9f;
    uint8_t l[6] = {PHY_LOCAL_ADDR, 4, 0, 0, 0, 3};
    n = request(l, 6, r);
    CHECK(n == 11 && (r[3] << 8 | r[4]) == PHY_FW_VERSION && (r[5] << 8 | r[6]) == 239, "local registers (n %d)", n);
    /* corrupted frame: dropped, not forwarded */
    uint32_t fwd = P.frames_fwd, crc = P.frames_crc;
    uint8_t bad[8] = {1, 4, 0, 0, 0, 1, 0x12, 0x34};
    panel_rx_n = 0;
    panel_send(bad, 8);
    idle(0.25);
    CHECK(P.frames_fwd == fwd && P.frames_crc == crc + 1 && panel_rx_n == 0, "bad CRC dropped");
    /* hot side silent (drive resetting) -> timeout counted, bus free again */
    uint32_t to = P.timeouts;
    MB.address = 9;                        /* drive does not answer */
    uint8_t q[6] = {1, 4, 0, 0, 0, 1};
    n = request(q, 6, r);
    CHECK(n == 0 && P.timeouts == to + 1, "timeout (n %d)", n);
    MB.address = 1;
    n = request(q, 6, r);
    CHECK(n == 7, "recovers after timeout (n %d)", n);
    printf("phy tests: %s (%d checks, %d failed)\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails != 0;
}
