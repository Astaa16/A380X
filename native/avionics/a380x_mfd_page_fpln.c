/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD ACTIVE/F-PLN and SECn/F-PLN page, lateral revision context menu, INSERT NEXT WPT and
 * NEW DEST windows (docs/mfd-spec/05-fms-fpln.md sections 1-6). Translation of the pinned FBW
 * MfdFmsFpln.tsx / FplnRevisionsMenu.tsx / InsertNextWptFrom.tsx / DestinationWindow.tsx.
 *
 * Geometry: the spec 05 anchors (r = below the title bar, f = above the footer) re-based on the
 * spec 01 frame (body 138..950) with border-box widgets: header band 143..188, row 0 205..245,
 * rows 1..8 245 + 72 (k - 1), destination separator 825, destination band 826..896, button row
 * 905..945 (40 px buttons). Text uses the 1.0-em line box of spec 01 (baseline = top + 0.7998 fs).
 */
#include "a380x_mfd_page_fpln.h"
#include "a380x_mfd_page_fpln_rev.h"
#include "a380x_fms.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NBSP "\xC2\xA0"
#define DEG "\xC2\xB0"
#define ID(n) MFD_ID(MFD_PAGE_ID_FPLN, (n))

#define FPLN_HDR_Y0 143.0f   /* r5 */
#define FPLN_HDR_Y1 188.0f   /* r50: separator */
#define FPLN_DEST_SEP 825.0f /* f-125 */
#define FPLN_DEST_CY 861.0f  /* f-89 */
#define FPLN_BTN_SEP 896.0f  /* f-54 */
#define FPLN_BTN_Y 905.0f    /* f-45 (40 px buttons, 5 px above the footer) */
#define FPLN_MENU_X 195.0f
#define FPLN_MENU_Y 183.0f
#define FPLN_MAX_SLOTS 9
#define FPLN_REQ_NEXT 0x4601u

/* ======================================================================================== */
/* Environment and shared helpers                                                           */
/* ======================================================================================== */

static A380XMfdFplnEnv fpln_env;
static A380XMfdFplnRevised fpln_rev[MFD_SIDE_COUNT];

void a380x_mfd_fpln_set_env(const A380XMfdFplnEnv *env)
{
    if (env) fpln_env = *env;
    else memset(&fpln_env, 0, sizeof(fpln_env));
}

void a380x_mfd_fpln_set_revised(MfdSide side, int plan, int altn, int leg)
{
    if ((int)side < 0 || (int)side >= MFD_SIDE_COUNT) return;
    fpln_rev[side].valid = 1;
    fpln_rev[side].plan = plan;
    fpln_rev[side].altn = altn ? 1 : 0;
    fpln_rev[side].leg = leg;
}

void a380x_mfd_fpln_reset_revised(MfdSide side)
{
    if ((int)side < 0 || (int)side >= MFD_SIDE_COUNT) return;
    memset(&fpln_rev[side], 0, sizeof(fpln_rev[side]));
    fpln_rev[side].leg = -1;
}

A380XMfdFplnRevised a380x_mfd_fpln_revised(MfdSide side)
{
    A380XMfdFplnRevised r;
    memset(&r, 0, sizeof(r));
    r.leg = -1;
    if ((int)side >= 0 && (int)side < MFD_SIDE_COUNT && fpln_rev[side].valid) r = fpln_rev[side];
    return r;
}

A380XFlightPlans *a380x_mfd_fpln_store(const MfdPageContext *ctx)
{
    if (fpln_env.plans) return fpln_env.plans;
    if (ctx && ctx->fms) return ctx->fms->fpln.plans;
    return NULL;
}

int a380x_mfd_fpln_lateral_plan(const MfdPageContext *ctx)
{
    A380XFlightPlans *fp = a380x_mfd_fpln_store(ctx);
    int p;
    if (!fp || !ctx) return -1;
    if (ctx->plan >= A380X_PLAN_SEC1 && ctx->plan <= A380X_PLAN_SEC3)
        return a380x_fpln_plan(fp, ctx->plan) ? ctx->plan : -1;
    if (ctx->fms) (void)a380x_fms_loaded_plan(ctx->fms, A380X_FMS_PLAN_ACTIVE); /* lazily syncs the TMPY data */
    p = a380x_fpln_loaded_index(fp, 0);
    return a380x_fpln_plan(fp, p) ? p : -1;
}

int a380x_mfd_fpln_edit_plan(const MfdPageContext *ctx)
{
    if (ctx && ctx->plan >= A380X_PLAN_SEC1 && ctx->plan <= A380X_PLAN_SEC3) return ctx->plan;
    return A380X_PLAN_ACTIVE;
}

void a380x_mfd_fpln_aircraft(const MfdPageContext *ctx, A380XFplnAircraft *out)
{
    memset(out, 0, sizeof(*out));
    out->lat = out->lon = NAN;
    out->true_track_deg = NAN;
    if (fpln_env.aircraft_valid) {
        *out = fpln_env.aircraft;
        return;
    }
    if (ctx && ctx->fms) {
        const A380XFmsInputs *in = a380x_fms_inputs(ctx->fms);
        out->valid = in->position_valid;
        out->lat = in->lat_deg;
        out->lon = in->lon_deg;
        out->alt_ft = (float)in->baro_alt_ft;
        out->true_track_deg = in->position_valid ? (float)in->true_track_deg : NAN;
        out->gs_kt = (float)in->ground_speed_kt;
        out->on_ground = in->on_ground;
        out->nav_mode = in->lateral_mode == A380X_FMS_LM_NAV;
    }
}

int a380x_mfd_fpln_phase(const MfdPageContext *ctx)
{
    return (ctx && ctx->fms) ? a380x_fms_phase(ctx->fms) : 0;
}

double a380x_mfd_fpln_utc(const MfdPageContext *ctx)
{
    if (ctx && ctx->fms) return a380x_fms_inputs(ctx->fms)->utc_seconds;
    return NAN;
}

void a380x_mfd_fpln_trans(const MfdPageContext *ctx, int lplan, double *ta_ft, double *tl_ft)
{
    A380XFlightPlans *fp = a380x_mfd_fpln_store(ctx);
    const A380XFplnPlan *p = fp ? a380x_fpln_plan(fp, lplan) : NULL;
    *ta_ft = p ? (double)p->pri.db_trans_alt_ft : NAN;
    *tl_ft = p ? (double)p->pri.db_trans_level_ft : NAN;
    if (ctx && ctx->fms && lplan >= 0 && lplan < A380X_FMS_NUM_PLANS && a380x_fms_plan_exists(ctx->fms, lplan)) {
        A380XFmsValue a = a380x_fms_get(ctx->fms, lplan, A380X_FMS_F_TRANS_ALT);
        A380XFmsValue l = a380x_fms_get(ctx->fms, lplan, A380X_FMS_F_TRANS_FL);
        if (a.has) *ta_ft = a.v;
        if (l.has) *tl_ft = l.v * 100.0;
    }
}

static void fpln_msg(MfdPageContext *ctx, MfdMsgId id)
{
    if (ctx->msgs) mfd_msg_add(ctx->msgs, id, NULL, NULL, NULL, NULL, NULL);
}

int a380x_mfd_fpln_result(MfdPageContext *ctx, int rc)
{
    A380XFlightPlans *fp = a380x_mfd_fpln_store(ctx);
    if (rc == A380X_FPLN_OK) {
        if (ctx->fms && fp && a380x_fpln_has_tmpy(fp)) a380x_fms_tmpy_begin(ctx->fms);
        return 1;
    }
    switch (rc) {
    case A380X_FPLN_E_NOT_IN_DB: fpln_msg(ctx, MFD_MSG_NOT_IN_DATABASE); break;
    case A380X_FPLN_E_NOT_ALLOWED: fpln_msg(ctx, MFD_MSG_NOT_ALLOWED); break;
    case A380X_FPLN_E_NO_INTERSECTION: fpln_msg(ctx, MFD_MSG_NO_INTERSECTION_FOUND); break;
    default: break; /* ARG / NO_PLAN / FULL: silent (FBW refuses silently) */
    }
    return 0;
}

int a380x_mfd_fpln_tmpy_insert(MfdPageContext *ctx)
{
    A380XFlightPlans *fp = a380x_mfd_fpln_store(ctx);
    A380XFplnAircraft ac;
    if (!fp || !a380x_fpln_has_tmpy(fp)) return 0;
    a380x_mfd_fpln_aircraft(ctx, &ac);
    if (ctx->fms) {
        a380x_fms_tmpy_begin(ctx->fms);
        if (ctx->fms->fpln.tmpy_insert) return a380x_fms_tmpy_insert(ctx->fms);
        if (a380x_fpln_tmpy_insert(fp, &ac) != A380X_FPLN_OK) return 0;
        (void)a380x_fms_tmpy_insert(ctx->fms);
        return 1;
    }
    return a380x_fpln_tmpy_insert(fp, &ac) == A380X_FPLN_OK;
}

int a380x_mfd_fpln_tmpy_erase(MfdPageContext *ctx)
{
    A380XFlightPlans *fp = a380x_mfd_fpln_store(ctx);
    if (!fp || !a380x_fpln_has_tmpy(fp)) return 0;
    if (ctx->fms) {
        a380x_fms_tmpy_begin(ctx->fms);
        if (ctx->fms->fpln.tmpy_erase) return a380x_fms_tmpy_erase(ctx->fms);
        if (a380x_fpln_tmpy_erase(fp) != A380X_FPLN_OK) return 0;
        (void)a380x_fms_tmpy_erase(ctx->fms);
        return 1;
    }
    return a380x_fpln_tmpy_erase(fp) == A380X_FPLN_OK;
}

MfdColor a380x_mfd_fpln_plan_colour(int lplan)
{
    if (lplan >= A380X_PLAN_SEC1) return MFD_COL_WHITE;
    if (lplan == A380X_PLAN_TEMPORARY) return MFD_COL_YELLOW;
    return MFD_COL_GREEN;
}

