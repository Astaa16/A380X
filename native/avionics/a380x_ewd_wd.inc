/* SPDX-License-Identifier: GPL-3.0-or-later
 * Lower EWD (warning display) renderer, translated from the pinned FlyByWire A380X EWD:
 *  - FormattedFwcText.tsx: FWC escape codes (\x1b<Nm colour, \x1b4m underline, \x1b'm frame,
 *    \x1bm reset, \r new line every 30 px, decorations on a 16 px letter grid). As in FBW, a
 *    decoration is only emitted when its segment is closed by an escape or \r, and only the
 *    Amber/Green/White/Red line classes have a stroke.
 *  - WdMemos / WdLimitations (two columns, 30 px lines, light grey divider) and the STS area
 *    (FAILURE PENDING / STS / ADV boxes).
 *  - WdAbstractChecklistComponent + EclLine (18 x 34 px lines, checkbox column, cyan selection
 *    box, white end-marker bracket, overflow arrows) with the CSS colour cascade of style.scss.
 * Unity-build include of a380x_ewd_render.inc (uses its canvas helpers and colours).
 */
#include "a380x_ewd_wd.h"

static const Color EWD_YELLOW={1,1,0}, EWD_MAGENTA={1,148.0f/255.0f,1};
enum { EWD_FWC_WHITE, EWD_FWC_YELLOW, EWD_FWC_RED, EWD_FWC_GREEN, EWD_FWC_AMBER, EWD_FWC_CYAN, EWD_FWC_MAGENTA };

static Color ewd_fwc_color(int c)
{
    switch(c) {
    case EWD_FWC_YELLOW:return EWD_YELLOW;case EWD_FWC_RED:return EWD_RED;case EWD_FWC_GREEN:return EWD_GREEN;
    case EWD_FWC_AMBER:return EWD_AMBER;case EWD_FWC_CYAN:return EWD_CYAN;case EWD_FWC_MAGENTA:return EWD_MAGENTA;
    default:return EWD_WHITE;
    }
}
/* Only these colours have a "<Colour>Line" stroke class in style.scss. */
static int ewd_fwc_has_line(int c) { return c==EWD_FWC_WHITE||c==EWD_FWC_RED||c==EWD_FWC_GREEN||c==EWD_FWC_AMBER; }

/* Drawable ASCII copy (NBSP -> space, other non-ASCII dropped); returns the length. */
static int ewd_wd_plain(char *out,size_t size,const char *in,size_t length)
{
    size_t i,n=0;
    for(i=0;i<length&&n+1<size;++i) {
        unsigned char c=(unsigned char)in[i];
        if(c==0xc2&&i+1<length&&(unsigned char)in[i+1]==0xa0) { out[n++]=' ';++i;continue; }
        if(c==0xa0) c=' ';
        if(c==0xc2&&i+1<length&&(unsigned char)in[i+1]==0xb0) { out[n++]=(char)c;out[n++]=in[++i];continue; }
        if(c>=32&&c<127) out[n++]=(char)c;
    }
    out[n]=0;
    return (int)n;
}

/* Width of a run including CSS letter-spacing after every character (tspans are contiguous). */
static float ewd_run_width(const char *text,float size,float tracking)
{
    return *text?ewd_font_width(text,size,tracking)+tracking:0.0f;
}

#define EWD_FWC_LINE_SPACING 30.0f
#define EWD_FWC_LETTER_WIDTH 16.0f
#define EWD_FWC_SIZE 24.0f      /* .EWDWarn */
#define EWD_FWC_TRACKING 1.75f

