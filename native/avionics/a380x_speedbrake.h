/* SPDX-License-Identifier: GPL-3.0-or-later
 * A380 SPEED BRAKE lever, speed brake command and ground spoilers: XPLM-free core (docs/SPEED_BRAKE.md).
 * X-Plane side: a380x_speedbrake.inc.
 *
 * Lever. Handle 0 (RET) .. 1 (FULL) with a notch at 1/2, and ARM: the lever pulled up at RET (FBW
 * O:Position 0 ARM, 1 RET, 2 1/2, 3 FULL; FBW SpoilersHandler handle 0..1 + armed). The cockpit manipulator
 * (one X-Plane click-hold-drag along the lever arc) writes the HAND in handle units; the drag range
 * continues 0.25 beyond RET toward ARM, as FBW's single lever drag reaches ARM past RET. Holding the lever
 * is pushing it down: while it is dragged between RET and FULL the handle is pushed down (lift -1) and only
 * then does it move; released, it springs back up and stays where it is (seated in RET within 0.03, in the
 * 1/2 notch when captured there). The 1/2 notch is sticky (captures within 0.03, holds until the hand is
 * 0.08 away, A380XDetentLever). A drag that STARTS with the lever at RET and goes 0.15 beyond RET pulls
 * the lever up into ARM (latched); dragging an armed lever aft of RET pushes it down (disarmed) and moves
 * it, as FBW's lever drag does. Commands and hardware are complete actions (FBW SpoilersHandler events).
 *
 * Computer. Translation of FlyByWire fbw-a380x/src/wasm/fbw_a380/src/model/A380PrimComputerFctl.cpp
 * (+ _data.cpp, pinned 2baa2b35): speed brake command (lever table 0/0.05/0.051/0.5/1 ->
 * 0/0/2/33.75/45 deg, 5 deg/s; inhibited by high-AoA protection or any TLA >= 35 deg, the inhibition
 * latched until the lever has been retracted (< 0.05) for 5 s), ground spoilers (armed = lever < -0.02;
 * extension condition ((lever > 0.05 or armed) and >= 3 TLA < 20 deg) or (reverse on engine 2 or 3 and
 * >= 3 TLA <= 35 deg); extension at wheel spin-up >= 72 kt on both sides, armed by a wheel latch set by
 * touchdown or wheels < 23 kt and reset at lift-off, so a rejected take-off above 72 kt extends them too;
 * held while the condition holds; 50 deg at 17 deg/s; FBW's phased lift dumping (partial extension,
 * 15 deg) is present but its triggers are constant false in FBW), the spoiler lift (5 deg on the ground
 * below 1+F) and the per-panel speed brake gains by PRIM flap index (0.03/s), 40 deg/s panel orders.
 * Degrees are positive UP here (FBW: negative).
 */
#ifndef A380X_SPEEDBRAKE_H
#define A380X_SPEEDBRAKE_H
#include <math.h>
#include <string.h>
#include "a380x_detent_lever.h"

/* ---------------------------------------------------------------- lever */
#define A380X_SB_HALF 0.5
#define A380X_SB_ARM_ZONE (-0.25)     /* manipulator range beyond RET toward ARM (handle units) */
#define A380X_SB_ARM_LATCH (-0.15)    /* a drag started at RET pulls the lever up into ARM here */
#define A380X_SB_ARM_RELEASE 0.02     /* an armed lever dragged aft of RET by more: pushed down, disarmed */
#define A380X_SB_RET_SEAT 0.03        /* released within this of RET: seated in RET */
#define A380X_SB_HALF_CAPTURE 0.03
#define A380X_SB_HALF_ESCAPE 0.08
#define A380X_SB_HALF_CLICK_IN 0.005
#define A380X_SB_HALF_CLICK_OUT 0.05
#define A380X_SB_HARDWARE_SNAP 0.02   /* hardware axis within this of RET / 1/2: that position */
#define A380X_SB_HARDWARE_DEADBAND 0.005
#define A380X_SB_LIFT_RATE 8.0        /* lift_anim units/s: push-down / pull-up in 0.125 s */
#define A380X_SB_ANIM_RATE 4.0        /* lever_anim handle units/s after a command or hardware step */
#define A380X_SB_ARMED_LEVER_POS (-0.05)   /* FlyByWireInterface: speed_brake_lever_pos when armed */

