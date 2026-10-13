/* SPDX-License-Identifier: GPL-3.0-or-later
 * Simulator-independent ADIRS controls, availability and alignment model.
 * NAV/ATT durations follow the pinned FlyByWire reference model, not a
 * complete aircraft maintenance or electrical simulation.
 */
#ifndef A380X_ADIRS_H
#define A380X_ADIRS_H
#ifdef __cplusplus
extern "C" {
#endif

enum { A380X_ADIRS_UNITS = 3, A380X_ADIRS_SIDES = 2 };
typedef enum {
    A380X_ADIRS_OFF = 0, A380X_ADIRS_NAV = 1, A380X_ADIRS_ATT = 2
} A380XAdirsMode;

typedef struct {
    int adr_powered, ir_powered, on_battery;
    int adr_fault, ir_fault;
    int adr_sensor_valid, attitude_sensor_valid;
    int heading_sensor_valid, navigation_sensor_valid;
    double heading_deg; /* Raw magnetic gyro heading, never crew-entered. */
} A380XAdirsUnitInputs;

typedef struct {
    A380XAdirsUnitInputs unit[A380X_ADIRS_UNITS];
    int on_ground, paused;
    double ground_speed_knots, latitude_deg;
    /* Positive overrides support test/installation configuration. Defaults:
     * NAV 300/cos(latitude) through 60 degrees, 600 seconds through 73,
     * 1020 seconds above 73; latitude above 82 cannot align. ATT 28 seconds. */
    double nav_alignment_seconds, att_alignment_seconds;
} A380XAdirsInputs;

typedef struct {
    int mode, ir_off, adr_off;
    int ir_pressed, adr_pressed;
    double knob_ratio, ir_press_ratio, adr_press_ratio; /* All 0..1. */
    int nav_locked; /* Cleared only by a new-flight init, never by landing. */
    int adr_valid, attitude_valid, heading_valid, nav_valid;
    int ir_fault_lit, adr_fault_lit, ir_off_lit, adr_off_lit;
    int aligned, att_ready, aligning;
    double alignment_elapsed, alignment_remaining, alignment_progress;
    double on_batt_test_remaining;
    int att_heading_entered, att_heading_required, att_heading_reminder;
    double heading_deg, heading_age_seconds;
    double previous_raw_heading_deg;
    int previous_raw_heading_valid, previous_ir_running;
} A380XAdirsUnitState;

typedef struct {
    A380XAdirsUnitState unit[A380X_ADIRS_UNITS];
    int adr_source[A380X_ADIRS_SIDES], ir_source[A380X_ADIRS_SIDES];
    int on_batt;
} A380XAdirsState;

/* hot_start seeds all three NAV units aligned for an engine-running load.
 * A normal cold init selects OFF, with both independent pushbuttons enabled. */
void a380x_adirs_init(A380XAdirsState *, const A380XAdirsInputs *, int hot_start);
void a380x_adirs_update(A380XAdirsState *, const A380XAdirsInputs *, double dt);
int a380x_adirs_set_mode(A380XAdirsState *, const A380XAdirsInputs *, int unit, int mode);
void a380x_adirs_toggle_ir(A380XAdirsState *, int unit);
void a380x_adirs_toggle_adr(A380XAdirsState *, int unit);
void a380x_adirs_set_pressed(A380XAdirsState *, int unit, int is_ir, int pressed);
/* side: captain=0/FO=1; source switches select own side or unit 3 only. */
int a380x_adirs_set_source(A380XAdirsState *, int side, int is_ir, int use_third);
/* Crew magnetic heading is accepted only in a powered, healthy ATT unit. */
int a380x_adirs_enter_heading(A380XAdirsState *, const A380XAdirsInputs *, int unit, double heading);
/* Explicit simulator convenience; cannot clear airborne NAV lockout. */
int a380x_adirs_align_now(A380XAdirsState *, const A380XAdirsInputs *);

#ifdef __cplusplus
}
#endif
#endif
