/* SPDX-License-Identifier: GPL-3.0-or-later
 * OpenGL implementation of fcu_text_draw() for the AFS CP LCDs: textured quads
 * from the embedded FBW FCU font atlas (a380x_fcu_font.h). Glyph origins are
 * snapped to whole texels so the atlas, rasterised at the drawn size, is copied
 * 1:1 into the cockpit panel texture.
 */
static int fcu_font_texture;

static void fcu_text_draw(int face,float x,float baseline_y,const char *text,Color color)
{
    const unsigned char *cursor=(const unsigned char *)text;
    if (!fcu_font_texture) {
        GLint unpack_alignment;
        unsigned char *alpha=(unsigned char *)malloc(FCU_FONT_WIDTH*FCU_FONT_HEIGHT);
        size_t i,offset=0;
        if (!alpha) return;
        for (i=0;i<sizeof(fcu_font_runs);i+=2) {
            memset(alpha+offset,fcu_font_runs[i+1],fcu_font_runs[i]);
            offset+=fcu_font_runs[i];
        }
        XPLMGenerateTextureNumbers(&fcu_font_texture,1);
        XPLMBindTexture2d(fcu_font_texture,0);
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack_alignment);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D,0,GL_ALPHA,FCU_FONT_WIDTH,FCU_FONT_HEIGHT,0,GL_ALPHA,GL_UNSIGNED_BYTE,alpha);
        glPixelStorei(GL_UNPACK_ALIGNMENT,unpack_alignment);
        free(alpha);
    }
    x=roundf(x); baseline_y=roundf(baseline_y);
    XPLMSetGraphicsState(0,1,0,0,1,0,0);
    XPLMBindTexture2d(fcu_font_texture,0);
    glColor3f(color.r,color.g,color.b);
    glBegin(GL_QUADS);
    for (;*cursor;++cursor) {
        const FcuGlyph *g=fcu_glyph_find(face,*cursor);
        if (!g) continue;
        if (g->w && g->h) {
            float left=x+g->left, top=baseline_y-g->top;
            float right=left+g->w, bottom=top-g->h;
            float u0=(float)g->x/FCU_FONT_WIDTH,u1=(float)(g->x+g->w)/FCU_FONT_WIDTH;
            float v0=(float)g->y/FCU_FONT_HEIGHT,v1=(float)(g->y+g->h)/FCU_FONT_HEIGHT;
            glTexCoord2f(u0,v0);glVertex2f(left,top);
            glTexCoord2f(u1,v0);glVertex2f(right,top);
            glTexCoord2f(u1,v1);glVertex2f(right,bottom);
            glTexCoord2f(u0,v1);glVertex2f(left,bottom);
        }
        x+=g->advance;
    }
    glEnd();
    XPLMSetGraphicsState(0,0,0,0,1,0,0);
}

static void fcu_font_shutdown(void)
{
    if (fcu_font_texture) {
        GLuint texture=(GLuint)fcu_font_texture;
        glDeleteTextures(1,&texture);
        fcu_font_texture=0;
    }
}
