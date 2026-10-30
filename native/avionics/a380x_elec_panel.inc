/* SPDX-License-Identifier: GPL-3.0-or-later
 * ELEC + EMER ELEC PWR overhead panel: X-Plane adapter (phase 1).
 *
 * Registers the a380x/xp12/elec/ commands and datarefs listed in
 * tools/elec_panel_contract.py, plays the FBW cockpit click samples at the panel and,
 * until the A380 electrical network exists, couples the pushbuttons minimally to the
 * stock X-Plane electrical system:
 *   BAT 1 / BAT 2 / ESS BAT / APU BAT -> battery_on[0] / [1] / [2] / [3] when the
 *       aircraft has that many native batteries (otherwise panel state only);
 *   GEN n -> generator_on[n-1] (forced off while DRIVE n is disconnected);
 *   APU GEN A or B -> APU_generator_on (one native APU generator);
 *   EXT n ON -> GPU_generator_on; AVAIL = ground power connected (ext/connected, see
 *       elec_gpu_request / gpu_connect_toggle);
 *   BUS TIE, AC ESS FEED, ELMU, PAX SYS, GALLEY, COMMERCIAL, RAT MAN ON: state only.
 * Stock changes made elsewhere (hardware bindings to sim/electrical/..., the flyPad
 * presets) are observed every frame and adopted by the pushbutton states.
 * FAULT lamps use real failure datarefs only (rel_batterN, rel_generaN) plus the
 * writable <id>_fault inputs; there is no invented fault.
 * Writing 1 to a380x/xp12/elec/external_logic hands all coupling to an external network
 * model (phase 2): the stock datarefs are then never touched.
 */
#include "a380x_elec_panel.h"

static A380XElecPanel elec;
static A380XElecInputs elec_inputs;

enum {
    EL_BATTERY_ON, EL_NUM_BATTERIES, EL_GENERATOR_ON, EL_APU_GEN_ON, EL_GPU_ON, EL_BUS_VOLTS,
    EL_ON_GROUND, EL_GROUNDSPEED, EL_ENGINE_RUNNING, EL_LOCAL_X, EL_LOCAL_Y, EL_LOCAL_Z,
    EL_PSI, EL_THETA, EL_PHI, EL_PE_X, EL_PE_Y, EL_PE_Z, EL_VIEW_EXTERNAL, EL_VIEW_X, EL_VIEW_Y, EL_VIEW_Z,
    EL_FAIL_BAT0, EL_FAIL_BAT1, EL_FAIL_BAT2, EL_FAIL_BAT3,
    EL_FAIL_GEN0, EL_FAIL_GEN1, EL_FAIL_GEN2, EL_FAIL_GEN3, EL_GPU_CALLABLE, EL_RAW_COUNT
};
static const char *const elec_raw_names[EL_RAW_COUNT] = {
    "sim/cockpit2/electrical/battery_on",
    "sim/aircraft/electrical/num_batteries",
    "sim/cockpit2/electrical/generator_on",
    "sim/cockpit2/electrical/APU_generator_on",
    "sim/cockpit2/electrical/GPU_generator_on",
    "sim/cockpit2/electrical/bus_volts",
    "sim/flightmodel/failures/onground_any",
    "sim/flightmodel/position/groundspeed",
    "sim/flightmodel/engine/ENGN_running",
    "sim/flightmodel/position/local_x",
    "sim/flightmodel/position/local_y",
    "sim/flightmodel/position/local_z",
    "sim/flightmodel/position/psi",
    "sim/flightmodel/position/theta",
    "sim/flightmodel/position/phi",
    "sim/aircraft/view/acf_peX",
    "sim/aircraft/view/acf_peY",
    "sim/aircraft/view/acf_peZ",
    "sim/graphics/view/view_is_external",
    "sim/graphics/view/view_x",
    "sim/graphics/view/view_y",
    "sim/graphics/view/view_z",
    "sim/operation/failures/rel_batter0",
    "sim/operation/failures/rel_batter1",
    "sim/operation/failures/rel_batter2",
    "sim/operation/failures/rel_batter3",
    "sim/operation/failures/rel_genera0",
    "sim/operation/failures/rel_genera1",
    "sim/operation/failures/rel_genera2",
    "sim/operation/failures/rel_genera3",
    "sim/cockpit2/electrical/GPU_can_be_called_for",
};
static XPLMDataRef elec_raw[EL_RAW_COUNT];
/* Native battery slot of BAT 1, ESS BAT, BAT 2, APU BAT (used only if the ACF has the slot). */
static const int elec_battery_control[4] = { A380X_ELEC_BAT1, A380X_ELEC_BAT2, A380X_ELEC_BAT_ESS, A380X_ELEC_BAT_APU };

/* ELEC panel centre (source glTF -0.0, 3.46, 32.27) relative to the captain's default eye point
 * (acf _pe_xyz -1.74/10.5/10.1 ft) in aircraft axes (x right, y up, z aft), metres. The cockpit
 * object is attached with psi 180 deg at z 115.108 ft: acf = (-x, y, 35.085 m - z). */
static const float elec_panel_from_eye[3] = { 0.530f, 0.262f, -0.263f };

