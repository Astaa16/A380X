/* SPDX-License-Identifier: GPL-3.0-or-later
 * Center-right clock. GPS follows the simulator's UTC clock; INT advances
 * independently from its last setting; SET permits manual calendar editing.
 */
#include <time.h>
#include "a380x_elec_power.h"

typedef struct {
    int mode, et_selector, date_held, set_field;
    int powered, chr_held, rst_held, et_display, chr_display;
    int year, month, day, hour, minute, second;
    int et_seconds, chr_seconds, chr_running;
    float fraction, et_fraction, chr_fraction, flash_time;
    XPLMDataRef zulu_seconds, zulu_days;
    XPLMDataRef battery_volts, bus_volts, paused, replay, sim_speed;
} ClockState;
static ClockState clock_state;

static int clock_has_power(void)
{
    float battery[8]={0}, bus[6]={0};
    int i;
    /* A380 network: the clock is on the HOT 1 bus (spec 6.4, assumption). */
    if (a380x_power.available) return a380x_power_bus(A380X_ELN_HOT1, 0);
    /* The native electrical model has no separate hot-bus dataref. Actual
     * battery voltage models the direct feed even with battery contactors
     * and the avionics master off; either DC supply can sustain the clock. */
    XPLMGetDatavf(clock_state.battery_volts,battery,0,8);
    XPLMGetDatavf(clock_state.bus_volts,bus,0,6);
    for (i=0;i<8;++i) if (isfinite(battery[i]) && battery[i]>=18.0f) return 1;
    for (i=0;i<6;++i) if (isfinite(bus[i]) && bus[i]>=18.0f) return 1;
    return 0;
}