/* FormattedFwcText at (x, y) = first baseline; returns the number of text lines drawn. */
static int ewd_fwc_text(const EwdCanvas *p,float x,float y,const char *message)
{
    char buffer[FWS_TEXT_MAX*2];
    size_t n=0,i,length=message?strlen(message):0;
    int color=EWD_FWC_WHITE,underlined=0,framed=0,col=0,start_col=0,lines=0,line_has_text=0;
    float y_offset=0,cursor_x=x;
    for(i=0;i<=length;++i) {
        char c=i<length?message[i]:0;
        int close=(c=='\x1b'||c=='\r'||c==0);
        if(close&&n>0) {
            char plain[FWS_TEXT_MAX*2];
            buffer[n]=0;ewd_wd_plain(plain,sizeof(plain),buffer,n);
            ewd_font_draw(p->x+cursor_x,p->y+1024-(y+y_offset),plain,EWD_FWC_SIZE,ewd_dim(ewd_fwc_color(color)),EWD_FWC_TRACKING,0);
            cursor_x+=ewd_run_width(plain,EWD_FWC_SIZE,EWD_FWC_TRACKING);
            line_has_text=1;
            if(c!=0&&underlined&&ewd_fwc_has_line(color))
                ewd_line(p,x+start_col*EWD_FWC_LETTER_WIDTH,y+y_offset+4,
                         x+start_col*EWD_FWC_LETTER_WIDTH+(col-start_col)*EWD_FWC_LETTER_WIDTH+5,y+y_offset+4,ewd_fwc_color(color),2);
            if(c!=0&&framed&&ewd_fwc_has_line(color)) {
                float fx=x+start_col*EWD_FWC_LETTER_WIDTH,fy=y+y_offset-22,fw=(col-start_col)*EWD_FWC_LETTER_WIDTH+12;
                Color fc=ewd_fwc_color(color);
                ewd_line(p,fx,fy,fx+fw,fy,fc,2);ewd_line(p,fx+fw,fy,fx+fw,fy+27,fc,2);
                ewd_line(p,fx+fw,fy+27,fx,fy+27,fc,2);ewd_line(p,fx,fy+27,fx,fy,fc,2);
            }
            n=0;start_col=col;
        }
        if(c==0) break;
        if(c=='\x1b') {
            /* Longest control sequences are 3 characters ("<7m"); FBW stops at the first match. */
            char ctrl[4];int k=0,matched=0;
            while(i+1<length&&k<3) {
                ctrl[k++]=message[++i];ctrl[k]=0;
                if(!strcmp(ctrl,"m")) { underlined=framed=0;matched=1; }
                else if(!strcmp(ctrl,"4m")) { underlined=1;matched=1; }
                else if(!strcmp(ctrl,")m")) matched=1; /* flashing: not animated (FBW TODO) */
                else if(!strcmp(ctrl,"'m")) { framed=1;matched=1; }
                else if(ctrl[0]=='<'&&k==3&&ctrl[2]=='m'&&ctrl[1]>='1'&&ctrl[1]<='7') {
                    static const int map[]={EWD_FWC_YELLOW,EWD_FWC_RED,EWD_FWC_GREEN,EWD_FWC_AMBER,EWD_FWC_CYAN,EWD_FWC_MAGENTA,EWD_FWC_WHITE};
                    color=map[ctrl[1]-'1'];matched=1;
                }
                if(matched) break;
            }
            continue;
        }
        if(c=='\r') {
            y_offset+=EWD_FWC_LINE_SPACING;++lines; /* FBW appends the <text> and advances */
            line_has_text=0;col=start_col=0;cursor_x=x;
            continue;
        }
        if(n+1<sizeof(buffer)) buffer[n++]=c;
        ++col;
    }
    if(line_has_text) ++lines;
    return lines;
}

/* Memo / limitation column: WdMemos and WdLimitations join the entries with '\r'. */
static void ewd_fwc_column(const EwdCanvas *p,float x,float y,const char *const *entries,int count)
{
    char joined[EWD_WD_MEMO_LINES*FWS_TEXT_MAX];
    size_t used=0;int i;
    joined[0]=0;
    for(i=0;i<count;++i) {
        size_t len;
        if(!entries[i]) continue;
        len=strlen(entries[i]);
        if(used+len+2>sizeof(joined)) break;
        if(used) joined[used++]='\r';
        memcpy(joined+used,entries[i],len);used+=len;joined[used]=0;
    }
    if(used) ewd_fwc_text(p,x,y,joined);
}

