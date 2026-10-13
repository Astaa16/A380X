/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Unit tests for the A380X FMS core (WP-C): spec 07 §12 and spec 04 Appendix B test vectors,
 * entry rules, phase manager, messages, SEC/TMPY data stores and outputs.
 * Build: clang -std=c11 -O2 -Wall -Wextra -Werror test_fms_core.c a380x_fms.c a380x_fms_speeds.c
 *        a380x_fms_messages.c a380x_mfd_formats.c -lm
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "a380x_fms.h"
#include "a380x_fms_speeds.h"

static int checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); assert(c); } } while (0)
#define CLOSE(a, b, tol) do { double a_ = (a), b_ = (b); checks++; \
    if (!(fabs(a_ - b_) <= (tol))) { fprintf(stderr, "%s:%d: %s = %.6f, expected %.6f\n", __FILE__, __LINE__, #a, a_, b_); assert(0); } } while (0)

/* ---------------------------------------------------------------------------------------- */
/* fakes                                                                                     */
/* ---------------------------------------------------------------------------------------- */

static int fake_airport(void *ctx, const char *icao, A380XFmsAirportInfo *o)
{
    (void)ctx;
    static const struct { const char *id; double elev, lat, lon, ta, tl; } db[] = {
        {"EGLL", 83, 51.4775, -0.4614, 6000, 70},
        {"KJFK", 13, 40.6398, -73.7789, 18000, 180},
        {"EGKK", 202, 51.1481, -0.1903, 6000, 70},
        {"KBOS", 20, 42.3656, -71.0096, 18000, 180},
        {"LFPG", 392, 49.0097, 2.5479, 5000, NAN},
        {"ZERO", 0, 0.0, 0.0, NAN, NAN},
    };
    for (size_t i = 0; i < sizeof db / sizeof db[0]; i++) {
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

/* Minimal lateral plan store owned by the test (opaque to the FMS). */
struct A380XFlightPlans {
    int lateral_tmpy;
    int sec[A380X_FMS_NUM_PLANS];
    A380XFmsRunwayInfo origin_rwy[A380X_FMS_NUM_PLANS], dest_rwy[A380X_FMS_NUM_PLANS];
    int approach_type;
    double lowest_clb_cstr;
    int calls_city_pair, calls_swap, calls_insert;
};

static int fp_exists(struct A380XFlightPlans *p, int plan)
{
    if (plan == A380X_FMS_PLAN_TMPY) return p->lateral_tmpy;
    if (plan >= A380X_FMS_PLAN_SEC1) return p->sec[plan];
    return 1;
}
static int fp_sec_copy(struct A380XFlightPlans *p, int s) { p->sec[s] = 1; return 1; }
static int fp_sec_copy2(struct A380XFlightPlans *p, int src, int s) { p->sec[s] = p->sec[src]; return p->sec[s]; }
static int fp_sec_del(struct A380XFlightPlans *p, int s) { p->sec[s] = 0; return 1; }
static int fp_origin_rwy(struct A380XFlightPlans *p, int plan, A380XFmsRunwayInfo *o) { *o = p->origin_rwy[plan]; return o->valid; }
static int fp_dest_rwy(struct A380XFlightPlans *p, int plan, A380XFmsRunwayInfo *o) { *o = p->dest_rwy[plan]; return o->valid; }
static int fp_approach(struct A380XFlightPlans *p, int plan, A380XFmsApproachInfo *o)
{
    (void)plan;
    o->valid = p->approach_type != 0;
    o->type = (A380XFmsApproachType)p->approach_type;
    snprintf(o->name, sizeof o->name, "%s", p->approach_type == A380X_FMS_APPR_ILS ? "ILS22L" : "RNV22L");
    return o->valid;
}
static double fp_lowest(struct A380XFlightPlans *p, int plan) { (void)plan; return p->lowest_clb_cstr; }
static int fp_city(struct A380XFlightPlans *p, int plan, const char *a, const char *b, const char *c)
{
    (void)plan; (void)a; (void)b; (void)c;
    p->calls_city_pair++;
    return 1;
}
static int fp_insert(struct A380XFlightPlans *p) { p->calls_insert++; p->lateral_tmpy = 0; return 1; }
static int fp_erase(struct A380XFlightPlans *p) { p->lateral_tmpy = 0; return 1; }
static int fp_swap(struct A380XFlightPlans *p, int s) { (void)s; p->calls_swap++; return 1; }

static struct A380XFlightPlans g_fp;

static A380XFmsRunwayInfo rwy(const char *id, double elev, double len, double brg)
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

static void make_fms(A380XFms *f, int with_fpln)
{
    A380XFmsNavHooks nav = {NULL, fake_airport};
    A380XFmsPlanHooks fp;
    memset(&fp, 0, sizeof fp);
    memset(&g_fp, 0, sizeof g_fp);
    g_fp.lowest_clb_cstr = NAN;
    for (int i = 0; i < A380X_FMS_NUM_PLANS; i++) {
        g_fp.origin_rwy[i].valid = 0;
        g_fp.dest_rwy[i].valid = 0;
    }
    if (with_fpln) {
        fp.plans = &g_fp;
        fp.plan_exists = fp_exists;
        fp.origin_runway = fp_origin_rwy;
        fp.destination_runway = fp_dest_rwy;
        fp.approach = fp_approach;
        fp.lowest_climb_constraint_ft = fp_lowest;
        fp.new_city_pair = fp_city;
        fp.tmpy_insert = fp_insert;
        fp.tmpy_erase = fp_erase;
        fp.sec_swap = fp_swap;
        fp.sec_copy_from_active = fp_sec_copy;
        fp.sec_copy_from_sec = fp_sec_copy2;
        fp.sec_init = fp_sec_copy;
        fp.sec_delete = fp_sec_del;
    }
    a380x_fms_init(f, NULL, &nav, with_fpln ? &fp : NULL);
}

static A380XFmsInputs base_inputs(void)
{
    A380XFmsInputs in;
    memset(&in, 0, sizeof in);
    in.utc_seconds = 12 * 3600 + 34 * 60;
    for (int i = 0; i < 3; i++) in.fmc_powered[i] = 1;
    in.switching_knob = 1;
    in.position_valid = 1;
    in.lat_deg = 51.4775;
    in.lon_deg = -0.4614;
    in.gps_primary = 0;
    in.adr_valid = 1;
    in.baro_alt_ft = 83;
    in.pressure_alt_ft = 83;
    in.sat_valid = 1;
    in.sat_c = 15;
    in.static_pressure_valid = 1;
    in.static_pressure_hpa = 1013.25;
    in.on_ground = 1;
    in.fcu_alt_valid = 1;
    in.fcu_selected_alt_ft = 5000;
    in.speed_managed = 1;
    in.cold_and_dark_spawn = 1;
    in.wind_dir_true_deg = in.wind_speed_kt = 0;
    return in;
}

static void run(A380XFms *f, const A380XFmsInputs *in, double seconds)
{
    int n = (int)(seconds / 0.05 + 0.5);
    for (int i = 0; i < n; i++) a380x_fms_update(f, in, 0.05);
}

static double getv(const A380XFms *f, int plan, A380XFmsField fld) { return a380x_fms_get(f, plan, fld).v; }

static void city_pair(A380XFms *f, int plan, const char *from, const char *to)
{
    MfdFmsError e;
    CHECK(a380x_fms_enter_text(f, plan, A380X_FMS_T_FROM, from, &e));
    CHECK(a380x_fms_enter_text(f, plan, A380X_FMS_T_TO, to, &e));
}

/* ---------------------------------------------------------------------------------------- */
/* spec 07 §12.1 speeds                                                                      */
/* ---------------------------------------------------------------------------------------- */

static void test_interpolation(void)
{
    static const double k[3] = {0, 10, 20}, v[3] = {0, 100, 300};
    CLOSE(a380x_lerp1(k, v, 3, -5), 0, 1e-12);
    CLOSE(a380x_lerp1(k, v, 3, 5), 50, 1e-12);
    CLOSE(a380x_lerp1(k, v, 3, 15), 200, 1e-12);
    CLOSE(a380x_lerp1(k, v, 3, 99), 300, 1e-12);
    /* clamping in both dimensions */
    CLOSE(a380x_speeds_vls_appr(A380X_APPR_CONF_FULL, 20, 100000), 123, 1e-9);
    CLOSE(a380x_speeds_vls_appr(A380X_APPR_CONF_1, 50, 700000), 195, 1e-9);
    CLOSE(a380x_speeds_green_dot(0, 450000), 216, 1e-9);           /* App. B: clamped to 10000 */
    CLOSE(a380x_speeds_vls_conf0(50000, 600000), 250, 1e-9);
    CLOSE(a380x_speeds_vls_appr(0, 35, 400000), a380x_speeds_vls_conf0(0, 400000), 1e-9);
    CHECK(isnan(a380x_speeds_vls_appr(6, 35, 400000)));
    CLOSE(a380x_speeds_vmca(-5000), 115, 1e-9);
    CLOSE(a380x_speeds_vmcg(3000), 115.5, 1e-9);
    CLOSE(a380x_js_round(2.5), 3, 0);
    CLOSE(a380x_js_round(-2.5), -2, 0);
    CLOSE(a380x_js_round(0.49999999999999994), 0, 0);
}

static void test_speed_vectors(void)
{
    A380XOperatingSpeeds s;
    /* A: PREFLIGHT, 500 t, CG 30, 0 ft, lever 0, V2 150 */
    a380x_operating_speeds(500000, 30, 0, 0, 0, 150, 0, 0, 0, 0, 1013.25, &s);
    CLOSE(s.vs1g, 170.732, 1e-3);
    CLOSE(s.vls, 196.341, 1e-3);
    CLOSE(a380x_js_round(s.vls), 196, 0);
    CLOSE(s.f2, 174.259, 1e-3);
    CLOSE(ceil(fmax(s.f2, 128)), 175, 0);
    CLOSE(s.s, 206.585, 1e-3);
    CLOSE(ceil(s.s), 207, 0);
    CLOSE(ceil(s.gd), 228, 0);
    CLOSE(s.vmax, 340, 1e-9); /* min(VMO, Mmo at sea level) */
    CLOSE(s.vfe_next, 263, 0);
    /* D: CRUISE, 450 t, CG 38, FL350, lever 0 */
    a380x_operating_speeds(450000, 38, 280, 0, 3, 150, 35000, 0, 0, 0, 238.42, &s);
    CLOSE(s.vls, 233.0, 1e-9);
    CLOSE(s.gd, 254.0, 1e-9);
    CLOSE(s.vmax, a380x_mach_to_cas(0.89, 238.42), 1e-9); /* Mmo limited */
    /* E: CLIMB, 480 t, CG 33, 3000 ft, lever 2 */
    a380x_operating_speeds(480000, 33, 200, 2, 2, 150, 3000, 0, 0, 0, 1013.25, &s);
    CLOSE(s.vls, 160.657, 1e-3);
    CLOSE(a380x_js_round(s.vls), 161, 0);
    CLOSE(s.f2, 187.286, 1e-3);
    CLOSE(ceil(fmax(s.f2, 138)), 188, 0);
    CLOSE(s.s, 202.6, 1e-9);
    CLOSE(ceil(s.s), 203, 0);
    CLOSE(s.vmax, 220, 0);
    CLOSE(s.vfe_next, 196, 0);
    /* F: CLIMB, lever 1, CAS 220 (CONF 1) */
    a380x_operating_speeds(480000, 33, 220, 1, 2, 150, 3000, 0, 0, 0, 1013.25, &s);
    CLOSE(s.vs1g, 143.647, 1e-3);
    CLOSE(s.vls, 169.503, 1e-3);
    CLOSE(a380x_js_round(s.vls), 170, 0);
    CLOSE(s.vapp, 181.686, 1e-3);
    CLOSE(s.vmax, 263, 0);
    CLOSE(s.vfe_next, 222, 0);
    /* same lever, CAS <= 212: CONF 1+F table */
    a380x_operating_speeds(480000, 33, 200, 1, 2, 150, 3000, 0, 0, 0, 1013.25, &s);
    CLOSE(s.vmax, 222, 0);
    CLOSE(s.vfe_next, 220, 0);
    /* G: TAKEOFF, lever 2, V2 160 */
    a380x_operating_speeds(480000, 33, 0, 2, 1, 160, 0, 0, 0, 0, 1013.25, &s);
    CLOSE(s.vs1g, 130.616, 1e-3);
    CLOSE(a380x_js_round(s.vls), 150, 0);
    /* lever FULL: VFE next 0 */
    a380x_operating_speeds(400000, 35, 140, 4, 5, 150, 0, 0, 0, 0, 1013.25, &s);
    CLOSE(s.vmax, 182, 0);
    CLOSE(s.vfe_next, 0, 0);
    /* speed-brake increment: clean, full speedbrake -> +25 kt; ignored for approach speeds */
    A380XOperatingSpeeds s0, s1;
    a380x_operating_speeds(450000, 35, 250, 0, 2, 150, 10000, 0, 0, 0.0, 1013.25, &s0);
    a380x_operating_speeds(450000, 35, 250, 0, 2, 150, 10000, 0, 0, 1.0, 1013.25, &s1);
    CLOSE(s1.vls - s0.vls, 25, 1e-9);
    a380x_operating_speeds(450000, 35, 250, 0, 2, 150, 10000, 0, 1, 1.0, 1013.25, &s1);
    CLOSE(s1.vls - s0.vls, 0, 1e-9);

    /* B / C via the approach configuration */
    a380x_operating_speeds(380000, 35, 0, 4, 5, 150, 0, 0, 1, 0, 1013.25, &s);
    CLOSE(s.vls, 134.686, 1e-3);
    CLOSE(ceil(s.vls), 135, 0);
    CLOSE(ceil(s.vapp), 140, 0);
    CLOSE(ceil(s.vref), 135, 0);
    CLOSE(s.f2, 166.886, 1e-3);
    CLOSE(ceil(s.f2), 167, 0);
    CLOSE(s.s, 180.2, 1e-9);
    CLOSE(ceil(s.s), 181, 0);
    CLOSE(ceil(s.gd), 199, 0);
    a380x_operating_speeds(380000, 35, 0, 3, 5, 150, 0, 30, 1, 0, 1013.25, &s);
    CLOSE(s.vls, 138.457, 1e-3);
    CLOSE(ceil(s.vls), 139, 0);
    CLOSE(ceil(s.vapp), 149, 0);
}

/* spec 04 Appendix B */
static void test_appendix_b(void)
{
    A380XOperatingSpeeds s;
    a380x_operating_speeds(450000, 35, 0, 0, 0, 150, 0, 0, 0, 0, 1013.25, &s);
    CLOSE(ceil(s.gd), 216, 0);
    CLOSE(ceil(s.s), 196, 0);
    CLOSE(ceil(fmax(s.f2, 128)), 164, 0);
    a380x_operating_speeds(400000, 35, 0, 4, 5, 150, 0, 0, 1, 0, 1013.25, &s);
    CLOSE(s.vls, 138.2857, 1e-3);
    CLOSE(ceil(s.vls), 139, 0);
    CLOSE(ceil(s.vapp), 144, 0);
    CLOSE(ceil(s.vref), 139, 0);
    a380x_operating_speeds(400000, 35, 0, 4, 5, 150, 0, 30, 1, 0, 1013.25, &s);
    CLOSE(ceil(s.vapp), 149, 0);
    a380x_operating_speeds(400000, 35, 0, 4, 5, 150, 0, 60, 1, 0, 1013.25, &s);
    CLOSE(ceil(s.vapp), 154, 0);
    a380x_operating_speeds(400000, 35, 0, 4, 5, 150, 0, -20, 1, 0, 1013.25, &s); /* tailwind: still +5 */
    CLOSE(ceil(s.vapp), 144, 0);
    CLOSE(a380x_econ_climb_speed(100), 290.40, 0.005);
    CLOSE(a380x_econ_descent_speed(100), 289, 0);
}

/* §12.2 altitudes */
static void test_altitudes(void)
{
    static const double v[4][6] = {
        {500000, 0, 37461.92, 375, 360, 300},
        {400000, 0, 41861.19, 420, 400, 335},
        {560000, 10, 32448.87, 325, 310, 260},
        {300000, -5, 45998.17, 430, 410, 340},
    };
    for (int i = 0; i < 4; i++) {
        double rec = fmin(a380x_rec_max_alt_ft(v[i][0], v[i][1]), 43000) / 100.0;
        CLOSE(rec, v[i][3], 0);
        CLOSE(floor(0.96 * rec / 5) * 5, v[i][4], 0);
        CLOSE(floor(0.80 * rec / 5) * 5, v[i][5], 0);
    }
    CLOSE(a380x_isa_temp_c(35000), -54.342, 1e-3);
    CLOSE(a380x_isa_temp_c(41000), a380x_isa_temp_c(36089), 1e-12);

    /* through the FMS: GW from FQMS, ISA, OPT null when EO */
    A380XFms f;
    make_fms(&f, 0);
    A380XFmsInputs in = base_inputs();
    in.fqms_gw_valid = 1;
    in.fqms_gw_kg = 500000;
    in.sat_c = a380x_isa_temp_c(in.baro_alt_ft);
    run(&f, &in, 0.3);
    CLOSE(getv(&f, 0, A380X_FMS_F_REC_MAX_FL), 375, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_OPT_FL), 360, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_EO_MAX_FL), 300, 0);
    CHECK(!a380x_fms_get(&f, A380X_FMS_PLAN_SEC1, A380X_FMS_F_REC_MAX_FL).has);
}

/* §12.3 managed speeds and Mach */
static void test_managed_laws(void)
{
    static const double ci[5] = {0, 100, 300, 500, 999};
    static const double clb[5] = {290.0, 290.4, 293.61, 300.02, 330.0};
    static const double des[5] = {288, 289, 292, 294, 300};
    for (int i = 0; i < 5; i++) {
        CLOSE(a380x_econ_climb_speed(ci[i]), clb[i], 0.006);
        CLOSE(a380x_econ_descent_speed(ci[i]), des[i], 0);
    }
    CLOSE(a380x_econ_climb_speed(NAN), 250, 0);
    CLOSE(a380x_econ_descent_speed(NAN), 300, 0);
    CLOSE(a380x_mach_to_cas(0.89, 1013.25), 588.71, 0.01);
    CLOSE(a380x_mach_to_cas(0.85, 1013.25), 562.25, 0.01);
    CLOSE(a380x_mach_to_cas(0.84, 1013.25), 555.64, 0.01);
    CLOSE(a380x_mach_to_cas(0.89, 238.42), 306.37, 0.01);
    CLOSE(a380x_mach_to_cas(0.85, 238.42), 290.92, 0.01);
    CLOSE(a380x_mach_to_cas(0.84, 238.42), 287.10, 0.01);
    CLOSE(a380x_cas_to_mach(a380x_mach_to_cas(0.84, 238.42), 238.42), 0.84, 1e-9);
    double cas;
    CHECK(a380x_managed_targets(293.61, 0.84, 1, 238.42, &cas) == 1);
    CLOSE(cas, 287.10, 0.01);
    CHECK(a380x_managed_targets(280, 0.84, 1, 238.42, &cas) == 0);
    CLOSE(cas, 280, 0);
    CHECK(a380x_managed_targets(293.61, 0.84, 0, 238.42, &cas) == 0);
    CLOSE(a380x_decel_dist_nm(250, 240), 3, 0);
    CLOSE(a380x_decel_dist_nm(400, 200), 20, 0);
    CLOSE(a380x_vmo_mmo(NAN), 340, 0);
}

/* §12.5 headwind / VAPP */
static void test_wind(void)
{
    CLOSE(a380x_angle_diff_deg(270, 240), 30, 1e-12);
    CLOSE(a380x_angle_diff_deg(10, 350), 20, 1e-12);
    CLOSE(a380x_headwind_kt(20, 270, 240), 17.32, 0.005);
    CLOSE(a380x_wind_component_add(17.32), 6, 0);
    CLOSE(a380x_wind_component_add(0), 5, 0);
    CLOSE(a380x_wind_component_add(100), 15, 0);
    CLOSE(fabs(a380x_crosswind_kt(20, 270, 240)), 10.0, 1e-9);
    CLOSE(a380x_gs_mini_target(140, 30, 15, 4, 182, 0), 145, 0);
    CLOSE(a380x_gs_mini_target(140, 0, 15, 4, 182, 0), 140, 0);
}

/* §12.4 fuel and LW */
static void test_fuel(void)
{
    A380XFms f;
    MfdFmsError e;
    make_fms(&f, 0);
    A380XFmsInputs in = base_inputs();
    run(&f, &in, 0.3);
    city_pair(&f, 0, "EGLL", "KJFK");
    CHECK(a380x_fms_enter_text(&f, 0, A380X_FMS_T_ALTN, "KBOS", &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_ZFW, 300000, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_ZFWCG, 30, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_BLOCK, 100000, &e));
    run(&f, &in, 0.3);
    CLOSE(getv(&f, 0, A380X_FMS_F_TOW), 398500, 1e-6);
    CHECK(!a380x_fms_get(&f, 0, A380X_FMS_F_TRIP).has); /* no VNAV: dashes */
    CHECK(!a380x_fms_get(&f, 0, A380X_FMS_F_LW).has);
    CHECK(!a380x_fms_get(&f, 0, A380X_FMS_F_EXTRA).has);
    A380XFmsValue taxi = a380x_fms_get(&f, 0, A380X_FMS_F_TAXI);
    CLOSE(taxi.v, 1500, 0);
    CHECK(taxi.entered == 0 && taxi.src == A380X_FMS_SRC_DEFAULT);
    a380x_fms_set_destination_prediction(&f, 3000, 7 * 3600, 40000); /* VNAV TRIP = 60 t */
    run(&f, &in, 0.3);
    CLOSE(getv(&f, 0, A380X_FMS_F_TRIP), 60000, 1e-6);
    CLOSE(getv(&f, 0, A380X_FMS_F_LW), 338500, 1e-6);
    CLOSE(getv(&f, 0, A380X_FMS_F_RTE_RSV), 3000, 1e-6);
    CLOSE(getv(&f, 0, A380X_FMS_F_RTE_RSV_PCT), 5, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_ALTN_FUEL), 6500, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_FINAL_FUEL), 6000, 1e-9);
    CLOSE(getv(&f, 0, A380X_FMS_F_FINAL_TIME), 30, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_MIN_DEST_FOB), 12500, 1e-9);
    CHECK(a380x_fms_get(&f, 0, A380X_FMS_F_MIN_DEST_FOB).entered == 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_EXTRA), 23000, 1e-6);
    CLOSE(getv(&f, 0, A380X_FMS_F_EXTRA_TIME), 23000 / 200.0 * 60.0, 1e-6);
    CLOSE(getv(&f, 0, A380X_FMS_F_DEST_EFOB), 40000, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_ALTN_EFOB), 33500, 0); /* quirk 5 fixed: kg */
    CLOSE(getv(&f, 0, A380X_FMS_F_DEST_ETA), 7 * 3600, 0); /* PREFLIGHT: span */
    CHECK(!a380x_fms_get(&f, A380X_FMS_PLAN_SEC1, A380X_FMS_F_TRIP).has);

    /* RTE RSV fuel / % are mutually exclusive */
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_RTE_RSV, 4500, &e));
    A380XFmsValue pct = a380x_fms_get(&f, 0, A380X_FMS_F_RTE_RSV_PCT);
    CLOSE(pct.v, 7.5, 1e-9);
    CHECK(pct.entered == 0);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_RTE_RSV_PCT, 3, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_RTE_RSV), 1800, 1e-6);
    CHECK(a380x_fms_get(&f, 0, A380X_FMS_F_RTE_RSV).entered == 0);
    /* FINAL time 45 min -> FINAL fuel 9.0 t, small font (spec 03 §13 4.2) */
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_FINAL_TIME, 45, &e));
    A380XFmsValue fin = a380x_fms_get(&f, 0, A380X_FMS_F_FINAL_FUEL);
    CLOSE(fin.v, 9000, 1e-9);
    CHECK(fin.entered == 0);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_FINAL_FUEL, 4000, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_FINAL_TIME), 20, 1e-9);
    CHECK(a380x_fms_get(&f, 0, A380X_FMS_F_FINAL_TIME).entered == 0);
    CHECK(a380x_fms_clear(&f, 0, A380X_FMS_F_FINAL_FUEL, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_FINAL_FUEL), 6000, 1e-9);
    /* spec 03 §13 4.1: ZFW 300, BLOCK 120, TAXI 1.5 -> TOW 418.5 t */
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_BLOCK, 120000, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_TOW), 418500, 1e-6);
    /* ALTN NONE clears the ALTN fuel */
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_ALTN_FUEL, 8000, &e));
    CHECK(a380x_fms_enter_text(&f, 0, A380X_FMS_T_ALTN, "NONE", &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_ALTN_FUEL), 0, 0);
    CHECK(a380x_fms_field_flags(&f, 0, A380X_FMS_F_ALTN_FUEL, A380X_FMS_PAGE_FUEL_LOAD) & A380X_FMS_FF_DISABLED);
    char buf[16];
    a380x_fms_get_text(&f, 0, A380X_FMS_T_ALTN, buf, sizeof buf);
    CHECK(strcmp(buf, "NONE") == 0);

    /* DEST EFOB BELOW MIN hysteresis (MIN 12.5 t) */
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_MIN_DEST_FOB, 12500, &e));
    static const double efob[4] = {12600, 12400, 12700, 12810};
    static const int state[4] = {0, 1, 1, 0};
    for (int i = 0; i < 4; i++) {
        a380x_fms_set_destination_prediction(&f, 3000, 3600, efob[i]);
        run(&f, &in, 0.25);
        CHECK(f.data.dest_efob_below_min == state[i]);
        CHECK(a380x_fms_outputs(&f)->dest_efob_below_min == state[i]);
        if (state[i]) CHECK(a380x_fms_get(&f, 0, A380X_FMS_F_DEST_EFOB).colour == A380X_FMS_COLOUR_AMBER);
    }
    /* message only in CRZ/DES etc.: PREFLIGHT -> none */
    CHECK(!mfd_msg_has_id(&f.msgs, MFD_MSG_DEST_EFOB_BELOW_MIN));
    /* CHECK MIN FUEL AT DEST: pilot MIN < ALTN + FINAL */
    CHECK(a380x_fms_enter_text(&f, 0, A380X_FMS_T_ALTN, "KBOS", &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_MIN_DEST_FOB, 10000, &e));
    run(&f, &in, 0.25);
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_CHECK_MIN_FUEL_AT_DEST));
    CHECK(a380x_fms_clear(&f, 0, A380X_FMS_F_MIN_DEST_FOB, &e));
    run(&f, &in, 0.5);
    CHECK(!mfd_msg_has_id(&f.msgs, MFD_MSG_CHECK_MIN_FUEL_AT_DEST));
}

