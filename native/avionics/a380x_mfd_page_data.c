/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD DATA/STATUS and DATA/AIRPORT pages. Translation of the pinned FBW
 * pages/FMS/DATA/MfdFmsDataStatus.tsx and MfdFmsDataAirport.tsx (spec 03 sections 5 and 6),
 * laid out with MSFS/Coherent border-box sizing on the spec 01 frame (body 138..950).
 */
#include "a380x_mfd_page_data.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NBSP "\xC2\xA0"
#define DEG "\xC2\xB0"
#define ID(n) MFD_ID(MFD_PAGE_ID_DATA_STATUS, (n))
#define AID(n) MFD_ID(MFD_PAGE_ID_DATA_AIRPORT, (n))

static int g_legacy_layout;

void mfd_page_data_set_legacy_layout(int legacy)
{
    g_legacy_layout = legacy ? 1 : 0;
}

/* ======================================================================================== */
/* AIRAC helpers                                                                            */
/* ======================================================================================== */

static long days_from_civil(long y, unsigned m, unsigned d)
{
    long era;
    unsigned yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m + (m > 2 ? (unsigned)-3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static void civil_from_days(long z, long *y, unsigned *m, unsigned *d)
{
    long era;
    unsigned doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    *y = (long)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y += *m <= 2;
}

int mfd_airac_cycle_dates(const char *cycle, char *out, size_t n)
{
    static const char *const mon[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    long base, year_start, k0, eff, exp, y;
    unsigned m1, d1, m2, d2;
    int yy, nn;
    if (!cycle || strlen(cycle) != 4 || sscanf(cycle, "%2d%2d", &yy, &nn) != 2 || nn < 1 || nn > 14) return 0;
    base = days_from_civil(2020, 1, 2);
    year_start = days_from_civil(2000 + yy, 1, 1);
    k0 = (year_start - base + 27) / 28; /* ceil */
    if (year_start - base < 0) k0 = -((base - year_start) / 28);
    eff = base + (k0 + nn - 1) * 28;
    exp = eff + 28;
    civil_from_days(eff, &y, &m1, &d1);
    civil_from_days(exp, &y, &m2, &d2);
    snprintf(out, n, "%02u%s-%02u%s", d1, mon[m1 - 1], d2, mon[m2 - 1]);
    return 1;
}

void mfd_navdb_serial(const char *prefix, const char *cycle, char *out, size_t n)
{
    snprintf(out, n, "%.2s%s0001", prefix ? prefix : "XP", cycle ? cycle : "");
}

/* ======================================================================================== */
/* DATA/STATUS                                                                              */
/* ======================================================================================== */

static void default_status(A380XMfdStatusData *st)
{
    memset(st, 0, sizeof(*st));
    snprintf(st->airframe, sizeof(st->airframe), "A380-800");
    snprintf(st->engine, sizeof(st->engine), "TRENT 972");
}

static void factor_text(float f, char *buf, size_t n)
{
    char num[32];
    mfd_js_to_fixed(fabs((double)f), 1, num, sizeof(num));
    snprintf(buf, n, "%c%.12s", f >= 0.0f ? '+' : '-', num);
}

static void swap_dialog(MfdPageContext *ctx, MfdDataStatusState *s, MfdRect box)
{
    int r = mfd_confirm_dialog(ctx->ui, ID(60), box, "SWAP" NBSP "?", 0);
    if (r == MFD_DIALOG_CANCEL) {
        s->swap_dialog = 0;
    } else if (r == MFD_DIALOG_CONFIRM) {
        s->swap_dialog = 0;
        if (ctx->host && ctx->host->swap_nav_database) ctx->host->swap_nav_database(ctx->host->ctx);
        mfd_navigate_all(ctx, "fms/data/status"); /* FMS reset lands on DATA/STATUS ([PORT] both sides) */
    }
}

static void stored_counts(MfdUi *ui, const A380XMfdStatusData *st, float col1, float val1, float col3, float val3,
                          float b1, float b2, float label_fs, float value_fs, float value_db)
{
    char t[8];
    MfdColor w = MFD_COL_WHITE, g = MFD_COL_GREEN;
    mfd_text(ui, col1, b1, MFD_ANCHOR_RIGHT, label_fs, w, "WAYPOINTS");
    snprintf(t, sizeof(t), "%02d", st->stored_waypoints);
    mfd_text(ui, val1, b1 + value_db, MFD_ANCHOR_LEFT, value_fs, g, t);
    mfd_text(ui, col3, b1, MFD_ANCHOR_RIGHT, label_fs, w, "ROUTES");
    snprintf(t, sizeof(t), "%02d", st->stored_routes);
    mfd_text(ui, val3, b1 + value_db, MFD_ANCHOR_LEFT, value_fs, g, t);
    mfd_text(ui, col1, b2, MFD_ANCHOR_RIGHT, label_fs, w, "NAVAIDS");
    snprintf(t, sizeof(t), "%02d", st->stored_navaids);
    mfd_text(ui, val1, b2 + value_db, MFD_ANCHOR_LEFT, value_fs, g, t);
    mfd_text(ui, col3, b2, MFD_ANCHOR_RIGHT, label_fs, w, "RUNWAYS");
    snprintf(t, sizeof(t), "%02d", st->stored_runways);
    mfd_text(ui, val3, b2 + value_db, MFD_ANCHOR_LEFT, value_fs, g, t);
}

static void delete_all_button(MfdPageContext *ctx, const A380XMfdStatusData *st, MfdRect r)
{
    MfdButtonDesc bd;
    int total = st->stored_waypoints + st->stored_routes + st->stored_navaids + st->stored_runways;
    mfd_button_desc_init(&bd, "DELETE ALL");
    bd.suffix = "*";
    bd.inner_w = 175.0f;
    bd.flags = total == 0 ? MFD_BTN_DISABLED : 0u;
    if (mfd_button_ex(ctx->ui, ID(50), r, &bd) == MFD_BUTTON_CLICKED && ctx->host && ctx->host->delete_all_stored)
        ctx->host->delete_all_stored(ctx->host->ctx);
}

static void swap_button(MfdPageContext *ctx, MfdDataStatusState *s, MfdRect r)
{
    MfdButtonDesc bd;
    mfd_button_desc_init(&bd, "SWAP");
    bd.suffix = "*";
    bd.inner_w = 100.0f;
    bd.label_dx = 10.0f;
    if (mfd_button_ex(ctx->ui, ID(40), r, &bd) == MFD_BUTTON_CLICKED) s->swap_dialog = 1;
}

/* Pinned FBW layout (MfdFmsDataStatus.tsx). Content box x 14..754 from y 186. */
static void acft_status_pinned(MfdPageContext *ctx, MfdDataStatusState *s)
{
    MfdUi *ui = ctx->ui;
    MfdDrawList *dl = ctx->dl;
    const A380XMfdStatusData *st = &s->st;
    char text[64], f1[16], f2[16];
    float x, w, adv24 = mfd_char_advance(24.0f), adv30 = mfd_char_advance(30.0f), box_w, swap_x, col_x, col_w;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    MfdFieldResult fr;
    MfdButtonDesc bd;

    /* Airframe line: margin-top 20, width 99 %, centred (30 green). */
    snprintf(text, sizeof(text), "%s" NBSP "/" NBSP "%s", st->airframe, st->engine);
    mfd_text(ui, 14.0f + 732.6f * 0.5f, 230.0f, MFD_ANCHOR_CENTER, 30.0f, MFD_COL_GREEN, text);

    /* IDLE / PERF row: width 655, border 1, padding 10 -> 14..669 x 436..498. */
    mfd_draw_bevel(dl, mfd_rect(14.0f, 436.0f, 655.0f, 62.0f), 1.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_SOLID);
    factor_text(st->idle_factor, f1, sizeof(f1));
    factor_text(st->perf_factor, f2, sizeof(f2));
    x = 25.0f;
    mfd_text(ui, x, 474.2f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "IDLE");
    x += 4.0f * adv24 + 25.0f;
    mfd_text(ui, x, 476.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, f1);
    x += mfd_text_width(f1, 30.0f) + 60.0f;
    mfd_text(ui, x, 474.2f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "PERF");
    x += 4.0f * adv24 + 25.0f;
    mfd_text(ui, x, 476.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, f2);
    x += mfd_text_width(f2, 30.0f) + 95.0f;
    mfd_button_desc_init(&bd, "MODIFY");
    bd.flags = MFD_BTN_DISABLED; /* [FBW-PH] always disabled */
    mfd_button_ex(ui, ID(10), mfd_rect(x, 447.0f, 125.0f, 40.0f), &bd);

    /* FUEL PENALTY row: width 380 -> 14..394 x 508..567; field 155 wide. */
    mfd_draw_bevel(dl, mfd_rect(14.0f, 508.0f, 380.0f, 59.0f), 1.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_SOLID);
    mfd_text(ui, 25.0f, 544.7f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "FUEL PENALTY" NBSP);
    mfd_field_desc_init(&fd, MFD_FMT_FUEL_PENALTY);
    fd.width = 155.0f;
    fd.fmt.err_details = 0; /* the page calls showFmsErrorMessage(e.type) only */
    memset(&fv, 0, sizeof(fv));
    fv.has_value = st->fuel_penalty_has;
    fv.is_computed = !st->fuel_penalty_pilot;
    fv.num = st->fuel_penalty_pct;
    fr = mfd_input_field(ui, ID(20), 25.0f + 13.0f * adv24, 519.0f, &fd, &fv, &out);
    if (fr != MFD_FIELD_NONE && ctx->host && ctx->host->set_fuel_penalty) {
        MfdFmsError err;
        memset(&err, 0, sizeof(err));
        if (!ctx->host->set_fuel_penalty(ctx->host->ctx, fr == MFD_FIELD_COMMITTED, out.num, &err))
            mfd_ui_error(ui, &err, 0);
    }

    /* First section divider: 1 px outset, 14..754 x 577..579. */
    mfd_draw_bevel(dl, mfd_rect(14.0f, 577.0f, 740.0f, 2.0f), 1.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_RAISED);

    /* NAV DATABASE line: shared baseline 618. */
    mfd_text(ui, 24.0f, 618.0f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "NAV DATABASE");
    mfd_text(ui, 24.0f + 12.0f * adv24 + 25.0f, 618.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, st->navdb_serial);

    /* ACTIVE box: border 1 lightgrey, padding 20/15/15/15, height 117 -> y 639..756. */
    w = mfd_text_width(st->navdb_active, 30.0f);
    if (w < 6.0f * adv24) w = 6.0f * adv24;
    box_w = w + 32.0f;
    mfd_draw_bevel(dl, mfd_rect(14.0f, 639.0f, box_w, 117.0f), 1.0f, MFD_COL_CSS_LIGHTGREY, MFD_BEVEL_SOLID);
    mfd_text(ui, 30.0f + w * 0.5f, 679.2f, MFD_ANCHOR_CENTER, 24.0f, MFD_COL_WHITE, "ACTIVE");
    mfd_text(ui, 30.0f, 733.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, st->navdb_active);

    /* SWAP *: 70 px after the box, 140 x 62, vertically centred in the 117 px row. */
    swap_x = 14.0f + box_w + 70.0f;
    swap_button(ctx, s, mfd_rect(swap_x, 666.5f, 140.0f, 62.0f));

    /* SECOND column: 55 px after SWAP, margin-top 8, label centred, value 25 px with two NBSP. */
    col_x = swap_x + 140.0f + 55.0f;
    snprintf(text, sizeof(text), NBSP NBSP "%s", st->navdb_second);
    col_w = mfd_text_width(text, 25.0f);
    if (col_w < 6.0f * adv24) col_w = 6.0f * adv24;
    mfd_text(ui, col_x + col_w * 0.5f, 682.7f, MFD_ANCHOR_CENTER, 24.0f, MFD_COL_WHITE, "SECOND");
    mfd_text(ui, col_x, 734.5f, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_GREEN, text);
    (void)adv30;

    /* Second section bottom border 2 px. */
    mfd_draw_fill_rect(dl, mfd_rect(14.0f, 781.0f, 740.0f, 2.0f), MFD_COL_LIGHT_GREY);

    /* PILOT STORED ELEMENTS */
    mfd_text(ui, 33.0f, 817.2f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "PILOT STORED ELEMENTS");
    /* grid 52% 22% 40% 10% of 337 px from x 14; labels right-aligned with 10 px margin. */
    stored_counts(ui, st, 179.24f, 189.24f, 388.18f, 398.18f, 861.2f, 913.2f, 24.0f, 30.0f, 1.8f);
    delete_all_button(ctx, st, mfd_rect(551.0f, 851.0f, 203.0f, 58.0f));

    if (s->swap_dialog) swap_dialog(ctx, s, mfd_rect(221.5f, 599.0f, 325.0f, 165.0f));
}

/* Older FBW layout of the reference screenshot (port option, measured on the screenshot and
 * re-based on the 40 px title bar). */
static void acft_status_legacy(MfdPageContext *ctx, MfdDataStatusState *s)
{
    MfdUi *ui = ctx->ui;
    MfdDrawList *dl = ctx->dl;
    const A380XMfdStatusData *st = &s->st;
    char f1[16], f2[16];
    MfdButtonDesc bd;

    mfd_text(ui, 384.0f, 228.0f, MFD_ANCHOR_CENTER, 30.0f, MFD_COL_GREEN, st->airframe);
    mfd_text(ui, 13.0f, 298.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "ENGINE");
    mfd_text(ui, 144.0f, 299.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, st->engine);

    mfd_draw_bevel(dl, mfd_rect(597.0f, 274.0f, 160.0f, 92.0f), 1.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_SOLID);
    factor_text(st->idle_factor, f1, sizeof(f1));
    factor_text(st->perf_factor, f2, sizeof(f2));
    mfd_text(ui, 607.0f, 309.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "IDLE");
    mfd_text(ui, 669.0f, 309.0f, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_GREEN, "+");
    mfd_text(ui, 684.0f, 309.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, f1 + 1);
    mfd_text(ui, 607.0f, 349.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "PERF");
    mfd_text(ui, 669.0f, 349.0f, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_GREEN, "+");
    mfd_text(ui, 684.0f, 349.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, f2 + 1);
    mfd_button_desc_init(&bd, "MODIFY");
    bd.flags = MFD_BTN_DISABLED;
    mfd_button_ex(ui, ID(10), mfd_rect(613.0f, 377.0f, 125.0f, 40.0f), &bd);

    mfd_draw_bevel(dl, mfd_rect(14.0f, 432.0f, 740.0f, 2.0f), 1.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_RAISED);
    mfd_text(ui, 13.0f, 472.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "NAV DATABASE");
    mfd_text(ui, 195.0f, 473.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, st->navdb_serial);

    mfd_draw_bevel(dl, mfd_rect(14.0f, 494.0f, 242.0f, 97.0f), 1.0f, MFD_COL_CSS_LIGHTGREY, MFD_BEVEL_SOLID);
    mfd_text(ui, 135.0f, 529.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "ACTIVE");
    mfd_text(ui, 32.0f, 569.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, st->navdb_active);
    swap_button(ctx, s, mfd_rect(338.0f, 513.0f, 128.0f, 58.0f));
    mfd_text(ui, 651.0f, 529.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "SECOND");
    mfd_text(ui, 566.0f, 565.0f, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_GREEN, st->navdb_second);

    mfd_draw_fill_rect(dl, mfd_rect(14.0f, 606.0f, 740.0f, 2.0f), MFD_COL_LIGHT_GREY);
    mfd_text(ui, 13.0f, 649.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "PILOT STORED ELEMENTS");
    stored_counts(ui, st, 140.0f, 150.0f, 306.0f, 318.0f, 706.0f, 750.0f, 20.0f, 25.0f, 0.0f);
    delete_all_button(ctx, st, mfd_rect(553.0f, 703.0f, 203.0f, 58.0f));

    if (s->swap_dialog) swap_dialog(ctx, s, mfd_rect(221.5f, 450.0f, 325.0f, 165.0f));
}

static void data_status_page(MfdPageContext *ctx)
{
    MfdDataStatusState *s = MFD_PAGE_STATE(ctx, MfdDataStatusState);
    static const char *const titles[] = {"ACFT STATUS", "FMS P/N"};
    MfdTabsDesc td;
    MfdRect content, clip;
    int k;

    if (ctx->entered) {
        s->tab = strcmp(ctx->uri->extra, "fms-pn") == 0 ? 1 : 0;
        s->swap_dialog = 0;
    }
    default_status(&s->st);
    if (ctx->host && ctx->host->read_status) ctx->host->read_status(ctx->host->ctx, &s->st);

    mfd_tabs_desc_init(&td);
    td.slant_deg = 25.0f;
    td.selected_text = MFD_COL_WHITE;
    /* Tab content: border-box height 735 grown by half of the 37 px slack (flex) = 753.5.
     * The legacy (screenshot) build ends the frame at y 869. */
    td.content_h = g_legacy_layout ? 693.0f : 753.5f;
    k = mfd_top_tabs(ctx->ui, ID(1), ctx->body, titles, 2, s->tab, &td, &content, &clip);
    if (k >= 0) {
        s->tab = k;
        s->swap_dialog = 0; /* switching tabs hides the SWAP dialog */
    }
    if (s->tab != 0) return; /* FMS P/N is empty in FBW [FBW-PH] */
    mfd_draw_clip_push(ctx->dl, clip);
    if (g_legacy_layout) acft_status_legacy(ctx, s);
    else acft_status_pinned(ctx, s);
    mfd_draw_clip_pop(ctx->dl);
}

/* ======================================================================================== */
/* DATA/AIRPORT (spec 03 section 6, re-based +4 px on the 138 px body top)                  */
/* ======================================================================================== */

static void coord_text(double lat, double lon, char *buf, size_t n)
{
    double alat = fabs(lat), alon = fabs(lon);
    int dlat = (int)alat, dlon = (int)alon;
    double mlat = (alat - dlat) * 60.0, mlon = (alon - dlon) * 60.0;
    int mi_lat = (int)mlat, mi_lon = (int)mlon;
    int t_lat = (int)floor((mlat - mi_lat) * 10.0), t_lon = (int)floor((mlon - mi_lon) * 10.0); /* [PORT] tenths */
    snprintf(buf, n, "%02d" DEG "%02d.%d%c/%03d" DEG "%02d.%d%c", dlat, mi_lat, t_lat, lat >= 0 ? 'N' : 'S', dlon,
             mi_lon, t_lon, lon >= 0 ? 'E' : 'W');
}

static void length_text(float len_m, int metric, char *num, size_t n, const char **unit)
{
    *unit = metric ? "M" : "FT";
    if (isnan(len_m)) {
        snprintf(num, n, "-----");
        return;
    }
    snprintf(num, n, "%.0f", metric ? (double)len_m : (double)len_m / MFD_M_PER_FT);
}

static void runway_list(MfdPageContext *ctx, MfdDataAirportState *s)
{
    int k;
    for (k = 0; k < s->apt.runway_count && k < 24; ++k) {
        const A380XMfdRunway *r = &s->apt.rwy[k];
        char num[16], label[48];
        const char *unit;
        MfdButtonDesc bd;
        int c = k / 8, row = k % 8;
        length_text(r->length_m, ctx->in->metric_units, num, sizeof(num), &unit);
        /* designation.padEnd(3) + " " + length.padStart(5, NBSP) + unit + " " + LS */
        snprintf(label, sizeof(label), "%-3s %*s%s%s%s", r->ident, 5, num, unit, r->ls_ident[0] ? " " : "",
                 r->ls_ident[0] ? "LS" : "");
        mfd_button_desc_init(&bd, label);
        bd.suffix = "";
        if (mfd_button_ex(ctx->ui, AID(100 + k), mfd_rect(25.0f + 230.0f * (float)c, 369.0f + 50.0f * (float)row,
                                                          220.0f, 40.0f), &bd) == MFD_BUTTON_CLICKED)
            s->selected = k;
    }
}

static void runway_detail(MfdPageContext *ctx, MfdDataAirportState *s)
{
    MfdUi *ui = ctx->ui;
    const A380XMfdRunway *r = &s->apt.rwy[s->selected];
    char t[48], num[16];
    const char *unit;
    float vx = 285.0f;
    mfd_draw_fill_rect(ctx->dl, mfd_rect(15.0f, 364.0f, 738.0f, 448.0f), MFD_COL_BLACK);
    mfd_text(ui, 265.0f, mfd_baseline(392.0f, 20.0f), MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "RWY");
    mfd_text(ui, vx, mfd_baseline(384.0f, 30.0f), MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, r->ident);
    snprintf(t, sizeof(t), "%d/%d", s->selected + 1, s->apt.runway_count);
    mfd_text(ui, 449.0f, mfd_baseline(414.0f, 20.0f), MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, t);
    if (mfd_icon_button(ui, AID(10), mfd_rect(396.5f, 444.0f, 50.0f, 50.0f), MFD_ICON_DOUBLE_LEFT, s->selected == 0))
        s->selected--;
    if (mfd_icon_button(ui, AID(11), mfd_rect(451.5f, 444.0f, 50.0f, 50.0f), MFD_ICON_DOUBLE_RIGHT,
                        s->selected >= s->apt.runway_count - 1))
        s->selected++;
    if (s->selected < 0) s->selected = 0;
    if (s->selected >= s->apt.runway_count) s->selected = s->apt.runway_count - 1;
    r = &s->apt.rwy[s->selected];
    mfd_text(ui, 265.0f, mfd_baseline(522.0f, 20.0f), MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "LAT/LONG");
    coord_text(r->lat, r->lon, t, sizeof(t));
    mfd_text(ui, vx, mfd_baseline(514.0f, 30.0f), MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, t);
    mfd_text(ui, 265.0f, mfd_baseline(577.0f, 20.0f), MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "ELEVATION");
    if (isnan(r->elevation_ft)) snprintf(t, sizeof(t), "-----");
    else snprintf(t, sizeof(t), "%5.0f", floor(r->elevation_ft / 10.0f + 0.5f) * 10.0);
    mfd_text(ui, vx, mfd_baseline(569.0f, 30.0f), MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, t);
    mfd_text(ui, vx + mfd_text_width(t, 30.0f) + 6.0f, mfd_baseline(577.0f, 20.0f), MFD_ANCHOR_LEFT, 20.0f,
             MFD_COL_DARK_BLUE, "FT");
    mfd_text(ui, 265.0f, mfd_baseline(632.0f, 20.0f), MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "LENGTH");
    length_text(r->length_m, ctx->in->metric_units, num, sizeof(num), &unit);
    snprintf(t, sizeof(t), " %s", num);
    mfd_text(ui, vx, mfd_baseline(624.0f, 30.0f), MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, t);
    mfd_text(ui, vx + mfd_text_width(t, 30.0f) + 6.0f, mfd_baseline(632.0f, 20.0f), MFD_ANCHOR_LEFT, 20.0f,
             MFD_COL_DARK_BLUE, unit);
    mfd_text(ui, 530.75f, mfd_baseline(632.0f, 20.0f), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "CRS");
    if (isnan(r->course_deg)) snprintf(t, sizeof(t), "---");
    else snprintf(t, sizeof(t), "%03.0f", fmod(floor(r->course_deg + 0.5f) + 360.0, 360.0));
    mfd_text(ui, 578.25f, mfd_baseline(624.0f, 30.0f), MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, t);
    mfd_text(ui, 640.5f, mfd_baseline(632.0f, 20.0f), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, DEG);
    mfd_text(ui, 265.0f, mfd_baseline(687.0f, 20.0f), MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "LS IDENT");
    mfd_text(ui, vx, mfd_baseline(679.0f, 30.0f), MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, r->ls_ident);
    if (mfd_button(ui, AID(12), mfd_rect(25.0f, 754.0f, 220.0f, 40.0f), "RWY LIST", 0)) s->selected = -1;
}

static void data_airport_page(MfdPageContext *ctx)
{
    MfdDataAirportState *s = MFD_PAGE_STATE(ctx, MfdDataAirportState);
    static const char *const titles[] = {"DATABASE ARPTs", "PILOT STORED RWYs"};
    MfdUi *ui = ctx->ui;
    MfdTabsDesc td;
    MfdRect content, clip;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    MfdFieldResult fr;
    int k;

    if (ctx->entered) {
        s->tab = strcmp(ctx->uri->extra, "pilot-stored") == 0 ? 1 : 0;
        s->selected = -1;
    }
    mfd_tabs_desc_init(&td);
    td.slant_deg = 25.0f;
    td.content_h = 708.0f;
    k = mfd_top_tabs(ui, AID(1), ctx->body, titles, 2, s->tab, &td, &content, &clip);
    if (k >= 0) s->tab = k;
    if (s->tab == 0 && mfd_button(ui, AID(2), mfd_rect(0.0f, 910.0f, 145.0f, 40.0f), "RETURN", 0))
        mfd_navigate_back(ctx);
    mfd_draw_clip_push(ctx->dl, clip);
    if (s->tab == 1) {
        /* [PORT] wording and button position (spec 03 section 6.2) */
        mfd_text(ui, 384.0f, mfd_baseline(231.0f, 20.0f), MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE,
                 "NO PILOT STORED RWY");
        mfd_button(ui, AID(3), mfd_rect(584.0f, 798.0f, 170.0f, 60.0f), "NEW RWY", MFD_BTN_DISABLED);
        mfd_draw_clip_pop(ctx->dl);
        return;
    }
    mfd_text(ui, 185.5f, mfd_baseline(214.5f, 20.0f), MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "ARPT IDENT");
    mfd_field_desc_init(&fd, MFD_FMT_TXT_AIRPORT);
    fd.flags = MFD_FIELD_MANDATORY | MFD_FIELD_NOT_CLEARABLE;
    memset(&fv, 0, sizeof(fv));
    fv.has_value = s->icao[0] != '\0';
    fv.text = s->icao;
    fr = mfd_input_field(ui, AID(4), 340.5f, 206.0f, &fd, &fv, &out);
    if (fr == MFD_FIELD_COMMITTED) {
        snprintf(s->icao, sizeof(s->icao), "%.5s", out.str);
        s->found = 0;
        s->selected = -1;
        s->lookup = strlen(s->icao) == 4;
    }
    if (s->lookup) {
        int r = (ctx->host && ctx->host->airport_lookup) ? ctx->host->airport_lookup(ctx->host->ctx, s->icao, &s->apt)
                                                          : 0;
        if (r != -1) {
            s->lookup = 0;
            s->found = r == 1;
            if (!s->found) { /* [PORT] NOT IN DATABASE, hide name and list */
                MfdFmsError err;
                mfd_err_set_plain(&err, MFD_ERR_NOT_IN_DATABASE);
                mfd_ui_error(ui, &err, 0);
            }
        }
    }
    if (s->found || s->lookup) {
        mfd_draw_bevel(ctx->dl, mfd_rect(14.0f, 363.0f, 740.0f, 450.0f), 1.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_SOLID);
    }
    if (s->found) {
        char t[48];
        mfd_text(ui, 384.0f, mfd_baseline(270.5f, 30.0f), MFD_ANCHOR_CENTER, 30.0f, MFD_COL_GREEN, s->apt.name);
        coord_text(s->apt.lat, s->apt.lon, t, sizeof(t));
        mfd_text(ui, 384.0f, mfd_baseline(305.5f, 30.0f), MFD_ANCHOR_CENTER, 30.0f, MFD_COL_GREEN, t);
        mfd_draw_clip_push(ctx->dl, mfd_rect(15.0f, 364.0f, 738.0f, 448.0f));
        if (s->selected >= 0 && s->selected < s->apt.runway_count) runway_detail(ctx, s);
        else runway_list(ctx, s);
        mfd_draw_clip_pop(ctx->dl);
    }
    mfd_draw_clip_pop(ctx->dl);
}

void a380x_mfd_page_data_register(void)
{
    mfd_register_page("fms/data/status", MFD_PAGE_ID_DATA_STATUS, "STATUS", data_status_page,
                      MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY | MFD_PAGE_PENALTY);
    mfd_register_page("fms/data/airport", MFD_PAGE_ID_DATA_AIRPORT, "AIRPORT", data_airport_page,
                      MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY);
}
