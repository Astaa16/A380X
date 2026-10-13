/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD per-side UI state, event resolution and widgets. See a380x_mfd_ui.h.
 * Geometry and states: docs/mfd-spec/01-framework-widgets.md section 13 (border-box sizes).
 */
#include "a380x_mfd_ui.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CURSOR_HIDE_S 5.0
#define KBD_WATCHDOG_S 20.0
#define VALIDATE_S 0.5
#define CTX_GUARD_S 0.1
#define MENU_ROW_H 36.0f
#define MENU_MAX_H 275.0f /* border-box max-height of .mfd-dropdown-menu */
#define MENU_SCROLLBAR_W 20.0f

static const char GLYPH_BOX_V[] = "\xE2\x96\xAF"; /* U+25AF */

/* ======================================================================================== */
/* Lifecycle and events                                                                     */
/* ======================================================================================== */

void mfd_ui_init(MfdUi *ui, MfdSide side, MfdDrawList *dl, MfdMsgQueue *msgs)
{
    memset(ui, 0, sizeof(*ui));
    ui->side = side;
    ui->dl = dl;
    ui->msgs = msgs;
    ui->menu_scroll_to = -1;
    ui->last_move = -1.0e9;
}

void mfd_ui_push_event(MfdUi *ui, const MfdEvent *ev)
{
    int tail;
    if (ui->ev_count >= MFD_UI_MAX_EVENTS) {
        /* Coalesce: drop the oldest pending move if possible, else drop the event. */
        ui->ev_dropped++;
        return;
    }
    /* Coalesce consecutive moves. */
    if (ev->kind == MFD_EV_MOVE && ui->ev_count > 0) {
        int last = (ui->ev_head + ui->ev_count - 1) % MFD_UI_MAX_EVENTS;
        if (ui->events[last].kind == MFD_EV_MOVE) {
            ui->events[last] = *ev;
            return;
        }
    }
    tail = (ui->ev_head + ui->ev_count) % MFD_UI_MAX_EVENTS;
    ui->events[tail] = *ev;
    ui->ev_count++;
}

static const MfdHit *hit_test(const MfdUi *ui, float x, float y)
{
    int prev = ui->hit_cur ^ 1, i;
    const MfdHit *best = NULL;
    for (i = 0; i < ui->hit_count[prev]; ++i) {
        const MfdHit *h = &ui->hits[prev][i];
        if (!mfd_rect_contains(h->r, x, y)) continue;
        if (!best || h->layer >= best->layer) best = h;
    }
    return best;
}

static void drop_focus(MfdUi *ui)
{
    ui->focus_id = 0;
    ui->edit = MFD_EDIT_IDLE;
    ui->buf[0] = '\0';
    ui->overflow = 0;
    ui->blur_pending = 0;
    ui->focus_external = 0;
    ui->kbd_capture = 0;
}

void mfd_ui_begin(MfdUi *ui, double now)
{
    int i;
    ui->dt = now - ui->now;
    if (ui->dt < 0.0 || ui->frame == 0) ui->dt = 0.0;
    ui->now = now;
    ui->frame++;
    ui->down_id = 0; ui->down_part = MFD_PART_MAIN;
    ui->click_id = 0; ui->click_part = MFD_PART_MAIN;
    ui->click_happened = 0;
    ui->wheel_id = 0; ui->wheel_part = MFD_PART_MAIN; ui->wheel = 0;
    ui->key_valid = 0;
    ui->focus_seen = 0;
    for (i = 0; i < MFD_UI_VALIDATING; ++i) ui->val[i].seen = 0;

    /* Swap hit lists: the list built last frame becomes the tested one. */
    ui->hit_cur ^= 1;
    ui->hit_count[ui->hit_cur] = 0;

    while (ui->ev_count > 0) {
        MfdEvent ev = ui->events[ui->ev_head];
        const MfdHit *h;
        int consequential = 1;
        ui->ev_head = (ui->ev_head + 1) % MFD_UI_MAX_EVENTS;
        ui->ev_count--;
        switch ((MfdEventKind)ev.kind) {
        case MFD_EV_MOVE:
            ui->px = ev.x; ui->py = ev.y;
            ui->pointer_inside = ev.x >= 0.0f && ev.y >= 0.0f && ev.x < (float)MFD_W && ev.y < (float)MFD_H;
            ui->last_move = now;
            ui->cursor_visible = ui->pointer_inside;
            consequential = 0;
            break;
        case MFD_EV_LEAVE:
            ui->pointer_inside = 0;
            ui->cursor_visible = 0;
            ui->button_down = 0;
            ui->press_id = 0;
            consequential = 0;
            break;
        case MFD_EV_DOWN:
            if (!ui->pointer_inside) { consequential = 0; break; }
            ui->button_down = 1;
            h = hit_test(ui, ui->px, ui->py);
            ui->press_id = h ? h->id : 0;
            ui->press_part = h ? h->part : MFD_PART_MAIN;
            ui->down_id = ui->press_id;
            ui->down_part = ui->press_part;
            /* Mouse-down outside the focused field blurs (commits) it (spec 01 section 11.3). */
            if (ui->focus_id && ui->down_id != ui->focus_id) ui->blur_pending = 1;
            break;
        case MFD_EV_UP: {
            int same;
            if (!ui->button_down) { consequential = 0; break; }
            ui->button_down = 0;
            h = hit_test(ui, ui->px, ui->py);
            same = h && ui->press_id && h->id == ui->press_id && h->part == ui->press_part;
            ui->click_happened = 1;
            if (same) {
                ui->click_id = h->id;
                ui->click_part = h->part;
            }
            /* Outside click closes the open menu (the click is still delivered). */
            if (ui->open_menu && (!same || ui->click_id != ui->open_menu)) ui->open_menu = 0;
            /* Context menu: any click > 100 ms after opening that is not on one of its items. */
            if (ui->ctx_menu && now - ui->ctx_open_time > CTX_GUARD_S && (!same || ui->click_id != ui->ctx_menu))
                ui->ctx_menu = 0;
            ui->press_id = 0;
            break;
        }
        case MFD_EV_WHEEL:
            if (!ui->pointer_inside) { consequential = 0; break; }
            h = hit_test(ui, ui->px, ui->py);
            ui->wheel_id = h ? h->id : 0;
            ui->wheel_part = h ? h->part : MFD_PART_MAIN;
            ui->wheel = ev.wheel;
            break;
        case MFD_EV_KEY:
            ui->key_valid = 1;
            ui->key = (MfdKey)ev.key;
            ui->key_ch = ev.ch;
            break;
        }
        if (consequential) break;
    }

    if (ui->cursor_visible && now - ui->last_move > CURSOR_HIDE_S) ui->cursor_visible = 0;
    ui->hover_id = 0;
    ui->hover_part = MFD_PART_MAIN;
    if (ui->pointer_inside && ui->cursor_visible) {
        const MfdHit *h = hit_test(ui, ui->px, ui->py);
        if (h) { ui->hover_id = h->id; ui->hover_part = h->part; }
    }
    if (ui->kbd_capture && now - ui->last_key_time > KBD_WATCHDOG_S) ui->kbd_capture = 0;
}

void mfd_ui_end(MfdUi *ui)
{
    int i;
    if (ui->focus_id && !ui->focus_seen) drop_focus(ui);
    ui->blur_pending = 0;
    for (i = 0; i < MFD_UI_VALIDATING; ++i) {
        if (ui->val[i].id && !ui->val[i].seen) ui->val[i].id = 0;
    }
    if (ui->cursor_visible && ui->pointer_inside) {
        MfdLayer old = mfd_draw_get_layer(ui->dl);
        mfd_draw_set_layer(ui->dl, MFD_LAYER_CURSOR);
        mfd_draw_cursor(ui->dl, ui->px, ui->py, ui->side == MFD_FO);
        mfd_draw_set_layer(ui->dl, old);
    }
}

void mfd_ui_reset_page(MfdUi *ui)
{
    int i;
    drop_focus(ui);
    ui->open_menu = 0;
    ui->menu_scroll = 0.0f;
    ui->menu_scroll_to = -1;
    ui->ctx_menu = 0;
    for (i = 0; i < MFD_UI_VALIDATING; ++i) ui->val[i].id = 0;
}

void mfd_ui_suspend(MfdUi *ui)
{
    ui->ev_count = 0;
    ui->ev_head = 0;
    ui->hover_id = ui->press_id = ui->click_id = ui->down_id = 0;
    ui->button_down = 0;
    ui->cursor_visible = 0;
    ui->key_valid = 0;
    ui->hit_count[0] = ui->hit_count[1] = 0;
    drop_focus(ui);
    ui->open_menu = 0;
    ui->ctx_menu = 0;
}

/* ======================================================================================== */
/* Queries                                                                                  */
/* ======================================================================================== */

void mfd_ui_hit(MfdUi *ui, MfdId id, int part, MfdRect r)
{
    MfdHit *h;
    MfdRect c = mfd_rect_intersect(r, mfd_draw_clip_current(ui->dl));
    if (c.w <= 0.0f || c.h <= 0.0f) return;
    if (ui->hit_count[ui->hit_cur] >= MFD_UI_MAX_HITS) {
        ui->hits_overflowed = 1;
        return;
    }
    h = &ui->hits[ui->hit_cur][ui->hit_count[ui->hit_cur]++];
    h->r = c;
    h->id = id;
    h->part = (int16_t)part;
    h->layer = (uint8_t)mfd_draw_get_layer(ui->dl);
    h->flags = 0;
}

int mfd_ui_is_hovered(const MfdUi *ui, MfdId id)
{
    return id && ui->hover_id == id;
}

int mfd_ui_is_hovered_part(const MfdUi *ui, MfdId id, int part)
{
    return id && ui->hover_id == id && ui->hover_part == part;
}

int mfd_ui_was_clicked(const MfdUi *ui, MfdId id)
{
    return id && ui->click_id == id;
}

int mfd_ui_clicked_part(const MfdUi *ui, MfdId id, int *part)
{
    if (!id || ui->click_id != id) return 0;
    if (part) *part = ui->click_part;
    return 1;
}

int mfd_ui_wheel_notches(const MfdUi *ui, MfdId id)
{
    return (id && ui->wheel_id == id) ? ui->wheel : 0;
}

int mfd_ui_peek_key(const MfdUi *ui, MfdKey *key, char *ch)
{
    if (!ui->key_valid) return 0;
    if (key) *key = ui->key;
    if (ch) *ch = ui->key_ch;
    return 1;
}

void mfd_ui_consume_key(MfdUi *ui)
{
    ui->key_valid = 0;
}

int mfd_ui_menu_is_open(const MfdUi *ui, MfdId id)
{
    return id && ui->open_menu == id;
}

void mfd_ui_close_menus(MfdUi *ui)
{
    ui->open_menu = 0;
}

void mfd_ui_error(MfdUi *ui, const MfdFmsError *err, int with_details)
{
    if (ui->msgs && err && err->type != MFD_ERR_NONE) mfd_msg_show_error(ui->msgs, err, with_details);
}

int mfd_ui_wants_keyboard(const MfdUi *ui)
{
    return ui->focus_id != 0 && ui->kbd_capture;
}

