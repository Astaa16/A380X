/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD SURV/CONTROLS and SURV/STATUS & SWITCHING. Translation of the pinned FBW
 * pages/SURV/MfdSurvControls.tsx / MfdSurvStatusSwitching.tsx (+ .scss), spec 06 sections 4-5.
 *
 * Geometry: FBW constants under border-box (SurvButton 100x65, radio rows 34 px, InputField 37 px,
 * SurvStatusButton 90x60). Under border-box the natural FBW layout fits the body (spec 06 open
 * question 1), except that the TAWS section (fixed height 205 from y 750) ends 5 px below the body:
 * its right border is clipped at the footer line, nothing else moves.
 */
#include "a380x_mfd_page_surv.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CID(n) MFD_ID(MFD_PAGE_ID_SURV_CONTROLS, (n))
#define SID(n) MFD_ID(MFD_PAGE_ID_SURV_STATUS, (n))

typedef struct {
    int xpdr_code, xpdr_mode, xpdr_failed, tcas_fault, on_ground;
    int xpdr_auto, alt_rptg, tcas_level, tcas_alt, terr_off, gpws_off, gs_off, flap_off, wxr_taws_sys, xpdr_tcas_sys;
    int terr_failed[2], gpws_failed[2];
} SurvData;

static double surv_rd(const MfdPageContext *ctx, unsigned int key)
{
    if (!ctx->host || !ctx->host->read_value) return NAN;
    return ctx->host->read_value(ctx->host->ctx, ctx->side, key);
}

static int surv_int(const MfdPageContext *ctx, unsigned int key, int dflt)
{
    double v = surv_rd(ctx, key);
    return isnan(v) ? dflt : (int)lround(v);
}

static void surv_cmd(const MfdPageContext *ctx, unsigned int cmd, double value)
{
    if (ctx->host && ctx->host->command) ctx->host->command(ctx->host->ctx, ctx->side, cmd, value);
}

static void surv_read(const MfdPageContext *ctx, SurvData *d)
{
    d->xpdr_code = surv_int(ctx, A380X_SURV_K_XPDR_CODE, -1);
    d->xpdr_mode = surv_int(ctx, A380X_SURV_K_XPDR_MODE, 0);
    d->xpdr_failed = surv_int(ctx, A380X_SURV_K_XPDR_FAILED, 0) != 0;
    d->tcas_fault = surv_int(ctx, A380X_SURV_K_TCAS_FAULT, 1) != 0; /* L:A32NX_TCAS_FAULT initial true */
    d->on_ground = surv_int(ctx, A380X_SURV_K_ON_GROUND, 1) != 0;
    d->xpdr_auto = surv_int(ctx, A380X_SURV_XPDR_AUTO, 1) != 0;
    d->alt_rptg = surv_int(ctx, A380X_SURV_ALT_RPTG, 1) != 0;
    d->tcas_level = surv_int(ctx, A380X_SURV_TCAS_ALERT_LEVEL, 2);
    d->tcas_alt = surv_int(ctx, A380X_SURV_TCAS_ALT_SELECT, 0);
    d->terr_off = surv_int(ctx, A380X_SURV_TERR_SYS_OFF, 0) != 0;
    d->gpws_off = surv_int(ctx, A380X_SURV_GPWS_SYS_OFF, 0) != 0;
    d->gs_off = surv_int(ctx, A380X_SURV_GS_MODE_OFF, 0) != 0;
    d->flap_off = surv_int(ctx, A380X_SURV_FLAP_MODE_OFF, 0) != 0;
    d->wxr_taws_sys = surv_int(ctx, A380X_SURV_WXR_TAWS_SYS, 1);
    d->xpdr_tcas_sys = surv_int(ctx, A380X_SURV_XPDR_TCAS_SYS, 1);
    d->terr_failed[0] = surv_int(ctx, A380X_SURV_K_TERR1_FAILED, 0) != 0;
    d->terr_failed[1] = surv_int(ctx, A380X_SURV_K_TERR2_FAILED, 0) != 0;
    d->gpws_failed[0] = surv_int(ctx, A380X_SURV_K_GPWS1_FAILED, 0) != 0;
    d->gpws_failed[1] = surv_int(ctx, A380X_SURV_K_GPWS2_FAILED, 0) != 0;
    if (d->tcas_level < 0 || d->tcas_level > 2) d->tcas_level = 2;
    if (d->tcas_alt < 0 || d->tcas_alt > 2) d->tcas_alt = 0;
}