/* ---------------------------------------------------------------------------------------- */
/* entries, defaults, messages                                                                */
/* ---------------------------------------------------------------------------------------- */

static void test_init_entries(void)
{
    A380XFms f;
    MfdFmsError e;
    make_fms(&f, 1);
    A380XFmsInputs in = base_inputs();
    run(&f, &in, 0.3);
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_PREFLIGHT);
    CHECK(a380x_fms_loaded_plan(&f, A380X_FMS_PLAN_ACTIVE) == A380X_FMS_PLAN_ACTIVE);
    CHECK(a380x_fms_loaded_plan(&f, A380X_FMS_PLAN_SEC1) == -1);

    /* mandatory / disabled before FROM/TO (spec 03 §13 check 3) */
    CHECK(a380x_fms_text_flags(&f, 0, A380X_FMS_T_FLT_NBR, A380X_FMS_PAGE_INIT) & A380X_FMS_FF_MANDATORY);
    CHECK(a380x_fms_text_flags(&f, 0, A380X_FMS_T_ALTN, A380X_FMS_PAGE_INIT) & A380X_FMS_FF_DISABLED);
    unsigned crz = a380x_fms_field_flags(&f, 0, A380X_FMS_F_CRZ_FL, A380X_FMS_PAGE_INIT);
    CHECK((crz & A380X_FMS_FF_DISABLED) && (crz & A380X_FMS_FF_MANDATORY) && !(crz & A380X_FMS_FF_CLEARABLE));
    CHECK(a380x_fms_field_flags(&f, 0, A380X_FMS_F_CI, A380X_FMS_PAGE_INIT) & A380X_FMS_FF_DISABLED);
    CHECK(!a380x_fms_enter(&f, 0, A380X_FMS_F_CRZ_FL, 350, &e) && e.type == MFD_ERR_NOT_ALLOWED);

    /* NOT IN DATABASE (quirk 16) and FORMAT */
    CHECK(!a380x_fms_enter_text(&f, 0, A380X_FMS_T_FROM, "XXXX", &e) && e.type == MFD_ERR_NOT_IN_DATABASE);
    CHECK(!a380x_fms_enter_text(&f, 0, A380X_FMS_T_FROM, "EGL", &e) && e.type == MFD_ERR_FORMAT_ERROR);
    char buf[64];
    CHECK(!a380x_fms_get_text(&f, 0, A380X_FMS_T_FROM, buf, sizeof buf));

    CHECK(a380x_fms_enter_text(&f, 0, A380X_FMS_T_FLT_NBR, "BAW117", &e));
    CHECK(a380x_fms_enter_text(&f, 0, A380X_FMS_T_FROM, "EGLL", &e));
    CHECK(!a380x_fms_has_active_flight_plan(&f));
    a380x_fms_get_text(&f, 0, A380X_FMS_T_FROM, buf, sizeof buf);
    CHECK(strcmp(buf, "EGLL") == 0);
    CHECK(a380x_fms_enter_text(&f, 0, A380X_FMS_T_TO, "KJFK", &e));
    CHECK(a380x_fms_has_active_flight_plan(&f));
    CHECK(g_fp.calls_city_pair == 1);
    a380x_fms_get_text(&f, 0, A380X_FMS_T_ALTN, buf, sizeof buf);
    CHECK(strcmp(buf, "NONE") == 0);
    a380x_fms_get_text(&f, 0, A380X_FMS_T_CPNY_RTE, buf, sizeof buf);
    CHECK(strcmp(buf, "NONE") == 0);
    a380x_fms_get_text(&f, 0, A380X_FMS_T_FLT_NBR, buf, sizeof buf);
    CHECK(strcmp(buf, "BAW117") == 0); /* kept across newCityPair */
    run(&f, &in, 0.25);
    a380x_fms_get_text(&f, 0, A380X_FMS_T_CALLSIGN, buf, sizeof buf);
    CHECK(strcmp(buf, "BAW117") == 0);
    CHECK(strcmp(a380x_fms_outputs(&f)->flight_number, "BAW117") == 0);
    CHECK(!a380x_fms_enter_text(&f, 0, A380X_FMS_T_FLT_NBR, NULL, &e) && e.type == MFD_ERR_NONE);

    /* origin / destination defaults (spec 07 §6.1): EGLL 83 ft, KJFK 13 ft */
    A380XFmsValue thr = a380x_fms_get(&f, 0, A380X_FMS_F_THR_RED);
    CLOSE(thr.v, 1580, 0); /* round10(83 + 1500) */
    CHECK(thr.entered == 0 && thr.src == A380X_FMS_SRC_DEFAULT);
    CLOSE(getv(&f, 0, A380X_FMS_F_ACC), 1580, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_EO_ACC), 1580, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_GA_THR_RED), 1510, 0);
    A380XFmsValue ta = a380x_fms_get(&f, 0, A380X_FMS_F_TRANS_ALT);
    CLOSE(ta.v, 6000, 0);
    CHECK(ta.src == A380X_FMS_SRC_DATABASE && !ta.entered);
    CLOSE(getv(&f, 0, A380X_FMS_F_TRANS_FL), 180, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_GA_TRANS), 18000, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_TROPO), 36090, 0);
    CHECK(a380x_fms_get(&f, 0, A380X_FMS_F_TROPO).entered == 0);
    MfdFmt fmt;
    a380x_fms_field_fmt(&f, 0, A380X_FMS_F_THR_RED, &fmt);
    CHECK(fmt.kind == MFD_FMT_ALT_OR_FL && fmt.has_trans && fmt.trans == 6000 && !fmt.trans_is_fl);
    a380x_fms_field_fmt(&f, 0, A380X_FMS_F_GA_ACC, &fmt);
    CHECK(fmt.kind == MFD_FMT_ALT_OR_FL && fmt.trans == 180 && fmt.trans_is_fl);
    a380x_fms_field_fmt(&f, 0, A380X_FMS_F_ZFW, &fmt);
    CHECK(fmt.kind == MFD_FMT_WEIGHT && fmt.min == 270000 && fmt.max == 373000 && fmt.metric == 1 && fmt.err_details);
    a380x_fms_field_fmt(&f, 0, A380X_FMS_F_FUEL_PENALTY, &fmt);
    CHECK(fmt.kind == MFD_FMT_FUEL_PENALTY && !fmt.err_details);

    /* THR RED / ACC [PORT] validation */
    CHECK(!a380x_fms_enter(&f, 0, A380X_FMS_F_THR_RED, 400, &e) && e.type == MFD_ERR_ENTRY_OUT_OF_RANGE);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_THR_RED, 2004, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_THR_RED), 2000, 0); /* round10 */
    CLOSE(getv(&f, 0, A380X_FMS_F_ACC), 2000, 0);     /* raised to THR RED */
    CHECK(a380x_fms_get(&f, 0, A380X_FMS_F_THR_RED).entered == 1);
    CHECK(!a380x_fms_enter(&f, 0, A380X_FMS_F_ACC, 1800, &e) && e.type == MFD_ERR_ENTRY_OUT_OF_RANGE);
    CHECK(a380x_fms_clear(&f, 0, A380X_FMS_F_THR_RED, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_THR_RED), 1580, 0);

    /* CRZ FL: trySetCruiseFl */
    CHECK(!a380x_fms_enter(&f, 0, A380X_FMS_F_CRZ_FL, 0, &e) && e.type == MFD_ERR_NONE); /* silent */
    CHECK(!a380x_fms_enter(&f, 0, A380X_FMS_F_CRZ_FL, 999, &e) && e.type == MFD_ERR_ENTRY_OUT_OF_RANGE);
    CHECK(!a380x_fms_clear(&f, 0, A380X_FMS_F_CRZ_FL, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_CRZ_FL, 350, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_CRZ_FL), 350, 0);
    A380XFmsValue ct = a380x_fms_get(&f, 0, A380X_FMS_F_CRZ_TEMP);
    CLOSE(ct.v, -54.342, 1e-3); /* ISA default, small font */
    CHECK(ct.entered == 0);
    run(&f, &in, 0.25);
    CLOSE(a380x_fms_outputs(&f)->cruise_altitude_ft, 35000, 0);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_CRZ_TEMP, -50, &e));
    CHECK(a380x_fms_get(&f, 0, A380X_FMS_F_CRZ_TEMP).entered == 1);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_CRZ_TEMP, 40, &e)); /* [PORT] warmer entries persist */
    run(&f, &in, 0.25);
    CLOSE(getv(&f, 0, A380X_FMS_F_CRZ_TEMP), 40, 0);
    /* CRZ FL ABOVE MAX FL (GW 500 t -> REC MAX 375) */
    in.fqms_gw_valid = 1;
    in.fqms_gw_kg = 500000;
    in.sat_c = a380x_isa_temp_c(in.baro_alt_ft);
    run(&f, &in, 0.25);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_CRZ_FL, 400, &e));
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_CRZ_FL_ABOVE_MAX_FL));

    /* CI / MODE */
    CHECK(!(a380x_fms_field_flags(&f, 0, A380X_FMS_F_CI, A380X_FMS_PAGE_INIT) & A380X_FMS_FF_DISABLED));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_CI_MODE, A380X_FMS_CI_LRC, &e));
    CHECK(a380x_fms_field_flags(&f, 0, A380X_FMS_F_CI, A380X_FMS_PAGE_INIT) & A380X_FMS_FF_DISABLED);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_CI_MODE, A380X_FMS_CI_ECON, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_CI, 100, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_CLB_ECON_SPD), 290.4004, 1e-3);
    CLOSE(getv(&f, 0, A380X_FMS_F_DES_MANAGED_SPD), 289, 0);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_DES_MANAGED_SPD, 280, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_DES_MANAGED_SPD), 280, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_DES_MANAGED_MACH), 0.84, 0);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_TRIP_WIND, -30, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_TROPO, 38000, &e));
    CHECK(a380x_fms_get(&f, 0, A380X_FMS_F_TROPO).entered == 1);

    /* re-entering FROM recreates the plan (FBW newCityPair resets the perf data store) */
    CHECK(a380x_fms_enter_text(&f, 0, A380X_FMS_T_FROM, "EGKK", &e));
    CHECK(g_fp.calls_city_pair == 2);
    CHECK(!a380x_fms_get(&f, 0, A380X_FMS_F_CRZ_FL).has);
    CLOSE(getv(&f, 0, A380X_FMS_F_THR_RED), 1700, 0); /* 202 + 1500 -> 1700 */

    /* defensive range check */
    CHECK(!a380x_fms_enter(&f, 0, A380X_FMS_F_ZFW, 100000, &e) && e.type == MFD_ERR_ENTRY_OUT_OF_RANGE);
    CHECK(strcmp(e.details, "RNG: 270000 TO 373000") == 0);
    /* read-only items reject entries */
    CHECK(!a380x_fms_enter(&f, 0, A380X_FMS_F_TOW, 1, &e) && e.type == MFD_ERR_NOT_ALLOWED);
    /* fuel penalty */
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_FUEL_PENALTY, 5, &e));
    CHECK(a380x_fms_get(&f, -1, A380X_FMS_F_FUEL_PENALTY).entered == 1);
}

