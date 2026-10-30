/* SPDX-License-Identifier: GPL-3.0-or-later
 * XPLM-free SPEED BRAKE lever / PRIM speed brake and ground spoiler core (a380x_speedbrake.h): the lever is
 * moved only while held (pushed down), the 1/2 notch, RET seating, ARM only by pulling up at RET, disarm by
 * pushing down, commands and hardware as FBW SpoilersHandler events; FBW's speed brake table, rate and
 * inhibition latch (TLA >= MCT, high-AoA protection, 5 s retracted reset), ground spoiler arming, extension
 * at touchdown wheel spin-up, rejected take-off above 72 kt, extension with reverse when not armed, with the
 * lever extended, retraction rules, the wheel latch reset at lift-off, inert phased lift dumping, spoiler
 * lift, per-panel gains by flap index and the X-Plane handle mapping.
 */
#include <stdio.h>
#include <stdlib.h>
#include "a380x_speedbrake.h"

static int checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); } } while (0)
#define NEAR(a, b, tol) (fabs((double)(a) - (double)(b)) <= (tol))

/* A cockpit drag: hand from where the lever is to `to` in steps of 0.02, one manipulator write each. */
static void drag_to(A380XSpeedbrakeLever *l, double to)
{
    double h = a380x_sb_lever_input(l), s = to >= h ? 0.02 : -0.02;
    for (; s > 0 ? h <= to + 1e-9 : h >= to - 1e-9; h += s) { a380x_sb_lever_drag(l, h); a380x_sb_lever_tick(l, 0.01); }
    a380x_sb_lever_drag(l, to);
    a380x_sb_lever_tick(l, 0.01);
}
static void release(A380XSpeedbrakeLever *l) { a380x_sb_lever_tick(l, 0.3); a380x_sb_lever_tick(l, 0.5); }

static void test_push_down_and_notch(void)
{
    A380XSpeedbrakeLever l;
    unsigned clicks;
    a380x_sb_lever_init(&l, 0.0, 0);
    CHECK(a380x_sb_lever_position(&l) == A380X_SB_POS_RET && !l.armed && l.lift_anim == 0.0 && l.lever_anim == 0.0);
    CHECK(a380x_sb_lever_pos(&l) == 0.0 && a380x_sb_lever_input(&l) == 0.0);
    /* Grabbed: pushed down (lift -1 in 0.125 s) before and while it moves. */
    a380x_sb_lever_drag(&l, 0.0);
    a380x_sb_lever_tick(&l, 0.2);
    CHECK(l.dragging && l.lift_anim == -1.0 && l.rot.value == 0.0);
    drag_to(&l, 0.3);
    CHECK(NEAR(l.rot.value, 0.3, 1e-9) && l.moving && NEAR(l.lever_anim, 0.3, 1e-9) && l.lift_anim == -1.0);
    CHECK(a380x_sb_lever_position(&l) == A380X_SB_POS_BETWEEN && NEAR(a380x_sb_lever_input(&l), 0.3, 1e-9));
    release(&l);   /* springs back up, stays where it was released */
    CHECK(!l.dragging && l.lift_anim == 0.0 && NEAR(l.rot.value, 0.3, 1e-9) && NEAR(a380x_sb_lever_pos(&l), 0.3, 1e-9));
    (void)a380x_sb_lever_take_clicks(&l);
    /* 1/2 notch: captured within 0.03, held until the hand is 0.08 away, one click. */
    drag_to(&l, 0.48);
    CHECK(l.rot.value == A380X_SB_HALF && a380x_sb_lever_position(&l) == A380X_SB_POS_HALF);
    clicks = a380x_sb_lever_take_clicks(&l);
    CHECK(clicks == A380X_SB_CLICK_HALF);
    drag_to(&l, 0.57);
    CHECK(l.rot.value == A380X_SB_HALF && !a380x_sb_lever_take_clicks(&l));
    drag_to(&l, 0.6);
    CHECK(NEAR(l.rot.value, 0.6, 1e-9));
    drag_to(&l, 0.52);   /* back into the notch */
    CHECK(l.rot.value == A380X_SB_HALF && a380x_sb_lever_take_clicks(&l) == A380X_SB_CLICK_HALF);
    release(&l);
    CHECK(l.rot.value == A380X_SB_HALF && !l.dragging && l.lift_anim == 0.0);
    /* A fast push across the notch (beyond the escape distance in one write) passes it. */
    a380x_sb_lever_drag(&l, 0.5);
    a380x_sb_lever_drag(&l, 0.95);
    CHECK(NEAR(l.rot.value, 0.95, 1e-9));
    drag_to(&l, 1.2);   /* FULL end stop */
    CHECK(l.rot.value == 1.0 && a380x_sb_lever_position(&l) == A380X_SB_POS_FULL);
    release(&l);
    /* Released just above RET: seated in RET. */
    drag_to(&l, 0.02);
    release(&l);
    CHECK(l.rot.value == 0.0 && a380x_sb_lever_position(&l) == A380X_SB_POS_RET);
    /* Commanded steps animate the lever at 4 units/s. */
    a380x_sb_lever_set(&l, 1.0);
    a380x_sb_lever_tick(&l, 0.1);
    CHECK(NEAR(l.lever_anim, 0.4, 1e-9) && l.lift_anim == 0.0);
    a380x_sb_lever_tick(&l, 0.2);
    CHECK(l.lever_anim == 1.0);
}

