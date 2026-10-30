/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production FLAPS adapter (a380x_flaps.inc) against a mock XPLM: registry contract, first sync from
 * X-Plane's handle, the lift-detent cockpit drag (pull-out, gate, spring), input reporting, the SFCC
 * configuration written into X-Plane's flap handle, the two plugin commands and the intercepted stock
 * flap commands, hardware-axis adoption, flight logic (CONF 1, relief, automatic retraction), FPPU surface
 * animation with and without hydraulic pressure, pause, replay, disable, a legacy ACF table, the display
 * feed and shutdown. No simulator is used.
 *
 *   ./test_flaps_adapter              run the tests
 *   ./test_flaps_adapter --contracts  C commands, D datarefs, R raw datarefs, X intercepted stock commands
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"
#define ARRAY_LEN(a) ((int)(sizeof(a)/sizeof((a)[0])))

typedef struct {
    char name[160];
    int registered, type, writable;
    double value;
    float array_f[16];
    XPLMGetDatai_f get_i; XPLMSetDatai_f set_i;
    XPLMGetDataf_f get_f; XPLMSetDataf_f set_f;
    XPLMGetDatad_f get_d; XPLMSetDatad_f set_d;
    void *read_refcon, *write_refcon;
} MockRef;
typedef struct {
    char name[160];
    XPLMCommandCallback_f callback[4];
    void *refcon[4];
    int handlers, stock, defaults_run;
} MockCommand;
static MockRef mock_refs[128];
static MockCommand mock_commands[32];
static int ref_count, command_count, handle_writes;

