/*
 * drive.c - Application layer of the v2 cooler drive. See drive.h.
 */
#include "drive.h"
#include "motorlib.h"
#include <math.h>
#include <string.h>

#define VBUS_NOM 390.0f

void drive_default_cfg(drive_cfg_t *c)
{
    c->vin_start_min = 160.0f;  c->vin_start_max = 265.0f;
    c->vin_ov_rms = 280.0f;     c->vin_ov_return = 265.0f;
    c->vin_uv_stop = 130.0f;    c->vin_uv_derate = 150.0f;
    c->iin_max = 8.0f;          c->p_max = 1250.0f;
    c->vbus_ov = 445.0f;        c->vbus_pfc_ok = 360.0f;
    c->t_hs_derate = 85.0f;     c->t_hs_stop = 100.0f;   c->t_hs_return = 80.0f;
    c->t_mot_derate = 130.0f;   c->t_mot_stop = 150.0f;  c->t_mot_return = 110.0f;
    c->cap_tol = 0.12f;
    c->pfc_idle_off_ms = 30000u;
}

void params_defaults(params_t *p)
{
    memset(p, 0, sizeof *p);
    cooler_regs_t tmp;
    cooler_regs_init(&tmp);
    for (unsigned i = 0; i < PARAMS_HR_SAVED; i++) p->hr[i] = tmp.hr[i];
    p->hr[HR_RUN] = 0;
    p->motor_id = 0xFFFFu;
}

/* ---------------------------------------------------------------- helpers */
static const uint16_t SCHED_OC[]   = { 10, 10, 10 };      /* E3 */
static const uint16_t SCHED_LOCK[] = { 10, 30, 60 };      /* E4, E7 start failure */

static uint16_t sat16(float v) { return v <= 0.0f ? 0u : v >= 65535.0f ? 65535u : (uint16_t)(v + 0.5f); }

static void log_event(drive_t *d, const drive_in_t *in, uint8_t al, uint8_t det)
{
    uint16_t rec[LOG_REC_REGS] = {
        al, det, (uint16_t)d->par->run_minutes, (uint16_t)(d->par->run_minutes >> 16),
        sat16(in->vin_rms), sat16(in->vbus), sat16(in->i_rms * 100.0f), sat16(in->t_hs + 40.0f)
    };
    cooler_log(d->regs, rec);
    memcpy(d->par->log, d->regs->log, sizeof d->par->log);
    d->par->log_count = d->regs->ir[IR_LOG_COUNT];
    d->par->log_seq = d->regs->ir[IR_LOG_SEQ];
    d->regs->ir[IR_ALARM_LATCHED] |= (uint16_t)(1u << (al & 15u));
    d->dirty = true;
}

/* event alarm with optional retry schedule */
static void raise_event(drive_t *d, const drive_in_t *in, uint8_t al, uint8_t det,
                        const uint16_t *sched, uint8_t n)
{
    if (d->latched) return;
    if (d->retry_al != al) { d->retries = 0; d->retry_al = al; }
    d->alarm = al;
    d->detail = det;
    if (sched && d->retries < n) {
        d->retry_at = in->now_ms + (uint32_t)sched[d->retries] * 1000u;
        d->retries++;
    } else {
        d->latched = true;
        d->detail |= AD_LATCHED;
    }
    log_event(d, in, al, d->detail);
}

/* ------------------------------------------------------------------ init */
void drive_init(drive_t *d, const drive_cfg_t *cfg, cooler_regs_t *regs, params_t *par)
{
    memset(d, 0, sizeof *d);
    d->cfg = *cfg;
    d->regs = regs;
    d->par = par;
    for (unsigned i = 0; i < PARAMS_HR_SAVED; i++) regs->hr[i] = par->hr[i];
    /* resume after power return only if the user wants it */
    regs->hr[HR_RUN] = (par->hr[HR_RESTORE_ON_PWR] && par->last_run) ? 1u : 0u;
    memcpy(regs->log, par->log, sizeof regs->log);
    regs->ir[IR_LOG_COUNT] = par->log_count;
    regs->ir[IR_LOG_SEQ] = par->log_seq;
    regs->ir[IR_FW_VER] = FW_VERSION;
    regs->ir[IR_SERIAL_LO] = (uint16_t)par->serial;
    regs->ir[IR_SERIAL_HI] = (uint16_t)(par->serial >> 16);
    regs->hr_dirty = false;
    d->pwr = PWR_WAIT_MAINS;
    d->id_pending = false;
    if (par->motor_valid) {
        motor_match_t m = motorlib_match(par->R, 0.5f * (par->Ld + par->Lq), par->psi, VBUS_NOM);
        d->i_limit_lib = m.i_limit;
        d->w_max_lib = m.w_max;
        d->pole_pairs = m.pole_pairs;
    }
}