static void test_engine_start_messages(void)
{
    A380XFms f;
    MfdFmsError e;
    make_fms(&f, 1);
    A380XFmsInputs in = base_inputs();
    run(&f, &in, 0.3);
    city_pair(&f, 0, "EGLL", "KJFK");
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_PACKS, A380X_FMS_PACKS_OFF_APU, &e));
    CHECK(!mfd_msg_has_id(&f.msgs, MFD_MSG_CHECK_TO_DATA));
    /* engine 2 starting, then N2 > 20 with ZFW/ZFWCG null -> INITIALIZE ZFW / ZFWCG */
    in.engine_starting[1] = 1;
    run(&f, &in, 0.25);
    in.engine_starting[1] = 0;
    in.n2_pct[1] = 60;
    in.fqms_fob_valid = 1;
    in.fqms_fob_kg = 100000;
    run(&f, &in, 0.25);
    CHECK(a380x_fms_engines_were_started(&f));
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_INITIALIZE_ZFW_ZFWCG));
    /* LRC default on engine start: CI 0, LRC */
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_LRC_IN_USE));
    CLOSE(getv(&f, 0, A380X_FMS_F_CI), 0, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_CI_MODE), A380X_FMS_CI_LRC, 0);
    CHECK(a380x_fms_field_flags(&f, 0, A380X_FMS_F_BLOCK, A380X_FMS_PAGE_FUEL_LOAD) & A380X_FMS_FF_HIDDEN);
    CLOSE(getv(&f, 0, A380X_FMS_F_FOB), 100000, 0);
    CHECK(!a380x_fms_get(&f, 0, A380X_FMS_F_GW).has); /* no ZFW yet, FQMS GW invalid */
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_ZFW, 300000, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_ZFWCG, 30, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_GW), 400000, 0);
    run(&f, &in, 0.5);
    CHECK(!mfd_msg_has_id(&f.msgs, MFD_MSG_INITIALIZE_ZFW_ZFWCG));
    /* TOW after engine start = GW - TAXI */
    CLOSE(getv(&f, 0, A380X_FMS_F_TOW), 398500, 1e-6);
    /* PACKS after engine start -> CHECK T.O. DATA */
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_PACKS, A380X_FMS_PACKS_ON, &e));
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_CHECK_TO_DATA));
    /* message shown once per power-up */
    mfd_msg_reset(&f.msgs);
    in.engine_starting[2] = 1;
    run(&f, &in, 0.5);
    CHECK(!mfd_msg_has_id(&f.msgs, MFD_MSG_INITIALIZE_ZFW_ZFWCG));
}

