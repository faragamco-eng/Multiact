/*
 * main_v2.c - Cooler drive v2 application (STM32G431, hot side).
 *
 *   ADC interrupt (16 kHz): mains supervisor + FOC current loop + PWM
 *   main loop, 1 ms:        FOC slow tasks, drive application, Modbus to
 *                           the PHY MCU (USART1), parameter storage,
 *                           firmware update, watchdog
 */
#include <math.h>
#include <string.h>
#include "bsp_v2.h"
#include "foc.h"
#include "drive.h"
#include "mains.h"
#include "params.h"
#include "fwu.h"
#include "modbus_slave.h"
#include "cooler_regs.h"

#define STG_ADDR  0x08011800u
#define SLOT_SIZE (54u * 1024u)
#define PAR_PAGE0 0x0801F000u
#define PAR_PAGE1 0x0801F800u

/* image information block at APP + 0x200 (filled by tools/fw_pack.py) */
__attribute__((section(".img_info"), used))
const img_info_t g_img_info = { IMG_MAGIC, IMG_HW_ID, FW_VERSION, 0, 0 };

static foc_t foc;
static drive_t drv;
static cooler_regs_t regs;
static params_t par;
static params_store_t pstore;
static mains_t mains;
static mb_slave_t mb;
static fwu_t fwu;
static volatile float s_vbus, s_vcap, s_ths, s_irms;

static const flash_if_t FLASH_IF = { 2048u, bsp_flash_erase, bsp_flash_program, bsp_flash_ptr };

/* ------------------------------------------------------------ 16 kHz */
void bsp_pwm_isr(const bsp_meas_t *m)
{
    mains_sample(&mains, m->vin_l, m->vin_n);
    if (bsp_brk_active() && foc.state != FOC_FAULT) foc_hw_fault(&foc);
    foc_meas_t fm;
    for (int k = 0; k < 3; k++) { fm.i[k] = m->i[k]; fm.valid[k] = m->valid[k]; }
    fm.vbus = m->vbus;
    foc_out_t o;
    foc_pwm_step(&foc, &fm, &o);
    bsp_pwm_set(o.duty, o.enable);
    s_vbus = m->vbus;
    s_vcap = m->vcap_low;
    s_ths = m->t_hs;
    float ia = sqrtf(0.5f * (foc.id * foc.id + foc.iq * foc.iq));
    s_irms += (ia - s_irms) * 0.002f;
}

/* ------------------------------------------------------------ Modbus glue */
static uint8_t ext_w(uint16_t a, uint16_t v, void *ctx) { return fwu_write_reg(ctx, a, v); }
static uint8_t ext_r(uint16_t a, uint16_t *v, bool in, void *ctx) { return fwu_read_reg(ctx, a, v, in); }
static const mb_regs_t MB_REGS = { cooler_read_holding, cooler_read_input, cooler_write_holding, &regs };

static mc_state_t mc_state(void)
{
    if (foc.state == FOC_FAULT) return MC_FAULT;
    if (foc.id_running || foc.state == FOC_ID_R || foc.state == FOC_ID_L || foc.state == FOC_ID_J) return MC_IDENTIFY;
    if (foc.state == FOC_CATCH || foc.state == FOC_START) return MC_STARTING;
    if (foc.state == FOC_RUN) return MC_RUN;
    return MC_IDLE;
}

