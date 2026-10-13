/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "a380x_avionics_core.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

A380XA1A2Message a380x_compute_a1a2(const A380XA1A2Inputs *i)
{
    if (i->man_toga) return A380X_A1A2_MAN_TOGA;
    if (i->man_flex) return A380X_A1A2_MAN_FLEX;
    if (i->mct && i->at_engaged && !i->at_active) return A380X_A1A2_MAN_MCT;
    if (i->manual_thrust && i->at_engaged && !i->at_active) return A380X_A1A2_MAN_THR;
    if (i->speed) return A380X_A1A2_SPEED;
    if (i->mach) return A380X_A1A2_MACH;
    if (i->mct) return A380X_A1A2_THR_MCT;
    if (i->climb) return A380X_A1A2_THR_CLB;
    if (i->manual_thrust && i->at_engaged && i->at_active) return A380X_A1A2_THR_LVR;
    if (i->idle) return A380X_A1A2_THR_IDLE;
    if (i->alpha_floor && i->at_engaged && i->at_active) return A380X_A1A2_A_FLOOR;
    if (i->toga_lock) return A380X_A1A2_TOGA_LK;
    if (i->autobrake_active && i->autobrake_mode == 1) return A380X_A1A2_BTV;
    if (i->autobrake_active && i->autobrake_mode == 2) return A380X_A1A2_BRK_LO;
    if (i->autobrake_active && i->autobrake_mode == 3) return A380X_A1A2_BRK_2;
    if (i->autobrake_active && i->autobrake_mode == 4) return A380X_A1A2_BRK_3;
    if (i->autobrake_active && i->autobrake_mode == 5) return A380X_A1A2_BRK_HI;
    if (i->autobrake_active && i->autobrake_mode == 6) return A380X_A1A2_BRK_RTO;
    return A380X_A1A2_NONE;
}

double a380x_efb_average_ground_speed(double current_altitude_ft,
                                      double target_altitude_ft,
                                      double current_ground_speed_knots)
{
    double bottom, top, below, above;
    if (current_ground_speed_knots < 1.0) current_ground_speed_knots = 1.0;
    if (current_altitude_ft <= 10000.0) return current_ground_speed_knots;
    bottom = fmax(target_altitude_ft, 0.0);
    top = fmax(current_altitude_ft, target_altitude_ft + 1.0);
    below = fmax(0.0, fmin(10000.0 - bottom, top - bottom));
    above = (top - bottom) - below;
    return (below * 250.0 + above * current_ground_speed_knots) / (top - bottom);
}

double a380x_efb_tod_distance_nm(double current_altitude_ft,
                                 double target_altitude_ft,
                                 double descent_angle_degrees)
{
    double vertical = fmax(0.0, current_altitude_ft - target_altitude_ft);
    double angle = fabs(descent_angle_degrees);
    if (vertical <= 0.0 || angle <= 0.01) return 0.0;
    return vertical / tan(angle * M_PI / 180.0) * 0.000164579;
}

double a380x_efb_battery_level(double current_percent,
                               int external_power_available,
                               double dt_seconds)
{
    double duration = external_power_available ? 10800.0 : -32400.0;
    double result;
    if (dt_seconds < 0.0) dt_seconds = 0.0;
    if (dt_seconds > 1.0) dt_seconds = 1.0;
    result = current_percent + 100.0 * dt_seconds / duration;
    if (result < 0.0) return 0.0;
    if (result > 100.0) return 100.0;
    return result;
}

A380XA3Message a380x_compute_a3(const A380XA3Inputs *i)
{
    if (i->thrust_locked) return A380X_A3_THR_LK;
    if (i->climb_demand) return A380X_A3_LVR_CLB;
    if (i->mct_demand) return A380X_A3_LVR_MCT;
    if (i->asymmetric_thrust) return A380X_A3_LVR_ASYM;
    if (!i->autobrake_active && i->autobrake_mode == 6) return A380X_A3_BRK_RTO;
    return A380X_A3_NONE;
}

