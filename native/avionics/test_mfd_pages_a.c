/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Headless tests and PNG/PPM previews for the MFD FMS pages of work package "pages A":
 * ACTIVE/INIT, SECn/INIT, FUEL&LOAD and SEC INDEX (a380x_mfd_page_init.c, a380x_mfd_page_fuel.c,
 * a380x_mfd_page_sec.c), driven through the real FMS core and the MFD frame with simulated
 * pointer clicks and KCCU keys.
 *
 * Reuses the software rasteriser of test_mfd_render.c (its main() is renamed and unused).
 *
 * Usage: test_mfd_pages_a <ppm-output-dir>
 * Link:  a380x_mfd.c a380x_mfd_ui.c a380x_mfd_draw.c a380x_mfd_page_data.c a380x_mfd_formats.c
 *        a380x_fms_messages.c a380x_fms.c a380x_fms_speeds.c a380x_fms_fpln.c a380x_navdata.c
 *        a380x_mfd_page_init.c a380x_mfd_page_fuel.c a380x_mfd_page_sec.c -lm
 */
#define main test_mfd_render_base_main
#include "test_mfd_render.c"
#undef main

#include "a380x_fms.h"
#include "a380x_fms_fpln.h"
#include "a380x_mfd_page_init.h"
#include "a380x_mfd_page_fuel.h"
#include "a380x_mfd_page_sec.h"

static int pa_failures;
#define PA_CHECK(c)                                                                  \
    do {                                                                             \
        if (!(c)) {                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);    \
            pa_failures++;                                                           \
        }                                                                            \
    } while (0)
#define PA_NEAR(a, b, eps) PA_CHECK(fabs((double)(a) - (double)(b)) <= (eps))

static A380XFms g_fms;
static A380XFlightPlans g_fp; /* lateral store stand-in (SEC route overview) */
static A380XFmsInputs g_fin;

/* ---- FMS hooks -------------------------------------------------------------------------------- */

static int pa_airport(void *ctx, const char *icao, A380XFmsAirportInfo *out)
{
    static const char *const known[] = {"EGLL", "KJFK", "EGKK", "LFPG", "KBOS", "EHAM"};
    size_t i;
    (void)ctx;
    for (i = 0; i < sizeof(known) / sizeof(known[0]); ++i) {
        if (strcmp(icao, known[i]) == 0) {
            memset(out, 0, sizeof(*out));
            out->found = 1;
            snprintf(out->ident, sizeof(out->ident), "%s", icao);
            out->elevation_ft = 83.0;
            out->lat_deg = 51.4775;
            out->lon_deg = -0.4614;
            out->trans_alt_ft = 6000.0;
            out->trans_level_fl = 70.0;
            return 1;
        }
    }
    return 0;
}

static void pa_leg(A380XFplnRoute *r, const char *ident, const char *annotation, const char *proc,
                   A380XFplnWpDesc desc)
{
    A380XFplnLeg *l = &r->el[r->count++];
    memset(l, 0, sizeof(*l));
    snprintf(l->ident, sizeof(l->ident), "%s", ident);
    snprintf(l->annotation, sizeof(l->annotation), "%s", annotation);
    snprintf(l->procedure_ident, sizeof(l->procedure_ident), "%s", proc);
    l->wp_desc = desc;
}

