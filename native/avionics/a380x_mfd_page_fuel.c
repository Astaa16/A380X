/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD ACTIVE/FUEL&LOAD and SECn/FUEL&LOAD. Translation of the pinned FBW
 * pages/FMS/MfdFmsFuelLoad.tsx and MfdFmsFuelLoad.scss (docs/mfd-spec/03-fms-init-fuel-data-sec-msg.md
 * section 4), border-box layout re-based +4 px on the 138 px body top (the RETURN button stays
 * bottom-anchored at 910..950). All values, flags and formulas come from the FMS core.
 */
#include "a380x_mfd_page_fuel.h"
#include "a380x_mfd_page_init.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FUEL_ID(n) MFD_ID(MFD_PAGE_ID_FUEL_LOAD, (n))
#define FUEL_DY 4.0f
#define FUEL_SPACING 15.0f /* .mfd-spacing-right */
#define FUEL_UNIT_GAP 6.0f /* .mfd-unit-trailing */

typedef struct {
    MfdPageContext *ctx;
    A380XFms *fms;
    int plan;
    int metric;
    const char *unit;
} FuelCtx;

/* Label (20 px) / value (25 px) / unit (20 px) group of .mfd-label-value-container.
 * value_top = top of the 25 px value box; labels and units sit 2.5 px lower (same centre).
 * anchor: x is the left edge, centre or right edge of the content (without the 7 px padding). */
static float fuel_group(MfdUi *ui, float x, MfdAnchor anchor, float value_top, const char *label, const char *value,
                        MfdColor vcol, const char *unit)
{
    float lw = label ? mfd_text_width(label, 20.0f) + FUEL_SPACING : 0.0f;
    float vw = mfd_text_width(value, 25.0f);
    float uw = unit ? FUEL_UNIT_GAP + mfd_text_width(unit, 20.0f) : 0.0f;
    float w = lw + vw + uw, x0 = x;
    if (anchor == MFD_ANCHOR_CENTER) x0 = x - w * 0.5f;
    else if (anchor == MFD_ANCHOR_RIGHT) x0 = x - w;
    if (label) mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_LEFT, x0, value_top + 2.5f, label);
    mfd_label(ui, MFD_TXT_VALUE, vcol, MFD_ANCHOR_LEFT, x0 + lw, value_top, value);
    if (unit) mfd_label(ui, MFD_TXT_UNIT, MFD_COL_COUNT, MFD_ANCHOR_LEFT, x0 + lw + vw + FUEL_UNIT_GAP, value_top + 2.5f,
                        unit);
    return w;
}

static A380XFmsValue fuel_get(const FuelCtx *f, A380XFmsField fld)
{
    return a380x_fms_get(f->fms, f->plan, fld);
}

static void fuel_weight(const FuelCtx *f, A380XFmsField fld, char *buf, size_t n, MfdColor *col)
{
    A380XFmsValue v = fuel_get(f, fld);
    a380x_mfd_fms_weight_text(v.has, v.v, f->metric, buf, n);
    if (col) *col = a380x_mfd_fms_value_colour(f->fms, f->plan, &v);
}

static MfdColor fuel_static_colour(const FuelCtx *f)
{
    return f->plan >= A380X_FMS_PLAN_SEC1 ? MFD_COL_WHITE : MFD_COL_GREEN;
}

static void fuel_label_right(MfdUi *ui, float right, float top, const char *s)
{
    mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_RIGHT, right, top + FUEL_DY, s);
}

static void fuel_field(const FuelCtx *f, int n, A380XFmsField fld, float x, float y, float w, MfdAnchor align)
{
    a380x_mfd_fms_field(f->ctx, FUEL_ID(n), f->plan, fld, A380X_FMS_PAGE_FUEL_LOAD, x, y + FUEL_DY, w, align);
}

/* Section A: GW / CG / FOB (box 25..743 x 144..183, padding 7). */
static void fuel_top_line(const FuelCtx *f)
{
    MfdUi *ui = f->ctx->ui;
    char t[32];
    MfdColor c = fuel_static_colour(f);
    A380XFmsValue cg;
    const float vt = 151.0f + FUEL_DY;

    fuel_weight(f, A380X_FMS_F_GW, t, sizeof(t), NULL);
    fuel_group(ui, 32.0f, MFD_ANCHOR_LEFT, vt, "GW", t, c, f->unit);
    cg = fuel_get(f, A380X_FMS_F_CG);
    if (cg.has) mfd_js_to_fixed(cg.v, 1, t, sizeof(t));
    else snprintf(t, sizeof(t), "--.-");
    fuel_group(ui, 377.75f, MFD_ANCHOR_CENTER, vt, "CG", t, c, "%");
    fuel_weight(f, A380X_FMS_F_FOB, t, sizeof(t), NULL);
    fuel_group(ui, 736.0f, MFD_ANCHOR_RIGHT, vt, "FOB", t, c, f->unit);
}

