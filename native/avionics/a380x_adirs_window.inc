/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native ADIRS source selection and manual ATT magnetic-heading entry.
 * Included after the ADIRS state and guarded control helpers in the adapter.
 */
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMMenus.h"
#include "XPLMUtilities.h"

static XPLMWindowID adirs_native_window;
static XPLMMenuID adirs_native_menu;
static XPLMCommandRef adirs_window_command;
static int adirs_window_focus = -1;
static int adirs_window_replace;
static char adirs_window_heading[3][12];
static char adirs_window_message[80];
static int adirs_window_message_error;
static int adirs_window_mouse_down;
static int adirs_window_held_unit = -1;
static int adirs_window_held_ir;

static void adirs_window_release_button(void)
{
    if (adirs_window_held_unit >= 0)
        adirs_set_button(adirs_window_held_unit, adirs_window_held_ir, 0);
    adirs_window_held_unit = -1;
}

static void adirs_window_cancel_pointer(void)
{
    adirs_window_release_button();
    adirs_window_mouse_down = 0;
}

static void adirs_window_release_focus(void)
{
    adirs_window_focus = -1;
    adirs_window_replace = 0;
    if (adirs_native_window && XPLMHasKeyboardFocus(adirs_native_window))
        XPLMTakeKeyboardFocus(NULL);
}

static void adirs_window_set_visible(int visible)
{
    if (!adirs_native_window) return;
    if (!visible) {
        adirs_window_cancel_pointer();
        adirs_window_release_focus();
    }
    XPLMSetWindowIsVisible(adirs_native_window, visible != 0);
    if (visible) XPLMBringWindowToFront(adirs_native_window);
}

static void adirs_window_text(int x, int y, const char *text, int tone)
{
    float color[3] = { .90f, .95f, .97f };
    if (tone == 1) { color[0] = .30f; color[1] = .83f; color[2] = .94f; }
    if (tone == 2) { color[0] = 1.f; color[1] = .72f; color[2] = .22f; }
    if (tone == 3) { color[0] = .48f; color[1] = .58f; color[2] = .64f; }
    XPLMDrawString(color, x, y, (char *)text, NULL, xplmFont_Basic);
}

static void adirs_window_rect(int left, int bottom, int right, int top,
                              float r, float g, float b)
{
    XPLMSetGraphicsState(0, 0, 0, 0, 0, 0, 0);
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    glVertex2i(left, bottom); glVertex2i(right, bottom);
    glVertex2i(right, top); glVertex2i(left, top);
    glEnd();
}

static void adirs_window_button(int left, int bottom, int width,
                                const char *label, int selected, int enabled)
{
    adirs_window_rect(left, bottom, left + width, bottom + 26,
                       selected ? .08f : .12f, selected ? .35f : .20f,
                       selected ? .43f : .25f);
    adirs_window_text(left + 8, bottom + 9, label, enabled ? (selected ? 1 : 0) : 3);
}

static int adirs_window_hit(int x, int y, int left, int bottom, int width)
{
    return x >= left && x < left + width && y >= bottom && y < bottom + 26;
}

static int adirs_window_can_enter(int unit)
{
    const A380XAdirsUnitState *u = &adirs.unit[unit];
    return u->mode == A380X_ADIRS_ATT && !u->ir_off && !u->ir_fault_lit;
}

static void adirs_window_submit(int unit)
{
    char *end;
    double heading;
    if (unit < 0 || unit >= 3) return;
    heading = strtod(adirs_window_heading[unit], &end);
    if (end == adirs_window_heading[unit] || *end || !isfinite(heading)
            || heading < 0. || heading > 360.) {
        snprintf(adirs_window_message, sizeof(adirs_window_message),
                 "IR %d: enter a magnetic heading from 000 to 360.", unit + 1);
        adirs_window_message_error = 1;
        return;
    }
    if (!adirs_enter_heading(unit, (float)heading)) {
        snprintf(adirs_window_message, sizeof(adirs_window_message),
                 "IR %d: entry requires ATT and an available IR.", unit + 1);
        adirs_window_message_error = 1;
        return;
    }
    snprintf(adirs_window_heading[unit], sizeof(adirs_window_heading[unit]),
             "%05.1f", heading >= 360. ? 0. : heading);
    snprintf(adirs_window_message, sizeof(adirs_window_message),
             "IR %d: heading entered. Update every 10 minutes.", unit + 1);
    adirs_window_message_error = 0;
    adirs_window_release_focus();
}