/* FBW O:Position of the lever (pedestal template): 0 ARM, 1 RET, 2 1/2, 3 FULL; -1 between. */
enum { A380X_SB_POS_BETWEEN = -1, A380X_SB_POS_ARM, A380X_SB_POS_RET, A380X_SB_POS_HALF, A380X_SB_POS_FULL };
/* Click events (one per frame is played): 1/2 notch entry, ARM latch, ARM release. */
#define A380X_SB_CLICK_HALF 1u
#define A380X_SB_CLICK_ARM 2u
#define A380X_SB_CLICK_DISARM 4u

typedef struct {
    A380XDetentLever rot;  /* handle 0 (RET) .. 1 (FULL), sticky 1/2 notch; its drag fields are unused */
    int armed;             /* pulled up at RET: ground spoilers armed */
    int dragging;          /* the cockpit manipulator is writing */
    int start_at_ret;      /* the current drag began with the lever at RET (or armed) */
    int moving;            /* the current drag is moving the handle (pushed down) */
    double hand;           /* last manipulator input, A380X_SB_ARM_ZONE..1 */
    double idle_s;         /* wall time since the last manipulator write */
    double lever_anim;     /* 3D lever rotation, handle units 0..1 */
    double lift_anim;      /* 3D lever height: -1 pushed down, 0 normal, +1 pulled up (ARM) */
    unsigned clicks;
} A380XSpeedbrakeLever;

static inline double a380x_sb_clamp(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }

static inline void a380x_sb_lever_init(A380XSpeedbrakeLever *l, double handle, int armed)
{
    static const A380XDetent half = {A380X_SB_HALF, A380X_SB_HALF_CAPTURE, A380X_SB_HALF_ESCAPE,
                                     A380X_SB_HALF_CLICK_IN, A380X_SB_HALF_CLICK_OUT};
    memset(l, 0, sizeof(*l));
    l->armed = armed != 0;
    a380x_detent_lever_init(&l->rot, 0.0, 1.0, &half, 1, armed ? 0.0 : handle);
    l->hand = l->lever_anim = l->rot.value;
    l->lift_anim = l->armed ? 1.0 : 0.0;
}

static inline double a380x_sb_lever_handle(const A380XSpeedbrakeLever *l) { return l->armed ? 0.0 : l->rot.value; }

/* FBW analog input speed_brake_lever_pos (FlyByWireInterface: armed -> -0.05, else the handle). */
static inline double a380x_sb_lever_pos(const A380XSpeedbrakeLever *l)
{
    return l->armed ? A380X_SB_ARMED_LEVER_POS : l->rot.value;
}

static inline int a380x_sb_lever_position(const A380XSpeedbrakeLever *l)
{
    if (l->armed) return A380X_SB_POS_ARM;
    if (l->rot.value <= 0.0) return A380X_SB_POS_RET;
    if (fabs(l->rot.value - A380X_SB_HALF) < 1e-9) return A380X_SB_POS_HALF;
    if (l->rot.value >= 1.0) return A380X_SB_POS_FULL;
    return A380X_SB_POS_BETWEEN;
}

static inline void a380x_sb_lever_set_armed(A380XSpeedbrakeLever *l, int armed)
{
    armed = armed != 0;
    if (armed == l->armed) return;
    l->armed = armed;
    l->clicks |= armed ? A380X_SB_CLICK_ARM : A380X_SB_CLICK_DISARM;
    if (armed) a380x_detent_lever_set(&l->rot, 0.0);
}

/* 1/2-notch click bookkeeping of the detent lever. */
static inline void a380x_sb_lever_collect(A380XSpeedbrakeLever *l)
{
    if (a380x_detent_lever_take_clicks(&l->rot)) l->clicks |= A380X_SB_CLICK_HALF;
}

