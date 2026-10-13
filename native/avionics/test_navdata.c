/* SPDX-License-Identifier: GPL-3.0-or-later
 * Navigation database + flight-plan tests (WP-D).
 *
 *   test_navdata [fixture_dir]            unit tests (default fixture dir: test_data/navdata next to
 *                                         the working directory)
 *   test_navdata --xplane <X-Plane root>  timing / memory check against the installed X-Plane data
 *
 * Fixtures (test_data/navdata): `custom/` is a CRLF excerpt of a Custom Data cycle 2609 (EGLL CIFP,
 * London navaids/fixes, airways L9/L10/L607, holds, airport metadata); `default/` is a tiny
 * hand-written default layer (cycle 2406) with the synthetic airport XXTS covering RF/AF/HM/PI/CD/
 * CR/FD/FC/VI/VM legs, an unresolved fix and malformed lines.
 */
#include "a380x_navdata.h"
#include "a380x_fms_fpln.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, tol) (fabs((double)(a) - (double)(b)) <= (tol))

static char g_dir[512] = "test_data/navdata";

/* ---------------- fake host ---------------- */
static int host_calls;
static int fake_airport(void *ctx, const char *icao, A380XNavAirportInfo *out)
{
    (void)ctx;
    host_calls++;
    if (!strcmp(icao, "XXTS")) {
        out->lat = 51.2; out->lon = -0.9; out->elev_ft = 300;
        snprintf(out->name, sizeof out->name, "TEST FIELD");
        return 1;
    }
    if (!strcmp(icao, "EGLL")) { out->lat = 0; out->lon = 0; snprintf(out->name, sizeof out->name, "HEATHROW"); return 1; }
    return 0;
}
static int fake_var_value_set;
static double fake_var_value;
static int fake_magvar(void *ctx, double lat, double lon, double *out)
{
    (void)ctx; (void)lat; (void)lon;
    if (!fake_var_value_set) return 0;
    *out = fake_var_value;
    return 1;
}

static A380XNavdata *open_fixture(const char *custom, const char *deflt, int cache)
{
    char c[600], d[600];
    snprintf(c, sizeof c, "%s/%s", g_dir, custom);
    snprintf(d, sizeof d, "%s/%s", g_dir, deflt);
    A380XNavdataConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.custom_data_dir = custom[0] ? c : "";
    cfg.default_data_dir = d;
    cfg.cifp_cache_size = cache;
    A380XNavdataHost host;
    memset(&host, 0, sizeof host);
    host.airport_info = fake_airport;
    host.magvar = fake_magvar;
    return a380x_nav_create(&cfg, &host);
}

/* ============================================================================================ */
/* Navdata                                                                                      */
/* ============================================================================================ */

static void test_airac(void)
{
    int y, m, d, xy, xm, xd;
    CHECK(a380x_nav_airac_dates("2609", &y, &m, &d, &xy, &xm, &xd) == 0);
    CHECK(y == 2026 && m == 9 && d == 3 && xy == 2026 && xm == 10 && xd == 1);
    CHECK(a380x_nav_airac_dates("2410", &y, &m, &d, &xy, &xm, &xd) == 0 && y == 2024 && m == 10 && d == 3);
    CHECK(a380x_nav_airac_dates("2001", &y, &m, &d, &xy, &xm, &xd) == 0 && y == 2020 && m == 1 && d == 2);
    CHECK(a380x_nav_airac_dates("2014", &y, &m, &d, &xy, &xm, &xd) == 0 && y == 2020 && m == 12 && d == 31);
    CHECK(a380x_nav_airac_dates("2406", &y, &m, &d, &xy, &xm, &xd) == 0 && y == 2024 && m == 6 && d == 13);
    CHECK(a380x_nav_airac_dates("2614", &y, &m, &d, &xy, &xm, &xd) != 0);   /* 2026 has 13 cycles */
    CHECK(a380x_nav_airac_dates("26x1", &y, &m, &d, &xy, &xm, &xd) != 0);
}

static void test_global(void)
{
    A380XNavdata *nav = open_fixture("custom", "default", 0);
    CHECK(nav != NULL);
    CHECK(a380x_nav_load_global(nav) == 0);
    A380XNavStats st;
    a380x_nav_stats(nav, &st);
    CHECK(st.custom_fix && st.custom_nav && st.custom_awy && st.custom_meta);
    CHECK(st.waypoints == 32);           /* excerpt incl. BADLN (bad type tolerated) and both LOGANs; BADLT rejected */
    CHECK(st.malformed_lines >= 4);      /* garbage, lat 95, broken VOR, short airway */
    CHECK(st.airports_meta == 3);
    CHECK(st.bytes > 0);
    const A380XNavCycle *cy = a380x_nav_cycle(nav);
    CHECK(cy->valid && !strcmp(cy->airac, "2609") && !strcmp(cy->revision, "1") && cy->from_custom_data);
    CHECK(cy->eff_day == 3 && cy->eff_month == 9 && cy->exp_month == 10 && cy->exp_day == 1);
    CHECK(!strcmp(cy->source_file, "cycle.json"));

    A380XNavFix f[8];
    CHECK(a380x_nav_find(nav, "OLDFX", A380X_NAVMASK_ALL, f, 8) == 0);   /* default layer ignored */
    int n = a380x_nav_find_near(nav, "BIG", A380X_NAVMASK_ALL, 51.47, -0.46, f, 8);
    CHECK(n == 3);
    CHECK(!strcmp(f[0].region, "EG") && f[0].kind == A380X_NAV_VOR && (f[0].flags & A380X_NAVF_DME));
    CHECK(NEAR(f[0].freq, 115.10, 0.001) && !strcmp(f[0].name, "BIGGIN VOR/DME"));
    CHECK(f[0].dist_nm < f[1].dist_nm && f[1].dist_nm < f[2].dist_nm);
    CHECK(!strcmp(f[2].region, "PA") && (f[2].flags & A380X_NAVF_TACAN));
    CHECK(NEAR(f[0].station_var, 1.0, 1e-6));
    CHECK(strchr(f[0].ident, '\r') == NULL);
    n = a380x_nav_find_near(nav, "LOGAN", A380X_NAVMASK_ALL, 51.47, -0.46, f, 1);
    CHECK(n == 2 && !strcmp(f[0].region, "EG"));   /* only nearest written, total reported */
    n = a380x_nav_find(nav, "D125O", A380X_NAVMASK_ALL, f, 8);
    CHECK(n == 1 && !strcmp(f[0].airport, "EGLL") && (f[0].flags & A380X_NAVF_TERMINAL));
    CHECK(f[0].wpt_type[0] != 0);
    /* ILS */
    CHECK(a380x_nav_find(nav, "IRR", A380X_NAVMASK(A380X_NAV_LOC), f, 8) == 1);
    CHECK(f[0].kind == A380X_NAV_LOC && (f[0].flags & A380X_NAVF_GS) && (f[0].flags & A380X_NAVF_DME));
    CHECK(NEAR(f[0].loc_true_brg, 269.709, 0.01) && NEAR(f[0].loc_mag_brg, 268, 1e-6) && NEAR(f[0].gs_angle_deg, 3.0, 1e-6));
    CHECK(NEAR(f[0].freq, 110.30, 0.001));
    /* NDB */
    CHECK(a380x_nav_find(nav, "WOD", A380X_NAVMASK(A380X_NAV_NDB), f, 8) == 1 && f[0].freq > 300 && f[0].freq < 400);
    /* airports */
    A380XNavAirportInfo ai;
    CHECK(a380x_nav_airport_info(nav, "EGLL", &ai) && ai.from_aptmeta && NEAR(ai.lat, 51.4775, 1e-6));
    CHECK(NEAR(ai.trans_alt_ft, 6000, 0) && isnan(ai.trans_level_ft) && !strcmp(ai.name, "HEATHROW"));
    CHECK(NEAR(ai.magvar_deg, 1.71, 0.05));        /* from the four LOC mag/true courses */
    CHECK(a380x_nav_airport_info(nav, "XXTS", &ai) && ai.from_host && NEAR(ai.lat, 51.2, 1e-9) && !strcmp(ai.name, "TEST FIELD"));
    CHECK(!a380x_nav_airport_info(nav, "ZZZZ", &ai));
    CHECK(a380x_nav_find(nav, "LFPG", A380X_NAVMASK(A380X_NAV_AIRPORT), f, 8) == 1 && NEAR(f[0].elev_ft, 392, 0));
    CHECK(a380x_nav_find(nav, "XXTS", A380X_NAVMASK(A380X_NAV_AIRPORT), f, 8) == 1 && f[0].db_id == -1);
    A380XNavFix r;
    CHECK(a380x_nav_resolve(nav, "BIG", "EG", A380X_NAV_VOR, "", NAN, NAN, &r) && NEAR(r.lat, 51.33, 0.01));
    CHECK(a380x_nav_resolve(nav, "BIG", "", A380X_NAV_VOR, "", 64.0, -145.0, &r) && !strcmp(r.region, "PA"));
    CHECK(!a380x_nav_resolve(nav, "NOPE", "", A380X_NAV_NONE, "", NAN, NAN, &r));
    CHECK(a380x_nav_nearby(nav, 51.47, -0.46, 30.0, A380X_NAVMASK(A380X_NAV_VOR), f, 8) >= 3 && f[0].dist_nm <= f[1].dist_nm);
    /* holds */
    A380XNavHold h[8];
    n = a380x_nav_find_holds(nav, "LAM", "EG", h, 8);
    CHECK(n >= 1 && h[0].turn == 'L' && NEAR(h[0].inbound_mag, 262, 0) && isfinite(h[0].fix.lat));
    /* pilot waypoints */
    CHECK(a380x_nav_make_latlon(51.5, -1.0, "LL01", &r) && r.kind == A380X_NAV_USER);
    A380XNavFix big, dvr, pbd, pbx;
    a380x_nav_resolve(nav, "BIG", "EG", A380X_NAV_VOR, "", NAN, NAN, &big);
    a380x_nav_resolve(nav, "DVR", "EG", A380X_NAV_VOR, "", NAN, NAN, &dvr);
    fake_var_value_set = 1; fake_var_value = 0.0;
    CHECK(a380x_nav_make_pbd(nav, &big, 90.0, 10.0, "PBD01", &pbd));
    CHECK(NEAR(a380x_geo_distance_nm(big.lat, big.lon, pbd.lat, pbd.lon), 10.0, 0.01));
    CHECK(a380x_nav_make_pbx(nav, &big, 90.0, &dvr, 0.0, "PBX01", &pbx));
    CHECK(NEAR(pbx.lon, dvr.lon, 0.05));
    fake_var_value_set = 0;
    a380x_nav_destroy(nav);
}

