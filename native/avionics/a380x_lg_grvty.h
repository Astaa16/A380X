/* SPDX-License-Identifier: GPL-3.0-or-later
 * A380 L/G GRVTY panel and gravity-extension handle: XPLM-free core (docs/LG_GRVTY.md).
 *
 * Translated from the pinned FlyByWire A380X source:
 *   Part_Interior_Cockpit/model/behaviour/mip.xml (lines ~122-188)
 *     GRVTY_LDG_LOCK1 (striped master cover) toggles L:A32NX_LG_GRVTY_MASTER_SWITCH_GUARD;
 *     GRVTY_LDG_LOCK2 / LOCK3 write L:A32NX_LG_GRVTY_SWITCH_GUARD_1 / _2 and move only while the
 *     master guard is open (CODE_POS_0/1_VERIF);
 *     GRVTY_LDG (ASOBO_GT_Switch_3States, START_STATE 1) writes L:A32NX_LG_GRVTY_SWITCH_POS
 *     0 RESET / 1 OFF (master guard open) / 2 DOWN (master guard and both switch guards open).
 *   wasm/systems/a380_systems/src/hydraulic/mod.rs
 *     A380GravityExtension: virtual handle angle +50 deg/s in DOWN (Extension), -200 deg/s in
 *     RESET, unchanged in OFF, clamped to 0..1080 deg; number of turns = floor(angle / 360).
 *     A380GearHydraulicController::update_safety_and_vent_valve / update_uplocks:
 *     >= 1 turn: cut-off valve closed and vent valves open (gear hydraulics isolated);
 *     >= 2 turns: door uplocks mechanically released; >= 3 turns: gear uplocks released.
 *   fbw-common .../hydraulic/landing_gear.rs: with the cut-off valve closed the gear manifold has
 *     no pressure, so the lever (LGCIU) moves nothing; a released uplock opens the door / extends
 *     the gear by gravity and air load, and the gear stays down until hydraulics return.
 *
 * The lever part (A380XGrvtyLever) is the X-Plane mapping: while the gear hydraulics are
 * isolated the plugin holds X-Plane's gear handle and the physical lever is a separate crew
 * lever; at >= 3 turns X-Plane's gear is selected down and its emergency override is engaged
 * (sim/flight_controls/landing_gear_emer_on, the mechanism Laminar's own A330 uses for its
 * gravity switch). Below one turn the handle follows the crew lever again, then the override is
 * released, so a lever already DOWN never retracts the gear.
 */
#ifndef A380X_LG_GRVTY_H
#define A380X_LG_GRVTY_H
#include <math.h>
#include <string.h>

enum { A380X_GRVTY_RESET = 0, A380X_GRVTY_OFF = 1, A380X_GRVTY_DOWN = 2 };
enum { A380X_GRVTY_LEVER_UP = 0, A380X_GRVTY_LEVER_DOWN = 1, A380X_GRVTY_LEVER_TOGGLE = 2,
       A380X_GRVTY_LEVER_OFF = 3 };

#define A380X_GRVTY_EXTEND_DEG_S 50.0   /* PLACEHOLDER_SPEED_TO_ROTATE_HANDLE_DEG_S */
#define A380X_GRVTY_RESET_DEG_S 200.0   /* PLACEHOLDER_SPEED_TO_RESET_HANDLE_DEG_S */
#define A380X_GRVTY_MAX_DEG (360.0 * 3.0)
#define A380X_GRVTY_GUARD_SECONDS 0.22f  /* cover / guard swing (as the ELEC guards) */
#define A380X_GRVTY_SWITCH_SECONDS 0.10f /* switch throw per position */

typedef struct {
    unsigned char master_guard;   /* L:A32NX_LG_GRVTY_MASTER_SWITCH_GUARD, 1 = open */
    unsigned char guard[2];       /* L:A32NX_LG_GRVTY_SWITCH_GUARD_1 / _2, 1 = open */
    unsigned char switch_pos;     /* L:A32NX_LG_GRVTY_SWITCH_POS 0 RESET, 1 OFF, 2 DOWN */
    double handle_angle_deg;      /* A380GravityExtension.handle_angle, 0..1080 */
    /* OBJ animation (wall time): cover / guards 0 closed .. 1 open, switch 0 RESET .. 2 DOWN */
    float master_anim, guard_anim[2], switch_anim;
} A380XGrvty;

