/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production SPEED BRAKE adapter (a380x_speedbrake.inc + a380x_cockpit_lever.inc) against a mock XPLM:
 * registry contract, first sync from X-Plane's handle, the cockpit drag (push down, 1/2 notch, ARM at RET),
 * input reporting, the FBW surface order written into X-Plane's speedbrake handle (never -0.5) and X-Plane's
 * auto speedbrake switched off and restored, the plugin commands and the six intercepted stock commands,
 * hardware-axis adoption without fighting, thrust-module and fallback TLA, gear classification and wheel
 * speeds, landing / reverse ground spoilers, A.FLOOR inhibition, flap gains and spoiler lift from the FLAPS
 * feed, clicks, pause, replay, disable, reload and shutdown. No simulator is used.
 *
 *   ./test_speedbrake_adapter              run the tests
 *   ./test_speedbrake_adapter --contracts  C commands, D datarefs, R raw datarefs, X intercepted stock commands
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "XPLMDataAccess.h"
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"
#include "XPLMSound.h"
#define ARRAY_LEN(a) ((int)(sizeof(a)/sizeof((a)[0])))

typedef struct {
    char name[160];
    int registered, type, writable;
    double value;
    float array_f[16];
    int array_i[16];
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
static MockRef mock_refs[160];
static MockCommand mock_commands[32];
static int ref_count, command_count, handle_writes, plays;

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
    if (strstr(name, "prop_mode")) return xplmType_IntArray;
    if (strstr(name, "tire_") || strstr(name, "gear_xnodef") || strstr(name, "throttle_ratio")) return xplmType_FloatArray;
    if (strstr(name, "paused") || strstr(name, "replay") || strstr(name, "onground") || strstr(name, "autosbrk")
        || strstr(name, "view_is_external"))
        return xplmType_Int;
    if (strstr(name, "local_x") || strstr(name, "local_y") || strstr(name, "local_z")) return xplmType_Double;
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
void XPLMSetDatai(XPLMDataRef handle, int value)
{
    MockRef *r = handle;
    assert(r && r->registered && !r->get_i && r->type == xplmType_Int);
    r->value = value;
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
    if (!strcmp(r->name, "sim/cockpit2/controls/speedbrake_ratio")) ++handle_writes;
    r->value = value;
}
int XPLMGetDatavf(XPLMDataRef handle, float *values, int offset, int count)
{
    MockRef *r = handle;
    assert(r && r->registered && r->type == xplmType_FloatArray && offset >= 0 && offset + count <= 16);
    memcpy(values, r->array_f + offset, (size_t)count * sizeof(*values));
    return count;
}
int XPLMGetDatavi(XPLMDataRef handle, int *values, int offset, int count)
{
    MockRef *r = handle;
    assert(r && r->registered && r->type == xplmType_IntArray && offset >= 0 && offset + count <= 16);
    memcpy(values, r->array_i + offset, (size_t)count * sizeof(*values));
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
/* X-Plane's own behaviour of its speed brake commands, run when no handler consumed the command. */
static void xplane_default(MockCommand *c)
{
    MockRef *h = ref("sim/cockpit2/controls/speedbrake_ratio");
    ++c->defaults_run;
    if (!strcmp(c->name, "sim/flight_controls/speed_brakes_down_one")) h->value = fmin(1.0, h->value + 0.25);
    else if (!strcmp(c->name, "sim/flight_controls/speed_brakes_arm_toggle")) h->value = h->value < 0 ? 0.0 : -0.5;
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
XPLMPluginID XPLMGetMyID(void) { return 7; }
void XPLMGetPluginInfo(XPLMPluginID id, char *name, char *path, char *signature, char *description)
{
    (void)id; (void)name; (void)signature; (void)description;
    if (path) snprintf(path, 256, "%s", SPEEDBRAKE_TEST_ROOT "/mac_x64/A380XAvionics.xpl");
}
FMOD_CHANNEL *XPLMPlayPCMOnBus(void *data, uint32_t size, FMOD_SOUND_FORMAT format, int rate, int channels,
                               int loop, XPLMAudioBus bus, XPLMPCMComplete_f callback, void *refcon)
{
    static int channel;
    assert(data && size > 1000 && format == FMOD_SOUND_FORMAT_PCM16 && rate == 44100 && channels == 1);
    assert(!loop && bus == xplm_AudioInterior && !callback && !refcon);
    ++plays;
    return (FMOD_CHANNEL *)&channel;
}
FMOD_RESULT XPLMSetAudioPosition(FMOD_CHANNEL *c, FMOD_VECTOR *position, FMOD_VECTOR *velocity)
{
    (void)c; (void)position; (void)velocity; return FMOD_OK;
}
FMOD_RESULT XPLMSetAudioFadeDistance(FMOD_CHANNEL *c, float min, float max) { (void)c; (void)min; (void)max; return FMOD_OK; }
FMOD_RESULT XPLMSetAudioVolume(FMOD_CHANNEL *c, float volume) { (void)c; (void)volume; return FMOD_OK; }

/* Unity-build environment of a380x_avionics.c: plugin_enabled, flaps_feed and speedbrake_feed
 * (a380x_pfd_inputs.inc) and the A.FLOOR latch of the guidance. */
#include "a380x_flaps.h"
#include "a380x_speedbrake.h"
static int plugin_enabled = 1;
static A380XFlapsFeed flaps_feed;
static A380XSpeedbrakeFeed speedbrake_feed;
static int alpha_floor_latched;
#define SPB_ALPHA_PROTECTION() (alpha_floor_latched != 0)
#include "a380x_speedbrake.inc"

#define S(name) "a380x/xp12/speedbrake/" name
static double pub(const char *name) { return XPLMGetDatad(ref(name)); }
static int pub_i(const char *name) { return XPLMGetDatai(ref(name)); }
static void write_input(double value) { MockRef *r = ref(S("lever_input")); r->set_d(r->write_refcon, value); }
static MockRef *xp_handle(void) { return ref("sim/cockpit2/controls/speedbrake_ratio"); }
static void frame(float dt) { speedbrake_update(dt); }
static void run(float seconds) { int i, n = (int)lroundf(seconds / 0.05f); for (i = 0; i < n; ++i) frame(0.05f); }
static void command(const char *name) { XPLMCommandOnce(XPLMFindCommand(name)); frame(0.02f); }
static int near(double a, double b) { return fabs(a - b) < 1e-5; }
static MockRef *thrust_tla[4];
static void set_tla(double a, double b, double c, double d)
{
    double v[4] = {a, b, c, d};
    int i;
    for (i = 0; i < 4; ++i) if (thrust_tla[i]->registered) thrust_tla[i]->value = v[i];
}
/* Main-gear wheel speed in kt through X-Plane's tire rotation x radius (0.718 m, gears 1..4). */
static void set_wheels(double kt)
{
    int i;
    for (i = 1; i <= 4; ++i) ref("sim/flightmodel2/gear/tire_rotation_speed_rad_sec")->array_f[i] = (float)(kt / 1.9438444924406046 / 0.718);
}
/* One cockpit drag from where the lever is to `to`, then released. */
static void drag(double to)
{
    double h = pub(S("lever_input")), s = to >= h ? 0.05 : -0.05;
    for (; s > 0 ? h <= to + 1e-9 : h >= to - 1e-9; h += s) { write_input(h); frame(0.02f); }
    write_input(to);
    frame(0.02f);
    run(0.5f);
}

static void setup_sim(void)
{
    static const float gear_x[10] = {0.0f, -6.17f, 6.17f, -2.63f, 2.63f};
    int i;
    ref("sim/time/sim_speed_actual")->value = 1.0;
    ref("sim/flightmodel/failures/onground_any")->value = 1;
    ref("sim/aircraft/specialcontrols/acf_autosbrkEQ")->value = 1;
    memcpy(ref("sim/aircraft/parts/acf_gear_xnodef")->array_f, gear_x, sizeof(gear_x));
    for (i = 0; i < 5; ++i) ref("sim/flightmodel2/gear/tire_radius_mtrs")->array_f[i] = 0.718f;
    for (i = 0; i < 4; ++i) {
        char name[64];
        snprintf(name, sizeof(name), "a380x/xp12/thrust/tla_%d", i + 1);
        thrust_tla[i] = new_ref(name, xplmType_Float | xplmType_Double);
    }
    for (i = 0; i < SPB_STOCK_COUNT; ++i) ((MockCommand *)XPLMCreateCommand(spb_stock_names[i], "X-Plane"))->stock = 1;
}

static void test_registry(void)
{
    int i;
    assert(ref(SPB_INPUT_NAME)->type == (xplmType_Float | xplmType_Double) && ref(SPB_INPUT_NAME)->writable);
    for (i = 0; i < SPB_FIRST_REAL; ++i) assert(ref(spb_ref_names[i])->type == xplmType_Int && !ref(spb_ref_names[i])->writable);
    for (i = SPB_FIRST_REAL; i < SPB_R_COUNT; ++i)
        assert(ref(spb_ref_names[i])->type == (xplmType_Float | xplmType_Double) && !ref(spb_ref_names[i])->writable);
    for (i = 0; i < SPB_C_COUNT; ++i) assert(((MockCommand *)XPLMFindCommand(spb_command_names[i]))->handlers == 1);
    for (i = 0; i < SPB_STOCK_COUNT; ++i) assert(((MockCommand *)XPLMFindCommand(spb_stock_names[i]))->handlers == 1);
    assert(spb.ready && !spb.synced && !speedbrake_feed.ready && spb.click.data);
}

static void test_first_sync_and_cockpit_drag(void)
{
    frame(0.05f);   /* first sync: handle 0 -> RET, nothing written, X-Plane's auto speedbrake off */
    assert(speedbrake_feed.ready && pub_i(S("lever_position")) == A380X_SB_POS_RET && handle_writes == 0);
    assert(ref("sim/aircraft/specialcontrols/acf_autosbrkEQ")->value == 0 && spb.auto_saved && spb.auto_original == 1);
    assert(spb.wheel[A380X_SB_WHEEL_LEFT_BODY] == 3 && spb.wheel[A380X_SB_WHEEL_LEFT_WING] == 1);
    assert(spb.wheel[A380X_SB_WHEEL_RIGHT_BODY] == 4 && spb.wheel[A380X_SB_WHEEL_RIGHT_WING] == 2);
    /* Grabbed: pushed down while held, the input reports the hand. */
    write_input(0.0); frame(0.2f);
    assert(pub_i(S("lever_held")) == 1 && pub(S("lift_anim")) == -1.0);
    write_input(0.3); frame(0.02f);
    assert(near(pub(S("lever_input")), 0.3) && near(pub(S("handle_position")), 0.3) && near(pub(S("lever_anim")), 0.3));
    run(0.5f);
    assert(pub_i(S("lever_held")) == 0 && pub(S("lift_anim")) == 0.0 && near(pub(S("lever_input")), 0.3));
    assert(pub_i(S("lever_position")) == A380X_SB_POS_BETWEEN && speedbrake_feed.lever_out && !speedbrake_feed.armed);
    /* On the ground, idle thrust: the speed brake order reaches X-Plane's handle on the ground scale. */
    run(5.0f);
    assert(pub(S("speed_brake_command_deg")) > 15.0 && near(xp_handle()->value, spb.xp_ratio) && xp_handle()->value > 0.0);
    assert(near(xp_handle()->value, a380x_spoilers_mean_deg(&spb.core.computer) / 50.0));
    /* Into the 1/2 notch (one click), then RET. */
    plays = 0;
    drag(0.49);
    assert(pub_i(S("lever_position")) == A380X_SB_POS_HALF && plays == 1 && spb.clicks_played >= 1);
    drag(0.0);
    assert(pub_i(S("lever_position")) == A380X_SB_POS_RET);
    run(12.0f);
    assert(xp_handle()->value == 0.0 && pub(S("speed_brake_command_deg")) == 0.0 && spb.adoptions == 0);
    /* Pulled up at RET: ARM, one click; X-Plane's handle never becomes -0.5. */
    plays = 0;
    drag(-0.2);
    assert(pub_i(S("ground_spoilers_armed")) == 1 && speedbrake_feed.armed && pub(S("lift_anim")) == 1.0);
    assert(pub_i(S("lever_position")) == A380X_SB_POS_ARM && plays == 1 && xp_handle()->value == 0.0);
    assert(pub(S("lever_input")) == 0.0 && speedbrake_feed.lever_pos == -0.05);
    /* Dragged aft: pushed down out of ARM. */
    drag(0.25);
    assert(pub_i(S("ground_spoilers_armed")) == 0 && near(pub(S("handle_position")), 0.25));
    drag(0.0);
}

static void test_commands(void)
{
    MockCommand *arm = XPLMFindCommand("sim/flight_controls/speed_brakes_arm_toggle");
    command("sim/flight_controls/speed_brakes_arm_toggle");   /* consumed: ARM, X-Plane's default not run */
    assert(pub_i(S("ground_spoilers_armed")) == 1 && arm->defaults_run == 0 && xp_handle()->value >= 0.0);
    command("sim/flight_controls/speed_brakes_down_one");      /* ARM -> RET */
    assert(pub_i(S("ground_spoilers_armed")) == 0 && pub_i(S("lever_position")) == A380X_SB_POS_RET);
    command("sim/flight_controls/speed_brakes_down_one");
    assert(pub_i(S("lever_position")) == A380X_SB_POS_HALF);
    command("sim/flight_controls/speed_brakes_down_all");
    assert(pub_i(S("lever_position")) == A380X_SB_POS_FULL);
    command("sim/flight_controls/speed_brakes_up_one");
    assert(pub_i(S("lever_position")) == A380X_SB_POS_HALF);
    command("sim/flight_controls/speed_brakes_up_all");
    assert(pub_i(S("lever_position")) == A380X_SB_POS_RET);
    command("sim/flight_controls/speed_brakes_up_one");        /* retracting from RET arms (X-Plane convention) */
    assert(pub_i(S("ground_spoilers_armed")) == 1);
    command("sim/flight_controls/speed_brakes_toggle");        /* FBW: disarm, RET -> FULL */
    assert(pub_i(S("ground_spoilers_armed")) == 0 && pub_i(S("lever_position")) == A380X_SB_POS_FULL);
    command("sim/flight_controls/speed_brakes_toggle");
    assert(pub_i(S("lever_position")) == A380X_SB_POS_RET);
    command(S("extend_one"));
    assert(pub_i(S("lever_position")) == A380X_SB_POS_HALF);
    command(S("retract_one"));
    command(S("retract_one"));
    assert(pub_i(S("ground_spoilers_armed")) == 1);
    command(S("arm_toggle"));
    assert(pub_i(S("ground_spoilers_armed")) == 0);
    command(S("arm"));
    assert(pub_i(S("ground_spoilers_armed")) == 1 && pub_i(S("lever_position")) == A380X_SB_POS_ARM);
    command(S("retract"));
    assert(pub_i(S("ground_spoilers_armed")) == 0 && pub_i(S("lever_position")) == A380X_SB_POS_RET);
    assert(spb.adoptions == 0 && arm->defaults_run == 0);
}

static void test_hardware_adoption(void)
{
    unsigned before = spb.adoptions;
    xp_handle()->value = 0.5;   /* hardware axis to half */
    frame(0.05f);
    assert(pub_i(S("lever_position")) == A380X_SB_POS_HALF && spb.adoptions == before + 1);
    assert(near(xp_handle()->value, spb.xp_ratio) && xp_handle()->value < 0.5);   /* surface order written back */
    run(1.0f);
    assert(spb.adoptions == before + 1);   /* its own writes are not adopted */
    xp_handle()->value = -0.5;   /* another writer arms the X-Plane way */
    frame(0.05f);
    assert(pub_i(S("ground_spoilers_armed")) == 1 && xp_handle()->value >= 0.0);
    run(10.0f);
    assert(xp_handle()->value == 0.0);
    xp_handle()->value = 0.0001f + (float)spb.written;   /* below the adoption threshold: noise */
    frame(0.05f);
    assert(pub_i(S("ground_spoilers_armed")) == 1);
}

static void test_landing_and_reverse(void)
{
    MockRef *ground = ref("sim/flightmodel/failures/onground_any");
    int i;
    /* Approach: armed, idle (thrust module TLA), wheels stopped, in flight. */
    command(S("arm"));
    set_tla(0, 0, 0, 0);
    set_wheels(0.0);
    ground->value = 0;
    run(1.0f);
    assert(pub_i(S("wheel_latch")) == 1 && pub_i(S("ground_spoilers_out")) == 0 && xp_handle()->value == 0.0);
    /* Touchdown, spin-up above 72 kt: full extension (50 deg at 17 deg/s), X-Plane's handle 1. */
    ground->value = 1;
    set_wheels(130.0);
    run(0.1f);
    assert(pub_i(S("ground_spoilers_out")) == 1);
    run(3.5f);
    assert(pub(S("ground_spoiler_command_deg")) == 50.0 && near(xp_handle()->value, 1.0));
    for (i = 1; i <= 8; ++i) { char n[64]; snprintf(n, sizeof(n), S("spoiler_%d_deg"), i); assert(pub(n) == 50.0); }
    assert(speedbrake_feed.ground_spoilers_out && speedbrake_feed.extension_ratio == 1.0);
    /* Disarmed by the lever (pushed down to RET): retracted. */
    command(S("retract"));
    run(4.0f);
    assert(pub_i(S("ground_spoilers_out")) == 0 && xp_handle()->value == 0.0);
    /* Thrust module absent: X-Plane's throttle; reverse on engine 2 extends them though not armed. */
    for (i = 0; i < 4; ++i) thrust_tla[i]->registered = 0;
    memset(spb.tla, 0, sizeof(spb.tla));
    ref("sim/cockpit2/engine/actuators/prop_mode")->array_i[1] = 3;
    ref("sim/cockpit2/engine/actuators/throttle_ratio")->array_f[1] = 0.5f;
    run(0.1f);
    assert(spb.in.tla[1] == -13.0 && pub_i(S("ground_spoilers_out")) == 1);
    ref("sim/cockpit2/engine/actuators/prop_mode")->array_i[1] = 1;
    ref("sim/cockpit2/engine/actuators/throttle_ratio")->array_f[1] = 0.0f;
    run(4.0f);
    assert(pub_i(S("ground_spoilers_out")) == 0 && xp_handle()->value == 0.0);
    ref("sim/cockpit2/engine/actuators/throttle_ratio")->array_f[0] = 1.0f;
    run(0.1f);
    assert(spb.in.tla[0] == 45.0 && pub_i(S("speed_brake_inhibited")) == 1);   /* TOGA inhibits the speed brake */
    ref("sim/cockpit2/engine/actuators/throttle_ratio")->array_f[0] = 0.0f;
    for (i = 0; i < 4; ++i) thrust_tla[i]->registered = 1;
    run(6.0f);
    assert(pub_i(S("speed_brake_inhibited")) == 0);
    set_wheels(0.0);
}

static void test_flight_logic(void)
{
    MockRef *ground = ref("sim/flightmodel/failures/onground_any");
    ground->value = 0;
    set_tla(20, 20, 20, 20);
    command("sim/flight_controls/speed_brakes_down_all");
    run(10.0f);
    /* In flight, clean: the mean FBW panel order over 45 deg. */
    assert(near(pub(S("speed_brake_command_deg")), 45.0));
    assert(near(xp_handle()->value, (5 * 20.0 + 3 * 45.0) / 8.0 / 45.0));
    /* A.FLOOR (the port's high-AoA protection flag): inhibited, retracting at 5 deg/s. */
    alpha_floor_latched = 1;
    run(1.0f);
    assert(pub_i(S("speed_brake_inhibited")) == 1 && near(pub(S("speed_brake_command_deg")), 40.0));
    alpha_floor_latched = 0;
    run(10.0f);
    assert(pub_i(S("speed_brake_inhibited")) == 1 && pub(S("speed_brake_command_deg")) == 0.0);   /* latched */
    command(S("retract"));
    run(5.5f);
    assert(pub_i(S("speed_brake_inhibited")) == 0);
    /* FLAPS feed: FULL gains (spoilers 1-2 6 deg, 3-5 0, 6-8 10 at full lever) after the 0.03/s fade. */
    memset(&flaps_feed, 0, sizeof(flaps_feed));
    flaps_feed.ready = 1;
    flaps_feed.display_config = 5;
    flaps_feed.actual_position_word = 1u << 19;
    command("sim/flight_controls/speed_brakes_down_all");
    run(30.0f);
    assert(near(pub(S("spoiler_1_deg")), 6.0) && near(pub(S("spoiler_4_deg")), 0.0) && near(pub(S("spoiler_7_deg")), 10.0));
    command(S("retract"));
    run(10.0f);
    /* On the ground with flaps below 1+F: spoiler lift 5 deg on every panel. */
    ground->value = 1;
    flaps_feed.display_config = 0;
    flaps_feed.actual_position_word = 0;
    run(2.0f);
    assert(pub_i(S("spoiler_lift_active")) == 1 && near(pub(S("spoiler_lift_deg")), 5.0) && near(pub(S("spoiler_8_deg")), 5.0));
    assert(near(xp_handle()->value, 0.1));
    memset(&flaps_feed, 0, sizeof(flaps_feed));
    run(2.0f);
    assert(xp_handle()->value == 0.0 && pub_i(S("phased_lift_dumping")) == 0);
}

static void test_pause_replay_disable_reload(void)
{
    MockRef *paused = ref("sim/time/paused"), *replay = ref("sim/time/is_in_replay");
    MockCommand *down = XPLMFindCommand("sim/flight_controls/speed_brakes_down_one");
    double held;
    /* Paused: the lever moves, the orders hold. */
    paused->value = 1;
    command("sim/flight_controls/speed_brakes_down_all");
    run(1.0f);
    assert(pub_i(S("lever_position")) == A380X_SB_POS_FULL && pub(S("speed_brake_command_deg")) == 0.0);
    paused->value = 0;
    run(2.0f);
    held = xp_handle()->value;
    assert(held > 0.0);
    /* Replay: no writes, cockpit input ignored; the replayed handle afterwards is not a lever action. */
    replay->value = 1;
    write_input(0.0);
    frame(0.05f);
    xp_handle()->value = 0.123;
    run(1.0f);
    assert(near(xp_handle()->value, 0.123) && pub_i(S("lever_position")) == A380X_SB_POS_FULL);
    replay->value = 0;
    frame(0.05f);
    assert(pub_i(S("lever_position")) == A380X_SB_POS_FULL && spb.adoptions == 2 && near(xp_handle()->value, spb.xp_ratio));
    /* Disabled: X-Plane's commands are its own again, its auto speedbrake restored; re-enabled, the
     * foreign handle value is adopted. */
    speedbrake_release_controls();
    plugin_enabled = 0;
    assert(ref("sim/aircraft/specialcontrols/acf_autosbrkEQ")->value == 1);
    xp_handle()->value = 0.0;
    XPLMCommandOnce(down);
    assert(down->defaults_run == 1 && xp_handle()->value == 0.25);
    plugin_enabled = 1;
    frame(0.05f);
    assert(near(pub(S("handle_position")), 0.25) && ref("sim/aircraft/specialcontrols/acf_autosbrkEQ")->value == 0);
    /* Aircraft reload: the lever is re-read from X-Plane's handle (ARMED = ARM). */
    speedbrake_plane_loaded();
    assert(!speedbrake_feed.ready && ref("sim/aircraft/specialcontrols/acf_autosbrkEQ")->value == 1);
    xp_handle()->value = -0.5;
    frame(0.05f);
    assert(pub_i(S("ground_spoilers_armed")) == 1 && xp_handle()->value == 0.0 && spb.adoptions == 0);
}

static void test_shutdown(void)
{
    int i;
    speedbrake_shutdown();
    assert(!speedbrake_feed.ready && !spb.ready && ref("sim/aircraft/specialcontrols/acf_autosbrkEQ")->value == 1);
    assert(!XPLMFindDataRef(SPB_INPUT_NAME));
    for (i = 0; i < SPB_R_COUNT; ++i) assert(!XPLMFindDataRef(spb_ref_names[i]));
    for (i = 0; i < SPB_C_COUNT; ++i) assert(((MockCommand *)XPLMFindCommand(spb_command_names[i]))->handlers == 0);
    for (i = 0; i < SPB_STOCK_COUNT; ++i) assert(((MockCommand *)XPLMFindCommand(spb_stock_names[i]))->handlers == 0);
    xp_handle()->value = 0.0;
    XPLMCommandOnce(XPLMFindCommand("sim/flight_controls/speed_brakes_arm_toggle"));   /* X-Plane's own again */
    assert(xp_handle()->value == -0.5);
    assert(speedbrake_init());   /* reload: persistent commands reused */
    frame(0.05f);
    assert(pub_i(S("ground_spoilers_armed")) == 1 && xp_handle()->value == 0.0);
    speedbrake_shutdown();
}

int main(int argc, char **argv)
{
    int i;
    setup_sim();
    assert(speedbrake_init());
    if (argc > 1 && !strcmp(argv[1], "--contracts")) {
        for (i = 0; i < SPB_C_COUNT; ++i) printf("C %s\n", spb_command_names[i]);
        printf("D %s\n", SPB_INPUT_NAME);
        for (i = 0; i < SPB_R_COUNT; ++i) printf("D %s\n", spb_ref_names[i]);
        for (i = 0; i < SPB_RAW_COUNT; ++i) printf("R %s\n", spb_raw_names[i]);
        for (i = 0; i < SPB_STOCK_COUNT; ++i) printf("X %s\n", spb_stock_names[i]);
        speedbrake_shutdown();
        return 0;
    }
    test_registry();
    test_first_sync_and_cockpit_drag();
    test_commands();
    test_hardware_adoption();
    test_landing_and_reverse();
    test_flight_logic();
    test_pause_replay_disable_reload();
    test_shutdown();
    puts("Speed brake adapter: registry, first sync, push-down drag with 1/2 notch and ARM at RET, clicks, commands and "
         "intercepted stock commands, hardware adoption, FBW order into X-Plane's handle (never ARMED) with X-Plane's "
         "auto speedbrake off, landing / reverse ground spoilers, thrust-module and fallback TLA, A.FLOOR inhibition, "
         "flap gains and spoiler lift, pause, replay, disable, reload and shutdown passed");
    return 0;
}
