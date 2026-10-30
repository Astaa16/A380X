/* Production state-to-light mapping: no X-Plane process is used. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "a380x_fcu_lighting.h"
int main(void)
{
    A380XGuidanceState s; float v[FCU_LIGHT_COUNT];
    memset(&s,0,sizeof(s));
    s.ap1=s.fd1=s.athr_armed=s.loc_armed=s.approach_selected=s.guidance_valid=1;
    s.vertical_mode=A380X_B1_ALT;
    a380x_fcu_light_levels(&s,1,1,v);
    for(int i=0;i<FCU_LIGHT_COUNT;++i) assert(v[i]==(i==FCU_LIGHT_AP2?0:1));
    s.ap1=0;s.ap2=1;s.loc_armed=0;s.lateral_mode=A380X_C1_LOC_STAR;
    a380x_fcu_light_levels(&s,1,0,v);
    assert(v[FCU_LIGHT_AP1]==0&&v[FCU_LIGHT_AP2]==.25f&&v[FCU_LIGHT_LOC]==.25f);
    /* Armed A/THR has a lit bar before it commands thrust. */
    assert(!s.athr_active&&v[FCU_LIGHT_ATHR]==.25f);
    s.athr_armed=s.fd1=s.approach_selected=s.guidance_valid=0;
    s.lateral_mode=A380X_C1_HDG;
    a380x_fcu_light_levels(&s,1,.5f,v);
    assert(v[FCU_LIGHT_AP2]==.625f);
    for(int i=FCU_LIGHT_FD;i<FCU_LIGHT_COUNT;++i)assert(v[i]==0);
    a380x_fcu_light_levels(&s,0,1,v);
    for(int i=0;i<FCU_LIGHT_COUNT;++i)assert(v[i]==0);
    a380x_fcu_light_levels(&s,1,NAN,v);assert(v[FCU_LIGHT_AP2]==.25f);
    puts("FCU lights: power, armed/active, capture, disengagement and independent night dimming passed");
    return 0;
}
