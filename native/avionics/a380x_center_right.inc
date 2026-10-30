/* SPDX-License-Identifier: GPL-3.0-or-later
 * Center-right gear/brake controls backed by X-Plane's landing-gear,
 * anti-skid and autobrake systems.
 */
#include "a380x_elec_power.h"
typedef struct {
    int gear_lever, gear_nose, gear_wing, gear_body, gear_arrow;
    int gear_not_down_warning;
    int gear_not_locked_down_warning, gear_not_locked_up_warning;
    int gear_nose_green, gear_nose_red, gear_wing_green, gear_wing_red;
    int gear_body_green, gear_body_red;
    int gear_wing_left_green, gear_wing_left_red;
    int gear_wing_right_green, gear_wing_right_red;
    int gear_body_left_green, gear_body_left_red;
    int gear_body_right_green, gear_body_right_red;
    int body_accu_psi, body_accu_zone;
    int autobrake_selected, autobrake_armed, rto_arm, antiskid_on;
    int rto_pressed, btv_reverted;
    int normal_brakes_available, autobrake_available, antiskid_available;
    int emergency_braking, autobrake_active;
    int reinflate_pressed, reinflate_active, reinflate_owns_pump;
    int brake_left_ratio_milli, brake_right_ratio_milli;
    int autobrake_was_active, decel_owned;
    float original_decels[4];
    int abs_initial;
    int previous_gear_lever;
    /* L/G GRVTY (a380x_lg_grvty.inc): while gravity extension isolates the gear hydraulics the
     * plugin holds X-Plane's gear handle and the physical lever is owned_lever_down. */
    int lever_owned, owned_lever_down;
    float gear_selection_elapsed;
    XPLMDataRef lever_ref, autobrake_ref, abs_ref, accumulator_ref;
    XPLMDataRef hydraulic_ref[2], pedal_ref[2], brake_failure_ref[2];
    XPLMDataRef electric_pump_ref, wheel_brake_ref, spoiler_ref, park_ref, decel_ref;
} CenterRightState;
static CenterRightState center_right;

typedef struct { const char *name; int *value; XPLMDataRef handle; } CenterRightRef;
#define CR_REF(name, field) {"a380x/xp12/center_right/" name, &center_right.field, NULL}
static CenterRightRef center_right_refs[] = {
    CR_REF("gear_lever",gear_lever), CR_REF("gear_nose",gear_nose),
    CR_REF("gear_wing",gear_wing), CR_REF("gear_body",gear_body),
    CR_REF("gear_arrow",gear_arrow), CR_REF("body_accu_psi",body_accu_psi),
    CR_REF("gear_not_down_warning",gear_not_down_warning),
    CR_REF("gear_not_locked_down_warning",gear_not_locked_down_warning),
    CR_REF("gear_not_locked_up_warning",gear_not_locked_up_warning),
    CR_REF("gear_nose_green",gear_nose_green), CR_REF("gear_nose_red",gear_nose_red),
    CR_REF("gear_wing_green",gear_wing_green), CR_REF("gear_wing_red",gear_wing_red),
    CR_REF("gear_body_green",gear_body_green), CR_REF("gear_body_red",gear_body_red),
    CR_REF("gear_wing_left_green",gear_wing_left_green),
    CR_REF("gear_wing_left_red",gear_wing_left_red),
    CR_REF("gear_wing_right_green",gear_wing_right_green),
    CR_REF("gear_wing_right_red",gear_wing_right_red),
    CR_REF("gear_body_left_green",gear_body_left_green),
    CR_REF("gear_body_left_red",gear_body_left_red),
    CR_REF("gear_body_right_green",gear_body_right_green),
    CR_REF("gear_body_right_red",gear_body_right_red),
    CR_REF("body_accu_zone",body_accu_zone),
    CR_REF("autobrake_selected",autobrake_selected),
    CR_REF("autobrake_armed",autobrake_armed), CR_REF("rto_arm",rto_arm),
    CR_REF("antiskid_on",antiskid_on),
    CR_REF("rto_pressed",rto_pressed), CR_REF("btv_reverted",btv_reverted),
    CR_REF("normal_brakes_available",normal_brakes_available),
    CR_REF("autobrake_available",autobrake_available),
    CR_REF("antiskid_available",antiskid_available),
    CR_REF("emergency_braking",emergency_braking),
    CR_REF("autobrake_active",autobrake_active),
    CR_REF("brake_left_ratio_milli",brake_left_ratio_milli),
    CR_REF("brake_right_ratio_milli",brake_right_ratio_milli),
    CR_REF("reinflate_pressed",reinflate_pressed),
    CR_REF("reinflate_active",reinflate_active),
};
#undef CR_REF
static int center_right_get(void *refcon)
{
    return *(((CenterRightRef *)refcon)->value);
}

