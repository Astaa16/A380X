/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Overhead controls whose systems X-Plane does not simulate (or this port has not ported yet).
 * Each publishes a writable state dataref a380x/xp12/ovhd/<name> that the cockpit manipulators
 * toggle or step directly (ATTR_manip_toggle / ATTR_manip_axis_knob), the same way the Laminar
 * A330 keeps switch state for its xlua systems. Push buttons are 0 = released (normal, lights
 * out), 1 = pushed; selectors use the documented positions. Future systems logic and button
 * annunciators read these values. The cockpit hotspots live in
 * config/overhead_extra_controls.json (tools/append_cockpit_controls.py).
 */

typedef struct {
    const char *name;
    float value, initial;
    XPLMDataRef handle;
} OvhdState;

static OvhdState ovhd_state[] = {
    /* ELEC */
    {"bat_ess", 0, 0, NULL}, {"bat_apu", 0, 0, NULL}, {"elmu", 0, 0, NULL},
    {"ac_ess_feed", 0, 0, NULL}, {"pax_sys", 0, 0, NULL}, {"galley", 0, 0, NULL},
    {"commercial_1", 0, 0, NULL}, {"commercial_2", 0, 0, NULL},
    {"idg_1_disc", 0, 0, NULL}, {"idg_2_disc", 0, 0, NULL}, {"idg_3_disc", 0, 0, NULL}, {"idg_4_disc", 0, 0, NULL},
    /* AIR: AIR FLOW 0 LO / 1 NORM / 2 HI / 3 MAN; XBLEED -1 CLOSE / 0 AUTO / 1 OPEN; temps 0 COLD .. 1 HOT */
    {"hot_air_1", 0, 0, NULL}, {"hot_air_2", 0, 0, NULL}, {"ram_air", 0, 0, NULL},
    {"cabin_air_extract", 0, 0, NULL}, {"bulk_cargo_isol", 0, 0, NULL}, {"fwd_cargo_isol", 0, 0, NULL},
    {"cargo_hot_air", 0, 0, NULL},
    {"air_flow", 1, 1, NULL}, {"xbleed", 0, 0, NULL},
    {"temp_cockpit", 0.5f, 0.5f, NULL}, {"temp_cabin", 0.5f, 0.5f, NULL},
    {"temp_fwd_cargo", 0.5f, 0.5f, NULL}, {"temp_bulk_cargo", 0.5f, 0.5f, NULL},
    /* CABIN PRESS: mode buttons 0 AUTO / 1 MAN; landing elevation target -1 DEC .. 1 INC;
     * MAN V/S switch -1 DN / 0 / 1 UP */
    {"press_alt_mode_man", 0, 0, NULL}, {"press_vs_mode_man", 0, 0, NULL}, {"ditching", 0, 0, NULL},
    {"cabin_alt_target", 0, 0, NULL}, {"cabin_vs_target", 0, 0, NULL},
    /* INT LT: EMER EXIT -1 OFF / 0 ARM / 1 ON; STORM and STBY COMPASS 0 OFF / 1 ON */
    {"emer_exit_lt", 0, 0, NULL}, {"storm_lt", 0, 0, NULL}, {"stby_compass_lt", 0, 0, NULL},
};

static float ovhd_get_f(void *refcon) { return ((OvhdState *)refcon)->value; }
static void ovhd_set_f(void *refcon, float value) { if (isfinite(value)) ((OvhdState *)refcon)->value = value; }
static int ovhd_get_i(void *refcon) { return (int)lroundf(((OvhdState *)refcon)->value); }
static void ovhd_set_i(void *refcon, int value) { ((OvhdState *)refcon)->value = (float)value; }

static void ovhd_state_shutdown(void)
{
    int i;
    for (i = 0; i < ARRAY_LEN(ovhd_state); ++i)
        if (ovhd_state[i].handle) { XPLMUnregisterDataAccessor(ovhd_state[i].handle); ovhd_state[i].handle = NULL; }
}

/* Never fatal: a name already owned by another plugin is skipped. */
static void ovhd_state_init(void)
{
    int i;
    for (i = 0; i < ARRAY_LEN(ovhd_state); ++i) {
        char name[96];
        OvhdState *s = &ovhd_state[i];
        s->value = s->initial;
        snprintf(name, sizeof(name), "a380x/xp12/ovhd/%s", s->name);
        if (XPLMFindDataRef(name)) continue;
        s->handle = XPLMRegisterDataAccessor(name, xplmType_Int | xplmType_Float, 1,
            ovhd_get_i, ovhd_set_i, ovhd_get_f, ovhd_set_f, NULL, NULL, NULL, NULL,
            NULL, NULL, NULL, NULL, s, s);
    }
}
