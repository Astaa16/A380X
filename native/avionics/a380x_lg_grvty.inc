/* SPDX-License-Identifier: GPL-3.0-or-later
 * L/G GRVTY panel: X-Plane adapter of the core in a380x_lg_grvty.h (docs/LG_GRVTY.md).
 *
 * Registers the a380x/xp12/gear/grvty_* commands and datarefs (tools/avionics_provider.py
 * GRVTY_COMMANDS / GRVTY_DATAREFS), runs the FBW gravity-extension handle on simulated time and
 * maps it onto X-Plane's gear:
 *   - >= 1 turn (cut-off valve closed): X-Plane's gear handle is held; the cockpit lever, the
 *     center-right gear_toggle command and the stock sim/flight_controls/landing_gear_* lever
 *     commands only move the crew lever (center_right.owned_lever_down);
 *   - >= 3 turns (gear uplocks released): X-Plane's handle is held DOWN and
 *     sim/flight_controls/landing_gear_emer_on is commanded once;
 *   - back below one turn (RESET): X-Plane's handle takes the crew lever position, then
 *     sim/flight_controls/landing_gear_emer_off releases the override.
 * Unity-build include of a380x_avionics.c after a380x_center_right.inc and before
 * a380x_fws_adapter.inc (which reads the switch position).
 */
#include "a380x_lg_grvty.h"

#define LGG_NAME(suffix) "a380x/xp12/gear/" suffix

enum { LGG_RAW_HANDLE, LGG_RAW_PAUSED, LGG_RAW_REPLAY, LGG_RAW_SPEED, LGG_RAW_COUNT };
static const char *const lgg_raw_names[LGG_RAW_COUNT] = {
    "sim/cockpit2/controls/gear_handle_down", "sim/time/paused", "sim/time/is_in_replay",
    "sim/time/sim_speed_actual",
};
/* Stock lever commands taken over while the hydraulics are isolated, then the override pair. */
enum { LGG_STOCK_TOGGLE, LGG_STOCK_UP, LGG_STOCK_DOWN, LGG_STOCK_OFF, LGG_STOCK_LEVERS,
       LGG_STOCK_EMER_ON = LGG_STOCK_LEVERS, LGG_STOCK_EMER_OFF, LGG_STOCK_COUNT };
static const char *const lgg_stock_names[LGG_STOCK_COUNT] = {
    "sim/flight_controls/landing_gear_toggle", "sim/flight_controls/landing_gear_up",
    "sim/flight_controls/landing_gear_down", "sim/flight_controls/landing_gear_off",
    "sim/flight_controls/landing_gear_emer_on", "sim/flight_controls/landing_gear_emer_off",
};
static const int lgg_stock_request[LGG_STOCK_LEVERS] = {
    A380X_GRVTY_LEVER_TOGGLE, A380X_GRVTY_LEVER_UP, A380X_GRVTY_LEVER_DOWN, A380X_GRVTY_LEVER_OFF,
};

enum { LGG_C_MASTER, LGG_C_GUARD_1, LGG_C_GUARD_2, LGG_C_SWITCH_UP, LGG_C_SWITCH_DOWN, LGG_C_COUNT };
static const char *const lgg_command_names[LGG_C_COUNT] = {
    LGG_NAME("grvty_master_guard_toggle"), LGG_NAME("grvty_guard_1_toggle"), LGG_NAME("grvty_guard_2_toggle"),
    LGG_NAME("grvty_switch_up"), LGG_NAME("grvty_switch_down"),
};
static const char *const lgg_command_descriptions[LGG_C_COUNT] = {
    "L/G GRVTY striped master cover: open / close",
    "L/G GRVTY switch guard 1 (left): open / close (master cover open)",
    "L/G GRVTY switch guard 2 (right): open / close (master cover open)",
    "L/G GRVTY switch one position up (toward RESET; master cover open)",
    "L/G GRVTY switch one position down (toward DOWN; DOWN needs the cover and both guards open)",
};

/* Integer state first, then real-valued (handle angle and the OBJ animation ratios). */
enum { LGG_R_MASTER, LGG_R_GUARD_1, LGG_R_GUARD_2, LGG_R_SWITCH_POS, LGG_R_TURNS, LGG_R_ISOLATED,
       LGG_R_DOORS, LGG_R_GEARS, LGG_R_LEVER_DISCONNECTED, LGG_R_EMER, LGG_R_ANGLE, LGG_R_MASTER_ANIM,
       LGG_R_GUARD_1_ANIM, LGG_R_GUARD_2_ANIM, LGG_R_SWITCH_ANIM, LGG_R_COUNT };