int mfd_ui_has_focus(const MfdUi *ui)
{
    return ui->focus_id != 0;
}

int mfd_key_from_name(const char *name, MfdKey *key, char *ch)
{
    static const struct { const char *name; MfdKey key; char ch; } table[] = {
        {"dot", MFD_KEY_CHAR, '.'}, {"sp", MFD_KEY_CHAR, ' '}, {"slash", MFD_KEY_CHAR, '/'},
        {"plusminus", MFD_KEY_PLUSMINUS, 0}, {"ent", MFD_KEY_ENT, 0}, {"backspace", MFD_KEY_BACKSPACE, 0},
        {"esc", MFD_KEY_ESC, 0}, {"esc2", MFD_KEY_ESC2, 0}, {"up", MFD_KEY_UP, 0}, {"down", MFD_KEY_DOWN, 0},
        {"left", MFD_KEY_LEFT, 0}, {"right", MFD_KEY_RIGHT, 0}, {"dir", MFD_KEY_DIR, 0},
        {"perf", MFD_KEY_PERF, 0}, {"init", MFD_KEY_INIT, 0}, {"navaid", MFD_KEY_NAVAID, 0},
        {"mailbox", MFD_KEY_MAILBOX, 0}, {"fpln", MFD_KEY_FPLN, 0}, {"dest", MFD_KEY_DEST, 0},
        {"secindex", MFD_KEY_SECINDEX, 0}, {"surv", MFD_KEY_SURV, 0}, {"atccom", MFD_KEY_ATCCOM, 0},
        {"nd", MFD_KEY_ND, 0}, {"kbd", MFD_KEY_KBD, 0}, {"clrinfo", MFD_KEY_CLRINFO, 0},
        {"rewind", MFD_KEY_REWIND, 0}, {"forward", MFD_KEY_FORWARD, 0},
    };
    size_t i;
    if (!name || !*name) return 0;
    if (name[1] == '\0') {
        char c = (char)toupper((unsigned char)name[0]);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            if (key) *key = MFD_KEY_CHAR;
            if (ch) *ch = c;
            return 1;
        }
    }
    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (strcmp(name, table[i].name) == 0) {
            if (key) *key = table[i].key;
            if (ch) *ch = table[i].ch;
            return 1;
        }
    }
    return 0;
}

/* ======================================================================================== */
/* Text helpers                                                                             */
/* ======================================================================================== */

float mfd_text_class_size(MfdTextClass cls)
{
    switch (cls) {
    case MFD_TXT_LABEL: return MFD_FS_LABEL;
    case MFD_TXT_LABEL_BIGGER: return MFD_FS_LABEL_BIGGER;
    case MFD_TXT_LABEL_BIGGEST: return MFD_FS_LABEL_BIGGEST;
    case MFD_TXT_UNIT: return 20.0f;
    case MFD_TXT_UNIT_BIGGER: return 24.0f;
    case MFD_TXT_VALUE: return MFD_FS_VALUE;
    case MFD_TXT_VALUE_SMALLER: return MFD_FS_VALUE_SMALLER;
    case MFD_TXT_VALUE_BIGGER: return MFD_FS_VALUE_BIGGER;
    case MFD_TXT_BUTTON: return MFD_FS_BUTTON;
    }
    return MFD_FS_LABEL;
}

MfdColor mfd_text_class_color(MfdTextClass cls)
{
    switch (cls) {
    case MFD_TXT_UNIT:
    case MFD_TXT_UNIT_BIGGER: return MFD_COL_DARK_BLUE;
    case MFD_TXT_VALUE:
    case MFD_TXT_VALUE_SMALLER:
    case MFD_TXT_VALUE_BIGGER: return MFD_COL_GREEN;
    default: return MFD_COL_WHITE;
    }
}

void mfd_text(MfdUi *ui, float x, float baseline, MfdAnchor anchor, float size, MfdColor c, const char *s)
{
    mfd_draw_text(ui->dl, MFD_FONT_SZ, size, c, anchor, x, baseline, s);
}

float mfd_label(MfdUi *ui, MfdTextClass cls, MfdColor color, MfdAnchor anchor, float x, float top, const char *s)
{
    float size = mfd_text_class_size(cls);
    MfdColor c = color == MFD_COL_COUNT ? mfd_text_class_color(cls) : color;
    mfd_draw_text(ui->dl, MFD_FONT_SZ, size, c, anchor, x, mfd_baseline(top, size), s);
    return mfd_text_width(s, size);
}

void mfd_hrule(MfdUi *ui, float x0, float x1, float y, float thickness, MfdColor c)
{
    mfd_draw_fill_rect(ui->dl, mfd_rect(x0, y, x1 - x0, thickness), c);
}

void mfd_frame_rect(MfdUi *ui, MfdRect r, float w, MfdColor c)
{
    mfd_draw_bevel(ui->dl, r, w, c, MFD_BEVEL_SOLID);
}

/* Two-colour mitred border (top/left = tl, bottom/right = br). */
static void border_tlbr(MfdDrawList *dl, MfdRect r, float w, MfdColor tl, MfdColor br)
{
    MfdPoint q[4];
    float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    q[0].x = x0; q[0].y = y0; q[1].x = x1; q[1].y = y0; q[2].x = x1 - w; q[2].y = y0 + w; q[3].x = x0 + w; q[3].y = y0 + w;
    mfd_draw_polygon(dl, q, 4, tl);
    q[0].x = x0; q[0].y = y0; q[1].x = x0 + w; q[1].y = y0 + w; q[2].x = x0 + w; q[2].y = y1 - w; q[3].x = x0; q[3].y = y1;
    mfd_draw_polygon(dl, q, 4, tl);
    q[0].x = x0; q[0].y = y1; q[1].x = x0 + w; q[1].y = y1 - w; q[2].x = x1 - w; q[2].y = y1 - w; q[3].x = x1; q[3].y = y1;
    mfd_draw_polygon(dl, q, 4, br);
    q[0].x = x1; q[0].y = y0; q[1].x = x1; q[1].y = y1; q[2].x = x1 - w; q[2].y = y1 - w; q[3].x = x1 - w; q[3].y = y0 + w;
    mfd_draw_polygon(dl, q, 4, br);
}

/* Split `s` into lines; returns the number of lines and the widest line width at size. */
static int text_lines(const char *s, float size, float *maxw)
{
    int lines = 1;
    float w = 0.0f;
    const char *p = s ? s : "";
    const char *start = p;
    *maxw = 0.0f;
    for (;; ++p) {
        if (*p == '\n' || *p == '\0') {
            w = mfd_text_widthn(start, (size_t)(p - start), size);
            if (w > *maxw) *maxw = w;
            if (*p == '\0') break;
            ++lines;
            start = p + 1;
        }
    }
    return lines;
}

/* Draw each '\n'-separated line centred on cx, first baseline b0, line pitch `pitch`. */
static void draw_lines_centered(MfdDrawList *dl, MfdFont font, float size, MfdColor c, float cx, float b0,
                                float pitch, const char *s)
{
    const char *p = s, *start = s;
    float b = b0;
    if (!s) return;
    for (;; ++p) {
        if (*p == '\n' || *p == '\0') {
            mfd_draw_textn(dl, font, size, c, MFD_ANCHOR_CENTER, cx, b, start, (size_t)(p - start));
            if (*p == '\0') break;
            start = p + 1;
            b += pitch;
        }
    }
}

/* ======================================================================================== */
/* Shapes, icons, cursor                                                                    */
/* ======================================================================================== */

void mfd_draw_triangle_down(MfdDrawList *dl, float x, float y, MfdColor c)
{
    mfd_draw_triangle(dl, x, y, x + 15.0f, y, x + 7.5f, y + 15.0f, c);
}

void mfd_draw_triangle_up(MfdDrawList *dl, float x, float y, MfdColor c)
{
    mfd_draw_triangle(dl, x + 7.5f, y, x + 15.0f, y + 15.0f, x, y + 15.0f, c);
}

/* Rotate local point (lx, ly) about (cx, cy) by deg (SVG sense, y down), scale, translate. */
static MfdPoint icon_pt(float lx, float ly, float cx, float cy, int deg, float scale, float ox, float oy)
{
    MfdPoint p;
    float dx = lx - cx, dy = ly - cy, rx = dx, ry = dy;
    switch (((deg % 360) + 360) % 360) {
    case 90: rx = -dy; ry = dx; break;
    case 180: rx = -dx; ry = -dy; break;
    case 270: rx = dy; ry = -dx; break;
    default: break;
    }
    p.x = ox + (cx + rx) * scale;
    p.y = oy + (cy + ry) * scale;
    return p;
}

static void icon_poly(MfdDrawList *dl, const float *pts, int n, float c0, int deg, float scale, float ox, float oy,
                      MfdColor c)
{
    MfdPoint p[8];
    int i;
    for (i = 0; i < n && i < 8; ++i) p[i] = icon_pt(pts[2 * i], pts[2 * i + 1], c0, c0, deg, scale, ox, oy);
    mfd_draw_polygon(dl, p, n, c);
}

void mfd_draw_icon(MfdDrawList *dl, MfdIcon icon, float x, float y, MfdColor c)
{
    static const float single_up[] = {7.5f, 1.0f, 15.0f, 14.0f, 0.0f, 14.0f};
    static const float single_down[] = {0.0f, 1.0f, 15.0f, 1.0f, 7.5f, 14.0f};
    static const float dbl_a[] = {0.0f, 17.5f, 17.5f, 0.0f, 35.0f, 17.5f};
    static const float dbl_b[] = {0.0f, 35.0f, 17.5f, 17.5f, 35.0f, 35.0f};
    static const float flat[] = {0.0f, 25.0f, 17.5f, 7.5f, 35.0f, 25.0f};
    float s15 = 35.0f / 15.0f;
    switch (icon) {
    case MFD_ICON_SINGLE_UP: icon_poly(dl, single_up, 3, 7.5f, 0, s15, x, y, c); break;
    case MFD_ICON_SINGLE_DOWN: icon_poly(dl, single_down, 3, 7.5f, 0, s15, x, y, c); break;
    case MFD_ICON_SINGLE_LEFT: icon_poly(dl, single_down, 3, 7.5f, 90, s15, x, y, c); break;
    case MFD_ICON_SINGLE_RIGHT: icon_poly(dl, single_down, 3, 7.5f, 270, s15, x, y, c); break;
    case MFD_ICON_DOUBLE_UP:
    case MFD_ICON_DOUBLE_DOWN:
    case MFD_ICON_DOUBLE_LEFT:
    case MFD_ICON_DOUBLE_RIGHT: {
        int deg = icon == MFD_ICON_DOUBLE_UP ? 0 : icon == MFD_ICON_DOUBLE_DOWN ? 180
                : icon == MFD_ICON_DOUBLE_LEFT ? 270 : 90;
        icon_poly(dl, dbl_a, 3, 17.5f, deg, 1.0f, x, y, c);
        icon_poly(dl, dbl_b, 3, 17.5f, deg, 1.0f, x, y, c);
        break;
    }
    case MFD_ICON_FLAT_SINGLE_UP: icon_poly(dl, flat, 3, 17.5f, 0, 1.0f, x, y, c); break;
    case MFD_ICON_FLAT_SINGLE_DOWN: icon_poly(dl, flat, 3, 17.5f, 180, 1.0f, x, y, c); break;
    case MFD_ICON_FLAT_SINGLE_LEFT: icon_poly(dl, flat, 3, 17.5f, 270, 1.0f, x, y, c); break;
    case MFD_ICON_FLAT_SINGLE_RIGHT: icon_poly(dl, flat, 3, 17.5f, 90, 1.0f, x, y, c); break;
    case MFD_ICON_ECL_SINGLE_UP:
    case MFD_ICON_ECL_SINGLE_DOWN: {
        int deg = icon == MFD_ICON_ECL_SINGLE_UP ? 0 : 180;
        MfdPoint p[3];
        p[0] = icon_pt(3.0f, 32.0f, 17.5f, 17.5f, deg, 1.0f, x, y);
        p[1] = icon_pt(17.5f, 3.0f, 17.5f, 17.5f, deg, 1.0f, x, y);
        p[2] = icon_pt(32.0f, 32.0f, 17.5f, 17.5f, deg, 1.0f, x, y);
        mfd_draw_polyline(dl, p, 3, 3.0f, 1, c);
        break;
    }
    }
}