typedef struct { short *data; uint32_t bytes; } ElecSample;
enum { ES_PRESS_A, ES_PRESS_B, ES_RELEASE_A, ES_RELEASE_B, ES_GUARD_OPEN, ES_GUARD_CLOSE, ES_COUNT };
static const char *const elec_sample_files[ES_COUNT] = {
    "elec_pb_press_a.pcm", "elec_pb_press_b.pcm", "elec_pb_release_a.pcm",
    "elec_pb_release_b.pcm", "elec_guard_open.pcm", "elec_guard_close.pcm"
};
static ElecSample elec_samples[ES_COUNT];

static struct {
    int external_logic, ext_connected, battery_slots, lamp_power, sound_variant, ready;
    int fault_input[A380X_ELEC_CONTROL_COUNT];
    int seen_battery[4], seen_generator[4], seen_apu_gen, seen_gpu;
    int dragging[A380X_ELEC_COVER_COUNT], drag_active[A380X_ELEC_COVER_COUNT];
    float drag_start[A380X_ELEC_COVER_COUNT];
    int drag_mouse_y[A380X_ELEC_COVER_COUNT];
    XPLMCommandRef gpu_request;
    int own_request;
    /* Phase 2 network (a380x_elec_adapter.inc, external_logic): FAULT conditions and the
     * per-legend lamp supply computed by the A380 network. */
    int network_lamps;
    unsigned char network_fault[A380X_ELEC_CONTROL_COUNT];
    unsigned char lamp_powered[A380X_ELEC_CONTROL_COUNT][2];
} elec_io;

static int elec_raw_int(int index) { return elec_raw[index] ? XPLMGetDatai(elec_raw[index]) : 0; }
static float elec_raw_float(int index)
{
    float value = elec_raw[index] ? XPLMGetDataf(elec_raw[index]) : 0.0f;
    return isfinite(value) ? value : 0.0f;
}
static int elec_failed(int index) { return elec_raw_int(index) >= 6; }

/* ---------------------------------------------------------------- sounds */
static void elec_load_sounds(void)
{
    char root[1024], path[1200];
    char *cut;
    int i;
    root[0] = 0;
    XPLMGetPluginInfo(XPLMGetMyID(), NULL, root, NULL, NULL);
    for (i = 0; i < 2; ++i) { /* .../A380XAvionics/<platform>/A380XAvionics.xpl -> .../A380XAvionics */
        cut = strrchr(root, '/'); if (!cut) cut = strrchr(root, '\\');
        if (!cut) return;
        *cut = 0;
    }
    for (i = 0; i < ES_COUNT; ++i) {
        FILE *file;
        long length;
        if (snprintf(path, sizeof(path), "%s/sounds/%s", root, elec_sample_files[i]) >= (int)sizeof(path)) continue;
        file = fopen(path, "rb");
        if (!file) continue;
        if (fseek(file, 0, SEEK_END) != 0) { fclose(file); continue; }
        length = ftell(file);
        if (length <= 0 || length > 400000 || (length & 1) || fseek(file, 0, SEEK_SET) != 0) { fclose(file); continue; }
        elec_samples[i].data = (short *)malloc((size_t)length);
        if (elec_samples[i].data && fread(elec_samples[i].data, 1, (size_t)length, file) == (size_t)length)
            elec_samples[i].bytes = (uint32_t)length;
        else { free(elec_samples[i].data); elec_samples[i].data = NULL; }
        fclose(file);
    }
}
static void elec_free_sounds(void)
{
    int i;
    for (i = 0; i < ES_COUNT; ++i) { free(elec_samples[i].data); elec_samples[i].data = NULL; elec_samples[i].bytes = 0; }
}
/* World (OpenGL local) position of the panel; 0 when it cannot be placed credibly. */
static int elec_panel_world(FMOD_VECTOR *out)
{
    double b[3], v1[3], v2[3], c, s, view[3], d;
    double psi = elec_raw_float(EL_PSI) * M_PI / 180.0, theta = elec_raw_float(EL_THETA) * M_PI / 180.0;
    double phi = elec_raw_float(EL_PHI) * M_PI / 180.0;
    if (!elec_raw[EL_LOCAL_X] || !elec_raw[EL_LOCAL_Y] || !elec_raw[EL_LOCAL_Z]) return 0;
    b[0] = elec_raw_float(EL_PE_X) + elec_panel_from_eye[0];
    b[1] = elec_raw_float(EL_PE_Y) + elec_panel_from_eye[1];
    b[2] = elec_raw_float(EL_PE_Z) + elec_panel_from_eye[2];
    /* X-Plane aircraft drawing: Ry(-psi) * Rx(theta) * Rz(-phi). */
    c = cos(-phi); s = sin(-phi);
    v1[0] = c * b[0] - s * b[1]; v1[1] = s * b[0] + c * b[1]; v1[2] = b[2];
    c = cos(theta); s = sin(theta);
    v2[0] = v1[0]; v2[1] = c * v1[1] - s * v1[2]; v2[2] = s * v1[1] + c * v1[2];
    c = cos(-psi); s = sin(-psi);
    out->x = (float)(XPLMGetDatad(elec_raw[EL_LOCAL_X]) + c * v2[0] + s * v2[2]);
    out->y = (float)(XPLMGetDatad(elec_raw[EL_LOCAL_Y]) + v2[1]);
    out->z = (float)(XPLMGetDatad(elec_raw[EL_LOCAL_Z]) - s * v2[0] + c * v2[2]);
    if (!isfinite(out->x) || !isfinite(out->y) || !isfinite(out->z)) return 0;
    if (elec_raw_int(EL_VIEW_EXTERNAL)) return 1;
    view[0] = out->x - elec_raw_float(EL_VIEW_X); view[1] = out->y - elec_raw_float(EL_VIEW_Y);
    view[2] = out->z - elec_raw_float(EL_VIEW_Z);
    d = sqrt(view[0] * view[0] + view[1] * view[1] + view[2] * view[2]);
    return d < 4.0; /* in the cockpit the overhead is within reach; otherwise play it unplaced */
}
static void elec_play(int sample)
{
    FMOD_CHANNEL *channel;
    FMOD_VECTOR position, velocity = {0.0f, 0.0f, 0.0f};
    if (sample < 0 || sample >= ES_COUNT || !elec_samples[sample].data) return;
    channel = XPLMPlayPCMOnBus(elec_samples[sample].data, elec_samples[sample].bytes, FMOD_SOUND_FORMAT_PCM16,
                               44100, 1, 0, xplm_AudioInterior, NULL, NULL);
    if (!channel) return;
    /* Sounds start on the next audio refresh, so placing them now is valid. */
    if (elec_panel_world(&position)) {
        XPLMSetAudioPosition(channel, &position, &velocity);
        XPLMSetAudioFadeDistance(channel, 0.5f, 12.0f);
    }
    XPLMSetAudioVolume(channel, 1.0f);
}
static void elec_play_events(unsigned int events)
{
    if (events & A380X_ELEC_EVENT_PRESS) elec_play(elec_io.sound_variant & 1 ? ES_PRESS_B : ES_PRESS_A);
    if (events & A380X_ELEC_EVENT_RELEASE) elec_play(elec_io.sound_variant & 1 ? ES_RELEASE_B : ES_RELEASE_A);
    if (events) ++elec_io.sound_variant;
}