static void test_arm_only_at_ret(void)
{
    A380XSpeedbrakeLever l;
    a380x_sb_lever_init(&l, 0.0, 0);
    /* Pulled up partially: rises with the hand, not latched; released, it drops back. */
    drag_to(&l, -0.1);
    a380x_sb_lever_tick(&l, 0.2);
    CHECK(!l.armed && NEAR(l.lift_anim, 0.1 / 0.15, 1e-9) && l.rot.value == 0.0);
    release(&l);
    CHECK(!l.armed && l.lift_anim == 0.0);
    (void)a380x_sb_lever_take_clicks(&l);
    /* Pulled up into ARM at RET: latched, one click, lever_pos -0.05. */
    drag_to(&l, -0.2);
    CHECK(l.armed && a380x_sb_lever_position(&l) == A380X_SB_POS_ARM && a380x_sb_lever_pos(&l) == -0.05);
    CHECK(a380x_sb_lever_take_clicks(&l) == A380X_SB_CLICK_ARM && NEAR(a380x_sb_lever_input(&l), -0.2, 1e-9));
    a380x_sb_lever_tick(&l, 0.2);
    CHECK(l.lift_anim == 1.0);
    release(&l);
    CHECK(l.armed && l.lift_anim == 1.0 && a380x_sb_lever_input(&l) == 0.0 && a380x_sb_lever_handle(&l) == 0.0);
    /* Held armed and nudged aft within 0.02: stays armed. */
    a380x_sb_lever_drag(&l, 0.0);
    a380x_sb_lever_drag(&l, 0.02);
    CHECK(l.armed && l.rot.value == 0.0);
    /* Dragged aft: pushed down out of ARM (disarmed, one click) and moving. */
    drag_to(&l, 0.3);
    CHECK(!l.armed && NEAR(l.rot.value, 0.3, 1e-9) && a380x_sb_lever_take_clicks(&l) == A380X_SB_CLICK_DISARM);
    a380x_sb_lever_tick(&l, 0.2);
    CHECK(l.lift_anim == -1.0);
    release(&l);
    /* From 0.3 all the way into the ARM zone in one drag: the lever stops at RET, not armed. */
    drag_to(&l, -0.25);
    CHECK(!l.armed && l.rot.value == 0.0);
    release(&l);
    CHECK(!l.armed && l.rot.value == 0.0 && l.lift_anim == 0.0);
    /* A new pull at RET arms. */
    drag_to(&l, -0.25);
    CHECK(l.armed);
    release(&l);
    /* Drag over the full range from ARM: disarmed, then follows to FULL. */
    drag_to(&l, 1.0);
    CHECK(!l.armed && l.rot.value == 1.0);
    release(&l);
    /* From RET: grabbed (pushed down), then pulled forward: the pull-up shows, then latches. */
    a380x_sb_lever_set(&l, 0.0);
    a380x_sb_lever_drag(&l, 0.0);
    a380x_sb_lever_tick(&l, 0.2);
    CHECK(l.lift_anim == -1.0);
    drag_to(&l, -0.16);
    CHECK(l.armed);
    release(&l);
}

