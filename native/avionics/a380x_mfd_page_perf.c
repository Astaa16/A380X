/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD FMS PERF page (ACTIVE/PERF, SECn/PERF). Translation of the pinned FBW
 * pages/FMS/MfdFmsPerf.tsx + MfdFmsPerf.scss (docs/mfd-spec/04-fms-perf.md).
 *
 * Geometry: every position below was re-derived from the FBW CSS under the port box model
 * (border-box, line box = 1.0 em, InputField 37 px, Button 18 + 22 x lines, DropdownMenu 44 px,
 * radio row 34 px) on the spec 01 frame (page body y 138..950). The vertical stack of the page
 * container is: top strip (margin 5) 143..205, tab navigator (margin 2) 212..883 (bar 212..248,
 * content frame 248..883), page footer row 885..947 (margin-bottom 3).
 *
 * Data: the FMS core is the single source of truth (values, flags, formats, validation,
 * side-effect messages and simvar-style outputs). The page only keeps the selected tab, the
 * open dialog and the page-local DES PRED TO value (FBW keeps it page-local too).
 */
#include "a380x_mfd_page_perf.h"
#include "a380x_fms.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PID(n) MFD_ID(MFD_PAGE_ID_PERF, (n))
#define PERF_NBSP "\xC2\xA0"
#define PERF_DEG "\xC2\xB0"

/* Widget id blocks */
enum {
    PERF_ID_CRZ_FL = 1,
    PERF_ID_TABS = 10,
    PERF_ID_TO = 100,
    PERF_ID_CLB = 200,
    PERF_ID_CRZ = 300,
    PERF_ID_DES = 400,
    PERF_ID_APPR = 500,
    PERF_ID_GA = 600,
    PERF_ID_FOOTER = 700,
    PERF_ID_DIALOG = 800
};

/* Layout anchors (canvas px, border-box). */
#define PERF_STRIP_Y 143.0f       /* top strip grid row 1 (37 px) */
#define PERF_STRIP_YC1 161.5f
#define PERF_STRIP_YC2 192.5f
#define PERF_TABS_Y 210.0f        /* TopTabNavigator margin box top */
#define PERF_TABS_H 675.0f        /* 210..885 */
#define PERF_FRAME_Y 248.0f       /* content frame top (tab bar bottom) */
#define PERF_IN_Y0 258.0f         /* T.O / APPR / GA content top (padding 10) */
#define PERF_IN_Y1 871.0f         /* content bottom (padding 10 above the 2 px border at 881) */
#define PERF_BOTTOM_ROW 868.0f    /* bottom rows: margin-bottom 3 */

typedef struct {
    MfdPageContext *ctx;
    MfdUi *ui;
    MfdDrawList *dl;
    A380XFms *fms;
    MfdPerfState *st;
    int plan;        /* loaded plan: ACTIVE, TMPY or SECn; -1 = none */
    int phase;       /* FMGC flight phase */
    int aoc;         /* loaded plan is Active or a copy of Active */
    int at;          /* loaded plan is Active or Temporary */
    int sec;         /* loaded plan is a SEC plan (static values white) */
    int eo;          /* engine out */
    int metric;      /* T / M units */
    A380XFmsPage fpage;
} PerfCtx;

/* ======================================================================================== */
/* Small helpers                                                                            */
/* ======================================================================================== */

static float perf_w(const char *s, float fs)
{
    return mfd_text_width(s, fs);
}

/* Text whose 1.0-em line box is vertically centred on yc. */
static void perf_txt(const PerfCtx *p, float x, float yc, MfdAnchor a, float fs, MfdColor c, const char *s)
{
    if (!s || !s[0]) return;
    mfd_text(p->ui, x, yc - fs * 0.5f + MFD_FONT_BASELINE * fs, a, fs, c, s);
}

static MfdColor perf_value_colour(const PerfCtx *p)
{
    return p->sec ? MFD_COL_WHITE : MFD_COL_GREEN;
}

static MfdColor perf_fms_colour(A380XFmsValue v)
{
    switch (v.colour) {
    case A380X_FMS_COLOUR_WHITE: return MFD_COL_WHITE;
    case A380X_FMS_COLOUR_AMBER: return MFD_COL_AMBER;
    default: return MFD_COL_GREEN;
    }
}

static A380XFmsValue perf_get(const PerfCtx *p, A380XFmsField f)
{
    A380XFmsValue none;
    if (p->fms && p->plan >= 0) return a380x_fms_get(p->fms, p->plan, f);
    memset(&none, 0, sizeof(none));
    none.v = NAN;
    return none;
}

static unsigned perf_flags(const PerfCtx *p, A380XFmsField f)
{
    if (!p->fms || p->plan < 0) return A380X_FMS_FF_DISABLED;
    return a380x_fms_field_flags(p->fms, p->plan, f, p->fpage);
}

/* FmgcData.fmcFormatValue: toFixed(0) or "---". */
static void perf_fmt_int(A380XFmsValue v, char *b, size_t n)
{
    if (v.has && isfinite(v.v)) mfd_js_to_fixed(v.v, 0, b, n);
    else snprintf(b, n, "---");
}

/* "." + toFixed(2).split('.')[1] */
static void perf_fmt_mach(double m, char *b, size_t n)
{
    char t[32];
    const char *dot;
    if (!isfinite(m)) {
        snprintf(b, n, ".--");
        return;
    }
    mfd_js_to_fixed(m, 2, t, sizeof(t));
    dot = strchr(t, '.');
    snprintf(b, n, "%s", dot ? dot : ".00");
}

/* kg -> "NNN.N" in t or klb, "---.-" when unknown. */
static void perf_fmt_weight(const PerfCtx *p, A380XFmsValue v, char *b, size_t n)
{
    if (!v.has || !isfinite(v.v)) {
        snprintf(b, n, "---.-");
        return;
    }
    mfd_js_to_fixed((p->metric ? v.v : v.v / MFD_KG_PER_LB) / 1000.0, 1, b, n);
}

/* Seconds -> "HH:MM" (time span or UTC seconds of day), "--:--" when unknown. */
static void perf_fmt_hhmm(A380XFmsValue v, char *b, size_t n)
{
    long s, h, m;
    if (!v.has || !isfinite(v.v) || v.v < 0) {
        snprintf(b, n, "--:--");
        return;
    }
    s = (long)floor(v.v);
    h = (s / 3600) % 24;
    m = (s % 3600) / 60;
    snprintf(b, n, "%02ld:%02ld", h, m);
}

/* label (20 white) + 15 + value (25) + 6 + unit (20 blue); left edge x. Returns the end x. */
static float perf_lvu(const PerfCtx *p, float x, float yc, const char *label, const char *value, MfdColor vc,
                      const char *unit)
{
    if (label && label[0]) {
        perf_txt(p, x, yc, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, label);
        x += perf_w(label, 20.0f) + 15.0f;
    }
    perf_txt(p, x, yc, MFD_ANCHOR_LEFT, 25.0f, vc, value);
    x += perf_w(value, 25.0f);
    if (unit && unit[0]) {
        x += 6.0f;
        perf_txt(p, x, yc, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, unit);
        x += perf_w(unit, 20.0f);
    }
    return x;
}

/* value (25) + 6 + unit (20), centred on cx (the unit is drawn only for a non-empty value). */
static void perf_cell_value(const PerfCtx *p, float cx, float yc, const char *value, const char *unit, MfdColor vc)
{
    float w;
    if (!value || !value[0]) return;
    w = perf_w(value, 25.0f);
    if (unit && unit[0]) w += 6.0f + perf_w(unit, 20.0f);
    perf_txt(p, cx - w * 0.5f, yc, MFD_ANCHOR_LEFT, 25.0f, vc, value);
    if (unit && unit[0])
        perf_txt(p, cx - w * 0.5f + perf_w(value, 25.0f) + 6.0f, yc, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, unit);
}

/* 13x13 SVG circle r 5, stroke 2 green (green-dot symbol); (x, y) = svg top-left. */
static void perf_green_dot(const PerfCtx *p, float x, float y)
{
    mfd_draw_disc(p->dl, x + 6.0f, y + 6.0f, 6.0f, MFD_COL_GREEN);
    mfd_draw_disc(p->dl, x + 6.0f, y + 6.0f, 4.0f, MFD_COL_BLACK);
}

/* Noise arrow: viewBox 60x60 polygon (0,28 50,28 50,20 60,30 50,40 50,32 0,32) in a 35x35 box. */
static void perf_noise_arrow(const PerfCtx *p, float x, float y)
{
    const float k = 35.0f / 60.0f;
    mfd_draw_fill_rect(p->dl, mfd_rect(x, y + 28.0f * k, 50.0f * k, 4.0f * k), MFD_COL_WHITE);
    mfd_draw_triangle(p->dl, x + 50.0f * k, y + 20.0f * k, x + 60.0f * k, y + 30.0f * k, x + 50.0f * k, y + 40.0f * k,
                      MFD_COL_WHITE);
}

static void perf_vline(const PerfCtx *p, float x, float y0, float y1, MfdColor c)
{
    mfd_draw_fill_rect(p->dl, mfd_rect(x, y0, 1.0f, y1 - y0), c);
}

static void perf_hline(const PerfCtx *p, float x0, float x1, float y, MfdColor c)
{
    mfd_hrule(p->ui, x0, x1, y, 1.0f, c);
}

/* ======================================================================================== */
/* Widgets bound to FMS fields                                                              */
/* ======================================================================================== */

typedef struct {
    A380XFmsField f;
    float x, y;        /* top-left */
    float right;       /* > 0: right-align the field's outer box on this x */
    float cx;          /* > 0: centre the field's outer box on this x (table cells) */
    float width;       /* FBW containerStyle width (border-box); 0 = auto */
    MfdAnchor align;   /* alignText */
    unsigned drop_ff;  /* FMS flags ignored on this page */
    uint32_t add;      /* extra MFD_FIELD_* flags */
} PerfField;

static void perf_field_init(PerfField *pf, A380XFmsField f, float x, float y, float width, MfdAnchor align)
{
    memset(pf, 0, sizeof(*pf));
    pf->f = f;
    pf->x = x;
    pf->y = y;
    pf->width = width;
    pf->align = align;
}

static void perf_report(PerfCtx *p, int ok, const MfdFmsError *err, int details)
{
    if (!ok && err->type != MFD_ERR_NONE) mfd_ui_error(p->ui, err, details);
}

/* InputField bound to an FMS field: flags, value, format and validation from the FMS.
 * Returns the outer width drawn (0 when hidden or no plan). */
