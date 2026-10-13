/* SPDX-License-Identifier: GPL-3.0-or-later
 * Executes the production FCU adapter and guidance core against in-memory XPLM.
 * No simulator process, network or aircraft file is read or written.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"
#include "a380x_guidance.h"
#include "a380x_pfd_data.h"
#define ARRAY_LEN(a) ((int)(sizeof(a)/sizeof((a)[0])))

typedef struct {
    char name[180]; double value; int array_i[500],count,registered,writable,writes;
    float array_f[500]; char bytes[150];
    XPLMGetDatai_f read_i; XPLMGetDataf_f read_f; void *read_refcon;
} MockRef;
typedef struct { char name[180]; XPLMCommandCallback_f callback; void *refcon; int active; } MockCommand;
static MockRef mock_refs[200];
static MockCommand mock_commands[60];
static int ref_count,command_count,registrations,registration_fail=-1,command_once_count;
static float mock_clock;
static const char *missing_raw;
static MockRef *r(XPLMDataRef ref) { assert(ref);return (MockRef *)ref; }
static MockRef *new_ref(const char *name) {
    MockRef *p;int i;assert(ref_count<ARRAY_LEN(mock_refs));p=&mock_refs[ref_count++];
    memset(p,0,sizeof(*p));snprintf(p->name,sizeof(p->name),"%s",name);p->count=500;
    for(i=0;i<500;++i) p->array_f[i]=strstr(name,"maximum")?1:strstr(name,"minimum")?0:.5f;
    if(strstr(name,"gear/deploy_ratio"))for(i=0;i<500;++i)p->array_f[i]=0;
    if(strstr(name,"acf_flap_detents"))p->value=6;
    if(strstr(name,"acf_flap_dn")){
        const float schedule[]={0,1,5,10,20,25,40};
        memset(p->array_f,0,sizeof(p->array_f));memcpy(p->array_f,schedule,sizeof(schedule));
    }
    if(strstr(name,"avionics_power"))p->value=1;
    if(strstr(name,"true_airspeed"))p->value=150;
    if(strstr(name,"acf_Vne"))p->value=340;
    if(strstr(name,"m_total"))p->value=400000;
    if(strstr(name,"pressure_altitude"))p->value=10020.125;
    if(strstr(name,"g_nrml"))p->value=1;
    if(strstr(name,"barometer_setting_in_hg"))p->value=29.92;
    if(strstr(name,"_slope_degt"))p->value=3.2;
    if(strstr(name,"nav1_type")||strstr(name,"nav2_type"))p->value=4;
    if(strstr(name,"display_horizontal")||strstr(name,"display_vertical")||strstr(name,"has_dme"))p->value=1;
    if(strstr(name,"course_deg"))p->value=90;
    if(strstr(name,"frequency_hz"))p->value=11030;
    if(strstr(name,"dme_distance"))p->value=12.5;
    if(strstr(name,"nav_id"))strcpy(p->bytes,"ITEST");
    return p;
}
XPLMDataRef XPLMFindDataRef(const char *name) {
    int i;if(missing_raw&&!strcmp(name,missing_raw))return NULL;
    for(i=0;i<ref_count;++i)if(!strcmp(mock_refs[i].name,name))return &mock_refs[i];
    return !strncmp(name,"sim/",4)?new_ref(name):NULL;
}
int XPLMGetDatai(XPLMDataRef ref) { MockRef*p=r(ref);return p->read_i?p->read_i(p->read_refcon):(int)p->value; }
float XPLMGetDataf(XPLMDataRef ref) { MockRef*p=r(ref);assert(!strstr(p->name,"pressure_altitude"));return p->read_f?p->read_f(p->read_refcon):(float)p->value; }
double XPLMGetDatad(XPLMDataRef ref) { return r(ref)->value; }
void XPLMSetDatai(XPLMDataRef ref,int value) { r(ref)->value=value;r(ref)->writes++; }
void XPLMSetDataf(XPLMDataRef ref,float value) { assert(isfinite(value));r(ref)->value=value;r(ref)->writes++; }
int XPLMGetDatavi(XPLMDataRef ref,int *values,int offset,int count) {
    MockRef*p=r(ref);if(count>p->count-offset)count=p->count-offset;if(count<0)return 0;
    memcpy(values,p->array_i+offset,(size_t)count*sizeof(*values));return count;
}
int XPLMGetDatavf(XPLMDataRef ref,float *values,int offset,int count) {
    MockRef*p=r(ref);if(count>p->count-offset)count=p->count-offset;if(count<0)return 0;
    memcpy(values,p->array_f+offset,(size_t)count*sizeof(*values));return count;
}
void XPLMSetDatavf(XPLMDataRef ref,float *values,int offset,int count) {
    int i;MockRef*p=r(ref);for(i=0;i<count;++i)assert(isfinite(values[i]));
    memcpy(p->array_f+offset,values,(size_t)count*sizeof(*values));p->writes++;
}
int XPLMGetDatab(XPLMDataRef ref,void *out,int offset,int count) {
    MockRef*p=r(ref);if(count>150-offset)count=150-offset;memcpy(out,p->bytes+offset,(size_t)count);return count;
}
XPLMDataRef XPLMRegisterDataAccessor(const char *name,XPLMDataTypeID type,int writable,
    XPLMGetDatai_f read_i,XPLMSetDatai_f write_i,XPLMGetDataf_f read_f,XPLMSetDataf_f write_f,
    XPLMGetDatad_f read_d,XPLMSetDatad_f write_d,XPLMGetDatavi_f read_vi,XPLMSetDatavi_f write_vi,
    XPLMGetDatavf_f read_vf,XPLMSetDatavf_f write_vf,XPLMGetDatab_f read_b,XPLMSetDatab_f write_b,
    void *read_refcon,void *write_refcon) {
    MockRef*p;(void)type;(void)read_d;(void)write_d;(void)read_vi;(void)write_vi;(void)read_vf;(void)write_vf;
    (void)read_b;(void)write_b;(void)write_refcon;
    assert(!writable&&!write_i&&!write_f);
    if(registrations++==registration_fail)return NULL;
    assert(!XPLMFindDataRef(name));p=new_ref(name);p->registered=1;p->writable=writable;
    p->read_i=read_i;p->read_f=read_f;p->read_refcon=read_refcon;return p;
}
void XPLMUnregisterDataAccessor(XPLMDataRef ref) { r(ref)->registered=0;r(ref)->name[0]=0; }
XPLMCommandRef XPLMFindCommand(const char *name) {
    int i;for(i=0;i<command_count;++i)if(!strcmp(mock_commands[i].name,name))return &mock_commands[i];return NULL;
}
XPLMCommandRef XPLMCreateCommand(const char *name,const char *description) {
    MockCommand*p=(MockCommand*)XPLMFindCommand(name);(void)description;if(p)return p;
    assert(command_count<ARRAY_LEN(mock_commands));p=&mock_commands[command_count++];memset(p,0,sizeof(*p));
    snprintf(p->name,sizeof(p->name),"%s",name);return p;
}
void XPLMRegisterCommandHandler(XPLMCommandRef ref,XPLMCommandCallback_f handler,int before,void *refcon) {
    MockCommand*p=(MockCommand*)ref;assert(before);p->callback=handler;p->refcon=refcon;p->active=1;
}
void XPLMUnregisterCommandHandler(XPLMCommandRef ref,XPLMCommandCallback_f handler,int before,void *refcon) {
    MockCommand*p=(MockCommand*)ref;assert(before&&p->callback==handler&&p->refcon==refcon);p->active=0;
}
void XPLMCommandOnce(XPLMCommandRef ref) {
    MockCommand*p=(MockCommand*)ref;assert(p);command_once_count++;
    if(p->active){p->callback(p,xplm_CommandBegin,p->refcon);p->callback(p,xplm_CommandEnd,p->refcon);}
}
float XPLMGetElapsedTime(void) { return mock_clock; }

typedef struct {
    XPLMDataRef baro,baro_standard,nav_type,nav_horizontal,nav_vertical,nav_loc,nav_gs,nav_course,nav_distance;
    XPLMDataRef nav_frequency,nav_identifier,nav_has_dme;char nav_ident[16];
    float nav_frequency_mhz,nav_dme_nm;int nav_dme_valid;
    float ias_trend_value,previous_ias_value,baro_hpa_value,loc_dots,gs_dots;
    int ias_trend_visible,trend_initialized,baro_std,loc_valid,gs_valid,ls_visible;
    float ias_value,altitude_value,vertical_speed_value,pitch_value,roll_value,heading_value,radio_altitude_value,mach_value;
    int navigation_valid;
    int air_valid,altitude_valid,vertical_speed_valid,attitude_valid,heading_valid,radio_altitude_valid,display_valid;
} Side;
static struct {XPLMDataRef avionics_power,on_ground,ground_track;Side side[2];} refs;
static struct {int fma_a1a2,fma_a3,fma_b1,fma_c1,fma_d1d2;} published;
static A380XGuidanceState guidance;
static A380XGuidanceInputs guidance_inputs;
static A380XPfdGuidance pfd_guidance;
static A380XFmsFeed fms_feed; /* native FMS outputs (MFD adapter); zero = none */
static int plugin_enabled;
static float throttle_values[4],ground_speed_knots,ground_track_degrees;
static XPLMDataRef find_required(const char *name,int *ok) {XPLMDataRef p=XPLMFindDataRef(name);if(!p)*ok=0;return p;}
static void read_side(Side *side,int powered) {side->display_valid=powered;}
#include "a380x_fcu_adapter.inc"
#include "a380x_lighting.inc"
#include "a380x_du.inc"
static void setup(void);

