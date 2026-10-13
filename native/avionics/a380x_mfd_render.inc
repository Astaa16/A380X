/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * OpenGL 2D executor for the A380X MFD display list (docs/mfd-spec/12-architecture.md section 3, WP-I).
 *
 * #include this file into the plugin translation unit AFTER the OpenGL and XPLM headers
 * (XPLMGraphics.h) and after a380x_mfd_draw.h. It needs a380x_mfd_draw.c linked (mfd_draw_order,
 * mfd_utf8_next) and embeds the glyph atlas of a380x_mfd_font.h.
 *
 * Coordinate contract: the current GL transform maps 1 unit to 1 framebuffer pixel with y up (the
 * X-Plane avionics-device draw callback does this). mfd_gl_draw() places the 768x1024 canvas with
 * its bottom-left corner at (ox, oy) and scales it by `scale` (1 = one canvas px per device px).
 * Canvas y is flipped: Y = oy + (1024 - y) * scale. Clip rectangles use glScissor in the same
 * units; pass use_scissor = 0 if the caller's transform is not pixel-aligned.
 *
 * Draw order: ascending layer, emission order inside a layer. Filled rectangles snap to whole
 * device pixels (spec 01 section 0.2); text pens snap to whole pixels when scale == 1 so the
 * per-size pre-rasterised glyphs stay crisp.
 */
#include "a380x_mfd_font.h"

static int mfd_gl_font_texture;
static uint16_t mfd_gl_order[MFD_DRAW_MAX_CMDS];

static int mfd_gl_font_init(void)
{
    GLint unpack_alignment;
    unsigned char *alpha;
    if (mfd_gl_font_texture) return 1;
    alpha = (unsigned char *)malloc((size_t)MFD_FONT_ATLAS_W * MFD_FONT_ATLAS_H); /* once, not per frame */
    if (!alpha) return 0;
    if (!mfd_font_decode(alpha)) {
        free(alpha);
        return 0;
    }
    XPLMGenerateTextureNumbers(&mfd_gl_font_texture, 1);
    XPLMBindTexture2d(mfd_gl_font_texture, 0);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_alignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, MFD_FONT_ATLAS_W, MFD_FONT_ATLAS_H, 0, GL_ALPHA, GL_UNSIGNED_BYTE, alpha);
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
    free(alpha);
    return 1;
}

static void mfd_gl_shutdown(void)
{
    if (mfd_gl_font_texture) {
        GLuint texture = (GLuint)mfd_gl_font_texture;
        glDeleteTextures(1, &texture);
        mfd_gl_font_texture = 0;
    }
}

typedef struct {
    float ox, oy, s;
} MfdGlXform;

static float mfd_gl_x(const MfdGlXform *t, float x) { return t->ox + x * t->s; }
static float mfd_gl_y(const MfdGlXform *t, float y) { return t->oy + ((float)MFD_H - y) * t->s; }
static float mfd_gl_snap(float v) { return floorf(v + 0.5f); }

static void mfd_gl_color(MfdColor c)
{
    uint32_t rgb = mfd_color_rgb(c);
    glColor4ub((GLubyte)((rgb >> 16) & 0xffu), (GLubyte)((rgb >> 8) & 0xffu), (GLubyte)(rgb & 0xffu), 255);
}

static void mfd_gl_rect(const MfdGlXform *t, float x, float y, float w, float h)
{
    float x0 = mfd_gl_snap(mfd_gl_x(t, x)), x1 = mfd_gl_snap(mfd_gl_x(t, x + w));
    float y0 = mfd_gl_snap(mfd_gl_y(t, y)), y1 = mfd_gl_snap(mfd_gl_y(t, y + h));
    if (x1 <= x0 || y0 <= y1) return;
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
}

static void mfd_gl_segment(const MfdGlXform *t, float x1, float y1, float x2, float y2, float w)
{
    float ax = mfd_gl_x(t, x1), ay = mfd_gl_y(t, y1), bx = mfd_gl_x(t, x2), by = mfd_gl_y(t, y2);
    float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy), nx, ny;
    if (len < 1e-6f) return;
    nx = -dy / len * w * t->s * 0.5f;
    ny = dx / len * w * t->s * 0.5f;
    glVertex2f(ax + nx, ay + ny); glVertex2f(bx + nx, by + ny);
    glVertex2f(bx - nx, by - ny); glVertex2f(ax - nx, ay - ny);
}