static int clock_leap(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
static int clock_month_days(int year, int month)
{
    static const int days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return days[month-1] + (month == 2 && clock_leap(year));
}
static void clock_clamp_day(void)
{
    int maximum = clock_month_days(clock_state.year, clock_state.month);
    if (clock_state.day > maximum) clock_state.day = maximum;
}
static void clock_next_day(void)
{
    if (++clock_state.day > clock_month_days(clock_state.year,clock_state.month)) {
        clock_state.day=1;
        if (++clock_state.month > 12) { clock_state.month=1; ++clock_state.year; }
    }
}
static void clock_tick_second(void)
{
    if (++clock_state.second < 60) return;
    clock_state.second=0;
    if (++clock_state.minute < 60) return;
    clock_state.minute=0;
    if (++clock_state.hour < 24) return;
    clock_state.hour=0;
    clock_next_day();
}
static void clock_sync_gps(void)
{
    time_t now=time(NULL);
    struct tm *utc=gmtime(&now);
    int day, month, remaining, second_of_day;
    float raw_seconds=XPLMGetDataf(clock_state.zulu_seconds);
    int year=utc ? utc->tm_year+1900 : 2026;
    if (!isfinite(raw_seconds)) return;
    second_of_day=(int)floorf(raw_seconds);
    if (second_of_day<0 || second_of_day>=86400) return;
    day=XPLMGetDatai(clock_state.zulu_days);
    if (day<0 || day>=365+clock_leap(year)) return;
    remaining=day+1;
    for (month=1;month<12 && remaining>clock_month_days(year,month);++month)
        remaining-=clock_month_days(year,month);
    clock_state.year=year; clock_state.month=month; clock_state.day=remaining;
    clock_state.hour=second_of_day/3600;
    clock_state.minute=(second_of_day/60)%60;
    clock_state.second=second_of_day%60;
}
static void clock_adjust(int direction)
{
    int *value=NULL, minimum=0, maximum=0;
    switch (clock_state.set_field) {
    case 0: value=&clock_state.hour; maximum=23; break;
    case 1: value=&clock_state.minute; maximum=59; break;
    case 2: value=&clock_state.second; maximum=59; break;
    case 3: value=&clock_state.day; minimum=1;
            maximum=clock_month_days(clock_state.year,clock_state.month); break;
    case 4: value=&clock_state.month; minimum=1; maximum=12; break;
    case 5: value=&clock_state.year; minimum=2000; maximum=2099; break;
    default: return;
    }
    *value += direction;
    if (*value>maximum) *value=minimum;
    if (*value<minimum) *value=maximum;
    if (clock_state.set_field==4 || clock_state.set_field==5) clock_clamp_day();
    clock_state.flash_time=0;
}

typedef struct { const char *name; int *value; XPLMDataRef handle; } ClockRef;
#define CLOCK_REF(name, field) {"a380x/xp12/clock/" name, &clock_state.field, NULL}
static ClockRef clock_refs[] = {
    CLOCK_REF("mode",mode), CLOCK_REF("et_selector",et_selector),
    CLOCK_REF("date_held",date_held), CLOCK_REF("set_field",set_field),
    CLOCK_REF("year",year), CLOCK_REF("month",month), CLOCK_REF("day",day),
    CLOCK_REF("hour",hour), CLOCK_REF("minute",minute), CLOCK_REF("second",second),
    CLOCK_REF("et_seconds",et_seconds), CLOCK_REF("chr_seconds",chr_seconds),
    CLOCK_REF("chr_running",chr_running),
    CLOCK_REF("powered",powered), CLOCK_REF("chr_held",chr_held),
    CLOCK_REF("rst_held",rst_held), CLOCK_REF("et_display",et_display),
    CLOCK_REF("chr_display",chr_display),
};
#undef CLOCK_REF
static int clock_get(void *refcon) { return *(((ClockRef *)refcon)->value); }

typedef struct { const char *name; int action; XPLMCommandRef handle; } ClockCommand;
enum { CLK_MODE_UP, CLK_MODE_DOWN, CLK_ET_UP, CLK_ET_DOWN, CLK_CHR,
       CLK_RST, CLK_DATE, CLK_FIELD_NEXT, CLK_SET_UP, CLK_SET_DOWN };
#define CLOCK_CMD(name, action) {"a380x/xp12/clock/" name, action, NULL}
static ClockCommand clock_commands[] = {
    CLOCK_CMD("mode_up",CLK_MODE_UP), CLOCK_CMD("mode_down",CLK_MODE_DOWN),
    CLOCK_CMD("et_up",CLK_ET_UP), CLOCK_CMD("et_down",CLK_ET_DOWN),
    CLOCK_CMD("chr_toggle",CLK_CHR), CLOCK_CMD("chr_reset",CLK_RST),
    CLOCK_CMD("date",CLK_DATE), CLOCK_CMD("set_field_next",CLK_FIELD_NEXT),
    CLOCK_CMD("set_up",CLK_SET_UP), CLOCK_CMD("set_down",CLK_SET_DOWN),
};
#undef CLOCK_CMD
static int clock_command_handler(XPLMCommandRef command, XPLMCommandPhase phase, void *refcon)
{
    ClockCommand *item=(ClockCommand *)refcon;
    (void)command;
    if (!plugin_enabled) return 1;
    if (item->action==CLK_CHR || item->action==CLK_RST) {
        int *held=item->action==CLK_CHR ? &clock_state.chr_held : &clock_state.rst_held;
        if (phase==xplm_CommandBegin) *held=1;
        if (phase==xplm_CommandEnd) *held=0;
    }
    if (item->action==CLK_DATE) {
        if (phase==xplm_CommandBegin) {
            if (clock_state.powered && clock_state.mode==2) {
                clock_state.set_field=(clock_state.set_field+1)%6;
                clock_state.flash_time=0;
            }
            clock_state.date_held=1;
        }
        if (phase==xplm_CommandEnd) clock_state.date_held=0;
        return 1;
    }
    if (item->action==CLK_ET_DOWN && phase==xplm_CommandEnd && clock_state.et_selector==2) {
        clock_state.et_selector=1;
        return 1;
    }
    if (phase!=xplm_CommandBegin) return 1;
    switch (item->action) {
    case CLK_MODE_UP: case CLK_MODE_DOWN:
        if (item->action==CLK_MODE_UP && clock_state.mode<2) ++clock_state.mode;
        if (item->action==CLK_MODE_DOWN && clock_state.mode>0) --clock_state.mode;
        if (clock_state.mode==0 && clock_state.powered) clock_sync_gps();
        clock_state.fraction=0;
        clock_state.flash_time=0;
        break;
    case CLK_ET_UP:
        if (clock_state.et_selector>0) --clock_state.et_selector;
        if (clock_state.et_selector==0 && clock_state.powered) clock_state.et_display=1;
        break;
    case CLK_ET_DOWN:
        if (clock_state.et_selector<2) ++clock_state.et_selector;
        if (clock_state.et_selector==2) {
            clock_state.et_seconds=0; clock_state.et_fraction=0;
            clock_state.et_display=0;
        }
        break;
    case CLK_CHR:
        if (!clock_state.powered) break;
        if (clock_state.mode==2) clock_adjust(1);
        else {
            clock_state.chr_running=!clock_state.chr_running;
            clock_state.chr_display=1;
        }
        break;
    case CLK_RST:
        if (!clock_state.powered) break;
        if (clock_state.mode==2) clock_adjust(-1);
        else if (clock_state.chr_running) {
            clock_state.chr_seconds=0; clock_state.chr_fraction=0;
        }
        break;
    case CLK_FIELD_NEXT:
        if (clock_state.powered && clock_state.mode==2) {
            clock_state.set_field=(clock_state.set_field+1)%6;
            clock_state.flash_time=0;
        }
        break;
    case CLK_SET_UP: case CLK_SET_DOWN:
        if (clock_state.powered && clock_state.mode==2)
            clock_adjust(item->action==CLK_SET_UP ? 1 : -1);
        break;
    default: break;
    }
    return 1;
}
static void clock_update(float dt)
{
    float speed;
    clock_state.powered=clock_has_power();
    if (!clock_state.powered) {
        clock_state.chr_running=0;
        clock_state.chr_display=clock_state.et_display=0;
        clock_state.chr_seconds=clock_state.et_seconds=0;
        clock_state.chr_fraction=clock_state.et_fraction=clock_state.fraction=0;
        return;
    }
    if (!isfinite(dt) || dt<0 || dt>5.0f) dt=0;
    clock_state.flash_time=fmodf(clock_state.flash_time+dt,1.0f);
    speed=XPLMGetDataf(clock_state.sim_speed);
    if (!isfinite(speed) || speed<0 || speed>100.0f
            || XPLMGetDatai(clock_state.paused) || XPLMGetDatai(clock_state.replay)) dt=0;
    else dt*=speed;
    if (clock_state.mode==0) clock_sync_gps();
    else if (clock_state.mode==1) {
        clock_state.fraction+=dt;
        while (clock_state.fraction>=1.0f) {
            clock_tick_second(); clock_state.fraction-=1.0f;
        }
    }
    if (clock_state.et_selector==0) {
        clock_state.et_display=1;
        clock_state.et_fraction+=dt;
        while (clock_state.et_fraction>=1.0f) {
            /* The ET display saturates at 99 h 59 min, as in the source clock. */
            if (clock_state.et_seconds<359940) ++clock_state.et_seconds;
            clock_state.et_fraction-=1.0f;
        }
    }
    if (clock_state.chr_running) {
        clock_state.chr_fraction+=dt;
        while (clock_state.chr_fraction>=1.0f) {
            clock_state.chr_seconds=(clock_state.chr_seconds+1)%6000;
            clock_state.chr_fraction-=1.0f;
        }
    }
}
static void clock_release_controls(void)
{
    clock_state.chr_held=clock_state.rst_held=clock_state.date_held=0;
    if (clock_state.et_selector==2) clock_state.et_selector=1;
}
static void clock_shutdown(void)
{
    int i;
    clock_release_controls();
    for (i=0;i<ARRAY_LEN(clock_commands);++i) if (clock_commands[i].handle) {
        XPLMUnregisterCommandHandler(clock_commands[i].handle,clock_command_handler,1,&clock_commands[i]);
        clock_commands[i].handle=NULL;
    }
    for (i=0;i<ARRAY_LEN(clock_refs);++i) if (clock_refs[i].handle) {
        XPLMUnregisterDataAccessor(clock_refs[i].handle); clock_refs[i].handle=NULL;
    }
}
static int clock_init(void)
{
    int i;
    memset(&clock_state,0,sizeof(clock_state));
    clock_state.et_selector=1;
    clock_state.zulu_seconds=XPLMFindDataRef("sim/time/zulu_time_sec");
    clock_state.zulu_days=XPLMFindDataRef("sim/time/zulu_date_days");
    clock_state.battery_volts=XPLMFindDataRef("sim/cockpit2/electrical/battery_voltage_actual_volts");
    clock_state.bus_volts=XPLMFindDataRef("sim/cockpit2/electrical/bus_volts");
    clock_state.paused=XPLMFindDataRef("sim/time/paused");
    clock_state.replay=XPLMFindDataRef("sim/time/is_in_replay");
    clock_state.sim_speed=XPLMFindDataRef("sim/time/sim_speed_actual");
    if (!clock_state.zulu_seconds || !clock_state.zulu_days || !clock_state.battery_volts
            || !clock_state.bus_volts || !clock_state.paused || !clock_state.replay
            || !clock_state.sim_speed) return 0;
    clock_state.year=2026; clock_state.month=1; clock_state.day=1;
    clock_sync_gps();
    clock_state.powered=clock_has_power();
    for (i=0;i<ARRAY_LEN(clock_refs);++i) {
        ClockRef *ref=&clock_refs[i];
        if (XPLMFindDataRef(ref->name)) { clock_shutdown(); return 0; }
        ref->handle=XPLMRegisterDataAccessor(ref->name,xplmType_Int,0,
            clock_get,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,ref,NULL);
        if (!ref->handle) { clock_shutdown(); return 0; }
    }
    for (i=0;i<ARRAY_LEN(clock_commands);++i) {
        ClockCommand *item=&clock_commands[i];
        /* XPLM retains commands across aircraft reloads; CreateCommand reuses them. */
        item->handle=XPLMCreateCommand(item->name,"A380 center-right clock control");
        if (!item->handle) { clock_shutdown(); return 0; }
        XPLMRegisterCommandHandler(item->handle,clock_command_handler,1,item);
    }
    return 1;
}