static void test_airways(void)
{
    A380XNavdata *nav = open_fixture("custom", "default", 0);
    A380XNavFix f;
    A380XNavRef big, konan, ref;
    a380x_nav_resolve(nav, "BIG", "EG", A380X_NAV_VOR, "", NAN, NAN, &f);
    a380x_nav_ref_from_fix(&big, &f);
    a380x_nav_resolve(nav, "KONAN", "EG", A380X_NAV_WAYPOINT, "", NAN, NAN, &f);
    a380x_nav_ref_from_fix(&konan, &f);
    A380XNavRef p[16];
    int n = a380x_nav_airway_path(nav, "L9", &big, "KONAN", p, 16);
    CHECK(n == 5);
    if (n == 5) {
        CHECK(!strcmp(p[0].ident, "DIKVU") && !strcmp(p[1].ident, "EMKAD") && !strcmp(p[2].ident, "UMTUM"));
        CHECK(!strcmp(p[3].ident, "DVR") && !strcmp(p[4].ident, "KONAN") && isfinite(p[4].lat));
    }
    n = a380x_nav_airway_path(nav, "L9", &konan, "GOXUL", p, 16);   /* reverse direction */
    CHECK(n == 6 && !strcmp(p[4].ident, "BIG") && !strcmp(p[5].ident, "GOXUL"));
    CHECK(a380x_nav_airway_path(nav, "Q999", &big, "KONAN", p, 16) == -1);
    CHECK(a380x_nav_airway_path(nav, "L10", &big, "ITVIP", p, 16) == -2);
    CHECK(a380x_nav_airway_path(nav, "L9", &big, "ITVIP", p, 16) == -3);
    CHECK(a380x_nav_airway_exists(nav, "L607") && !a380x_nav_airway_exists(nav, "Z99"));
    CHECK(a380x_nav_airway_contains(nav, "L607", &big) && !a380x_nav_airway_contains(nav, "L10", &big));
    CHECK(a380x_nav_airway_intersection(nav, "L9", &big, "L10", &ref) && !strcmp(ref.ident, "DVR"));
    CHECK(!a380x_nav_airway_intersection(nav, "L607", &konan, "L10", &ref) || !strcmp(ref.ident, "DVR"));
    char names[8][A380X_NAV_IDENT_LEN];
    n = a380x_nav_airways_at(nav, &big, names, 8);
    CHECK(n == 2 && !strcmp(names[0], "L607") && !strcmp(names[1], "L9"));
    a380x_nav_destroy(nav);
}

static const A380XNavProcLeg *trans_leg(const A380XNavAirport *a, const A380XNavProcTrans *t, int i)
{
    return &a->legs[t->first_leg + i];
}

