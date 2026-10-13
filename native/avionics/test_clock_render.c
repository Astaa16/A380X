/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exercise the production clock aperture renderer without an OpenGL context.
 * Optional argv[1] writes an SVG of the three lit apertures for visual review.
 */
#include <assert.h>
#include <stdio.h>

typedef struct { float r,g,b; } Color;
static const Color BLACK={0,0,0};
static struct {
    int powered, mode, set_field, date_held, chr_display, et_display, et_selector;
    int hour, minute, second, day, month, year, chr_seconds, et_seconds;
    float flash_time;
} clock_state;
static int aperture_counts[3];
static FILE *svg;
static void rectangle(float left,float bottom,float right,float top,Color color)
{
    int aperture;
    assert(right>left && top>bottom);
    if (color.r || color.g || color.b) {
        assert(left>=332 && right<=420);
        aperture=bottom>=104 ? 0 : bottom>=72 ? 1 : 2;
        assert(bottom>=104-32*aperture && top<=132-32*aperture);
        assert(color.r>0.8f && color.g>0.8f && color.b>0.8f);
        ++aperture_counts[aperture];
    }
    if (svg) fprintf(svg,"<rect x=\"%.2f\" y=\"%.2f\" width=\"%.2f\" height=\"%.2f\" fill=\"rgb(%d,%d,%d)\"/>\n",
        left-332,132-top,right-left,top-bottom,
        (int)(255*color.r),(int)(255*color.g),(int)(255*color.b));
}
#include "a380x_clock_render.inc"

static void reset_counts(void)
{ aperture_counts[0]=aperture_counts[1]=aperture_counts[2]=0; }

int main(int argc,char **argv)
{
    int steady;
    draw_clock();
    assert(!aperture_counts[0] && !aperture_counts[1] && !aperture_counts[2]);
    clock_state.powered=1;
    clock_state.hour=12; clock_state.minute=46; clock_state.second=40;
    clock_state.day=6; clock_state.month=10; clock_state.year=2026;
    draw_clock();
    assert(!aperture_counts[0] && aperture_counts[1]>0 && !aperture_counts[2]);
    steady=aperture_counts[1];
    reset_counts();
    clock_state.mode=2; clock_state.set_field=0; clock_state.flash_time=0.75f;
    draw_clock();
    assert(aperture_counts[1]>0 && aperture_counts[1]<steady);
    reset_counts();
    clock_state.mode=0; clock_state.flash_time=0;
    clock_state.chr_display=clock_state.et_display=1;
    clock_state.chr_seconds=123; clock_state.et_seconds=7380;
    if (argc>1) {
        svg=fopen(argv[1],"w");
        assert(svg);
        fprintf(svg,"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"440\" height=\"460\" viewBox=\"0 0 88 92\">\n");
    }
    draw_clock();
    if (svg) { fputs("</svg>\n",svg); fclose(svg); svg=NULL; }
    assert(aperture_counts[0]>0 && aperture_counts[1]>0 && aperture_counts[2]>0);
    reset_counts();
    clock_state.et_selector=2;
    draw_clock();
    assert(aperture_counts[0]>0 && aperture_counts[1]>0 && !aperture_counts[2]);
    reset_counts();
    clock_state.date_held=1;
    draw_clock();
    assert(aperture_counts[1]>0);
    puts("Clock: three bounded white segmented apertures, cold blanking, ET reset and SET flashing");
    return 0;
}