static void test_commands_and_hardware(void)
{
    A380XSpeedbrakeLever l;
    a380x_sb_lever_init(&l, 0.0, 1);
    CHECK(l.armed && l.lift_anim == 1.0 && a380x_sb_lever_position(&l) == A380X_SB_POS_ARM);
    a380x_sb_lever_step(&l, -1);   /* armed: nothing above ARM */
    CHECK(l.armed);
    a380x_sb_lever_step(&l, 1);    /* ARM -> RET */
    CHECK(!l.armed && l.rot.value == 0.0);
    a380x_sb_lever_step(&l, 1);
    CHECK(l.rot.value == 0.5);
    a380x_sb_lever_step(&l, 1);
    CHECK(l.rot.value == 1.0);
    a380x_sb_lever_step(&l, 1);
    CHECK(l.rot.value == 1.0);
    a380x_sb_lever_step(&l, -1);
    CHECK(l.rot.value == 0.5);
    a380x_sb_lever_step(&l, -1);
    CHECK(l.rot.value == 0.0 && !l.armed);
    a380x_sb_lever_step(&l, -1);   /* X-Plane: retracting from RET arms */
    CHECK(l.armed && l.rot.value == 0.0);
    a380x_sb_lever_set(&l, 0.3);   /* between positions: steps to the next one */
    CHECK(!l.armed);
    a380x_sb_lever_step(&l, 1);
    CHECK(l.rot.value == 0.5);
    a380x_sb_lever_set(&l, 0.7);
    a380x_sb_lever_step(&l, -1);
    CHECK(l.rot.value == 0.5);
    /* FBW events: toggle, arm on/off, set 0 keeps ARM. */
    a380x_sb_lever_toggle(&l);
    CHECK(l.rot.value == 0.0 && !l.armed);
    a380x_sb_lever_toggle(&l);
    CHECK(l.rot.value == 1.0);
    a380x_sb_lever_arm(&l, 1);
    CHECK(l.armed && l.rot.value == 0.0);
    a380x_sb_lever_set(&l, 0.0);
    CHECK(l.armed);
    a380x_sb_lever_arm(&l, 0);
    CHECK(!l.armed);
    /* Hardware: X-Plane's ARMED -0.5, 0 keeps ARM, a position disarms, snaps, noise ignored. */
    a380x_sb_lever_hardware(&l, -0.5);
    CHECK(l.armed);
    a380x_sb_lever_hardware(&l, 0.0);
    CHECK(l.armed);
    a380x_sb_lever_hardware(&l, 0.3);
    CHECK(!l.armed && l.rot.value == 0.3);
    a380x_sb_lever_hardware(&l, 0.303);
    CHECK(l.rot.value == 0.3);
    a380x_sb_lever_hardware(&l, 0.515);
    CHECK(l.rot.value == 0.5);
    a380x_sb_lever_hardware(&l, 0.015);
    CHECK(l.rot.value == 0.0);
    a380x_sb_lever_hardware(&l, NAN);
    CHECK(l.rot.value == 0.0);
    a380x_sb_lever_hardware(&l, 1.4);
    CHECK(l.rot.value == 1.0);
    /* A command during a drag ends the drag (complete action). */
    a380x_sb_lever_drag(&l, 0.9);
    a380x_sb_lever_step(&l, -1);
    CHECK(!l.dragging && l.rot.value == 0.5);
}

static A380XSpoilerInputs flight(double lever_pos, double tla)
{
    A380XSpoilerInputs in;
    int i;
    memset(&in, 0, sizeof(in));
    in.lever_pos = lever_pos;
    for (i = 0; i < 4; ++i) in.tla[i] = tla;
    in.sfcc_valid = 1;
    return in;
}
static void wheels(A380XSpoilerInputs *in, double kt) { int i; for (i = 0; i < 4; ++i) in->wheel_kt[i] = kt; }
static void run(A380XSpoilerComputer *c, const A380XSpoilerInputs *in, double seconds)
{
    int i, n = (int)lround(seconds / 0.05);
    for (i = 0; i < n; ++i) a380x_spoilers_update(c, in, 0.05);
}

