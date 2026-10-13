/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD F-PLN revision pages: DEPARTURE, ARRIVAL, AIRWAYS, DIRECT-TO, HOLD, VERT REV, FIX INFO,
 * DUPLICATE NAMES and FlightPlanFooter (docs/mfd-spec/05-fms-fpln.md sections 7-15). Translation of
 * the pinned FBW MfdFmsFplnDep/Arr/Airways/DirectTo/Hold/VertRev/FixInfo/DuplicateNames.tsx.
 *
 * Geometry: spec 05 anchors re-based on the spec 01 frame (r = 138 + y below the title bar, bottom
 * button rows at 908..948), border-box widgets (InputField 37, Button 18 + 22 x lines).
 */
#include "a380x_mfd_page_fpln_rev.h"
#include "a380x_fms.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NBSP "\xC2\xA0"
#define DEG "\xC2\xB0"
#define R(y) (138.0f + (float)(y))
#define FPREV_BOTTOM_Y 908.0f
#define FPREV_REQ_DIR 0x4602u
#define FPREV_REQ_AWY 0x4603u
#define FPREV_REQ_FIX 0x4604u
#define FPREV_MAX_ITEMS 64

#define DEP_ID(n) MFD_ID(MFD_PAGE_ID_FPLN_DEPARTURE, (n))
#define ARR_ID(n) MFD_ID(MFD_PAGE_ID_FPLN_ARRIVAL, (n))
#define AWY_ID(n) MFD_ID(MFD_PAGE_ID_FPLN_AIRWAYS, (n))
#define DIR_ID(n) MFD_ID(MFD_PAGE_ID_FPLN_DIRECT_TO, (n))
#define HOLD_ID(n) MFD_ID(MFD_PAGE_ID_FPLN_HOLD, (n))
#define VREV_ID(n) MFD_ID(MFD_PAGE_ID_FPLN_VERT_REV, (n))
#define FIX_ID(n) MFD_ID(MFD_PAGE_ID_FPLN_FIX_INFO, (n))
#define DUP_ID(n) MFD_ID(MFD_PAGE_ID_FPLN_DUPLICATE, (n))

/* ======================================================================================== */
/* Common                                                                                   */
/* ======================================================================================== */

typedef struct {
    A380XFlightPlans *fp;
    const A380XFplnPlan *plan;
    const A380XFplnRoute *route;   /* route of the revised element (primary or ALTN) */
    A380XMfdFplnRevised rev;
    int lplan, edit, altn, tmpy, sec;
    MfdColor vcol;                 /* value colour: green / yellow TMPY / white SEC */
} FprevCtx;

static void fprev_ctx(MfdPageContext *ctx, FprevCtx *c)
{
    memset(c, 0, sizeof(*c));
    c->fp = a380x_mfd_fpln_store(ctx);
    c->lplan = a380x_mfd_fpln_lateral_plan(ctx);
    c->plan = c->lplan >= 0 ? a380x_fpln_plan(c->fp, c->lplan) : NULL;
    c->rev = a380x_mfd_fpln_revised(ctx->side);
    c->altn = c->rev.valid ? c->rev.altn : 0;
    c->route = c->plan ? (c->altn ? &c->plan->alt : &c->plan->pri) : NULL;
    c->edit = a380x_mfd_fpln_edit_plan(ctx);
    c->tmpy = c->lplan == A380X_PLAN_TEMPORARY;
    c->sec = ctx->plan >= A380X_PLAN_SEC1;
    c->vcol = c->sec ? MFD_COL_WHITE : (c->tmpy ? MFD_COL_YELLOW : MFD_COL_GREEN);
}

static const A380XFplnLeg *fprev_rev_leg(const FprevCtx *c)
{
    if (!c->route || !c->rev.valid || c->rev.leg < 0 || c->rev.leg >= c->route->count) return NULL;
    return &c->route->el[c->rev.leg];
}

static void fprev_uri(const MfdPageContext *ctx, const char *page, char *buf, size_t n)
{
    snprintf(buf, n, "fms/%.12s/%.40s", ctx->uri->category[0] ? ctx->uri->category : "active", page);
}

static void fprev_text_c(MfdUi *ui, float x, float cy, MfdAnchor a, float fs, MfdColor c, const char *s)
{
    mfd_text(ui, x, cy - fs * 0.5f + MFD_FONT_BASELINE * fs, a, fs, c, s);
}

static void fprev_text_top(MfdUi *ui, float x, float top, MfdAnchor a, float fs, MfdColor c, const char *s)
{
    mfd_text(ui, x, mfd_baseline(top, fs), a, fs, c, s);
}

/* Bottom row: RETURN (left, no TMPY) / TMPY F-PLN (right, yellow, TMPY). Returns 1 = RETURN,
 * 2 = TMPY F-PLN, 0 = nothing. The navigation is done by the caller (fprev_footer_nav). */
static int fprev_footer(MfdPageContext *ctx, MfdId id_ret, MfdId id_tmpy, int tmpy)
{
    MfdButtonDesc bd;
    if (!tmpy && mfd_button(ctx->ui, id_ret, mfd_rect(2.0f, FPREV_BOTTOM_Y, 111.0f, MFD_BUTTON_H), "RETURN", 0))
        return 1;
    if (tmpy) {
        mfd_button_desc_init(&bd, "TMPY F-PLN");
        bd.flags = MFD_BTN_TEXT_YELLOW;
        if (mfd_button_ex(ctx->ui, id_tmpy, mfd_rect(600.0f, FPREV_BOTTOM_Y, 166.0f, MFD_BUTTON_H), &bd) ==
            MFD_BUTTON_CLICKED)
            return 2;
    }
    return 0;
}

static void fprev_footer_nav(MfdPageContext *ctx, int which)
{
    char uri[MFD_URI_LEN];
    if (!which) return;
    a380x_mfd_fpln_reset_revised(ctx->side);
    if (which == 1) {
        mfd_navigate_back(ctx);
    } else {
        fprev_uri(ctx, "f-pln", uri, sizeof(uri));
        mfd_navigate(ctx, uri);
    }
}

/* .mfd-fms-fpln-labeled-box-container: 1 px light-grey frame, title on the top border. */
static void fprev_labelled_box(MfdPageContext *ctx, MfdRect r, const char *title, const char *title2, MfdColor c2)
{
    float w = mfd_text_width(title, 20.0f) + (title2 ? mfd_text_width(title2, 24.0f) : 0.0f) + 4.0f;
    mfd_draw_bevel(ctx->dl, r, 1.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_SOLID);
    mfd_draw_fill_rect(ctx->dl, mfd_rect(r.x + 11.0f, r.y - 12.0f, w, 24.0f), MFD_COL_BLACK);
    fprev_text_c(ctx->ui, r.x + 13.0f, r.y, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, title);
    if (title2)
        fprev_text_c(ctx->ui, r.x + 13.0f + mfd_text_width(title, 20.0f), r.y, MFD_ANCHOR_LEFT, 24.0f, c2, title2);
}

/* Label (20 px, top) over value (25 px, 10 px below); optional trailing unit (6 px gap). */
static void fprev_label_value(MfdUi *ui, float x, float top, const char *label, const char *value, MfdColor vc,
                              const char *unit)
{
    fprev_text_top(ui, x, top, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, label);
    fprev_text_top(ui, x, top + 30.0f, MFD_ANCHOR_LEFT, 25.0f, vc, value);
    if (unit && unit[0])
        fprev_text_top(ui, x + mfd_text_width(value, 25.0f) + 6.0f, top + 34.0f, MFD_ANCHOR_LEFT, 20.0f,
                       MFD_COL_DARK_BLUE, unit);
}

static void fprev_runway_label(const A380XNavRunway *rw, int metric, int ils, char *buf, size_t n)
{
    char des[16], len[24];
    int i, pad;
    snprintf(des, sizeof(des), "%s", rw->designator);
    if (isfinite(rw->length_ft))
        snprintf(len, sizeof(len), "%.0f", metric ? floor(rw->length_ft * MFD_M_PER_FT + 0.5) : floor(rw->length_ft + 0.5));
    else
        snprintf(len, sizeof(len), "-----");
    buf[0] = '\0';
    snprintf(buf, n, "%s", des);
    for (i = (int)strlen(des); i < 3; ++i) strncat(buf, NBSP, n - strlen(buf) - 1);
    strncat(buf, NBSP, n - strlen(buf) - 1);
    pad = 5 - (int)strlen(len);
    for (i = 0; i < pad; ++i) strncat(buf, NBSP, n - strlen(buf) - 1);
    strncat(buf, len, n - strlen(buf) - 1);
    strncat(buf, metric ? "M" : "FT", n - strlen(buf) - 1);
    if (ils && rw->ls_ident[0]) strncat(buf, " ILS", n - strlen(buf) - 1);
}

static void fprev_runway_values(MfdUi *ui, const A380XNavRunway *rw, int metric, float x_len, float x_crs, float top,
                                MfdColor vc)
{
    char t[32];
    if (rw && isfinite(rw->length_ft))
        snprintf(t, sizeof(t), "%.0f", metric ? floor(rw->length_ft * MFD_M_PER_FT + 0.5) : floor(rw->length_ft + 0.5));
    else
        snprintf(t, sizeof(t), "----");
    fprev_label_value(ui, x_len, top, "LENGTH", t, vc, metric ? "M" : "FT");
    if (rw && isfinite(rw->mag_brg)) snprintf(t, sizeof(t), "%03.0f", fmod(floor(rw->mag_brg + 0.5) + 359.0, 360.0) + 1.0);
    else snprintf(t, sizeof(t), "---");
    fprev_label_value(ui, x_crs, top, "CRS", t, vc, DEG);
}

static void fprev_freq(const A380XNavRunway *rw, char *buf, size_t n)
{
    if (rw && rw->ls_freq_mhz > 0.0f) snprintf(buf, n, "%.2f", (double)rw->ls_freq_mhz);
    else snprintf(buf, n, "---.--");
}

/* ======================================================================================== */
/* DUPLICATE NAMES (spec 05 section 14)                                                     */
/* ======================================================================================== */

typedef struct {
    int open;
    unsigned req;
    int n, from, result;
    A380XNavFix items[A380X_MFD_DUP_MAX];
    A380XNavFix chosen;
} FprevDup;

static FprevDup fprev_dup[MFD_SIDE_COUNT];

void a380x_mfd_fpln_dup_open(MfdSide side, unsigned requester, const A380XNavFix *items, int n)
{
    FprevDup *d;
    if ((int)side < 0 || (int)side >= MFD_SIDE_COUNT || !items || n <= 0) return;
    d = &fprev_dup[side];
    if (n > A380X_MFD_DUP_MAX) n = A380X_MFD_DUP_MAX;
    memcpy(d->items, items, sizeof(items[0]) * (size_t)n);
    d->n = n;
    d->from = 0;
    d->req = requester;
    d->result = 0;
    d->open = 1;
}

int a380x_mfd_fpln_dup_is_open(MfdSide side)
{
    return (int)side >= 0 && (int)side < MFD_SIDE_COUNT && fprev_dup[side].open;
}

static void fprev_dup_coord(double lat, double lon, char *buf, size_t n)
{
    long a = (long)floor(fabs(lat) * 60.0 + 0.5), b = (long)floor(fabs(lon) * 60.0 + 0.5);
    snprintf(buf, n, "%02ld" DEG "%02ld%c/%03ld" DEG "%02ld%c", a / 60, a % 60, lat >= 0 ? 'N' : 'S', b / 60, b % 60,
             lon >= 0 ? 'E' : 'W');
}