/* Manipulator write: hand in handle units (A380X_SB_ARM_ZONE..1). */
static inline void a380x_sb_lever_drag(A380XSpeedbrakeLever *l, double hand)
{
    if (!isfinite(hand)) return;
    hand = a380x_sb_clamp(hand, A380X_SB_ARM_ZONE, 1.0);
    if (!l->dragging) {
        l->dragging = 1;
        l->moving = 0;
        l->start_at_ret = l->armed || l->rot.value <= 0.0;
    }
    l->hand = hand;
    l->idle_s = 0.0;
    if (l->armed) {
        if (hand <= A380X_SB_ARM_RELEASE) return;   /* stays pulled up */
        a380x_sb_lever_set_armed(l, 0);              /* pushed down out of ARM (FBW: SPOILERS_ARM_OFF) */
    }
    if (l->start_at_ret && l->rot.value <= 0.0 && hand <= A380X_SB_ARM_LATCH) {
        if (l->rot.dragging) a380x_detent_lever_end_drag(&l->rot);
        l->moving = 0;
        a380x_sb_lever_set_armed(l, 1);              /* pulled up at RET (FBW: SPOILERS_ARM_ON) */
        return;
    }
    if (hand < 0.0 && l->rot.value <= 0.0 && !l->moving) return;   /* toward ARM, not latched yet */
    l->moving = 1;                                   /* pushed down: the handle follows (1/2 notch) */
    a380x_detent_lever_drag(&l->rot, hand < 0.0 ? 0.0 : hand);
    a380x_sb_lever_collect(l);
}

/* Released (no manipulator write for the detent-lever timeout): the handle springs up and stays. */
static inline void a380x_sb_lever_end_drag(A380XSpeedbrakeLever *l)
{
    if (l->rot.dragging) a380x_detent_lever_end_drag(&l->rot);
    if (!l->armed && l->rot.value > 0.0 && l->rot.value < A380X_SB_RET_SEAT) a380x_detent_lever_set(&l->rot, 0.0);
    l->dragging = l->moving = l->start_at_ret = 0;
    l->hand = a380x_sb_lever_handle(l);
    l->idle_s = 0.0;
    a380x_sb_lever_collect(l);
}

/* Complete action to a handle position (disarms when the handle leaves RET, FBW SpoilersHandler). */
static inline void a380x_sb_lever_set(A380XSpeedbrakeLever *l, double handle)
{
    if (!isfinite(handle)) return;
    if (l->dragging) a380x_sb_lever_end_drag(l);
    handle = a380x_sb_clamp(handle, 0.0, 1.0);
    if (handle > 0.0) a380x_sb_lever_set_armed(l, 0);
    a380x_detent_lever_set(&l->rot, handle);
    l->hand = a380x_sb_lever_handle(l);
    a380x_sb_lever_collect(l);
}

/* FBW onEventSpoilersArmSet / ArmOn: armed with the handle at RET. */
static inline void a380x_sb_lever_arm(A380XSpeedbrakeLever *l, int armed)
{
    if (l->dragging) a380x_sb_lever_end_drag(l);
    if (armed) a380x_detent_lever_set(&l->rot, 0.0);
    a380x_sb_lever_set_armed(l, armed);
    l->hand = a380x_sb_lever_handle(l);
    a380x_sb_lever_collect(l);
}

/* One lever position toward FULL (+1) or toward ARM (-1): ARM <-> RET <-> 1/2 <-> FULL, from between
 * two positions to the next one in that direction (X-Plane: retracting from RET arms). */
static inline void a380x_sb_lever_step(A380XSpeedbrakeLever *l, int direction)
{
    double v;
    if (l->dragging) a380x_sb_lever_end_drag(l);
    v = l->rot.value;
    if (direction > 0) {
        if (l->armed) { a380x_sb_lever_arm(l, 0); return; }
        a380x_sb_lever_set(l, v < A380X_SB_HALF - 1e-9 ? A380X_SB_HALF : 1.0);
    } else {
        if (l->armed) return;
        if (v <= 0.0) { a380x_sb_lever_arm(l, 1); return; }
        a380x_sb_lever_set(l, v > A380X_SB_HALF + 1e-9 ? A380X_SB_HALF : 0.0);
    }
}

/* FBW onEventSpoilersToggle: disarmed, RET if extended, FULL if retracted. */
static inline void a380x_sb_lever_toggle(A380XSpeedbrakeLever *l)
{
    double target = a380x_sb_lever_handle(l) > 0.0 ? 0.0 : 1.0;
    a380x_sb_lever_arm(l, 0);
    a380x_sb_lever_set(l, target);
}