bool drive_motor_busy(void *drive)
{
    const drive_t *d = drive;
    return d->last_mc != MC_IDLE && d->last_mc != MC_FAULT;
}

/* ------------------------------------------------------------ power path */
static bool relays_closed(const drive_t *d) { return d->pwr == PWR_BUS_RECT || d->pwr == PWR_BUS_PFC; }

static void power_path(drive_t *d, const drive_in_t *in, bool motor_wants_bus)
{
    const drive_cfg_t *c = &d->cfg;
    uint32_t now = in->now_ms;
    bool in_range = in->mains_valid && in->vin_rms >= c->vin_start_min && in->vin_rms <= c->vin_start_max;

    /* 1. over-voltage: fastest path, any state with relays (or precharge) closed */
    if ((in->mains_ov_fast || (in->mains_valid && in->vin_rms > c->vin_ov_rms)) && d->pwr != PWR_OV_OPEN) {
        d->pwr = PWR_OV_OPEN;
        d->pwr_t0 = now;
        d->ok_since = 0;
        return;
    }
    /* 2. outage */
    if (in->mains_outage && d->pwr != PWR_OUTAGE && d->pwr != PWR_WAIT_MAINS && d->pwr != PWR_OV_OPEN) {
        d->pwr = PWR_OUTAGE;
        d->pwr_t0 = now;
        if (d->par->outages < 0xFFFFFFFFu) d->par->outages++;
        d->par->last_run = (uint8_t)(d->regs->hr[HR_RUN] != 0u);
        d->dirty = true;
        d->save_due = now;               /* save at once (aux supply still alive) */
        return;
    }

    switch (d->pwr) {
    case PWR_OUTAGE:
    case PWR_WAIT_MAINS:
        if (d->bus_lockout || (int32_t)(now - d->hold_until) < 0) { d->ok_since = 0; break; }
        if (!in_range) { d->ok_since = 0; break; }
        if (!d->ok_since) d->ok_since = now ? now : 1u;
        if (now - d->ok_since >= 2000u) {
            d->pwr = PWR_PRECHARGE;
            d->pwr_t0 = now;
            d->v_hist = in->vbus;
            d->v_hist_t = now;
        }
        break;
    case PWR_PRECHARGE: {
        if (!in->mains_valid) { d->pwr = PWR_WAIT_MAINS; d->ok_since = 0; break; }
        /* done when the bus is near the line peak and no longer rising */
        if (now - d->v_hist_t >= 100u) {
            float dv = in->vbus - d->v_hist;
            d->v_hist = in->vbus;
            d->v_hist_t = now;
            if (in->vbus > 0.85f * 1.4142f * in->vin_rms && dv < 3.0f) {
                d->pwr = PWR_BUS_RECT;       /* K1 closes now, K2 opens 100 ms later */
                d->pwr_t0 = now;
                break;
            }
        }
        if (now - d->pwr_t0 > 5000u) {       /* PTC open / bus shorted / caps failed */
            d->pwr = PWR_WAIT_MAINS;
            d->ok_since = 0;
            d->hold_until = now + 60000u;    /* let the PTC cool before retrying */
            if (!d->latched) raise_event(d, in, AL_BUS_PFC, AD_PRECHARGE, SCHED_OC, 3);
        }
        break;
    }
    case PWR_BUS_RECT:
        if (motor_wants_bus) { d->pwr = PWR_BUS_PFC; d->pfc_t0 = now; }
        break;
    case PWR_BUS_PFC:
        if (motor_wants_bus) d->motor_idle_since = 0;
        else if (!d->motor_idle_since) d->motor_idle_since = now;
        if (d->motor_idle_since && now - d->motor_idle_since > c->pfc_idle_off_ms) {
            d->pwr = PWR_BUS_RECT;
            d->motor_idle_since = 0;
            break;
        }
        if (now - d->pfc_t0 > 1500u && now - d->pfc_t0 < 1600u && in->vbus < c->vbus_pfc_ok)
            raise_event(d, in, AL_BUS_PFC, AD_PFC_NO_BOOST, SCHED_OC, 3);
        break;
    case PWR_OV_OPEN:
        if (in->mains_valid && in->vin_rms < c->vin_ov_return && !in->mains_ov_fast) {
            if (!d->ok_since) d->ok_since = now ? now : 1u;
            if (now - d->ok_since >= 5000u) { d->pwr = PWR_WAIT_MAINS; d->ok_since = 0; }
        } else d->ok_since = 0;
        break;
    }
}