static void test_vspeeds_and_runway_change(void)
{
    A380XFms f;
    MfdFmsError e;
    make_fms(&f, 1);
    A380XFmsInputs in = base_inputs();
    run(&f, &in, 0.3);
    city_pair(&f, 0, "EGLL", "KJFK");
    g_fp.origin_rwy[0] = rwy("27R", 75, 3900, 270);
    run(&f, &in, 0.25);
    CHECK(f.data.vspeeds_for_runway_set && strcmp(f.data.vspeeds_for_runway, "27R") == 0);
    /* mandatory V-speeds on the active plan */
    CHECK(a380x_fms_field_flags(&f, 0, A380X_FMS_F_V1, A380X_FMS_PAGE_PERF_TO) & A380X_FMS_FF_MANDATORY);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_V1, 150, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_VR, 145, &e));
    run(&f, &in, 0.25);
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_V_TO_DISAGREE));
    CHECK(a380x_fms_outputs(&f)->vspeeds_disagree);
    CHECK(a380x_fms_outputs(&f)->to_speeds_not_inserted);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_VR, 155, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_V2, 160, &e));
    run(&f, &in, 0.5);
    CHECK(!mfd_msg_has_id(&f.msgs, MFD_MSG_V_TO_DISAGREE));
    const A380XFmsOutputs *o = a380x_fms_outputs(&f);
    CHECK(!o->vspeeds_disagree && !o->to_speeds_not_inserted);
    CLOSE(o->v1_knots, 150, 0);
    CLOSE(o->vr_knots, 155, 0);
    CLOSE(o->v2_knots, 160, 0);
    /* T.O SHIFT range = runway length */
    MfdFmt fmt;
    a380x_fms_field_fmt(&f, 0, A380X_FMS_F_TO_SHIFT, &fmt);
    CHECK(fmt.kind == MFD_FMT_LENGTH && fmt.min == 1 && fmt.max == 3900);
    CHECK(!a380x_fms_enter(&f, 0, A380X_FMS_F_TO_SHIFT, 4000, &e) && e.type == MFD_ERR_ENTRY_OUT_OF_RANGE);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_TO_SHIFT, 300, &e));

    /* runway change: speeds to be confirmed, CHECK T.O. DATA resolved when VR is set */
    g_fp.origin_rwy[0] = rwy("27L", 78, 3660, 270);
    run(&f, &in, 0.25);
    CHECK(!a380x_fms_get(&f, 0, A380X_FMS_F_V1).has);
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_CHECK_TO_DATA));
    CHECK(a380x_fms_should_show_confirm_to_speeds(&f, 0));
    CLOSE(getv(&f, 0, A380X_FMS_F_VR_TBC), 155, 0);
    CHECK(a380x_fms_field_flags(&f, 0, A380X_FMS_F_TO_F, A380X_FMS_PAGE_PERF_TO) & A380X_FMS_FF_HIDDEN);
    CHECK(a380x_fms_confirm_to_speeds(&f, 0));
    CLOSE(getv(&f, 0, A380X_FMS_F_V2), 160, 0);
    CHECK(!a380x_fms_should_show_confirm_to_speeds(&f, 0));
    run(&f, &in, 0.5);
    CHECK(!mfd_msg_has_id(&f.msgs, MFD_MSG_CHECK_TO_DATA));

    /* THS FOR: 0 and CLR silently ignored (FBW) */
    CHECK(!a380x_fms_enter(&f, 0, A380X_FMS_F_THS_FOR, 0, &e) && e.type == MFD_ERR_NONE);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_THS_FOR, 28.5, &e));
    CHECK(!a380x_fms_clear(&f, 0, A380X_FMS_F_THS_FOR, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_THS_FOR), 28.5, 0);
    /* FLEX: var written only while FLEX is selected (quirk 19) */
    CHECK(a380x_fms_field_flags(&f, 0, A380X_FMS_F_FLEX_TEMP, A380X_FMS_PAGE_PERF_TO) & A380X_FMS_FF_HIDDEN);
    CHECK(!a380x_fms_enter(&f, 0, A380X_FMS_F_FLEX_TEMP, 40, &e) && e.type == MFD_ERR_NOT_ALLOWED);
    CHECK(!a380x_fms_option_enabled(&f, 0, A380X_FMS_F_TO_POWER, A380X_FMS_TO_DERATED));
    CHECK(!a380x_fms_enter(&f, 0, A380X_FMS_F_TO_POWER, A380X_FMS_TO_DERATED, &e) && e.type == MFD_ERR_NOT_ALLOWED);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_TO_POWER, A380X_FMS_TO_FLEX, &e));
    run(&f, &in, 0.25);
    CLOSE(a380x_fms_outputs(&f)->to_flex_temp_c, 0, 0);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_FLEX_TEMP, 0, &e));
    run(&f, &in, 0.25);
    CLOSE(a380x_fms_outputs(&f)->to_flex_temp_c, 0.1, 0);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_FLEX_TEMP, 45, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_TO_FLAPS, 2, &e));
    run(&f, &in, 0.25);
    CLOSE(a380x_fms_outputs(&f)->to_flex_temp_c, 45, 0);
    CHECK(a380x_fms_outputs(&f)->to_flaps == 2);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_TO_POWER, A380X_FMS_TO_TOGA, &e));
    run(&f, &in, 0.25);
    CLOSE(a380x_fms_outputs(&f)->to_flex_temp_c, 0, 0);
}