static void fprev_dup_draw(MfdPageContext *ctx, FprevDup *d)
{
    MfdUi *ui = ctx->ui;
    MfdDrawList *dl = ctx->dl;
    MfdLayer old = mfd_draw_get_layer(dl);
    static const char *const hdr[4] = {"IDENT", "DIST(NM)", "LAT/LONG", "FREQ/CHAN"};
    static const float col_x[5] = {0.0f, 153.6f, 307.2f, 537.6f, 768.0f};
    int i, part, dist_valid = 0;
    float box_y = 276.0f, row_h = 50.9f;
    mfd_draw_set_layer(dl, MFD_LAYER_OVERLAY);
    mfd_ui_hit(ui, DUP_ID(1), MFD_PART_BLOCKER, mfd_rect(0.0f, MFD_TITLE_Y0, 768.0f, MFD_BODY_Y1 - MFD_TITLE_Y0));
    mfd_draw_fill_rect(dl, mfd_rect(0.0f, MFD_TITLE_Y0, 768.0f, MFD_BODY_Y1 - MFD_TITLE_Y0), MFD_COL_BLACK);
    /* own title bar (ActivePageTitleBar, FMS style) */
    mfd_draw_fill_rect(dl, mfd_rect(0.0f, MFD_TITLE_Y0, 594.0f, 40.0f), MFD_COL_DARK_GREY);
    mfd_draw_fill_rect(dl, mfd_rect(596.0f, MFD_TITLE_Y0, 70.0f, 40.0f), MFD_COL_DARK_GREY);
    mfd_draw_fill_rect(dl, mfd_rect(668.0f, MFD_TITLE_Y0, 100.0f, 40.0f), MFD_COL_DARK_GREY);
    mfd_draw_text(dl, MFD_FONT_SZ, MFD_FS_TITLE, MFD_COL_TITLE_TEXT, MFD_ANCHOR_LEFT, 7.0f, 125.0f, "DUPLICATE NAMES");
    if (ctx->in && ctx->in->engine_out) {
        mfd_draw_fill_rect(dl, mfd_rect(612.0f, 101.0f, mfd_text_width("EO", 30.0f) + 4.0f, 34.0f), MFD_COL_AMBER);
        mfd_draw_text(dl, MFD_FONT_SZ, 30.0f, MFD_COL_TITLE_TEXT, MFD_ANCHOR_LEFT, 614.0f, 127.0f, "EO");
    }
    for (i = 0; i < 4; ++i)
        fprev_text_c(ui, (col_x[i] + col_x[i + 1]) * 0.5f, 257.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, hdr[i]);
    mfd_draw_bevel(dl, mfd_rect(0.0f, box_y, 768.0f, 511.0f), 2.0f, MFD_COL_DARK_GREY, MFD_BEVEL_SOLID);
    for (i = 0; i < d->n; ++i)
        if (d->items[i].dist_nm > 0.0) dist_valid = 1;
    if (mfd_ui_clicked_part(ui, DUP_ID(2), &part) && part >= 0 && d->from + part < d->n) {
        d->chosen = d->items[d->from + part];
        d->result = 1;
        d->open = 0;
    }
    for (i = 0; i < 10 && d->from + i < d->n; ++i) {
        const A380XNavFix *f = &d->items[d->from + i];
        MfdRect row = mfd_rect(2.0f, box_y + 2.0f + row_h * (float)i, 764.0f, row_h);
        float cy = row.y + row.h * 0.5f;
        char t[48];
        mfd_ui_hit(ui, DUP_ID(2), i, row);
        if (mfd_ui_is_hovered_part(ui, DUP_ID(2), i)) mfd_draw_bevel(dl, row, 2.0f, MFD_COL_CYAN, MFD_BEVEL_SOLID);
        fprev_text_c(ui, (col_x[0] + col_x[1]) * 0.5f, cy, MFD_ANCHOR_CENTER, 30.0f, MFD_COL_GREEN, f->ident);
        if (dist_valid) {
            snprintf(t, sizeof(t), "%.0f", floor(f->dist_nm + 0.5));
            fprev_text_c(ui, (col_x[1] + col_x[2]) * 0.5f, cy, MFD_ANCHOR_CENTER, 30.0f, MFD_COL_GREEN, t);
        }
        fprev_dup_coord(f->lat, f->lon, t, sizeof(t));
        fprev_text_c(ui, (col_x[2] + col_x[3]) * 0.5f, cy, MFD_ANCHOR_CENTER, 30.0f, MFD_COL_GREEN, t);
        if (f->freq > 0.0f) {
            if (f->freq > 120.0f) snprintf(t, sizeof(t), "%.0f", (double)f->freq);
            else snprintf(t, sizeof(t), "%.2f", (double)f->freq);
            fprev_text_c(ui, (col_x[3] + col_x[4]) * 0.5f, cy, MFD_ANCHOR_CENTER, 30.0f, MFD_COL_GREEN, t);
        }
    }
    if (mfd_icon_button(ui, DUP_ID(3), mfd_rect(324.0f, 797.0f, 50.0f, 50.0f), MFD_ICON_DOUBLE_DOWN,
                        !(d->n > d->from + 10)))
        d->from++;
    if (mfd_icon_button(ui, DUP_ID(4), mfd_rect(394.0f, 797.0f, 50.0f, 50.0f), MFD_ICON_DOUBLE_UP, d->from <= 0))
        d->from--;
    if (mfd_button(ui, DUP_ID(5), mfd_rect(2.0f, FPREV_BOTTOM_Y, 111.0f, MFD_BUTTON_H), "RETURN", 0)) {
        d->result = -1;
        d->open = 0;
    }
    mfd_draw_set_layer(dl, old);
}

int a380x_mfd_fpln_dup_run(MfdPageContext *ctx, unsigned requester, A380XNavFix *out)
{
    FprevDup *d;
    int r;
    if (!ctx || (int)ctx->side < 0 || (int)ctx->side >= MFD_SIDE_COUNT) return 0;
    d = &fprev_dup[ctx->side];
    if (ctx->entered && (d->open || d->result)) { /* navigation rejects the pending request */
        d->open = 0;
        d->result = 0;
        d->req = 0;
        return 0;
    }
    if (d->open) fprev_dup_draw(ctx, d);
    if (d->result != 0 && d->req == requester) {
        r = d->result;
        if (r == 1 && out) *out = d->chosen;
        d->result = 0;
        d->req = 0;
        return r;
    }
    return 0;
}

/* ======================================================================================== */
/* DEPARTURE (spec 05 section 7)                                                            */
/* ======================================================================================== */

static void dep_page(MfdPageContext *ctx)
{
    MfdUi *ui = ctx->ui;
    FprevCtx c;
    const A380XNavAirport *apt;
    const A380XNavRunway *rw = NULL;
    const A380XNavProcedure *sid = NULL;
    static char rwy_lbl[48][40], sid_lbl[FPREV_MAX_ITEMS][24];
    MfdMenuItem rwy_items[48], sid_items[FPREV_MAX_ITEMS], tr_items[FPREV_MAX_ITEMS];
    const A380XNavRunway *rwy_ptr[48];
    const A380XNavProcedure *sid_ptr[FPREV_MAX_ITEMS];
    const A380XNavProcTrans *tr_ptr[FPREV_MAX_ITEMS];
    int n_rwy = 0, n_sid = 0, n_tr = 0, i, k, metric = ctx->in ? ctx->in->metric_units : 1;
    MfdButtonDesc bd;
    char t[48];
    float top1 = R(36), top2 = R(96);

    fprev_ctx(ctx, &c);
    apt = (c.fp && c.route && c.route->origin_icao[0]) ? a380x_nav_airport(c.fp->nav, c.route->origin_icao) : NULL;
    if (apt && c.route->origin_rwy[0]) rw = a380x_nav_runway(apt, c.route->origin_rwy);
    if (apt && c.route->sid[0]) sid = a380x_nav_procedure(apt, A380X_PROC_SID, c.route->sid);

    /* lists */
    if (apt) {
        for (i = 0; i < apt->runway_count && n_rwy < 48; ++i) {
            rwy_ptr[n_rwy] = &apt->runways[i];
            fprev_runway_label(&apt->runways[i], metric, 1, rwy_lbl[n_rwy], sizeof(rwy_lbl[0]));
            rwy_items[n_rwy].label = rwy_lbl[n_rwy];
            rwy_items[n_rwy].disabled = 0;
            n_rwy++;
        }
        sid_items[0].label = "NONE";
        sid_items[0].disabled = 0;
        sid_ptr[0] = NULL;
        n_sid = 1;
        for (i = 0; i < a380x_nav_procedure_count(apt, A380X_PROC_SID) && n_sid < FPREV_MAX_ITEMS; ++i) {
            const A380XNavProcedure *p = a380x_nav_procedure_at(apt, A380X_PROC_SID, i);
            if (!p || (c.route->origin_rwy[0] && !a380x_nav_sid_serves_runway(apt, p, c.route->origin_rwy))) continue;
            snprintf(sid_lbl[n_sid], sizeof(sid_lbl[0]), "%s%s", p->ident, p->rnp_ar ? " (RNP)" : "");
            sid_items[n_sid].label = sid_lbl[n_sid];
            sid_items[n_sid].disabled = 0;
            sid_ptr[n_sid++] = p;
        }
        tr_items[0].label = "NONE";
        tr_items[0].disabled = 0;
        n_tr = 1;
        if (sid) {
            const A380XNavProcTrans *tl[FPREV_MAX_ITEMS];
            int m = a380x_nav_proc_transitions(apt, sid, A380X_TRANS_ENROUTE, tl, FPREV_MAX_ITEMS - 1);
            if (m > FPREV_MAX_ITEMS - 1) m = FPREV_MAX_ITEMS - 1;
            for (i = 0; i < m; ++i) {
                tr_ptr[n_tr] = tl[i];
                tr_items[n_tr].label = tl[i]->ident;
                tr_items[n_tr].disabled = 0;
                n_tr++;
            }
        }
    }

    fprev_labelled_box(ctx, mfd_rect(0.0f, R(20), 768.0f, 142.0f), "SELECTED DEPARTURE", NULL, MFD_COL_WHITE);
    /* row 1 */
    fprev_text_c(ui, 16.0f, top1 + 25.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "FROM");
    fprev_text_c(ui, 81.0f, top1 + 25.0f, MFD_ANCHOR_LEFT, 25.0f, c.vcol,
                 (c.route && c.route->origin_icao[0]) ? c.route->origin_icao : "----");
    fprev_label_value(ui, 384.0f, top1, "RWY", rw ? rw->designator : "---", c.vcol, NULL);
    fprev_runway_values(ui, rw, metric, 507.0f, 666.0f, top1, c.vcol);
    /* row 2 */
    fprev_label_value(ui, 16.0f, top2, "EOSID", rw ? "NONE" : "------", c.vcol, NULL);
    fprev_freq(rw, t, sizeof(t));
    fprev_label_value(ui, 200.0f, top2, "FREQ/CHAN", t, c.vcol, NULL);
    if (c.route && c.route->sid[0]) snprintf(t, sizeof(t), "%s", c.route->sid);
    else if (c.route && c.route->sid_none) snprintf(t, sizeof(t), "NONE");
    else snprintf(t, sizeof(t), "%s", n_sid > 1 ? "------" : "NONE");
    fprev_label_value(ui, 421.0f, top2, "SID", t, c.vcol, NULL);
    if (c.route && c.route->sid_trans[0]) snprintf(t, sizeof(t), "%s", c.route->sid_trans);
    else if (sid && n_tr <= 1) snprintf(t, sizeof(t), "NONE");
    else snprintf(t, sizeof(t), "------");
    fprev_label_value(ui, 605.0f, top2, "TRANS", t, c.vcol, NULL);

    /* menu buttons */
    mfd_button_desc_init(&bd, "RWY");
    bd.menu = rwy_items;
    bd.menu_count = n_rwy;
    bd.scroll_to = -1;
    bd.flags = n_rwy ? 0u : MFD_BTN_DISABLED;
    k = mfd_button_ex(ui, DEP_ID(1), mfd_rect(50.0f, R(182), 250.0f, MFD_BUTTON_H), &bd);
    if (k >= 0 && k < n_rwy) a380x_mfd_fpln_result(ctx, a380x_fpln_set_origin_runway(c.fp, c.edit, c.altn, rwy_ptr[k]->ident));
    mfd_button_desc_init(&bd, "SID");
    bd.menu = sid_items;
    bd.menu_count = n_sid;
    bd.scroll_to = -1;
    bd.flags = (rw && n_sid > 1) ? 0u : MFD_BTN_DISABLED; /* [PORT] disabled without SIDs */
    k = mfd_button_ex(ui, DEP_ID(2), mfd_rect(400.0f, R(182), 140.0f, MFD_BUTTON_H), &bd);
    if (k >= 0 && k < n_sid)
        a380x_mfd_fpln_result(ctx, a380x_fpln_set_sid(c.fp, c.edit, c.altn, k == 0 ? "NONE" : sid_ptr[k]->ident));
    mfd_button_desc_init(&bd, "TRANS");
    bd.menu = tr_items;
    bd.menu_count = n_tr;
    bd.scroll_to = -1;
    bd.flags = (sid && n_tr > 1) ? 0u : MFD_BTN_DISABLED;
    k = mfd_button_ex(ui, DEP_ID(3), mfd_rect(590.0f, R(182), 130.0f, MFD_BUTTON_H), &bd);
    if (k >= 0 && k < n_tr)
        a380x_mfd_fpln_result(ctx, a380x_fpln_set_sid_trans(c.fp, c.edit, c.altn, k == 0 ? "NONE" : tr_ptr[k]->ident));

    fprev_footer_nav(ctx, fprev_footer(ctx, DEP_ID(10), DEP_ID(11), c.tmpy));
}

/* ======================================================================================== */
/* ARRIVAL (spec 05 section 8)                                                              */
/* ======================================================================================== */

static int arr_appr_listed(const A380XNavProcedure *p)
{
    switch (p->appr_type) {
    case A380X_APPR_TACAN: case A380X_APPR_MLS: case A380X_APPR_MLS_A: case A380X_APPR_MLS_BC:
    case A380X_APPR_LOC_BACKCOURSE:
        return 0;
    default:
        return p->runway[0] != '\0';
    }
}

static int arr_appr_less(const A380XNavProcedure *a, const A380XNavProcedure *b)
{
    int c = strcmp(a->runway, b->runway);
    if (c) return c < 0;
    return a380x_nav_approach_type_order(a->appr_type) < a380x_nav_approach_type_order(b->appr_type);
}

