/* SPDX-License-Identifier: GPL-3.0-or-later
 * Detented cockpit lever with click sound: X-Plane side of a380x_detent_lever.h, shared by the
 * thrust and reverse levers (a380x_thrust.inc) and meant for the flaps lever and the speed brake.
 *
 *  - CockpitLeverInput: the writable float/double dataref an OBJ drag manipulator writes (the
 *    hand). Reads return the owner's value (hand while dragging, lever otherwise), so the next
 *    click-hold starts where the lever is shown; writes are handed to the owner (stickiness,
 *    interlocks). Writes while the plugin is disabled are ignored.
 *  - CockpitSound: a short mono PCM16 WAV from the plugin's sounds/ folder, played on X-Plane's
 *    interior bus at a cockpit position given relative to the pilot's eye point (aircraft axes,
 *    metres; the eye follows acf_peX/Y/Z), never while paused, in replay or with the plugin disabled.
 * Unity-build include of a380x_avionics.c (needs plugin_enabled); requires XPLM400 (XPLMSound).
 */
#ifndef A380X_COCKPIT_LEVER_INC
#define A380X_COCKPIT_LEVER_INC
#include "XPLMSound.h"

typedef struct {
    const char *name;
    double (*get)(void *refcon);
    void (*set)(void *refcon, double value);
    void *refcon;
    XPLMDataRef handle;
} CockpitLeverInput;

static float cockpit_lever_get_f(void *refcon)
{
    CockpitLeverInput *in = (CockpitLeverInput *)refcon;
    return (float)in->get(in->refcon);
}
static double cockpit_lever_get_d(void *refcon)
{
    CockpitLeverInput *in = (CockpitLeverInput *)refcon;
    return in->get(in->refcon);
}
static void cockpit_lever_set_d(void *refcon, double value)
{
    CockpitLeverInput *in = (CockpitLeverInput *)refcon;
    if (!plugin_enabled || !isfinite(value)) return;
    in->set(in->refcon, value);
}
static void cockpit_lever_set_f(void *refcon, float value) { cockpit_lever_set_d(refcon, (double)value); }

static int cockpit_lever_input_register(CockpitLeverInput *in)
{
    if (XPLMFindDataRef(in->name)) return 0;
    in->handle = XPLMRegisterDataAccessor(in->name, xplmType_Float | xplmType_Double, 1, NULL, NULL,
        cockpit_lever_get_f, cockpit_lever_set_f, cockpit_lever_get_d, cockpit_lever_set_d,
        NULL, NULL, NULL, NULL, NULL, NULL, in, in);
    return in->handle != NULL;
}
static void cockpit_lever_input_unregister(CockpitLeverInput *in)
{
    if (in->handle) XPLMUnregisterDataAccessor(in->handle);
    in->handle = NULL;
}

/* ---------------------------------------------------------------- click sounds */
typedef struct { short *data; uint32_t bytes; int rate; } CockpitSound;

enum { CS_LOCAL_X, CS_LOCAL_Y, CS_LOCAL_Z, CS_PSI, CS_THETA, CS_PHI, CS_PE_X, CS_PE_Y, CS_PE_Z,
       CS_VIEW_EXTERNAL, CS_VIEW_X, CS_VIEW_Y, CS_VIEW_Z, CS_PAUSED, CS_REPLAY, CS_RAW_COUNT };
static const char *const cockpit_sound_raw_names[CS_RAW_COUNT] = {
    "sim/flightmodel/position/local_x", "sim/flightmodel/position/local_y", "sim/flightmodel/position/local_z",
    "sim/flightmodel/position/psi", "sim/flightmodel/position/theta", "sim/flightmodel/position/phi",
    "sim/aircraft/view/acf_peX", "sim/aircraft/view/acf_peY", "sim/aircraft/view/acf_peZ",
    "sim/graphics/view/view_is_external", "sim/graphics/view/view_x", "sim/graphics/view/view_y",
    "sim/graphics/view/view_z", "sim/time/paused", "sim/time/is_in_replay",
};
static XPLMDataRef cockpit_sound_raw[CS_RAW_COUNT];