/* Section B: ZFW / ZFWCG / BLOCK / FUEL PLANNING. */
static void fuel_block_lines(const FuelCtx *f)
{
    MfdUi *ui = f->ctx->ui;
    MfdButtonDesc bd;
    MfdRect r;
    unsigned bfl;
    float cx, top;

    fuel_label_right(ui, 175.0f, 201.5f, "ZFW");
    fuel_field(f, 1, A380X_FMS_F_ZFW, 190.0f, 193.0f, 150.0f, MFD_ANCHOR_RIGHT);
    fuel_label_right(ui, 515.0f, 201.5f, "ZFWCG");
    fuel_field(f, 2, A380X_FMS_F_ZFWCG, 530.0f, 193.0f, 125.0f, MFD_ANCHOR_CENTER);

    /* The BLOCK row (label, field and FUEL PLANNING) is visibility:hidden once engines were started. */
    bfl = a380x_fms_field_flags(f->fms, f->plan, A380X_FMS_F_BLOCK, A380X_FMS_PAGE_FUEL_LOAD);
    if (bfl & A380X_FMS_FF_HIDDEN) return;
    fuel_label_right(ui, 175.0f, 266.0f, "BLOCK");
    fuel_field(f, 3, A380X_FMS_F_BLOCK, 190.0f, 257.5f, 150.0f, MFD_ANCHOR_RIGHT);

    /* FUEL PLANNING *: [FBW-PH] always disabled. Two centred lines + '*' 10 px to the right,
     * right padding 2: 151.8 x 62 centred in 340..768. */
    r = mfd_rect(478.1f, 245.0f + FUEL_DY, 151.8f, 62.0f);
    mfd_button_desc_init(&bd, "");
    bd.flags = MFD_BTN_DISABLED;
    bd.pad_right = 2.0f;
    mfd_button_ex(ui, FUEL_ID(4), r, &bd);
    cx = r.x + 14.0f + mfd_text_width("PLANNING", 22.0f) * 0.5f;
    top = r.y + 11.0f;
    mfd_text(ui, cx, mfd_baseline(top, 22.0f), MFD_ANCHOR_CENTER, 22.0f, MFD_COL_DARK_GREY, "FUEL");
    mfd_text(ui, cx, mfd_baseline(top + 22.0f, 22.0f), MFD_ANCHOR_CENTER, 22.0f, MFD_COL_DARK_GREY, "PLANNING");
    mfd_text(ui, r.x + 14.0f + mfd_text_width("PLANNING", 22.0f) + 10.0f, mfd_baseline(top + 11.0f, 22.0f),
             MFD_ANCHOR_LEFT, 22.0f, MFD_COL_DARK_GREY, "*");
}

