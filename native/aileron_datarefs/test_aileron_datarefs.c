/* Link the real plugin translation unit to these SDK mocks, not a fake header.
 * No simulator is launched. Unmocked SDK calls fail the test executable link.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "XPLMDataAccess.h"
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"

int XPluginStart(char *, char *, char *);
void XPluginStop(void);
int XPluginEnable(void);
void XPluginDisable(void);
void XPluginReceiveMessage(XPLMPluginID, int, void *);

enum { REF_COUNT = 22, RATIO_COUNT = 14 };
static const char *names[REF_COUNT] = {
    "a380x/xp12/visual/aileron_l_inboard_ratio",
    "a380x/xp12/visual/aileron_l_midboard_ratio",
    "a380x/xp12/visual/aileron_l_outboard_ratio",
    "a380x/xp12/visual/aileron_r_inboard_ratio",
    "a380x/xp12/visual/aileron_r_midboard_ratio",
    "a380x/xp12/visual/aileron_r_outboard_ratio",
    "a380x/xp12/hydraulics/aileron_l_inboard_ratio",
    "a380x/xp12/hydraulics/aileron_l_midboard_ratio",
    "a380x/xp12/hydraulics/aileron_l_outboard_ratio",
    "a380x/xp12/hydraulics/aileron_r_inboard_ratio",
    "a380x/xp12/hydraulics/aileron_r_midboard_ratio",
    "a380x/xp12/hydraulics/aileron_r_outboard_ratio",
    "a380x/xp12/hydraulics/elevator_ratio",
    "a380x/xp12/hydraulics/rudder_ratio",
    "a380x/xp12/hydraulics/spoiler_l_inboard_deg",
    "a380x/xp12/hydraulics/spoiler_r_inboard_deg",
    "a380x/xp12/hydraulics/spoiler_l_inner_deg",
    "a380x/xp12/hydraulics/spoiler_r_inner_deg",
    "a380x/xp12/hydraulics/spoiler_l_middle_deg",
    "a380x/xp12/hydraulics/spoiler_r_middle_deg",
    "a380x/xp12/hydraulics/spoiler_l_outer_deg",
    "a380x/xp12/hydraulics/spoiler_r_outer_deg",
};
typedef struct {
    int active;
    XPLMGetDataf_f read;
    XPLMSetDataf_f write;
    void *read_refcon;
    void *write_refcon;
} MockRef;
static MockRef registered[REF_COUNT];
static int find_calls, register_calls, unregister_calls, debug_calls;
static int collision_index = -1, fail_index = -1;
static char name[256], signature[256], description[256];
static int foreign_handle;

XPLMDataRef XPLMFindDataRef(const char *data_name)
{
    int i;
    ++find_calls;
    for (i = 0; i < REF_COUNT; ++i) {
        if (strcmp(data_name, names[i]) == 0) {
            if (i == collision_index)
                return &foreign_handle;
            return registered[i].active ? &registered[i] : NULL;
        }
    }
    assert(!"plugin looked up an unexpected/native dataref");
    return NULL;
}

XPLMDataRef XPLMRegisterDataAccessor(
    const char *data_name, XPLMDataTypeID type, int writable,
    XPLMGetDatai_f read_int, XPLMSetDatai_f write_int,
    XPLMGetDataf_f read_float, XPLMSetDataf_f write_float,
    XPLMGetDatad_f read_double, XPLMSetDatad_f write_double,
    XPLMGetDatavi_f read_ints, XPLMSetDatavi_f write_ints,
    XPLMGetDatavf_f read_floats, XPLMSetDatavf_f write_floats,
    XPLMGetDatab_f read_bytes, XPLMSetDatab_f write_bytes,
    void *read_refcon, void *write_refcon)
{
    int i = register_calls++;
    assert(find_calls == REF_COUNT); /* All collision checks preceded first register. */
    assert(i < REF_COUNT && strcmp(data_name, names[i]) == 0);
    assert(type == xplmType_Float && writable == 1);
    assert(read_float != NULL && write_float != NULL);
    assert(read_refcon != NULL && write_refcon == read_refcon);
    assert(read_int == NULL && write_int == NULL);
    assert(read_double == NULL && write_double == NULL);
    assert(read_ints == NULL && write_ints == NULL);
    assert(read_floats == NULL && write_floats == NULL);
    assert(read_bytes == NULL && write_bytes == NULL);
    if (i == fail_index)
        return NULL;
    registered[i] = (MockRef){1, read_float, write_float, read_refcon, write_refcon};
    assert(read_float(read_refcon) == 0.0f);
    return &registered[i];
}

void XPLMUnregisterDataAccessor(XPLMDataRef ref)
{
    int i;
    /* Reject unregistering a foreign, failed or already-unregistered ref. */
    for (i = 0; i < REF_COUNT; ++i) {
        if (ref == &registered[i]) {
            assert(registered[i].active);
            registered[i].active = 0;
            ++unregister_calls;
            return;
        }
    }
    assert(!"unregistered a dataref not owned by this plugin");
}

void XPLMDebugString(const char *message)
{
    assert(message != NULL);
    ++debug_calls;
}

static void reset_mock(void)
{
    int i;
    XPluginStop();
    for (i = 0; i < REF_COUNT; ++i)
        assert(!registered[i].active);
    memset(registered, 0, sizeof(registered));
    find_calls = register_calls = unregister_calls = debug_calls = 0;
    collision_index = fail_index = -1;
    memset(name, 0, sizeof(name));
    memset(signature, 0, sizeof(signature));
    memset(description, 0, sizeof(description));
}