static void mfd_gl_text(const MfdGlXform *t, const MfdDrawList *dl, const MfdCmd *cmd)
{
    const char *p = &dl->text[cmd->first], *end = p + cmd->count;
    float strip_scale, adv = MFD_FONT_ADVANCE * cmd->size;
    int si = mfd_font_size_index(cmd->size * t->s, &strip_scale);
    float width = (float)mfd_utf8_countn(p, cmd->count) * adv;
    float pen = cmd->a[0] - width * (cmd->anchor == MFD_ANCHOR_CENTER ? 0.5f : cmd->anchor == MFD_ANCHOR_RIGHT ? 1.0f : 0.0f);
    float base = mfd_gl_y(t, cmd->a[1]);
    int crisp = strip_scale > 0.999f && strip_scale < 1.001f;
    if (crisp) base = mfd_gl_snap(base);
    while (p < end && *p) {
        unsigned int cp = mfd_utf8_next(&p);
        int slot = mfd_font_slot(cp, cmd->font == MFD_FONT_ECAM);
        if (slot >= 0) {
            const MfdFontGlyph *g = &mfd_font_glyphs[si * MFD_FONT_SLOTS + slot];
            if (g->w) {
                float px = mfd_gl_x(t, pen), k = strip_scale;
                float left, top, right, bottom;
                float u0 = (float)g->x / MFD_FONT_ATLAS_W, u1 = (float)(g->x + g->w) / MFD_FONT_ATLAS_W;
                float v0 = (float)g->y / MFD_FONT_ATLAS_H, v1 = (float)(g->y + g->h) / MFD_FONT_ATLAS_H;
                if (crisp) px = mfd_gl_snap(px);
                left = px + g->left * k;
                right = left + g->w * k;
                top = base - g->top * k; /* g->top < 0: above the baseline (y up) */
                bottom = top - g->h * k;
                glTexCoord2f(u0, v0); glVertex2f(left, top);
                glTexCoord2f(u1, v0); glVertex2f(right, top);
                glTexCoord2f(u1, v1); glVertex2f(right, bottom);
                glTexCoord2f(u0, v1); glVertex2f(left, bottom);
            }
        }
        pen += adv;
    }
}

/* Execute a display list. Returns the number of commands drawn. */
static uint32_t mfd_gl_draw(const MfdDrawList *dl, float ox, float oy, float scale, int use_scissor)
{
    MfdGlXform t;
    uint32_t i, n;
    int cur_clip = -1, textured = -1;
    if (!dl) return 0;
    t.ox = ox; t.oy = oy; t.s = scale;
    n = mfd_draw_order(dl, mfd_gl_order);
    mfd_gl_font_init();
    if (use_scissor) glEnable(GL_SCISSOR_TEST);
    for (i = 0; i < n; ++i) {
        const MfdCmd *cmd = &dl->cmds[mfd_gl_order[i]];
        const MfdPoint *pts = &dl->points[cmd->first];
        int want_tex = cmd->kind == MFD_CMD_TEXT;
        if (use_scissor && (int)cmd->clip != cur_clip) {
            MfdRect c = dl->clips[cmd->clip];
            float x0 = mfd_gl_snap(mfd_gl_x(&t, c.x)), x1 = mfd_gl_snap(mfd_gl_x(&t, c.x + c.w));
            float y0 = mfd_gl_snap(mfd_gl_y(&t, c.y + c.h)), y1 = mfd_gl_snap(mfd_gl_y(&t, c.y));
            glScissor((GLint)x0, (GLint)y0, (GLsizei)(x1 > x0 ? x1 - x0 : 0), (GLsizei)(y1 > y0 ? y1 - y0 : 0));
            cur_clip = (int)cmd->clip;
        }
        if (want_tex != textured) {
            if (want_tex) {
                XPLMSetGraphicsState(0, 1, 0, 0, 1, 0, 0);
                XPLMBindTexture2d(mfd_gl_font_texture, 0);
            } else {
                XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
            }
            textured = want_tex;
        }
        mfd_gl_color((MfdColor)cmd->color);
        switch ((MfdCmdKind)cmd->kind) {
        case MFD_CMD_FILL_RECT:
            glBegin(GL_QUADS);
            mfd_gl_rect(&t, cmd->a[0], cmd->a[1], cmd->a[2], cmd->a[3]);
            glEnd();
            break;
        case MFD_CMD_STROKE_RECT: {
            float x = cmd->a[0], y = cmd->a[1], w = cmd->a[2], h = cmd->a[3], s = cmd->width;
            glBegin(GL_QUADS);
            mfd_gl_rect(&t, x, y, w, s);
            mfd_gl_rect(&t, x, y + h - s, w, s);
            mfd_gl_rect(&t, x, y + s, s, h - 2.0f * s);
            mfd_gl_rect(&t, x + w - s, y + s, s, h - 2.0f * s);
            glEnd();
            break;
        }
        case MFD_CMD_LINE:
            glBegin(GL_QUADS);
            mfd_gl_segment(&t, cmd->a[0], cmd->a[1], cmd->a[2], cmd->a[3], cmd->width);
            glEnd();
            break;
        case MFD_CMD_POLYLINE: {
            uint32_t k, segs = cmd->closed ? cmd->count : cmd->count - 1;
            glBegin(GL_QUADS);
            for (k = 0; k < segs; ++k) {
                const MfdPoint *a = &pts[k], *b = &pts[(k + 1) % cmd->count];
                mfd_gl_segment(&t, a->x, a->y, b->x, b->y, cmd->width);
            }
            glEnd();
            break;
        }
        case MFD_CMD_POLYGON:
        case MFD_CMD_TRIANGLE: {
            uint32_t k;
            glBegin(GL_TRIANGLE_FAN);
            for (k = 0; k < cmd->count; ++k) glVertex2f(mfd_gl_x(&t, pts[k].x), mfd_gl_y(&t, pts[k].y));
            glEnd();
            break;
        }
        case MFD_CMD_TEXT:
            if (!mfd_gl_font_texture) break;
            glBegin(GL_QUADS);
            mfd_gl_text(&t, dl, cmd);
            glEnd();
            break;
        }
    }
    if (use_scissor) glDisable(GL_SCISSOR_TEST);
    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    return n;
}
