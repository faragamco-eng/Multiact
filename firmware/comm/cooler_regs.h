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
#define COOLER_PROTO_VER    2u

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
    IR_COUNT
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
} cooler_regs_t;

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
