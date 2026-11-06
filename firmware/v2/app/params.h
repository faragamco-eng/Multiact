/*
 * params.h - Persistent drive parameters in two flash pages.
 *
 * Records are appended (newest = highest sequence number with a good
 * CRC). When a page is full the other page is erased and used. A power
 * failure during a write leaves the previous record valid. Endurance:
 * 6 records per page x 2 pages x 10 k erase cycles = 120 k saves.
 */
#ifndef PARAMS_H
#define PARAMS_H

#include <stdbool.h>
#include <stdint.h>
#include "flash_if.h"
#include "cooler_regs.h"

#define PARAMS_HR_SAVED 16u          /* holding registers 0..15 persist */

typedef struct {
    /* user settings (holding registers) */
    uint16_t hr[PARAMS_HR_SAVED];
    /* identified motor */
    float    R, Ld, Lq, psi, Je, vdt;
    uint16_t motor_id;               /* 0xFFFF = unknown */
    uint8_t  motor_valid;
    uint8_t  last_run;               /* resume after power return */
    /* counters */
    uint32_t energy_wh, run_minutes, starts, outages;
    uint32_t serial;                 /* factory */
    uint16_t log[LOG_LEN][LOG_REC_REGS];
    uint16_t log_count, log_seq;
} params_t;

typedef struct {
    const flash_if_t *fl;
    uint32_t page[2];
    int      cur;                    /* page holding the newest record   */
    uint32_t next;                   /* address for the next record      */
    uint32_t seq;
} params_store_t;

/* returns true when a valid record was found (else p is left unchanged) */
bool params_load(params_store_t *s, const flash_if_t *fl, uint32_t page0, uint32_t page1, params_t *p);
bool params_save(params_store_t *s, const params_t *p);
uint32_t params_record_size(void);

#endif
