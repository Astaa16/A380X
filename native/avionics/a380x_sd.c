/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "a380x_sd.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define SD_PI 3.14159265358979323846f
#define DEG "\xc2\xb0"

SdValue a380x_sd_value(float v, int supplied, float lo, float hi)
{
    SdValue r = {0, 0};
    if (supplied && isfinite(v) && v >= lo && v <= hi) { r.value = v; r.valid = 1; }
    return r;
}

int a380x_sd_page_supported(int page) { return page == SD_ENGINE || page == SD_APU; }

static void text(MfdDrawList *d, float x, float y, float size, MfdColor c, MfdAnchor a, const char *s)
{ mfd_draw_text(d, MFD_FONT_ECAM, size, c, a, x, y, s); }

static void line(MfdDrawList *d, float x, float y, float x1, float y1, float w, MfdColor c)
{ mfd_draw_line(d, x, y, x1, y1, w, c); }

static int usable(SdValue v) { return v.valid && isfinite(v.value); }

static void number(MfdDrawList *d, float x, float y, SdValue v, int active, int decimals, MfdColor c)
{
    char s[32];
    if (!active || !usable(v)) { text(d,x,y-4,29,MFD_COL_AMBER,MFD_ANCHOR_CENTER,"XX"); return; }
    if (decimals) {
        int n = (int)lroundf(v.value * 10);
        snprintf(s,sizeof(s),"%d",n/10);
        text(d,x+8,y,29,c,MFD_ANCHOR_RIGHT,s);
        text(d,x+14,y-6,25,c,MFD_ANCHOR_CENTER,".");
        snprintf(s,sizeof(s),"%d",abs(n%10));
        text(d,x+22,y,25,c,MFD_ANCHOR_LEFT,s);
    } else {
        snprintf(s,sizeof(s),"%.0f",v.value);
        text(d,x+28,y,29,c,MFD_ANCHOR_RIGHT,s);
    }
}

static void circle(MfdDrawList *d,float x,float y,float r,MfdColor c)
{ mfd_draw_arc(d,x,y,r,0,2*SD_PI,2,c); }

static void pressure(MfdDrawList *d,float x,float y,SdValue v,int active)
{
    mfd_draw_arc(d,x,y,45,-SD_PI,0,2,MFD_COL_WHITE);
    if (active && usable(v)) {
        float a = -SD_PI + SD_PI * fminf(500,fmaxf(0,v.value))/500;
        MfdColor c = v.value < 25 ? MFD_COL_RED : MFD_COL_GREEN;
        mfd_draw_arc(d,x,y,44,-SD_PI,-SD_PI+22*SD_PI/180,6,MFD_COL_RED);
        line(d,x+36*cosf(a),y+36*sinf(a),x+54*cosf(a),y+54*sinf(a),4,c);
        number(d,x,y,v,1,0,c);
    } else number(d,x,y,v,0,0,MFD_COL_GREEN);
}

static void engine_page(const SdData *s,MfdDrawList *d)
{
    static const float xs[] = {67,230,543,701};
    text(d,6,29,32,MFD_COL_WHITE,MFD_ANCHOR_LEFT,"ENGINE");
    line(d,6,35,122,35,2,MFD_COL_WHITE);
    for (int i=0;i<4;++i) {
        const SdEngine *e=&s->engine[i]; float x=xs[i], y=68;
        number(d,x,y,e->n2,e->active,1,MFD_COL_GREEN);
        number(d,x,y+38,e->n3,e->active,1,MFD_COL_GREEN);
        SdValue flow=e->fuel_flow_kg_h;
        if (usable(flow)) flow.value=ceilf(flow.value/10)*10;
        number(d,x,y+92,flow,e->active,0,MFD_COL_GREEN);
        circle(d,x,y+206,45,MFD_COL_WHITE);
        number(d,x,y+206,e->oil_quantity_qt,e->active,1,MFD_COL_GREEN);
        number(d,x,y+248,e->oil_temperature_c,e->active,0,
               usable(e->oil_temperature_c)&&e->oil_temperature_c.value>177?MFD_COL_AMBER:MFD_COL_GREEN);
        pressure(d,x,y+320,e->oil_pressure_psi,e->active);
        for (int k=0;k<3;++k) number(d,x-14,y+380+k*35,e->vibration[k],e->active,1,MFD_COL_GREEN);
        if (e->starting) {
            line(d,x-63,y-30,x-63,y+97,2,MFD_COL_WHITE);
            line(d,x-63,y-30,x+57,y-30,2,MFD_COL_WHITE);
            line(d,x+57,y-30,x+57,y+97,2,MFD_COL_WHITE);
            text(d,x,y+568,25,MFD_COL_WHITE,MFD_ANCHOR_CENTER,"START");
        } else {
            number(d,x,y+568,e->nacelle_temperature_c,e->active,0,MFD_COL_GREEN);
        }
        for (int k=0;k<3;++k) {
            float dx=i>1?-96:64;
            line(d,x+dx,y-10+k*40,x+dx+26,y-10+k*40,2,MFD_COL_WHITE);
        }
    }
    const char *labels[]={"N2","N3","FF","OIL","VIB","NAC"};
    const float ys[]={68,108,148,240,452,636};
    for (int i=0;i<6;++i) text(d,388,ys[i],25,MFD_COL_WHITE,MFD_ANCHOR_RIGHT,labels[i]);
    text(d,414,68,25,MFD_COL_CYAN,MFD_ANCHOR_RIGHT,"%");
    text(d,414,108,25,MFD_COL_CYAN,MFD_ANCHOR_RIGHT,"%");
    text(d,410,182,25,MFD_COL_CYAN,MFD_ANCHOR_RIGHT,"KG/H");
    text(d,398,276,25,MFD_COL_CYAN,MFD_ANCHOR_RIGHT,"QT");
    text(d,398,316,25,MFD_COL_CYAN,MFD_ANCHOR_RIGHT,DEG "C");
    text(d,410,386,25,MFD_COL_CYAN,MFD_ANCHOR_RIGHT,"PSI");
    text(d,428,452,25,MFD_COL_WHITE,MFD_ANCHOR_RIGHT,"N1");
    text(d,428,486,25,MFD_COL_WHITE,MFD_ANCHOR_RIGHT,"N2");
    text(d,428,520,25,MFD_COL_WHITE,MFD_ANCHOR_RIGHT,"N3");
    text(d,428,670,25,MFD_COL_CYAN,MFD_ANCHOR_RIGHT,DEG "C");
}