static void ewd_wd_box(const EwdCanvas *p,float x,float y,float w,float h,Color c,float width)
{
    ewd_line(p,x,y,x+w,y,c,width);ewd_line(p,x+w,y,x+w,y+h,c,width);
    ewd_line(p,x+w,y+h,x,y+h,c,width);ewd_line(p,x,y+h,x,y,c,width);
}

/* .LimitationsHeading: 24 px, letter-spacing 1.75 px, centred; heading line box ~28 px. */
#define EWD_WD_HEADING_H 28.0f
static void ewd_wd_heading(const EwdCanvas *p,float cx,float y_top,const char *text,int underline)
{
    float w=ewd_font_width(text,24,1.75f),baseline=y_top+21;
    ewd_font_draw(p->x+cx,p->y+1024-baseline,text,24,ewd_dim(EWD_WHITE),1.75f,1);
    if(underline) ewd_line(p,cx-w/2,baseline+3,cx+w/2,baseline+3,EWD_WHITE,2);
}

/* WdLimitations + WdMemos; returns nothing, draws from the WD top (y 383). */
static void ewd_wd_memos(const EwdCanvas *p,const EwdWdView *v)
{
    float y=383;
    const Color divider={211.0f/255.0f,211.0f/255.0f,211.0f/255.0f}; /* "lightgrey" */
    int limit_lines=v->limit_all_count>v->limit_appr_ldg_count?v->limit_all_count:v->limit_appr_ldg_count;
    int memo_lines=v->memo_left_count>v->memo_right_count?v->memo_left_count:v->memo_right_count;
    if(limit_lines>EWD_WD_LIMIT_LINES) limit_lines=EWD_WD_LIMIT_LINES;
    if(memo_lines>EWD_WD_MEMO_LINES) memo_lines=EWD_WD_MEMO_LINES;
    if(limit_lines>0) {
        float column_top,column_h=EWD_WD_HEADING_H+4+limit_lines*EWD_FWC_LINE_SPACING+3;
        y+=12;
        ewd_wd_heading(p,384,y,"LIMITATIONS",1);
        y+=EWD_WD_HEADING_H+4;column_top=y;
        ewd_wd_heading(p,195,y,"ALL PHASES",0);
        ewd_wd_heading(p,576,y,"APPR & LDG",0);
        y+=EWD_WD_HEADING_H+4;
        ewd_fwc_column(p,8,y+24,v->limit_all,v->limit_all_count<EWD_WD_LIMIT_LINES?v->limit_all_count:EWD_WD_LIMIT_LINES);
        ewd_fwc_column(p,392,y+24,v->limit_appr_ldg,v->limit_appr_ldg_count<EWD_WD_LIMIT_LINES?v->limit_appr_ldg_count:EWD_WD_LIMIT_LINES);
        ewd_rect(p,383,column_top,2,column_h,divider);
        y=column_top+column_h+4+12;
        ewd_rect(p,8,y,752,2,EWD_LIGHT_GREY);
        y+=2;
    }
    y+=12;
    ewd_rect(p,383,y,2,memo_lines*EWD_FWC_LINE_SPACING+3,divider);
    ewd_fwc_column(p,8,y+24,v->memo_left,v->memo_left_count<EWD_WD_MEMO_LINES?v->memo_left_count:EWD_WD_MEMO_LINES);
    ewd_fwc_column(p,392,y+24,v->memo_right,v->memo_right_count<EWD_WD_MEMO_LINES?v->memo_right_count:EWD_WD_MEMO_LINES);
}