A380XB1Message a380x_compute_b1(const A380XB1Inputs *i)
{
    if (i->gs_track) return A380X_B1_GS;
    if (i->gs_capture) return A380X_B1_GS_STAR;
    if (i->pitch_takeoff || i->pitch_goaround) return A380X_B1_SRS;
    if (i->tcas) return A380X_B1_TCAS;
    if (i->app_descent) return A380X_B1_APP_DES;
    if (i->descent) return A380X_B1_DES;
    if (i->open_descent) return A380X_B1_OP_DES;
    if (i->climb) return A380X_B1_CLB;
    if (i->open_climb) return A380X_B1_OP_CLB;
    if (i->alt_hold && !i->altitude_constraint && !i->cruise_altitude) return A380X_B1_ALT;
    if (i->alt_acquire && !i->altitude_constraint && !i->cruise_altitude) return A380X_B1_ALT_STAR;
    if (i->alt_acquire && i->altitude_constraint && !i->cruise_altitude) return A380X_B1_ALT_CST_STAR;
    if (i->alt_hold && i->altitude_constraint && !i->cruise_altitude) return A380X_B1_ALT_CST;
    if (i->alt_acquire && !i->altitude_constraint && i->cruise_altitude) return A380X_B1_ALT_CRZ_STAR;
    if (i->alt_hold && !i->altitude_constraint && i->cruise_altitude) return A380X_B1_ALT_CRZ;
    if (i->fpa) return A380X_B1_FPA;
    if (i->vertical_speed) return A380X_B1_VS;
    return A380X_B1_NONE;
}

A380XC1Message a380x_compute_c1(const A380XC1Inputs *i)
{
    if (i->goaround_track) return A380X_C1_GA_TRK;
    if (i->loc_capture && i->backbeam) return A380X_C1_LOC_BC_STAR;
    if (i->loc_capture && !i->backbeam) return A380X_C1_LOC_STAR;
    if (i->heading) return A380X_C1_HDG;
    if (i->runway && i->runway_localizer) return A380X_C1_RWY;
    if (i->runway && i->runway_track) return A380X_C1_RWY_TRK;
    if (i->track) return A380X_C1_TRACK;
    if (i->loc_track && i->backbeam) return A380X_C1_LOC_BC;
    if (i->loc_track && !i->backbeam) return A380X_C1_LOC;
    if (i->navigation) return A380X_C1_NAV;
    return A380X_C1_NONE;
}

A380XD1D2Message a380x_compute_d1d2(const A380XD1D2Inputs *i)
{
    if (i->land2_capacity) return A380X_D1D2_LAND_2;
    if (i->land3_single_capacity) return A380X_D1D2_LAND_3_SINGLE;
    if (i->land3_dual_capacity) return A380X_D1D2_LAND_3_DUAL;
    if (i->land_mode_armed || i->land_mode_active || i->approach1_condition) return A380X_D1D2_APPR_1;
    return A380X_D1D2_NONE;
}

static void confirm_init(A380XConfirmNode *node, double delay, int rising)
{
    memset(node, 0, sizeof(*node));
    node->delay = delay;
    node->rising = rising;
}

static int confirm_update(A380XConfirmNode *node, int value, double dt)
{
    int delayed_edge;
    value = !!value;
    if (!node->initialized) {
        node->initialized = 1;
        node->previous = value;
        node->output = value;
        return value;
    }
    delayed_edge = node->rising ? value : !value;
    if (!delayed_edge) {
        node->timer = 0.0;
        node->output = value;
    } else if (value != node->previous) {
        node->timer = node->delay;
        node->output = !value;
    } else if (node->timer > 0.0) {
        node->timer = fmax(0.0, node->timer - dt);
        node->output = node->timer == 0.0 ? value : !value;
    } else {
        node->output = value;
    }
    node->previous = value;
    return node->output;
}

static int memory_update(A380XMemoryNode *node, int set_value, int reset_value)
{
    if (set_value && reset_value) node->value = node->set_has_priority;
    else if (set_value) node->value = 1;
    else if (reset_value) node->value = 0;
    return node->value;
}

void a380x_flight_phase_init(A380XFlightPhaseState *state)
{
    memset(state, 0, sizeof(*state));
    confirm_init(&state->ground_confirm, 1.0, 1);
    /* Matches the pinned FWS structure.  The X-Plane adapter intentionally
     * uses three seconds while engine start behavior is still under test. */
    confirm_init(&state->engine_confirm, 3.0, 1);
    state->above_80.set_has_priority = 1;
    state->above_v1.set_has_priority = 1;
    state->phase = 1;
}

