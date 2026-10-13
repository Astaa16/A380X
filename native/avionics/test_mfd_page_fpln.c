/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * F-PLN page family test (WP-G): renders previews of the F-PLN pages through the software
 * rasteriser of test_mfd_render.c (included with its main() renamed) and runs headless interaction
 * scenarios against the real flight-plan store (a380x_fms_fpln.c), the FMS core (a380x_fms.c, TMPY
 * data stores through the plan hooks) and the navigation database (installed X-Plane 12 data when
 * present, else the test fixtures).
 *
 * Usage: test_mfd_page_fpln <ppm-output-dir> [x-plane-root]
 * Link: a380x_mfd.c a380x_mfd_ui.c a380x_mfd_draw.c a380x_mfd_page_data.c a380x_mfd_formats.c
 *       a380x_fms_messages.c a380x_fms.c a380x_fms_speeds.c a380x_fms_fpln.c a380x_navdata.c
 *       a380x_mfd_page_fpln.c a380x_mfd_page_fpln_rev.c -lm
 */
#define main test_mfd_render_main
#include "test_mfd_render.c"
#undef main

#include "a380x_mfd_page_fpln.h"
#include "a380x_mfd_page_fpln_rev.h"
#include "a380x_fms.h"

#include <sys/stat.h>

#define R(y) (138.0f + (float)(y))

#define CHECK(c)                                                                     \
    do {                                                                             \
        if (!(c)) {                                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);    \
            g_fail++;                                                                \
        }                                                                            \
    } while (0)

static int g_fail;
static const char *g_out = ".";
static A380XNavdata *g_nav;
static A380XFlightPlans *g_fp;
static A380XFms g_fms;
static A380XFplnAircraft g_ac;
static int g_real;

/* ---- FMS plan hooks (what the integrator's adapter does) ----------------------------------- */

static int hk_exists(struct A380XFlightPlans *p, int plan) { return a380x_fpln_plan(p, plan) != NULL; }
static unsigned hk_version(struct A380XFlightPlans *p, int plan)
{
    const A380XFplnPlan *pl = a380x_fpln_plan(p, plan);
    return pl ? pl->version : 0u;
}
static int hk_city(struct A380XFlightPlans *p, int plan, char o[8], char d[8], char a[8])
{
    const A380XFplnPlan *pl = a380x_fpln_plan(p, plan);
    if (!pl) return 0;
    snprintf(o, 8, "%s", pl->pri.origin_icao);
    snprintf(d, 8, "%s", pl->pri.dest_icao);
    snprintf(a, 8, "%s", pl->alt.dest_icao);
    return 1;
}
static int hk_insert(struct A380XFlightPlans *p) { return a380x_fpln_tmpy_insert(p, &g_ac) == A380X_FPLN_OK; }
static int hk_erase(struct A380XFlightPlans *p) { return a380x_fpln_tmpy_erase(p) == A380X_FPLN_OK; }

/* ---- harness ------------------------------------------------------------------------------- */

static void sync(void)
{
    g_in.tmpy_exists = (unsigned char)a380x_fpln_has_tmpy(g_fp);
    frames(2);
}

static void settle(void)
{
    g_t += 0.6;
    sync();
}

static void fp_reset(void)
{
    A380XMfdSystemsHost host;
    A380XFmsPlanHooks hk;
    A380XMfdFplnEnv env;
    memset(&hk, 0, sizeof(hk));
    hk.plans = g_fp;
    hk.plan_exists = hk_exists;
    hk.plan_version = hk_version;
    hk.city_pair = hk_city;
    hk.tmpy_insert = hk_insert;
    hk.tmpy_erase = hk_erase;
    a380x_fms_init(&g_fms, NULL, NULL, &hk);
    memset(&g_in, 0, sizeof(g_in));
    g_in.du_on[0] = g_in.du_on[1] = 1;
    g_in.fms_knob = 1;
    g_in.fmc_a_healthy = g_in.fmc_b_healthy = 1;
    g_in.metric_units = 1;
    memset(&host, 0, sizeof(host));
    a380x_mfd_init(&g_mfd, &g_fms, &g_fms.msgs, &host);
    memset(&env, 0, sizeof(env));
    env.plans = g_fp;
    env.aircraft_valid = 1;
    env.aircraft = g_ac;
    a380x_mfd_fpln_set_env(&env);
    a380x_mfd_fpln_reset_revised(MFD_CAPT);
    sync();
}