void mfd_draw_cursor(MfdDrawList *dl, float x, float y, int rotated)
{
    static const float upper[] = {0, 0, 40, 35, 80, 0};
    static const float lower[] = {0, 80, 40, 45, 80, 80};
    MfdPoint p[3];
    int i, deg = rotated ? 90 : 0;
    float ox = x - 40.0f, oy = y - 40.0f;
    MfdPoint a, b;
    for (i = 0; i < 3; ++i) p[i] = icon_pt(upper[2 * i], upper[2 * i + 1], 40.0f, 40.0f, deg, 1.0f, ox, oy);
    mfd_draw_polyline(dl, p, 3, 3.0f, 0, MFD_COL_YELLOW);
    for (i = 0; i < 3; ++i) p[i] = icon_pt(lower[2 * i], lower[2 * i + 1], 40.0f, 40.0f, deg, 1.0f, ox, oy);
    mfd_draw_polyline(dl, p, 3, 3.0f, 0, MFD_COL_YELLOW);
    a = icon_pt(40, 39, 40, 40, deg, 1.0f, ox, oy); b = icon_pt(40, 41, 40, 40, deg, 1.0f, ox, oy);
    mfd_draw_line(dl, a.x, a.y, b.x, b.y, 2.0f, MFD_COL_YELLOW);
    a = icon_pt(39, 40, 40, 40, deg, 1.0f, ox, oy); b = icon_pt(41, 40, 40, 40, deg, 1.0f, ox, oy);
    mfd_draw_line(dl, a.x, a.y, b.x, b.y, 2.0f, MFD_COL_YELLOW);
}

/* ======================================================================================== */
/* Dropdown list (Button menu, DropdownMenu list, page-selector menu)                        */
/* ======================================================================================== */

typedef const char *(*ListItemFn)(const void *ctx, int i, int *disabled);

static const char *menu_item_get(const void *ctx, int i, int *disabled)
{
    const MfdMenuItem *items = (const MfdMenuItem *)ctx;
    if (disabled) *disabled = items[i].disabled;
    return items[i].label ? items[i].label : "";
}

static float list_outer_h(int n)
{
    float content = MENU_ROW_H * (float)n;
    float h = content + 4.0f;
    return h > MENU_MAX_H ? MENU_MAX_H : h;
}

static float list_width(float container_w, ListItemFn get, const void *ctx, int n)
{
    int i;
    float maxw = 0.0f, w;
    int scroll = (MENU_ROW_H * (float)n + 4.0f) > MENU_MAX_H;
    for (i = 0; i < n; ++i) {
        float tw = mfd_text_width(get(ctx, i, NULL), MFD_FS_MENU);
        if (tw > maxw) maxw = tw;
    }
    w = maxw + 2.0f * 16.0f + 2.0f * 3.0f + 4.0f + (scroll ? MENU_SCROLLBAR_W : 0.0f);
    return w > container_w ? w : container_w;
}

static void open_menu(MfdUi *ui, MfdId id, int scroll_to)
{
    ui->open_menu = id;
    ui->menu_scroll = 0.0f;
    ui->menu_scroll_to = scroll_to;
    ui->menu_first_frame = 1;
}

/* Draws an open list at (x, y) of width w at layer 60 and returns the clicked enabled item. */
static int draw_list(MfdUi *ui, MfdId id, float x, float y, float w, ListItemFn get, const void *ctx, int n,
                     int centered)
{
    MfdDrawList *dl = ui->dl;
    MfdLayer old = mfd_draw_get_layer(dl);
    float h = list_outer_h(n), view_h = h - 4.0f, content_h = MENU_ROW_H * (float)n;
    int scroll = content_h > view_h + 0.01f;
    float row_w = w - 4.0f - (scroll ? MENU_SCROLLBAR_W : 0.0f);
    float max_scroll = scroll ? content_h - view_h : 0.0f;
    int i, part, clicked = -1, notches;
    MfdRect outer = mfd_rect(x, y, w, h);

    if (ui->menu_first_frame && ui->menu_scroll_to >= 0) ui->menu_scroll = MENU_ROW_H * (float)ui->menu_scroll_to;
    ui->menu_first_frame = 0;
    ui->menu_scroll_to = -1;
    notches = mfd_ui_wheel_notches(ui, id);
    if (notches && ui->wheel_part != MFD_PART_MAIN) ui->menu_scroll -= MENU_ROW_H * (float)notches;
    if (ui->menu_scroll > max_scroll) ui->menu_scroll = max_scroll;
    if (ui->menu_scroll < 0.0f) ui->menu_scroll = 0.0f;
    ui->menu_scroll_max = max_scroll;

    if (mfd_ui_clicked_part(ui, id, &part) && part >= 0 && part < n) {
        int dis = 0;
        get(ctx, part, &dis);
        if (!dis) clicked = part;
    }

    mfd_draw_set_layer(dl, MFD_LAYER_MENU);
    mfd_ui_hit(ui, id, MFD_PART_BLOCKER, outer);
    mfd_draw_box(dl, outer, MFD_COL_DARKER_GREY, 2.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_RAISED);
    mfd_draw_clip_push(dl, mfd_rect(x + 2.0f, y + 2.0f, w - 4.0f, view_h));
    for (i = 0; i < n; ++i) {
        int dis = 0;
        const char *label = get(ctx, i, &dis);
        MfdRect row = mfd_rect(x + 2.0f, y + 2.0f + MENU_ROW_H * (float)i - ui->menu_scroll, row_w, MENU_ROW_H);
        if (row.y + row.h < y || row.y > y + h) continue;
        if (!dis && mfd_ui_is_hovered_part(ui, id, i)) mfd_draw_bevel(dl, row, 3.0f, MFD_COL_CYAN, MFD_BEVEL_SOLID);
        if (centered) {
            mfd_draw_text(dl, MFD_FONT_SZ, MFD_FS_MENU, dis ? MFD_COL_DARK_GREY : MFD_COL_WHITE, MFD_ANCHOR_CENTER,
                          row.x + row.w * 0.5f, row.y + 24.0f, label);
        } else {
            mfd_draw_clip_push(dl, mfd_rect(row.x + 3.0f, row.y, row.w - 6.0f, row.h));
            mfd_draw_text(dl, MFD_FONT_SZ, MFD_FS_MENU, dis ? MFD_COL_DARK_GREY : MFD_COL_WHITE, MFD_ANCHOR_LEFT,
                          row.x + 19.0f, row.y + 24.0f, label);
            mfd_draw_clip_pop(dl);
        }
        mfd_ui_hit(ui, id, i, row);
    }
    mfd_draw_clip_pop(dl);
    if (scroll) {
        float track_x = x + w - 2.0f - MENU_SCROLLBAR_W;
        float thumb_h = view_h * view_h / content_h;
        float thumb_y = y + 2.0f + (max_scroll > 0.0f ? ui->menu_scroll / max_scroll * (view_h - thumb_h) : 0.0f);
        mfd_draw_fill_rect(dl, mfd_rect(track_x, y + 2.0f, MENU_SCROLLBAR_W, view_h), MFD_COL_DARKER_GREY);
        mfd_draw_fill_rect(dl, mfd_rect(track_x, thumb_y, MENU_SCROLLBAR_W, thumb_h), MFD_COL_LIGHT_GREY);
        mfd_ui_hit(ui, id, MFD_PART_SCROLLBAR, mfd_rect(track_x, y + 2.0f, MENU_SCROLLBAR_W, view_h));
    }
    mfd_draw_set_layer(dl, old);
    return clicked;
}

/* ======================================================================================== */
/* Button                                                                                   */
/* ======================================================================================== */

void mfd_button_desc_init(MfdButtonDesc *d, const char *label)
{
    memset(d, 0, sizeof(*d));
    d->label = label;
    d->color = MFD_COL_COUNT;
    d->scroll_to = -1;
}

float mfd_button_auto_width(const char *label)
{
    float w;
    text_lines(label, MFD_FS_BUTTON, &w);
    return w + 28.0f;
}

float mfd_menu_button_auto_width(const char *label)
{
    float w;
    text_lines(label, MFD_FS_BUTTON, &w);
    return w + 51.0f;
}