/* A foreign value of X-Plane's handle (hardware axis, another plugin, preset): < -0.25 is X-Plane's
 * ARMED (-0.5); otherwise a handle position (FBW SpoilersAxisSet: a position above RET disarms), within
 * 0.02 of RET or 1/2 that position, changes below 0.005 ignored (axis noise). */
static inline void a380x_sb_lever_hardware(A380XSpeedbrakeLever *l, double ratio)
{
    double v;
    if (!isfinite(ratio)) return;
    if (ratio < -0.25) { a380x_sb_lever_arm(l, 1); return; }
    v = a380x_sb_clamp(ratio, 0.0, 1.0);
    if (v < A380X_SB_HARDWARE_SNAP) v = 0.0;
    else if (fabs(v - A380X_SB_HALF) < A380X_SB_HARDWARE_SNAP) v = A380X_SB_HALF;
    if (fabs(v - l->rot.value) < A380X_SB_HARDWARE_DEADBAND && !(v == 0.0 && l->rot.value != 0.0)) return;
    a380x_sb_lever_set(l, v);
}

/* Wall-clock step: drag timeout, push-down / pull-up and lever animation. */
static inline void a380x_sb_lever_tick(A380XSpeedbrakeLever *l, double wall_dt)
{
    double target, step;
    if (!isfinite(wall_dt) || wall_dt < 0.0) wall_dt = 0.0;
    if (l->dragging) {
        l->idle_s += wall_dt;
        if (l->idle_s >= A380X_DETENT_LEVER_TIMEOUT_S) a380x_sb_lever_end_drag(l);
    }
    if (l->armed) target = 1.0;
    else if (l->dragging && l->start_at_ret && l->rot.value <= 0.0 && l->hand < 0.0)
        target = a380x_sb_clamp(l->hand / A380X_SB_ARM_LATCH, 0.0, 1.0);   /* being pulled up toward ARM */
    else if (l->dragging) target = -1.0;   /* held: pushed down (moving, or held at its position) */
    else target = 0.0;
    step = A380X_SB_LIFT_RATE * wall_dt;
    l->lift_anim = l->lift_anim < target ? fmin(target, l->lift_anim + step) : fmax(target, l->lift_anim - step);
    if (l->dragging && l->moving) l->lever_anim = l->rot.value;
    else {
        step = A380X_SB_ANIM_RATE * wall_dt;
        l->lever_anim = l->lever_anim < l->rot.value ? fmin(l->rot.value, l->lever_anim + step)
                                                     : fmax(l->rot.value, l->lever_anim - step);
    }
}

/* The manipulator dataref: the hand while dragging, else where the lever is (RET when armed). */
static inline double a380x_sb_lever_input(const A380XSpeedbrakeLever *l)
{
    return l->dragging ? l->hand : a380x_sb_lever_handle(l);
}

static inline unsigned a380x_sb_lever_take_clicks(A380XSpeedbrakeLever *l)
{
    unsigned clicks = l->clicks;
    l->clicks = 0;
    return clicks;
}

/* ---------------------------------------------------------------- PRIM speed brake / ground spoilers */
/* A380PrimComputerFctl_data.cpp constants. */
#define A380X_SB_RETRACTED_POS 0.05        /* CompareToConstant_const_n: lever < 0.05 = retracted */
#define A380X_SB_RETRACTED_CONFIRM_S 5.0   /* ConfirmNode_timeDelay_g */
#define A380X_SB_INHIBIT_TLA 35.0          /* CompareToConstant1..4_const: TLA >= MCT inhibits */
#define A380X_SB_RATE_DEG_S 5.0            /* RateLimiterGenericVariableTs24 */
#define A380X_SB_MAX_DEG 45.0
#define A380X_GS_ARMED_POS (-0.02)         /* CompareToConstant_const_m */
#define A380X_GS_LEVER_OUT_POS 0.05        /* CompareToConstant26_const */
#define A380X_GS_IDLE_TLA 20.0             /* CompareToConstant11/19/20/21_const: TLA < 20 */
#define A380X_GS_MCT_TLA 35.0              /* CompareToConstant29/16/17/18_const: TLA <= 35 */
#define A380X_GS_PARTIAL_TLA 25.0          /* CompareToConstant1/2_const: TLA 1, 2 <= 25 */
#define A380X_GS_WHEEL_SLOW_KT 23.0        /* CompareToConstant13/9/10/14_const */
#define A380X_GS_WHEEL_SPIN_KT 72.0        /* CompareToConstant7/8/3/4_const */
#define A380X_GS_DEG 50.0                  /* Constant_Value */
#define A380X_GS_RATE_DEG_S 17.0           /* RateLimiterVariableTs6 */
#define A380X_GS_PARTIAL_DEG 15.0          /* Constant5_Value */
#define A380X_GS_PARTIAL_RATE_DEG_S 5.0    /* RateLimiterVariableTs4 */
#define A380X_SPLR_LIFT_DEG 5.0            /* Constant9_Value */
#define A380X_SPLR_LIFT_RATE_DEG_S 5.0     /* RateLimiterGenericVariableTs25 */
#define A380X_SPLR_GAIN_RATE 0.03          /* RateLimiterGenericVariableTs26/27/28 */
#define A380X_SPLR_PANEL_RATE_DEG_S 40.0   /* RateLimiterGenericVariableTs8..23 */
#define A380X_SPLR_PANELS 8

