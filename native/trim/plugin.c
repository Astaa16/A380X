/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent aircraft-local trim panel. Does not modify/load the avionics DLL,
 * touch its panel atlas, or override flight-control surfaces / joystick axes.
 */
#include "trim.h"
#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"
#include "XPLMPlanes.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef A380X_TRIM_TEST
#include <OpenGL/gl.h>
#endif

#define PREFIX "a380x/xp12/trim/"
#define COUNT(a) ((int)(sizeof(a)/sizeof((a)[0])))
enum { PITCH, RUDDER, GROUND, PAUSED, REPLAY, BUS, ELECTRIC, PFAIL, RFAIL,
       UP_LIMIT, DN_LIMIT, YAW_LEFT, YAW_RIGHT, YAW_MIN, YAW_MAX, PITCH_TIME,
       ELEVATORS, BANK, NZ, IAS, AGL, FLAPS, EXTERNAL_SURFACES, NATIVE_AP, TIME_STEP, RAW_COUNT };
static const char *raw_names[]={
    "sim/cockpit2/controls/elevator_trim", "sim/cockpit2/controls/rudder_trim",
    "sim/flightmodel/failures/onground_any", "sim/time/paused", "sim/time/is_in_replay",
    "sim/cockpit2/electrical/bus_volts", "sim/cockpit2/autopilot/electric_trim_on",
    "sim/operation/failures/rel_trim_elv", "sim/operation/failures/rel_trim_rud",
    "sim/aircraft/controls/acf_hstb_trim_up", "sim/aircraft/controls/acf_hstb_trim_dn",
    "sim/aircraft/controls/acf_rudd_lr", "sim/aircraft/controls/acf_rudd_rr",
    "sim/aircraft/controls/acf_min_trim_rudd", "sim/aircraft/controls/acf_max_trim_rudd",
    "sim/aircraft/controls/acf_elev_trim_time", "sim/flightmodel2/wing/elevator1_deg",
    "sim/flightmodel/position/phi", "sim/flightmodel/forces/g_nrml",
    "sim/flightmodel/position/indicated_airspeed", "sim/flightmodel/position/y_agl",
    "sim/cockpit2/controls/flap_ratio", "sim/operation/override/override_control_surfaces",
    "sim/cockpit2/autopilot/servos_on", "sim/operation/misc/frame_rate_period"
};
enum { OUT_PITCH_SWITCH, OUT_RUDDER_KNOB, OUT_RESET_PRESS, OUT_PITCH, OUT_RUDDER,
       OUT_DEGREES, OUT_POWER, OUT_DEVICE, OUT_DRAWS, OUT_LAW, OUT_MANUAL, OUT_AUTO,
       OUT_RESETTING, OUT_BOUND, OUT_COMPUTER, OUT_COUNT };
static const char *out_names[]={"pitch_switch","rudder_knob","reset_pressed",
    "pitch_ratio","rudder_ratio","rudder_degrees","powered","device_active",
    "draw_count","control_law","manual_pitch_available","auto_pitch_active",
    "rudder_resetting","device_bound","computer_active"};

typedef struct { const char *name; int action; XPLMCommandRef ref; unsigned source; } Command;
static Command commands[]={
    {PREFIX "pitch_up",TRIM_PITCH_UP,0,1}, {PREFIX "pitch_down",TRIM_PITCH_DOWN,0,1},
    {PREFIX "rudder_left",TRIM_RUDDER_LEFT,0,1}, {PREFIX "rudder_right",TRIM_RUDDER_RIGHT,0,1},
    {PREFIX "rudder_reset",TRIM_RUDDER_RESET,0,1},
    {"sim/flight_controls/pitch_trim_up",TRIM_PITCH_UP,0,2},
    {"sim/flight_controls/pitch_trim_down",TRIM_PITCH_DOWN,0,2},
    {"sim/flight_controls/pitch_trim_up_elec",TRIM_PITCH_UP,0,4},
    {"sim/flight_controls/pitch_trim_down_elec",TRIM_PITCH_DOWN,0,4},
    {"sim/flight_controls/pitch_trim_up_mech",TRIM_PITCH_UP,0,8},
    {"sim/flight_controls/pitch_trim_down_mech",TRIM_PITCH_DOWN,0,8},
    {"sim/flight_controls/pitch_trimA_up",TRIM_PITCH_UP,0,16},
    {"sim/flight_controls/pitch_trimA_down",TRIM_PITCH_DOWN,0,16},
    {"sim/flight_controls/pitch_trimB_up",TRIM_PITCH_UP,0,32},
    {"sim/flight_controls/pitch_trimB_down",TRIM_PITCH_DOWN,0,32},
    {"sim/flight_controls/rudder_trim_left",TRIM_RUDDER_LEFT,0,2},
    {"sim/flight_controls/rudder_trim_right",TRIM_RUDDER_RIGHT,0,2},
    {"sim/flight_controls/rudder_trim_center",TRIM_RUDDER_RESET,0,2},
    {"sim/flight_controls/rudder_trimA_left",TRIM_RUDDER_LEFT,0,4},
    {"sim/flight_controls/rudder_trimA_right",TRIM_RUDDER_RIGHT,0,4},
    {"sim/flight_controls/rudder_trimB_left",TRIM_RUDDER_LEFT,0,8},
    {"sim/flight_controls/rudder_trimB_right",TRIM_RUDDER_RIGHT,0,8}
};