int mfd_button_ex(MfdUi *ui, MfdId id, MfdRect r, const MfdButtonDesc *d)
{
    MfdDrawList *dl = ui->dl;
    uint32_t f = d->flags;
    int has_menu = d->menu && d->menu_count > 0;
    int disabled = (f & MFD_BTN_DISABLED) != 0;
    int opened, hovered, part, result = MFD_BUTTON_NONE, lines, upward = 0;
    float maxw, pad_r, cx0, cx1, top, list_w = 0.0f, list_h = 0.0f, list_x = 0.0f, list_y = 0.0f;
    MfdColor face, border, text;
    MfdBevel bevel;

    if (f & MFD_BTN_HIDDEN) {
        if (ui->open_menu == id) ui->open_menu = 0;
        return MFD_BUTTON_NONE;
    }
    lines = text_lines(d->label, MFD_FS_BUTTON, &maxw);
    pad_r = d->pad_right > 0.0f ? d->pad_right : (has_menu ? 5.0f : 12.0f);
    if (r.w <= 0.0f) {
        if (has_menu && !(f & MFD_BTN_NO_ARROW)) r.w = maxw + 51.0f;
        else if (d->suffix && d->inner_w > 0.0f) r.w = d->inner_w + 14.0f + pad_r + 2.0f;
        else r.w = maxw + 14.0f + pad_r + 2.0f;
    }
    if (r.h <= 0.0f) r.h = mfd_button_height(lines);

    if (mfd_ui_clicked_part(ui, id, &part) && part == MFD_PART_MAIN && !disabled) {
        result = MFD_BUTTON_CLICKED;
        if (has_menu) {
            if (ui->open_menu == id) ui->open_menu = 0;
            else open_menu(ui, id, d->scroll_to);
        }
    }
    opened = has_menu && ui->open_menu == id;
    if (has_menu) {
        list_w = list_width(r.w, menu_item_get, d->menu, d->menu_count);
        list_h = list_outer_h(d->menu_count);
        list_x = (f & MFD_BTN_MENU_RIGHT) ? r.x + r.w - list_w : r.x;
        list_y = r.y + r.h;
        if (list_y + list_h > (float)MFD_H) {
            upward = 1;
            list_y = r.y - list_h;
        }
    }

    hovered = mfd_ui_is_hovered_part(ui, id, MFD_PART_MAIN);
    if (f & MFD_BTN_HIGHLIGHTED) face = MFD_COL_WHITE;
    else if ((f & MFD_BTN_SELECTED) || opened) face = MFD_COL_DARK_GREY;
    else face = MFD_COL_DARKER_GREY;
    bevel = ((f & MFD_BTN_SELECTED) || opened) ? MFD_BEVEL_SUNKEN : MFD_BEVEL_RAISED;
    border = (hovered && !disabled) ? MFD_COL_CYAN : MFD_COL_LIGHT_GREY;
    if (d->color != MFD_COL_COUNT) text = d->color;
    else if (f & MFD_BTN_HIGHLIGHTED) text = MFD_COL_DARKER_GREY;
    else if (disabled) text = MFD_COL_DARK_GREY;
    else if (f & MFD_BTN_TEXT_YELLOW) text = MFD_COL_YELLOW;
    else text = MFD_COL_WHITE;
    if (disabled && d->color != MFD_COL_COUNT) text = MFD_COL_DARK_GREY;

    mfd_draw_box(dl, r, face, 2.0f, border, bevel);
    /* Content box: border 2 + padding 9/12/5/pad_r. Text block vertically centred. */
    cx0 = r.x + 14.0f;
    cx1 = r.x + r.w - 2.0f - pad_r;
    top = r.y + 11.0f + (r.h - 18.0f - 22.0f * (float)lines) * 0.5f;
    if (has_menu && !(f & MFD_BTN_NO_ARROW)) {
        /* label slot (padding-left 5, padding-right 10) + 15x15 arrow, vertically centred */
        float slot0 = cx0 + 5.0f, slot1 = cx1 - 15.0f - 10.0f;
        float ay = r.y + 2.0f + 9.0f + (r.h - 18.0f - 15.0f) * 0.5f;
        MfdColor ac = disabled ? MFD_COL_CSS_GREY : MFD_COL_WHITE;
        draw_lines_centered(dl, MFD_FONT_SZ, MFD_FS_BUTTON, text, (slot0 + slot1) * 0.5f,
                            top + MFD_FONT_BASELINE * MFD_FS_BUTTON, 22.0f, d->label);
        if (upward) mfd_draw_triangle_up(dl, cx1 - 15.0f, ay, ac);
        else mfd_draw_triangle_down(dl, cx1 - 15.0f, ay, ac);
    } else if (d->suffix) {
        float row_w = d->inner_w > 0.0f ? d->inner_w : (cx1 - cx0);
        float rx0 = (cx0 + cx1 - row_w) * 0.5f;
        float b = top + MFD_FONT_BASELINE * MFD_FS_BUTTON;
        mfd_draw_text(dl, MFD_FONT_SZ, MFD_FS_BUTTON, text, MFD_ANCHOR_LEFT, rx0 + d->label_dx, b, d->label);
        mfd_draw_text(dl, MFD_FONT_SZ, MFD_FS_BUTTON, text, MFD_ANCHOR_RIGHT, rx0 + row_w, b, d->suffix);
    } else {
        draw_lines_centered(dl, MFD_FONT_SZ, MFD_FS_BUTTON, text, (cx0 + cx1) * 0.5f,
                            top + MFD_FONT_BASELINE * MFD_FS_BUTTON, 22.0f, d->label);
    }
    mfd_ui_hit(ui, id, MFD_PART_MAIN, r);

    if (opened) {
        int k = draw_list(ui, id, list_x, list_y, list_w, menu_item_get, d->menu, d->menu_count, 0);
        if (k >= 0) {
            ui->open_menu = 0;
            result = k;
        }
    }
    return result;
}

int mfd_button(MfdUi *ui, MfdId id, MfdRect r, const char *label, uint32_t flags)
{
    MfdButtonDesc d;
    mfd_button_desc_init(&d, label);
    d.flags = flags;
    return mfd_button_ex(ui, id, r, &d) == MFD_BUTTON_CLICKED;
}

int mfd_menu_button(MfdUi *ui, MfdId id, MfdRect r, const char *label, const MfdMenuItem *items, int n,
                    uint32_t flags)
{
    MfdButtonDesc d;
    int k;
    mfd_button_desc_init(&d, label);
    d.flags = flags;
    d.menu = items;
    d.menu_count = n;
    k = mfd_button_ex(ui, id, r, &d);
    return k >= 0 ? k : -1;
}

int mfd_icon_button(MfdUi *ui, MfdId id, MfdRect r, MfdIcon icon, int disabled)
{
    int part, hovered = !disabled && mfd_ui_is_hovered_part(ui, id, MFD_PART_MAIN);
    mfd_draw_box(ui->dl, r, MFD_COL_DARKER_GREY, 2.0f, hovered ? MFD_COL_CYAN : MFD_COL_LIGHT_GREY,
                 MFD_BEVEL_RAISED);
    mfd_draw_icon(ui->dl, icon, r.x + (r.w - 35.0f) * 0.5f, r.y + (r.h - 35.0f) * 0.5f,
                  disabled ? MFD_COL_CSS_GREY : MFD_COL_WHITE);
    mfd_ui_hit(ui, id, MFD_PART_MAIN, r);
    return !disabled && mfd_ui_clicked_part(ui, id, &part) && part == MFD_PART_MAIN;
}

/* ======================================================================================== */
/* InputField                                                                               */
/* ======================================================================================== */

void mfd_field_desc_init(MfdFieldDesc *d, MfdFmtKind kind)
{
    memset(d, 0, sizeof(*d));
    mfd_fmt_init(&d->fmt, kind);
    d->align = MFD_ANCHOR_CENTER;
}

static void to_value(const MfdFieldDesc *d, const MfdFieldValue *v, MfdValue *out)
{
    memset(out, 0, sizeof(*out));
    if (!v || !v->has_value) return;
    out->has = 1;
    out->num = v->num;
    if (v->text) {
        size_t n = strlen(v->text);
        if (n >= sizeof(out->str)) n = sizeof(out->str) - 1;
        memcpy(out->str, v->text, n);
        out->str[n] = '\0';
    } else if (mfd_fmt_is_text(d->fmt.kind)) {
        out->has = 0;
    }
}

static int field_char_limit(const MfdFieldDesc *d)
{
    int md = mfd_fmt_max_digits(&d->fmt);
    if (d->flags & MFD_FIELD_FREE_TEXT) return MFD_FIELD_MAX_CHARS;
    if (mfd_fmt_max_overflow(&d->fmt) > 0) return md + mfd_fmt_max_overflow(&d->fmt);
    return md > MFD_FIELD_MAX_CHARS ? MFD_FIELD_MAX_CHARS : md;
}

static int field_can_overflow(const MfdFieldDesc *d)
{
    return (d->flags & MFD_FIELD_FREE_TEXT) || mfd_fmt_max_overflow(&d->fmt) > 0;
}

static float field_overflow_width(const MfdFieldDesc *d)
{
    int n;
    if (d->flags & MFD_FIELD_FREE_TEXT) n = MFD_FIELD_MAX_CHARS;
    else n = mfd_fmt_max_overflow(&d->fmt) + mfd_fmt_max_digits(&d->fmt) +
             mfd_utf8_count(mfd_fmt_error_unit(&d->fmt)) + 1;
    return 18.0f * (float)n;
}

/* Replace every '-' with the mandatory glyph. */
static void mandatory_text(const char *src, const char *glyph, char *dst, size_t n)
{
    size_t o = 0, gl = strlen(glyph);
    for (; *src && o + 1 < n; ++src) {
        if (*src == '-') {
            if (o + gl + 1 > n) break;
            memcpy(dst + o, glyph, gl);
            o += gl;
        } else {
            dst[o++] = *src;
        }
    }
    dst[o] = '\0';
}

static MfdValidating *val_find(MfdUi *ui, MfdId id)
{
    int i;
    for (i = 0; i < MFD_UI_VALIDATING; ++i) {
        if (ui->val[i].id == id) return &ui->val[i];
    }
    return NULL;
}

static MfdValidating *val_alloc(MfdUi *ui, MfdId id)
{
    int i, oldest = 0;
    MfdValidating *v = val_find(ui, id);
    if (v) return v;
    for (i = 0; i < MFD_UI_VALIDATING; ++i) {
        if (!ui->val[i].id) return &ui->val[i];
        if (ui->val[i].until < ui->val[oldest].until) oldest = i;
    }
    return &ui->val[oldest];
}

static void field_focus(MfdUi *ui, MfdId id, int external)
{
    ui->focus_id = id;
    ui->edit = MFD_EDIT_FOCUSED;
    ui->buf[0] = '\0';
    ui->edit_t0 = ui->now;
    ui->overflow = 0;
    ui->blur_pending = 0;
    ui->focus_external = external;
    ui->focus_seen = 1;
    ui->last_key_time = ui->now;
    ui->kbd_capture = !ui->kbd_mode;
}

/* Commit with validation (ENT or mouse-down outside). The focus is released. */
static void field_commit(MfdUi *ui, MfdId id, const MfdFieldDesc *d, const MfdValue *cur)
{
    char input[MFD_FIELD_BUF];
    MfdValue nv;
    MfdFmsError err;
    MfdParseStatus st;
    MfdValidating *slot;
    if (ui->edit == MFD_EDIT_FOCUSED) {
        if (cur->has) {
            MfdFieldText ft;
            mfd_fmt_render(&d->fmt, cur, &ft);
            snprintf(input, sizeof(input), "%s", ft.value);
        } else {
            input[0] = '\0';
        }
    } else {
        snprintf(input, sizeof(input), "%s", ui->buf);
    }
    drop_focus(ui);
    if ((d->flags & MFD_FIELD_NOT_CLEARABLE) && input[0] == '\0') return; /* [PORT] canBeCleared */
    memset(&nv, 0, sizeof(nv));
    memset(&err, 0, sizeof(err));
    st = mfd_fmt_parse(&d->fmt, input, &nv, &err);
    if (st == MFD_PARSE_ERROR) {
        if (!(d->flags & MFD_FIELD_NO_ERRORS)) mfd_ui_error(ui, &err, d->fmt.err_details);
        return;
    }
    slot = val_alloc(ui, id);
    memset(slot, 0, sizeof(*slot));
    slot->id = id;
    slot->until = ui->now + VALIDATE_S;
    slot->status = st;
    slot->value = nv;
    if (st == MFD_PARSE_CLEAR) slot->value.has = 0;
    snprintf(slot->text, sizeof(slot->text), "%s", input);
    slot->seen = 1;
}