typedef struct { const char *name; int action; XPLMCommandRef handle; } CenterRightCommand;
enum { CR_GEAR, CR_AUTOBRAKE_UP, CR_AUTOBRAKE_DOWN, CR_RTO, CR_ANTISKID, CR_REINFLATE };
#define CR_CMD(name, action) {"a380x/xp12/center_right/" name, action, NULL}
static CenterRightCommand center_right_commands[] = {
    CR_CMD("gear_toggle",CR_GEAR),
    CR_CMD("autobrake_up",CR_AUTOBRAKE_UP),
    CR_CMD("autobrake_down",CR_AUTOBRAKE_DOWN),
    CR_CMD("rto_toggle",CR_RTO),
    CR_CMD("antiskid_toggle",CR_ANTISKID),
    CR_CMD("accus_reinflate",CR_REINFLATE),
};
#undef CR_CMD

static int center_right_native_brake_mode(int selected)
{
    /* X-Plane has four landing deceleration levels. BTV uses its HI level
     * until a runway-exit guidance source is available in the port. */
    static const int modes[6]={1,5,2,3,4,5};
    return modes[selected];
}
static float center_right_ratio(float value)
{
    return isfinite(value) ? fmaxf(0.0f,fminf(1.0f,value)) : 0.0f;
}
static void center_right_update(float dt);
static void center_right_disarm(void)
{
    XPLMSetDatai(center_right.autobrake_ref,1);
    center_right.autobrake_selected=0;
    center_right.autobrake_armed=0;
    center_right.rto_arm=0;
    center_right.btv_reverted=0;
    center_right.autobrake_active=0;
    center_right.autobrake_was_active=0;
}
static int center_right_command_handler(XPLMCommandRef command,
                                         XPLMCommandPhase phase, void *refcon)
{
    CenterRightCommand *item=(CenterRightCommand *)refcon;
    (void)command;
    if (phase==xplm_CommandEnd) {
        if (item->action==CR_RTO) center_right.rto_pressed=0;
        if (item->action==CR_REINFLATE) center_right.reinflate_pressed=0;
        return 1;
    }
    if (!plugin_enabled || phase!=xplm_CommandBegin) return 1;
    /* Refresh conditions at the click, rather than accepting a stale armed
     * state between flight-loop callbacks. */
    center_right_update(0.0f);
    switch (item->action) {
    case CR_GEAR:
        /* Hydraulics isolated by L/G GRVTY: the lever moves without commanding the gear. */
        if (center_right.lever_owned) center_right.owned_lever_down=!center_right.owned_lever_down;
        else XPLMSetDatai(center_right.lever_ref,!XPLMGetDatai(center_right.lever_ref));
        break;
    case CR_AUTOBRAKE_UP: case CR_AUTOBRAKE_DOWN:
        center_right.autobrake_selected+=item->action==CR_AUTOBRAKE_UP ? 1 : -1;
        if (center_right.autobrake_selected<0) center_right.autobrake_selected=0;
        if (center_right.autobrake_selected>5) center_right.autobrake_selected=5;
        center_right.rto_arm=0;
        center_right.autobrake_was_active=0;
        if (!center_right.autobrake_available) center_right_disarm();
        else XPLMSetDatai(center_right.autobrake_ref,
                     center_right_native_brake_mode(center_right.autobrake_selected));
        break;
    case CR_RTO:
        center_right.rto_pressed=1;
        if (center_right.rto_arm) center_right_disarm();
        else if (XPLMGetDatai(refs.on_ground) && center_right.autobrake_available) {
            center_right.autobrake_selected=0;
            center_right.autobrake_was_active=0;
            XPLMSetDatai(center_right.autobrake_ref,0);
        }
        break;
    case CR_ANTISKID:
        XPLMSetDatai(center_right.abs_ref,!XPLMGetDatai(center_right.abs_ref));
        break;
    case CR_REINFLATE:
        center_right.reinflate_pressed=1;
        /* The port has one native accumulator. Request its configured
         * electric pump and let X-Plane charge it; never write fake pressure.
         * A pump that was already on is left on at completion. */
        if (center_right.body_accu_psi<4872 && !center_right.reinflate_active) {
            center_right.reinflate_owns_pump=!XPLMGetDatai(center_right.electric_pump_ref);
            XPLMSetDatai(center_right.electric_pump_ref,1);
            center_right.reinflate_active=1;
        }
        break;
    default: break;
    }
    center_right_update(0.0f);
    return 1;
}
static int center_right_gear_state(int first, int last)
{
    int i, down=1, up=1;
    for (i=first;i<=last;++i) {
        down=down && gear_values[i]>=0.99f;
        up=up && gear_values[i]<=0.01f;
    }
    if (center_right.gear_lever && down) return 1;
    if (!center_right.gear_lever && up) return 0;
    return 2;
}
static void center_right_update(float dt)
{
    float pressure=XPLMGetDataf(center_right.accumulator_ref);
    float hydraulic_pressure=0.0f, pedal[2], wheel_brakes[5]={0};
    int native_mode, inertial_available, spoilers_deployed, on_ground;
    int all_down=1, all_up=1, below_750, i;
    center_right.gear_lever=center_right.lever_owned ? center_right.owned_lever_down!=0
                                                     : XPLMGetDatai(center_right.lever_ref)!=0;
    if (center_right.gear_lever!=center_right.previous_gear_lever) {
        center_right.previous_gear_lever=center_right.gear_lever;
        center_right.gear_selection_elapsed=0.0f;
    } else if (isfinite(dt) && dt>0.0f) {
        center_right.gear_selection_elapsed+=fminf(dt,5.0f);
    }
    for (i=0;i<5;++i) {
        if (!isfinite(gear_values[i]) || gear_values[i]<0.99f) all_down=0;
        if (!isfinite(gear_values[i]) || gear_values[i]>0.01f) all_up=0;
    }
    center_right.gear_nose=center_right_gear_state(0,0);
    center_right.gear_wing=center_right_gear_state(1,2);
    center_right.gear_body=center_right_gear_state(3,4);
    center_right.gear_nose_green=center_right.gear_nose==1;
    center_right.gear_nose_red=center_right.gear_nose==2;
    center_right.gear_wing_green=center_right.gear_wing==1;
    center_right.gear_wing_red=center_right.gear_wing==2;
    center_right.gear_body_green=center_right.gear_body==1;
    center_right.gear_body_red=center_right.gear_body==2;
    center_right.gear_wing_left_green=center_right_gear_state(1,1)==1;
    center_right.gear_wing_left_red=center_right_gear_state(1,1)==2;
    center_right.gear_wing_right_green=center_right_gear_state(2,2)==1;
    center_right.gear_wing_right_red=center_right_gear_state(2,2)==2;
    center_right.gear_body_left_green=center_right_gear_state(3,3)==1;
    center_right.gear_body_left_red=center_right_gear_state(3,3)==2;
    center_right.gear_body_right_green=center_right_gear_state(4,4)==1;
    center_right.gear_body_right_red=center_right_gear_state(4,4)==2;
    /* The package ACF specifies a 20-second gear cycle for all five gear.
     * Red UNLK is immediate; the ECAM lock alert follows that normal delay. */
    center_right.gear_not_locked_down_warning=center_right.gear_lever &&
        !all_down && center_right.gear_selection_elapsed>=20.0f;
    center_right.gear_not_locked_up_warning=!center_right.gear_lever &&
        !all_up && center_right.gear_selection_elapsed>=20.0f;
    below_750=(refs.side[0].radio_altitude_valid &&
        refs.side[0].radio_altitude_value<750.0f) ||
        (refs.side[1].radio_altitude_valid &&
        refs.side[1].radio_altitude_value<750.0f);
    center_right.gear_not_down_warning=flap_value>=0.5f && below_750 && !all_down;
    center_right.gear_arrow=center_right.gear_not_down_warning &&
        fmodf(glare.flash_time,1.0f)<0.5f;
    if (!isfinite(pressure)) pressure=0.0f;
    center_right.body_accu_psi=(int)lroundf(fmaxf(0.0f,fminf(1.0f,pressure))*5000.0f);
    center_right.body_accu_zone=center_right.body_accu_psi>=4872 ? 2 :
        center_right.body_accu_psi<=3840 ? 0 : 1;
    if (center_right.reinflate_active &&
        (center_right.body_accu_psi>=4872 || !XPLMGetDatai(center_right.electric_pump_ref))) {
        if (center_right.reinflate_owns_pump)
            XPLMSetDatai(center_right.electric_pump_ref,0);
        center_right.reinflate_active=0;
        center_right.reinflate_owns_pump=0;
    }
    for (i=0;i<2;++i) {
        float value=XPLMGetDataf(center_right.hydraulic_ref[i]);
        if (isfinite(value)) hydraulic_pressure=fmaxf(hydraulic_pressure,value);
        pedal[i]=center_right_ratio(XPLMGetDataf(center_right.pedal_ref[i]));
    }
    /* FBW A380 BCU normal-brake hysteresis (hydraulic/mod.rs). The native
     * scaffold uses 3000-PSI engine systems 1/2, so its pressure units already
     * match these thresholds. The full three-ADIRS alignment computer is not
     * ported: a powered, valid raw attitude/heading channel is used here. */
    if (hydraulic_pressure>2176.0f && pedal[0]<0.2f && pedal[1]<0.2f)
        center_right.normal_brakes_available=1;
    else if (hydraulic_pressure<1305.0f)
        center_right.normal_brakes_available=0;
    if (XPLMGetDatai(center_right.brake_failure_ref[0])>=6 ||
        XPLMGetDatai(center_right.brake_failure_ref[1])>=6)
        center_right.normal_brakes_available=0;
    center_right.antiskid_on=XPLMGetDatai(center_right.abs_ref)!=0;
    center_right.emergency_braking=!center_right.antiskid_on;
    center_right.antiskid_available=center_right.antiskid_on &&
        (center_right.normal_brakes_available || center_right.body_accu_psi>1305);
    /* Autobrake panel: AC ESS 400XP (spec 6.4) with ADIRS attitude/heading. */
    inertial_available=a380x_power_bus(A380X_ELN_AC_ESS, XPLMGetDatai(refs.avionics_power)) &&
        ((refs.side[0].attitude_valid && refs.side[0].heading_valid) ||
         (refs.side[1].attitude_valid && refs.side[1].heading_valid));
    center_right.autobrake_available=center_right.antiskid_on &&
        center_right.normal_brakes_available && inertial_available;
    on_ground=XPLMGetDatai(refs.on_ground)!=0;
    native_mode=XPLMGetDatai(center_right.autobrake_ref);
    if (!center_right.autobrake_available || (native_mode==0 && !on_ground) ||
        native_mode<0 || native_mode>5) {
        center_right_disarm();
        native_mode=1;
    }
    center_right.rto_arm=native_mode==0;
    if (center_right.rto_arm || native_mode==1) {
        center_right.autobrake_selected=0;
        if (native_mode==1) center_right.autobrake_was_active=0;
    } else if (native_mode>=2 && native_mode<=5 &&
               (center_right.autobrake_selected==0 ||
                native_mode!=center_right_native_brake_mode(center_right.autobrake_selected))) {
        /* Keep BTV on its physical detent while the native brake uses HI. */
        center_right.autobrake_selected=native_mode;
    }
    center_right.btv_reverted=center_right.autobrake_selected==1 && native_mode==5;
    /* This is the effective mode for FMA. BTV's physical detent must not
     * advertise exit guidance: until OANS/BTV is ported it reverts to BRK HI. */
    center_right.autobrake_armed=center_right.rto_arm ? 6 : native_mode>1 ? native_mode : 0;
    XPLMGetDatavf(center_right.wheel_brake_ref,wheel_brakes,0,5);
    center_right.brake_left_ratio_milli=(int)lroundf(center_right_ratio(wheel_brakes[3])*1000.0f);
    center_right.brake_right_ratio_milli=(int)lroundf(center_right_ratio(wheel_brakes[4])*1000.0f);
    spoilers_deployed=XPLMGetDataf(center_right.spoiler_ref)>0.05f;
    /* X-Plane publishes actual wheel braking but no autobrake-active bit.
     * Only illuminate ACTIV when braking is observed with spoilers deployed,
     * on ground, without pedal or parking-brake demand. */
    center_right.autobrake_active=center_right.autobrake_armed && on_ground &&
        spoilers_deployed && pedal[0]<0.01f && pedal[1]<0.01f &&
        XPLMGetDataf(center_right.park_ref)<0.01f &&
        (center_right.brake_left_ratio_milli>10 || center_right.brake_right_ratio_milli>10);
    if (center_right.autobrake_active) center_right.autobrake_was_active=1;
    if (center_right.autobrake_was_active &&
        (!spoilers_deployed ||
         (center_right.rto_arm ?
            (pedal[0]>0.77f || pedal[1]>0.77f || (pedal[0]>0.53f && pedal[1]>0.53f)) :
            (pedal[0]>0.53f || pedal[1]>0.53f || (pedal[0]>0.11f && pedal[1]>0.11f)))))
        center_right_disarm();
}
static void center_right_release_controls(void)
{
    if (center_right.reinflate_active && center_right.reinflate_owns_pump && center_right.electric_pump_ref)
        XPLMSetDatai(center_right.electric_pump_ref,0);
    center_right.reinflate_active=0;
    center_right.reinflate_owns_pump=0;
    center_right.rto_pressed=0;
    center_right.reinflate_pressed=0;
}
static void center_right_shutdown(void)
{
    int i;
    for (i=0;i<ARRAY_LEN(center_right_commands);++i)
        if (center_right_commands[i].handle) {
            XPLMUnregisterCommandHandler(center_right_commands[i].handle,
                center_right_command_handler,1,&center_right_commands[i]);
            center_right_commands[i].handle=NULL;
        }
    for (i=0;i<ARRAY_LEN(center_right_refs);++i)
        if (center_right_refs[i].handle) {
            XPLMUnregisterDataAccessor(center_right_refs[i].handle);
            center_right_refs[i].handle=NULL;
        }
    center_right_release_controls();
    if (center_right.decel_owned && center_right.decel_ref)
        XPLMSetDatavf(center_right.decel_ref,center_right.original_decels,0,4);
    center_right.decel_owned=0;
    if (center_right.abs_ref) XPLMSetDatai(center_right.abs_ref,center_right.abs_initial);
}
static int center_right_init(void)
{
    int i;
    memset(&center_right,0,sizeof(center_right));
    center_right.lever_ref=XPLMFindDataRef("sim/cockpit2/controls/gear_handle_down");
    center_right.autobrake_ref=XPLMFindDataRef("sim/cockpit2/switches/auto_brake_level");
    center_right.abs_ref=XPLMFindDataRef("sim/aircraft/gear/acf_has_abs");
    center_right.accumulator_ref=XPLMFindDataRef(
        "sim/cockpit2/hydraulics/indicators/brake_accumulator_pressure_ratio");
    center_right.hydraulic_ref[0]=XPLMFindDataRef("sim/cockpit2/hydraulics/indicators/hydraulic_pressure_1");
    center_right.hydraulic_ref[1]=XPLMFindDataRef("sim/cockpit2/hydraulics/indicators/hydraulic_pressure_2");
    center_right.pedal_ref[0]=XPLMFindDataRef("sim/cockpit2/controls/left_brake_ratio");
    center_right.pedal_ref[1]=XPLMFindDataRef("sim/cockpit2/controls/right_brake_ratio");
    center_right.brake_failure_ref[0]=XPLMFindDataRef("sim/operation/failures/rel_lbrakes");
    center_right.brake_failure_ref[1]=XPLMFindDataRef("sim/operation/failures/rel_rbrakes");
    center_right.electric_pump_ref=XPLMFindDataRef("sim/cockpit2/hydraulics/actuators/electric_hydraulic_pump_on");
    center_right.wheel_brake_ref=XPLMFindDataRef("sim/flightmodel2/gear/tire_part_brake");
    center_right.spoiler_ref=XPLMFindDataRef("sim/flightmodel/controls/sbrkrat");
    center_right.park_ref=XPLMFindDataRef("sim/cockpit2/controls/wheel_brake_ratio");
    center_right.decel_ref=XPLMFindDataRef("sim/aircraft/specialcontrols/acf_autofbrk_decels");
    if (!center_right.lever_ref || !center_right.autobrake_ref ||
        !center_right.abs_ref || !center_right.accumulator_ref ||
        !center_right.electric_pump_ref || !center_right.wheel_brake_ref ||
        !center_right.spoiler_ref || !center_right.park_ref || !center_right.decel_ref) return 0;
    for (i=0;i<2;++i)
        if (!center_right.hydraulic_ref[i] || !center_right.pedal_ref[i] ||
            !center_right.brake_failure_ref[i]) return 0;
    center_right.abs_initial=XPLMGetDatai(center_right.abs_ref);
    if (XPLMGetDatavf(center_right.decel_ref,center_right.original_decels,0,4)!=4) return 0;
    {
        /* Final landing targets from A380AutobrakeController in the pinned
         * FBW hydraulic/autobrakes.rs. X-Plane consumes positive g units.
         * The native controller supplies engagement/ramp timing and RTO. */
        float decels[4]={2.0f/9.80665f,2.5f/9.80665f,3.0f/9.80665f,3.5f/9.80665f};
        XPLMSetDatavf(center_right.decel_ref,decels,0,4);
        center_right.decel_owned=1;
    }
    center_right.previous_gear_lever=XPLMGetDatai(center_right.lever_ref)!=0;
    {
        int native=XPLMGetDatai(center_right.autobrake_ref);
        center_right.rto_arm=native==0 && XPLMGetDatai(refs.on_ground);
        center_right.autobrake_selected=native>=2 && native<=5 ? native : 0;
    }
    for (i=0;i<ARRAY_LEN(center_right_refs);++i) {
        CenterRightRef *ref=&center_right_refs[i];
        if (XPLMFindDataRef(ref->name)) { center_right_shutdown(); return 0; }
        ref->handle=XPLMRegisterDataAccessor(ref->name,xplmType_Int,0,
            center_right_get,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,ref,NULL);
        if (!ref->handle) { center_right_shutdown(); return 0; }
    }
    for (i=0;i<ARRAY_LEN(center_right_commands);++i) {
        CenterRightCommand *item=&center_right_commands[i];
        /* XPLM retains commands across aircraft reloads; CreateCommand reuses them. */
        item->handle=XPLMCreateCommand(item->name,"A380 center-right control");
        if (!item->handle) { center_right_shutdown(); return 0; }
        XPLMRegisterCommandHandler(item->handle,center_right_command_handler,1,item);
    }
    center_right_update(0.0f);
    return 1;
}