void a380x_mfd_fpln_format_latlon(double lat, double lon, char *buf, size_t n)
{
    long tlat, tlon;
    if (!isfinite(lat) || !isfinite(lon)) {
        snprintf(buf, n, "%s", "");
        return;
    }
    tlat = (long)floor(fabs(lat) * 600.0 + 0.5); /* tenths of minutes */
    tlon = (long)floor(fabs(lon) * 600.0 + 0.5);
    snprintf(buf, n, "%ld" DEG "%02ld.%ld%c/%03ld" DEG "%02ld.%ld%c", tlat / 600, (tlat % 600) / 10, tlat % 10,
             lat >= 0.0 ? 'N' : 'S', tlon / 600, (tlon % 600) / 10, tlon % 10, lon >= 0.0 ? 'E' : 'W');
}


int a380x_mfd_fpln_star_button(MfdUi *ui, MfdId id, MfdRect r, const char *l1, const char *l2, MfdColor c,
                               int disabled)
{
    MfdButtonDesc bd;
    float w1 = mfd_text_width(l1, 22.0f), w2 = l2 ? mfd_text_width(l2, 22.0f) : 0.0f, lw = w1 > w2 ? w1 : w2;
    float ws = mfd_text_width("*", 22.0f), x0, x1, top;
    int lines = l2 ? 2 : 1, clicked;
    if (r.w <= 0.0f) r.w = lw + 10.0f + ws + 14.0f + 4.0f;
    if (r.h <= 0.0f) r.h = mfd_button_height(lines);
    mfd_button_desc_init(&bd, "");
    bd.flags = disabled ? MFD_BTN_DISABLED : 0u;
    clicked = mfd_button_ex(ui, id, r, &bd) == MFD_BUTTON_CLICKED;
    if (disabled) c = MFD_COL_DARK_GREY;
    x0 = r.x + 14.0f;
    x1 = r.x + r.w - 4.0f;
    top = r.y + 11.0f + (r.h - 18.0f - 22.0f * (float)lines) * 0.5f;
    mfd_text(ui, (x0 + x1 - ws - 10.0f) * 0.5f, top + MFD_FONT_BASELINE * 22.0f, MFD_ANCHOR_CENTER, 22.0f, c, l1);
    if (l2) mfd_text(ui, (x0 + x1 - ws - 10.0f) * 0.5f, top + 22.0f + MFD_FONT_BASELINE * 22.0f, MFD_ANCHOR_CENTER, 22.0f, c, l2);
    mfd_text(ui, x1, top + 11.0f * (float)lines - 11.0f + MFD_FONT_BASELINE * 22.0f, MFD_ANCHOR_RIGHT, 22.0f, c, "*");
    return clicked;
}

/* ---- getOrCreateWaypoint ------------------------------------------------------------------ */

static void fpln_error(MfdPageContext *ctx, MfdFmsErrorType t)
{
    MfdFmsError e;
    mfd_err_set_plain(&e, t);
    mfd_ui_error(ctx->ui, &e, 0);
}

static int fpln_all_digits(const char *s, size_t n)
{
    size_t i;
    if (n == 0) return 0;
    for (i = 0; i < n; ++i)
        if (!isdigit((unsigned char)s[i])) return 0;
    return 1;
}

/* Number with optional single decimal point; returns 1 and the value. */
static int fpln_number(const char *s, size_t n, double *out)
{
    char tmp[16];
    size_t i, dots = 0, digits = 0;
    if (n == 0 || n >= sizeof(tmp)) return 0;
    for (i = 0; i < n; ++i) {
        if (s[i] == '.') dots++;
        else if (isdigit((unsigned char)s[i])) digits++;
        else return 0;
    }
    if (dots > 1 || digits == 0) return 0;
    memcpy(tmp, s, n);
    tmp[n] = '\0';
    *out = atof(tmp);
    return 1;
}

/* One coordinate "N5128.6" / "5128.6N" / "51.5" (lat: deg_digits_max 2, lon: 3). */
static int fpln_coord(const char *s, char pos, char neg, int max_deg_digits, double *out)
{
    char body[16];
    size_t n = strlen(s), i, intlen;
    int sign = 1;
    double v, deg, min;
    const char *dot;
    if (n == 0 || n >= sizeof(body)) return 0;
    if (s[0] == pos || s[0] == neg) {
        sign = s[0] == neg ? -1 : 1;
        s++;
        n--;
    } else if (s[n - 1] == pos || s[n - 1] == neg) {
        sign = s[n - 1] == neg ? -1 : 1;
        n--;
    }
    memcpy(body, s, n);
    body[n] = '\0';
    dot = strchr(body, '.');
    if (!dot || strchr(dot + 1, '.') || strlen(dot + 1) != 1) return 0;
    intlen = (size_t)(dot - body);
    for (i = 0; i < n; ++i)
        if (body[i] != '.' && !isdigit((unsigned char)body[i])) return 0;
    if (intlen < 2 || intlen > (size_t)max_deg_digits + 2) return 0;
    v = atof(body);
    if (intlen > (size_t)max_deg_digits) { /* DDMM.M / DDDMM.M */
        deg = floor(v / 100.0);
        min = v - deg * 100.0;
        if (min >= 60.0) return 0;
        v = deg + min / 60.0;
    }
    *out = sign * v;
    return 1;
}

static int fpln_find_place(MfdPageContext *ctx, A380XNavdata *nav, const char *ident, A380XNavFix *out, int max)
{
    A380XFplnAircraft ac;
    a380x_mfd_fpln_aircraft(ctx, &ac);
    if (ac.valid && isfinite(ac.lat) && isfinite(ac.lon))
        return a380x_nav_find_near(nav, ident, A380X_NAVMASK_PLACE, ac.lat, ac.lon, out, max);
    return a380x_nav_find(nav, ident, A380X_NAVMASK_PLACE, out, max);
}

int a380x_mfd_fpln_resolve_place(MfdPageContext *ctx, const char *text, A380XNavFix *out, int max)
{
    A380XFlightPlans *fp = a380x_mfd_fpln_store(ctx);
    A380XNavdata *nav = fp ? fp->nav : NULL;
    char s[32], id[A380X_NAV_IDENT_LEN];
    const char *slash, *slash2;
    size_t n;
    int k;
    if (!text || !out || max <= 0) return 0;
    snprintf(s, sizeof(s), "%s", text);
    for (k = 0; s[k]; ++k) s[k] = (char)toupper((unsigned char)s[k]);
    n = strlen(s);
    if (n == 0) return 0;
    if (!nav) {
        fpln_error(ctx, MFD_ERR_NOT_IN_DATABASE);
        return 0;
    }
    if (mfd_is_runway_format(s)) {
        char icao[5];
        const A380XNavAirport *apt;
        const A380XNavRunway *rw;
        memcpy(icao, s, 4);
        icao[4] = '\0';
        apt = a380x_nav_airport(nav, icao);
        rw = apt ? a380x_nav_runway(apt, s + 4) : NULL;
        if (!rw && !mfd_is_place_format(s)) {
            fpln_error(ctx, MFD_ERR_NOT_IN_DATABASE);
            return 0;
        }
        if (rw) {
        memset(&out[0], 0, sizeof(out[0]));
        out[0].kind = A380X_NAV_RUNWAY;
        snprintf(out[0].ident, sizeof(out[0].ident), "%.7s", s);
        snprintf(out[0].airport, sizeof(out[0].airport), "%s", icao);
        out[0].lat = rw->thr_lat;
        out[0].lon = rw->thr_lon;
        out[0].elev_ft = rw->thr_elev_ft;
        out[0].station_var = out[0].loc_true_brg = out[0].loc_mag_brg = out[0].gs_angle_deg = NAN;
        out[0].db_id = -1;
        return 1;
        }
    }
    if (mfd_is_place_format(s)) {
        int c = fpln_find_place(ctx, nav, s, out, max);
        if (c <= 0) {
            fpln_error(ctx, MFD_ERR_NOT_IN_DATABASE);
            return 0;
        }
        return c > max ? max : c;
    }
    slash = strchr(s, '/');
    slash2 = slash ? strchr(slash + 1, '/') : NULL;
    if (slash && !slash2) {
        /* lat/lon */
        char a[16], b[16];
        double lat, lon;
        size_t la = (size_t)(slash - s);
        if (la < sizeof(a) && strlen(slash + 1) < sizeof(b)) {
            memcpy(a, s, la);
            a[la] = '\0';
            snprintf(b, sizeof(b), "%s", slash + 1);
            if (fpln_coord(a, 'N', 'S', 2, &lat) && fpln_coord(b, 'E', 'W', 3, &lon)) {
                if (fabs(lat) > 90.0 || fabs(lon) > 180.0) {
                    fpln_error(ctx, MFD_ERR_ENTRY_OUT_OF_RANGE);
                    return 0;
                }
                a380x_fpln_next_pilot_ident(fp, "LL", id);
                if (!a380x_nav_make_latlon(lat, lon, id, &out[0])) {
                    fpln_error(ctx, MFD_ERR_FORMAT_ERROR);
                    return 0;
                }
                return 1;
            }
        }
        /* PBX: PLACE-BRG/PLACE-BRG */
        {
            const char *d1 = memchr(s, '-', la), *d2 = strrchr(slash + 1, '-');
            if (d1 && d2) {
                char p1[A380X_NAV_IDENT_LEN], p2[A380X_NAV_IDENT_LEN];
                size_t l1 = (size_t)(d1 - s), l2 = (size_t)(d2 - (slash + 1));
                double b1, b2;
                if (l1 >= 2 && l1 <= 7 && l2 >= 2 && l2 <= 7 && fpln_all_digits(d1 + 1, la - l1 - 1) &&
                    fpln_all_digits(d2 + 1, strlen(d2 + 1)) && la - l1 - 1 <= 3 && strlen(d2 + 1) <= 3) {
                    A380XNavFix f1, f2;
                    memcpy(p1, s, l1);
                    p1[l1] = '\0';
                    memcpy(p2, slash + 1, l2);
                    p2[l2] = '\0';
                    b1 = atof(d1 + 1);
                    b2 = atof(d2 + 1);
                    if (b1 > 360.0 || b2 > 360.0) {
                        fpln_error(ctx, MFD_ERR_ENTRY_OUT_OF_RANGE);
                        return 0;
                    }
                    if (fpln_find_place(ctx, nav, p1, &f1, 1) <= 0 || fpln_find_place(ctx, nav, p2, &f2, 1) <= 0) {
                        fpln_error(ctx, MFD_ERR_NOT_IN_DATABASE);
                        return 0;
                    }
                    a380x_fpln_next_pilot_ident(fp, "PBX", id);
                    if (!a380x_nav_make_pbx(nav, &f1, b1, &f2, b2, id, &out[0])) {
                        fpln_msg(ctx, MFD_MSG_NO_INTERSECTION_FOUND);
                        return 0;
                    }
                    return 1;
                }
            }
        }
        /* P/D: PLACE/DIST (not implemented in FBW) */
        {
            double v;
            const char *q = slash + 1;
            if (*q == '-') q++;
            if (la >= 2 && la <= 7 && fpln_number(q, strlen(q), &v)) {
                fpln_error(ctx, MFD_ERR_NOT_YET_IMPLEMENTED);
                return 0;
            }
        }
    } else if (slash && slash2 && !strchr(slash2 + 1, '/')) {
        /* PBD: PLACE/BRG/DIST */
        size_t lp = (size_t)(slash - s), lb = (size_t)(slash2 - slash - 1);
        double brg, dist;
        if (lp >= 2 && lp <= 7 && lb >= 1 && lb <= 3 && fpln_all_digits(slash + 1, lb) &&
            fpln_number(slash2 + 1, strlen(slash2 + 1), &dist)) {
            char place[A380X_NAV_IDENT_LEN];
            A380XNavFix pf;
            memcpy(place, s, lp);
            place[lp] = '\0';
            brg = atof(slash + 1);
            if (brg > 360.0 || dist <= 0.0 || dist > 999.9) {
                fpln_error(ctx, MFD_ERR_ENTRY_OUT_OF_RANGE);
                return 0;
            }
            if (fpln_find_place(ctx, nav, place, &pf, 1) <= 0) {
                fpln_error(ctx, MFD_ERR_NOT_IN_DATABASE);
                return 0;
            }
            a380x_fpln_next_pilot_ident(fp, "PBD", id);
            if (!a380x_nav_make_pbd(nav, &pf, brg, dist, id, &out[0])) {
                fpln_error(ctx, MFD_ERR_FORMAT_ERROR);
                return 0;
            }
            return 1;
        }
    }
    fpln_error(ctx, MFD_ERR_FORMAT_ERROR);
    return 0;
}

