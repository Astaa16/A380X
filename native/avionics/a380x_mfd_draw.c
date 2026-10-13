/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD display-list builder. See a380x_mfd_draw.h.
 */
#include "a380x_mfd_draw.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- List management --------------------------------------------------------------------- */

void mfd_draw_reset(MfdDrawList *dl)
{
    dl->cmd_count = 0;
    dl->point_count = 0;
    dl->text_len = 0;
    dl->clip_count = 1;
    dl->clips[0] = mfd_rect(0.0f, 0.0f, (float)MFD_W, (float)MFD_H);
    dl->clip_depth = 0;
    dl->layer = MFD_LAYER_PAGE;
    dl->overflowed = 0;
}

void mfd_draw_set_layer(MfdDrawList *dl, MfdLayer layer)
{
    dl->layer = (uint8_t)layer;
}

MfdLayer mfd_draw_get_layer(const MfdDrawList *dl)
{
    return (MfdLayer)dl->layer;
}

static uint16_t current_clip(const MfdDrawList *dl)
{
    return dl->clip_depth > 0 ? dl->clip_stack[dl->clip_depth - 1] : 0;
}

MfdRect mfd_draw_clip_current(const MfdDrawList *dl)
{
    return dl->clips[current_clip(dl)];
}

void mfd_draw_clip_push(MfdDrawList *dl, MfdRect r)
{
    MfdRect c = mfd_rect_intersect(mfd_draw_clip_current(dl), r);
    if (dl->clip_depth >= MFD_DRAW_CLIP_DEPTH || dl->clip_count >= MFD_DRAW_MAX_CLIPS) {
        dl->overflowed = 1;
        /* Keep push/pop balanced: re-use the current clip. */
        if (dl->clip_depth < MFD_DRAW_CLIP_DEPTH) {
            dl->clip_stack[dl->clip_depth++] = current_clip(dl);
        } else {
            dl->clip_depth++;
        }
        return;
    }
    dl->clips[dl->clip_count] = c;
    dl->clip_stack[dl->clip_depth++] = (uint16_t)dl->clip_count;
    dl->clip_count++;
}

void mfd_draw_clip_pop(MfdDrawList *dl)
{
    if (dl->clip_depth > 0) dl->clip_depth--;
}

uint32_t mfd_draw_order(const MfdDrawList *dl, uint16_t *order)
{
    uint32_t counts[256], start[256], i, sum = 0;
    memset(counts, 0, sizeof(counts));
    for (i = 0; i < dl->cmd_count; ++i) counts[dl->cmds[i].layer]++;
    for (i = 0; i < 256; ++i) { start[i] = sum; sum += counts[i]; }
    for (i = 0; i < dl->cmd_count; ++i) order[start[dl->cmds[i].layer]++] = (uint16_t)i;
    return dl->cmd_count;
}