static void adirs_window_draw_callback(XPLMWindowID window, void *refcon)
{
    int left, top, right, bottom, i, side, ground;
    GLboolean culling = glIsEnabled(GL_CULL_FACE);
    char text[80];
    (void)refcon;
    /* X-Plane may leave clockwise face culling enabled; these 2-D quads
     * are counterclockwise. Preserve the host's state around the window. */
    glDisable(GL_CULL_FACE);
    XPLMGetWindowGeometry(window, &left, &top, &right, &bottom);
    adirs_window_rect(left, bottom, right, top, .025f, .085f, .11f);
    adirs_window_text(left + 18, bottom + 437, "ADIRS / ATT HEADING", 1);
    if (adirs.on_batt) adirs_window_text(left + 550, bottom + 437, "ON BATT", 2);
    for (i = 0; i < 3; ++i) {
        const A380XAdirsUnitState *u = &adirs.unit[i];
        int x = left + 18 + i * 212, mode;
        static const char *mode_labels[] = { "OFF", "NAV", "ATT" };
        const char *ir_status = u->ir_off ? "OFF" : u->ir_fault_lit ? "FAULT" :
            u->mode == A380X_ADIRS_OFF ? "OFF" : u->attitude_valid ? "AVAILABLE" : "WAIT";
        const char *adr_status = u->adr_off ? "OFF" : u->adr_fault_lit ? "FAULT" :
            u->adr_valid ? "AVAILABLE" : "WAIT";
        snprintf(text, sizeof(text), "ADIRU %d", i + 1);
        adirs_window_text(x, bottom + 407, text, 1);
        for (mode = A380X_ADIRS_OFF; mode <= A380X_ADIRS_ATT; ++mode)
            adirs_window_button(x + mode * 65, bottom + 374, 62,
                                mode_labels[mode], u->mode == mode, 1);
        adirs_window_rect(x, bottom + 340, x + 192, bottom + 366,
                         u->ir_pressed ? .08f : .12f, u->ir_pressed ? .35f : .20f,
                         u->ir_pressed ? .43f : .25f);
        snprintf(text, sizeof(text), "IR    %s", ir_status);
        adirs_window_text(x + 8, bottom + 349, text, u->ir_fault_lit ? 2 : u->ir_pressed ? 1 : 0);
        adirs_window_rect(x, bottom + 306, x + 192, bottom + 332,
                         u->adr_pressed ? .08f : .12f, u->adr_pressed ? .35f : .20f,
                         u->adr_pressed ? .43f : .25f);
        snprintf(text, sizeof(text), "ADR   %s", adr_status);
        adirs_window_text(x + 8, bottom + 315, text, u->adr_fault_lit ? 2 : u->adr_pressed ? 1 : 0);
        if (u->aligning) {
            int seconds = isfinite(u->alignment_remaining) && u->alignment_remaining > 0.
                ? (int)ceil(fmin(u->alignment_remaining, 5999.)) : 0;
            snprintf(text, sizeof(text), "ALIGN  %02d:%02d", seconds / 60, seconds % 60);
        } else snprintf(text, sizeof(text), "NAV   %s", u->nav_valid ? "AVAILABLE" : "UNAVAILABLE");
        adirs_window_text(x, bottom + 287, text, u->nav_valid ? 1 : 0);
        if (u->nav_locked) adirs_window_text(x, bottom + 267, "NAV LOCKED THIS FLIGHT", 2);
        else if (u->heading_valid) {
            snprintf(text, sizeof(text), "HDG   %05.1f MAG", u->heading_deg);
            adirs_window_text(x, bottom + 267, text, 0);
        }
        adirs_window_text(x, bottom + 237, "ATT MAGNETIC HEADING", 0);
        adirs_window_rect(x, bottom + 200, x + 112, bottom + 226,
            adirs_window_focus == i ? .10f : .05f,
            adirs_window_focus == i ? .29f : .14f,
            adirs_window_focus == i ? .37f : .18f);
        snprintf(text, sizeof(text), "%s%s", adirs_window_heading[i][0] ?
            adirs_window_heading[i] : "---.-", adirs_window_focus == i ? "_" : "");
        adirs_window_text(x + 8, bottom + 209, text, adirs_window_can_enter(i) ? 1 : 3);
        adirs_window_button(x + 120, bottom + 200, 72, "ENTER", 0, adirs_window_can_enter(i));
        if (u->mode == A380X_ADIRS_ATT) {
            if (u->att_heading_reminder) adirs_window_text(x, bottom + 182, "UPDATE HEADING", 2);
            else if (u->att_heading_required) adirs_window_text(x, bottom + 182, "ENTER HEADING", 2);
            else if (u->att_heading_entered) {
                int age = isfinite(u->heading_age_seconds) && u->heading_age_seconds > 0.
                    ? (int)fmin(u->heading_age_seconds, 5999.) : 0;
                snprintf(text, sizeof(text), "HEADING AGE  %02d:%02d", age / 60, age % 60);
                adirs_window_text(x, bottom + 182, text, 0);
            }
        }
    }
    adirs_window_rect(left + 18, bottom + 167, right - 18, bottom + 168, .22f, .31f, .36f);
    for (side = 0; side < 2; ++side) {
        int x = left + 18 + side * 326, row;
        adirs_window_text(x, bottom + 145, side ? "FIRST OFFICER SOURCES" : "CAPTAIN SOURCES", 1);
        for (row = 0; row < 2; ++row) {
            int y = bottom + 108 - row * 33;
            int third = (row ? adirs.adr_source[side] : adirs.ir_source[side]) == 2;
            adirs_window_text(x, y + 9, row ? "ADR" : "IR", 0);
            adirs_window_button(x + 45, y, 102, "NORMAL", !third, 1);
            adirs_window_button(x + 155, y, 100, row ? "ADR 3" : "IR 3", third, 1);
        }
    }
    adirs_window_text(left + 18, bottom + 53, "NAV selection order: 1, 2, then 3.   ATT: click heading, type, ENTER.", 3);
    ground = refs.on_ground && XPLMGetDatai(refs.on_ground);
    adirs_window_button(left + 497, bottom + 14, 135, "ALIGN NOW", 0, ground);
    adirs_window_text(left + 18, bottom + 24, adirs_window_message[0] ? adirs_window_message :
        "ATT provides attitude and crew-entered heading only.", adirs_window_message_error ? 2 : 0);
    if (culling) glEnable(GL_CULL_FACE);
}

