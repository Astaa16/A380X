/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef A380X_FCU_MOTION_H
#define A380X_FCU_MOTION_H
#include <math.h>
#include <string.h>
#include "a380x_guidance.h"

enum { A380X_FCU_MOTION_BUTTONS=11, A380X_FCU_MOTION_KNOBS=4 };
typedef struct {
    float button[A380X_FCU_MOTION_BUTTONS], rotation[A380X_FCU_MOTION_KNOBS];
    float axial[A380X_FCU_MOTION_KNOBS], selector;
    float target[15], release_at[15], last_event[15];
    float last_update;
    unsigned char held[15], active_command[15];
    unsigned char clock_valid;
} A380XFcuMotion;

static inline int a380x_fcu_motion_button(A380XFcuCommand command)
{
    switch(command) {
    case A380X_FCU_AP1:return 0; case A380X_FCU_AP2:return 1;
    case A380X_FCU_FD1:return 2; case A380X_FCU_ATHR:return 3;
    case A380X_FCU_LOC:return 4; case A380X_FCU_ALT_HOLD:return 5;
    case A380X_FCU_APPR:return 6; case A380X_FCU_SPD_MACH:return 7;
    case A380X_FCU_TRUE_MAG:return 8; case A380X_FCU_TRK_FPA:return 9;
    case A380X_FCU_METRIC_ALT:return 10; default:return -1;
    }
}
static inline int a380x_fcu_motion_knob(A380XFcuCommand command)
{
    switch(command) {
    case A380X_FCU_SPD_ROTATE:case A380X_FCU_SPD_PUSH:case A380X_FCU_SPD_PULL:return 0;
    case A380X_FCU_HDG_ROTATE:case A380X_FCU_HDG_PUSH:case A380X_FCU_HDG_PULL:return 1;
    case A380X_FCU_ALT_ROTATE:case A380X_FCU_ALT_PUSH:case A380X_FCU_ALT_PULL:return 2;
    case A380X_FCU_VS_ROTATE:case A380X_FCU_VS_PUSH:case A380X_FCU_VS_PULL:return 3;
    default:return -1;
    }
}
static inline int a380x_fcu_motion_rotary(A380XFcuCommand command)
{
    return command==A380X_FCU_SPD_ROTATE || command==A380X_FCU_HDG_ROTATE ||
        command==A380X_FCU_ALT_ROTATE || command==A380X_FCU_VS_ROTATE;
}

/* Command phases only control mechanical depression, never the latched mode.
 * A short command-once click remains visible for 100 ms. Continue refreshes
 * held geometry without firing another logical button action. */
static inline void a380x_fcu_motion_phase(A380XFcuMotion *s,A380XFcuCommand command,int phase,float now)
{
    int index=a380x_fcu_motion_button(command), knob=a380x_fcu_motion_knob(command);
    float direction=1;
    if(index<0) {
        if(knob<0 || a380x_fcu_motion_rotary(command)) return;
        index=A380X_FCU_MOTION_BUTTONS+knob;
        if(command==A380X_FCU_SPD_PULL || command==A380X_FCU_HDG_PULL ||
           command==A380X_FCU_ALT_PULL || command==A380X_FCU_VS_PULL) direction=-1;
    }
    if(phase!=0 && s->active_command[index]!=(unsigned char)(command+1)) return;
    s->last_event[index]=now;
    if(phase==2) {s->held[index]=0;return;}
    if(phase==0) {s->release_at[index]=now+.1f;s->active_command[index]=(unsigned char)(command+1);}
    s->held[index]=1;s->target[index]=direction;
}
/* Applied rotary detents follow the same repeat/debounce path as guidance. */
static inline void a380x_fcu_motion_applied(A380XFcuMotion *s,A380XFcuCommand command,double amount)
{
    int knob=a380x_fcu_motion_knob(command);
    if(knob<0 || !a380x_fcu_motion_rotary(command) || !isfinite(amount)) return;
    /* The retained FBW FCU templates use COUNT=36 per revolution. Their V/S
     * wheel increases counterclockwise (CLOCKWISE_DIR=DEC), unlike the other
     * three selectors. This affects mesh feedback, never guidance increments. */
    s->rotation[knob]=fmodf(fmodf(s->rotation[knob]+(float)amount*(knob==3 ? -10.f : 10.f),360.f)+360.f,360.f);
}
static inline float a380x_fcu_motion_approach(float value,float target,float step)
{
    return value<target ? fminf(target,value+step) : fmaxf(target,value-step);
}
static inline void a380x_fcu_motion_update(A380XFcuMotion *s,float dt,float now,int selector_1000)
{
    int i;
    /* Mechanical motion uses wall time even while the simulation is paused;
     * the SDK's "last loop" interval may belong to another plugin callback. */
    if(s->clock_valid && isfinite(now) && now>=s->last_update) dt=now-s->last_update;
    if(isfinite(now)) {s->last_update=now;s->clock_valid=1;}
    if(!isfinite(dt) || dt<0) dt=0;
    dt=fminf(dt,.1f);
    for(i=0;i<15;++i) {
        float *position=i<A380X_FCU_MOTION_BUTTONS ? &s->button[i] : &s->axial[i-A380X_FCU_MOTION_BUTTONS];
        /* A missing release cannot leave a control stuck after lost focus or
         * a disconnected binding. Normal held commands refresh Continue. */
        if(now-s->last_event[i]>1.f) s->held[i]=0;
        if(!s->held[i] && now>=s->release_at[i]) s->target[i]=0;
        *position=a380x_fcu_motion_approach(*position,s->target[i],dt*20.f);
    }
    s->selector=a380x_fcu_motion_approach(s->selector,selector_1000 ? 1.f : 0.f,dt*12.f);
}
#endif
