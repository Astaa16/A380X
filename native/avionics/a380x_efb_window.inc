/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Native X-Plane host for the A380 control panel. Included by a380x_efb.c
 * after the shared panel drawing and hit-testing functions are declared.
 */
#include <string.h>

/* The current SDK moved the window decoration member behind XPLM301.
 * X-Plane 12 supports this API; opt in before the display header is read.
 */
#ifndef XPLM301
#define XPLM301 1
#endif
#include "XPLMDisplay.h"
#include "XPLMMenus.h"
#include "XPLMUtilities.h"

typedef struct {
    const char *name;
    const char *description;
    int visibility; /* -1 toggles; 0 closes; 1 opens. */
    XPLMCommandRef ref;
} EfbWindowCommand;

static XPLMWindowID efb_native_window;
static XPLMMenuID efb_native_menu;
static int efb_window_press_active;
static EfbWindowCommand efb_window_commands[] = {
    {"a380x/xp12/efb/window_toggle", "A380 Control Panel: open or close", -1, NULL},
    {"a380x/xp12/efb/window_open", "A380 Control Panel: open", 1, NULL},
    {"a380x/xp12/efb/window_close", "A380 Control Panel: close", 0, NULL}
};

static void efb_window_set_visible(int visible)
{
    if (efb_native_window == NULL) return;
    efb_window_press_active = 0;
    XPLMSetWindowIsVisible(efb_native_window, visible != 0);
    if (visible) XPLMBringWindowToFront(efb_native_window);
}

static void efb_window_draw_callback(XPLMWindowID window, void *refcon)
{
    int left, top, right, bottom;
    (void)refcon;
    XPLMGetWindowGeometry(window, &left, &top, &right, &bottom);
    if (right > left && top > bottom)
        a380x_iscs_draw_window(left, bottom, right, top);
}

static int efb_window_mouse_callback(XPLMWindowID window, int x, int y,
                                     XPLMMouseStatus status, void *refcon)
{
    int left, top, right, bottom;
    (void)refcon;
    if (status == xplm_MouseUp) {
        efb_window_press_active = 0;
    } else if (status == xplm_MouseDown && !efb_window_press_active) {
        efb_window_press_active = 1;
        XPLMGetWindowGeometry(window, &left, &top, &right, &bottom);
        if (right > left && top > bottom && x >= left && x <= right &&
            y >= bottom && y <= top) {
            /* Client and atlas coordinates both increase upwards. The
             * desktop window never inherits the cockpit screen's UV flip.
             */
            const float atlas_x = 152.0f + (float)(x - left) * 720.0f /
                                                    (float)(right - left);
            const float atlas_y = 516.0f + (float)(y - bottom) * 500.0f /
                                                    (float)(top - bottom);
            a380x_iscs_click(atlas_x, atlas_y);
        }
    }
    /* Consume the whole gesture, including drags and release. A held click
     * must not toggle a button twice or drag the cockpit camera underneath.
     */
    return 1;
}

static int efb_window_right_mouse_callback(XPLMWindowID window, int x, int y,
                                           XPLMMouseStatus status, void *refcon)
{
    (void)window; (void)x; (void)y; (void)status; (void)refcon;
    return 1;
}

static void efb_window_key_callback(XPLMWindowID window, char key,
                                    XPLMKeyFlags flags, char virtual_key,
                                    void *refcon, int losing_focus)
{
    (void)window; (void)key; (void)flags; (void)virtual_key;
    (void)refcon; (void)losing_focus;
}

static XPLMCursorStatus efb_window_cursor_callback(XPLMWindowID window,
                                                  int x, int y, void *refcon)
{
    (void)window; (void)x; (void)y; (void)refcon;
    return xplm_CursorArrow;
}

static int efb_window_wheel_callback(XPLMWindowID window, int x, int y,
                                     int wheel, int clicks, void *refcon)
{
    (void)window; (void)x; (void)y; (void)wheel; (void)clicks; (void)refcon;
    return 1;
}