/* Apply this frame's key to the focused field. Returns 1 if the field committed or cancelled. */
static int field_key(MfdUi *ui, MfdId id, const MfdFieldDesc *d, const MfdValue *cur)
{
    MfdKey k;
    char ch;
    size_t len;
    int limit = field_char_limit(d);
    if (!mfd_ui_peek_key(ui, &k, &ch)) return 0;
    len = strlen(ui->buf);
    switch (k) {
    case MFD_KEY_CHAR:
        ch = (char)toupper((unsigned char)ch);
        if (ch == ',') ch = '.';
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '/' || ch == '.' ||
              ch == '+' || ch == '-')) {
            break;
        }
        if (ui->edit == MFD_EDIT_FOCUSED) {
            ui->buf[0] = '\0';
            len = 0;
            ui->edit = MFD_EDIT_EDITING;
            ui->edit_t0 = ui->now;
        }
        if ((int)len < limit && len + 1 < sizeof(ui->buf)) {
            ui->buf[len] = ch;
            ui->buf[len + 1] = '\0';
            ++len;
        }
        ui->overflow |= field_can_overflow(d) && (int)len >= mfd_fmt_max_digits(&d->fmt);
        break;
    case MFD_KEY_PLUSMINUS:
        if (ui->edit == MFD_EDIT_FOCUSED) {
            ui->buf[0] = '\0';
            len = 0;
            ui->edit = MFD_EDIT_EDITING;
            ui->edit_t0 = ui->now;
        }
        if (len > 0 && ui->buf[0] == '+') ui->buf[0] = '-';
        else if (len > 0 && ui->buf[0] == '-') ui->buf[0] = '+';
        else if ((int)len < limit) { /* [PORT] apply the length limit */
            memmove(ui->buf + 1, ui->buf, len + 1);
            ui->buf[0] = '-';
            ++len;
        }
        ui->overflow |= field_can_overflow(d) && (int)len >= mfd_fmt_max_digits(&d->fmt);
        break;
    case MFD_KEY_BACKSPACE:
        if (ui->edit == MFD_EDIT_FOCUSED) {
            if (!(d->flags & MFD_FIELD_NOT_CLEARABLE)) { /* [PORT] canBeCleared enforced */
                ui->buf[0] = '\0';
                ui->edit = MFD_EDIT_EDITING;
                ui->edit_t0 = ui->now;
            }
        } else if (len > 0) {
            ui->buf[len - 1] = '\0';
        }
        break;
    case MFD_KEY_ENT:
        mfd_ui_consume_key(ui);
        field_commit(ui, id, d, cur);
        return 1;
    case MFD_KEY_ESC:
    case MFD_KEY_ESC2:
        mfd_ui_consume_key(ui); /* [PORT] ESC cancels: no validation, display restored */
        drop_focus(ui);
        return 1;
    case MFD_KEY_UP: case MFD_KEY_DOWN: case MFD_KEY_LEFT: case MFD_KEY_RIGHT:
        break; /* ignored */
    default:
        return 0; /* page keys etc. are not field keys */
    }
    ui->last_key_time = ui->now;
    if (!ui->kbd_mode) ui->kbd_capture = 1;
    mfd_ui_consume_key(ui);
    return 0;
}

typedef struct {
    int focused_unmod, editing, validating, inactive, disabled, hovered, overflow, caret;
} FieldLook;

/* Draws the field box and text. Returns the outer rect. */
static MfdRect field_render(MfdUi *ui, float x, float y, const MfdFieldDesc *d, const MfdFieldValue *v,
                            const MfdValue *cur, const FieldLook *lk, const char *val_text)
{
    MfdDrawList *dl = ui->dl;
    MfdFieldText ft;
    char shown[MFD_FIELD_BUF * 3 + 4];
    const char *glyph = d->mandatory_glyph ? d->mandatory_glyph : GLYPH_BOX_V;
    float unit_fs = (d->flags & MFD_FIELD_BIG_UNIT) ? 24.0f : 20.0f;
    float lead_w, trail_w, text_w, fs, ta0, ta1, tx, w, bl;
    int md = mfd_fmt_max_digits(&d->fmt);
    int is_placeholder = 0, limit = field_char_limit(d);
    int at_limit = 0;
    MfdColor col, bg = MFD_COL_COUNT;
    MfdLayer old = mfd_draw_get_layer(dl);
    MfdRect outer;

    mfd_fmt_render(&d->fmt, cur, &ft);
    lead_w = mfd_text_width(ft.lead, unit_fs);
    trail_w = mfd_text_width(ft.trail, unit_fs);

    if (lk->editing) {
        size_t n = strlen(ui->buf);
        at_limit = lk->caret && (int)mfd_utf8_count(ui->buf) >= limit && n > 0;
        snprintf(shown, sizeof(shown), "%s", ui->buf);
        if (at_limit) shown[mfd_utf8_offset(shown, mfd_utf8_count(shown) - 1)] = '\0';
        fs = MFD_FS_INPUT;
    } else if (lk->validating) {
        snprintf(shown, sizeof(shown), "%s", val_text);
        fs = (v && v->has_value && v->is_computed) ? MFD_FS_INPUT_COMPUTED : MFD_FS_INPUT;
    } else {
        is_placeholder = !cur->has || ft.is_placeholder;
        if (is_placeholder && (d->flags & MFD_FIELD_MANDATORY) && !lk->inactive && !lk->disabled) {
            mandatory_text(ft.value, glyph, shown, sizeof(shown));
        } else {
            snprintf(shown, sizeof(shown), "%s", ft.value);
        }
        fs = (!lk->focused_unmod && v && v->is_computed) ? MFD_FS_INPUT_COMPUTED : MFD_FS_INPUT;
    }
    text_w = mfd_text_width(shown, fs) + ((lk->editing && lk->caret) ? 18.0f : 0.0f);

    if (lk->overflow) {
        w = field_overflow_width(d);
    } else if (d->width > 0.0f) {
        w = d->width;
    } else {
        float ta = 18.0f * (float)md;
        w = 4.0f + lead_w + 6.0f + (text_w > ta ? text_w : ta) + 6.0f + trail_w + 4.0f;
    }
    outer = mfd_rect(x, y, w, MFD_FIELD_H);

    /* Colours (spec 01 section 13.4.4) */
    if (lk->inactive) col = MFD_COL_GREEN;
    else if (lk->validating) col = MFD_COL_LIGHT_GREY;
    else if (lk->disabled) col = (d->flags & MFD_FIELD_YELLOW_WHEN_DISABLED) ? MFD_COL_YELLOW : MFD_COL_DARK_GREY;
    else if (lk->focused_unmod) {
        col = MFD_COL_BLACK;
        bg = (d->flags & MFD_FIELD_TMPY) ? MFD_COL_YELLOW : MFD_COL_CYAN;
    } else if (lk->editing) col = (d->flags & MFD_FIELD_TMPY) ? MFD_COL_YELLOW : MFD_COL_CYAN;
    else if (is_placeholder && (d->flags & MFD_FIELD_MANDATORY)) col = MFD_COL_AMBER;
    else if (d->flags & MFD_FIELD_TMPY) col = MFD_COL_YELLOW;
    else col = MFD_COL_CYAN;

    if (lk->overflow) {
        mfd_draw_set_layer(dl, MFD_LAYER_OVERFLOW);
        mfd_draw_fill_rect(dl, outer, MFD_COL_BLACK);
        mfd_draw_bevel(dl, outer, 1.0f, MFD_COL_DARK_GREY, MFD_BEVEL_SOLID);
    } else if (lk->inactive) {
        mfd_draw_fill_rect(dl, outer, MFD_COL_BLACK);
    } else {
        mfd_draw_fill_rect(dl, outer, lk->disabled ? MFD_COL_DARKER_GREY : MFD_COL_BLACK);
        if (!(d->flags & MFD_FIELD_TRANSPARENT_BORDER)) {
            mfd_draw_bevel(dl, outer, 2.0f, (lk->hovered && !lk->disabled) ? MFD_COL_CYAN : MFD_COL_LIGHT_GREY,
                           MFD_BEVEL_SUNKEN);
        }
    }

    /* Horizontal layout: [lead][6][text area][6][trail] inside border 2 + padding 2 (1 + 2 in overflow). */
    {
        float inset = lk->overflow ? 3.0f : 4.0f;
        ta0 = x + inset + lead_w + 6.0f;
        ta1 = x + w - inset - trail_w - 6.0f;
        if (ft.lead[0]) {
            mfd_draw_text(dl, MFD_FONT_SZ, unit_fs, MFD_COL_DARK_BLUE, MFD_ANCHOR_LEFT, x + inset,
                          y + 18.5f - unit_fs * 0.5f + MFD_FONT_BASELINE * unit_fs, ft.lead);
        }
        if (ft.trail[0]) {
            mfd_draw_text(dl, MFD_FONT_SZ, unit_fs, MFD_COL_DARK_BLUE, MFD_ANCHOR_LEFT, ta1 + 6.0f,
                          y + 18.5f - unit_fs * 0.5f + MFD_FONT_BASELINE * unit_fs, ft.trail);
        }
    }
    if (lk->editing || d->align == MFD_ANCHOR_LEFT) tx = ta0;
    else if (d->align == MFD_ANCHOR_RIGHT) tx = ta1 - text_w;
    else tx = (ta0 + ta1 - text_w) * 0.5f;
    /* 27 px line box centred in the 25 px content (top y + 5); smaller fonts get half-leading. */
    bl = y + 5.0f + (27.0f - fs) * 0.5f + MFD_FONT_BASELINE * fs;
    if (bg != MFD_COL_COUNT && shown[0]) {
        mfd_draw_fill_rect(dl, mfd_rect(tx, y + 5.0f, mfd_text_width(shown, fs), 27.0f), bg);
    }
    mfd_draw_text(dl, MFD_FONT_SZ, fs, col, MFD_ANCHOR_LEFT, tx, bl, shown);
    if (lk->editing && lk->caret) {
        double phase = fmod(ui->now - ui->edit_t0, 1.0);
        int on = phase >= 0.5;
        float cx = tx + mfd_text_width(shown, fs);
        MfdColor ccol = (d->flags & MFD_FIELD_TMPY) ? MFD_COL_YELLOW : MFD_COL_CYAN;
        if (on) mfd_draw_fill_rect(dl, mfd_rect(cx, y + 5.0f, 18.0f, 27.0f), ccol);
        if (at_limit) {
            const char *last = ui->buf + mfd_utf8_offset(ui->buf, mfd_utf8_count(ui->buf) - 1);
            mfd_draw_text(dl, MFD_FONT_SZ, MFD_FS_INPUT, on ? MFD_COL_BLACK : ccol, MFD_ANCHOR_CENTER, cx + 9.0f,
                          bl, last);
        }
    }
    mfd_draw_set_layer(dl, old);
    return outer;
}

float mfd_field_auto_width(const MfdFieldDesc *d, const MfdFieldValue *v)
{
    MfdValue cur;
    MfdFieldText ft;
    float unit_fs = (d->flags & MFD_FIELD_BIG_UNIT) ? 24.0f : 20.0f;
    float ta = 18.0f * (float)mfd_fmt_max_digits(&d->fmt), tw;
    if (d->width > 0.0f) return d->width;
    to_value(d, v, &cur);
    mfd_fmt_render(&d->fmt, &cur, &ft);
    tw = mfd_text_width(ft.value, (v && v->is_computed) ? MFD_FS_INPUT_COMPUTED : MFD_FS_INPUT);
    return 4.0f + mfd_text_width(ft.lead, unit_fs) + 6.0f + (tw > ta ? tw : ta) + 6.0f +
           mfd_text_width(ft.trail, unit_fs) + 4.0f;
}