static void go(const char *uri)
{
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, uri);
    sync();
}

static void shot(const char *name)
{
    sync();
    leave();
    snap(g_out, name);
}

static MfdFplnState *fpln_state(void) { return (MfdFplnState *)(void *)g_mfd.sides[MFD_CAPT].page_state; }
static const char *uri_now(void) { return a380x_mfd_active_uri(&g_mfd, MFD_CAPT); }

static float slot_cy(int s) { return s == 0 ? 225.0f : 245.0f + 72.0f * (float)(s - 1) + 52.0f; }

/* Click the ident of a primary leg on the F-PLN page (scrolls the page state when needed). */
static void click_leg(int leg)
{
    int lplan = a380x_fpln_loaded_index(g_fp, 0), line = a380x_fpln_line_of_leg(g_fp, lplan, NULL, leg, 0);
    int slots = lplan == A380X_PLAN_TEMPORARY ? 8 : 9;
    MfdFplnState *st = fpln_state();
    CHECK(line >= 0);
    if (line < st->from || line >= st->from + slots) {
        st->from = line;
        sync();
    }
    click(12.0f, slot_cy(line - st->from));
}

static void rev_item(int k) { g_t += 0.15; frame(); click(250.0f, 183.0f + 2.0f + 42.0f * (float)k + 21.0f); }

/* Open a menu button (centre bx, by; bottom edge list_y) and choose item idx of n. */
static void menu_pick(float bx, float by, float list_y, int n, int idx)
{
    float list_h = 4.0f + 36.0f * (float)n, view, max_scroll, scroll = 0.0f;
    if (list_h > 275.0f) list_h = 275.0f;
    view = list_h - 4.0f;
    max_scroll = 36.0f * (float)n - view;
    if (max_scroll < 0.0f) max_scroll = 0.0f;
    click(bx, by);
    if (36.0f * (float)idx + 36.0f > view) {
        int notches = idx;
        move(bx, list_y + 20.0f);
        mfd_pointer_wheel(&g_mfd, MFD_CAPT, -notches);
        frame();
        scroll = 36.0f * (float)notches;
        if (scroll > max_scroll) scroll = max_scroll;
    }
    click(bx, list_y + 2.0f + 36.0f * (float)idx + 18.0f - scroll);
}

static void type_ent(float x, float y, const char *text)
{
    click(x, y);
    type(text);
    key(MFD_KEY_ENT, 0);
    settle();
}

static int find_leg(const A380XFplnRoute *r, const char *ident, int from)
{
    int i;
    for (i = from < 0 ? 0 : from; i < r->count; ++i)
        if (!a380x_fpln_is_disco(&r->el[i]) && !strcmp(r->el[i].ident, ident)) return i;
    return -1;
}

static const A380XFplnPlan *shown(void) { return a380x_fpln_plan(g_fp, a380x_fpln_loaded_index(g_fp, 0)); }

static int msg_has(MfdMsgId id) { return mfd_msg_has_id(&g_fms.msgs, id); }

/* ---- pure checks ----------------------------------------------------------------------------- */

static void test_line_builder(void)
{
    A380XFplnLineIter it;
    A380XFplnLine ln;
    int n = 0, kinds[8];
    CHECK(a380x_fpln_new_city_pair(g_fp, A380X_PLAN_ACTIVE, "EGLL", g_real ? "LFPG" : "EGLL", NULL) == 0);
    a380x_fpln_lines_begin(&it, g_fp, A380X_PLAN_ACTIVE, NULL);
    while (a380x_fpln_lines_next(&it, &ln) && n < 8) kinds[n++] = ln.kind == A380X_FLINE_SPECIAL ? (int)ln.special : -1;
    /* EGLL, DISCONTINUITY, LFPG, END OF F-PLN, NO ALTN F-PLN */
    CHECK(n == 5);
    CHECK(kinds[0] == -1 && kinds[1] == A380X_FSPEC_DISCONTINUITY && kinds[2] == -1);
    CHECK(kinds[3] == A380X_FSPEC_END_OF_FPLN && kinds[4] == A380X_FSPEC_NO_ALTN_FPLN);
}