/* XPDR in Mode A/C/S (Transponder.ts: OFF/STBY otherwise). X-Plane modes: 2 A, 3 C, 5 GND (S), 6, 7. */
static int surv_mode_acs(int mode)
{
    return mode == 2 || mode == 3 || mode == 5 || mode == 6 || mode == 7;
}

static void surv_heading(MfdUi *ui, float x, float baseline, const char *text, int failed)
{
    mfd_draw_text(ui->dl, MFD_FONT_ECAM, 28.0f, failed ? MFD_COL_AMBER : MFD_COL_WHITE, MFD_ANCHOR_LEFT, x, baseline,
                  text);
}

/* ======================================================================================== */
/* SURV/CONTROLS (spec 06 section 4)                                                        */
/* ======================================================================================== */

static void surv_squawk(MfdPageContext *ctx, const SurvData *d)
{
    MfdUi *ui = ctx->ui;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    MfdMsgQueue *q = ctx->msgs;
    unsigned int v0 = q ? q->version : 0u;
    MfdFieldResult fr;

    mfd_field_desc_init(&fd, MFD_FMT_SQUAWK);
    fd.width = 100.0f;
    fd.fmt.err_details = 0;
    memset(&fv, 0, sizeof(fv));
    fv.has_value = d->xpdr_code >= 0;
    fv.num = (double)d->xpdr_code;
    fr = mfd_input_field(ui, CID(1), 30.3f, 220.0f, &fd, &fv, &out);
    if (fr == MFD_FIELD_COMMITTED) surv_cmd(ctx, A380X_SURV_C_XPDR_CODE, out.num); /* [PORT] Q15: 0000 accepted */
    /* FBW errorHandler: a FormatError becomes SQWK CODE NOT VALID (the UI queued FORMAT ERROR). */
    if (q && q->version != v0 && q->count > 0 && q->e[0].id == MFD_MSG_FORMAT_ERROR && !q->e[0].cleared) {
        char text[128];
        snprintf(text, sizeof(text), "%s", q->e[0].text);
        mfd_msg_remove_text(q, text);
        mfd_msg_add(q, MFD_MSG_SQWK_CODE_NOT_VALID, NULL, NULL, NULL, NULL, NULL);
    }
}

static void surv_default_settings(MfdPageContext *ctx, const SurvData *d, int terr_failed, int gpws_failed)
{
    if (!d->xpdr_failed) {
        surv_cmd(ctx, A380X_SURV_XPDR_AUTO, 1.0);
        surv_cmd(ctx, A380X_SURV_ALT_RPTG, 1.0);
    }
    if (!d->tcas_fault) {
        surv_cmd(ctx, A380X_SURV_TCAS_ALERT_LEVEL, 2.0);
        surv_cmd(ctx, A380X_SURV_TCAS_ALT_SELECT, 0.0);
    }
    /* WXR failed in the port (no weather radar image): WXR defaults never applied [FBW]. */
    if (!terr_failed) surv_cmd(ctx, A380X_SURV_TERR_SYS_OFF, 0.0);
    if (!gpws_failed) {
        surv_cmd(ctx, A380X_SURV_GPWS_SYS_OFF, 0.0);
        surv_cmd(ctx, A380X_SURV_GS_MODE_OFF, 0.0);
        surv_cmd(ctx, A380X_SURV_FLAP_MODE_OFF, 0.0);
    }
}