static MockRef *new_ref(const char *name, int type)
{
    MockRef *r;
    assert(ref_count < ARRAY_LEN(mock_refs));
    r = &mock_refs[ref_count++];
    memset(r, 0, sizeof(*r));
    snprintf(r->name, sizeof(r->name), "%s", name);
    r->registered = 1;
    r->type = type;
    return r;
}
static int sim_type(const char *name)
{
    if (strstr(name, "acf_flap_dn")) return xplmType_FloatArray;
    if (strstr(name, "pressure_altitude")) return xplmType_Double;
    if (strstr(name, "paused") || strstr(name, "replay") || strstr(name, "onground") || strstr(name, "detents"))
        return xplmType_Int;
    return xplmType_Float;
}
XPLMDataRef XPLMFindDataRef(const char *name)
{
    int i;
    for (i = 0; i < ref_count; ++i)
        if (mock_refs[i].registered && !strcmp(mock_refs[i].name, name)) return &mock_refs[i];
    return !strncmp(name, "sim/", 4) ? new_ref(name, sim_type(name)) : NULL;
}
static MockRef *ref(const char *name) { MockRef *r = XPLMFindDataRef(name); assert(r); return r; }
XPLMDataTypeID XPLMGetDataRefTypes(XPLMDataRef handle) { return ((MockRef *)handle)->type; }
int XPLMGetDatai(XPLMDataRef handle)
{
    MockRef *r = handle;
    assert(r && r->registered);
    return r->get_i ? r->get_i(r->read_refcon) : (int)r->value;
}
float XPLMGetDataf(XPLMDataRef handle)
{
    MockRef *r = handle;
    assert(r && r->registered);
    return r->get_f ? r->get_f(r->read_refcon) : (float)r->value;
}
double XPLMGetDatad(XPLMDataRef handle)
{
    MockRef *r = handle;
    assert(r && r->registered);
    return r->get_d ? r->get_d(r->read_refcon) : r->value;
}
void XPLMSetDataf(XPLMDataRef handle, float value)
{
    MockRef *r = handle;
    assert(r && r->registered);
    if (r->set_f) { r->set_f(r->write_refcon, value); return; }
    assert(!r->get_f);
    if (strstr(r->name, "flap_handle_request_ratio")) ++handle_writes;
    r->value = value;
}
int XPLMGetDatavf(XPLMDataRef handle, float *values, int offset, int count)
{
    MockRef *r = handle;
    assert(r && r->registered && r->type == xplmType_FloatArray && offset >= 0 && offset + count <= 16);
    memcpy(values, r->array_f + offset, (size_t)count * sizeof(*values));
    return count;
}
XPLMDataRef XPLMRegisterDataAccessor(const char *name, XPLMDataTypeID type, int writable,
    XPLMGetDatai_f read_i, XPLMSetDatai_f write_i, XPLMGetDataf_f read_f, XPLMSetDataf_f write_f,
    XPLMGetDatad_f read_d, XPLMSetDatad_f write_d, XPLMGetDatavi_f read_vi, XPLMSetDatavi_f write_vi,
    XPLMGetDatavf_f read_vf, XPLMSetDatavf_f write_vf, XPLMGetDatab_f read_b, XPLMSetDatab_f write_b,
    void *read_refcon, void *write_refcon)
{
    MockRef *r;
    assert(!read_vi && !write_vi && !read_vf && !write_vf && !read_b && !write_b);
    assert(type == xplmType_Int ? (read_i && !read_f && !read_d)
                                : (type == (xplmType_Float | xplmType_Double) && !read_i && read_f && read_d));
    assert(writable ? (write_f && write_d && !write_i) : (!write_i && !write_f && !write_d && !write_refcon));
    assert(!XPLMFindDataRef(name));
    r = new_ref(name, type);
    r->writable = writable;
    r->get_i = read_i; r->set_i = write_i; r->get_f = read_f; r->set_f = write_f;
    r->get_d = read_d; r->set_d = write_d;
    r->read_refcon = read_refcon; r->write_refcon = write_refcon;
    return r;
}
void XPLMUnregisterDataAccessor(XPLMDataRef handle)
{
    MockRef *r = handle;
    assert(r && r->registered);
    r->registered = 0;
}
XPLMCommandRef XPLMFindCommand(const char *name)
{
    int i;
    for (i = 0; i < command_count; ++i) if (!strcmp(mock_commands[i].name, name)) return &mock_commands[i];
    return NULL;
}
XPLMCommandRef XPLMCreateCommand(const char *name, const char *description)
{
    MockCommand *c = XPLMFindCommand(name);
    assert(description && *description);
    if (c) return c;
    assert(command_count < ARRAY_LEN(mock_commands));
    c = &mock_commands[command_count++];
    memset(c, 0, sizeof(*c));
    snprintf(c->name, sizeof(c->name), "%s", name);
    return c;
}
void XPLMRegisterCommandHandler(XPLMCommandRef handle, XPLMCommandCallback_f callback, int before, void *refcon)
{
    MockCommand *c = handle;
    assert(c && before && callback && c->handlers < 4);
    c->callback[c->handlers] = callback;
    c->refcon[c->handlers++] = refcon;
}
void XPLMUnregisterCommandHandler(XPLMCommandRef handle, XPLMCommandCallback_f callback, int before, void *refcon)
{
    MockCommand *c = handle;
    int i;
    assert(c && before);
    for (i = 0; i < c->handlers; ++i) if (c->callback[i] == callback && c->refcon[i] == refcon) {
        memmove(c->callback + i, c->callback + i + 1, sizeof(c->callback[0]) * (size_t)(c->handlers - i - 1));
        memmove(c->refcon + i, c->refcon + i + 1, sizeof(c->refcon[0]) * (size_t)(c->handlers - i - 1));
        --c->handlers;
        return;
    }
    assert(!"handler not registered");
}
/* X-Plane's own behaviour of its flap commands runs when no handler consumed the command. */
static void xplane_default(MockCommand *c)
{
    MockRef *h = ref("sim/cockpit2/controls/flap_handle_request_ratio");
    ++c->defaults_run;
    if (!strcmp(c->name, "sim/flight_controls/flaps_down")) h->value = fmin(1.0, h->value + 1.0 / 6.0);
    else if (!strcmp(c->name, "sim/flight_controls/flaps_up")) h->value = fmax(0.0, h->value - 1.0 / 6.0);
}
static void phase(MockCommand *c, XPLMCommandPhase p)
{
    int i;
    for (i = 0; i < c->handlers; ++i) if (!c->callback[i](c, p, c->refcon[i])) return;
    if (c->stock && p == xplm_CommandBegin) xplane_default(c);
}
void XPLMCommandBegin(XPLMCommandRef c) { phase(c, xplm_CommandBegin); }
void XPLMCommandEnd(XPLMCommandRef c) { phase(c, xplm_CommandEnd); }
void XPLMCommandOnce(XPLMCommandRef c) { phase(c, xplm_CommandBegin); phase(c, xplm_CommandEnd); }