/* ======================================================================================== */
/* F-PLN page                                                                               */
/* ======================================================================================== */

typedef struct {
    MfdPageContext *ctx;
    MfdFplnState *st;
    A380XFlightPlans *fp;
    const A380XFplnPlan *plan;
    const A380XFplnPredFeed *feed;
    int uplan, lplan, sec, tmpy, active;
    MfdColor pcol;
    int slots, phase, metric;
    double utc, ett, ta, tl;
} FplnView;

typedef struct {
    int count, active_line, from_line, dest_line;
} FplnScan;

static float fpln_slot_y(int s) { return s == 0 ? 205.0f : 245.0f + 72.0f * (float)(s - 1); }
static float fpln_slot_h(int s) { return s == 0 ? 40.0f : 72.0f; }

/* Text whose 1.0-em line box is vertically centred on cy. */
static void fpln_text_c(MfdUi *ui, float x, float cy, MfdAnchor a, float fs, MfdColor c, const char *s)
{
    mfd_text(ui, x, cy - fs * 0.5f + MFD_FONT_BASELINE * fs, a, fs, c, s);
}

/* Upper-row text, top-aligned at the row top (spec 05 T1 baseline 0.847 fs). */
static void fpln_text_top(MfdUi *ui, float x, float top, MfdAnchor a, float fs, MfdColor c, const char *s)
{
    mfd_text(ui, x, top + 0.847f * fs, a, fs, c, s);
}

static void fpln_hhmm(double secs, char *buf, size_t n)
{
    long t = (long)floor(secs);
    t %= 86400;
    if (t < 0) t += 86400;
    snprintf(buf, n, "%02ld:%02ld", t / 3600, (t / 60) % 60);
}

static void fpln_scan(const FplnView *v, FplnScan *o)
{
    A380XFplnLineIter it;
    A380XFplnLine ln;
    int n = 0, from_leg = a380x_fpln_from_leg_index(v->plan), dest_leg = v->plan ? v->plan->pri.destination_leg_index : -1;
    int act = v->plan ? v->plan->active_leg_index : -1;
    o->count = 0;
    o->active_line = o->from_line = o->dest_line = -1;
    if (!v->plan) return;
    a380x_fpln_lines_begin(&it, v->fp, v->lplan, v->feed);
    while (a380x_fpln_lines_next(&it, &ln)) {
        if (ln.kind == A380X_FLINE_WAYPOINT && !ln.is_pseudo && !ln.is_altn && ln.leg_index >= 0) {
            if (ln.leg_index == from_leg && o->from_line < 0) o->from_line = n;
            if (ln.leg_index == act && o->active_line < 0) o->active_line = n;
            if (ln.leg_index == dest_leg && o->dest_line < 0) o->dest_line = n;
        }
        n++;
    }
    o->count = n;
    if (o->from_line < 0) o->from_line = o->active_line >= 0 ? (o->active_line > 0 ? o->active_line - 1 : 0) : 0;
}

static int fpln_clamp(int from, const FplnScan *sc, int slots)
{
    int lo = sc->from_line < 0 ? 0 : sc->from_line;
    int hi = sc->count - slots;
    if (hi < lo) hi = lo;
    if (from < lo) from = lo;
    if (from > hi) from = hi;
    return from;
}

/* ---- connector (spec 05 section 3.7) ------------------------------------------------------- */

static void fpln_seg(MfdDrawList *dl, float y0, float ax, float ay, float bx, float by, MfdColor c)
{
    mfd_draw_line(dl, 557.0f + ax, y0 + ay, 557.0f + bx, y0 + by, 2.0f, c);
}

static void fpln_diamond(MfdDrawList *dl, float y0, int first, MfdColor c)
{
    MfdPoint p[4];
    float o = first ? -32.0f : 0.0f;
    p[0].x = 572.0f; p[0].y = y0 + 63.0f + o;
    p[1].x = 565.0f; p[1].y = y0 + 51.0f + o;
    p[2].x = 572.0f; p[2].y = y0 + 37.0f + o;
    p[3].x = 579.0f; p[3].y = y0 + 51.0f + o;
    if (first) { /* (15,31)(8,19)(15,5)(22,19) */
        p[0].y = y0 + 31.0f; p[1].y = y0 + 19.0f; p[2].y = y0 + 5.0f; p[3].y = y0 + 19.0f;
    }
    mfd_draw_polyline(dl, p, 4, 2.0f, 1, c);
}

static void fpln_connector(MfdDrawList *dl, float y0, int first, int last, int after_sp, int before_sp, int active,
                           int before_active, int pseudo, int hold, MfdColor c)
{
    MfdColor w = c == MFD_COL_GREEN ? MFD_COL_WHITE : c;
    if (hold) { /* G */
        fpln_seg(dl, y0, 15, 72, 15, 0, c);
        return;
    }
    if (first && before_active) return; /* A: FROM line */
    if (first) {                        /* B */
        if (!before_sp) fpln_seg(dl, y0, 15, 40, 15, 30, c);
        fpln_seg(dl, y0, 8, 18, 0, 18, (active && c == MFD_COL_GREEN) ? MFD_COL_WHITE : c);
        fpln_diamond(dl, y0, 1, (active && c != MFD_COL_YELLOW) ? MFD_COL_WHITE : c);
        return;
    }
    if (after_sp) { /* C */
        if (!last && !before_sp) fpln_seg(dl, y0, 15, 72, 15, 63, c);
        fpln_diamond(dl, y0, 0, c);
        fpln_seg(dl, y0, 8, 50, 0, 50, c);
        return;
    }
    if (active) { /* D */
        fpln_seg(dl, y0, 15, 72, 15, 62, c);
        fpln_seg(dl, y0, 15, 9, 15, 37, w);
        fpln_diamond(dl, y0, 0, w);
        fpln_seg(dl, y0, 8, 50, 0, 50, w);
        fpln_seg(dl, y0, 15, 10, 30, 10, w);
        return;
    }
    if (pseudo) { /* E */
        if (!last && !before_sp) fpln_seg(dl, y0, 15, 72, 15, 50, c);
        fpln_seg(dl, y0, 15, 0, 15, 50, c);
        fpln_seg(dl, y0, 15, 50, 0, 50, c);
        fpln_seg(dl, y0, 15, 10, 30, 10, c);
        return;
    }
    /* F */
    if (!last && !before_sp) fpln_seg(dl, y0, 15, 72, 15, 63, c);
    fpln_seg(dl, y0, 15, 0, 15, 37, c);
    fpln_diamond(dl, y0, 0, c);
    fpln_seg(dl, y0, 8, 50, 0, 50, c);
    fpln_seg(dl, y0, 15, 10, 30, 10, c);
}

/* ---- field formatting (spec 05 section 3.5) -------------------------------------------------- */

static double fpln_ts(const FplnView *v, double secs)
{
    if (v->phase >= A380X_FMS_PHASE_TAKEOFF && isfinite(v->utc)) return v->utc + secs;
    if (isfinite(v->ett)) return v->ett + secs;
    return secs;
}

static int fpln_is_wpt(const A380XFplnLine *l) { return l && l->kind == A380X_FLINE_WAYPOINT; }

static int fpln_has_spd(const A380XFplnLine *l) { return l->spd_cstr.desc != 0 && isfinite(l->spd_cstr.speed); }
static int fpln_has_alt(const A380XFplnLine *l) { return l->alt_cstr.desc != 0; }

static void fpln_speed_text(float spd, char *buf, size_t n)
{
    if (spd > 2.0f) snprintf(buf, n, "%.0f", (double)spd);
    else {
        char t[16];
        mfd_js_to_fixed(spd, 2, t, sizeof(t));
        snprintf(buf, n, ".%s", strchr(t, '.') ? strchr(t, '.') + 1 : "00");
    }
}

