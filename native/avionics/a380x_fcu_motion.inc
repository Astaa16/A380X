/* Read-only mechanical FCU animation, independent of guidance engagement. */
#include "a380x_fcu_motion.h"
static A380XFcuMotion fcu_motion;
typedef struct {const char *name;float *value;XPLMDataRef handle;} FcuMotionOutput;
#define FM_BUTTON(name,index) {"a380x/xp12/fcu/" name "_pressed",&fcu_motion.button[index],NULL}
#define FM_KNOB(name,index) {"a380x/xp12/fcu/" name "_knob_rotation",&fcu_motion.rotation[index],NULL}, \
    {"a380x/xp12/fcu/" name "_knob_axial",&fcu_motion.axial[index],NULL}
static FcuMotionOutput fcu_motion_outputs[]={
    FM_BUTTON("ap1",0),FM_BUTTON("ap2",1),FM_BUTTON("fd",2),FM_BUTTON("athr",3),
    FM_BUTTON("loc",4),FM_BUTTON("alt_button",5),FM_BUTTON("approach",6),
    FM_BUTTON("speed_mach",7),FM_BUTTON("true_mag",8),FM_BUTTON("trk_fpa",9),FM_BUTTON("metric",10),
    FM_KNOB("speed",0),FM_KNOB("heading",1),FM_KNOB("altitude",2),FM_KNOB("vertical_speed",3),
    {"a380x/xp12/fcu/altitude_selector",&fcu_motion.selector,NULL}
};
#undef FM_BUTTON
#undef FM_KNOB
static float fcu_motion_get(void *refcon) {return *((FcuMotionOutput *)refcon)->value;}
static void fcu_motion_shutdown(void)
{
    int i;
    for(i=0;i<ARRAY_LEN(fcu_motion_outputs);++i) if(fcu_motion_outputs[i].handle) {
        XPLMUnregisterDataAccessor(fcu_motion_outputs[i].handle);fcu_motion_outputs[i].handle=NULL;
    }
    memset(&fcu_motion,0,sizeof(fcu_motion));
}
static int fcu_motion_init(void)
{
    int i;memset(&fcu_motion,0,sizeof(fcu_motion));
    for(i=0;i<ARRAY_LEN(fcu_motion_outputs);++i) {
        FcuMotionOutput *o=&fcu_motion_outputs[i];
        if(XPLMFindDataRef(o->name)) {fcu_motion_shutdown();return 0;}
        o->handle=XPLMRegisterDataAccessor(o->name,xplmType_Float,0,NULL,NULL,fcu_motion_get,NULL,
            NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,o,NULL);
        if(!o->handle) {fcu_motion_shutdown();return 0;}
    }
    return 1;
}
