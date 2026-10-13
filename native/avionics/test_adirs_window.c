/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exercises the production ADIRS window against the production state core.
 * XPLM and OpenGL calls are recorded; no simulator/window/context is needed.
 * Build with the SDK includes, -DAPL=1 and a380x_adirs.c.
 */
#define XPLM200 1
#define XPLM210 1
#define XPLM300 1
#define XPLM301 1
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OpenGL/gl.h>
#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMMenus.h"
#include "XPLMUtilities.h"
#include "a380x_adirs.h"

static A380XAdirsState adirs;
static A380XAdirsInputs test_inputs;
static struct { XPLMDataRef on_ground; } refs;
static int entries, align_calls;
static int button_presses[3][2], mode_selections[3][3];
static int adirs_set_mode(int unit, int mode)
{
    int result = a380x_adirs_set_mode(&adirs, &test_inputs, unit, mode);
    if (result) ++mode_selections[unit][mode];
    return result;
}
static int adirs_set_button(int unit, int is_ir, int pressed)
{
    A380XAdirsUnitState *u;
    int was_pressed;
    assert(unit >= 0 && unit < 3 && (is_ir == 0 || is_ir == 1));
    u = &adirs.unit[unit];
    was_pressed = is_ir ? u->ir_pressed : u->adr_pressed;
    if (pressed) ++button_presses[unit][is_ir];
    if (pressed && !was_pressed) {
        if (is_ir) a380x_adirs_toggle_ir(&adirs, unit);
        else a380x_adirs_toggle_adr(&adirs, unit);
    }
    a380x_adirs_set_pressed(&adirs, unit, is_ir, pressed);
    a380x_adirs_update(&adirs, &test_inputs, 0.);
    return 1;
}
static int adirs_enter_heading(int unit, float heading)
{
    int result = a380x_adirs_enter_heading(&adirs, &test_inputs, unit, heading);
    entries += result;
    return result;
}
static int adirs_set_source(int side, int ir, int third)
{
    return a380x_adirs_set_source(&adirs, side, ir, third);
}
static int adirs_align_now(void)
{
    ++align_calls;
    return a380x_adirs_align_now(&adirs, &test_inputs);
}
#include "a380x_adirs_window.inc"