static void test_cifp(void)
{
    A380XNavdata *nav = open_fixture("custom", "default", 2);
    const A380XNavAirport *a = a380x_nav_airport(nav, "EGLL");
    CHECK(a != NULL);
    if (!a) return;
    CHECK(a->info.has_cifp && a->runway_count == 4 && a->unresolved_fixes == 0 && a->malformed_lines >= 1);
    const A380XNavRunway *rw = a380x_nav_runway(a, "27R");
    CHECK(rw && rw == a380x_nav_runway(a, "RW27R"));
    if (rw) {
        CHECK(NEAR(rw->true_brg, 269.71, 0.05) && NEAR(rw->mag_brg, 268.0, 0.1));
        CHECK(rw->length_ft > 12500 && rw->length_ft < 12900);          /* 3902 m physical */
        CHECK(!strcmp(rw->ls_ident, "IRR") && NEAR(rw->ls_freq_mhz, 110.30, 0.001) && rw->ls_category == 3 && rw->ls_has_gs);
        CHECK(NEAR(rw->thr_elev_ft, 78, 0) && !strcmp(rw->designator, "27R") && rw->side == 'R');
    }
    rw = a380x_nav_runway(a, "09L");
    CHECK(rw && NEAR(rw->disp_thr_ft, 1014, 0) && a380x_geo_distance_nm(rw->start_lat, rw->start_lon, rw->thr_lat, rw->thr_lon) > 0.15);
    CHECK(a380x_nav_procedure_count(a, A380X_PROC_SID) == 3 && a380x_nav_procedure_count(a, A380X_PROC_STAR) == 2);
    CHECK(!strcmp(a380x_nav_procedure_at(a, A380X_PROC_SID, 0)->ident, "CPT3F"));   /* sorted */
    const A380XNavProcedure *cpt = a380x_nav_procedure(a, A380X_PROC_SID, "CPT3F");
    CHECK(cpt && a380x_nav_sid_serves_runway(a, cpt, "RW27R") && !a380x_nav_sid_serves_runway(a, cpt, "RW09L"));
    const A380XNavProcTrans *tr[8];
    CHECK(a380x_nav_proc_transitions(a, cpt, A380X_TRANS_COMMON, tr, 8) == 1);
    CHECK(tr[0]->leg_count == 6 && trans_leg(a, tr[0], 0)->type == A380X_LEG_CI);
    const A380XNavProcLeg *L = trans_leg(a, tr[0], 0);
    CHECK(L->alt_desc == '+' && NEAR(L->alt1_ft, 590, 0) && NEAR(L->trans_alt_ft, 6000, 0) && NEAR(L->course_deg, 268.0, 1e-4));
    L = trans_leg(a, tr[0], 1);   /* CF D255G, LON 255/7 */
    CHECK(L->type == A380X_LEG_CF && !strcmp(L->fix.ident, "D255G") && !strcmp(L->navaid.ident, "LON") && isfinite(L->navaid.lat));
    CHECK(NEAR(L->theta_deg, 255.0, 1e-4) && NEAR(L->rho_nm, 7.0, 1e-4) && NEAR(L->dist_nm, 5.0, 1e-4) && L->overfly);
    L = trans_leg(a, tr[0], 3);   /* TF WOD, NDB */
    CHECK(!strcmp(L->fix.ident, "WOD") && L->fix.kind == A380X_NAV_NDB);
    L = trans_leg(a, tr[0], 4);   /* blank descriptor with an altitude = AT */
    CHECK(L->alt_desc == '@' && NEAR(L->alt1_ft, 6000, 0));
    const A380XNavProcedure *star = a380x_nav_procedure(a, A380X_PROC_STAR, "LOGA2H");
    CHECK(star && a380x_nav_proc_transitions(a, star, A380X_TRANS_COMMON, tr, 8) == 1);
    CHECK(NEAR(trans_leg(a, tr[0], 0)->alt1_ft, 25000, 0) && trans_leg(a, tr[0], 3)->spd_desc == '-');
    const A380XNavProcedure *ils = a380x_nav_procedure(a, A380X_PROC_APPROACH, "I27R");
    char name[32];
    a380x_nav_approach_name(ils, 1, name, sizeof name);
    CHECK(ils && !strcmp(name, "ILS27R") && !strcmp(ils->runway, "RW27R") && ils->appr_type == A380X_APPR_ILS);
    CHECK(a380x_nav_proc_transitions(a, ils, A380X_TRANS_APPR_VIA, tr, 8) == 2 && !strcmp(tr[0]->ident, "BNN"));
    CHECK(trans_leg(a, tr[0], 0)->type == A380X_LEG_FC);
    CHECK(a380x_nav_proc_transitions(a, ils, A380X_TRANS_APPR_FINAL, tr, 8) == 1);
    L = trans_leg(a, tr[0], 1);   /* FF27R G 2500 / -3.00 */
    CHECK(L->alt_desc == 'G' && NEAR(L->vertical_angle_deg, -3.0, 1e-4) && L->desc[3] == 'F');
    L = trans_leg(a, tr[0], 2);
    CHECK(L->fix.kind == A380X_NAV_RUNWAY && L->desc[3] == 'M' && NEAR(L->fix.lat, 51.4777, 0.001));
    CHECK(trans_leg(a, tr[0], 3)->missed_start && !trans_leg(a, tr[0], 2)->missed_start);
    const A380XNavProcedure *rnv = a380x_nav_procedure(a, A380X_PROC_APPROACH, "R27R");
    a380x_nav_approach_name(rnv, 1, name, sizeof name);
    CHECK(rnv && !strcmp(name, "RNV27R") && !rnv->rnp_ar);
    CHECK(a380x_nav_proc_transitions(a, rnv, A380X_TRANS_APPR_FINAL, tr, 8) == 1 && NEAR(trans_leg(a, tr[0], 0)->rnp_nm, 1.0, 1e-6));
    CHECK(NEAR(trans_leg(a, tr[0], 2)->rnp_nm, 0.30, 1e-6));
    CHECK(a380x_nav_trans_matches_runway("RW27B", "RW27R") && a380x_nav_trans_matches_runway("ALL", "RW09L"));
    CHECK(!a380x_nav_trans_matches_runway("RW27B", "RW09R") && !a380x_nav_trans_matches_runway("RW27L", "RW27R"));
    CHECK(a380x_nav_approach_type_order(A380X_APPR_ILS) < a380x_nav_approach_type_order(A380X_APPR_RNAV));

    /* synthetic airport from the default layer (per-airport fallback) */
    const A380XNavAirport *x = a380x_nav_airport(nav, "XXTS");
    CHECK(x && x->info.has_cifp && x->info.from_host && x->runway_count == 3);
    if (x) CHECK(a380x_nav_runway(x, "18") && isnan(a380x_nav_runway(x, "18")->thr_lat) && isnan(a380x_nav_runway(x, "18")->length_ft));
    if (x) {
        CHECK(x->unresolved_fixes == 1 && x->malformed_lines >= 2);
        const A380XNavProcedure *s = a380x_nav_procedure(x, A380X_PROC_SID, "TST1A");
        CHECK(s && a380x_nav_proc_transitions(x, s, A380X_TRANS_RUNWAY, tr, 8) == 2);
        CHECK(a380x_nav_proc_transitions(x, s, A380X_TRANS_ENROUTE, tr, 8) == 1 && !strcmp(tr[0]->ident, "BIG"));
        CHECK(a380x_nav_proc_transitions(x, s, A380X_TRANS_COMMON, tr, 8) == 1);
        L = trans_leg(x, tr[0], 4);
        CHECK(L->type == A380X_LEG_TF && L->alt_desc == '-' && NEAR(L->alt1_ft, 7000, 0) && NEAR(L->speed_kt, 250, 0));
        const A380XNavProcedure *st = a380x_nav_procedure(x, A380X_PROC_STAR, "TST1B");
        CHECK(st && a380x_nav_star_serves_runway(x, st, "RW27") && !a380x_nav_star_serves_runway(x, st, "RW09"));
        a380x_nav_proc_transitions(x, st, A380X_TRANS_COMMON, tr, 8);
        L = trans_leg(x, tr[0], 1);
        CHECK(L->type == A380X_LEG_HM && NEAR(L->time_min, 1.0, 1e-6) && isnan(L->dist_nm) && L->turn == 'L');
        L = trans_leg(x, tr[0], 3);
        CHECK(L->type == A380X_LEG_AF && NEAR(L->rho_nm, 11.0, 1e-5) && NEAR(L->theta_deg, 250, 1e-4));
        const A380XNavProcedure *ap = a380x_nav_procedure(x, A380X_PROC_APPROACH, "R27");
        CHECK(ap && a380x_nav_proc_transitions(x, ap, A380X_TRANS_APPR_FINAL, tr, 8) == 1);
        L = trans_leg(x, tr[0], 1);
        CHECK(L->type == A380X_LEG_RF && NEAR(L->arc_radius_nm, 2.0, 1e-6) && !strcmp(L->center.ident, "D125S") && isfinite(L->center.lat));
        L = trans_leg(x, tr[0], 7);
        CHECK(L->type == A380X_LEG_VM && L->course_true && NEAR(L->course_deg, 90, 1e-6));
        L = trans_leg(x, tr[0], 2);
        CHECK(L->fix.kind == A380X_NAV_RUNWAY && L->alt_desc == '@');
        CHECK(a380x_nav_runway(x, "09") && NEAR(a380x_nav_runway(x, "09")->true_brg, 90.0, 0.1));
    }
    /* LRU of 2: EGLL evicted by a third airport, reloaded on demand */
    CHECK(a380x_nav_airport(nav, "LFPG") != NULL);   /* known (aptmeta), no CIFP: empty record */
    CHECK(a380x_nav_airport(nav, "LFPG")->proc_count == 0 && !a380x_nav_airport(nav, "LFPG")->info.has_cifp);
    CHECK(a380x_nav_airport(nav, "QQQQ") == NULL);
    A380XNavStats st;
    a380x_nav_stats(nav, &st);
    CHECK(st.cifp_cached <= 2);
    const A380XNavAirport *a2 = a380x_nav_airport(nav, "EGLL");
    CHECK(a2 && a2->runway_count == 4);
    a380x_nav_destroy(nav);

    /* default layer only: cycle from the earth_nav.dat header, default EGLL CIFP used */
    nav = open_fixture("", "default", 0);
    const A380XNavCycle *cy = a380x_nav_cycle(nav);
    CHECK(cy->valid && !strcmp(cy->airac, "2406") && !cy->from_custom_data && !strcmp(cy->source_file, "earth_nav.dat"));
    A380XNavFix f;
    CHECK(a380x_nav_find(nav, "OLDFX", A380X_NAVMASK_ALL, &f, 1) == 1);
    a = a380x_nav_airport(nav, "EGLL");
    CHECK(a && a->runway_count == 1);
    a380x_nav_destroy(nav);

    /* missing directories: graceful */
    A380XNavdataConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.xplane_root = "/nonexistent/x-plane";
    nav = a380x_nav_create(&cfg, NULL);
    CHECK(nav && a380x_nav_load_global(nav) == -1 && a380x_nav_find(nav, "BIG", A380X_NAVMASK_ALL, &f, 1) == 0);
    CHECK(a380x_nav_airport(nav, "EGLL") == NULL);
    a380x_nav_destroy(nav);
}

/* ============================================================================================ */
/* Flight plan                                                                                  */
/* ============================================================================================ */

static int find_ident(const A380XFplnRoute *r, const char *ident)
{
    for (int i = 0; i < r->count; i++) if (!a380x_fpln_is_disco(&r->el[i]) && !strcmp(r->el[i].ident, ident)) return i;
    return -1;
}
static int count_discos(const A380XFplnRoute *r)
{
    int n = 0;
    for (int i = 0; i < r->count; i++) n += a380x_fpln_is_disco(&r->el[i]);
    return n;
}
static void check_segments_ordered(const A380XFplnRoute *r)
{
    for (int i = 1; i < r->count; i++) CHECK(r->el[i].segment >= r->el[i - 1].segment);
}
static A380XNavFix fixof(A380XNavdata *nav, const char *id, const char *rg)
{
    A380XNavFix f;
    memset(&f, 0, sizeof f);
    CHECK(a380x_nav_resolve(nav, id, rg, A380X_NAV_NONE, "", 51.47, -0.46, &f));
    return f;
}
static A380XFplnAircraft ac_at(double lat, double lon, float alt, float trk, int ground)
{
    A380XFplnAircraft a;
    memset(&a, 0, sizeof a);
    a.valid = 1; a.lat = lat; a.lon = lon; a.alt_ft = alt; a.true_track_deg = trk; a.gs_kt = 250; a.on_ground = ground;
    return a;
}