/* Unity-build environment of a380x_avionics.c: plugin_enabled, and flaps_feed (a380x_pfd_inputs.inc). */
#include "a380x_flaps.h"
static int plugin_enabled = 1;
static A380XFlapsFeed flaps_feed;
#include "a380x_flaps.inc"

#define F(name) "a380x/xp12/flaps/" name
static double pub(const char *name) { return XPLMGetDatad(ref(name)); }
static int pub_i(const char *name) { return XPLMGetDatai(ref(name)); }
static void write_input(const char *name, double value) { MockRef *r = ref(name); r->set_d(r->write_refcon, value); }
static MockRef *xp_handle(void) { return ref("sim/cockpit2/controls/flap_handle_request_ratio"); }
static void frame(float dt) { flaps_update(dt); }
static void run(float seconds) { int i, n = (int)lroundf(seconds / 0.05f); for (i = 0; i < n; ++i) frame(0.05f); }
static void command(const char *name) { XPLMCommandOnce(XPLMFindCommand(name)); frame(0.02f); }
static int near(double a, double b) { return fabs(a - b) < 1e-5; }
/* One cockpit action: pull out, drag the hand in steps (one frame each), release (drag timeout). */
static void action(double to)
{
    double h = pub(F("lever_input")), s = to >= h ? 0.1 : -0.1;
    write_input(F("lift_input"), 1.0);
    for (; s > 0 ? h <= to + 1e-9 : h >= to - 1e-9; h += s) { write_input(F("lever_input"), h); frame(0.02f); }
    write_input(F("lever_input"), to);
    run(0.8f);   /* released: 0.4 s drag timeout, then the sleeve returns (0.2 s) */
}
static void set_a380_acf(void)
{
    static const float dn[10] = {0, 0, 8, 17, 17, 26, 33, 0, 0, 0};
    MockRef *r = ref("sim/aircraft/controls/acf_flap_dn");
    memcpy(r->array_f, dn, sizeof(dn));
    ref("sim/aircraft/controls/acf_flap_detents")->value = 6;
}

static void setup_sim(void)
{
    int i;
    set_a380_acf();
    ref("sim/time/sim_speed_actual")->value = 1.0;
    ref("sim/flightmodel/failures/onground_any")->value = 1;
    ref("sim/cockpit2/hydraulics/indicators/hydraulic_pressure_1")->value = 3000.0;
    ref("sim/cockpit2/hydraulics/indicators/hydraulic_pressure_2")->value = 3000.0;
    ref("sim/flightmodel/position/alpha")->value = 3.0;
    for (i = 0; i < FLP_STOCK_COUNT; ++i) ((MockCommand *)XPLMCreateCommand(flp_stock_names[i], "X-Plane"))->stock = 1;
}

