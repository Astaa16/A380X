/* SPDX-License-Identifier: GPL-3.0-or-later
 * Production ELEC panel adapter against a mock XPLM: registry contract, stock coupling,
 * observed stock/ground-service changes, guards, mouse drag, sounds and lifecycle.
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMSound.h"
#include "XPLMUtilities.h"
#ifndef ELEC_TEST_ROOT
#define ELEC_TEST_ROOT "."
#endif

typedef struct {
    char name[128];
    int registered, writable, type, vi[16];
    double value;
    float vf[16];
    XPLMGetDatai_f get_i; XPLMSetDatai_f set_i;
    XPLMGetDataf_f get_f; XPLMSetDataf_f set_f;
    XPLMGetDatad_f get_d; XPLMSetDatad_f set_d;
    void *read_context, *write_context;
} MockRef;
typedef struct { char name[128]; XPLMCommandCallback_f callback; void *context; } MockCommand;
static MockRef mock_refs[2048];
static MockCommand mock_commands[160];
static int ref_count, command_count, fail_registration_after = -1, plays, positioned;
static int mouse_y = 500, plugin_enabled = 1;
static float clock_now;
static FMOD_VECTOR last_position;
static const void *last_sample;

static MockRef *new_ref(const char *name)
{
    MockRef *r;
    assert(ref_count < (int)(sizeof(mock_refs) / sizeof(mock_refs[0])));
    r = &mock_refs[ref_count++];
    memset(r, 0, sizeof(*r));
    snprintf(r->name, sizeof(r->name), "%s", name);
    r->registered = 1;
    return r;
}
XPLMDataRef XPLMFindDataRef(const char *name)
{
    int n;
    for (n = 0; n < ref_count; ++n) if (mock_refs[n].registered && !strcmp(name, mock_refs[n].name)) return &mock_refs[n];
    return !strncmp(name, "sim/", 4) ? new_ref(name) : NULL;
}
static MockRef *raw(const char *name) { return (MockRef *)XPLMFindDataRef(name); }
int XPLMGetDatai(XPLMDataRef h) { MockRef *r = h; assert(r && r->registered); return r->get_i ? r->get_i(r->read_context) : (int)r->value; }
float XPLMGetDataf(XPLMDataRef h) { MockRef *r = h; assert(r && r->registered); return r->get_f ? r->get_f(r->read_context) : (float)r->value; }
double XPLMGetDatad(XPLMDataRef h) { MockRef *r = h; assert(r && r->registered); return r->get_d ? r->get_d(r->read_context) : r->value; }
void XPLMSetDatai(XPLMDataRef h, int v)
{
    MockRef *r = h; assert(r && r->registered);
    if (r->set_i) { assert(r->writable); r->set_i(r->write_context, v); } else r->value = v;
}
void XPLMSetDataf(XPLMDataRef h, float v)
{
    MockRef *r = h; assert(r && r->registered);
    if (r->set_f) { assert(r->writable); r->set_f(r->write_context, v); } else r->value = v;
}
int XPLMGetDatavi(XPLMDataRef h, int *values, int offset, int count)
{
    MockRef *r = h; assert(r && r->registered && offset >= 0 && offset + count <= 16);
    memcpy(values, r->vi + offset, (size_t)count * sizeof(int)); return count;
}
int XPLMGetDatavf(XPLMDataRef h, float *values, int offset, int count)
{
    MockRef *r = h; assert(r && r->registered && offset >= 0 && offset + count <= 16);
    memcpy(values, r->vf + offset, (size_t)count * sizeof(float)); return count;
}
void XPLMSetDatavi(XPLMDataRef h, int *values, int offset, int count)
{
    MockRef *r = h; assert(r && r->registered && offset >= 0 && offset + count <= 16);
    memcpy(r->vi + offset, values, (size_t)count * sizeof(int));
}
XPLMDataRef XPLMRegisterDataAccessor(const char *name, XPLMDataTypeID type, int writable,
    XPLMGetDatai_f get_i, XPLMSetDatai_f set_i, XPLMGetDataf_f get_f, XPLMSetDataf_f set_f,
    XPLMGetDatad_f get_d, XPLMSetDatad_f set_d, XPLMGetDatavi_f get_vi, XPLMSetDatavi_f set_vi,
    XPLMGetDatavf_f get_vf, XPLMSetDatavf_f set_vf, XPLMGetDatab_f get_b, XPLMSetDatab_f set_b,
    void *read_context, void *write_context)
{
    MockRef *r;
    assert(!XPLMFindDataRef(name) && !get_vi && !set_vi && !get_vf && !set_vf && !get_b && !set_b);
    assert(type == xplmType_Int ? (get_i && !get_f && !get_d) : (type == (xplmType_Float | xplmType_Double) && get_f && get_d));
    assert(writable ? (write_context && (type == xplmType_Int ? set_i != NULL : (set_f && set_d))) : (!set_i && !set_f && !set_d && !write_context));
    if (fail_registration_after == 0) return NULL;
    if (fail_registration_after > 0) --fail_registration_after;
    r = new_ref(name);
    r->type = type; r->writable = writable; r->get_i = get_i; r->set_i = set_i; r->get_f = get_f; r->set_f = set_f;
    r->get_d = get_d; r->set_d = set_d; r->read_context = read_context; r->write_context = write_context;
    return r;
}
void XPLMUnregisterDataAccessor(XPLMDataRef h) { MockRef *r = h; assert(r && r->registered); r->registered = 0; }
XPLMCommandRef XPLMFindCommand(const char *name)
{
    int n;
    for (n = 0; n < command_count; ++n) if (!strcmp(name, mock_commands[n].name)) return &mock_commands[n];
    return NULL;
}
XPLMCommandRef XPLMCreateCommand(const char *name, const char *description)
{
    MockCommand *c = XPLMFindCommand(name);
    (void)description;
    if (c) return c;
    assert(command_count < (int)(sizeof(mock_commands) / sizeof(mock_commands[0])));
    c = &mock_commands[command_count++];
    snprintf(c->name, sizeof(c->name), "%s", name);
    return c;
}
void XPLMRegisterCommandHandler(XPLMCommandRef h, XPLMCommandCallback_f callback, int before, void *context)
{
    MockCommand *c = h; assert(c && !c->callback && callback && before); c->callback = callback; c->context = context;
}
void XPLMUnregisterCommandHandler(XPLMCommandRef h, XPLMCommandCallback_f callback, int before, void *context)
{
    MockCommand *c = h; assert(c && c->callback == callback && c->context == context && before);
    c->callback = NULL; c->context = NULL;
}
static int own_gpu_requests;
void XPLMCommandOnce(XPLMCommandRef h)
{
    MockCommand *c = h; assert(c);
    ++own_gpu_requests;
    if (c->callback) c->callback(c, xplm_CommandBegin, c->context), c->callback(c, xplm_CommandEnd, c->context);
}
float XPLMGetElapsedTime(void) { return clock_now; }
void XPLMGetMouseLocationGlobal(int *x, int *y) { if (x) *x = 100; if (y) *y = mouse_y; }
XPLMPluginID XPLMGetMyID(void) { return 7; }
void XPLMGetPluginInfo(XPLMPluginID id, char *name, char *path, char *signature, char *description)
{
    (void)id; (void)name; (void)signature; (void)description;
    if (path) snprintf(path, 256, "%s/mac_x64/A380XAvionics.xpl", ELEC_TEST_ROOT);
}
static int fake_channel;
FMOD_CHANNEL *XPLMPlayPCMOnBus(void *buffer, uint32_t size, FMOD_SOUND_FORMAT format, int rate, int channels,
                               int loop, XPLMAudioBus bus, XPLMPCMComplete_f callback, void *refcon)
{
    assert(buffer && size > 1000 && format == FMOD_SOUND_FORMAT_PCM16 && rate == 44100 && channels == 1 && !loop);
    assert(bus == xplm_AudioInterior && !callback && !refcon);
    ++plays; last_sample = buffer; return (FMOD_CHANNEL *)&fake_channel;
}
FMOD_RESULT XPLMSetAudioPosition(FMOD_CHANNEL *channel, FMOD_VECTOR *position, FMOD_VECTOR *velocity)
{
    assert(channel == (FMOD_CHANNEL *)&fake_channel && position && velocity);
    ++positioned; last_position = *position; return FMOD_OK;
}
FMOD_RESULT XPLMSetAudioFadeDistance(FMOD_CHANNEL *channel, float min_distance, float max_distance)
{
    assert(channel && min_distance > 0.0f && max_distance > min_distance); return FMOD_OK;
}
FMOD_RESULT XPLMSetAudioVolume(FMOD_CHANNEL *channel, float volume) { assert(channel && volume > 0.0f); return FMOD_OK; }

#include "a380x_elec_panel.inc"

static MockCommand *cmd(const char *suffix)
{
    char name[128]; MockCommand *c;
    snprintf(name, sizeof(name), "a380x/xp12/elec/%s", suffix);
    c = XPLMFindCommand(name); assert(c && c->callback); return c;
}
static void phase(const char *suffix, XPLMCommandPhase p) { MockCommand *c = cmd(suffix); assert(c->callback(c, p, c->context) == 1); }
static void click(const char *suffix) { phase(suffix, xplm_CommandBegin); phase(suffix, xplm_CommandEnd); }
static MockRef *pub(const char *suffix)
{
    char name[128]; MockRef *r;
    snprintf(name, sizeof(name), "a380x/xp12/elec/%s", suffix);
    r = (MockRef *)XPLMFindDataRef(name); assert(r && r->registered); return r;
}
static int geti(const char *suffix) { return XPLMGetDatai(pub(suffix)); }
static float getf(const char *suffix) { return XPLMGetDataf(pub(suffix)); }
static void frames(float seconds)
{
    while (seconds > 0.0f) { clock_now += 0.02f; seconds -= 0.02f; elec_update(0.02f); }
}
static void cold_aircraft(void)
{
    raw("sim/aircraft/electrical/num_batteries")->value = 2;
    memset(raw("sim/cockpit2/electrical/battery_on")->vi, 0, sizeof(int) * 16);
    memset(raw("sim/cockpit2/electrical/generator_on")->vi, 0, sizeof(int) * 16);
    raw("sim/cockpit2/electrical/generator_on")->vi[0] = 1;   /* stock default: GEN 1 switch on */
    raw("sim/cockpit2/electrical/APU_generator_on")->value = 0;
    raw("sim/cockpit2/electrical/GPU_generator_on")->value = 0;
    memset(raw("sim/cockpit2/electrical/bus_volts")->vf, 0, sizeof(float) * 16);
    raw("sim/flightmodel/failures/onground_any")->value = 1;
    raw("sim/flightmodel/position/groundspeed")->value = 0;
    XPLMCreateCommand("sim/ground_ops/toggle_gpu_request", "ground power");
}

