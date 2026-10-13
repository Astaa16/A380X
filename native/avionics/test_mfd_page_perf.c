/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PERF page test: headless interaction tests and PNG previews (via PPM) of the MFD
 * ACTIVE/PERF page driven by a real FMS core. Reuses the software rasteriser and the MFD fixture
 * of test_mfd_render.c (its main() is renamed and never called).
 *
 * Usage: test_mfd_page_perf <ppm-output-dir>
 * Link: test_mfd_page_perf.c a380x_mfd_page_perf.c a380x_mfd.c a380x_mfd_ui.c a380x_mfd_draw.c
 *       a380x_mfd_page_data.c a380x_mfd_formats.c a380x_fms_messages.c a380x_fms.c a380x_fms_speeds.c -lm
 */
#define main test_mfd_render_main
#include "test_mfd_render.c"
#undef main

#include "a380x_fms.h"
#include "a380x_mfd_page_perf.h"

static int perf_checks;
#define PCHECK(c)                                                                                   \
    do {                                                                                            \
        perf_checks++;                                                                              \
        if (!(c)) {                                                                                 \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);                   \
            exit(1);                                                                                \
        }                                                                                           \
    } while (0)

/* ======================================================================================== */
/* FMS fixture: navdata and lateral plan fakes                                              */
/* ======================================================================================== */

static int perf_fake_airport(void *ctx, const char *icao, A380XFmsAirportInfo *o)
{
    static const struct { const char *id; double elev, lat, lon, ta, tl; } db[] = {
        {"EGLL", 83, 51.4775, -0.4614, 6000, 70},
        {"KJFK", 13, 40.6398, -73.7789, 18000, 180},
    };
    size_t i;
    (void)ctx;
    for (i = 0; i < sizeof db / sizeof db[0]; i++) {
        if (strcmp(icao, db[i].id) == 0) {
            o->found = 1;
            snprintf(o->ident, sizeof o->ident, "%s", icao);
            o->elevation_ft = db[i].elev;
            o->lat_deg = db[i].lat;
            o->lon_deg = db[i].lon;
            o->trans_alt_ft = db[i].ta;
            o->trans_level_fl = db[i].tl;
            return 1;
        }
    }
    return 0;
}

struct A380XFlightPlans {
    int sec[A380X_FMS_NUM_PLANS];
    int have_runways;
};
static struct A380XFlightPlans perf_fp;

static int perf_fp_exists(struct A380XFlightPlans *p, int plan)
{
    if (plan == A380X_FMS_PLAN_TMPY) return 0;
    if (plan >= A380X_FMS_PLAN_SEC1) return p->sec[plan];
    return 1;
}
static A380XFmsRunwayInfo perf_rwy(const char *id, double elev, double len, double brg)
{
    A380XFmsRunwayInfo r;
    memset(&r, 0, sizeof r);
    r.valid = 1;
    snprintf(r.ident, sizeof r.ident, "%s", id);
    r.threshold_elev_ft = elev;
    r.length_m = len;
    r.magnetic_bearing_deg = brg;
    r.threshold_lat_deg = r.threshold_lon_deg = NAN;
    return r;
}
static int perf_fp_origin_rwy(struct A380XFlightPlans *p, int plan, A380XFmsRunwayInfo *o)
{
    (void)plan;
    if (!p->have_runways) return 0;
    *o = perf_rwy("27L", 77, 3902, 270);
    return 1;
}
static int perf_fp_dest_rwy(struct A380XFlightPlans *p, int plan, A380XFmsRunwayInfo *o)
{
    (void)plan;
    if (!p->have_runways) return 0;
    *o = perf_rwy("22L", 13, 3460, 222);
    return 1;
}
static int perf_fp_approach(struct A380XFlightPlans *p, int plan, A380XFmsApproachInfo *o)
{
    (void)plan;
    memset(o, 0, sizeof(*o));
    if (!p->have_runways) return 0;
    o->valid = 1;
    o->type = A380X_FMS_APPR_ILS;
    snprintf(o->name, sizeof o->name, "ILS22L");
    return 1;
}
static int perf_fp_city(struct A380XFlightPlans *p, int plan, const char *a, const char *b, const char *c)
{
    (void)p; (void)plan; (void)a; (void)b; (void)c;
    return 1;
}
static int perf_fp_sec_copy(struct A380XFlightPlans *p, int s) { p->sec[s] = 1; return 1; }
static int perf_fp_sec_del(struct A380XFlightPlans *p, int s) { p->sec[s] = 0; return 1; }