static void test_approach(void)
{
    A380XFms f;
    MfdFmsError e;
    make_fms(&f, 1);
    A380XFmsInputs in = base_inputs();
    in.fqms_gw_valid = 1;
    in.fqms_gw_kg = 380000;
    in.fqms_cg_valid = 1;
    in.fqms_cg_pct = 35;
    run(&f, &in, 0.3);
    city_pair(&f, 0, "EGLL", "ZERO"); /* destination elevation 0 */
    run(&f, &in, 0.25);
    /* §12.1 B: LW = GW (no prediction), CONF FULL, no wind */
    CLOSE(getv(&f, 0, A380X_FMS_F_APPR_VLS), 135, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_VAPP), 140, 0);
    CHECK(a380x_fms_get(&f, 0, A380X_FMS_F_VAPP).entered == 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_APPR_VREF), 135, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_APPR_F), 167, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_APPR_S), 181, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_APPR_GD), 199, 0);
    CLOSE(a380x_fms_outputs(&f)->approach_vapp_knots, 140, 0);
    /* §12.1 C: CONF 3, tower headwind 30 kt */
    g_fp.dest_rwy[0] = rwy("22L", 0, 3000, 220);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_LDG_CONF, A380X_FMS_LDG_CONF3, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_APPR_WIND_DIR, 220, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_APPR_WIND_SPD, 30, &e));
    run(&f, &in, 0.25);
    CLOSE(getv(&f, 0, A380X_FMS_F_APPR_VLS), 139, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_VAPP), 149, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_APPR_HEADWIND), 30, 1e-9);
    CLOSE(getv(&f, 0, A380X_FMS_F_APPR_CROSSWIND), 0, 1e-9);
    const A380XFmsOutputs *o = a380x_fms_outputs(&f);
    CHECK(o->landing_conf3 && o->approach_headwind_valid);
    CLOSE(o->approach_headwind_knots, 30, 1e-9);
    /* §12.5 wind 270/20 on QFU 240 */
    g_fp.dest_rwy[0] = rwy("24", 0, 3000, 240);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_APPR_WIND_DIR, 270, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_APPR_WIND_SPD, 20, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_APPR_HEADWIND), 17.32, 0.005);
    CLOSE(getv(&f, 0, A380X_FMS_F_APPR_CROSSWIND), 10.0, 1e-9);
    /* pilot VAPP overrides; CLR reverts */
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_VAPP, 150, &e));
    CHECK(a380x_fms_get(&f, 0, A380X_FMS_F_VAPP).entered == 1);
    CHECK(a380x_fms_clear(&f, 0, A380X_FMS_F_VAPP, &e));
    CHECK(a380x_fms_get(&f, 0, A380X_FMS_F_VAPP).entered == 0);
    /* QNH CLR ignored; OAT/QNH mandatory within 180 NM of the destination */
    CHECK(!(a380x_fms_field_flags(&f, 0, A380X_FMS_F_APPR_QNH, A380X_FMS_PAGE_PERF_APPR) & A380X_FMS_FF_MANDATORY));
    in.lat_deg = in.lon_deg = 0.5;
    run(&f, &in, 0.25);
    CLOSE(getv(&f, 0, A380X_FMS_F_DEST_DIST), 42.4, 0.1);
    CHECK(a380x_fms_field_flags(&f, 0, A380X_FMS_F_APPR_QNH, A380X_FMS_PAGE_PERF_APPR) & A380X_FMS_FF_MANDATORY);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_APPR_QNH, 29.92, &e));
    CHECK(!a380x_fms_clear(&f, 0, A380X_FMS_F_APPR_QNH, &e));
    run(&f, &in, 0.25);
    CLOSE(a380x_fms_outputs(&f)->dest_qnh_hpa, 1013.2, 0.05);
    /* RADIO minimum only for ILS/GLS; minimums gated by "in range" */
    CHECK(a380x_fms_field_flags(&f, 0, A380X_FMS_F_APPR_RADIO, A380X_FMS_PAGE_PERF_APPR) & A380X_FMS_FF_HIDDEN);
    g_fp.approach_type = A380X_FMS_APPR_ILS;
    CHECK(!(a380x_fms_field_flags(&f, 0, A380X_FMS_F_APPR_RADIO, A380X_FMS_PAGE_PERF_APPR) & A380X_FMS_FF_HIDDEN));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_APPR_RADIO, 0, &e)); /* NO DH */
    run(&f, &in, 0.25);
    o = a380x_fms_outputs(&f);
    CLOSE(o->dh_ft, 0, 0);
    CHECK(!o->minimums_in_range && !o->no_dh);
    char buf[16];
    a380x_fms_get_text(&f, 0, A380X_FMS_T_APPROACH, buf, sizeof buf);
    CHECK(strcmp(buf, "ILS22L") == 0);
    /* SEC plans show dashes for approach speeds */
    CHECK(a380x_fms_sec_copy_from_active(&f, 1));
    CHECK(!a380x_fms_get(&f, A380X_FMS_PLAN_SEC1, A380X_FMS_F_APPR_VLS).has);
    CHECK(a380x_fms_get(&f, A380X_FMS_PLAN_SEC1, A380X_FMS_F_APPR_WIND_DIR).colour == A380X_FMS_COLOUR_WHITE);
}