static XPLMCreateWindow_t saved;
static XPLMWindowID focused;
static int shown, destroyed, menu_destroyed, command_created, registered;
static int vertices, label_count, cull_disable_calls, cull_enable_calls;
static GLboolean culling;
static char labels[128][100];
static int has_label(const char *value)
{
    int i;
    for (i = 0; i < label_count; ++i) if (!strcmp(labels[i], value)) return 1;
    return 0;
}
int XPLMGetDatai(XPLMDataRef r) { (void)r; return test_inputs.on_ground; }
void XPLMGetScreenBoundsGlobal(int *l, int *t, int *r, int *b)
{ *l = 0; *t = 1000; *r = 1400; *b = 0; }
XPLMWindowID XPLMCreateWindowEx(XPLMCreateWindow_t *p)
{ saved = *p; shown = p->visible; return &saved; }
void XPLMGetWindowGeometry(XPLMWindowID w, int *l, int *t, int *r, int *b)
{ (void)w; *l = saved.left; *t = saved.top; *r = saved.right; *b = saved.bottom; }
void XPLMSetWindowPositioningMode(XPLMWindowID w, XPLMWindowPositioningMode m, int i)
{ (void)w; (void)m; (void)i; }
void XPLMSetWindowGravity(XPLMWindowID w, float l, float t, float r, float b)
{ (void)w; (void)l; (void)t; (void)r; (void)b; }
void XPLMSetWindowResizingLimits(XPLMWindowID w, int a, int b, int c, int d)
{ (void)w; assert(a == 650 && b == 460 && c == 650 && d == 460); }
void XPLMSetWindowTitle(XPLMWindowID w, const char *s)
{ (void)w; assert(!strcmp(s, "A380 ADIRS")); }
void XPLMSetWindowIsVisible(XPLMWindowID w, int v) { (void)w; shown = v; }
int XPLMGetWindowIsVisible(XPLMWindowID w) { (void)w; return shown; }
void XPLMBringWindowToFront(XPLMWindowID w) { (void)w; }
void XPLMDestroyWindow(XPLMWindowID w) { assert(w == &saved); ++destroyed; }
int XPLMHasKeyboardFocus(XPLMWindowID w) { return w == focused; }
void XPLMTakeKeyboardFocus(XPLMWindowID w)
{
    if (focused && focused != w) saved.handleKeyFunc(w, 0, 0, 0, NULL, 1);
    focused = w;
}
XPLMMenuID XPLMCreateMenu(const char *s, XPLMMenuID p, int i, XPLMMenuHandler_f f, void *r)
{
    (void)p; (void)i; (void)f; (void)r;
    assert(!strcmp(s, "ADIRS"));
    return (void *)42;
}
int XPLMAppendMenuItem(XPLMMenuID m, const char *s, void *r, int f)
{ (void)m; (void)s; (void)r; (void)f; return 0; }
void XPLMDestroyMenu(XPLMMenuID m) { assert(m == (void *)42); ++menu_destroyed; }
XPLMCommandRef XPLMFindCommand(const char *s)
{ (void)s; return command_created ? (void *)43 : NULL; }
XPLMCommandRef XPLMCreateCommand(const char *s, const char *d)
{
    (void)d;
    assert(!strcmp(s, "a380x/xp12/adirs/window_toggle"));
    ++command_created;
    return (void *)43;
}
void XPLMRegisterCommandHandler(XPLMCommandRef c, XPLMCommandCallback_f f, int b, void *r)
{ (void)c; (void)f; (void)b; (void)r; assert(!registered); ++registered; }
void XPLMUnregisterCommandHandler(XPLMCommandRef c, XPLMCommandCallback_f f, int b, void *r)
{ (void)c; (void)f; (void)b; (void)r; assert(registered == 1); --registered; }
void XPLMDrawString(float *r, int x, int y, const char *s, int *w, XPLMFontID f)
{
    (void)r; (void)w; (void)f;
    assert(x >= saved.left && x < saved.right && y >= saved.bottom && y < saved.top);
    assert(label_count < 128);
    snprintf(labels[label_count++], sizeof(labels[0]), "%s", s);
}
void XPLMSetGraphicsState(int a, int b, int c, int d, int e, int f, int g)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; }
GLboolean glIsEnabled(GLenum cap) { assert(cap == GL_CULL_FACE); return culling; }
void glDisable(GLenum cap) { assert(cap == GL_CULL_FACE); culling = GL_FALSE; ++cull_disable_calls; }
void glEnable(GLenum cap) { assert(cap == GL_CULL_FACE); culling = GL_TRUE; ++cull_enable_calls; }
void glColor3f(GLfloat r, GLfloat g, GLfloat b) { (void)r; (void)g; (void)b; }
void glBegin(GLenum type) { assert(type == GL_QUADS && !culling); }
void glVertex2i(GLint x, GLint y)
{
    assert(x >= saved.left && x <= saved.right && y >= saved.bottom && y <= saved.top);
    ++vertices;
}
void glEnd(void) {}

