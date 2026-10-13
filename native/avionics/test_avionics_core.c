/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "a380x_avionics_core.h"

static void assert_close(double actual, double expected, double tolerance)
{
    assert(fabs(actual - expected) <= tolerance);
}

static void test_a1a2_priority(void)
{
    A380XA1A2Inputs in = {0};
    in.autobrake_active = 1; in.autobrake_mode = 6;
    assert(a380x_compute_a1a2(&in) == A380X_A1A2_BRK_RTO);
    in.toga_lock = 1;
    assert(a380x_compute_a1a2(&in) == A380X_A1A2_TOGA_LK);
    in.alpha_floor = 1; in.at_engaged = 1; in.at_active = 1;
    assert(a380x_compute_a1a2(&in) == A380X_A1A2_A_FLOOR);
    in.idle = 1;
    assert(a380x_compute_a1a2(&in) == A380X_A1A2_THR_IDLE);
    in.manual_thrust = 1;
    assert(a380x_compute_a1a2(&in) == A380X_A1A2_THR_LVR);
    in.climb = 1;
    assert(a380x_compute_a1a2(&in) == A380X_A1A2_THR_CLB);
    in.mct = 1;
    assert(a380x_compute_a1a2(&in) == A380X_A1A2_THR_MCT);
    in.mach = 1;
    assert(a380x_compute_a1a2(&in) == A380X_A1A2_MACH);
    in.speed = 1;
    assert(a380x_compute_a1a2(&in) == A380X_A1A2_SPEED);
    in.at_active = 0;
    assert(a380x_compute_a1a2(&in) == A380X_A1A2_MAN_MCT);
    in.man_flex = 1;
    assert(a380x_compute_a1a2(&in) == A380X_A1A2_MAN_FLEX);
    in.man_toga = 1;
    assert(a380x_compute_a1a2(&in) == A380X_A1A2_MAN_TOGA);
}

static void test_a3_priority(void)
{
    A380XA3Inputs in = {0};
    in.autobrake_mode = 6;
    assert(a380x_compute_a3(&in) == A380X_A3_BRK_RTO);
    in.asymmetric_thrust = 1;
    assert(a380x_compute_a3(&in) == A380X_A3_LVR_ASYM);
    in.mct_demand = 1;
    assert(a380x_compute_a3(&in) == A380X_A3_LVR_MCT);
    in.climb_demand = 1;
    assert(a380x_compute_a3(&in) == A380X_A3_LVR_CLB);
    in.thrust_locked = 1;
    assert(a380x_compute_a3(&in) == A380X_A3_THR_LK);
}

static void test_b1_priority_and_variants(void)
{
    A380XB1Inputs in = {0};
    in.vertical_speed = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_VS);
    in.fpa = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_FPA);
    in.alt_hold = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_ALT);
    in.alt_hold = 0; in.alt_acquire = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_ALT_STAR);
    in.altitude_constraint = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_ALT_CST_STAR);
    in.alt_acquire = 0; in.alt_hold = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_ALT_CST);
    in.altitude_constraint = 0; in.cruise_altitude = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_ALT_CRZ);
    in.alt_hold = 0; in.open_climb = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_OP_CLB);
    in.climb = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_CLB);
    in.open_descent = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_OP_DES);
    in.descent = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_DES);
    in.app_descent = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_APP_DES);
    in.tcas = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_TCAS);
    in.pitch_takeoff = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_SRS);
    in.gs_capture = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_GS_STAR);
    in.gs_track = 1;
    assert(a380x_compute_b1(&in) == A380X_B1_GS);
}

static void test_c1_priority(void)
{
    A380XC1Inputs in = {0};
    in.navigation = 1;
    assert(a380x_compute_c1(&in) == A380X_C1_NAV);
    in.loc_track = 1;
    assert(a380x_compute_c1(&in) == A380X_C1_LOC);
    in.backbeam = 1;
    assert(a380x_compute_c1(&in) == A380X_C1_LOC_BC);
    in.track = 1;
    assert(a380x_compute_c1(&in) == A380X_C1_TRACK);
    in.runway = 1; in.runway_track = 1;
    assert(a380x_compute_c1(&in) == A380X_C1_RWY_TRK);
    in.runway_localizer = 1;
    assert(a380x_compute_c1(&in) == A380X_C1_RWY);
    in.heading = 1;
    assert(a380x_compute_c1(&in) == A380X_C1_HDG);
    in.loc_capture = 1;
    assert(a380x_compute_c1(&in) == A380X_C1_LOC_BC_STAR);
    in.goaround_track = 1;
    assert(a380x_compute_c1(&in) == A380X_C1_GA_TRK);
}

static void test_d1d2_priority(void)
{
    A380XD1D2Inputs in = {0};
    in.approach1_condition = 1;
    assert(a380x_compute_d1d2(&in) == A380X_D1D2_APPR_1);
    in.land3_dual_capacity = 1;
    assert(a380x_compute_d1d2(&in) == A380X_D1D2_LAND_3_DUAL);
    in.land3_single_capacity = 1;
    assert(a380x_compute_d1d2(&in) == A380X_D1D2_LAND_3_SINGLE);
    in.land2_capacity = 1;
    assert(a380x_compute_d1d2(&in) == A380X_D1D2_LAND_2);
}