static A380XFms g_fms;
static A380XFmsInputs g_fin;

static void perf_base_inputs(void)
{
    int i;
    memset(&g_fin, 0, sizeof g_fin);
    g_fin.utc_seconds = 12 * 3600 + 34 * 60;
    for (i = 0; i < 3; i++) g_fin.fmc_powered[i] = 1;
    g_fin.switching_knob = 1;
    g_fin.position_valid = 1;
    g_fin.lat_deg = 51.4775;
    g_fin.lon_deg = -0.4614;
    g_fin.adr_valid = 1;
    g_fin.baro_alt_ft = 83;
    g_fin.pressure_alt_ft = 83;
    g_fin.sat_valid = 1;
    g_fin.sat_c = 15;
    g_fin.static_pressure_valid = 1;
    g_fin.static_pressure_hpa = 1013.25;
    g_fin.on_ground = 1;
    g_fin.fcu_alt_valid = 1;
    g_fin.fcu_selected_alt_ft = 5000;
    g_fin.fcu_selected_speed_kt = 150;
    g_fin.speed_managed = 1;
    g_fin.cold_and_dark_spawn = 1;
}

/* Copy the FMS state the MFD frame needs (title-bar tags, phase). */
static void perf_sync(void)
{
    g_in.flight_phase = a380x_fms_phase(&g_fms);
    g_in.engine_out = (unsigned char)a380x_fms_engine_out(&g_fms);
    g_in.tmpy_exists = (unsigned char)a380x_fms_tmpy_exists(&g_fms);
    g_in.metric_units = (unsigned char)g_fms.cfg.metric_units;
}

static void perf_fms_run(double seconds)
{
    int n = (int)(seconds / 0.05 + 0.5), i;
    for (i = 0; i < n; i++) a380x_fms_update(&g_fms, &g_fin, 0.05);
    perf_sync();
}

static void perf_reset(int runways)
{
    A380XFmsNavHooks nav;
    A380XFmsPlanHooks fp;
    A380XMfdSystemsHost host;
    memset(&nav, 0, sizeof nav);
    nav.airport_lookup = perf_fake_airport;
    memset(&fp, 0, sizeof fp);
    memset(&perf_fp, 0, sizeof perf_fp);
    perf_fp.have_runways = runways;
    fp.plans = &perf_fp;
    fp.plan_exists = perf_fp_exists;
    fp.origin_runway = perf_fp_origin_rwy;
    fp.destination_runway = perf_fp_dest_rwy;
    fp.approach = perf_fp_approach;
    fp.new_city_pair = perf_fp_city;
    fp.sec_copy_from_active = perf_fp_sec_copy;
    fp.sec_init = perf_fp_sec_copy;
    fp.sec_delete = perf_fp_sec_del;
    a380x_fms_init(&g_fms, NULL, &nav, &fp);
    perf_base_inputs();
    perf_fms_run(1.0);

    memset(&g_in, 0, sizeof(g_in));
    g_in.du_on[0] = g_in.du_on[1] = 1;
    g_in.fms_knob = 1;
    g_in.fmc_a_healthy = g_in.fmc_b_healthy = 1;
    g_in.metric_units = 1;
    memset(&host, 0, sizeof(host));
    host.read_status = host_status;
    a380x_mfd_init(&g_mfd, &g_fms, &g_fms.msgs, &host);
    perf_sync();
    frames(2);
}

static void perf_open(const char *uri)
{
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, uri);
    frames(3);
}

static MfdPerfState *perf_state(void)
{
    return (MfdPerfState *)(void *)g_mfd.sides[MFD_CAPT].page_state;
}

static void perf_enter(A380XFmsField f, double v)
{
    MfdFmsError err;
    int ok = a380x_fms_enter(&g_fms, A380X_FMS_PLAN_ACTIVE, f, v, &err);
    if (!ok) fprintf(stderr, "enter %d = %g rejected (%d %s)\n", (int)f, v, (int)err.type, err.details);
    PCHECK(ok);
}

static void perf_text_entry(A380XFmsText t, const char *s)
{
    MfdFmsError err;
    PCHECK(a380x_fms_enter_text(&g_fms, A380X_FMS_PLAN_ACTIVE, t, s, &err));
}