static void test_fpln_departure_tmpy(A380XNavdata *nav)
{
    A380XFlightPlans *fp = a380x_fpln_create(nav);
    CHECK(fp != NULL);
    /* empty plan lines */
    CHECK(a380x_fpln_line_count(fp, A380X_PLAN_ACTIVE, NULL) == 0);
    CHECK(a380x_fpln_new_city_pair(fp, A380X_PLAN_ACTIVE, "EGLL", "ZZZZ", NULL) == A380X_FPLN_E_NOT_IN_DB);
    CHECK(a380x_fpln_new_city_pair(fp, A380X_PLAN_ACTIVE, "EGLL", "LFPG", "EGKK") == 0);
    const A380XFplnPlan *a = a380x_fpln_plan(fp, A380X_PLAN_ACTIVE);
    CHECK(a && a->pri.count == 3 && a->active_leg_index == 1 && a380x_fpln_is_disco(&a->pri.el[1]));
    CHECK(!strcmp(a->pri.el[0].ident, "EGLL") && !strcmp(a->pri.el[2].ident, "LFPG") && a->pri.destination_leg_index == 2);
    CHECK(a->alt.count == 3 && !strcmp(a->alt.el[0].ident, "LFPG") && !strcmp(a->alt.el[2].ident, "EGKK"));
    CHECK(NEAR(a->pri.db_trans_alt_ft, 6000, 0) && isnan(a->pri.db_trans_level_ft));
    /* line list: EGLL, DISCO, LFPG, END, LFPG(altn), DISCO, EGKK, END ALTN */
    A380XFplnLineIter it;
    A380XFplnLine ln;
    a380x_fpln_lines_begin(&it, fp, A380X_PLAN_ACTIVE, NULL);
    const char *want[] = { "EGLL", "DISCONTINUITY", "LFPG", "END OF F-PLN", "LFPG", "DISCONTINUITY", "EGKK", "END OF ALTN F-PLN" };
    int k = 0;
    while (a380x_fpln_lines_next(&it, &ln)) {
        const char *txt = ln.kind == A380X_FLINE_SPECIAL ? ln.special_text : ln.ident;
        CHECK(k < 8 && !strcmp(txt, want[k]));
        if (k == 4) CHECK(ln.is_altn && ln.leg_index == 0);
        if (k == 1) CHECK(ln.leg_index == 1);
        k++;
    }
    CHECK(k == 8);
    CHECK(a380x_fpln_line_of_leg(fp, A380X_PLAN_ACTIVE, NULL, 2, 0) == 2);

    /* a refused edit leaves no TMPY */
    uint32_t v0 = a->version;
    CHECK(a380x_fpln_set_sid(fp, A380X_PLAN_ACTIVE, 0, "NOSID") == A380X_FPLN_E_NOT_IN_DB);
    CHECK(!a380x_fpln_has_tmpy(fp) && a->version == v0);
    /* runway + SID go to TMPY */
    CHECK(a380x_fpln_set_origin_runway(fp, A380X_PLAN_ACTIVE, 0, "27R") == 0);
    CHECK(a380x_fpln_has_tmpy(fp) && a380x_fpln_loaded_index(fp, 0) == A380X_PLAN_TEMPORARY);
    CHECK(!strcmp(a->pri.el[0].ident, "EGLL"));   /* ACTIVE untouched */
    const A380XFplnPlan *t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);
    CHECK(t && !strcmp(t->pri.el[0].ident, "EGLL27R") && t->pri.el[0].wp_desc == A380X_WPDESC_RUNWAY);
    CHECK(t->flags & A380X_PLANF_COPIED_FROM_ACTIVE);
    CHECK(a380x_fpln_set_sid(fp, A380X_PLAN_ACTIVE, 0, "CPT3G") == A380X_FPLN_E_NOT_ALLOWED);  /* 09R only */
    CHECK(a380x_fpln_set_sid(fp, A380X_PLAN_ACTIVE, 0, "CPT3F") == 0);
    const A380XFplnRoute *r = &t->pri;
    /* EGLL27R, 590 (CI), D255G, D259K, WOD, D100H, CPT, DISCO, LFPG */
    CHECK(r->count == 9);
    CHECK(r->el[1].type == A380X_LEG_CI && !strcmp(r->el[1].ident, "INTCPT") && !strcmp(r->el[1].annotation, "C268\xc2\xb0"));
    CHECK(!strcmp(r->el[2].ident, "D255G") && !strcmp(r->el[2].annotation, "C255\xc2\xb0") && r->el[2].segment == A380X_SEG_SID);
    CHECK(!strcmp(r->el[4].ident, "WOD") && !strcmp(r->el[4].annotation, "CPT3F"));
    CHECK(!strcmp(r->el[6].ident, "CPT") && a380x_fpln_is_disco(&r->el[7]) && (r->el[7].flags & A380X_LEGF_AUTO_DISCO));
    CHECK(!strcmp(r->el[0].annotation, "CPT3F") && !strcmp(r->sid, "CPT3F"));
    CHECK(r->el[1].term_lat == r->el[1].term_lat && (r->el[2].flags & A380X_LEGF_CALC_VALID));
    check_segments_ordered(r);
    float d_cpt = r->el[6].cum_dist_nm;
    CHECK(d_cpt > 25 && d_cpt < 60);
    /* TMPY line list has yellow-plan semantics only through the index; ERASE discards */
    CHECK(a380x_fpln_tmpy_erase(fp) == 0 && !a380x_fpln_has_tmpy(fp) && a->pri.count == 3);
    CHECK(a380x_fpln_set_origin_runway(fp, A380X_PLAN_ACTIVE, 0, "RW27R") == 0);
    CHECK(a380x_fpln_set_sid(fp, A380X_PLAN_ACTIVE, 0, "DET2F") == 0);
    CHECK(a380x_fpln_tmpy_insert(fp, NULL) == 0 && !a380x_fpln_has_tmpy(fp));
    r = &a->pri;
    CHECK(!strcmp(r->sid, "DET2F") && find_ident(r, "DET") >= 0 && a->version > v0);
    int i_det = find_ident(r, "DET");
    /* INSERT NEXT WPT: BIG after DET -> BIG + disco; duplicate rule */
    A380XNavFix big = fixof(nav, "BIG", "EG");
    CHECK(a380x_fpln_next_waypoint(fp, A380X_PLAN_ACTIVE, 0, i_det, &big) == 0);
    t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);
    r = &t->pri;
    int i_big = find_ident(r, "BIG");
    CHECK(i_big == i_det + 1 && r->el[i_big].segment == A380X_SEG_ENROUTE && r->el[i_big].type == A380X_LEG_TF);
    CHECK(a380x_fpln_is_disco(&r->el[i_big + 1]) && (r->el[i_big].flags & A380X_LEGF_PILOT));
    check_segments_ordered(r);
    /* airways from BIG: L9 then L10 with the automatic intersection at DVR, TO ITVIP */
    CHECK(a380x_fpln_airway_start(fp, A380X_PLAN_ACTIVE, 0, i_big) == 0);
    CHECK(a380x_fpln_airway_via(fp, A380X_PLAN_ACTIVE, "Q999") == A380X_FPLN_E_NOT_IN_DB);
    CHECK(a380x_fpln_airway_via(fp, A380X_PLAN_ACTIVE, "L10") == A380X_FPLN_E_NOT_ALLOWED);
    CHECK(a380x_fpln_airway_via(fp, A380X_PLAN_ACTIVE, "L9") == 0);
    CHECK(a380x_fpln_airway_via(fp, A380X_PLAN_ACTIVE, "L10") == 0);
    t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);
    CHECK(t->awy.line_count == 2 && !strcmp(t->awy.line[0].to.ident, "DVR") && t->awy.line[0].to_locked);
    CHECK(a380x_fpln_airway_to_ident(fp, A380X_PLAN_ACTIVE, "KONAN") == A380X_FPLN_E_NOT_IN_DB);  /* not on L10 */
    CHECK(a380x_fpln_airway_to_ident(fp, A380X_PLAN_ACTIVE, "ITVIP") == 0);
    CHECK(t->awy.line_count == 3 && !strcmp(t->awy.line[2].from.ident, "ITVIP"));
    CHECK(a380x_fpln_tmpy_insert(fp, NULL) == 0);     /* INSERT finalises pending airways */
    r = &a->pri;
    i_big = find_ident(r, "BIG");
    CHECK(i_big > 0 && !strcmp(r->el[i_big + 1].ident, "DIKVU") && !strcmp(r->el[i_big + 1].annotation, "L9"));
    CHECK(!strcmp(r->el[i_big + 4].ident, "DVR") && !strcmp(r->el[i_big + 5].ident, "ITVIP") && !strcmp(r->el[i_big + 5].annotation, "L10"));
    CHECK(a380x_fpln_is_disco(&r->el[i_big + 6]) && !strcmp(r->el[r->count - 1].ident, "LFPG"));
    CHECK(!a->awy.active);
    check_segments_ordered(r);
    /* INSERT NEXT WPT of a downstream fix removes the legs in between */
    A380XNavFix dvr = fixof(nav, "DVR", "EG");
    int before = r->count;
    CHECK(a380x_fpln_next_waypoint(fp, A380X_PLAN_ACTIVE, 0, i_big, &dvr) == 0);
    t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);
    CHECK(t->pri.count == before - 3 && !strcmp(t->pri.el[i_big + 1].ident, "DVR"));
    a380x_fpln_tmpy_erase(fp);
    /* DELETE: from leg refused, normal leg removed, disco between duplicates merged */
    CHECK(a380x_fpln_delete_element(fp, A380X_PLAN_ACTIVE, 0, 0) == A380X_FPLN_E_NOT_ALLOWED);
    CHECK(!a380x_fpln_has_tmpy(fp));
    CHECK(a380x_fpln_delete_element(fp, A380X_PLAN_ACTIVE, 0, i_big + 2) == 0);   /* EMKAD */
    t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);
    CHECK(find_ident(&t->pri, "EMKAD") < 0 && t->pri.el[i_big + 2].type == A380X_LEG_TF);
    CHECK(a380x_fpln_delete_element(fp, A380X_PLAN_ACTIVE, 0, t->pri.count - 2) == 0);   /* disco before LFPG */
    CHECK(count_discos(&t->pri) == 0 && t->pri.el[t->pri.count - 1].type == A380X_LEG_TF);
    a380x_fpln_tmpy_erase(fp);
    /* overfly toggle */
    CHECK(a380x_fpln_toggle_overfly(fp, A380X_PLAN_ACTIVE, 0, i_big) == 0);
    t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);
    CHECK(t->pri.el[i_big].flags & A380X_LEGF_OVERFLY);
    a380x_fpln_lines_begin(&it, fp, A380X_PLAN_TEMPORARY, NULL);
    int saw_overfly = 0;
    while (a380x_fpln_lines_next(&it, &ln)) if (ln.leg_index == i_big && ln.kind == A380X_FLINE_WAYPOINT) saw_overfly = ln.overfly;
    CHECK(saw_overfly);
    a380x_fpln_tmpy_erase(fp);
    /* constraints: CLB at an enroute leg propagates backwards */
    CHECK(a380x_fpln_set_alt_cstr(fp, A380X_PLAN_ACTIVE, 0, 0, 0, NULL) == A380X_FPLN_E_NOT_ALLOWED);   /* runway */
    A380XFplnAltCstr c = { '+', 15000, NAN };
    CHECK(a380x_fpln_set_alt_cstr(fp, A380X_PLAN_ACTIVE, 0, i_big + 1, 0, &c) == 0);
    t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);
    CHECK(t->pri.el[i_big + 1].cstr_type == A380X_CSTR_CLB && t->pri.el[i_big].cstr_type == A380X_CSTR_CLB);
    CHECK(a380x_fpln_eff_alt(&t->pri.el[i_big + 1]).desc == '+');
    CHECK(a380x_fpln_set_spd_cstr(fp, A380X_PLAN_ACTIVE, 0, i_big + 1, 0, 250) == 0);
    CHECK(a380x_fpln_eff_spd(&t->pri.el[i_big + 1]).desc == '-' && NEAR(a380x_fpln_eff_spd(&t->pri.el[i_big + 1]).speed, 250, 0));
    CHECK(a380x_fpln_set_spd_cstr(fp, A380X_PLAN_ACTIVE, 0, i_big + 1, 0, 0) == 0 && !a380x_fpln_eff_spd(&t->pri.el[i_big + 1]).desc);
    a380x_fpln_tmpy_erase(fp);

    /* SEC: copy, edit in place (no TMPY), activate/swap refused while TMPY exists */
    CHECK(a380x_fpln_sec_copy_from_active(fp, 1) == 0);
    const A380XFplnPlan *s1 = a380x_fpln_plan(fp, A380X_PLAN_SEC1);
    CHECK(s1 && (s1->flags & A380X_PLANF_COPIED_FROM_ACTIVE) && s1->pri.count == a->pri.count);
    CHECK(a380x_fpln_delete_element(fp, A380X_PLAN_SEC1, 0, i_big) == 0 && !a380x_fpln_has_tmpy(fp));
    CHECK(s1->pri.count == a->pri.count - 1);
    CHECK(a380x_fpln_set_origin_runway(fp, A380X_PLAN_ACTIVE, 0, "09L") == 0);
    CHECK(a380x_fpln_sec_activate(fp, 1) == A380X_FPLN_E_TMPY_EXISTS);
    a380x_fpln_tmpy_erase(fp);
    CHECK(a380x_fpln_sec_can_activate(fp, 1, 0, 1) == 1);
    int n_sec = s1->pri.count, n_act = a->pri.count;
    CHECK(a380x_fpln_sec_swap(fp, 1) == 0 && a->pri.count == n_sec && s1->pri.count == n_act);
    CHECK(a380x_fpln_sec_activate(fp, 1) == 0 && a->pri.count == n_act);
    CHECK(a380x_fpln_sec_delete(fp, 1) == 0 && a380x_fpln_plan(fp, A380X_PLAN_SEC1) == NULL);
    /* NEW DEST */
    int i_dvr = find_ident(&a->pri, "DVR");
    CHECK(a380x_fpln_new_dest(fp, A380X_PLAN_ACTIVE, 0, i_dvr, "EGKK") == 0);
    t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);
    CHECK(t->pri.count == i_dvr + 3 && a380x_fpln_is_disco(&t->pri.el[i_dvr + 1]) && !strcmp(t->pri.el[i_dvr + 2].ident, "EGKK"));
    CHECK(!strcmp(t->pri.dest_icao, "EGKK") && t->pri.destination_leg_index == i_dvr + 2);
    CHECK(!strcmp(t->alt.origin_icao, "EGKK"));
    a380x_fpln_tmpy_erase(fp);
    /* ENABLE ALTN */
    int crz = 0;
    CHECK(a380x_fpln_enable_altn(fp, A380X_PLAN_ACTIVE, i_dvr, &crz) == 0 && crz == 220);
    t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);
    CHECK(!strcmp(t->pri.dest_icao, "EGKK") && t->alt.count == 0 && !strcmp(t->pri.el[t->pri.count - 1].ident, "EGKK"));
    a380x_fpln_tmpy_erase(fp);
    a380x_fpln_destroy(fp);
}

