/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native FCU glass. Windows follow the authored 4x256 by 40 pixel atlas.
 * State/dashes come from the same controller used by PFD/FD and AP servos.
 */
static float fcu_integral_level = 1.0f, fcu_lcd_level = 1.0f;
static Color fcu_tint(Color color, float level)
{
    Color result = {color.r * level, color.g * level, color.b * level};
    return result;
}
static void fcu_digit(float x,float y,char digit,Color color)
{
    static const unsigned char segments[10]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    unsigned bits=(digit>='0'&&digit<='9') ? segments[digit-'0'] : digit=='-' ? 0x40 : 0;
    /* Each atlas quarter maps to half of an AFS window. Compensate for its
     * horizontal UV compression so seven-segment glyphs retain their shape. */
    const float w=32,h=24,t=3;
    if (bits&1) rectangle(x+t,y+h-t,x+w-t,y+h,color);
    if (bits&2) rectangle(x+w-t,y+h/2+t/2,x+w,y+h-t,color);
    if (bits&4) rectangle(x+w-t,y+t,x+w,y+h/2-t/2,color);
    if (bits&8) rectangle(x+t,y,x+w-t,y+t,color);
    if (bits&16) rectangle(x,y+t,x+t,y+h/2-t/2,color);
    if (bits&32) rectangle(x,y+h/2+t/2,x+t,y+h-t,color);
    if (bits&64) rectangle(x+t,y+h/2-t/2,x+w-t,y+h/2+t/2,color);
    if (digit=='+') {
        rectangle(x+4,y+11,x+28,y+13,color); rectangle(x+15,y+5,x+18,y+19,color);
    }
    if (digit=='.') rectangle(x+3,y,x+6,y+3,color);
}

static void fcu_number(float center,const char *digits,Color color)
{
    size_t i; float width=0,x;
    for (i=0;i<strlen(digits);++i) width+=digits[i]=='.'?14:40;
    x=center-width/2;
    for (i=0;i<strlen(digits);++i) { fcu_digit(x,475,digits[i],color); x+=digits[i]=='.'?14:40; }
}

static void fcu_label(float center,const char *label,Color color)
{
    float width=XPLMMeasureString(xplmFont_Proportional,label,(int)strlen(label));
    text_at(center-width/2,501,label,color);
}

static void fcu_managed_dot(float x,int lit,Color color)
{
    int i;
    if (!lit) return;
    set_color(color); glBegin(GL_TRIANGLE_FAN); glVertex2f(x,487);
    for(i=0;i<=24;++i) { float a=i*(float)(2*M_PI/24); glVertex2f(x+3*cosf(a),487+3*sinf(a)); }
    glEnd();
}

static void draw_fcu(void)
{
    char speed[12],heading[12],altitude[12],vertical[12];
    const Color screen_blue = {0.16f, 0.07f, 0.92f};
    Color integral = fcu_tint(WHITE, fcu_integral_level);
    Color lcd = fcu_tint(WHITE, fcu_lcd_level);
    rectangle(0,472,1024,512,BLACK);
    if (!guidance_inputs.powered) return;
    /* Adjacent quarters meet inside each physical window without a black seam. */
    rectangle(0,472,1024,512,fcu_tint(screen_blue,fcu_lcd_level));
    if (guidance.speed_dashes) snprintf(speed,sizeof(speed),"---");
    else if (guidance.mach_mode) snprintf(speed,sizeof(speed),"%.2f",guidance.selected_mach);
    else snprintf(speed,sizeof(speed),"%03d",(int)lround(guidance.selected_speed_knots));
    if (guidance.heading_dashes) snprintf(heading,sizeof(heading),"---");
    else snprintf(heading,sizeof(heading),"%03d",(int)lround(guidance.selected_heading_deg)%360);
    snprintf(altitude,sizeof(altitude),"%05d",(int)lround(guidance.selected_altitude_ft));
    if (guidance.vs_dashes) snprintf(vertical,sizeof(vertical),"-----");
    else if (guidance.trk_fpa_mode) snprintf(vertical,sizeof(vertical),"%+.1f",guidance.selected_fpa_deg);
    else snprintf(vertical,sizeof(vertical),"%+05d",(int)lround(guidance.selected_vs_fpm));
    fcu_label(128,guidance.mach_mode?"MACH":"SPD",integral);
    fcu_label(384,guidance.trk_fpa_mode?"TRK":"HDG",integral);
    fcu_label(640,"ALT",integral); fcu_label(896,guidance.trk_fpa_mode?"FPA":"V/S",integral);
    fcu_number(128,speed,lcd); fcu_number(384,heading,lcd);
    fcu_number(640,altitude,lcd); fcu_number(896,vertical,lcd);
    fcu_managed_dot(205,guidance.speed_managed,lcd); fcu_managed_dot(461,guidance.heading_managed,lcd);
    fcu_managed_dot(751,guidance.altitude_managed,lcd);
    if (!guidance.mach_mode && !guidance.speed_dashes) text_at(205,475,"KT",lcd);
    if (guidance.true_heading) text_at(267,501,"TRUE",integral);
}
