/* SPDX-License-Identifier: GPL-3.0-or-later
 * Engine Warning Display state shared by the native adapter and renderer.
 * Values marked invalid must render as unavailable, never as a live zero.
 */
#ifndef A380X_EWD_DATA_H
#define A380X_EWD_DATA_H
#include "a380x_ewd_wd.h"

enum {
    EWD_COLOR_WHITE, EWD_COLOR_GREEN, EWD_COLOR_CYAN,
    EWD_COLOR_AMBER, EWD_COLOR_RED
};

typedef struct {
    char text[64];
    int color;
} EwdLine;

typedef struct {
    float n1, egt, thrust, commanded_thrust, throttle;
    float idle_n1, commanded_n1, throttle_n1;
    int idle_n1_valid, commanded_n1_valid, throttle_n1_valid;
    int n1_valid, egt_valid, thrust_valid, commanded_valid, throttle_valid;
    int active, n1_degraded, reverse, reverse_deployed, starting, fire, anti_ice;
    float available_thrust;
    int available_valid, running, running_valid, avail, avail_valid;
    int active_valid, reverse_valid, starting_valid, fire_valid, anti_ice_valid;
    float n1_limit, egt_limit;
    int n1_limit_valid, egt_limit_valid;
} EwdEngine;

typedef struct {
    EwdEngine engine[4];
    int powered, thrust_limit_valid, athr_valid, athr_engaged;
    float thrust_limit;
    char thrust_mode[12];
    float flex_temp;
    int flex_valid, idle;
    char bleed[32];
    EwdLine memo[16], warning[16], limit[16];
    int memo_count, warning_count, limit_count;
    /* Native FWS warning display; when valid it replaces memo/warning/limit above. */
    EwdWdView wd;
    /* Upper-area FWS / FG indications (EWD.tsx): A.FLOOR (A_FLOOR or TOGA_LK), THR LK, and the
     * engine attention getters (white normal / amber abnormal) when the FWS supplies them. */
    int a_floor, thr_lk, attention_valid, attention_normal[4], attention_abnormal[4];
} EwdData;

#endif
