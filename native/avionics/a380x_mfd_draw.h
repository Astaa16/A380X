/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD display-list builder (docs/mfd-spec/12-architecture.md section 4).
 *
 * Every primitive is appended to a fixed-capacity MfdDrawList with the list's current layer
 * and current clip. Nothing allocates. When a buffer is full the command is dropped and
 * dl->overflowed is set (sticky until mfd_draw_reset).
 *
 * Coordinates: canvas px, origin top-left, y down. Text is positioned by its baseline point;
 * the anchor selects whether x is the left edge, centre or right edge of the advance box.
 * Text width is code points x 0.626953125 x size (both MFD fonts are monospaced).
 */
#ifndef A380X_MFD_DRAW_H
#define A380X_MFD_DRAW_H

#include "a380x_mfd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MFD_BEVEL_RAISED = 0, /* CSS outset: top/left = c, bottom/right = dark(c) */
    MFD_BEVEL_SUNKEN,     /* CSS inset: top/left = dark(c), bottom/right = c */
    MFD_BEVEL_SOLID       /* all four sides = c */
} MfdBevel;

/* ---- List management --------------------------------------------------------------------- */

void     mfd_draw_reset(MfdDrawList *dl);              /* empty list, layer 0, no clip */
void     mfd_draw_set_layer(MfdDrawList *dl, MfdLayer layer);
MfdLayer mfd_draw_get_layer(const MfdDrawList *dl);
/* Push a clip rectangle (intersected with the current clip); pop restores the previous one. */
void     mfd_draw_clip_push(MfdDrawList *dl, MfdRect r);
void     mfd_draw_clip_pop(MfdDrawList *dl);
MfdRect  mfd_draw_clip_current(const MfdDrawList *dl);
/* Stable sort of the command indices by layer (emission order kept inside a layer).
 * order must hold dl->cmd_count entries. Returns dl->cmd_count. */
uint32_t mfd_draw_order(const MfdDrawList *dl, uint16_t *order);

/* ---- Primitives -------------------------------------------------------------------------- */

void mfd_draw_fill_rect(MfdDrawList *dl, MfdRect r, MfdColor c);
/* Stroke drawn inside r with the given width (CSS border semantics). */
void mfd_draw_stroke_rect(MfdDrawList *dl, MfdRect r, float width, MfdColor c);
void mfd_draw_line(MfdDrawList *dl, float x1, float y1, float x2, float y2, float width, MfdColor c);
void mfd_draw_polyline(MfdDrawList *dl, const MfdPoint *pts, int n, float width, int closed, MfdColor c);
void mfd_draw_polygon(MfdDrawList *dl, const MfdPoint *pts, int n, MfdColor c); /* convex, filled */
void mfd_draw_triangle(MfdDrawList *dl, float x1, float y1, float x2, float y2, float x3, float y3,
                       MfdColor c);
/* Filled disc / circle outline approximated by a polygon / polyline (32 segments). */
void mfd_draw_disc(MfdDrawList *dl, float cx, float cy, float r, MfdColor c);
/* Arc stroked along radius r from angle a0 to a1 (radians, 0 = +x, y down => clockwise). */
void mfd_draw_arc(MfdDrawList *dl, float cx, float cy, float r, float a0, float a1, float width, MfdColor c);

/* CSS border of width w drawn inside r (mitred corners). Transparent borders: do not call. */
void mfd_draw_bevel(MfdDrawList *dl, MfdRect r, float w, MfdColor c, MfdBevel style);
/* Convenience: fill r with `fill`, then a w-px bevel border in `border`. */
void mfd_draw_box(MfdDrawList *dl, MfdRect r, MfdColor fill, float w, MfdColor border, MfdBevel style);

/* ---- Text -------------------------------------------------------------------------------- */

void mfd_draw_text(MfdDrawList *dl, MfdFont font, float size, MfdColor c, MfdAnchor anchor,
                   float x, float baseline, const char *utf8);
void mfd_draw_textn(MfdDrawList *dl, MfdFont font, float size, MfdColor c, MfdAnchor anchor,
                    float x, float baseline, const char *utf8, size_t nbytes);

/* Decode the next UTF-8 code point and advance *s (invalid bytes decode as U+FFFD). */
unsigned int mfd_utf8_next(const char **s);
/* Number of code points in a string (or in its first nbytes). */
int   mfd_utf8_count(const char *utf8);
int   mfd_utf8_countn(const char *utf8, size_t nbytes);
/* Byte offset of code point index i (clamped to the string length). */
size_t mfd_utf8_offset(const char *utf8, int i);
float mfd_text_width(const char *utf8, float size);
float mfd_text_widthn(const char *utf8, size_t nbytes, float size);
static inline float mfd_char_advance(float size) { return MFD_FONT_ADVANCE * size; }
/* Baseline of a 1.0-em line box whose top is `top`. */
static inline float mfd_baseline(float top, float size) { return top + MFD_FONT_BASELINE * size; }

#ifdef __cplusplus
}
#endif

#endif