static void tick(A380XFlightPhaseState *state, A380XFlightPhaseInputs *in,
                 double seconds, int expected)
{
    int i, count = (int)(seconds * 20.0 + 0.5);
    for (i = 0; i < count; ++i) a380x_flight_phase_update(state, in, 0.05);
    assert(state->phase == expected);
}

static void test_flight_phases_and_hysteresis(void)
{
    A380XFlightPhaseState state;
    A380XFlightPhaseInputs in = {1, 0, 0, 0.0, 0.0, 145.0};
    a380x_flight_phase_init(&state);
    assert(a380x_flight_phase_update(&state, &in, 0.05) == 1);
    in.running_engines = 2;
    tick(&state, &in, 3.1, 11);
    in.engines_at_takeoff_power = 1;
    assert(a380x_flight_phase_update(&state, &in, 0.05) == 3);
    in.indicated_airspeed = 84.0;
    assert(a380x_flight_phase_update(&state, &in, 0.05) == 4);
    in.indicated_airspeed = 149.0;
    assert(a380x_flight_phase_update(&state, &in, 0.05) == 5);
    /* Memory hysteresis holds both thresholds between their reset/set bands. */
    in.indicated_airspeed = 144.0;
    assert(a380x_flight_phase_update(&state, &in, 0.05) == 5);
    in.on_ground = 0; in.radio_height_ft = 100.0;
    /* The FBW rising-edge confirm delays touchdown, not lift-off. */
    assert(a380x_flight_phase_update(&state, &in, 0.05) == 6);
    in.radio_height_ft = 800.0;
    assert(a380x_flight_phase_update(&state, &in, 0.05) == 7);
    in.engines_at_takeoff_power = 0; in.radio_height_ft = 2000.0;
    assert(a380x_flight_phase_update(&state, &in, 0.05) == 8);
    in.radio_height_ft = 500.0;
    assert(a380x_flight_phase_update(&state, &in, 0.05) == 9);
}

static void test_flight_phase_without_v1(void)
{
    /* No FMS V1 (0 / negative / NaN): never "above V1" (FBW FwsFlightPhases). */
    static const double missing[] = {0.0, -5.0, NAN};
    int k;
    for (k = 0; k < 3; ++k) {
        A380XFlightPhaseState state;
        A380XFlightPhaseInputs in = {1, 2, 0, 0.0, 0.0, 0.0};
        in.configured_v1 = missing[k];
        a380x_flight_phase_init(&state);
        tick(&state, &in, 3.1, 11);
        in.engines_at_takeoff_power = 1;
        in.indicated_airspeed = 84.0;
        assert(a380x_flight_phase_update(&state, &in, 0.05) == 4);
        in.indicated_airspeed = 4.5;
        assert(a380x_flight_phase_update(&state, &in, 0.05) == 3);
        in.indicated_airspeed = 180.0;
        assert(a380x_flight_phase_update(&state, &in, 0.05) == 4);
        in.on_ground = 0; in.radio_height_ft = 100.0;
        assert(a380x_flight_phase_update(&state, &in, 0.05) == 6);
    }
}

static void test_message_text(void)
{
    assert(strcmp(a380x_a1a2_text(A380X_A1A2_MAN_TOGA), "MAN TOGA") == 0);
    assert(strcmp(a380x_b1_text(A380X_B1_ALT_CST_STAR), "ALT CST*") == 0);
    assert(strcmp(a380x_c1_text(A380X_C1_RWY_TRK), "RWY TRK") == 0);
    assert(strcmp(a380x_d1d2_text(A380X_D1D2_LAND_3_DUAL), "LAND 3 DUAL") == 0);
    assert(strcmp(a380x_a3_text((A380XA3Message)999), "") == 0);
}

int main(void)
{
    test_a1a2_priority();
    test_a3_priority();
    test_b1_priority_and_variants();
    test_c1_priority();
    test_d1d2_priority();
    test_flight_phases_and_hysteresis();
    test_flight_phase_without_v1();
    test_message_text();
    assert_close(a380x_efb_tod_distance_nm(33000.0, 3000.0, 3.0), 94.2, 0.2);
    assert_close(a380x_efb_average_ground_speed(33000.0, 3000.0, 480.0),
                 (7000.0 * 250.0 + 23000.0 * 480.0) / 30000.0, 0.001);
    assert_close(a380x_efb_battery_level(50.0, 1, 1.0),
                 50.0 + 100.0 / 10800.0, 0.000001);
    assert_close(a380x_efb_battery_level(50.0, 0, 1.0),
                 50.0 - 100.0 / 32400.0, 0.000001);
    puts("a380x avionics + flyPad core tests passed");
    return 0;
}
