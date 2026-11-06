/*
 * motorlib.h - Library of known cooler motors, matched by the identified
 * electrical "fingerprint" (R, L, flux). A match gives the pole-pair count,
 * rated current and rated speed, which identification alone cannot know.
 *
 * The entries below are PLACEHOLDERS derived from the 36S/40P motor the
 * drive was designed around. Replace them with the values the drive reports
 * (IR_R_MOHM, IR_L_UH_DIV10, IR_FLUX_UVS) for each motor model on the
 * bench, plus a tachometer reading for the rated rpm.
 */
#ifndef MOTORLIB_H
#define MOTORLIB_H

#include <stdint.h>

typedef struct {
    const char *name;
    float R, L, psi;          /* fingerprint (phase R [ohm], L [H], psi [V*s/rad el.]) */
    uint8_t pole_pairs;
    float i_rated;            /* phase current, peak [A] */
    uint16_t rpm_rated;       /* mechanical rpm at 100 % */
} motor_entry_t;

typedef struct {
    int      index;           /* -1: unknown motor */
    float    distance;        /* normalised fingerprint distance of the best entry */
    uint8_t  pole_pairs;      /* 0 when unknown */
    float    i_limit;         /* peak current to use [A] */
    float    w_max;           /* electrical rad/s at 100 % */
} motor_match_t;

extern const motor_entry_t MOTORLIB[];
extern const unsigned MOTORLIB_N;

/* vbus: nominal bus [V] (used for the conservative profile of unknown motors) */
motor_match_t motorlib_match(float R, float L, float psi, float vbus);

#endif