/* Type into the field at (x, y), validate, and let the 500 ms validation elapse. */
static void perf_type_field(float x, float y, const char *s)
{
    click(x, y);
    type(s);
    key(MFD_KEY_ENT, 0);
    g_t += 0.6;
    frames(2);
}

/* Origin/destination, weights, CRZ FL, CI: the common "INIT done" state. */
static void perf_init_flight(void)
{
    perf_text_entry(A380X_FMS_T_FROM, "EGLL");
    perf_text_entry(A380X_FMS_T_TO, "KJFK");
    perf_enter(A380X_FMS_F_CRZ_FL, 350);
    perf_enter(A380X_FMS_F_CI_MODE, A380X_FMS_CI_ECON);
    perf_enter(A380X_FMS_F_CI, 80);
    perf_enter(A380X_FMS_F_ZFW, 300000);
    perf_enter(A380X_FMS_F_ZFWCG, 37);
    perf_enter(A380X_FMS_F_BLOCK, 120000);
    perf_fms_run(1.0);
}

static void perf_set_phase(int ph)
{
    g_fms.phase = ph; /* white-box: previews of in-flight states without flying the phase manager */
    perf_sync();
}

static void perf_snap(const char *dir, const char *name)
{
    leave();
    frames(1);
    snap(dir, name);
}

/* ======================================================================================== */
/* Interaction tests                                                                        */
/* ======================================================================================== */

#define V1_X 110.6f
#define V_YC0 354.5f
#define V_YC1 405.5f
#define V_YC2 456.5f

static void test_tab_logic(void)
{
    PCHECK(mfd_perf_tab_from_extra("appr") == MFD_PERF_TAB_APPR);
    PCHECK(mfd_perf_tab_from_extra("to") == MFD_PERF_TAB_TO);
    PCHECK(mfd_perf_tab_from_extra("ga/x") == MFD_PERF_TAB_GA);
    PCHECK(mfd_perf_tab_from_extra("") == -1);
    PCHECK(mfd_perf_tab_from_extra("foo") == -1);
    /* follows the phase only when the crew was looking at the phase tab */
    PCHECK(mfd_perf_follow_phase(MFD_PERF_TAB_TO, 1, 2, 1) == MFD_PERF_TAB_CLB);
    PCHECK(mfd_perf_follow_phase(MFD_PERF_TAB_APPR, 1, 2, 1) == MFD_PERF_TAB_APPR);
    PCHECK(mfd_perf_follow_phase(MFD_PERF_TAB_TO, 1, 2, 0) == MFD_PERF_TAB_TO);
    PCHECK(mfd_perf_follow_phase(MFD_PERF_TAB_GA, 6, 7, 1) == MFD_PERF_TAB_GA); /* DONE: no follow */
    PCHECK(mfd_perf_follow_phase(MFD_PERF_TAB_TO, 0, 1, 1) == MFD_PERF_TAB_TO);
}