/* ---------------------------------------------------------------- stock coupling */
static int elec_battery_slot(int control)
{
    int slot;
    for (slot = 0; slot < 4; ++slot)
        if (elec_battery_control[slot] == control) return slot < elec_io.battery_slots ? slot : -1;
    return -1;
}
static int elec_any_ext_on(void)
{
    return elec.ext_on[0] || elec.ext_on[1] || elec.ext_on[2] || elec.ext_on[3];
}
static void elec_read_inputs(void)
{
    int n, engines[8] = {0};
    float bus[6] = {0};
    elec_inputs.on_ground = elec_raw_int(EL_ON_GROUND) != 0;
    if (elec_raw[EL_ENGINE_RUNNING]) XPLMGetDatavi(elec_raw[EL_ENGINE_RUNNING], engines, 0, 4);
    for (n = 0; n < 4; ++n) elec_inputs.engine_running[n] = engines[n] != 0;
    if (!elec_io.external_logic) {
        elec_io.battery_slots = elec_raw_int(EL_NUM_BATTERIES);
        if (elec_io.battery_slots < 0) elec_io.battery_slots = 0;
        if (elec_raw[EL_BUS_VOLTS]) XPLMGetDatavf(elec_raw[EL_BUS_VOLTS], bus, 0, 6);
        elec_io.lamp_power = 0;
        for (n = 0; n < 6; ++n) if (isfinite(bus[n]) && bus[n] >= 18.0f) elec_io.lamp_power = 1;
        /* Ground power cannot stay plugged in once the aircraft rolls or flies. */
        if (!elec_inputs.on_ground || elec_raw_float(EL_GROUNDSPEED) > 2.0f) elec_io.ext_connected = 0;
    }
    elec_inputs.lamp_power = elec_io.lamp_power;
    elec_inputs.per_lamp_power = elec_io.external_logic && elec_io.network_lamps;
    if (elec_inputs.per_lamp_power) memcpy(elec_inputs.lamp_powered, elec_io.lamp_powered, sizeof(elec_inputs.lamp_powered));
    for (n = 0; n < A380X_ELEC_CONTROL_COUNT; ++n)
        elec_inputs.fault[n] = elec_io.fault_input[n] != 0 || (elec_inputs.per_lamp_power && elec_io.network_fault[n]);
    if (!elec_io.external_logic) {
        for (n = 0; n < 4; ++n) {
            int slot = elec_battery_slot(elec_battery_control[n]);
            if (slot >= 0 && elec_failed(EL_FAIL_BAT0 + slot)) elec_inputs.fault[elec_battery_control[n]] = 1;
            if (elec_failed(EL_FAIL_GEN0 + n)) elec_inputs.fault[A380X_ELEC_GEN1 + n] = 1;
        }
    }
    for (n = 0; n < 4; ++n) elec_inputs.ext_available[n] = (unsigned char)(elec_io.ext_connected != 0);
}
/* Adopt stock changes made by anything else (hardware commands, presets, other plugins). */
static void elec_adopt_stock(void)
{
    int n, values[8] = {0}, value;
    if (elec_io.external_logic) return;
    XPLMGetDatavi(elec_raw[EL_BATTERY_ON], values, 0, 4);
    for (n = 0; n < 4; ++n) {
        int control = elec_battery_control[n], slot = elec_battery_slot(control);
        if (slot < 0) continue;
        value = values[slot] != 0;
        if (value != elec_io.seen_battery[slot]) elec.normal[control] = (unsigned char)value;
        elec_io.seen_battery[slot] = value;
    }
    XPLMGetDatavi(elec_raw[EL_GENERATOR_ON], values, 0, 4);
    for (n = 0; n < 4; ++n) {
        value = values[n] != 0;
        if (value != elec_io.seen_generator[n]) elec.normal[A380X_ELEC_GEN1 + n] = (unsigned char)value;
        elec_io.seen_generator[n] = value;
    }
    value = elec_raw_int(EL_APU_GEN_ON) != 0;
    if (value != elec_io.seen_apu_gen) elec.normal[A380X_ELEC_APU_GEN_A] = elec.normal[A380X_ELEC_APU_GEN_B] = (unsigned char)value;
    elec_io.seen_apu_gen = value;
    value = elec_raw_int(EL_GPU_ON) != 0;
    if (value != elec_io.seen_gpu) {
        if (value) { elec_io.ext_connected = 1; if (!elec_any_ext_on()) elec.ext_on[0] = 1; }
        else memset(elec.ext_on, 0, sizeof(elec.ext_on));
    }
    elec_io.seen_gpu = value;
}
/* Drive the stock switches from the pushbuttons (only where they differ). */
static void elec_drive_stock(void)
{
    int n, values[8] = {0}, desired;
    if (elec_io.external_logic || !elec_io.ready) return;
    XPLMGetDatavi(elec_raw[EL_BATTERY_ON], values, 0, 4);
    for (n = 0; n < 4; ++n) {
        int control = elec_battery_control[n], slot = elec_battery_slot(control);
        if (slot < 0) continue;
        desired = elec.normal[control] != 0;
        if ((values[slot] != 0) != desired) XPLMSetDatavi(elec_raw[EL_BATTERY_ON], &desired, slot, 1);
        elec_io.seen_battery[slot] = desired;
    }
    XPLMGetDatavi(elec_raw[EL_GENERATOR_ON], values, 0, 4);
    for (n = 0; n < 4; ++n) {
        desired = elec.normal[A380X_ELEC_GEN1 + n] && !elec.drive_disc[n];
        if ((values[n] != 0) != desired) XPLMSetDatavi(elec_raw[EL_GENERATOR_ON], &desired, n, 1);
        elec_io.seen_generator[n] = desired;
    }
    desired = elec.normal[A380X_ELEC_APU_GEN_A] || elec.normal[A380X_ELEC_APU_GEN_B];
    if ((elec_raw_int(EL_APU_GEN_ON) != 0) != desired) XPLMSetDatai(elec_raw[EL_APU_GEN_ON], desired);
    elec_io.seen_apu_gen = desired;
    desired = elec_io.ext_connected && elec_any_ext_on();
    if ((elec_raw_int(EL_GPU_ON) != 0) != desired) XPLMSetDatai(elec_raw[EL_GPU_ON], desired);
    elec_io.seen_gpu = desired;
}
/* X-Plane exposes no "GPU cart connected" state: a ground-power request (flyPad / ground
 * services) connects it, a second request removes it. */