static void test_registry_and_cold_state(void)
{
    int n;
    assert(elec_init());
    assert(geti("stock_battery_slots") == 2);
    assert(geti("pb/bat1") == 0 && geti("pb/bat2") == 0);            /* adopted from the cold ACF */
    assert(geti("pb/bat_ess") == 1 && geti("pb/bat_apu") == 1);      /* no native slot: panel AUTO */
    assert(geti("pb/gen1") == 1 && geti("pb/gen2") == 0);
    assert(geti("pb/apu_gen_a") == 0 && geti("pb/apu_gen_b") == 0);
    assert(geti("pb/bus_tie") == 1 && geti("pb/ac_ess_feed") == 1 && geti("pb/pax_sys") == 1);
    assert(!geti("ext/connected") && !geti("pb/ext1"));
    assert(getf("cover/drive1") == 0.0f && !pub("cover/drive1")->get_i && pub("cover/drive1")->writable);
    assert(!pub("pb/drive1")->writable && pub("pb/bat1")->writable && pub("pb/ext1")->writable);
    assert(pub("pb/bat1_press_ratio")->type == (xplmType_Float | xplmType_Double) && !pub("pb/bat1_press_ratio")->writable);
    frames(0.1f);
    assert(getf("lamp/bat1_lower") == 0.0f);                                  /* no power: dark */
    raw("sim/cockpit2/electrical/bus_volts")->vf[0] = 28.0f; frames(0.04f);
    assert(geti("lamp_power") == 1 && getf("lamp/bat1_lower") == 1.0f && getf("lamp/gen2_lower") == 1.0f);
    assert(getf("lamp/gen1_lower") == 0.0f && getf("lamp/bat1_upper") == 0.0f);
    for (n = 0; n < command_count; ++n) if (!strncmp(mock_commands[n].name, "a380x/", 6)) assert(mock_commands[n].callback);
}