static void test_display_unit_controls(void) {
    int i,j;
    XPLMDataRef handles[8];
    setup();assert(du_init());
    for(i=0;i<ARRAY_LEN(du_channels);++i) {
        DuChannel *channel=&du_channels[i];
        assert(channel->handle&&channel->up&&channel->down);
        handles[i]=channel->handle;
        assert(XPLMGetDatai(channel->handle)==100);
        for(j=0;j<25;++j)XPLMCommandOnce(channel->down);
        assert(XPLMGetDatai(channel->handle)==0);
        if(i==0)assert(XPLMGetDatai(du_channels[4].handle)==100);
        XPLMCommandOnce(channel->up);
        assert(XPLMGetDatai(channel->handle)==5);
        for(j=0;j<25;++j)XPLMCommandOnce(channel->up);
        assert(XPLMGetDatai(channel->handle)==100);
    }
    du_shutdown();
    for(i=0;i<ARRAY_LEN(du_channels);++i)assert(!r(handles[i])->registered);
}

static void test_glareshield_lighting(void) {
    int i,j;
    XPLMDataRef panel;
    XPLMDataRef handles[4];
    setup();
    assert(lighting_init());
    panel=XPLMFindDataRef("sim/cockpit2/switches/panel_brightness_ratio");
    assert(fabsf(r(panel)->array_f[1]-1.0f)<.001f);
    assert(fabsf(r(panel)->array_f[2]-1.0f)<.001f);
    for(i=0;i<ARRAY_LEN(lighting);++i) {
        LightingChannel *channel=&lighting[i];
        assert(channel->handle&&channel->up&&channel->down);
        handles[i]=channel->handle;
        for(j=0;j<25;++j) XPLMCommandOnce(channel->down);
        assert(XPLMGetDataf(channel->handle)==0.0f);
        XPLMCommandOnce(channel->up);
        assert(fabsf(XPLMGetDataf(channel->handle)-.05f)<.001f);
        assert(XPLMGetDatai(channel->handle)==5);
        for(j=0;j<25;++j) XPLMCommandOnce(channel->up);
        assert(XPLMGetDataf(channel->handle)==1.0f);
    }
    XPLMCommandOnce(lighting[0].down);
    assert(fabsf(r(panel)->array_f[1]-.95f)<.001f);
    assert(fabsf(r(panel)->array_f[2]-1.0f)<.001f);
    XPLMCommandOnce(lighting[3].down);
    assert(fabsf(r(panel)->array_f[2]-.95f)<.001f);
    lighting_shutdown();
    for(i=0;i<ARRAY_LEN(lighting);++i) assert(!r(handles[i])->registered);
}