static int efb_window_command_callback(XPLMCommandRef command,
                                       XPLMCommandPhase phase, void *refcon)
{
    EfbWindowCommand *entry = (EfbWindowCommand *)refcon;
    (void)command;
    if (phase == xplm_CommandBegin && efb_native_window != NULL) {
        const int visible = entry->visibility < 0 ?
            !XPLMGetWindowIsVisible(efb_native_window) : entry->visibility;
        efb_window_set_visible(visible);
    }
    return 0;
}

static void efb_window_menu_callback(void *menu_ref, void *item_ref)
{
    (void)menu_ref; (void)item_ref;
    if (efb_native_window != NULL)
        efb_window_set_visible(!XPLMGetWindowIsVisible(efb_native_window));
}

static void efb_window_shutdown(void)
{
    unsigned i;
    for (i = 0; i < sizeof(efb_window_commands) / sizeof(efb_window_commands[0]); ++i) {
        EfbWindowCommand *entry = &efb_window_commands[i];
        if (entry->ref != NULL) {
            XPLMUnregisterCommandHandler(entry->ref, efb_window_command_callback,
                                         1, entry);
            entry->ref = NULL;
        }
    }
    if (efb_native_menu != NULL) {
        XPLMDestroyMenu(efb_native_menu);
        efb_native_menu = NULL;
    }
    if (efb_native_window != NULL) {
        XPLMDestroyWindow(efb_native_window);
        efb_native_window = NULL;
    }
    efb_window_press_active = 0;
}

static int efb_window_init(void)
{
    XPLMCreateWindow_t window;
    int screen_left, screen_top, screen_right, screen_bottom;
    unsigned i;
    if (efb_native_window != NULL) return 1;
    memset(&window, 0, sizeof(window));
    XPLMGetScreenBoundsGlobal(&screen_left, &screen_top, &screen_right, &screen_bottom);
    window.structSize = sizeof(window);
    window.left = screen_left + (screen_right - screen_left - 720) / 2;
    window.bottom = screen_bottom + (screen_top - screen_bottom - 500) / 2;
    window.right = window.left + 720;
    window.top = window.bottom + 500;
    window.visible = 0;
    window.drawWindowFunc = efb_window_draw_callback;
    window.handleMouseClickFunc = efb_window_mouse_callback;
    window.handleKeyFunc = efb_window_key_callback;
    window.handleCursorFunc = efb_window_cursor_callback;
    window.handleMouseWheelFunc = efb_window_wheel_callback;
    window.handleRightClickFunc = efb_window_right_mouse_callback;
    window.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
    window.layer = xplm_WindowLayerFloatingWindows;
    efb_native_window = XPLMCreateWindowEx(&window);
    if (efb_native_window == NULL) return 0;
    XPLMSetWindowPositioningMode(efb_native_window, xplm_WindowPositionFree, -1);
    XPLMSetWindowGravity(efb_native_window, 0.5f, 0.5f, 0.5f, 0.5f);
    XPLMSetWindowResizingLimits(efb_native_window, 720, 500, 1440, 1000);
    XPLMSetWindowTitle(efb_native_window, "A380 ISCS");

    efb_native_menu = XPLMCreateMenu("ISCS", NULL, 0,
                                    efb_window_menu_callback, NULL);
    if (efb_native_menu == NULL) goto failed;
    if (XPLMAppendMenuItem(efb_native_menu, "Open / Close", NULL, 0) < 0) goto failed;
    for (i = 0; i < sizeof(efb_window_commands) / sizeof(efb_window_commands[0]); ++i) {
        EfbWindowCommand *entry = &efb_window_commands[i];
        entry->ref = XPLMCreateCommand(entry->name, entry->description);
        if (entry->ref == NULL) goto failed;
        XPLMRegisterCommandHandler(entry->ref, efb_window_command_callback, 1, entry);
    }
    return 1;

failed:
    efb_window_shutdown();
    return 0;
}
