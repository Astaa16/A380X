/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD ACTIVE/INIT and SECn/INIT. Translation of the pinned FBW pages/FMS/MfdFmsInit.tsx and
 * MfdFmsInit.scss (docs/mfd-spec/03-fms-init-fuel-data-sec-msg.md section 3), laid out with
 * border-box sizing on the spec 01 frame: the spec 03 boxes (page top 134) are re-based +4 px on
 * the 138 px body top. Also the FMS-binding helpers shared with FUEL&LOAD and SEC INDEX.
 */
#include "a380x_mfd_page_init.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define INIT_ID(n) MFD_ID(MFD_PAGE_ID_INIT, (n))
#define INIT_DY 4.0f /* spec 03 page top 134 -> frame body top 138 */

/* ======================================================================================== */
/* Shared helpers                                                                           */
/* ======================================================================================== */

int a380x_mfd_fms_metric(const MfdPageContext *ctx)
{
    if (ctx->fms) return ctx->fms->cfg.metric_units ? 1 : 0;
    return ctx->in ? (ctx->in->metric_units ? 1 : 0) : 1;
}

MfdColor a380x_mfd_fms_value_colour(const A380XFms *fms, int plan, const A380XFmsValue *v)
{
    (void)fms;
    if (v && v->colour == A380X_FMS_COLOUR_AMBER) return MFD_COL_AMBER;
    if (plan >= A380X_FMS_PLAN_SEC1) return MFD_COL_WHITE;
    if (v && v->colour == A380X_FMS_COLOUR_WHITE) return MFD_COL_WHITE;
    return MFD_COL_GREEN;
}

void a380x_mfd_fms_weight_text(int has, double kg, int metric, char *buf, size_t n)
{
    MfdFieldText ft;
    mfd_fmt_render_weight(has && !isnan(kg), kg, metric, &ft);
    snprintf(buf, n, "%s", ft.value);
}

void a380x_mfd_fms_hhmm_text(int has, double seconds, int clock, char *buf, size_t n)
{
    long mins;
    if (!has || isnan(seconds) || isinf(seconds)) {
        snprintf(buf, n, "--:--");
        return;
    }
    mins = (long)floor(seconds / 60.0);
    if (mins < 0) mins = 0;
    if (clock) mins %= 24L * 60L;
    snprintf(buf, n, "%02ld:%02ld", (mins / 60) % 100, mins % 60);
}

static uint32_t init_field_flags(unsigned fl)
{
    uint32_t f = 0;
    if (fl & A380X_FMS_FF_DISABLED) f |= MFD_FIELD_DISABLED;
    if (fl & A380X_FMS_FF_INACTIVE) f |= MFD_FIELD_INACTIVE;
    if (fl & A380X_FMS_FF_MANDATORY) f |= MFD_FIELD_MANDATORY;
    if (!(fl & A380X_FMS_FF_CLEARABLE)) f |= MFD_FIELD_NOT_CLEARABLE;
    if (fl & A380X_FMS_FF_TMPY) f |= MFD_FIELD_TMPY;
    return f;
}

static void init_disabled_field(MfdUi *ui, MfdId id, MfdFmtKind kind, float x, float y, float width,
                                MfdAnchor align)
{
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    mfd_field_desc_init(&fd, kind);
    fd.flags = MFD_FIELD_DISABLED | MFD_FIELD_NOT_CLEARABLE;
    fd.width = width;
    fd.align = align;
    memset(&fv, 0, sizeof(fv));
    (void)mfd_input_field(ui, id, x, y, &fd, &fv, &out);
}