static struct {
    XPLMDataRef raw[RAW_COUNT], out[OUT_COUNT];
    XPLMDataRef fbw_enabled, fbw_normal;
    XPLMDataRef guidance_servo;
    XPLMAvionicsID device;
    TrimState state;
    TrimInputs inputs;
    int enabled, owned, initialized, law, draws;
    char aircraft_root[2048];
} plugin;

static void slash(char *s) { for(;*s;s++) if(*s=='\\')*s='/'; }
static int owned_aircraft(void)
{
    char name[256]={0},path[2048]={0};
    XPLMGetNthAircraftModel(0,name,path); slash(path);
    return !strcmp(name,"A380X-XP12.acf") &&
        !strncmp(path,plugin.aircraft_root,strlen(plugin.aircraft_root));
}
static float rawf(int i) { return XPLMGetDataf(plugin.raw[i]); }
static int rawi(int i) { return XPLMGetDatai(plugin.raw[i]); }

static void read_inputs(void)
{
    TrimInputs *in=&plugin.inputs;
    float bus[6]={0},elevators[2]={0};
    in->pitch=rawf(PITCH); in->rudder=rawf(RUDDER);
    in->ground=rawi(GROUND)!=0; in->suspended=rawi(PAUSED)||rawi(REPLAY);
    XPLMGetDatavf(plugin.raw[BUS],bus,0,6);
    /* Installed ACF assigns both trim actuators to bus bit 1 (native bus 0). */
    in->powered=plugin.owned && bus[0]>=18 && isfinite(bus[0]);
    in->pitch_available=rawi(PFAIL)<6 && rawi(ELECTRIC)!=0;
    in->rudder_available=rawi(RFAIL)<6;
    in->pitch_up_deg=fabs(rawf(UP_LIMIT)); in->pitch_down_deg=fabs(rawf(DN_LIMIT));
    in->rudder_left_deg=fabs(rawf(YAW_LEFT)*rawf(YAW_MIN));
    in->rudder_right_deg=fabs(rawf(YAW_RIGHT)*rawf(YAW_MAX));
    in->pitch_time=rawf(PITCH_TIME);
    XPLMGetDatavf(plugin.raw[ELEVATORS],elevators,8,2);
    in->elevator_deg=(elevators[0]+elevators[1])*.5;
    in->bank_deg=rawf(BANK); in->nz=rawf(NZ); in->ias=rawf(IAS);
    in->radio_height_ft=rawf(AGL)*3.280839895; in->flaps=rawf(FLAPS);
    in->external_surfaces=rawi(EXTERNAL_SURFACES)!=0;
    in->native_autopilot=rawi(NATIVE_AP)!=0;
    if(!plugin.guidance_servo)plugin.guidance_servo=XPLMFindDataRef("a380x/xp12/fcu/servo_active");
    in->computer_active=plugin.guidance_servo && XPLMGetDatai(plugin.guidance_servo)!=0;
    in->law=plugin.law;
    /* Optional experimental FBW controller already exposes this authority.
     * Disabled FBW's normal_law=0 is NOT an assertion of direct law. */
    if(!plugin.fbw_enabled)plugin.fbw_enabled=XPLMFindDataRef("a380x/xp12/fbw/enabled");
    if(!plugin.fbw_normal)plugin.fbw_normal=XPLMFindDataRef("a380x/xp12/fbw/normal_law");
    if(plugin.fbw_enabled && plugin.fbw_normal && XPLMGetDatai(plugin.fbw_enabled))
        in->law=XPLMGetDatai(plugin.fbw_normal)?TRIM_NORMAL:TRIM_DIRECT;
}