/* ---------------------------------------------------------------------------------------- */
/* phase manager                                                                              */
/* ---------------------------------------------------------------------------------------- */

static void test_phases(void)
{
    A380XFms f;
    MfdFmsError e;
    make_fms(&f, 1);
    A380XFmsInputs in = base_inputs();
    in.gps_primary = 1;
    run(&f, &in, 0.3);
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_NAV_PRIMARY));
    CHECK(a380x_fms_outputs(&f)->nd_fm_message_flags[0] == (1u << 6));
    city_pair(&f, 0, "EGLL", "KJFK");
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_ZFW, 300000, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_ZFWCG, 30, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_BLOCK, 100000, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_CI, 50, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_CRZ_FL, 350, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_V1, 140, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_VR, 150, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_V2, 160, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_PRESEL_CLB, 260, &e));
    for (int i = 0; i < 4; i++) in.n2_pct[i] = 60;
    in.fqms_gw_valid = in.fqms_fob_valid = 1;
    in.fqms_gw_kg = 400000;
    in.fqms_fob_kg = 100000;
    run(&f, &in, 0.5);
    /* managed target in PREFLIGHT = V2 */
    const A380XFmsOutputs *o = a380x_fms_outputs(&f);
    CHECK(o->managed_speed_valid);
    CLOSE(o->managed_speed_knots, 160, 0);
    CHECK(o->thr_red_alt_valid && o->acc_alt_valid);
    CLOSE(o->acc_alt_ft, 1580, 0);
    CHECK(o->trans_alt_valid);
    CLOSE(o->trans_alt_ft, 6000, 0);

    /* PREFLIGHT -> TAKEOFF: SRS + N1 >= 70 on engines 1-2, confirmed 0.2 s */
    in.vertical_mode = A380X_FMS_VM_SRS;
    in.n1_pct[0] = in.n1_pct[1] = 85;
    run(&f, &in, 0.1);
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_PREFLIGHT);
    run(&f, &in, 0.2);
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_TAKEOFF);
    run(&f, &in, 0.25);
    o = a380x_fms_outputs(&f);
    CLOSE(o->presel_speed_knots, 260, 0); /* climb presel armed */
    CLOSE(getv(&f, 0, A380X_FMS_F_TOW), 400000, 0); /* TOW = GW, frozen */
    CHECK(a380x_fms_field_flags(&f, 0, A380X_FMS_F_TAXI, A380X_FMS_PAGE_FUEL_LOAD) & A380X_FMS_FF_DISABLED);
    CHECK(!a380x_fms_get(&f, 0, A380X_FMS_F_TAXI).has);
    CLOSE(getv(&f, 0, A380X_FMS_F_RTE_RSV_PCT), 0, 0);
    CHECK(a380x_fms_field_flags(&f, 0, A380X_FMS_F_V1, A380X_FMS_PAGE_PERF_TO) & A380X_FMS_FF_INACTIVE);
    CHECK(a380x_fms_text_flags(&f, 0, A380X_FMS_T_FROM, A380X_FMS_PAGE_INIT) & A380X_FMS_FF_DISABLED);
    in.fqms_gw_kg = 399000;
    run(&f, &in, 0.25);
    CLOSE(getv(&f, 0, A380X_FMS_F_TOW), 400000, 0);

    /* TAKEOFF -> CLIMB above ACC (1580 ft) */
    in.on_ground = 0;
    in.vertical_mode = A380X_FMS_VM_SRS;
    in.baro_alt_ft = in.pressure_alt_ft = 1500;
    in.cas_kt = 170;
    in.ground_speed_kt = 170;
    run(&f, &in, 0.2);
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_TAKEOFF);
    unsigned seq = a380x_fms_outputs(&f)->presel_activate_seq;
    in.baro_alt_ft = in.pressure_alt_ft = 1700;
    run(&f, &in, 0.3);
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_CLIMB);
    CHECK(a380x_fms_outputs(&f)->presel_activate_seq == seq + 1);
    o = a380x_fms_outputs(&f);
    CHECK(!o->thr_red_alt_valid); /* NCD after TAKEOFF */
    CHECK(o->to_acc_alt_valid);
    /* CLIMB managed speed: speed limit 250 below 10000 ft */
    in.vertical_mode = A380X_FMS_VM_CLB;
    run(&f, &in, 0.3);
    CLOSE(a380x_fms_outputs(&f)->managed_speed_knots, 250, 0);
    /* above FL100: ECON climb; Mach target at FL350 pressure */
    in.baro_alt_ft = in.pressure_alt_ft = 30000;
    in.static_pressure_hpa = 238.42;
    run(&f, &in, 0.3);
    o = a380x_fms_outputs(&f);
    CHECK(o->managed_speed_is_mach);
    CLOSE(o->managed_mach, 0.84, 0);
    CLOSE(o->managed_speed_knots, 287.10, 0.01);
    /* SPD LIMIT EXCEEDED below 10000 ft */
    in.baro_alt_ft = in.pressure_alt_ft = 8000;
    in.static_pressure_hpa = 1013.25;
    in.cas_kt = 275;
    run(&f, &in, 0.3);
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_SPD_LIM_EXCEEDED));
    in.cas_kt = 250;
    run(&f, &in, 0.3);
    CHECK(!mfd_msg_has_id(&f.msgs, MFD_MSG_SPD_LIM_EXCEEDED));
    /* CLIMB -> CRUISE at CRZ FL */
    in.fcu_selected_alt_ft = 35000;
    in.baro_alt_ft = in.pressure_alt_ft = 34990;
    in.vertical_mode = A380X_FMS_VM_ALT;
    run(&f, &in, 0.3);
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_CRUISE);
    /* trySetCruiseFl in CRUISE below the FCU level: FCU FL inserted + NEW CRZ ALT */
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_CRZ_FL, 330, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_CRZ_FL), 350, 0);
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_NEW_CRZ_ALT));
    /* step climb by FCU: OP CLB activation with FCU above CRZ FL */
    in.fcu_selected_alt_ft = 37000;
    in.vertical_mode = A380X_FMS_VM_OP_CLB;
    run(&f, &in, 0.3);
    CLOSE(getv(&f, 0, A380X_FMS_F_CRZ_FL), 370, 0);
    CLOSE(a380x_fms_outputs(&f)->cruise_altitude_ft, 37000, 0);
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_CRUISE);
    /* CRUISE -> DESCENT: FCU below, OP DES (open mode activation = push/pull) */
    in.vertical_mode = A380X_FMS_VM_ALT_CRZ;
    in.baro_alt_ft = in.pressure_alt_ft = 37000;
    run(&f, &in, 0.3);
    in.fcu_selected_alt_ft = 10000;
    in.speed_managed = 0;
    run(&f, &in, 0.3);
    in.vertical_mode = A380X_FMS_VM_OP_DES;
    run(&f, &in, 0.3);
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_DESCENT);
    CHECK(!a380x_fms_get(&f, 0, A380X_FMS_F_CRZ_FL).has);
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_ENTER_DEST_DATA));
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_CHECK_SPD_MODE));
    CHECK(!mfd_msg_has_id(&f.msgs, MFD_MSG_NEW_CRZ_ALT)); /* quirk 3 fixed */
    CHECK(a380x_fms_outputs(&f)->msg_check_speed_mode);
    in.speed_managed = 1;
    run(&f, &in, 0.5);
    CHECK(!mfd_msg_has_id(&f.msgs, MFD_MSG_CHECK_SPD_MODE));
    CHECK(!a380x_fms_outputs(&f)->msg_check_speed_mode);
    /* [PORT] managed descent target = ECON DES */
    o = a380x_fms_outputs(&f);
    CHECK(o->managed_speed_valid);
    /* ENTER DEST DATA resolution */
    g_fp.dest_rwy[0] = rwy("22L", 13, 3460, 220);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_APPR_QNH, 1013, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_APPR_TEMP, 15, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_APPR_WIND_DIR, 220, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_APPR_WIND_SPD, 10, &e));
    run(&f, &in, 0.5);
    CHECK(!mfd_msg_has_id(&f.msgs, MFD_MSG_ENTER_DEST_DATA));
    /* ACTIVATE APPR */
    CHECK(a380x_fms_can_activate_approach(&f, 0));
    CHECK(a380x_fms_activate_approach(&f));
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_APPROACH);
    run(&f, &in, 0.3);
    o = a380x_fms_outputs(&f);
    CLOSE(o->managed_speed_knots, o->approach_vapp_knots, 0);
    CLOSE(o->to_flex_temp_c, 0, 0);
    CHECK(o->minimums_in_range);
    /* APPROACH -> GOAROUND via SRS GA, missed ACC fallback = alt + 1500 if unset */
    in.vertical_mode = A380X_FMS_VM_SRS_GA;
    in.baro_alt_ft = 1000;
    run(&f, &in, 0.3);
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_GOAROUND);
    o = a380x_fms_outputs(&f);
    CHECK(o->acc_alt_valid);
    CLOSE(o->acc_alt_ft, o->missed_acc_alt_ft, 0);
    CLOSE(o->goaround_init_speed_kt, in.cas_kt, 0);
    /* back to approach, land, 30 s on ground with engines 2/3 off -> DONE */
    CHECK(a380x_fms_activate_approach(&f));
    in.vertical_mode = A380X_FMS_VM_FLARE;
    in.on_ground = 1;
    in.baro_alt_ft = in.pressure_alt_ft = 13;
    in.ground_speed_kt = in.cas_kt = 0;
    run(&f, &in, 10);
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_APPROACH);
    run(&f, &in, 21);
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_DONE);
    /* DONE: everything reset, navigate both MFDs to DATA/STATUS */
    CHECK(!a380x_fms_has_active_flight_plan(&f));
    CHECK(a380x_fms_outputs(&f)->nav_request == A380X_FMS_NAV_DATA_STATUS);
    CLOSE(a380x_fms_outputs(&f)->cruise_altitude_ft, -1, 0);
    /* DONE -> PREFLIGHT when an MFD shows ACTIVE INIT/FUEL/PERF */
    in.mfd_on_active_init_fuel_perf = 1;
    run(&f, &in, 0.2);
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_PREFLIGHT);

    /* NAV PRIMARY LOST */
    in.gps_primary = 0;
    run(&f, &in, 0.3);
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_NAV_PRIMARY_LOST));
    CHECK(!mfd_msg_has_id(&f.msgs, MFD_MSG_NAV_PRIMARY));
    CHECK(a380x_fms_outputs(&f)->nd_fm_message_flags[1] == (1u << 9));
}