static void mouse(int x, int y, XPLMMouseStatus status)
{ saved.handleMouseClickFunc(&saved, saved.left + x, saved.bottom + y, status, NULL); }
static void click(int x, int y)
{ mouse(x, y, xplm_MouseDown); mouse(x, y, xplm_MouseUp); }
static void key(char c)
{
    /* The decimal key's virtual code differs from ASCII '.', which is DELETE. */
    char virtual_key = c == '.' ? XPLM_VK_DECIMAL : c;
    saved.handleKeyFunc(&saved, c, xplm_DownFlag, virtual_key, NULL, 0);
}
static void type(const char *value) { while (*value) key(*value++); }
static void draw(void)
{ label_count = vertices = 0; saved.drawWindowFunc(&saved, NULL); assert(vertices > 20); }
static void prepare(void)
{
    int i;
    memset(&test_inputs, 0, sizeof(test_inputs));
    test_inputs.on_ground = 1;
    for (i = 0; i < 3; ++i) {
        A380XAdirsUnitInputs *u = &test_inputs.unit[i];
        u->adr_powered = u->ir_powered = u->adr_sensor_valid = 1;
        u->attitude_sensor_valid = u->heading_sensor_valid = u->navigation_sensor_valid = 1;
        u->heading_deg = 20.;
    }
    a380x_adirs_init(&adirs, &test_inputs, 0);
    for (i = 0; i < 3; ++i)
        assert(a380x_adirs_set_mode(&adirs, &test_inputs, i, A380X_ADIRS_ATT));
    a380x_adirs_update(&adirs, &test_inputs, 28.);
}
static void exercise_panel_controls(void)
{
    int unit, mode, is_ir, other;
    for (unit = 0; unit < 3; ++unit) {
        int x = 30 + unit * 212;
        for (mode = A380X_ADIRS_OFF; mode <= A380X_ADIRS_ATT; ++mode) {
            int previous = mode_selections[unit][mode];
            click(x + mode * 65, 384);
            assert(adirs.unit[unit].mode == mode);
            assert(mode_selections[unit][mode] == previous + 1);
        }
        for (is_ir = 0; is_ir <= 1; ++is_ir) {
            A380XAdirsUnitState *u = &adirs.unit[unit];
            int y = is_ir ? 350 : 316, previous = button_presses[unit][is_ir];
            click(x, 210); assert(focused); /* Click must safely take focus from entry. */
            mouse(x, y, xplm_MouseDown);
            assert(!focused);
            assert((is_ir ? u->ir_off : u->adr_off) == 1);
            assert((is_ir ? u->ir_pressed : u->adr_pressed) == 1);
            mouse(x, y, xplm_MouseDown); mouse(x + 20, y, xplm_MouseDrag);
            assert(button_presses[unit][is_ir] == previous + 1);
            assert((is_ir ? u->ir_off : u->adr_off) == 1);
            /* The opposite subsystem and both other ADIRUs remain untouched. */
            assert((is_ir ? u->adr_off : u->ir_off) == 0);
            for (other = 0; other < 3; ++other) if (other != unit)
                assert(!adirs.unit[other].ir_off && !adirs.unit[other].adr_off);
            mouse(x, y, xplm_MouseUp);
            assert((is_ir ? u->ir_pressed : u->adr_pressed) == 0);
            click(x, y);
            assert((is_ir ? u->ir_off : u->adr_off) == 0);
            assert(button_presses[unit][is_ir] == previous + 2);
        }
    }
    /* Leaving a hit region cancels only the press. Dragging back never toggles. */
    mouse(30, 350, xplm_MouseDown);
    assert(adirs.unit[0].ir_pressed && adirs.unit[0].ir_off);
    mouse(-5, 350, xplm_MouseDrag); assert(!adirs.unit[0].ir_pressed);
    mouse(30, 350, xplm_MouseDrag); assert(!adirs.unit[0].ir_pressed && adirs.unit[0].ir_off);
    mouse(30, 350, xplm_MouseUp); click(30, 350); assert(!adirs.unit[0].ir_off);
    /* Explicit cancellation and hiding cannot leave a pushbutton held down. */
    mouse(242, 316, xplm_MouseDown);
    saved.handleRightClickFunc(&saved, 0, 0, xplm_MouseDown, NULL);
    assert(!adirs.unit[1].adr_pressed && adirs.unit[1].adr_off);
    click(242, 316); assert(!adirs.unit[1].adr_off);
    mouse(454, 350, xplm_MouseDown); key(XPLM_VK_ESCAPE);
    assert(!adirs.unit[2].ir_pressed && adirs.unit[2].ir_off);
    click(454, 350); assert(!adirs.unit[2].ir_off);
    mouse(454, 316, xplm_MouseDown); adirs_window_set_visible(0);
    assert(!adirs.unit[2].adr_pressed && adirs.unit[2].adr_off);
    adirs_window_set_visible(1); click(454, 316); assert(!adirs.unit[2].adr_off);
    a380x_adirs_update(&adirs, &test_inputs, 28.);
}
static void exercise_entry(void)
{
    click(30, 210);
    assert(focused == &saved && adirs_window_focus == 0);
    type("270.5\r");
    assert(adirs.unit[0].heading_deg == 270.5 && adirs.unit[0].heading_valid && !focused);
    click(245, 210); type("999\r");
    assert(adirs_window_message_error && focused == &saved && !adirs.unit[1].att_heading_entered);
    click(245, 210); type("090"); click(355, 210);
    assert(adirs.unit[1].heading_deg == 90. && !focused);
    click(460, 210); type("360\r");
    assert(adirs.unit[2].heading_deg == 0. && !focused);
    click(460, 210); type("123"); key('\b'); key('4'); key('\r');
    assert(adirs.unit[2].heading_deg == 124. && !focused);
    /* A lost focus must stop keyboard edits without discarding stored text. */
    click(460, 210);
    saved.handleKeyFunc(NULL, 0, 0, 0, NULL, 1);
    focused = NULL;
    assert(adirs_window_focus == -1);
    key('9'); assert(!strcmp(adirs_window_heading[2], "124.0"));
    click(460, 210); key(XPLM_VK_ESCAPE); assert(!focused);
    /* ATT heading entry remains blocked if IR power drops after field focus. */
    click(460, 210); type("111"); test_inputs.unit[2].ir_powered = 0; key('\r');
    assert(adirs_window_message_error && adirs.unit[2].heading_deg == 124.);
    test_inputs.unit[2].ir_powered = 1;
}
static void exercise_sources_and_alignment(void)
{
    click(173, 120); assert(adirs.ir_source[0] == 2 && adirs.adr_source[0] == 0);
    click(510, 85); assert(adirs.adr_source[1] == 2 && adirs.ir_source[1] == 1);
    click(80, 120); assert(adirs.ir_source[0] == 0);
    click(400, 85); assert(adirs.adr_source[1] == 1);
    click(510, 30); assert(adirs_window_message_error); /* Every unit is ATT. */
    assert(a380x_adirs_set_mode(&adirs, &test_inputs, 2, A380X_ADIRS_NAV));
    click(510, 30); assert(!adirs_window_message_error && adirs.unit[2].nav_valid);
    test_inputs.on_ground = 0;
    click(510, 30); assert(adirs_window_message_error);
    test_inputs.on_ground = 1;
}
int main(void)
{
    int cycle;
    refs.on_ground = (void *)1;
    for (cycle = 0; cycle < 3; ++cycle) {
        prepare();
        assert(adirs_window_init() && registered == 1 && !shown);
        assert(adirs_window_init() && registered == 1);
        adirs_window_command_callback(NULL, xplm_CommandBegin, NULL); assert(shown);
        exercise_panel_controls();
        exercise_entry();
        a380x_adirs_update(&adirs, &test_inputs, 600.);
        culling = GL_TRUE; draw();
        assert(culling && has_label("UPDATE HEADING"));
        assert(cull_enable_calls == cycle * 1 + 1);
        culling = GL_FALSE; draw(); assert(!culling);
        exercise_sources_and_alignment();
        test_inputs.on_ground = 0;
        assert(a380x_adirs_set_mode(&adirs, &test_inputs, 2, A380X_ADIRS_ATT));
        draw(); assert(has_label("NAV LOCKED THIS FLIGHT"));
        click(460, 210); assert(focused);
        adirs_window_set_visible(0); assert(!focused && !shown);
        adirs_window_set_visible(1); click(460, 210); assert(focused);
        mouse(460, 316, xplm_MouseDown); assert(adirs.unit[2].adr_pressed);
        adirs_window_shutdown(); assert(!focused && !registered);
        assert(!adirs.unit[2].adr_pressed);
    }
    assert(command_created == 1 && destroyed == 3 && menu_destroyed == 3);
    assert(entries == 12 && align_calls == 9 && cull_disable_calls == 9);
    puts("ADIRS window: all mode selectors and pushbuttons, held/cancelled clicks, heading entry, sources, alignment, reminders, culling, focus and reloads passed");
    return 0;
}