static void start(void)
{
    assert(XPluginStart(name, signature, description) == 1);
    assert(register_calls == REF_COUNT && find_calls == REF_COUNT);
}

static float get(int i)
{
    assert(registered[i].active);
    return registered[i].read(registered[i].read_refcon);
}

static void set(int i, float value)
{
    assert(registered[i].active);
    registered[i].write(registered[i].write_refcon, value);
}

static void test_early_registration(void)
{
    int i;
    reset_mock();
    assert(XPluginEnable() == 0 && find_calls == 0 && register_calls == 0);
    start();
    /* Simulate attached OBJ resolution immediately after Start, before Enable. */
    for (i = 0; i < REF_COUNT; ++i) {
        assert(XPLMFindDataRef(names[i]) == &registered[i]);
        assert(get(i) == 0.0f);
    }
    assert(XPluginEnable() == 1 && register_calls == REF_COUNT);
    assert(strcmp(signature, "org.a380x.xp12.aileron-visual-datarefs.v1") == 0);
    assert(strlen(name) < 256 && strlen(description) < 256);
}

static void test_independent_cached_values(void)
{
    int i;
    reset_mock(); start();
    for (i = 0; i < REF_COUNT; ++i)
        set(i, (float)(i + 1) / (float)(REF_COUNT + 1));
    for (i = 0; i < REF_COUNT; ++i)
        assert(get(i) == (float)(i + 1) / (float)(REF_COUNT + 1));
    assert(find_calls == REF_COUNT && register_calls == REF_COUNT && unregister_calls == 0);
}

static void test_clamping(void)
{
    int i;
    reset_mock(); start();
    for (i = 0; i < REF_COUNT; ++i) {
        float minimum = i < RATIO_COUNT ? -1.0f : 0.0f;
        float maximum = i < RATIO_COUNT ? 1.0f : 65.0f;
        set(i, -100.0f); assert(get(i) == minimum);
        set(i, 100.0f); assert(get(i) == maximum);
        set(i, minimum); assert(get(i) == minimum);
        set(i, maximum); assert(get(i) == maximum);
        if (i >= RATIO_COUNT) {
            set(i, 32.5f); assert(get(i) == 32.5f);
        }
        set(i, 0.125f); assert(get(i) == 0.125f);
    }
}

static void test_nonfinite_rejected(void)
{
    int i;
    reset_mock(); start();
    for (i = 0; i < REF_COUNT; ++i) {
        set(i, 0.25f);
        set(i, NAN); assert(get(i) == 0.25f);
        set(i, INFINITY); assert(get(i) == 0.25f);
        set(i, -INFINITY); assert(get(i) == 0.25f);
    }
}

static void test_every_collision_prevents_registration(void)
{
    int i;
    for (i = 0; i < REF_COUNT; ++i) {
        reset_mock(); collision_index = i;
        assert(XPluginStart(name, signature, description) == 0);
        assert(find_calls == REF_COUNT && register_calls == 0 && unregister_calls == 0);
        assert(XPluginEnable() == 0);
        XPluginStop(); assert(unregister_calls == 0);
    }
}

static void test_every_partial_failure_rolls_back(void)
{
    int i, j;
    for (i = 0; i < REF_COUNT; ++i) {
        reset_mock(); fail_index = i;
        assert(XPluginStart(name, signature, description) == 0);
        assert(register_calls == i + 1 && unregister_calls == i);
        for (j = 0; j < REF_COUNT; ++j)
            assert(!registered[j].active);
        assert(XPluginEnable() == 0);
        XPluginStop(); assert(unregister_calls == i);
    }
}

static void test_stop_is_idempotent(void)
{
    reset_mock(); start();
    XPluginStop(); assert(unregister_calls == REF_COUNT && XPluginEnable() == 0);
    XPluginStop(); assert(unregister_calls == REF_COUNT);
}

static void test_disable_keeps_refs_neutral(void)
{
    int i;
    reset_mock(); start();
    for (i = 0; i < REF_COUNT; ++i)
        set(i, 0.75f);
    XPluginDisable();
    for (i = 0; i < REF_COUNT; ++i)
        assert(get(i) == 0.0f);
    assert(register_calls == REF_COUNT && unregister_calls == 0 && XPluginEnable() == 1);
}

static void test_duplicate_start_does_not_touch_owned_refs(void)
{
    reset_mock(); start(); set(0, 0.5f);
    assert(XPluginStart(name, signature, description) == 0);
    assert(register_calls == REF_COUNT && find_calls == REF_COUNT && unregister_calls == 0);
    assert(get(0) == 0.5f);
}

static void test_restart_and_messages(void)
{
    reset_mock(); start(); set(3, 0.75f);
    XPluginReceiveMessage(0, 102, NULL);
    assert(get(3) == 0.75f && register_calls == REF_COUNT && find_calls == REF_COUNT);
    reset_mock(); start();
    assert(get(3) == 0.0f);
}

int main(void)
{
    test_early_registration();
    test_independent_cached_values();
    test_clamping();
    test_nonfinite_rejected();
    test_every_collision_prevents_registration();
    test_every_partial_failure_rolls_back();
    test_stop_is_idempotent();
    test_disable_keeps_refs_neutral();
    test_duplicate_start_does_not_touch_owned_refs();
    test_restart_and_messages();
    XPluginStop();
    puts("10 tests passed (including all 22 collision and rollback positions and both ranges).");
    return 0;
}