static void test_registry(void)
{
    int i;
    for (i = 0; i < FLP_IN_COUNT; ++i)
        assert(ref(flp_input_names[i])->type == (xplmType_Float | xplmType_Double) && ref(flp_input_names[i])->writable);
    for (i = 0; i < FLP_FIRST_REAL; ++i) assert(ref(flp_ref_names[i])->type == xplmType_Int && !ref(flp_ref_names[i])->writable);
    for (i = FLP_FIRST_REAL; i < FLP_R_COUNT; ++i)
        assert(ref(flp_ref_names[i])->type == (xplmType_Float | xplmType_Double) && !ref(flp_ref_names[i])->writable);
    for (i = 0; i < FLP_C_COUNT; ++i) assert(((MockCommand *)XPLMFindCommand(flp_command_names[i]))->handlers == 1);
    for (i = 0; i < FLP_STOCK_COUNT; ++i) assert(((MockCommand *)XPLMFindCommand(flp_stock_names[i]))->handlers == 1);
    assert(flp.ready && !flaps_feed.ready);
}

static void test_first_sync_and_cockpit_drag(void)
{
    frame(0.05f);                                     /* first sync: handle 0 -> lever 0, CONF 0 */
    assert(flaps_feed.ready && pub_i(F("xp_layout_ok")) == 1 && pub_i(F("handle_index")) == 0);
    assert(pub_i(F("conf_index")) == A380X_CONF0 && handle_writes == 0 && pub_i(F("lever_latched")) == 1);
    /* Not pulled out: the lever stays in its detent, the input reports the hand while dragging. */
    write_input(F("lever_input"), 0.8); frame(0.02f);
    assert(pub(F("lever_anim")) == 0.0 && near(pub(F("lever_input")), 0.8) && pub(F("lift_input")) == 0.0);
    run(0.5f);
    assert(pub(F("lever_input")) == 0.0);            /* drag over: the input shows the lever again */
    /* Pulled out, dragged to FULL: the gate at 1 stops it; released it sits in 1. */
    write_input(F("lift_input"), 1.0); frame(0.05f);
    assert(pub(F("lift_input")) == 1.0 && pub_i(F("lever_latched")) == 0);
    run(0.2f);
    assert(near(pub(F("lock_anim")), 1.0));           /* sleeve pulled */
    action(4.0);
    assert(pub(F("lever_anim")) == 1.0 && pub_i(F("lever_latched")) == 1 && pub_i(F("handle_index")) == 1);
    assert(pub_i(F("conf_index")) == A380X_CONF1F && pub_i(F("config_display")) == 2);   /* ground: 1+F */
    assert(near(xp_handle()->value, 2.0 / 6.0) && pub(F("lock_anim")) == 0.0);
    action(4.0);                                      /* 1 -> 3 (gate) */
    assert(pub(F("lever_anim")) == 3.0 && pub_i(F("conf_index")) == A380X_CONF3 && near(xp_handle()->value, 5.0 / 6.0));
    action(4.0);
    assert(pub(F("lever_anim")) == 4.0 && pub_i(F("conf_index")) == A380X_CONFFULL && near(xp_handle()->value, 1.0));
    action(0.0);                                      /* FULL -> 3 -> ... */
    assert(pub(F("lever_anim")) == 3.0);
    action(0.0);
    action(0.0);
    assert(pub(F("lever_anim")) == 0.0 && pub_i(F("conf_index")) == A380X_CONF0 && xp_handle()->value == 0.0);
}

static void test_commands(void)
{
    command(F("lever_down"));
    assert(pub_i(F("handle_index")) == 1 && near(xp_handle()->value, 2.0 / 6.0));
    command(F("lever_down"));
    assert(pub_i(F("handle_index")) == 2 && near(xp_handle()->value, 3.0 / 6.0));
    command(F("lever_up"));
    assert(pub_i(F("handle_index")) == 1);
    command("sim/flight_controls/flaps_down");        /* consumed: X-Plane's own notch never runs */
    assert(pub_i(F("handle_index")) == 2 && ((MockCommand *)XPLMFindCommand("sim/flight_controls/flaps_down"))->defaults_run == 0);
    command("sim/flight_controls/flaps_up");
    assert(pub_i(F("handle_index")) == 1);
    command("sim/flight_controls/flaps_down_full");   /* hardware-style: no gates */
    assert(pub_i(F("handle_index")) == 4 && near(xp_handle()->value, 1.0));
    command("sim/flight_controls/flaps_up_full");
    assert(pub_i(F("handle_index")) == 0 && xp_handle()->value == 0.0);
    command("sim/flight_controls/flaps_detent_3");
    assert(pub_i(F("handle_index")) == 3 && near(xp_handle()->value, 5.0 / 6.0));
    command("sim/flight_controls/flaps_detent_8");
    assert(pub_i(F("handle_index")) == 4);
    command("sim/flight_controls/flaps_up_full");
    assert(flp.adoptions == 0);
}