MfdFieldResult a380x_mfd_fms_field(MfdPageContext *ctx, MfdId id, int plan, A380XFmsField f,
                                   A380XFmsPage page, float x, float y, float width, MfdAnchor align)
{
    A380XFms *fms = ctx->fms;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    MfdFieldResult r;
    A380XFmsValue v;
    unsigned fl;

    if (!fms) return MFD_FIELD_NONE;
    fl = a380x_fms_field_flags(fms, plan, f, page);
    if (fl & A380X_FMS_FF_HIDDEN) return MFD_FIELD_NONE;
    mfd_field_desc_init(&fd, MFD_FMT_SPEED_KT);
    a380x_fms_field_fmt(fms, plan, f, &fd.fmt);
    if (fd.fmt.kind == MFD_FMT_COUNT) return MFD_FIELD_NONE; /* read-only item: not a field */
    fd.flags = init_field_flags(fl);
    if (fl & A380X_FMS_FF_READONLY) fd.flags |= MFD_FIELD_DISABLED;
    fd.width = width;
    fd.align = align;
    v = a380x_fms_get(fms, plan, f);
    memset(&fv, 0, sizeof(fv));
    fv.has_value = v.has && !(fl & A380X_FMS_FF_NA);
    fv.is_computed = fv.has_value && !v.entered;
    fv.num = v.v;
    r = mfd_input_field(ctx->ui, id, x, y, &fd, &fv, &out);
    if (r != MFD_FIELD_NONE) {
        MfdFmsError err;
        int ok;
        memset(&err, 0, sizeof(err));
        if (r == MFD_FIELD_COMMITTED && out.has) ok = a380x_fms_enter(fms, plan, f, out.num, &err);
        else ok = a380x_fms_clear(fms, plan, f, &err);
        if (!ok && err.type != MFD_ERR_NONE) mfd_ui_error(ctx->ui, &err, fd.fmt.err_details);
    }
    return r;
}

MfdFieldResult a380x_mfd_fms_text_field(MfdPageContext *ctx, MfdId id, int plan, A380XFmsText t,
                                        A380XFmsPage page, MfdFmtKind kind, float x, float y, float width,
                                        MfdAnchor align)
{
    A380XFms *fms = ctx->fms;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    MfdFieldResult r;
    char text[32];
    unsigned fl;

    if (!fms) {
        init_disabled_field(ctx->ui, id, kind, x, y, width, align);
        return MFD_FIELD_NONE;
    }
    fl = a380x_fms_text_flags(fms, plan, t, page);
    if (fl & A380X_FMS_FF_HIDDEN) return MFD_FIELD_NONE;
    mfd_field_desc_init(&fd, kind);
    fd.flags = init_field_flags(fl);
    fd.width = width;
    fd.align = align;
    memset(&fv, 0, sizeof(fv));
    fv.has_value = a380x_fms_get_text(fms, plan, t, text, sizeof(text));
    fv.text = fv.has_value ? text : NULL;
    r = mfd_input_field(ctx->ui, id, x, y, &fd, &fv, &out);
    if (r != MFD_FIELD_NONE) {
        MfdFmsError err;
        memset(&err, 0, sizeof(err));
        if (!a380x_fms_enter_text(fms, plan, t, (r == MFD_FIELD_COMMITTED && out.has) ? out.str : "", &err) &&
            err.type != MFD_ERR_NONE)
            mfd_ui_error(ctx->ui, &err, 1);
    }
    return r;
}

void a380x_mfd_fms_ci_mode_dropdown(MfdPageContext *ctx, MfdId id, int plan, A380XFmsPage page, MfdRect r)
{
    static const char *const init_modes[] = {"LRC", "ECON"};
    static const char *const init_modes_eo[] = {"EO-LRC", "EO-ECON"};
    A380XFms *fms = ctx->fms;
    MfdDropdownDesc dd;
    A380XFmsValue v;
    unsigned fl;
    int k = -1;
    char txt[8];

    if (!fms) return;
    fl = a380x_fms_field_flags(fms, plan, A380X_FMS_F_CI_MODE, page);
    if (fl & A380X_FMS_FF_HIDDEN) return;
    v = a380x_fms_get(fms, plan, A380X_FMS_F_CI_MODE);
    memset(&dd, 0, sizeof(dd));
    dd.values = a380x_fms_engine_out(fms) ? init_modes_eo : init_modes;
    dd.count = 2;
    dd.selected = (v.has && v.v >= 0.0 && v.v <= 1.0) ? (int)v.v : -1;
    dd.disabled = (fl & A380X_FMS_FF_DISABLED) != 0;
    dd.inactive = (fl & A380X_FMS_FF_INACTIVE) != 0;
    dd.tmpy = (fl & A380X_FMS_FF_TMPY) != 0;
    dd.digits = 7; /* numberOfDigitsForInputField */
    if (mfd_dropdown(ctx->ui, id, r, &dd, &k, txt, sizeof(txt)) && k >= 0) {
        MfdFmsError err;
        memset(&err, 0, sizeof(err));
        if (!a380x_fms_enter(fms, plan, A380X_FMS_F_CI_MODE, (double)k, &err) && err.type != MFD_ERR_NONE)
            mfd_ui_error(ctx->ui, &err, 1);
    }
}