#define LGG_FIRST_REAL LGG_R_ANGLE
static const char *const lgg_ref_names[LGG_R_COUNT] = {
    LGG_NAME("grvty_master_guard"), LGG_NAME("grvty_guard_1"), LGG_NAME("grvty_guard_2"),
    LGG_NAME("grvty_switch_pos"), LGG_NAME("grvty_handle_turns"), LGG_NAME("grvty_hyd_isolated"),
    LGG_NAME("grvty_doors_released"), LGG_NAME("grvty_gears_released"),
    LGG_NAME("grvty_lever_disconnected"), LGG_NAME("grvty_emer_override"),
    LGG_NAME("grvty_handle_angle_deg"), LGG_NAME("grvty_master_guard_anim"),
    LGG_NAME("grvty_guard_1_anim"), LGG_NAME("grvty_guard_2_anim"), LGG_NAME("grvty_switch_anim"),
};

static struct {
    A380XGrvty panel;
    A380XGrvtyLever lever;
    XPLMDataRef raw[LGG_RAW_COUNT];
    XPLMCommandRef stock[LGG_STOCK_COUNT];
    unsigned char stock_handled[LGG_STOCK_LEVERS];
    XPLMCommandRef command[LGG_C_COUNT];
    XPLMDataRef ref[LGG_R_COUNT];
    unsigned refused;   /* interlock-refused clicks (diagnostics) */
    int ready;
} lgg;

static double lgg_value(int r)
{
    const A380XGrvty *g = &lgg.panel;
    switch (r) {
    case LGG_R_MASTER: return g->master_guard;
    case LGG_R_GUARD_1: return g->guard[0];
    case LGG_R_GUARD_2: return g->guard[1];
    case LGG_R_SWITCH_POS: return g->switch_pos;
    case LGG_R_TURNS: return a380x_grvty_turns(g);
    case LGG_R_ISOLATED: return a380x_grvty_hyd_isolated(g);
    case LGG_R_DOORS: return a380x_grvty_doors_released(g);
    case LGG_R_GEARS: return a380x_grvty_gears_released(g);
    case LGG_R_LEVER_DISCONNECTED: return lgg.lever.owned;
    case LGG_R_EMER: return lgg.lever.emer_override;
    case LGG_R_ANGLE: return g->handle_angle_deg;
    case LGG_R_MASTER_ANIM: return g->master_anim;
    case LGG_R_GUARD_1_ANIM: return g->guard_anim[0];
    case LGG_R_GUARD_2_ANIM: return g->guard_anim[1];
    case LGG_R_SWITCH_ANIM: return g->switch_anim;
    default: return 0.0;
    }
}
static int lgg_get_int(void *refcon) { return (int)lround(lgg_value((int)(intptr_t)refcon)); }
static float lgg_get_float(void *refcon) { return (float)lgg_value((int)(intptr_t)refcon); }
static double lgg_get_double(void *refcon) { return lgg_value((int)(intptr_t)refcon); }

static void lgg_apply(A380XGrvtyLeverActions a)
{
    if (a.write_handle >= 0 && lgg.raw[LGG_RAW_HANDLE]) XPLMSetDatai(lgg.raw[LGG_RAW_HANDLE], a.write_handle);
    if (a.emer_on && lgg.stock[LGG_STOCK_EMER_ON]) XPLMCommandOnce(lgg.stock[LGG_STOCK_EMER_ON]);
    if (a.emer_off && lgg.stock[LGG_STOCK_EMER_OFF]) XPLMCommandOnce(lgg.stock[LGG_STOCK_EMER_OFF]);
    center_right.lever_owned = lgg.lever.owned;
}
static int lgg_raw_int(int r) { return lgg.raw[r] ? XPLMGetDatai(lgg.raw[r]) : 0; }

/* Run before center_right_update(): the crew lever is then current for the lamps and the FWS. */
static void lgg_update(float dt)
{
    double sim_dt = 0.0;
    float speed;
    int replay;
    if (!lgg.ready || !plugin_enabled) return;
    if (!isfinite(dt) || dt < 0.0f || dt > 5.0f) dt = 0.0f;
    speed = lgg.raw[LGG_RAW_SPEED] ? XPLMGetDataf(lgg.raw[LGG_RAW_SPEED]) : 1.0f;
    replay = lgg_raw_int(LGG_RAW_REPLAY) != 0;
    if (isfinite(speed) && speed >= 0.0f && speed <= 100.0f && !lgg_raw_int(LGG_RAW_PAUSED) && !replay)
        sim_dt = (double)dt * speed;
    a380x_grvty_update(&lgg.panel, sim_dt, dt);
    if (replay) return; /* X-Plane replays its own gear; never fight it */
    lgg_apply(a380x_grvty_lever_step(&lgg.lever, &lgg.panel, lgg_raw_int(LGG_RAW_HANDLE),
                                     &center_right.owned_lever_down));
}

static int lgg_command_handler(XPLMCommandRef command, XPLMCommandPhase phase, void *refcon)
{
    A380XGrvty *g = &lgg.panel;
    int accepted = 0;
    (void)command;
    if (phase != xplm_CommandBegin || !plugin_enabled || !lgg.ready) return 1;
    switch ((int)(intptr_t)refcon) {
    case LGG_C_MASTER: accepted = a380x_grvty_master_toggle(g); break;
    case LGG_C_GUARD_1: accepted = a380x_grvty_guard_toggle(g, 0); break;
    case LGG_C_GUARD_2: accepted = a380x_grvty_guard_toggle(g, 1); break;
    case LGG_C_SWITCH_UP: accepted = a380x_grvty_switch_step(g, -1); break;
    case LGG_C_SWITCH_DOWN: accepted = a380x_grvty_switch_step(g, 1); break;
    default: break;
    }
    if (!accepted) ++lgg.refused;   /* interlock: nothing moves */
    return 1;
}

