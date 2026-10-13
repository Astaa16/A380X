/* SPDX-License-Identifier: GPL-3.0-or-later
 * EFIS control-panel LCD cells. Rectangles match avionics_panel_layout.py.
 * Vector dot glyphs remain legible through the narrow physical screen UVs.
 */
static const unsigned char *efis_glyph(char c)
{
    static const unsigned char letters[26][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    static const unsigned char digits[10][7] = {
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14}
    };
    static const unsigned char blank[7]={0}, dash[7]={0,0,0,31,0,0,0}, dot[7]={0,0,0,0,0,6,6};
    if (c>='A' && c<='Z') return letters[c-'A'];
    if (c>='0' && c<='9') return digits[c-'0'];
    return c=='-' ? dash : c=='.' ? dot : blank;
}

static void efis_dot_text(float cx,float y,const char *text,float sx,float sy,Color color)
{
    int i,row,col;
    float x=cx-((float)strlen(text)*6.0f-1.0f)*sx*.5f;
    for(i=0;text[i];++i) {
        const unsigned char *bits=efis_glyph(text[i]);
        for(row=0;row<7;++row) for(col=0;col<5;++col)
            if(bits[row]&(1<<(4-col)))
                rectangle(x+col*sx,y+(6-row)*sy,x+col*sx+sx*.82f,y+(7-row)*sy-sy*.18f,color);
        x+=6*sx;
    }
}

static void efis_baro_number(float cx,float y,const char *text,Color color)
{
    static const unsigned char segments[10]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    int i; float width=0,x;
    for(i=0;text[i];++i) width+=text[i]=='.'?8:28;
    x=cx-(width-4)*.5f;
    for(i=0;text[i];++i) {
        const float w=23,h=39,t=3.5f;
        unsigned b=(text[i]>='0'&&text[i]<='9')?segments[text[i]-'0']:0;
        if(text[i]=='.') {rectangle(x,y,x+5,y+5,color);x+=8;continue;}
        if(b&1) rectangle(x+t,y+h-t,x+w-t,y+h,color);
        if(b&2) rectangle(x+w-t,y+h/2+t/2,x+w,y+h-t,color);
        if(b&4) rectangle(x+w-t,y+t,x+w,y+h/2-t/2,color);
        if(b&8) rectangle(x+t,y,x+w-t,y+t,color);
        if(b&16) rectangle(x,y+t,x+t,y+h/2-t/2,color);
        if(b&32) rectangle(x,y+h/2+t/2,x+t,y+h-t,color);
        if(b&64) rectangle(x+t,y+h/2-t/2,x+w-t,y+h/2+t/2,color);
        x+=28;
    }
}

static void draw_efis_cp(void)
{
    static const char *upper[5]={"CSTR","WPT","VORD","NDB","ARPT"};
    static const char *lower[5]={"VOR1","WX","TERR","TRAF","VOR2"};
    int side,i,row;
    float level=fcu_lcd_level;
    Color blue=fcu_tint((Color){.16f,.07f,.92f},level),white=fcu_tint(WHITE,level);
    Color subdued=fcu_tint(WHITE,level*.65f),selected=fcu_tint(GREEN,level);
    for(side=0;side<2;++side) {
        const EfisSide *s=&efis[side];
        float x=side?876:4;
        int flags[2][5]={{s->cstr,s->wpt,s->vord,s->ndb,s->arpt},
                         {s->vor1,0,s->terr,s->traf,s->vor2}};
        char value[24];
        rectangle(x,520,x+144,612,s->powered?blue:BLACK);
        for(row=0;row<2;++row) for(i=0;i<5;++i) {
            float y=(row?812:624)+36*i;
            rectangle(x,y,x+144,y+32,s->powered?blue:BLACK);
            if(!s->powered) continue;
            efis_dot_text(x+72,y+5,row?lower[i]:upper[i],4.0f,3.1f,
                flags[row][i]?white:subdued);
            if(flags[row][i]) {
                rectangle(x+3,y+3,x+6,y+29,selected);
                rectangle(x+138,y+3,x+141,y+29,selected);
            }
            if(row==1&&i==1&&s->wx_requested)
                efis_dot_text(x+72,y+1,"INOP",1.5f,1.0f,fcu_tint(AMBER,level));
        }
        if(!s->powered) continue;
        if(s->baro_std) {
            efis_dot_text(x+30,592,"PRE",1.3f,1.6f,white);
            if(s->baro_inhg) snprintf(value,sizeof(value),"%.2f",s->baro_preset_hpa/33.8638867f);
            else snprintf(value,sizeof(value),"%.0f",s->baro_preset_hpa);
            efis_dot_text(x+105,592,value,1.4f,1.6f,white);
            efis_dot_text(x+72,543,"STD",5.0f,5.0f,white);
        } else {
            efis_dot_text(x+32,587,"QNH",2.4f,2.5f,white);
            if(s->baro_inhg) snprintf(value,sizeof(value),"%.2f",s->qnh_hpa/33.8638867f);
            else snprintf(value,sizeof(value),"%.0f",s->qnh_hpa);
            efis_baro_number(x+72,532,value,white);
        }
    }
}