static int adirs_window_mouse_callback(XPLMWindowID window, int x, int y,
                                       XPLMMouseStatus status, void *refcon)
{
    int left, top, right, bottom, i, side, row;
    (void)refcon;
    if (status == xplm_MouseUp) {
        adirs_window_cancel_pointer();
        return 1;
    }
    XPLMGetWindowGeometry(window, &left, &top, &right, &bottom);
    x -= left; y -= bottom;
    if (status == xplm_MouseDrag) {
        if (adirs_window_held_unit >= 0 && !adirs_window_hit(x, y,
                18 + adirs_window_held_unit * 212, adirs_window_held_ir ? 340 : 306, 192))
            adirs_window_release_button();
        return 1;
    }
    if (status != xplm_MouseDown || adirs_window_mouse_down) return 1;
    adirs_window_mouse_down = 1;
    for (i = 0; i < 3; ++i) {
        int field_x = 18 + i * 212, mode, is_ir;
        for (mode = A380X_ADIRS_OFF; mode <= A380X_ADIRS_ATT; ++mode) {
            if (!adirs_window_hit(x, y, field_x + mode * 65, 374, 62)) continue;
            adirs_window_release_focus();
            adirs_window_mouse_down = 1;
            adirs_window_message_error = !adirs_set_mode(i, mode);
            if (adirs_window_message_error)
                snprintf(adirs_window_message, sizeof(adirs_window_message), "IR %d: selector unavailable.", i + 1);
            else adirs_window_message[0] = 0;
            return 1;
        }
        for (is_ir = 0; is_ir <= 1; ++is_ir) {
            if (!adirs_window_hit(x, y, field_x, is_ir ? 340 : 306, 192)) continue;
            adirs_window_release_focus();
            adirs_window_mouse_down = 1;
            if (adirs_set_button(i, is_ir, 1)) {
                adirs_window_held_unit = i;
                adirs_window_held_ir = is_ir;
                adirs_window_message[0] = 0;
                adirs_window_message_error = 0;
            }
            return 1;
        }
        if (adirs_window_hit(x, y, field_x, 200, 112)) {
            if (!adirs_window_can_enter(i)) {
                snprintf(adirs_window_message, sizeof(adirs_window_message),
                         "IR %d: select ATT to enter a magnetic heading.", i + 1);
                adirs_window_message_error = 1;
                adirs_window_release_focus();
                return 1;
            }
            adirs_window_focus = i;
            adirs_window_replace = 1;
            adirs_window_message[0] = 0;
            adirs_window_message_error = 0;
            XPLMTakeKeyboardFocus(window);
            return 1;
        }
        if (adirs_window_hit(x, y, field_x + 120, 200, 72)) {
            adirs_window_submit(i);
            return 1;
        }
    }
    adirs_window_release_focus();
    for (side = 0; side < 2; ++side) for (row = 0; row < 2; ++row) {
        int sx = 18 + side * 326, sy = 108 - row * 33;
        int normal = adirs_window_hit(x, y, sx + 45, sy, 102);
        if (normal || adirs_window_hit(x, y, sx + 155, sy, 100)) {
            adirs_set_source(side, !row, !normal);
            adirs_window_message[0] = 0;
            adirs_window_message_error = 0;
            return 1;
        }
    }
    if (adirs_window_hit(x, y, 497, 14, 135)) {
        int aligned = adirs_align_now();
        snprintf(adirs_window_message, sizeof(adirs_window_message), "%s", aligned ?
            "Available NAV units aligned." : "Align requires ground, stationary, NAV and power.");
        adirs_window_message_error = !aligned;
    }
    return 1;
}

