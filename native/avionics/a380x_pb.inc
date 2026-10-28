/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Overhead push buttons: Airbus dark cockpit legends and press travel.
 *
 * tools/build_overhead_pushbuttons.py generates the button table (a380x_pb_table.inc) from the FBW
 * cockpit model and patches the panel OBJs:
 *   a380x/xp12/pb/push[i]   0 released .. 1 pushed in, drives the cap and legend press animation;
 *   a380x/xp12/pb/upper[i]  upper legend light level (FAULT amber, OPEN, AVAIL ...);
 *   a380x/xp12/pb/lower[i]  lower legend light level (OFF white, ON blue, MAN, ALTN ...);
 *   a380x/xp12/pb/dark      always 0, keeps the rest of the decal OBJ unlit;
 *   a380x/xp12/pb/state[i]  writable latched (or held, for momentary buttons) state of every button
 *                           that is not tied to an X-Plane system; the cockpit hotspots write it.
 * A button tied to a system (battery, generators, bleeds, packs, anti-ice, fuel pumps, APU, ADIRS)
 * reads "pushed" from that system's dataref instead, so its legend follows the sim.
 *
 * Dark cockpit: with the button pushed in (normal), nothing is lit. OFF legends light white when
 * the button is released; ON/MAN/ALTN/OVRD/DISC legends light when pushed. ANN LT TEST lights
 * every legend; nothing lights without electrical power.
 */

typedef struct {
    const char *node, *source;
    int source_index, mode;       /* mode: 0 own state, 1 pushed = value != 0, 2 value == 0, 3 value == 2 */
    const char *fault;
    int upper_kind, lower_kind;   /* see PB_UPPER_* / PB_LOWER_* */
    int momentary, initial;
} PbButton;

enum { PB_UPPER_NONE, PB_UPPER_FAULT, PB_UPPER_OPEN, PB_UPPER_EXT_AVAIL, PB_UPPER_APU_AVAIL };
enum { PB_LOWER_NONE, PB_LOWER_OFF, PB_LOWER_ON, PB_LOWER_APU_START };

#include "a380x_pb_table.inc"

static struct {
    float state[PB_COUNT], push[PB_COUNT], upper[PB_COUNT], lower[PB_COUNT];
    XPLMDataRef source[PB_COUNT], fault[PB_COUNT];
    XPLMDataRef bus_volts, ann_test, apu_n1, on_ground, ground_speed;
    XPLMDataRef published[5];
    int ready;
} pb;

static int pb_read_vf(const void *values, int offset, int max, int count)
{
    int n;
    if (!values) return count;
    if (offset < 0 || offset >= count) return 0;
    n = count - offset < max ? count - offset : max;
    return n > 0 ? n : 0;
}

#define PB_ARRAY_ACCESSORS(field)                                                                   \
    static int pb_get_##field##_vf(void *refcon, float *values, int offset, int max)               \
    {                                                                                               \
        int n = pb_read_vf(values, offset, max, PB_COUNT);                                         \
        (void)refcon;                                                                               \
        if (values && n > 0) memcpy(values, pb.field + offset, (size_t)n * sizeof(float));          \
        return n;                                                                                   \
    }                                                                                               \
    static int pb_get_##field##_vi(void *refcon, int *values, int offset, int max)                 \
    {                                                                                               \
        int i, n = pb_read_vf(values, offset, max, PB_COUNT);                             \
        (void)refcon;                                                                               \
        if (values) for (i = 0; i < n; ++i) values[i] = (int)lroundf(pb.field[offset + i]);         \
        return n;                                                                                   \
    }
PB_ARRAY_ACCESSORS(state)
PB_ARRAY_ACCESSORS(push)
PB_ARRAY_ACCESSORS(upper)
PB_ARRAY_ACCESSORS(lower)

static void pb_set_state_vf(void *refcon, float *values, int offset, int count)
{
    int i;
    (void)refcon;
    for (i = 0; i < count && offset + i < PB_COUNT; ++i)
        if (offset + i >= 0 && isfinite(values[i])) pb.state[offset + i] = values[i] != 0.0f ? 1.0f : 0.0f;
}

static void pb_set_state_vi(void *refcon, int *values, int offset, int count)
{
    int i;
    (void)refcon;
    for (i = 0; i < count && offset + i < PB_COUNT; ++i)
        if (offset + i >= 0) pb.state[offset + i] = values[i] != 0 ? 1.0f : 0.0f;
}

static float pb_get_dark(void *refcon) { (void)refcon; return 0.0f; }

static float pb_read(XPLMDataRef ref, int index)
{
    if (!ref) return 0.0f;
    if (index < 0) {
        return (XPLMGetDataRefTypes(ref) & xplmType_Float) ? XPLMGetDataf(ref) : (float)XPLMGetDatai(ref);
    }
    if (XPLMGetDataRefTypes(ref) & xplmType_FloatArray) {
        float v = 0.0f;
        XPLMGetDatavf(ref, &v, index, 1);
        return v;
    } else {
        int v = 0;
        XPLMGetDatavi(ref, &v, index, 1);
        return (float)v;
    }
}