static void test_speed_brake(void)
{
    A380XSpoilerComputer c;
    A380XSpoilerInputs in = flight(0.0, 20.0);
    int k;
    CHECK(a380x_sb_lever_table(0.0) == 0.0 && a380x_sb_lever_table(0.05) == 0.0 && NEAR(a380x_sb_lever_table(0.051), 2.0, 1e-9));
    CHECK(NEAR(a380x_sb_lever_table(0.5), 33.75, 1e-9) && a380x_sb_lever_table(1.0) == 45.0);
    CHECK(a380x_sb_lever_table(-0.05) == 0.0 && NEAR(a380x_sb_lever_table(0.75), 39.375, 1e-9));
    a380x_spoilers_init(&c, &in);
    CHECK(c.speed_brake_deg == 0.0 && !c.inhibited && !c.armed && c.gain[0] == a380x_sb_gain_table[0][0]);
    in.lever_pos = 1.0;
    run(&c, &in, 1.0);
    CHECK(NEAR(c.speed_brake_deg, 5.0, 1e-9));   /* 5 deg/s */
    run(&c, &in, 8.0);
    CHECK(c.speed_brake_deg == 45.0);
    run(&c, &in, 2.0);
    /* Clean gains: spoilers 1-5 20 deg, 6-8 45 deg. */
    for (k = 0; k < 5; ++k) CHECK(NEAR(c.panel_deg[k], 20.0, 1e-9));
    for (k = 5; k < 8; ++k) CHECK(NEAR(c.panel_deg[k], 45.0, 1e-9));
    CHECK(NEAR(a380x_sb_xp_ratio(&c, 0), (5 * 20.0 + 3 * 45.0) / 8.0 / 45.0, 1e-9));
    CHECK(a380x_spoilers_extension(&c) == 1.0);
    /* Half lever: 33.75 deg command. */
    in.lever_pos = 0.5;
    run(&c, &in, 3.0);
    CHECK(NEAR(c.speed_brake_deg, 33.75, 1e-9) && NEAR(c.panel_deg[0], 15.0, 1e-9) && NEAR(c.panel_deg[7], 33.75, 1e-9));
    /* FULL flaps: gains move at 0.03/s to 6 / 0 / 10 deg at full lever. */
    in.lever_pos = 1.0;
    in.flap_index = 5;
    run(&c, &in, 3.0);
    CHECK(c.gain[0] > a380x_sb_gain_table[0][5] && c.gain[0] < a380x_sb_gain_table[0][0]);
    CHECK(NEAR(c.gain[0], a380x_sb_gain_table[0][0] - 0.09, 1e-9));
    run(&c, &in, 40.0);
    CHECK(NEAR(c.panel_deg[0], 6.0, 1e-6) && NEAR(c.panel_deg[3], 0.0, 1e-6) && NEAR(c.panel_deg[6], 10.0, 1e-6));
    /* Paused: nothing moves. */
    in.lever_pos = 0.0;
    a380x_spoilers_update(&c, &in, 0.0);
    CHECK(c.speed_brake_deg == 45.0);
}

static void test_inhibition(void)
{
    A380XSpoilerComputer c;
    A380XSpoilerInputs in = flight(1.0, 25.0);
    a380x_spoilers_init(&c, &in);
    CHECK(c.speed_brake_deg == 45.0 && !c.inhibited);   /* loaded settled */
    in.tla[3] = 35.0;   /* one lever at MCT: inhibited, retracts at 5 deg/s */
    run(&c, &in, 1.0);
    CHECK(c.inhibited && NEAR(c.speed_brake_deg, 40.0, 1e-9));
    run(&c, &in, 10.0);
    CHECK(c.speed_brake_deg == 0.0);
    in.tla[3] = 25.0;   /* thrust back: still inhibited (latched) while the lever is out */
    run(&c, &in, 20.0);
    CHECK(c.inhibited && c.speed_brake_deg == 0.0);
    in.lever_pos = 0.0;   /* lever retracted: the latch resets after 5 s */
    run(&c, &in, 4.9);
    CHECK(c.inhibited);
    run(&c, &in, 0.2);
    CHECK(!c.inhibited);
    in.lever_pos = 1.0;
    run(&c, &in, 1.0);
    CHECK(NEAR(c.speed_brake_deg, 5.0, 1e-9));
    /* High-AoA protection inhibits too; an armed lever counts as retracted for the reset. */
    in.alpha_prot = 1;
    run(&c, &in, 0.1);
    CHECK(c.inhibited);
    in.alpha_prot = 0;
    in.lever_pos = A380X_SB_ARMED_LEVER_POS;
    run(&c, &in, 5.1);
    CHECK(!c.inhibited && c.speed_brake_deg == 0.0);
    /* TLA exactly 35 on the ground inhibits as well (FLX/MCT). */
    in = flight(0.5, 35.0);
    in.on_ground = 1;
    a380x_spoilers_init(&c, &in);
    CHECK(c.inhibited && c.speed_brake_deg == 0.0);
}