static void surv_controls_page(MfdPageContext *ctx)
{
    MfdUi *ui = ctx->ui;
    MfdDrawList *dl = ctx->dl;
    SurvData d;
    int k, alt_shown, alt_disabled, tcas_disabled, sys, terr_failed, gpws_failed;
    uint32_t mask;
    static const char *const auto_stby[2] = {"AUTO", "STBY"};
    static const char *const tara[3] = {"TA/RA", "TA ONLY", "STBY"};
    static const char *const nab[3] = {"NORM", "ABV", "BLW"};
    static const char *const elevn[3] = {"AUTO", "ELEVN", "TILT"};
    static const char *const wxr_lab[6] = {"WXR", "PRED W/S", "TURB", "GAIN", "MODE", "WX ON VD"};
    static const char *const wxr_true[6] = {"AUTO", "AUTO", "AUTO", "AUTO", "WX", "AUTO"};
    static const char *const wxr_false[6] = {"OFF", "OFF", "OFF", "MAN", "MAP", "OFF"};
    static const int wxr_state[6] = {0, 0, 0, 1, 1, 0}; /* FBW initial WXR states */
    static const float wxr_cx[3] = {358.88f, 499.04f, 639.36f};
    static const char *const taws_lab[4] = {"TERR SYS", "GPWS", "G/S MODE", "FLAP MODE"};
    static const float taws_cx[4] = {130.36f, 300.52f, 420.68f, 547.27f};

    (void)MFD_PAGE_STATE(ctx, MfdSurvControlsState);
    surv_read(ctx, &d);
    sys = d.wxr_taws_sys;
    terr_failed = sys < 1 || sys > 2 || d.terr_failed[sys - 1];
    gpws_failed = sys < 1 || sys > 2 || d.gpws_failed[sys - 1];

    /* ---- Section 1: XPDR (5..308.2 after the flex shrink of 40 % + 60 % + margins) ---- */
    surv_heading(ui, 145.2f, 163.4f, "XPDR", d.xpdr_failed);
    mfd_text(ui, 80.3f, 215.2f, MFD_ANCHOR_CENTER, 24.0f, MFD_COL_WHITE, "SQWK");
    surv_squawk(ctx, &d);
    if (mfd_button(ui, CID(2), mfd_rect(30.3f, 262.0f, 100.0f, 40.0f), "IDENT", 0))
        surv_cmd(ctx, A380X_SURV_C_XPDR_IDENT, 1.0);
    mfd_text(ui, 80.3f, 341.2f, MFD_ANCHOR_CENTER, 24.0f, MFD_COL_WHITE, "ALT RPTG");
    alt_shown = d.on_ground ? d.alt_rptg : (d.xpdr_mode == 3 || d.xpdr_mode == 5 || d.xpdr_mode == 6 || d.xpdr_mode == 7);
    alt_disabled = d.xpdr_failed || !surv_mode_acs(d.xpdr_mode);
    if (mfd_surv_button(ui, CID(3), 30.3f, 351.0f, "ON", "OFF", alt_shown, alt_disabled))
        surv_cmd(ctx, A380X_SURV_ALT_RPTG, alt_shown ? 0.0 : 1.0);
    {
        int idx = surv_mode_acs(d.xpdr_mode) ? 0 : 1;
        k = mfd_radio_group(ui, CID(4), 163.8f, 198.0f, 114.2f, auto_stby, 2, idx, 0u,
                            idx == 0 ? MFD_COL_GREEN : MFD_COL_WHITE, 50.0f);
        if (k >= 0) surv_cmd(ctx, A380X_SURV_XPDR_AUTO, k == 0 ? 1.0 : 0.0);
    }
    mfd_draw_fill_rect(dl, mfd_rect(306.2f, 138.0f, 2.0f, 288.0f), MFD_COL_LIGHT_GREY);

    /* ---- TCAS (313.2..768, padding-top 75) ---- */
    surv_heading(ui, 463.2f, 165.4f, "TCAS", d.tcas_fault);
    tcas_disabled = d.tcas_fault || d.xpdr_mode == 0 || d.xpdr_mode == 1;
    mask = tcas_disabled ? 0x7u : 0u;
    k = mfd_radio_group(ui, CID(5), 383.4f, 213.0f, 163.1f, tara, 3, 2 - d.tcas_level, mask,
                        (2 - d.tcas_level) == 0 ? MFD_COL_GREEN : MFD_COL_WHITE, 10.0f);
    if (k >= 0) surv_cmd(ctx, A380X_SURV_TCAS_ALERT_LEVEL, (double)(2 - k));
    mfd_draw_fill_rect(dl, mfd_rect(586.5f, 213.0f, 2.0f, 132.0f), MFD_COL_LIGHT_GREY);
    k = mfd_radio_group(ui, CID(6), 593.5f, 213.0f, 114.2f, nab, 3, d.tcas_alt, mask,
                        d.tcas_alt == 0 ? MFD_COL_GREEN : MFD_COL_CYAN, 10.0f);
    if (k >= 0) surv_cmd(ctx, A380X_SURV_TCAS_ALT_SELECT, (double)k);
    mfd_draw_fill_rect(dl, mfd_rect(766.0f, 138.0f, 2.0f, 207.0f), MFD_COL_LIGHT_GREY);
    mfd_draw_fill_rect(dl, mfd_rect(0.0f, 426.0f, 768.0f, 2.0f), MFD_COL_LIGHT_GREY);

    /* ---- Section 2: WXR (438..733), failed in the port [FBW wxrFailed = true] ---- */
    surv_heading(ui, 188.9f, 460.4f, "WXR", 1);
    mfd_text(ui, 39.95f, 565.2f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "ELEVN/TILT");
    mfd_radio_group(ui, CID(7), 49.95f, 570.0f, 130.5f, elevn, 3, 0, 0x7u, MFD_COL_GREEN, 10.0f);
    for (k = 0; k < 6; ++k) {
        float cx = wxr_cx[k % 3], top = k < 3 ? 488.0f : 613.0f;
        mfd_text(ui, cx, top + 16.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, wxr_lab[k]);
        mfd_surv_button(ui, CID(10 + k), cx - 50.0f, top + 20.0f, wxr_true[k], wxr_false[k], wxr_state[k], 1);
    }

    /* ---- Section 3: TAWS (border-top 748..750, content 750..955) ---- */
    mfd_draw_fill_rect(dl, mfd_rect(0.0f, 748.0f, 768.0f, 2.0f), MFD_COL_LIGHT_GREY);
    surv_heading(ui, 100.0f, 777.4f, "TAWS", terr_failed && gpws_failed);
    for (k = 0; k < 4; ++k) {
        int state = k == 0 ? !d.terr_off : k == 1 ? !d.gpws_off : k == 2 ? !d.gs_off : !d.flap_off;
        int disabled = k == 0 ? terr_failed : gpws_failed;
        static const unsigned int keys[4] = {A380X_SURV_TERR_SYS_OFF, A380X_SURV_GPWS_SYS_OFF, A380X_SURV_GS_MODE_OFF,
                                             A380X_SURV_FLAP_MODE_OFF};
        mfd_text(ui, taws_cx[k], 846.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, taws_lab[k]);
        if (mfd_surv_button(ui, CID(20 + k), taws_cx[k] - 50.0f, 850.0f, "ON", "OFF", state, disabled))
            surv_cmd(ctx, keys[k], state ? 1.0 : 0.0); /* new state = !state -> *_off = state */
    }
    mfd_draw_fill_rect(dl, mfd_rect(613.7f, 750.0f, 2.0f, 200.0f), MFD_COL_LIGHT_GREY); /* clipped at 950 */
    mfd_text(ui, 695.7f, 846.5f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "SURV");
    if (mfd_button(ui, CID(30), mfd_rect(625.7f, 850.5f, 140.0f, 62.0f), "DEFAULT\nSETTINGS", 0))
        surv_default_settings(ctx, &d, terr_failed, gpws_failed);
}