static void fpln_wind_dir(const A380XFplnWptPred *p, char *buf, size_t n)
{
    if (p->wind_kind == 1) snprintf(buf, n, "%03.0f" DEG, fmod(floor(p->wind_dir_true_deg + 0.5) + 360.0, 360.0));
    else if (p->wind_kind == 2) snprintf(buf, n, "%s", p->tailwind_kt > 0.0f ? "TAIL" : "HEAD");
    else buf[0] = '\0';
}

static void fpln_wind_mag(const A380XFplnWptPred *p, char *buf, size_t n)
{
    double m = p->wind_kind == 1 ? p->wind_speed_kt : fabs((double)p->tailwind_kt);
    if (p->wind_kind == 0) buf[0] = '\0';
    else snprintf(buf, n, "%03.0f", floor(m + 0.5));
}

/* ---- one row --------------------------------------------------------------------------------- */

static void fpln_special_row(FplnView *v, int s, const A380XFplnLine *ln)
{
    MfdUi *ui = v->ctx->ui;
    float y0 = fpln_slot_y(s), h = fpln_slot_h(s), cy = y0 + h * 0.5f;
    const char *label = ln->special_text ? ln->special_text : "";
    const char *delim = strlen(label) > 13 ? "- - - - -" : "- - - - - -" NBSP;
    float wd = mfd_text_width(delim, 30.0f), wl = mfd_text_width(label, 30.0f);
    float x = 384.0f - (2.0f * wd + wl + 30.0f) * 0.5f;
    MfdId id = ID(10 + s * 8 + 4);
    MfdRect box = mfd_rect(0.0f, y0, 768.0f, h);
    mfd_ui_hit(ui, id, MFD_PART_MAIN, box);
    if (mfd_ui_is_hovered(ui, id)) mfd_draw_bevel(v->ctx->dl, box, 2.0f, MFD_COL_CYAN, MFD_BEVEL_SOLID);
    fpln_text_c(ui, x, cy, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_WHITE, delim);
    fpln_text_c(ui, x + wd + 15.0f, cy, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_WHITE, label);
    fpln_text_c(ui, x + wd + 30.0f + wl, cy, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_WHITE, delim);
}

static void fpln_open_rev_menu(FplnView *v, int d, const A380XFplnLine *ln);
static void fpln_vert_rev(FplnView *v, const A380XFplnLine *ln, const char *tab);

static int fpln_eligible(const FplnView *v, const A380XFplnLine *ln)
{
    if (!fpln_is_wpt(ln) || ln->is_pseudo || !ln->leg || ln->leg_index < 0) return 0;
    if (!a380x_fpln_constraint_revision_allowed(ln->leg)) return 0;
    return ln->is_altn || ln->leg_index >= v->plan->active_leg_index;
}

static void fpln_row(FplnView *v, int s, int d, const A380XFplnLine *ln, const A380XFplnLine *prev,
                     const A380XFplnLine *next)
{
    MfdPageContext *ctx = v->ctx;
    MfdUi *ui = ctx->ui;
    MfdDrawList *dl = ctx->dl;
    float y0 = fpln_slot_y(s), h = fpln_slot_h(s);
    int first = s == 0, last = s == v->slots - 1;
    int after_sp = prev && prev->kind == A380X_FLINE_SPECIAL;
    int before_sp = next && next->kind == A380X_FLINE_SPECIAL;
    int hold = ln->kind == A380X_FLINE_HOLD_HM;
    int active = ln->is_active_leg || ln->is_active_hold_fix;
    int before_active = !active && ln->is_before_active;
    MfdColor lc = (ln->is_altn || ln->is_missed) ? MFD_COL_CYAN : v->pcol;
    MfdColor tc = (active && lc == MFD_COL_GREEN) ? MFD_COL_WHITE : lc;
    MfdColor ac = v->tmpy ? MFD_COL_YELLOW : ((ln->is_altn || ln->is_missed) ? MFD_COL_CYAN : MFD_COL_WHITE);
    float cy = first ? y0 + 20.0f : y0 + 52.0f; /* lower-row centre */
    float ly = first ? y0 : y0 + 32.0f;          /* lower-row top */
    MfdId id_ident = ID(10 + s * 8 + 0), id_time = ID(10 + s * 8 + 1), id_spd = ID(10 + s * 8 + 2),
          id_alt = ID(10 + s * 8 + 3), id_exit = ID(10 + s * 8 + 5);
    int selected = v->st->rev_open && v->st->rev_line == d;
    int clickable_ident = ln->leg_index >= 0 && !ln->is_pseudo;
    int eligible = !hold && fpln_eligible(v, ln);
    char t[48];

    if (ln->kind == A380X_FLINE_SPECIAL) {
        fpln_special_row(v, s, ln);
        if (ln->leg_index >= 0 && mfd_ui_was_clicked(ui, ID(10 + s * 8 + 4))) fpln_open_rev_menu(v, d, ln);
        return;
    }

    /* ---- IDENT column ---- */
    {
        float tw = mfd_text_width(ln->ident, 30.0f) + (ln->overfly ? mfd_text_width("@", 24.0f) : 0.0f);
        MfdRect box = mfd_rect(0.0f, ly, tw + 6.0f, 40.0f);
        MfdColor ic = selected ? MFD_COL_WHITE : tc;
        if (!first && ln->annotation[0] && !hold)
            fpln_text_top(ui, 190.0f, y0 + 4.0f, MFD_ANCHOR_RIGHT, 22.0f, ac, ln->annotation);
        if (selected) mfd_draw_fill_rect(dl, box, MFD_COL_DARK_GREY);
        if (clickable_ident) {
            mfd_ui_hit(ui, id_ident, MFD_PART_MAIN, box);
            if (mfd_ui_is_hovered(ui, id_ident)) mfd_draw_bevel(dl, box, 2.0f, MFD_COL_CYAN, MFD_BEVEL_SOLID);
        }
        fpln_text_c(ui, 3.0f, cy, MFD_ANCHOR_LEFT, 30.0f, ic, ln->ident);
        if (ln->overfly) {
            float bx = 3.0f + mfd_text_width(ln->ident, 30.0f);
            mfd_text(ui, bx, cy - 15.0f + MFD_FONT_BASELINE * 30.0f, MFD_ANCHOR_LEFT, 24.0f, ic, "@");
        }
        if (clickable_ident && mfd_ui_was_clicked(ui, id_ident)) fpln_open_rev_menu(v, d, ln);
    }

    /* ---- TIME column ---- */
    {
        MfdRect box = mfd_rect(192.0f, y0, 88.0f, h);
        int click = !hold && fpln_is_wpt(ln) && !ln->is_altn && eligible;
        if (click) {
            mfd_ui_hit(ui, id_time, MFD_PART_MAIN, box);
            if (mfd_ui_is_hovered(ui, id_time)) mfd_draw_bevel(dl, box, 2.0f, MFD_COL_CYAN, MFD_BEVEL_SOLID);
            if (mfd_ui_was_clicked(ui, id_time)) fpln_vert_rev(v, ln, "rta");
        }
        if (hold) {
            if (!first) fpln_text_top(ui, 236.0f, y0 + 1.0f, MFD_ANCHOR_CENTER, 22.0f, MFD_COL_WHITE, "SPD");
            if (isfinite(ln->hold_speed_kt)) {
                snprintf(t, sizeof(t), "%.0f", (double)ln->hold_speed_kt);
                fpln_text_c(ui, 236.0f, cy, MFD_ANCHOR_CENTER, 22.0f, tc, t);
            }
        } else {
            if (v->active && ln->pred.valid) fpln_hhmm(fpln_ts(v, ln->pred.seconds_from_present), t, sizeof(t));
            else snprintf(t, sizeof(t), "--:--");
            fpln_text_c(ui, 236.0f, cy, MFD_ANCHOR_CENTER, 22.0f, tc, t);
        }
    }

    /* ---- SPD column ---- */
    if (!hold) {
        MfdRect box = mfd_rect(280.0f, y0, 116.0f, h);
        int click = eligible && !v->st->efob_mode;
        if (click) {
            mfd_ui_hit(ui, id_spd, MFD_PART_MAIN, box);
            if (mfd_ui_is_hovered(ui, id_spd)) mfd_draw_bevel(dl, box, 2.0f, MFD_COL_CYAN, MFD_BEVEL_SOLID);
            if (mfd_ui_was_clicked(ui, id_spd)) fpln_vert_rev(v, ln, "spd");
        }
        if (v->st->efob_mode) {
            if (v->active && ln->pred.valid && isfinite(ln->pred.efob_kg)) {
                double w = v->metric ? ln->pred.efob_kg : ln->pred.efob_kg / MFD_KG_PER_LB;
                mfd_js_to_fixed(w / 1000.0, 1, t, sizeof(t));
            } else {
                snprintf(t, sizeof(t), "--.-");
            }
            fpln_text_c(ui, 332.0f, cy, MFD_ANCHOR_LEFT, 22.0f, tc, t);
        } else if (!v->active) {
            fpln_text_c(ui, 342.0f, cy, MFD_ANCHOR_LEFT, 22.0f, tc, "---");
        } else {
            const A380XFplnWptPred *p = &ln->pred;
            int has_pred = p->valid && isfinite(p->speed) && p->speed > 0.0f;
            int has_c = fpln_has_spd(ln);
            snprintf(t, sizeof(t), "---");
            if (has_pred && fpln_is_wpt(prev) && prev->pred.valid && isfinite(prev->pred.speed) &&
                prev->pred.speed > 0.0f &&
                ((p->speed >= 2.0f && fabsf(prev->pred.speed - p->speed) < 1.0f) ||
                 (p->speed < 2.0f && fabsf(prev->pred.speed - p->speed) < 0.01f)) &&
                !has_c && !first && !after_sp)
                snprintf(t, sizeof(t), "\"");
            else if (has_pred) fpln_speed_text(p->speed, t, sizeof(t));
            if (has_c && has_pred) {
                fpln_text_c(ui, 332.0f, cy, MFD_ANCHOR_CENTER, 30.0f, p->spd_cstr_met ? MFD_COL_MAGENTA : MFD_COL_AMBER,
                            "*");
                fpln_text_c(ui, 342.0f, cy, MFD_ANCHOR_LEFT, 22.0f, tc, t);
            } else if (has_c) {
                fpln_speed_text(ln->spd_cstr.speed, t, sizeof(t));
                fpln_text_c(ui, 342.0f, cy, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_MAGENTA, t);
            } else {
                fpln_text_c(ui, 342.0f, cy, MFD_ANCHOR_LEFT, 22.0f, tc, t);
            }
        }
    } else if (ln->hold_decel_sequenced || ln->is_active_leg) {
        /* IMMEDIATE EXIT / RESUME HOLD (spec 05 section 3.10, [PORT] kept inside row + 24 .. + 81) */
        if (a380x_mfd_fpln_star_button(ui, id_exit, mfd_rect(322.0f, y0 + 24.0f, 200.0f, 57.0f),
                                       ln->hold_imm_exit ? "RESUME" : "IMMEDIATE", ln->hold_imm_exit ? "HOLD" : "EXIT",
                                       MFD_COL_AMBER, 0)) {
            if (ln->hold_decel_sequenced)
                a380x_mfd_fpln_result(ctx, a380x_fpln_delete_element(v->fp, a380x_mfd_fpln_edit_plan(ctx), 0,
                                                                     ln->leg_index));
            else if (v->lplan == A380X_PLAN_ACTIVE)
                a380x_mfd_fpln_result(ctx, a380x_fpln_set_hold_imm_exit(v->fp, ln->leg_index, !ln->hold_imm_exit));
        }
    }

    /* ---- ALT column ---- */
    if (!hold) {
        MfdRect box = mfd_rect(396.0f, y0, 161.0f, h);
        if (eligible) {
            mfd_ui_hit(ui, id_alt, MFD_PART_MAIN, box);
            if (mfd_ui_is_hovered(ui, id_alt)) mfd_draw_bevel(dl, box, 2.0f, MFD_COL_CYAN, MFD_BEVEL_SOLID);
            if (mfd_ui_was_clicked(ui, id_alt) && !v->st->efob_mode) fpln_vert_rev(v, ln, "alt");
        }
        if (v->st->efob_mode) {
            if (!v->active) {
                fpln_text_c(ui, 535.0f, cy, MFD_ANCHOR_RIGHT, 22.0f, tc, "---" DEG "/---");
            } else {
                char dir[16], mag[16], pd[16], pm[16];
                const A380XFplnWptPred *p = &ln->pred;
                int has_w = p->valid && p->wind_kind != 0;
                int prev_w = fpln_is_wpt(prev) && prev->pred.valid && prev->pred.wind_kind != 0;
                fpln_wind_dir(p, dir, sizeof(dir));
                fpln_wind_mag(p, mag, sizeof(mag));
                if (prev_w) {
                    fpln_wind_dir(&prev->pred, pd, sizeof(pd));
                    fpln_wind_mag(&prev->pred, pm, sizeof(pm));
                }
                if (!has_w) {
                    snprintf(dir, sizeof(dir), "---");
                    snprintf(mag, sizeof(mag), "--");
                } else if (prev_w && !first && !after_sp) {
                    if (!strcmp(pd, dir)) snprintf(dir, sizeof(dir), "\"");
                    if (!strcmp(pm, mag)) snprintf(mag, sizeof(mag), "\""); /* [PORT] same exclusion */
                }
                fpln_text_c(ui, 453.5f, cy, MFD_ANCHOR_CENTER, 22.0f, tc, dir);
                fpln_text_c(ui, 483.0f, cy, MFD_ANCHOR_CENTER, 22.0f, tc, "/");
                fpln_text_c(ui, 512.5f, cy, MFD_ANCHOR_CENTER, 22.0f, tc, mag);
            }
        } else if (!v->active) {
            fpln_text_c(ui, 437.5f, cy, MFD_ANCHOR_LEFT, 22.0f, tc, "---");
        } else {
            const A380XFplnWptPred *p = &ln->pred;
            int has_pred = p->valid && isfinite(p->altitude_ft) && p->altitude_ft != 0.0f;
            int has_c = fpln_has_alt(ln);
            double ta = ln->use_trans_level && !ln->is_pseudo ? v->tl : v->ta;
            snprintf(t, sizeof(t), "---");
            if (has_pred) {
                if (fpln_is_wpt(prev) && prev->pred.valid && isfinite(prev->pred.altitude_ft) &&
                    prev->pred.altitude_ft != 0.0f && fabsf(prev->pred.altitude_ft - p->altitude_ft) < 100.0f &&
                    !has_c && !first && !after_sp)
                    snprintf(t, sizeof(t), "\"");
                else if (isfinite(ta) && p->altitude_ft >= ta)
                    snprintf(t, sizeof(t), "FL%03.0f", floor(p->altitude_ft / 100.0 + 0.5));
                else
                    snprintf(t, sizeof(t), "%.0f", floor(p->altitude_ft / 10.0 + 0.5) * 10.0);
            }
            if (has_c && has_pred) {
                fpln_text_c(ui, 427.5f, cy, MFD_ANCHOR_CENTER, 30.0f, p->alt_cstr_met ? MFD_COL_MAGENTA : MFD_COL_AMBER,
                            "*");
                fpln_text_c(ui, 437.5f, cy, MFD_ANCHOR_LEFT, 22.0f, tc, t);
            } else if (has_c && isfinite(ln->alt_cstr.alt1_ft)) {
                const A380XFplnAltCstr *c = &ln->alt_cstr;
                if (isfinite(c->alt2_ft)) {
                    snprintf(t, sizeof(t), "WINDOW");
                } else {
                    const char *pre = "";
                    char val[16];
                    if (c->desc == '+' || c->desc == 'V' || c->desc == 'J' || c->desc == 'H') pre = "+";
                    else if (c->desc == '-' || c->desc == 'Y') pre = "-";
                    if (!isfinite(ta) || c->alt1_ft < ta) snprintf(val, sizeof(val), "%.0f", (double)c->alt1_ft);
                    else snprintf(val, sizeof(val), "FL%.0f", floor(c->alt1_ft / 100.0 + 0.5));
                    snprintf(t, sizeof(t), "%s%s", pre, val);
                }
                fpln_text_c(ui, 417.5f, cy, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_MAGENTA, t);
            } else {
                fpln_text_c(ui, 437.5f, cy, MFD_ANCHOR_LEFT, 22.0f, tc, t);
            }
        }
    }

    /* ---- connector ---- */
    fpln_connector(dl, y0, first, last, after_sp, before_sp, active, before_active, ln->is_pseudo, hold, lc);

    /* ---- TRK / DIST / FPA (upper row) ---- */
    if (!first && !after_sp && !hold) {
        if (isfinite(ln->trk_mag_deg) && !(isfinite(ln->dist_nm) && ln->dist_nm < 0.05f)) {
            double trk = floor(ln->trk_mag_deg + 0.5);
            if (trk <= 0.0) trk += 360.0; /* [PORT] 360 for north */
            snprintf(t, sizeof(t), "%03.0f" DEG, trk);
            fpln_text_top(ui, 594.0f, y0 + 1.0f, MFD_ANCHOR_LEFT, 22.0f, tc, t);
        }
        if (isfinite(ln->dist_nm)) {
            snprintf(t, sizeof(t), "%.0f", floor(ln->dist_nm + 0.5));
            fpln_text_top(ui, 705.0f, y0 + 1.0f, MFD_ANCHOR_RIGHT, 22.0f, tc, t);
        }
        if (!ln->is_pseudo && isfinite(ln->fpa_deg) && ln->fpa_deg != 0.0f) {
            mfd_js_to_fixed(ln->fpa_deg, 1, t, sizeof(t));
            fpln_text_top(ui, 766.0f, y0 + 1.0f, MFD_ANCHOR_RIGHT, 22.0f, tc, t);
        }
    }
}

