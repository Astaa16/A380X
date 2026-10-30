/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "a380x_fcu_motion.h"
int main(void)
{
    A380XFcuMotion s;int i;memset(&s,0,sizeof(s));
    /* Representative AP1: hold, command-once pulse, spring return; no latch. */
    a380x_fcu_motion_phase(&s,A380X_FCU_AP1,0,0);
    a380x_fcu_motion_update(&s,.05f,.05f,0);assert(s.button[0]==1);
    a380x_fcu_motion_phase(&s,A380X_FCU_AP1,1,.5f);
    a380x_fcu_motion_update(&s,.05f,.55f,0);assert(s.button[0]==1);
    a380x_fcu_motion_phase(&s,A380X_FCU_AP1,2,.56f);
    a380x_fcu_motion_update(&s,.05f,.61f,0);assert(s.button[0]==0);
    a380x_fcu_motion_phase(&s,A380X_FCU_AP1,0,1);
    a380x_fcu_motion_phase(&s,A380X_FCU_AP1,2,1);
    a380x_fcu_motion_update(&s,.05f,1.05f,0);assert(s.button[0]==1);
    a380x_fcu_motion_update(&s,.05f,1.11f,0);assert(s.button[0]==0);
    /* Representative ALT: physical rotation is one detent per applied event,
     * push and pull move in opposite directions, step ring follows its state. */
    a380x_fcu_motion_applied(&s,A380X_FCU_ALT_ROTATE,1);assert(s.rotation[2]==10);
    a380x_fcu_motion_applied(&s,A380X_FCU_ALT_ROTATE,-2);assert(s.rotation[2]==350);
    a380x_fcu_motion_applied(&s,A380X_FCU_VS_ROTATE,1);assert(s.rotation[3]==350);
    a380x_fcu_motion_phase(&s,A380X_FCU_ALT_PUSH,0,2);
    a380x_fcu_motion_update(&s,.05f,2.05f,1);assert(s.axial[2]==1);
    a380x_fcu_motion_phase(&s,A380X_FCU_ALT_PUSH,2,2.06f);
    a380x_fcu_motion_update(&s,.05f,2.2f,1);assert(s.axial[2]==0 && s.selector==1);
    a380x_fcu_motion_phase(&s,A380X_FCU_ALT_PULL,0,3);
    a380x_fcu_motion_update(&s,.05f,3.05f,0);assert(s.axial[2]==-1);
    a380x_fcu_motion_update(&s,.05f,4.1f,0);assert(s.axial[2]==0);
    a380x_fcu_motion_phase(&s,A380X_FCU_ALT_PUSH,0,5);
    a380x_fcu_motion_phase(&s,A380X_FCU_ALT_PULL,0,5.01f);
    a380x_fcu_motion_phase(&s,A380X_FCU_ALT_PUSH,2,5.02f);
    a380x_fcu_motion_update(&s,.05f,5.15f,0);assert(s.axial[2]==-1 && s.held[13]);
    a380x_fcu_motion_phase(&s,A380X_FCU_ALT_PULL,2,5.16f);
    a380x_fcu_motion_update(&s,.05f,5.22f,0);assert(s.axial[2]==0);
    for(i=0;i<A380X_FCU_MOTION_BUTTONS;++i)assert(isfinite(s.button[i]));
    {
        static const A380XFcuCommand commands[]={A380X_FCU_AP1,A380X_FCU_AP2,A380X_FCU_FD1,A380X_FCU_ATHR,
            A380X_FCU_LOC,A380X_FCU_ALT_HOLD,A380X_FCU_APPR,A380X_FCU_SPD_MACH,A380X_FCU_TRUE_MAG,
            A380X_FCU_TRK_FPA,A380X_FCU_METRIC_ALT};
        for(i=0;i<A380X_FCU_MOTION_BUTTONS;++i) {
            int j;memset(&s,0,sizeof(s));
            a380x_fcu_motion_phase(&s,commands[i],0,0);
            a380x_fcu_motion_update(&s,.05f,.05f,0);
            for(j=0;j<A380X_FCU_MOTION_BUTTONS;++j)assert(s.button[j]==(j==i ? 1.f : 0.f));
            a380x_fcu_motion_phase(&s,commands[i],2,.06f);
            a380x_fcu_motion_update(&s,.05f,.2f,0);assert(s.button[i]==0);
        }
    }
    puts("FCU physical AP1 and ALT: press/hold/release, one-shot pulse, rotary detents, signed axial travel and selector passed.");
    return 0;
}