/* Section C: middle box (lines at 327 and 636). */
static void fuel_middle(const FuelCtx *f)
{
    MfdUi *ui = f->ctx->ui;
    MfdColor c = fuel_static_colour(f);
    char t[32];
    A380XFmsValue v;
    unsigned fl;
    const float dy = FUEL_DY;

    mfd_hrule(ui, 0.0f, (float)MFD_W, 327.0f + dy, 1.0f, MFD_COL_LIGHT_GREY);
    mfd_hrule(ui, 0.0f, (float)MFD_W, 636.0f + dy, 1.0f, MFD_COL_LIGHT_GREY);

    /* left grid: c1 0..125 (labels end at 110), c2 125..280, c3 280..425 */
    fuel_label_right(ui, 110.0f, 351.5f, "TAXI");
    fuel_field(f, 10, A380X_FMS_F_TAXI, 125.0f, 343.0f, 150.0f, MFD_ANCHOR_RIGHT);

    fuel_label_right(ui, 110.0f, 409.5f, "TRIP");
    fuel_weight(f, A380X_FMS_F_TRIP, t, sizeof(t), NULL);
    fuel_group(ui, 273.0f, MFD_ANCHOR_RIGHT, 407.0f + dy, NULL, t, c, f->unit);
    v = fuel_get(f, A380X_FMS_F_TRIP_TIME);
    a380x_mfd_fms_hhmm_text(v.has, v.v, 0, t, sizeof(t));
    mfd_label(ui, MFD_TXT_VALUE, c, MFD_ANCHOR_CENTER, 352.5f, 407.0f + dy, t);

    fuel_label_right(ui, 110.0f, 467.5f, "RTE RSV");
    fuel_field(f, 11, A380X_FMS_F_RTE_RSV, 125.0f, 459.0f, 150.0f, MFD_ANCHOR_RIGHT);
    fuel_field(f, 12, A380X_FMS_F_RTE_RSV_PCT, 285.0f, 459.0f, 120.0f, MFD_ANCHOR_CENTER);

    fuel_label_right(ui, 110.0f, 524.5f, "ALTN");
    fuel_field(f, 13, A380X_FMS_F_ALTN_FUEL, 125.0f, 516.0f, 150.0f, MFD_ANCHOR_RIGHT);
    mfd_label(ui, MFD_TXT_VALUE, c, MFD_ANCHOR_CENTER, 352.5f, 522.0f + dy, "--:--"); /* [FBW-PH] ALTN time */

    fuel_label_right(ui, 110.0f, 581.5f, "FINAL");
    fuel_field(f, 14, A380X_FMS_F_FINAL_FUEL, 125.0f, 573.0f, 150.0f, MFD_ANCHOR_RIGHT);
    fuel_field(f, 15, A380X_FMS_F_FINAL_TIME, 285.0f, 573.0f, 120.0f, MFD_ANCHOR_CENTER);

    /* right grid: labels end at 538.2, values from 553.2 */
    fuel_label_right(ui, 538.2f, 351.5f, "PAX NBR");
    fuel_field(f, 20, A380X_FMS_F_PAX, 553.2f, 343.0f, 75.0f, MFD_ANCHOR_CENTER);
    fuel_label_right(ui, 538.2f, 402.0f, "MODE");
    a380x_mfd_fms_ci_mode_dropdown(f->ctx, FUEL_ID(21), f->plan, A380X_FMS_PAGE_FUEL_LOAD,
                                   mfd_rect(553.2f, 390.0f + dy, 175.0f, MFD_DROPDOWN_H));
    fuel_label_right(ui, 538.2f, 452.5f, "CI");
    fuel_field(f, 22, A380X_FMS_F_CI, 553.2f, 444.0f, 75.0f, MFD_ANCHOR_CENTER);
    fl = a380x_fms_field_flags(f->fms, f->plan, A380X_FMS_F_JTSN_GW, A380X_FMS_PAGE_FUEL_LOAD);
    if (!(fl & A380X_FMS_FF_HIDDEN)) {
        fuel_label_right(ui, 538.2f, 499.5f, "JTSN GW");
        fuel_field(f, 23, A380X_FMS_F_JTSN_GW, 553.2f, 491.0f, 150.0f, MFD_ANCHOR_RIGHT);
    }
    fuel_label_right(ui, 538.2f, 547.5f, "TOW");
    fuel_weight(f, A380X_FMS_F_TOW, t, sizeof(t), NULL);
    fuel_group(ui, 660.6f, MFD_ANCHOR_CENTER, 545.0f + dy, NULL, t, c, f->unit);
    fuel_label_right(ui, 538.2f, 596.5f, "LW");
    fuel_weight(f, A380X_FMS_F_LW, t, sizeof(t), NULL);
    fuel_group(ui, 660.6f, MFD_ANCHOR_CENTER, 594.0f + dy, NULL, t, c, f->unit);
}