/* uDLookupTable_bp01Data / _tableData (look1_binlxpw: linear, extrapolated). */
static const double a380x_sb_lever_bp[5] = {0.0, 0.05, 0.051, 0.5, 1.0};
static const double a380x_sb_lever_deg[5] = {0.0, 0.0, 2.0, 33.75, 45.0};
/* Speed brake gains by PRIM flap_handle_index (0 "0", 1 "1", 2 "1+F", 3 "2", 4 "3", 5 "FULL"): spoilers 1-2,
 * 3-5 (Spoiler345Computation) and 6-8. */
static const double a380x_sb_gain_table[3][6] = {
    {0.44444444444444442, 0.44444444444444442, 0.26666666666666666, 0.2, 0.17777777777777778, 0.13333333333333333},
    {0.44444444444444442, 0.44444444444444442, 0.37777777777777777, 0.2, 0.066666666666666666, 0.0},
    {1.0, 1.0, 0.46666666666666667, 0.37777777777777777, 0.22222222222222221, 0.22222222222222221},
};
static const int a380x_sb_panel_group[A380X_SPLR_PANELS] = {0, 0, 1, 1, 1, 2, 2, 2};

enum { A380X_SB_WHEEL_LEFT_BODY, A380X_SB_WHEEL_LEFT_WING, A380X_SB_WHEEL_RIGHT_BODY, A380X_SB_WHEEL_RIGHT_WING };

typedef struct {
    double lever_pos;     /* FBW speed_brake_lever_pos: -0.05 armed, else the handle 0..1 */
    double tla[4];        /* FBW thr_lever_N_pos, deg TLA (reverse negative) */
    int on_ground;
    double wheel_kt[4];   /* A380X_SB_WHEEL_* */
    int alpha_prot;       /* high-AoA protection active (port: see docs/SPEED_BRAKE.md) */
    int flap_index;       /* PRIM flap_handle_index 0..5 */
    int sfcc_valid;       /* an SFCC actual-position word is valid */
    int flaps_1f;         /* its bit 20: flaps at or beyond 1+F */
} A380XSpoilerInputs;

typedef struct {
    int initialized;
    double retracted_s; int retracted;   /* ConfirmNode (rising, 5 s) on lever < 0.05 */
    int inhibit_latch, inhibited;        /* Memory_PreviousInput (S/R, reset dominant) */
    double speed_brake_deg;              /* speed_brake_command_deg, positive up */
    int prev_rise, prev_fall;            /* PulseNode7 (rising) / PulseNode6 (falling) on on_ground */
    int wheel_latch;                     /* Memory_PreviousInput_a */
    int armed, condition, spin_up;
    int ground_spoilers_out;             /* Delay1_DSTATE_b */
    int partial_a, partial_b, phased;    /* Delay2 / Delay3 / phased_lift_dumping_active */
    double partial_deg, ground_spoiler_deg;
    int lift_active; double lift_deg;
    double gain[3];
    double panel_deg[A380X_SPLR_PANELS]; /* per-panel order, both wings (no roll term) */
} A380XSpoilerComputer;