/* STS area (40 px): inline boxes, 24 px text, 2 px padding, 2 px white outset border. */
static void ewd_wd_sts(const EwdCanvas *p,const EwdWdView *v,float top)
{
    const char *labels[3]={"FAILURE PENDING",v->sts_deferred?"STS & DEFRD PROC":"STS","ADV"};
    const int shown[3]={v->failure_pending,v->sts,v->adv};
    static const float shift[3]={40,150,360};
    float flow=8;int i;
    for(i=0;i<3;++i) {
        float w=ewd_font_width(labels[i],24,0)+4+4,x=flow+shift[i];
        if(shown[i]) {
            ewd_wd_box(p,x+1,top+4+1,w-2,32,EWD_WHITE,2);
            ewd_font_draw(p->x+x+4,p->y+1024-(top+4+27),labels[i],24,ewd_dim(EWD_WHITE),0,0);
        }
        flow+=w; /* visibility:hidden keeps its inline box */
    }
}

/* .EclLine colour from the style.scss cascade (2-class rules in order, 3-class, then !important). */
static Color ewd_ecl_color(const FwsWdLine *l)
{
    int s=l->style,checklist_item=!l->abnormal_procedure&&s==FWS_STYLE_CHECKLIST_ITEM;
    Color c=EWD_CYAN;
    if(s==FWS_STYLE_DEFERRED_PROCEDURE) c=EWD_AMBER;
    if(s==FWS_STYLE_COMPLETED_DEFERRED_PROCEDURE) c=EWD_WHITE;
    if(s==FWS_STYLE_GREEN||(s==FWS_STYLE_GREEN_TABLE&&l->active_procedure&&!l->inactive)) c=EWD_GREEN;
    if(s==FWS_STYLE_CYAN) c=EWD_CYAN;
    if(s==FWS_STYLE_AMBER) c=EWD_AMBER;
    if(s==FWS_STYLE_WHITE) c=EWD_WHITE;
    if(s==FWS_STYLE_RED) c=EWD_RED;
    if(s==FWS_STYLE_HEADLINE||s==FWS_STYLE_SUB_HEADLINE||s==FWS_STYLE_CENTERED_SUB_HEADLINE) c=EWD_WHITE;
    if(s==FWS_STYLE_CHECKLIST_CONDITION) c=EWD_WHITE;
    if(s==FWS_STYLE_OMISSION_DOTS) c=EWD_WHITE;
    if(s==FWS_STYLE_LAND_ANSA) c=EWD_AMBER;
    if(s==FWS_STYLE_LAND_ASAP) c=EWD_RED;
    if(checklist_item&&l->checked) c=EWD_GREEN;
    if(l->abnormal_procedure&&l->checked) c=EWD_WHITE;
    if(l->inactive||s==FWS_STYLE_COMPLETED_CHECKLIST) c=EWD_GREY;
    return c;
}

/* ■ / □ in the 26 px checkbox column (24 px wide). */
static void ewd_ecl_checkbox(const EwdCanvas *p,float cx,float cy,int checked,Color c)
{
    const float half=7;
    if(checked) ewd_rect(p,cx-half,cy-half,2*half,2*half,c);
    else ewd_wd_box(p,cx-half,cy-half,2*half,2*half,c,2);
}

static void ewd_ecl_arrow(const EwdCanvas *p,float x,float y,int up)
{
    /* OverflowArrowSymbol: 30 px wide (viewBox 36 x 40), green stem + head; up = rotated 180. */
    const float s=30.0f/36.0f;int i;
    float sx[4]={13.25f,22.78f,22.78f,13.25f},sy[4]={1.62f,1.62f,19.05f,19.05f};
    float hx[3]={0.14f,17.82f,35.85f},hy[3]={14.21f,38.23f,14.17f};
    glColor3f(EWD_GREEN.r*ewd_brightness,EWD_GREEN.g*ewd_brightness,EWD_GREEN.b*ewd_brightness);
    glBegin(GL_TRIANGLE_FAN);
    for(i=0;i<4;++i) { float px=up?36-sx[i]:sx[i],py=up?40-sy[i]:sy[i];glVertex2f(p->x+x+px*s,p->y+1024-(y+py*s)); }
    glEnd();
    glBegin(GL_TRIANGLE_FAN);
    for(i=0;i<3;++i) { float px=up?36-hx[i]:hx[i],py=up?40-hy[i]:hy[i];glVertex2f(p->x+x+px*s,p->y+1024-(y+py*s)); }
    glEnd();
}