/* "Before" handler of the stock lever commands: consumed only while the hydraulics are isolated. */
static int lgg_stock_lever_handler(XPLMCommandRef command, XPLMCommandPhase phase, void *refcon)
{
    int k = (int)(intptr_t)refcon;
    (void)command;
    if (!plugin_enabled || !lgg.ready || !lgg.lever.owned || k < 0 || k >= LGG_STOCK_LEVERS) return 1;
    if (phase == xplm_CommandBegin)
        a380x_grvty_lever_command(&lgg.lever, lgg_stock_request[k], &center_right.owned_lever_down);
    return 0;
}

static int lgg_switch_position(void) { return lgg.ready ? lgg.panel.switch_pos : A380X_GRVTY_OFF; }

/* Plugin disable: the lever drives X-Plane's gear again (handle = crew lever, override off). */
static void lgg_release_controls(void)
{
    if (!lgg.ready) return;
    lgg_apply(a380x_grvty_lever_release(&lgg.lever, center_right.owned_lever_down));
}

/* XPLM_MSG_PLANE_LOADED: cold & dark panel; X-Plane has set its own gear handle. */
static void lgg_plane_loaded(void)
{
    if (!lgg.ready) return;
    lgg_apply(a380x_grvty_lever_abandon(&lgg.lever));
    a380x_grvty_init(&lgg.panel);
}

static void lgg_shutdown(void)
{
    int i;
    lgg_release_controls();
    lgg.ready = 0;
    for (i = 0; i < LGG_STOCK_LEVERS; ++i) if (lgg.stock_handled[i]) {
        XPLMUnregisterCommandHandler(lgg.stock[i], lgg_stock_lever_handler, 1, (void *)(intptr_t)i);
        lgg.stock_handled[i] = 0;
    }
    for (i = 0; i < LGG_C_COUNT; ++i) if (lgg.command[i]) {
        XPLMUnregisterCommandHandler(lgg.command[i], lgg_command_handler, 1, (void *)(intptr_t)i);
        lgg.command[i] = NULL;
    }
    for (i = 0; i < LGG_R_COUNT; ++i) if (lgg.ref[i]) {
        XPLMUnregisterDataAccessor(lgg.ref[i]);
        lgg.ref[i] = NULL;
    }
    center_right.lever_owned = 0;
}

static int lgg_init(void)
{
    int i;
    memset(&lgg, 0, sizeof(lgg));
    a380x_grvty_init(&lgg.panel);
    for (i = 0; i < LGG_RAW_COUNT; ++i)
        if (!(lgg.raw[i] = XPLMFindDataRef(lgg_raw_names[i]))) return 0;
    for (i = 0; i < LGG_R_COUNT; ++i) if (XPLMFindDataRef(lgg_ref_names[i])) return 0;
    for (i = 0; i < LGG_R_COUNT; ++i) {
        void *refcon = (void *)(intptr_t)i;
        lgg.ref[i] = i >= LGG_FIRST_REAL
            ? XPLMRegisterDataAccessor(lgg_ref_names[i], xplmType_Float | xplmType_Double, 0, NULL, NULL,
                  lgg_get_float, NULL, lgg_get_double, NULL, NULL, NULL, NULL, NULL, NULL, NULL, refcon, NULL)
            : XPLMRegisterDataAccessor(lgg_ref_names[i], xplmType_Int, 0, lgg_get_int, NULL, NULL, NULL,
                  NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, refcon, NULL);
        if (!lgg.ref[i]) { lgg_shutdown(); return 0; }
    }
    for (i = 0; i < LGG_C_COUNT; ++i) {
        /* XPLM retains commands across aircraft reloads; CreateCommand reuses them. */
        lgg.command[i] = XPLMCreateCommand(lgg_command_names[i], lgg_command_descriptions[i]);
        if (!lgg.command[i]) { lgg_shutdown(); return 0; }
        XPLMRegisterCommandHandler(lgg.command[i], lgg_command_handler, 1, (void *)(intptr_t)i);
    }
    for (i = 0; i < LGG_STOCK_COUNT; ++i) {
        lgg.stock[i] = XPLMFindCommand(lgg_stock_names[i]);
        if (lgg.stock[i] && i < LGG_STOCK_LEVERS) {
            XPLMRegisterCommandHandler(lgg.stock[i], lgg_stock_lever_handler, 1, (void *)(intptr_t)i);
            lgg.stock_handled[i] = 1;
        }
    }
    lgg.ready = 1;
    center_right.lever_owned = 0;
    return 1;
}
