#include "mains.h"
#include <math.h>

void mains_init(mains_t *m, float fs, float ov_peak)
{
    *m = (mains_t){0};
    m->fs = fs;
    m->ov_peak = ov_peak;
    m->hyst = 15.0f;
    m->outage = true;
}

void mains_clear_ov(mains_t *m) { m->ov_fast = false; m->ov_n = 0; }

void mains_sample(mains_t *m, float v_l, float v_n)
{
    float v = v_l - v_n;
    float a = fabsf(v);

    /* fast over-voltage: 3 consecutive samples above the trip level */
    if (a > m->ov_peak) { if (++m->ov_n >= 3u) m->ov_fast = true; }
    else m->ov_n = 0;

    m->acc2 += v * v;
    if (a > m->pk_run) m->pk_run = a;
    m->n++;
    m->n_since_zc++;

    /* rising zero crossing with hysteresis closes a full cycle */
    int8_t s = m->sign;
    if (v > m->hyst) s = 1;
    else if (v < -m->hyst) s = -1;
    if (s != m->sign) {
        if (s == 1 && m->sign == -1) {
            float f = m->fs / (float)m->n;
            if (f > 35.0f && f < 75.0f) {
                m->rms = sqrtf(m->acc2 / (float)m->n);
                m->peak = m->pk_run;
                m->freq = f;
                m->valid = true;
                m->outage = false;
                m->cycles++;
            }
            m->acc2 = 0.0f;
            m->pk_run = 0.0f;
            m->n = 0;
        }
        m->n_since_zc = 0;
        m->sign = s;
    }
    /* no crossing for 30 ms (1.5 periods at 50 Hz) -> outage */
    if ((float)m->n_since_zc > 0.030f * m->fs) {
        m->outage = true;
        m->valid = false;
        m->rms = 0.0f;
        m->acc2 = 0.0f;
        m->pk_run = 0.0f;
        m->n = 0;
        m->n_since_zc = 0;
    }
    if ((float)m->n > 0.05f * m->fs) {           /* runaway cycle (DC, noise) */
        m->acc2 = 0.0f; m->pk_run = 0.0f; m->n = 0;
    }
}
