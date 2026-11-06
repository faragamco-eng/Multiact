#include "motorlib.h"
#include <math.h>

/* PLACEHOLDER values - see motorlib.h */
const motor_entry_t MOTORLIB[] = {          /* same motors as foc/foc_sim.c */
    { "SIZO-like 36S/40P 1.2 kW",        1.6f, 15e-3f, 0.156f, 20, 6.0f, 450 },
    { "ELECTROGEN-like 36S/40P (salient)", 2.4f, 20e-3f, 0.170f, 20, 5.5f, 450 },
    { "old 12S/14P",                     3.0f, 25e-3f, 0.227f,  7, 4.5f, 900 },
    { "new design 36S/40P (low R)",      1.0f, 10e-3f, 0.140f, 20, 6.5f, 450 },
};
const unsigned MOTORLIB_N = sizeof MOTORLIB / sizeof MOTORLIB[0];

/* R changes with temperature (+-25 %), L and psi are stable: weight psi most */
motor_match_t motorlib_match(float R, float L, float psi, float vbus)
{
    motor_match_t m = { -1, 1e9f, 0, 0.0f, 0.0f };
    for (unsigned k = 0; k < MOTORLIB_N; k++) {
        const motor_entry_t *e = &MOTORLIB[k];
        float dr = logf(R / e->R) / 0.25f;
        float dl = logf(L / e->L) / 0.10f;
        float dp = logf(psi / e->psi) / 0.05f;
        float d = sqrtf(dr * dr + dl * dl + dp * dp);
        if (d < m.distance) { m.distance = d; m.index = (int)k; }
    }
    if (m.index >= 0 && m.distance < 1.0f) {
        const motor_entry_t *e = &MOTORLIB[m.index];
        m.pole_pairs = e->pole_pairs;
        m.i_limit = e->i_rated * 1.3f;
        m.w_max = (float)e->rpm_rated * (float)e->pole_pairs * 6.2831853f / 60.0f;
        return m;
    }
    /* unknown motor: conservative profile.
     * speed: back-EMF at 100 % = 75 % of the available phase voltage
     * current: limited so copper loss stays near what a 1.2 kW cooler motor sees */
    m.index = -1;
    m.w_max = 0.75f * (vbus / 1.7320508f) / psi;
    float i_cu = sqrtf(60.0f / (1.5f * R));            /* <= 60 W copper loss */
    m.i_limit = i_cu < 6.0f ? i_cu : 6.0f;
    return m;
}