/* ---- navigation from a row ------------------------------------------------------------------ */

static void fpln_uri(const MfdPageContext *ctx, const char *page, char *buf, size_t n)
{
    snprintf(buf, n, "fms/%.12s/%.40s", ctx->uri->category[0] ? ctx->uri->category : "active", page);
}

static void fpln_vert_rev(FplnView *v, const A380XFplnLine *ln, const char *tab)
{
    char uri[MFD_URI_LEN], page[40];
    a380x_mfd_fpln_set_revised(v->ctx->side, v->uplan, ln->is_altn, ln->leg_index);
    snprintf(page, sizeof(page), "f-pln-vert-rev/%.20s", tab);
    fpln_uri(v->ctx, page, uri, sizeof(uri));
    mfd_navigate(v->ctx, uri);
}

/* Menu types (FBW FplnRevisionsMenuType) */
enum { FPLN_RT_WAYPOINT = 0, FPLN_RT_DEPARTURE, FPLN_RT_ARRIVAL, FPLN_RT_DISCO };

static void fpln_open_rev_menu(FplnView *v, int d, const A380XFplnLine *ln)
{
    MfdFplnState *st = v->st;
    const A380XFplnRoute *r = ln->is_altn ? &v->plan->alt : &v->plan->pri;
    const A380XFplnLeg *l;
    if (st->rev_open || ln->leg_index < 0 || ln->leg_index >= r->count) return; /* not if already open */
    l = &r->el[ln->leg_index];
    st->rev_open = 1;
    st->rev_line = d;
    st->rev_lplan = v->lplan;
    st->rev_altn = ln->is_altn;
    st->rev_leg = ln->leg_index;
    st->rev_version = v->plan->version;
    if (a380x_fpln_is_disco(l)) st->rev_type = FPLN_RT_DISCO;
    else {
        A380XFplnSegmentClass c = a380x_fpln_segment_class(l->segment);
        st->rev_type = c == A380X_SEGCLASS_DEPARTURE ? FPLN_RT_DEPARTURE
                       : c == A380X_SEGCLASS_ARRIVAL ? FPLN_RT_ARRIVAL : FPLN_RT_WAYPOINT;
    }
    a380x_mfd_fpln_set_revised(v->ctx->side, v->uplan, ln->is_altn, ln->leg_index);
    mfd_context_menu_open(v->ctx->ui, ID(130), FPLN_MENU_X, FPLN_MENU_Y);
}

/* ---- lateral revision menu (spec 05 section 4) ------------------------------------------------ */

static int fpln_is_vectors_leg(const A380XFplnLeg *l)
{
    return l && !a380x_fpln_is_disco(l) && a380x_leg_is_vectors(l->type);
}