static void test_place_formats(void)
{
    A380XNavFix fx[A380X_MFD_DUP_MAX];
    MfdPageContext ctx;
    char ll[48];
    memset(&ctx, 0, sizeof(ctx));
    ctx.fms = &g_fms;
    ctx.msgs = &g_fms.msgs;
    ctx.ui = &g_mfd.sides[MFD_CAPT].ui;
    a380x_mfd_fpln_format_latlon(51.4775, -0.461389, ll, sizeof(ll));
    CHECK(!strcmp(ll, "51\xC2\xB0" "28.7N/000\xC2\xB0" "27.7W"));
    CHECK(a380x_mfd_fpln_resolve_place(&ctx, "5100.0N/00130.0E", fx, 4) == 1 && fabs(fx[0].lat - 51.0) < 1e-6 &&
          fabs(fx[0].lon - 1.5) < 1e-6);
    CHECK(a380x_mfd_fpln_resolve_place(&ctx, "N51.5/W001.5", fx, 4) == 1 && fabs(fx[0].lat - 51.5) < 1e-6 &&
          fabs(fx[0].lon + 1.5) < 1e-6);
    CHECK(a380x_mfd_fpln_resolve_place(&ctx, "EGLL27R", fx, 4) == 1 && fx[0].kind == A380X_NAV_RUNWAY);
    CHECK(a380x_mfd_fpln_resolve_place(&ctx, "ZZZZZ", fx, 4) == 0 && msg_has(MFD_MSG_NOT_IN_DATABASE));
    CHECK(a380x_mfd_fpln_resolve_place(&ctx, "A$B", fx, 4) == 0 && msg_has(MFD_MSG_FORMAT_ERROR));
    CHECK(a380x_mfd_fpln_resolve_place(&ctx, "LAM/10", fx, 4) == 0 && msg_has(MFD_MSG_NOT_YET_IMPLEMENTED));
    if (g_real) {
        int n = a380x_mfd_fpln_resolve_place(&ctx, "LAM", fx, A380X_MFD_DUP_MAX);
        CHECK(n > 1 && !strcmp(fx[0].region, "EG")); /* nearest first: Lambourne */
        CHECK(a380x_mfd_fpln_resolve_place(&ctx, "LAM/090/10", fx, 4) == 1 && !strncmp(fx[0].ident, "PBD", 3));
        CHECK(a380x_mfd_fpln_resolve_place(&ctx, "LAM-360/BPK-090", fx, 4) == 1 && !strncmp(fx[0].ident, "PBX", 3));
    }
    mfd_msg_init(&g_fms.msgs);
}

/* ---- scenarios ------------------------------------------------------------------------------- */

static int sid_index(const char *rwy, const char *sid, int *n_out)
{
    const A380XNavAirport *apt = a380x_nav_airport(g_nav, "EGLL");
    int i, n = 1, idx = -1;
    for (i = 0; apt && i < a380x_nav_procedure_count(apt, A380X_PROC_SID); ++i) {
        const A380XNavProcedure *p = a380x_nav_procedure_at(apt, A380X_PROC_SID, i);
        if (!p || !a380x_nav_sid_serves_runway(apt, p, rwy)) continue;
        if (!strcmp(p->ident, sid)) idx = n;
        n++;
    }
    *n_out = n;
    return idx;
}

static int rwy_index(const char *icao, const char *rwy, int *n_out)
{
    const A380XNavAirport *apt = a380x_nav_airport(g_nav, icao);
    int i;
    *n_out = apt ? apt->runway_count : 0;
    for (i = 0; apt && i < apt->runway_count; ++i)
        if (!strcmp(apt->runways[i].ident, rwy)) return i;
    return -1;
}

static int appr_listed(const A380XNavProcedure *p)
{
    return p->appr_type != A380X_APPR_TACAN && p->appr_type != A380X_APPR_MLS && p->appr_type != A380X_APPR_MLS_A &&
           p->appr_type != A380X_APPR_MLS_BC && p->appr_type != A380X_APPR_LOC_BACKCOURSE && p->runway[0];
}

static int appr_index(const char *icao, const char *ident, int *n_out)
{
    const A380XNavAirport *apt = a380x_nav_airport(g_nav, icao);
    const A380XNavProcedure *list[128];
    int i, j, n = 0;
    for (i = 0; apt && i < a380x_nav_procedure_count(apt, A380X_PROC_APPROACH) && n < 63; ++i) {
        const A380XNavProcedure *p = a380x_nav_procedure_at(apt, A380X_PROC_APPROACH, i);
        if (!p || !appr_listed(p)) continue;
        for (j = n; j > 0; --j) {
            int c = strcmp(p->runway, list[j - 1]->runway);
            if (c > 0 || (c == 0 && a380x_nav_approach_type_order(p->appr_type) >=
                                        a380x_nav_approach_type_order(list[j - 1]->appr_type)))
                break;
            list[j] = list[j - 1];
        }
        list[j] = p;
        n++;
    }
    *n_out = n + 1;
    for (i = 0; i < n; ++i)
        if (!strcmp(list[i]->ident, ident)) return i + 1;
    return -1;
}