static float perf_field(PerfCtx *p, int id, const PerfField *pf)
{
    MfdFieldDesc d;
    MfdFieldValue fv;
    MfdValue out;
    MfdFieldResult r;
    A380XFmsValue v;
    unsigned ff;
    float x, w;

    if (!p->fms || p->plan < 0) return 0.0f;
    ff = perf_flags(p, pf->f) & ~pf->drop_ff;
    if (ff & A380X_FMS_FF_HIDDEN) return 0.0f;
    mfd_field_desc_init(&d, MFD_FMT_SPEED_KT);
    a380x_fms_field_fmt(p->fms, p->plan, pf->f, &d.fmt);
    if (d.fmt.kind == MFD_FMT_COUNT) return 0.0f;
    if (d.fmt.kind == MFD_FMT_LENGTH) d.fmt.max_digits = 4; /* T.O SHIFT: 4 digits (spec 04 §5.1) */
    if (d.fmt.kind == MFD_FMT_QNH) {
        const A380XFmsInputs *in = a380x_fms_inputs(p->fms);
        d.fmt.qnh_is_hpa = !in->efis_baro_inhg[p->ctx->side ? 1 : 0];
    }
    d.width = pf->width;
    d.align = pf->align;
    d.flags = pf->add;
    if (ff & A380X_FMS_FF_DISABLED) d.flags |= MFD_FIELD_DISABLED;
    if (ff & A380X_FMS_FF_INACTIVE) d.flags |= MFD_FIELD_INACTIVE;
    if (ff & A380X_FMS_FF_MANDATORY) d.flags |= MFD_FIELD_MANDATORY;
    if (ff & A380X_FMS_FF_TMPY) d.flags |= MFD_FIELD_TMPY;
    if (!(ff & A380X_FMS_FF_CLEARABLE)) d.flags |= MFD_FIELD_NOT_CLEARABLE;

    v = perf_get(p, pf->f);
    memset(&fv, 0, sizeof(fv));
    fv.has_value = v.has;
    fv.is_computed = v.has && !v.entered;
    fv.num = v.v;

    w = mfd_field_auto_width(&d, &fv);
    if (pf->right > 0.0f) x = pf->right - w;
    else if (pf->cx > 0.0f) x = pf->cx - w * 0.5f;
    else x = pf->x;
    memset(&out, 0, sizeof(out));
    r = mfd_input_field(p->ui, PID(id), x, pf->y, &d, &fv, &out);
    if (r != MFD_FIELD_NONE) {
        MfdFmsError err;
        int ok;
        memset(&err, 0, sizeof(err));
        if (r == MFD_FIELD_COMMITTED) ok = a380x_fms_enter(p->fms, p->plan, pf->f, out.num, &err);
        else ok = a380x_fms_clear(p->fms, p->plan, pf->f, &err);
        perf_report(p, ok, &err, d.fmt.err_details);
    }
    return w;
}

/* Static (inactive-looking) value of an FMS field in .mfd-value style. */
static void perf_value_of(const PerfCtx *p, A380XFmsField f, char *b, size_t n, MfdColor *c)
{
    A380XFmsValue v = perf_get(p, f);
    perf_fmt_int(v, b, n);
    if (c) *c = v.has ? perf_fms_colour(v) : perf_value_colour(p);
}

/* DropdownMenu bound to an enum FMS field (stored value = index + offset). */
static void perf_dropdown(PerfCtx *p, int id, A380XFmsField f, MfdRect r, const char *const *vals, int n, int digits,
                          int offset, int align_left)
{
    MfdDropdownDesc dd;
    A380XFmsValue v = perf_get(p, f);
    unsigned ff = perf_flags(p, f);
    int idx = -1;
    if (ff & A380X_FMS_FF_HIDDEN) return;
    memset(&dd, 0, sizeof(dd));
    dd.values = vals;
    dd.count = n;
    dd.selected = v.has ? (int)v.v - offset : -1;
    if (dd.selected >= n) dd.selected = -1;
    dd.inactive = (ff & A380X_FMS_FF_INACTIVE) != 0;
    dd.disabled = (ff & A380X_FMS_FF_DISABLED) != 0;
    dd.tmpy = (ff & A380X_FMS_FF_TMPY) != 0;
    dd.digits = digits;
    dd.align_left = align_left;
    if (mfd_dropdown(p->ui, PID(id), r, &dd, &idx, NULL, 0) && idx >= 0 && p->fms && p->plan >= 0) {
        MfdFmsError err;
        int ok;
        memset(&err, 0, sizeof(err));
        ok = a380x_fms_enter(p->fms, p->plan, f, (double)(idx + offset), &err);
        perf_report(p, ok, &err, 0);
    }
}

/* Button whose FBW label is "<line1><br/><line2>" + 10 px + "*" (CONFIRM T.O SPDs, ACTIVATE APPR,
 * CLEAR EO): padding-right 2, label block left, star right, both vertically centred. */
static int perf_star_button(PerfCtx *p, int id, MfdRect r, const char *l1, const char *l2, MfdColor col,
                            uint32_t flags)
{
    MfdButtonDesc bd;
    MfdColor tc;
    float lw, top;
    int res;
    if (flags & MFD_BTN_HIDDEN) return 0;
    mfd_button_desc_init(&bd, "");
    bd.flags = flags;
    bd.pad_right = 2.0f;
    res = mfd_button_ex(p->ui, PID(id), r, &bd);
    tc = (flags & MFD_BTN_DISABLED) ? MFD_COL_DARK_GREY : col;
    lw = perf_w(l1, 22.0f) > perf_w(l2, 22.0f) ? perf_w(l1, 22.0f) : perf_w(l2, 22.0f);
    top = r.y + 11.0f;
    mfd_draw_text(p->dl, MFD_FONT_SZ, 22.0f, tc, MFD_ANCHOR_CENTER, r.x + 14.0f + lw * 0.5f,
                  top + MFD_FONT_BASELINE * 22.0f, l1);
    mfd_draw_text(p->dl, MFD_FONT_SZ, 22.0f, tc, MFD_ANCHOR_CENTER, r.x + 14.0f + lw * 0.5f,
                  top + 22.0f + MFD_FONT_BASELINE * 22.0f, l2);
    mfd_draw_text(p->dl, MFD_FONT_SZ, 22.0f, tc, MFD_ANCHOR_RIGHT, r.x + r.w - 4.0f,
                  top + 11.0f + MFD_FONT_BASELINE * 22.0f, "*");
    return res == MFD_BUTTON_CLICKED;
}

/* Width of a star button: label block + 10 + star + padding 12/2 + border 2x2. */
static float perf_star_button_w(const char *l1, const char *l2)
{
    float lw = perf_w(l1, 22.0f) > perf_w(l2, 22.0f) ? perf_w(l1, 22.0f) : perf_w(l2, 22.0f);
    return lw + 10.0f + perf_w("*", 22.0f) + 12.0f + 2.0f + 4.0f;
}

/* ======================================================================================== */
/* Tab selection (spec 04 §4.1)                                                             */
/* ======================================================================================== */

int mfd_perf_tab_from_extra(const char *extra)
{
    static const char *const names[MFD_PERF_TAB_COUNT] = {"to", "clb", "crz", "des", "appr", "ga"};
    int i;
    char first[16];
    size_t k = 0;
    if (!extra) return -1;
    while (extra[k] && extra[k] != '/' && k + 1 < sizeof(first)) {
        first[k] = extra[k];
        ++k;
    }
    first[k] = '\0';
    for (i = 0; i < MFD_PERF_TAB_COUNT; ++i)
        if (strcmp(first, names[i]) == 0) return i;
    return -1;
}

int mfd_perf_follow_phase(int selected, int old_phase, int new_phase, int aoc)
{
    if (selected + 1 == old_phase && aoc && new_phase >= A380X_FMS_PHASE_TAKEOFF &&
        new_phase <= A380X_FMS_PHASE_GOAROUND)
        return new_phase - 1;
    return selected;
}

static int perf_initial_tab(const PerfCtx *p)
{
    int t = mfd_perf_tab_from_extra(p->ctx->uri ? p->ctx->uri->extra : NULL);
    if (t >= 0) return t;
    if (p->aoc) {
        int ph = p->phase < 1 ? 1 : p->phase > 6 ? 6 : p->phase;
        return ph - 1;
    }
    return MFD_PERF_TAB_TO;
}

/* ======================================================================================== */
/* Top strip: CRZ FL, OPT, REC MAX, EO MAX (spec 04 §3)                                     */
/* ======================================================================================== */

static void perf_fl_group(PerfCtx *p, float x, float yc, const char *label, MfdColor lc, A380XFmsField f)
{
    char v[16];
    A380XFmsValue fv = perf_get(p, f);
    MfdColor vc = MFD_COL_WHITE;
    if (fv.has && isfinite(fv.v) && fv.v != 0.0) {
        mfd_js_to_fixed(fv.v, 0, v, sizeof(v));
        vc = MFD_COL_GREEN;
    } else {
        snprintf(v, sizeof(v), "---");
    }
    perf_txt(p, x, yc, MFD_ANCHOR_LEFT, 20.0f, lc, label);
    x += perf_w(label, 20.0f) + 15.0f;
    perf_txt(p, x, yc, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, "FL");
    x += perf_w("FL", 20.0f) + 6.0f;
    perf_txt(p, x, yc, MFD_ANCHOR_LEFT, 25.0f, vc, v);
}

static float perf_fl_group_w(const char *label)
{
    return perf_w(label, 20.0f) + 15.0f + perf_w("FL", 20.0f) + 6.0f + perf_w("---", 25.0f);
}

static void perf_top_strip(PerfCtx *p)
{
    PerfField pf;
    perf_txt(p, 25.0f, PERF_STRIP_YC1, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "CRZ");
    /* CRZ FL: never disabled/inactive on PERF; mandatory iff the loaded plan is Active and the
     * phase is before DESCENT (PERF:767-770). */
    perf_field_init(&pf, A380X_FMS_F_CRZ_FL, 25.0f + perf_w("CRZ", 20.0f) + 15.0f, PERF_STRIP_Y, 0.0f,
                    MFD_ANCHOR_CENTER);
    pf.drop_ff = A380X_FMS_FF_DISABLED | A380X_FMS_FF_INACTIVE | A380X_FMS_FF_MANDATORY;
    if (p->plan == A380X_FMS_PLAN_ACTIVE && p->phase < A380X_FMS_PHASE_DESCENT) pf.add = MFD_FIELD_MANDATORY;
    perf_field(p, PERF_ID_CRZ_FL, &pf);

    if (!p->at) return; /* visibility: hidden for SEC plans */
    perf_fl_group(p, 384.0f - perf_fl_group_w("OPT") * 0.5f, PERF_STRIP_YC1, "OPT", MFD_COL_WHITE, A380X_FMS_F_OPT_FL);
    perf_fl_group(p, 743.0f - perf_fl_group_w("REC MAX"), PERF_STRIP_YC1, "REC MAX", MFD_COL_WHITE,
                  A380X_FMS_F_REC_MAX_FL);
    perf_fl_group(p, 743.0f - perf_fl_group_w(PERF_NBSP "EO MAX"), PERF_STRIP_YC2, PERF_NBSP "EO MAX",
                  p->eo ? MFD_COL_AMBER : MFD_COL_WHITE, A380X_FMS_F_EO_MAX_FL);
}

/* ======================================================================================== */
/* T.O tab (spec 04 §5)                                                                     */
/* ======================================================================================== */

/* T.O inner box x 14..754, y 258..871:
 *   A RWY / T.O SHIFT            258..314 (border 313)
 *   B V-speeds / thrust          314..493 (grid rows 329/380/431, border 492)
 *   C FLAPS/THS/PACKS/ANTI ICE   503..598 (border 597)
 *   D THR RED / ACCEL / NOISE    608..758 (rows 608/658/708)
 *   F TRANS / EO ACCEL / CPNY    795..868 (border 795) */
