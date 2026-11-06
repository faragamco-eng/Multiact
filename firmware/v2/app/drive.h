/*
 * drive.h - Application layer of the v2 cooler drive (hot-side MCU).
 *
 * Portable: no register access. The target glue (main_v2.c) fills a
 * drive_in_t every millisecond from the BSP and the FOC core, calls
 * drive_tick_1ms() and applies drive_out_t (relays, PFC enable, FOC
 * commands). The host test (test/test_drive.c) drives the same code with
 * a simulated mains / bus / motor.
 *
 * Responsibilities
 *   - power-up: wait for valid mains -> precharge through K2+PTC -> K1
 *     -> PFC only while the motor needs it (low standby power)
 *   - mains over-voltage (440 VAC neutral loss): relays open within the
 *     half cycle, automatic return when the mains is back in range
 *   - outage: relays open, motor stopped (or kept on the battery module),
 *     state saved, automatic restart when the mains returns
 *   - alarms E1..E11 with retry schedules, latching and event log
 *   - derating: input current, low mains, heatsink and motor temperature
 *   - motor identification, motor library match, persistent parameters
 */
#ifndef DRIVE_H
#define DRIVE_H

#include <stdbool.h>
#include <stdint.h>
#include "cooler_regs.h"
#include "params.h"

#define FW_VERSION 0x0200u           /* 2.0 */

typedef enum { MC_IDLE = 0, MC_IDENTIFY, MC_STARTING, MC_RUN, MC_FAULT } mc_state_t;

/* motor-control fault bits (same values as FOC_F_* in foc.h) */
enum {
    MCF_OVERCURRENT = 1u << 0, MCF_LOCKED = 1u << 1, MCF_START = 1u << 2, MCF_PHASE_LOSS = 1u << 3,
    MCF_OVERVOLT = 1u << 4, MCF_UNDERVOLT = 1u << 5, MCF_HW = 1u << 6, MCF_ID = 1u << 7,
};

typedef struct {
    uint32_t now_ms;
    /* mains (from mains.c) */
    bool  mains_valid, mains_outage, mains_ov_fast;
    float vin_rms, vin_freq;
    /* bus and temperatures */
    float vbus, vcap_low;            /* whole bus / lowest series capacitor [V] */
    float t_hs;                      /* heatsink NTC [degC]                      */
    bool  brk_thermal;               /* heatsink switch open (BKIN, live level)  */
    bool  brk_oc;                    /* comparator break happened (latched by BSP) */
    /* motor control */
    mc_state_t mc_state;
    uint32_t mc_faults;
    float w_el;                      /* electrical speed [rad/s], signed        */
    float i_rms;                     /* phase current rms [A]                   */
    float p_motor;                   /* electrical power into the motor [W]     */
    bool  id_valid;                  /* identification result available         */
    float R, Ld, Lq, psi, Je, vdt;
    /* battery module (option) */
    bool  module_present;
    uint8_t batt_soc;
    /* communication */
    bool  comm_lost;
} drive_in_t;

typedef struct {
    bool  k1, k2, pump, drain;       /* relays                                  */
    bool  pfc_on;
    bool  mc_run;
    float mc_speed;                  /* 0..1 of mc_w_max                        */
    bool  mc_identify, mc_clear;     /* one-tick pulses                         */
    bool  mc_load_motor;             /* pulse: (re)load R..vdt below into FOC   */
    float mc_i_max, mc_w_max, mc_p_lim;
    bool  clear_brk_oc;
    bool  mains_ack_ov;              /* clear the latched fast-OV flag in mains.c */
    bool  led;
    bool  save;                      /* params changed: write flash (main loop) */
} drive_out_t;

typedef struct {
    float vin_start_min, vin_start_max;  /* allowed to connect the bus         */
    float vin_ov_rms, vin_ov_return;     /* E1                                 */
    float vin_uv_stop, vin_uv_derate;    /* E2                                 */
    float iin_max;                       /* input current limit [A rms]       */
    float p_max;                         /* drive output power limit [W]      */
    float vbus_ov, vbus_pfc_ok;
    float t_hs_derate, t_hs_stop, t_hs_return;
    float t_mot_derate, t_mot_stop, t_mot_return;
    float cap_tol;                       /* allowed deviation from VBUS/3     */
    uint32_t pfc_idle_off_ms;
} drive_cfg_t;

typedef enum { RS_IDLE = 0, RS_WAIT } retry_state_t;

typedef struct {
    drive_cfg_t cfg;
    cooler_regs_t *regs;
    params_t *par;

    pwr_state_t pwr;
    uint32_t pwr_t0, ok_since, v_hist_t;
    float    v_hist;
    uint32_t pfc_t0, motor_idle_since;

    uint8_t  alarm, detail;          /* active alarm                        */
    bool     latched;
    uint16_t seen_mask;
    uint8_t  retries;                /* retries used ...                    */
    uint8_t  retry_al;               /* ... for this alarm                   */
    uint32_t retry_at;
    uint32_t run_ok_since;
    uint32_t cap_bad_ms;
    uint32_t hold_until;             /* no precharge before this time       */
    bool     bus_lockout;            /* capacitor imbalance: keep relays open */
    uint8_t  last_cond;

    float    t_mot_rise;             /* motor winding over-temperature model [K] */
    bool     mot_hot, hs_hot;
    float    speed_ramp;
    bool     was_running;
    mc_state_t last_mc;
    bool     id_pending, id_seen;
    float    energy_frac_wh;
    uint32_t run_ms, save_due, last_save_ms;
    bool     dirty;
    float    i_limit_lib, w_max_lib;
    uint8_t  pole_pairs;
} drive_t;

void drive_default_cfg(drive_cfg_t *c);
/* par: loaded (or defaulted) parameters; regs: Modbus image */
void drive_init(drive_t *d, const drive_cfg_t *cfg, cooler_regs_t *regs, params_t *par);
void drive_tick_1ms(drive_t *d, const drive_in_t *in, drive_out_t *out);
bool drive_motor_busy(void *drive);   /* for fwu_init */
void params_defaults(params_t *p);

#endif