static void arr_page(MfdPageContext *ctx)
{
    MfdUi *ui = ctx->ui;
    FprevCtx c;
    const A380XNavAirport *apt;
    const A380XNavRunway *rw = NULL;
    const A380XNavProcedure *appr = NULL, *star = NULL;
    static char rwy_lbl[48][40], appr_lbl[FPREV_MAX_ITEMS][32], via_lbl[FPREV_MAX_ITEMS][24];
    MfdMenuItem rwy_items[48], appr_items[FPREV_MAX_ITEMS], via_items[FPREV_MAX_ITEMS], star_items[FPREV_MAX_ITEMS],
        tr_items[FPREV_MAX_ITEMS];
    const A380XNavRunway *rwy_ptr[48];
    const A380XNavProcedure *appr_ptr[FPREV_MAX_ITEMS], *star_ptr[FPREV_MAX_ITEMS];
    const A380XNavProcTrans *via_ptr[FPREV_MAX_ITEMS], *tr_ptr[FPREV_MAX_ITEMS];
    int n_rwy = 0, n_appr = 0, n_via = 0, n_star = 0, n_tr = 0, n_appr_db = 0, n_star_db = 0, n_via_db = 0;
    int i, j, k, metric = ctx->in ? ctx->in->metric_units : 1, appr_scroll = -1;
    MfdButtonDesc bd;
    char t[48];
    static const float X[5] = {16.0f, 163.0f, 310.0f, 458.0f, 605.0f};
    float top1 = R(36), top2 = R(96);

    fprev_ctx(ctx, &c);
    apt = (c.fp && c.route && c.route->dest_icao[0]) ? a380x_nav_airport(c.fp->nav, c.route->dest_icao) : NULL;
    if (apt && c.route->dest_rwy[0]) rw = a380x_nav_runway(apt, c.route->dest_rwy);
    if (apt && c.route->approach[0]) appr = a380x_nav_procedure(apt, A380X_PROC_APPROACH, c.route->approach);
    if (apt && c.route->star[0]) star = a380x_nav_procedure(apt, A380X_PROC_STAR, c.route->star);

    if (apt) {
        for (i = 0; i < apt->runway_count && n_rwy < 48; ++i) {
            rwy_ptr[n_rwy] = &apt->runways[i];
            fprev_runway_label(&apt->runways[i], metric, 0, rwy_lbl[n_rwy], sizeof(rwy_lbl[0]));
            rwy_items[n_rwy].label = rwy_lbl[n_rwy];
            rwy_items[n_rwy].disabled = 0;
            n_rwy++;
        }
        /* approaches: filtered and sorted by runway then type order */
        appr_items[0].label = "NONE";
        appr_items[0].disabled = 0;
        appr_ptr[0] = NULL;
        n_appr = 1;
        for (i = 0; i < a380x_nav_procedure_count(apt, A380X_PROC_APPROACH) && n_appr < FPREV_MAX_ITEMS; ++i) {
            const A380XNavProcedure *p = a380x_nav_procedure_at(apt, A380X_PROC_APPROACH, i);
            if (!p || !arr_appr_listed(p)) continue;
            for (j = n_appr; j > 1 && arr_appr_less(p, appr_ptr[j - 1]); --j) appr_ptr[j] = appr_ptr[j - 1];
            appr_ptr[j] = p;
            n_appr++;
        }
        n_appr_db = n_appr - 1;
        for (i = 1; i < n_appr; ++i) {
            a380x_nav_approach_name(appr_ptr[i], 1, appr_lbl[i], sizeof(appr_lbl[0]));
            appr_items[i].label = appr_lbl[i];
            appr_items[i].disabled = 0;
            if (appr_scroll < 0 && c.route->dest_rwy[0] && !strcmp(appr_ptr[i]->runway, c.route->dest_rwy))
                appr_scroll = i;
        }
        /* VIAs */
        via_items[0].label = "NONE";
        via_items[0].disabled = 0;
        n_via = 1;
        if (appr) {
            const A380XNavProcTrans *vl[FPREV_MAX_ITEMS], *srt[FPREV_MAX_ITEMS];
            int m = a380x_nav_proc_transitions(apt, appr, A380X_TRANS_APPR_VIA, vl, FPREV_MAX_ITEMS - 1), ns = 0;
            if (m > FPREV_MAX_ITEMS - 1) m = FPREV_MAX_ITEMS - 1;
            n_via_db = m;
            if (star) {
                ns = a380x_nav_proc_transitions(apt, star, A380X_TRANS_RUNWAY, srt, FPREV_MAX_ITEMS);
                if (ns > FPREV_MAX_ITEMS) ns = FPREV_MAX_ITEMS;
            }
            for (i = 0; i < m; ++i) {
                int ok = ns == 0;
                for (j = 0; j < ns && !ok; ++j) {
                    const A380XNavProcTrans *s = srt[j];
                    if (s->leg_count > 0 && vl[i]->leg_count > 0 &&
                        !strcmp(apt->legs[s->first_leg + s->leg_count - 1].fix.ident,
                                apt->legs[vl[i]->first_leg].fix.ident))
                        ok = 1;
                }
                if (!ok) continue;
                snprintf(via_lbl[n_via], sizeof(via_lbl[0]), "%s%s", vl[i]->ident, appr->rnp_ar ? " (RNP)" : "");
                via_items[n_via].label = via_lbl[n_via];
                via_items[n_via].disabled = 0;
                via_ptr[n_via++] = vl[i];
            }
        }
        /* STARs */
        star_items[0].label = "NONE";
        star_items[0].disabled = 0;
        n_star = 1;
        n_star_db = a380x_nav_procedure_count(apt, A380X_PROC_STAR);
        for (i = 0; i < n_star_db && n_star < FPREV_MAX_ITEMS; ++i) {
            const A380XNavProcedure *p = a380x_nav_procedure_at(apt, A380X_PROC_STAR, i);
            if (!p || (appr && !a380x_nav_star_serves_runway(apt, p, appr->runway))) continue;
            star_items[n_star].label = p->ident;
            star_items[n_star].disabled = 0;
            star_ptr[n_star++] = p;
        }
        tr_items[0].label = "NONE";
        tr_items[0].disabled = 0;
        n_tr = 1;
        if (star) {
            const A380XNavProcTrans *tl[FPREV_MAX_ITEMS];
            int m = a380x_nav_proc_transitions(apt, star, A380X_TRANS_ENROUTE, tl, FPREV_MAX_ITEMS - 1);
            if (m > FPREV_MAX_ITEMS - 1) m = FPREV_MAX_ITEMS - 1;
            for (i = 0; i < m; ++i) {
                tr_ptr[n_tr] = tl[i];
                tr_items[n_tr].label = tl[i]->ident;
                tr_items[n_tr].disabled = 0;
                n_tr++;
            }
        }
    }

    fprev_labelled_box(ctx, mfd_rect(0.0f, R(20), 768.0f, 142.0f), "SELECTED ARRIVAL", NULL, MFD_COL_WHITE);
    fprev_text_c(ui, X[0], top1 + 25.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "TO");
    fprev_text_c(ui, X[0] + mfd_text_width("TO", 20.0f) + 12.0f, top1 + 25.0f, MFD_ANCHOR_LEFT, 25.0f, c.vcol,
                 (c.route && c.route->dest_icao[0]) ? c.route->dest_icao : "----");
    fprev_label_value(ui, X[1], top1, "LS", (rw && rw->ls_ident[0]) ? rw->ls_ident : "----", c.vcol, NULL);
    fprev_label_value(ui, X[2], top1, "RWY", rw ? rw->designator : "---", c.vcol, NULL);
    fprev_runway_values(ui, rw, metric, X[3], X[4], top1, c.vcol);
    if (appr) a380x_nav_approach_name(appr, 0, t, sizeof(t));
    else if (c.route && c.route->appr_none) snprintf(t, sizeof(t), "NONE");
    else snprintf(t, sizeof(t), "%s", n_appr_db > 0 ? "------" : "NONE");
    fprev_label_value(ui, X[0], top2, "APPR", t, c.vcol, NULL);
    fprev_freq(appr ? rw : NULL, t, sizeof(t));
    fprev_label_value(ui, X[1], top2, "FREQ/CHAN", t, c.vcol, NULL);
    if (c.route && c.route->appr_via[0]) snprintf(t, sizeof(t), "%s", c.route->appr_via);
    else snprintf(t, sizeof(t), "%s", (!appr || n_via_db > 0) ? "------" : "NONE");
    fprev_label_value(ui, X[2], top2, "VIA", t, c.vcol, NULL);
    if (c.route && c.route->star[0]) snprintf(t, sizeof(t), "%s", c.route->star);
    else if (c.route && c.route->star_none) snprintf(t, sizeof(t), "NONE");
    else snprintf(t, sizeof(t), "%s", n_star_db > 0 ? "------" : "NONE");
    fprev_label_value(ui, X[3], top2, "STAR", t, c.vcol, NULL);
    if (c.route && c.route->star_trans[0]) snprintf(t, sizeof(t), "%s", c.route->star_trans);
    else if (star && n_tr <= 1) snprintf(t, sizeof(t), "NONE");
    else snprintf(t, sizeof(t), "------");
    fprev_label_value(ui, X[4], top2, "TRANS", t, c.vcol, NULL);

    mfd_button_desc_init(&bd, "RWY");
    bd.menu = rwy_items;
    bd.menu_count = n_rwy;
    bd.scroll_to = -1;
    bd.flags = n_rwy ? 0u : MFD_BTN_DISABLED;
    k = mfd_button_ex(ui, ARR_ID(1), mfd_rect(0.0f, R(182), 190.0f, MFD_BUTTON_H), &bd);
    if (k >= 0 && k < n_rwy) a380x_mfd_fpln_result(ctx, a380x_fpln_set_dest_runway(c.fp, c.edit, c.altn, rwy_ptr[k]->ident));
    mfd_button_desc_init(&bd, "APPR");
    bd.menu = appr_items;
    bd.menu_count = n_appr;
    bd.scroll_to = appr_scroll;
    bd.flags = n_appr_db > 0 ? 0u : MFD_BTN_DISABLED;
    k = mfd_button_ex(ui, ARR_ID(2), mfd_rect(201.0f, R(182), 160.0f, MFD_BUTTON_H), &bd);
    if (k >= 0 && k < n_appr)
        a380x_mfd_fpln_result(ctx, a380x_fpln_set_approach(c.fp, c.edit, c.altn, k == 0 ? "NONE" : appr_ptr[k]->ident));
    mfd_button_desc_init(&bd, "VIA");
    bd.menu = via_items;
    bd.menu_count = n_via;
    bd.scroll_to = -1;
    bd.flags = (appr && n_via > 1) ? 0u : MFD_BTN_DISABLED;
    k = mfd_button_ex(ui, ARR_ID(3), mfd_rect(372.0f, R(182), 125.0f, MFD_BUTTON_H), &bd);
    if (k >= 0 && k < n_via)
        a380x_mfd_fpln_result(ctx, a380x_fpln_set_appr_via(c.fp, c.edit, c.altn, k == 0 ? "NONE" : via_ptr[k]->ident));
    mfd_button_desc_init(&bd, "STAR");
    bd.menu = star_items;
    bd.menu_count = n_star;
    bd.scroll_to = -1;
    bd.flags = (n_star_db > 0 && appr) ? 0u : MFD_BTN_DISABLED;
    k = mfd_button_ex(ui, ARR_ID(4), mfd_rect(507.0f, R(182), 125.0f, MFD_BUTTON_H), &bd);
    if (k >= 0 && k < n_star)
        a380x_mfd_fpln_result(ctx, a380x_fpln_set_star(c.fp, c.edit, c.altn, k == 0 ? "NONE" : star_ptr[k]->ident));
    mfd_button_desc_init(&bd, "TRANS");
    bd.menu = tr_items;
    bd.menu_count = n_tr;
    bd.scroll_to = -1;
    bd.flags = (star && n_tr > 1) ? 0u : MFD_BTN_DISABLED;
    k = mfd_button_ex(ui, ARR_ID(5), mfd_rect(643.0f, R(182), 125.0f, MFD_BUTTON_H), &bd);
    if (k >= 0 && k < n_tr)
        a380x_mfd_fpln_result(ctx, a380x_fpln_set_star_trans(c.fp, c.edit, c.altn, k == 0 ? "NONE" : tr_ptr[k]->ident));

    fprev_footer_nav(ctx, fprev_footer(ctx, ARR_ID(10), ARR_ID(11), c.tmpy));
}

/* ======================================================================================== */
/* AIRWAYS (spec 05 section 9)                                                              */
/* ======================================================================================== */

static void awy_finalise(MfdPageContext *ctx, const FprevCtx *c)
{
    if (c->plan && c->plan->awy.active) a380x_mfd_fpln_result(ctx, a380x_fpln_airway_finalise(c->fp, c->edit));
}