static void test_stock_coupling_and_hardware(void)
{
    MockRef *battery = raw("sim/cockpit2/electrical/battery_on"), *gen = raw("sim/cockpit2/electrical/generator_on");
    int before = plays;
    phase("bat1_toggle", xplm_CommandBegin);
    assert(battery->vi[0] == 1 && geti("pb/bat1") == 1 && plays == before + 1);   /* press sample */
    frames(0.1f); assert(getf("pb/bat1_press_ratio") > 0.99f);
    phase("bat1_toggle", xplm_CommandEnd); assert(plays == before + 2);                   /* release sample */
    frames(0.2f); assert(getf("pb/bat1_press_ratio") == 0.0f);
    assert(positioned >= 1);
    /* Hardware bound to the stock command: the pb follows. */
    battery->vi[1] = 1; frames(0.04f); assert(geti("pb/bat2") == 1 && getf("lamp/bat2_lower") == 0.0f);
    battery->vi[0] = 0; frames(0.04f); assert(geti("pb/bat1") == 0);
    /* ESS / APU BAT have no native slot on this ACF: state only, slots 2/3 untouched. */
    click("bat_ess_toggle"); assert(geti("pb/bat_ess") == 0 && battery->vi[2] == 0);
    click("bat_ess_on"); assert(geti("pb/bat_ess") == 1);
    click("bat1_on"); assert(battery->vi[0] == 1);
    click("bat1_on"); assert(battery->vi[0] == 1);                      /* explicit: idempotent */
    /* GEN and DRIVE: a disconnected IDG keeps its generator off whatever the switch says. */
    click("gen2_toggle"); assert(gen->vi[1] == 1);
    click("drive2_cover_toggle"); frames(0.3f); assert(getf("cover/drive2") == 1.0f);
    click("drive2_press"); frames(0.04f); assert(geti("pb/drive2") == 1 && gen->vi[1] == 0 && geti("pb/gen2") == 1);
    gen->vi[1] = 1; frames(0.04f); assert(gen->vi[1] == 0);
    raw("sim/flightmodel/failures/onground_any")->value = 0;
    click("drive2_reset"); assert(geti("pb/drive2") == 1);              /* airborne: refused */
    raw("sim/flightmodel/failures/onground_any")->value = 1;
    raw("sim/flightmodel/engine/ENGN_running")->vi[1] = 1;
    click("drive2_reset"); assert(geti("pb/drive2") == 1);              /* engine 2 running: refused */
    raw("sim/flightmodel/engine/ENGN_running")->vi[1] = 0;
    click("drive2_reset"); frames(0.04f); assert(geti("pb/drive2") == 0 && gen->vi[1] == 1);
    /* Real failures only: FAULT from rel_genera / rel_batter. */
    raw("sim/operation/failures/rel_genera2")->value = 6; frames(0.04f);
    assert(getf("lamp/gen3_upper") == 1.0f);
    raw("sim/operation/failures/rel_genera2")->value = 0;
    raw("sim/operation/failures/rel_batter0")->value = 6; frames(0.04f);
    assert(getf("lamp/bat1_upper") == 1.0f && getf("lamp/bat_ess_upper") == 0.0f);
    raw("sim/operation/failures/rel_batter0")->value = 0;
    XPLMSetDatai(pub("fail/ac_ess_feed"), 1); frames(0.04f); assert(getf("lamp/ac_ess_feed_upper") == 1.0f);
    XPLMSetDatai(pub("fail/ac_ess_feed"), 0);
    /* APU GEN A/B share the single native APU generator. */
    click("apu_gen_a_toggle"); assert(raw("sim/cockpit2/electrical/APU_generator_on")->value == 1);
    click("apu_gen_b_toggle"); click("apu_gen_a_toggle"); assert(raw("sim/cockpit2/electrical/APU_generator_on")->value == 1);
    click("apu_gen_b_toggle"); assert(raw("sim/cockpit2/electrical/APU_generator_on")->value == 0);
    raw("sim/cockpit2/electrical/APU_generator_on")->value = 1; frames(0.04f);
    assert(geti("pb/apu_gen_a") == 1 && geti("pb/apu_gen_b") == 1);
}