static inline double a380x_sb_rate(double y, double u, double rate, double dt)
{
    return y + a380x_sb_clamp(u - y, -rate * dt, rate * dt);
}

/* look1_binlxpw over the lever table (linear, extrapolated beyond the end points). */
static inline double a380x_sb_lever_table(double pos)
{
    int i;
    if (!isfinite(pos)) return 0.0;
    for (i = 1; i < 4 && pos > a380x_sb_lever_bp[i]; ++i) {}
    return a380x_sb_lever_deg[i - 1] + (a380x_sb_lever_deg[i] - a380x_sb_lever_deg[i - 1])
         * (pos - a380x_sb_lever_bp[i - 1]) / (a380x_sb_lever_bp[i] - a380x_sb_lever_bp[i - 1]);
}

static inline int a380x_sb_flap_index(int index) { return index < 0 || index > 5 ? 0 : index; }

/* S/R flip-flop of the Simulink model (Logic_table): reset dominant. */
static inline int a380x_sb_sr(int set, int reset, int q) { return reset ? 0 : set ? 1 : q; }

static inline int a380x_sb_count(const double *tla, double limit, int below_or_equal)
{
    int i, n = 0;
    for (i = 0; i < 4; ++i) n += below_or_equal ? tla[i] <= limit : tla[i] < limit;
    return n;
}

static inline int a380x_sb_inhibit_condition(const A380XSpoilerInputs *in)
{
    int i, any = in->alpha_prot != 0;
    for (i = 0; i < 4; ++i) any = any || in->tla[i] >= A380X_SB_INHIBIT_TLA;
    return any;
}

static inline double a380x_sb_panel_target(const A380XSpoilerComputer *c, int k)
{
    double sb;
    if (c->ground_spoilers_out || c->phased) return c->ground_spoiler_deg;
    sb = fmax(c->gain[a380x_sb_panel_group[k]] * c->speed_brake_deg, c->lift_deg);
    return fmin(sb, A380X_SB_MAX_DEG);   /* Spoiler345Computation clamp (no roll term) */
}

/* Settled start (aircraft load): rate limiters at their targets, no ground spoiler extension. */
static inline void a380x_spoilers_init(A380XSpoilerComputer *c, const A380XSpoilerInputs *in)
{
    int k, flap = a380x_sb_flap_index(in->flap_index);
    memset(c, 0, sizeof(*c));
    c->initialized = 1;
    c->prev_rise = 1;   /* PulseNode isRisingEdge = true: no pulse on the first step */
    c->prev_fall = 0;
    c->inhibited = a380x_sb_inhibit_condition(in);
    c->speed_brake_deg = c->inhibited ? 0.0 : a380x_sb_lever_table(in->lever_pos);
    c->armed = in->lever_pos < A380X_GS_ARMED_POS;
    c->lift_active = in->on_ground && in->sfcc_valid && !in->flaps_1f;
    c->lift_deg = c->lift_active ? A380X_SPLR_LIFT_DEG : 0.0;
    for (k = 0; k < 3; ++k) c->gain[k] = a380x_sb_gain_table[k][flap];
    for (k = 0; k < A380X_SPLR_PANELS; ++k) c->panel_deg[k] = a380x_sb_panel_target(c, k);
}

