/* SPDX-License-Identifier: GPL-3.0-or-later
 * Three live clock apertures in the cockpit atlas: CHR above UTC above ET.
 * The authored face already supplies all printed legends and button labels.
 */
static void clock_digit(float x,float y,int digit,float width,float height,Color color)
{
    static const unsigned char segments[10]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    float t=height*0.10f;
    unsigned bits;
    if (digit<0 || digit>9) return;
    bits=segments[digit];
    if (bits&1) rectangle(x+t,y+height-t,x+width-t,y+height,color);
    if (bits&2) rectangle(x+width-t,y+height/2+t/2,x+width,y+height-t,color);
    if (bits&4) rectangle(x+width-t,y+t,x+width,y+height/2-t/2,color);
    if (bits&8) rectangle(x+t,y,x+width-t,y+t,color);
    if (bits&16) rectangle(x,y+t,x+t,y+height/2-t/2,color);
    if (bits&32) rectangle(x,y+height/2+t/2,x+t,y+height-t,color);
    if (bits&64) rectangle(x+t,y+height/2-t/2,x+width-t,y+height/2+t/2,color);
}
static void clock_pair(float x,float y,int value,float width,float height,int hidden,Color color)
{
    if (hidden) return;
    clock_digit(x,y,(value/10)%10,width,height,color);
    clock_digit(x+width+2,y,value%10,width,height,color);
}
static void clock_colon(float x,float y,float height,Color color)
{
    rectangle(x,y+height*.25f,x+2,y+height*.25f+2,color);
    rectangle(x,y+height*.70f,x+2,y+height*.70f+2,color);
}
static void draw_clock(void)
{
    const Color digits={0.86f,0.87f,0.85f};
    int date, flash, left, middle, right;
    /* CLOCK_CHR=(332,104,420,132), UTC=(332,72,420,100), ET=(332,40,420,68). */
    rectangle(332,40,420,132,BLACK);
    if (!clock_state.powered) return;
    if (clock_state.chr_display) {
        clock_pair(350,108,clock_state.chr_seconds/60,10,20,0,digits);
        clock_colon(375,108,20,digits);
        clock_pair(380,108,clock_state.chr_seconds%60,10,20,0,digits);
    }
    date=clock_state.date_held || (clock_state.mode==2 && clock_state.set_field>=3);
    flash=clock_state.mode==2 && clock_state.flash_time>=0.5f;
    left=date ? clock_state.day : clock_state.hour;
    middle=date ? clock_state.month : clock_state.minute;
    right=date ? clock_state.year%100 : clock_state.second;
    clock_pair(335,76,left,11,20,flash && clock_state.set_field==(date?3:0),digits);
    if (!date) clock_colon(361,76,20,digits);
    clock_pair(366,76,middle,11,20,flash && clock_state.set_field==(date?4:1),digits);
    clock_pair(396,78,right,8,15,flash && clock_state.set_field==(date?5:2),digits);
    if (clock_state.et_display && clock_state.et_selector!=2) {
        clock_pair(350,44,clock_state.et_seconds/3600,10,20,0,digits);
        clock_colon(375,44,20,digits);
        clock_pair(380,44,(clock_state.et_seconds/60)%60,10,20,0,digits);
    }
}
