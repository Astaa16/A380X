/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native translation of the pinned FBW A380 EWD (768 x 1024 EIS canvas).
 * EWD.tsx, EngineGauge/ThrustGauge/N1/EGT and WdMemos define the geometry.
 * All engine needles/readouts consume the current adapter snapshot.
 */
#include "a380x_ewd_data.h"
static const Color EWD_WHITE={1,1,1}, EWD_GREEN={0,1,0}, EWD_CYAN={0,1,1};
static const Color EWD_AMBER={230.0f/255.0f,128.0f/255.0f,0}, EWD_RED={1,0,0};
static const Color EWD_BLACK={0,0,0}, EWD_GREY={79.0f/255.0f,84.0f/255.0f,103.0f/255.0f};
static const Color EWD_LIGHT_GREY={196.0f/255.0f,198.0f/255.0f,207.0f/255.0f};
typedef struct { float x,y; } EwdCanvas;
static float ewd_brightness=1;
static Color ewd_dim(Color c) {c.r*=ewd_brightness;c.g*=ewd_brightness;c.b*=ewd_brightness;return c;}
static float ewd_clamp(float value,float low,float high) { return fmaxf(low,fminf(high,value)); }
static void ewd_line(const EwdCanvas *p,float x1,float y1,float x2,float y2,Color color,float width)
{
    glLineWidth(width); line(p->x+x1,p->y+1024-y1,p->x+x2,p->y+1024-y2,ewd_dim(color));
}
static void ewd_rect(const EwdCanvas *p,float x,float y,float w,float h,Color color)
{ rectangle(p->x+x,p->y+1024-y-h,p->x+x+w,p->y+1024-y,ewd_dim(color)); }
static void ewd_text(const EwdCanvas *p,float x,float y,const char *s,float size,Color color,int anchor)
{ ewd_font_draw(p->x+x,p->y+1024-y,s,size,ewd_dim(color),0,anchor); }
static void ewd_memo_text(const EwdCanvas *p,float x,float y,const char *s,float size,Color color)
{ ewd_font_draw(p->x+x,p->y+1024-y,s,size,ewd_dim(color),1.75f,0); }
static void ewd_spread_text(const EwdCanvas *p,float x,float y,const char *s,Color color,int anchor)
{ ewd_font_draw(p->x+x,p->y+1024-y,s,26,ewd_dim(color),1.6f,anchor); }
static void ewd_box(const EwdCanvas *p,float x,float y,float w,float h,int fill)
{
    if(fill) ewd_rect(p,x,y,w,h,EWD_BLACK);
    ewd_line(p,x,y,x+w,y,EWD_GREY,2);ewd_line(p,x+w,y,x+w,y+h,EWD_GREY,2);
    ewd_line(p,x+w,y+h,x,y+h,EWD_GREY,2);ewd_line(p,x,y+h,x,y,EWD_GREY,2);
}
static void ewd_arc(const EwdCanvas *p,float cx,float cy,float r,float start,float end,Color color,float width)
{
    int i,segments=(int)ceilf(fabsf(end-start)/3.0f);
    float previous_x=cx+r*sinf(start*(float)M_PI/180.0f);
    float previous_y=cy-r*cosf(start*(float)M_PI/180.0f);
    if(segments<1) segments=1;
    for(i=1;i<=segments;++i) {
        float a=(start+(end-start)*(float)i/(float)segments)*(float)M_PI/180.0f;
        float x=cx+r*sinf(a),y=cy-r*cosf(a);
        ewd_line(p,previous_x,previous_y,x,y,color,width);previous_x=x;previous_y=y;
    }
}
static void ewd_radial(const EwdCanvas *p,float cx,float cy,float radius,float angle,
                        float inner,float outer,Color color,float width)
{
    float dx=sinf(angle*(float)M_PI/180.0f),dy=-cosf(angle*(float)M_PI/180.0f);
    ewd_line(p,cx+dx*radius*inner,cy+dy*radius*inner,
                cx+dx*radius*outer,cy+dy*radius*outer,color,width);
}
static void ewd_decimal(const EwdCanvas *p,float x,float y,float value,Color color)
{
    char whole[16],decimal[4];int tenths=(int)lroundf(value*10.0f);
    snprintf(whole,sizeof(whole),"%d",tenths/10);snprintf(decimal,sizeof(decimal),"%d",abs(tenths%10));
    ewd_text(p,x,y,whole,26,color,2);ewd_text(p,x+14,y,".",26,color,2);
    ewd_text(p,x+30,y,decimal,20,color,2);
}
static void ewd_pie(const EwdCanvas *p,float cx,float cy,float radius,float start,float end)
{
    int i,segments=(int)ceilf(fabsf(end-start)/2.0f);
    if(segments<1) return;
    glColor3f(EWD_GREY.r*ewd_brightness,EWD_GREY.g*ewd_brightness,EWD_GREY.b*ewd_brightness);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(p->x+cx,p->y+1024-cy);
    for(i=0;i<=segments;++i) {
        float a=(start+(end-start)*i/segments)*(float)M_PI/180;
        glVertex2f(p->x+cx+radius*sinf(a),p->y+1024-cy+radius*cosf(a));
    }
    glEnd();
}
static void ewd_transient(const EwdCanvas *p,float cx,float cy,float r,float actual,float target)
{
    int i;
    static const float radii[]={.8f,.65f,.5f,.35f};
    if(fabsf(actual-target)<=1.1f) return;
    for(i=0;i<4;++i) ewd_arc(p,cx,cy,r*radii[i],fminf(actual,target),fmaxf(actual,target),EWD_GREEN,1);
    ewd_radial(p,cx,cy,r,target,0,.8f,EWD_GREEN,1);
}
static void ewd_egt_marker(const EwdCanvas *p,float cx,float cy,float r)
{
    static const float vx[]={-6,6,6,-2},vy[]={0,0,4,4};
    float angle=333*(float)M_PI/180,c=cosf(angle),s=sinf(angle);
    float x=cx+c*r,y=cy+s*r,px[4],py[4];int i;
    for(i=0;i<4;++i) { px[i]=x+vx[i]*c-vy[i]*s;py[i]=y+vx[i]*s+vy[i]*c; }
    glColor3f(EWD_AMBER.r*ewd_brightness,EWD_AMBER.g*ewd_brightness,EWD_AMBER.b*ewd_brightness);glBegin(GL_TRIANGLE_FAN);
    for(i=0;i<4;++i) glVertex2f(p->x+px[i],p->y+1024-py[i]);
    glEnd();
    for(i=0;i<4;++i) ewd_line(p,px[i],py[i],px[(i+1)%4],py[(i+1)%4],EWD_AMBER,3);
}
static void ewd_attention(const EwdCanvas *p,float x,Color color)
{
    ewd_line(p,x-74,113,x-74,41,color,2);ewd_line(p,x-74,41,x+88,41,color,2);
    ewd_line(p,x+88,41,x+88,113,color,2);
    ewd_line(p,x-74,294,x-74,366,color,2);ewd_line(p,x-74,366,x+88,366,color,2);
    ewd_line(p,x+88,366,x+88,294,color,2);
}
static void ewd_engine(const EwdCanvas *p,const EwdEngine *e,float x,int number,int athr_engaged,int native_attention)
{
    float cx=x+2,cy=123,r=64;int i;
    int thr_valid=e->active&&!e->n1_degraded&&e->thrust_valid&&isfinite(e->thrust);
    int n1_valid=e->active&&e->n1_valid&&isfinite(e->n1);
    int egt_valid=e->active&&e->egt_valid&&isfinite(e->egt);
    int degraded=e->active&&!thr_valid;
    int reverse=(number==2||number==3)&&e->reverse;
    char text[24];
    if(!thr_valid) {
        ewd_arc(p,cx,143,r,320,400,EWD_AMBER,2);
        ewd_spread_text(p,cx+55,75,"THR XX",EWD_AMBER,2);
    } else {
        static const float tick[]={0,25,50,75,100};
        if(!reverse&&(e->running||e->avail)&&e->available_valid&&isfinite(e->available_thrust))
            ewd_pie(p,cx,cy,r,239.24f,230+2.2f*ewd_clamp(e->available_thrust,4.2f,100));
        ewd_arc(p,cx,cy,r,230,450,EWD_WHITE,2);
        for(i=0;i<5;++i) ewd_radial(p,cx,cy,r,230+2.2f*tick[i],.9f,1,EWD_WHITE,2);
        /* SVG central alignment to EIS font baseline: half the 20px cap height. */
        ewd_text(p,cx-34.12f,cy+42.52f,"0",20,EWD_WHITE,1);
        ewd_text(p,cx-14.70f,cy-31.63f,"5",20,EWD_WHITE,1);
        ewd_text(p,cx+30.6f,cy+14.5f,"10",20,EWD_WHITE,1);
        if(reverse) {
            ewd_pie(p,cx,cy,58,131.33333f,216.66667f);
            ewd_arc(p,cx,cy,58,130,230,EWD_WHITE,2);
            ewd_radial(p,cx,cy,58,130,1.1f,1,EWD_WHITE,2);
            ewd_radial(p,cx,cy,58,180,1.1f,1,EWD_WHITE,2);
            ewd_radial(p,cx,cy,58,230-ewd_clamp(e->thrust,0,100),0,1.1f,EWD_GREEN,3);
            ewd_box(p,cx-46,cy-27,90,24,1);
            snprintf(text,sizeof(text),"REV %d",number);
            ewd_spread_text(p,cx-43,cy-5,text,e->reverse_deployed?EWD_GREEN:EWD_AMBER,0);
            ewd_text(p,cx+24.88f,cy+42.51f,"MAX",20,EWD_WHITE,1);
        } else {
            if(e->commanded_valid&&isfinite(e->commanded_thrust))
                ewd_transient(p,cx,cy,r,230+2.2f*ewd_clamp(e->thrust,0,100),230+2.2f*ewd_clamp(e->commanded_thrust,0,100));
            ewd_box(p,cx-11,cy+21,96,30,0);
            ewd_radial(p,cx,cy,r,230+2.2f*ewd_clamp(e->thrust,0,100),0,1.1f,EWD_GREEN,3);
            ewd_decimal(p,cx+48,170,e->thrust,EWD_GREEN);
            if(e->avail) { ewd_box(p,cx-46,cy-27,90,24,1);ewd_spread_text(p,cx-44,cy-5,"AVAIL",EWD_GREEN,0); }
        }
        if(e->throttle_valid&&isfinite(e->throttle)) {
            float angle=(reverse?230-ewd_clamp(e->throttle,0,100):230+2.2f*ewd_clamp(e->throttle,0,100))*(float)M_PI/180;
            float radius=(reverse?58:r)*1.12f;
            ewd_arc(p,cx+radius*sinf(angle),cy-radius*cosf(angle),4,0,360,EWD_CYAN,2);
        }
    }
    if(!e->active||degraded) {
        float nx=degraded?x+2:x;
        ewd_arc(p,nx,191,64,230,450,EWD_WHITE,2);
        if(degraded&&n1_valid) {
            const float ticks[]={20,40,60,80,100};
            float indicated_n1=e->idle_n1_valid?fmaxf(e->n1,e->idle_n1):e->n1;
            for(i=0;i<5;++i) ewd_radial(p,nx,191,64,230+(ticks[i]-20)/91*220,.9f,1,EWD_WHITE,2);
            ewd_arc(p,nx,191,62,426,450,EWD_RED,6);
            ewd_text(p,nx-36.12f,235.52f,"2",20,EWD_WHITE,1);
            ewd_text(p,nx-24.86f,163.57f,"6",20,EWD_WHITE,1);
            ewd_text(p,nx+20.54f,177.93f,"10",20,EWD_WHITE,1);
            if(athr_engaged&&e->commanded_n1_valid&&isfinite(e->commanded_n1))
                ewd_transient(p,x,191,64,230+(e->n1-20)/91*220,230+(e->commanded_n1-20)/91*220);
            ewd_box(p,nx-13,210,96,30,0);
            ewd_radial(p,nx,191,64,230+(ewd_clamp(indicated_n1,0,111)-20)/91*220,0,1.1f,EWD_GREEN,3);
            ewd_decimal(p,nx+46,236,e->n1,EWD_GREEN);
            if(e->throttle_n1_valid&&isfinite(e->throttle_n1)) {
                float a=(230+(ewd_clamp(e->throttle_n1,3,111)-20)/91*220)*(float)M_PI/180;
                ewd_arc(p,nx+64*1.12f*sinf(a),191-64*1.12f*cosf(a),4,0,360,EWD_CYAN,2);
            }
        } else ewd_spread_text(p,x+48,236,"XX",EWD_AMBER,2);
    } else if(n1_valid) ewd_decimal(p,x+6,236,e->n1,EWD_GREEN);
    else ewd_text(p,x+36,236,"XX",26,EWD_AMBER,2);
    cx=x+4;cy=338;r=68;
    ewd_arc(p,cx,cy,r,270,450,EWD_WHITE,2);
    if(!egt_valid) ewd_text(p,cx+17,349.7f,"XX",26,EWD_AMBER,2);
    else {
        Color color=e->egt>=900?EWD_RED:e->egt>850?EWD_AMBER:EWD_GREEN;
        ewd_arc(p,cx,cy,r-2,430,450,EWD_RED,6);
        for(i=0;i<=2;++i) ewd_radial(p,cx,cy,r,270+i*90,.9f,1,i==2?EWD_RED:EWD_WHITE,2);
        ewd_egt_marker(p,cx,cy,r);
        ewd_box(p,cx-36,cy-11,72,26,0);
        ewd_radial(p,cx,cy,r,270+ewd_clamp(e->egt,0,1000)*.18f,.75f,1.15f,color,3);
        snprintf(text,sizeof(text),"%.0f",e->egt);
        ewd_text(p,cx+33,349.7f,text,23,color,2);
    }
    if(!native_attention) return;
    if(e->active&&(e->fire||(e->egt_valid&&e->egt>=900))) ewd_attention(p,x,EWD_AMBER);
    else if(e->active&&e->starting) ewd_attention(p,x,EWD_WHITE);
}
/* FWS attention getters (AttentionGetter.tsx through EngineGauge): amber abnormal over white. */
static void ewd_fws_attention(const EwdCanvas *p,const EwdData *d,const float *engine_x)
{
    int i;
    for(i=0;i<4;++i) {
        if(d->attention_abnormal[i]) ewd_attention(p,engine_x[i],EWD_AMBER);
        else if(d->attention_normal[i]) ewd_attention(p,engine_x[i],EWD_WHITE);
    }
}
#include "a380x_ewd_wd.inc"
static Color ewd_message_color(int color)
{
    switch(color) {
        case EWD_COLOR_GREEN:return EWD_GREEN;case EWD_COLOR_CYAN:return EWD_CYAN;
        case EWD_COLOR_AMBER:return EWD_AMBER;case EWD_COLOR_RED:return EWD_RED;
        default:return EWD_WHITE;
    }
}
static void draw_ewd(const EwdData *d,float x,float y)
{
    EwdCanvas p={x,y};int i,any_active=0,reverse=0;
    static const float engine_x[]={93,262,497,668};
    ewd_rect(&p,0,0,768,1024,EWD_BLACK);
    if(!d->powered) return;
    for(i=0;i<4;++i) { any_active|=d->engine[i].active;if(i==1||i==2) reverse|=d->engine[i].reverse; }
    if(!reverse&&!d->thr_lk) {
        if(any_active&&d->thrust_limit_valid&&isfinite(d->thrust_limit)) {
            ewd_text(&p,330,30,d->thrust_mode,30,EWD_CYAN,2);
            char whole[16],fraction[4];int tenths=(int)lroundf(d->thrust_limit*10);
            snprintf(whole,sizeof(whole),"%d",tenths/10);snprintf(fraction,sizeof(fraction),"%d",abs(tenths%10));
            ewd_text(&p,399,28,whole,26,EWD_GREEN,2);ewd_text(&p,416,28,".",26,EWD_GREEN,2);
            ewd_text(&p,431,28,fraction,20,EWD_GREEN,2);
            ewd_text(&p,447,28,"%",20,EWD_CYAN,2);
            if(d->flex_valid) { char s[20];snprintf(s,sizeof(s),"%.0f\xc2\xb0" "C",d->flex_temp);ewd_text(&p,484,30,s,20,EWD_CYAN,0); }
        } else ewd_text(&p,312,30,"XX",26,EWD_AMBER,1);
        ewd_text(&p,750,30,d->bleed,26,EWD_GREEN,2);
    }
    if(d->idle) ewd_text(&p,386,90,"IDLE",26,EWD_GREEN,1);
    for(i=0;i<4;++i) ewd_engine(&p,&d->engine[i],engine_x[i],i+1,d->athr_engaged,!d->attention_valid);
    if(d->attention_valid) ewd_fws_attention(&p,d,engine_x);
    ewd_text(&p,386,121,"THR",26,EWD_WHITE,1);ewd_text(&p,386,142,"%",20,EWD_CYAN,1);
    ewd_text(&p,386,220,"N1",26,EWD_WHITE,1);ewd_text(&p,386,243,"%",20,EWD_CYAN,1);
    ewd_text(&p,384,316,"EGT",26,EWD_WHITE,1);ewd_text(&p,384,339,"\xc2\xb0" "C",20,EWD_CYAN,1);
    if(d->engine[0].thrust_valid&&d->engine[1].thrust_valid) ewd_line(&p,171,228,195,226,EWD_LIGHT_GREY,3);
    if(d->engine[1].thrust_valid) ewd_line(&p,335,216,355,214,EWD_LIGHT_GREY,3);
    if(d->engine[2].thrust_valid) ewd_line(&p,416,216,436,218,EWD_LIGHT_GREY,3);
    if(d->engine[2].thrust_valid&&d->engine[3].thrust_valid) ewd_line(&p,576,226,600,228,EWD_LIGHT_GREY,3);
    ewd_rect(&p,8,379,752,4,EWD_LIGHT_GREY);
    if(d->a_floor) ewd_text(&p,20,30,"A.FLOOR",26,EWD_AMBER,0);
    if(d->thr_lk) ewd_text(&p,387,30,"THR LK",30,EWD_AMBER,2);
    if(d->wd.valid) ewd_wd_draw(&p,&d->wd);
    else if(d->warning_count>0) {
        for(i=0;i<d->warning_count&&i<16;++i)
            ewd_memo_text(&p,30,419+i*34,d->warning[i].text,26,ewd_message_color(d->warning[i].color));
    } else {
        int rows=d->memo_count>d->limit_count?d->memo_count:d->limit_count;
        if(rows>10) rows=10;
        if(rows>0) {
            const Color divider={211.0f/255.0f,211.0f/255.0f,211.0f/255.0f};
            ewd_rect(&p,383,395,2,(float)rows*30+3,divider);
        }
        for(i=0;i<d->limit_count&&i<10;++i)
            ewd_memo_text(&p,8,419+i*30,d->limit[i].text,24,ewd_message_color(d->limit[i].color));
        for(i=0;i<d->memo_count&&i<10;++i)
            ewd_memo_text(&p,392,419+i*30,d->memo[i].text,24,ewd_message_color(d->memo[i].color));
    }
    glLineWidth(1);
}