static void test_fpln_arrival(A380XNavdata *nav)
{
    A380XFlightPlans *fp = a380x_fpln_create(nav);
    CHECK(a380x_fpln_new_city_pair(fp, A380X_PLAN_SEC2, "EGKK", "EGLL", NULL) == 0);   /* SEC: edits in place */
    const A380XFplnPlan *p = a380x_fpln_plan(fp, A380X_PLAN_SEC2);
    CHECK(p && p->alt.count == 0);
    CHECK(a380x_fpln_set_star(fp, A380X_PLAN_SEC2, 0, "LAM1Z") == 0 && !a380x_fpln_has_tmpy(fp));
    CHECK(a380x_fpln_set_approach(fp, A380X_PLAN_SEC2, 0, "I27R") == 0);
    CHECK(!strcmp(p->pri.dest_rwy, "RW27R"));
    CHECK(a380x_fpln_set_appr_via(fp, A380X_PLAN_SEC2, 0, "XXX") == A380X_FPLN_E_NOT_IN_DB);
    CHECK(a380x_fpln_set_appr_via(fp, A380X_PLAN_SEC2, 0, "BNN") == 0);
    const A380XFplnRoute *r = &p->pri;
    check_segments_ordered(r);
    /* EGKK, DISCO, LAM, DONNA, BNN, BNN/11 (FC), D125O, D125S, D131V, CF27R, FF27R, EGLL27R, 1580, 3000 */
    int i_lam = find_ident(r, "LAM");
    CHECK(i_lam == 2 && a380x_fpln_is_disco(&r->el[1]) && r->el[i_lam].segment == A380X_SEG_STAR);
    int i_bnn = find_ident(r, "BNN");
    CHECK(i_bnn == 4 && r->el[i_bnn + 1].type == A380X_LEG_FC && !strcmp(r->el[i_bnn + 1].ident, "BNN/11"));
    CHECK(r->el[i_bnn + 1].segment == A380X_SEG_APPR_VIA && count_discos(r) == 1);   /* connected without a disco */
    int i_cf = find_ident(r, "CF27R");
    CHECK(i_cf > 0 && r->el[i_cf + 1].segment == A380X_SEG_APPROACH && find_ident(r, "CF27R") == i_cf);
    int i_rw = find_ident(r, "EGLL27R");
    CHECK(i_rw > 0 && r->destination_leg_index == i_rw && r->first_missed_appr_index == i_rw + 1);
    CHECK(r->count == i_rw + 3 && r->el[i_rw + 1].segment == A380X_SEG_MISSED && !strcmp(r->el[i_rw + 1].ident, "1580"));
    CHECK(!strcmp(r->el[i_rw].annotation, "ILS27R") && !strcmp(r->el[i_rw + 1].annotation, "C268\xc2\xb0"));
    CHECK(r->el[i_rw + 1].cum_dist_nm < r->el[i_rw].cum_dist_nm);   /* missed approach restarts */
    CHECK(r->el[i_bnn].cstr_type == A380X_CSTR_DES);
    char nm[16];
    a380x_fpln_approach_name(fp, r, nm, sizeof nm);
    CHECK(!strcmp(nm, "ILS27R"));
    a380x_fpln_runway_display(r, 1, nm, sizeof nm);
    CHECK(!strcmp(nm, "EGLL27R"));
    /* line list: missed approach rows flagged, DIST restarts, END OF F-PLN then NO ALTN F-PLN */
    A380XFplnLineIter it;
    A380XFplnLine ln;
    a380x_fpln_lines_begin(&it, fp, A380X_PLAN_SEC2, NULL);
    int n = 0, missed = 0, end = -1, noalt = -1;
    float d_first_missed = NAN;
    while (a380x_fpln_lines_next(&it, &ln)) {
        if (ln.kind == A380X_FLINE_WAYPOINT && ln.is_missed) { if (!missed) d_first_missed = ln.dist_nm; missed++; }
        if (ln.special == A380X_FSPEC_END_OF_FPLN) end = n;
        if (ln.special == A380X_FSPEC_NO_ALTN_FPLN) noalt = n;
        if (ln.kind == A380X_FLINE_WAYPOINT && ln.leg_index == i_bnn) CHECK(isfinite(ln.trk_true_deg) && isfinite(ln.dist_nm));
        if (ln.kind == A380X_FLINE_WAYPOINT && ln.leg_index == i_lam) CHECK(!(ln.dist_nm > 0));   /* after a disco: no path */
        if (ln.kind == A380X_FLINE_WAYPOINT && ln.leg_index == i_rw) CHECK(ln.use_trans_level && NEAR(ln.fpa_deg, -3.0, 1e-4));
        n++;
    }
    CHECK(missed == 2 && isfinite(d_first_missed) && d_first_missed > 0 && end == n - 2 && noalt == n - 1);
    /* STAR transition + approach change keep the enroute legs */
    A380XNavFix sam = fixof(nav, "SAM", "EG");
    CHECK(a380x_fpln_next_waypoint(fp, A380X_PLAN_SEC2, 0, 0, &sam) == 0);
    CHECK(find_ident(&p->pri, "SAM") == 1 && p->pri.el[1].segment == A380X_SEG_ENROUTE);
    CHECK(a380x_fpln_set_approach(fp, A380X_PLAN_SEC2, 0, "R27R") == 0);
    r = &p->pri;
    CHECK(find_ident(r, "SAM") == 1 && find_ident(r, "LAM") > 0 && find_ident(r, "IVLAR") > 0);
    CHECK(find_ident(r, "BNN/11") < 0 && r->el[find_ident(r, "IVLAR") - 1].flags & A380X_LEGF_DISCONTINUITY);   /* STAR end BNN != IVLAR */
    CHECK(r->el[r->first_missed_appr_index].type == A380X_LEG_CA);
    CHECK(a380x_fpln_set_dest_runway(fp, A380X_PLAN_SEC2, 0, "09L") == 0);
    r = &p->pri;
    CHECK(!p->pri.approach[0] && !strcmp(r->el[r->count - 1].ident, "EGLL09L") && r->destination_leg_index == r->count - 1);
    /* holds */
    int i_d = find_ident(r, "DONNA");
    A380XFplnHold dh;
    CHECK(a380x_fpln_default_hold(fp, A380X_PLAN_SEC2, 0, i_d, 10000, 0, &dh) == 0);
    CHECK(dh.type == A380X_HOLD_COMPUTED && dh.turn == A380X_TURN_RIGHT && NEAR(dh.time_min, 1.0, 0) && dh.inbound_crs_mag != 100.0f);
    CHECK(a380x_fpln_default_hold(fp, A380X_PLAN_SEC2, 0, i_d, 20000, 0, &dh) == 0 && NEAR(dh.time_min, 1.5, 0));
    int i_l = find_ident(r, "LAM");
    A380XFplnHold db;
    CHECK(a380x_fpln_default_hold(fp, A380X_PLAN_SEC2, 0, i_l, 9000, 1, &db) == 0 && db.type == A380X_HOLD_DATABASE);
    CHECK(db.turn == A380X_TURN_LEFT && NEAR(db.inbound_crs_mag, 262, 0));
    int hm = a380x_fpln_add_or_edit_hold(fp, A380X_PLAN_SEC2, 0, i_d, &dh, NULL, &dh);
    CHECK(hm == i_d + 1 && p->pri.el[hm].type == A380X_LEG_HM && !strcmp(p->pri.el[hm].ident, "DONNA"));
    A380XFplnHold mod = dh;
    mod.type = A380X_HOLD_PILOT; mod.turn = A380X_TURN_LEFT; mod.inbound_crs_mag = 90;
    CHECK(a380x_fpln_add_or_edit_hold(fp, A380X_PLAN_SEC2, 0, hm, &mod, &mod, &dh) == hm);
    CHECK(p->pri.el[hm].hold_modified.valid && p->pri.el[hm].turn == A380X_TURN_LEFT);
    a380x_fpln_lines_begin(&it, fp, A380X_PLAN_SEC2, NULL);
    int saw_hold = 0;
    while (a380x_fpln_lines_next(&it, &ln)) {
        if (ln.kind == A380X_FLINE_HOLD_HM) { saw_hold = 1; CHECK(!strcmp(ln.ident, "HOLD L") && ln.leg_index == hm); }
        if (ln.kind == A380X_FLINE_WAYPOINT && ln.leg_index == hm) CHECK(!strcmp(ln.annotation, "C090\xc2\xb0\xc2\xa0\xc2\xa0"));
    }
    CHECK(saw_hold);
    A380XFplnLegPath path;
    CHECK(a380x_fpln_leg_path(fp, A380X_PLAN_SEC2, 0, hm, &path) && path.kind == A380X_PATH_HOLD && !path.clockwise);
    CHECK(a380x_fpln_revert_hold_to_computed(fp, A380X_PLAN_SEC2, 0, hm) == 0 && !p->pri.el[hm].hold_modified.valid);
    CHECK(a380x_fpln_delete_element(fp, A380X_PLAN_SEC2, 0, i_d) == 0);   /* fix + its hold */
    CHECK(find_ident(&p->pri, "DONNA") < 0);
    a380x_fpln_destroy(fp);
}