static void perf_tab_to(PerfCtx *p)
{
    static const char *const thrust[] = {"TOGA", "FLEX", "DERATED"};
    static const char *const flaps[] = {"1", "2", "3"};
    static const char *const packs[] = {"OFF/APU", "ON"};
    static const char *const anti_ice[] = {"OFF", "ENG ONLY", "ENG + WING"};
    static const char *const derated[] = {"D01", "D02", "D03", "D04", "D05"};
    static const A380XFmsField vfield[3] = {A380X_FMS_F_V1, A380X_FMS_F_VR, A380X_FMS_F_V2};
    static const A380XFmsField tbcfield[3] = {A380X_FMS_F_V1_TBC, A380X_FMS_F_VR_TBC, A380X_FMS_F_V2_TBC};
    static const char *const vlabel[3] = {"V1", "VR", "V2"};
    const float row_yc[3] = {354.5f, 405.5f, 456.5f};
    MfdUi *ui = p->ui;
    PerfField pf;
    char t[32], v[16];
    MfdColor vc;
    int i, confirm, to_inactive = p->aoc && p->phase >= A380X_FMS_PHASE_TAKEOFF, noise;
    float w, x;

    /* ---- A: RWY, T.O SHIFT ---- */
    perf_txt(p, 29.0f, 285.5f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "RWY");
    t[0] = '\0';
    if (p->fms && p->plan >= 0) a380x_fms_get_text(p->fms, p->plan, A380X_FMS_T_ORIGIN_RWY, t, sizeof(t));
    if (!t[0]) snprintf(t, sizeof(t), "---");
    perf_txt(p, 29.0f + perf_w("RWY", 20.0f) + 15.0f, 285.5f, MFD_ANCHOR_LEFT, 25.0f, perf_value_colour(p), t);
    perf_field_init(&pf, A380X_FMS_F_TO_SHIFT, 0.0f, 267.0f, 0.0f, MFD_ANCHOR_CENTER);
    pf.right = 747.0f;
    w = perf_field(p, PERF_ID_TO + 1, &pf);
    if (w <= 0.0f) w = 104.5f;
    perf_txt(p, 747.0f - w - 15.0f, 285.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "T.O SHIFT");
    perf_hline(p, 14.0f, 754.0f, 313.0f, MFD_COL_LIGHT_GREY);

    /* ---- B: V-speeds (grid x 14..418, border-right 428) ---- */
    confirm = p->fms && p->plan >= 0 && a380x_fms_should_show_confirm_to_speeds(p->fms, p->plan);
    for (i = 0; i < 3; ++i) {
        perf_txt(p, 21.0f, row_yc[i], MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, vlabel[i]);
        perf_field_init(&pf, vfield[i], 21.0f + perf_w(vlabel[i], 20.0f) + 15.0f, row_yc[i] - 18.5f, 0.0f,
                        MFD_ANCHOR_RIGHT);
        perf_field(p, PERF_ID_TO + 10 + i, &pf);
    }
    if (confirm) {
        /* column 2: speeds to be confirmed (yellow), column 3: CONFIRM T.O SPDs button */
        for (i = 0; i < 3; ++i) {
            perf_value_of(p, tbcfield[i], v, sizeof(v), NULL);
            perf_lvu(p, 177.5f, row_yc[i], NULL, v, MFD_COL_YELLOW, "KT");
        }
        w = perf_star_button_w("CONFIRM", "T.O SPDs");
        if (perf_star_button(p, PERF_ID_TO + 20, mfd_rect(418.0f - w, 420.0f, w, 62.0f), "CONFIRM", "T.O SPDs",
                             MFD_COL_YELLOW, 0u))
            a380x_fms_confirm_to_speeds(p->fms, p->plan);
    } else {
        perf_value_of(p, A380X_FMS_F_TO_F, v, sizeof(v), &vc);
        perf_lvu(p, 304.9f, row_yc[0], "F", v, vc, "KT");
        perf_value_of(p, A380X_FMS_F_TO_S, v, sizeof(v), &vc);
        perf_lvu(p, 304.9f, row_yc[1], "S", v, vc, "KT");
        perf_green_dot(p, 304.9f, row_yc[2] - 6.5f);
        perf_value_of(p, A380X_FMS_F_TO_GD, v, sizeof(v), &vc);
        perf_lvu(p, 332.9f, row_yc[2], NULL, v, vc, "KT");
    }
    perf_vline(p, 428.0f, 319.0f, 482.0f, MFD_COL_LIGHT_GREY);

    /* thrust setting radio group (x 444, 3 rows of 34 with 15 px spacing, centred in 314..479) */
    {
        A380XFmsValue pw = perf_get(p, A380X_FMS_F_TO_POWER);
        uint32_t mask = 0;
        int k;
        for (k = 0; k < 3; ++k)
            if (!p->fms || p->plan < 0 || !a380x_fms_option_enabled(p->fms, p->plan, A380X_FMS_F_TO_POWER, k))
                mask |= 1u << k;
        k = mfd_radio_group(ui, PID(PERF_ID_TO + 30), 444.0f, 323.0f, 175.0f, thrust, 3, pw.has ? (int)pw.v : -1,
                            mask, MFD_COL_CYAN, 15.0f);
        if (k >= 0 && p->fms && p->plan >= 0) {
            MfdFmsError err;
            int ok;
            memset(&err, 0, sizeof(err));
            ok = a380x_fms_enter(p->fms, p->plan, A380X_FMS_F_TO_POWER, (double)k, &err);
            perf_report(p, ok, &err, 0);
        }
    }
    /* FLEX temperature (visible only when FLEX), DERATED dropdown (only when DERATED) */
    perf_field_init(&pf, A380X_FMS_F_FLEX_TEMP, 636.0f, 386.0f, 0.0f, MFD_ANCHOR_CENTER);
    perf_field(p, PERF_ID_TO + 31, &pf);
    {
        A380XFmsValue dv = perf_get(p, A380X_FMS_F_DERATED);
        unsigned ff = perf_flags(p, A380X_FMS_F_DERATED);
        if (!(ff & A380X_FMS_FF_HIDDEN)) {
            MfdDropdownDesc dd;
            int idx = -1;
            memset(&dd, 0, sizeof(dd));
            dd.values = derated;
            dd.count = 5;
            dd.selected = dv.has && dv.v >= 0 ? (int)dv.v : -1;
            dd.inactive = (ff & A380X_FMS_FF_INACTIVE) != 0;
            dd.digits = 3;
            dd.align_left = 1;
            if (mfd_dropdown(ui, PID(PERF_ID_TO + 32), mfd_rect(629.0f, 430.0f, 100.0f, 44.0f), &dd, &idx, NULL, 0) &&
                idx >= 0) {
                p->st->derated_prev = dd.selected;
                p->st->derated_next = idx;
                p->st->dialog = MFD_PERF_DLG_DERATED;
            }
        }
    }
    perf_hline(p, 14.0f, 754.0f, 492.0f, MFD_COL_LIGHT_GREY);

    /* ---- C: FLAPS / THS FOR | PACKS / ANTI ICE ---- */
    perf_txt(p, 14.0f, 513.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "FLAPS");
    perf_txt(p, 119.1f, 513.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "THS FOR");
    perf_txt(p, 268.7f, 513.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "PACKS");
    perf_txt(p, 498.8f, 513.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "ANTI ICE");
    perf_vline(p, 253.7f, 503.0f, 582.0f, MFD_COL_CSS_LIGHTGREY);
    perf_dropdown(p, PERF_ID_TO + 40, A380X_FMS_F_TO_FLAPS, mfd_rect(14.0f, 538.0f, 75.0f, 44.0f), flaps, 3, 1, 1, 0);
    perf_field_init(&pf, A380X_FMS_F_THS_FOR, 119.1f, 541.5f, 0.0f, MFD_ANCHOR_RIGHT);
    perf_field(p, PERF_ID_TO + 41, &pf);
    perf_dropdown(p, PERF_ID_TO + 42, A380X_FMS_F_PACKS, mfd_rect(268.7f, 538.0f, 200.0f, 44.0f), packs, 2, 8, 0, 0);
    perf_dropdown(p, PERF_ID_TO + 43, A380X_FMS_F_ANTI_ICE, mfd_rect(498.8f, 538.0f, 225.0f, 44.0f), anti_ice, 3, 10,
                  0, 0);
    perf_hline(p, 14.0f, 754.0f, 597.0f, MFD_COL_LIGHT_GREY);

    /* ---- D: THR RED / ACCEL / NOISE (columns 14 | 175.6 | 347.2 | 468.8 | 600.4 .. 734) ---- */
    noise = perf_get(p, A380X_FMS_F_NOISE).has && perf_get(p, A380X_FMS_F_NOISE).v != 0.0;
    perf_txt(p, 139.0f, 633.0f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "THR RED");
    perf_field_init(&pf, A380X_FMS_F_THR_RED, 175.6f, 614.5f, 150.0f, MFD_ANCHOR_RIGHT);
    perf_field(p, PERF_ID_TO + 50, &pf);
    perf_txt(p, 139.0f, 683.0f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "ACCEL");
    perf_field_init(&pf, A380X_FMS_F_ACC, 175.6f, 664.5f, 150.0f, MFD_ANCHOR_RIGHT);
    perf_field(p, PERF_ID_TO + 51, &pf);
    if (noise) {
        perf_noise_arrow(p, 368.8f, 608.0f);
        perf_txt(p, 453.8f, 625.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "N1");
        perf_field_init(&pf, A380X_FMS_F_NOISE_N1, 468.8f, 608.0f, 110.0f, MFD_ANCHOR_RIGHT);
        perf_field(p, PERF_ID_TO + 52, &pf);
        perf_noise_arrow(p, 368.8f, 658.0f);
        perf_txt(p, 453.8f, 675.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "SPD");
        perf_field_init(&pf, A380X_FMS_F_NOISE_SPD, 468.8f, 658.0f, 110.0f, MFD_ANCHOR_RIGHT);
        perf_field(p, PERF_ID_TO + 53, &pf);
        if (!to_inactive &&
            mfd_button(ui, PID(PERF_ID_TO + 54), mfd_rect(600.4f, 608.0f, mfd_button_auto_width("CANCEL\nNOISE"), 62.0f),
                       "CANCEL\nNOISE", 0))
            a380x_fms_cancel_noise(p->fms, p->plan);
        perf_txt(p, 139.0f, 733.0f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "NOISE END");
        perf_field_init(&pf, A380X_FMS_F_NOISE_END, 186.4f, 708.0f, 150.0f, MFD_ANCHOR_RIGHT);
        perf_field(p, PERF_ID_TO + 55, &pf);
    } else if (!to_inactive) {
        /* NOISE: always disabled [FBW-PH] */
        mfd_button(ui, PID(PERF_ID_TO + 56), mfd_rect(175.6f, 708.0f, mfd_button_auto_width("NOISE"), 40.0f), "NOISE",
                   MFD_BTN_DISABLED);
    }

    /* ---- F: TRANS / EO ACCEL / CPNY T.O REQUEST (x 16..752, y 795..868, centre 837) ---- */
    perf_hline(p, 16.0f, 752.0f, 795.0f, MFD_COL_LIGHT_GREY);
    perf_txt(p, 23.0f, 837.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "TRANS");
    perf_field_init(&pf, A380X_FMS_F_TRANS_ALT, 23.0f + perf_w("TRANS", 20.0f) + 15.0f, 818.5f, 150.0f,
                    MFD_ANCHOR_RIGHT);
    pf.drop_ff = A380X_FMS_FF_INACTIVE | A380X_FMS_FF_DISABLED; /* never inactive */
    perf_field(p, PERF_ID_TO + 60, &pf);
    x = 23.0f + perf_w("TRANS", 20.0f) + 15.0f + 150.0f + 7.0f + 20.3f + 7.0f;
    perf_txt(p, x, 837.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "EO ACCEL");
    perf_field_init(&pf, A380X_FMS_F_EO_ACC, x + perf_w("EO ACCEL", 20.0f) + 15.0f, 818.5f, 150.0f, MFD_ANCHOR_RIGHT);
    perf_field(p, PERF_ID_TO + 61, &pf);
    if (p->at && !to_inactive) {
        MfdButtonDesc bd;
        mfd_button_desc_init(&bd, "CPNY T.O\nREQUEST");
        bd.flags = MFD_BTN_DISABLED; /* [FBW-PH] */
        w = perf_w("CPNY T.O", 22.0f) + 64.0f;
        mfd_button_ex(ui, PID(PERF_ID_TO + 62), mfd_rect(752.0f - w, 806.0f, w, 62.0f), &bd);
    }
}