static void cockpit_sound_bind(void)
{
    int i;
    for (i = 0; i < CS_RAW_COUNT; ++i)
        if (!cockpit_sound_raw[i]) cockpit_sound_raw[i] = XPLMFindDataRef(cockpit_sound_raw_names[i]);
}
static double cockpit_sound_value(int r)
{
    double v;
    if (!cockpit_sound_raw[r]) return 0.0;
    v = (XPLMGetDataRefTypes(cockpit_sound_raw[r]) & xplmType_Double) ? XPLMGetDatad(cockpit_sound_raw[r])
        : (XPLMGetDataRefTypes(cockpit_sound_raw[r]) & xplmType_Int) ? (double)XPLMGetDatai(cockpit_sound_raw[r])
        : (double)XPLMGetDataf(cockpit_sound_raw[r]);
    return isfinite(v) ? v : 0.0;
}

static uint32_t cockpit_le32(const unsigned char *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static unsigned cockpit_le16(const unsigned char *p) { return p[0] | (p[1] << 8); }

/* Strict RIFF/WAVE reader: PCM (format 1), mono, 16-bit, 8..96 kHz, at most 2 s of samples. */
static int cockpit_sound_parse_wav(CockpitSound *s, const unsigned char *b, size_t n)
{
    size_t at = 12;
    int have_format = 0;
    memset(s, 0, sizeof(*s));
    if (n < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WAVE", 4)) return 0;
    while (at + 8 <= n) {
        uint32_t size = cockpit_le32(b + at + 4);
        const unsigned char *chunk = b + at + 8;
        if (size > n - at - 8) return 0;
        if (!memcmp(b + at, "fmt ", 4)) {
            if (size < 16 || cockpit_le16(chunk) != 1 || cockpit_le16(chunk + 2) != 1
                || cockpit_le16(chunk + 14) != 16) return 0;
            s->rate = (int)cockpit_le32(chunk + 4);
            if (s->rate < 8000 || s->rate > 96000) return 0;
            have_format = 1;
        } else if (!memcmp(b + at, "data", 4)) {
            if (!have_format || !size || (size & 1) || size > (uint32_t)s->rate * 4u) return 0;
            s->data = (short *)malloc(size);
            if (!s->data) return 0;
            memcpy(s->data, chunk, size);   /* little-endian PCM16 on every supported platform */
            s->bytes = size;
            return 1;
        }
        at += 8 + size + (size & 1);
    }
    return 0;
}

/* Loads <plugin folder>/sounds/<file>; a missing or invalid file leaves the sound silent. */
static int cockpit_sound_load(CockpitSound *s, const char *file)
{
    char root[1024], path[1200];
    unsigned char *buffer;
    FILE *f;
    long length;
    int i, ok = 0;
    memset(s, 0, sizeof(*s));
    root[0] = 0;
    XPLMGetPluginInfo(XPLMGetMyID(), NULL, root, NULL, NULL);
    for (i = 0; i < 2; ++i) { /* .../A380XAvionics/<platform>/A380XAvionics.xpl -> .../A380XAvionics */
        char *cut = strrchr(root, '/');
        if (!cut) cut = strrchr(root, '\\');
        if (!cut) return 0;
        *cut = 0;
    }
    if (snprintf(path, sizeof(path), "%s/sounds/%s", root, file) >= (int)sizeof(path)) return 0;
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) == 0 && (length = ftell(f)) > 44 && length <= 400000 && fseek(f, 0, SEEK_SET) == 0) {
        buffer = (unsigned char *)malloc((size_t)length);
        if (buffer && fread(buffer, 1, (size_t)length, f) == (size_t)length)
            ok = cockpit_sound_parse_wav(s, buffer, (size_t)length);
        free(buffer);
    }
    fclose(f);
    return ok;
}
static void cockpit_sound_free(CockpitSound *s) { free(s->data); memset(s, 0, sizeof(*s)); }