static void test_fpln_dirto_sequencing(A380XNavdata *nav)
{
    A380XFlightPlans *fp = a380x_fpln_create(nav);
    a380x_fpln_new_city_pair(fp, A380X_PLAN_ACTIVE, "EGLL", "LFPG", NULL);
    a380x_fpln_set_origin_runway(fp, A380X_PLAN_ACTIVE, 0, "27R");
    a380x_fpln_set_sid(fp, A380X_PLAN_ACTIVE, 0, "DET2F");
    a380x_fpln_tmpy_insert(fp, NULL);
    const A380XFplnPlan *a = a380x_fpln_plan(fp, A380X_PLAN_ACTIVE);
    int i_det = find_ident(&a->pri, "DET");
    A380XNavFix big = fixof(nav, "BIG", "EG"), dvr = fixof(nav, "DVR", "EG");
    a380x_fpln_next_waypoint(fp, A380X_PLAN_ACTIVE, 0, i_det, &big);
    a380x_fpln_tmpy_insert(fp, NULL);
    a380x_fpln_airway_start(fp, A380X_PLAN_ACTIVE, 0, i_det + 1);
    a380x_fpln_airway_via(fp, A380X_PLAN_ACTIVE, "L9");
    a380x_fpln_airway_to_ident(fp, A380X_PLAN_ACTIVE, "KONAN");
    a380x_fpln_airway_finalise(fp, A380X_PLAN_ACTIVE);
    a380x_fpln_tmpy_insert(fp, NULL);
    const A380XFplnRoute *r = &a->pri;
    CHECK(find_ident(r, "KONAN") == i_det + 6);

    /* sequencing: on the ground nothing; airborne the CF/TF legs sequence near their fixes */
    A380XFplnAircraft ac = ac_at(r->el[0].fix.lat, r->el[0].fix.lon, 100, 268, 1);
    CHECK(a380x_fpln_update(fp, &ac) == 0 && a->active_leg_index == 1);
    const A380XFplnLeg *l1 = &r->el[1];
    ac = ac_at(l1->term_lat, l1->term_lon, 1500, 255, 0);
    uint32_t ev = a380x_fpln_update(fp, &ac);
    CHECK((ev & A380X_FPLN_EVT_SEQUENCED) && a->active_leg_index >= 2);
    for (int step = 0; step < 40 && a->active_leg_index < i_det + 6; step++) {
        const A380XFplnLeg *l = &r->el[a->active_leg_index];
        if (!isfinite(l->term_lat)) break;
        ac = ac_at(l->term_lat, l->term_lon, 20000, l->path_true_crs, 0);
        a380x_fpln_update(fp, &ac);
    }
    CHECK(a->active_leg_index == i_det + 6);   /* KONAN, the leg before the disco */
    ac = ac_at(r->el[i_det + 6].fix.lat, r->el[i_det + 6].fix.lon, 20000, 110, 0);
    ev = a380x_fpln_update(fp, &ac);
    CHECK((ev & A380X_FPLN_EVT_AT_DISCO) && (ev & A380X_FPLN_EVT_DISCO_AHEAD) && a->active_leg_index == i_det + 6);
    CHECK(a380x_fpln_from_leg_index(a) == i_det + 5);

    /* reset to an active leg early in the plan for DIR TO */
    a380x_fpln_sec_copy_from_active(fp, 3);
    A380XFlightPlans *g = fp;
    g->plan[A380X_PLAN_ACTIVE].active_leg_index = i_det + 1;   /* TO = BIG */
    a380x_fpln_recompute(g, A380X_PLAN_ACTIVE);
    A380XFplnAircraft pos = ac_at(51.40, -0.10, 9000, 90, 0);
    int k_dvr = find_ident(r, "DVR");
    CHECK(a380x_fpln_direct_to_leg(fp, A380X_PLAN_ACTIVE, &pos, 0, A380X_DIRTO_DIRECT, NAN) == A380X_FPLN_E_NOT_ALLOWED);
    CHECK(a380x_fpln_direct_to_leg(fp, A380X_PLAN_ACTIVE, &pos, k_dvr, A380X_DIRTO_DIRECT, NAN) == 0);
    const A380XFplnPlan *t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);
    CHECK(t && t->pri.count == r->count - k_dvr + 1 && t->active_leg_index == 1);
    CHECK(!strcmp(t->pri.el[0].ident, "T-P") && (t->pri.el[0].flags & A380X_LEGF_PENDING_DIRTO_TP));
    CHECK(t->pri.el[1].type == A380X_LEG_DF && !strcmp(t->pri.el[1].ident, "DVR") && !strcmp(t->pri.el[2].ident, "KONAN"));
    float d = t->pri.el[1].cum_dist_nm;
    CHECK(NEAR(d, a380x_geo_distance_nm(pos.lat, pos.lon, dvr.lat, dvr.lon), 0.01));   /* DIR TO page DIST */
    CHECK(!strcmp(t->pri.origin_icao, "EGLL"));
    A380XFplnAircraft later = ac_at(51.35, 0.20, 9500, 100, 0);
    CHECK(a380x_fpln_tmpy_insert(fp, &later) == 0);
    CHECK(a->active_leg_index == 1 && NEAR(a->pri.el[0].fix.lat, 51.35, 1e-9) && !(a->pri.el[0].flags & A380X_LEGF_PENDING_DIRTO_TP));
    CHECK(a380x_fpln_dist_to_dest(fp, A380X_PLAN_ACTIVE, &later) > a380x_geo_distance_nm(later.lat, later.lon, dvr.lat, dvr.lon) + 100.0);   /* across the disco to LFPG */
    /* restore from SEC3 and try DIR TO a fix outside the plan and WITH ABEAM */
    CHECK(a380x_fpln_sec_activate(fp, 3) == 0);
    g->plan[A380X_PLAN_ACTIVE].active_leg_index = i_det + 1;
    a380x_fpln_recompute(g, A380X_PLAN_ACTIVE);
    A380XNavFix lam = fixof(nav, "LAM", "EG");
    CHECK(a380x_fpln_direct_to_fix(fp, A380X_PLAN_ACTIVE, &pos, &lam, A380X_DIRTO_DIRECT, NAN) == 0);
    t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);
    CHECK(!strcmp(t->pri.el[1].ident, "LAM") && a380x_fpln_is_disco(&t->pri.el[2]) && !strcmp(t->pri.el[3].ident, "BIG"));
    CHECK(t->pri.el[3].type == A380X_LEG_IF);
    CHECK(a380x_fpln_tmpy_erase(fp) == 0);
    A380XFplnAircraft west = ac_at(51.33, -0.60, 9000, 90, 0);
    CHECK(a380x_fpln_direct_to_leg(fp, A380X_PLAN_ACTIVE, &west, find_ident(r, "UMTUM"), A380X_DIRTO_ABEAM, NAN) == 0);
    t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);
    int nab = 0;
    for (int i = 0; i < t->pri.count; i++) if (t->pri.el[i].flags & A380X_LEGF_ABEAM) nab++;
    CHECK(nab >= 2 && !strncmp(t->pri.el[1].ident, "AB", 2) && t->pri.el[1].type == A380X_LEG_DF);
    CHECK(a380x_fpln_direct_to_leg(fp, A380X_PLAN_ACTIVE, &pos, find_ident(r, "DVR"), A380X_DIRTO_RADIAL_OUT, 120) == 0);
    t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);   /* previous TMPY replaced */
    CHECK(t->pri.el[2].type == A380X_LEG_FM && !strcmp(t->pri.el[2].ident, "MANUAL") && a380x_fpln_is_disco(&t->pri.el[3]));
    CHECK(a380x_fpln_delete_element(fp, A380X_PLAN_ACTIVE, 0, 3) == A380X_FPLN_E_NOT_ALLOWED);   /* disco after FM */
    CHECK(a380x_fpln_direct_to_leg(fp, A380X_PLAN_ACTIVE, &pos, find_ident(r, "DVR"), A380X_DIRTO_RADIAL_IN, 120) == 0);
    t = a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY);
    CHECK(t->pri.el[1].type == A380X_LEG_CF && NEAR(t->pri.el[1].course_deg, 120, 0) && !strcmp(t->pri.el[1].annotation, "C120\xc2\xb0"));
    a380x_fpln_tmpy_erase(fp);
    /* FM leg is never sequenced */
    a380x_fpln_direct_to_leg(fp, A380X_PLAN_ACTIVE, &pos, find_ident(r, "DVR"), A380X_DIRTO_RADIAL_OUT, 120);
    a380x_fpln_tmpy_insert(fp, &pos);
    ac = ac_at(dvr.lat, dvr.lon, 9000, 120, 0);
    a380x_fpln_update(fp, &ac);
    CHECK(a->active_leg_index == 2);
    ac = ac_at(dvr.lat - 0.2, dvr.lon + 0.3, 9000, 120, 0);
    ev = a380x_fpln_update(fp, &ac);
    CHECK(a->active_leg_index == 2 && (ev & A380X_FPLN_EVT_MANUAL_LEG));
    /* paths for the ND */
    A380XFplnLegPath path;
    CHECK(a380x_fpln_leg_path(fp, A380X_PLAN_ACTIVE, 0, 1, &path) && path.kind == A380X_PATH_LINE);
    CHECK(!a380x_fpln_leg_path(fp, A380X_PLAN_ACTIVE, 0, 3, &path));   /* disco */
    a380x_fpln_destroy(fp);
}