static void test_spawn_and_engine_out(void)
{
    A380XFms f;
    make_fms(&f, 1);
    A380XFmsInputs in = base_inputs();
    in.on_ground = 0;
    in.baro_alt_ft = in.pressure_alt_ft = 25000;
    for (int i = 0; i < 4; i++) in.n2_pct[i] = 90;
    run(&f, &in, 0.3);
    CHECK(a380x_fms_phase(&f) == A380X_FMS_PHASE_CRUISE);
    in.n2_pct[3] = 0;
    run(&f, &in, 0.3);
    CHECK(a380x_fms_engine_out(&f));
    CHECK(a380x_fms_outputs(&f)->nav_request == A380X_FMS_NAV_PERF);
    CHECK(!a380x_fms_get(&f, 0, A380X_FMS_F_OPT_FL).has); /* OPT FL null when EO */
    a380x_fms_clear_engine_out(&f);
    CHECK(!a380x_fms_engine_out(&f));
    run(&f, &in, 0.3);
    CHECK(!a380x_fms_engine_out(&f)); /* pulse only on the rising edge */
    in.n2_pct[3] = 90;
    run(&f, &in, 0.3);
    in.n2_pct[2] = 0;
    run(&f, &in, 0.3);
    CHECK(a380x_fms_engine_out(&f));
    in.n2_pct[2] = 90;
    run(&f, &in, 0.3);
    CHECK(!a380x_fms_engine_out(&f));

    A380XFms g;
    make_fms(&g, 1);
    in = base_inputs();
    in.on_ground = 0;
    in.baro_alt_ft = 3000;
    in.flap_lever = 3;
    for (int i = 0; i < 4; i++) in.n2_pct[i] = 70;
    run(&g, &in, 0.3);
    CHECK(a380x_fms_phase(&g) == A380X_FMS_PHASE_APPROACH);
    /* non cold & dark spawn: ZFW auto-init from FQMS */
    A380XFms h;
    make_fms(&h, 1);
    in = base_inputs();
    in.cold_and_dark_spawn = 0;
    in.fqms_gw_valid = in.fqms_fob_valid = 1;
    in.fqms_gw_kg = 420000;
    in.fqms_fob_kg = 110000;
    run(&h, &in, 0.3);
    CLOSE(getv(&h, 0, A380X_FMS_F_ZFW), 310000, 0);
    CHECK(!a380x_fms_get(&h, 0, A380X_FMS_F_ZFWCG).has);
}

/* ---------------------------------------------------------------------------------------- */
/* SEC / TMPY                                                                                 */
/* ---------------------------------------------------------------------------------------- */