/* Lateral part of IMPORT ACTIVE: a London - New York route (FBW-like annotations). */
static int pa_sec_copy_from_active(struct A380XFlightPlans *p, int sec_plan)
{
    A380XFplnPlan *pl = &p->plan[sec_plan];
    A380XFplnRoute *r = &pl->pri;
    memset(pl, 0, sizeof(*pl));
    pl->exists = 1;
    pl->version = 1;
    pl->active_leg_index = -1;
    snprintf(r->origin_icao, sizeof(r->origin_icao), "EGLL");
    snprintf(r->dest_icao, sizeof(r->dest_icao), "KJFK");
    pa_leg(r, "EGLL27R", "CPT3J", "CPT3J", A380X_WPDESC_RUNWAY);
    pa_leg(r, "D254F", "CPT3J", "CPT3J", A380X_WPDESC_OTHER);
    pa_leg(r, "WOD", "CPT3J", "CPT3J", A380X_WPDESC_OTHER);
    pa_leg(r, "CPT", "CPT3J", "CPT3J", A380X_WPDESC_OTHER);
    pa_leg(r, "KENET", "UL9", "", A380X_WPDESC_OTHER);
    pa_leg(r, "STU", "UL9", "", A380X_WPDESC_OTHER);
    pa_leg(r, "PEMAK", "UL9", "", A380X_WPDESC_OTHER);
    pa_leg(r, "BANBA", "L9", "", A380X_WPDESC_OTHER);
    pa_leg(r, "SLANY", "L9", "", A380X_WPDESC_OTHER);
    pa_leg(r, "BAKUR", "", "", A380X_WPDESC_OTHER);
    pa_leg(r, "5520N", "", "", A380X_WPDESC_OTHER);
    pa_leg(r, "5530N", "", "", A380X_WPDESC_OTHER);
    pa_leg(r, "5440N", "", "", A380X_WPDESC_OTHER);
    pa_leg(r, "5350N", "", "", A380X_WPDESC_OTHER);
    pa_leg(r, "JOOPY", "", "", A380X_WPDESC_OTHER);
    pa_leg(r, "ALLRY", "N63B", "", A380X_WPDESC_OTHER);
    pa_leg(r, "BRADD", "N63B", "", A380X_WPDESC_OTHER);
    pa_leg(r, "SCUPP", "J581", "", A380X_WPDESC_OTHER);
    pa_leg(r, "BOS", "J581", "", A380X_WPDESC_OTHER);
    pa_leg(r, "", "", "", A380X_WPDESC_NONE);
    r->el[r->count - 1].flags = A380X_LEGF_DISCONTINUITY;
    pa_leg(r, "PARCH", "ROBER2", "ROBER2", A380X_WPDESC_OTHER);
    pa_leg(r, "ROBER", "ROBER2", "ROBER2", A380X_WPDESC_OTHER);
    pa_leg(r, "CCC", "ROBER2", "ROBER2", A380X_WPDESC_OTHER);
    pa_leg(r, "ZALPO", "I04R", "I04R", A380X_WPDESC_OTHER);
    pa_leg(r, "KJFK04R", "I04R", "I04R", A380X_WPDESC_RUNWAY);
    pa_leg(r, "KJFK", "", "", A380X_WPDESC_AIRPORT);
    r->first_missed_appr_index = r->count;
    r->destination_leg_index = r->count - 1;
    return 1;
}

static int pa_sec_delete(struct A380XFlightPlans *p, int sec_plan)
{
    memset(&p->plan[sec_plan], 0, sizeof(p->plan[sec_plan]));
    return 1;
}

static int pa_sec_init(struct A380XFlightPlans *p, int sec_plan)
{
    memset(&p->plan[sec_plan], 0, sizeof(p->plan[sec_plan]));
    p->plan[sec_plan].exists = 1; /* empty plan: legCount 0 */
    p->plan[sec_plan].pri.first_missed_appr_index = 0;
    return 1;
}

/* ---- Fixture ---------------------------------------------------------------------------------- */

static void pa_frame(void)
{
    a380x_fms_update(&g_fms, &g_fin, 0.02);
    g_in.now = g_t;
    g_in.tmpy_exists = (unsigned char)a380x_fms_tmpy_exists(&g_fms);
    g_in.metric_units = (unsigned char)g_fms.cfg.metric_units;
    g_in.flight_phase = a380x_fms_phase(&g_fms);
    a380x_mfd_frame(&g_mfd, &g_in);
    g_t += 0.02;
    g_fin.utc_seconds += 0.02;
    PA_CHECK(!a380x_mfd_display_list(&g_mfd, MFD_CAPT)->overflowed);
}

static void pa_frames(int n) { while (n-- > 0) pa_frame(); }

static void pa_click(float x, float y)
{
    mfd_pointer_move(&g_mfd, MFD_CAPT, x, y);
    pa_frame();
    mfd_pointer_button(&g_mfd, MFD_CAPT, 1);
    pa_frame();
    mfd_pointer_button(&g_mfd, MFD_CAPT, 0);
    pa_frame();
}

static void pa_key(MfdKey k, char ch) { mfd_key(&g_mfd, MFD_CAPT, k, ch); pa_frame(); }

static void pa_settle(void)
{
    g_t += 0.6; /* InputField 500 ms validation */
    pa_frames(4);
}