static void test_fpln_xxts(A380XNavdata *nav)
{
    /* every leg type of the synthetic airport gets a name and (where defined) geometry */
    A380XFlightPlans *fp = a380x_fpln_create(nav);
    CHECK(a380x_fpln_new_city_pair(fp, A380X_PLAN_SEC1, "XXTS", "XXTS", NULL) == 0);
    CHECK(a380x_fpln_set_origin_runway(fp, A380X_PLAN_SEC1, 0, "09") == 0);
    CHECK(a380x_fpln_set_sid(fp, A380X_PLAN_SEC1, 0, "TST1A") == 0);
    CHECK(a380x_fpln_set_sid_trans(fp, A380X_PLAN_SEC1, 0, "BIG") == 0);
    CHECK(a380x_fpln_set_star(fp, A380X_PLAN_SEC1, 0, "TST1B") == 0);
    CHECK(a380x_fpln_set_approach(fp, A380X_PLAN_SEC1, 0, "R27") == 0);
    const A380XFplnRoute *r = &a380x_fpln_plan(fp, A380X_PLAN_SEC1)->pri;
    check_segments_ordered(r);
    int i;
    CHECK((i = find_ident(r, "1000")) == 1 && !strcmp(r->el[i].annotation, "H090\xc2\xb0") && (r->el[i].flags & A380X_LEGF_EST_GEOMETRY));
    CHECK((i = find_ident(r, "OCK/05")) > 0 && r->el[i].type == A380X_LEG_FD && isfinite(r->el[i].leg_dist_nm));
    CHECK((i = find_ident(r, "LON/10")) > 0 && r->el[i].type == A380X_LEG_CD);
    if (i > 0) CHECK(NEAR(a380x_geo_distance_nm(r->el[i].term_lat, r->el[i].term_lon, r->el[i].navaid.lat, r->el[i].navaid.lon), 10.0, 0.05));
    CHECK((i = find_ident(r, "OCK063")) > 0 && r->el[i].type == A380X_LEG_CR && isfinite(r->el[i].term_lat));
    CHECK(find_ident(r, "BIG") > 0 && find_ident(r, "DIKVU") == find_ident(r, "BIG") + 1);   /* SID end = trans start merged */
    CHECK((i = find_ident(r, "LAM")) > 0 && r->el[i + 1].type == A380X_LEG_HM && !strcmp(r->el[i + 1].annotation, "HOLD L"));
    CHECK((i = find_ident(r, "LAM/05")) > 0 && r->el[i].type == A380X_LEG_FC);
    CHECK((i = find_ident(r, "D131V")) > 0 && r->el[i].type == A380X_LEG_AF && !strcmp(r->el[i].annotation, "11 LAM"));
    CHECK((i = find_ident(r, "D125S")) > 0 && r->el[i].segment == A380X_SEG_STAR_RWY_TRANS && r->el[i].alt_db.desc == 'B');
    CHECK((i = find_ident(r, "CF27R")) > 0 && r->el[i].type == A380X_LEG_RF && !strcmp(r->el[i].annotation, " 4 ARC") && NEAR(r->el[i].leg_dist_nm, 4.0, 1e-5));
    A380XFplnLegPath path;
    CHECK(a380x_fpln_leg_path(fp, A380X_PLAN_SEC1, 0, i, &path) && path.kind == A380X_PATH_ARC && NEAR(path.radius_nm, 2.0, 0.5));
    CHECK((i = find_ident(r, "XXTS27")) > 0 && r->destination_leg_index == i);
    CHECK((i = find_ident(r, "INTCPT")) > 0);
    CHECK((i = find_ident(r, "MANUAL")) > 0 && !strcmp(r->el[i].annotation, "H090T"));
    int seen_pi = 0;
    for (int k = 0; k < r->count; k++) if (r->el[k].type == A380X_LEG_PI) { seen_pi = 1; CHECK(!strcmp(r->el[k].annotation, "PROC L")); }
    CHECK(seen_pi);
    /* VIA with an unresolved fix is still selectable; the leg is flagged */
    CHECK(a380x_fpln_set_appr_via(fp, A380X_PLAN_SEC1, 0, "ZZZ") == 0);
    r = &a380x_fpln_plan(fp, A380X_PLAN_SEC1)->pri;
    CHECK((i = find_ident(r, "ZZZZZ")) > 0 && (r->el[i].flags & A380X_LEGF_FIX_UNRESOLVED) && !(r->el[i].flags & A380X_LEGF_CALC_VALID));
    CHECK(a380x_fpln_line_count(fp, A380X_PLAN_SEC1, NULL) > r->count);
    a380x_fpln_destroy(fp);
}