static void test_sec_tmpy(void)
{
    A380XFms f;
    MfdFmsError e;
    make_fms(&f, 1);
    A380XFmsInputs in = base_inputs();
    run(&f, &in, 0.3);
    city_pair(&f, 0, "EGLL", "KJFK");
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_ZFW, 300000, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_ZFWCG, 30, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_CI, 80, &e));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_V2, 160, &e));

    /* IMPORT ACTIVE: copy + piped keys */
    CHECK(a380x_fms_sec_copy_from_active(&f, 2));
    int s2 = A380X_FMS_SEC_PLAN(2);
    CHECK(a380x_fms_loaded_plan(&f, s2) == s2);
    CHECK(a380x_fms_is_active_or_copy(&f, s2));
    CLOSE(getv(&f, s2, A380X_FMS_F_V2), 160, 0);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_CI, 120, &e));
    run(&f, &in, 0.25);
    CLOSE(getv(&f, s2, A380X_FMS_F_CI), 120, 0); /* piped */
    CHECK(a380x_fms_enter(&f, s2, A380X_FMS_F_CI, 10, &e)); /* SEC edit breaks the link */
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_CI, 130, &e));
    run(&f, &in, 0.25);
    CLOSE(getv(&f, s2, A380X_FMS_F_CI), 10, 0);
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_ZFW, 305000, &e));
    run(&f, &in, 0.25);
    CLOSE(getv(&f, s2, A380X_FMS_F_ZFW), 305000, 0); /* P* piped before engine start */
    CHECK(a380x_fms_get(&f, s2, A380X_FMS_F_ZFW).colour == A380X_FMS_COLOUR_WHITE);
    CHECK(a380x_fms_field_flags(&f, s2, A380X_FMS_F_ZFW, A380X_FMS_PAGE_FUEL_LOAD) & A380X_FMS_FF_SEC);
    CHECK(!(a380x_fms_field_flags(&f, s2, A380X_FMS_F_ZFW, A380X_FMS_PAGE_FUEL_LOAD) & A380X_FMS_FF_MANDATORY));
    A380XFmsSecInfo si;
    a380x_fms_sec_info(&f, 2, &si);
    CHECK(si.exists && si.has_city_pair && strcmp(si.from, "EGLL") == 0 && strcmp(si.to, "KJFK") == 0);
    CHECK(si.flags & A380X_FMS_PLANF_COPIED_FROM_ACTIVE);
    CHECK(si.can_swap); /* PREFLIGHT */
    char buf[64];
    CHECK(a380x_fms_get_text(&f, s2, A380X_FMS_T_SEC_CREATED, buf, sizeof buf));
    CHECK(strcmp(buf, "CREATED\xc2\xa0\xc2\xa0" "1234 (IMPORT ACTIVE)") == 0);

    /* SEC 1 manual with a different city pair and ZFW -> swap */
    CHECK(a380x_fms_sec_init(&f, 1));
    int s1 = A380X_FMS_SEC_PLAN(1);
    CHECK(!a380x_fms_is_active_or_copy(&f, s1));
    CHECK(!a380x_fms_sec_can_swap(&f, 1));
    city_pair(&f, s1, "EGKK", "KBOS");
    CHECK(a380x_fms_enter(&f, s1, A380X_FMS_F_ZFW, 290000, &e));
    CHECK(a380x_fms_enter(&f, s1, A380X_FMS_F_ZFWCG, 36, &e));
    CHECK(a380x_fms_get_text(&f, s1, A380X_FMS_T_SEC_CREATED, buf, sizeof buf));
    CHECK(strstr(buf, "(MANUAL)") != NULL);
    CHECK(a380x_fms_sec_swap(&f, 1));
    CHECK(g_fp.calls_swap == 1);
    char from[8];
    a380x_fms_get_text(&f, 0, A380X_FMS_T_FROM, from, sizeof from);
    CHECK(strcmp(from, "EGKK") == 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_ZFW), 290000, 0); /* before engine start: new value kept */
    CLOSE(getv(&f, 0, A380X_FMS_F_CI), 130, 0);     /* CI null in new active -> old kept */
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_CHECK_TO_DATA));
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_CHECK_ZFW)); /* 15 t and 6 % */
    a380x_fms_get_text(&f, s1, A380X_FMS_T_FROM, from, sizeof from);
    CHECK(strcmp(from, "EGLL") == 0);
    CHECK(a380x_fms_get_text(&f, s1, A380X_FMS_T_SEC_CREATED, buf, sizeof buf) && strstr(buf, "SWAP ACTIVE"));
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_ZFW, 291000, &e));
    CHECK(!mfd_msg_has_id(&f.msgs, MFD_MSG_CHECK_ZFW));
    CHECK(a380x_fms_sec_delete(&f, 1));
    CHECK(!a380x_fms_sec_exists(&f, 1));
    CHECK(a380x_fms_sec_copy_from_sec(&f, 2, 3));
    CHECK(a380x_fms_sec_exists(&f, 3));

    /* TMPY lifecycle: lateral TMPY created by the plan module -> data store copy */
    g_fp.lateral_tmpy = 1;
    CHECK(a380x_fms_loaded_plan(&f, A380X_FMS_PLAN_ACTIVE) == A380X_FMS_PLAN_TMPY);
    CHECK(a380x_fms_tmpy_exists(&f));
    CHECK(a380x_fms_field_flags(&f, 1, A380X_FMS_F_ZFW, A380X_FMS_PAGE_FUEL_LOAD) & A380X_FMS_FF_TMPY);
    CHECK(a380x_fms_text_flags(&f, 1, A380X_FMS_T_FROM, A380X_FMS_PAGE_INIT) & A380X_FMS_FF_DISABLED);
    CHECK(!a380x_fms_sec_can_swap(&f, 2)); /* no swap while TMPY */
    CHECK(!a380x_fms_enter(&f, 1, A380X_FMS_F_CRZ_FL, 390, &e) && e.type == MFD_ERR_NOT_ALLOWED);
    CHECK(a380x_fms_enter(&f, 1, A380X_FMS_F_CI, 77, &e));
    CLOSE(getv(&f, 0, A380X_FMS_F_CI), 130, 0);
    CHECK(a380x_fms_tmpy_insert(&f));
    CHECK(g_fp.calls_insert == 1 && !a380x_fms_tmpy_exists(&f));
    CLOSE(getv(&f, 0, A380X_FMS_F_CI), 77, 0);
    /* erase path */
    g_fp.lateral_tmpy = 1;
    run(&f, &in, 0.25); /* safety-net detection */
    CHECK(a380x_fms_tmpy_exists(&f));
    CHECK(a380x_fms_enter(&f, 1, A380X_FMS_F_CI, 99, &e));
    CHECK(a380x_fms_tmpy_erase(&f));
    CLOSE(getv(&f, 0, A380X_FMS_F_CI), 77, 0);
}

/* ---------------------------------------------------------------------------------------- */
/* health, reset, constraints                                                                 */
/* ---------------------------------------------------------------------------------------- */

static void test_health_reset(void)
{
    A380XFms f;
    MfdFmsError e;
    make_fms(&f, 1);
    A380XFmsInputs in = base_inputs();
    run(&f, &in, 0.3);
    CHECK(strcmp(a380x_fms_header_source(&f, 0), "FMS 1") == 0);
    CHECK(strcmp(a380x_fms_header_source(&f, 1), "FMS 2") == 0);
    city_pair(&f, 0, "EGLL", "KJFK");
    /* reconciliation of THR RED / ACC with a climb constraint */
    g_fp.lowest_clb_cstr = 1204;
    run(&f, &in, 0.25);
    CLOSE(getv(&f, 0, A380X_FMS_F_THR_RED), 1200, 0);
    CLOSE(getv(&f, 0, A380X_FMS_F_ACC), 1200, 0);
    CHECK(mfd_msg_has_id(&f.msgs, MFD_MSG_NEW_THR_RED_ALT) && mfd_msg_has_id(&f.msgs, MFD_MSG_NEW_ACC_ALT));
    g_fp.lowest_clb_cstr = NAN;
    /* FMC A + C failed, knob NORM: captain side failed */
    in.fmc_failed[0] = in.fmc_failed[2] = 1;
    run(&f, &in, 0.25);
    CHECK(a380x_fms_side_failed(&f, 0) && !a380x_fms_side_failed(&f, 1));
    CHECK(strcmp(a380x_fms_header_source(&f, 0), "FMS 1-C") == 0);
    CHECK(a380x_fms_outputs(&f)->valid);
    in.switching_knob = 0;
    run(&f, &in, 0.25);
    CHECK(!a380x_fms_side_failed(&f, 0));
    CHECK(strcmp(a380x_fms_header_source(&f, 0), "FMS 2") == 0);
    /* all inop -> reset once, outputs invalid */
    in.fmc_failed[1] = 1;
    unsigned seq = a380x_fms_outputs(&f)->nav_request_seq;
    run(&f, &in, 0.5);
    CHECK(!a380x_fms_outputs(&f)->valid);
    CHECK(!a380x_fms_has_active_flight_plan(&f));
    CHECK(a380x_fms_outputs(&f)->nav_request_seq == seq + 1);
    in.fmc_failed[0] = in.fmc_failed[1] = in.fmc_failed[2] = 0;
    in.switching_knob = 1;
    run(&f, &in, 0.3);
    CHECK(a380x_fms_outputs(&f)->valid);
    /* quirk 1: all three reset buttons pulled */
    in.fmc_reset[0] = in.fmc_reset[1] = in.fmc_reset[2] = 1;
    run(&f, &in, 0.3);
    CHECK(!a380x_fms_outputs(&f)->valid);
    in.fmc_reset[0] = in.fmc_reset[1] = in.fmc_reset[2] = 0;
    run(&f, &in, 0.3);
    /* explicit reset (DATA/STATUS SWAP) empties the queue and the fuel penalty */
    CHECK(a380x_fms_enter(&f, 0, A380X_FMS_F_FUEL_PENALTY, 3, &e));
    a380x_fms_reset(&f);
    CHECK(f.msgs.count == 0);
    CHECK(!a380x_fms_get(&f, 0, A380X_FMS_F_FUEL_PENALTY).has);
    /* waypoint predictions not available */
    A380XFmsWaypointPrediction wp;
    CHECK(!a380x_fms_waypoint_prediction(&f, 0, 3, &wp) && !wp.valid);
    /* position monitor fix */
    a380x_fms_set_position_monitor_fix(&f, "BIG", 51.33, 0.03);
    CHECK(a380x_fms_position_monitor_fix(&f)->valid);
    a380x_fms_clear_position_monitor_fix(&f);
    CHECK(!a380x_fms_position_monitor_fix(&f)->valid);
    CLOSE(a380x_fms_great_circle_nm(51.4775, -0.4614, 40.6398, -73.7789), 2991, 3);
}

int main(void)
{
    test_interpolation();
    test_speed_vectors();
    test_appendix_b();
    test_altitudes();
    test_managed_laws();
    test_wind();
    test_fuel();
    test_init_entries();
    test_engine_start_messages();
    test_vspeeds_and_runway_change();
    test_approach();
    test_phases();
    test_spawn_and_engine_out();
    test_sec_tmpy();
    test_health_reset();
    printf("a380x FMS core tests passed (%d checks)\n", checks);
    return 0;
}