/* Click a field, type, ENT, wait for the validation. */
static void pa_enter(float x, float y, const char *s)
{
    pa_click(x, y);
    while (*s) pa_key(MFD_KEY_CHAR, *s++);
    pa_key(MFD_KEY_ENT, 0);
    pa_settle();
}

static void pa_leave(void) { mfd_pointer_leave(&g_mfd, MFD_CAPT); pa_frame(); }

static void pa_go(const char *uri)
{
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, uri);
    pa_frames(2);
}

static void pa_reset(void)
{
    A380XFmsNavHooks nav;
    A380XFmsPlanHooks hooks;
    A380XMfdSystemsHost host;

    memset(&g_fp, 0, sizeof(g_fp));
    memset(&nav, 0, sizeof(nav));
    nav.airport_lookup = pa_airport;
    memset(&hooks, 0, sizeof(hooks));
    hooks.plans = &g_fp;
    hooks.sec_copy_from_active = pa_sec_copy_from_active;
    hooks.sec_delete = pa_sec_delete;
    hooks.sec_init = pa_sec_init;
    a380x_fms_init(&g_fms, NULL, &nav, &hooks);

    memset(&g_fin, 0, sizeof(g_fin));
    g_fin.utc_seconds = 13 * 3600 + 42 * 60;
    g_fin.fmc_powered[0] = g_fin.fmc_powered[1] = g_fin.fmc_powered[2] = 1;
    g_fin.switching_knob = 1;
    g_fin.position_valid = 1;
    g_fin.lat_deg = 51.4775;
    g_fin.lon_deg = -0.4614;
    g_fin.adr_valid = 1;
    g_fin.baro_alt_ft = g_fin.pressure_alt_ft = 83.0;
    g_fin.sat_valid = 1;
    g_fin.sat_c = 15.0;
    g_fin.static_pressure_valid = 1;
    g_fin.static_pressure_hpa = 1013.25;
    g_fin.on_ground = 1;
    g_fin.fcu_alt_valid = 1;
    g_fin.fcu_selected_alt_ft = 5000.0;
    g_fin.speed_managed = 1;
    g_fin.cold_and_dark_spawn = 1;

    memset(&g_in, 0, sizeof(g_in));
    g_in.du_on[0] = g_in.du_on[1] = 1;
    g_in.fms_knob = 1;
    g_in.fmc_a_healthy = g_in.fmc_b_healthy = 1;
    g_in.metric_units = 1;
    memset(&host, 0, sizeof(host));
    host.read_status = host_status;
    a380x_mfd_init(&g_mfd, &g_fms, &g_fms.msgs, &host);
    a380x_mfd_cpny_fpln_set_hooks(NULL);
    pa_frames(2);
}

static void pa_snap(const char *dir, const char *name)
{
    char png[64];
    snprintf(png, sizeof(png), "%s", name);
    snap(dir, png);
}

static const char *pa_footer(void)
{
    const MfdMsgEntry *e = mfd_msg_footer_entry(&g_fms.msgs);
    return e ? e->text : "";
}

static int pa_has_msg(MfdMsgId id) { return mfd_msg_has_id(&g_fms.msgs, id); }

static double pa_get(int plan, A380XFmsField f) { return a380x_fms_get(&g_fms, plan, f).v; }

static int pa_text_is(int plan, A380XFmsText t, const char *s)
{
    char b[32];
    a380x_fms_get_text(&g_fms, plan, t, b, sizeof(b));
    return strcmp(b, s) == 0;
}

/* ---- INIT ------------------------------------------------------------------------------------- */

#define INIT_FLT_NBR 250.0f, 174.0f
#define INIT_FROM 196.0f, 233.0f
#define INIT_TO 333.0f, 233.0f
#define INIT_ALTN 495.0f, 233.0f
#define INIT_CRZ_FL 200.0f, 426.0f
#define INIT_CRZ_TEMP 510.0f, 426.0f
#define INIT_CI 185.0f, 527.0f
#define INIT_TRIP_WIND 212.0f, 574.0f
#define INIT_CPNY 680.0f, 174.0f