/* Cold & dark: covers and guards closed, switch OFF, handle at rest. */
static inline void a380x_grvty_init(A380XGrvty *g)
{
    memset(g, 0, sizeof(*g));
    g->switch_pos = A380X_GRVTY_OFF;
    g->switch_anim = (float)A380X_GRVTY_OFF;
}

/* GRVTY_LDG_LOCK1 LEFT_SINGLE_CODE: an unconditional toggle. */
static inline int a380x_grvty_master_toggle(A380XGrvty *g)
{
    g->master_guard = (unsigned char)!g->master_guard;
    return 1;
}

/* GRVTY_LDG_LOCK2 / LOCK3 (guard 0 / 1): both positions verify the master guard. */
static inline int a380x_grvty_guard_toggle(A380XGrvty *g, int guard)
{
    if (guard < 0 || guard > 1 || !g->master_guard) return 0;
    g->guard[guard] = (unsigned char)!g->guard[guard];
    return 1;
}

/* GRVTY_LDG CODE_POS_n_VERIF. */
static inline int a380x_grvty_switch_allowed(const A380XGrvty *g, int position)
{
    if (position == A380X_GRVTY_DOWN) return g->master_guard && g->guard[0] && g->guard[1];
    return (position == A380X_GRVTY_RESET || position == A380X_GRVTY_OFF) && g->master_guard;
}

/* Returns 1 when the switch moved; a refused move leaves the switch (and its animation) alone. */
static inline int a380x_grvty_switch_set(A380XGrvty *g, int position)
{
    if (position < A380X_GRVTY_RESET || position > A380X_GRVTY_DOWN || position == g->switch_pos ||
        !a380x_grvty_switch_allowed(g, position)) return 0;
    g->switch_pos = (unsigned char)position;
    return 1;
}

/* One detent per click: direction > 0 toward DOWN, < 0 toward RESET. */
static inline int a380x_grvty_switch_step(A380XGrvty *g, int direction)
{
    if (!direction) return 0;
    return a380x_grvty_switch_set(g, (int)g->switch_pos + (direction > 0 ? 1 : -1));
}

static inline float a380x_grvty_approach(float value, float target, float step)
{
    if (!isfinite(value)) return target;
    return value < target ? fminf(target, value + step) : fmaxf(target, value - step);
}

/* sim_dt: simulated seconds (0 while paused / in replay) for the handle, as FBW's
 * context.delta(); anim_dt: wall seconds for the switch and guard motion. */
static inline void a380x_grvty_update(A380XGrvty *g, double sim_dt, float anim_dt)
{
    double rate = g->switch_pos == A380X_GRVTY_DOWN ? A380X_GRVTY_EXTEND_DEG_S
                : g->switch_pos == A380X_GRVTY_RESET ? -A380X_GRVTY_RESET_DEG_S : 0.0;
    int i;
    if (!isfinite(sim_dt) || sim_dt < 0.0) sim_dt = 0.0;
    if (!isfinite(anim_dt) || anim_dt < 0.0f) anim_dt = 0.0f;
    if (!isfinite(g->handle_angle_deg)) g->handle_angle_deg = 0.0;
    g->handle_angle_deg = fmax(0.0, fmin(A380X_GRVTY_MAX_DEG, g->handle_angle_deg + rate * sim_dt));
    g->master_anim = a380x_grvty_approach(g->master_anim, (float)g->master_guard, anim_dt / A380X_GRVTY_GUARD_SECONDS);
    for (i = 0; i < 2; ++i)
        g->guard_anim[i] = a380x_grvty_approach(g->guard_anim[i], (float)g->guard[i], anim_dt / A380X_GRVTY_GUARD_SECONDS);
    g->switch_anim = a380x_grvty_approach(g->switch_anim, (float)g->switch_pos, anim_dt / A380X_GRVTY_SWITCH_SECONDS);
}

/* GearGravityExtension::extension_handle_number_of_turns: floor(angle / 360) as u8. */
static inline int a380x_grvty_turns(const A380XGrvty *g)
{
    double turns = floor(g->handle_angle_deg / 360.0);
    return turns <= 0.0 ? 0 : turns >= 3.0 ? 3 : (int)turns;
}
/* A380GearHydraulicController: cut-off valve closed / vent valves open. */
static inline int a380x_grvty_hyd_isolated(const A380XGrvty *g) { return a380x_grvty_turns(g) >= 1; }
static inline int a380x_grvty_doors_released(const A380XGrvty *g) { return a380x_grvty_turns(g) >= 2; }
static inline int a380x_grvty_gears_released(const A380XGrvty *g) { return a380x_grvty_turns(g) >= 3; }