static void test_external_power_and_ground_services(void)
{
    MockRef *gpu = raw("sim/cockpit2/electrical/GPU_generator_on");
    MockCommand *request = XPLMFindCommand("sim/ground_ops/toggle_gpu_request");
    click("ext1_press"); frames(0.04f); assert(gpu->value == 0 && !geti("pb/ext1"));       /* nothing connected */
    assert(request->callback(request, xplm_CommandBegin, request->context) == 1);   /* flyPad / ground services */
    frames(0.04f);
    assert(geti("ext/connected") && geti("ext/avail3") && getf("lamp/ext1_upper") == 1.0f && gpu->value == 0);
    click("ext1_press"); frames(0.04f);
    assert(gpu->value == 1 && geti("pb/ext1") && getf("lamp/ext1_lower") == 1.0f && getf("lamp/ext1_upper") == 0.0f);
    click("ext3_press"); click("ext1_press"); frames(0.04f); assert(gpu->value == 1 && geti("pb/ext3"));
    click("ext3_press"); frames(0.04f); assert(gpu->value == 0 && getf("lamp/ext3_upper") == 1.0f);
    /* Stock GPU on (hardware / preset) shows EXT 1 ON; stock GPU off clears every ON. */
    gpu->value = 1; frames(0.04f); assert(geti("pb/ext1") && getf("lamp/ext1_lower") == 1.0f);
    gpu->value = 0; frames(0.04f); assert(!geti("pb/ext1") && geti("ext/connected"));
    click("ext2_press"); frames(0.04f); assert(gpu->value == 1);
    raw("sim/flightmodel/position/groundspeed")->value = 3.0; frames(0.04f);     /* rolling: unplugged */
    assert(!geti("ext/connected") && gpu->value == 0 && getf("lamp/ext2_lower") == 0.0f);
    raw("sim/flightmodel/position/groundspeed")->value = 0;
    request->callback(request, xplm_CommandBegin, request->context); frames(0.04f); assert(geti("ext/connected"));
    request->callback(request, xplm_CommandBegin, request->context); frames(0.04f); assert(!geti("ext/connected"));
    /* flyPad: gpu_connect_toggle connects and also calls the X-Plane truck when one can come. */
    raw("sim/cockpit2/electrical/GPU_can_be_called_for")->value = 1;
    click("gpu_connect_toggle"); frames(0.04f); assert(geti("ext/connected") && own_gpu_requests == 1);
    click("gpu_connect_toggle"); frames(0.04f); assert(!geti("ext/connected") && own_gpu_requests == 2);
    raw("sim/flightmodel/failures/onground_any")->value = 0;
    click("gpu_connect_toggle"); assert(!geti("ext/connected") && own_gpu_requests == 2);   /* airborne */
    raw("sim/flightmodel/failures/onground_any")->value = 1;
}