/* One PRIM cycle (simulated dt; nothing moves while paused), FBW evaluation order. */
static inline void a380x_spoilers_update(A380XSpoilerComputer *c, const A380XSpoilerInputs *in, double dt)
{
    const double *w = in->wheel_kt;
    int k, rise, fall, slow, idle3, reverse, mct3, flap = a380x_sb_flap_index(in->flap_index), condition;
    if (!c->initialized) a380x_spoilers_init(c, in);
    if (!isfinite(dt) || dt <= 0.0) return;
    /* Speed brake: inhibition (latched until 5 s retracted), lever table, 5 deg/s. */
    condition = a380x_sb_inhibit_condition(in);
    if (in->lever_pos < A380X_SB_RETRACTED_POS) {
        c->retracted_s += dt;
        if (c->retracted_s >= A380X_SB_RETRACTED_CONFIRM_S) c->retracted = 1;
    } else {
        c->retracted_s = 0.0;
        c->retracted = 0;
    }
    c->inhibit_latch = a380x_sb_sr(condition, c->retracted, c->inhibit_latch);
    c->inhibited = condition || c->inhibit_latch;
    c->speed_brake_deg = a380x_sb_rate(c->speed_brake_deg, c->inhibited ? 0.0 : a380x_sb_lever_table(in->lever_pos),
                                       A380X_SB_RATE_DEG_S, dt);
    /* Ground spoilers. */
    rise = in->on_ground && !c->prev_rise;
    c->prev_rise = in->on_ground != 0;
    fall = !in->on_ground && c->prev_fall;
    c->prev_fall = in->on_ground != 0;
    slow = (w[A380X_SB_WHEEL_LEFT_BODY] < A380X_GS_WHEEL_SLOW_KT || w[A380X_SB_WHEEL_LEFT_WING] < A380X_GS_WHEEL_SLOW_KT)
        && (w[A380X_SB_WHEEL_RIGHT_BODY] < A380X_GS_WHEEL_SLOW_KT || w[A380X_SB_WHEEL_RIGHT_WING] < A380X_GS_WHEEL_SLOW_KT);
    c->wheel_latch = a380x_sb_sr(rise || slow, fall, c->wheel_latch);
    c->armed = in->lever_pos < A380X_GS_ARMED_POS;
    idle3 = a380x_sb_count(in->tla, A380X_GS_IDLE_TLA, 0) > 2;
    reverse = in->tla[1] < 0.0 || in->tla[2] < 0.0;
    mct3 = a380x_sb_count(in->tla, A380X_GS_MCT_TLA, 1) > 2;
    c->condition = ((in->lever_pos > A380X_GS_LEVER_OUT_POS || c->armed) && idle3) || (reverse && mct3);
    c->spin_up = (w[A380X_SB_WHEEL_LEFT_BODY] >= A380X_GS_WHEEL_SPIN_KT || w[A380X_SB_WHEEL_LEFT_WING] >= A380X_GS_WHEEL_SPIN_KT)
              && (w[A380X_SB_WHEEL_RIGHT_BODY] >= A380X_GS_WHEEL_SPIN_KT || w[A380X_SB_WHEEL_RIGHT_WING] >= A380X_GS_WHEEL_SPIN_KT)
              && c->wheel_latch;
    c->ground_spoilers_out = c->condition && (c->spin_up || c->ground_spoilers_out);
    /* Phased lift dumping: FBW wires both triggers (PulseNode4/5) to constant false, so it never sets. */
    c->partial_a = c->condition && c->partial_a;
    c->partial_b = c->armed && in->tla[0] <= A380X_GS_PARTIAL_TLA && in->tla[1] <= A380X_GS_PARTIAL_TLA && c->partial_b;
    c->phased = !c->ground_spoilers_out && (c->partial_a || c->partial_b);
    c->partial_deg = a380x_sb_rate(c->partial_deg, c->phased ? A380X_GS_PARTIAL_DEG : 0.0, A380X_GS_PARTIAL_RATE_DEG_S, dt);
    c->ground_spoiler_deg = a380x_sb_rate(c->ground_spoiler_deg, c->ground_spoilers_out ? A380X_GS_DEG : c->partial_deg,
                                          A380X_GS_RATE_DEG_S, dt);
    /* Spoiler lift on the ground below 1+F, speed brake gains, panel orders. */
    c->lift_active = in->on_ground && in->sfcc_valid && !in->flaps_1f;
    c->lift_deg = a380x_sb_rate(c->lift_deg, c->lift_active ? A380X_SPLR_LIFT_DEG : 0.0, A380X_SPLR_LIFT_RATE_DEG_S, dt);
    for (k = 0; k < 3; ++k) c->gain[k] = a380x_sb_rate(c->gain[k], a380x_sb_gain_table[k][flap], A380X_SPLR_GAIN_RATE, dt);
    for (k = 0; k < A380X_SPLR_PANELS; ++k)
        c->panel_deg[k] = a380x_sb_rate(c->panel_deg[k], a380x_sb_panel_target(c, k), A380X_SPLR_PANEL_RATE_DEG_S, dt);
}

