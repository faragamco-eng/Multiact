/*
 * cooler_regs.h - Modbus register map of the cooler motor drive.
 * Shared by the drive firmware, the panel (ESP32) and the PC test bench.
 * Slave address 1, 2400 bps 8N1 on the 2-wire panel bus
 * (9600/19200 allowed on the factory test connector).
 *
 * Units are chosen so every value fits in an unsigned 16-bit register.
 */
#ifndef COOLER_REGS_H
#define COOLER_REGS_H

#include <stdbool.h>
#include <stdint.h>

#define COOLER_MB_ADDR      1u
#define COOLER_PROTO_VER    3u

/* ---------------- holding registers (read/write) ---------------- */
enum {
    HR_RUN            = 0,   /* 0 stop, 1 run (fan)                       */
    HR_SPEED_PERMILLE = 1,   /* 0..1000 = 0..100.0 % of max speed         */
    HR_PUMP           = 2,   /* 0 off, 1 on                               */
    HR_AUX            = 3,   /* 0/1 auxiliary relay (swing, drain valve)  */
    HR_ACCEL_S        = 4,   /* 1..120 s, 0..100 % ramp time              */
    HR_IDENTIFY       = 5,   /* write 1: run motor identification         */
    HR_CLEAR_ALARM    = 6,   /* write 1: acknowledge latched alarm        */
    HR_COMM_TIMEOUT_S = 7,   /* 10..600 s, stop fan+pump when panel lost  */
    HR_POLE_PAIRS     = 8,   /* 0 = from motor library / unknown, 1..30   */
    HR_MAX_RPM        = 9,   /* 0 = auto, else mechanical rpm at 100 %    */
    HR_I_LIMIT_DA     = 10,  /* phase current limit, 0.1 A (10..120)      */
    HR_RESTORE_ON_PWR = 11,  /* 1 = resume last state after power return  */
    /* battery / solar backup (protocol v2) */
    HR_BATT_SPEED_MAX = 12,  /* 0..1000 permille: speed cap on battery     */
    HR_BATT_PUMP      = 13,  /* 1 = keep pump running on battery          */
    HR_BATT_RESERVE   = 14,  /* 5..80 %: stop the fan at this charge       */
    HR_BATT_CHARGE_W  = 15,  /* 0..600 W: max charge power from mains      */
    HR_COUNT
};