/* Derated dialog ([PORT] kept inside the tab frame: x 424 instead of FBW 434). */
static void perf_derated_dialog(PerfCtx *p)
{
    static const char *const derated[] = {"D01", "D02", "D03", "D04", "D05"};
    char title[24];
    int r, nx = p->st->derated_next;
    MfdFmsError err;
    snprintf(title, sizeof(title), "DERATED %s", derated[(nx >= 0 && nx < 5) ? nx : 0]);
    r = mfd_confirm_dialog(p->ui, PID(PERF_ID_DIALOG + 20), mfd_rect(424.0f, 319.0f, 325.0f, 165.0f), title, 0);
    if (r == MFD_DIALOG_NONE) return;
    memset(&err, 0, sizeof(err));
    if (r == MFD_DIALOG_CONFIRM) a380x_fms_enter(p->fms, p->plan, A380X_FMS_F_DERATED, (double)nx, &err);
    else if (p->st->derated_prev >= 0)
        a380x_fms_enter(p->fms, p->plan, A380X_FMS_F_DERATED, (double)p->st->derated_prev, &err);
    p->st->dialog = MFD_PERF_DLG_NONE;
}

/* ======================================================================================== */
/* CLB / CRZ / DES common parts                                                             */
/* ======================================================================================== */

/* Cost index mode dropdown + CI field (x 11 / 201 / 241.1); yc = row centre. */
static void perf_ci_row(PerfCtx *p, int id, float yc, int ci_inactive)
{
    static const char *const modes[] = {"LRC", "ECON"};
    static const char *const eo_modes[] = {"EO-LRC", "EO-ECON"};
    PerfField pf;
    perf_dropdown(p, id, A380X_FMS_F_CI_MODE, mfd_rect(11.0f, yc - 22.0f, 175.0f, 44.0f), p->eo ? eo_modes : modes, 2,
                  7, 0, 0);
    perf_txt(p, 201.0f, yc, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "CI");
    perf_field_init(&pf, A380X_FMS_F_CI, 201.0f + perf_w("CI", 20.0f) + 15.0f, yc - 18.5f, 75.0f, MFD_ANCHOR_CENTER);
    pf.drop_ff = A380X_FMS_FF_MANDATORY | (ci_inactive ? 0u : A380X_FMS_FF_INACTIVE); /* not mandatory on PERF */
    perf_field(p, id + 1, &pf);
}

/* Speed-table geometry (4..754): CLB 23/22/13/42 %, CRZ & DES 23/13/22/42 %. */
#define PERF_COL_MODE_R 176.5f   /* MODE column right edge (1 px separator 175.5..176.5) */
#define PERF_CX_MODE 90.25f
#define PERF_CX_PRED 596.5f
#define PERF_CLB_CX_SPD 259.0f
#define PERF_CLB_CX_MACH 390.25f
#define PERF_CRZ_CX_MACH 225.25f
#define PERF_CRZ_CX_SPD 356.5f

static void perf_table_header(PerfCtx *p, float yc, const char *c2, float cx2, const char *c3, float cx3)
{
    perf_txt(p, PERF_CX_MODE, yc, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "MODE");
    perf_txt(p, cx2, yc, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, c2);
    perf_txt(p, cx3, yc, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, c3);
}

/* Mode label of a table row, right-aligned in the MODE column. */
static void perf_mode_label(PerfCtx *p, float right, float yc, const char *s, MfdColor c, float fs)
{
    perf_txt(p, right, yc, MFD_ANCHOR_RIGHT, fs, c, s);
}

/* DEST row of CRZ / DES (spec 04 §7.3): flex space-between over x 21..752, centre y 848. */
static void perf_dest_row(PerfCtx *p, int des)
{
    char ident[12], eta[12], efob[16];
    const char *unit = p->metric ? "T" : "KLB";
    A380XFmsValue ev = perf_get(p, A380X_FMS_F_DEST_EFOB);
    MfdColor ic = p->at ? MFD_COL_GREEN : MFD_COL_WHITE, ec;
    float wd, wi, we, wf, wb, gap, x, yc = 848.0f;
    MfdButtonDesc bd;

    ident[0] = '\0';
    if (p->fms && p->plan >= 0) a380x_fms_get_text(p->fms, p->plan, A380X_FMS_T_TO, ident, sizeof(ident));
    if (!ident[0]) snprintf(ident, sizeof(ident), "----");
    perf_fmt_hhmm(perf_get(p, A380X_FMS_F_DEST_ETA), eta, sizeof(eta));
    perf_fmt_weight(p, ev, efob, sizeof(efob));
    ec = ev.has ? perf_fms_colour(ev) : perf_value_colour(p);

    wd = perf_w("DEST", 24.0f);
    wi = perf_w(ident, 24.0f);
    we = perf_w(eta, 24.0f);
    wf = 7.0f + perf_w(efob, 25.0f) + 6.0f + perf_w(unit, 20.0f) + 7.0f;
    wb = des ? mfd_button_auto_width("SPD CSTR")
             : mfd_button_auto_width("CMS") + 10.0f + mfd_button_auto_width("STEP ALTs");
    gap = (731.0f - (wd + wi + we + wf + wb)) / 4.0f;
    x = 21.0f;
    perf_txt(p, x, yc, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "DEST");
    x += wd + gap;
    perf_txt(p, x, yc, MFD_ANCHOR_LEFT, 24.0f, ic, ident);
    x += wi + gap;
    perf_txt(p, x, yc, MFD_ANCHOR_LEFT, 24.0f, ic, eta);
    x += we + gap;
    perf_lvu(p, x + 7.0f, yc, NULL, efob, ec, unit);
    x += wf + gap;
    if (des) {
        mfd_button_desc_init(&bd, "SPD CSTR");
        bd.flags = (p->aoc && p->phase > A380X_FMS_PHASE_DESCENT) ? MFD_BTN_DISABLED : 0u;
        if (mfd_button_ex(p->ui, PID(PERF_ID_DES + 90), mfd_rect(x, yc - 20.0f, wb, 40.0f), &bd) == MFD_BUTTON_CLICKED) {
            char uri[MFD_URI_LEN];
            snprintf(uri, sizeof(uri), "fms/%s/f-pln-vert-rev/spd", p->ctx->uri->category);
            mfd_navigate(p->ctx, uri);
        }
    } else {
        float wc = mfd_button_auto_width("CMS");
        mfd_button(p->ui, PID(PERF_ID_CRZ + 90), mfd_rect(x, yc - 20.0f, wc, 40.0f), "CMS", MFD_BTN_DISABLED);
        mfd_button_desc_init(&bd, "STEP ALTs");
        bd.flags = (p->aoc && p->phase > A380X_FMS_PHASE_CRUISE) ? MFD_BTN_DISABLED : 0u;
        if (mfd_button_ex(p->ui, PID(PERF_ID_CRZ + 91), mfd_rect(x + wc + 10.0f, yc - 20.0f, 752.0f - (x + wc + 10.0f), 40.0f),
                          &bd) == MFD_BUTTON_CLICKED) {
            char uri[MFD_URI_LEN];
            snprintf(uri, sizeof(uri), "fms/%s/f-pln-vert-rev/step-alts", p->ctx->uri->category);
            mfd_navigate(p->ctx, uri);
        }
    }
}

/* FCU speed target from the FMS inputs: kt (>= 1) or Mach (< 1); NAN when unknown. */
static double perf_fcu_speed(const PerfCtx *p)
{
    const A380XFmsInputs *in = a380x_fms_inputs(p->fms);
    if (in->fcu_speed_is_mach) return in->fcu_selected_mach > 0.0 ? in->fcu_selected_mach : NAN;
    return in->fcu_selected_speed_kt > 0.0 ? in->fcu_selected_speed_kt : NAN;
}

/* ======================================================================================== */
/* CLB tab (spec 04 §6)                                                                     */
/* ======================================================================================== */

typedef struct {
    char mode[3][12];
    char spd[3][12];
    char mach[3][12];
    char pred[3][20];
} PerfTable;