static int elec_gpu_request(XPLMCommandRef command, XPLMCommandPhase phase, void *refcon)
{
    (void)command; (void)refcon;
    /* Observed in both phases: the network (external logic) uses the same connection flag. */
    if (!plugin_enabled || phase != xplm_CommandBegin || elec_io.own_request) return 1;
    if (elec_io.ext_connected) elec_io.ext_connected = 0;
    else if (elec_raw_int(EL_ON_GROUND)) elec_io.ext_connected = 1;
    return 1;
}

/* flyPad / ground crew: connect or remove ground power; when X-Plane can provide a GPU its
 * truck is requested too (our own observer ignores that request). */
static void elec_gpu_connect_toggle(void)
{
    if (!elec_io.ext_connected && !elec_raw_int(EL_ON_GROUND)) return;
    elec_io.ext_connected = !elec_io.ext_connected;
    if (elec_io.gpu_request && elec_raw[EL_GPU_CALLABLE] && elec_raw_int(EL_GPU_CALLABLE)) {
        elec_io.own_request = 1;
        XPLMCommandOnce(elec_io.gpu_request);
        elec_io.own_request = 0;
    }
}

static void elec_update(float dt)
{
    unsigned int opened, closed;
    int n;
    if (!plugin_enabled || !elec_io.ready) return;
    elec_adopt_stock();
    elec_read_inputs();
    a380x_elec_update(&elec, &elec_inputs, XPLMGetElapsedTime(), dt);
    elec_drive_stock();
    opened = elec.cover_opened; closed = elec.cover_closed;
    elec.cover_opened = elec.cover_closed = 0;
    for (n = 0; n < A380X_ELEC_COVER_COUNT; ++n) {
        if (opened & (1u << n)) elec_play(ES_GUARD_OPEN);
        if (closed & (1u << n)) elec_play(ES_GUARD_CLOSE);
    }
}