static void test_interactions(void)
{
    MfdPerfState *st;

    perf_reset(1);
    perf_init_flight();
    perf_open("fms/active/perf");
    st = perf_state();
    PCHECK(st->tab == MFD_PERF_TAB_TO); /* PREFLIGHT -> T.O */
    PCHECK(strcmp(a380x_mfd_active_uri(&g_mfd, MFD_CAPT), "fms/active/perf") == 0);

    /* V-speed entry through the field */
    perf_type_field(V1_X, V_YC0, "140");
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_V1).has);
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_V1).v == 140.0);
    perf_type_field(V1_X, V_YC1, "145");
    perf_type_field(V1_X, V_YC2, "150");
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_VR).v == 145.0);
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_V2).v == 150.0);
    perf_fms_run(0.5);
    PCHECK(a380x_fms_outputs(&g_fms)->v1_knots == 140.0);
    PCHECK(!mfd_msg_has_id(&g_fms.msgs, MFD_MSG_V_TO_DISAGREE));

    /* out-of-range entry: format rejects, value kept, message queued */
    perf_type_field(V1_X, V_YC0, "500");
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_V1).v == 140.0);
    PCHECK(mfd_msg_has_id(&g_fms.msgs, MFD_MSG_ENTRY_OUT_OF_RANGE));

    /* V1 > VR -> V1/VR/V2 DISAGREE (type II, removed when resolved) */
    perf_type_field(V1_X, V_YC0, "160");
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_V1).v == 160.0);
    perf_fms_run(0.5);
    PCHECK(a380x_fms_outputs(&g_fms)->vspeeds_disagree);
    PCHECK(mfd_msg_has_id(&g_fms.msgs, MFD_MSG_V_TO_DISAGREE));
    perf_type_field(V1_X, V_YC0, "140");
    perf_fms_run(0.5);
    PCHECK(!a380x_fms_outputs(&g_fms)->vspeeds_disagree);

    /* FLEX: radio (row 2 at y 387..421) then the temperature field (636, 386) */
    PCHECK(a380x_fms_field_flags(&g_fms, 0, A380X_FMS_F_FLEX_TEMP, A380X_FMS_PAGE_PERF_TO) & A380X_FMS_FF_HIDDEN);
    click(480.0f, 404.0f);
    frames(1);
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_TO_POWER).v == A380X_FMS_TO_FLEX);
    PCHECK(!(a380x_fms_field_flags(&g_fms, 0, A380X_FMS_F_FLEX_TEMP, A380X_FMS_PAGE_PERF_TO) & A380X_FMS_FF_HIDDEN));
    perf_type_field(685.0f, 404.5f, "55");
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_FLEX_TEMP).v == 55.0);
    perf_fms_run(0.5);
    PCHECK(a380x_fms_outputs(&g_fms)->to_flex_temp_c == 55.0);
    /* DERATED is never selectable [FBW-PH] */
    click(480.0f, 453.0f);
    frames(1);
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_TO_POWER).v == A380X_FMS_TO_FLEX);
    /* back to TOGA */
    click(480.0f, 355.0f);
    frames(1);
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_TO_POWER).v == A380X_FMS_TO_TOGA);
    click(480.0f, 404.0f);
    frames(1);

    /* FLAPS dropdown: open (14..89 x 538..582), pick "2" (second row of the list) */
    click(40.0f, 560.0f);
    frames(1);
    PCHECK(mfd_ui_menu_is_open(&g_mfd.sides[MFD_CAPT].ui, MFD_ID(MFD_PAGE_ID_PERF, 140)));
    click(40.0f, 582.0f + 2.0f + 36.0f + 18.0f);
    frames(1);
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_TO_FLAPS).v == 2.0);

    /* THS FOR (119.1, 541.5): 0 is silently ignored, 31 accepted */
    perf_type_field(170.0f, 560.0f, "31");
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_THS_FOR).v == 31.0);

    /* THR RED below origin elevation + 400 ft -> ENTRY OUT OF RANGE ([PORT]) */
    mfd_msg_init(&g_fms.msgs);
    perf_type_field(250.0f, 633.0f, "0300"); /* 3 chars would be FL300 */
    PCHECK(mfd_msg_has_id(&g_fms.msgs, MFD_MSG_ENTRY_OUT_OF_RANGE));
    PCHECK(!a380x_fms_get(&g_fms, 0, A380X_FMS_F_THR_RED).entered);
    perf_type_field(250.0f, 633.0f, "1600");
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_THR_RED).entered);
    PCHECK(a380x_fms_get(&g_fms, 0, A380X_FMS_F_THR_RED).v == 1600.0);

    /* tab switching by click: CLB (x 126.8..266.7), APPR, back to T.O */
    click(196.0f, 232.0f);
    PCHECK(perf_state()->tab == MFD_PERF_TAB_CLB);
    click(571.0f, 232.0f);
    PCHECK(perf_state()->tab == MFD_PERF_TAB_APPR);
    click(60.0f, 232.0f);
    PCHECK(perf_state()->tab == MFD_PERF_TAB_TO);

    /* URI extra selects the tab; a phase change follows the phase tab only */
    perf_open("fms/active/perf/des");
    PCHECK(perf_state()->tab == MFD_PERF_TAB_DES);
    perf_open("fms/active/perf/to");
    PCHECK(perf_state()->tab == MFD_PERF_TAB_TO);
    perf_set_phase(A380X_FMS_PHASE_TAKEOFF);
    frames(1);
    PCHECK(perf_state()->tab == MFD_PERF_TAB_TO); /* PREFLIGHT -> TAKEOFF never switches */
    perf_set_phase(A380X_FMS_PHASE_CLIMB);
    frames(1);
    PCHECK(perf_state()->tab == MFD_PERF_TAB_CLB);
    click(571.0f, 232.0f); /* crew looks at APPR: no follow on CLIMB -> CRUISE */
    perf_set_phase(A380X_FMS_PHASE_CRUISE);
    frames(1);
    PCHECK(perf_state()->tab == MFD_PERF_TAB_APPR);
    /* T.O fields are inactive (static) once TAKEOFF has started: entries cannot reach the FMS */
    click(60.0f, 232.0f);
    PCHECK(a380x_fms_field_flags(&g_fms, 0, A380X_FMS_F_V1, A380X_FMS_PAGE_PERF_TO) & A380X_FMS_FF_INACTIVE);
    click(V1_X, V_YC0);
    PCHECK(!mfd_ui_has_focus(&g_mfd.sides[MFD_CAPT].ui));

    /* ACTIVATE APPR (footer, CRUISE): button -> dialog -> CONFIRM */
    perf_open("fms/active/perf/crz");
    PCHECK(a380x_fms_can_activate_approach(&g_fms, 0));
    click(117.76f + 70.0f, 916.0f);
    PCHECK(perf_state()->dialog == MFD_PERF_DLG_ACTIVATE_APPR);
    {
        float fw = mfd_text_width("CONFIRM *", MFD_FS_BUTTON) + 22.0f;
        click(2.0f + 280.0f - 7.0f - fw * 0.5f, 783.0f + 165.0f - 27.0f);
    }
    PCHECK(perf_state()->dialog == MFD_PERF_DLG_NONE);
    PCHECK(a380x_fms_phase(&g_fms) == A380X_FMS_PHASE_APPROACH);

    /* SEC1/PERF shows the copy (white values); deleting SEC1 sends the page back to ACTIVE */
    PCHECK(a380x_fms_sec_copy_from_active(&g_fms, 1));
    perf_open("fms/sec1/perf");
    PCHECK(strcmp(a380x_mfd_active_uri(&g_mfd, MFD_CAPT), "fms/sec1/perf") == 0);
    PCHECK(a380x_fms_sec_delete(&g_fms, 1));
    frames(3);
    PCHECK(strcmp(a380x_mfd_active_uri(&g_mfd, MFD_CAPT), "fms/active/perf") == 0);
    PCHECK(!g_mfd.sides[MFD_CAPT].dl.overflowed);
}