static void awy_page(MfdPageContext *ctx)
{
    MfdFplnAwyState *st = MFD_PAGE_STATE(ctx, MfdFplnAwyState);
    MfdUi *ui = ctx->ui;
    FprevCtx c;
    const A380XFplnLeg *L;
    const A380XFplnAirwayEntry *e;
    int i, rows, which;
    A380XNavFix chosen;

    fprev_ctx(ctx, &c);
    L = fprev_rev_leg(&c);
    e = c.plan ? &c.plan->awy : NULL;

    fprev_text_c(ui, 15.0f, R(28.5), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "AIRWAYS FROM");
    if (L) fprev_text_c(ui, 186.0f, R(28.5), MFD_ANCHOR_LEFT, 30.0f, c.tmpy ? MFD_COL_YELLOW : MFD_COL_GREEN, L->ident);
    mfd_draw_box(ctx->dl, mfd_rect(30.0f, R(52), 708.0f, 630.0f), MFD_COL_BLACK, 2.0f, MFD_COL_LIGHT_GREY,
                 MFD_BEVEL_RAISED);

    rows = (e && e->active) ? e->line_count : 0;
    /* [PORT] an extra VIA-only line after an airway line without TO: automatic intersection */
    if (rows > 0 && rows < A380X_FPLN_AWY_LINES) {
        const A380XFplnAwyLine *ll = &e->line[rows - 1];
        if (ll->via_locked && !ll->to_locked && ll->via[0] && strcmp(ll->via, "DCT") != 0) rows++;
    }
    for (i = 0; i < rows && i < A380X_FPLN_AWY_LINES; ++i) {
        int real = i < e->line_count, last = real && i == e->line_count - 1, extra = !real;
        const A380XFplnAwyLine *ln = real ? &e->line[i] : NULL;
        float y = R(84) + 52.0f * (float)i;
        MfdFieldDesc fv_d, ft_d;
        MfdFieldValue fv, ft;
        MfdValue out;
        float wv, wt;
        mfd_field_desc_init(&fv_d, MFD_FMT_TXT_AIRWAY);
        mfd_field_desc_init(&ft_d, MFD_FMT_TXT_WAYPOINT);
        fv_d.flags = MFD_FIELD_YELLOW_WHEN_DISABLED | (c.tmpy ? MFD_FIELD_TMPY : 0u);
        ft_d.flags = fv_d.flags;
        memset(&fv, 0, sizeof(fv));
        memset(&ft, 0, sizeof(ft));
        if (ln && ln->via[0]) {
            fv.has_value = 1;
            fv.text = ln->via;
        }
        if (ln && ln->to_locked) {
            ft.has_value = 1;
            ft.text = ln->to.ident;
        }
        if ((ln && ln->via_locked) || (!last && !extra)) fv_d.flags |= MFD_FIELD_DISABLED;
        if ((ln && ln->to_locked) || extra || (!last && !extra)) ft_d.flags |= MFD_FIELD_DISABLED;
        wv = mfd_field_auto_width(&fv_d, &fv);
        wt = mfd_field_auto_width(&ft_d, &ft);
        fprev_text_c(ui, 47.0f, y + 18.5f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "VIA");
        if (mfd_input_field(ui, AWY_ID(10 + i * 2), 47.0f + mfd_text_width("VIA", 20.0f) + 5.0f, y, &fv_d, &fv, &out) ==
                MFD_FIELD_COMMITTED &&
            out.str[0])
            a380x_mfd_fpln_result(ctx, a380x_fpln_airway_via(c.fp, c.edit, out.str));
        if (!extra) {
            fprev_text_c(ui, 721.0f - wt - 5.0f, y + 18.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "TO");
            if (mfd_input_field(ui, AWY_ID(11 + i * 2), 721.0f - wt, y, &ft_d, &ft, &out) == MFD_FIELD_COMMITTED &&
                out.str[0] && ln) {
                if (ln->via[0] && strcmp(ln->via, "DCT") != 0) {
                    a380x_mfd_fpln_result(ctx, a380x_fpln_airway_to_ident(c.fp, c.edit, out.str));
                } else {
                    A380XNavFix fx[A380X_MFD_DUP_MAX];
                    int n = a380x_mfd_fpln_resolve_place(ctx, out.str, fx, A380X_MFD_DUP_MAX);
                    if (n == 1) a380x_mfd_fpln_result(ctx, a380x_fpln_airway_to_fix(c.fp, c.edit, &fx[0], 1));
                    else if (n > 1) {
                        a380x_mfd_fpln_dup_open(ctx->side, FPREV_REQ_AWY, fx, n);
                        st->dup_wait = 1;
                    }
                }
            }
        }
        (void)wv;
    }
    mfd_icon_button(ui, AWY_ID(2), mfd_rect(314.0f, R(687), 60.0f, 60.0f), MFD_ICON_DOUBLE_DOWN, 1);
    mfd_icon_button(ui, AWY_ID(3), mfd_rect(394.0f, R(687), 60.0f, 60.0f), MFD_ICON_DOUBLE_UP, 1);

    which = fprev_footer(ctx, AWY_ID(4), AWY_ID(5), c.tmpy);
    if (which == 1 && c.sec) awy_finalise(ctx, &c);
    if (which == 2) awy_finalise(ctx, &c);
    if (which) {
        char uri[MFD_URI_LEN];
        a380x_mfd_fpln_reset_revised(ctx->side);
        fprev_uri(ctx, "f-pln", uri, sizeof(uri));
        mfd_navigate(ctx, uri);
    }
    if (a380x_mfd_fpln_dup_run(ctx, FPREV_REQ_AWY, &chosen) == 1 && st->dup_wait)
        a380x_mfd_fpln_result(ctx, a380x_fpln_airway_to_fix(c.fp, c.edit, &chosen, 1));
    if (!a380x_mfd_fpln_dup_is_open(ctx->side)) st->dup_wait = 0;
}

/* ======================================================================================== */
/* DIRECT-TO (spec 05 section 10)                                                           */
/* ======================================================================================== */

static void dir_do(MfdPageContext *ctx, A380XFlightPlans *fp, int leg, const A380XNavFix *f, int mode)
{
    A380XFplnAircraft ac;
    a380x_mfd_fpln_aircraft(ctx, &ac);
    if (a380x_fpln_has_tmpy(fp)) {
        a380x_mfd_fpln_tmpy_erase(ctx);
        a380x_mfd_fpln_reset_revised(ctx->side);
    }
    if (leg >= 0) {
        a380x_mfd_fpln_set_revised(ctx->side, A380X_PLAN_ACTIVE, 0, leg);
        a380x_mfd_fpln_result(ctx, a380x_fpln_direct_to_leg(fp, A380X_PLAN_ACTIVE, &ac, leg, (A380XDirToMode)mode, NAN));
    } else if (f) {
        a380x_mfd_fpln_result(ctx, a380x_fpln_direct_to_fix(fp, A380X_PLAN_ACTIVE, &ac, f, (A380XDirToMode)mode, NAN));
    }
}

static void dir_page(MfdPageContext *ctx)
{
    MfdFplnDirState *st = MFD_PAGE_STATE(ctx, MfdFplnDirState);
    MfdUi *ui = ctx->ui;
    A380XFlightPlans *fp = a380x_mfd_fpln_store(ctx);
    const A380XFplnPlan *act = fp ? a380x_fpln_plan(fp, A380X_PLAN_ACTIVE) : NULL;
    const A380XFplnPlan *tmp = fp ? a380x_fpln_plan(fp, A380X_PLAN_TEMPORARY) : NULL;
    A380XMfdFplnRevised rev = a380x_mfd_fpln_revised(ctx->side);
    const char *vals[FPREV_MAX_ITEMS + 1];
    int map[FPREV_MAX_ITEMS + 1], n = 0, i, sel = -1, tmpy = tmp != NULL;
    MfdDropdownDesc dd;
    char typed[32], t[32];
    static const char *const opts[4] = {"DIRECT", "DIRECT WITH ABEAM", "CRS IN", "CRS OUT"};
    MfdColor vc = tmpy ? MFD_COL_YELLOW : MFD_COL_GREEN;
    A380XNavFix chosen;

    if (ctx->entered) {
        st->option = 0;
        st->selected = -1;
        st->typed[0] = '\0';
    }
    if (act) {
        int end = act->pri.first_missed_appr_index;
        for (i = act->active_leg_index < 0 ? 0 : act->active_leg_index; i < end && i < act->pri.count && n < FPREV_MAX_ITEMS;
             ++i) {
            const A380XFplnLeg *l = &act->pri.el[i];
            if (a380x_fpln_is_disco(l) || !a380x_leg_is_xf(l->type)) continue;
            vals[n] = l->ident;
            map[n++] = i;
        }
    }
    /* With TMPY the dropdown preselects the revised waypoint; a typed fix is shown as an extra value. */
    if (tmpy && rev.valid && !rev.altn && !st->typed[0])
        for (i = 0; i < n; ++i)
            if (map[i] == rev.leg) sel = i;
    if (st->typed[0] && tmpy) {
        vals[n] = st->typed;
        map[n] = -1;
        sel = n++;
    }

    fprev_text_c(ui, 0.0f, R(32), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "DIRECT TO");
    memset(&dd, 0, sizeof(dd));
    dd.values = vals;
    dd.count = n;
    dd.selected = sel;
    dd.free_text = 1;
    dd.digits = 7;
    dd.tmpy = tmpy;
    typed[0] = '\0';
    i = -1;
    if (fp && act && mfd_dropdown(ui, DIR_ID(1), mfd_rect(128.0f, R(10), 175.0f, MFD_DROPDOWN_H), &dd, &i, typed,
                                  sizeof(typed))) {
        if (i >= 0 && i < n && map[i] >= 0) {
            st->typed[0] = '\0';
            dir_do(ctx, fp, map[i], NULL, st->option);
        } else if (typed[0]) {
            A380XNavFix fx[A380X_MFD_DUP_MAX];
            int c = a380x_mfd_fpln_resolve_place(ctx, typed, fx, A380X_MFD_DUP_MAX);
            if (c == 1) {
                snprintf(st->typed, sizeof(st->typed), "%s", fx[0].ident);
                dir_do(ctx, fp, -1, &fx[0], st->option);
            } else if (c > 1) {
                a380x_mfd_fpln_dup_open(ctx->side, FPREV_REQ_DIR, fx, c);
                st->dup_wait = 1;
            }
        }
    } else if (!fp || !act) {
        dd.disabled = 1;
        mfd_dropdown(ui, DIR_ID(1), mfd_rect(128.0f, R(10), 175.0f, MFD_DROPDOWN_H), &dd, &i, typed, sizeof(typed));
    }

    /* UTC / DIST */
    fprev_text_c(ui, 150.0f, R(112), MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "UTC");
    fprev_text_c(ui, 254.0f, R(112), MFD_ANCHOR_RIGHT, 30.0f, vc, "--:--");
    fprev_text_c(ui, 150.0f, R(149), MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "DIST");
    snprintf(t, sizeof(t), "---");
    if (tmp && tmp->active_leg_index >= 0 && tmp->active_leg_index < tmp->pri.count) {
        const A380XFplnLeg *l = &tmp->pri.el[tmp->active_leg_index];
        if ((l->flags & A380X_LEGF_CALC_VALID) && isfinite(l->cum_dist_nm)) snprintf(t, sizeof(t), "%.0f", floor(l->cum_dist_nm));
    }
    fprev_text_c(ui, 254.0f, R(149), MFD_ANCHOR_RIGHT, 30.0f, vc, t);
    fprev_text_c(ui, 260.0f, R(149), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, "NM");

    /* OPTIONS box */
    mfd_draw_bevel(ctx->dl, mfd_rect(384.0f, R(10), 384.0f, 258.0f), 2.0f, MFD_COL_CSS_GREY, MFD_BEVEL_SOLID);
    fprev_text_top(ui, 396.0f, R(17), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "OPTIONS");
    i = mfd_radio_group(ui, DIR_ID(2), 411.0f, R(80), 340.0f, opts, 4, st->option, 0xEu,
                        tmpy ? MFD_COL_YELLOW : MFD_COL_CYAN, 0.0f);
    if (i >= 0) st->option = i;

    /* bottom */
    if (!tmpy) {
        if (mfd_button(ui, DIR_ID(3), mfd_rect(2.0f, FPREV_BOTTOM_Y, 111.0f, MFD_BUTTON_H), "RETURN", 0)) {
            a380x_mfd_fpln_reset_revised(ctx->side);
            mfd_navigate(ctx, "fms/active/f-pln");
        }
    } else {
        MfdButtonDesc bd;
        float h = mfd_button_height(2), y = 948.0f - h;
        mfd_button_desc_init(&bd, "ERASE\nDIR TO*");
        bd.color = MFD_COL_AMBER;
        if (mfd_button_ex(ui, DIR_ID(4), mfd_rect(2.0f, y, 0.0f, h), &bd) == MFD_BUTTON_CLICKED) {
            a380x_mfd_fpln_tmpy_erase(ctx);
            a380x_mfd_fpln_reset_revised(ctx->side);
            mfd_navigate(ctx, "fms/active/f-pln");
        }
        mfd_button_desc_init(&bd, "INSERT\nDIR TO*");
        bd.color = MFD_COL_AMBER;
        if (mfd_button_ex(ui, DIR_ID(5), mfd_rect(766.0f - mfd_button_auto_width("INSERT\nDIR TO*"), y, 0.0f, h), &bd) ==
            MFD_BUTTON_CLICKED) {
            a380x_mfd_fpln_tmpy_insert(ctx);
            a380x_mfd_fpln_reset_revised(ctx->side);
            mfd_navigate(ctx, "fms/active/f-pln");
        }
    }
    if (a380x_mfd_fpln_dup_run(ctx, FPREV_REQ_DIR, &chosen) == 1 && st->dup_wait && fp) {
        snprintf(st->typed, sizeof(st->typed), "%s", chosen.ident);
        dir_do(ctx, fp, -1, &chosen, st->option);
    }
    if (!a380x_mfd_fpln_dup_is_open(ctx->side)) st->dup_wait = 0;
}