/* ---------------------------------------------------------------- datarefs
 * Names follow docs/ELEC_SYSTEM_SPEC.md 10.3: pb/<name> (position), pb/<name>_press_ratio,
 * lamp/<name>_upper|_lower (SEQ1|SEQ2), ext/..., fail/<name>, plus cover/<name> and ann_lt. */
enum {
    ERK_PB, ERK_PRESSED, ERK_LAMP, ERK_EXT_AVAIL, ERK_EXT_CONNECTED, ERK_FAIL, ERK_COVER,
    ERK_ANN_LT, ERK_LAMP_POWER, ERK_EXTERNAL, ERK_SLOTS
};
typedef struct { char name[128]; int kind, index, legend, writable, real; XPLMDataRef handle; } ElecRef;
static ElecRef elec_refs[192];
static int elec_ref_count;

static int elec_ann_lt(void)
{
    return elec_inputs.annunciator_test ? 0 : elec_inputs.annunciator_dim ? 2 : 1;
}
/* pb/<name>: latched pb-sw pushed in (AUTO / ON); EXT n ON; DRIVE n released (DISC, latched);
 * RAT MAN ON deployment requested. */
static float elec_pb_value(int c)
{
    switch (a380x_elec_controls[c].kind) {
    case A380X_ELEC_EXT: return elec.ext_on[c - A380X_ELEC_EXT1];
    case A380X_ELEC_GUARDED_MOMENTARY:
        return c == A380X_ELEC_RAT_MAN_ON ? elec.rat_requested : elec.drive_disc[c - A380X_ELEC_DRIVE1];
    default: return elec.normal[c];
    }
}
static float elec_ref_value(const ElecRef *r)
{
    switch (r->kind) {
    case ERK_PB: return elec_pb_value(r->index);
    case ERK_PRESSED: return elec.press[r->index].position;
    case ERK_LAMP: return plugin_enabled ? a380x_elec_lamp(&elec, &elec_inputs, r->index, r->legend) : 0.0f;
    case ERK_EXT_AVAIL: return elec_inputs.ext_available[r->index];
    case ERK_EXT_CONNECTED: return (float)elec_io.ext_connected;
    case ERK_FAIL: return (float)elec_io.fault_input[r->index];
    case ERK_COVER: return elec.cover[r->index];
    case ERK_ANN_LT: return (float)elec_ann_lt();
    case ERK_LAMP_POWER: return (float)elec_io.lamp_power;
    case ERK_EXTERNAL: return (float)elec_io.external_logic;
    case ERK_SLOTS: return (float)elec_io.battery_slots;
    default: return 0.0f;
    }
}
static int elec_get_int(void *refcon) { return (int)lroundf(elec_ref_value((const ElecRef *)refcon)); }
static float elec_get_float(void *refcon) { return elec_ref_value((const ElecRef *)refcon); }
static double elec_get_double(void *refcon) { return elec_ref_value((const ElecRef *)refcon); }
static void elec_set_value(ElecRef *r, float value)
{
    int on = isfinite(value) && value >= 0.5f;
    if (!plugin_enabled || !r->writable || !isfinite(value)) return;
    switch (r->kind) {
    case ERK_PB: {                                       /* authority write (no guard, no animation) */
        int c = r->index;
        if (a380x_elec_controls[c].kind == A380X_ELEC_EXT)
            elec.ext_on[c - A380X_ELEC_EXT1] = (unsigned char)(on && elec_inputs.ext_available[c - A380X_ELEC_EXT1]);
        else if (c == A380X_ELEC_RAT_MAN_ON) elec.rat_requested = (unsigned char)on;
        else if (a380x_elec_is_latch(c)) elec.normal[c] = (unsigned char)on;
        else return;                                     /* DRIVE DISC: only the pb or the reset */
        break;
    }
    case ERK_EXT_CONNECTED: elec_io.ext_connected = on; break;
    case ERK_FAIL: elec_io.fault_input[r->index] = on; break;
    case ERK_COVER: a380x_elec_cover_set(&elec, r->index, value); break;
    case ERK_ANN_LT: {                                   /* 0 TEST, 1 BRT, 2 DIM */
        int position = (int)lroundf(value);
        elec_inputs.annunciator_test = position <= 0;
        elec_inputs.annunciator_dim = position >= 2;
        break;
    }
    case ERK_LAMP_POWER: elec_io.lamp_power = on; break;          /* effective with external_logic */
    case ERK_EXTERNAL: elec_io.external_logic = on; break;
    default: return;
    }
    elec_read_inputs();
    elec_drive_stock();
}
static void elec_set_int(void *refcon, int value) { elec_set_value((ElecRef *)refcon, (float)value); }
static void elec_set_float(void *refcon, float value) { elec_set_value((ElecRef *)refcon, value); }
static void elec_set_double(void *refcon, double value) { elec_set_value((ElecRef *)refcon, (float)value); }