/* ======================================================================================== */
/* Previews                                                                                 */
/* ======================================================================================== */

static void perf_fill_to(void)
{
    perf_enter(A380X_FMS_F_V1, 140);
    perf_enter(A380X_FMS_F_VR, 145);
    perf_enter(A380X_FMS_F_V2, 150);
    perf_enter(A380X_FMS_F_TO_POWER, A380X_FMS_TO_FLEX);
    perf_enter(A380X_FMS_F_FLEX_TEMP, 55);
    perf_enter(A380X_FMS_F_TO_FLAPS, 2);
    perf_enter(A380X_FMS_F_THS_FOR, 31.0);
    perf_enter(A380X_FMS_F_PACKS, A380X_FMS_PACKS_ON);
    perf_enter(A380X_FMS_F_ANTI_ICE, A380X_FMS_AI_OFF);
    perf_fms_run(0.5);
}

static void perf_fill_appr(void)
{
    perf_enter(A380X_FMS_F_APPR_QNH, 1013);
    perf_enter(A380X_FMS_F_APPR_TEMP, 15);
    perf_enter(A380X_FMS_F_APPR_WIND_DIR, 250);
    perf_enter(A380X_FMS_F_APPR_WIND_SPD, 18);
    perf_enter(A380X_FMS_F_APPR_BARO, 300);
    perf_enter(A380X_FMS_F_APPR_RADIO, 200);
    perf_fms_run(0.5);
}