static void fpln_rev_menu(FplnView *v)
{
    MfdPageContext *ctx = v->ctx;
    MfdFplnState *st = v->st;
    const A380XFplnRoute *r;
    const A380XFplnLeg *L, *prev;
    MfdMenuItem it[15];
    int idx, A, T, P, is_from, db_fix, k, disco, has_altn, edit = a380x_mfd_fpln_edit_plan(ctx);
    if (!st->rev_open) return;
    if (!mfd_context_menu_is_open(ctx->ui, ID(130)) || st->rev_lplan != v->lplan || v->plan->version != st->rev_version) {
        st->rev_open = 0;
        if (mfd_context_menu_is_open(ctx->ui, ID(130))) mfd_ui_close_menus(ctx->ui);
        return;
    }
    A = st->rev_altn;
    r = A ? &v->plan->alt : &v->plan->pri;
    idx = st->rev_leg;
    if (idx < 0 || idx >= r->count) {
        st->rev_open = 0;
        return;
    }
    L = &r->el[idx];
    prev = idx > 0 ? &r->el[idx - 1] : NULL; /* [PORT] the plan the row belongs to */
    T = st->rev_type;
    P = v->lplan;
    disco = a380x_fpln_is_disco(L);
    is_from = !A && idx == a380x_fpln_from_leg_index(v->plan);
    db_fix = a380x_fpln_is_db_fix(L);
    has_altn = v->plan->alt.dest_icao[0] != '\0' || v->plan->alt.count > 0;

    it[0].label = P >= A380X_PLAN_SEC1 ? NBSP : "FROM P.POS DIR TO";
    it[0].disabled = A || idx >= r->first_missed_appr_index || P == A380X_PLAN_TEMPORARY || P >= A380X_PLAN_SEC1 ||
                     T == FPLN_RT_DISCO || is_from || !db_fix;
    it[1].label = "INSERT NEXT WPT";
    it[1].disabled = fpln_is_vectors_leg(L);
    it[2].label = "DELETE *";
    it[2].disabled = (disco && fpln_is_vectors_leg(prev)) || is_from;
    it[3].label = "DEPARTURE";
    it[3].disabled = T != FPLN_RT_DEPARTURE;
    it[4].label = "ARRIVAL";
    it[4].disabled = T != FPLN_RT_ARRIVAL;
    it[5].label = "(N/A) OFFSET";
    it[5].disabled = 1;
    it[6].label = "HOLD";
    it[6].disabled = T == FPLN_RT_DISCO || is_from || !db_fix;
    it[7].label = "AIRWAYS";
    it[7].disabled = T == FPLN_RT_DISCO || is_from || !db_fix || L->wp_desc == A380X_WPDESC_AIRPORT ||
                     L->wp_desc == A380X_WPDESC_RUNWAY;
    it[8].label = (!A && T != FPLN_RT_DISCO && (L->flags & A380X_LEGF_OVERFLY)) ? "DELETE OVERFLY *" : "OVERFLY *";
    it[8].disabled = A || T == FPLN_RT_DISCO || is_from || !db_fix;
    it[9].label = "ENABLE ALTN *";
    it[9].disabled = A || disco || !has_altn;
    it[10].label = "NEW DEST";
    it[10].disabled = is_from || !db_fix;
    it[11].label = "CONSTRAINTS";
    it[11].disabled = T == FPLN_RT_DISCO || !a380x_fpln_constraint_revision_allowed(L);
    it[12].label = "(N/A) CMS";
    it[12].disabled = 1;
    it[13].label = "STEP ALTs";
    it[13].disabled = A || !db_fix || T == FPLN_RT_DISCO;
    it[14].label = "(N/A) WIND";
    it[14].disabled = 1;

    k = mfd_context_menu(ctx->ui, ID(130), it, 15);
    if (k < 0) {
        if (!mfd_context_menu_is_open(ctx->ui, ID(130))) st->rev_open = 0;
        return;
    }
    st->rev_open = 0;
    {
        char uri[MFD_URI_LEN];
        switch (k) {
        case 0: {
            A380XFplnAircraft ac;
            a380x_mfd_fpln_aircraft(ctx, &ac);
            if (a380x_mfd_fpln_result(ctx, a380x_fpln_direct_to_leg(v->fp, A380X_PLAN_ACTIVE, &ac, idx,
                                                                    A380X_DIRTO_DIRECT, NAN)))
                mfd_navigate(ctx, "fms/active/f-pln-direct-to");
            break;
        }
        case 1:
            st->window = 1;
            break;
        case 2:
            a380x_mfd_fpln_result(ctx, a380x_fpln_delete_element(v->fp, edit, A, idx));
            break;
        case 3:
            fpln_uri(ctx, "f-pln-departure", uri, sizeof(uri));
            mfd_navigate(ctx, uri);
            break;
        case 4:
            fpln_uri(ctx, "f-pln-arrival", uri, sizeof(uri));
            mfd_navigate(ctx, uri);
            break;
        case 6:
            if (!a380x_leg_is_hx(L->type)) {
                A380XFplnHold dflt;
                A380XFplnAltCstr c = a380x_fpln_eff_alt(L);
                A380XFplnAircraft ac;
                float alt;
                int rc;
                a380x_mfd_fpln_aircraft(ctx, &ac);
                alt = isfinite(c.alt1_ft) ? c.alt1_ft : ac.alt_ft;
                rc = a380x_fpln_default_hold(v->fp, edit, A, idx, alt, 0, &dflt);
                if (rc == A380X_FPLN_OK) rc = a380x_fpln_add_or_edit_hold(v->fp, edit, A, idx, &dflt, NULL, &dflt);
                if (rc >= 0) {
                    a380x_mfd_fpln_result(ctx, A380X_FPLN_OK);
                    a380x_mfd_fpln_set_revised(ctx->side, v->uplan, A, rc);
                } else {
                    a380x_mfd_fpln_result(ctx, rc);
                    break;
                }
            }
            fpln_uri(ctx, "f-pln-hold", uri, sizeof(uri));
            mfd_navigate(ctx, uri);
            break;
        case 7:
            if (a380x_mfd_fpln_result(ctx, a380x_fpln_airway_start(v->fp, edit, A, idx))) {
                fpln_uri(ctx, "f-pln-airways", uri, sizeof(uri));
                mfd_navigate(ctx, uri);
            }
            break;
        case 8:
            a380x_mfd_fpln_result(ctx, a380x_fpln_toggle_overfly(v->fp, edit, A, idx));
            break;
        case 9:
            a380x_mfd_fpln_result(ctx, a380x_fpln_enable_altn(v->fp, edit, idx, NULL));
            break;
        case 10:
            st->window = 2;
            break;
        case 11:
            fpln_uri(ctx, "f-pln-vert-rev/alt", uri, sizeof(uri));
            mfd_navigate(ctx, uri);
            break;
        case 13:
            fpln_uri(ctx, "f-pln-vert-rev/step-alts", uri, sizeof(uri));
            mfd_navigate(ctx, uri);
            break;
        default:
            break;
        }
    }
}

/* ---- INSERT NEXT WPT and NEW DEST windows (spec 05 sections 5 and 6) -------------------------- */

static void fpln_fix_from_ref(const A380XNavRef *r, A380XNavFix *f)
{
    memset(f, 0, sizeof(*f));
    f->kind = (A380XNavKind)r->kind;
    f->flags = r->flags;
    snprintf(f->ident, sizeof(f->ident), "%s", r->ident);
    snprintf(f->region, sizeof(f->region), "%s", r->region);
    f->lat = r->lat;
    f->lon = r->lon;
    f->freq = r->freq;
    f->station_var = r->station_var;
    f->elev_ft = f->loc_true_brg = f->loc_mag_brg = f->gs_angle_deg = NAN;
    f->dme_lat = r->lat;
    f->dme_lon = r->lon;
    f->db_id = -1;
}

static const A380XFplnLeg *fpln_rev_leg(const FplnView *v)
{
    const A380XFplnRoute *r = v->st->rev_altn ? &v->plan->alt : &v->plan->pri;
    if (v->st->rev_leg < 0 || v->st->rev_leg >= r->count) return NULL;
    return &r->el[v->st->rev_leg];
}

static void fpln_window_frame(MfdPageContext *ctx, MfdRect box)
{
    mfd_ui_hit(ctx->ui, ID(140), MFD_PART_BLOCKER, box);
    mfd_draw_box(ctx->dl, box, MFD_COL_BLACK, 2.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_RAISED);
}

static void fpln_close_window(FplnView *v)
{
    v->st->window = 0;
    a380x_mfd_fpln_reset_revised(v->ctx->side);
}

static void fpln_next_wpt_insert(FplnView *v, const A380XNavFix *f)
{
    a380x_mfd_fpln_result(v->ctx, a380x_fpln_next_waypoint(v->fp, a380x_mfd_fpln_edit_plan(v->ctx), v->st->rev_altn,
                                                           v->st->rev_leg, f));
}