static int star_index(const char *icao, const char *rwy, const char *star, int *n_out)
{
    const A380XNavAirport *apt = a380x_nav_airport(g_nav, icao);
    int i, n = 1, idx = -1;
    for (i = 0; apt && i < a380x_nav_procedure_count(apt, A380X_PROC_STAR) && n < 64; ++i) {
        const A380XNavProcedure *p = a380x_nav_procedure_at(apt, A380X_PROC_STAR, i);
        if (!p || !a380x_nav_star_serves_runway(apt, p, rwy)) continue;
        if (!strcmp(p->ident, star)) idx = n;
        n++;
    }
    *n_out = n;
    return idx;
}

static void scenario_route(void)
{
    const A380XFplnPlan *p;
    int n, k, det, dvr, line0;

    fp_reset();
    CHECK(a380x_fpln_new_city_pair(g_fp, A380X_PLAN_ACTIVE, "EGLL", "LFPG", NULL) == 0);
    go("fms/active/f-pln");
    CHECK(!strcmp(uri_now(), "fms/active/f-pln"));
    shot("fpln_empty");

    /* revision menu on the origin -> DEPARTURE */
    click_leg(0);
    CHECK(fpln_state()->rev_open);
    rev_item(3);
    sync();
    CHECK(!strcmp(uri_now(), "fms/active/f-pln-departure"));
    /* RWY 27R, SID DET2F (menus) -> TMPY */
    k = rwy_index("EGLL", "RW27R", &n);
    CHECK(k >= 0);
    menu_pick(175.0f, R(182) + 20.0f, R(182) + 40.0f, n, k);
    sync();
    CHECK(a380x_fpln_has_tmpy(g_fp));
    CHECK(!strcmp(a380x_fpln_plan(g_fp, A380X_PLAN_TEMPORARY)->pri.origin_rwy, "RW27R"));
    CHECK(g_fms.plans[A380X_FMS_PLAN_TMPY].exists); /* FMS data store follows (a380x_fms_tmpy_begin) */
    k = sid_index("RW27R", "DET2F", &n);
    CHECK(k > 0);
    menu_pick(470.0f, R(182) + 20.0f, R(182) + 40.0f, n, k);
    sync();
    CHECK(!strcmp(a380x_fpln_plan(g_fp, A380X_PLAN_TEMPORARY)->pri.sid, "DET2F"));
    shot("fpln_dep");
    /* TMPY F-PLN -> F-PLN (yellow, 8 rows, ERASE/INSERT) */
    click(683.0f, 928.0f);
    sync();
    CHECK(!strcmp(uri_now(), "fms/active/f-pln"));
    shot("fpln_tmpy");
    click(705.0f, 792.0f); /* INSERT TMPY */
    sync();
    CHECK(!a380x_fpln_has_tmpy(g_fp));
    CHECK(!g_fms.plans[A380X_FMS_PLAN_TMPY].exists);
    p = a380x_fpln_plan(g_fp, A380X_PLAN_ACTIVE);
    CHECK(!strcmp(p->pri.sid, "DET2F"));

    /* AIRWAYS from DET: L6 DVR, L9 KONAN */
    det = find_leg(&p->pri, "DET", 0);
    CHECK(det > 0);
    click_leg(det);
    rev_item(7);
    sync();
    CHECK(!strcmp(uri_now(), "fms/active/f-pln-airways"));
    type_ent(110.0f, R(84) + 18.0f, "L6");
    type_ent(690.0f, R(84) + 18.0f, "DVR");
    type_ent(110.0f, R(136) + 18.0f, "L9");
    type_ent(690.0f, R(136) + 18.0f, "KONAN");
    p = a380x_fpln_plan(g_fp, A380X_PLAN_TEMPORARY);
    CHECK(p && p->awy.active && p->awy.line_count == 3 && !strcmp(p->awy.line[1].to.ident, "KONAN"));
    shot("fpln_airways");
    click(683.0f, 928.0f); /* TMPY F-PLN: finalise */
    sync();
    p = shown();
    dvr = find_leg(&p->pri, "DVR", det);
    CHECK(dvr == det + 1 && find_leg(&p->pri, "KONAN", det) == det + 2);
    CHECK(!strcmp(p->pri.el[dvr].annotation, "L6"));
    click(705.0f, 792.0f);
    sync();
    CHECK(!a380x_fpln_has_tmpy(g_fp));

    /* ARRIVAL via the destination ident: APPR I27R, STAR */
    click(75.0f, 861.0f);
    sync();
    CHECK(!strcmp(uri_now(), "fms/active/f-pln-arrival"));
    k = appr_index("LFPG", "I27R", &n);
    CHECK(k > 0);
    menu_pick(281.0f, R(182) + 20.0f, R(182) + 40.0f, n, k);
    sync();
    {
        const A380XNavAirport *apt = a380x_nav_airport(g_nav, "LFPG");
        const A380XNavProcedure *star = NULL;
        int i;
        for (i = 0; apt && i < a380x_nav_procedure_count(apt, A380X_PROC_STAR); ++i) {
            const A380XNavProcedure *s = a380x_nav_procedure_at(apt, A380X_PROC_STAR, i);
            if (s && !strncmp(s->ident, "BIBA", 4) && a380x_nav_star_serves_runway(apt, s, "RW27R")) {
                star = s;
                break;
            }
        }
        CHECK(star != NULL);
        if (star) {
            char ident[8];
            snprintf(ident, sizeof(ident), "%s", star->ident);
            k = star_index("LFPG", "RW27R", ident, &n);
            menu_pick(569.0f, R(182) + 20.0f, R(182) + 40.0f, n, k);
            sync();
            CHECK(!strcmp(a380x_fpln_plan(g_fp, A380X_PLAN_TEMPORARY)->pri.star, ident));
        }
    }
    CHECK(!strcmp(a380x_fpln_plan(g_fp, A380X_PLAN_TEMPORARY)->pri.approach, "I27R"));
    shot("fpln_arr");
    click(683.0f, 928.0f);
    sync();
    click(705.0f, 792.0f);
    sync();
    CHECK(!a380x_fpln_has_tmpy(g_fp));
    p = a380x_fpln_plan(g_fp, A380X_PLAN_ACTIVE);
    CHECK(p->pri.first_missed_appr_index < p->pri.count);
    shot("fpln_full");
    /* scroll: wheel over the list moves the window by one line per notch */
    line0 = fpln_state()->from;
    move(400.0f, 500.0f);
    mfd_pointer_wheel(&g_mfd, MFD_CAPT, -3);
    sync();
    CHECK(fpln_state()->from == line0 + 3);
    key(MFD_KEY_UP, 0);
    sync();
    CHECK(fpln_state()->from == line0 + 2);
    /* DEST button: destination leg in the last slot */
    click(721.0f, 861.0f);
    sync();
    {
        int dl = a380x_fpln_line_of_leg(g_fp, 0, NULL, p->pri.destination_leg_index, 0);
        CHECK(dl - fpln_state()->from == 8);
    }
    shot("fpln_dest");
    click(650.0f, 861.0f); /* double-up */
    sync();

    /* revision menu open on DVR */
    fpln_state()->from = 0;
    sync();
    click_leg(dvr);
    move(250.0f, 183.0f + 2.0f + 42.0f * 1.0f + 21.0f);
    snap(g_out, "fpln_rev_menu");
    /* INSERT NEXT WPT: lat/lon after DVR -> LL01 + disco in TMPY */
    rev_item(1);
    sync();
    CHECK(fpln_state()->window == 1);
    shot("fpln_next_wpt");
    type_ent(300.0f, 309.0f, "5100.0N/00130.0E");
    sync();
    p = shown();
    CHECK(a380x_fpln_has_tmpy(g_fp) && fpln_state()->window == 0);
    CHECK(p->pri.el[dvr + 1].ident[0] == 'L' && p->pri.el[dvr + 1].ident[1] == 'L');
    CHECK(a380x_fpln_is_disco(&p->pri.el[dvr + 2]));
    /* ERASE TMPY */
    click(55.0f, 792.0f);
    sync();
    CHECK(!a380x_fpln_has_tmpy(g_fp));

    /* DUPLICATE NAMES through INSERT NEXT WPT "BIG" after DET: choose Biggin (row 0, nearest) */
    click_leg(det);
    rev_item(1);
    sync();
    type_ent(300.0f, 309.0f, "BIG");
    sync();
    CHECK(a380x_mfd_fpln_dup_is_open(MFD_CAPT));
    shot("fpln_dup");
    click(384.0f, 276.0f + 2.0f + 25.0f);
    sync();
    CHECK(!a380x_mfd_fpln_dup_is_open(MFD_CAPT));
    p = shown();
    CHECK(a380x_fpln_has_tmpy(g_fp) && !strcmp(p->pri.el[det + 1].ident, "BIG") &&
          !strcmp(p->pri.el[det + 1].fix.region, "EG") && a380x_fpln_is_disco(&p->pri.el[det + 2]));
    a380x_mfd_fpln_tmpy_erase(&(MfdPageContext){.fms = &g_fms, .ui = &g_mfd.sides[0].ui});
    sync();

    /* NEW DEST window */
    click_leg(dvr);
    rev_item(10);
    sync();
    CHECK(fpln_state()->window == 2);
    shot("fpln_new_dest");
    type_ent(450.0f, 508.0f, "EHAM");
    p = shown();
    CHECK(a380x_fpln_has_tmpy(g_fp) && !strcmp(p->pri.dest_icao, "EHAM"));
    click(55.0f, 792.0f);
    sync();

    /* DIR TO: F-PLN DIR TO button, pick KONAN in the list */
    click(703.0f, 925.0f);
    sync();
    CHECK(!strcmp(uri_now(), "fms/active/f-pln-direct-to"));
    {
        const A380XFplnPlan *a = a380x_fpln_plan(g_fp, A380X_PLAN_ACTIVE);
        int i, idx = -1, cnt = 0, kon = find_leg(&a->pri, "KONAN", 0);
        for (i = a->active_leg_index; i < a->pri.first_missed_appr_index; ++i) {
            if (a380x_fpln_is_disco(&a->pri.el[i]) || !a380x_leg_is_xf(a->pri.el[i].type)) continue;
            if (i == kon) idx = cnt;
            cnt++;
        }
        CHECK(idx >= 0);
        menu_pick(215.0f, R(10) + 22.0f, R(10) + 44.0f, cnt, idx);
        settle();
    }
    CHECK(a380x_fpln_has_tmpy(g_fp));
    p = shown();
    CHECK(p && !strcmp(p->pri.el[p->active_leg_index].ident, "KONAN") &&
          (p->pri.el[p->active_leg_index - 1].flags & A380X_LEGF_DIRTO_TP));
    shot("fpln_dirto");
    click(700.0f, 917.0f); /* INSERT DIR TO* */
    sync();
    CHECK(!a380x_fpln_has_tmpy(g_fp) && !strcmp(uri_now(), "fms/active/f-pln"));
    p = a380x_fpln_plan(g_fp, A380X_PLAN_ACTIVE);
    CHECK(!strcmp(p->pri.el[p->active_leg_index].ident, "KONAN"));
    shot("fpln_after_dirto");

    /* FROM P.POS DIR TO from the revision menu (on the leg after KONAN) */
    {
        int nxt = p->active_leg_index + 1;
        while (nxt < p->pri.count && !a380x_fpln_is_db_fix(&p->pri.el[nxt])) nxt++;
        click_leg(nxt);
        rev_item(0);
        sync();
        CHECK(!strcmp(uri_now(), "fms/active/f-pln-direct-to") && a380x_fpln_has_tmpy(g_fp));
        click(57.0f, 917.0f); /* ERASE DIR TO* */
        sync();
        CHECK(!a380x_fpln_has_tmpy(g_fp));
    }

    /* HOLD at DVR -> HOLD page */
    p = a380x_fpln_plan(g_fp, A380X_PLAN_ACTIVE);
    go("fms/active/f-pln");
    {
        int kon = find_leg(&p->pri, "KONAN", 0), nxt = kon + 1;
        while (nxt < p->pri.count && !a380x_fpln_is_db_fix(&p->pri.el[nxt])) nxt++;
        click_leg(nxt);
        rev_item(6);
        sync();
        CHECK(!strcmp(uri_now(), "fms/active/f-pln-hold"));
        p = shown();
        CHECK(p && p->pri.el[nxt + 1].type == A380X_LEG_HM);
        type_ent(120.0f, R(124) + 18.0f, "250");
        p = shown();
        CHECK(p->pri.el[nxt + 1].hold_modified.valid && fabsf(p->pri.el[nxt + 1].hold_modified.inbound_crs_mag - 250.0f) < 0.5f);
        shot("fpln_hold");
        click(683.0f, 928.0f);
        sync();
        shot("fpln_hold_line");
        click(55.0f, 792.0f);
        sync();
    }

    /* VERT REV ALT on a STAR waypoint: AT OR ABOVE 5000 */
    go("fms/active/f-pln");
    p = a380x_fpln_plan(g_fp, A380X_PLAN_ACTIVE);
    {
        int i, w = -1;
        for (i = p->active_leg_index; i < p->pri.count; ++i)
            if (a380x_fpln_constraint_revision_allowed(&p->pri.el[i]) && p->pri.el[i].segment >= A380X_SEG_STAR_TRANS) {
                w = i;
                break;
            }
        CHECK(w >= 0);
        if (w >= 0) {
            int line = a380x_fpln_line_of_leg(g_fp, 0, NULL, w, 0), slot;
            fpln_state()->from = line > 0 ? line - 1 : 0;
            sync();
            slot = line - fpln_state()->from;
            click(470.0f, slot_cy(slot));
            sync();
            CHECK(!strcmp(uri_now(), "fms/active/f-pln-vert-rev/alt"));
            click(60.0f, R(63) + 159.0f + 39.0f + 17.0f); /* AT OR ABOVE */
            type_ent(380.0f, R(63) + 218.0f, "5000");
            p = shown();
            CHECK(p && p->pri.el[w].alt_pilot.desc == '+' && fabsf(p->pri.el[w].alt_pilot.alt1_ft - 5000.0f) < 1.0f);
            shot("fpln_vert_rev_alt");
            click(683.0f, 928.0f);
            sync();
            click(55.0f, 792.0f);
            sync();
        }
    }
    go("fms/active/f-pln-vert-rev/step-alts");
    CHECK(fpln_state() != NULL);
    {
        MfdFmsError err;
        const A380XFplnPlan *a = a380x_fpln_plan(g_fp, A380X_PLAN_ACTIVE);
        int kon = find_leg(&a->pri, "KONAN", 0);
        a380x_fms_enter_text(&g_fms, A380X_FMS_PLAN_ACTIVE, A380X_FMS_T_FROM, "EGLL", &err);
        a380x_fms_enter_text(&g_fms, A380X_FMS_PLAN_ACTIVE, A380X_FMS_T_TO, "LFPG", &err);
        CHECK(a380x_fms_enter(&g_fms, A380X_FMS_PLAN_ACTIVE, A380X_FMS_F_CRZ_FL, 350.0, &err));
        if (err.type) fprintf(stderr, "CRZ FL err %d %s\n", (int)err.type, err.details);
        sync();
        /* step at KONAN through the empty row: waypoint dropdown (first entry), then FL370 */
        menu_pick(154.0f, R(63) + 96.0f + 8.0f + 22.0f, R(63) + 96.0f + 8.0f + 44.0f, 8, 0);
        type_ent(366.0f, R(63) + 96.0f + 11.5f + 18.0f, "370");
        a = shown();
        CHECK(kon >= 0 && a->pri.el[a->active_leg_index].cruise_step.valid &&
              fabsf(a->pri.el[a->active_leg_index].cruise_step.to_alt_ft - 37000.0f) < 1.0f);
        /* a second step 500 ft away is refused: STEP NOT ALLOWED AT */
        menu_pick(154.0f, R(63) + 156.0f + 8.0f + 22.0f, R(63) + 156.0f + 8.0f + 44.0f, 8, 2);
        type_ent(366.0f, R(63) + 156.0f + 11.5f + 18.0f, "375");
        CHECK(((MfdFplnVrevState *)(void *)g_mfd.sides[MFD_CAPT].page_state)->step_reject_leg >= 0);
    }
    shot("fpln_vert_rev_step");
    click(683.0f, 928.0f); /* TMPY F-PLN */
    sync();
    CHECK(!strcmp(uri_now(), "fms/active/f-pln"));
    click(55.0f, 792.0f); /* ERASE TMPY */
    sync();
    CHECK(!a380x_fpln_has_tmpy(g_fp));
    go("fms/active/f-pln-fix-info");
    type_ent(200.0f, R(78) + 18.0f, "LAM");
    CHECK(a380x_mfd_fpln_dup_is_open(MFD_CAPT)); /* LAM: Lambourne + Lamerd */
    click(384.0f, 276.0f + 2.0f + 25.0f);
    sync();
    CHECK(a380x_fpln_plan(g_fp, 0)->fix_info[0].valid && !strcmp(a380x_fpln_plan(g_fp, 0)->fix_info[0].fix.ident, "LAM"));
    type_ent(80.0f, R(167) + 88.0f + 68.0f, "090");
    CHECK(a380x_fpln_plan(g_fp, 0)->fix_info[0].radial_count == 1);
    shot("fpln_fix_info");

    /* EFOB / T.WIND mode */
    go("fms/active/f-pln");
    click(445.0f, 164.0f);
    click(445.0f, 204.0f);
    sync();
    CHECK(fpln_state()->efob_mode == 1);
    shot("fpln_efob");
}

