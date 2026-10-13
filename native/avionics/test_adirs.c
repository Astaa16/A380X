/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "a380x_adirs.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static A380XAdirsInputs fixture(void) {
    A380XAdirsInputs i;
    memset(&i, 0, sizeof(i));
    i.on_ground = 1;
    i.latitude_deg = 45.0;
    i.nav_alignment_seconds = 5.0;
    i.att_alignment_seconds = 2.0;
    for (int n = 0; n < A380X_ADIRS_UNITS; ++n) {
        i.unit[n].adr_powered = i.unit[n].ir_powered = 1;
        i.unit[n].adr_sensor_valid = i.unit[n].attitude_sensor_valid = 1;
        i.unit[n].heading_sensor_valid = i.unit[n].navigation_sensor_valid = 1;
        i.unit[n].heading_deg = 350.0;
    }
    return i;
}
static void tick(A380XAdirsState *s, A380XAdirsInputs *i, double seconds) {
    a380x_adirs_update(s, i, seconds);
}
static void test_cold_alignment_and_order(void) {
    A380XAdirsInputs i = fixture();
    A380XAdirsState s;
    a380x_adirs_init(&s, &i, 0);
    assert(!s.on_batt && s.adr_source[0] == 0 && s.ir_source[1] == 1);
    for (int n = 0; n < 3; ++n) {
        assert(s.unit[n].mode == A380X_ADIRS_OFF);
        assert(!s.unit[n].adr_valid && !s.unit[n].nav_valid);
        assert(a380x_adirs_set_mode(&s, &i, n, A380X_ADIRS_NAV));
        assert(s.unit[n].aligning && !s.unit[n].nav_valid && s.unit[n].adr_valid);
        assert(s.on_batt && s.unit[n].on_batt_test_remaining == 5.0);
    }
    tick(&s, &i, 4.0);
    assert(s.on_batt && !s.unit[0].nav_valid && s.unit[0].alignment_remaining == 1.0);
    tick(&s, &i, 1.0);
    for (int n = 0; n < 3; ++n)
        assert(s.unit[n].nav_valid && s.unit[n].attitude_valid && s.unit[n].heading_valid);
    assert(!s.on_batt);
    /* Selecting 1, 2, 3 is a crew procedure, never a simulated interlock. */
    a380x_adirs_init(&s, &i, 0);
    a380x_adirs_set_mode(&s, &i, 2, A380X_ADIRS_NAV);
    tick(&s, &i, 5.0);
    assert(s.unit[2].nav_valid && !s.unit[0].nav_valid && !s.unit[1].nav_valid);
}
static void test_motion_and_pause(void) {
    A380XAdirsInputs i = fixture();
    A380XAdirsState s;
    a380x_adirs_init(&s, &i, 0);
    a380x_adirs_set_mode(&s, &i, 0, A380X_ADIRS_NAV);
    tick(&s, &i, 4.0);
    i.paused = 1;
    tick(&s, &i, 100.0);
    assert(s.unit[0].alignment_elapsed == 4.0 && s.on_batt);
    i.paused = 0;
    i.ground_speed_knots = 2.0;
    tick(&s, &i, 1.0);
    assert(!s.unit[0].aligned && !s.unit[0].aligning && s.unit[0].alignment_elapsed == 0.0);
    i.ground_speed_knots = 0.0;
    tick(&s, &i, 4.0);
    assert(!s.unit[0].nav_valid);
    tick(&s, &i, 1.0);
    assert(s.unit[0].nav_valid);
    i.ground_speed_knots = 300.0;
    i.on_ground = 0;
    i.latitude_deg = 70.0;
    tick(&s, &i, 5.0);
    assert(s.unit[0].nav_valid && s.unit[0].alignment_progress == 1.0);
    assert(s.unit[0].alignment_remaining == 0.0);
}
static void test_pushbuttons_failures_and_power(void) {
    A380XAdirsInputs i = fixture();
    A380XAdirsState s;
    a380x_adirs_init(&s, &i, 1);
    a380x_adirs_toggle_ir(&s, 0);
    tick(&s, &i, .1);
    assert(!s.unit[0].nav_valid && s.unit[0].adr_valid && s.unit[0].ir_off_lit);
    assert(s.unit[0].aligned && s.unit[1].nav_valid);
    a380x_adirs_toggle_ir(&s, 0);
    a380x_adirs_toggle_adr(&s, 0);
    tick(&s, &i, .1);
    assert(s.unit[0].nav_valid && !s.unit[0].adr_valid && s.unit[0].adr_off_lit);
    a380x_adirs_toggle_adr(&s, 0);
    i.unit[0].ir_fault = 1;
    i.unit[1].adr_fault = 1;
    tick(&s, &i, .1);
    assert(s.unit[0].adr_valid && !s.unit[0].nav_valid && s.unit[0].ir_fault_lit);
    assert(s.unit[1].nav_valid && !s.unit[1].adr_valid && s.unit[1].adr_fault_lit);
    assert(s.unit[2].adr_valid && s.unit[2].nav_valid);
    i.unit[0].ir_fault = 0;
    tick(&s, &i, 5.0);
    assert(s.unit[0].nav_valid && !s.unit[0].ir_fault_lit);
    i.unit[0].adr_powered = 0;
    tick(&s, &i, .1);
    assert(!s.unit[0].adr_valid && s.unit[0].nav_valid);
    i.unit[0].adr_powered = 1;
    i.unit[0].ir_powered = 0;
    tick(&s, &i, .1);
    assert(s.unit[0].adr_valid && !s.unit[0].nav_valid && !s.unit[0].aligned);
    i.unit[0].ir_powered = 1;
    i.on_ground = 0;
    tick(&s, &i, 100.0);
    assert(!s.unit[0].nav_valid && s.unit[0].adr_valid);
    i.on_ground = 1;
    tick(&s, &i, 5.0);
    assert(s.unit[0].nav_valid);
    a380x_adirs_set_mode(&s, &i, 0, A380X_ADIRS_OFF);
    assert(!s.unit[0].adr_valid && !s.unit[0].nav_valid);
}
static void test_battery_and_manual_sources(void) {
    A380XAdirsInputs i = fixture();
    A380XAdirsState s;
    a380x_adirs_init(&s, &i, 1);
    assert(!s.on_batt);
    i.unit[2].on_battery = 1;
    tick(&s, &i, 100.0);
    assert(s.on_batt);
    i.unit[0].ir_fault = 1;
    i.unit[1].adr_fault = 1;
    tick(&s, &i, .1);
    assert(s.ir_source[0] == 0 && s.adr_source[1] == 1); /* No automatic transfer. */
    assert(a380x_adirs_set_source(&s, 0, 1, 1));
    assert(a380x_adirs_set_source(&s, 1, 0, 1));
    assert(s.ir_source[0] == 2 && s.adr_source[0] == 0);
    assert(s.adr_source[1] == 2 && s.ir_source[1] == 1);
    assert(s.unit[s.ir_source[0]].nav_valid && s.unit[s.adr_source[1]].adr_valid);
    a380x_adirs_set_source(&s, 0, 1, 0);
    a380x_adirs_set_source(&s, 1, 0, 0);
    assert(s.ir_source[0] == 0 && s.adr_source[1] == 1);
    i.unit[2].ir_powered = i.unit[2].adr_powered = 0;
    tick(&s, &i, 5.0);
    assert(!s.on_batt);
}
static void test_att_manual_heading(void) {
    A380XAdirsInputs i = fixture();
    A380XAdirsState s;
    a380x_adirs_init(&s, &i, 1);
    assert(!a380x_adirs_enter_heading(&s, &i, 0, 120.0));
    i.on_ground = 0;
    a380x_adirs_set_mode(&s, &i, 0, A380X_ADIRS_ATT);
    assert(s.unit[0].nav_locked && !s.unit[0].attitude_valid);
    tick(&s, &i, 2.0);
    assert(s.unit[0].attitude_valid && !s.unit[0].nav_valid && !s.unit[0].heading_valid);
    assert(s.unit[0].att_heading_required && s.unit[0].adr_valid);
    assert(a380x_adirs_enter_heading(&s, &i, 0, 100.0));
    assert(s.unit[0].heading_valid && s.unit[0].heading_deg == 100.0);
    assert(!s.unit[0].att_heading_required);
    i.unit[0].heading_deg = 10.0;
    tick(&s, &i, 1.0);
    assert(s.unit[0].heading_deg == 120.0); /* Raw north wrap is +20 degrees. */
    i.unit[0].heading_deg = 340.0;
    tick(&s, &i, 1.0);
    assert(s.unit[0].heading_deg == 90.0);
    tick(&s, &i, 597.0);
    assert(!s.unit[0].att_heading_reminder);
    tick(&s, &i, 1.0);
    assert(s.unit[0].att_heading_reminder && s.unit[0].heading_valid);
    assert(a380x_adirs_enter_heading(&s, &i, 0, 360.0));
    assert(s.unit[0].heading_deg == 0.0 && !s.unit[0].att_heading_reminder);
    assert(!a380x_adirs_enter_heading(&s, &i, 0, NAN));
    assert(!a380x_adirs_enter_heading(&s, &i, 0, 361.0));
    assert(!a380x_adirs_enter_heading(&s, &i, 0, -1.0));
    i.unit[0].heading_deg = NAN;
    tick(&s, &i, .1);
    assert(!s.unit[0].heading_valid && s.unit[0].attitude_valid);
    assert(isfinite(s.unit[0].heading_deg));
}
static void test_nav_lock_flight_lifecycle(void) {
    for (int destination = A380X_ADIRS_OFF; destination <= A380X_ADIRS_ATT; destination += 2) {
        A380XAdirsInputs i = fixture();
        A380XAdirsState s;
        a380x_adirs_init(&s, &i, 1);
        i.on_ground = 0;
        a380x_adirs_set_mode(&s, &i, 0, destination);
        assert(s.unit[0].nav_locked); /* Setter catches event before next frame. */
        a380x_adirs_set_mode(&s, &i, 0, A380X_ADIRS_NAV);
        tick(&s, &i, 1000.0);
        assert(!s.unit[0].nav_valid && s.unit[1].nav_valid && s.unit[0].adr_valid);
        i.on_ground = 1;
        tick(&s, &i, 1000.0);
        a380x_adirs_align_now(&s, &i);
        assert(s.unit[0].nav_locked && !s.unit[0].nav_valid);
        i.on_ground = 0;
        tick(&s, &i, 1000.0);
        assert(s.unit[0].nav_locked && !s.unit[0].nav_valid); /* Touch and go. */
        i.on_ground = 1;
        a380x_adirs_init(&s, &i, 0);
        assert(!s.unit[0].nav_locked);
        a380x_adirs_set_mode(&s, &i, 0, A380X_ADIRS_NAV);
        assert(a380x_adirs_align_now(&s, &i) == 1 && s.unit[0].nav_valid);
        a380x_adirs_set_mode(&s, &i, 0, A380X_ADIRS_OFF);
        assert(!s.unit[0].nav_locked); /* A ground mode change is recoverable. */
    }
}
static void test_duration_and_alignment_guards(void) {
    A380XAdirsInputs i = fixture();
    A380XAdirsState s;
    const double latitude[] = {0.0, 60.0, -65.0, 75.0};
    const double duration[] = {300.0, 600.0, 600.0, 1020.0};
    i.nav_alignment_seconds = i.att_alignment_seconds = 0.0;
    for (int n = 0; n < 4; ++n) {
        i.latitude_deg = latitude[n];
        a380x_adirs_init(&s, &i, 0);
        a380x_adirs_set_mode(&s, &i, 0, A380X_ADIRS_NAV);
        assert(fabs(s.unit[0].alignment_remaining - duration[n]) < 1e-6);
        tick(&s, &i, duration[n] - 1.0);
        assert(!s.unit[0].nav_valid);
        tick(&s, &i, 1.01);
        assert(s.unit[0].nav_valid);
    }
    i.latitude_deg = 83.0;
    a380x_adirs_init(&s, &i, 0);
    a380x_adirs_set_mode(&s, &i, 0, A380X_ADIRS_NAV);
    tick(&s, &i, 2000.0);
    assert(!s.unit[0].nav_valid && !a380x_adirs_align_now(&s, &i));
    i.latitude_deg = 0.0;
    i.ground_speed_knots = NAN;
    tick(&s, &i, 2000.0);
    assert(!s.unit[0].nav_valid && !a380x_adirs_align_now(&s, &i));
    i.ground_speed_knots = 0.0;
    i.on_ground = 0;
    assert(!a380x_adirs_align_now(&s, &i));
    a380x_adirs_set_mode(&s, &i, 0, A380X_ADIRS_ATT);
    assert(s.unit[0].alignment_remaining == 28.0);
    tick(&s, &i, 28.0);
    assert(s.unit[0].attitude_valid && !s.unit[0].nav_valid);
}
static void test_sensor_and_input_guards(void) {
    A380XAdirsInputs i = fixture();
    A380XAdirsState s;
    a380x_adirs_init(&s, &i, 1);
    i.unit[0].adr_sensor_valid = 0;
    i.unit[1].navigation_sensor_valid = 0;
    i.unit[2].attitude_sensor_valid = 0;
    tick(&s, &i, NAN);
    assert(!s.unit[0].adr_valid && s.unit[0].nav_valid);
    assert(s.unit[1].attitude_valid && !s.unit[1].nav_valid);
    assert(!s.unit[2].attitude_valid && s.unit[2].nav_valid);
    i.unit[0].heading_deg = NAN;
    tick(&s, &i, -10.0);
    assert(!s.unit[0].heading_valid && !s.unit[0].nav_valid && s.unit[0].attitude_valid);
    assert(!a380x_adirs_set_mode(&s, &i, 3, A380X_ADIRS_NAV));
    assert(!a380x_adirs_set_mode(&s, &i, 0, 3));
    assert(!a380x_adirs_set_source(&s, -1, 0, 1));
    assert(!a380x_adirs_set_source(&s, 2, 1, 1));
    assert(!a380x_adirs_enter_heading(&s, &i, 3, 90.0));
    a380x_adirs_toggle_adr(&s, -1);
    a380x_adirs_toggle_ir(&s, 3);
    a380x_adirs_set_pressed(&s, 3, 0, 1);
}
static void test_control_animation(void) {
    A380XAdirsInputs i = fixture();
    A380XAdirsState s;
    a380x_adirs_init(&s, &i, 0);
    a380x_adirs_set_mode(&s, &i, 0, A380X_ADIRS_ATT);
    a380x_adirs_set_pressed(&s, 0, 1, 1);
    a380x_adirs_set_pressed(&s, 1, 0, 1);
    tick(&s, &i, .02);
    assert(s.unit[0].knob_ratio > 0.0 && s.unit[0].knob_ratio < 1.0);
    assert(s.unit[0].ir_press_ratio > 0.0 && !s.unit[0].adr_press_ratio);
    assert(s.unit[1].adr_press_ratio > 0.0 && !s.unit[1].ir_press_ratio);
    tick(&s, &i, 1.0);
    assert(s.unit[0].knob_ratio > .999);
    a380x_adirs_set_pressed(&s, 0, 1, 0);
    a380x_adirs_set_pressed(&s, 1, 0, 0);
    tick(&s, &i, 1.0);
    assert(s.unit[0].ir_press_ratio < .001 && s.unit[1].adr_press_ratio < .001);
}
int main(void) {
    test_cold_alignment_and_order();
    test_motion_and_pause();
    test_pushbuttons_failures_and_power();
    test_battery_and_manual_sources();
    test_att_manual_heading();
    test_nav_lock_flight_lifecycle();
    test_duration_and_alignment_guards();
    test_sensor_and_input_guards();
    test_control_animation();
    puts("ADIRS: alignment, independent ADR/IR, ATT heading, manual sources, power, NAV lock and animation passed");
    return 0;
}