/* ======================================================================================== */
/* HOLD (spec 05 section 11)                                                                */
/* ======================================================================================== */

static void hold_page(MfdPageContext *ctx)
{
    MfdFplnHoldState *st = MFD_PAGE_STATE(ctx, MfdFplnHoldState);
    MfdUi *ui = ctx->ui;
    FprevCtx c;
    const A380XFplnLeg *L;
    const A380XFplnHold *h = NULL;
    A380XFplnHold desired, dflt;
    int edited = 0, k, metric = ctx->in ? ctx->in->metric_units : 1;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    MfdButtonDesc bd;
    const char *title = "";
    static const char *const turns[2] = {"LEFT", "RIGHT"};
    static const char *const params[2] = {"TIME", "DIST"};
    MfdColor rc;

    fprev_ctx(ctx, &c);
    L = fprev_rev_leg(&c);
    if (L && !a380x_leg_is_hx(L->type)) L = NULL;
    if (L) {
        h = L->hold_modified.valid ? &L->hold_modified : &L->hold_default;
        if (!h->valid) h = NULL;
    }
    if (h) title = h->type == A380X_HOLD_COMPUTED ? "COMPUTED HOLD AT" NBSP
                   : h->type == A380X_HOLD_PILOT  ? "MODIFIED HOLD AT" NBSP
                                                  : "DATABASE HOLD AT" NBSP; /* [PORT] */
    if (!st->param_init && h) {
        st->param = isfinite(h->dist_nm) && !isfinite(h->time_min) ? 1 : 0;
        st->param_init = 1;
    }
    rc = c.tmpy ? MFD_COL_YELLOW : MFD_COL_CYAN;
    if (h) desired = *h;
    else memset(&desired, 0, sizeof(desired));

    fprev_labelled_box(ctx, mfd_rect(0.0f, R(20), 604.0f, 682.0f), title, L ? L->ident : "",
                       c.sec ? MFD_COL_WHITE : MFD_COL_GREEN);
    fprev_text_top(ui, 16.0f, R(86), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "INBOUND CRS");
    mfd_field_desc_init(&fd, MFD_FMT_INBOUND_COURSE);
    fd.flags = (c.tmpy ? MFD_FIELD_TMPY : 0u) | MFD_FIELD_NOT_CLEARABLE | (h ? 0u : MFD_FIELD_DISABLED);
    memset(&fv, 0, sizeof(fv));
    fv.has_value = h && isfinite(h->inbound_crs_mag);
    fv.num = h ? h->inbound_crs_mag : 0.0;
    if (mfd_input_field(ui, HOLD_ID(1), 91.0f, R(124), &fd, &fv, &out) == MFD_FIELD_COMMITTED && h) {
        desired.inbound_crs_mag = (float)out.num;
        edited = 1;
    }
    fprev_text_top(ui, 16.0f, R(211), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "TURN");
    k = mfd_radio_group(ui, HOLD_ID(2), 91.0f, R(249), 200.0f, turns, 2,
                        h ? (h->turn == A380X_TURN_LEFT ? 0 : 1) : -1, h ? 0u : 3u, rc, 0.0f);
    if (k >= 0 && h) {
        desired.turn = k == 0 ? A380X_TURN_LEFT : A380X_TURN_RIGHT;
        edited = 1;
    }
    fprev_text_top(ui, 16.0f, R(372), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "LEG DEFINING PARAMETER");
    k = mfd_radio_group(ui, HOLD_ID(3), 91.0f, R(410), 130.0f, params, 2, h ? st->param : -1, h ? 0u : 3u, rc, 0.0f);
    if (k >= 0) st->param = k;
    if (st->param == 0) {
        mfd_field_desc_init(&fd, MFD_FMT_HOLD_TIME);
        fd.flags = (c.tmpy ? MFD_FIELD_TMPY : 0u) | MFD_FIELD_NOT_CLEARABLE | (h ? 0u : MFD_FIELD_DISABLED);
        memset(&fv, 0, sizeof(fv));
        fv.has_value = h && isfinite(h->time_min);
        fv.num = fv.has_value ? h->time_min : 0.0;
        if (mfd_input_field(ui, HOLD_ID(4), 235.0f, R(410), &fd, &fv, &out) == MFD_FIELD_COMMITTED && h) {
            desired.time_min = (float)out.num;
            desired.dist_nm = NAN;
            edited = 1;
        }
    } else {
        mfd_field_desc_init(&fd, MFD_FMT_HOLD_DIST);
        fd.flags = (c.tmpy ? MFD_FIELD_TMPY : 0u) | MFD_FIELD_NOT_CLEARABLE | (h ? 0u : MFD_FIELD_DISABLED);
        memset(&fv, 0, sizeof(fv));
        fv.has_value = h && isfinite(h->dist_nm);
        fv.num = fv.has_value ? h->dist_nm : 0.0;
        if (mfd_input_field(ui, HOLD_ID(5), 235.0f, R(447), &fd, &fv, &out) == MFD_FIELD_COMMITTED && h) {
            desired.dist_nm = (float)out.num;
            desired.time_min = NAN;
            edited = 1;
        }
    }
    mfd_hrule(ui, 1.0f, 603.0f, R(533), 1.0f, MFD_COL_LIGHT_GREY);
    fprev_text_top(ui, 16.0f, R(584), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "LAST EXIT (FOR EXTRA FUEL 0 AT ALTN)");
    fprev_text_top(ui, 130.5f, R(632), MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "AT");
    fprev_text_top(ui, 273.5f, R(632), MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "UTC");
    fprev_text_top(ui, 445.0f, R(632), MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "EFOB");
    fprev_text_top(ui, 273.5f, R(660), MFD_ANCHOR_CENTER, 25.0f, c.sec ? MFD_COL_GREEN : MFD_COL_MAGENTA, "--:--");
    fprev_text_top(ui, 445.0f, R(660), MFD_ANCHOR_CENTER, 25.0f, c.sec ? MFD_COL_GREEN : MFD_COL_MAGENTA, "---.-");
    fprev_text_top(ui, 445.0f + mfd_text_width("---.-", 25.0f) * 0.5f + 6.0f, R(664), MFD_ANCHOR_LEFT, 20.0f,
                   MFD_COL_DARK_BLUE, metric ? "T" : "KLB");

    mfd_button_desc_init(&bd, "DATABASE");
    bd.flags = MFD_BTN_DISABLED;
    mfd_button_ex(ui, HOLD_ID(6), mfd_rect(609.0f, R(45), 154.0f, 64.0f), &bd);
    mfd_button_desc_init(&bd, "COMPUTED");
    bd.flags = (L && L->hold_modified.valid) ? 0u : MFD_BTN_DISABLED;
    if (mfd_button_ex(ui, HOLD_ID(7), mfd_rect(609.0f, R(119), 154.0f, 64.0f), &bd) == MFD_BUTTON_CLICKED && L)
        a380x_mfd_fpln_result(ctx, a380x_fpln_revert_hold_to_computed(c.fp, c.edit, c.altn, c.rev.leg));

    if (edited && L) { /* [PORT] modify only on user edits; index/plan 0 accepted */
        int r;
        desired.valid = 1;
        desired.type = A380X_HOLD_PILOT;
        if (L->hold_default.valid) dflt = L->hold_default;
        else {
            memset(&dflt, 0, sizeof(dflt));
            dflt.valid = 1;
            dflt.type = A380X_HOLD_DATABASE;
            dflt.dist_nm = 1.0f;
            dflt.time_min = 1.0f;
            dflt.inbound_crs_mag = 0.0f;
            dflt.turn = A380X_TURN_RIGHT;
        }
        r = a380x_fpln_add_or_edit_hold(c.fp, c.edit, c.altn, c.rev.leg, &desired, &desired, &dflt);
        if (r >= 0) {
            a380x_mfd_fpln_result(ctx, A380X_FPLN_OK);
            a380x_mfd_fpln_set_revised(ctx->side, c.rev.plan, c.altn, r);
        } else {
            a380x_mfd_fpln_result(ctx, r);
        }
    }
    fprev_footer_nav(ctx, fprev_footer(ctx, HOLD_ID(10), HOLD_ID(11), c.tmpy));
}

/* ======================================================================================== */
/* VERT REV (spec 05 section 12)                                                            */
/* ======================================================================================== */

static double fprev_ett_s = NAN;
double a380x_mfd_fpln_ett(void) { return fprev_ett_s; }

static int vrev_list(const FprevCtx *c, int altn, const char **vals, int *map, int max)
{
    const A380XFplnRoute *r;
    int i, n = 0, start;
    if (!c->plan) return 0;
    r = altn ? &c->plan->alt : &c->plan->pri;
    start = altn ? 0 : (c->plan->active_leg_index < 0 ? 0 : c->plan->active_leg_index);
    for (i = start; i < r->count && n < max; ++i) {
        const A380XFplnLeg *l = &r->el[i];
        if (a380x_fpln_is_disco(l) || !a380x_leg_is_xf(l->type)) continue;
        vals[n] = l->ident;
        map[n++] = i;
    }
    return n;
}

static int vrev_eligible(const FprevCtx *c, int altn, int idx)
{
    const A380XFplnRoute *r = altn ? &c->plan->alt : &c->plan->pri;
    if (idx < 0 || idx >= r->count) return 0;
    if (!a380x_fpln_constraint_revision_allowed(&r->el[idx])) return 0;
    return altn || idx >= c->plan->active_leg_index;
}

/* Type line "CLB/DES" + label + waypoint dropdown, centred at x 384. Returns 1 if the waypoint changed. */
static int vrev_wpt_row(MfdPageContext *ctx, MfdFplnVrevState *st, const FprevCtx *c, float tt, const char *label,
                        A380XFplnCstrType type, MfdId id)
{
    const char *vals[FPREV_MAX_ITEMS];
    int map[FPREV_MAX_ITEMS], n = vrev_list(c, st->wpt_altn, vals, map, FPREV_MAX_ITEMS), sel = -1, i, k = -1;
    const char *ts = type == A380X_CSTR_CLB ? "CLB" : type == A380X_CSTR_DES ? "DES" : "";
    float w = mfd_text_width(ts, 28.0f) + (ts[0] ? 10.0f : 0.0f) + mfd_text_width(label, 24.0f) + 175.0f;
    float x = 384.0f - w * 0.5f, cy = tt + 87.0f;
    MfdDropdownDesc dd;
    char typed[8];
    for (i = 0; i < n; ++i)
        if (map[i] == st->wpt) sel = i;
    if (ts[0]) {
        fprev_text_c(ctx->ui, x, cy, MFD_ANCHOR_LEFT, 28.0f, MFD_COL_GREEN, ts);
        x += mfd_text_width(ts, 28.0f) + 10.0f;
    }
    fprev_text_c(ctx->ui, x, cy, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, label);
    x += mfd_text_width(label, 24.0f);
    memset(&dd, 0, sizeof(dd));
    dd.values = vals;
    dd.count = n;
    dd.selected = sel;
    dd.digits = 7;
    dd.tmpy = c->tmpy;
    if (mfd_dropdown(ctx->ui, id, mfd_rect(x, tt + 65.0f, 175.0f, MFD_DROPDOWN_H), &dd, &k, typed, sizeof(typed)) && k >= 0 &&
        k < n) {
        st->wpt = map[k];
        return 1;
    }
    return 0;
}

static void vrev_delete_button(MfdPageContext *ctx, MfdId id, float x, float y, const char *l2, int enabled, int *clicked)
{
    *clicked = a380x_mfd_fpln_star_button(ctx->ui, id, mfd_rect(x, y, 170.0f, 0.0f), "DELETE", l2, MFD_COL_AMBER,
                                          !enabled);
}