static void test_guards_drag_and_sounds(void)
{
    int before;
    MockRef *cover = pub("cover/ac_ess_feed");
    click("ac_ess_feed_toggle"); click("ac_ess_feed_off"); assert(geti("pb/ac_ess_feed") == 1);  /* guarded */
    /* Grab and pull the guard down by 70 px: it follows the hand, then flips open. */
    before = plays;
    mouse_y = 500; phase("ac_ess_feed_cover_toggle", xplm_CommandBegin);
    mouse_y = 497; phase("ac_ess_feed_cover_toggle", xplm_CommandContinue); assert(XPLMGetDataf(cover) == 0.0f);
    mouse_y = 430; phase("ac_ess_feed_cover_toggle", xplm_CommandContinue);
    assert(fabsf(XPLMGetDataf(cover) - 0.5f) < 1e-4f);
    frames(0.02f); assert(plays == before + 1);                                  /* unlatch snap */
    phase("ac_ess_feed_cover_toggle", xplm_CommandEnd); frames(0.3f); assert(XPLMGetDataf(cover) == 1.0f);
    click("ac_ess_feed_toggle"); assert(geti("pb/ac_ess_feed") == 0 && getf("lamp/ac_ess_feed_lower") == 1.0f);
    click("ac_ess_feed_on"); assert(geti("pb/ac_ess_feed") == 1);
    /* Short drag back up: it falls back open; a plain click closes it. */
    mouse_y = 500; phase("ac_ess_feed_cover_toggle", xplm_CommandBegin);
    mouse_y = 530; phase("ac_ess_feed_cover_toggle", xplm_CommandContinue); phase("ac_ess_feed_cover_toggle", xplm_CommandEnd);
    frames(0.3f); assert(XPLMGetDataf(cover) == 1.0f);
    before = plays; click("ac_ess_feed_cover_toggle"); frames(0.3f);
    assert(XPLMGetDataf(cover) == 0.0f && plays == before + 1);                 /* seated snap */
    click("rat_man_on_cover_open"); frames(0.3f); click("rat_man_on_press"); assert(geti("pb/rat_man_on") == 1);
    click("rat_man_on_cover_close"); frames(0.3f); assert(getf("cover/rat_man_on") == 0.0f);
    XPLMSetDataf(cover, 0.7f); assert(XPLMGetDataf(cover) == 0.7f);               /* writable (phase 2 / tests) */
    XPLMSetDataf(cover, 0.0f);
    /* ANN LT TEST lights every authored legend while powered. */
    XPLMSetDatai(pub("ann_lt"), 0);
    assert(getf("lamp/gen4_upper") == 1.0f && getf("lamp/ext4_lower") == 1.0f && getf("lamp/drive3_lower") == 1.0f);
    XPLMSetDatai(pub("ann_lt"), 1); assert(geti("ann_lt") == 1);
}