static void normal_side(Side*s) {
    memset(s,0,sizeof(*s));s->ias_value=250;s->mach_value=.55f;s->altitude_value=10000;
    s->heading_value=90;s->radio_altitude_value=8000;s->pitch_value=2;
    s->navigation_valid=1;
    s->air_valid=s->altitude_valid=s->vertical_speed_valid=s->attitude_valid=s->heading_valid=s->radio_altitude_valid=s->display_valid=1;
}
static void setup(void) {
    int i;memset(mock_refs,0,sizeof(mock_refs));memset(mock_commands,0,sizeof(mock_commands));
    memset(&fcu,0,sizeof(fcu));memset(&guidance,0,sizeof(guidance));
    ref_count=command_count=registrations=command_once_count=0;registration_fail=-1;missing_raw=NULL;mock_clock=0;
    refs.avionics_power=XPLMFindDataRef("sim/cockpit2/switches/avionics_power_on");
    refs.on_ground=XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    refs.ground_track=XPLMFindDataRef("sim/flightmodel/position/hpath");r(refs.ground_track)->value=90;
    normal_side(&refs.side[0]);normal_side(&refs.side[1]);ground_speed_knots=280;
    for(i=0;i<4;++i)throttle_values[i]=.5f;
    XPLMCreateCommand("sim/autopilot/servos_off_any","native disconnect");plugin_enabled=1;
}
static void tick(int count) {int i;for(i=0;i<count;++i){mock_clock+=.1f;update_guidance(.1f);}}
static void command(const char *suffix) {
    char name[180];MockCommand*c;snprintf(name,sizeof(name),"a380x/xp12/fcu/%s",suffix);
    c=XPLMFindCommand(name);assert(c&&c->active);c->callback(c,xplm_CommandBegin,c->refcon);
}
static void engage(void) {tick(70);command("ap1_toggle");tick(1);assert(guidance.ap1&&fcu.own_pitch&&fcu.own_roll);}
static void test_registry_and_partial_init(void) {
    int i,j;setup();assert(fcu_init());assert(ARRAY_LEN(fcu_commands)==32&&ARRAY_LEN(fcu_outputs)==25);
    for(i=0;i<ARRAY_LEN(fcu_commands);++i){assert(fcu_commands[i].handle);for(j=0;j<i;++j)assert(strcmp(fcu_commands[i].name,fcu_commands[j].name));}
    for(i=0;i<ARRAY_LEN(fcu_outputs);++i)assert(r(fcu_outputs[i].handle)->registered&&!r(fcu_outputs[i].handle)->writable);
    command("speed_up");assert(guidance.selected_speed_knots==251);
    {MockCommand*c=fcu_commands[0].handle;c->callback(c,xplm_CommandContinue,c->refcon);assert(guidance.selected_speed_knots==251);
     mock_clock=.41f;c->callback(c,xplm_CommandContinue,c->refcon);assert(guidance.selected_speed_knots==252);}
    fcu_shutdown();for(i=0;i<command_count;++i)assert(!mock_commands[i].active);
    for(i=0;i<ref_count;++i)assert(!mock_refs[i].registered);
    setup();registration_fail=5;assert(!fcu_init());for(i=0;i<ref_count;++i)assert(!mock_refs[i].registered);
    setup();registration_fail=27;assert(!fcu_init());for(i=0;i<ref_count;++i)assert(!mock_refs[i].registered);
    setup();missing_raw="sim/flightmodel2/position/pressure_altitude";assert(!fcu_init());assert(!guidance.initialized);
}
static void test_pfd_aircraft_inputs(void) {
    int i;MockRef *envelope,*vls,*prot,*limit;
    setup();
    envelope=new_ref("a380x/xp12/systems/pfd/envelope_valid");
    vls=new_ref("a380x/xp12/systems/pfd/vls_knots");vls->value=170;
    prot=new_ref("a380x/xp12/systems/pfd/alpha_prot_knots");prot->value=155;
    limit=new_ref("a380x/xp12/systems/pfd/alpha_limit_knots");limit->value=145;
    assert(fcu_init());tick(1);
    assert(pfd_guidance.flap_valid&&pfd_guidance.slat_valid&&pfd_guidance.speedbrake_valid);
    assert(pfd_guidance.flap_config==0&&pfd_guidance.vmax_knots==340);
    assert(pfd_guidance.vfe_next_knots==263&&pfd_guidance.clean_performance_valid);
    assert(fabs(pfd_guidance.vls_knots-189.0161)<.001&&!pfd_guidance.envelope_valid);
    assert(fabs(pfd_green_dot(400000,10000)-205)<.001);
    assert(fabs(pfd_green_dot(425000,15000)-219.25)<.001);
    assert(pfd_green_dot(NAN,10000)==0&&pfd_green_dot(200000,10000)==0);
    for(i=0;i<ARRAY_LEN(pfd_outputs);++i)
        assert(r(pfd_outputs[i].handle)->registered&&!r(pfd_outputs[i].handle)->writable);
    r(pfd_input_refs[PI_FLAP_HANDLE])->value=.5;r(pfd_input_refs[PI_FLAP_ACTUAL])->value=.25;
    r(pfd_input_refs[PI_SLAT_ACTUAL])->value=1;tick(1);
    assert(pfd_guidance.flap_handle_index==2&&pfd_guidance.flap_config==3);
    assert(!pfd_guidance.clean_performance_valid&&!pfd_guidance.vls_knots);
    assert(pfd_guidance.flap_target_ratio==.25&&pfd_guidance.flap_ratio==.25);
    assert(pfd_guidance.vmax_knots==220&&pfd_guidance.vfe_next_knots==196);
    assert(guidance_inputs.vmax_knots==220);
    /* Surface-target mapping follows the actual aircraft schedule. */
    r(pfd_input_refs[PI_FLAP_ANGLES])->array_f[3]=12;tick(1);
    assert(fabs(pfd_guidance.flap_target_ratio-.3)<.001);
    r(pfd_input_refs[PI_FLAP_ANGLES])->array_f[3]=10;
    r(pfd_input_refs[PI_FLAP_HANDLE])->value=1;r(pfd_input_refs[PI_FLAP_ACTUAL])->value=1;tick(1);
    assert(pfd_guidance.vmax_knots==182&&pfd_guidance.vfe_next_knots==0);
    r(pfd_input_refs[PI_FLAP_HANDLE])->value=0;tick(1);
    assert(pfd_guidance.vmax_knots==182&&pfd_guidance.flap_target_ratio==0);
    r(pfd_input_refs[PI_FLAP_ACTUAL])->value=.625;tick(1);
    assert(pfd_guidance.vmax_knots==196); /* actual25deg, native detent5 -> CONF3 */
    r(pfd_input_refs[PI_FLAP_ACTUAL])->value=.5;tick(1);
    assert(pfd_guidance.vmax_knots==196); /* actual20deg, native detent4 -> CONF3 */
    r(pfd_input_refs[PI_FLAP_ACTUAL])->value=.25;tick(1);
    assert(pfd_guidance.vmax_knots==220); /* actual10deg -> CONF2 */
    r(pfd_input_refs[PI_FLAP_ACTUAL])->value=.125;tick(1);
    assert(pfd_guidance.vmax_knots==222); /* actual5deg -> CONF1+F */
    r(pfd_input_refs[PI_FLAP_ANGLES])->array_f[3]=NAN;tick(1);
    assert(!pfd_guidance.flap_valid); /* no invented mapping on invalid schedule */
    r(pfd_input_refs[PI_FLAP_ANGLES])->array_f[3]=10;
    r(pfd_input_refs[PI_FLAP_ACTUAL])->value=0;tick(1);
    assert(pfd_guidance.vmax_knots==263); /* remaining slat extension */
    r(pfd_input_refs[PI_SLAT_ACTUAL])->value=0;
    for(i=0;i<5;++i)r(pfd_input_refs[PI_GEAR])->array_f[i]=1;
    tick(1);assert(pfd_guidance.gear_down&&pfd_guidance.vmax_knots<=250);
    r(pfd_input_refs[PI_SPOILER_HANDLE])->value=-.5;
    r(pfd_input_refs[PI_SPOILER_ACTUAL])->value=.7;tick(1);
    assert(pfd_guidance.spoilers_armed&&fabs(pfd_guidance.spoiler_ratio-.7)<.001);
    r(pfd_input_refs[PI_FLAP_FAILURE])->value=6;tick(1);assert(pfd_guidance.flap_fault);
    r(pfd_input_refs[PI_STALL])->value=1;tick(1);assert(pfd_guidance.stall_warning);
    r(refs.on_ground)->value=1;tick(1);assert(!pfd_guidance.stall_warning&&pfd_guidance.on_ground);
    envelope->value=1;tick(1);
    assert(pfd_guidance.envelope_valid&&pfd_guidance.vls_knots==170);
    assert(!pfd_guidance.clean_performance_valid);
    assert(pfd_guidance.alpha_prot_knots==155&&pfd_guidance.alpha_limit_knots==145);
    assert(guidance_inputs.vls_knots==170);
    limit->value=160;tick(1);assert(!pfd_guidance.alpha_prot_knots&&!pfd_guidance.alpha_limit_knots);
    vls->value=NAN;tick(1);assert(!pfd_guidance.vls_knots);
    envelope->value=0;tick(1);assert(!pfd_guidance.vls_knots&&!pfd_guidance.alpha_prot_knots);
    assert(fabs(XPLMGetDataf(XPLMFindDataRef("a380x/xp12/pfd/vmax_knots"))-pfd_guidance.vmax_knots)<.001);
    fcu_shutdown();for(i=0;i<ref_count;++i)assert(!mock_refs[i].registered);
}
static void test_clean_performance_boundary(void) {
    setup();assert(fcu_init());tick(1);
    /* Known pinned PRIM nodes include the rows that differ from the TS table. */
    assert(pfd_clean_vls(250000,0)==149&&pfd_clean_vls(600000,43000)==250);
    assert(pfd_clean_vls(400000,15000)==193);
    assert(pfd_clean_vls(550000,38000)==262&&pfd_clean_vls(600000,38000)==280);
    assert(fabs(pfd_clean_vls(425000,12500)-198.25)<.0001);
    assert(pfd_clean_vls(249999,10000)==0&&pfd_clean_vls(600001,10000)==0);
    assert(pfd_clean_vls(400000,-1)==0&&pfd_clean_vls(400000,43001)==0);
    assert(pfd_clean_vls(NAN,10000)==0&&pfd_clean_vls(400000,NAN)==0);
    assert(pfd_guidance.clean_performance_valid&&guidance_inputs.vls_knots>0);
    assert(!pfd_guidance.envelope_valid&&!pfd_guidance.alpha_prot_knots&&
           !pfd_guidance.alpha_limit_knots&&!pfd_guidance.stall_warning_knots&&
           !pfd_guidance.f_speed_knots&&!pfd_guidance.s_speed_knots);
    r(pfd_input_refs[PI_FLAP_HANDLE])->value=.01;tick(1);
    assert(!pfd_guidance.clean_performance_valid&&!pfd_guidance.vls_knots);
    r(pfd_input_refs[PI_FLAP_HANDLE])->value=0;r(pfd_input_refs[PI_FLAP_ACTUAL])->value=.001;tick(1);
    assert(!pfd_guidance.clean_performance_valid&&!pfd_guidance.vls_knots);
    r(pfd_input_refs[PI_FLAP_ACTUAL])->value=0;r(pfd_input_refs[PI_SLAT_ACTUAL])->value=.001;tick(1);
    assert(!pfd_guidance.clean_performance_valid&&!pfd_guidance.vls_knots);
    r(pfd_input_refs[PI_SLAT_ACTUAL])->value=0;r(pfd_input_refs[PI_SPOILER_HANDLE])->value=.1;tick(1);
    assert(!pfd_guidance.clean_performance_valid&&!pfd_guidance.vls_knots);
    r(pfd_input_refs[PI_SPOILER_HANDLE])->value=0;r(pfd_input_refs[PI_SPOILER_ACTUAL])->value=.001;tick(1);
    assert(!pfd_guidance.clean_performance_valid&&!pfd_guidance.vls_knots);
    r(pfd_input_refs[PI_SPOILER_ACTUAL])->value=0;r(pfd_input_refs[PI_SPOILER_HANDLE])->value=-.5;tick(1);
    assert(pfd_guidance.clean_performance_valid&&pfd_guidance.vls_knots>0&&pfd_guidance.spoilers_armed);
    refs.side[0].air_valid=0;tick(1);assert(!pfd_guidance.clean_performance_valid&&!pfd_guidance.vls_knots);
    refs.side[0].air_valid=1;r(fcu.weight)->value=249999;tick(1);
    assert(!pfd_guidance.clean_performance_valid&&!pfd_guidance.vls_knots);
    r(fcu.weight)->value=400000;r(fcu.pressure_altitude)->value=43001;tick(1);
    assert(!pfd_guidance.clean_performance_valid&&!pfd_guidance.vls_knots);
    r(fcu.pressure_altitude)->value=10000;r(pfd_input_refs[PI_FLAP_FAILURE])->value=6;tick(1);
    assert(!pfd_guidance.clean_performance_valid&&!pfd_guidance.vls_knots);
    r(pfd_input_refs[PI_FLAP_FAILURE])->value=0;tick(1);
    assert(pfd_guidance.clean_performance_valid&&pfd_guidance.vls_knots==189);
    assert(XPLMGetDatai(XPLMFindDataRef("a380x/xp12/pfd/clean_performance_valid"))==1);
    fcu_shutdown();
}
static void test_raw_sensor_and_radio_contracts(void) {
    setup();assert(fcu_init());r(fcu.magvar)->value=7;r(refs.ground_track)->value=101;
    refs.side[1].ias_value=260;refs.side[1].heading_value=120;guidance.ap2=1;
    r(refs.side[0].baro)->value=30;r(refs.side[1].baro)->value=29;
    r(refs.side[1].baro_standard)->value=1;r(refs.side[1].nav_loc)->value=.5;r(refs.side[1].nav_gs)->value=-.7;
    r(fcu.g_axial)->value=-.1;r(fcu.g_side)->value=.2;r(fcu.g_normal)->value=1.3;
    read_guidance_inputs(.1f);
    assert(guidance_inputs.ias_knots==260&&guidance_inputs.heading_deg==120);
    assert(fabs(guidance_inputs.true_heading_deg-127)<.001&&fabs(guidance_inputs.track_deg-94)<.001);
    assert(fabs(guidance_inputs.pressure_altitude_ft-10020.125)<.001);
    assert(fabs(guidance_inputs.accel_x_m_s2-.981)<.001&&fabs(guidance_inputs.accel_y_m_s2-1.962)<.001);
    assert(fabs(guidance_inputs.accel_z_m_s2-2.943)<.001);
    assert(fabs(refs.side[0].baro_hpa_value-1015.9166)<.01&&refs.side[1].baro_std);
    assert(guidance_inputs.ils[1].gs_valid&&fabs(guidance_inputs.ils[1].gs_deviation_dots-.7)<.001);
    assert(fabs(guidance_inputs.ils[1].gs_angle_deg-3.2)<.001);
    assert(!strcmp(refs.side[1].nav_ident,"ITEST")&&fabs(refs.side[1].nav_frequency_mhz-110.30)<.001);
    assert(refs.side[1].nav_dme_valid&&fabs(refs.side[1].nav_dme_nm-12.5)<.001);
    refs.side[1].mach_value=.89f;r(fcu.mmo)->value=.89;read_guidance_inputs(.1f);
    assert(fabs(guidance_inputs.vmax_knots-260)<.001);
    r(fcu.mmo)->value=.85;read_guidance_inputs(.1f);assert(guidance_inputs.vmax_knots<260);
    r(fcu.mmo)->value=0;read_guidance_inputs(.1f);assert(guidance_inputs.vmax_knots==340);
    r(refs.side[1].nav_type)->value=4;r(refs.side[1].nav_frequency)->value=11390;read_guidance_inputs(.1f);assert(!guidance_inputs.ils[1].loc_valid&&!guidance_inputs.ils[1].gs_valid);
    r(refs.side[1].nav_frequency)->value=11020;read_guidance_inputs(.1f);assert(!guidance_inputs.ils[1].loc_valid);
    r(refs.side[1].nav_frequency)->value=11030;r(refs.side[1].nav_type)->value=8;read_guidance_inputs(.1f);assert(guidance_inputs.ils[1].loc_valid);
    r(refs.side[1].nav_type)->value=4;r(fcu.gs_angle[1])->value=NAN;read_guidance_inputs(.1f);assert(!guidance_inputs.ils[1].gs_valid);
    r(refs.side[1].nav_course)->value=NAN;read_guidance_inputs(.1f);assert(!guidance_inputs.ils[1].loc_valid);
    r(refs.side[1].nav_has_dme)->value=0;read_guidance_inputs(.1f);assert(!refs.side[1].nav_dme_valid&&guidance_inputs.ils[1].distance_nm==0);
    r(fcu.g_normal)->value=NAN;read_guidance_inputs(.1f);assert(!guidance_inputs.attitude_valid);
    fcu_shutdown();
}
static void test_override_lifecycles(void) {
    setup();assert(fcu_init());tick(70);refs.side[1].attitude_valid=0;command("ap2_toggle");assert(!guidance.ap2);refs.side[1].attitude_valid=1;engage();assert(command_once_count==2);
    command("athr_toggle");tick(1);assert(guidance.athr_active&&fcu.own_throttle);
    command("athr_disconnect");assert(fcu.own_pitch&&fcu.own_roll&&!fcu.own_throttle);
    command("athr_toggle");tick(1);command("ap_disconnect");assert(!fcu.own_pitch&&!fcu.own_roll&&fcu.own_throttle);
    command("ap1_toggle");tick(1);assert(guidance.ap1);
    r(fcu.paused)->value=1;tick(1);assert(!fcu.own_pitch&&!fcu.own_roll&&!fcu.own_throttle);
    r(fcu.paused)->value=0;tick(1);assert(fcu.own_pitch&&fcu.own_roll&&fcu.own_throttle);
    r(fcu.replay)->value=1;tick(1);assert(!fcu.own_pitch&&!fcu.own_roll&&!fcu.own_throttle);
    r(fcu.replay)->value=0;tick(1);
    refs.side[0].attitude_valid=0;tick(1);assert(!guidance.ap1&&!fcu.own_pitch&&!fcu.own_roll&&!fcu.own_throttle);
    fcu_shutdown();
    setup();assert(fcu_init());engage();r(fcu.override_roll)->value=0;tick(1);assert(!guidance.ap1&&!fcu.own_pitch&&!fcu.own_roll);
    command("athr_toggle");tick(1);assert(fcu.own_throttle);r(fcu.override_throttle)->value=0;tick(1);assert(!guidance.athr_armed&&!guidance.athr_active&&!fcu.own_throttle);
    r(fcu.override_throttle)->value=1;command("athr_toggle");assert(!guidance.athr_armed);
    guidance.athr_armed=1;tick(1);assert(!guidance.athr_active&&XPLMGetDatai(fcu.override_throttle)==1);
    r(fcu.override_pitch)->value=1;command("ap1_toggle");assert(!guidance.ap1);
    fcu_shutdown();assert(XPLMGetDatai(fcu.override_pitch)==1&&XPLMGetDatai(fcu.override_throttle)==1);
}
static void test_hardware_and_disconnect(void) {
    setup();assert(fcu_init());r(fcu.axis_assignments)->array_i[0]=1;
    r(fcu.axis_minimum)->array_f[0]=.1f;r(fcu.axis_maximum)->array_f[0]=.5f;r(fcu.axis_values)->array_f[0]=.3f;
    engage();assert(!pilot_override());r(fcu.axis_values)->array_f[0]=.45f;tick(1);assert(!guidance.ap1&&!fcu.own_pitch);
    r(fcu.axis_values)->array_f[0]=.3f;command("ap1_toggle");tick(1);assert(guidance.ap1);
    XPLMCommandOnce(fcu.native_disconnect);assert(!guidance.ap1&&!fcu.own_pitch&&!fcu.own_roll);
    command("ap1_toggle");tick(1);r(refs.avionics_power)->value=0;tick(1);assert(!guidance.ap1&&!guidance.athr_armed&&!fcu.own_pitch);
    fcu_shutdown();
}
int main(int argc,char **argv) {
    int i;
    if(argc>1&&!strcmp(argv[1],"--contracts")){
        setup();assert(fcu_init());
        for(i=0;i<ARRAY_LEN(fcu_commands);++i)printf("C %s\n",fcu_commands[i].name);
        for(i=0;i<ARRAY_LEN(fcu_outputs);++i)printf("D %s\n",fcu_outputs[i].name);
        for(i=0;i<ARRAY_LEN(pfd_outputs);++i)printf("P %s\n",pfd_outputs[i].name);
        for(i=0;i<ref_count;++i)if(!strncmp(mock_refs[i].name,"sim/",4))printf("R %s\n",mock_refs[i].name);
        fcu_shutdown();return 0;
    }
    test_registry_and_partial_init();test_raw_sensor_and_radio_contracts();test_override_lifecycles();test_hardware_and_disconnect();
    test_glareshield_lighting();
    test_display_unit_controls();
    test_pfd_aircraft_inputs();
    test_clean_performance_boundary();
    puts("a380x FCU XPLM adapter tests passed");return 0;
}
