/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exercises production ADIRS XPLM boundaries without a simulator process.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"
#define ARRAY_LEN(a) ((int)(sizeof(a)/sizeof((a)[0])))
typedef struct {
    char name[180];
    int registered, writable, vi[20];
    double value;
    float vf[20];
    XPLMGetDatai_f get_i;
    XPLMGetDataf_f get_f;
    XPLMGetDatad_f get_d;
    XPLMSetDatai_f set_i;
    void *read_context, *write_context;
} MockRef;
typedef struct {
    char name[180];
    XPLMCommandCallback_f callback;
    void *context;
} MockCommand;
static MockRef mock_refs[2048];
static MockCommand mock_commands[64];
static int ref_count, command_count, fail_registration_after = -1;
static const char *missing_raw;
static MockRef *raw(const char *name) { return XPLMFindDataRef(name); }
static MockRef *new_ref(const char *name) {
    assert(ref_count < ARRAY_LEN(mock_refs));
    MockRef *r = &mock_refs[ref_count++];
    memset(r, 0, sizeof(*r));
    snprintf(r->name, sizeof(r->name), "%s", name);
    r->registered = 1;
    if (strstr(name, "barometer_setting")) r->value = 29.92;
    if (strstr(name, "sim_speed_actual")) r->value = 1;
    if (strstr(name, "acf_Vne")) r->value = 340;
    if (strstr(name, "acf_Mmo")) r->value = .89;
    if (strstr(name, "airspeed_kts")) r->value = 150;
    if (strstr(name, "altitude_ft")) r->value = 10000;
    return r;
}
XPLMDataRef XPLMFindDataRef(const char *name) {
    if (missing_raw && !strcmp(name, missing_raw)) return NULL;
    for (int n = 0; n < ref_count; ++n)
        if (mock_refs[n].registered && !strcmp(name, mock_refs[n].name)) return &mock_refs[n];
    return !strncmp(name, "sim/", 4) ? new_ref(name) : NULL;
}
int XPLMGetDatai(XPLMDataRef handle) {
    MockRef *r = handle; assert(r && r->registered);
    return r->get_i ? r->get_i(r->read_context) : (int)r->value;
}
float XPLMGetDataf(XPLMDataRef handle) {
    MockRef *r = handle; assert(r && r->registered);
    return r->get_f ? r->get_f(r->read_context) : (float)r->value;
}
double XPLMGetDatad(XPLMDataRef handle) {
    MockRef *r = handle; assert(r && r->registered);
    return r->get_d ? r->get_d(r->read_context) : r->value;
}
void XPLMSetDatai(XPLMDataRef handle, int value) {
    MockRef *r = handle; assert(r && r->registered);
    if (r->set_i) { assert(r->writable); r->set_i(r->write_context, value); }
    else r->value = value;
}
int XPLMGetDatavf(XPLMDataRef handle, float *values, int offset, int count) {
    MockRef *r = handle; assert(r && r->registered && offset >= 0 && offset + count <= 20);
    memcpy(values, r->vf + offset, (size_t)count * sizeof(*values)); return count;
}
int XPLMGetDatavi(XPLMDataRef handle, int *values, int offset, int count) {
    MockRef *r = handle; assert(r && r->registered && offset >= 0 && offset + count <= 20);
    memcpy(values, r->vi + offset, (size_t)count * sizeof(*values)); return count;
}
XPLMDataRef XPLMRegisterDataAccessor(const char *name, XPLMDataTypeID type, int writable,
    XPLMGetDatai_f get_i, XPLMSetDatai_f set_i, XPLMGetDataf_f get_f, XPLMSetDataf_f set_f,
    XPLMGetDatad_f get_d, XPLMSetDatad_f set_d, XPLMGetDatavi_f get_vi, XPLMSetDatavi_f set_vi,
    XPLMGetDatavf_f get_vf, XPLMSetDatavf_f set_vf, XPLMGetDatab_f get_b, XPLMSetDatab_f set_b,
    void *read_context, void *write_context) {
    assert(!XPLMFindDataRef(name));
    assert(type == xplmType_Int || type == (xplmType_Float | xplmType_Double));
    assert(!set_f && !set_d && !get_vi && !set_vi && !get_vf && !set_vf && !get_b && !set_b);
    assert((writable && set_i && write_context) || (!writable && !set_i && !write_context));
    if (fail_registration_after == 0) return NULL;
    if (fail_registration_after > 0) --fail_registration_after;
    MockRef *r = new_ref(name);
    r->writable = writable; r->get_i = get_i; r->get_f = get_f; r->get_d = get_d; r->set_i = set_i;
    r->read_context = read_context; r->write_context = write_context;
    return r;
}
void XPLMUnregisterDataAccessor(XPLMDataRef handle) {
    MockRef *r = handle; assert(r && r->registered); r->registered = 0;
}
XPLMCommandRef XPLMFindCommand(const char *name) {
    for (int n = 0; n < command_count; ++n)
        if (!strcmp(name, mock_commands[n].name)) return &mock_commands[n];
    return NULL;
}
XPLMCommandRef XPLMCreateCommand(const char *name, const char *description) {
    (void)description;
    MockCommand *c = XPLMFindCommand(name);
    if (c) return c;
    assert(command_count < ARRAY_LEN(mock_commands));
    c = &mock_commands[command_count++];
    snprintf(c->name, sizeof(c->name), "%s", name); return c;
}
void XPLMRegisterCommandHandler(XPLMCommandRef handle, XPLMCommandCallback_f callback,
                               int before, void *context) {
    MockCommand *c = handle; assert(c && !c->callback && callback && before);
    c->callback = callback; c->context = context;
}
void XPLMUnregisterCommandHandler(XPLMCommandRef handle, XPLMCommandCallback_f callback,
                                 int before, void *context) {
    MockCommand *c = handle; assert(c && c->callback == callback && c->context == context && before);
    c->callback = NULL; c->context = NULL;
}
typedef struct {
    XPLMDataRef mach, fail_ias, fail_altitude, fail_attitude, fail_heading, fail_vertical_speed;
    float ias_value, mach_value, altitude_value, pitch_value, roll_value, heading_value, vertical_speed_value;
    int air_valid, altitude_valid, attitude_valid, heading_valid, vertical_speed_valid, navigation_valid;
    int ias_trend_visible, trend_initialized;
} Side;
static struct { XPLMDataRef on_ground, ground_speed, ground_track, n1; Side side[2]; } refs;
static int plugin_enabled = 1;
static int failed(XPLMDataRef r) { return r && XPLMGetDatai(r) >= 6; }
static XPLMDataRef find_required(const char *name, int *ok) {
    XPLMDataRef r = XPLMFindDataRef(name); if (!r) *ok = 0; return r;
}
#include "a380x_adirs_adapter.inc"