static void elec_add_ref(const char *suffix, int kind, int index, int legend, int writable, int real)
{
    ElecRef *r;
    if (elec_ref_count >= (int)(sizeof(elec_refs) / sizeof(elec_refs[0]))) return;
    r = &elec_refs[elec_ref_count++];
    snprintf(r->name, sizeof(r->name), "a380x/xp12/elec/%s", suffix);
    r->kind = kind; r->index = index; r->legend = legend; r->writable = writable; r->real = real; r->handle = NULL;
}
static void elec_build_refs(void)
{
    char name[64];
    int c, n;
    elec_ref_count = 0;
    for (c = 0; c < A380X_ELEC_CONTROL_COUNT; ++c) if (a380x_elec_controls[c].kind != A380X_ELEC_INDICATOR) {
        int drive = c >= A380X_ELEC_DRIVE1 && c <= A380X_ELEC_DRIVE4;
        snprintf(name, sizeof(name), "pb/%s", a380x_elec_controls[c].id); elec_add_ref(name, ERK_PB, c, 0, !drive, 0);
    }
    for (c = 0; c < A380X_ELEC_CONTROL_COUNT; ++c) if (a380x_elec_controls[c].kind != A380X_ELEC_INDICATOR) {
        snprintf(name, sizeof(name), "pb/%s_press_ratio", a380x_elec_controls[c].id); elec_add_ref(name, ERK_PRESSED, c, 0, 0, 1);
    }
    for (c = 0; c < A380X_ELEC_CONTROL_COUNT; ++c) {
        if (a380x_elec_controls[c].seq1 != A380X_ELEC_LEGEND_NONE) {
            snprintf(name, sizeof(name), "lamp/%s_upper", a380x_elec_controls[c].id);
            elec_add_ref(name, ERK_LAMP, c, a380x_elec_controls[c].seq1, 0, 1);
        }
        if (a380x_elec_controls[c].seq2 != A380X_ELEC_LEGEND_NONE) {
            snprintf(name, sizeof(name), "lamp/%s_lower", a380x_elec_controls[c].id);
            elec_add_ref(name, ERK_LAMP, c, a380x_elec_controls[c].seq2, 0, 1);
        }
    }
    elec_add_ref("lamp/elmu_upper", ERK_LAMP, A380X_ELEC_ELMU, A380X_ELEC_LEGEND_FAULT, 0, 1); /* no authored face */
    for (n = 0; n < 4; ++n) { snprintf(name, sizeof(name), "ext/avail%d", n + 1); elec_add_ref(name, ERK_EXT_AVAIL, n, 0, 0, 0); }
    elec_add_ref("ext/connected", ERK_EXT_CONNECTED, 0, 0, 1, 0);
    for (c = 0; c < A380X_ELEC_CONTROL_COUNT; ++c) if (a380x_elec_controls[c].has_fault) {
        snprintf(name, sizeof(name), "fail/%s", a380x_elec_controls[c].id); elec_add_ref(name, ERK_FAIL, c, 0, 1, 0);
    }
    for (n = 0; n < A380X_ELEC_COVER_COUNT; ++n) {
        snprintf(name, sizeof(name), "cover/%s", a380x_elec_cover_ids[n]); elec_add_ref(name, ERK_COVER, n, 0, 1, 1);
    }
    elec_add_ref("ann_lt", ERK_ANN_LT, 0, 0, 1, 0);
    elec_add_ref("lamp_power", ERK_LAMP_POWER, 0, 0, 1, 0);
    elec_add_ref("external_logic", ERK_EXTERNAL, 0, 0, 1, 0);
    elec_add_ref("stock_battery_slots", ERK_SLOTS, 0, 0, 0, 0);
}

/* ---------------------------------------------------------------- commands */
enum { ECA_PRESS, ECA_NORMAL, ECA_OFF, ECA_DRIVE_RESET, ECA_DRIVE_RESET_ALL, ECA_GPU_CONNECT,
       ECA_COVER, ECA_COVER_OPEN, ECA_COVER_CLOSE };
typedef struct { char name[128]; int action, index; XPLMCommandRef handle; } ElecCommand;
static ElecCommand elec_commands[96];
static int elec_command_count;