static void fpln_next_wpt_window(FplnView *v)
{
    MfdPageContext *ctx = v->ctx;
    MfdUi *ui = ctx->ui;
    const A380XFplnLeg *L = fpln_rev_leg(v);
    const A380XFplnRoute *r = v->st->rev_altn ? &v->plan->alt : &v->plan->pri;
    MfdRect box = mfd_rect(180.0f, 193.0f, 500.0f, 625.0f);
    const char *vals[64];
    int map[64], n = 0, i, sel = -1;
    char coord[48], typed[32];
    MfdDropdownDesc dd;
    MfdLayer old = mfd_draw_get_layer(ctx->dl);
    if (!L && v->st->rev_leg >= r->count) {
        fpln_close_window(v);
        return;
    }
    mfd_draw_set_layer(ctx->dl, MFD_LAYER_DIALOG);
    fpln_window_frame(ctx, box);
    mfd_text(ui, 197.0f, mfd_baseline(212.0f, 20.0f), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "INSERT NEXT WPT FROM" NBSP);
    if (L && !a380x_fpln_is_disco(L)) {
        mfd_text(ui, 197.0f + mfd_text_width("INSERT NEXT WPT FROM" NBSP, 20.0f), mfd_baseline(207.0f, 30.0f),
                 MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, L->ident);
        a380x_mfd_fpln_format_latlon(L->fix.lat, L->fix.lon, coord, sizeof(coord));
        mfd_text(ui, 247.0f, mfd_baseline(247.0f, 30.0f), MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, coord);
    }
    for (i = v->st->rev_leg + 1; i < r->count && n < 64; ++i) {
        const A380XFplnLeg *l = &r->el[i];
        if (a380x_fpln_is_disco(l) || !a380x_leg_is_xf(l->type) || !l->ident[0]) continue;
        vals[n] = l->ident;
        map[n] = i;
        n++;
    }
    memset(&dd, 0, sizeof(dd));
    dd.values = vals;
    dd.count = n;
    dd.selected = -1;
    dd.free_text = 1;
    dd.digits = 7;
    dd.tmpy = v->tmpy;
    typed[0] = '\0';
    if (mfd_dropdown(ui, ID(141), mfd_rect(247.0f, 287.0f, 175.0f, MFD_DROPDOWN_H), &dd, &sel, typed, sizeof(typed))) {
        if (sel >= 0 && sel < n) {
            A380XNavFix f;
            fpln_fix_from_ref(&r->el[map[sel]].fix, &f);
            fpln_next_wpt_insert(v, &f);
            fpln_close_window(v);
        } else if (typed[0]) {
            A380XNavFix fx[A380X_MFD_DUP_MAX];
            int c = a380x_mfd_fpln_resolve_place(ctx, typed, fx, A380X_MFD_DUP_MAX);
            if (c == 1) {
                fpln_next_wpt_insert(v, &fx[0]);
                fpln_close_window(v);
            } else if (c > 1) {
                a380x_mfd_fpln_dup_open(ctx->side, FPLN_REQ_NEXT, fx, c);
                v->st->dup_wait = 1;
                v->st->window = 0; /* revised context kept until the choice */
            } else {
                fpln_close_window(v);
            }
        }
    }
    if (mfd_button(ui, ID(142), mfd_rect(192.0f, 771.0f, 0.0f, 0.0f), "CANCEL", 0)) fpln_close_window(v);
    mfd_draw_set_layer(ctx->dl, old);
}

static void fpln_new_dest_window(FplnView *v)
{
    MfdPageContext *ctx = v->ctx;
    MfdUi *ui = ctx->ui;
    const A380XFplnLeg *L = fpln_rev_leg(v);
    MfdRect box = mfd_rect(200.0f, 368.0f, 500.0f, 300.0f);
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    MfdFieldResult fr;
    float fw;
    MfdLayer old = mfd_draw_get_layer(ctx->dl);
    mfd_draw_set_layer(ctx->dl, MFD_LAYER_DIALOG);
    fpln_window_frame(ctx, box);
    mfd_text(ui, 217.0f, mfd_baseline(415.0f, 20.0f), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "NEW DEST FROM" NBSP);
    if (L) mfd_text(ui, 217.0f + mfd_text_width("NEW DEST FROM" NBSP, 20.0f), mfd_baseline(410.0f, 30.0f),
                    MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, L->ident);
    mfd_field_desc_init(&fd, MFD_FMT_TXT_AIRPORT);
    memset(&fv, 0, sizeof(fv));
    fw = mfd_field_auto_width(&fd, &fv);
    fr = mfd_input_field(ui, ID(143), 450.0f - fw * 0.5f, 490.0f, &fd, &fv, &out);
    if (fr == MFD_FIELD_COMMITTED && out.str[0]) {
        if (strlen(out.str) != 4) {
            MfdFmsError e;
            mfd_err_set_format(&e, "AAAA", "");
            mfd_ui_error(ui, &e, 1);
        } else {
            a380x_mfd_fpln_result(ctx, a380x_fpln_new_dest(v->fp, a380x_mfd_fpln_edit_plan(ctx), v->st->rev_altn,
                                                           v->st->rev_leg, out.str));
        }
        fpln_close_window(v);
    }
    if (mfd_button(ui, ID(144), mfd_rect(212.0f, 621.0f, 0.0f, 0.0f), "CANCEL", 0)) fpln_close_window(v);
    mfd_draw_set_layer(ctx->dl, old);
}

/* ---- header band, TMPY row, destination line, button row -------------------------------------- */

static void fpln_header(FplnView *v)
{
    MfdUi *ui = v->ctx->ui;
    MfdButtonDesc bd;
    MfdMenuItem item;
    float cy = 165.5f;
    int k;
    fpln_text_c(ui, 3.0f, cy, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "FROM");
    fpln_text_c(ui, 236.0f, cy, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE,
                (v->phase == A380X_FMS_PHASE_PREFLIGHT || v->phase == A380X_FMS_PHASE_DONE) ? "TIME" : "UTC");
    item.label = v->st->efob_mode ? "SPD" NBSP NBSP NBSP NBSP NBSP "ALT" : "EFOB" NBSP NBSP NBSP NBSP "T.WIND";
    item.disabled = 0;
    mfd_button_desc_init(&bd, v->st->efob_mode ? "EFOB" NBSP NBSP NBSP NBSP "T.WIND"
                                               : NBSP "SPD" NBSP NBSP NBSP NBSP "ALT" NBSP NBSP NBSP);
    bd.menu = &item;
    bd.menu_count = 1;
    bd.scroll_to = -1;
    k = mfd_button_ex(ui, ID(1), mfd_rect(315.0f, FPLN_HDR_Y0, 260.0f, 43.0f), &bd);
    if (k == 0) v->st->efob_mode = !v->st->efob_mode;
    fpln_text_c(ui, 592.0f, cy, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "TRK");
    fpln_text_c(ui, 707.0f, cy, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "DIST");
    fpln_text_c(ui, 768.0f, cy, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "FPA");
    mfd_hrule(ui, 0.0f, 768.0f, FPLN_HDR_Y1, 1.0f, MFD_COL_LIGHT_GREY);
}

static void fpln_tmpy_row(FplnView *v)
{
    float y = 823.0f - mfd_button_height(2);
    if (a380x_mfd_fpln_star_button(v->ctx->ui, ID(110), mfd_rect(0.0f, y, 111.0f, 0.0f), "ERASE", "TMPY",
                                   MFD_COL_AMBER, 0)) {
        a380x_mfd_fpln_tmpy_erase(v->ctx);
        v->st->rev_open = 0;
    }
    if (a380x_mfd_fpln_star_button(v->ctx->ui, ID(111), mfd_rect(643.0f, y, 125.0f, 0.0f), "INSERT", "TMPY",
                                   MFD_COL_AMBER, 0)) {
        a380x_mfd_fpln_tmpy_insert(v->ctx);
        v->st->rev_open = 0;
    }
}

static int fpln_dest_button(MfdUi *ui, MfdId id, MfdRect r, const char *label)
{
    MfdDrawList *dl = ui->dl;
    int hov;
    mfd_ui_hit(ui, id, MFD_PART_MAIN, r);
    hov = mfd_ui_is_hovered(ui, id);
    mfd_draw_box(dl, r, MFD_COL_DARKER_GREY, 2.0f, hov ? MFD_COL_CYAN : MFD_COL_LIGHT_GREY, MFD_BEVEL_RAISED);
    fpln_text_c(ui, r.x + r.w * 0.5f, r.y + r.h * 0.5f, MFD_ANCHOR_CENTER, 30.0f, MFD_COL_WHITE, label);
    return mfd_ui_was_clicked(ui, id);
}

static void fpln_dest_line(FplnView *v, const FplnScan *sc)
{
    MfdPageContext *ctx = v->ctx;
    MfdUi *ui = ctx->ui;
    const A380XFplnRoute *r = v->plan ? &v->plan->pri : NULL;
    int has_dest = r && r->dest_icao[0];
    float cy = FPLN_DEST_CY;
    char t[32];
    MfdColor vc_none = v->tmpy ? MFD_COL_YELLOW : (v->active ? MFD_COL_GREEN : MFD_COL_WHITE);
    MfdColor vc = v->pcol;
    mfd_hrule(ui, 0.0f, 768.0f, FPLN_DEST_SEP, 1.0f, MFD_COL_LIGHT_GREY);
    if (has_dest) {
        char label[24];
        if (r->dest_rwy[0]) a380x_fpln_runway_display(r, 1, label, sizeof(label));
        else snprintf(label, sizeof(label), "%s", r->dest_icao);
        if (fpln_dest_button(ui, ID(103), mfd_rect(0.0f, cy - 22.5f, 150.0f, 45.0f), label)) {
            char uri[MFD_URI_LEN];
            a380x_mfd_fpln_reset_revised(ctx->side);
            fpln_uri(ctx, "f-pln-arrival", uri, sizeof(uri));
            mfd_navigate(ctx, uri);
        }
    } else {
        fpln_text_c(ui, 0.0f, cy, MFD_ANCHOR_LEFT, 20.0f, v->active ? MFD_COL_GREEN : MFD_COL_WHITE, "--------");
    }
    /* time */
    {
        int ok = 0;
        if (v->active && ctx->fms) {
            A380XFmsValue e = a380x_fms_get(ctx->fms, A380X_PLAN_ACTIVE, A380X_FMS_F_DEST_ETA);
            if (e.has) {
                fpln_hhmm(e.v, t, sizeof(t));
                ok = 1;
            }
        }
        if (!ok) snprintf(t, sizeof(t), "--:--");
        fpln_text_c(ui, 193.0f, cy, MFD_ANCHOR_LEFT, 20.0f, ok ? vc : vc_none, t);
    }
    /* EFOB */
    {
        int ok = 0;
        MfdColor c = vc_none;
        if (v->active && ctx->fms) {
            A380XFmsValue e = a380x_fms_get(ctx->fms, A380X_PLAN_ACTIVE, A380X_FMS_F_DEST_EFOB);
            if (e.has && isfinite(e.v)) {
                double w = v->metric ? e.v : e.v / MFD_KG_PER_LB;
                mfd_js_to_fixed(w / 1000.0, 1, t, sizeof(t));
                ok = 1;
                c = e.colour == A380X_FMS_COLOUR_AMBER ? MFD_COL_AMBER : vc;
            }
        }
        if (!ok) snprintf(t, sizeof(t), "---.-");
        fpln_text_c(ui, 364.0f, cy, MFD_ANCHOR_RIGHT, 20.0f, c, t);
        if (ok) fpln_text_c(ui, 370.0f, cy, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, v->metric ? "T" : "KLB");
    }
    /* distance */
    {
        float d = NAN;
        if (v->active) {
            A380XFplnAircraft ac;
            a380x_mfd_fpln_aircraft(ctx, &ac);
            d = a380x_fpln_dist_to_dest(v->fp, A380X_PLAN_ACTIVE, &ac);
        }
        if (isfinite(d)) snprintf(t, sizeof(t), "%.0f", floor(d + 0.5));
        else snprintf(t, sizeof(t), "----");
        fpln_text_c(ui, 484.0f, cy, MFD_ANCHOR_RIGHT, 20.0f, isfinite(d) ? vc : vc_none, t);
        if (isfinite(d)) fpln_text_c(ui, 490.0f, cy, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, "NM");
    }
    /* scroll + DEST */
    {
        MfdFplnState *st = v->st;
        int lo = sc->from_line < 0 ? 0 : sc->from_line;
        if (mfd_icon_button(ui, ID(100), mfd_rect(560.0f, cy - 30.0f, 60.0f, 60.0f), MFD_ICON_DOUBLE_DOWN,
                            st->from >= sc->count - v->slots)) {
            int f = st->from + 8;
            if (f > sc->count - 9) f = sc->count - 9;
            st->from = fpln_clamp(f > st->from ? f : st->from + 1, sc, v->slots);
        }
        if (mfd_icon_button(ui, ID(101), mfd_rect(620.0f, cy - 30.0f, 60.0f, 60.0f), MFD_ICON_DOUBLE_UP,
                            st->from <= lo))
            st->from = fpln_clamp(st->from - 8 < 0 ? 0 : st->from - 8, sc, v->slots);
        if (mfd_button(ui, ID(102), mfd_rect(680.0f, cy - 30.0f, 83.0f, 60.0f), "DEST", has_dest ? 0u : MFD_BTN_DISABLED))
            st->from = fpln_clamp(sc->dest_line >= 0 ? sc->dest_line + 1 - v->slots : 0, sc, v->slots);
    }
    mfd_hrule(ui, 0.0f, 768.0f, FPLN_BTN_SEP, 1.0f, MFD_COL_LIGHT_GREY);
}