static float get_output(void *refcon)
{
    switch((int)(intptr_t)refcon) {
    case OUT_PITCH_SWITCH:return (float)plugin.state.pitch_switch;
    case OUT_RUDDER_KNOB:return (float)plugin.state.rudder_knob;
    case OUT_RESET_PRESS:return (float)plugin.state.reset_pressed;
    case OUT_PITCH:return (float)plugin.inputs.pitch;
    case OUT_RUDDER:return (float)plugin.inputs.rudder;
    case OUT_DEGREES:return (float)trim_rudder_degrees(&plugin.inputs);
    case OUT_POWER:return plugin.enabled && plugin.inputs.powered;
    case OUT_DEVICE:return plugin.enabled && plugin.owned && plugin.device!=NULL;
    case OUT_DRAWS:return (float)plugin.draws;
    case OUT_LAW:return (float)plugin.inputs.law;
    case OUT_MANUAL:return plugin.state.manual_pitch && plugin.inputs.powered && plugin.inputs.pitch_available;
    case OUT_AUTO:return plugin.state.auto_pitch;
    case OUT_RESETTING:return plugin.state.resetting;
    case OUT_BOUND:return plugin.device?XPLMIsAvionicsBound(plugin.device):0;
    case OUT_COMPUTER:return plugin.inputs.computer_active;
    default:return 0;
    }
}
static int get_output_int(void *r) { return (int)get_output(r); }
static void set_law(void *r,float value)
{
    (void)r;
    if(isfinite(value) && value>=TRIM_NORMAL && value<=TRIM_DIRECT && value==floorf(value)) {
        plugin.law=(int)value; trim_release(&plugin.state);
    }
}
static void set_law_int(void *r,int value) { set_law(r,(float)value); }

static int command_handler(XPLMCommandRef ref,XPLMCommandPhase phase,void *refcon)
{
    Command *c=refcon; (void)ref;
    if(!plugin.enabled || !owned_aircraft())return 1;
    trim_command(&plugin.state,c->action,c->source,(int)phase);
    return 0;
}