/* ======================================================================================== */
/* CPNY F-PLN REQUEST (shared/CpnyFplnButtonUtils.tsx)                                      */
/* ======================================================================================== */

static A380XMfdCpnyFplnHooks init_cpny;

void a380x_mfd_cpny_fpln_set_hooks(const A380XMfdCpnyFplnHooks *hooks)
{
    if (hooks) init_cpny = *hooks;
    else memset(&init_cpny, 0, sizeof(init_cpny));
}

void a380x_mfd_cpny_fpln_button(MfdPageContext *ctx, MfdId id, MfdRect r, int plan)
{
    static const MfdMenuItem init_cpny_menu[] = {{"INSERT*", 0}, {"CLEAR*", 0}};
    A380XFms *fms = ctx->fms;
    MfdButtonDesc bd;
    int avail = init_cpny.available ? init_cpny.available(init_cpny.ctx) : 0;
    int busy = init_cpny.in_progress ? init_cpny.in_progress(init_cpny.ctx) : 0;
    int req_for = init_cpny.requested_for ? init_cpny.requested_for(init_cpny.ctx) : -1;
    int at = plan == A380X_FMS_PLAN_ACTIVE || plan == A380X_FMS_PLAN_TMPY;
    int disabled, k;

    disabled = busy || (at && (!fms || a380x_fms_engines_were_started(fms) || a380x_fms_has_active_flight_plan(fms) ||
                               req_for >= A380X_FMS_PLAN_SEC1));
    mfd_button_desc_init(&bd, busy ? "REQUEST\nPENDING..." : avail ? "RECEIVED\nCPNY F-PLN" : "CPNY F-PLN\nREQUEST");
    bd.flags = MFD_BTN_NO_ARROW | (disabled ? MFD_BTN_DISABLED : 0u);
    if (avail && !busy) {
        bd.menu = init_cpny_menu;
        bd.menu_count = 2;
    }
    k = mfd_button_ex(ctx->ui, id, r, &bd);
    if (k == MFD_BUTTON_CLICKED && !avail) {
        if (init_cpny.request) {
            init_cpny.request(init_cpny.ctx, plan);
        } else if (ctx->msgs) {
            /* [PORT] no datalink in the port: the request cannot be sent. */
            mfd_msg_add(ctx->msgs, MFD_MSG_COM_DATALINK_NOT_AVAIL, NULL, NULL, NULL, NULL, NULL);
        }
    } else if (k == 0 && init_cpny.insert) {
        init_cpny.insert(init_cpny.ctx, plan);
    } else if (k == 1 && init_cpny.clear) {
        init_cpny.clear(init_cpny.ctx);
    }
}

/* ======================================================================================== */
/* INIT page                                                                                */
/* ======================================================================================== */

/* .mfd-label.init-input-field: 150 wide, right aligned with 5 px right padding (text ends at 145). */
static void init_label(MfdUi *ui, float top, const char *s)
{
    mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_RIGHT, 145.0f, top + INIT_DY, s);
}

static int init_btn(MfdUi *ui, int n, float x, float y, float w, float h, const char *label, uint32_t flags)
{
    return mfd_button(ui, INIT_ID(n), mfd_rect(x, y + INIT_DY, w, h), label, flags);
}

