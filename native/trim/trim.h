/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef A380X_TRIM_H
#define A380X_TRIM_H

enum { TRIM_NORMAL, TRIM_ALTERNATE, TRIM_DIRECT };
enum { TRIM_PITCH_UP, TRIM_PITCH_DOWN, TRIM_RUDDER_LEFT, TRIM_RUDDER_RIGHT,
       TRIM_RUDDER_RESET, TRIM_ACTION_COUNT };

typedef struct {
    double pitch, rudder;          /* X-Plane normalized trim: + nose up/right */
    double pitch_up_deg, pitch_down_deg, rudder_left_deg, rudder_right_deg;
    double pitch_time, elevator_deg, bank_deg, nz, ias, radio_height_ft, flaps;
    int ground, law, powered, pitch_available, rudder_available, suspended;
    int external_surfaces, native_autopilot, computer_active;
} TrimInputs;

typedef struct {
    unsigned held[TRIM_ACTION_COUNT]; /* bitmask per command: multiple input sources */
    int pulse[TRIM_ACTION_COUNT];     /* preserve a click that ends between frames */
    int resetting, reset_edge, manual_pitch, auto_pitch;
    double pitch, rudder, elevator_filtered, pitch_switch, rudder_knob, reset_pressed;
    int write_pitch, write_rudder;
} TrimState;

void trim_init(TrimState *s);
void trim_release(TrimState *s);
void trim_command(TrimState *s, int action, unsigned source, int phase);
void trim_step(TrimState *s, const TrimInputs *in, double dt);
double trim_rudder_degrees(const TrimInputs *in);
void trim_format_rudder(double degrees, char result[8]);

#endif