MfdFieldResult mfd_input_field(MfdUi *ui, MfdId id, float x, float y, const MfdFieldDesc *d,
                               const MfdFieldValue *v, MfdValue *out)
{
    MfdValue cur;
    FieldLook lk;
    MfdValidating *slot;
    MfdFieldResult result = MFD_FIELD_NONE;
    MfdRect outer;
    int part;
    int focusable = !(d->flags & (MFD_FIELD_INACTIVE | MFD_FIELD_DISABLED));

    to_value(d, v, &cur);
    memset(&lk, 0, sizeof(lk));
    lk.inactive = (d->flags & MFD_FIELD_INACTIVE) != 0;
    lk.disabled = (d->flags & MFD_FIELD_DISABLED) != 0;

    if (ui->focus_id == id) {
        ui->focus_seen = 1;
        if (!focusable) {
            drop_focus(ui);
        } else if (ui->blur_pending) {
            ui->blur_pending = 0;
            field_commit(ui, id, d, &cur);
        } else {
            field_key(ui, id, d, &cur);
        }
    }

    slot = val_find(ui, id);
    if (slot) {
        slot->seen = 1;
        if (ui->now >= slot->until) {
            result = slot->status == MFD_PARSE_OK ? MFD_FIELD_COMMITTED : MFD_FIELD_CLEARED;
            if (out) *out = slot->value;
            slot->id = 0;
            slot = NULL;
        }
    }

    if (ui->focus_id != id && !slot && focusable && mfd_ui_clicked_part(ui, id, &part) && part == MFD_PART_MAIN) {
        field_focus(ui, id, 0);
        /* An idle value longer than maxDigits opens in overflow mode. */
        if (field_can_overflow(d) && cur.has) {
            MfdFieldText ft;
            mfd_fmt_render(&d->fmt, &cur, &ft);
            ui->overflow = mfd_utf8_count(ft.value) > mfd_fmt_max_digits(&d->fmt);
        }
    }

    lk.focused_unmod = ui->focus_id == id && ui->edit == MFD_EDIT_FOCUSED;
    lk.editing = ui->focus_id == id && ui->edit == MFD_EDIT_EDITING;
    lk.caret = lk.editing && ui->buf[0] != '\0';
    lk.validating = slot != NULL;
    lk.hovered = focusable && mfd_ui_is_hovered(ui, id);
    lk.overflow = ui->focus_id == id && ui->overflow;
    if (!lk.overflow && !lk.validating && ui->focus_id != id && field_can_overflow(d) && cur.has) {
        MfdFieldText ft;
        mfd_fmt_render(&d->fmt, &cur, &ft);
        lk.overflow = mfd_utf8_count(ft.value) > mfd_fmt_max_digits(&d->fmt);
    }

    outer = field_render(ui, x, y, d, v, &cur, &lk, slot ? slot->text : "");
    if (lk.overflow) {
        MfdLayer old = mfd_draw_get_layer(ui->dl);
        mfd_draw_set_layer(ui->dl, MFD_LAYER_OVERFLOW);
        mfd_ui_hit(ui, id, MFD_PART_MAIN, outer);
        mfd_draw_set_layer(ui->dl, old);
    } else {
        mfd_ui_hit(ui, id, MFD_PART_MAIN, outer);
    }
    return result;
}

/* ======================================================================================== */
/* DropdownMenu                                                                             */
/* ======================================================================================== */

typedef struct {
    const MfdDropdownDesc *d;
    int map[64];
    int n;
} DropdownFilter;

static const char *dropdown_item_get(const void *ctx, int i, int *disabled)
{
    const DropdownFilter *f = (const DropdownFilter *)ctx;
    if (disabled) *disabled = 0;
    return f->d->values[f->map[i]] ? f->d->values[f->map[i]] : "";
}

int mfd_dropdown(MfdUi *ui, MfdId id, MfdRect r, const MfdDropdownDesc *d, int *out_index, char *out_text, size_t n)
{
    MfdDrawList *dl = ui->dl;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue cur;
    FieldLook lk;
    DropdownFilter filt;
    MfdValidating *slot;
    int part, modified = 0, i, digits = d->digits > 0 ? d->digits : 6;
    int usable = !d->inactive && !d->disabled;
    int open, hovered;
    float W = r.w, field_w, field_x;
    MfdRect outer = mfd_rect(r.x, r.y, W, MFD_DROPDOWN_H);

    mfd_field_desc_init(&fd, MFD_FMT_TXT_DROPDOWN);
    fd.fmt.max_digits = digits;
    fd.flags = MFD_FIELD_TRANSPARENT_BORDER | MFD_FIELD_NO_ERRORS | (d->free_text ? MFD_FIELD_FREE_TEXT : 0u) |
               (d->tmpy ? MFD_FIELD_TMPY : 0u) | (d->inactive ? MFD_FIELD_INACTIVE : 0u) |
               (d->disabled ? MFD_FIELD_DISABLED : 0u);
    fd.align = d->align_left ? MFD_ANCHOR_LEFT : MFD_ANCHOR_CENTER;
    memset(&fv, 0, sizeof(fv));
    if (d->selected >= 0 && d->selected < d->count && d->values[d->selected]) {
        fv.has_value = 1;
        fv.text = d->values[d->selected];
    }
    to_value(&fd, &fv, &cur);

    /* Focused field handling (keys, outside mouse-down). */
    if (ui->focus_id == id) {
        ui->focus_seen = 1;
        if (!usable) {
            drop_focus(ui);
        } else if (ui->blur_pending) {
            /* Click outside while open: commit with validation (as ENT) if something was typed. */
            ui->blur_pending = 0;
            if (ui->edit == MFD_EDIT_EDITING && d->free_text) field_commit(ui, id, &fd, &cur);
            else drop_focus(ui);
            if (ui->open_menu == id) ui->open_menu = 0;
        } else if (field_key(ui, id, &fd, &cur)) {
            /* ENT committed (free text) or ESC cancelled: close the list. For non-free-text
             * dropdowns a commit without a list selection reverts the display ([PORT] quirk 6). */
            if (!d->free_text) {
                MfdValidating *s = val_find(ui, id);
                if (s) s->id = 0;
            }
            if (ui->open_menu == id) ui->open_menu = 0;
        }
    }
    slot = val_find(ui, id);
    if (slot) {
        slot->seen = 1;
        if (ui->now >= slot->until) {
            if (d->free_text) {
                modified = 1;
                if (out_index) *out_index = -1;
                if (out_text && n) snprintf(out_text, n, "%s", slot->status == MFD_PARSE_OK ? slot->value.str : "");
            }
            slot->id = 0;
            slot = NULL;
        }
    }

    /* Toggle on outer click. */
    if (usable && mfd_ui_clicked_part(ui, id, &part) && part == MFD_PART_MAIN) {
        if (ui->open_menu == id) {
            ui->open_menu = 0;
            if (ui->focus_id == id) drop_focus(ui); /* closing by toggle: blur without validation */
        } else {
            open_menu(ui, id, -1);
            field_focus(ui, id, 1);
        }
    }
    open = ui->open_menu == id;
    if (!open && ui->focus_id == id && ui->edit == MFD_EDIT_FOCUSED) drop_focus(ui);

    /* Filter (values that start with the typed text). */
    filt.d = d;
    filt.n = 0;
    for (i = 0; i < d->count && filt.n < 64; ++i) {
        const char *s = d->values[i] ? d->values[i] : "";
        if (ui->focus_id == id && ui->edit == MFD_EDIT_EDITING && strncmp(s, ui->buf, strlen(ui->buf)) != 0) continue;
        filt.map[filt.n++] = i;
    }

    /* Outer box */
    hovered = usable && mfd_ui_is_hovered_part(ui, id, MFD_PART_MAIN);
    if (d->inactive) {
        mfd_draw_fill_rect(dl, outer, MFD_COL_BLACK);
    } else {
        mfd_draw_box(dl, outer, MFD_COL_DARKER_GREY, 2.0f, hovered ? MFD_COL_CYAN : MFD_COL_LIGHT_GREY,
                     MFD_BEVEL_RAISED);
        mfd_draw_fill_rect(dl, mfd_rect(r.x + 2.0f, r.y + 2.0f, W - 29.0f, 40.0f), MFD_COL_BLACK);
        mfd_draw_triangle_down(dl, r.x + W - 14.5f - 7.5f, r.y + 14.5f,
                               d->disabled ? MFD_COL_CSS_GREY : MFD_COL_WHITE);
    }
    /* Embedded field (transparent border), centred vertically in the 40 px inner box. */
    memset(&lk, 0, sizeof(lk));
    lk.inactive = d->inactive;
    lk.disabled = d->disabled;
    lk.focused_unmod = ui->focus_id == id && ui->edit == MFD_EDIT_FOCUSED;
    lk.editing = ui->focus_id == id && ui->edit == MFD_EDIT_EDITING;
    lk.caret = lk.editing && ui->buf[0] != '\0';
    lk.validating = slot != NULL;
    field_w = mfd_field_auto_width(&fd, &fv);
    if (d->align_left) field_x = r.x + 2.0f;
    else field_x = r.x + 2.0f + ((W - 29.0f) - field_w) * 0.5f;
    mfd_draw_clip_push(dl, mfd_rect(r.x + 2.0f, r.y + 2.0f, W - 29.0f, 40.0f));
    field_render(ui, field_x, r.y + 3.5f, &fd, &fv, &cur, &lk, slot ? slot->text : "");
    mfd_draw_clip_pop(dl);
    mfd_ui_hit(ui, id, MFD_PART_MAIN, outer);

    if (open) {
        float lw = list_width(W, dropdown_item_get, &filt, filt.n);
        int k = draw_list(ui, id, r.x, r.y + MFD_DROPDOWN_H, lw, dropdown_item_get, &filt, filt.n, !d->align_left);
        if (k >= 0) {
            modified = 1;
            if (out_index) *out_index = filt.map[k];
            if (out_text && n) out_text[0] = '\0';
            ui->open_menu = 0;
            if (ui->focus_id == id) drop_focus(ui);
        }
    }
    return modified;
}

/* ======================================================================================== */
/* PageSelectorDropdownMenu                                                                 */
/* ======================================================================================== */