static void adirs_window_key_callback(XPLMWindowID window, char key,
    XPLMKeyFlags flags, char virtual_key, void *refcon, int losing_focus)
{
    char *value;
    size_t len;
    unsigned char vk = (unsigned char)virtual_key;
    (void)window; (void)refcon;
    if (losing_focus) {
        adirs_window_cancel_pointer();
        adirs_window_focus = -1;
        adirs_window_replace = 0;
        return;
    }
    if (!(flags & xplm_UpFlag) && vk == XPLM_VK_ESCAPE) {
        adirs_window_cancel_pointer();
        adirs_window_release_focus();
        return;
    }
    if (adirs_window_focus < 0 || adirs_window_focus >= 3 || (flags & xplm_UpFlag)) return;
    if (vk == XPLM_VK_RETURN || key == '\r' || key == '\n') {
        if (flags & xplm_DownFlag) adirs_window_submit(adirs_window_focus);
        return;
    }
    if (flags & (xplm_ControlFlag | xplm_OptionAltFlag)) return;
    value = adirs_window_heading[adirs_window_focus];
    len = strlen(value);
    if (vk == XPLM_VK_BACK || vk == XPLM_VK_DELETE || key == '\b') {
        if (adirs_window_replace) value[0] = 0;
        else if (len) value[len - 1] = 0;
        adirs_window_replace = 0;
        return;
    }
    if ((key >= '0' && key <= '9') || key == '.') {
        if (adirs_window_replace) { value[0] = 0; len = 0; adirs_window_replace = 0; }
        if (len < 6 && (key != '.' || !strchr(value, '.'))) {
            value[len] = key;
            value[len + 1] = 0;
        }
    }
}

static XPLMCursorStatus adirs_window_cursor_callback(XPLMWindowID window,
    int x, int y, void *refcon)
{
    (void)window; (void)x; (void)y; (void)refcon;
    return xplm_CursorArrow;
}