static void perf_tab_clb(PerfCtx *p)
{
    static const char *const clb_derated[] = {"NONE", "01", "02", "03", "04", "05"};
    PerfTable tb;
    PerfField pf;
    char econ_spd[12], econ_m[12], sl_spd[12], sl_m[12], v[16];
    A380XFmsValue presel = perf_get(p, A380X_FMS_F_PRESEL_CLB);
    A380XFmsValue sl = perf_get(p, A380X_FMS_F_CLB_SPD_LIM), sla = perf_get(p, A380X_FMS_F_CLB_SPD_LIM_ALT);
    int in_flight = p->phase >= A380X_FMS_PHASE_CLIMB && p->phase <= A380X_FMS_PHASE_DESCENT;
    int not_yet_clb = !p->aoc || p->phase < A380X_FMS_PHASE_CLIMB;
    int to_inactive = p->aoc && p->phase >= A380X_FMS_PHASE_TAKEOFF;
    int clb_inactive = p->aoc && p->phase > A380X_FMS_PHASE_CLIMB;
    const A380XFmsInputs *in = a380x_fms_inputs(p->fms);
    MfdColor vc = perf_value_colour(p), c1, c2;
    float x, w;
    int noise;

    /* ---- header row 248..321: CI mode / CI, DERATED CLB ---- */
    perf_ci_row(p, PERF_ID_CLB + 1, 277.0f, 0);
    perf_txt(p, 607.0f, 284.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "DERATED CLB");
    perf_dropdown(p, PERF_ID_CLB + 3, A380X_FMS_F_CLB_DERATED, mfd_rect(622.0f, 262.5f, 125.0f, 44.0f), clb_derated, 6,
                  4, 0, 0);

    /* ---- table: H 321..399, L1 399..468, L2 468..529, L3 529..579, filler 579..619 ---- */
    perf_table_header(p, 359.5f, "SPD", PERF_CLB_CX_SPD, "MACH", PERF_CLB_CX_MACH);
    if (p->at) {
        perf_txt(p, 514.0f, 341.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "PRED TO" PERF_NBSP);
        perf_field_init(&pf, A380X_FMS_F_CLB_PRED_TO, 529.0f, 351.0f, 150.0f, MFD_ANCHOR_RIGHT);
        perf_field(p, PERF_ID_CLB + 4, &pf);
    }
    perf_hline(p, 4.0f, 754.0f, 398.0f, MFD_COL_LIGHT_GREY);
    perf_hline(p, 4.0f, 754.0f, 467.0f, MFD_COL_LIGHT_GREY);
    perf_hline(p, 4.0f, 754.0f, 528.0f, MFD_COL_LIGHT_GREY);
    perf_hline(p, 4.0f, 754.0f, 578.0f, MFD_COL_LIGHT_GREY);
    perf_vline(p, 175.5f, 321.0f, 619.0f, MFD_COL_LIGHT_GREY);

    memset(&tb, 0, sizeof(tb));
    perf_fmt_int(perf_get(p, A380X_FMS_F_CLB_ECON_SPD), econ_spd, sizeof(econ_spd));
    perf_fmt_mach(perf_get(p, A380X_FMS_F_CLB_ECON_MACH).v, econ_m, sizeof(econ_m));
    if (sl.has) perf_fmt_int(sl, sl_spd, sizeof(sl_spd));
    else sl_spd[0] = '\0';
    perf_fmt_mach(perf_get(p, A380X_FMS_F_CLB_SPD_LIM_MACH).v, sl_m, sizeof(sl_m));
    {
        int below_lim = sl.has && sla.has && in->baro_alt_ft < sla.v;
        if (p->phase < A380X_FMS_PHASE_CLIMB) {
            snprintf(tb.mode[0], 12, "PRESEL");
            snprintf(tb.mode[1], 12, "MANAGED");
            snprintf(tb.spd[1], 12, "%s", sl_spd);
            snprintf(tb.pred[1], 20, "--:-- ----" /* FBW "--:--   ----": HTML collapses the spaces */);
            snprintf(tb.mode[2], 12, "ECON");
            snprintf(tb.spd[2], 12, "%s", econ_spd);
            snprintf(tb.mach[2], 12, "%s", econ_m);
        } else if (in->speed_managed) {
            snprintf(tb.mode[0], 12, "MANAGED");
            if (below_lim) {
                snprintf(tb.spd[0], 12, "%s", sl_spd);
                snprintf(tb.mach[0], 12, "%s", sl_m);
                snprintf(tb.mode[2], 12, "ECON");
                snprintf(tb.spd[2], 12, "%s", econ_spd);
                snprintf(tb.mach[2], 12, "%s", econ_m);
            } else {
                snprintf(tb.spd[0], 12, "%s", econ_spd);
                snprintf(tb.mach[0], 12, "%s", econ_m);
            }
        } else {
            double fcu = perf_fcu_speed(p);
            snprintf(tb.mode[0], 12, "SELECTED");
            if (isfinite(fcu) && fcu >= 1.0) mfd_js_to_fixed(fcu, 0, tb.spd[0], 12);
            if (isfinite(fcu) && fcu < 1.0) perf_fmt_mach(fcu, tb.mach[0], 12);
            snprintf(tb.mode[1], 12, "MANAGED");
            snprintf(tb.spd[1], 12, "%s", below_lim ? sl_spd : econ_spd);
            snprintf(tb.mach[1], 12, "%s", below_lim ? sl_m : econ_m);
            snprintf(tb.mode[2], 12, "ECON");
            snprintf(tb.spd[2], 12, "%s", econ_spd);
            snprintf(tb.mach[2], 12, "%s", econ_m);
        }
    }
    /* mode label colours (PERF:511-531) */
    if (in_flight && p->eo) c1 = MFD_COL_AMBER;
    else if ((in_flight && !p->eo) || (p->phase < A380X_FMS_PHASE_CLIMB && presel.has)) c1 = MFD_COL_GREEN;
    else c1 = MFD_COL_WHITE;
    c2 = (in_flight || (p->phase < A380X_FMS_PHASE_CLIMB && !presel.has)) ? MFD_COL_GREEN : MFD_COL_WHITE;
    perf_mode_label(p, 165.5f, 433.5f, tb.mode[0], c1, in_flight ? 28.0f : 20.0f);
    perf_mode_label(p, 165.5f, 498.5f, tb.mode[1], c2, 20.0f);
    perf_mode_label(p, 160.5f, 554.0f, tb.mode[2], MFD_COL_WHITE, 20.0f);

    if (not_yet_clb) {
        perf_field_init(&pf, A380X_FMS_F_PRESEL_CLB, 0.0f, 415.0f, 0.0f, MFD_ANCHOR_RIGHT);
        pf.cx = PERF_CLB_CX_SPD;
        pf.drop_ff = A380X_FMS_FF_HIDDEN;
        perf_field(p, PERF_ID_CLB + 5, &pf);
    } else {
        perf_cell_value(p, PERF_CLB_CX_SPD, 433.5f, tb.spd[0], "KT", vc);
    }
    perf_cell_value(p, PERF_CLB_CX_MACH, 433.5f, tb.mach[0], NULL, vc);
    perf_cell_value(p, PERF_CX_PRED, 433.5f, tb.pred[0], NULL, vc);
    perf_cell_value(p, PERF_CLB_CX_SPD, 498.5f, tb.spd[1], "KT", vc);
    perf_cell_value(p, PERF_CLB_CX_MACH, 498.5f, tb.mach[1], NULL, vc);
    perf_cell_value(p, PERF_CX_PRED, 498.5f, tb.pred[1], NULL, vc);
    perf_cell_value(p, PERF_CLB_CX_SPD, 554.0f, tb.spd[2], "KT", vc);
    perf_cell_value(p, PERF_CLB_CX_MACH, 554.0f, tb.mach[2], NULL, vc);

    /* ---- THR RED / ACCEL / NOISE / CLB SPD LIM grid (rows 629/679/729) ----
     * [PORT] hidden whenever the loaded plan is Active (or a copy) and the phase >= CLIMB. */
    noise = perf_get(p, A380X_FMS_F_NOISE).has && perf_get(p, A380X_FMS_F_NOISE).v != 0.0;
    if (!(p->aoc && p->phase >= A380X_FMS_PHASE_CLIMB)) {
        perf_txt(p, 144.0f, 646.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "THR RED");
        perf_field_init(&pf, A380X_FMS_F_THR_RED, 199.8f, 629.0f, 150.0f, MFD_ANCHOR_RIGHT);
        perf_field(p, PERF_ID_CLB + 10, &pf);
        perf_txt(p, 144.0f, 696.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "ACCEL");
        perf_field_init(&pf, A380X_FMS_F_ACC, 199.8f, 679.0f, 150.0f, MFD_ANCHOR_RIGHT);
        perf_field(p, PERF_ID_CLB + 11, &pf);
        if (noise) {
            perf_noise_arrow(p, 381.4f, 629.0f);
            perf_txt(p, 466.4f, 646.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "N1");
            perf_field_init(&pf, A380X_FMS_F_NOISE_N1, 481.4f, 629.0f, 110.0f, MFD_ANCHOR_RIGHT);
            perf_field(p, PERF_ID_CLB + 12, &pf);
            perf_noise_arrow(p, 381.4f, 679.0f);
            perf_txt(p, 466.4f, 696.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "SPD");
            perf_field_init(&pf, A380X_FMS_F_NOISE_SPD, 481.4f, 679.0f, 110.0f, MFD_ANCHOR_RIGHT);
            perf_field(p, PERF_ID_CLB + 13, &pf);
            if (!to_inactive &&
                mfd_button(p->ui, PID(PERF_ID_CLB + 14),
                           mfd_rect(615.2f, 648.0f, mfd_button_auto_width("CANCEL\nNOISE"), 62.0f), "CANCEL\nNOISE", 0))
                a380x_fms_cancel_noise(p->fms, p->plan);
            perf_txt(p, 169.0f, 746.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "NOISE END");
            perf_field_init(&pf, A380X_FMS_F_NOISE_END, 199.8f, 729.0f, 150.0f, MFD_ANCHOR_RIGHT);
            perf_field(p, PERF_ID_CLB + 15, &pf);
        } else {
            char fl[12];
            perf_txt(p, 169.0f, 746.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "CLB SPD LIM");
            perf_fmt_int(sl, v, sizeof(v));
            if (sla.has) mfd_js_to_fixed(sla.v / 100.0, 0, fl, sizeof(fl));
            else snprintf(fl, sizeof(fl), "---");
            x = perf_lvu(p, 199.8f + 7.0f, 748.5f, NULL, v, vc, "KT") + 7.0f;
            perf_txt(p, x, 748.5f, MFD_ANCHOR_LEFT, 25.0f, vc, "/");
            x += perf_w("/", 25.0f) + 7.0f;
            perf_txt(p, x, 748.5f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, "FL");
            perf_txt(p, x + perf_w("FL", 20.0f) + 6.0f, 748.5f, MFD_ANCHOR_LEFT, 25.0f, vc, fl);
        }
    }

    /* ---- bottom row (right-aligned, 817..868, centre 842.5) ---- */
    w = mfd_button_auto_width("SPD CSTR");
    {
        MfdButtonDesc bd;
        mfd_button_desc_init(&bd, "SPD CSTR");
        bd.flags = clb_inactive ? MFD_BTN_DISABLED : 0u;
        if (mfd_button_ex(p->ui, PID(PERF_ID_CLB + 20), mfd_rect(752.0f - w, 822.5f, w, 40.0f), &bd) ==
            MFD_BUTTON_CLICKED) {
            char uri[MFD_URI_LEN];
            snprintf(uri, sizeof(uri), "fms/%s/f-pln-vert-rev/spd", p->ctx->uri->category);
            mfd_navigate(p->ctx, uri);
        }
    }
    x = 752.0f - w - 7.0f - 150.0f - 15.0f - perf_w("TRANS", 20.0f);
    perf_txt(p, x, 842.5f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "TRANS");
    perf_field_init(&pf, A380X_FMS_F_TRANS_ALT, x + perf_w("TRANS", 20.0f) + 15.0f, 824.0f, 150.0f, MFD_ANCHOR_RIGHT);
    pf.drop_ff = A380X_FMS_FF_INACTIVE | A380X_FMS_FF_DISABLED;
    perf_field(p, PERF_ID_CLB + 21, &pf);
    if (!noise && !to_inactive) {
        float nw = mfd_button_auto_width("NOISE");
        mfd_button(p->ui, PID(PERF_ID_CLB + 22), mfd_rect(x - 7.0f - 50.0f - nw, 822.5f, nw, 40.0f), "NOISE",
                   MFD_BTN_DISABLED);
    }
}

/* ======================================================================================== */
/* CRZ tab (spec 04 §7)                                                                     */
/* ======================================================================================== */