/* ---------------- input registers (read only) ---------------- */
enum {
    IR_PROTO_VER      = 0,
    IR_FW_VER         = 1,   /* major<<8 | minor                          */
    IR_STATE          = 2,   /* drive_state_t                             */
    IR_ALARM          = 3,   /* active alarm code (0 = none, 1..9 = E1..E9)*/
    IR_ALARM_LATCHED  = 4,   /* bit n = alarm En seen since last clear    */
    IR_SPEED_RPM      = 5,   /* mechanical rpm (0 if pole pairs unknown)  */
    IR_FREQ_DHZ       = 6,   /* electrical frequency, 0.1 Hz              */
    IR_VIN_DV         = 7,   /* mains rms, 0.1 V                          */
    IR_VBUS_V         = 8,   /* DC bus, V                                 */
    IR_PIN_W          = 9,   /* input power, W                            */
    IR_IMOT_CA        = 10,  /* motor phase current rms, 0.01 A           */
    IR_T_DRIVE_C      = 11,  /* heatsink, deg C + 40 (offset)             */
    IR_T_MOTOR_C      = 12,  /* estimated winding, deg C + 40 (offset)    */
    IR_ENERGY_WH_LO   = 13,  /* 32-bit Wh counter                         */
    IR_ENERGY_WH_HI   = 14,
    IR_RUN_HOURS      = 15,
    IR_MOTOR_ID       = 16,  /* index in motor library, 0xFFFF unknown    */
    IR_R_MOHM         = 17,  /* identified phase resistance, mOhm         */
    IR_L_UH_DIV10     = 18,  /* identified phase inductance, 10 uH        */
    IR_FLUX_UVS       = 19,  /* flux linkage, 0.1 mV*s (=1e-4 V*s/rad)     */
    IR_PUMP_STATE     = 20,
    IR_COMM_ERRORS    = 21,
    /* battery / solar backup (protocol v2) */
    IR_POWER_SOURCE   = 22,  /* power_source_t                             */
    IR_BATT_SOC       = 23,  /* state of charge, %                         */
    IR_BATT_DV        = 24,  /* battery voltage, 0.1 V                     */
    IR_BATT_W         = 25,  /* battery power, W + 32768 (>32768 = discharge) */
    IR_BATT_MIN_LEFT  = 26,  /* estimated runtime at the present load, min */
    IR_SOLAR_W        = 27,  /* solar input power, W                       */
    IR_MODULE_STATUS  = 28,  /* bit0 present, bit1 hot standby, bit2 charging, bit3 fault */
    /* protocol v3: v2 board (discrete inverter, series bus capacitors) */
    IR_ALARM_DETAIL   = 29,  /* sub-code of the active alarm, see AD_*     */
    IR_VCAP_LOW_V     = 30,  /* voltage of the lowest bus capacitor, V     */
    IR_MAINS_DHZ      = 31,  /* mains frequency, 0.1 Hz                    */
    IR_STARTS         = 32,  /* motor start counter (saturating)           */
    IR_OUTAGES        = 33,  /* mains outage counter (saturating)          */
    IR_SERIAL_LO      = 34,  /* board serial number (factory parameter)    */
    IR_SERIAL_HI      = 35,
    IR_HW_REV         = 36,  /* 0x0200 = board v2.0                        */
    IR_LOG_COUNT      = 37,  /* events stored in the log (0..LOG_LEN)      */
    IR_LOG_SEQ        = 38,  /* sequence number of the newest event        */
    IR_POWER_STATE    = 39,  /* pwr_state_t (mains / precharge / bus)      */
    IR_COUNT
};

/* Event log: input registers IR_LOG_BASE + 8*k, k = 0 newest .. LOG_LEN-1.
 * Each record: code, detail, run-minutes lo, hi, vin [V], vbus [V],
 * current [0.01 A], heatsink temp [degC + 40]. */
#define IR_LOG_BASE   0x0100u
#define LOG_LEN       16u
#define LOG_REC_REGS  8u

/* Firmware update window (protocol v3). The panel streams a drive image:
 *   HR FWU_SIZE/CRC -> FWU_CMD=BEGIN -> repeated FC16 writes of
 *   {FWU_OFS_LO, FWU_OFS_HI, data[FWU_DATA_REGS]} -> FWU_CMD=COMMIT.
 * The drive refuses BEGIN while the motor runs. Status in IR FWU_*. */
#define HR_FWU_BASE   0x0200u
enum {
    HR_FWU_CMD     = HR_FWU_BASE + 0,   /* 1 begin, 2 commit, 3 abort        */
    HR_FWU_SIZE_LO = HR_FWU_BASE + 1,
    HR_FWU_SIZE_HI = HR_FWU_BASE + 2,
    HR_FWU_CRC_LO  = HR_FWU_BASE + 3,   /* CRC-32 (IEEE) of the whole image  */
    HR_FWU_CRC_HI  = HR_FWU_BASE + 4,
    HR_FWU_OFS_LO  = HR_FWU_BASE + 14,  /* byte offset of the following data */
    HR_FWU_OFS_HI  = HR_FWU_BASE + 15,
    HR_FWU_DATA    = HR_FWU_BASE + 16,  /* FWU_DATA_REGS registers, big-endian bytes */
};
#define FWU_DATA_REGS 48u
enum { IR_FWU_STATE = 0x0200, IR_FWU_RX_LO, IR_FWU_RX_HI, IR_FWU_ERROR };
enum { FWU_CMD_BEGIN = 1, FWU_CMD_COMMIT = 2, FWU_CMD_ABORT = 3 };

