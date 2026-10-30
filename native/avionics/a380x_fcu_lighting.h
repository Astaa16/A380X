/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef A380X_FCU_LIGHTING_H
#define A380X_FCU_LIGHTING_H
#include <math.h>
#include "a380x_guidance.h"
enum { FCU_LIGHT_AP1, FCU_LIGHT_AP2, FCU_LIGHT_FD, FCU_LIGHT_ATHR,
       FCU_LIGHT_LOC, FCU_LIGHT_ALT, FCU_LIGHT_APPR, FCU_LIGHT_COUNT };
/* Active bars are separate from the legend backlight and LCD dimmer. The
 * 25% night minimum is a port readability setting, not a measured A380 curve. */
static inline void a380x_fcu_light_levels(const A380XGuidanceState *s,
                                         int powered, float integral,
                                         float levels[FCU_LIGHT_COUNT])
{
    float dim = isfinite(integral) ? fminf(1, fmaxf(0, integral)) : 0;
    float intensity = powered ? .25f + .75f * dim : 0;
    levels[FCU_LIGHT_AP1] = s->ap1 ? intensity : 0;
    levels[FCU_LIGHT_AP2] = s->ap2 ? intensity : 0;
    levels[FCU_LIGHT_FD] = (s->fd1 || s->fd2) ? intensity : 0;
    levels[FCU_LIGHT_ATHR] = s->athr_armed ? intensity : 0;
    levels[FCU_LIGHT_LOC] = (s->loc_armed || s->lateral_mode == A380X_C1_LOC
                           || s->lateral_mode == A380X_C1_LOC_STAR) ? intensity : 0;
    levels[FCU_LIGHT_ALT] = (s->guidance_valid && s->vertical_mode == A380X_B1_ALT) ? intensity : 0;
    levels[FCU_LIGHT_APPR] = s->approach_selected ? intensity : 0;
}
#endif