static void perf_tab_crz(PerfCtx *p)
{
    PerfTable tb;
    PerfField pf;
    char crz_m[12], crz_spd[12];
    A380XFmsValue pm = perf_get(p, A380X_FMS_F_PRESEL_CRZ_MACH), ps = perf_get(p, A380X_FMS_F_PRESEL_CRZ_SPD);
    int presel = pm.has || ps.has;
    int in_flight = p->phase >= A380X_FMS_PHASE_CLIMB && p->phase <= A380X_FMS_PHASE_DESCENT;
    int not_yet_crz = !p->aoc || p->phase < A380X_FMS_PHASE_CRUISE;
    const A380XFmsInputs *in = a380x_fms_inputs(p->fms);
    MfdColor vc = perf_value_colour(p), c1, c2;
    double fcu = perf_fcu_speed(p);
    int fcu_mach = isfinite(fcu) && fcu < 1.0;

    perf_ci_row(p, PERF_ID_CRZ + 1, 277.0f, 0);

    /* ---- table: H 306..401, L1 401..470, L2 470..531, L3 531..581, LRC 581..630, MAX TURB 630..679 ---- */
    perf_table_header(p, 353.0f, "MACH", PERF_CRZ_CX_MACH, "SPD", PERF_CRZ_CX_SPD);
    perf_hline(p, 4.0f, 754.0f, 400.0f, MFD_COL_LIGHT_GREY);
    perf_hline(p, 4.0f, 754.0f, 469.0f, MFD_COL_LIGHT_GREY);
    perf_hline(p, 4.0f, 754.0f, 530.0f, MFD_COL_LIGHT_GREY);
    perf_hline(p, 4.0f, 754.0f, 580.0f, MFD_COL_LIGHT_GREY);
    perf_vline(p, 175.5f, 306.0f, 679.0f, MFD_COL_LIGHT_GREY);

    /* prediction block: no VNAV prediction in the port -> STEP block (FBW `else` branch) with the
     * next cruise step (none available from the FMS yet: empty waypoint / FL). */
    {
        const char *wpt = "", *fl = "";
        float w1 = perf_w("AT", 20.0f) + 15.0f + perf_w(wpt, 30.0f);
        float w2 = perf_w("STEP TO", 20.0f) + 15.0f + 7.0f + perf_w("FL", 20.0f) + 6.0f + perf_w(fl, 30.0f) + 7.0f;
        float x1 = PERF_CX_PRED - w1 * 0.5f, x2 = PERF_CX_PRED - w2 * 0.5f;
        perf_txt(p, x1, 331.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "AT");
        perf_txt(p, x1 + perf_w("AT", 20.0f) + 15.0f, 331.0f, MFD_ANCHOR_LEFT, 30.0f, vc, wpt);
        perf_txt(p, x2, 368.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "STEP TO");
        x2 += perf_w("STEP TO", 20.0f) + 15.0f + 7.0f;
        perf_txt(p, x2, 368.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, "FL");
        perf_txt(p, x2 + perf_w("FL", 20.0f) + 6.0f, 368.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, fl);
    }

    memset(&tb, 0, sizeof(tb));
    perf_fmt_mach(perf_get(p, A380X_FMS_F_CRZ_ECON_MACH).v, crz_m, sizeof(crz_m));
    perf_fmt_int(perf_get(p, A380X_FMS_F_CRZ_ECON_SPD), crz_spd, sizeof(crz_spd));
    if (p->phase < A380X_FMS_PHASE_CRUISE) {
        snprintf(tb.mode[0], 12, "PRESEL");
        snprintf(tb.mode[1], 12, "MANAGED");
        snprintf(tb.spd[1], 12, "---");
        snprintf(tb.mach[1], 12, "%s", crz_m);
        snprintf(tb.pred[1], 20, "--:-- ----" /* FBW "--:--   ----": HTML collapses the spaces */);
    } else if (in->speed_managed) {
        snprintf(tb.mode[0], 12, "MANAGED");
        snprintf(tb.spd[0], 12, "%s", fcu_mach ? "---" : crz_spd); /* [PORT] managed CRZ speed (FBW: CLB) */
        snprintf(tb.mach[0], 12, "%s", fcu_mach ? crz_m : ".--");
    } else {
        snprintf(tb.mode[0], 12, "SELECTED");
        if (fcu_mach) {
            snprintf(tb.spd[0], 12, "---");
            perf_fmt_mach(fcu, tb.mach[0], 12);
        } else if (isfinite(fcu)) {
            mfd_js_to_fixed(fcu, 0, tb.spd[0], 12);
        }
        snprintf(tb.mode[1], 12, "MANAGED");
        snprintf(tb.spd[1], 12, "%s", fcu_mach ? "---" : crz_spd);
        snprintf(tb.mach[1], 12, "%s", fcu_mach ? crz_m : ".--");
    }
    if (in_flight && p->eo) c1 = MFD_COL_AMBER;
    else if ((in_flight && !p->eo) || (p->phase < A380X_FMS_PHASE_CLIMB && presel)) c1 = MFD_COL_GREEN;
    else c1 = MFD_COL_WHITE;
    c2 = (p->phase < A380X_FMS_PHASE_CLIMB && in->speed_managed && !presel) ? MFD_COL_GREEN : MFD_COL_WHITE;
    perf_mode_label(p, 165.5f, 435.5f, tb.mode[0], c1, in_flight ? 28.0f : 20.0f);
    perf_mode_label(p, 165.5f, 500.5f, tb.mode[1], c2, 20.0f);

    if (not_yet_crz) {
        perf_field_init(&pf, A380X_FMS_F_PRESEL_CRZ_MACH, 0.0f, 417.0f, 0.0f, MFD_ANCHOR_RIGHT);
        pf.cx = PERF_CRZ_CX_MACH;
        pf.drop_ff = A380X_FMS_FF_HIDDEN;
        perf_field(p, PERF_ID_CRZ + 5, &pf);
        perf_field_init(&pf, A380X_FMS_F_PRESEL_CRZ_SPD, 0.0f, 417.0f, 0.0f, MFD_ANCHOR_RIGHT);
        pf.cx = PERF_CRZ_CX_SPD;
        pf.drop_ff = A380X_FMS_FF_HIDDEN;
        perf_field(p, PERF_ID_CRZ + 6, &pf);
    } else {
        perf_cell_value(p, PERF_CRZ_CX_MACH, 435.5f, tb.mach[0], NULL, MFD_COL_GREEN);
        perf_cell_value(p, PERF_CRZ_CX_SPD, 435.5f, tb.spd[0], "KT", vc);
    }
    perf_cell_value(p, PERF_CRZ_CX_MACH, 500.5f, tb.mach[1], NULL, vc);
    perf_cell_value(p, PERF_CRZ_CX_SPD, 500.5f, tb.spd[1], "KT", vc);
    perf_cell_value(p, PERF_CX_PRED, 500.5f, tb.pred[1], "NM", vc);

    /* LRC / MAX TURB rows: FBW hard-coded strings [FBW-PH] */
    perf_mode_label(p, 170.5f, 605.5f, "LRC", MFD_COL_WHITE, 20.0f);
    perf_cell_value(p, PERF_CRZ_CX_MACH, 605.5f, ".84", NULL, vc);
    perf_cell_value(p, PERF_CRZ_CX_SPD, 605.5f, "---", NULL, vc);
    perf_mode_label(p, 170.5f, 654.5f, "MAX TURB", MFD_COL_WHITE, 20.0f);
    perf_cell_value(p, PERF_CRZ_CX_MACH, 654.5f, ".85", NULL, vc);
    perf_cell_value(p, PERF_CRZ_CX_SPD, 654.5f, "---", NULL, vc);

    perf_dest_row(p, 0);
}

/* ======================================================================================== */
/* DES tab (spec 04 §8)                                                                     */
/* ======================================================================================== */

static void perf_tab_des(PerfCtx *p)
{
    PerfField pf;
    char des_spd[12], des_m[12], fcu_s[12], fcu_m[12];
    int in_flight = p->phase >= A380X_FMS_PHASE_CLIMB && p->phase <= A380X_FMS_PHASE_DESCENT;
    int not_yet_des = !p->aoc || p->phase < A380X_FMS_PHASE_DESCENT;
    int crz_inactive = p->aoc && p->phase > A380X_FMS_PHASE_CRUISE;
    const A380XFmsInputs *in = a380x_fms_inputs(p->fms);
    MfdColor vc = perf_value_colour(p);
    double fcu = perf_fcu_speed(p);
    float w;

    /* ---- header 248..315 (centre 281.5) ---- */
    perf_ci_row(p, PERF_ID_DES + 1, 281.5f, crz_inactive);
    if (p->at) {
        perf_field_init(&pf, A380X_FMS_F_DES_CABIN_RATE, 564.0f, 263.0f, 175.0f, MFD_ANCHOR_RIGHT);
        w = perf_field(p, PERF_ID_DES + 3, &pf);
        if (w > 0.0f) perf_txt(p, 549.0f, 281.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "DES CABIN RATE");
    }

    /* ---- table: H 315..393, L1 393..462, L2 462..523, filler 523..598 ---- */
    perf_table_header(p, 354.0f, "MACH", PERF_CRZ_CX_MACH, "SPD", PERF_CRZ_CX_SPD);
    if (p->at) {
        /* PRED TO: page-local value, FL format relative to TRANS FL, disabled when not in descent */
        MfdFieldDesc d;
        MfdFieldValue fv;
        MfdValue out;
        A380XFmsValue tl = perf_get(p, A380X_FMS_F_TRANS_FL);
        MfdFieldResult r;
        perf_txt(p, 514.0f, 335.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "PRED TO" PERF_NBSP);
        mfd_field_desc_init(&d, MFD_FMT_ALT_OR_FL);
        d.fmt.min = 0.0;
        d.fmt.max = 43000.0;
        d.fmt.has_trans = tl.has;
        d.fmt.trans = tl.has ? tl.v : 0.0;
        d.fmt.trans_is_fl = 1;
        d.width = 150.0f;
        d.align = MFD_ANCHOR_RIGHT;
        if (not_yet_des || p->phase != A380X_FMS_PHASE_DESCENT) d.flags |= MFD_FIELD_DISABLED;
        if (p->plan == A380X_FMS_PLAN_TMPY) d.flags |= MFD_FIELD_TMPY;
        memset(&fv, 0, sizeof(fv));
        fv.has_value = p->st->des_pred_has;
        fv.num = p->st->des_pred_ft;
        r = mfd_input_field(p->ui, PID(PERF_ID_DES + 4), 529.0f, 345.0f, &d, &fv, &out);
        if (r == MFD_FIELD_COMMITTED) {
            p->st->des_pred_has = 1;
            p->st->des_pred_ft = out.num;
        } else if (r == MFD_FIELD_CLEARED) {
            p->st->des_pred_has = 0;
        }
    }
    perf_hline(p, 4.0f, 754.0f, 392.0f, MFD_COL_LIGHT_GREY);
    perf_hline(p, 4.0f, 754.0f, 461.0f, MFD_COL_LIGHT_GREY);
    perf_hline(p, 4.0f, 754.0f, 522.0f, MFD_COL_LIGHT_GREY);
    perf_vline(p, 175.5f, 315.0f, 598.0f, MFD_COL_LIGHT_GREY);

    perf_fmt_int(perf_get(p, A380X_FMS_F_DES_MANAGED_SPD), des_spd, sizeof(des_spd));
    perf_fmt_mach(perf_get(p, A380X_FMS_F_DES_MANAGED_MACH).v, des_m, sizeof(des_m));
    fcu_s[0] = fcu_m[0] = '\0';
    if (isfinite(fcu) && fcu >= 1.0) mfd_js_to_fixed(fcu, 0, fcu_s, sizeof(fcu_s));
    if (isfinite(fcu) && fcu < 1.0) perf_fmt_mach(fcu, fcu_m, sizeof(fcu_m));

    {
        int selected = p->phase >= A380X_FMS_PHASE_DESCENT && !in->speed_managed;
        perf_mode_label(p, 165.5f, 427.5f, selected ? "SELECTED" : "MANAGED", in_flight ? MFD_COL_GREEN : MFD_COL_WHITE,
                        in_flight ? 28.0f : 20.0f);
        /* MACH cell: input while in descent, otherwise the (empty) static line */
        if (!not_yet_des) {
            perf_field_init(&pf, A380X_FMS_F_DES_MANAGED_MACH, 0.0f, 409.0f, 0.0f, MFD_ANCHOR_RIGHT);
            pf.cx = PERF_CRZ_CX_MACH;
            pf.drop_ff = A380X_FMS_FF_HIDDEN | A380X_FMS_FF_INACTIVE;
            perf_field(p, PERF_ID_DES + 5, &pf);
        } else if (p->phase >= A380X_FMS_PHASE_DESCENT) {
            perf_cell_value(p, PERF_CRZ_CX_MACH, 427.5f, selected ? fcu_m : des_m, NULL, MFD_COL_GREEN);
        }
        /* SPD cell: input before descent, otherwise static */
        if (not_yet_des) {
            perf_field_init(&pf, A380X_FMS_F_DES_MANAGED_SPD, 0.0f, 409.0f, 0.0f, MFD_ANCHOR_RIGHT);
            pf.cx = PERF_CRZ_CX_SPD;
            pf.drop_ff = A380X_FMS_FF_HIDDEN | A380X_FMS_FF_INACTIVE;
            perf_field(p, PERF_ID_DES + 6, &pf);
        } else {
            perf_cell_value(p, PERF_CRZ_CX_SPD, 427.5f, selected ? fcu_s : des_spd, "KT", MFD_COL_GREEN);
        }
        perf_cell_value(p, PERF_CX_PRED, 427.5f, "--:-- ----", NULL, vc);
        if (selected) {
            perf_mode_label(p, 165.5f, 492.5f, "MANAGED", MFD_COL_WHITE, 20.0f);
            perf_cell_value(p, PERF_CRZ_CX_MACH, 492.5f, des_m, NULL, vc);
            perf_cell_value(p, PERF_CRZ_CX_SPD, 492.5f, des_spd, "KT", vc);
        }
    }
    perf_dest_row(p, 1);
}