static void test_external_logic_and_lifecycle(void)
{
    MockRef *battery = raw("sim/cockpit2/electrical/battery_on");
    int saved_commands, n;
    XPLMSetDatai(pub("external_logic"), 1);
    battery->vi[0] = 0; click("bat1_on");                                    /* phase 2 owns the network */
    frames(0.04f); assert(battery->vi[0] == 0 && geti("pb/bat1") == 1);
    XPLMSetDatai(pub("lamp_power"), 0); frames(0.04f); assert(getf("lamp/gen2_lower") == 0.0f);
    XPLMSetDatai(pub("ext/connected"), 1); XPLMSetDatai(pub("pb/ext4"), 1); frames(0.04f);
    assert(getf("lamp/ext4_lower") == 0.0f && geti("pb/ext4"));                     /* no lamp power */
    elec_plane_loaded();                                                       /* reload: back to phase 1 */
    assert(!geti("external_logic") && geti("pb/bat1") == 0 && !geti("pb/ext4"));
    phase("bat2_toggle", xplm_CommandBegin);
    plugin_enabled = 0; phase("bat2_toggle", xplm_CommandEnd); assert(!elec.held[A380X_ELEC_BAT2]);
    click("bat2_toggle"); assert(geti("pb/bat2") == 0);                             /* disabled: ignored */
    plugin_enabled = 1;
    elec_shutdown();
    saved_commands = command_count;
    for (n = 0; n < command_count; ++n) assert(!mock_commands[n].callback);
    for (n = 0; n < elec_ref_count; ++n) assert(!XPLMFindDataRef(elec_refs[n].name));
    assert(elec_init() && command_count == saved_commands); elec_shutdown();
    fail_registration_after = 7; assert(!elec_init()); fail_registration_after = -1;
    for (n = 0; n < elec_ref_count; ++n) assert(!XPLMFindDataRef(elec_refs[n].name));
    for (n = 0; n < command_count; ++n) assert(!mock_commands[n].callback);
    assert(elec_init()); elec_shutdown();
}