static void vrev_spd_tab(MfdPageContext *ctx, MfdFplnVrevState *st, const FprevCtx *c, float tt)
{
    MfdUi *ui = ctx->ui;
    const A380XFplnRoute *r = st->wpt_altn ? &c->plan->alt : &c->plan->pri;
    const A380XFplnLeg *L = (st->wpt >= 0 && st->wpt < r->count) ? &r->el[st->wpt] : NULL;
    int ok = L && vrev_eligible(c, st->wpt_altn, st->wpt), k, del;
    A380XFplnCstrType type = L ? a380x_fpln_cstr_type_for(L) : A380X_CSTR_UNKNOWN;
    A380XFplnSpdCstr sc = L ? a380x_fpln_eff_spd(L) : (A380XFplnSpdCstr){0, NAN};
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    static const char *const cd[2] = {"CLB CSTR", "DES CSTR"};
    char msg[48];
    int is_des, des_lim;
    A380XFmsValue lim, lim_alt;
    if (L && !ok) {
        snprintf(msg, sizeof(msg), "SPD CSTR NOT ALLOWED AT %s", L->ident);
        fprev_text_top(ui, 384.0f, tt + 15.0f, MFD_ANCHOR_CENTER, 28.0f, MFD_COL_AMBER, msg);
    }
    if (vrev_wpt_row(ctx, st, c, tt, "SPD CSTR AT" NBSP, type, VREV_ID(20))) st->cstr_type_sel = 0;
    /* constraint line */
    mfd_field_desc_init(&fd, MFD_FMT_SPEED_KT);
    fd.fmt.min = 90.0;
    fd.fmt.max = 340.0;
    fd.flags = (c->tmpy ? MFD_FIELD_TMPY : 0u) | (ok ? 0u : MFD_FIELD_DISABLED);
    memset(&fv, 0, sizeof(fv));
    fv.has_value = ok && sc.desc != 0 && isfinite(sc.speed) && sc.speed > 2.0f;
    fv.num = fv.has_value ? sc.speed : 0.0;
    is_des = type == A380X_CSTR_DES || (type == A380X_CSTR_UNKNOWN && st->cstr_type_sel == 2);
    switch (mfd_input_field(ui, VREV_ID(21), 24.0f, tt + 159.0f, &fd, &fv, &out)) {
    case MFD_FIELD_COMMITTED:
        if (ok && (type != A380X_CSTR_UNKNOWN || st->cstr_type_sel))
            a380x_mfd_fpln_result(ctx, a380x_fpln_set_spd_cstr(c->fp, c->edit, st->wpt_altn, st->wpt, is_des, (float)out.num));
        break;
    case MFD_FIELD_CLEARED:
        if (ok) a380x_mfd_fpln_result(ctx, a380x_fpln_set_spd_cstr(c->fp, c->edit, st->wpt_altn, st->wpt, is_des, 0.0f));
        break;
    default:
        break;
    }
    if (ok && type == A380X_CSTR_UNKNOWN) {
        k = mfd_radio_group(ui, VREV_ID(22), 260.0f, tt + 159.0f, 220.0f, cd, 2, st->cstr_type_sel - 1, 0u,
                            MFD_COL_AMBER, 0.0f);
        if (k >= 0) st->cstr_type_sel = k + 1;
    }
    vrev_delete_button(ctx, VREV_ID(23), 570.0f, tt + 159.0f, "SPD CSTR", ok && sc.desc != 0, &del);
    if (del) a380x_mfd_fpln_result(ctx, a380x_fpln_set_spd_cstr(c->fp, c->edit, st->wpt_altn, st->wpt, is_des, 0.0f));
    mfd_draw_fill_rect(ctx->dl, mfd_rect(14.0f, tt + 302.0f, 740.0f, 2.0f), MFD_COL_LIGHT_GREY);
    /* speed limit (display only: the FMS has no speed-limit entry yet) */
    des_lim = type == A380X_CSTR_DES ||
              (c->lplan <= A380X_PLAN_TEMPORARY && a380x_mfd_fpln_phase(ctx) > A380X_FMS_PHASE_CRUISE &&
               a380x_mfd_fpln_phase(ctx) < A380X_FMS_PHASE_GOAROUND);
    fprev_text_top(ui, 24.0f, tt + 324.0f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, des_lim ? "DES SPD LIMIT" : "CLB SPD LIMIT");
    memset(&lim, 0, sizeof(lim));
    memset(&lim_alt, 0, sizeof(lim_alt));
    if (ctx->fms && c->lplan >= 0 && a380x_fms_plan_exists(ctx->fms, c->lplan)) {
        lim = a380x_fms_get(ctx->fms, c->lplan, des_lim ? A380X_FMS_F_DES_SPD_LIM : A380X_FMS_F_CLB_SPD_LIM);
        lim_alt = a380x_fms_get(ctx->fms, c->lplan, des_lim ? A380X_FMS_F_DES_SPD_LIM_ALT : A380X_FMS_F_CLB_SPD_LIM_ALT);
    }
    mfd_field_desc_init(&fd, MFD_FMT_SPEED_KT);
    fd.flags = MFD_FIELD_DISABLED | (c->tmpy ? MFD_FIELD_TMPY : 0u);
    memset(&fv, 0, sizeof(fv));
    fv.has_value = lim.has;
    fv.is_computed = !lim.entered;
    fv.num = lim.v;
    mfd_input_field(ui, VREV_ID(24), 24.0f, tt + 375.0f, &fd, &fv, &out);
    fprev_text_c(ui, 300.0f, tt + 393.5f, MFD_ANCHOR_CENTER, 24.0f, MFD_COL_WHITE, "AT OR BELOW");
    mfd_field_desc_init(&fd, MFD_FMT_ALT_OR_FL);
    fd.flags = MFD_FIELD_DISABLED | (c->tmpy ? MFD_FIELD_TMPY : 0u);
    memset(&fv, 0, sizeof(fv));
    fv.has_value = lim_alt.has;
    fv.is_computed = !lim_alt.entered;
    fv.num = lim_alt.v;
    mfd_input_field(ui, VREV_ID(25), 400.0f, tt + 375.0f, &fd, &fv, &out);
    vrev_delete_button(ctx, VREV_ID(26), 570.0f, tt + 368.0f, "SPD LIMIT", 0, &del);
}

static void vrev_alt_tab(MfdPageContext *ctx, MfdFplnVrevState *st, const FprevCtx *c, float tt)
{
    MfdUi *ui = ctx->ui;
    const A380XFplnRoute *r = st->wpt_altn ? &c->plan->alt : &c->plan->pri;
    const A380XFplnLeg *L = (st->wpt >= 0 && st->wpt < r->count) ? &r->el[st->wpt] : NULL;
    int ok = L && vrev_eligible(c, st->wpt_altn, st->wpt), k, del, opt, is_des;
    A380XFplnCstrType type = L ? a380x_fpln_cstr_type_for(L) : A380X_CSTR_UNKNOWN;
    A380XFplnAltCstr ac;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    static const char *const opts[3] = {"AT", "AT OR ABOVE", "AT OR BELOW"};
    static const char *const cd[2] = {"CLB CSTR", "DES CSTR"};
    char msg[48];
    double ta, tl;
    float lx = 24.0f, cx = 330.0f;
    if (L) ac = a380x_fpln_eff_alt(L);
    else {
        ac.desc = 0;
        ac.alt1_ft = ac.alt2_ft = NAN;
    }
    a380x_mfd_fpln_trans(ctx, c->lplan, &ta, &tl);
    msg[0] = '\0';
    if (L && !ok) snprintf(msg, sizeof(msg), "ALT CSTR NOT ALLOWED AT %s", L->ident);
    else if (L && ((L->flags & A380X_LEGF_TOO_STEEP_AFTER) ||
                   (st->wpt > 0 && (r->el[st->wpt - 1].flags & A380X_LEGF_TOO_STEEP_AFTER))))
        snprintf(msg, sizeof(msg), "TOO STEEP PATH AHEAD");
    if (msg[0]) fprev_text_top(ui, 384.0f, tt + 15.0f, MFD_ANCHOR_CENTER, 28.0f, MFD_COL_AMBER, msg);
    if (vrev_wpt_row(ctx, st, c, tt, "ALT CSTR AT" NBSP, type, VREV_ID(30))) {
        st->cstr_type_sel = 0;
        st->alt_option = -1;
    }
    /* preselection from the effective descriptor */
    opt = st->alt_option;
    if (opt < 0) {
        switch (ac.desc) {
        case '@': case 'I': case 'X': opt = 0; break;
        case '+': case 'J': case 'V': opt = 1; break;
        case '-': case 'Y': opt = 2; break;
        default: opt = -1; break;
        }
    }
    k = mfd_radio_group(ui, VREV_ID(31), lx, tt + 159.0f, 250.0f, opts, 3, ok ? opt : -1, ok ? 0u : 7u,
                        c->tmpy ? MFD_COL_YELLOW : MFD_COL_CYAN, 0.0f);
    if (k >= 0) st->alt_option = opt = k;
    mfd_draw_fill_rect(ctx->dl, mfd_rect(lx + 275.0f, tt + 159.0f, 2.0f, 140.0f), MFD_COL_LIGHT_GREY);
    if (ac.desc == 'B') fprev_text_top(ui, lx, tt + 159.0f + 117.0f + 20.0f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "WINDOW");
    is_des = type == A380X_CSTR_DES || (type == A380X_CSTR_UNKNOWN && st->cstr_type_sel == 2);
    if (ok && opt >= 0) {
        mfd_field_desc_init(&fd, MFD_FMT_ALT_OR_FL);
        fd.fmt.min = 0.0;
        fd.fmt.max = 43000.0;
        fd.fmt.has_trans = is_des ? isfinite(tl) : isfinite(ta);
        fd.fmt.trans = is_des ? tl / 100.0 : ta;
        fd.fmt.trans_is_fl = is_des;
        fd.flags = c->tmpy ? MFD_FIELD_TMPY : 0u;
        memset(&fv, 0, sizeof(fv));
        fv.has_value = ac.desc != 0 && isfinite(ac.alt1_ft);
        fv.num = fv.has_value ? ac.alt1_ft : 0.0;
        switch (mfd_input_field(ui, VREV_ID(32), cx, tt + 200.0f, &fd, &fv, &out)) {
        case MFD_FIELD_COMMITTED:
            if (type != A380X_CSTR_UNKNOWN || st->cstr_type_sel) {
                A380XFplnAltCstr nc;
                nc.desc = opt == 0 ? '@' : opt == 1 ? '+' : '-';
                nc.alt1_ft = (float)out.num;
                nc.alt2_ft = NAN;
                a380x_mfd_fpln_result(ctx, a380x_fpln_set_alt_cstr(c->fp, c->edit, st->wpt_altn, st->wpt, is_des, &nc));
                st->alt_option = -1;
            }
            break;
        case MFD_FIELD_CLEARED:
            a380x_mfd_fpln_result(ctx, a380x_fpln_set_alt_cstr(c->fp, c->edit, st->wpt_altn, st->wpt, is_des, NULL));
            st->alt_option = -1;
            break;
        default:
            break;
        }
    }
    if (ac.desc == 'B' && isfinite(ac.alt1_ft) && isfinite(ac.alt2_ft)) {
        /* window: alt2 (lower) - alt1, FL when above the transition */
        double tr = is_des ? tl : ta;
        int fl1 = isfinite(tr) && (is_des ? ac.alt1_ft >= tr : ac.alt1_ft > tr);
        int fl2 = isfinite(tr) && (is_des ? ac.alt2_ft >= tr : ac.alt2_ft > tr);
        char w[40];
        float x = cx;
        snprintf(w, sizeof(w), fl2 ? "%03.0f-" : "%.0f-", fl2 ? floor(ac.alt2_ft / 100.0 + 0.5) : (double)ac.alt2_ft);
        snprintf(w + strlen(w), sizeof(w) - strlen(w), fl1 ? "%03.0f" : "%.0f",
                 fl1 ? floor(ac.alt1_ft / 100.0 + 0.5) : (double)ac.alt1_ft);
        if (fl1 && !fl2) {
            fprev_text_top(ui, x, tt + 262.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, "FT");
            x += mfd_text_width("FT", 20.0f) + 6.0f;
        }
        fprev_text_top(ui, x, tt + 258.0f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_GREEN, w);
        fprev_text_top(ui, x + mfd_text_width(w, 24.0f) + 6.0f, tt + 262.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE,
                       fl1 ? "FL" : "FT");
    }
    vrev_delete_button(ctx, VREV_ID(33), 570.0f, tt + 159.0f, "ALT CSTR", ok && ac.desc != 0, &del);
    if (del) {
        a380x_mfd_fpln_result(ctx, a380x_fpln_set_alt_cstr(c->fp, c->edit, st->wpt_altn, st->wpt, is_des, NULL));
        st->alt_option = -1;
    }
    if (ok && type == A380X_CSTR_UNKNOWN) {
        k = mfd_radio_group(ui, VREV_ID(34), 560.0f, tt + 241.0f, 190.0f, cd, 2, st->cstr_type_sel - 1, 0u,
                            MFD_COL_AMBER, 0.0f);
        if (k >= 0) st->cstr_type_sel = k + 1;
    }
    mfd_draw_fill_rect(ctx->dl, mfd_rect(14.0f, tt + 320.0f, 740.0f, 2.0f), MFD_COL_LIGHT_GREY);
}