#define EWD_ECL_TOP 395.0f       /* 383 + WarningsColumn padding 12 */
#define EWD_ECL_LINE_H 34.0f
#define EWD_ECL_LEFT 48.0f       /* 8 margin + 30 overflow column + 10 EclLine margin */
#define EWD_ECL_RIGHT 737.0f     /* end marker (28 px, margin-left -5) starts at 732 */

static void ewd_ecl_line(const EwdCanvas *p,const FwsWdLine *l,float top,int selected)
{
    int s=l->style;
    Color c=ewd_ecl_color(l);
    char text[FWS_TEXT_MAX];
    int headline_abn=l->abnormal_procedure&&s==FWS_STYLE_HEADLINE;
    int checkbox_hidden=s==FWS_STYLE_HEADLINE||s==FWS_STYLE_GREEN_TABLE||s==FWS_STYLE_OMISSION_DOTS||
                        s==FWS_STYLE_LAND_ANSA||s==FWS_STYLE_LAND_ASAP;
    int checkbox_invisible=l->sensed||(l->first_line&&l->last_line)||l->special==FWS_SPECIAL_EMPTY;
    float baseline=top+26,x=EWD_ECL_LEFT+3;
    /* End marker bracket. */
    if(!((l->first_line&&l->last_line)||l->special==FWS_SPECIAL_EMPTY)) {
        if(!selected) ewd_rect(p,732,top,3,EWD_ECL_LINE_H,EWD_WHITE);
        ewd_rect(p,757,top,3,EWD_ECL_LINE_H,EWD_WHITE);
        if(l->first_line) ewd_rect(p,732,top,28,3,EWD_WHITE);
        if(l->last_line) ewd_rect(p,732,top+EWD_ECL_LINE_H-3,28,3,EWD_WHITE);
    }
    if(s==FWS_STYLE_SEPARATION_LINE) { ewd_rect(p,38,top+14,689,2,EWD_WHITE);return; }
    if(selected) { /* 3 px cyan border, right side transparent */
        ewd_rect(p,EWD_ECL_LEFT,top,EWD_ECL_RIGHT-EWD_ECL_LEFT,3,EWD_CYAN);
        ewd_rect(p,EWD_ECL_LEFT,top+EWD_ECL_LINE_H-3,EWD_ECL_RIGHT-EWD_ECL_LEFT,3,EWD_CYAN);
        ewd_rect(p,EWD_ECL_LEFT,top,3,EWD_ECL_LINE_H,EWD_CYAN);
    }
    if(!checkbox_hidden) {
        if(!checkbox_invisible) {
            Color box=c;
            if(l->checked) box=l->abnormal_procedure?EWD_WHITE:EWD_GREEN;
            if(s==FWS_STYLE_CHECKLIST_CONDITION) box=EWD_WHITE;
            if(l->abnormal_procedure&&l->inactive) box=EWD_GREY;
            ewd_ecl_checkbox(p,x+12,top+EWD_ECL_LINE_H/2,l->checked,box);
        }
        x+=24+20;
    }
    if(headline_abn) { ewd_fwc_text(p,x+10,top+0.5f+24,l->text);return; }
    {   /* d.text.substring(0, 39), plain 26 px */
        char raw[FWS_TEXT_MAX];
        size_t len=strlen(l->text);
        if(len>39) len=39;
        memcpy(raw,l->text,len);raw[len]=0;
        /* strip escapes (EclLineText shows raw text; FBW never puts codes there except headlines) */
        {
            size_t i,n=0;
            for(i=0;i<len;++i) {
                if(raw[i]=='\x1b') { while(i+1<len&&raw[i+1]!='m') ++i;++i;continue; }
                text[n++]=raw[i];
            }
            text[n]=0;
            {char plain[FWS_TEXT_MAX];ewd_wd_plain(plain,sizeof(plain),text,n);strcpy(text,plain);}
        }
    }
    if(s==FWS_STYLE_GREEN_TABLE) {
        char left[FWS_TEXT_MAX],right[FWS_TEXT_MAX];
        const char *bar=strchr(text,'|');
        float width=EWD_ECL_RIGHT-3-x,split=x+width*1.5f/2.5f;
        if(bar) { size_t k=(size_t)(bar-text);memcpy(left,text,k);left[k]=0;strcpy(right,bar+1); }
        else { strcpy(left,text);right[0]=0; }
        ewd_font_draw(p->x+x,p->y+1024-baseline,left,26,ewd_dim(c),0,0);
        ewd_rect(p,split-8,top+3,2,EWD_ECL_LINE_H-6,c);
        if(text[0]=='|') ewd_font_draw(p->x+split,p->y+1024-baseline,right,26,ewd_dim(c),0,0);
        else ewd_font_draw(p->x+(split+EWD_ECL_RIGHT-3)/2,p->y+1024-baseline,right,26,ewd_dim(c),0,1);
        ewd_rect(p,EWD_ECL_LEFT,top+EWD_ECL_LINE_H-3-2,EWD_ECL_RIGHT-EWD_ECL_LEFT,2,c);
        return;
    }
    if(s==FWS_STYLE_LAND_ANSA||s==FWS_STYLE_LAND_ASAP)
        ewd_font_draw(p->x+EWD_ECL_RIGHT-3,p->y+1024-baseline,text,26,ewd_dim(c),0,2);
    else if(s==FWS_STYLE_CENTERED_SUB_HEADLINE)
        ewd_font_draw(p->x+(EWD_ECL_LEFT+EWD_ECL_RIGHT)/2,p->y+1024-baseline,text,26,ewd_dim(c),0,1);
    else ewd_font_draw(p->x+x,p->y+1024-baseline,text,26,ewd_dim(c),0,0);
    if(s==FWS_STYLE_HEADLINE) { /* text-decoration: underline */
        float w=ewd_font_width(text,26,0);
        ewd_line(p,x,baseline+3,x+w,baseline+3,c,2);
    }
    if(s==FWS_STYLE_SUB_HEADLINE||s==FWS_STYLE_CENTERED_SUB_HEADLINE) /* border-bottom 2px currentColor */
        ewd_rect(p,EWD_ECL_LEFT,top+EWD_ECL_LINE_H-3-2,EWD_ECL_RIGHT-EWD_ECL_LEFT,2,c);
}

static void ewd_wd_procedures(const EwdCanvas *p,const EwdWdView *v)
{
    int i;
    for(i=0;i<FWS_WD_NUM_LINES;++i) ewd_ecl_line(p,&v->line[i],EWD_ECL_TOP+i*EWD_ECL_LINE_H,i==v->selected_line);
    if(v->overflow_up) ewd_ecl_arrow(p,8,383+50,1);
    if(v->overflow_down) ewd_ecl_arrow(p,8,383+639-12-33.3f,0);
}

static void ewd_wd_draw(const EwdCanvas *p,const EwdWdView *v)
{
    if(v->mode==EWD_WD_PROCEDURES) ewd_wd_procedures(p,v);
    else ewd_wd_memos(p,v);
    if(v->sts_area_visible) {
        /* Memos fill the area (flex 1); after the 18-line ECL the 40 px area starts at 1022. */
        ewd_wd_sts(p,v,v->mode==EWD_WD_PROCEDURES?383+639:1024-40);
    }
}