static void test_sound_placement(void)
{
    FMOD_VECTOR p;
    assert(elec_init());
    raw("sim/flightmodel/position/local_x")->value = 1000.0; raw("sim/flightmodel/position/local_y")->value = 50.0;
    raw("sim/flightmodel/position/local_z")->value = -2000.0;
    raw("sim/aircraft/view/acf_peX")->value = -0.53; raw("sim/aircraft/view/acf_peY")->value = 3.0;
    raw("sim/aircraft/view/acf_peZ")->value = -30.0;
    raw("sim/flightmodel/position/psi")->value = 90.0;   /* nose east (+x): body aft (+z) -> world -x */
    raw("sim/graphics/view/view_x")->value = 1030.0; raw("sim/graphics/view/view_y")->value = 53.0;
    raw("sim/graphics/view/view_z")->value = -2000.53;
    assert(elec_panel_world(&p));
    assert(fabsf(p.x - (1000.0f + 30.263f)) < 1e-3f && fabsf(p.y - 53.262f) < 1e-3f && fabsf(p.z - (-2000.0f + 0.0f)) < 1e-3f);
    raw("sim/graphics/view/view_x")->value = 1100.0; assert(!elec_panel_world(&p));   /* camera not at the panel */
    raw("sim/graphics/view/view_is_external")->value = 1; assert(elec_panel_world(&p));
    elec_shutdown();
}

int main(int argc, char **argv)
{
    int n;
    cold_aircraft();
    if (argc > 1 && !strcmp(argv[1], "--contracts")) {
        assert(elec_init());
        for (n = 0; n < command_count; ++n) if (!strncmp(mock_commands[n].name, "a380x/", 6)) printf("C %s\n", mock_commands[n].name);
        for (n = 0; n < ref_count; ++n) if (mock_refs[n].registered) {
            if (!strncmp(mock_refs[n].name, "a380x/", 6)) printf("D %s\n", mock_refs[n].name);
            else if (!strncmp(mock_refs[n].name, "sim/", 4)) printf("R %s\n", mock_refs[n].name);
        }
        for (n = 0; n < ES_COUNT; ++n) if (elec_samples[n].data) printf("S %s\n", elec_sample_files[n]);
        elec_shutdown();
        return 0;
    }
    test_registry_and_cold_state();
    test_stock_coupling_and_hardware();
    test_external_power_and_ground_services();
    test_guards_drag_and_sounds();
    test_external_logic_and_lifecycle();
    test_sound_placement();
    puts("ELEC panel adapter: registry, stock coupling, hardware/ground-service observation, guards/drag, sounds and lifecycle passed");
    return 0;
}