static int adirs_window_wheel_callback(XPLMWindowID window, int x, int y,
    int wheel, int clicks, void *refcon)
{
    (void)window; (void)x; (void)y; (void)wheel; (void)clicks; (void)refcon;
    return 1;
}

static int adirs_window_right_mouse_callback(XPLMWindowID window, int x, int y,
    XPLMMouseStatus status, void *refcon)
{
    (void)window; (void)x; (void)y; (void)refcon;
    if (status == xplm_MouseDown) adirs_window_cancel_pointer();
    return 1;
}

static int adirs_window_command_callback(XPLMCommandRef command,
    XPLMCommandPhase phase, void *refcon)
{
    (void)command; (void)refcon;
    if (phase == xplm_CommandBegin && adirs_native_window)
        adirs_window_set_visible(!XPLMGetWindowIsVisible(adirs_native_window));
    return 0;
}

static void adirs_window_menu_callback(void *menu_ref, void *item_ref)
{
    (void)menu_ref; (void)item_ref;
    if (adirs_native_window)
        adirs_window_set_visible(!XPLMGetWindowIsVisible(adirs_native_window));
}

static void adirs_window_shutdown(void)
{
    adirs_window_cancel_pointer();
    adirs_window_release_focus();
    if (adirs_window_command) {
        XPLMUnregisterCommandHandler(adirs_window_command, adirs_window_command_callback, 1, NULL);
        adirs_window_command = NULL;
    }
    if (adirs_native_menu) { XPLMDestroyMenu(adirs_native_menu); adirs_native_menu = NULL; }
    if (adirs_native_window) { XPLMDestroyWindow(adirs_native_window); adirs_native_window = NULL; }
    memset(adirs_window_heading, 0, sizeof(adirs_window_heading));
    adirs_window_message[0] = 0;
    adirs_window_message_error = 0;
}

static int adirs_window_init(void)
{
    XPLMCreateWindow_t window;
    int left, top, right, bottom;
    if (adirs_native_window) return 1;
    memset(&window, 0, sizeof(window));
    XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);
    window.structSize = sizeof(window);
    window.left = left + (right - left - 650) / 2;
    window.bottom = bottom + (top - bottom - 460) / 2;
    window.right = window.left + 650;
    window.top = window.bottom + 460;
    window.visible = 0;
    window.drawWindowFunc = adirs_window_draw_callback;
    window.handleMouseClickFunc = adirs_window_mouse_callback;
    window.handleKeyFunc = adirs_window_key_callback;
    window.handleCursorFunc = adirs_window_cursor_callback;
    window.handleMouseWheelFunc = adirs_window_wheel_callback;
    window.handleRightClickFunc = adirs_window_right_mouse_callback;
    window.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
    window.layer = xplm_WindowLayerFloatingWindows;
    adirs_native_window = XPLMCreateWindowEx(&window);
    if (!adirs_native_window) return 0;
    XPLMSetWindowPositioningMode(adirs_native_window, xplm_WindowPositionFree, -1);
    XPLMSetWindowGravity(adirs_native_window, .5f, .5f, .5f, .5f);
    XPLMSetWindowResizingLimits(adirs_native_window, 650, 460, 650, 460);
    XPLMSetWindowTitle(adirs_native_window, "A380 ADIRS");
    adirs_native_menu = XPLMCreateMenu("ADIRS", NULL, 0, adirs_window_menu_callback, NULL);
    if (!adirs_native_menu) goto failed;
    if (XPLMAppendMenuItem(adirs_native_menu, "Open / Close", NULL, 0) < 0) goto failed;
    /* Commands persist across aircraft reloads; only handlers belong to us. */
    adirs_window_command = XPLMFindCommand("a380x/xp12/adirs/window_toggle");
    if (!adirs_window_command)
        adirs_window_command = XPLMCreateCommand("a380x/xp12/adirs/window_toggle", "A380 ADIRS: open or close");
    if (!adirs_window_command) goto failed;
    XPLMRegisterCommandHandler(adirs_window_command, adirs_window_command_callback, 1, NULL);
    return 1;
failed:
    adirs_window_shutdown();
    return 0;
}