static inline double a380x_spoilers_mean_deg(const A380XSpoilerComputer *c)
{
    double sum = 0.0;
    int k;
    for (k = 0; k < A380X_SPLR_PANELS; ++k) sum += c->panel_deg[k];
    return sum / A380X_SPLR_PANELS;
}

/* Speed brake / ground spoiler extension as a fraction of its maximum (VLS increment, PFD symbol). */
static inline double a380x_spoilers_extension(const A380XSpoilerComputer *c)
{
    return a380x_sb_clamp(fmax(c->speed_brake_deg / A380X_SB_MAX_DEG, c->ground_spoiler_deg / A380X_GS_DEG), 0.0, 1.0);
}

/* ---------------------------------------------------------------- X-Plane handle */
/* X-Plane deflects every speedbrake/spoiler group by its ACF maximum x the handle: in flight
 * acf/_sbrk1_up_flit = _spoi*_with_flit_sbrk = 45 deg, on the ground _sbrk1_up_grnd / _spoi1_with_grnd_sbrk
 * 50 deg. The handle written is the mean FBW panel order over that maximum (0..1, never X-Plane's -0.5). */
#define A380X_SB_XP_FLIGHT_MAX_DEG 45.0
#define A380X_SB_XP_GROUND_MAX_DEG 50.0
static inline double a380x_sb_xp_ratio(const A380XSpoilerComputer *c, int on_ground)
{
    double max = on_ground ? A380X_SB_XP_GROUND_MAX_DEG : A380X_SB_XP_FLIGHT_MAX_DEG;
    return a380x_sb_clamp(a380x_spoilers_mean_deg(c) / max, 0.0, 1.0);
}

/* ---------------------------------------------------------------- feed to the displays and the FWS */
typedef struct {
    int ready;                   /* the speed brake module drives X-Plane's speedbrake handle */
    int armed;                   /* FBW A32NX_SPOILERS_ARMED (FCDC discrete word 4 bit 27) */
    int position;                /* A380X_SB_POS_* */
    int lever_out;               /* handle > 0.05: the port FWS speed brake command */
    int inhibited, ground_spoilers_out, phased, lift_active;
    double handle;               /* FBW A32NX_SPOILERS_HANDLE_POSITION 0..1 */
    double lever_pos;            /* FBW speed_brake_lever_pos */
    double speed_brake_deg, ground_spoiler_deg, extension_ratio;
    double panel_deg[A380X_SPLR_PANELS];
} A380XSpeedbrakeFeed;

typedef struct {
    A380XSpeedbrakeLever lever;
    A380XSpoilerComputer computer;
} A380XSpeedbrake;

static inline void a380x_speedbrake_init(A380XSpeedbrake *s, double handle, int armed, A380XSpoilerInputs *in)
{
    memset(s, 0, sizeof(*s));
    a380x_sb_lever_init(&s->lever, handle, armed);
    in->lever_pos = a380x_sb_lever_pos(&s->lever);
    a380x_spoilers_init(&s->computer, in);
}

/* `in` without lever_pos (filled from the lever). */
static inline void a380x_speedbrake_update(A380XSpeedbrake *s, A380XSpoilerInputs *in, double sim_dt)
{
    in->lever_pos = a380x_sb_lever_pos(&s->lever);
    a380x_spoilers_update(&s->computer, in, sim_dt);
}

static inline void a380x_speedbrake_feed(const A380XSpeedbrake *s, A380XSpeedbrakeFeed *out)
{
    const A380XSpoilerComputer *c = &s->computer;
    memset(out, 0, sizeof(*out));
    out->ready = 1;
    out->armed = s->lever.armed;
    out->position = a380x_sb_lever_position(&s->lever);
    out->handle = a380x_sb_lever_handle(&s->lever);
    out->lever_pos = a380x_sb_lever_pos(&s->lever);
    out->lever_out = out->handle > A380X_GS_LEVER_OUT_POS;
    out->inhibited = c->inhibited;
    out->ground_spoilers_out = c->ground_spoilers_out;
    out->phased = c->phased;
    out->lift_active = c->lift_active;
    out->speed_brake_deg = c->speed_brake_deg;
    out->ground_spoiler_deg = c->ground_spoiler_deg;
    out->extension_ratio = a380x_spoilers_extension(c);
    memcpy(out->panel_deg, c->panel_deg, sizeof(out->panel_deg));
}

#endif