static void test_ground_spoilers_landing(void)
{
    A380XSpoilerComputer c;
    A380XSpoilerInputs in = flight(A380X_SB_ARMED_LEVER_POS, 0.0);
    int k;
    a380x_spoilers_init(&c, &in);
    run(&c, &in, 1.0);
    CHECK(c.armed && c.wheel_latch && c.condition && !c.ground_spoilers_out);   /* armed, idle, wheels stopped */
    /* Touchdown, wheels spin up: below 72 kt nothing, at 72 kt on both sides they extend. */
    in.on_ground = 1;
    wheels(&in, 60.0);
    run(&c, &in, 0.5);
    CHECK(!c.ground_spoilers_out);
    in.wheel_kt[A380X_SB_WHEEL_LEFT_BODY] = in.wheel_kt[A380X_SB_WHEEL_LEFT_WING] = 140.0;
    run(&c, &in, 0.5);
    CHECK(!c.ground_spoilers_out);   /* one side only */
    in.wheel_kt[A380X_SB_WHEEL_RIGHT_WING] = 72.0;
    a380x_spoilers_update(&c, &in, 0.05);
    CHECK(c.ground_spoilers_out && NEAR(c.ground_spoiler_deg, 17.0 * 0.05, 1e-9));
    run(&c, &in, 3.0);
    CHECK(c.ground_spoiler_deg == 50.0);
    for (k = 0; k < 8; ++k) CHECK(c.panel_deg[k] == 50.0);
    CHECK(a380x_sb_xp_ratio(&c, 1) == 1.0 && a380x_spoilers_extension(&c) == 1.0);
    /* Held while slowing down (the condition still holds). */
    wheels(&in, 10.0);
    run(&c, &in, 5.0);
    CHECK(c.ground_spoilers_out);
    /* Thrust advanced (two levers above 20 deg): retracted at 17 deg/s. */
    in.tla[0] = in.tla[1] = 25.0;
    run(&c, &in, 1.0);
    CHECK(!c.ground_spoilers_out && NEAR(c.ground_spoiler_deg, 33.0, 1e-9));
    run(&c, &in, 3.0);
    CHECK(c.ground_spoiler_deg == 0.0);
    /* Back to idle at low wheel speed: no re-extension (needs a new spin-up). */
    in.tla[0] = in.tla[1] = 0.0;
    run(&c, &in, 1.0);
    CHECK(!c.ground_spoilers_out);
    /* Disarming retracts: extended again by a new spin-up, then the lever pushed down to RET. */
    wheels(&in, 100.0);
    run(&c, &in, 0.1);
    CHECK(c.ground_spoilers_out);
    in.lever_pos = 0.0;
    run(&c, &in, 0.1);
    CHECK(!c.ground_spoilers_out && !c.armed);
    /* Phased lift dumping never sets (FBW: both triggers wired to constant false). */
    CHECK(!c.phased && c.partial_deg == 0.0);
}

