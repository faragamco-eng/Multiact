/*
 * app_config.h - Board scaling and control tuning for the 310 V DC
 *                sensorless six-step BLDC drive (STM32G431 + 600 V IPM).
 *
 * Every hardware-dependent number lives here so the same control core
 * (core/sixstep.c) can run on the target and in the host simulator.
 * Values marked [HW] must match the schematic (hardware/netlist.md).
 * Values marked [TUNE] are motor dependent and are set during bring-up.
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ------------------------------------------------------------------ */
/* Clocks / PWM                                                        */
/* ------------------------------------------------------------------ */
#define SYSCLK_HZ           170000000u
#define PWM_FREQ_HZ         16000u                    /* IGBT IPM: 16 kHz        */
#define PWM_ARR             (SYSCLK_HZ / (2u * PWM_FREQ_HZ))   /* center-aligned: 5312 */
#define PWM_PERIOD_US       (1000000.0f / PWM_FREQ_HZ)          /* 62.5 us             */
/* Dead time: DTG = 0b10xxxxxx -> DT = (64 + DTG[5:0]) * 2 * tDTS
 * tDTS = 5.88 ns -> (64+21)*2*5.88 ns = 1.0 us. IPM interlock adds its own. */
#define PWM_DTG             0x95u

/* ADC trigger lead before the center of the ON (or OFF) window, in timer
 * ticks. Injected sequence of 4 channels takes ~2.4 us, so start ~1.2 us
 * early to put the floating-phase conversion near the window center. */
#define ADC_TRIG_LEAD_TICKS 200u

/* Duty limits (fraction of period).                                   */
#define DUTY_MAX            0.96f   /* keep bootstrap caps refreshed      */
#define DUTY_MIN_RUN        0.03f

/* BEMF sampling mode switch (hysteresis). Below DUTY_OFF_SAMPLE the
 * floating phase is sampled at the center of the PWM OFF time and compared
 * against ~0 V; above DUTY_ON_SAMPLE it is sampled at the center of the ON
 * time and compared against Vbus/2.                                     */
#define DUTY_ON_SAMPLE      0.30f
#define DUTY_OFF_SAMPLE     0.22f

/* ------------------------------------------------------------------ */
/* Analog scaling [HW]                                                 */
/* ------------------------------------------------------------------ */
#define ADC_VREF            3.3f
#define ADC_FS              4095.0f
/* Vbus and the three BEMF dividers MUST have the same ratio:
 * 3 x 330 k (series, for creepage/voltage rating) over 7.5 k.          */
#define VDIV_RTOP           990000.0f
#define VDIV_RBOT           7500.0f
#define VDIV_RATIO          ((VDIV_RTOP + VDIV_RBOT) / VDIV_RBOT)   /* 133.0 */
#define VBUS_V_PER_COUNT    (ADC_VREF / ADC_FS * VDIV_RATIO)       /* 0.107 V */

/* DC-link shunt in the IPM common emitter return + OPAMP2 PGA x4.      */
#define SHUNT_OHM           0.033f
#define ISENSE_GAIN         4.0f
#define I_A_PER_COUNT       (ADC_VREF / ADC_FS / (SHUNT_OHM * ISENSE_GAIN))

/* NTC on the heatsink: 10 k @25 C, B = 3435, 10 k pull-up to 3.3 V.   */
#define NTC_R25             10000.0f
#define NTC_BETA            3435.0f
#define NTC_RPULLUP         10000.0f

/* ------------------------------------------------------------------ */
/* Protection thresholds                                               */
/* ------------------------------------------------------------------ */
#define VBUS_OV_V           405.0f   /* bulk caps are 450 V rated        */
#define VBUS_UV_V           180.0f
#define VBUS_START_MIN_V    230.0f   /* refuse to start below this       */
#define I_TRIP_SW_A         13.0f    /* software trip (instantaneous)    */
#define I_LIMIT_CBC_A       10.0f    /* COMP2 cycle-by-cycle limit       */
#define I_LIMIT_RUN_A       6.0f     /* [TUNE] average phase current lim */
/* IPM CIN hardware trip: Vref(CIN) ~0.5 V / 33 mOhm ~= 15 A (datasheet). */
#define T_WARN_C            85.0f    /* derate                           */
#define T_TRIP_C            100.0f

/* ------------------------------------------------------------------ */
/* Motor / start-up [TUNE]                                             */
/* ------------------------------------------------------------------ */
#define MOTOR_POLE_PAIRS    4u
#define MOTOR_MAX_RPM       4000.0f
#define MOTOR_MIN_RPM       300.0f   /* below this closed loop is unreliable */

#define PRECHARGE_MS        10u      /* all low sides on: bootstrap charge */
#define ALIGN_MS            500u
#define ALIGN_I_A           2.0f     /* phase current during alignment    */
#define RAMP_I_A            2.0f     /* phase current during the ramp     */
#define START_I_KI          0.0015f  /* duty per A per ms (align/ramp)    */
#define START_DUTY_MAX      0.50f

#define RAMP_F0_HZ          4.0f     /* electrical Hz at start of ramp    */
#define RAMP_F1_HZ          50.0f    /* electrical Hz at end of ramp      */
#define RAMP_ACC_HZ_S       40.0f    /* electrical Hz per second          */
#define RAMP_V_PER_HZ       0.80f    /* BEMF feed-forward, ~0.9 x Ke(line, V/Hz) */
#define RAMP_REG_MIN_HZ     8.0f     /* ZC sync active above (BEMF big enough) */
#define RAMP_HOLD_MS        500u     /* hold at F1 waiting for lock       */
#define RAMP_LOCK_STEPS     6u       /* consecutive good ZCs to hand over */
#define RAMP_HANDOVER_MIN_HZ 10.0f
#define START_RETRIES       3u

/* ------------------------------------------------------------------ */
/* Sensorless commutation                                              */
/* ------------------------------------------------------------------ */
#define ZC_BLANK_FRAC       0.15f    /* blank this fraction of T60 after comm */
#define ZC_BLANK_MIN_US     80u
#define ZC_HYST_COUNTS      6u       /* ~0.6 V at bus scale               */
#define ZC_OFF_THR_COUNTS   18u      /* ~2 V: threshold in OFF sampling   */
#define COMM_ADVANCE_DEG    4.0f     /* base timing advance (elec. deg)   */
#define COMM_ADV_DEG_PER_A  1.5f     /* + advance per amp of phase current */
#define COMM_ADVANCE_MAX_DEG 20.0f
#define DEMAG_CLAMP_COUNTS  40       /* terminal within ~4 V of a rail = demag */
#define COMM_DELAY_COMP_US  4u       /* ISR + filter latency compensation */
#define ZC_TIMEOUT_FRAC     2.0f     /* force commutation after 2 x T60   */
#define STALL_MISSES        6u       /* consecutive forced commutations   */

/* ------------------------------------------------------------------ */
/* Speed loop (runs at 1 kHz)                                          */
/* ------------------------------------------------------------------ */
#define SPEED_LOOP_HZ       1000u
#define SPEED_KP            0.0002f   /* duty per rpm                    */
#define SPEED_KI            0.008f   /* duty per rpm per second         */
#define SPEED_SLEW_RPM_S    1500.0f   /* reference ramp                  */
#define ILIM_KI             0.02f     /* duty per A per ms when limiting  */

#endif /* APP_CONFIG_H */