static MfdCmd *new_cmd(MfdDrawList *dl, MfdCmdKind kind, MfdColor c)
{
    MfdCmd *cmd;
    if (dl->cmd_count >= MFD_DRAW_MAX_CMDS) {
        dl->overflowed = 1;
        return NULL;
    }
    cmd = &dl->cmds[dl->cmd_count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->kind = (uint8_t)kind;
    cmd->layer = dl->layer;
    cmd->color = (uint8_t)c;
    cmd->clip = current_clip(dl);
    return cmd;
}

static int reserve_points(MfdDrawList *dl, int n)
{
    if (n <= 0 || dl->point_count + (uint32_t)n > MFD_DRAW_MAX_POINTS || dl->cmd_count >= MFD_DRAW_MAX_CMDS) {
        dl->overflowed = 1;
        return 0;
    }
    return 1;
}

/* ---- Primitives -------------------------------------------------------------------------- */

void mfd_draw_fill_rect(MfdDrawList *dl, MfdRect r, MfdColor c)
{
    MfdCmd *cmd;
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    cmd = new_cmd(dl, MFD_CMD_FILL_RECT, c);
    if (!cmd) return;
    cmd->a[0] = r.x; cmd->a[1] = r.y; cmd->a[2] = r.w; cmd->a[3] = r.h;
}

void mfd_draw_stroke_rect(MfdDrawList *dl, MfdRect r, float width, MfdColor c)
{
    MfdCmd *cmd;
    if (r.w <= 0.0f || r.h <= 0.0f || width <= 0.0f) return;
    cmd = new_cmd(dl, MFD_CMD_STROKE_RECT, c);
    if (!cmd) return;
    cmd->a[0] = r.x; cmd->a[1] = r.y; cmd->a[2] = r.w; cmd->a[3] = r.h;
    cmd->width = width;
}

void mfd_draw_line(MfdDrawList *dl, float x1, float y1, float x2, float y2, float width, MfdColor c)
{
    MfdCmd *cmd = new_cmd(dl, MFD_CMD_LINE, c);
    if (!cmd) return;
    cmd->a[0] = x1; cmd->a[1] = y1; cmd->a[2] = x2; cmd->a[3] = y2;
    cmd->width = width;
}

static void points_cmd(MfdDrawList *dl, MfdCmdKind kind, const MfdPoint *pts, int n, float width, int closed,
                       MfdColor c)
{
    MfdCmd *cmd;
    if (!reserve_points(dl, n)) return;
    cmd = new_cmd(dl, kind, c);
    if (!cmd) return;
    cmd->first = dl->point_count;
    cmd->count = (uint32_t)n;
    cmd->width = width;
    cmd->closed = (uint8_t)(closed ? 1 : 0);
    memcpy(&dl->points[dl->point_count], pts, sizeof(MfdPoint) * (size_t)n);
    dl->point_count += (uint32_t)n;
}

void mfd_draw_polyline(MfdDrawList *dl, const MfdPoint *pts, int n, float width, int closed, MfdColor c)
{
    if (n < 2) return;
    points_cmd(dl, MFD_CMD_POLYLINE, pts, n, width, closed, c);
}

void mfd_draw_polygon(MfdDrawList *dl, const MfdPoint *pts, int n, MfdColor c)
{
    if (n < 3) return;
    points_cmd(dl, MFD_CMD_POLYGON, pts, n, 0.0f, 1, c);
}

void mfd_draw_triangle(MfdDrawList *dl, float x1, float y1, float x2, float y2, float x3, float y3, MfdColor c)
{
    MfdPoint p[3];
    p[0].x = x1; p[0].y = y1; p[1].x = x2; p[1].y = y2; p[2].x = x3; p[2].y = y3;
    points_cmd(dl, MFD_CMD_TRIANGLE, p, 3, 0.0f, 1, c);
}

#define MFD_CIRCLE_SEGMENTS 32

void mfd_draw_disc(MfdDrawList *dl, float cx, float cy, float r, MfdColor c)
{
    MfdPoint p[MFD_CIRCLE_SEGMENTS];
    int i;
    if (r <= 0.0f) return;
    for (i = 0; i < MFD_CIRCLE_SEGMENTS; ++i) {
        double a = 2.0 * M_PI * (double)i / MFD_CIRCLE_SEGMENTS;
        p[i].x = cx + (float)(r * cos(a));
        p[i].y = cy + (float)(r * sin(a));
    }
    mfd_draw_polygon(dl, p, MFD_CIRCLE_SEGMENTS, c);
}

void mfd_draw_arc(MfdDrawList *dl, float cx, float cy, float r, float a0, float a1, float width, MfdColor c)
{
    MfdPoint p[MFD_CIRCLE_SEGMENTS + 1];
    int i, n = MFD_CIRCLE_SEGMENTS;
    float span = a1 - a0;
    if (r <= 0.0f) return;
    n = (int)ceilf(fabsf(span) / (float)(2.0 * M_PI) * MFD_CIRCLE_SEGMENTS);
    if (n < 2) n = 2;
    if (n > MFD_CIRCLE_SEGMENTS) n = MFD_CIRCLE_SEGMENTS;
    for (i = 0; i <= n; ++i) {
        double a = a0 + span * (double)i / n;
        p[i].x = cx + (float)(r * cos(a));
        p[i].y = cy + (float)(r * sin(a));
    }
    mfd_draw_polyline(dl, p, n + 1, width, 0, c);
}

void mfd_draw_bevel(MfdDrawList *dl, MfdRect r, float w, MfdColor c, MfdBevel style)
{
    MfdColor light = c, dark = c;
    float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    MfdPoint q[4];
    if (w <= 0.0f || r.w <= 0.0f || r.h <= 0.0f) return;
    if (w * 2.0f > r.w) w = r.w * 0.5f;
    if (w * 2.0f > r.h) w = r.h * 0.5f;
    if (style == MFD_BEVEL_RAISED) {
        dark = mfd_color_dark(c);
    } else if (style == MFD_BEVEL_SUNKEN) {
        light = mfd_color_dark(c);
    }
    if (style == MFD_BEVEL_SOLID) {
        /* Four rectangles, no mitre needed. */
        mfd_draw_fill_rect(dl, mfd_rect(x0, y0, r.w, w), c);
        mfd_draw_fill_rect(dl, mfd_rect(x0, y1 - w, r.w, w), c);
        mfd_draw_fill_rect(dl, mfd_rect(x0, y0 + w, w, r.h - 2.0f * w), c);
        mfd_draw_fill_rect(dl, mfd_rect(x1 - w, y0 + w, w, r.h - 2.0f * w), c);
        return;
    }
    /* Top (light) */
    q[0].x = x0; q[0].y = y0; q[1].x = x1; q[1].y = y0;
    q[2].x = x1 - w; q[2].y = y0 + w; q[3].x = x0 + w; q[3].y = y0 + w;
    mfd_draw_polygon(dl, q, 4, light);
    /* Left (light) */
    q[0].x = x0; q[0].y = y0; q[1].x = x0 + w; q[1].y = y0 + w;
    q[2].x = x0 + w; q[2].y = y1 - w; q[3].x = x0; q[3].y = y1;
    mfd_draw_polygon(dl, q, 4, light);
    /* Bottom (dark) */
    q[0].x = x0; q[0].y = y1; q[1].x = x0 + w; q[1].y = y1 - w;
    q[2].x = x1 - w; q[2].y = y1 - w; q[3].x = x1; q[3].y = y1;
    mfd_draw_polygon(dl, q, 4, dark);
    /* Right (dark) */
    q[0].x = x1; q[0].y = y0; q[1].x = x1; q[1].y = y1;
    q[2].x = x1 - w; q[2].y = y1 - w; q[3].x = x1 - w; q[3].y = y0 + w;
    mfd_draw_polygon(dl, q, 4, dark);
}

void mfd_draw_box(MfdDrawList *dl, MfdRect r, MfdColor fill, float w, MfdColor border, MfdBevel style)
{
    mfd_draw_fill_rect(dl, r, fill);
    mfd_draw_bevel(dl, r, w, border, style);
}

/* ---- Text -------------------------------------------------------------------------------- */

unsigned int mfd_utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    unsigned int c = *p;
    if (c < 0x80u) {
        *s += 1;
        return c;
    }
    if ((c & 0xE0u) == 0xC0u && (p[1] & 0xC0u) == 0x80u) {
        *s += 2;
        return ((c & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    }
    if ((c & 0xF0u) == 0xE0u && (p[1] & 0xC0u) == 0x80u && (p[2] & 0xC0u) == 0x80u) {
        *s += 3;
        return ((c & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    }
    if ((c & 0xF8u) == 0xF0u && (p[1] & 0xC0u) == 0x80u && (p[2] & 0xC0u) == 0x80u &&
        (p[3] & 0xC0u) == 0x80u) {
        *s += 4;
        return ((c & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
    }
    *s += 1;
    return 0xFFFDu;
}

int mfd_utf8_countn(const char *utf8, size_t nbytes)
{
    const char *p = utf8, *end = utf8 + nbytes;
    int n = 0;
    if (!utf8) return 0;
    while (p < end && *p) {
        mfd_utf8_next(&p);
        ++n;
    }
    return n;
}

int mfd_utf8_count(const char *utf8)
{
    return utf8 ? mfd_utf8_countn(utf8, strlen(utf8)) : 0;
}

size_t mfd_utf8_offset(const char *utf8, int i)
{
    const char *p = utf8;
    if (!utf8) return 0;
    while (i > 0 && *p) {
        mfd_utf8_next(&p);
        --i;
    }
    return (size_t)(p - utf8);
}

float mfd_text_widthn(const char *utf8, size_t nbytes, float size)
{
    return (float)mfd_utf8_countn(utf8, nbytes) * MFD_FONT_ADVANCE * size;
}

float mfd_text_width(const char *utf8, float size)
{
    return (float)mfd_utf8_count(utf8) * MFD_FONT_ADVANCE * size;
}

void mfd_draw_textn(MfdDrawList *dl, MfdFont font, float size, MfdColor c, MfdAnchor anchor, float x,
                    float baseline, const char *utf8, size_t nbytes)
{
    MfdCmd *cmd;
    if (!utf8 || nbytes == 0) return;
    if (dl->text_len + (uint32_t)nbytes > MFD_DRAW_MAX_TEXT || dl->cmd_count >= MFD_DRAW_MAX_CMDS) {
        dl->overflowed = 1;
        return;
    }
    cmd = new_cmd(dl, MFD_CMD_TEXT, c);
    if (!cmd) return;
    cmd->font = (uint8_t)font;
    cmd->anchor = (uint8_t)anchor;
    cmd->size = size;
    cmd->a[0] = x;
    cmd->a[1] = baseline;
    cmd->first = dl->text_len;
    cmd->count = (uint32_t)nbytes;
    memcpy(&dl->text[dl->text_len], utf8, nbytes);
    dl->text_len += (uint32_t)nbytes;
}

void mfd_draw_text(MfdDrawList *dl, MfdFont font, float size, MfdColor c, MfdAnchor anchor, float x,
                   float baseline, const char *utf8)
{
    if (!utf8) return;
    mfd_draw_textn(dl, font, size, c, anchor, x, baseline, utf8, strlen(utf8));
}
