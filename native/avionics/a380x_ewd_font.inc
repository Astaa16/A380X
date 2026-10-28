/* SPDX-License-Identifier: GPL-3.0-or-later
 * Embedded FBW EIS glyphs. No platform font substitution at runtime.
 */
#include "a380x_ewd_font.h"
static int ewd_font_texture;

static const EwdGlyph *ewd_next_glyph(const unsigned char **cursor)
{
    unsigned int code = *(*cursor)++;
    if (code == 0xc2 && **cursor == 0xb0) { ++*cursor; code = 176; }
    if (code == 176) return &ewd_glyphs[95];
    if (code < 32 || code > 126) code = '?';
    return &ewd_glyphs[code - 32];
}

static float ewd_font_width(const char *text, float size, float tracking)
{
    const unsigned char *cursor = (const unsigned char *)text;
    float width = 0.0f;
    while (*cursor) { width += ewd_next_glyph(&cursor)->advance * size / EWD_FONT_SIZE + tracking; }
    return width > 0.0f ? width - tracking : 0.0f;
}

/* Optional vertical clip window (y-up, drawing units) for rolling drum digits: glyph quads are
 * cut at the window edges with matching texture coordinates. Disabled when y0 >= y1. */
static float ewd_clip_y0 = 0.0f, ewd_clip_y1 = 0.0f;

static void ewd_font_clip_y(float y0, float y1)
{
    ewd_clip_y0 = y0;
    ewd_clip_y1 = y1;
}

static void ewd_font_draw(float x, float y, const char *text, float size,
                          Color color, float tracking, int anchor)
{
    const unsigned char *cursor = (const unsigned char *)text;
    float scale = size / EWD_FONT_SIZE;
    if (!ewd_font_texture) {
        GLint unpack_alignment;
        unsigned char *alpha = (unsigned char *)malloc(EWD_FONT_WIDTH * EWD_FONT_HEIGHT);
        size_t i, offset = 0;
        if (!alpha) return;
        for (i=0; i<sizeof(ewd_font_runs); i+=2) {
            memset(alpha+offset, ewd_font_runs[i+1], ewd_font_runs[i]);
            offset += ewd_font_runs[i];
        }
        XPLMGenerateTextureNumbers(&ewd_font_texture, 1);
        XPLMBindTexture2d(ewd_font_texture, 0);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack_alignment);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D,0,GL_ALPHA,EWD_FONT_WIDTH,EWD_FONT_HEIGHT,0,GL_ALPHA,GL_UNSIGNED_BYTE,alpha);
        glPixelStorei(GL_UNPACK_ALIGNMENT,unpack_alignment);
        free(alpha);
    }
    x -= ewd_font_width(text,size,tracking) * (anchor == 2 ? 1.0f : anchor == 1 ? 0.5f : 0.0f);
    XPLMSetGraphicsState(0,1,0,0,1,0,0);
    XPLMBindTexture2d(ewd_font_texture,0);
    glColor3f(color.r,color.g,color.b);
    glBegin(GL_QUADS);
    while (*cursor) {
        const EwdGlyph *g = ewd_next_glyph(&cursor);
        float left=x+g->left*scale, top=y-g->top*scale;
        float right=left+g->w*scale, bottom=top-g->h*scale;
        float u0=(float)g->x/EWD_FONT_WIDTH,u1=(float)(g->x+g->w)/EWD_FONT_WIDTH;
        float v0=(float)g->y/EWD_FONT_HEIGHT,v1=(float)(g->y+g->h)/EWD_FONT_HEIGHT;
        if (ewd_clip_y0 < ewd_clip_y1 && top > bottom) {
            float clipped_top = fminf(top, ewd_clip_y1), clipped_bottom = fmaxf(bottom, ewd_clip_y0);
            if (clipped_top <= clipped_bottom) { x += g->advance*scale+tracking; continue; }
            v1 = v0 + (top - clipped_bottom) / (top - bottom) * (v1 - v0);
            v0 = v0 + (top - clipped_top) / (top - bottom) * ((float)(g->y+g->h)/EWD_FONT_HEIGHT - v0);
            top = clipped_top; bottom = clipped_bottom;
        }
        glTexCoord2f(u0,v0);glVertex2f(left,top);
        glTexCoord2f(u1,v0);glVertex2f(right,top);
        glTexCoord2f(u1,v1);glVertex2f(right,bottom);
        glTexCoord2f(u0,v1);glVertex2f(left,bottom);
        x += g->advance*scale+tracking;
    }
    glEnd();
    XPLMSetGraphicsState(0,0,0,0,1,0,0);
}

static void ewd_font_shutdown(void)
{
    if (ewd_font_texture) {
        GLuint texture=(GLuint)ewd_font_texture;
        glDeleteTextures(1,&texture);
        ewd_font_texture=0;
    }
}