static void vrev_rta_tab(MfdPageContext *ctx, float tt)
{
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    float lw = mfd_text_width("ETT", 20.0f), x;
    fprev_text_top(ctx->ui, 384.0f, tt + 40.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "NOT IMPLEMENTED");
    x = 384.0f - (lw + 15.0f + 175.0f) * 0.5f;
    fprev_text_c(ctx->ui, x, tt + 98.5f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "ETT");
    mfd_field_desc_init(&fd, MFD_FMT_TIME_HHMMSS);
    fd.width = 175.0f;
    memset(&fv, 0, sizeof(fv));
    fv.has_value = isfinite(fprev_ett_s);
    fv.num = fv.has_value ? fprev_ett_s : 0.0;
    switch (mfd_input_field(ctx->ui, VREV_ID(40), x + lw + 15.0f, tt + 80.0f, &fd, &fv, &out)) {
    case MFD_FIELD_COMMITTED: fprev_ett_s = out.num; break;
    case MFD_FIELD_CLEARED: fprev_ett_s = NAN; break;
    default: break;
    }
}

/* STEP ALTs ---------------------------------------------------------------------------------- */

typedef struct { int leg; float alt; int ignored; float dist; } VrevStep;

static int vrev_steps(const A380XFplnPlan *p, VrevStep *out, int max)
{
    int i, n = 0;
    for (i = 0; i < p->pri.count && n < max; ++i) {
        const A380XFplnLeg *l = &p->pri.el[i];
        if (a380x_fpln_is_disco(l) || !l->cruise_step.valid) continue;
        out[n].leg = i;
        out[n].alt = l->cruise_step.to_alt_ft;
        out[n].ignored = l->cruise_step.ignored;
        out[n].dist = l->pred.valid ? l->pred.dist_from_aircraft_nm : NAN;
        n++;
    }
    return n;
}

/* checkStepInsertionRules over the whole chain with (leg, alt) inserted (replacing `skip_leg`). */
static int vrev_step_ok(const VrevStep *s, int n, int skip_leg, int leg, float alt, double crz_ft)
{
    double prev = crz_ft;
    int i, descended = 0, done = 0;
    for (i = 0; i <= n; ++i) {
        double a;
        if (i < n && s[i].leg == skip_leg) continue;
        if (!done && (i == n || s[i].leg > leg)) {
            a = alt;
            done = 1;
            --i; /* re-visit s[i] after the inserted step */
        } else if (i < n) {
            a = s[i].alt;
        } else {
            break;
        }
        if (fabs(a - prev) < 1000.0) return 0;
        if (a > prev && descended) return 0;
        if (a < prev) descended = 1;
        prev = a;
    }
    return 1;
}

static void vrev_step_add(MfdPageContext *ctx, MfdFplnVrevState *st, const FprevCtx *c, const VrevStep *s, int n,
                          int old_leg, int leg, float alt, double crz_ft, double rec_max_fl)
{
    if (!vrev_step_ok(s, n, old_leg, leg, alt, crz_ft)) {
        st->step_reject_leg = leg;
        return;
    }
    st->step_reject_leg = -1;
    if (old_leg >= 0) a380x_fpln_remove_cruise_step(c->fp, c->edit, old_leg);
    if (a380x_mfd_fpln_result(ctx, a380x_fpln_add_cruise_step(c->fp, c->edit, leg, alt)) && isfinite(rec_max_fl) &&
        alt > rec_max_fl * 100.0 && ctx->msgs)
        mfd_msg_add(ctx->msgs, MFD_MSG_STEP_ABOVE_MAX_FL, NULL, NULL, NULL, NULL, NULL);
}

static void vrev_step_tab(MfdPageContext *ctx, MfdFplnVrevState *st, const FprevCtx *c, MfdRect content, float tt)
{
    MfdUi *ui = ctx->ui;
    const char *msg = NULL;
    int phase = a380x_mfd_fpln_phase(ctx), aoc = c->lplan <= A380X_PLAN_TEMPORARY, i, nsteps, row;
    A380XFmsValue crz;
    double crz_ft, rec_max = NAN, ta, tl;
    VrevStep steps[24];
    const char *vals[FPREV_MAX_ITEMS];
    int map[FPREV_MAX_ITEMS], nv;
    static const float CX[5] = {30.0f, 278.0f, 455.0f, 597.0f, 739.0f};
    char t[48];
    memset(&crz, 0, sizeof(crz));
    if (ctx->fms && c->lplan >= 0 && a380x_fms_plan_exists(ctx->fms, c->lplan)) {
        A380XFmsValue rm = a380x_fms_get(ctx->fms, c->lplan, A380X_FMS_F_REC_MAX_FL);
        crz = a380x_fms_get(ctx->fms, c->lplan, A380X_FMS_F_CRZ_FL);
        if (rm.has) rec_max = rm.v;
    }
    if (ctx->fms && a380x_fms_engine_out(ctx->fms)) msg = "STEP ALTs NOT ALLOWED IN EO";
    else if (aoc && phase == A380X_FMS_PHASE_DESCENT) msg = "STEP ALTs NOT ALLOWED IN DES";
    else if (aoc && phase == A380X_FMS_PHASE_APPROACH) msg = "STEP ALTs NOT ALLOWED IN APPR";
    else if (aoc && phase == A380X_FMS_PHASE_GOAROUND) msg = "STEP ALTs NOT ALLOWED IN GA";
    else if (aoc && phase == A380X_FMS_PHASE_DONE) msg = "STEP ALTs NOT ALLOWED IN DONE";
    else if (!crz.has) msg = "STEP ALTs NOT ALLOWED: NO CRZ FL";
    if (msg) {
        fprev_text_c(ui, 384.0f, content.y + content.h * 0.5f, MFD_ANCHOR_CENTER, 24.0f, MFD_COL_AMBER, msg);
        return;
    }
    crz_ft = crz.v * 100.0;
    a380x_mfd_fpln_trans(ctx, c->lplan, &ta, &tl);
    nsteps = vrev_steps(c->plan, steps, 24);
    nv = vrev_list(c, 0, vals, map, FPREV_MAX_ITEMS);

    /* labelled box with the cruise FL */
    {
        float x = 40.0f;
        mfd_draw_bevel(ctx->dl, mfd_rect(14.0f, tt + 20.0f, 740.0f, 400.0f), 1.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_SOLID);
        snprintf(t, sizeof(t), "%03.0f", crz.v);
        mfd_draw_fill_rect(ctx->dl, mfd_rect(36.0f, tt + 8.0f, 360.0f, 24.0f), MFD_COL_BLACK);
        fprev_text_c(ui, x, tt + 20.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "STEP ALTs FROM CRZ");
        x += mfd_text_width("STEP ALTs FROM CRZ", 20.0f) + 15.0f;
        fprev_text_c(ui, x, tt + 20.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, "FL");
        x += mfd_text_width("FL", 20.0f) + 4.0f;
        fprev_text_c(ui, x, tt + 20.0f, MFD_ANCHOR_LEFT, 25.0f, c->vcol, t);
    }
    /* header */
    fprev_text_c(ui, CX[0] + 60.0f, tt + 76.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "WPT");
    fprev_text_c(ui, CX[1] + 25.0f, tt + 76.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "ALT");
    fprev_text_c(ui, (CX[2] + CX[3]) * 0.5f, tt + 76.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "DIST");
    fprev_text_c(ui, (CX[3] + CX[4]) * 0.5f, tt + 76.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE,
                 phase == A380X_FMS_PHASE_PREFLIGHT ? "TIME" : "UTC");
    mfd_draw_fill_rect(ctx->dl, mfd_rect(CX[0], tt + 96.0f, CX[4] - CX[0], 1.0f), MFD_COL_GREY);
    mfd_draw_fill_rect(ctx->dl, mfd_rect(CX[2], tt + 36.0f, 1.0f, 360.0f), MFD_COL_GREY);
    if (st->step_start > nsteps) st->step_start = nsteps;
    for (row = 0; row < 5; ++row) {
        int si = st->step_start + row, is_new = si == nsteps, sel = -1, k = -1;
        float y = tt + 96.0f + 60.0f * (float)row;
        MfdDropdownDesc dd;
        MfdFieldDesc fd;
        MfdFieldValue fv;
        MfdValue out;
        char typed[8];
        int wpt_leg;
        if (si > nsteps) break;
        wpt_leg = is_new ? st->step_new_wpt : steps[si].leg;
        for (i = 0; i < nv; ++i)
            if (map[i] == wpt_leg) sel = i;
        memset(&dd, 0, sizeof(dd));
        dd.values = vals;
        dd.count = nv;
        dd.selected = sel;
        dd.digits = 7;
        dd.tmpy = c->tmpy;
        if (mfd_dropdown(ui, VREV_ID(60 + row * 2), mfd_rect(66.5f, y + 8.0f, 175.0f, MFD_DROPDOWN_H), &dd, &k, typed,
                         sizeof(typed)) && k >= 0 && k < nv) {
            if (is_new) st->step_new_wpt = map[k];
            else vrev_step_add(ctx, st, c, steps, nsteps, steps[si].leg, map[k], steps[si].alt, crz_ft, rec_max);
        }
        mfd_field_desc_init(&fd, MFD_FMT_ALT_OR_FL);
        fd.width = 150.0f;
        fd.fmt.min = 0.0;
        fd.fmt.max = 43000.0;
        fd.fmt.has_trans = isfinite(ta);
        fd.fmt.trans = ta;
        fd.flags = c->tmpy ? MFD_FIELD_TMPY : 0u;
        memset(&fv, 0, sizeof(fv));
        fv.has_value = !is_new;
        fv.num = is_new ? 0.0 : steps[si].alt;
        switch (mfd_input_field(ui, VREV_ID(61 + row * 2), 291.5f, y + 11.5f, &fd, &fv, &out)) {
        case MFD_FIELD_COMMITTED:
            if (is_new && st->step_new_wpt >= 0) {
                vrev_step_add(ctx, st, c, steps, nsteps, -1, st->step_new_wpt, (float)out.num, crz_ft, rec_max);
                st->step_new_wpt = -1;
            } else if (!is_new) {
                vrev_step_add(ctx, st, c, steps, nsteps, steps[si].leg, steps[si].leg, (float)out.num, crz_ft, rec_max);
            }
            break;
        case MFD_FIELD_CLEARED:
            if (!is_new) a380x_mfd_fpln_result(ctx, a380x_fpln_remove_cruise_step(c->fp, c->edit, steps[si].leg));
            break;
        default:
            break;
        }
        if (!is_new) {
            const char *m = steps[si].ignored ? "IGNORED"
                            : (isfinite(rec_max) && steps[si].alt > rec_max * 100.0) ? "ABOVE MAX FL" : NULL;
            if (m) {
                fprev_text_c(ui, (CX[2] + CX[4]) * 0.5f, y + 30.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, m);
            } else {
                if (isfinite(steps[si].dist)) snprintf(t, sizeof(t), "%.0f", floor(steps[si].dist + 0.5));
                else snprintf(t, sizeof(t), "---");
                fprev_text_c(ui, CX[3] - 34.0f, y + 30.0f, MFD_ANCHOR_RIGHT, 25.0f, c->vcol, t);
                fprev_text_c(ui, CX[3] - 30.0f, y + 30.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, "NM");
                fprev_text_c(ui, (CX[3] + CX[4]) * 0.5f, y + 30.0f, MFD_ANCHOR_CENTER, 25.0f, c->vcol, "--:--");
            }
        }
    }
    if (st->step_reject_leg >= 0 && st->step_reject_leg < c->plan->pri.count) {
        snprintf(t, sizeof(t), "STEP NOT ALLOWED AT %s", c->plan->pri.el[st->step_reject_leg].ident);
        fprev_text_c(ui, 384.0f, tt + 440.0f, MFD_ANCHOR_CENTER, 28.0f, MFD_COL_AMBER, t);
    }
    if (mfd_icon_button(ui, VREV_ID(50), mfd_rect(324.0f, tt + 465.0f, 50.0f, 50.0f), MFD_ICON_DOUBLE_DOWN,
                        nsteps - st->step_start <= 4))
        st->step_start++;
    if (mfd_icon_button(ui, VREV_ID(51), mfd_rect(394.0f, tt + 465.0f, 50.0f, 50.0f), MFD_ICON_DOUBLE_UP,
                        st->step_start == 0))
        st->step_start--;
    /* OPTIMUM STEP POINT (static [FBW-NA]) */
    mfd_draw_bevel(ctx->dl, mfd_rect(14.0f, tt + 545.0f, 740.0f, 90.0f), 1.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_SOLID);
    mfd_draw_fill_rect(ctx->dl, mfd_rect(36.0f, tt + 533.0f, mfd_text_width("OPTIMUM STEP POINT", 20.0f) + 8.0f, 24.0f),
                       MFD_COL_BLACK);
    fprev_text_c(ui, 40.0f, tt + 545.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "OPTIMUM STEP POINT");
    fprev_text_c(ui, 40.0f, tt + 580.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "TO" NBSP);
    fprev_text_c(ui, 40.0f + mfd_text_width("TO" NBSP, 20.0f), tt + 580.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, "FL");
    fprev_text_c(ui, 40.0f + mfd_text_width("TO" NBSP "FL", 20.0f) + 4.0f, tt + 580.0f, MFD_ANCHOR_LEFT, 25.0f, c->vcol,
                 "---");
    fprev_text_c(ui, 384.0f, tt + 612.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "NO OPTIMUM STEP FOUND");
}