int main(void)
{
    bsp_init();
    bsp_relays(false, false, false, false);
    bsp_pfc(false);

    if (!params_load(&pstore, &FLASH_IF, PAR_PAGE0, PAR_PAGE1, &par)) params_defaults(&par);
    cooler_regs_init(&regs);
    drive_cfg_t dcfg;
    drive_default_cfg(&dcfg);
    drive_init(&drv, &dcfg, &regs, &par);

    foc_cfg_t fcfg;
    foc_default_cfg(&fcfg, 1.0f / (float)PWM_HZ);
    fcfg.v_deadtime = 4.0f;                    /* first guess, self-calibrated by the FOC */
    foc_motor_t mot = { par.R, par.Ld, par.Lq, par.psi, par.Je, par.vdt, par.motor_valid != 0 };
    foc_init(&foc, &fcfg, &mot);

    mains_init(&mains, (float)PWM_HZ, 290.0f * 1.41421356f);
    fwu_init(&fwu, &FLASH_IF, STG_ADDR, SLOT_SIZE, drive_motor_busy, &drv);
    regs.ext_write = ext_w; regs.ext_read = ext_r; regs.ext_ctx = &fwu;
    mb_init(&mb, COOLER_MB_ADDR, &MB_REGS);

    bsp_current_offset_calibrate();

    uint32_t last = bsp_ms();
    bool save_pending = false;
    uint32_t frames_ok = 0;
    for (;;) {
        /* --- Modbus (bytes from the PHY bridge) */
        int c;
        while ((c = bsp_uart_getc()) >= 0) mb_rx_byte(&mb, (uint8_t)c);
        if (bsp_uart_gap()) {
            if (mb_rx_gap(&mb)) bsp_uart_send(mb.tx, mb.tx_len);
            if (mb.frames_ok != frames_ok) { frames_ok = mb.frames_ok; cooler_comm_seen(&regs, bsp_ms()); }
            regs.ir[IR_COMM_ERRORS] = (uint16_t)mb.frames_crc_err;
        }

        uint32_t now = bsp_ms();
        if (now == last) continue;
        last = now;

        /* --- 1 ms tasks */
        foc_tick_1ms(&foc);
        drive_in_t in;
        memset(&in, 0, sizeof in);
        in.now_ms = now;
        in.mains_valid = mains.valid; in.mains_outage = mains.outage; in.mains_ov_fast = mains.ov_fast;
        in.vin_rms = mains.rms; in.vin_freq = mains.freq;
        in.vbus = s_vbus; in.vcap_low = s_vcap; in.t_hs = s_ths;
        in.brk_thermal = bsp_brk_thermal();
        in.brk_oc = bsp_brk_oc_latched();
        in.mc_state = mc_state();
        in.mc_faults = foc.faults;
        in.w_el = foc.w;
        in.i_rms = s_irms;
        in.p_motor = foc.p_lp;
        in.id_valid = foc.mot.valid;
        in.R = foc.mot.R; in.Ld = foc.mot.Ld; in.Lq = foc.mot.Lq; in.psi = foc.mot.psi;
        in.Je = foc.mot.Je; in.vdt = foc.mot.vdt;
        in.comm_lost = cooler_comm_lost(&regs, now);
        in.module_present = false;            /* battery module: later option (RS-485 on the PHY) */

        drive_out_t out;
        drive_tick_1ms(&drv, &in, &out);

        if (out.mains_ack_ov) mains_clear_ov(&mains);
        bsp_relays(out.k1, out.k2, out.pump, out.drain);
        bsp_pfc(out.pfc_on);
        bsp_led(out.led);
        if (out.clear_brk_oc && !bsp_brk_thermal()) bsp_brk_clear();
        if (out.mc_clear) foc_clear_faults(&foc);
        if (out.mc_i_max > 0.0f) { foc.cfg.i_max = out.mc_i_max; foc.cfg.i_trip = out.mc_i_max * 1.8f; }
        if (out.mc_w_max > 0.0f) foc.cfg.w_max = out.mc_w_max;
        foc_set_power_limit(&foc, out.mc_p_lim);
        foc_set_speed(&foc, out.mc_speed);
        if (out.mc_identify) foc_identify(&foc);
        else foc_run(&foc, out.mc_run);

        /* --- flash: never while the motor runs (single bank: the CPU stalls during writes) */
        if (out.save) save_pending = true;
        if (save_pending && (in.mc_state == MC_IDLE || in.mc_state == MC_FAULT)) {
            params_save(&pstore, &par);
            save_pending = false;
        }
        fwu_poll(&fwu);
        if (fwu.reboot_req && !bsp_uart_tx_busy()) {
            bsp_relays(false, false, false, false);
            bsp_reset();
        }
        bsp_wdg_kick();
    }
}