/* ======================================================================================== */
/* APPR tab (spec 04 §9)                                                                    */
/* ======================================================================================== */

static void perf_tab_appr(PerfCtx *p)
{
    static const char *const conf[] = {"CONF 3", "FULL"};
    PerfField pf;
    char appr[16], dest[12], lw[16], v[16];
    const char *unit = p->metric ? "T" : "KLB";
    MfdColor vc = perf_value_colour(p), c;
    A380XFmsValue hw, xw, lc;
    unsigned rf;
    int precision, k;
    float w1, w2, w3, gap, x;

    /* ---- header 258..314 (border 313), centre 285.5: APPR ident | DEST | LW ---- */
    appr[0] = dest[0] = '\0';
    if (p->fms && p->plan >= 0) {
        a380x_fms_get_text(p->fms, p->plan, A380X_FMS_T_APPROACH, appr, sizeof(appr));
        a380x_fms_get_text(p->fms, p->plan, A380X_FMS_T_TO, dest, sizeof(dest));
    }
    if (!appr[0]) snprintf(appr, sizeof(appr), "-------");
    if (!dest[0]) snprintf(dest, sizeof(dest), "----");
    perf_fmt_weight(p, perf_get(p, A380X_FMS_F_LW), lw, sizeof(lw));
    w1 = 15.0f + perf_w("APPR", 20.0f) + 15.0f + perf_w(appr, 25.0f) + 15.0f;
    w2 = 30.0f + perf_w(dest, 25.0f);
    w3 = 15.0f + perf_w("LW", 20.0f) + 15.0f + perf_w(lw, 25.0f) + 6.0f + perf_w(unit, 20.0f) + 15.0f;
    gap = (740.0f - w1 - w2 - w3) * 0.5f;
    perf_lvu(p, 29.0f, 285.5f, "APPR", appr, vc, NULL);
    perf_txt(p, 14.0f + w1 + gap + 15.0f, 285.5f, MFD_ANCHOR_LEFT, 25.0f, vc, dest);
    perf_lvu(p, 754.0f - w3 + 15.0f, 285.5f, "LW", lw, vc, unit);
    mfd_draw_fill_rect(p->dl, mfd_rect(14.0f, 313.0f, 740.0f, 1.0f), MFD_COL_CSS_LIGHTGREY);

    /* ---- weather box x 14..385.1, y 334..588 ---- */
    mfd_draw_bevel(p->dl, mfd_rect(14.0f, 334.0f, 371.1f, 254.0f), 1.0f, MFD_COL_CSS_LIGHTGREY, MFD_BEVEL_SOLID);
    perf_txt(p, 152.5f, 371.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "MAG WIND");
    mfd_draw_bevel(p->dl, mfd_rect(167.5f, 350.0f, 201.6f, 43.0f), 1.0f, MFD_COL_CSS_LIGHTGREY, MFD_BEVEL_SOLID);
    perf_field_init(&pf, A380X_FMS_F_APPR_WIND_DIR, 170.5f, 353.0f, 0.0f, MFD_ANCHOR_CENTER);
    perf_field(p, PERF_ID_APPR + 1, &pf);
    perf_field_init(&pf, A380X_FMS_F_APPR_WIND_SPD, 267.0f, 353.0f, 0.0f, MFD_ANCHOR_CENTER);
    perf_field(p, PERF_ID_APPR + 2, &pf);
    hw = perf_get(p, A380X_FMS_F_APPR_HEADWIND);
    xw = perf_get(p, A380X_FMS_F_APPR_CROSSWIND);
    if (hw.has) snprintf(v, sizeof(v), "%03.0f", fabs(hw.v));
    else snprintf(v, sizeof(v), "---");
    perf_lvu(p, 45.0f, 435.5f, (hw.has && hw.v < 0.0) ? "TL" : "HD", v, vc, "KT");
    if (xw.has) snprintf(v, sizeof(v), "%03.0f", fabs(xw.v));
    else snprintf(v, sizeof(v), "---");
    perf_lvu(p, 193.2f, 435.5f, "CROSS", v, vc, "KT");
    perf_txt(p, 155.0f, 501.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "OAT");
    perf_field_init(&pf, A380X_FMS_F_APPR_TEMP, 170.0f, 483.0f, 125.0f, MFD_ANCHOR_RIGHT);
    perf_field(p, PERF_ID_APPR + 3, &pf);
    perf_txt(p, 155.0f, 553.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "QNH");
    perf_field_init(&pf, A380X_FMS_F_APPR_QNH, 170.0f, 535.0f, 125.0f, MFD_ANCHOR_RIGHT);
    perf_field(p, PERF_ID_APPR + 4, &pf);

    /* ---- MINIMUM box x 14..385.1 from 628 (precision 121 tall, else 104) ---- */
    rf = perf_flags(p, A380X_FMS_F_APPR_RADIO);
    precision = !(rf & A380X_FMS_FF_HIDDEN) && p->fms && p->plan >= 0;
    mfd_draw_bevel(p->dl, mfd_rect(14.0f, 628.0f, 371.1f, precision ? 121.0f : 104.0f), 1.0f, MFD_COL_LIGHT_GREY,
                   MFD_BEVEL_SOLID);
    mfd_draw_fill_rect(p->dl, mfd_rect(25.0f, 619.0f, perf_w("MINIMUM", 20.0f) + 4.0f, 20.0f), MFD_COL_BLACK);
    perf_txt(p, 27.0f, 629.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "MINIMUM");
    perf_txt(p, 155.0f, 662.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "BARO");
    perf_field_init(&pf, A380X_FMS_F_APPR_BARO, 170.0f, 644.0f, 150.0f, MFD_ANCHOR_RIGHT);
    perf_field(p, PERF_ID_APPR + 5, &pf);
    perf_txt(p, 155.0f, precision ? 714.5f : 706.0f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE,
             precision ? "RADIO" : "-----");
    if (precision) {
        perf_field_init(&pf, A380X_FMS_F_APPR_RADIO, 170.0f, 696.0f, 150.0f, MFD_ANCHOR_RIGHT);
        perf_field(p, PERF_ID_APPR + 6, &pf);
    }

    /* ---- right column (centre 589.55): GD / S / F / VREF, LDG CONF box, VAPP ---- */
    perf_green_dot(p, 555.0f, 357.0f);
    perf_value_of(p, A380X_FMS_F_APPR_GD, v, sizeof(v), &c);
    perf_lvu(p, 583.0f, 363.5f, NULL, v, c, "KT");
    perf_txt(p, 568.0f, 402.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "S");
    perf_value_of(p, A380X_FMS_F_APPR_S, v, sizeof(v), &c);
    perf_lvu(p, 583.0f, 402.5f, NULL, v, c, "KT");
    perf_txt(p, 568.0f, 441.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "F");
    perf_value_of(p, A380X_FMS_F_APPR_F, v, sizeof(v), &c);
    perf_lvu(p, 583.0f, 441.5f, NULL, v, c, "KT");
    if (p->at) {
        perf_txt(p, 518.0f, 488.5f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "VREF");
        perf_value_of(p, A380X_FMS_F_APPR_VREF, v, sizeof(v), NULL);
        perf_lvu(p, 583.0f, 488.5f, NULL, v, MFD_COL_GREEN, "KT");
    }
    mfd_draw_bevel(p->dl, mfd_rect(489.55f, 528.0f, 200.0f, 149.0f), 1.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_SOLID);
    lc = perf_get(p, A380X_FMS_F_LDG_CONF);
    k = mfd_radio_group(p->ui, PID(PERF_ID_APPR + 10), 516.0f, 529.0f, 147.0f, conf, 2, lc.has ? (int)lc.v : 1, 0u,
                        MFD_COL_CYAN, 15.0f);
    if (k >= 0 && p->fms && p->plan >= 0) {
        MfdFmsError err;
        int ok;
        memset(&err, 0, sizeof(err));
        ok = a380x_fms_enter(p->fms, p->plan, A380X_FMS_F_LDG_CONF, (double)k, &err);
        perf_report(p, ok, &err, 0);
    }
    perf_value_of(p, A380X_FMS_F_APPR_VLS, v, sizeof(v), &c);
    perf_lvu(p, 524.2f, 656.5f, "VLS", v, c, "KT");
    perf_txt(p, 507.4f, 715.5f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "VAPP");
    perf_field_init(&pf, A380X_FMS_F_VAPP, 507.4f + perf_w("VAPP", 20.0f) + 15.0f, 697.0f, 0.0f, MFD_ANCHOR_RIGHT);
    pf.drop_ff = A380X_FMS_FF_INACTIVE | A380X_FMS_FF_MANDATORY;
    perf_field(p, PERF_ID_APPR + 11, &pf);

    /* ---- bottom row 803..868 (centre 840.5): TRANS FL | VERT DEV ---- */
    perf_txt(p, 148.0f, 840.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "TRANS");
    perf_field_init(&pf, A380X_FMS_F_TRANS_FL, 163.0f, 822.0f, 110.0f, MFD_ANCHOR_LEFT);
    pf.drop_ff = A380X_FMS_FF_INACTIVE | A380X_FMS_FF_DISABLED;
    perf_field(p, PERF_ID_APPR + 12, &pf);
    if (p->at) {
        const char *vd = "+-----";
        A380XFmsValue dv = perf_get(p, A380X_FMS_F_VERT_DEV);
        char t[16];
        if (dv.has && p->plan == A380X_FMS_PLAN_ACTIVE && p->phase >= A380X_FMS_PHASE_DESCENT) {
            snprintf(t, sizeof(t), "%+.0fFT", dv.v);
            vd = t;
        }
        x = 737.0f - perf_w(vd, 25.0f);
        perf_txt(p, x, 840.5f, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_GREEN, vd);
        perf_txt(p, x - 15.0f, 840.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "VERT DEV");
    }
}

