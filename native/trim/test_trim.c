#include "trim.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static TrimInputs inputs(void)
{
    TrimInputs in={0};
    in.pitch_up_deg=in.pitch_down_deg=10;
    in.rudder_left_deg=in.rudder_right_deg=30;
    in.pitch_time=25; in.ground=1; in.powered=1;
    in.pitch_available=in.rudder_available=1;
    in.ias=200; in.radio_height_ft=1000; in.nz=1;
    in.computer_active=1;
    return in;
}
static void tick(TrimState *s,TrimInputs *in,double dt)
{
    trim_step(s,in,dt);
    if(s->write_pitch)in->pitch=s->pitch;
    if(s->write_rudder)in->rudder=s->rudder;
}
int main(void)
{
    TrimState s; TrimInputs in=inputs(); char label[8]; int n;
    trim_init(&s);
    trim_command(&s,TRIM_PITCH_UP,1,0);trim_command(&s,TRIM_PITCH_UP,1,2);
    tick(&s,&in,.01);assert(in.pitch>0);in.pitch=0;trim_init(&s);
    trim_command(&s,TRIM_PITCH_UP,1,0);
    for(n=0;n<100;n++){trim_command(&s,TRIM_PITCH_UP,1,1);tick(&s,&in,.01);}
    assert(fabs(in.pitch-.08)<1e-9 && s.pitch_switch==1);
    trim_command(&s,TRIM_PITCH_UP,1,2);
    for(n=0;n<10;n++)tick(&s,&in,.01);
    assert(fabs(in.pitch-.08)<1e-9 && s.pitch_switch==.5);
    /* Two hardware sources do not double the rate or release one another. */
    trim_command(&s,TRIM_PITCH_DOWN,1,0);trim_command(&s,TRIM_PITCH_DOWN,2,0);
    trim_command(&s,TRIM_PITCH_DOWN,1,2);tick(&s,&in,.1);assert(in.pitch<.08);
    trim_release(&s);
    in.ground=0; in.law=TRIM_NORMAL; in.elevator_deg=0;
    trim_command(&s,TRIM_PITCH_UP,1,0);tick(&s,&in,.1);assert(!s.write_pitch);
    in.law=TRIM_ALTERNATE;tick(&s,&in,.1);assert(!s.write_pitch);
    in.law=TRIM_DIRECT;tick(&s,&in,.1);assert(s.write_pitch && s.manual_pitch);
    trim_release(&s);in.law=TRIM_NORMAL;in.pitch=0;in.elevator_deg=-10;
    for(n=0;n<500;n++)tick(&s,&in,.01);
    assert(in.pitch>0 && in.pitch<=.075 && s.auto_pitch);
    in.computer_active=0;tick(&s,&in,.1);assert(!s.write_pitch && !s.auto_pitch);
    in.computer_active=1;
    in.bank_deg=40;tick(&s,&in,.1);assert(!s.write_pitch);
    in.bank_deg=0;in.native_autopilot=1;tick(&s,&in,.1);assert(!s.write_pitch);
    in.native_autopilot=0;in.external_surfaces=1;tick(&s,&in,.1);assert(!s.write_pitch);
    in.external_surfaces=0;in.ground=1;in.pitch=.9999;
    trim_command(&s,TRIM_PITCH_UP,1,0);tick(&s,&in,.1);assert(in.pitch==1);
    in.powered=0;tick(&s,&in,.1);assert(!s.write_pitch && !s.write_rudder);
    in.powered=1;in.pitch_available=0;tick(&s,&in,.1);assert(!s.write_pitch);
    in.pitch_available=1;in.pitch=NAN;tick(&s,&in,.1);assert(!s.write_pitch);
    trim_release(&s);in=inputs();trim_command(&s,TRIM_RUDDER_LEFT,1,0);
    for(n=0;n<100;n++)tick(&s,&in,.01);
    assert(fabs(trim_rudder_degrees(&in)+1)<1e-8 && s.rudder_knob==0);
    trim_command(&s,TRIM_RUDDER_LEFT,1,2);tick(&s,&in,.1);assert(s.rudder_knob==.5);
    trim_command(&s,TRIM_RUDDER_RESET,1,0);tick(&s,&in,.01);
    trim_command(&s,TRIM_RUDDER_RESET,1,2);
    for(n=0;n<200;n++)tick(&s,&in,.01);
    assert(in.rudder==0 && !s.resetting);
    in.rudder=.5;trim_command(&s,TRIM_RUDDER_RESET,1,0);tick(&s,&in,.1);
    trim_command(&s,TRIM_RUDDER_RIGHT,1,0);tick(&s,&in,.1);assert(!s.resetting);
    in.rudder=1;for(n=0;n<500;n++)tick(&s,&in,.01);
    assert(fabs(trim_rudder_degrees(&in)-20)<1e-8);
    trim_format_rudder(-1.25,label);assert(!strcmp(label,"L 1.2"));
    trim_format_rudder(2.4,label);assert(!strcmp(label,"R 2.4"));
    trim_format_rudder(.001,label);assert(!strcmp(label,"L 0.0"));
    trim_format_rudder(NAN,label);assert(!strcmp(label,"---"));
    in.suspended=1;tick(&s,&in,.1);assert(!s.write_rudder && !s.held[TRIM_RUDDER_RIGHT]);
    puts("PASS: trim authority, direction, rates, release, limits, reset, display, pause and failure gates");
    return 0;
}
