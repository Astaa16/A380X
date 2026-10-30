/* SPDX-License-Identifier: GPL-3.0-or-later
 * Aircraft-local trim authority and conservative elevator-unloading servo.
 * This is an X-Plane adapter, NOT a port of the complete Airbus PRIM/SEC laws.
 * Automatic rate scheduling / bank & load-factor freeze are based on the pinned
 * FBW A380PitchNormalLaw; rudder travel is limited to 20 degrees (FBW API).
 */
#include "trim.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static double bound(double v, double lo, double hi) { return fmax(lo, fmin(hi, v)); }
static double approach(double a, double b, double step) { return a + bound(b-a, -step, step); }

void trim_init(TrimState *s)
{
    memset(s, 0, sizeof(*s));
    s->pitch_switch = s->rudder_knob = .5;
}

void trim_release(TrimState *s)
{
    memset(s->held, 0, sizeof(s->held));
    memset(s->pulse, 0, sizeof(s->pulse));
    s->resetting = s->reset_edge = 0;
    s->pitch_switch = s->rudder_knob = .5;
    s->reset_pressed = 0;
    s->write_pitch = s->write_rudder = s->manual_pitch = s->auto_pitch = 0;
    s->elevator_filtered = 0;
}

void trim_command(TrimState *s, int action, unsigned source, int phase)
{
    if (action < 0 || action >= TRIM_ACTION_COUNT || !source) return;
    if (phase == 0) {
        if (!(s->held[action] & source)) s->pulse[action]=1;
        if (action == TRIM_RUDDER_RESET && !(s->held[action] & source)) s->reset_edge = 1;
        s->held[action] |= source;
    } else if (phase == 2) s->held[action] &= ~source;
    /* Continue never integrates a second time: all motion uses simulator dt. */
}

double trim_rudder_degrees(const TrimInputs *in)
{
    if (!isfinite(in->rudder)) return 0;
    return in->rudder * (in->rudder < 0 ? in->rudder_left_deg : in->rudder_right_deg);
}

void trim_format_rudder(double degrees, char result[8])
{
    if (!isfinite(degrees)) { strcpy(result, "---"); return; }
    /* Avoid showing R 0.0 or negative zero due to insignificant residuals. */
    if (fabs(degrees) < .05) degrees = 0;
    snprintf(result, 8, "%c%4.1f", degrees > 0 ? 'R' : 'L', fmin(fabs(degrees),99.9));
}

void trim_step(TrimState *s, const TrimInputs *in, double dt)
{
    int pitch = !!(s->held[TRIM_PITCH_UP]||s->pulse[TRIM_PITCH_UP]) -
                !!(s->held[TRIM_PITCH_DOWN]||s->pulse[TRIM_PITCH_DOWN]);
    int yaw = !!(s->held[TRIM_RUDDER_RIGHT]||s->pulse[TRIM_RUDDER_RIGHT]) -
              !!(s->held[TRIM_RUDDER_LEFT]||s->pulse[TRIM_RUDDER_LEFT]);
    double step, full, deg, rate = 0;
    s->write_pitch = s->write_rudder = s->manual_pitch = s->auto_pitch = 0;
    s->pitch = isfinite(in->pitch) ? bound(in->pitch,-1,1) : 0;
    s->rudder = isfinite(in->rudder) ? bound(in->rudder,-1,1) : 0;
    if (!isfinite(dt) || dt <= 0 || dt > .25 || in->suspended) {
        /* No catch-up jump or held command across pause/replay/load stalls. */
        trim_release(s);
        return;
    }
    s->pitch_switch = approach(s->pitch_switch, .5 + .5*pitch, dt*8);
    s->rudder_knob = approach(s->rudder_knob, .5 + .5*yaw, dt*8);
    s->reset_pressed = approach(s->reset_pressed,
        !!(s->held[TRIM_RUDDER_RESET]||s->pulse[TRIM_RUDDER_RESET]), dt*12);
    memset(s->pulse,0,sizeof(s->pulse));
    if (!in->powered) { s->resetting = s->reset_edge = 0; s->elevator_filtered=0; return; }

    /* Rudder knob is spring-centred; actual trim persists after release.
     * RESET latches a rate-limited return to neutral, cancelled by manual input. */
    if (s->reset_edge && in->rudder_available) s->resetting = 1;
    s->reset_edge = 0;
    if (yaw) s->resetting = 0;
    if (in->rudder_available && isfinite(in->rudder)) {
        deg = trim_rudder_degrees(in);
        if (s->resetting) {
            deg = approach(deg, 0, dt); /* 1 deg/s */
            if (fabs(deg)<1e-8) deg=0;
        }
        else deg += yaw * dt;
        deg = bound(deg, -fmin(20,in->rudder_left_deg), fmin(20,in->rudder_right_deg));
        full = deg < 0 ? in->rudder_left_deg : in->rudder_right_deg;
        if ((yaw || s->resetting) && full > .01) {
            s->rudder = deg/full;
            s->write_rudder = s->rudder!=in->rudder;
        }
        if (s->resetting && fabs(deg)<1e-8) s->resetting=0;
    } else s->resetting=0;

    s->manual_pitch = in->ground || in->law == TRIM_DIRECT;
    if (!in->pitch_available || !isfinite(in->pitch) || in->external_surfaces) {
        s->elevator_filtered=0; return;
    }
    if (s->manual_pitch && pitch) {
        /* Retain this ACF's servo speed and asymmetric incidence limits. */
        if (isfinite(in->pitch_time) && in->pitch_time>.1) {
            step = 2*dt/in->pitch_time;
            s->pitch = bound(in->pitch + pitch*step,-1,1);
            s->write_pitch = s->pitch != in->pitch;
        }
        s->elevator_filtered=0;
    } else if (!s->manual_pitch && in->computer_active && !in->native_autopilot &&
               in->law != TRIM_DIRECT && in->ias>100 && in->radio_height_ft>50 &&
               isfinite(in->elevator_deg) && isfinite(in->bank_deg) &&
               isfinite(in->nz) && fabs(in->bank_deg)<=30 && in->nz>.5 && in->nz<1.25) {
        /* Requires a REAL closed-loop pitch controller. Hand-flown native
         * elevator input does not unload when THS moves and would run trim away.
         * Feedback is actual elevator deflection, not a second attitude/AP law.
         * Positive X-Plane elevator degrees = trailing edge DOWN: unload with
         * negative (nose-down) trim. Freeze outside the normal flight envelope. */
        s->auto_pitch=1;
        s->elevator_filtered += (in->elevator_deg-s->elevator_filtered)*dt/(1+dt);
        if (fabs(s->elevator_filtered)>1) rate=-s->elevator_filtered*.02;
        rate=bound(rate, in->flaps>.01?-.25:-.15, in->flaps>.01?.25:.15);
        full=rate<0 ? in->pitch_down_deg : in->pitch_up_deg;
        if (full>.01) {
            s->pitch=bound(in->pitch+rate*dt/full,-1,1);
            s->write_pitch=s->pitch!=in->pitch;
        }
    } else s->elevator_filtered=0;
}