/* ======================================================================================== */
/* GA tab (spec 04 §10)                                                                     */
/* ======================================================================================== */

static void perf_tab_ga(PerfCtx *p)
{
    PerfField pf;
    char v[16];
    MfdColor c;
    A380XFmsValue tr;
    float x;

    perf_value_of(p, A380X_FMS_F_APPR_F, v, sizeof(v), &c);
    perf_lvu(p, 221.0f, 337.5f, "F", v, c, "KT");
    perf_value_of(p, A380X_FMS_F_APPR_S, v, sizeof(v), &c);
    perf_lvu(p, 221.0f, 376.5f, "S", v, c, "KT");
    perf_green_dot(p, 221.0f, 409.0f);
    perf_value_of(p, A380X_FMS_F_APPR_GD, v, sizeof(v), &c);
    perf_lvu(p, 249.0f, 415.5f, NULL, v, c, "KT");

    perf_txt(p, 164.0f, 553.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "THR RED");
    perf_field_init(&pf, A380X_FMS_F_GA_THR_RED, 179.0f, 535.0f, 150.0f, MFD_ANCHOR_RIGHT);
    pf.drop_ff = A380X_FMS_FF_INACTIVE;
    perf_field(p, PERF_ID_GA + 1, &pf);
    perf_txt(p, 164.0f, 605.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "ACCEL");
    perf_field_init(&pf, A380X_FMS_F_GA_ACC, 179.0f, 587.0f, 150.0f, MFD_ANCHOR_RIGHT);
    pf.drop_ff = A380X_FMS_FF_INACTIVE;
    perf_field(p, PERF_ID_GA + 2, &pf);
    perf_txt(p, 494.0f, 605.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "EO ACCEL");
    perf_field_init(&pf, A380X_FMS_F_GA_EO_ACC, 509.0f, 587.0f, 150.0f, MFD_ANCHOR_RIGHT);
    pf.drop_ff = A380X_FMS_FF_INACTIVE;
    perf_field(p, PERF_ID_GA + 3, &pf);

    perf_txt(p, 171.0f, 851.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "TRANS");
    tr = perf_get(p, A380X_FMS_F_GA_TRANS);
    perf_fmt_int(tr, v, sizeof(v));
    x = 186.0f;
    perf_txt(p, x, 851.5f, MFD_ANCHOR_LEFT, 25.0f, perf_value_colour(p), v);
    perf_txt(p, x + perf_w(v, 25.0f) + 6.0f, 851.5f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, "FT");
}

/* ======================================================================================== */
/* Page footer row and dialogs (spec 04 §11)                                                */
/* ======================================================================================== */

static void perf_footer(PerfCtx *p)
{
    float x = 2.0f, w;
    int show_appr = p->fms && p->plan >= 0 && a380x_fms_can_activate_approach(p->fms, p->plan);

    w = mfd_button_auto_width("RETURN");
    if (mfd_button(p->ui, PID(PERF_ID_FOOTER + 1), mfd_rect(x, 907.0f, w, 40.0f), "RETURN", 0))
        mfd_navigate_back(p->ctx);
    x += w + 5.0f;
    w = perf_star_button_w("ACTIVATE", "APPR");
    if (perf_star_button(p, PERF_ID_FOOTER + 2, mfd_rect(x, 885.0f, w, 62.0f), "ACTIVATE", "APPR", MFD_COL_AMBER,
                         show_appr ? 0u : MFD_BTN_HIDDEN))
        p->st->dialog = MFD_PERF_DLG_ACTIVATE_APPR;
    x += w + 5.0f;
    w = mfd_button_auto_width("POS MONITOR");
    if (mfd_button(p->ui, PID(PERF_ID_FOOTER + 3), mfd_rect(x, 907.0f, w, 40.0f), "POS MONITOR",
                   p->at ? 0u : MFD_BTN_HIDDEN))
        mfd_navigate(p->ctx, "fms/position/monitor/withReturn");
    x += w + 5.0f;
    w = perf_star_button_w("CLEAR", "EO");
    if (perf_star_button(p, PERF_ID_FOOTER + 4, mfd_rect(x, 885.0f, w, 62.0f), "CLEAR", "EO", MFD_COL_AMBER,
                         p->eo ? 0u : MFD_BTN_HIDDEN))
        p->st->dialog = MFD_PERF_DLG_CLEAR_EO;
}

static void perf_footer_dialogs(PerfCtx *p)
{
    int r;
    if (p->st->dialog == MFD_PERF_DLG_ACTIVATE_APPR) {
        r = mfd_confirm_dialog(p->ui, PID(PERF_ID_DIALOG), mfd_rect(2.0f, 783.0f, 280.0f, 165.0f),
                               "ACTIVATE APPR" PERF_NBSP "?", 1);
        if (r == MFD_DIALOG_CONFIRM && p->fms) a380x_fms_activate_approach(p->fms);
        if (r != MFD_DIALOG_NONE) p->st->dialog = MFD_PERF_DLG_NONE;
    } else if (p->st->dialog == MFD_PERF_DLG_CLEAR_EO) {
        MfdLayer old;
        r = mfd_confirm_dialog(p->ui, PID(PERF_ID_DIALOG + 10), mfd_rect(2.0f, 783.0f, 450.0f, 165.0f),
                               "CLEAR EO" PERF_NBSP "?", 1);
        old = mfd_draw_get_layer(p->dl);
        mfd_draw_set_layer(p->dl, MFD_LAYER_DIALOG);
        mfd_text(p->ui, 2.0f + 225.0f, 783.0f + 67.0f + MFD_FONT_BASELINE * 20.0f, MFD_ANCHOR_CENTER, 20.0f,
                 MFD_COL_WHITE, "(BACK TO ALL ENGs COMPUTATION)");
        mfd_draw_set_layer(p->dl, old);
        if (r == MFD_DIALOG_CONFIRM && p->fms) a380x_fms_clear_engine_out(p->fms);
        if (r != MFD_DIALOG_NONE) p->st->dialog = MFD_PERF_DLG_NONE;
    }
}

/* ======================================================================================== */
/* Page                                                                                     */
/* ======================================================================================== */

static void perf_page(MfdPageContext *ctx)
{
    static const char *const titles[MFD_PERF_TAB_COUNT] = {"T.O", "CLB", "CRZ", "DES", "APPR", "GA"};
    static const A380XFmsPage fpages[MFD_PERF_TAB_COUNT] = {A380X_FMS_PAGE_PERF_TO, A380X_FMS_PAGE_PERF_CLB,
                                                            A380X_FMS_PAGE_PERF_CRZ, A380X_FMS_PAGE_PERF_DES,
                                                            A380X_FMS_PAGE_PERF_APPR, A380X_FMS_PAGE_PERF_GA};
    MfdPerfState *st = MFD_PAGE_STATE(ctx, MfdPerfState);
    PerfCtx pc;
    MfdTabsDesc td;
    MfdRect content, clip;
    int k;

    memset(&pc, 0, sizeof(pc));
    pc.ctx = ctx;
    pc.ui = ctx->ui;
    pc.dl = ctx->dl;
    pc.fms = ctx->fms;
    pc.st = st;
    pc.plan = -1;
    pc.phase = ctx->in ? ctx->in->flight_phase : 0;
    pc.metric = ctx->in ? ctx->in->metric_units : 1;
    if (pc.fms) {
        int uri_plan = ctx->plan >= 0 ? ctx->plan : A380X_FMS_PLAN_ACTIVE;
        pc.plan = a380x_fms_loaded_plan(pc.fms, uri_plan);
        if (pc.plan < 0 && uri_plan >= A380X_FMS_PLAN_SEC1) {
            /* SEC plan deleted while displayed -> ACTIVE/PERF (spec 04 §1.2) */
            mfd_navigate(ctx, "fms/active/perf");
            return;
        }
        pc.phase = a380x_fms_phase(pc.fms);
        pc.eo = a380x_fms_engine_out(pc.fms);
        pc.metric = pc.fms->cfg.metric_units;
        if (pc.plan >= 0) pc.aoc = a380x_fms_is_active_or_copy(pc.fms, pc.plan);
    } else if (ctx->in) {
        pc.eo = ctx->in->engine_out;
    }
    pc.at = pc.plan == A380X_FMS_PLAN_ACTIVE || pc.plan == A380X_FMS_PLAN_TMPY;
    pc.sec = pc.plan >= A380X_FMS_PLAN_SEC1;

    if (ctx->entered) {
        st->tab = perf_initial_tab(&pc);
        st->prev_phase = pc.phase;
        st->dialog = MFD_PERF_DLG_NONE;
        st->derated_prev = -1;
    } else if (pc.phase != st->prev_phase) {
        int nt = mfd_perf_follow_phase(st->tab, st->prev_phase, pc.phase, pc.aoc);
        if (nt != st->tab) {
            st->tab = nt;
            if (st->dialog == MFD_PERF_DLG_DERATED) st->dialog = MFD_PERF_DLG_NONE;
        }
        st->prev_phase = pc.phase;
    }
    if (st->tab < 0 || st->tab >= MFD_PERF_TAB_COUNT) st->tab = 0;
    pc.fpage = fpages[st->tab];

    perf_top_strip(&pc);

    mfd_tabs_desc_init(&td);
    td.highlighted = (pc.phase >= A380X_FMS_PHASE_TAKEOFF && pc.phase <= A380X_FMS_PHASE_GOAROUND) ? pc.phase - 1 : -1;
    td.selected_text = MFD_COL_WHITE;
    k = mfd_top_tabs(ctx->ui, PID(PERF_ID_TABS), mfd_rect(0.0f, PERF_TABS_Y, 768.0f, PERF_TABS_H), titles,
                     MFD_PERF_TAB_COUNT, st->tab, &td, &content, &clip);
    if (k >= 0) {
        st->tab = k;
        if (st->dialog == MFD_PERF_DLG_DERATED) st->dialog = MFD_PERF_DLG_NONE;
        pc.fpage = fpages[st->tab];
    }
    (void)content;

    if (pc.fms && pc.plan >= 0) {
        mfd_draw_clip_push(ctx->dl, clip);
        switch (st->tab) {
        case MFD_PERF_TAB_TO: perf_tab_to(&pc); break;
        case MFD_PERF_TAB_CLB: perf_tab_clb(&pc); break;
        case MFD_PERF_TAB_CRZ: perf_tab_crz(&pc); break;
        case MFD_PERF_TAB_DES: perf_tab_des(&pc); break;
        case MFD_PERF_TAB_APPR: perf_tab_appr(&pc); break;
        default: perf_tab_ga(&pc); break;
        }
        if (st->tab == MFD_PERF_TAB_TO && st->dialog == MFD_PERF_DLG_DERATED) perf_derated_dialog(&pc);
        mfd_draw_clip_pop(ctx->dl);
    }

    perf_footer(&pc);
    perf_footer_dialogs(&pc);
}

void a380x_mfd_page_perf_register(void)
{
    mfd_register_page("fms/*/perf", MFD_PAGE_ID_PERF, "PERF", perf_page,
                      MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY | MFD_PAGE_TMPY);
}