static void test_fpln_misc(A380XNavdata *nav)
{
    A380XFlightPlans *fp = a380x_fpln_create(nav);
    /* fix info, pilot idents, 200-leg limit */
    CHECK(a380x_fpln_set_fix_info(fp, 1, NULL) == A380X_FPLN_E_NO_PLAN);
    a380x_fpln_new_city_pair(fp, A380X_PLAN_ACTIVE, "EGLL", "LFPG", NULL);
    A380XFplnFixInfo fi;
    memset(&fi, 0, sizeof fi);
    fi.valid = 1; fi.fix = fixof(nav, "BIG", "EG"); fi.radial_count = 1; fi.radial_mag[0] = 90;
    fake_var_value_set = 1; fake_var_value = 2.0;
    CHECK(a380x_fpln_set_fix_info(fp, 2, &fi) == 0 && NEAR(fp->plan[0].fix_info[1].radial_true[0], 92, 1e-5));
    fake_var_value_set = 0;
    char id[A380X_NAV_IDENT_LEN];
    a380x_fpln_next_pilot_ident(fp, "PBD", id);
    CHECK(!strcmp(id, "PBD01"));
    a380x_fpln_next_pilot_ident(fp, "LL", id);
    CHECK(!strcmp(id, "LL02"));
    A380XNavFix f;
    int ok = 0, err = 0;
    for (int k = 0; k < 250; k++) {
        char nm[8];
        snprintf(nm, sizeof nm, "W%03d", k);
        a380x_nav_make_latlon(50.0 + k * 0.01, -1.0, nm, &f);
        int e = a380x_fpln_next_waypoint(fp, A380X_PLAN_ACTIVE, 0, k == 0 ? 0 : k, &f);
        if (e == 0) ok++; else { err = e; break; }
    }
    CHECK(err == A380X_FPLN_E_FULL && fp->plan[A380X_PLAN_TEMPORARY].pri.count <= A380X_FPLN_MAX_ELEMENTS);
    CHECK(ok > 190);
    a380x_fpln_tmpy_erase(fp);
    /* reset */
    a380x_fpln_reset(fp);
    CHECK(!a380x_fpln_plan(fp, A380X_PLAN_ACTIVE));
    a380x_fpln_destroy(fp);
}

/* ============================================================================================ */
/* Real data timing                                                                             */
/* ============================================================================================ */

static double now_ms(void) { return (double)clock() * 1000.0 / CLOCKS_PER_SEC; }

static int run_xplane(const char *root)
{
    A380XNavdataConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.xplane_root = root;
    A380XNavdata *nav = a380x_nav_create(&cfg, NULL);
    double t0 = now_ms();
    int rc = a380x_nav_load_global(nav);
    double t1 = now_ms();
    A380XNavStats st;
    a380x_nav_stats(nav, &st);
    printf("global load rc=%d cpu=%.0f ms: %d waypoints, %d navaids, %d airports(meta), %d airway segments / %d names, "
           "%d malformed lines, custom fix/nav/awy/meta=%d/%d/%d/%d, heap %.1f MB\n",
           rc, t1 - t0, st.waypoints, st.navaids, st.airports_meta, st.airway_segments, st.airway_names, st.malformed_lines,
           st.custom_fix, st.custom_nav, st.custom_awy, st.custom_meta, st.bytes / 1048576.0);
    const A380XNavCycle *c = a380x_nav_cycle(nav);
    printf("cycle %s (%s) %04d-%02d-%02d .. %04d-%02d-%02d\n", c->airac, c->source_file, c->eff_year, c->eff_month, c->eff_day,
           c->exp_year, c->exp_month, c->exp_day);
    const char *ids[] = { "BIG", "LAM", "KONAN", "D125O", "EGLL", "KJFK", "SFO", "TOPPS", "ABC", "WSSS" };
    A380XNavFix f[16];
    t0 = now_ms();
    int total = 0;
    for (int rep = 0; rep < 10000; rep++)
        for (int k = 0; k < 10; k++) total += a380x_nav_find(nav, ids[k], A380X_NAVMASK_ALL, f, 16);
    t1 = now_ms();
    printf("100000 ident lookups: %.1f ms (%.3f us each, %d matches per 10)\n", t1 - t0, (t1 - t0) * 0.01, total / 10000);
    const char *apts[] = { "EGLL", "KJFK", "LFPG", "EDDF" };
    for (int k = 0; k < 4; k++) {
        t0 = now_ms();
        const A380XNavAirport *a = a380x_nav_airport(nav, apts[k]);
        t1 = now_ms();
        if (!a) { printf("%s: no CIFP\n", apts[k]); continue; }
        printf("CIFP %s: %.1f ms, %d runways, %d procedures, %d transitions, %d legs, %d unresolved, %d malformed\n", apts[k],
               t1 - t0, a->runway_count, a->proc_count, a->trans_count, a->leg_count, a->unresolved_fixes, a->malformed_lines);
    }
    A380XFlightPlans *fp = a380x_fpln_create(nav);
    t0 = now_ms();
    a380x_fpln_new_city_pair(fp, A380X_PLAN_ACTIVE, "EGLL", "LFPG", "EDDF");
    a380x_fpln_set_origin_runway(fp, A380X_PLAN_ACTIVE, 0, "27R");
    a380x_fpln_set_sid(fp, A380X_PLAN_ACTIVE, 0, "DET2F");
    const A380XNavAirport *lfpg = a380x_nav_airport(nav, "LFPG");
    const A380XNavProcedure *ap = a380x_nav_procedure_at(lfpg, A380X_PROC_APPROACH, 0);
    if (ap) a380x_fpln_set_approach(fp, A380X_PLAN_ACTIVE, 0, ap->ident);
    a380x_fpln_tmpy_insert(fp, NULL);
    t1 = now_ms();
    const A380XFplnPlan *p = a380x_fpln_plan(fp, A380X_PLAN_ACTIVE);
    printf("flight plan EGLL 27R DET2F -> LFPG %s: %d elements, %d lines, built in %.1f ms; sizeof(A380XFlightPlans)=%.2f MB\n",
           ap ? ap->ident : "-", p ? p->pri.count : 0, a380x_fpln_line_count(fp, A380X_PLAN_ACTIVE, NULL), t1 - t0,
           sizeof(A380XFlightPlans) / 1048576.0);
    a380x_fpln_destroy(fp);
    a380x_nav_stats(nav, &st);
    printf("heap after CIFP loads: %.1f MB (%d airports cached)\n", st.bytes / 1048576.0, st.cifp_cached);
    a380x_nav_destroy(nav);
    return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "--xplane")) return run_xplane(argv[2]);
    if (argc >= 2) snprintf(g_dir, sizeof g_dir, "%s", argv[1]);
    test_airac();
    test_global();
    test_airways();
    test_cifp();
    A380XNavdata *nav = open_fixture("custom", "default", 0);
    test_fpln_departure_tmpy(nav);
    test_fpln_arrival(nav);
    test_fpln_dirto_sequencing(nav);
    test_fpln_xxts(nav);
    test_fpln_misc(nav);
    a380x_nav_destroy(nav);
    if (g_fail) { printf("test_navdata: %d of %d checks FAILED\n", g_fail, g_checks); return 1; }
    printf("test_navdata: all %d checks passed\n", g_checks);
    return 0;
}