static void test_init(const char *dir)
{
    unsigned fl;
    pa_reset();
    pa_go("fms/active/init");
    PA_CHECK(strcmp(a380x_mfd_active_uri(&g_mfd, MFD_CAPT), "fms/active/init") == 0);

    /* Empty ACTIVE: mandatory FLT NBR / FROM / TO / ALTN / CRZ FL / CI, ALTN / CRZ FL / CI disabled. */
    fl = a380x_fms_text_flags(&g_fms, 0, A380X_FMS_T_ALTN, A380X_FMS_PAGE_INIT);
    PA_CHECK((fl & A380X_FMS_FF_MANDATORY) && (fl & A380X_FMS_FF_DISABLED));
    fl = a380x_fms_field_flags(&g_fms, 0, A380X_FMS_F_CRZ_FL, A380X_FMS_PAGE_INIT);
    PA_CHECK((fl & A380X_FMS_FF_MANDATORY) && (fl & A380X_FMS_FF_DISABLED));
    pa_leave();
    pa_snap(dir, "init_empty");

    /* CPNY F-PLN REQUEST: no datalink in the port. */
    pa_click(INIT_CPNY);
    PA_CHECK(pa_has_msg(MFD_MSG_COM_DATALINK_NOT_AVAIL));
    mfd_msg_reset(&g_fms.msgs);

    /* Disabled field: a click does nothing (CRZ FL before FROM/TO). */
    pa_click(INIT_CRZ_FL);
    PA_CHECK(!mfd_ui_has_focus(&g_mfd.sides[MFD_CAPT].ui));

    /* FLT NBR */
    pa_enter(INIT_FLT_NBR, "AFR001");
    PA_CHECK(pa_text_is(0, A380X_FMS_T_FLT_NBR, "AFR001"));

    /* FROM / TO: unknown airport -> NOT IN DATABASE, field reverts */
    pa_enter(INIT_FROM, "ZZZZ");
    PA_CHECK(pa_has_msg(MFD_MSG_NOT_IN_DATABASE));
    PA_CHECK(pa_text_is(0, A380X_FMS_T_FROM, ""));
    mfd_msg_reset(&g_fms.msgs);
    /* format error: AirportFormat needs 4 characters */
    pa_enter(INIT_FROM, "EGL");
    PA_CHECK(strstr(pa_footer(), "FORMAT ERROR") != NULL);
    mfd_msg_reset(&g_fms.msgs);

    pa_enter(INIT_FROM, "EGLL");
    PA_CHECK(pa_text_is(0, A380X_FMS_T_FROM, "EGLL"));
    PA_CHECK(!a380x_fms_has_active_flight_plan(&g_fms));
    pa_enter(INIT_TO, "KJFK");
    PA_CHECK(pa_text_is(0, A380X_FMS_T_TO, "KJFK"));
    PA_CHECK(a380x_fms_has_active_flight_plan(&g_fms));
    PA_CHECK(pa_text_is(0, A380X_FMS_T_ALTN, "NONE"));
    PA_CHECK(pa_text_is(0, A380X_FMS_T_CPNY_RTE, "NONE"));
    PA_CHECK(pa_text_is(0, A380X_FMS_T_FLT_NBR, "AFR001")); /* kept by newCityPair */

    /* ALTN */
    pa_enter(INIT_ALTN, "EGKK");
    PA_CHECK(pa_text_is(0, A380X_FMS_T_ALTN, "EGKK"));

    /* CRZ FL: 999 -> ENTRY OUT OF RANGE (format range 0..430), then 350 */
    pa_enter(INIT_CRZ_FL, "999");
    PA_CHECK(strstr(pa_footer(), "ENTRY OUT OF RANGE") != NULL);
    PA_CHECK(!a380x_fms_get(&g_fms, 0, A380X_FMS_F_CRZ_FL).has);
    mfd_msg_reset(&g_fms.msgs);
    pa_enter(INIT_CRZ_FL, "350");
    PA_NEAR(pa_get(0, A380X_FMS_F_CRZ_FL), 350.0, 1e-9);
    {
        A380XFmsValue t = a380x_fms_get(&g_fms, 0, A380X_FMS_F_CRZ_TEMP);
        PA_CHECK(t.has && !t.entered); /* ISA default, small font */
        PA_NEAR(t.v, a380x_fms_isa_temp(35000.0), 0.01);
    }

    /* CI: format error, then 75 (MODE must be ECON) */
    if (a380x_fms_get(&g_fms, 0, A380X_FMS_F_CI_MODE).v != A380X_FMS_CI_ECON) {
        MfdFmsError e;
        a380x_fms_enter(&g_fms, 0, A380X_FMS_F_CI_MODE, A380X_FMS_CI_ECON, &e);
    }
    pa_enter(INIT_CI, "A5");
    PA_CHECK(strstr(pa_footer(), "FORMAT ERROR") != NULL);
    PA_CHECK(strstr(pa_footer(), "FORMAT: XXX") != NULL);
    mfd_msg_reset(&g_fms.msgs);
    pa_enter(INIT_CI, "75");
    PA_NEAR(pa_get(0, A380X_FMS_F_CI), 75.0, 1e-9);
    pa_enter(INIT_CI, "1000"); /* maxDigits 3: the 4th digit is not accepted -> 100 */
    PA_NEAR(pa_get(0, A380X_FMS_F_CI), 100.0, 1e-9);
    pa_enter(INIT_CI, "75");

    /* CRZ TEMP: 50 is stored as -50 (CrzTempFormat), pilot font */
    pa_enter(INIT_CRZ_TEMP, "50");
    PA_NEAR(pa_get(0, A380X_FMS_F_CRZ_TEMP), -50.0, 1e-9);
    PA_CHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_CRZ_TEMP).entered);
    /* CLR (backspace on an unmodified field) reverts to the ISA value */
    pa_click(INIT_CRZ_TEMP);
    pa_key(MFD_KEY_BACKSPACE, 0);
    pa_key(MFD_KEY_ENT, 0);
    pa_settle();
    PA_CHECK(!a380x_fms_get(&g_fms, 0, A380X_FMS_F_CRZ_TEMP).entered);

    /* TRIP WIND HD20 */
    pa_enter(INIT_TRIP_WIND, "HD20");
    PA_NEAR(pa_get(0, A380X_FMS_F_TRIP_WIND), -20.0, 1e-9);

    pa_leave();
    pa_snap(dir, "init_filled");

    /* Buttons: FUEL&LOAD navigates to fms/active/fuel-load */
    pa_click(230.0f, 817.0f);
    pa_frames(1);
    PA_CHECK(strcmp(a380x_mfd_active_uri(&g_mfd, MFD_CAPT), "fms/active/fuel-load") == 0);
    /* RETURN goes back to INIT */
    pa_click(72.0f, 930.0f);
    pa_frames(1);
    PA_CHECK(strcmp(a380x_mfd_active_uri(&g_mfd, MFD_CAPT), "fms/active/init") == 0);
    /* DEPARTURE enabled with FROM/TO in PREFLIGHT */
    pa_click(230.0f, 717.0f);
    pa_frames(1);
    PA_CHECK(strcmp(a380x_mfd_active_uri(&g_mfd, MFD_CAPT), "fms/active/f-pln-departure") == 0);
}