static void pb_shutdown(void)
{
    int i;
    for (i = 0; i < ARRAY_LEN(pb.published); ++i)
        if (pb.published[i]) { XPLMUnregisterDataAccessor(pb.published[i]); pb.published[i] = NULL; }
    pb.ready = 0;
}

static XPLMDataRef pb_register_array(const char *name, XPLMGetDatavi_f get_i, XPLMGetDatavf_f get_f,
                                     XPLMSetDatavi_f set_i, XPLMSetDatavf_f set_f)
{
    if (XPLMFindDataRef(name)) return NULL;
    return XPLMRegisterDataAccessor(name, xplmType_IntArray | xplmType_FloatArray, set_f != NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, get_i, set_i, get_f, set_f, NULL, NULL, NULL, NULL);
}

/* Never fatal: missing sim datarefs leave a button on its own state, missing faults stay dark. */
static void pb_init(void)
{
    int i;
    memset(&pb, 0, sizeof(pb));
    for (i = 0; i < PB_COUNT; ++i) {
        const PbButton *b = &pb_buttons[i];
        pb.state[i] = (float)b->initial;
        if (b->mode != 0) pb.source[i] = XPLMFindDataRef(b->source);
        if (b->fault[0]) pb.fault[i] = XPLMFindDataRef(b->fault);
    }
    pb.bus_volts = XPLMFindDataRef("sim/cockpit2/electrical/bus_volts");
    pb.ann_test = XPLMFindDataRef("sim/cockpit/warnings/annunciator_test_pressed");
    pb.apu_n1 = XPLMFindDataRef("sim/cockpit2/electrical/APU_N1_percent");
    pb.on_ground = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    pb.ground_speed = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
    pb.published[0] = pb_register_array("a380x/xp12/pb/state", pb_get_state_vi, pb_get_state_vf,
                                        pb_set_state_vi, pb_set_state_vf);
    pb.published[1] = pb_register_array("a380x/xp12/pb/push", pb_get_push_vi, pb_get_push_vf, NULL, NULL);
    pb.published[2] = pb_register_array("a380x/xp12/pb/upper", pb_get_upper_vi, pb_get_upper_vf, NULL, NULL);
    pb.published[3] = pb_register_array("a380x/xp12/pb/lower", pb_get_lower_vi, pb_get_lower_vf, NULL, NULL);
    if (!XPLMFindDataRef("a380x/xp12/pb/dark"))
        pb.published[4] = XPLMRegisterDataAccessor("a380x/xp12/pb/dark", xplmType_Float, 0,
            NULL, NULL, pb_get_dark, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    pb.ready = 1;
}

static void pb_update(void)
{
    float volts[8] = {0}, apu_n1, bus = 0.0f;
    int i, n, test, ext_avail;
    if (!pb.ready) return;
    n = pb.bus_volts ? XPLMGetDatavf(pb.bus_volts, volts, 0, 8) : 0;
    for (i = 0; i < n; ++i) bus = volts[i] > bus ? volts[i] : bus;
    test = pb.ann_test && XPLMGetDatai(pb.ann_test) != 0;
    apu_n1 = pb.apu_n1 ? XPLMGetDataf(pb.apu_n1) : 0.0f;
    /* X-Plane has no GPU connection state: offer it while parked. */
    ext_avail = pb.on_ground && XPLMGetDatai(pb.on_ground) && pb.ground_speed && XPLMGetDataf(pb.ground_speed) < 1.0f;
    for (i = 0; i < PB_COUNT; ++i) {
        const PbButton *b = &pb_buttons[i];
        float value = pb.source[i] ? pb_read(pb.source[i], b->source_index) : 0.0f;
        int pushed, upper = 0, lower = 0;
        switch (pb.source[i] ? b->mode : 0) {
        case 1: pushed = value != 0.0f; break;
        case 2: pushed = value == 0.0f; break;
        case 3: pushed = lroundf(value) == 2; break;
        default: pushed = pb.state[i] != 0.0f; break;
        }
        switch (b->upper_kind) {
        case PB_UPPER_FAULT: upper = pb.fault[i] && pb_read(pb.fault[i], -1) != 0.0f; break;
        case PB_UPPER_OPEN: upper = pushed; break;
        case PB_UPPER_EXT_AVAIL: upper = ext_avail && !pushed; break;
        case PB_UPPER_APU_AVAIL: upper = apu_n1 > 95.0f; break;
        default: break;
        }
        switch (b->lower_kind) {
        case PB_LOWER_OFF: lower = !pushed; break;
        case PB_LOWER_ON: lower = pushed; break;
        case PB_LOWER_APU_START: lower = pushed || (lroundf(value) == 1 && apu_n1 > 1.0f && apu_n1 < 95.0f); break;
        default: break;
        }
        if (test) upper = lower = 1;
        if (bus < 18.0f) upper = lower = 0;
        pb.push[i] = pushed ? 1.0f : 0.0f;
        pb.upper[i] = upper ? 1.0f : 0.0f;
        pb.lower[i] = lower ? 1.0f : 0.0f;
    }
}