static void command(const char *suffix, XPLMCommandPhase phase) {
    char name[180]; snprintf(name, sizeof(name), "a380x/xp12/adirs/%s", suffix);
    MockCommand *c = XPLMFindCommand(name); assert(c && c->callback);
    assert(c->callback(c, phase, c->context) == 1);
}
static void once(const char *suffix) { command(suffix, xplm_CommandBegin); command(suffix, xplm_CommandEnd); }
static int published(const char *suffix) {
    char name[180]; snprintf(name, sizeof(name), "a380x/xp12/adirs/%s", suffix);
    return XPLMGetDatai(XPLMFindDataRef(name));
}
static void inject(const char *suffix, int value) {
    char name[180]; snprintf(name, sizeof(name), "a380x/xp12/adirs/%s", suffix);
    XPLMSetDatai(XPLMFindDataRef(name), value);
}
static void cold_inputs(void) {
    for (int n = 0; n < ref_count; ++n) if (!strncmp(mock_refs[n].name, "sim/", 4)) {
        mock_refs[n].value = 0;
        memset(mock_refs[n].vf, 0, sizeof(mock_refs[n].vf));
        memset(mock_refs[n].vi, 0, sizeof(mock_refs[n].vi));
        if (strstr(mock_refs[n].name, "barometer_setting")) mock_refs[n].value = 29.92;
    }
    refs.on_ground = raw("sim/flightmodel/failures/onground_any");
    refs.ground_speed = raw("sim/flightmodel/position/groundspeed");
    refs.ground_track = raw("sim/flightmodel/position/hpath");
    refs.n1 = raw("sim/cockpit2/engine/indicators/N1_percent");
    ((MockRef *)refs.on_ground)->value = 1;
    for (int n = 0; n < 2; ++n) {
        char name[100];
        snprintf(name, sizeof(name), "sim/cockpit2/gauges/indicators/mach_%s", n ? "copilot" : "pilot");
        refs.side[n].mach = raw(name);
#define SIDE_REF(field, tail) snprintf(name, sizeof(name), "sim/operation/failures/%s_" tail, n ? "rel_cop" : "rel_ss"); refs.side[n].field = raw(name)
        SIDE_REF(fail_ias, "asi"); SIDE_REF(fail_altitude, "alt");
        SIDE_REF(fail_attitude, "ahz"); SIDE_REF(fail_heading, "dgy"); SIDE_REF(fail_vertical_speed, "vvi");
#undef SIDE_REF
    }
    raw("sim/cockpit2/electrical/bus_volts")->vf[0] = 28;
    raw("sim/cockpit2/electrical/battery_voltage_actual_volts")->vf[0] = 28;
    raw("sim/cockpit2/electrical/battery_on")->vi[0] = 1;
    raw("sim/time/sim_speed_actual")->value = 1;
    raw("sim/aircraft/view/acf_Vne")->value = 340;
    raw("sim/aircraft/view/acf_Mmo")->value = .89;
}
static void test_controls_sources_and_failures(void) {
    cold_inputs(); assert(adirs_init());
    assert(published("ir1_mode") == 0 && !refs.side[0].air_valid && !refs.side[0].attitude_valid);
    once("ir1_mode_down"); assert(published("ir1_mode") == 0);
    once("ir1_mode_up"); once("ir2_mode_up"); once("ir3_mode_up");
    assert(published("on_batt") && refs.side[0].air_valid && !refs.side[0].attitude_valid);
    once("align_now"); assert(refs.side[0].attitude_valid && refs.side[1].navigation_valid);
    command("ir1_toggle", xplm_CommandBegin); adirs_update(.05f);
    assert(published("ir1_off") && adirs_sample[0].ir_press > 0 && refs.side[0].air_valid);
    assert(!refs.side[0].attitude_valid);
    command("ir1_toggle", xplm_CommandContinue); assert(published("ir1_off"));
    command("ir1_toggle", xplm_CommandEnd); adirs_update(1);
    assert(adirs_sample[0].ir_press < .001);
    once("ir1_toggle"); assert(refs.side[0].attitude_valid);
    once("adr2_toggle"); assert(published("adr2_off") && !refs.side[1].air_valid && refs.side[1].attitude_valid);
    once("adr2_toggle");
    inject("ir1_failure", 1); inject("adr2_failure", 1);
    assert(published("ir1_fault") && !refs.side[0].attitude_valid && refs.side[0].air_valid);
    assert(published("adr2_fault") && !refs.side[1].air_valid && refs.side[1].attitude_valid);
    assert(published("captain_ir_source") == 0 && published("first_officer_adr_source") == 1);
    ((MockRef *)adirs_sample[2].ias_ref)->value = 222;
    ((MockRef *)adirs_sample[2].altitude_ref)->value = 12000;
    ((MockRef *)adirs_sample[1].baro_ref)->value = 30.02;
    once("captain_ir_toggle"); once("first_officer_adr_toggle");
    assert(refs.side[0].attitude_valid && refs.side[1].air_valid && refs.side[1].ias_value == 222);
    assert(fabs(refs.side[1].altitude_value - 12100) < .01);
    assert(published("captain_adr_source") == 0 && published("first_officer_ir_source") == 1);
    inject("adr3_failure", 1); assert(!refs.side[1].air_valid && refs.side[0].attitude_valid);
    inject("adr3_failure", 0); assert(refs.side[1].air_valid);
    ((MockRef *)adirs_sample[2].alpha_ref)->value = NAN; adirs_update(0);
    assert(!refs.side[1].air_valid && refs.side[1].attitude_valid);
    ((MockRef *)adirs_sample[2].alpha_ref)->value = 0; adirs_update(0);
    assert(refs.side[1].air_valid);
    ((MockRef *)adirs_sample[1].baro_ref)->value = NAN; adirs_update(0);
    assert(refs.side[1].air_valid && !refs.side[1].altitude_valid);
    ((MockRef *)adirs_sample[1].baro_ref)->value = 29.92;
    inject("ir1_failure", 0); inject("adr2_failure", 0); once("align_now");
    once("captain_ir_toggle"); once("first_officer_adr_toggle");
    ((MockRef *)refs.side[0].fail_ias)->value = 5; adirs_update(0); assert(refs.side[0].air_valid);
    ((MockRef *)refs.side[0].fail_ias)->value = 6; adirs_update(0); assert(!refs.side[0].air_valid);
    once("captain_adr_toggle"); assert(refs.side[0].air_valid);
    adirs_shutdown();
}
static void test_att_lock_and_hot_reset(void) {
    cold_inputs(); ((MockRef *)refs.n1)->vf[0] = 25; assert(adirs_init());
    assert(published("ir1_mode") == 1 && refs.side[0].navigation_valid);
    ((MockRef *)refs.on_ground)->value = 0;
    once("ir1_mode_up"); assert(published("ir1_nav_locked") && !refs.side[0].navigation_valid);
    adirs_update(28); assert(refs.side[0].attitude_valid && !refs.side[0].heading_valid);
    assert(!refs.side[0].vertical_speed_valid);
    assert(adirs_enter_heading(0, 123)); assert(refs.side[0].heading_valid && refs.side[0].heading_value == 123);
    raw("sim/flightmodel/position/psi")->value = 20; adirs_update(1);
    assert(refs.side[0].heading_value == 143);
    adirs_update(599); assert(published("ir1_heading_reminder"));
    once("ir1_mode_down"); assert(!refs.side[0].navigation_valid);
    ((MockRef *)refs.on_ground)->value = 1; once("align_now");
    assert(published("ir1_nav_locked") && !refs.side[0].navigation_valid);
    ((MockRef *)refs.on_ground)->value = 0;
    adirs_reset(); assert(!published("ir1_nav_locked") && refs.side[0].navigation_valid);
    adirs_shutdown();
}
static void test_power_timing_disabled_and_reload(void) {
    cold_inputs(); assert(adirs_init()); once("ir1_mode_up");
    raw("sim/time/sim_speed_actual")->value = 2; adirs_update(1);
    assert(adirs.unit[0].alignment_elapsed == 2);
    raw("sim/time/paused")->value = 1; adirs_update(50);
    assert(adirs.unit[0].alignment_elapsed == 2);
    raw("sim/time/sim_speed_actual")->value = 0;
    once("ir2_mode_up"); command("adr1_toggle", xplm_CommandBegin); adirs_update(.05f);
    assert(adirs_sample[1].knob > 0 && adirs_sample[1].knob < 1);
    assert(adirs_sample[0].adr_press > 0 && adirs.unit[0].alignment_elapsed == 2);
    command("adr1_toggle", xplm_CommandEnd); once("adr1_toggle");
    raw("sim/time/sim_speed_actual")->value = 2;
    raw("sim/time/paused")->value = 0;
    raw("sim/operation/prefs/replay_mode")->value = 1; adirs_update(50);
    assert(adirs.unit[0].alignment_elapsed == 2);
    raw("sim/operation/prefs/replay_mode")->value = 0;
    once("align_now"); adirs_update(10); assert(published("on_batt"));
    raw("sim/cockpit2/electrical/generator_on")->vi[0] = 1;
    raw("sim/cockpit2/electrical/generator_volts")->vf[0] = 28;
    raw("sim/cockpit2/electrical/generator_amps")->vf[0] = 50;
    adirs_update(0); assert(!published("on_batt"));
    raw("sim/cockpit2/electrical/bus_volts")->vf[0] = 0; adirs_update(0);
    assert(!refs.side[0].air_valid && !refs.side[0].attitude_valid && !published("on_batt"));
    raw("sim/cockpit2/electrical/bus_volts")->vf[0] = 28; adirs_update(0);
    assert(refs.side[0].air_valid && !refs.side[0].attitude_valid && published("on_batt"));
    command("ir1_toggle", xplm_CommandBegin); adirs_update(.1f); assert(adirs_sample[0].ir_press > 0);
    plugin_enabled = 0; once("ir1_mode_up"); inject("adr1_failure", 1);
    assert(published("ir1_mode") == 1 && !adirs_sample[0].adr_failure);
    command("ir1_toggle", xplm_CommandEnd); assert(!adirs.unit[0].ir_pressed);
    adirs_release_controls(); assert(adirs_sample[0].ir_press == 0);
    plugin_enabled = 1; adirs_shutdown();
    int saved_commands = command_count;
    for (int n = 0; n < command_count; ++n) assert(!mock_commands[n].callback);
    for (int n = 0; n < ARRAY_LEN(adirs_refs); ++n) assert(!XPLMFindDataRef(adirs_refs[n].name));
    assert(adirs_init() && command_count == saved_commands); adirs_shutdown();
    fail_registration_after = 5; assert(!adirs_init()); fail_registration_after = -1;
    for (int n = 0; n < ARRAY_LEN(adirs_refs); ++n) assert(!XPLMFindDataRef(adirs_refs[n].name));
    missing_raw = "sim/flightmodel/position/latitude"; assert(!adirs_init()); missing_raw = NULL;
    assert(adirs_init()); adirs_shutdown();
}
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--contracts")) {
        cold_inputs(); assert(adirs_init());
        for (int n = 0; n < command_count; ++n) printf("C %s\n", mock_commands[n].name);
        for (int n = 0; n < ref_count; ++n) if (mock_refs[n].registered) {
            if (!strncmp(mock_refs[n].name, "a380x/", 6)) printf("D %s\n", mock_refs[n].name);
            else if (!strncmp(mock_refs[n].name, "sim/", 4)) printf("R %s\n", mock_refs[n].name);
        }
        adirs_shutdown(); return 0;
    }
    test_controls_sources_and_failures();
    test_att_lock_and_hot_reset();
    test_power_timing_disabled_and_reload();
    puts("ADIRS adapter: registered controls, cold/hot lifecycle, side routing, failures, power and disabled cleanup passed");
    return 0;
}