static void apu_gauge(MfdDrawList *d,float x,float y,const char *label,const char *unit,
                      SdValue v,float maximum,float limit)
{
    float start=135*SD_PI/180,end=406*SD_PI/180;
    mfd_draw_arc(d,x,y,64,start,end,2,MFD_COL_WHITE);
    text(d,x-94,y+8,26,MFD_COL_WHITE,MFD_ANCHOR_RIGHT,label);
    text(d,x+100,y+8,23,MFD_COL_CYAN,MFD_ANCHOR_LEFT,unit);
    number(d,x,y+9,v,1,0,usable(v)&&isfinite(limit)&&v.value>=limit?MFD_COL_RED:MFD_COL_GREEN);
    if (usable(v)) {
        float a=start+(end-start)*fminf(maximum,fmaxf(0,v.value))/maximum;
        line(d,x+54*cosf(a),y+54*sinf(a),x+75*cosf(a),y+75*sinf(a),3,
             isfinite(limit)&&v.value>=limit?MFD_COL_RED:MFD_COL_GREEN);
    }
}

static void apu_page(const SdData *s,MfdDrawList *d)
{
    text(d,6,29,32,MFD_COL_WHITE,MFD_ANCHOR_LEFT,"APU");
    line(d,6,35,64,35,2,MFD_COL_WHITE);
    if(s->apu_available) text(d,329,56,34,MFD_COL_GREEN,MFD_ANCHOR_LEFT,"AVAIL");
    for(int i=0;i<2;++i) {
        float x=78+i*114; char label[16];snprintf(label,sizeof(label),"GEN %d",i+1);
        text(d,x+48,104,22,MFD_COL_WHITE,MFD_ANCHOR_CENTER,label);
        mfd_draw_stroke_rect(d,mfd_rect(x,114,96,95),2,MFD_COL_WHITE);
        number(d,x+40,146,s->apu_gen_load[i],1,0,MFD_COL_GREEN);
        number(d,x+40,172,s->apu_gen_volts[i],1,0,MFD_COL_GREEN);
        number(d,x+40,198,s->apu_gen_hz[i],1,0,MFD_COL_GREEN);
    }
    const char *units[]={"%","V","HZ"};
    for(int i=0;i<3;++i) text(d,320,146+i*26,22,MFD_COL_CYAN,MFD_ANCHOR_LEFT,units[i]);
    mfd_draw_stroke_rect(d,mfd_rect(558,136,118,71),2,MFD_COL_WHITE);
    text(d,583,163,22,MFD_COL_WHITE,MFD_ANCHOR_LEFT,"BLEED");
    number(d,580,198,s->apu_bleed_psi,1,0,MFD_COL_GREEN);
    text(d,620,198,22,MFD_COL_CYAN,MFD_ANCHOR_LEFT,"PSI");
    MfdColor valve=s->apu_bleed_valid?MFD_COL_GREEN:MFD_COL_AMBER;
    circle(d,619,99,19.5f,valve);
    if(s->apu_bleed_valid) {
        if(s->apu_bleed_open) line(d,619,80,619,118,2,valve);
        else line(d,600,99,638,99,2,valve);
    } else text(d,619,107,22,MFD_COL_AMBER,MFD_ANCHOR_CENTER,"X");
    line(d,619,119,619,136,2,valve);
    line(d,29,263,29,234,4,MFD_COL_WHITE);line(d,29,234,741,234,4,MFD_COL_WHITE);
    line(d,741,234,741,263,4,MFD_COL_WHITE);
    text(d,258,277,26,MFD_COL_WHITE,MFD_ANCHOR_LEFT,"APU FU");
    number(d,415,277,s->apu_fuel_used_kg,1,0,MFD_COL_GREEN);
    text(d,474,277,23,MFD_COL_CYAN,MFD_ANCHOR_LEFT,"KG");
    apu_gauge(d,165,361,"N1","%",s->apu_n1,120,105);
    apu_gauge(d,510,361,"N2","%",s->apu_n2,120,102);
    /* APU EGT limits vary with operating state. No warning limit is invented
     * when the simulator supplies temperature but not that limit. */
    apu_gauge(d,165,568,"EGT",DEG "C",s->apu_egt_c,1200,NAN);
}

void a380x_sd_draw(const SdData *s,int page,MfdDrawList *d)
{
    mfd_draw_reset(d);
    mfd_draw_fill_rect(d,mfd_rect(0,0,MFD_W,MFD_H),MFD_COL_BLACK);
    if (!s || !s->powered) return;
    if(page==SD_ENGINE) engine_page(s,d);
    else if(page==SD_APU) apu_page(s,d);
    else text(d,384,480,29,MFD_COL_AMBER,MFD_ANCHOR_CENTER,"PAGE NOT AVAILABLE");
}