int mfd_page_selector(MfdUi *ui, MfdId id, MfdRect r, const char *label, const MfdMenuItem *items, int n,
                      int active, uint32_t flags)
{
    MfdDrawList *dl = ui->dl;
    int part, result = -1, lines, hovered;
    float maxw, lc0, lc1, cx, b0, tri = n > 1 ? 31.0f : 0.0f;

    if (mfd_ui_clicked_part(ui, id, &part) && part == MFD_PART_MAIN) {
        if (n > 1) {
            if (ui->open_menu == id) ui->open_menu = 0;
            else open_menu(ui, id, -1);
        } else if (n == 1 && !items[0].disabled) {
            result = 0;
        }
    }
    hovered = mfd_ui_is_hovered_part(ui, id, MFD_PART_MAIN);
    mfd_draw_fill_rect(dl, r, MFD_COL_DARKER_GREY);
    if (hovered) border_tlbr(dl, r, 2.0f, MFD_COL_CYAN, MFD_COL_CYAN);
    else border_tlbr(dl, r, 2.0f, MFD_COL_LIGHT_GREY, MFD_COL_SELECTOR_SHADOW);

    lines = text_lines(label, MFD_FS_PAGE_SELECTOR, &maxw);
    lc0 = r.x + 2.0f;
    lc1 = r.x + r.w - 2.0f - tri;
    cx = (lc0 + lc1) * 0.5f;
    if (lines <= 1) {
        float ytop = r.y + (r.h - 30.0f) * 0.5f; /* label border box 30 tall, centred */
        b0 = ytop + 4.0f + MFD_FONT_BASELINE * MFD_FS_PAGE_SELECTOR;
        if (flags & MFD_PSEL_EMER) {
            mfd_draw_fill_rect(dl, mfd_rect(cx - maxw * 0.5f, ytop + 4.0f, maxw, MFD_FS_PAGE_SELECTOR), MFD_COL_EMER);
        }
        if (active) mfd_draw_bevel(dl, mfd_rect(cx - maxw * 0.5f - 4.0f, ytop, maxw + 8.0f, 30.0f), 1.0f,
                                   MFD_COL_WHITE, MFD_BEVEL_SOLID);
    } else {
        float ytop = r.y + (r.h - 50.0f) * 0.5f; /* two-line label box 50 tall */
        b0 = ytop + 3.0f + MFD_FONT_BASELINE * MFD_FS_PAGE_SELECTOR;
        if (active) mfd_draw_bevel(dl, mfd_rect(cx - maxw * 0.5f - 4.0f, ytop, maxw + 8.0f, 50.0f), 1.0f,
                                   MFD_COL_WHITE, MFD_BEVEL_SOLID);
    }
    draw_lines_centered(dl, MFD_FONT_ECAM, MFD_FS_PAGE_SELECTOR, (flags & MFD_PSEL_EMER) ? MFD_COL_BLACK : MFD_COL_WHITE,
                        cx, b0, 22.0f, label);
    if (n > 1) mfd_draw_triangle_down(dl, r.x + r.w - 25.0f, r.y + 18.0f, MFD_COL_WHITE);
    mfd_ui_hit(ui, id, MFD_PART_MAIN, r);

    if (n > 1 && ui->open_menu == id) {
        float lw = list_width(r.w, menu_item_get, items, n);
        int k = draw_list(ui, id, r.x, r.y + r.h, lw, menu_item_get, items, n, 0);
        if (k >= 0) {
            ui->open_menu = 0;
            result = k;
        }
    }
    return result;
}

/* ======================================================================================== */
/* TopTabNavigator                                                                          */
/* ======================================================================================== */

void mfd_tabs_desc_init(MfdTabsDesc *d)
{
    memset(d, 0, sizeof(*d));
    d->bar_h = 36.0f;
    d->slant_deg = 30.0f;
    d->highlighted = -1;
    d->selected_text = MFD_COL_WHITE;
}

static void draw_tab(MfdUi *ui, MfdId id, int i, float xi, float T, float w, float h, float tw, int selected,
                     const char *title, float fs, MfdColor text_col)
{
    MfdDrawList *dl = ui->dl;
    MfdColor fill = selected ? MFD_COL_BLACK : MFD_COL_DARKER_GREY;
    float lab0 = xi + tw, lab1 = xi + w - tw;
    float lab_h = selected ? h : h - 2.0f;
    float content_top = T + 2.0f + 6.0f, content_h = lab_h - 2.0f - 6.0f - 2.0f;
    MfdRect label = mfd_rect(lab0, T, lab1 - lab0, lab_h);

    if (!selected) mfd_draw_fill_rect(dl, mfd_rect(xi, T + h - 2.0f, w, 2.0f), MFD_COL_LIGHT_GREY);
    /* left slant */
    mfd_draw_triangle(dl, xi, T + h, xi + tw, T, xi + tw, T + h, fill);
    mfd_draw_line(dl, xi, T + h, xi + tw, T, 2.0f, MFD_COL_CSS_LIGHTGREY);
    if (!selected) mfd_draw_line(dl, xi, T + h - 1.0f, xi + tw, T + h - 1.0f, 2.0f, MFD_COL_CSS_LIGHTGREY);
    /* label */
    mfd_draw_fill_rect(dl, label, fill);
    mfd_draw_fill_rect(dl, mfd_rect(lab0, T, lab1 - lab0, 2.0f), MFD_COL_LIGHT_GREY);
    if (mfd_ui_is_hovered(ui, id) && ui->hover_part == i) mfd_draw_bevel(dl, label, 2.0f, MFD_COL_CYAN, MFD_BEVEL_RAISED);
    mfd_draw_text(dl, MFD_FONT_SZ, fs, text_col, MFD_ANCHOR_CENTER, (lab0 + lab1) * 0.5f,
                  content_top + (content_h - fs) * 0.5f + MFD_FONT_BASELINE * fs, title);
    /* right slant */
    mfd_draw_triangle(dl, lab1, T, xi + w, T + h, lab1, T + h, fill);
    mfd_draw_line(dl, lab1, T, xi + w, T + h, 2.0f, MFD_COL_CSS_LIGHTGREY);
    if (!selected) mfd_draw_line(dl, lab1, T + h - 1.0f, xi + w, T + h - 1.0f, 2.0f, MFD_COL_CSS_LIGHTGREY);
    mfd_ui_hit(ui, id, i, mfd_rect(xi, T, w, h));
}

int mfd_top_tabs(MfdUi *ui, MfdId id, MfdRect region, const char *const *titles, int n, int selected,
                 const MfdTabsDesc *dd, MfdRect *content, MfdRect *clip)
{
    MfdDrawList *dl = ui->dl;
    MfdTabsDesc d;
    float xc = region.x + 2.0f, T = region.y + 2.0f, Wc = region.w - 4.0f;
    float h, tw, w, fs, H, fy;
    int i, part, clicked = -1;
    MfdPoint q[4];

    if (dd) d = *dd;
    else mfd_tabs_desc_init(&d);
    if (d.bar_h <= 0.0f) d.bar_h = 36.0f;
    if (d.slant_deg <= 0.0f) d.slant_deg = 30.0f;
    h = d.bar_h;
    tw = h * tanf(d.slant_deg * (float)M_PI / 180.0f);
    if (n < 1) n = 1;
    w = (Wc - d.right_space + 15.0f * (float)(n - 1)) / (float)n;
    fs = n <= 2 ? floorf(h * 0.66f + 0.5f) : floorf(h * 0.58f + 0.5f);

    if (mfd_ui_clicked_part(ui, id, &part) && part >= 0 && part < n && part != selected) clicked = part;

    for (i = 0; i < n; ++i) {
        MfdColor tc = (d.highlighted == i) ? MFD_COL_GREEN : MFD_COL_WHITE;
        if (i == selected) continue;
        draw_tab(ui, id, i, xc + (float)i * (w - 15.0f), T, w, h, tw, 0, titles[i], fs, tc);
    }
    if (selected >= 0 && selected < n) {
        MfdColor tc = (d.highlighted == selected) ? MFD_COL_GREEN : d.selected_text;
        draw_tab(ui, id, selected, xc + (float)selected * (w - 15.0f), T, w, h, tw, 1, titles[selected], fs, tc);
    }
    if (d.right_space > 0.0f) {
        mfd_draw_fill_rect(dl, mfd_rect(xc + Wc - d.right_space, T + h - 2.0f, d.right_space, 2.0f),
                           MFD_COL_CSS_LIGHTGREY);
    }

    /* Content frame: raised 2 px on left/right/bottom, no top. */
    fy = T + h;
    H = d.content_h > 0.0f ? d.content_h : (region.y + region.h - 2.0f) - fy;
    q[0].x = xc; q[0].y = fy; q[1].x = xc + 2.0f; q[1].y = fy; q[2].x = xc + 2.0f; q[2].y = fy + H - 2.0f;
    q[3].x = xc; q[3].y = fy + H;
    mfd_draw_polygon(dl, q, 4, MFD_COL_LIGHT_GREY);
    q[0].x = xc; q[0].y = fy + H; q[1].x = xc + 2.0f; q[1].y = fy + H - 2.0f; q[2].x = xc + Wc - 2.0f;
    q[2].y = fy + H - 2.0f; q[3].x = xc + Wc; q[3].y = fy + H;
    mfd_draw_polygon(dl, q, 4, MFD_COL_BEVEL_DARK_LG);
    mfd_draw_fill_rect(dl, mfd_rect(xc + Wc - 2.0f, fy, 2.0f, H - 2.0f), MFD_COL_BEVEL_DARK_LG);

    if (content) *content = mfd_rect(xc + 12.0f, fy + 10.0f, Wc - 24.0f, H - 22.0f);
    if (clip) *clip = mfd_rect(xc + 2.0f, fy, Wc - 4.0f, H - 2.0f);
    return clicked;
}

/* ======================================================================================== */
/* RadioButtonGroup                                                                         */
/* ======================================================================================== */

int mfd_radio_group(MfdUi *ui, MfdId id, float x, float y, float w, const char *const *labels, int n, int selected,
                    uint32_t disabled_mask, MfdColor color, float extra_spacing)
{
    MfdDrawList *dl = ui->dl;
    int i, part, clicked = -1;
    float ry = y;
    if (color == MFD_COL_COUNT) color = MFD_COL_CYAN;
    if (mfd_ui_clicked_part(ui, id, &part) && part >= 0 && part < n && !(disabled_mask & (1u << part)))
        clicked = part;
    for (i = 0; i < n; ++i) {
        int dis = (disabled_mask & (1u << i)) != 0, checked = i == selected;
        float cx, cy;
        MfdColor tc;
        if (extra_spacing > 0.0f) ry += extra_spacing;
        else if (i > 0) ry += 5.0f;
        cx = x + 2.0f + 15.0f;
        cy = ry + 17.0f;
        if (!dis && mfd_ui_is_hovered_part(ui, id, i))
            mfd_draw_bevel(dl, mfd_rect(x, ry, w, 34.0f), 2.0f, MFD_COL_CYAN, MFD_BEVEL_RAISED);
        /* 30 px circle: 2 px sunken ring (top-left dark, bottom-right light), black interior */
        mfd_draw_disc(dl, cx, cy, 15.0f, MFD_COL_LIGHT_GREY);
        mfd_draw_arc(dl, cx, cy, 14.0f, (float)(0.75 * M_PI), (float)(1.75 * M_PI), 2.0f, MFD_COL_BEVEL_DARK_LG);
        mfd_draw_disc(dl, cx, cy, 13.0f, dis ? MFD_COL_DARK_GREY : MFD_COL_BLACK);
        if (checked) mfd_draw_disc(dl, cx, cy, 9.0f, dis ? MFD_COL_DARK_GREY : color);
        if (dis) tc = MFD_COL_DARK_GREY;
        else if (checked || color == MFD_COL_AMBER) tc = color;
        else tc = MFD_COL_WHITE;
        mfd_draw_text(dl, MFD_FONT_SZ, 26.0f, tc, MFD_ANCHOR_LEFT, x + 47.0f, ry + 4.0f + MFD_FONT_BASELINE * 26.0f,
                      labels[i]);
        mfd_ui_hit(ui, id, i, mfd_rect(x, ry, w, 34.0f));
        ry += 34.0f;
    }
    return clicked;
}