static void test_ground_spoilers_unarmed(void)
{
    A380XSpoilerComputer c;
    A380XSpoilerInputs in = flight(0.0, 0.0);
    a380x_spoilers_init(&c, &in);
    run(&c, &in, 1.0);
    in.on_ground = 1;
    wheels(&in, 130.0);
    run(&c, &in, 1.0);
    CHECK(!c.condition && !c.ground_spoilers_out);   /* not armed, idle, no reverse */
    /* Reverse on engine 2 (others at or below MCT): extension though not armed. */
    in.tla[1] = -10.0;
    run(&c, &in, 0.1);
    CHECK(c.condition && c.ground_spoilers_out);
    in.tla[1] = 0.0;   /* reverse stowed, not armed: retract */
    run(&c, &in, 0.1);
    CHECK(!c.ground_spoilers_out);
    /* Reverse on an outer engine does not count. */
    in.tla[0] = -10.0;
    run(&c, &in, 0.1);
    CHECK(!c.ground_spoilers_out);
    /* Reverse with two levers above MCT does not count either. */
    in.tla[0] = in.tla[3] = 40.0;
    in.tla[2] = -10.0;
    run(&c, &in, 0.1);
    CHECK(!c.condition);
    /* Lever extended (not armed) at touchdown with idle thrust: extension. */
    in = flight(0.5, 0.0);
    a380x_spoilers_init(&c, &in);
    run(&c, &in, 1.0);
    in.on_ground = 1;
    wheels(&in, 120.0);
    run(&c, &in, 0.1);
    CHECK(c.ground_spoilers_out);
    run(&c, &in, 5.0);
    CHECK(c.panel_deg[0] == 50.0);   /* ground spoiler order replaces the speed brake order */
}

static void test_rejected_take_off_and_lift_off(void)
{
    A380XSpoilerComputer c;
    A380XSpoilerInputs in = flight(A380X_SB_ARMED_LEVER_POS, 0.0);
    in.on_ground = 1;
    in.flaps_1f = 1;
    a380x_spoilers_init(&c, &in);
    run(&c, &in, 1.0);
    CHECK(c.wheel_latch && !c.ground_spoilers_out);   /* standstill: the wheel latch is set */
    /* Take-off roll at FLX: no extension. */
    in.tla[0] = in.tla[1] = in.tla[2] = in.tla[3] = 35.0;
    wheels(&in, 65.0);
    run(&c, &in, 1.0);
    CHECK(!c.ground_spoilers_out && !c.condition);
    /* Rejected below 72 kt: no extension. */
    in.tla[0] = in.tla[1] = in.tla[2] = in.tla[3] = 0.0;
    run(&c, &in, 1.0);
    CHECK(c.condition && !c.ground_spoilers_out);
    /* The same rejection above 72 kt wheel speed: extension. */
    in.tla[0] = in.tla[1] = in.tla[2] = in.tla[3] = 35.0;
    wheels(&in, 120.0);
    run(&c, &in, 1.0);
    CHECK(!c.ground_spoilers_out);
    in.tla[0] = in.tla[1] = in.tla[2] = in.tla[3] = 0.0;
    run(&c, &in, 0.1);
    CHECK(c.ground_spoilers_out);
    /* Continued take-off instead: thrust up retracts, lift-off resets the wheel latch. */
    in.tla[0] = in.tla[1] = in.tla[2] = in.tla[3] = 35.0;
    wheels(&in, 160.0);
    run(&c, &in, 4.0);
    CHECK(!c.ground_spoilers_out && c.ground_spoiler_deg == 0.0 && c.wheel_latch);
    in.on_ground = 0;
    a380x_spoilers_update(&c, &in, 0.05);
    CHECK(!c.wheel_latch);
    /* In flight with the wheels still turning and thrust at idle: no extension (latch reset). */
    in.tla[0] = in.tla[1] = in.tla[2] = in.tla[3] = 0.0;
    run(&c, &in, 2.0);
    CHECK(c.condition && !c.wheel_latch && !c.ground_spoilers_out);
    /* Wheels spun down (< 23 kt): the latch is set again for the landing. */
    wheels(&in, 20.0);
    run(&c, &in, 0.1);
    CHECK(c.wheel_latch && !c.ground_spoilers_out);
    /* A touchdown pulse sets the latch even with fast wheels (no spin-down in the air). */
    wheels(&in, 150.0);
    in.on_ground = 1;
    run(&c, &in, 0.1);
    CHECK(c.ground_spoilers_out);
}