/* ----------------------------------------------------------- tick */
void drive_tick_1ms(drive_t *d, const drive_in_t *in, drive_out_t *o)
{
    const drive_cfg_t *c = &d->cfg;
    cooler_regs_t *r = d->regs;
    uint32_t now = in->now_ms;
    memset(o, 0, sizeof *o);

    /* ---- commands from the panel */
    if (r->clear_req) {
        r->clear_req = false;
        d->latched = false;
        d->alarm = AL_NONE;
        d->detail = 0;
        d->retries = 0;
        d->retry_al = AL_NONE;
        r->ir[IR_ALARM_LATCHED] = 0;
        o->mc_clear = true;
        o->clear_brk_oc = true;
        d->bus_lockout = false;
        d->cap_bad_ms = 0;
    }
    if (r->identify_req) { r->identify_req = false; d->id_pending = true; }
    if (r->hr_dirty) {
        r->hr_dirty = false;
        for (unsigned i = 0; i < PARAMS_HR_SAVED; i++) d->par->hr[i] = r->hr[i];
        d->par->last_run = (uint8_t)(r->hr[HR_RUN] != 0u);
        d->dirty = true;
        d->save_due = now + 10000u;          /* debounce: fewer flash writes */
    }

    /* ---- motor faults -> event alarms */
    if (in->mc_state == MC_FAULT && d->last_mc != MC_FAULT) {
        uint32_t f = in->mc_faults;
        bool supply_event = (d->pwr == PWR_OV_OPEN || d->pwr == PWR_OUTAGE || d->pwr == PWR_WAIT_MAINS);
        if ((f & MCF_HW) && in->brk_thermal)
            ;                                 /* handled as condition alarm E6 below */
        else if ((f & MCF_OVERCURRENT) || (f & MCF_HW))
            raise_event(d, in, AL_OVERCURRENT, in->brk_oc ? AD_OC_COMPARATOR : AD_OC_SOFTWARE, SCHED_OC, 3);
        else if (f & MCF_LOCKED)
            raise_event(d, in, AL_LOCKED_ROTOR, AD_NONE, SCHED_LOCK, 3);
        else if (f & MCF_PHASE_LOSS)
            raise_event(d, in, AL_PHASE_LOSS, AD_PHASE_LOSS, 0, 0);
        else if (f & MCF_START)
            raise_event(d, in, AL_PHASE_LOSS, AD_START_FAIL, SCHED_LOCK, 3);
        else if (f & MCF_ID)
            raise_event(d, in, AL_PHASE_LOSS, AD_ID_FAIL, 0, 0);
        else if ((f & (MCF_OVERVOLT | MCF_UNDERVOLT)) && !supply_event)
            raise_event(d, in, AL_BUS_PFC, (f & MCF_OVERVOLT) ? AD_BUS_OV : AD_BUS_UV, SCHED_OC, 3);
        if (supply_event || ((f & MCF_HW) && in->brk_thermal)) o->mc_clear = true;
    }
    if (in->mc_state == MC_FAULT && d->alarm == AL_NONE && !o->mc_clear) o->mc_clear = true;

    /* ---- bus over-voltage independent of the motor */
    if (relays_closed(d) && in->vbus > c->vbus_ov && d->alarm != AL_BUS_PFC)
        raise_event(d, in, AL_BUS_PFC, AD_BUS_OV, SCHED_OC, 3);

    /* ---- series capacitor balance (aged / leaky capacitor) */
    if (in->vbus > 200.0f && fabsf(in->vcap_low - in->vbus / 3.0f) > c->cap_tol * in->vbus / 3.0f) {
        if (++d->cap_bad_ms == 2000u) {
            d->latched = false;              /* more important than any other alarm */
            raise_event(d, in, AL_BUS_PFC, AD_CAP_IMBALANCE, 0, 0);
            d->bus_lockout = true;           /* relays stay open until acknowledged */
            d->pwr = PWR_WAIT_MAINS;
        }
    } else d->cap_bad_ms = 0;

    /* ---- retry timer */
    if (d->alarm && !d->latched && d->retry_at && (int32_t)(now - d->retry_at) >= 0) {
        d->alarm = AL_NONE;
        d->detail = 0;
        d->retry_at = 0;
        o->mc_clear = true;
        o->clear_brk_oc = true;
    }
    /* a long successful run resets the retry budget */
    if (in->mc_state == MC_RUN) {
        if (!d->run_ok_since) d->run_ok_since = now ? now : 1u;
        if (now - d->run_ok_since > 600000u) { d->retries = 0; d->retry_al = AL_NONE; }
    } else d->run_ok_since = 0;

    /* ---- temperatures */
    if (in->t_hs >= c->t_hs_stop) d->hs_hot = true;
    else if (in->t_hs < c->t_hs_return) d->hs_hot = false;
    {   /* winding rise: first order, tau 15 min, 80 K at the rated current */
        float irated = d->i_limit_lib > 0.0f ? d->i_limit_lib / 1.3f / 1.4142f : 4.0f;
        float target = 80.0f * (in->i_rms * in->i_rms) / (irated * irated);
        d->t_mot_rise += (target - d->t_mot_rise) * (0.001f / 900.0f);
        float t_mot = 40.0f + d->t_mot_rise;
        if (t_mot >= c->t_mot_stop) d->mot_hot = true;
        else if (t_mot < c->t_mot_return) d->mot_hot = false;
        r->ir[IR_T_MOTOR_C] = sat16(t_mot + 40.0f);
    }

    /* ---- condition alarms (clear themselves when the condition is gone) */
    uint8_t cond = AL_NONE, cond_det = AD_NONE;
    if (d->pwr == PWR_OV_OPEN) cond = AL_VIN_HIGH;
    else if (in->mains_valid && in->vin_rms < c->vin_uv_stop && relays_closed(d)) cond = AL_VIN_LOW;
    else if (in->brk_thermal || d->hs_hot) { cond = AL_DRIVE_HOT; cond_det = in->brk_thermal ? AD_HS_SWITCH : AD_NONE; }
    else if (d->mot_hot) cond = AL_MOTOR_HOT;
    else if (in->comm_lost) cond = AL_COMM_LOST;
    else if (d->pwr == PWR_OUTAGE && in->module_present && in->batt_soc <= r->hr[HR_BATT_RESERVE]) cond = AL_BATT_LOW;
    if (cond != d->last_cond && cond != AL_NONE) log_event(d, in, cond, cond_det);
    d->last_cond = cond;

    uint8_t shown = d->alarm ? d->alarm : cond;
    r->ir[IR_ALARM] = shown;
    r->ir[IR_ALARM_DETAIL] = d->alarm ? d->detail : cond_det;

    /* ---- motor command */
    bool battery_mode = (d->pwr == PWR_OUTAGE && in->module_present);
    bool bus_ok = relays_closed(d) || battery_mode;
    bool want_run = r->hr[HR_RUN] && d->alarm == AL_NONE && cond == AL_NONE;
    if (d->id_pending && d->alarm == AL_NONE && cond == AL_NONE && relays_closed(d) &&
        (in->mc_state == MC_IDLE)) {
        o->mc_identify = true;
        d->id_pending = false;
        d->id_seen = false;
    }
    bool identifying = (in->mc_state == MC_IDENTIFY);
    power_path(d, in, (want_run || identifying) && !battery_mode);

    /* speed with derating */
    float spd = (float)r->hr[HR_SPEED_PERMILLE] / 1000.0f;
    if (in->t_hs > c->t_hs_derate)
        spd *= 1.0f - 0.6f * fminf(1.0f, (in->t_hs - c->t_hs_derate) / (c->t_hs_stop - c->t_hs_derate));
    float t_mot = 40.0f + d->t_mot_rise;
    if (t_mot > c->t_mot_derate)
        spd *= 1.0f - 0.6f * fminf(1.0f, (t_mot - c->t_mot_derate) / (c->t_mot_stop - c->t_mot_derate));
    if (battery_mode) spd = fminf(spd, (float)r->hr[HR_BATT_SPEED_MAX] / 1000.0f);

    /* power limit: input current, low mains, output rating */
    float p_lim = c->p_max;
    if (in->mains_valid && !battery_mode) {
        p_lim = fminf(p_lim, c->iin_max * in->vin_rms * 0.92f);
        if (in->vin_rms < c->vin_uv_derate)
            p_lim *= fmaxf(0.3f, (in->vin_rms - c->vin_uv_stop) / (c->vin_uv_derate - c->vin_uv_stop));
    }
    if (d->pwr == PWR_BUS_RECT) p_lim = fminf(p_lim, 250.0f);   /* PFC still starting */

    o->mc_run = want_run && bus_ok;
    o->mc_speed = spd;
    o->mc_p_lim = battery_mode ? 0.0f : p_lim;
    float i_user = (float)r->hr[HR_I_LIMIT_DA] / 10.0f;
    o->mc_i_max = (d->i_limit_lib > 0.0f) ? fminf(i_user, d->i_limit_lib) : i_user;
    float w_max = d->w_max_lib > 0.0f ? d->w_max_lib : 0.0f;
    uint8_t pp = r->hr[HR_POLE_PAIRS] ? (uint8_t)r->hr[HR_POLE_PAIRS] : d->pole_pairs;
    if (r->hr[HR_MAX_RPM] && pp) w_max = (float)r->hr[HR_MAX_RPM] * (float)pp * 6.2831853f / 60.0f;
    o->mc_w_max = w_max;                       /* 0 = keep the FOC default */

    /* ---- identification result */
    if (d->last_mc == MC_IDENTIFY && in->mc_state != MC_IDENTIFY && in->id_valid) {
        params_t *p = d->par;
        p->R = in->R; p->Ld = in->Ld; p->Lq = in->Lq; p->psi = in->psi; p->Je = in->Je; p->vdt = in->vdt;
        p->motor_valid = 1;
        motor_match_t m = motorlib_match(in->R, 0.5f * (in->Ld + in->Lq), in->psi, VBUS_NOM);
        p->motor_id = m.index < 0 ? 0xFFFFu : (uint16_t)m.index;
        d->i_limit_lib = m.i_limit;
        d->w_max_lib = m.w_max;
        d->pole_pairs = m.pole_pairs;
        d->dirty = true;
        d->save_due = now;
    }

    o->mains_ack_ov = (d->pwr == PWR_OV_OPEN);   /* re-latches within 0.2 ms if still too high */

    /* ---- relays and PFC */
    switch (d->pwr) {
    case PWR_PRECHARGE: o->k2 = true; break;
    case PWR_BUS_RECT:
    case PWR_BUS_PFC:
        o->k1 = true;
        o->k2 = (now - d->pwr_t0) < 100u && d->pwr == PWR_BUS_RECT;   /* overlap */
        o->pfc_on = (d->pwr == PWR_BUS_PFC);
        break;
    default: break;
    }
    bool aux_ok = relays_closed(d) && !in->comm_lost && cond != AL_VIN_HIGH && cond != AL_VIN_LOW;
    o->pump = aux_ok && r->hr[HR_PUMP];
    o->drain = aux_ok && r->hr[HR_AUX];

    /* ---- counters */
    if (in->mc_state == MC_RUN || in->mc_state == MC_STARTING) {
        if (!d->was_running && d->par->starts < 0xFFFFFFFFu) d->par->starts++;
        d->was_running = true;
        if (++d->run_ms >= 60000u) {
            d->run_ms = 0;
            d->par->run_minutes++;
            if (d->par->run_minutes % 60u == 0u) { d->dirty = true; d->save_due = now; }
        }
    } else d->was_running = false;
    float p_in = in->p_motor > 0.0f ? in->p_motor / 0.93f + 4.0f : 4.0f;
    d->energy_frac_wh += p_in * (0.001f / 3600.0f);
    if (d->energy_frac_wh >= 1.0f) { d->energy_frac_wh -= 1.0f; d->par->energy_wh++; }

    /* ---- state for the panel */
    drive_state_t ds = DS_STANDBY;
    if (d->latched) ds = DS_ALARM;
    else if (d->alarm) ds = DS_RETRY_WAIT;
    else if (in->mc_state == MC_IDENTIFY) ds = DS_IDENTIFY;
    else if (in->mc_state == MC_STARTING) ds = DS_STARTING;
    else if (in->mc_state == MC_RUN) ds = o->mc_run ? DS_RUNNING : DS_STOPPING;
    else if (cond) ds = DS_ALARM;
    r->ir[IR_STATE] = (uint16_t)ds;
    r->ir[IR_POWER_STATE] = (uint16_t)d->pwr;
    float fe = fabsf(in->w_el) / 6.2831853f;
    r->ir[IR_FREQ_DHZ] = sat16(fe * 10.0f);
    r->ir[IR_SPEED_RPM] = pp ? sat16(fe * 60.0f / (float)pp) : 0u;
    r->ir[IR_VIN_DV] = sat16(in->vin_rms * 10.0f);
    r->ir[IR_VBUS_V] = sat16(in->vbus);
    r->ir[IR_VCAP_LOW_V] = sat16(in->vcap_low);
    r->ir[IR_MAINS_DHZ] = sat16(in->vin_freq * 10.0f);
    r->ir[IR_PIN_W] = sat16(p_in);
    r->ir[IR_IMOT_CA] = sat16(in->i_rms * 100.0f);
    r->ir[IR_T_DRIVE_C] = sat16(in->t_hs + 40.0f);
    r->ir[IR_ENERGY_WH_LO] = (uint16_t)d->par->energy_wh;
    r->ir[IR_ENERGY_WH_HI] = (uint16_t)(d->par->energy_wh >> 16);
    r->ir[IR_RUN_HOURS] = sat16((float)(d->par->run_minutes / 60u));
    r->ir[IR_STARTS] = sat16((float)d->par->starts);
    r->ir[IR_OUTAGES] = sat16((float)d->par->outages);
    r->ir[IR_MOTOR_ID] = d->par->motor_valid ? d->par->motor_id : 0xFFFFu;
    if (d->par->motor_valid) {
        r->ir[IR_R_MOHM] = sat16(d->par->R * 1000.0f);
        r->ir[IR_L_UH_DIV10] = sat16(0.5f * (d->par->Ld + d->par->Lq) * 1e5f);
        r->ir[IR_FLUX_UVS] = sat16(d->par->psi * 1e4f);
    }
    r->ir[IR_PUMP_STATE] = o->pump;
    r->ir[IR_POWER_SOURCE] = (uint16_t)(d->pwr == PWR_OUTAGE ? (in->module_present ? PS_BATTERY : PS_NONE) : PS_MAINS);
    r->ir[IR_MODULE_STATUS] = in->module_present ? 1u : 0u;
    r->ir[IR_BATT_SOC] = in->batt_soc;

    /* ---- LED: heartbeat 1 Hz, alarm En = n short blinks every 8 s */
    if (shown) {
        uint32_t t = now % 8000u;
        o->led = (t / 250u) < (uint32_t)(2u * shown) && ((t / 250u) & 1u) == 0u;
    } else o->led = (now % 1000u) < 100u;

    /* ---- persistence request (main loop writes flash) */
    if (d->dirty && (int32_t)(now - d->save_due) >= 0 && now - d->last_save_ms >= 2000u) {
        o->save = true;
        d->dirty = false;
        d->last_save_ms = now;
    }

    d->last_mc = in->mc_state;
}