static void init_page(MfdPageContext *ctx)
{
    MfdInitState *st = MFD_PAGE_STATE(ctx, MfdInitState);
    MfdUi *ui = ctx->ui;
    A380XFms *fms = ctx->fms;
    const char *cat = ctx->uri->category;
    int uri_plan, plan, idx, act, sec_page, from_to, phase;
    char from[16], to[16], uri[MFD_URI_LEN];
    const float dy = INIT_DY;

    if (!fms) return; /* FBW renders nothing until fmcService.master exists */
    uri_plan = ctx->plan < 0 ? A380X_FMS_PLAN_ACTIVE : ctx->plan;
    plan = a380x_fms_loaded_plan(fms, uri_plan);              /* -1: SECn does not exist */
    idx = uri_plan == A380X_FMS_PLAN_ACTIVE ? plan : uri_plan; /* loadedFlightPlanIndex */
    act = idx == A380X_FMS_PLAN_ACTIVE || idx == A380X_FMS_PLAN_TMPY;
    sec_page = !act;
    st->last_plan = plan;
    phase = a380x_fms_phase(fms);
    from_to = plan >= 0 && a380x_fms_get_text(fms, plan, A380X_FMS_T_FROM, from, sizeof(from)) &&
              a380x_fms_get_text(fms, plan, A380X_FMS_T_TO, to, sizeof(to));

    /* L1: FLT NBR / ACFT STATUS / CPNY F-PLN (row 139..201) */
    init_label(ui, 160.0f, "FLT NBR");
    a380x_mfd_fms_text_field(ctx, INIT_ID(1), plan, A380X_FMS_T_FLT_NBR, A380X_FMS_PAGE_INIT,
                             MFD_FMT_TXT_LONG_ALNUM, 150.0f, 151.5f + dy, 200.0f, MFD_ANCHOR_CENTER);
    if (init_btn(ui, 2, 355.0f, 150.0f, 200.0f, 40.0f, "ACFT STATUS", 0)) mfd_navigate(ctx, "fms/data/status");
    a380x_mfd_cpny_fpln_button(ctx, INIT_ID(3), mfd_rect(593.0f, 139.0f + dy, 175.0f, 62.0f), idx);

    /* L2: FROM / TO / ALTN (row 211..248) */
    init_label(ui, 219.5f, "FROM");
    a380x_mfd_fms_text_field(ctx, INIT_ID(4), plan, A380X_FMS_T_FROM, A380X_FMS_PAGE_INIT, MFD_FMT_TXT_AIRPORT,
                             150.0f, 211.0f + dy, 92.0f, MFD_ANCHOR_CENTER);
    mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_LEFT, 252.0f, 219.5f + dy, "TO");
    a380x_mfd_fms_text_field(ctx, INIT_ID(5), plan, A380X_FMS_T_TO, A380X_FMS_PAGE_INIT, MFD_FMT_TXT_AIRPORT,
                             287.0f, 211.0f + dy, 92.0f, MFD_ANCHOR_CENTER);
    mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_LEFT, 389.0f, 219.5f + dy, "ALTN");
    a380x_mfd_fms_text_field(ctx, INIT_ID(6), plan, A380X_FMS_T_ALTN, A380X_FMS_PAGE_INIT, MFD_FMT_TXT_AIRPORT,
                             449.0f, 211.0f + dy, 92.0f, MFD_ANCHOR_CENTER);

    /* L3: CPNY RTE / RTE SEL (row 263..303) [FBW-PH] disabled */
    init_label(ui, 273.0f, "CPNY RTE");
    a380x_mfd_fms_text_field(ctx, INIT_ID(7), plan, A380X_FMS_T_CPNY_RTE, A380X_FMS_PAGE_INIT,
                             MFD_FMT_TXT_LONG_ALNUM, 150.0f, 264.5f + dy, 200.0f, MFD_ANCHOR_CENTER);
    init_btn(ui, 8, 355.0f, 263.0f, 200.0f, 40.0f, "RTE SEL", MFD_BTN_DISABLED);

    /* L4: ALTN RTE / ALTN RTE SEL (row 308..374, separator at 373) */
    init_label(ui, 318.0f, "ALTN RTE");
    a380x_mfd_fms_text_field(ctx, INIT_ID(9), plan, A380X_FMS_T_ALTN_RTE, A380X_FMS_PAGE_INIT,
                             MFD_FMT_TXT_LONG_ALNUM, 150.0f, 309.5f + dy, 200.0f, MFD_ANCHOR_CENTER);
    init_btn(ui, 10, 355.0f, 308.0f, 200.0f, 40.0f, "ALTN RTE SEL", MFD_BTN_DISABLED);
    mfd_hrule(ui, 0.0f, (float)MFD_W, 373.0f + dy, 1.0f, MFD_COL_LIGHT_GREY);

    /* L5: CRZ FL / CRZ TEMP (row 404..441) */
    init_label(ui, 412.5f, "CRZ FL");
    a380x_mfd_fms_field(ctx, INIT_ID(11), plan, A380X_FMS_F_CRZ_FL, A380X_FMS_PAGE_INIT, 150.0f, 404.0f + dy, 0.0f,
                        MFD_ANCHOR_CENTER);
    mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_RIGHT, 450.0f, 412.5f + dy, "CRZ TEMP");
    a380x_mfd_fms_field(ctx, INIT_ID(12), plan, A380X_FMS_F_CRZ_TEMP, A380X_FMS_PAGE_INIT, 455.0f, 404.0f + dy,
                        110.0f, MFD_ANCHOR_CENTER);

    /* L6: MODE / TROPO (row 451..495) */
    init_label(ui, 463.0f, "MODE");
    a380x_mfd_fms_ci_mode_dropdown(ctx, INIT_ID(13), plan, A380X_FMS_PAGE_INIT,
                                   mfd_rect(150.0f, 451.0f + dy, 175.0f, MFD_DROPDOWN_H));
    mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_RIGHT, 452.5f, 463.0f + dy, "TROPO");
    a380x_mfd_fms_field(ctx, INIT_ID(14), plan, A380X_FMS_F_TROPO, A380X_FMS_PAGE_INIT, 457.5f, 454.5f + dy, 0.0f,
                        MFD_ANCHOR_RIGHT);

    /* L7: CI / TRIP WIND / WIND / CPNY WIND REQUEST (row 500..628, separator at 627) */
    init_label(ui, 515.0f, "CI");
    init_label(ui, 562.0f, "TRIP WIND");
    a380x_mfd_fms_field(ctx, INIT_ID(15), plan, A380X_FMS_F_CI, A380X_FMS_PAGE_INIT, 150.0f, 505.0f + dy, 70.0f,
                        MFD_ANCHOR_CENTER);
    a380x_mfd_fms_field(ctx, INIT_ID(16), plan, A380X_FMS_F_TRIP_WIND, A380X_FMS_PAGE_INIT, 150.0f, 552.0f + dy,
                        125.0f, MFD_ANCHOR_CENTER);
    init_btn(ui, 17, 355.0f, 552.0f, 0.0f, 40.0f, "WIND", MFD_BTN_DISABLED);
    init_btn(ui, 18, 583.0f, 515.0f, 175.0f, 62.0f, "CPNY WIND\nREQUEST", MFD_BTN_DISABLED);
    mfd_hrule(ui, 0.0f, (float)MFD_W, 627.0f + dy, 1.0f, MFD_COL_LIGHT_GREY);

    /* B1..B5: only for ACTIVE / TMPY (visibility: hidden keeps the space) */
    if (act) {
        if (init_btn(ui, 20, 150.0f, 643.0f, 160.0f, 40.0f, "IRS", 0)) mfd_navigate(ctx, "fms/position/irs");
        if (init_btn(ui, 21, 150.0f, 693.0f, 160.0f, 40.0f, "DEPARTURE",
                     (!from_to || phase != A380X_FMS_PHASE_PREFLIGHT) ? MFD_BTN_DISABLED : 0u)) {
            snprintf(uri, sizeof(uri), "fms/%s/f-pln-departure", cat);
            mfd_navigate(ctx, uri);
        }
        init_btn(ui, 22, 360.0f, 693.0f, 0.0f, 40.0f, "RTE SUMMARY", MFD_BTN_DISABLED);
        if (init_btn(ui, 23, 150.0f, 743.0f, 160.0f, 40.0f, "NAVAIDS", 0))
            mfd_navigate(ctx, "fms/position/navaids/withReturn");
        if (init_btn(ui, 24, 150.0f, 793.0f, 160.0f, 40.0f, "FUEL&LOAD", 0)) {
            snprintf(uri, sizeof(uri), "fms/%s/fuel-load", cat);
            mfd_navigate(ctx, uri);
        }
        if (init_btn(ui, 25, 150.0f, 843.0f, 160.0f, 40.0f, "T.O. PERF", 0)) {
            snprintf(uri, sizeof(uri), "fms/%s/perf/to", cat);
            mfd_navigate(ctx, uri);
        }
        init_btn(ui, 26, 583.0f, 843.0f, 175.0f, 62.0f, "CPNY T.O.\nREQUEST", MFD_BTN_DISABLED);
    }

    /* S1: RETURN (SEC only) -> SEC INDEX on tab N */
    if (sec_page && init_btn(ui, 27, 0.0f, 905.0f, 150.0f, 40.0f, "RETURN", 0)) {
        snprintf(uri, sizeof(uri), "fms/sec/index/%d", idx - 2);
        mfd_navigate(ctx, uri);
    }
}

void a380x_mfd_page_init_register(void)
{
    mfd_register_page("fms/*/init", MFD_PAGE_ID_INIT, "INIT", init_page,
                      MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY | MFD_PAGE_TMPY);
}

#undef INIT_ID
#undef INIT_DY