/* Section D: DEST / ALTN grid (x 0..480) and MIN FUEL AT DEST / EXTRA (x 480..768, centre 624). */
static void fuel_bottom(const FuelCtx *f, int act)
{
    MfdUi *ui = f->ctx->ui;
    A380XFms *fms = f->fms;
    MfdColor c = fuel_static_colour(f), ic = act ? MFD_COL_GREEN : MFD_COL_WHITE, ec;
    char t[32], dest[16], altn[16], tm[16];
    float total, x0, gw;
    int pair;
    A380XFmsValue v;
    const float dy = FUEL_DY;
    static const float cc[4] = {48.05f, 149.15f, 262.75f, 401.65f}; /* column centres */

    /* idents: FBW destIcaoDisplay / altnIcao ("NONE" without them) */
    pair = a380x_fms_get_text(fms, f->plan, A380X_FMS_T_CPNY_RTE, t, sizeof(t)); /* "NONE" iff FROM and TO */
    if (!pair || !a380x_fms_get_text(fms, f->plan, A380X_FMS_T_TO, dest, sizeof(dest))) snprintf(dest, sizeof(dest), "NONE");
    if (!pair || !a380x_fms_get_text(fms, f->plan, A380X_FMS_T_ALTN, altn, sizeof(altn))) snprintf(altn, sizeof(altn), "NONE");

    /* header */
    mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_CENTER, cc[2], 687.0f + dy,
              a380x_fms_phase(fms) == A380X_FMS_PHASE_PREFLIGHT ? "TIME" : "UTC");
    mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_CENTER, cc[3], 687.0f + dy, "EFOB");
    mfd_hrule(ui, 0.0f, 480.0f, 717.0f + dy, 1.0f, MFD_COL_LIGHT_GREY);

    /* DEST row */
    mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_CENTER, cc[0], 742.5f + dy, "DEST");
    mfd_label(ui, MFD_TXT_LABEL_BIGGER, ic, MFD_ANCHOR_CENTER, cc[1], 740.5f + dy, dest);
    v = a380x_fms_get(fms, f->plan, A380X_FMS_F_DEST_ETA);
    a380x_mfd_fms_hhmm_text(act && v.has, v.v, a380x_fms_phase(fms) != A380X_FMS_PHASE_PREFLIGHT, tm, sizeof(tm));
    mfd_label(ui, MFD_TXT_LABEL_BIGGER, ic, MFD_ANCHOR_CENTER, cc[2], 740.5f + dy, tm);
    fuel_weight(f, A380X_FMS_F_DEST_EFOB, t, sizeof(t), &ec);
    fuel_group(ui, cc[3], MFD_ANCHOR_CENTER, 740.0f + dy, NULL, t, ec, f->unit);

    /* ALTN row */
    mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_CENTER, cc[0], 796.5f + dy, "ALTN");
    mfd_label(ui, MFD_TXT_LABEL_BIGGER, ic, MFD_ANCHOR_CENTER, cc[1], 794.5f + dy, altn);
    mfd_label(ui, MFD_TXT_LABEL_BIGGER, ic, MFD_ANCHOR_CENTER, cc[2], 794.5f + dy, "--:--"); /* [PORT] ALTN ETA */
    fuel_weight(f, A380X_FMS_F_ALTN_EFOB, t, sizeof(t), NULL);
    fuel_group(ui, cc[3], MFD_ANCHOR_CENTER, 789.0f + dy, NULL, t, c, f->unit);

    /* MIN FUEL AT DEST */
    mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_CENTER, 624.0f, 687.0f + dy, "MIN FUEL AT DEST");
    fuel_field(f, 30, A380X_FMS_F_MIN_DEST_FOB, 549.0f, 727.0f, 150.0f, MFD_ANCHOR_RIGHT);

    /* EXTRA: [value unit] (7 px padding, 20 px right margin) + time, centred on 624 */
    mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_CENTER, 624.0f, 794.0f + dy, "EXTRA");
    fuel_weight(f, A380X_FMS_F_EXTRA, t, sizeof(t), NULL);
    v = a380x_fms_get(fms, f->plan, A380X_FMS_F_EXTRA_TIME);
    a380x_mfd_fms_hhmm_text(v.has, v.v, 0, tm, sizeof(tm));
    gw = mfd_text_width(t, 25.0f) + FUEL_UNIT_GAP + mfd_text_width(f->unit, 20.0f) + 14.0f;
    total = gw + 20.0f + mfd_text_width(tm, 25.0f);
    x0 = 624.0f - total * 0.5f;
    fuel_group(ui, x0 + 7.0f, MFD_ANCHOR_LEFT, 826.0f + dy, NULL, t, c, f->unit);
    mfd_label(ui, MFD_TXT_VALUE, c, MFD_ANCHOR_LEFT, x0 + gw + 20.0f, 826.0f + dy, tm);
}

static void fuel_page(MfdPageContext *ctx)
{
    MfdFuelState *st = MFD_PAGE_STATE(ctx, MfdFuelState);
    FuelCtx f;
    int uri_plan, idx;

    if (!ctx->fms) return;
    uri_plan = ctx->plan < 0 ? A380X_FMS_PLAN_ACTIVE : ctx->plan;
    memset(&f, 0, sizeof(f));
    f.ctx = ctx;
    f.fms = ctx->fms;
    f.plan = a380x_fms_loaded_plan(ctx->fms, uri_plan);
    st->plan = f.plan;
    if (f.plan < 0) {
        /* SECn deleted (or never created): FmsPage navigates to the ACTIVE equivalent (spec 03 1.3). */
        mfd_navigate(ctx, "fms/active/fuel-load");
        return;
    }
    idx = f.plan;
    f.metric = a380x_mfd_fms_metric(ctx);
    f.unit = f.metric ? "T" : "KLB";

    fuel_top_line(&f);
    fuel_block_lines(&f);
    fuel_middle(&f);
    fuel_bottom(&f, idx == A380X_FMS_PLAN_ACTIVE || idx == A380X_FMS_PLAN_TMPY);

    if (mfd_button(ctx->ui, FUEL_ID(40), mfd_rect(0.0f, 910.0f, 145.0f, 40.0f), "RETURN", 0)) mfd_navigate_back(ctx);
}

void a380x_mfd_page_fuel_register(void)
{
    mfd_register_page("fms/*/fuel-load", MFD_PAGE_ID_FUEL_LOAD, "FUEL&LOAD", fuel_page,
                      MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY | MFD_PAGE_TMPY | MFD_PAGE_PENALTY);
}

#undef FUEL_ID
#undef FUEL_DY
#undef FUEL_SPACING
#undef FUEL_UNIT_GAP