static void previews(const char *dir)
{
    /* 1. T.O, empty (no flight plan) */
    perf_reset(0);
    perf_open("fms/active/perf");
    perf_snap(dir, "perf_to_empty");

    /* 2. T.O filled: V1/VR/V2, FLEX 55, defaults THR RED/ACC (small), F/S/GD */
    perf_reset(1);
    perf_init_flight();
    perf_fill_to();
    perf_open("fms/active/perf");
    perf_snap(dir, "perf_to_filled");

    /* 3. T.O with a field focused and a V1/VR/V2 DISAGREE message in the footer */
    perf_enter(A380X_FMS_F_V1, 160);
    perf_fms_run(0.5);
    click(V1_X, V_YC1);
    perf_snap(dir, "perf_to_disagree");

    /* 4. CLB before take-off (PRESEL / MANAGED / ECON, THR RED grid, CLB SPD LIM) */
    perf_reset(1);
    perf_init_flight();
    perf_enter(A380X_FMS_F_PRESEL_CLB, 280);
    perf_fms_run(0.5);
    perf_open("fms/active/perf/clb");
    perf_snap(dir, "perf_clb");

    /* 5. CLB in flight, managed, below the speed limit altitude */
    perf_reset(1);
    perf_init_flight();
    g_fin.on_ground = 0;
    g_fin.baro_alt_ft = 6000;
    g_fin.pressure_alt_ft = 6000;
    perf_set_phase(A380X_FMS_PHASE_CLIMB);
    a380x_fms_update(&g_fms, &g_fin, 0.0);
    perf_set_phase(A380X_FMS_PHASE_CLIMB);
    perf_open("fms/active/perf");
    perf_snap(dir, "perf_clb_inflight");

    /* 6. CRZ before take-off, destination prediction available (ETA / EFOB) */
    perf_reset(1);
    perf_init_flight();
    a380x_fms_set_destination_prediction(&g_fms, 3000, 7 * 3600 + 25 * 60, 21400);
    perf_fms_run(0.5);
    perf_open("fms/active/perf/crz");
    perf_snap(dir, "perf_crz");

    /* 7. DES in descent (managed): MACH input, green 28 px mode label */
    perf_reset(1);
    perf_init_flight();
    a380x_fms_set_destination_prediction(&g_fms, 150, 25 * 60, 21400);
    perf_fms_run(0.5);
    perf_set_phase(A380X_FMS_PHASE_DESCENT);
    perf_open("fms/active/perf");
    perf_snap(dir, "perf_des");

    /* 8. APPR filled (QNH, temp, wind, minimums, VLS/VAPP) */
    perf_reset(1);
    perf_init_flight();
    a380x_fms_set_destination_prediction(&g_fms, 3000, 7 * 3600 + 25 * 60, 21400);
    perf_fill_appr();
    perf_open("fms/active/perf/appr");
    perf_snap(dir, "perf_appr");

    /* 9. GA */
    perf_open("fms/active/perf/ga");
    perf_snap(dir, "perf_ga");

    /* 9b. SEC1 copy of the same plan: static values white, OPT/REC MAX/VREF/POS MONITOR hidden */
    PCHECK(a380x_fms_sec_copy_from_active(&g_fms, 1));
    perf_open("fms/sec1/perf/appr");
    perf_snap(dir, "perf_sec1_appr");

    /* 9c. T.O tab after take-off: every T.O entry inactive (static green) */
    perf_reset(1);
    perf_init_flight();
    perf_fill_to();
    perf_set_phase(A380X_FMS_PHASE_CLIMB);
    perf_open("fms/active/perf/to");
    perf_snap(dir, "perf_to_inactive");

    /* 10. CONFIRM T.O SPDs (speeds to be confirmed after a runway change, white-box) */
    perf_reset(1);
    perf_init_flight();
    g_fms.data.v1_tbc = 141;
    g_fms.data.vr_tbc = 146;
    g_fms.data.v2_tbc = 151;
    perf_open("fms/active/perf");
    perf_snap(dir, "perf_to_confirm");

    /* 11. ACTIVATE APPR dialog in cruise */
    perf_reset(1);
    perf_init_flight();
    perf_set_phase(A380X_FMS_PHASE_CRUISE);
    perf_open("fms/active/perf");
    click(117.76f + 70.0f, 916.0f);
    perf_snap(dir, "perf_activate_appr");

    /* 12. Engine out in climb: EO MAX amber, EO-ECON, CLEAR EO dialog */
    perf_reset(1);
    perf_init_flight();
    perf_set_phase(A380X_FMS_PHASE_CLIMB);
    g_fms.data.engine_out = 1;
    perf_sync();
    perf_open("fms/active/perf");
    {
        float x = 2.0f + mfd_button_auto_width("RETURN") + 5.0f + 152.14f + 5.0f + mfd_button_auto_width("POS MONITOR") +
                  5.0f;
        click(x + 50.0f, 916.0f);
    }
    perf_snap(dir, "perf_clear_eo");
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    if (!mfd_font_decode(g_atlas)) {
        fprintf(stderr, "font decode failed\n");
        return 1;
    }
    a380x_mfd_page_data_register();
    a380x_mfd_page_perf_register();

    test_tab_logic();
    test_interactions();
    previews(dir);
    printf("test_mfd_page_perf: OK (%d checks)\n", perf_checks);
    return 0;
}