static void scenario_sec(void)
{
    /* SEC1 copy of ACTIVE: white rows, RETURN, no DIR TO; edits in place (no TMPY) */
    fp_reset();
    CHECK(a380x_fpln_sec_copy_from_active(g_fp, 1) == 0);
    go("fms/sec1/f-pln");
    CHECK(!strcmp(uri_now(), "fms/sec1/f-pln"));
    click_leg(0);
    CHECK(fpln_state()->rev_open);
    rev_item(2); /* DELETE * on the FROM line is disabled -> menu closes, nothing happens */
    sync();
    CHECK(!a380x_fpln_has_tmpy(g_fp) && !fpln_state()->rev_open);
    click_leg(a380x_fpln_plan(g_fp, A380X_PLAN_SEC1)->active_leg_index + 2);
    rev_item(8); /* OVERFLY * on a SEC plan: edited in place, no TMPY */
    sync();
    CHECK(!a380x_fpln_has_tmpy(g_fp));
    CHECK(a380x_fpln_plan(g_fp, A380X_PLAN_SEC1)->pri.el[a380x_fpln_plan(g_fp, A380X_PLAN_SEC1)->active_leg_index + 2].flags &
          A380X_LEGF_OVERFLY);
    click_leg(a380x_fpln_plan(g_fp, A380X_PLAN_SEC1)->active_leg_index + 2);
    shot("fpln_sec");
}