#ifndef A380X_TRIM_TEST
static void rect(float x,float y,float w,float h)
{
    glBegin(GL_QUADS);glVertex2f(x,y);glVertex2f(x+w,y);glVertex2f(x+w,y+h);glVertex2f(x,y+h);glEnd();
}
static void digit(char c,float x)
{
    static const unsigned masks[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    static const float seg[7][4]={{5,94,37,7},{42,55,7,37},{42,13,7,37},
        {5,5,37,7},{0,13,7,37},{0,55,7,37},{5,48,37,7}};
    unsigned mask= c>='0'&&c<='9'?masks[c-'0']:c=='L'?0x38:c=='R'?0x73:c=='-'?0x40:0;
    int i;
    for(i=0;i<7;i++) if(mask&(1u<<i))rect(x+seg[i][0],18+seg[i][1],seg[i][2],seg[i][3]);
    if(c=='R') {
        glBegin(GL_QUADS);glVertex2f(x+23,73);glVertex2f(x+30,73);
        glVertex2f(x+49,23);glVertex2f(x+42,23);glEnd();
    }
}
#endif
static void draw_display(void *refcon)
{
    (void)refcon;
    ++plugin.draws;
#ifndef A380X_TRIM_TEST
    char label[8]; int i; float x,width=0;
    XPLMSetGraphicsState(0,0,0,0,1,0,0);
    glClearColor(.008f,.007f,.003f,1);glClear(GL_COLOR_BUFFER_BIT);
    if(!plugin.enabled || !plugin.owned || !plugin.inputs.powered)return;
    trim_format_rudder(trim_rudder_degrees(&plugin.inputs),label);
    glColor4f(.94f,.58f,.035f,1);
    digit(label[0],15);
    for(i=1;label[i];i++)width+=label[i]=='.'?13:label[i]==' '?35:64;
    x=384-15-width;
    for(i=1;label[i];i++) {
        if(label[i]=='.'){rect(x-5,23,7,7);x+=13;}
        else {digit(label[i],x);x+=label[i]==' '?35:64;}
    }
#endif
}
static float display_brightness(float rheostat,float ambient,float bus,void *refcon)
{
    (void)rheostat;(void)ambient;(void)bus;(void)refcon;
    return plugin.enabled && plugin.inputs.powered?1:0;
}
static float flight_loop(float elapsed,float since_loop,int counter,void *refcon)
{
    (void)elapsed;(void)since_loop;(void)counter;(void)refcon;
    plugin.owned=owned_aircraft();
    if(!plugin.enabled || !plugin.owned) { trim_release(&plugin.state); return -1; }
    read_inputs();
    trim_step(&plugin.state,&plugin.inputs,rawf(TIME_STEP));
    if(plugin.state.write_pitch)XPLMSetDataf(plugin.raw[PITCH],(float)plugin.state.pitch);
    if(plugin.state.write_rudder)XPLMSetDataf(plugin.raw[RUDDER],(float)plugin.state.rudder);
    plugin.inputs.pitch=rawf(PITCH);plugin.inputs.rudder=rawf(RUDDER);
    if(plugin.device)XPLMAvionicsNeedsDrawing(plugin.device);
    return -1;
}

PLUGIN_API void XPluginDisable(void)
{
    plugin.enabled=0;trim_release(&plugin.state);
    if(plugin.device) { XPLMDestroyAvionics(plugin.device);plugin.device=NULL; }
}
PLUGIN_API int XPluginEnable(void)
{
    XPLMCreateAvionics_t p={0};
    if(!plugin.initialized)return 0;
    if(plugin.enabled)return 1;
    plugin.enabled=1;plugin.owned=owned_aircraft();read_inputs();
    p.structSize=sizeof(p);p.screenWidth=p.bezelWidth=384;p.screenHeight=p.bezelHeight=136;
    p.drawOnDemand=1;p.drawCallback=draw_display;p.brightnessCallback=display_brightness;
    p.deviceID="a380x.xp12.trim.rudder";p.deviceName="A380X Rudder Trim Position";
    plugin.device=XPLMCreateAvionicsEx(&p);
    if(!plugin.device) { plugin.enabled=0;return 0; }
    XPLMAvionicsNeedsDrawing(plugin.device);
    return 1;
}
PLUGIN_API void XPluginStop(void)
{
    int i;
    XPluginDisable();
    if(plugin.initialized)XPLMUnregisterFlightLoopCallback(flight_loop,NULL);
    for(i=0;i<COUNT(commands);i++)if(commands[i].ref) {
        XPLMUnregisterCommandHandler(commands[i].ref,command_handler,1,&commands[i]);commands[i].ref=NULL;
    }
    for(i=0;i<OUT_COUNT;i++)if(plugin.out[i]) {
        XPLMUnregisterDataAccessor(plugin.out[i]);plugin.out[i]=NULL;
    }
    plugin.initialized=0;
}
PLUGIN_API int XPluginStart(char *name,char *signature,char *description)
{
    int i;char path[2048]={0},*at,refname[160];
    memset(&plugin,0,sizeof(plugin));trim_init(&plugin.state);
    strcpy(name,"A380X Trim Panel");strcpy(signature,"org.a380x.xp12.trim.v1");
    strcpy(description,"Aircraft-local pitch/rudder trim controls, spring animations and RTPI");
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS",1);
    XPLMGetPluginInfo(XPLMGetMyID(),NULL,path,NULL,NULL);slash(path);
    at=strstr(path,"/plugins/A380XTrim/");
    if(!at)return 0;
    at[1]=0;strcpy(plugin.aircraft_root,path);
    for(i=0;i<RAW_COUNT;i++) {
        plugin.raw[i]=XPLMFindDataRef(raw_names[i]);
        if(!plugin.raw[i]) { XPLMDebugString("A380X Trim: missing required native dataref\n");return 0; }
    }
    if(!XPLMCanWriteDataRef(plugin.raw[PITCH]) || !XPLMCanWriteDataRef(plugin.raw[RUDDER]))return 0;
    for(i=0;i<OUT_COUNT;i++) {
        snprintf(refname,sizeof(refname),PREFIX "%s",out_names[i]);
        if(XPLMFindDataRef(refname)) { XPluginStop();return 0; }
        plugin.out[i]=XPLMRegisterDataAccessor(refname,xplmType_Float|xplmType_Int,i==OUT_LAW,
            get_output_int,i==OUT_LAW?set_law_int:NULL,get_output,i==OUT_LAW?set_law:NULL,
            NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,(void *)(intptr_t)i,(void *)(intptr_t)i);
        if(!plugin.out[i]) { XPluginStop();return 0; }
    }
    for(i=0;i<COUNT(commands);i++) {
        commands[i].ref=i<5?XPLMCreateCommand(commands[i].name,commands[i].name+strlen(PREFIX)):
            XPLMFindCommand(commands[i].name);
        if(!commands[i].ref) { XPluginStop();return 0; }
        XPLMRegisterCommandHandler(commands[i].ref,command_handler,1,&commands[i]);
    }
    plugin.initialized=1;
    XPLMRegisterFlightLoopCallback(flight_loop,-1,NULL);
    XPLMDebugString("A380X Trim: panel controls and RTPI registered; existing avionics unchanged\n");
    return 1;
}
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from,int message,void *parameter)
{
    (void)from;
    if(message==XPLM_MSG_PLANE_LOADED && (intptr_t)parameter==0) {
        trim_release(&plugin.state);plugin.fbw_enabled=plugin.fbw_normal=plugin.guidance_servo=NULL;
        plugin.law=TRIM_NORMAL;plugin.owned=owned_aircraft();
    }
}
