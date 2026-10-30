/* SPDX-License-Identifier: GPL-3.0-or-later
 * Pixel preview of the AFS CP LCDs: runs the real a380x_fcu_render.inc and
 * rasterises its rectangles and text with the embedded FCU font atlas exactly as
 * the GL path samples it (1:1 texels, whole-texel glyph origins).
 *
 *   clang -std=c11 -O2 -Wall -Wextra -Werror test_fcu_render.c -lm -o test_fcu_render
 *   ./test_fcu_render <out-dir>   writes fcu-<state>.ppm (2048 x 212, both LCDs)
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "a380x_guidance.h"
#include "a380x_efis.h"

typedef struct { float r,g,b; } Color;
static const Color BLACK={.004f,.008f,.012f};
static A380XGuidanceState guidance;
static A380XGuidanceInputs guidance_inputs;
static EfisSide efis[2];

enum { IMG_W=2048, IMG_H=720, IMG_Y0=1280 }; /* AFS CP slices + EFIS CP band */
static float image[IMG_H][IMG_W][3];
static unsigned char *atlas;

static void rectangle(float x0,float y0,float x1,float y1,Color c) {
    for(int y=(int)floorf(y0);y<(int)ceilf(y1);++y) for(int x=(int)floorf(x0);x<(int)ceilf(x1);++x) {
        int row=IMG_H-1-(y-IMG_Y0);
        if(row<0||row>=IMG_H||x<0||x>=IMG_W)continue;
        image[row][x][0]=c.r;image[row][x][1]=c.g;image[row][x][2]=c.b;
    }
}
static void fcu_text_draw(int face,float x,float baseline_y,const char *text,Color color);
#include "a380x_fcu_render.inc"
#include "a380x_efis_render.inc"

static void fcu_text_draw(int face,float x,float baseline_y,const char *text,Color color) {
    x=roundf(x);baseline_y=roundf(baseline_y);
    for(const unsigned char *p=(const unsigned char *)text;*p;++p) {
        const FcuGlyph *g=fcu_glyph_find(face,*p);
        assert(g);
        for(int gy=0;gy<g->h;++gy) for(int gx=0;gx<g->w;++gx) {
            float a=atlas[(g->y+gy)*FCU_FONT_WIDTH+g->x+gx]/255.f;
            int px=(int)x+g->left+gx, py=(int)baseline_y-g->top-gy-1; /* panel y-up */
            int row=IMG_H-1-(py-IMG_Y0);
            if(row<0||row>=IMG_H||px<0||px>=IMG_W||a<=0)continue;
            assert(px>=0&&px<IMG_W&&row>=0&&row<IMG_H); /* never draws outside the LCDs */
            image[row][px][0]=image[row][px][0]*(1-a)+color.r*a;
            image[row][px][1]=image[row][px][1]*(1-a)+color.g*a;
            image[row][px][2]=image[row][px][2]*(1-a)+color.b*a;
        }
        x+=g->advance;
    }
}

static void write_ppm(const char *dir,const char *name) {
    char path[1024];snprintf(path,sizeof(path),"%s/fcu-%s.ppm",dir,name);
    FILE *f=fopen(path,"wb");assert(f);
    fprintf(f,"P6\n%d %d\n255\n",IMG_W,IMG_H);
    for(int y=0;y<IMG_H;++y) for(int x=0;x<IMG_W;++x) for(int c=0;c<3;++c)
        fputc((int)lroundf(fminf(1,fmaxf(0,image[y][x][c]))*255),f);
    fclose(f);
}
static void render(const char *dir,const char *name) {
    memset(image,0,sizeof(image));draw_fcu();draw_efis_cp();write_ppm(dir,name);
}

int main(int argc,char **argv) {
    const char *dir=argc>1?argv[1]:".";
    size_t offset=0;
    atlas=malloc(FCU_FONT_WIDTH*FCU_FONT_HEIGHT);assert(atlas);
    for(size_t i=0;i<sizeof(fcu_font_runs);i+=2){memset(atlas+offset,fcu_font_runs[i+1],fcu_font_runs[i]);offset+=fcu_font_runs[i];}
    assert(offset==(size_t)FCU_FONT_WIDTH*FCU_FONT_HEIGHT);
    guidance_inputs.powered=1;
    /* EFIS CP: captain QNH 29.92 inHg (reference capture), F/O STD with a 1020 hPa preset. */
    efis[0].powered=efis[1].powered=1;efis[0].baro_inhg=1;efis[0].qnh_hpa=1013.25f;efis[0].baro_preset_hpa=1013.25f;
    efis[1].baro_std=1;efis[1].baro_preset_hpa=1020;efis[1].baro_preset_visible=1;efis[1].vor1=1;efis[1].vor2=1;
    /* Reference capture: SPD 100 KT, HDG 000 deg, ALT 00100, V/S dashed. */
    guidance.selected_speed_knots=100;guidance.selected_mach=.2;guidance.selected_heading_deg=0;
    guidance.selected_altitude_ft=100;guidance.selected_vs_fpm=0;guidance.selected_fpa_deg=0;guidance.vs_dashes=1;
    render(dir,"reference");
    guidance.speed_dashes=guidance.heading_dashes=1;guidance.speed_managed=guidance.heading_managed=1;
    guidance.selected_altitude_ft=35000;
    render(dir,"managed");
    guidance.speed_dashes=guidance.heading_dashes=guidance.vs_dashes=0;
    guidance.speed_managed=guidance.heading_managed=0;
    guidance.selected_speed_knots=250;guidance.selected_heading_deg=275;guidance.selected_altitude_ft=12000;
    guidance.selected_vs_fpm=-1800;
    render(dir,"selected");
    guidance.mach_mode=guidance.trk_fpa_mode=guidance.true_heading=1;
    guidance.selected_mach=.85;guidance.selected_heading_deg=93;guidance.selected_fpa_deg=-3.0;
    render(dir,"mach-trk-fpa");
    guidance_inputs.powered=0;efis[0].powered=efis[1].powered=0;render(dir,"off");
    free(atlas);
    puts("AFS CP + EFIS CP LCD raster: reference, managed, selected, MACH/TRK/FPA, STD preset and power-off previews written");
    return 0;
}