/* ---- FUEL&LOAD -------------------------------------------------------------------------------- */

#define FUEL_ZFW 265.0f, 215.0f
#define FUEL_ZFWCG 592.0f, 215.0f
#define FUEL_BLOCK 265.0f, 280.0f
#define FUEL_TAXI 200.0f, 365.0f
#define FUEL_FINAL_TIME 345.0f, 595.0f
#define FUEL_PAX 590.0f, 365.0f
#define FUEL_RTE_RSV 200.0f, 481.0f

static void test_fuel(const char *dir)
{
    MfdFmsError e;
    pa_reset();
    pa_go("fms/active/fuel-load");
    pa_leave();
    pa_snap(dir, "fuel_empty");

    /* City pair first (newCityPair resets the plan's perf data), CI for the MODE/CI fields. */
    a380x_fms_enter_text(&g_fms, 0, A380X_FMS_T_FROM, "EGLL", &e);
    a380x_fms_enter_text(&g_fms, 0, A380X_FMS_T_TO, "KJFK", &e);
    a380x_fms_enter_text(&g_fms, 0, A380X_FMS_T_ALTN, "KBOS", &e);

    /* ZFW out of range (270..373 t), then valid entries */
    pa_enter(FUEL_ZFW, "250");
    PA_CHECK(strstr(pa_footer(), "ENTRY OUT OF RANGE") != NULL);
    PA_CHECK(strstr(pa_footer(), "RNG: 270.0 TO 373.0 T") != NULL);
    mfd_msg_reset(&g_fms.msgs);
    pa_enter(FUEL_ZFW, "300");
    PA_NEAR(pa_get(0, A380X_FMS_F_ZFW), 300000.0, 1e-6);
    pa_enter(FUEL_ZFWCG, "30");
    PA_NEAR(pa_get(0, A380X_FMS_F_ZFWCG), 30.0, 1e-9);
    pa_enter(FUEL_BLOCK, "120");
    PA_NEAR(pa_get(0, A380X_FMS_F_BLOCK), 120000.0, 1e-6);
    pa_frames(15); /* 250 ms FMS cycle */
    PA_NEAR(pa_get(0, A380X_FMS_F_TOW), 418500.0, 1.0); /* 300 + 120 - 1.5 taxi */
    PA_CHECK(!a380x_fms_get(&g_fms, 0, A380X_FMS_F_TAXI).entered); /* default taxi, small font */

    /* FINAL time 0045 -> FINAL fuel 9.0 t computed */
    pa_enter(FUEL_FINAL_TIME, "0045");
    pa_frames(15);
    PA_NEAR(pa_get(0, A380X_FMS_F_FINAL_TIME), 45.0, 1e-9);
    PA_NEAR(pa_get(0, A380X_FMS_F_FINAL_FUEL), 9000.0, 1.0);
    PA_CHECK(!a380x_fms_get(&g_fms, 0, A380X_FMS_F_FINAL_FUEL).entered);

    /* RTE RSV 3.0 without TRIP: % shows --.- */
    pa_enter(FUEL_RTE_RSV, "3.0");
    PA_NEAR(pa_get(0, A380X_FMS_F_RTE_RSV), 3000.0, 1e-6);
    PA_CHECK(!a380x_fms_get(&g_fms, 0, A380X_FMS_F_RTE_RSV_PCT).has);

    /* PAX NBR */
    pa_enter(FUEL_PAX, "480");
    PA_NEAR(pa_get(0, A380X_FMS_F_PAX), 480.0, 1e-9);

    /* A VNAV destination prediction stands in for the (not yet ported) predictions:
     * 7 h 05, EFOB 21.4 t. */
    a380x_fms_enter(&g_fms, 0, A380X_FMS_F_CI_MODE, A380X_FMS_CI_ECON, &e);
    a380x_fms_enter(&g_fms, 0, A380X_FMS_F_CI, 75, &e);
    a380x_fms_set_destination_prediction(&g_fms, 3000.0, 7.0 * 3600.0 + 5.0 * 60.0, 21400.0);
    pa_frames(15);
    PA_CHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_TRIP).has);
    pa_leave();
    pa_snap(dir, "fuel_filled");

    /* Imperial units */
    g_fms.cfg.metric_units = 0;
    pa_frames(2);
    pa_snap(dir, "fuel_filled_klb");
    g_fms.cfg.metric_units = 1;
}