static void elec_add_command(const char *suffix, int action, int index)
{
    ElecCommand *command;
    if (elec_command_count >= (int)(sizeof(elec_commands) / sizeof(elec_commands[0]))) return;
    command = &elec_commands[elec_command_count++];
    snprintf(command->name, sizeof(command->name), "a380x/xp12/elec/%s", suffix);
    command->action = action; command->index = index; command->handle = NULL;
}
static void elec_build_commands(void)
{
    char name[64];
    int c, n;
    elec_command_count = 0;
    /* <pb>_toggle (latched pb-sw) / <pb>_press (EXT, DRIVE, RAT MAN ON): held like the real button. */
    for (c = 0; c < A380X_ELEC_CONTROL_COUNT; ++c) if (a380x_elec_controls[c].kind != A380X_ELEC_INDICATOR) {
        snprintf(name, sizeof(name), "%s_%s", a380x_elec_controls[c].id, a380x_elec_is_latch(c) ? "toggle" : "press");
        elec_add_command(name, ECA_PRESS, c);
    }
    for (c = 0; c < A380X_ELEC_CONTROL_COUNT; ++c) if (a380x_elec_is_latch(c)) {
        snprintf(name, sizeof(name), "%s_on", a380x_elec_controls[c].id); elec_add_command(name, ECA_NORMAL, c);
        snprintf(name, sizeof(name), "%s_off", a380x_elec_controls[c].id); elec_add_command(name, ECA_OFF, c);
    }
    for (n = 0; n < 4; ++n) { snprintf(name, sizeof(name), "drive%d_reset", n + 1); elec_add_command(name, ECA_DRIVE_RESET, n); }
    elec_add_command("drive_reset_all", ECA_DRIVE_RESET_ALL, 0);
    elec_add_command("gpu_connect_toggle", ECA_GPU_CONNECT, 0);
    for (n = 0; n < A380X_ELEC_COVER_COUNT; ++n) {
        snprintf(name, sizeof(name), "%s_cover_toggle", a380x_elec_cover_ids[n]); elec_add_command(name, ECA_COVER, n);
        snprintf(name, sizeof(name), "%s_cover_open", a380x_elec_cover_ids[n]); elec_add_command(name, ECA_COVER_OPEN, n);
        snprintf(name, sizeof(name), "%s_cover_close", a380x_elec_cover_ids[n]); elec_add_command(name, ECA_COVER_CLOSE, n);
    }
}
static int elec_mouse_y(void)
{
    int x = 0, y = 0;
    XPLMGetMouseLocationGlobal(&x, &y);
    return y;
}
/* Guard: a click toggles it; holding and dragging the mouse down/up swings it with the hand
 * (desktop). On release after a drag it settles open past half travel, otherwise closed. */
#define ELEC_DRAG_THRESHOLD_PX 5
#define ELEC_DRAG_FULL_PX 140.0f
static void elec_cover_command(int cover, XPLMCommandPhase phase)
{
    if (phase == xplm_CommandBegin) {
        elec_io.dragging[cover] = 1; elec_io.drag_active[cover] = 0;
        elec_io.drag_start[cover] = elec.cover[cover];
        elec_io.drag_mouse_y[cover] = elec_mouse_y();
        return;
    }
    if (!elec_io.dragging[cover]) return;
    if (phase == xplm_CommandContinue) {
        int dy = elec_io.drag_mouse_y[cover] - elec_mouse_y();  /* + = mouse moved down = guard swings open */
        if (!elec_io.drag_active[cover] && abs(dy) >= ELEC_DRAG_THRESHOLD_PX) elec_io.drag_active[cover] = 1;
        if (elec_io.drag_active[cover]) a380x_elec_cover_set(&elec, cover, elec_io.drag_start[cover] + (float)dy / ELEC_DRAG_FULL_PX);
        return;
    }
    if (elec_io.drag_active[cover]) a380x_elec_cover_settle(&elec, cover);
    else a380x_elec_cover_toggle(&elec, cover);
    elec_io.dragging[cover] = elec_io.drag_active[cover] = 0;
}
static int elec_command_handler(XPLMCommandRef handle, XPLMCommandPhase phase, void *refcon)
{
    ElecCommand *command = (ElecCommand *)refcon;
    unsigned int events = 0;
    float now = XPLMGetElapsedTime();
    int n;
    (void)handle;
    if (command->action == ECA_PRESS && phase == xplm_CommandEnd) {
        elec_play_events(a380x_elec_release(&elec, command->index)); /* always release, even if disabled */
        return 1;
    }
    if (command->action == ECA_COVER && phase == xplm_CommandEnd) { elec_cover_command(command->index, phase); return 1; }
    if (!plugin_enabled || !elec_io.ready) return 1;
    if (command->action == ECA_COVER) { elec_cover_command(command->index, phase); return 1; }
    if (phase != xplm_CommandBegin) return 1;
    elec_adopt_stock();
    elec_read_inputs();
    switch (command->action) {
    case ECA_PRESS: events = a380x_elec_press(&elec, &elec_inputs, command->index, now); break;
    case ECA_NORMAL: events = a380x_elec_set(&elec, command->index, 1, now) & A380X_ELEC_EVENT_PRESS; break;
    case ECA_OFF: events = a380x_elec_set(&elec, command->index, 0, now) & A380X_ELEC_EVENT_PRESS; break;
    case ECA_DRIVE_RESET: a380x_elec_drive_reset(&elec, &elec_inputs, command->index); break;
    case ECA_DRIVE_RESET_ALL: for (n = 0; n < 4; ++n) a380x_elec_drive_reset(&elec, &elec_inputs, n); break;
    case ECA_GPU_CONNECT: elec_gpu_connect_toggle(); break;
    case ECA_COVER_OPEN: a380x_elec_cover_target(&elec, command->index, 1.0f); break;
    case ECA_COVER_CLOSE: a380x_elec_cover_target(&elec, command->index, 0.0f); break;
    default: break;
    }
    elec_read_inputs();
    elec_drive_stock();
    elec_play_events(events);
    return 1;
}

