/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "a380x_adirs.h"
#include <math.h>
#include <string.h>

static int valid_unit(int n) { return n >= 0 && n < A380X_ADIRS_UNITS; }
static double bounded(double x, double lo, double hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static double heading(double x) {
    x = fmod(x, 360.0);
    return x < 0.0 ? x + 360.0 : x;
}
static double heading_delta(double a, double b) {
    double d = heading(a) - heading(b);
    return d > 180.0 ? d - 360.0 : (d < -180.0 ? d + 360.0 : d);
}
static double nav_duration(const A380XAdirsInputs *i) {
    if (isfinite(i->nav_alignment_seconds) && i->nav_alignment_seconds > 0.0)
        return bounded(i->nav_alignment_seconds, .01, 86400.0);
    double latitude = isfinite(i->latitude_deg) ? fabs(i->latitude_deg) : 0.0;
    if (latitude > 73.0) return 1020.0;
    if (latitude > 60.0) return 600.0;
    return 300.0 / cos(latitude * .017453292519943295);
}
static double att_duration(const A380XAdirsInputs *i) {
    if (isfinite(i->att_alignment_seconds) && i->att_alignment_seconds > 0.0)
        return bounded(i->att_alignment_seconds, .01, 86400.0);
    return 28.0;
}
static int stationary(const A380XAdirsInputs *i) {
    return i->on_ground && isfinite(i->ground_speed_knots) &&
        fabs(i->ground_speed_knots) <= .5 && isfinite(i->latitude_deg) &&
        fabs(i->latitude_deg) <= 82.0;
}
static void reset_alignment(A380XAdirsUnitState *u) {
    u->aligned = u->att_ready = u->aligning = 0;
    u->alignment_elapsed = u->alignment_remaining = u->alignment_progress = 0.0;
    u->att_heading_entered = u->att_heading_required = u->att_heading_reminder = 0;
    u->heading_age_seconds = 0.0;
    u->previous_raw_heading_valid = 0;
}

void a380x_adirs_init(A380XAdirsState *s, const A380XAdirsInputs *i, int hot_start) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->adr_source[1] = s->ir_source[1] = 1;
    for (int n = 0; n < A380X_ADIRS_UNITS; ++n) {
        A380XAdirsUnitState *u = &s->unit[n];
        u->mode = hot_start ? A380X_ADIRS_NAV : A380X_ADIRS_OFF;
        u->knob_ratio = hot_start ? .5 : 0.0;
        if (hot_start) {
            u->aligned = u->att_ready = 1;
            u->alignment_progress = 1.0;
            u->alignment_elapsed = i ? nav_duration(i) : 300.0;
            u->previous_ir_running = i && i->unit[n].ir_powered && !i->unit[n].ir_fault;
        }
    }
    if (i) a380x_adirs_update(s, i, 0.0);
}

void a380x_adirs_update(A380XAdirsState *s, const A380XAdirsInputs *i, double dt) {
    if (!s || !i) return;
    if (!isfinite(dt) || dt < 0.0 || i->paused) dt = 0.0;
    dt = bounded(dt, 0.0, 86400.0);
    double anim = 1.0 - exp(-18.0 * dt);
    s->on_batt = 0;
    for (int n = 0; n < A380X_ADIRS_UNITS; ++n) {
        A380XAdirsUnitState *u = &s->unit[n];
        const A380XAdirsUnitInputs *v = &i->unit[n];
        int switched_on = u->mode != A380X_ADIRS_OFF;
        int ir_running = switched_on && v->ir_powered && !v->ir_fault;
        int adr_running = switched_on && v->adr_powered && !v->adr_fault;
        int raw_heading_valid = v->heading_sensor_valid && isfinite(v->heading_deg);
        u->knob_ratio += (.5 * u->mode - u->knob_ratio) * anim;
        u->ir_press_ratio += ((u->ir_pressed ? 1.0 : 0.0) - u->ir_press_ratio) * anim;
        u->adr_press_ratio += ((u->adr_pressed ? 1.0 : 0.0) - u->adr_press_ratio) * anim;
        u->ir_fault_lit = switched_on && v->ir_powered && v->ir_fault;
        u->adr_fault_lit = switched_on && v->adr_powered && v->adr_fault;
        u->ir_off_lit = v->ir_powered && u->ir_off;
        u->adr_off_lit = v->adr_powered && u->adr_off;
        u->adr_valid = adr_running && !u->adr_off && v->adr_sensor_valid;
        u->attitude_valid = u->heading_valid = u->nav_valid = 0;

        if (!ir_running) {
            reset_alignment(u);
            u->on_batt_test_remaining = 0.0;
        } else {
            if (!u->previous_ir_running) u->on_batt_test_remaining = 5.0;
            u->on_batt_test_remaining = fmax(0.0, u->on_batt_test_remaining - dt);
            double duration = u->mode == A380X_ADIRS_NAV ? nav_duration(i) : att_duration(i);
            if (u->mode == A380X_ADIRS_NAV) {
                /* Only unfinished alignment restarts with movement; taxi and
                 * takeoff must not invalidate a previously aligned unit. */
                if (!u->aligned && (!stationary(i) || u->nav_locked))
                    u->alignment_elapsed = 0.0;
                if (!u->aligned && stationary(i) && !u->nav_locked) {
                    u->alignment_elapsed = fmin(duration, u->alignment_elapsed + dt);
                    u->aligned = u->alignment_elapsed >= duration;
                }
                if (u->aligned) u->alignment_elapsed = duration;
                u->att_ready = u->aligned;
                u->aligning = !u->aligned && !u->nav_locked && stationary(i);
                u->attitude_valid = !u->ir_off && u->aligned && !u->nav_locked &&
                    v->attitude_sensor_valid;
                u->heading_valid = !u->ir_off && u->aligned && !u->nav_locked && raw_heading_valid;
                u->nav_valid = !u->ir_off && u->aligned && !u->nav_locked &&
                    raw_heading_valid && v->navigation_sensor_valid;
                if (raw_heading_valid) u->heading_deg = heading(v->heading_deg);
                u->att_heading_required = u->att_heading_reminder = 0;
            } else if (u->mode == A380X_ADIRS_ATT) {
                u->alignment_elapsed = fmin(duration, u->alignment_elapsed + dt);
                u->att_ready = u->alignment_elapsed >= duration;
                u->aligning = !u->att_ready;
                if (u->att_heading_entered) {
                    if (raw_heading_valid && u->previous_raw_heading_valid && dt > 0.0)
                        u->heading_deg = heading(u->heading_deg +
                            heading_delta(v->heading_deg, u->previous_raw_heading_deg));
                    u->heading_age_seconds = fmin(86400.0, u->heading_age_seconds + dt);
                }
                u->att_heading_required = !u->att_heading_entered;
                u->att_heading_reminder = u->att_heading_entered && u->heading_age_seconds >= 600.0;
                u->attitude_valid = !u->ir_off && u->att_ready && v->attitude_sensor_valid;
                u->heading_valid = !u->ir_off && u->att_ready && u->att_heading_entered && raw_heading_valid;
                if (dt > 0.0 || !u->previous_raw_heading_valid) {
                    if (raw_heading_valid) u->previous_raw_heading_deg = heading(v->heading_deg);
                    u->previous_raw_heading_valid = raw_heading_valid;
                }
            }
            u->alignment_progress = bounded(u->alignment_elapsed / duration, 0.0, 1.0);
            u->alignment_remaining = fmax(0.0, duration - u->alignment_elapsed);
        }
        u->previous_ir_running = ir_running;
        if ((ir_running && u->on_batt_test_remaining > 0.0) ||
            (v->on_battery && (ir_running || (adr_running && !u->adr_off)))) s->on_batt = 1;
    }
}