/* ---- SEC INDEX -------------------------------------------------------------------------------- */

static void test_sec(const char *dir)
{
    MfdFmsError e;
    A380XFmsSecInfo info;
    pa_reset();
    a380x_fms_enter_text(&g_fms, 0, A380X_FMS_T_FLT_NBR, "AFR001", &e);
    a380x_fms_enter_text(&g_fms, 0, A380X_FMS_T_FROM, "EGLL", &e);
    a380x_fms_enter_text(&g_fms, 0, A380X_FMS_T_TO, "KJFK", &e);
    a380x_fms_enter(&g_fms, 0, A380X_FMS_F_ZFW, 300000, &e);
    pa_go("fms/sec/index/1");
    pa_leave();
    pa_snap(dir, "sec_empty");

    /* IMPORT -> ACTIVE* */
    pa_click(661.0f, 281.0f);
    pa_click(620.0f, 321.0f);
    pa_frames(2);
    PA_CHECK(a380x_fms_sec_exists(&g_fms, 1));
    a380x_fms_sec_info(&g_fms, 1, &info);
    PA_CHECK(info.has_city_pair && strcmp(info.from, "EGLL") == 0 && strcmp(info.to, "KJFK") == 0);
    PA_CHECK(info.flags & A380X_FMS_PLANF_COPIED_FROM_ACTIVE);
    PA_CHECK(info.can_swap); /* PREFLIGHT */
    {
        const MfdSecIndexState *st = (const MfdSecIndexState *)g_mfd.sides[MFD_CAPT].page_state;
        PA_CHECK(st->route_count >= 12);
        PA_CHECK(strcmp(st->route[0].annotation, "RWY") == 0 && strcmp(st->route[0].ident, "27R") == 0);
        PA_CHECK(strcmp(st->route[1].annotation, "CPT3J") == 0 && strcmp(st->route[1].ident, "CPT") == 0);
        PA_CHECK(strcmp(st->route[2].annotation, "UL9") == 0 && strcmp(st->route[2].ident, "PEMAK") == 0);
        PA_CHECK(strcmp(st->route[4].annotation, "DIR") == 0 && strcmp(st->route[4].ident, "BAKUR") == 0);
        PA_CHECK(strcmp(st->route[st->route_count - 1].ident, "04R") == 0);
    }
    pa_leave();
    pa_snap(dir, "sec_sec1");

    /* SEC2 tab: empty, IMPORT offers SEC 1* (enabled) and SEC 3* (greyed) */
    pa_click(384.0f, 158.0f);
    pa_click(661.0f, 281.0f);
    pa_leave();
    pa_snap(dir, "sec_sec2_import_menu");
    pa_click(620.0f, 357.0f); /* SEC 1* */
    pa_frames(2);
    PA_CHECK(a380x_fms_sec_exists(&g_fms, 2));

    /* INIT on SEC2 -> SEC2/INIT with RETURN */
    pa_click(661.5f, 696.0f);
    pa_frames(1);
    PA_CHECK(strcmp(a380x_mfd_active_uri(&g_mfd, MFD_CAPT), "fms/sec2/init") == 0);
    pa_leave();
    pa_snap(dir, "init_sec2");
    pa_click(75.0f, 929.0f); /* RETURN -> fms/sec/index/2 */
    pa_frames(1);
    PA_CHECK(strcmp(a380x_mfd_active_uri(&g_mfd, MFD_CAPT), "fms/sec/index/2") == 0);

    /* Back on SEC1 tab: SWAP ACTIVE* -> CHECK T.O. DATA; DELETE* clears the tab */
    pa_click(134.0f, 158.0f);
    mfd_msg_reset(&g_fms.msgs);
    pa_click(489.0f, 803.5f);
    pa_frames(2);
    PA_CHECK(pa_has_msg(MFD_MSG_CHECK_TO_DATA));
    a380x_fms_sec_info(&g_fms, 1, &info);
    PA_CHECK(info.flags & A380X_FMS_PLANF_SWAPPED_WITH_ACTIVE);
    pa_click(94.0f, 803.5f);
    pa_frames(2);
    PA_CHECK(!a380x_fms_sec_exists(&g_fms, 1));
}