/* ======================================================================================== */
/* ContextMenu                                                                              */
/* ======================================================================================== */

void mfd_context_menu_open(MfdUi *ui, MfdId id, float x, float y)
{
    ui->ctx_menu = id;
    ui->ctx_x = x;
    ui->ctx_y = y;
    ui->ctx_open_time = ui->now;
}

int mfd_context_menu_is_open(const MfdUi *ui, MfdId id)
{
    return id && ui->ctx_menu == id;
}

int mfd_context_menu(MfdUi *ui, MfdId id, const MfdMenuItem *items, int n)
{
    MfdDrawList *dl = ui->dl;
    MfdLayer old;
    int i, part, clicked = -1;
    float maxw = 0.0f, w, x, y;
    if (ui->ctx_menu != id) return -1;
    if (mfd_ui_clicked_part(ui, id, &part)) {
        if (part >= 0 && part < n && !items[part].disabled) clicked = part;
        if (clicked >= 0 || ui->now - ui->ctx_open_time > CTX_GUARD_S) {
            ui->ctx_menu = 0; /* hide, then the caller acts on the item */
            return clicked;
        }
    }
    for (i = 0; i < n; ++i) {
        float tw = mfd_text_width(items[i].label, 22.0f);
        if (tw > maxw) maxw = tw;
    }
    w = maxw + 2.0f * 2.0f + 2.0f * 3.0f + 4.0f;
    x = ui->ctx_x;
    y = ui->ctx_y;
    old = mfd_draw_get_layer(dl);
    mfd_draw_set_layer(dl, MFD_LAYER_DIALOG);
    mfd_ui_hit(ui, id, MFD_PART_BLOCKER, mfd_rect(x, y, w, 4.0f + 42.0f * (float)n));
    mfd_draw_box(dl, mfd_rect(x, y, w, 4.0f + 42.0f * (float)n), MFD_COL_DARKER_GREY, 2.0f, MFD_COL_LIGHT_GREY,
                 MFD_BEVEL_RAISED);
    for (i = 0; i < n; ++i) {
        MfdRect row = mfd_rect(x + 2.0f, y + 2.0f + 42.0f * (float)i, w - 4.0f, 42.0f);
        if (!items[i].disabled && mfd_ui_is_hovered_part(ui, id, i))
            mfd_draw_bevel(dl, row, 3.0f, MFD_COL_CYAN, MFD_BEVEL_SOLID);
        mfd_draw_text(dl, MFD_FONT_SZ, 22.0f, items[i].disabled ? MFD_COL_DARK_GREY : MFD_COL_WHITE, MFD_ANCHOR_LEFT,
                      row.x + 5.0f, row.y + 27.6f, items[i].label);
        mfd_ui_hit(ui, id, i, row);
    }
    mfd_draw_set_layer(dl, old);
    return clicked;
}

/* ======================================================================================== */
/* ConfirmationDialog                                                                       */
/* ======================================================================================== */

int mfd_confirm_dialog(MfdUi *ui, MfdId id, MfdRect box, const char *title, int amber_title)
{
    MfdDrawList *dl = ui->dl;
    MfdLayer old = mfd_draw_get_layer(dl);
    MfdButtonDesc cancel, confirm;
    int result = MFD_DIALOG_NONE;
    float cw = mfd_button_auto_width("CANCEL"), fw;
    mfd_draw_set_layer(dl, MFD_LAYER_DIALOG);
    mfd_ui_hit(ui, id, MFD_PART_BLOCKER, box);
    mfd_draw_box(dl, box, MFD_COL_BLACK, 2.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_RAISED);
    mfd_draw_text(dl, MFD_FONT_SZ, MFD_FS_LABEL, amber_title ? MFD_COL_AMBER : MFD_COL_WHITE, MFD_ANCHOR_CENTER,
                  box.x + box.w * 0.5f, box.y + 27.0f + MFD_FONT_BASELINE * MFD_FS_LABEL, title);
    mfd_button_desc_init(&cancel, "CANCEL");
    mfd_button_desc_init(&confirm, "CONFIRM *");
    confirm.pad_right = 6.0f;
    fw = mfd_text_width("CONFIRM *", MFD_FS_BUTTON) + 14.0f + 6.0f + 2.0f;
    if (mfd_button_ex(ui, id + 1u, mfd_rect(box.x + 7.0f, box.y + box.h - 47.0f, cw, 40.0f), &cancel) ==
        MFD_BUTTON_CLICKED)
        result = MFD_DIALOG_CANCEL;
    if (mfd_button_ex(ui, id + 2u, mfd_rect(box.x + box.w - 7.0f - fw, box.y + box.h - 47.0f, fw, 40.0f), &confirm) ==
        MFD_BUTTON_CLICKED)
        result = MFD_DIALOG_CONFIRM;
    mfd_draw_set_layer(dl, old);
    return result;
}

/* ======================================================================================== */
/* SURV widgets                                                                             */
/* ======================================================================================== */

static int two_state_button(MfdUi *ui, MfdId id, float x, float y, const char *lt, const char *lf, int state,
                            int disabled, int adsc)
{
    MfdDrawList *dl = ui->dl;
    MfdRect r = mfd_rect(x, y, 100.0f, 65.0f);
    int part;
    float up_fs, lo_fs, top, cx = x + 50.0f;
    MfdColor up_c, lo_c;
    MfdFont up_f = MFD_FONT_SZ, lo_f = MFD_FONT_SZ;
    if (disabled) {
        /* [PORT] keep the 100x65 footprint, all grey; the label of the inactive state is hidden. */
        mfd_draw_fill_rect(dl, r, MFD_COL_DARK_GREY);
        top = y + (65.0f - 40.0f) * 0.5f;
        if (state) {
            mfd_draw_text(dl, MFD_FONT_SZ, 20.0f, MFD_COL_WHITE, MFD_ANCHOR_CENTER, cx,
                          top + MFD_FONT_BASELINE * 20.0f, lt);
        } else {
            mfd_draw_text(dl, MFD_FONT_SZ, 20.0f, MFD_COL_WHITE, MFD_ANCHOR_CENTER, cx,
                          top + 20.0f + MFD_FONT_BASELINE * 20.0f, lf);
        }
        mfd_ui_hit(ui, id, MFD_PART_MAIN, r);
        return 0;
    }
    if (adsc) {
        /* active label: upper .mfd-value.bigger (30 green) / lower .mfd-label.bigger (24 white);
         * inactive label: .mfd-adsc-label-off (Ecam 20 #868a9c) */
        up_fs = state ? 30.0f : 20.0f; up_c = state ? MFD_COL_GREEN : MFD_COL_DARK_GREY;
        up_f = state ? MFD_FONT_SZ : MFD_FONT_ECAM;
        lo_fs = state ? 20.0f : 24.0f; lo_c = state ? MFD_COL_DARK_GREY : MFD_COL_WHITE;
        lo_f = state ? MFD_FONT_ECAM : MFD_FONT_SZ;
    } else {
        /* active label: .mfd-value.bigger (30 green); inactive label: .mfd-label (20 white) */
        up_fs = state ? 30.0f : 20.0f; up_c = state ? MFD_COL_GREEN : MFD_COL_WHITE;
        lo_fs = state ? 20.0f : 30.0f; lo_c = state ? MFD_COL_WHITE : MFD_COL_GREEN;
    }
    /* 4 px sunken border + 5 px padding: content 47 px; the label stack is centred in it. */
    mfd_draw_box(dl, r, MFD_COL_BLACK, 4.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_SUNKEN);
    top = y + 9.0f + (47.0f - (up_fs + lo_fs)) * 0.5f;
    mfd_draw_text(dl, up_f, up_fs, up_c, MFD_ANCHOR_CENTER, cx, top + MFD_FONT_BASELINE * up_fs, lt);
    mfd_draw_text(dl, lo_f, lo_fs, lo_c, MFD_ANCHOR_CENTER, cx, top + up_fs + MFD_FONT_BASELINE * lo_fs, lf);
    mfd_ui_hit(ui, id, MFD_PART_MAIN, r);
    return mfd_ui_clicked_part(ui, id, &part) && part == MFD_PART_MAIN;
}

int mfd_surv_button(MfdUi *ui, MfdId id, float x, float y, const char *label_true, const char *label_false, int state,
                    int disabled)
{
    return two_state_button(ui, id, x, y, label_true, label_false, state, disabled, 0);
}

int mfd_adsc_button(MfdUi *ui, MfdId id, float x, float y, const char *label_true, const char *label_false, int state,
                    int disabled)
{
    return two_state_button(ui, id, x, y, label_true, label_false, state, disabled, 1);
}

int mfd_surv_status_button(MfdUi *ui, MfdId id, MfdRect r, const char *label, int active)
{
    MfdDrawList *dl = ui->dl;
    int i, part, hovered = mfd_ui_is_hovered_part(ui, id, MFD_PART_MAIN);
    float block_h = (r.h - 4.0f) * 0.5f, bar_h = block_h * 0.143f, gap = (block_h - 3.0f * bar_h) / 4.0f;
    mfd_draw_fill_rect(dl, r, MFD_COL_SURV_STATUS_BG);
    mfd_draw_bevel(dl, r, 2.0f, hovered ? MFD_COL_CYAN : MFD_COL_LIGHT_GREY, MFD_BEVEL_SOLID);
    mfd_draw_fill_rect(dl, mfd_rect(r.x + 2.0f, r.y + 2.0f, r.w - 4.0f, block_h), MFD_COL_BLACK);
    for (i = 0; i < 3; ++i) {
        mfd_draw_fill_rect(dl, mfd_rect(r.x + 9.0f, r.y + 2.0f + gap + (float)i * (bar_h + gap), r.w - 18.0f, bar_h),
                           active ? MFD_COL_GREEN : MFD_COL_LIGHT_GREY);
    }
    mfd_draw_text(dl, MFD_FONT_SZ, 22.0f, MFD_COL_WHITE, MFD_ANCHOR_CENTER, r.x + r.w * 0.5f,
                  r.y + 2.0f + block_h + 3.0f + MFD_FONT_BASELINE * 22.0f, label);
    mfd_ui_hit(ui, id, MFD_PART_MAIN, r);
    return mfd_ui_clicked_part(ui, id, &part) && part == MFD_PART_MAIN;
}

void mfd_surv_status_item(MfdUi *ui, float x, float top, const char *label, const char *sys, int active, int failed)
{
    char text[64];
    MfdColor c = failed ? MFD_COL_AMBER : active ? MFD_COL_GREEN : MFD_COL_WHITE;
    snprintf(text, sizeof(text), "%s %s", label ? label : "", active ? (sys ? sys : "") : "OFF");
    mfd_draw_text(ui->dl, MFD_FONT_SZ, 24.0f, c, MFD_ANCHOR_LEFT, x, mfd_baseline(top, 24.0f), text);
}
