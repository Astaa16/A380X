/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD FCU BKUP / AFS CP and EFIS CP ([PORT] design of spec 06 sections 10-11; FBW routes are
 * 404). Widgets are FBW MFD widgets with border-box sizes: the spec's content-box outer sizes are
 * kept where the spec gives them as outer rectangles (fields 152 px wide, buttons), the InputField
 * is 37 px tall (centred on the spec's 49 px slot) and the SurvStatusButtons are 90 x 60 (centred on
 * the spec's 94 x 64 slots).
 *
 * Window strings use the formatting of the port FCU display (a380x_fcu_render.inc draw_fcu) so the
 * MFD and the 3D FCU always agree.
 */
#include "a380x_mfd_page_fcubkup.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FID(n) MFD_ID(MFD_PAGE_ID_FCUBKUP_AFS, (n))
#define EID(n) MFD_ID(MFD_PAGE_ID_FCUBKUP_EFIS, (n))
#define DEG "\xC2\xB0"

#define FCUB_SLOTS 10 /* AFS arrows 0..7, EFIS baro arrows 8..9 */

typedef struct {
    int active;
    MfdId id;
    unsigned int cmd;
} FcubHeld;

static FcubHeld fcub_held[MFD_SIDE_COUNT][FCUB_SLOTS];

static double fcub_rd(const MfdPageContext *ctx, unsigned int key)
{
    if (!ctx->host || !ctx->host->read_value) return NAN;
    return ctx->host->read_value(ctx->host->ctx, ctx->side, key);
}

static int fcub_flag(const MfdPageContext *ctx, unsigned int key)
{
    double v = fcub_rd(ctx, key);
    return !isnan(v) && v != 0.0;
}

static void fcub_cmd(const MfdPageContext *ctx, unsigned int cmd, double value)
{
    if (ctx->host && ctx->host->command) ctx->host->command(ctx->host->ctx, ctx->side, cmd, value);
}

void a380x_mfd_page_fcubkup_release_held(A380XMfd *m)
{
    int s, k;
    if (!m) return;
    for (s = 0; s < MFD_SIDE_COUNT; ++s) {
        const A380XMfdSide *S = &m->sides[s];
        for (k = 0; k < FCUB_SLOTS; ++k) {
            FcubHeld *h = &fcub_held[s][k];
            if (!h->active) continue;
            if (strcmp(S->uri.sys, "fcubkup") == 0 && strcmp(S->uri.category, k < 8 ? "afs" : "efis") == 0 &&
                S->ui.press_id == h->id && m->in.du_on[s])
                continue;
            if (m->host.command) m->host.command(m->host.ctx, (MfdSide)s, h->cmd, A380X_MFD_ACT_END);
            h->active = 0;
        }
    }
}

/* -/+ arrow: press = ACT_BEGIN, release = ACT_END (the command handler repeats while held). */
static void fcub_arrow(MfdPageContext *ctx, int slot, MfdId id, MfdRect r, MfdIcon icon, int disabled, unsigned int cmd)
{
    FcubHeld *h = &fcub_held[ctx->side][slot];
    int pressed;
    mfd_icon_button(ctx->ui, id, r, icon, disabled);
    pressed = !disabled && ctx->ui->press_id == id;
    if (pressed && !h->active) {
        h->active = 1;
        h->id = id;
        h->cmd = cmd;
        fcub_cmd(ctx, cmd, A380X_MFD_ACT_BEGIN);
    } else if (!pressed && h->active) {
        fcub_cmd(ctx, h->cmd, A380X_MFD_ACT_END);
        h->active = 0;
    }
}

/* SurvStatusButton (90 x 60); disabled: grey label, light-grey bars, no hover and no hit. */
static int fcub_pb(MfdUi *ui, MfdId id, float x, float y, const char *label, int active, int disabled)
{
    MfdRect r = mfd_rect(x, y, 90.0f, 60.0f);
    MfdDrawList *dl = ui->dl;
    int i;
    float block_h, bar_h, gap;
    if (!disabled) return mfd_surv_status_button(ui, id, r, label, active);
    block_h = (r.h - 4.0f) * 0.5f;
    bar_h = block_h * 0.143f;
    gap = (block_h - 3.0f * bar_h) / 4.0f;
    mfd_draw_fill_rect(dl, r, MFD_COL_SURV_STATUS_BG);
    mfd_draw_bevel(dl, r, 2.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_SOLID);
    mfd_draw_fill_rect(dl, mfd_rect(r.x + 2.0f, r.y + 2.0f, r.w - 4.0f, block_h), MFD_COL_BLACK);
    for (i = 0; i < 3; ++i)
        mfd_draw_fill_rect(dl, mfd_rect(r.x + 9.0f, r.y + 2.0f + gap + (float)i * (bar_h + gap), r.w - 18.0f, bar_h),
                           MFD_COL_LIGHT_GREY);
    mfd_draw_text(dl, MFD_FONT_SZ, 22.0f, MFD_COL_DARK_GREY, MFD_ANCHOR_CENTER, r.x + r.w * 0.5f,
                  r.y + 2.0f + block_h + 3.0f + MFD_FONT_BASELINE * 22.0f, label);
    return 0;
}

/* ======================================================================================== */
/* AFS CP (spec 06 section 10)                                                              */
/* ======================================================================================== */

typedef enum { FCUB_SPD, FCUB_MACH, FCUB_HDG, FCUB_ALT, FCUB_VS, FCUB_FPA } FcubKind;

static int fcub_digits_only(const char *s, int lo, int hi)
{
    int n = 0;
    for (; *s; ++s, ++n)
        if (*s < '0' || *s > '9') return 0;
    return n >= lo && n <= hi;
}

/* Parse a typed window value (spec 06 10.2 FCU_* formats). Returns 1 with *v or 0 with *err. */
static int fcub_parse(FcubKind k, const char *in, double *v, MfdFmsError *err)
{
    const char *p = in;
    double x;
    switch (k) {
    case FCUB_SPD:
        if (!fcub_digits_only(in, 2, 3)) { mfd_err_set_format(err, "XXX", "KT"); return 0; }
        x = (double)atoi(in);
        if (x < 100.0 || x > 399.0) { mfd_err_set_range(err, "100", "399", "KT"); return 0; }
        break;
    case FCUB_MACH: {
        int nd;
        if (*p == '0') ++p;
        if (*p != '.' || !fcub_digits_only(p + 1, 1, 2)) { mfd_err_set_format(err, ".XX", ""); return 0; }
        nd = (int)strlen(p + 1);
        x = (double)atoi(p + 1) / (nd == 1 ? 10.0 : 100.0);
        if (x < 0.10 || x > 0.99) { mfd_err_set_range(err, ".10", ".99", ""); return 0; }
        break;
    }
    case FCUB_HDG:
        if (!fcub_digits_only(in, 1, 3)) { mfd_err_set_format(err, "XXX", DEG); return 0; }
        x = (double)atoi(in);
        if (x > 360.0) { mfd_err_set_range(err, "0", "360", DEG); return 0; }
        if (x >= 360.0) x = 0.0;
        break;
    case FCUB_ALT:
        if (!fcub_digits_only(in, 3, 5)) { mfd_err_set_format(err, "XXXXX", "FT"); return 0; }
        x = (double)atoi(in);
        if (x < 100.0 || x > 49000.0) { mfd_err_set_range(err, "100", "49000", "FT"); return 0; }
        break;
    case FCUB_VS: {
        int neg = 0;
        if (*p == '+' || *p == '-') neg = *p++ == '-';
        if (!fcub_digits_only(p, 1, 4)) { mfd_err_set_format(err, "+/-XXXX", ""); return 0; }
        x = (double)atoi(p) * (neg ? -1.0 : 1.0);
        x = 50.0 * floor(x / 50.0 + 0.5);
        if (x < -6000.0 || x > 6000.0) { mfd_err_set_range(err, "-6000", "+6000", ""); return 0; }
        break;
    }
    case FCUB_FPA: {
        int neg = 0;
        if (*p == '+' || *p == '-') neg = *p++ == '-';
        if (!(p[0] >= '0' && p[0] <= '9' &&
              (p[1] == '\0' || (p[1] == '.' && p[2] >= '0' && p[2] <= '9' && p[3] == '\0')))) {
            mfd_err_set_format(err, "+/-X.X", DEG);
            return 0;
        }
        x = (double)(p[0] - '0') + (p[1] == '.' ? (double)(p[2] - '0') / 10.0 : 0.0);
        if (neg) x = -x;
        break;
    }
    default:
        return 0;
    }
    *v = x;
    return 1;
}

static void fcub_label(MfdUi *ui, float x, const char *s, int active, MfdAnchor a)
{
    mfd_text(ui, x, 172.0f, a, 22.0f, active ? MFD_COL_WHITE : MFD_COL_DARK_GREY, s);
}

/* Window value field (InputField look, 152 px wide, 37 px tall at y 188). */
static void fcub_window(MfdPageContext *ctx, MfdId id, float x0, FcubKind kind, const char *text, int digits,
                        const char *placeholder, const char *unit, int disabled)
{
    static const unsigned int set_cmd[6] = {A380X_FCUB_C_SET_SPEED, A380X_FCUB_C_SET_MACH, A380X_FCUB_C_SET_HEADING,
                                            A380X_FCUB_C_SET_ALTITUDE, A380X_FCUB_C_SET_VS, A380X_FCUB_C_SET_FPA};
    MfdUi *ui = ctx->ui;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    float x = x0 + 12.0f, y = 188.0f;
    mfd_field_desc_init(&fd, MFD_FMT_TXT_DROPDOWN);
    fd.fmt.max_digits = digits;
    fd.fmt.placeholder = placeholder;
    fd.width = 152.0f;
    fd.align = (unit && unit[0]) ? MFD_ANCHOR_LEFT : MFD_ANCHOR_CENTER; /* keep clear of the unit */
    fd.flags = MFD_FIELD_NOT_CLEARABLE | (disabled ? MFD_FIELD_DISABLED : 0u);
    memset(&fv, 0, sizeof(fv));
    fv.has_value = text != NULL && !disabled;
    fv.text = text;
    if (mfd_input_field(ui, id, x, y, &fd, &fv, &out) == MFD_FIELD_COMMITTED) {
        MfdFmsError err;
        double v;
        memset(&err, 0, sizeof(err));
        if (fcub_parse(kind, out.str, &v, &err)) fcub_cmd(ctx, set_cmd[kind], v);
        else mfd_ui_error(ui, &err, 1);
    }
    if (unit && unit[0] && !disabled && !(ui->focus_id == id && ui->edit == MFD_EDIT_EDITING))
        mfd_text(ui, x + 152.0f - 6.0f, y + 24.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_DARK_BLUE, unit);
}

static void fcub_afs_page(MfdPageContext *ctx)
{
    MfdUi *ui = ctx->ui;
    int pw = fcub_flag(ctx, A380X_FCUB_K_POWERED), dis = !pw;
    int mach = fcub_flag(ctx, A380X_FCUB_K_MACH_MODE), trk = fcub_flag(ctx, A380X_FCUB_K_TRK_FPA);
    int step1000 = fcub_rd(ctx, A380X_FCUB_K_ALT_STEP_FT) >= 999.0;
    uint32_t bf = dis ? MFD_BTN_DISABLED : 0u;
    char spd[16], hdg[16], alt[16], vs[16];
    const char *spd_t = NULL, *hdg_t = NULL, *vs_t = NULL;
    double v;
    int c;
    static const unsigned int up[4] = {A380X_FCUB_C_SPEED_UP, A380X_FCUB_C_HEADING_UP, A380X_FCUB_C_ALTITUDE_UP,
                                       A380X_FCUB_C_VS_UP};
    static const unsigned int down[4] = {A380X_FCUB_C_SPEED_DOWN, A380X_FCUB_C_HEADING_DOWN,
                                         A380X_FCUB_C_ALTITUDE_DOWN, A380X_FCUB_C_VS_DOWN};
    static const unsigned int push[4] = {A380X_FCUB_C_SPEED_PUSH, A380X_FCUB_C_HEADING_PUSH,
                                         A380X_FCUB_C_ALTITUDE_PUSH, A380X_FCUB_C_VS_PUSH};
    static const unsigned int pull[4] = {A380X_FCUB_C_SPEED_PULL, A380X_FCUB_C_HEADING_PULL,
                                         A380X_FCUB_C_ALTITUDE_PULL, A380X_FCUB_C_VS_PULL};
    static const unsigned int managed[3] = {A380X_FCUB_K_SPEED_MANAGED, A380X_FCUB_K_HEADING_MANAGED,
                                            A380X_FCUB_K_ALTITUDE_MANAGED};

    (void)MFD_PAGE_STATE(ctx, MfdFcubState);
    /* window strings = draw_fcu() of a380x_fcu_render.inc */
    if (pw && !fcub_flag(ctx, A380X_FCUB_K_SPEED_DASHES)) {
        if (mach) snprintf(spd, sizeof(spd), "%.2f", fcub_rd(ctx, A380X_FCUB_K_SELECTED_MACH));
        else snprintf(spd, sizeof(spd), "%03ld", lround(fcub_rd(ctx, A380X_FCUB_K_SELECTED_SPEED)));
        spd_t = spd;
    }
    if (pw && !fcub_flag(ctx, A380X_FCUB_K_HEADING_DASHES)) {
        v = fcub_rd(ctx, A380X_FCUB_K_SELECTED_HEADING);
        snprintf(hdg, sizeof(hdg), "%03ld", lround(isnan(v) ? 0.0 : v) % 360);
        hdg_t = hdg;
    }
    v = fcub_rd(ctx, A380X_FCUB_K_SELECTED_ALTITUDE);
    snprintf(alt, sizeof(alt), "%05ld", lround(isnan(v) ? 0.0 : v));
    if (pw && !fcub_flag(ctx, A380X_FCUB_K_VS_DASHES)) {
        if (trk) snprintf(vs, sizeof(vs), "%+.1f", fcub_rd(ctx, A380X_FCUB_K_SELECTED_FPA));
        else snprintf(vs, sizeof(vs), "%+05ld", lround(fcub_rd(ctx, A380X_FCUB_K_SELECTED_VS)));
        vs_t = vs;
    }

    /* window labels (baseline 172): active white, inactive #868a9c */
    fcub_label(ui, 40.0f, "SPD", !mach, MFD_ANCHOR_LEFT);
    fcub_label(ui, 100.0f, "MACH", mach, MFD_ANCHOR_LEFT);
    if (fcub_flag(ctx, A380X_FCUB_K_TRUE_HEADING)) fcub_label(ui, 200.0f, "TRUE", 1, MFD_ANCHOR_LEFT);
    fcub_label(ui, 266.0f, "HDG", !trk, MFD_ANCHOR_LEFT);
    fcub_label(ui, 322.0f, "TRK", trk, MFD_ANCHOR_LEFT);
    fcub_label(ui, 480.0f, "ALT", 1, MFD_ANCHOR_CENTER);
    fcub_label(ui, 616.0f, "V/S", !trk, MFD_ANCHOR_LEFT);
    fcub_label(ui, 676.0f, "FPA", trk, MFD_ANCHOR_LEFT);

    fcub_window(ctx, FID(1), 0.0f, mach ? FCUB_MACH : FCUB_SPD, spd_t, mach ? 4 : 3, mach ? ".--" : "---",
                mach ? "" : "KT", dis);
    fcub_window(ctx, FID(2), 192.0f, FCUB_HDG, hdg_t, 3, "---", DEG, dis);
    fcub_window(ctx, FID(3), 384.0f, FCUB_ALT, pw ? alt : NULL, 5, "-----", "FT", dis);
    fcub_window(ctx, FID(4), 576.0f, trk ? FCUB_FPA : FCUB_VS, vs_t, trk ? 4 : 5, trk ? "--.-" : "-----",
                trk ? DEG : "", dis);

    for (c = 0; c < 4; ++c) {
        float x0 = 192.0f * (float)c;
        if (c < 3 && pw && fcub_flag(ctx, managed[c])) mfd_draw_disc(ctx->dl, x0 + 178.0f, 206.5f, 7.0f, MFD_COL_GREEN);
        fcub_arrow(ctx, 2 * c, FID(10 + 2 * c), mfd_rect(x0 + 16.0f, 243.0f, 50.0f, 50.0f), MFD_ICON_SINGLE_DOWN, dis,
                   down[c]);
        fcub_arrow(ctx, 2 * c + 1, FID(11 + 2 * c), mfd_rect(x0 + 110.0f, 243.0f, 50.0f, 50.0f), MFD_ICON_SINGLE_UP,
                   dis, up[c]);
        if (mfd_button(ui, FID(20 + c), mfd_rect(x0 + 10.0f, 305.0f, 83.2f, 40.0f), "PUSH",
                       c == 3 ? MFD_BTN_DISABLED : bf)) /* V/S push: no effect on the A380 */
            fcub_cmd(ctx, push[c], A380X_MFD_ACT_ONCE);
        if (mfd_button(ui, FID(24 + c), mfd_rect(x0 + 98.8f, 305.0f, 83.2f, 40.0f), "PULL", bf))
            fcub_cmd(ctx, pull[c], A380X_MFD_ACT_ONCE);
    }
    if (mfd_button(ui, FID(30), mfd_rect(26.85f, 357.0f, 138.3f, 40.0f), "SPD/MACH", bf))
        fcub_cmd(ctx, A380X_FCUB_C_SPD_MACH, A380X_MFD_ACT_ONCE);
    if (mfd_button(ui, FID(31), mfd_rect(218.85f, 357.0f, 138.3f, 40.0f), "TRUE/MAG", bf))
        fcub_cmd(ctx, A380X_FCUB_C_TRUE_MAG, A380X_MFD_ACT_ONCE);
    if (mfd_button(ui, FID(32), mfd_rect(401.7f, 357.0f, 69.4f, 40.0f), "100", bf | ((step1000 || dis) ? 0u : MFD_BTN_SELECTED)) &&
        step1000)
        fcub_cmd(ctx, A380X_FCUB_C_ALT_STEP_TOGGLE, A380X_MFD_ACT_ONCE);
    if (mfd_button(ui, FID(33), mfd_rect(475.1f, 357.0f, 83.2f, 40.0f), "1000", bf | ((step1000 && !dis) ? MFD_BTN_SELECTED : 0u)) &&
        !step1000)
        fcub_cmd(ctx, A380X_FCUB_C_ALT_STEP_TOGGLE, A380X_MFD_ACT_ONCE);
    if (mfd_button(ui, FID(34), mfd_rect(609.7f, 357.0f, 124.6f, 62.0f), "HDG V/S\nTRK FPA", bf))
        fcub_cmd(ctx, A380X_FCUB_C_TRK_FPA, A380X_MFD_ACT_ONCE);
    mfd_draw_fill_rect(ctx->dl, mfd_rect(0.0f, 435.0f, 768.0f, 2.0f), MFD_COL_LIGHT_GREY);

    /* pushbuttons (centred on the spec's 94 x 64 slots) */
    {
        static const char *const r1[5] = {"LOC", "AP1", "AP2", "A/THR", "APPR"};
        static const unsigned int r1k[5] = {A380X_FCUB_K_LOC, A380X_FCUB_K_AP1, A380X_FCUB_K_AP2, A380X_FCUB_K_ATHR,
                                            A380X_FCUB_K_APPR};
        static const unsigned int r1c[5] = {A380X_FCUB_C_LOC, A380X_FCUB_C_AP1, A380X_FCUB_C_AP2, A380X_FCUB_C_ATHR,
                                            A380X_FCUB_C_APPR};
        static const char *const r2[4] = {"FD", "ALT", "EXPED", "METRIC"};
        static const unsigned int r2k[4] = {A380X_FCUB_K_FD, A380X_FCUB_K_ALT_HOLD, 0u, A380X_FCUB_K_METRIC_ALT};
        static const unsigned int r2c[4] = {A380X_FCUB_C_FD, A380X_FCUB_C_ALT_HOLD, 0u, A380X_FCUB_C_METRIC};
        for (c = 0; c < 5; ++c) {
            if (fcub_pb(ui, FID(40 + c), 103.0f + 118.0f * (float)c, 457.0f, r1[c], pw && fcub_flag(ctx, r1k[c]), dis))
                fcub_cmd(ctx, r1c[c], A380X_MFD_ACT_ONCE);
        }
        for (c = 0; c < 4; ++c) {
            int exped = c == 2; /* EXPED: no A380 function / no port command (open question 8) */
            if (fcub_pb(ui, FID(50 + c), 162.0f + 118.0f * (float)c, 545.0f, r2[c],
                        !exped && pw && fcub_flag(ctx, r2k[c]), dis || exped))
                fcub_cmd(ctx, r2c[c], A380X_MFD_ACT_ONCE);
        }
    }
}

/* ======================================================================================== */
/* EFIS CP (spec 06 section 11)                                                             */
/* ======================================================================================== */

static void fcub_steps(MfdPageContext *ctx, int from, int to, unsigned int up_cmd, unsigned int down_cmd)
{
    int n = to - from, i;
    for (i = 0; i < (n < 0 ? -n : n); ++i) fcub_cmd(ctx, n > 0 ? up_cmd : down_cmd, A380X_MFD_ACT_ONCE);
}

static void fcub_efis_page(MfdPageContext *ctx)
{
    MfdUi *ui = ctx->ui;
    int pw = fcub_flag(ctx, A380X_EFIS_K_POWERED), dis = !pw, k;
    int std = fcub_flag(ctx, A380X_EFIS_K_BARO_STD), inhg = fcub_flag(ctx, A380X_EFIS_K_BARO_INHG);
    int mode = (int)lround(isnan(fcub_rd(ctx, A380X_EFIS_K_MODE)) ? 3.0 : fcub_rd(ctx, A380X_EFIS_K_MODE));
    int range = (int)lround(isnan(fcub_rd(ctx, A380X_EFIS_K_RANGE_INDEX)) ? 1.0 : fcub_rd(ctx, A380X_EFIS_K_RANGE_INDEX));
    double preset = fcub_rd(ctx, A380X_EFIS_K_BARO_PRESET_HPA);
    uint32_t bf = dis ? MFD_BTN_DISABLED : 0u;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    static const char *const modes[5] = {"ROSE LS", "ROSE VOR", "ROSE NAV", "ARC", "PLAN"};
    static const char *const ranges[8] = {"ZOOM", "10", "20", "40", "80", "160", "320", "640"};
    static const char *const data_lab[5] = {"CSTR", "WPT", "VOR D", "NDB", "ARPT"};
    static const unsigned int data_k[5] = {A380X_EFIS_K_CSTR, A380X_EFIS_K_WPT, A380X_EFIS_K_VORD, A380X_EFIS_K_NDB,
                                           A380X_EFIS_K_ARPT};
    static const unsigned int data_c[5] = {A380X_EFIS_C_CSTR, A380X_EFIS_C_WPT, A380X_EFIS_C_VORD, A380X_EFIS_C_NDB,
                                           A380X_EFIS_C_ARPT};
    static const char *const ovl_lab[3] = {"WX", "TERR", "TRAF"};
    static const unsigned int ovl_k[3] = {A380X_EFIS_K_WX_REQUESTED, A380X_EFIS_K_TERR, A380X_EFIS_K_TRAF};
    static const unsigned int ovl_c[3] = {A380X_EFIS_C_WX, A380X_EFIS_C_TERR, A380X_EFIS_C_TRAF};

    (void)MFD_PAGE_STATE(ctx, MfdFcubState);
    if (mode < 0 || mode > 4) mode = 3;
    if (range < 0 || range > 7) range = 1;

    /* BARO */
    mfd_draw_text(ui->dl, MFD_FONT_ECAM, 28.0f, MFD_COL_WHITE, MFD_ANCHOR_LEFT, 16.0f, 172.0f, "BARO");
    mfd_field_desc_init(&fd, MFD_FMT_QNH);
    fd.fmt.qnh_is_hpa = !inhg;
    fd.width = 160.0f;
    fd.flags = MFD_FIELD_NOT_CLEARABLE | (dis ? MFD_FIELD_DISABLED : 0u);
    memset(&fv, 0, sizeof(fv));
    fv.has_value = pw && !isnan(preset);
    fv.num = inhg ? floor(preset / MFD_HPA_PER_INHG * 100.0 + 0.5) / 100.0 : floor(preset + 0.5);
    if (mfd_input_field(ui, EID(1), 16.0f, 192.5f, &fd, &fv, &out) == MFD_FIELD_COMMITTED)
        fcub_cmd(ctx, A380X_EFIS_C_SET_BARO_HPA, mfd_qnh_to_millibar(out.num));
    fcub_arrow(ctx, 8, EID(2), mfd_rect(186.0f, 186.0f, 50.0f, 50.0f), MFD_ICON_SINGLE_DOWN, dis, A380X_EFIS_C_BARO_DOWN);
    fcub_arrow(ctx, 9, EID(3), mfd_rect(242.0f, 186.0f, 50.0f, 50.0f), MFD_ICON_SINGLE_UP, dis, A380X_EFIS_C_BARO_UP);
    if (mfd_button(ui, EID(4), mfd_rect(16.0f, 247.0f, 69.4f, 40.0f), "STD", bf | ((std && !dis) ? MFD_BTN_SELECTED : 0u)) && !std)
        fcub_cmd(ctx, A380X_EFIS_C_BARO_STD, A380X_MFD_ACT_ONCE);
    if (mfd_button(ui, EID(5), mfd_rect(91.4f, 247.0f, 69.4f, 40.0f), "QNH", bf | ((std || dis) ? 0u : MFD_BTN_SELECTED)) && std)
        fcub_cmd(ctx, A380X_EFIS_C_BARO_QNH, A380X_MFD_ACT_ONCE);
    if (mfd_button(ui, EID(6), mfd_rect(200.0f, 247.0f, 69.4f, 40.0f), "HPA", bf | ((inhg || dis) ? 0u : MFD_BTN_SELECTED)) && inhg)
        fcub_cmd(ctx, A380X_EFIS_C_BARO_UNIT, A380X_MFD_ACT_ONCE);
    if (mfd_button(ui, EID(7), mfd_rect(275.4f, 247.0f, 97.0f, 40.0f), "IN HG", bf | ((inhg && !dis) ? MFD_BTN_SELECTED : 0u)) &&
        !inhg)
        fcub_cmd(ctx, A380X_EFIS_C_BARO_UNIT, A380X_MFD_ACT_ONCE);

    /* PFD: LS, VV, TAXI (TAXI not available on the backup page) */
    mfd_draw_text(ui->dl, MFD_FONT_ECAM, 28.0f, MFD_COL_WHITE, MFD_ANCHOR_LEFT, 420.0f, 172.0f, "PFD");
    if (fcub_pb(ui, EID(10), 422.0f, 188.0f, "LS", pw && fcub_flag(ctx, A380X_EFIS_K_LS), dis))
        fcub_cmd(ctx, A380X_EFIS_C_LS, A380X_MFD_ACT_ONCE);
    if (fcub_pb(ui, EID(11), 540.0f, 188.0f, "VV", pw && fcub_flag(ctx, A380X_EFIS_K_VV), dis))
        fcub_cmd(ctx, A380X_EFIS_C_VV, A380X_MFD_ACT_ONCE);
    fcub_pb(ui, EID(12), 658.0f, 188.0f, "TAXI", 0, 1);
    mfd_draw_fill_rect(ui->dl, mfd_rect(0.0f, 303.0f, 768.0f, 2.0f), MFD_COL_LIGHT_GREY);

    /* ND MODE / ND RANGE radio groups (rows 34 px, pitch 39) */
    mfd_text(ui, 16.0f, 340.2f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "ND MODE");
    k = mfd_radio_group(ui, EID(20), 16.0f, 352.0f, 214.0f, modes, 5, mode, dis ? 0x1fu : 0u, MFD_COL_CYAN, 0.0f);
    if (k >= 0) fcub_steps(ctx, mode, k, A380X_EFIS_C_MODE_UP, A380X_EFIS_C_MODE_DOWN);
    mfd_text(ui, 250.0f, 340.2f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "ND RANGE");
    k = mfd_radio_group(ui, EID(21), 250.0f, 352.0f, 190.0f, ranges, 8, range, dis ? 0xffu : 0x1u, MFD_COL_CYAN, 0.0f);
    if (k >= 1) fcub_steps(ctx, range, k, A380X_EFIS_C_RANGE_UP, A380X_EFIS_C_RANGE_DOWN);

    /* BRG PTR */
    mfd_text(ui, 16.0f, 600.2f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "BRG PTR");
    if (fcub_pb(ui, EID(30), 18.0f, 614.0f, "VOR 1", pw && fcub_flag(ctx, A380X_EFIS_K_VOR1), dis))
        fcub_cmd(ctx, A380X_EFIS_C_VOR1, A380X_MFD_ACT_ONCE);
    if (fcub_pb(ui, EID(31), 136.0f, 614.0f, "VOR 2", pw && fcub_flag(ctx, A380X_EFIS_K_VOR2), dis))
        fcub_cmd(ctx, A380X_EFIS_C_VOR2, A380X_MFD_ACT_ONCE);

    /* ND DATA / ND OVERLAY */
    mfd_text(ui, 466.0f, 340.2f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "ND DATA");
    for (k = 0; k < 5; ++k) {
        float x = 468.0f + 102.0f * (float)(k % 3), y = k < 3 ? 354.0f : 426.0f;
        if (fcub_pb(ui, EID(40 + k), x, y, data_lab[k], pw && fcub_flag(ctx, data_k[k]), dis))
            fcub_cmd(ctx, data_c[k], A380X_MFD_ACT_ONCE);
    }
    mfd_text(ui, 466.0f, 528.2f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "ND OVERLAY");
    for (k = 0; k < 3; ++k) {
        if (fcub_pb(ui, EID(50 + k), 468.0f + 102.0f * (float)k, 542.0f, ovl_lab[k], pw && fcub_flag(ctx, ovl_k[k]), dis))
            fcub_cmd(ctx, ovl_c[k], A380X_MFD_ACT_ONCE);
    }
}

void a380x_mfd_page_fcubkup_register(void)
{
    mfd_register_page("fcubkup/afs", MFD_PAGE_ID_FCUBKUP_AFS, "AFS CP", fcub_afs_page, 0u);
    mfd_register_page("fcubkup/efis", MFD_PAGE_ID_FCUBKUP_EFIS, "EFIS CP", fcub_efis_page, 0u);
}
