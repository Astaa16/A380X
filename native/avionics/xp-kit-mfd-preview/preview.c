/* SPDX-License-Identifier: GPL-3.0-or-later
 * Isolated web-review harness. No XPLM/OpenGL, no shared memory with X-Plane.
 * Same page, widget, entry and FMS core as the aircraft; preview inputs only.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "a380x_fms.h"
#include "a380x_fms_fpln.h"
#include "a380x_mfd.h"
#include "a380x_mfd_page_data.h"
#include "a380x_mfd_page_fpln.h"
#include "a380x_mfd_page_position.h"
#include "a380x_mfd_page_surv.h"
#include "a380x_xpkit_shared.h"

static A380XNavdata *fmsa_nav;
static A380XFlightPlans *fmsa_fpln;
static A380XFplnAircraft fmsa_aircraft;
static double fmsa_nan(void) { return NAN; }
/* Build-time extraction of the unchanged simulator-independent plan hooks. */
#include "preview_fms_hooks.inc"
void a380x_mfd_pages_register(void);
static A380XFms fms;
static A380XMfd mfd;
static A380XMfdInputs ui_in;
static A380XFmsInputs inputs;
static A380XXpKitMfd packets[2];
static double preview_time;
static double penalty;
static struct { unsigned key; double value; } systems[128];
static int system_count;

static void status(void *ctx,A380XMfdStatusData *out) {
    (void)ctx;memset(out,0,sizeof(*out));
    snprintf(out->airframe,sizeof(out->airframe),"A380-800");
    snprintf(out->engine,sizeof(out->engine),"TRENT 972");
    const A380XNavCycle *cycle=a380x_nav_cycle(fmsa_nav);
    if(cycle&&cycle->valid) {
        snprintf(out->navdb_serial,sizeof(out->navdb_serial),"XP%s0001",cycle->airac);
        mfd_airac_cycle_dates(cycle->airac,out->navdb_active,sizeof(out->navdb_active));
        snprintf(out->navdb_second,sizeof(out->navdb_second),"%s",out->navdb_active);
    }
    out->fuel_penalty_has=1;out->fuel_penalty_pct=penalty;out->fuel_penalty_pilot=penalty>0;
}
static int set_penalty(void *ctx,int has,double pct,MfdFmsError *err) {
    (void)ctx;
    if(has&&(!isfinite(pct)||pct<0||pct>99.9)) {err->type=MFD_ERR_ENTRY_OUT_OF_RANGE;return 0;}
    penalty=has?pct:0;return 1;
}
static int airport_page(void *ctx,const char *icao,A380XMfdAirport *out) {
    (void)ctx;const A380XNavAirport *apt=a380x_nav_airport(fmsa_nav,icao);
    if(!apt)return 0;
    memset(out,0,sizeof(*out));
    snprintf(out->icao,sizeof(out->icao),"%.5s",icao);
    snprintf(out->name,sizeof(out->name),"%s",apt->info.name);
    out->lat=apt->info.lat;out->lon=apt->info.lon;
    out->runway_count=apt->runway_count<A380X_MFD_MAX_RUNWAYS?apt->runway_count:A380X_MFD_MAX_RUNWAYS;
    for(int i=0;i<out->runway_count;++i) {
        const A380XNavRunway *r=&apt->runways[i];A380XMfdRunway *d=&out->rwy[i];
        snprintf(d->ident,sizeof(d->ident),"%.5s",r->designator);
        d->lat=r->thr_lat;d->lon=r->thr_lon;d->elevation_ft=r->thr_elev_ft;
        d->length_m=r->length_ft*.3048f;d->course_deg=r->true_brg;
    }
    return 1;
}
static double system_read(void *ctx,MfdSide side,unsigned key) {
    (void)ctx;(void)side;
    switch(key) {
        case A380X_POS_K_FM_VALID:case A380X_POS_K_GPS_PRIMARY:
        case A380X_POS_K_GPS1_VALID:case A380X_POS_K_GPS2_VALID:case A380X_POS_K_ON_GROUND:return 1;
        case A380X_POS_K_FM_LAT:return inputs.lat_deg;
        case A380X_POS_K_FM_LON:return inputs.lon_deg;
        case A380X_POS_K_EPU_NM:return .05;
        case A380X_POS_K_RNP_NM:return 1;
        case A380X_POS_K_UTC_S:return inputs.utc_seconds;
        case A380X_SURV_K_ON_GROUND:return 1;
        case A380X_SURV_K_XPDR_CODE:return 2000;
        default:break;
    }
    for(int i=0;i<system_count;++i)if(systems[i].key==key)return systems[i].value;
    return NAN; /* Unknown simulated systems stay explicitly unavailable. */
}
static void system_command(void *ctx,MfdSide side,unsigned key,double value) {
    (void)ctx;(void)side;
    for(int i=0;i<system_count;++i)if(systems[i].key==key){systems[i].value=value;return;}
    if(system_count<128){systems[system_count].key=key;systems[system_count++].value=value;}
}