/* ---------------------------------------------------------------- lifecycle */
static void elec_release_controls(void)
{
    int c, n;
    for (c = 0; c < A380X_ELEC_CONTROL_COUNT; ++c) {
        a380x_elec_release(&elec, c);
        elec.press[c].progress = elec.press[c].position = elec.press[c].hold_until = 0.0f;
    }
    for (n = 0; n < A380X_ELEC_COVER_COUNT; ++n) {
        if (elec_io.drag_active[n]) a380x_elec_cover_settle(&elec, n);
        elec_io.dragging[n] = elec_io.drag_active[n] = 0;
    }
}
/* Aircraft (re)load: guards closed, IDGs connected, latches AUTO, then adopt the stock state. */
static void elec_reset(void)
{
    int n, values[8] = {0};
    a380x_elec_init(&elec);
    memset(elec_io.fault_input, 0, sizeof(elec_io.fault_input));
    memset(elec_io.network_fault, 0, sizeof(elec_io.network_fault));
    elec_io.network_lamps = 0;
    memset(elec_io.dragging, 0, sizeof(elec_io.dragging));
    memset(elec_io.drag_active, 0, sizeof(elec_io.drag_active));
    elec_io.external_logic = 0;
    elec_io.battery_slots = elec_raw_int(EL_NUM_BATTERIES);
    if (elec_io.battery_slots < 0) elec_io.battery_slots = 0;
    XPLMGetDatavi(elec_raw[EL_BATTERY_ON], values, 0, 4);
    for (n = 0; n < 4; ++n) elec_io.seen_battery[n] = !values[n]; /* force adoption */
    XPLMGetDatavi(elec_raw[EL_GENERATOR_ON], values, 0, 4);
    for (n = 0; n < 4; ++n) elec_io.seen_generator[n] = !values[n];
    elec_io.seen_apu_gen = !elec_raw_int(EL_APU_GEN_ON);
    elec_io.seen_gpu = !elec_raw_int(EL_GPU_ON);
    elec_io.ext_connected = 0;
    elec_adopt_stock();
    if (!elec_io.seen_gpu) memset(elec.ext_on, 0, sizeof(elec.ext_on));
    elec_read_inputs();
}
static void elec_plane_loaded(void)
{
    if (!elec_io.ready) return;
    elec_reset();
}
static void elec_shutdown(void)
{
    int i;
    elec_release_controls();
    elec_io.ready = 0;
    if (elec_io.gpu_request) {
        XPLMUnregisterCommandHandler(elec_io.gpu_request, elec_gpu_request, 1, NULL);
        elec_io.gpu_request = NULL;
    }
    for (i = 0; i < elec_command_count; ++i) if (elec_commands[i].handle) {
        XPLMUnregisterCommandHandler(elec_commands[i].handle, elec_command_handler, 1, &elec_commands[i]);
        elec_commands[i].handle = NULL;
    }
    for (i = 0; i < elec_ref_count; ++i) if (elec_refs[i].handle) {
        XPLMUnregisterDataAccessor(elec_refs[i].handle); elec_refs[i].handle = NULL;
    }
    elec_free_sounds();
}
static int elec_init(void)
{
    int i;
    memset(&elec_io, 0, sizeof(elec_io));
    memset(&elec_inputs, 0, sizeof(elec_inputs));
    a380x_elec_init(&elec);
    for (i = 0; i < EL_RAW_COUNT; ++i) {
        elec_raw[i] = XPLMFindDataRef(elec_raw_names[i]);
        if (!elec_raw[i] && i <= EL_GPU_ON) return 0; /* the switches the panel drives are mandatory */
    }
    elec_build_refs();
    elec_build_commands();
    for (i = 0; i < elec_ref_count; ++i) if (XPLMFindDataRef(elec_refs[i].name)) return 0;
    for (i = 0; i < elec_ref_count; ++i) {
        ElecRef *r = &elec_refs[i];
        r->handle = r->real
            ? XPLMRegisterDataAccessor(r->name, xplmType_Float | xplmType_Double, r->writable,
                  NULL, NULL, elec_get_float, r->writable ? elec_set_float : NULL,
                  elec_get_double, r->writable ? elec_set_double : NULL,
                  NULL, NULL, NULL, NULL, NULL, NULL, r, r->writable ? r : NULL)
            : XPLMRegisterDataAccessor(r->name, xplmType_Int, r->writable,
                  elec_get_int, r->writable ? elec_set_int : NULL, NULL, NULL, NULL, NULL,
                  NULL, NULL, NULL, NULL, NULL, NULL, r, r->writable ? r : NULL);
        if (!r->handle) { elec_shutdown(); return 0; }
    }
    for (i = 0; i < elec_command_count; ++i) {
        ElecCommand *command = &elec_commands[i];
        command->handle = XPLMCreateCommand(command->name, command->name);
        if (!command->handle) { elec_shutdown(); return 0; }
        XPLMRegisterCommandHandler(command->handle, elec_command_handler, 1, command);
    }
    elec_io.gpu_request = XPLMFindCommand("sim/ground_ops/toggle_gpu_request");
    if (elec_io.gpu_request) XPLMRegisterCommandHandler(elec_io.gpu_request, elec_gpu_request, 1, NULL);
    elec_load_sounds();
    elec_io.ready = 1;
    elec_reset();
    return 1;
}