/* ------------------------------------------------------------------ X-Plane gear-handle mapping */
typedef struct {
    unsigned char owned;            /* the plugin holds X-Plane's gear handle (hydraulics isolated) */
    unsigned char held_handle_down; /* value X-Plane's gear_handle_down is held at while owned */
    unsigned char gear_released;    /* this episode reached the gear uplock release */
    unsigned char emer_override;    /* landing_gear_emer_on issued and not yet released */
} A380XGrvtyLever;

typedef struct {
    signed char write_handle;       /* -1 none, else the value to write to gear_handle_down */
    unsigned char emer_on, emer_off;/* command landing_gear_emer_on / _off once (after the write) */
} A380XGrvtyLeverActions;

static inline A380XGrvtyLeverActions a380x_grvty_no_action(void)
{
    A380XGrvtyLeverActions a;
    a.write_handle = -1;
    a.emer_on = a.emer_off = 0;
    return a;
}

/* One frame, after a380x_grvty_update. sim_handle_down: X-Plane's gear handle now. *crew_lever:
 * the physical L/G lever (FBW GEAR_LEVER_POSITION_REQUEST); it follows X-Plane's handle while the
 * lever drives the gear and is the plugin's own state while the hydraulics are isolated. */
static inline A380XGrvtyLeverActions a380x_grvty_lever_step(A380XGrvtyLever *l, const A380XGrvty *g,
                                                           int sim_handle_down, int *crew_lever)
{
    A380XGrvtyLeverActions a = a380x_grvty_no_action();
    int isolated = a380x_grvty_hyd_isolated(g);
    sim_handle_down = sim_handle_down != 0;
    if (!l->owned) {
        *crew_lever = sim_handle_down;
        if (!isolated) return a;
        l->owned = 1;                                   /* cut-off valve closed: hold the gear */
        l->held_handle_down = (unsigned char)sim_handle_down;
        l->gear_released = 0;
    }
    if (sim_handle_down != l->held_handle_down) {
        /* Something else moved X-Plane's handle (hardware, a dataref writer): that is the crew
         * moving the lever, which has no hydraulic effect now. */
        *crew_lever = sim_handle_down;
        a.write_handle = (signed char)l->held_handle_down;
    }
    if (a380x_grvty_gears_released(g) && !l->gear_released) {
        /* Gear uplocks released: the gear falls down and locks whatever the lever says. */
        l->gear_released = 1;
        l->held_handle_down = 1;
        a.write_handle = 1;
        if (!l->emer_override) { l->emer_override = 1; a.emer_on = 1; }
    }
    if (!isolated) {
        /* RESET brought the handle below one turn: cut-off valve open, the lever controls the gear
         * again. The handle takes the lever position before the override is released. */
        l->owned = 0;
        a.write_handle = (signed char)(*crew_lever != 0);
        if (l->emer_override) { l->emer_override = 0; a.emer_off = 1; }
    }
    return a;
}

/* A crew lever command. Returns 1 when it only moves the crew lever (the X-Plane handle is held). */
static inline int a380x_grvty_lever_command(const A380XGrvtyLever *l, int request, int *crew_lever)
{
    if (!l->owned) return 0;
    if (request == A380X_GRVTY_LEVER_TOGGLE) *crew_lever = !*crew_lever;
    else if (request == A380X_GRVTY_LEVER_UP) *crew_lever = 0;
    else if (request == A380X_GRVTY_LEVER_DOWN) *crew_lever = 1;
    /* A380X_GRVTY_LEVER_OFF: the A380 lever has no OFF position. */
    return 1;
}

/* Plugin disable / stop: hand X-Plane's handle back at the crew lever, then release the override. */
static inline A380XGrvtyLeverActions a380x_grvty_lever_release(A380XGrvtyLever *l, int crew_lever)
{
    A380XGrvtyLeverActions a = a380x_grvty_no_action();
    if (l->owned) a.write_handle = (signed char)(crew_lever != 0);
    if (l->emer_override) a.emer_off = 1;
    memset(l, 0, sizeof(*l));
    return a;
}

/* Aircraft (re)load: X-Plane has just set its own handle; only the override is released. */
static inline A380XGrvtyLeverActions a380x_grvty_lever_abandon(A380XGrvtyLever *l)
{
    A380XGrvtyLeverActions a = a380x_grvty_no_action();
    if (l->emer_override) a.emer_off = 1;
    memset(l, 0, sizeof(*l));
    return a;
}
#endif