/* IR_ALARM_DETAIL values */
enum {
    AD_NONE = 0,
    AD_OC_SOFTWARE = 1, AD_OC_COMPARATOR = 2,          /* E3 */
    AD_HS_SWITCH = 3,                                  /* E6: thermal switch opened */
    AD_BUS_OV = 4, AD_BUS_UV = 5, AD_PRECHARGE = 6,    /* E9 */
    AD_CAP_IMBALANCE = 7, AD_PFC_NO_BOOST = 8,         /* E9 */
    AD_START_FAIL = 9, AD_PHASE_LOSS = 10, AD_ID_FAIL = 11, /* E7 */
    AD_LATCHED = 0x80                                  /* OR-ed: retries used up */
};

typedef enum {
    PS_MAINS = 0,            /* PFC supplies the bus                       */
    PS_BATTERY = 1,          /* outage: battery module supplies the bus    */
    PS_SOLAR = 2,            /* outage: solar (+battery) supplies the bus  */
    PS_NONE = 3
} power_source_t;

typedef enum {
    DS_STANDBY = 0, DS_IDENTIFY, DS_STARTING, DS_RUNNING, DS_STOPPING,
    DS_RETRY_WAIT, DS_ALARM
} drive_state_t;

/* Alarm codes shown on the panel as E1..E9 (see docs/04). */
enum {
    AL_NONE = 0, AL_VIN_HIGH = 1, AL_VIN_LOW = 2, AL_OVERCURRENT = 3,
    AL_LOCKED_ROTOR = 4, AL_MOTOR_HOT = 5, AL_DRIVE_HOT = 6,
    AL_PHASE_LOSS = 7, AL_COMM_LOST = 8, AL_BUS_PFC = 9,
    AL_BATT_LOW = 10,        /* E10: battery at reserve, fan stopped       */
    AL_MODULE = 11           /* E11: battery module fault / no comms       */
};

typedef enum {
    PWR_WAIT_MAINS = 0,      /* relays open, waiting for valid mains        */
    PWR_PRECHARGE = 1,       /* K2 + PTC charging the bus                   */
    PWR_BUS_RECT = 2,        /* K1 closed, PFC off (bus = rectified peak)   */
    PWR_BUS_PFC = 3,         /* K1 closed, PFC regulating 390 V             */
    PWR_OV_OPEN = 4,         /* mains over-voltage: relays opened           */
    PWR_OUTAGE = 5           /* mains lost (battery module may supply bus)  */
} pwr_state_t;

/* Runtime estimate shown to the user (minutes). usable_wh = remaining
 * energy above the reserve; load_w = battery discharge power. */
uint16_t cooler_batt_minutes(float usable_wh, float load_w);

/* Drive-side register image + command handling. */
typedef struct {
    uint16_t hr[HR_COUNT];
    uint16_t ir[IR_COUNT];
    uint32_t last_valid_ms;      /* time of last frame addressed to us */
    bool     identify_req;
    bool     clear_req;
    bool     hr_dirty;           /* a persistent setting changed (-> save) */
    uint16_t log[LOG_LEN][LOG_REC_REGS];
    /* optional handler for registers outside the base map (firmware update) */
    uint8_t (*ext_write)(uint16_t a, uint16_t v, void *ctx);
    uint8_t (*ext_read)(uint16_t a, uint16_t *v, bool input, void *ctx);
    void    *ext_ctx;
} cooler_regs_t;

/* add an event to the log (newest first) */
void cooler_log(cooler_regs_t *c, const uint16_t rec[LOG_REC_REGS]);

void cooler_regs_init(cooler_regs_t *c);
/* Modbus callbacks (ctx = cooler_regs_t*) */
uint8_t cooler_read_holding(uint16_t a, uint16_t *v, void *ctx);
uint8_t cooler_read_input(uint16_t a, uint16_t *v, void *ctx);
uint8_t cooler_write_holding(uint16_t a, uint16_t v, void *ctx);
/* Call once per received valid frame and periodically; returns true when
 * the panel has been silent longer than HR_COMM_TIMEOUT_S (-> AL_COMM_LOST). */
void cooler_comm_seen(cooler_regs_t *c, uint32_t now_ms);
bool cooler_comm_lost(const cooler_regs_t *c, uint32_t now_ms);

#endif /* COOLER_REGS_H */
