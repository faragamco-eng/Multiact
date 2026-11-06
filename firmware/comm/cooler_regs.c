/*
 * cooler_regs.c - Register image of the drive with range checking.
 */
#include "cooler_regs.h"
#include "modbus_slave.h"

typedef struct { uint16_t min, max, def; } range_t;

static const range_t HR_RANGE[HR_COUNT] = {
    [HR_RUN]            = {0, 1, 0},
    [HR_SPEED_PERMILLE] = {0, 1000, 500},
    [HR_PUMP]           = {0, 1, 0},
    [HR_AUX]            = {0, 1, 0},
    [HR_ACCEL_S]        = {1, 120, 20},
    [HR_IDENTIFY]       = {0, 1, 0},
    [HR_CLEAR_ALARM]    = {0, 1, 0},
    [HR_COMM_TIMEOUT_S] = {10, 600, 60},
    [HR_POLE_PAIRS]     = {0, 30, 0},
    [HR_MAX_RPM]        = {0, 3000, 0},
    [HR_I_LIMIT_DA]     = {10, 120, 60},
    [HR_RESTORE_ON_PWR] = {0, 1, 1},
    [HR_BATT_SPEED_MAX] = {0, 1000, 800},
    [HR_BATT_PUMP]      = {0, 1, 1},
    [HR_BATT_RESERVE]   = {5, 80, 15},
    [HR_BATT_CHARGE_W]  = {0, 600, 250},
};

void cooler_regs_init(cooler_regs_t *c)
{
    *c = (cooler_regs_t){0};
    for (unsigned i = 0; i < HR_COUNT; i++)
        c->hr[i] = HR_RANGE[i].def;
    c->ir[IR_PROTO_VER] = COOLER_PROTO_VER;
    c->ir[IR_MOTOR_ID] = 0xFFFFu;
    c->ir[IR_POWER_SOURCE] = PS_MAINS;
    c->ir[IR_BATT_W] = 32768u;
    c->ir[IR_HW_REV] = 0x0200u;
}

void cooler_log(cooler_regs_t *c, const uint16_t rec[LOG_REC_REGS])
{
    for (unsigned k = LOG_LEN - 1u; k > 0u; k--)
        for (unsigned j = 0; j < LOG_REC_REGS; j++) c->log[k][j] = c->log[k - 1u][j];
    for (unsigned j = 0; j < LOG_REC_REGS; j++) c->log[0][j] = rec[j];
    if (c->ir[IR_LOG_COUNT] < LOG_LEN) c->ir[IR_LOG_COUNT]++;
    c->ir[IR_LOG_SEQ]++;
}

uint8_t cooler_read_holding(uint16_t a, uint16_t *v, void *ctx)
{
    cooler_regs_t *c = ctx;
    if (a >= HR_COUNT) return c->ext_read ? c->ext_read(a, v, false, c->ext_ctx) : MB_EX_ILLEGAL_ADDRESS;
    *v = c->hr[a];
    return 0;
}

uint8_t cooler_read_input(uint16_t a, uint16_t *v, void *ctx)
{
    cooler_regs_t *c = ctx;
    if (a >= IR_LOG_BASE && a < IR_LOG_BASE + LOG_LEN * LOG_REC_REGS) {
        unsigned k = (unsigned)(a - IR_LOG_BASE);
        *v = c->log[k / LOG_REC_REGS][k % LOG_REC_REGS];
        return 0;
    }
    if (a >= IR_COUNT) return c->ext_read ? c->ext_read(a, v, true, c->ext_ctx) : MB_EX_ILLEGAL_ADDRESS;
    *v = c->ir[a];
    return 0;
}

uint8_t cooler_write_holding(uint16_t a, uint16_t v, void *ctx)
{
    cooler_regs_t *c = ctx;
    if (a >= HR_COUNT) return c->ext_write ? c->ext_write(a, v, c->ext_ctx) : MB_EX_ILLEGAL_ADDRESS;
    if (v < HR_RANGE[a].min || v > HR_RANGE[a].max) return MB_EX_ILLEGAL_VALUE;
    /* command registers trigger an action and read back as 0 */
    if (a == HR_IDENTIFY)    { if (v) c->identify_req = true; return 0; }
    if (a == HR_CLEAR_ALARM) { if (v) c->clear_req = true;    return 0; }
    if (c->hr[a] != v) c->hr_dirty = true;
    c->hr[a] = v;
    return 0;
}

void cooler_comm_seen(cooler_regs_t *c, uint32_t now_ms) { c->last_valid_ms = now_ms; }

bool cooler_comm_lost(const cooler_regs_t *c, uint32_t now_ms)
{
    return (now_ms - c->last_valid_ms) > (uint32_t)c->hr[HR_COMM_TIMEOUT_S] * 1000u;
}

uint16_t cooler_batt_minutes(float usable_wh, float load_w)
{
    if (usable_wh <= 0.0f) return 0u;
    if (load_w < 5.0f) return 0xFFFFu;               /* idle: "not limited" */
    float m = usable_wh / load_w * 60.0f;
    return m > 65000.0f ? 65000u : (uint16_t)m;
}