/* ======================================================================================== */
/* SURV/STATUS & SWITCHING (spec 06 section 5)                                              */
/* ======================================================================================== */

static void surv_group(MfdDrawList *dl, MfdRect r, int active)
{
    mfd_draw_stroke_rect(dl, r, 1.0f, active ? MFD_COL_GREEN : MFD_COL_LIGHT_GREY);
}

static void surv_status_page(MfdPageContext *ctx)
{
    MfdUi *ui = ctx->ui;
    MfdDrawList *dl = ctx->dl;
    SurvData d;
    int n;
    static const char *const sysname[2] = {"1", "2"};

    surv_read(ctx, &d);
    for (n = 0; n < 2; ++n) {
        float bx = n == 0 ? 50.0f : 448.0f, ix = bx + 17.0f;
        int act = d.wxr_taws_sys == n + 1, lact = d.xpdr_tcas_sys == n + 1;
        /* upper box: border-box 270 wide, padding 50/10/10/10 */
        mfd_draw_stroke_rect(dl, mfd_rect(bx, 188.0f, 270.0f, 246.0f), 1.0f, MFD_COL_LIGHT_GREY);
        surv_group(dl, mfd_rect(bx + 11.0f, 239.0f, 248.0f, 104.0f), act);
        mfd_surv_status_item(ui, ix, 245.0f, "WXR DISPLAY", sysname[n], act, 1); /* [PORT] Q14 */
        mfd_surv_status_item(ui, ix, 279.0f, "TURB", sysname[n], act, 1);
        mfd_surv_status_item(ui, ix, 313.0f, "PRED W/S", sysname[n], act, 1);
        surv_group(dl, mfd_rect(bx + 11.0f, 353.0f, 248.0f, 70.0f), act);
        mfd_surv_status_item(ui, ix, 359.0f, "TERR SYS", sysname[n], act, d.terr_failed[n]);
        mfd_surv_status_item(ui, ix, 393.0f, "GPWS", sysname[n], act, d.gpws_failed[n]);
        if (mfd_surv_status_button(ui, SID(1 + n), mfd_rect(bx + 90.0f, 153.0f, 90.0f, 60.0f), n == 0 ? "SYS 1" : "SYS 2",
                                   act))
            surv_cmd(ctx, A380X_SURV_WXR_TAWS_SYS, act ? 0.0 : (double)(n + 1));

        /* lower box */
        mfd_draw_stroke_rect(dl, mfd_rect(bx, 534.0f, 270.0f, 139.0f), 1.0f, MFD_COL_LIGHT_GREY);
        surv_group(dl, mfd_rect(bx + 11.0f, 585.0f, 248.0f, 36.0f), lact);
        mfd_surv_status_item(ui, ix, 591.0f, "XPDR", sysname[n], lact, d.xpdr_failed);
        surv_group(dl, mfd_rect(bx + 11.0f, 626.0f, 248.0f, 36.0f), lact);
        mfd_surv_status_item(ui, ix, 632.0f, "TCAS", sysname[n], lact, d.tcas_fault);
        if (mfd_surv_status_button(ui, SID(3 + n), mfd_rect(bx + 90.0f, 499.0f, 90.0f, 60.0f), n == 0 ? "SYS 1" : "SYS 2",
                                   lact))
            surv_cmd(ctx, A380X_SURV_XPDR_TCAS_SYS, (double)(n + 1)); /* [PORT] (FBW: no action) */
    }
    mfd_text(ui, 384.0f, 262.2f, MFD_ANCHOR_CENTER, 24.0f, MFD_COL_WHITE, "WXR");
    mfd_text(ui, 384.0f, 376.2f, MFD_ANCHOR_CENTER, 24.0f, MFD_COL_WHITE, "TAWS");
    mfd_text(ui, 384.0f, 608.2f, MFD_ANCHOR_CENTER, 24.0f, MFD_COL_WHITE, "XPDR");
    mfd_text(ui, 384.0f, 652.2f, MFD_ANCHOR_CENTER, 24.0f, MFD_COL_WHITE, "TCAS");
}

void a380x_mfd_page_surv_register(void)
{
    mfd_register_page("surv/controls", MFD_PAGE_ID_SURV_CONTROLS, "CONTROLS", surv_controls_page, 0u);
    mfd_register_page("surv/status-switching", MFD_PAGE_ID_SURV_STATUS, "STATUS & SWITCHING", surv_status_page, 0u);
}