/* ---- Unit / layout checks without rendering --------------------------------------------------- */

static void test_helpers(void)
{
    char b[16];
    a380x_mfd_fms_weight_text(1, 418500.0, 1, b, sizeof(b));
    PA_CHECK(strcmp(b, "418.5") == 0);
    a380x_mfd_fms_weight_text(0, 0.0, 1, b, sizeof(b));
    PA_CHECK(strcmp(b, "---.-") == 0);
    a380x_mfd_fms_hhmm_text(1, 7.0 * 3600.0 + 5.0 * 60.0 + 59.0, 0, b, sizeof(b));
    PA_CHECK(strcmp(b, "07:05") == 0);
    a380x_mfd_fms_hhmm_text(1, 25.0 * 3600.0, 1, b, sizeof(b));
    PA_CHECK(strcmp(b, "01:00") == 0);
    a380x_mfd_fms_hhmm_text(0, 0.0, 0, b, sizeof(b));
    PA_CHECK(strcmp(b, "--:--") == 0);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    if (!mfd_font_decode(g_atlas)) {
        fprintf(stderr, "font decode failed\n");
        return 1;
    }
    a380x_mfd_page_data_register();
    a380x_mfd_page_init_register();
    a380x_mfd_page_fuel_register();
    a380x_mfd_page_sec_register();

    test_helpers();
    test_init(dir);
    test_fuel(dir);
    test_sec(dir);

    if (pa_failures) {
        fprintf(stderr, "test_mfd_pages_a: %d failure(s)\n", pa_failures);
        return 1;
    }
    printf("test_mfd_pages_a: OK\n");
    return 0;
}