int main(int argc, char **argv)
{
    const char *root = argc > 2 ? argv[2] : "/Users/sava/Library/Application Support/Steam/steamapps/common/X-Plane 12";
    struct stat sb;
    A380XNavdataConfig cfg;
    g_out = argc > 1 ? argv[1] : ".";
    if (!mfd_font_decode(g_atlas)) {
        fprintf(stderr, "font decode failed\n");
        return 1;
    }
    memset(&cfg, 0, sizeof(cfg));
    {
        char p[1024];
        snprintf(p, sizeof(p), "%s/Resources/default data/earth_fix.dat", root);
        g_real = stat(p, &sb) == 0;
    }
    if (g_real) {
        cfg.xplane_root = root;
    } else {
        cfg.custom_data_dir = "test_data/navdata/custom";
        cfg.default_data_dir = "test_data/navdata/default";
    }
    g_nav = a380x_nav_create(&cfg, NULL);
    g_fp = a380x_fpln_create(g_nav);
    if (!g_nav || !g_fp) {
        fprintf(stderr, "navdata / flight plan store unavailable\n");
        return 1;
    }
    memset(&g_ac, 0, sizeof(g_ac));
    g_ac.valid = 1;
    g_ac.lat = 51.30;
    g_ac.lon = 0.20; /* east of London, airborne */
    g_ac.alt_ft = 12000.0f;
    g_ac.true_track_deg = 110.0f;
    g_ac.gs_kt = 320.0f;
    a380x_mfd_page_fpln_register();
    a380x_mfd_page_fpln_rev_register();

    fp_reset();
    test_line_builder();
    test_place_formats();
    if (g_real) {
        scenario_route();
        scenario_sec();
    } else {
        printf("installed X-Plane data not found: route scenarios skipped\n");
        fp_reset();
        go("fms/active/f-pln");
        shot("fpln_empty");
    }
    a380x_fpln_destroy(g_fp);
    a380x_nav_destroy(g_nav);
    if (g_fail) {
        printf("test_mfd_page_fpln: %d FAILED\n", g_fail);
        return 1;
    }
    printf("test_mfd_page_fpln: OK\n");
    return 0;
}