static void test_touchdown_pulse(void)
{
    A380XSpoilerComputer c;
    A380XSpoilerInputs in = flight(A380X_SB_ARMED_LEVER_POS, 0.0);
    wheels(&in, 150.0);   /* wheels never spun down after the previous take-off */
    in.on_ground = 1;
    a380x_spoilers_init(&c, &in);
    in.on_ground = 0;
    run(&c, &in, 1.0);
    CHECK(!c.wheel_latch);
    in.on_ground = 1;
    a380x_spoilers_update(&c, &in, 0.05);
    CHECK(c.wheel_latch && c.ground_spoilers_out);
}

static void test_spoiler_lift(void)
{
    A380XSpoilerComputer c;
    A380XSpoilerInputs in = flight(0.0, 0.0);
    int k;
    in.on_ground = 1;
    in.flaps_1f = 1;
    a380x_spoilers_init(&c, &in);
    CHECK(!c.lift_active && c.lift_deg == 0.0 && c.panel_deg[0] == 0.0);
    in.flaps_1f = 0;   /* flaps retracted on the ground: all panels 5 deg at 5 deg/s */
    run(&c, &in, 0.5);
    CHECK(c.lift_active && NEAR(c.lift_deg, 2.5, 1e-9));
    run(&c, &in, 1.0);
    for (k = 0; k < 8; ++k) CHECK(NEAR(c.panel_deg[k], 5.0, 1e-9));
    CHECK(NEAR(a380x_sb_xp_ratio(&c, 1), 5.0 / 50.0, 1e-9) && a380x_spoilers_extension(&c) == 0.0);
    in.sfcc_valid = 0;   /* no valid SFCC word: no lift */
    run(&c, &in, 2.0);
    CHECK(!c.lift_active && c.panel_deg[0] == 0.0);
    in.sfcc_valid = 1;
    in.on_ground = 0;   /* airborne: none */
    run(&c, &in, 2.0);
    CHECK(!c.lift_active && c.lift_deg == 0.0);
    /* A speed brake order above the lift wins (the larger extension). */
    in.on_ground = 1;
    in.lever_pos = 0.5;
    run(&c, &in, 10.0);
    CHECK(NEAR(c.panel_deg[0], 15.0, 1e-9));
}

static void test_system_and_feed(void)
{
    A380XSpeedbrake s;
    A380XSpeedbrakeFeed feed;
    A380XSpoilerInputs in = flight(0.0, 0.0);
    a380x_speedbrake_init(&s, 0.0, 1, &in);
    CHECK(in.lever_pos == -0.05 && s.computer.armed);
    a380x_speedbrake_feed(&s, &feed);
    CHECK(feed.ready && feed.armed && feed.position == A380X_SB_POS_ARM && feed.handle == 0.0 && !feed.lever_out);
    CHECK(feed.lever_pos == -0.05);
    a380x_sb_lever_set(&s.lever, 0.5);
    a380x_speedbrake_update(&s, &in, 0.1);
    a380x_speedbrake_feed(&s, &feed);
    CHECK(!feed.armed && feed.lever_out && feed.position == A380X_SB_POS_HALF && feed.handle == 0.5);
    CHECK(NEAR(feed.speed_brake_deg, 0.5, 1e-9) && NEAR(feed.extension_ratio, 0.5 / 45.0, 1e-9));
    a380x_sb_lever_set(&s.lever, 0.04);
    a380x_speedbrake_feed(&s, &feed);
    CHECK(!feed.lever_out);
}

int main(void)
{
    test_push_down_and_notch();
    test_arm_only_at_ret();
    test_commands_and_hardware();
    test_speed_brake();
    test_inhibition();
    test_ground_spoilers_landing();
    test_ground_spoilers_unarmed();
    test_rejected_take_off_and_lift_off();
    test_touchdown_pulse();
    test_spoiler_lift();
    test_system_and_feed();
    if (failures) {
        fprintf(stderr, "%d of %d speed brake checks failed\n", failures, checks);
        return 1;
    }
    printf("Speed brake core: %d checks passed (push-down, 1/2 notch, RET seat, ARM only at RET, commands, hardware, "
           "speed brake table / rate / inhibition latch, ground spoilers at touchdown, RTO > 72 kt, reverse unarmed, "
           "retraction, lift-off latch, spoiler lift, panel gains, X-Plane mapping)\n", checks);
    return 0;
}