int a380x_flight_phase_update(A380XFlightPhaseState *state,
                              const A380XFlightPhaseInputs *in,
                              double dt)
{
    int grounded, one_engine, above_80, above_v1;
    /* FBW FwsFlightPhases.ts: without a V1 (FMS value <= 0, or not a number)
     * the aircraft is never "above V1"; the phase stays in 4 until lift-off. */
    int has_v1 = isfinite(in->configured_v1) && in->configured_v1 > 0.0;
    double v1 = has_v1 ? in->configured_v1 : 0.0;
    if (!isfinite(dt) || dt < 0.0) dt = 0.0;
    if (dt > 0.1) dt = 0.1;
    grounded = confirm_update(&state->ground_confirm, in->on_ground, dt);
    one_engine = confirm_update(&state->engine_confirm, in->running_engines >= 1, dt);
    above_80 = memory_update(&state->above_80,
                             in->indicated_airspeed > 83.0,
                             in->indicated_airspeed < 77.0);
    above_v1 = memory_update(&state->above_v1,
                             has_v1 && in->indicated_airspeed > v1 + 3.0,
                             !has_v1 || in->indicated_airspeed < v1 - 3.0);
    if (!one_engine && grounded) {
        state->shutdown_timer += dt;
        state->phase = state->shutdown_timer >= 300.0 ? 12 : 1;
    } else {
        state->shutdown_timer = 0.0;
        if (grounded && in->engines_at_takeoff_power && !above_80) state->phase = 3;
        else if (grounded && in->engines_at_takeoff_power && above_80 && !above_v1) state->phase = 4;
        else if (grounded && in->engines_at_takeoff_power && above_v1) state->phase = 5;
        else if (!grounded && in->engines_at_takeoff_power && in->radio_height_ft < 400.0) state->phase = 6;
        else if (!grounded && in->engines_at_takeoff_power && in->radio_height_ft < 1500.0) state->phase = 7;
        else if (!grounded && in->radio_height_ft > 800.0) state->phase = 8;
        else if (!grounded) state->phase = 9;
        else if (grounded && above_80) state->phase = 10;
        else if (grounded && one_engine) state->phase = 11;
        else state->phase = 2;
    }
    return state->phase;
}

#define TEXT_FUNCTION(name, type, ...) \
    const char *name(type message) { \
        static const char *values[] = { __VA_ARGS__ }; \
        unsigned index = (unsigned)message; \
        return index < sizeof(values) / sizeof(values[0]) ? values[index] : ""; \
    }

TEXT_FUNCTION(a380x_a1a2_text, A380XA1A2Message,
    "", "MAN TOGA", "MAN GA SOFT", "MAN FLEX", "MAN MCT", "MAN THR",
    "SPEED", "MACH", "THR MCT", "THR CLB", "THR LVR", "THR IDLE",
    "A.FLOOR", "TOGA LK", "BTV", "BRK LO", "BRK 2", "BRK 3", "BRK HI", "BRK RTO")
TEXT_FUNCTION(a380x_a3_text, A380XA3Message,
    "", "THR LK", "LVR TOGA", "LVR CLB", "LVR MCT", "LVR ASYM", "BRK RTO")
TEXT_FUNCTION(a380x_b1_text, A380XB1Message,
    "", "G/S", "F-G/S", "G/S*", "F-G/S*", "SRS", "TCAS", "APP DES",
    "DES", "OP DES", "CLB", "OP CLB", "ALT", "ALT*", "ALT CST",
    "ALT CST*", "ALT CRZ", "ALT CRZ*", "FPA", "V/S")
TEXT_FUNCTION(a380x_c1_text, A380XC1Message,
    "", "GA TRK", "LOC BC*", "LOC*", "F-LOC*", "HDG", "RWY", "RWY TRK",
    "TRACK", "LOC BC", "LOC", "F-LOC", "NAV")
TEXT_FUNCTION(a380x_d1d2_text, A380XD1D2Message,
    "", "LAND 2", "LAND 3 SINGLE", "LAND 3 DUAL", "APPR 1", "LAND 1",
    "F-APP", "F-APP+RAW", "RAW ONLY")

#undef TEXT_FUNCTION