static void fpln_button_row(FplnView *v)
{
    MfdPageContext *ctx = v->ctx;
    MfdUi *ui = ctx->ui;
    static const MfdMenuItem info_items[7] = {
        {"ALTERNATE", 1}, {"CLOSEST AIRPORTS", 1}, {"EQUI-TIME POINT", 1}, {"FIX INFO", 0},
        {"LL CROSSING", 1}, {"TIME", 1}, {"CPNY F-PLN REPORT", 1}};
    MfdMenuItem items[7];
    int k;
    if (v->sec) {
        if (mfd_button(ui, ID(120), mfd_rect(0.0f, FPLN_BTN_Y, 125.0f, MFD_BUTTON_H), "RETURN", 0)) {
            char uri[MFD_URI_LEN];
            snprintf(uri, sizeof(uri), "fms/sec/index/%d", ctx->plan - 2);
            mfd_navigate(ctx, uri);
        }
    } else if (v->phase == A380X_FMS_PHASE_PREFLIGHT) {
        if (mfd_button(ui, ID(120), mfd_rect(0.0f, FPLN_BTN_Y, 125.0f, MFD_BUTTON_H), "INIT", 0))
            mfd_navigate(ctx, "fms/active/init");
    }
    memcpy(items, info_items, sizeof(items));
    items[3].disabled = v->sec;
    k = mfd_menu_button(ui, ID(121), mfd_rect(290.0f, FPLN_BTN_Y, 189.0f, MFD_BUTTON_H), "F-PLN INFO", items, 7, 0);
    if (k == 3) mfd_navigate(ctx, "fms/active/f-pln-fix-info");
    if (!v->sec && mfd_button(ui, ID(122), mfd_rect(648.0f, FPLN_BTN_Y, 110.0f, MFD_BUTTON_H), "DIR TO", 0))
        mfd_navigate(ctx, "fms/active/f-pln-direct-to");
}

/* ---- page ------------------------------------------------------------------------------------ */

static int fpln_wheel(MfdUi *ui)
{
    int n = mfd_ui_wheel_notches(ui, ID(5)), s, k;
    for (s = 0; s < FPLN_MAX_SLOTS; ++s)
        for (k = 0; k < 6; ++k) n += mfd_ui_wheel_notches(ui, ID(10 + s * 8 + k));
    return n;
}

static void fpln_page(MfdPageContext *ctx)
{
    MfdFplnState *st = MFD_PAGE_STATE(ctx, MfdFplnState);
    MfdUi *ui = ctx->ui;
    FplnView v;
    FplnScan sc;
    A380XFplnLine win[FPLN_MAX_SLOTS + 2];
    int have[FPLN_MAX_SLOTS + 2], i, n;

    memset(&v, 0, sizeof(v));
    v.ctx = ctx;
    v.st = st;
    v.fp = a380x_mfd_fpln_store(ctx);
    v.feed = fpln_env.feed;
    v.uplan = ctx->plan < 0 ? 0 : ctx->plan;
    v.lplan = a380x_mfd_fpln_lateral_plan(ctx);
    if (ctx->entered) {
        st->from = 0;
        st->pending_extra = !strcmp(ctx->uri->extra, "top") ? 1 : !strcmp(ctx->uri->extra, "dest") ? 2 : 0;
        st->rev_open = 0;
        st->window = 0;
        st->last_lplan = -2;
    }
    /* SEC plan deleted while shown: same URI on the active category (spec 05 section 1.2). */
    if (v.fp && ctx->plan >= A380X_PLAN_SEC1 && v.lplan < 0) {
        mfd_navigate(ctx, "fms/active/f-pln");
        return;
    }
    v.plan = v.lplan >= 0 ? a380x_fpln_plan(v.fp, v.lplan) : NULL;
    v.sec = ctx->plan >= A380X_PLAN_SEC1;
    v.tmpy = v.lplan == A380X_PLAN_TEMPORARY;
    v.active = v.lplan == A380X_PLAN_ACTIVE;
    v.pcol = v.sec ? MFD_COL_WHITE : a380x_mfd_fpln_plan_colour(v.lplan);
    v.slots = v.tmpy ? 8 : 9;
    v.phase = a380x_mfd_fpln_phase(ctx);
    v.metric = ctx->in ? ctx->in->metric_units : 1;
    v.utc = a380x_mfd_fpln_utc(ctx);
    v.ett = a380x_mfd_fpln_ett();
    a380x_mfd_fpln_trans(ctx, v.lplan, &v.ta, &v.tl);

    fpln_scan(&v, &sc);
    if (st->pending_extra && sc.count > 0) {
        if (st->pending_extra == 1) st->from = sc.active_line > 0 ? sc.active_line - 1 : 0;
        else st->from = sc.dest_line >= 0 ? sc.dest_line + 1 - v.slots : 0;
        st->pending_extra = 0;
    }
    if (v.plan && (v.plan->version != st->last_version || v.plan->active_leg_index != st->last_active ||
                   v.lplan != st->last_lplan)) {
        if (st->rev_open && v.plan->version != st->rev_version) st->rev_open = 0;
        st->last_version = v.plan->version;
        st->last_active = v.plan->active_leg_index;
        st->last_lplan = v.lplan;
    }

    /* scrolling: KCCU UP/DOWN and the wheel over the list (not while a popup is open) */
    if (!st->rev_open && !st->window && !mfd_ui_has_focus(ui)) {
        MfdKey k;
        char ch;
        int w = fpln_wheel(ui);
        if (mfd_ui_peek_key(ui, &k, &ch) && (k == MFD_KEY_UP || k == MFD_KEY_DOWN)) {
            st->from += k == MFD_KEY_UP ? -1 : 1;
            mfd_ui_consume_key(ui);
        }
        st->from -= w;
    }
    st->from = fpln_clamp(st->from, &sc, v.slots);

    /* collect lines from-1 .. from+slots */
    for (i = 0; i < FPLN_MAX_SLOTS + 2; ++i) have[i] = 0;
    if (v.plan) {
        A380XFplnLineIter it;
        A380XFplnLine ln;
        a380x_fpln_lines_begin(&it, v.fp, v.lplan, v.feed);
        n = 0;
        while (a380x_fpln_lines_next(&it, &ln)) {
            int k = n - (st->from - 1);
            if (k >= 0 && k < v.slots + 2) {
                win[k] = ln;
                have[k] = 1;
            }
            n++;
            if (n > st->from + v.slots) break;
        }
    }

    fpln_header(&v);
    mfd_ui_hit(ui, ID(5), MFD_PART_MAIN, mfd_rect(0.0f, 189.0f, 768.0f, 632.0f));
    for (i = 0; i < v.slots; ++i) {
        int k = i + 1;
        if (!have[k]) break;
        fpln_row(&v, i, st->from + i, &win[k], have[k - 1] ? &win[k - 1] : NULL, have[k + 1] ? &win[k + 1] : NULL);
    }
    if (v.tmpy) fpln_tmpy_row(&v);
    fpln_dest_line(&v, &sc);
    fpln_button_row(&v);

    if (v.plan) {
        fpln_rev_menu(&v);
        if (st->window == 1) fpln_next_wpt_window(&v);
        else if (st->window == 2) fpln_new_dest_window(&v);
    } else {
        st->rev_open = 0;
        st->window = 0;
    }
    {
        A380XNavFix f;
        int r = a380x_mfd_fpln_dup_run(ctx, FPLN_REQ_NEXT, &f);
        if (st->dup_wait && r != 0) {
            if (r == 1 && v.plan) fpln_next_wpt_insert(&v, &f);
            st->dup_wait = 0;
            a380x_mfd_fpln_reset_revised(ctx->side);
        }
    }
}

void a380x_mfd_page_fpln_register(void)
{
    mfd_register_page("fms/*/f-pln", MFD_PAGE_ID_FPLN, "F-PLN", fpln_page,
                      MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY | MFD_PAGE_TMPY);
}