int a380x_adirs_set_mode(A380XAdirsState *s, const A380XAdirsInputs *i, int n, int mode) {
    if (!s || !i || !valid_unit(n) || mode < A380X_ADIRS_OFF || mode > A380X_ADIRS_ATT) return 0;
    A380XAdirsUnitState *u = &s->unit[n];
    if (u->mode == mode) return 1;
    if (u->mode == A380X_ADIRS_NAV && mode != A380X_ADIRS_NAV && !i->on_ground)
        u->nav_locked = 1;
    u->mode = mode;
    reset_alignment(u);
    u->previous_ir_running = 0;
    a380x_adirs_update(s, i, 0.0);
    return 1;
}

void a380x_adirs_toggle_ir(A380XAdirsState *s, int n) {
    if (!s || !valid_unit(n)) return;
    s->unit[n].ir_off = !s->unit[n].ir_off;
    if (s->unit[n].ir_off)
        s->unit[n].attitude_valid = s->unit[n].heading_valid = s->unit[n].nav_valid = 0;
}
void a380x_adirs_toggle_adr(A380XAdirsState *s, int n) {
    if (!s || !valid_unit(n)) return;
    s->unit[n].adr_off = !s->unit[n].adr_off;
    if (s->unit[n].adr_off) s->unit[n].adr_valid = 0;
}
void a380x_adirs_set_pressed(A380XAdirsState *s, int n, int is_ir, int pressed) {
    if (!s || !valid_unit(n)) return;
    if (is_ir) s->unit[n].ir_pressed = !!pressed;
    else s->unit[n].adr_pressed = !!pressed;
}
int a380x_adirs_set_source(A380XAdirsState *s, int side, int is_ir, int use_third) {
    if (!s || side < 0 || side >= A380X_ADIRS_SIDES) return 0;
    if (is_ir) s->ir_source[side] = use_third ? 2 : side;
    else s->adr_source[side] = use_third ? 2 : side;
    return 1;
}
int a380x_adirs_enter_heading(A380XAdirsState *s, const A380XAdirsInputs *i, int n, double h) {
    if (!s || !i || !valid_unit(n) || !isfinite(h) || h < 0.0 || h > 360.0) return 0;
    A380XAdirsUnitState *u = &s->unit[n];
    const A380XAdirsUnitInputs *v = &i->unit[n];
    if (u->mode != A380X_ADIRS_ATT || !v->ir_powered || v->ir_fault || u->ir_off ||
        !v->heading_sensor_valid || !isfinite(v->heading_deg)) return 0;
    u->att_heading_entered = 1;
    u->heading_deg = heading(h);
    u->heading_age_seconds = 0.0;
    u->previous_raw_heading_deg = heading(v->heading_deg);
    u->previous_raw_heading_valid = 1;
    a380x_adirs_update(s, i, 0.0);
    return 1;
}
int a380x_adirs_align_now(A380XAdirsState *s, const A380XAdirsInputs *i) {
    if (!s || !i || !stationary(i)) return 0;
    int count = 0;
    for (int n = 0; n < A380X_ADIRS_UNITS; ++n) {
        A380XAdirsUnitState *u = &s->unit[n];
        if (u->mode != A380X_ADIRS_NAV || u->nav_locked || !i->unit[n].ir_powered ||
            i->unit[n].ir_fault) continue;
        u->alignment_elapsed = nav_duration(i);
        u->aligned = u->att_ready = 1;
        ++count;
    }
    a380x_adirs_update(s, i, 0.0);
    return count;
}