static void test_hardware_adoption(void)
{
    int writes;
    xp_handle()->value = 0.76;                         /* hardware flap axis: lever 3 */
    frame(0.05f);
    assert(pub_i(F("handle_index")) == 3 && flp.adoptions == 1 && near(xp_handle()->value, 5.0 / 6.0));
    writes = handle_writes;
    run(0.5f);
    assert(flp.adoptions == 1 && handle_writes == writes);   /* no fight, no further writes */
    xp_handle()->value = 0.27;                         /* lever 1 (1.08) */
    frame(0.05f);
    assert(pub_i(F("handle_index")) == 1 && pub_i(F("conf_index")) == A380X_CONF1F && near(xp_handle()->value, 2.0 / 6.0));
    xp_handle()->value = 0.0;                          /* flyPad preset "flaps up" (writes flap_ratio = 0) */
    frame(0.05f);
    assert(pub_i(F("handle_index")) == 0 && flp.adoptions == 3 && xp_handle()->value == 0.0);
}

static void test_flight_logic_and_surfaces(void)
{
    MockRef *ias = ref("sim/cockpit2/gauges/indicators/airspeed_kts_pilot");
    run(60.0f);                                        /* surfaces settled clean */
    assert(pub(F("flap_anim_ratio")) == 0.0 && pub(F("slat_anim_ratio")) == 0.0);
    ref("sim/flightmodel/failures/onground_any")->value = 0;
    ias->value = 230.0;
    command(F("lever_down"));                          /* 0 -> 1 above 205 kt: CONF 1, slats only */
    assert(pub_i(F("conf_index")) == A380X_CONF1 && near(xp_handle()->value, 1.0 / 6.0) && pub_i(F("config_display")) == 1);
    run(40.0f);
    assert(near(pub(F("slat_fppu_deg")), 286.48) && pub(F("flap_fppu_deg")) == 0.0 && near(pub(F("slat_angle_deg")), 20.0));
    assert(near(pub(F("slat_anim_ratio")), 286.48 / 327.39) && pub(F("aileron_droop_deg")) == 0.0);
    assert(pub(F("vfe_knots")) == 263.0 && pub(F("vfe_next_knots")) == 222.0);
    command(F("lever_down"));                          /* 2 at 230 kt: relief keeps 1+F */
    assert(pub_i(F("conf_index")) == A380X_CONF1F && pub_i(F("flap_relief")) == 1 && near(xp_handle()->value, 2.0 / 6.0));
    run(30.0f);
    assert(near(pub(F("flap_angle_deg")), 8.0) && near(pub(F("flap_anim_ratio")), 215.68 / 338.99));
    assert(near(pub(F("aileron_droop_deg")), 5.0) && pub(F("vfe_knots")) == 222.0);
    ias->value = 216.0;                               /* below 217.5: CONF 2 */
    frame(0.05f);
    assert(pub_i(F("conf_index")) == A380X_CONF2 && !pub_i(F("flap_relief")) && near(xp_handle()->value, 3.0 / 6.0));
    command(F("lever_up"));                            /* back to 1 at 216 kt: 1 (above 212) */
    assert(pub_i(F("conf_index")) == A380X_CONF1);
    ias->value = 200.0; frame(0.05f);                  /* below 205: re-extends 1+F */
    assert(pub_i(F("conf_index")) == A380X_CONF1F);
    ias->value = 213.0; frame(0.05f);                  /* automatic retraction above 212 */
    assert(pub_i(F("conf_index")) == A380X_CONF1 && pub_i(F("config_display")) == 1);
    /* No hydraulic pressure: the SFCC commands, the surfaces stay. */
    ref("sim/cockpit2/hydraulics/indicators/hydraulic_pressure_1")->value = 0.0;
    ref("sim/cockpit2/hydraulics/indicators/hydraulic_pressure_2")->value = 0.0;
    ias->value = 150.0;
    run(2.0f);                                         /* the 0.3 s speed filter lets the drive coast briefly */
    command(F("lever_down"));
    command(F("lever_down"));
    {
        double before = pub(F("flap_fppu_deg"));
        run(20.0f);
        assert(fabs(pub(F("flap_fppu_deg")) - before) < 0.01 && flp.core.flaps.max_speed == 0.0 && pub_i(F("conf_index")) == A380X_CONF3);
        assert(near(xp_handle()->value, 5.0 / 6.0));
    }
    ref("sim/cockpit2/hydraulics/indicators/hydraulic_pressure_1")->value = 3000.0;   /* green only: half speed */
    run(1.0f);
    assert(pub(F("flap_fppu_deg")) > 0.0);
    ref("sim/cockpit2/hydraulics/indicators/hydraulic_pressure_2")->value = 3000.0;
    /* Paused: the surfaces hold, the lever still moves. */
    ref("sim/time/paused")->value = 1;
    {
        double before = pub(F("flap_fppu_deg"));
        command(F("lever_up"));
        run(2.0f);
        assert(pub(F("flap_fppu_deg")) == before && pub_i(F("handle_index")) == 2);
    }
    ref("sim/time/paused")->value = 0;
    run(60.0f);
    assert(near(pub(F("flap_fppu_deg")), 259.28) && pub_i(F("conf_index")) == A380X_CONF2);
    assert(flaps_feed.handle_index == 2 && flaps_feed.conf == A380X_CONF2 && flaps_feed.display_config == 3);
    assert(near(flaps_feed.flap_deg, 17.0) && near(flaps_feed.slat_deg, 20.0) && flaps_feed.flap_target == -1);
    assert((flaps_feed.status_word & (1u << 18)) && (flaps_feed.status_word & (1u << 27)));
    ref("sim/flightmodel/failures/onground_any")->value = 1;
    ias->value = 0.0;
}

