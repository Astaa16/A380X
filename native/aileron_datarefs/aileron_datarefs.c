/* Aircraft-scoped OBJ animation datarefs; no simulator-state dependencies.
 * Register in XPluginStart, before X-Plane resolves attached OBJ datarefs:
 * https://developer.x-plane.com/article/deferredinitialization/
 *
 * This plugin only owns cached Float values. A separate visual driver may
 * find/write them after startup. It never controls flight physics.
 */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "XPLMDataAccess.h"
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"

typedef struct {
    const char *name;
    float value;
    float minimum;
    float maximum;
    XPLMDataRef handle;
} VisualRef;

#define REF(prefix) {"a380x/xp12/visual/aileron_" prefix "_ratio", 0.0f, -1.0f, 1.0f, NULL}
#define HYD_REF(name) {"a380x/xp12/hydraulics/" name "_ratio", 0.0f, -1.0f, 1.0f, NULL}
#define SPOILER_REF(name) {"a380x/xp12/hydraulics/spoiler_" name "_deg", 0.0f, 0.0f, 65.0f, NULL}
static VisualRef refs[] = {
    REF("l_inboard"), REF("l_midboard"), REF("l_outboard"),
    REF("r_inboard"), REF("r_midboard"), REF("r_outboard"),
    HYD_REF("aileron_l_inboard"), HYD_REF("aileron_l_midboard"), HYD_REF("aileron_l_outboard"),
    HYD_REF("aileron_r_inboard"), HYD_REF("aileron_r_midboard"), HYD_REF("aileron_r_outboard"),
    HYD_REF("elevator"), HYD_REF("rudder"),
    SPOILER_REF("l_inboard"), SPOILER_REF("r_inboard"),
    SPOILER_REF("l_inner"), SPOILER_REF("r_inner"),
    SPOILER_REF("l_middle"), SPOILER_REF("r_middle"),
    SPOILER_REF("l_outer"), SPOILER_REF("r_outer")
};
#undef REF
#undef HYD_REF
#undef SPOILER_REF
enum { REF_COUNT = sizeof(refs) / sizeof(refs[0]) };
static int started = 0;

static float read_value(void *refcon)
{
    const VisualRef *ref = (const VisualRef *)refcon;
    return ref != NULL ? ref->value : 0.0f;
}

static void write_value(void *refcon, float value)
{
    VisualRef *ref = (VisualRef *)refcon;
    /* Reject NaN/infinity and retain the last valid pose. Never export NaN. */
    if (ref == NULL || !isfinite(value))
        return;
    ref->value = value < ref->minimum ? ref->minimum : value > ref->maximum ? ref->maximum : value;
}

static void clear_values(void)
{
    int i;
    for (i = 0; i < REF_COUNT; ++i)
        refs[i].value = 0.0f;
}

static void unregister_owned(void)
{
    int i;
    for (i = 0; i < REF_COUNT; ++i) {
        if (refs[i].handle != NULL) {
            XPLMUnregisterDataAccessor(refs[i].handle);
            refs[i].handle = NULL;
        }
    }
    clear_values();
    started = 0;
}

PLUGIN_API int XPluginStart(char *name, char *signature, char *description)
{
    int i;
    int collision = 0;
    strcpy(name, "A380X Control Surface Visual Datarefs");
    strcpy(signature, "org.a380x.xp12.aileron-visual-datarefs.v1");
    strcpy(description, "Early aircraft OBJ visual datarefs; cached floats only.");
    if (started) {
        XPLMDebugString("A380X Aileron Datarefs: duplicate start refused.\n");
        return 0;
    }

    /* Preflight ALL names before registering ANY, regardless of owner/type.
     * Do not borrow/unregister another provider's references. A late Lua
     * provider must bind to this complete set, not create a competing set.
     */
    for (i = 0; i < REF_COUNT; ++i) {
        if (XPLMFindDataRef(refs[i].name) != NULL) {
            XPLMDebugString("A380X Aileron Datarefs: existing dataref collision: ");
            XPLMDebugString(refs[i].name);
            XPLMDebugString("\n");
            collision = 1;
        }
    }
    if (collision)
        return 0;

    clear_values();
    for (i = 0; i < REF_COUNT; ++i) {
        refs[i].handle = XPLMRegisterDataAccessor(
            refs[i].name, xplmType_Float, 1,
            NULL, NULL, read_value, write_value,
            NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
            &refs[i], &refs[i]);
        if (refs[i].handle == NULL) {
            XPLMDebugString("A380X Aileron Datarefs: registration failed; rolling back.\n");
            unregister_owned();
            return 0;
        }
    }
    started = 1;
    XPLMDebugString("A380X Aileron Datarefs: 22 writable Floats registered in XPluginStart.\n");
    return 1;
}

PLUGIN_API void XPluginStop(void)
{
    unregister_owned();
}

PLUGIN_API int XPluginEnable(void)
{
    return started;
}

PLUGIN_API void XPluginDisable(void)
{
    /* Keep ownership until Stop; resume from neutral, not an old visual pose. */
    clear_values();
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, int message, void *param)
{
    (void)from;
    (void)message;
    (void)param;
}

#if IBM
BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
#endif