void preview_tick(double dt) {
    if(dt<.001)dt=.001;if(dt>1)dt=1;preview_time+=dt;
    inputs.utc_seconds=46800+preview_time;
    a380x_fms_update(&fms,&inputs,dt);
    ui_in.now=preview_time;ui_in.tmpy_exists=a380x_fpln_has_tmpy(fmsa_fpln);
    ui_in.flight_phase=a380x_fms_phase(&fms);
    A380XMfdFplnEnv env={0};env.plans=fmsa_fpln;env.aircraft_valid=1;env.aircraft=fmsa_aircraft;
    a380x_mfd_fpln_set_env(&env);
    a380x_mfd_frame(&mfd,&ui_in);
}
void preview_reset(int example) {
    a380x_fpln_reset(fmsa_fpln);
    A380XFmsPlanHooks hooks;fmsa_plan_hooks(&hooks);
    A380XFmsNavHooks nav={NULL,fmsa_fms_airport};
    a380x_fms_init(&fms,NULL,&nav,&hooks);
    memset(&inputs,0,sizeof(inputs));
    for(int i=0;i<3;++i)inputs.fmc_powered[i]=1;
    inputs.switching_knob=1;inputs.position_valid=inputs.gps_primary=inputs.irs_aligned=1;
    inputs.lat_deg=51.4775;inputs.lon_deg=-.4614;inputs.on_ground=1;
    inputs.adr_valid=inputs.sat_valid=inputs.static_pressure_valid=inputs.radio_alt_valid=1;
    inputs.sat_c=15;inputs.static_pressure_hpa=1013.25;inputs.baro_alt_ft=inputs.pressure_alt_ft=83;
    inputs.fqms_fob_valid=inputs.fqms_gw_valid=inputs.fqms_cg_valid=1;
    inputs.fqms_fob_kg=100000;inputs.fqms_gw_kg=460000;inputs.fqms_cg_pct=31;
    inputs.fcu_alt_valid=1;inputs.fcu_selected_alt_ft=10000;
    inputs.fcu_selected_speed_kt=250;inputs.cold_and_dark_spawn=0;
    memset(&ui_in,0,sizeof(ui_in));ui_in.du_on[0]=ui_in.du_on[1]=1;
    ui_in.fms_knob=1;ui_in.fmc_a_healthy=ui_in.fmc_b_healthy=1;ui_in.metric_units=1;
    A380XMfdSystemsHost host={0};host.read_status=status;host.airport_lookup=airport_page;
    host.set_fuel_penalty=set_penalty;host.read_value=system_read;host.command=system_command;
    a380x_mfd_init(&mfd,&fms,&fms.msgs,&host);
    penalty=0;system_count=0;preview_time=0;
    memset(&fmsa_aircraft,0,sizeof(fmsa_aircraft));fmsa_aircraft.valid=1;
    fmsa_aircraft.lat=inputs.lat_deg;fmsa_aircraft.lon=inputs.lon_deg;fmsa_aircraft.on_ground=1;
    fmsa_aircraft.alt_ft=83;fmsa_aircraft.true_track_deg=270;
    a380x_mfd_fpln_reset_revised(MFD_CAPT);a380x_mfd_fpln_reset_revised(MFD_FO);
    preview_tick(.3);
    if(example) {
        MfdFmsError error={0};
        a380x_fms_enter_text(&fms,0,A380X_FMS_T_FROM,"EGLL",&error);
        a380x_fms_enter_text(&fms,0,A380X_FMS_T_TO,"LFPG",&error);
        preview_tick(.3);
        a380x_fpln_set_origin_runway(fmsa_fpln,0,0,"27R");
        a380x_fpln_set_sid(fmsa_fpln,0,0,"DET2F");
        if(a380x_fpln_has_tmpy(fmsa_fpln))a380x_fms_tmpy_begin(&fms);
        a380x_fms_tmpy_insert(&fms);preview_tick(.3);
        a380x_fms_enter(&fms,0,A380X_FMS_F_ZFW,360000,&error);
        a380x_fms_enter(&fms,0,A380X_FMS_F_ZFWCG,31,&error);
        a380x_fms_enter(&fms,0,A380X_FMS_F_BLOCK,100000,&error);
        a380x_fms_enter(&fms,0,A380X_FMS_F_CRZ_FL,350,&error);
        a380x_fms_enter(&fms,0,A380X_FMS_F_CI,25,&error);
        preview_tick(.3);
    }
    a380x_mfd_navigate(&mfd,MFD_CAPT,"fms/data/status");
    a380x_mfd_navigate(&mfd,MFD_FO,"fms/active/perf");
    preview_tick(.1);preview_tick(.1);
}
int preview_init(const char *root) {
    A380XNavdataConfig cfg={0};cfg.xplane_root=root;
    fmsa_nav=a380x_nav_create(&cfg,NULL);if(!fmsa_nav)return 0;
    if(a380x_nav_load_global(fmsa_nav))return 0;
    fmsa_fpln=a380x_fpln_create(fmsa_nav);if(!fmsa_fpln)return 0;
    a380x_mfd_pages_register();preview_reset(1);return 1;
}
void preview_event(int side,int type,float x,float y,int amount,const char *key_name) {
    if(side<0||side>1)return;
    switch(type) {
        case 0:mfd_pointer_move(&mfd,(MfdSide)side,x,y);break;
        case 1:mfd_pointer_button(&mfd,(MfdSide)side,1);break;
        case 2:mfd_pointer_button(&mfd,(MfdSide)side,0);break;
        case 3:mfd_pointer_wheel(&mfd,(MfdSide)side,amount);break;
        case 4:mfd_pointer_leave(&mfd,(MfdSide)side);break;
        case 5:{MfdKey key;char ch;if(mfd_key_from_name(key_name,&key,&ch))mfd_key(&mfd,(MfdSide)side,key,ch);break;}
    }
    preview_tick(.01);
}
void preview_navigate(int side,const char *uri) {if(side>=0&&side<2)a380x_mfd_navigate(&mfd,(MfdSide)side,uri);preview_tick(.01);}
const A380XXpKitMfd *preview_frame(int side) {
    if(side<0||side>1)return NULL;
    A380XXpKitMfd *out=&packets[side];
    out->powered=ui_in.du_on[side];
    snprintf(out->uri,sizeof(out->uri),"%s",a380x_mfd_active_uri(&mfd,(MfdSide)side));
    memcpy(&out->list,a380x_mfd_display_list(&mfd,(MfdSide)side),sizeof(out->list));
    atomic_fetch_add(&out->sequence,2);return out;
}
unsigned preview_menu(int side){return side>=0&&side<2?mfd.sides[side].ui.open_menu:0;}
int preview_focus(int side){return side>=0&&side<2?a380x_mfd_wants_keyboard(&mfd,(MfdSide)side):0;}
void preview_power(int side,int on){if(side>=0&&side<2)ui_in.du_on[side]=on!=0;preview_tick(.01);}
double preview_value(const char *name) {
    static const struct {const char *name;A380XFmsField field;} fields[]={
        {"cost_index",A380X_FMS_F_CI},{"cruise_fl",A380X_FMS_F_CRZ_FL},{"zfw_kg",A380X_FMS_F_ZFW},
        {"block_kg",A380X_FMS_F_BLOCK},{"v1",A380X_FMS_F_V1},{"vr",A380X_FMS_F_VR},{"v2",A380X_FMS_F_V2},
        {"flaps",A380X_FMS_F_TO_FLAPS},{"packs",A380X_FMS_F_PACKS},{"anti_ice",A380X_FMS_F_ANTI_ICE}};
    for(size_t i=0;i<sizeof(fields)/sizeof(fields[0]);++i)if(!strcmp(name,fields[i].name)) {
        A380XFmsValue v=a380x_fms_get(&fms,a380x_fms_loaded_plan(&fms,0),fields[i].field);return v.has?v.v:NAN;
    }
    return NAN;
}