static void test_replay_disable_and_reload(void)
{
    int writes = handle_writes, defaults;
    ref("sim/time/is_in_replay")->value = 1;
    xp_handle()->value = 1.0;                          /* X-Plane replays a FULL handle */
    write_input(F("lift_input"), 1.0); write_input(F("lever_input"), 3.0);
    command(F("lever_down"));
    run(1.0f);
    assert(handle_writes == writes && pub_i(F("handle_index")) == 2);   /* no writes, inputs ignored */
    ref("sim/time/is_in_replay")->value = 0;
    frame(0.05f);                                      /* re-synchronised from X-Plane, not adopted */
    assert(pub_i(F("handle_index")) == 4 && pub_i(F("conf_index")) == A380X_CONFFULL && flp.adoptions == 3);
    /* Disabled: inputs ignored, X-Plane's own flap commands run; on enable re-sync from X-Plane. */
    plugin_enabled = 0;
    flaps_release_controls();
    write_input(F("lift_input"), 1.0);
    defaults = ((MockCommand *)XPLMFindCommand("sim/flight_controls/flaps_up"))->defaults_run;
    XPLMCommandOnce(XPLMFindCommand("sim/flight_controls/flaps_up"));
    assert(((MockCommand *)XPLMFindCommand("sim/flight_controls/flaps_up"))->defaults_run == defaults + 1);
    assert(pub_i(F("lever_latched")) == 1 && near(xp_handle()->value, 5.0 / 6.0));
    plugin_enabled = 1;
    frame(0.05f);
    assert(pub_i(F("conf_index")) == A380X_CONF3 && pub_i(F("handle_index")) == 3 && flp.adoptions == 3);
    /* Aircraft reload: lever and surfaces re-read from X-Plane's handle. */
    xp_handle()->value = 0.0;
    flaps_plane_loaded();
    frame(0.05f);
    assert(pub_i(F("handle_index")) == 0 && pub(F("flap_fppu_deg")) == 0.0 && flp.adoptions == 0);
}

