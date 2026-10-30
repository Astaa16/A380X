/* SPDX-License-Identifier: GPL-3.0-or-later
 * EFIS CP LCDs, translated from the pinned FlyByWire A380X FCU display
 * (FCU/Components/NdDataDisplay.tsx and BaroRefDisplay.tsx):
 *  - ND-filter legends are FBW's own dot-matrix artwork (a380x_efis_legends.h,
 *    extracted from html_ui/Images/fbw-a380x/fcu), redrawn dot-for-dot: lit dots
 *    white, unlit dots faint grey. VOR1/VOR2 show the VOR or pointer legend.
 *    As in FBW the legends do not change with the filter state; the selection is
 *    shown on the ND.
 *  - Baro window: FBW "Digital" value (QNH value, "Std"), Poppins QNH legend with
 *    the inactive QFE legend ghosted, and the small STD preselection (top right)
 *    once the knob has been turned in STD.
 * Rectangles are panel-atlas texels and must match tools/avionics_panel_layout.py
 * (EFIS_UPPER/LOWER_PANEL_RECTS, EFIS_BARO_PANEL_RECTS).
 */
#include "a380x_efis_legends.h"
static const float efis_upper_cells[2][5][4]={
    {{0,1500,307,1604},{307,1500,614,1604},{614,1500,922,1604},{922,1500,1229,1604},{1229,1500,1536,1604}},
    {{0,1724,307,1828},{307,1724,614,1828},{614,1724,922,1828},{922,1724,1229,1828},{1229,1724,1536,1828}}};
static const float efis_lower_cells[2][5][4]={
    {{0,1612,262,1716},{321,1612,624,1716},{624,1612,923,1716},{923,1612,1210,1716},{1274,1612,1536,1716}},
    {{0,1836,262,1940},{326,1836,613,1940},{613,1836,912,1940},{912,1836,1215,1940},{1274,1836,1536,1940}}};
static const float efis_upper_rows[2][4]={{0,1500,1536,1604},{0,1724,1536,1828}};
static const float efis_baro_rects[2][4]={{1544,1500,1928,1744},{1544,1752,1928,1996}};

static Color efis_mix(Color a,Color b,float t) {
    return (Color){a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t};
}
/* One FBW legend centred in its cell: 24 x 7 square dots on a 52-unit pitch. */
static void efis_draw_legend(const float cell[4],int legend,Color lit,Color unlit) {
    /* Reference capture: a legend is 54 % of its LCD row tall; never wider than 92 % of its cell. */
    float w=cell[2]-cell[0],h=cell[3]-cell[1];
    float span=(EFIS_LEGEND_ROWS-1)+(float)EFIS_LEGEND_DOT/(float)EFIS_LEGEND_PITCH;
    float pitch=fminf(.54f*h/span,.92f*w/((EFIS_LEGEND_COLS-1)+(float)EFIS_LEGEND_DOT/(float)EFIS_LEGEND_PITCH));
    float dot=pitch*(float)EFIS_LEGEND_DOT/(float)EFIS_LEGEND_PITCH;
    float x0=cell[0]+(w-(EFIS_LEGEND_COLS-1)*pitch-dot)*.5f;
    float top=cell[3]-(h-(EFIS_LEGEND_ROWS-1)*pitch-dot)*.5f;
    for(int row=0;row<EFIS_LEGEND_ROWS;++row) for(int col=0;col<EFIS_LEGEND_COLS;++col) {
        int state=efis_legends[legend][row][col];
        if(!state)continue;
        float x=x0+col*pitch,y=top-row*pitch-dot;
        rectangle(x,y,x+dot,y+dot,state==2?lit:unlit);
    }
}
/* Baro text at FBW BaroRefDisplay positions (fractions of the window, y down). */
static void efis_baro_text(const float r[4],int face,float fx,float fy,int right,const char *text,Color color) {
    float w=r[2]-r[0],h=r[3]-r[1],x=r[0]+fx*w;
    if(right)x-=fcu_text_width(face,text);
    fcu_text_draw(face,x,r[3]-fy*h,text,color);
}
static void efis_baro_value(char *out,size_t n,float hpa,int inhg) {
    if(inhg)snprintf(out,n,"%.2f",fminf(99.99f,fmaxf(0,hpa/33.8638867f)));
    else snprintf(out,n,"%04d",(int)lroundf(fminf(9999,fmaxf(0,hpa))));
}

static void draw_efis_cp(void)
{
    static const int upper[5]={EFIS_LEGEND_CSTR,EFIS_LEGEND_WPT,EFIS_LEGEND_VORD,EFIS_LEGEND_NDB,EFIS_LEGEND_ARPT};
    float level=fcu_lcd_level;
    Color blue=fcu_tint(FCU_LCD_BACKGROUND,level),white=fcu_tint(FCU_LCD_INK,level);
    Color ghost=fcu_tint(FCU_LCD_GHOST,level);
    /* FBW unlit dot: rgba(195,195,195,38/255) over the LCD background. */
    Color unlit=efis_mix(blue,fcu_tint((Color){195.f/255,195.f/255,195.f/255},level),38.f/255);
    for(int side=0;side<2;++side) {
        const EfisSide *s=&efis[side];
        int lower[5]={s->vor1?EFIS_LEGEND_VOR1:EFIS_LEGEND_POINTER1,
                      EFIS_LEGEND_WX,EFIS_LEGEND_TERR,EFIS_LEGEND_TRAF,
                      s->vor2?EFIS_LEGEND_VOR2:EFIS_LEGEND_POINTER2};
        Color bed=s->powered?blue:BLACK;
        const float *u=efis_upper_rows[side],*b=efis_baro_rects[side];
        rectangle(u[0],u[1],u[2],u[3],bed);
        for(int i=0;i<5;++i) {
            const float *c=efis_lower_cells[side][i];
            rectangle(c[0],c[1],c[2],c[3],bed);
        }
        rectangle(b[0],b[1],b[2],b[3],bed);
        if(!s->powered)continue;
        for(int i=0;i<5;++i) {
            efis_draw_legend(efis_upper_cells[side][i],upper[i],white,unlit);
            efis_draw_legend(efis_lower_cells[side][i],lower[i],white,unlit);
        }
        /* Baro: QNH legend lit in QNH mode, ghosted with the inactive QFE otherwise. */
        {
            char value[24];
            float qnh_end=.06f+fcu_text_width(FCU_FACE_BARO_LABEL,"QNH")/(b[2]-b[0]);
            efis_baro_text(b,FCU_FACE_BARO_LABEL,.06f,.23f,0,"QNH",s->baro_std?ghost:white);
            if(!(s->baro_std&&s->baro_preset_visible)) /* the STD preset occupies that corner */
                efis_baro_text(b,FCU_FACE_BARO_LABEL,qnh_end+.02f,.23f,0,"QFE",ghost);
            if(s->baro_std) {
                efis_baro_text(b,FCU_FACE_BARO_VALUE,.04f,.95f,0,"Std",white);
                if(s->baro_preset_visible) {
                    efis_baro_value(value,sizeof(value),s->baro_preset_hpa,s->baro_inhg);
                    efis_baro_text(b,FCU_FACE_BARO_PRESET,.97f,.30f,1,value,white);
                }
            } else {
                efis_baro_value(value,sizeof(value),s->qnh_hpa,s->baro_inhg);
                efis_baro_text(b,FCU_FACE_BARO_VALUE,.04f,.95f,0,value,white);
            }
        }
    }
}