/* World (OpenGL local) position of a cockpit point given relative to the eye in aircraft axes
 * (x right, y up, z aft); 0 when it cannot be placed credibly (then the sound plays unplaced). */
static int cockpit_sound_world(const float from_eye[3], FMOD_VECTOR *out)
{
    double b[3], v1[3], v2[3], c, s, view[3];
    double psi = cockpit_sound_value(CS_PSI) * 0.017453292519943295, theta = cockpit_sound_value(CS_THETA) * 0.017453292519943295;
    double phi = cockpit_sound_value(CS_PHI) * 0.017453292519943295;
    if (!cockpit_sound_raw[CS_LOCAL_X] || !cockpit_sound_raw[CS_LOCAL_Y] || !cockpit_sound_raw[CS_LOCAL_Z]) return 0;
    b[0] = cockpit_sound_value(CS_PE_X) + from_eye[0];
    b[1] = cockpit_sound_value(CS_PE_Y) + from_eye[1];
    b[2] = cockpit_sound_value(CS_PE_Z) + from_eye[2];
    /* X-Plane aircraft drawing: Ry(-psi) * Rx(theta) * Rz(-phi) (as a380x_elec_panel.inc). */
    c = cos(-phi); s = sin(-phi);
    v1[0] = c * b[0] - s * b[1]; v1[1] = s * b[0] + c * b[1]; v1[2] = b[2];
    c = cos(theta); s = sin(theta);
    v2[0] = v1[0]; v2[1] = c * v1[1] - s * v1[2]; v2[2] = s * v1[1] + c * v1[2];
    c = cos(-psi); s = sin(-psi);
    out->x = (float)(cockpit_sound_value(CS_LOCAL_X) + c * v2[0] + s * v2[2]);
    out->y = (float)(cockpit_sound_value(CS_LOCAL_Y) + v2[1]);
    out->z = (float)(cockpit_sound_value(CS_LOCAL_Z) - s * v2[0] + c * v2[2]);
    if (!isfinite(out->x) || !isfinite(out->y) || !isfinite(out->z)) return 0;
    if (cockpit_sound_value(CS_VIEW_EXTERNAL) != 0.0) return 1;
    view[0] = out->x - cockpit_sound_value(CS_VIEW_X); view[1] = out->y - cockpit_sound_value(CS_VIEW_Y);
    view[2] = out->z - cockpit_sound_value(CS_VIEW_Z);
    return sqrt(view[0] * view[0] + view[1] * view[1] + view[2] * view[2]) < 4.0;
}

/* Sounds only for the running, un-paused aircraft (never in replay). */
static int cockpit_sound_allowed(void)
{
    return plugin_enabled && cockpit_sound_value(CS_PAUSED) == 0.0 && cockpit_sound_value(CS_REPLAY) == 0.0;
}

static int cockpit_sound_play(const CockpitSound *s, const float from_eye[3], float volume)
{
    FMOD_CHANNEL *channel;
    FMOD_VECTOR position, velocity = {0.0f, 0.0f, 0.0f};
    if (!s->data || !cockpit_sound_allowed()) return 0;
    channel = XPLMPlayPCMOnBus(s->data, s->bytes, FMOD_SOUND_FORMAT_PCM16, s->rate, 1, 0,
                               xplm_AudioInterior, NULL, NULL);
    if (!channel) return 0;
    /* Sounds start on the next audio refresh, so placing them now is valid. */
    if (cockpit_sound_world(from_eye, &position)) {
        XPLMSetAudioPosition(channel, &position, &velocity);
        XPLMSetAudioFadeDistance(channel, 0.5f, 12.0f);
    }
    XPLMSetAudioVolume(channel, volume);
    return 1;
}

#endif /* A380X_COCKPIT_LEVER_INC */