static void test_legacy_acf(void)
{
    static const float legacy[10] = {0, 1, 5, 10, 20, 25, 40, 0, 0, 0};
    memcpy(ref("sim/aircraft/controls/acf_flap_dn")->array_f, legacy, sizeof(legacy));
    command(F("lever_down"));                          /* ground 1+F = 8 deg in the old table */
    assert(pub_i(F("xp_layout_ok")) == 0 && near(xp_handle()->value, 2.6 / 6.0));
    command(F("lever_up"));
    assert(xp_handle()->value == 0.0);
    set_a380_acf();
    frame(0.05f);
    assert(pub_i(F("xp_layout_ok")) == 1);
}

static void test_shutdown(void)
{
    int i;
    flaps_shutdown();
    assert(!flaps_feed.ready && !flp.ready);
    for (i = 0; i < FLP_IN_COUNT; ++i) assert(!XPLMFindDataRef(flp_input_names[i]));
    for (i = 0; i < FLP_R_COUNT; ++i) assert(!XPLMFindDataRef(flp_ref_names[i]));
    for (i = 0; i < FLP_C_COUNT; ++i) assert(((MockCommand *)XPLMFindCommand(flp_command_names[i]))->handlers == 0);
    for (i = 0; i < FLP_STOCK_COUNT; ++i) assert(((MockCommand *)XPLMFindCommand(flp_stock_names[i]))->handlers == 0);
    XPLMCommandOnce(XPLMFindCommand("sim/flight_controls/flaps_down"));   /* X-Plane's own again */
    assert(near(xp_handle()->value, 1.0 / 6.0));
    assert(flaps_init());                              /* reload: persistent commands reused */
    frame(0.05f);
    /* CONF 1 adopted from X-Plane's handle, then the SFCC selects 1+F on the ground. */
    assert(pub_i(F("handle_index")) == 1 && pub_i(F("conf_index")) == A380X_CONF1F && near(xp_handle()->value, 2.0 / 6.0));
    flaps_shutdown();
}

int main(int argc, char **argv)
{
    int i;
    setup_sim();
    assert(flaps_init());
    if (argc > 1 && !strcmp(argv[1], "--contracts")) {
        for (i = 0; i < FLP_C_COUNT; ++i) printf("C %s\n", flp_command_names[i]);
        for (i = 0; i < FLP_IN_COUNT; ++i) printf("D %s\n", flp_input_names[i]);
        for (i = 0; i < FLP_R_COUNT; ++i) printf("D %s\n", flp_ref_names[i]);
        for (i = 0; i < FLP_RAW_COUNT; ++i) printf("R %s\n", flp_raw_names[i]);
        for (i = 0; i < FLP_STOCK_COUNT; ++i) printf("X %s\n", flp_stock_names[i]);
        flaps_shutdown();
        return 0;
    }
    test_registry();
    test_first_sync_and_cockpit_drag();
    test_commands();
    test_hardware_adoption();
    test_flight_logic_and_surfaces();
    test_replay_disable_and_reload();
    test_legacy_acf();
    test_shutdown();
    puts("Flaps adapter: registry, first sync, lift-detent drag with gates, commands and intercepted stock "
         "commands, hardware adoption, SFCC into X-Plane's handle, FPPU surfaces and hydraulics, pause, replay, "
         "disable, reload, legacy ACF and shutdown passed");
    return 0;
}