static void vrev_page(MfdPageContext *ctx)
{
    MfdFplnVrevState *st = MFD_PAGE_STATE(ctx, MfdFplnVrevState);
    static const char *const titles[5] = {"RTA", "SPD", "CMS", "ALT", "STEP ALTs"};
    MfdTabsDesc td;
    MfdRect content, clip;
    FprevCtx c;
    int k;
    float tt;

    fprev_ctx(ctx, &c);
    if (ctx->entered) {
        const char *x = ctx->uri->extra;
        st->tab = !strcmp(x, "rta") ? 0 : !strcmp(x, "spd") ? 1 : !strcmp(x, "cms") ? 2 : !strcmp(x, "step-alts") ? 4 : 3;
        st->wpt_altn = (st->tab == 1 || st->tab == 3) && c.rev.valid ? c.rev.altn : 0;
        st->wpt = (st->tab == 1 || st->tab == 3) && c.rev.valid ? c.rev.leg : -1;
        st->alt_option = -1;
        st->step_new_wpt = -1;
        st->step_reject_leg = -1;
    }
    if (c.plan) {
        /* selection no longer available -> first entry, reset the unsaved states */
        const char *vals[FPREV_MAX_ITEMS];
        int map[FPREV_MAX_ITEMS], n = vrev_list(&c, st->wpt_altn, vals, map, FPREV_MAX_ITEMS), i, found = 0;
        for (i = 0; i < n; ++i)
            if (map[i] == st->wpt) found = 1;
        if (!found) {
            st->wpt = n > 0 ? map[0] : -1;
            st->cstr_type_sel = 0;
            st->alt_option = -1;
        }
    }
    mfd_tabs_desc_init(&td);
    td.content_h = FPREV_BOTTOM_Y - 8.0f - (R(15) + 2.0f + 36.0f);
    k = mfd_top_tabs(ctx->ui, VREV_ID(1), mfd_rect(0.0f, R(15), 768.0f, FPREV_BOTTOM_Y - 4.0f - R(15)), titles, 5,
                     st->tab, &td, &content, &clip);
    if (k >= 0 && k != st->tab) {
        st->tab = k;
        st->alt_option = -1;
        st->cstr_type_sel = 0;
    }
    tt = content.y;
    mfd_draw_clip_push(ctx->dl, clip);
    if (!c.plan) {
        /* nothing to revise */
    } else if (st->tab == 0) {
        vrev_rta_tab(ctx, tt);
    } else if (st->tab == 1) {
        vrev_spd_tab(ctx, st, &c, tt);
    } else if (st->tab == 2) {
        fprev_text_c(ctx->ui, 384.0f, content.y + content.h * 0.5f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE,
                     "NOT IMPLEMENTED");
    } else if (st->tab == 3) {
        vrev_alt_tab(ctx, st, &c, tt);
    } else {
        vrev_step_tab(ctx, st, &c, content, tt);
    }
    mfd_draw_clip_pop(ctx->dl);
    fprev_footer_nav(ctx, fprev_footer(ctx, VREV_ID(10), VREV_ID(11), c.tmpy));
}

/* ======================================================================================== */
/* FIX INFO (spec 05 section 13)                                                            */
/* ======================================================================================== */

static void fix_set(MfdPageContext *ctx, A380XFlightPlans *fp, int tab, const A380XNavFix *f)
{
    A380XFplnFixInfo fi;
    memset(&fi, 0, sizeof(fi));
    fi.valid = 1;
    fi.fix = *f;
    a380x_mfd_fpln_result(ctx, a380x_fpln_set_fix_info(fp, tab + 1, &fi));
}

static void fix_page(MfdPageContext *ctx)
{
    MfdFplnFixState *st = MFD_PAGE_STATE(ctx, MfdFplnFixState);
    MfdUi *ui = ctx->ui;
    static const char *const titles[4] = {"FIX 1", "FIX 2", "FIX 3", "FIX 4"};
    A380XFlightPlans *fp = a380x_mfd_fpln_store(ctx);
    const A380XFplnPlan *act = fp ? a380x_fpln_plan(fp, A380X_PLAN_ACTIVE) : NULL;
    const A380XFplnFixInfo *fi;
    int tmpy = fp && a380x_fpln_has_tmpy(fp), k, i;
    MfdColor vc = tmpy ? MFD_COL_YELLOW : MFD_COL_GREEN;
    MfdTabsDesc td;
    MfdRect content, clip;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    MfdFieldResult fr;
    A380XNavFix chosen;
    float x, y0 = R(167);
    static const float RH[4] = {88.0f, 184.0f, 112.0f, 184.0f};

    mfd_tabs_desc_init(&td);
    td.content_h = FPREV_BOTTOM_Y - 6.0f - (R(14) + 2.0f + 36.0f); /* 720 in FBW; re-based on the 812 px body */
    k = mfd_top_tabs(ui, FIX_ID(1), mfd_rect(0.0f, R(14), 768.0f, FPREV_BOTTOM_Y - 4.0f - R(14)), titles, 4, st->tab, &td,
                     &content, &clip);
    if (k >= 0) st->tab = k;
    fi = act ? &act->fix_info[st->tab] : NULL;
    if (fi && !fi->valid) fi = NULL;
    mfd_draw_clip_push(ctx->dl, clip);

    fprev_text_top(ui, 52.0f, R(86), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "REF IDENT");
    x = 52.0f + mfd_text_width("REF IDENT", 20.0f) + 19.0f;
    mfd_field_desc_init(&fd, MFD_FMT_TXT_FIX);
    fd.flags = (tmpy ? MFD_FIELD_TMPY : 0u) | (act ? 0u : MFD_FIELD_DISABLED);
    memset(&fv, 0, sizeof(fv));
    fv.has_value = fi != NULL;
    fv.text = fi ? fi->fix.ident : NULL;
    fr = mfd_input_field(ui, FIX_ID(2), x, R(78), &fd, &fv, &out);
    if (fr == MFD_FIELD_CLEARED && act) {
        a380x_fpln_set_fix_info(fp, st->tab + 1, NULL);
    } else if (fr == MFD_FIELD_COMMITTED && act && out.str[0]) {
        A380XNavFix fx[A380X_MFD_DUP_MAX];
        int n = a380x_mfd_fpln_resolve_place(ctx, out.str, fx, A380X_MFD_DUP_MAX);
        if (n == 1) fix_set(ctx, fp, st->tab, &fx[0]);
        else if (n > 1) {
            a380x_mfd_fpln_dup_open(ctx->side, FPREV_REQ_FIX, fx, n);
            st->dup_wait = 1;
        }
    }

    /* table */
    {
        float y = y0;
        for (i = 0; i < 4; ++i) {
            if (i > 0) mfd_draw_fill_rect(ctx->dl, mfd_rect(14.0f, y - 1.0f, 725.0f, 2.0f), MFD_COL_DARK_GREY);
            y += RH[i];
        }
        mfd_draw_fill_rect(ctx->dl, mfd_rect(172.0f, y0, 2.0f, y - y0), MFD_COL_DARK_GREY);
    }
    fprev_text_c(ui, 456.5f, y0 + 26.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "F-PLN INTERCEPT");
    fprev_text_c(ui, 254.0f, y0 + 62.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "UTC");
    fprev_text_c(ui, 384.0f, y0 + 62.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "DIST");
    fprev_text_c(ui, 505.0f, y0 + 62.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "ALT");
    /* radials */
    {
        float ry = y0 + RH[0];
        fprev_text_top(ui, 37.0f, ry + 14.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "RADIAL");
        for (i = 0; i < 2; ++i) {
            float fy = ry + 50.0f + 70.0f * (float)i;
            int dis = !fi || (i == 1 && fi->radial_count < 1);
            mfd_field_desc_init(&fd, MFD_FMT_RADIAL);
            fd.width = 106.0f;
            fd.flags = (tmpy ? MFD_FIELD_TMPY : 0u) | (dis ? MFD_FIELD_DISABLED : 0u);
            memset(&fv, 0, sizeof(fv));
            fv.has_value = fi && fi->radial_count > i;
            fv.num = fv.has_value ? fi->radial_mag[i] : 0.0;
            fr = mfd_input_field(ui, FIX_ID(10 + i), 37.0f, fy, &fd, &fv, &out);
            if (fi && fr != MFD_FIELD_NONE) {
                A380XFplnFixInfo n = *fi;
                if (fr == MFD_FIELD_COMMITTED) {
                    int slot = i < n.radial_count ? i : n.radial_count;
                    double var = a380x_nav_magvar(fp->nav, fi->fix.lat, fi->fix.lon);
                    n.radial_mag[slot] = (float)out.num;
                    n.radial_true[slot] = (float)fmod(out.num + var + 360.0, 360.0);
                    if (slot >= n.radial_count) n.radial_count = slot + 1;
                } else if (i < n.radial_count) {
                    if (i == 0 && n.radial_count == 2) {
                        n.radial_mag[0] = n.radial_mag[1];
                        n.radial_true[0] = n.radial_true[1];
                    }
                    n.radial_count--;
                }
                a380x_mfd_fpln_result(ctx, a380x_fpln_set_fix_info(fp, st->tab + 1, &n));
            }
            /* prediction line (never computed [FBW-NA]) */
            fprev_text_c(ui, 254.0f, fy + 18.5f, MFD_ANCHOR_CENTER, 30.0f, vc, "--:--");
            fprev_text_c(ui, 384.0f, fy + 18.5f, MFD_ANCHOR_CENTER, 30.0f, vc, "----");
            fprev_text_c(ui, 505.0f, fy + 18.5f, MFD_ANCHOR_CENTER, 30.0f, vc, "---");
        }
    }
    /* radius */
    {
        float ry = y0 + RH[0] + RH[1];
        fprev_text_top(ui, 37.0f, ry + 14.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "RADIUS");
        mfd_field_desc_init(&fd, MFD_FMT_RADIUS);
        fd.flags = (tmpy ? MFD_FIELD_TMPY : 0u) | (fi ? 0u : MFD_FIELD_DISABLED);
        memset(&fv, 0, sizeof(fv));
        fv.has_value = fi && fi->radius_valid;
        fv.num = fv.has_value ? fi->radius_nm : 0.0;
        fr = mfd_input_field(ui, FIX_ID(12), 37.0f, ry + 50.0f, &fd, &fv, &out);
        if (fi && fr != MFD_FIELD_NONE) {
            A380XFplnFixInfo n = *fi;
            n.radius_valid = fr == MFD_FIELD_COMMITTED;
            n.radius_nm = (float)out.num;
            a380x_mfd_fpln_result(ctx, a380x_fpln_set_fix_info(fp, st->tab + 1, &n));
        }
    }
    mfd_button(ui, FIX_ID(13), mfd_rect(37.0f, y0 + RH[0] + RH[1] + RH[2] + 20.0f, 123.0f, MFD_BUTTON_H), "ABEAM",
               MFD_BTN_DISABLED);
    mfd_draw_clip_pop(ctx->dl);

    /* FlightPlanFooter */
    fprev_footer_nav(ctx, fprev_footer(ctx, FIX_ID(20), FIX_ID(21), tmpy));
    if (a380x_mfd_fpln_dup_run(ctx, FPREV_REQ_FIX, &chosen) == 1 && st->dup_wait && act) fix_set(ctx, fp, st->tab, &chosen);
    if (!a380x_mfd_fpln_dup_is_open(ctx->side)) st->dup_wait = 0;
}

/* ======================================================================================== */
/* Registration                                                                             */
/* ======================================================================================== */

void a380x_mfd_page_fpln_rev_register(void)
{
    const uint32_t f = MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY | MFD_PAGE_TMPY;
    mfd_register_page("fms/*/f-pln-departure", MFD_PAGE_ID_FPLN_DEPARTURE, "F-PLN/DEPARTURE", dep_page, f);
    mfd_register_page("fms/*/f-pln-arrival", MFD_PAGE_ID_FPLN_ARRIVAL, "F-PLN/ARRIVAL", arr_page, f);
    mfd_register_page("fms/*/f-pln-airways", MFD_PAGE_ID_FPLN_AIRWAYS, "F-PLN/AIRWAYS", awy_page, f);
    mfd_register_page("fms/*/f-pln-hold", MFD_PAGE_ID_FPLN_HOLD, "F-PLN/HOLD", hold_page, f);
    mfd_register_page("fms/*/f-pln-vert-rev", MFD_PAGE_ID_FPLN_VERT_REV, "F-PLN/VERT REV", vrev_page, f);
    mfd_register_page("fms/active/f-pln-direct-to", MFD_PAGE_ID_FPLN_DIRECT_TO, "F-PLN/DIRECT-TO", dir_page, f);
    mfd_register_page("fms/active/f-pln-fix-info", MFD_PAGE_ID_FPLN_FIX_INFO, "F-PLN/FIX INFO", fix_page, f);
}
