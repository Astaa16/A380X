/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD POSITION/MONITOR, POSITION/IRS and POSITION/NAVAIDS pages. Translation of the pinned
 * FBW pages/FMS/POSITION/MfdFmsPosition{Monitor,Irs,Navaids}.tsx (+ .scss), spec 06 sections 1-3.
 *
 * Geometry: the FBW constants evaluated with border-box sizing (InputField 37 px, Button width:W = W
 * px, explicit heights as given). Under border-box every page fits the 812 px body without the
 * content-box "fit adjustments" of spec 06 (open question 1), so the natural FBW positions are used.
 * Positions below are derived in comments where they are not literal FBW numbers.
 */
#include "a380x_mfd_page_position.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NBSP "\xC2\xA0"
#define DEG "\xC2\xB0"
#define MID(n) MFD_ID(MFD_PAGE_ID_POSITION_MONITOR, (n))
#define IID(n) MFD_ID(MFD_PAGE_ID_POSITION_IRS, (n))
#define NID(n) MFD_ID(MFD_PAGE_ID_POSITION_NAVAIDS, (n))

#define POS_NM_PER_RAD 3440.065
#define POS_DEG2RAD 0.017453292519943295

/* noPositionAvailableText (MFD/shared/utils.ts:54), 18 characters. */
static const char pos_no_position[] = "--" DEG "--.--/---" DEG "--.--";

static A380XMfdPositionHost pos_host;
static int pos_host_valid;

/* Shared FMS data (FmgcData.positionMonitorFix, navaid deselection list). */
static struct {
    int valid;
    char ident[8];
    double lat, lon;
} pos_fix;
static char pos_desel[6][A380X_POS_NAVAID_IDENT];

void a380x_mfd_page_position_set_host(const A380XMfdPositionHost *host)
{
    if (host) {
        pos_host = *host;
        pos_host_valid = 1;
    } else {
        memset(&pos_host, 0, sizeof(pos_host));
        pos_host_valid = 0;
    }
}

int a380x_mfd_position_monitor_fix(char ident[8], double *lat, double *lon)
{
    if (!pos_fix.valid) return 0;
    if (ident) snprintf(ident, 8, "%s", pos_fix.ident);
    if (lat) *lat = pos_fix.lat;
    if (lon) *lon = pos_fix.lon;
    return 1;
}

void a380x_mfd_position_set_monitor_fix(const char *ident_or_null, double lat, double lon)
{
    if (!ident_or_null || !*ident_or_null) {
        memset(&pos_fix, 0, sizeof(pos_fix));
        return;
    }
    pos_fix.valid = 1;
    snprintf(pos_fix.ident, sizeof(pos_fix.ident), "%.7s", ident_or_null);
    pos_fix.lat = lat;
    pos_fix.lon = lon;
}

const char *a380x_mfd_position_deselected(int i)
{
    if (i < 0 || i >= 6) return "";
    return pos_desel[i];
}

void a380x_mfd_page_position_reset(void)
{
    memset(&pos_fix, 0, sizeof(pos_fix));
    memset(pos_desel, 0, sizeof(pos_desel));
}

/* ======================================================================================== */
/* Helpers                                                                                  */
/* ======================================================================================== */

static double pos_rd(const MfdPageContext *ctx, unsigned int key)
{
    if (!ctx->host || !ctx->host->read_value) return NAN;
    return ctx->host->read_value(ctx->host->ctx, ctx->side, key);
}

static int pos_flag(const MfdPageContext *ctx, unsigned int key)
{
    double v = pos_rd(ctx, key);
    return !isnan(v) && v != 0.0;
}

static double pos_ir(const MfdPageContext *ctx, int unit, int field)
{
    return pos_rd(ctx, A380X_POS_K_IR(unit, field));
}

static void pos_cmd(const MfdPageContext *ctx, unsigned int cmd, double value)
{
    if (ctx->host && ctx->host->command) ctx->host->command(ctx->host->ctx, ctx->side, cmd, value);
}

static void pos_error(MfdUi *ui, MfdFmsErrorType t)
{
    MfdFmsError err;
    mfd_err_set_plain(&err, t);
    mfd_ui_error(ui, &err, 0);
}

/* coordinateToString(lat, lon, false) with [PORT] true tenths of a minute (spec 06 0.4). */
static void pos_coord(double lat, double lon, char *buf, size_t n)
{
    double alat = fabs(lat), alon = fabs(lon);
    int dlat = (int)alat, dlon = (int)alon;
    double mlat = (alat - dlat) * 60.0, mlon = (alon - dlon) * 60.0;
    int mi_lat = (int)mlat, mi_lon = (int)mlon;
    int t_lat = (int)floor((mlat - mi_lat) * 10.0), t_lon = (int)floor((mlon - mi_lon) * 10.0);
    if (t_lat > 9) t_lat = 9;
    if (t_lon > 9) t_lon = 9;
    snprintf(buf, n, "%02u" DEG "%02u.%u%c/%03u" DEG "%02u.%u%c", (unsigned)dlat % 100u, (unsigned)mi_lat % 60u,
             (unsigned)t_lat % 10u, lat >= 0 ? 'N' : 'S', (unsigned)dlon % 1000u, (unsigned)mi_lon % 60u,
             (unsigned)t_lon % 10u, lon >= 0 ? 'E' : 'W');
}

static double pos_gc_nm(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * POS_DEG2RAD, p2 = lat2 * POS_DEG2RAD, dl = (lon2 - lon1) * POS_DEG2RAD;
    double a = sin((p2 - p1) * 0.5), b = sin(dl * 0.5);
    double h = a * a + cos(p1) * cos(p2) * b * b;
    if (h > 1.0) h = 1.0;
    return 2.0 * asin(sqrt(h)) * POS_NM_PER_RAD;
}

static double pos_gc_course(double lat1, double lon1, double lat2, double lon2)
{
    double p1 = lat1 * POS_DEG2RAD, p2 = lat2 * POS_DEG2RAD, dl = (lon2 - lon1) * POS_DEG2RAD;
    double y = sin(dl) * cos(p2), x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    double c = atan2(y, x) / POS_DEG2RAD;
    return fmod(c + 360.0, 360.0);
}

/* JS padEnd with NBSP to `width` code points. */
static void pos_pad_end_nbsp(char *buf, size_t n, int width)
{
    int count = mfd_utf8_count(buf);
    size_t len = strlen(buf);
    while (count < width && len + 2 < n) {
        buf[len++] = '\xC2';
        buf[len++] = '\xA0';
        buf[len] = '\0';
        ++count;
    }
}

/* Button whose label row (lines + optional right "*" after `gap` px) is centred in the content box
 * (FBW: label <div style="display:flex; justify-content:space-between"><span margin-right:gap>..</span>
 * <span>*</span></div> centred by the .mfd-button flex box). gap is the span margin-right. */
static int pos_row_button(MfdUi *ui, MfdId id, MfdRect r, const char *label, const char *star, float gap,
                          uint32_t flags)
{
    MfdButtonDesc bd;
    float maxw = 0.0f, cx0 = r.x + 14.0f, cx1 = r.x + r.w - 14.0f, row_w, x0, lab_cx, pad;
    int lines = 1, res;
    const char *p, *start = label;
    for (p = label;; ++p) {
        if (*p == '\n' || *p == '\0') {
            float w = mfd_text_widthn(start, (size_t)(p - start), MFD_FS_BUTTON);
            if (w > maxw) maxw = w;
            if (*p == '\0') break;
            ++lines;
            start = p + 1;
        }
    }
    row_w = maxw + gap + (star ? mfd_text_width(star, MFD_FS_BUTTON) : 0.0f);
    x0 = (cx0 + cx1 - row_w) * 0.5f;
    lab_cx = x0 + maxw * 0.5f;
    /* the widget centres the lines in [r.x + 14, r.x + r.w - 2 - pad_right] */
    pad = r.x + r.w - 2.0f - (2.0f * lab_cx - cx0);
    mfd_button_desc_init(&bd, label);
    bd.flags = flags;
    bd.pad_right = pad > 0.01f ? pad : 0.01f;
    res = mfd_button_ex(ui, id, r, &bd);
    if (star && !(flags & MFD_BTN_HIDDEN)) {
        float top = r.y + 11.0f + (r.h - 18.0f - 22.0f * (float)lines) * 0.5f;
        float b = top + (22.0f * (float)lines - 22.0f) * 0.5f + MFD_FONT_BASELINE * MFD_FS_BUTTON;
        mfd_text(ui, x0 + maxw + gap, b, MFD_ANCHOR_LEFT, MFD_FS_BUTTON,
                 (flags & MFD_BTN_DISABLED) ? MFD_COL_DARK_GREY : MFD_COL_WHITE, star);
    }
    return res == MFD_BUTTON_CLICKED;
}

/* 1-px outset rule .mfd-pos-monitor-line (border 1 px outset #c4c6cf, height 1 -> 2 px border-box). */
static void pos_line(MfdPageContext *ctx, float x, float y, float w)
{
    mfd_draw_bevel(ctx->dl, mfd_rect(x, y, w, 2.0f), 1.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_RAISED);
}

/* CSS `2px outset lightgrey`: top/left #d3d3d3, bottom/right darkened (#7f7f7f ~ CSS grey). */
static void pos_outset_lightgrey(MfdDrawList *dl, MfdRect r, float w)
{
    MfdPoint q[4];
    float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    mfd_draw_fill_rect(dl, mfd_rect(x0, y0, r.w, w), MFD_COL_CSS_LIGHTGREY);
    mfd_draw_fill_rect(dl, mfd_rect(x0, y0, w, r.h), MFD_COL_CSS_LIGHTGREY);
    q[0].x = x0; q[0].y = y1; q[1].x = x0 + w; q[1].y = y1 - w; q[2].x = x1 - w; q[2].y = y1 - w; q[3].x = x1; q[3].y = y1;
    mfd_draw_polygon(dl, q, 4, MFD_COL_CSS_GREY);
    q[0].x = x1; q[0].y = y0; q[1].x = x1; q[1].y = y1; q[2].x = x1 - w; q[2].y = y1 - w; q[3].x = x1 - w; q[3].y = y0 + w;
    mfd_draw_polygon(dl, q, 4, MFD_COL_CSS_GREY);
}

static void pos_read_navaids(const MfdPageContext *ctx, A380XMfdNavaidData *nav)
{
    int i;
    memset(nav, 0, sizeof(*nav));
    for (i = 0; i < 2; ++i) {
        nav->vor[i].freq_mhz = NAN;
        nav->vor[i].course_deg = NAN;
        nav->vor[i].slope_deg = NAN;
    }
    nav->ls.freq_mhz = NAN;
    nav->ls.course_deg = NAN;
    nav->ls.slope_deg = NAN;
    for (i = 0; i < 3; ++i) nav->selected[i].freq_mhz = NAN;
    if (pos_host_valid && pos_host.read_navaids) pos_host.read_navaids(pos_host.ctx, ctx->side, nav);
}

static int pos_fm_position(const MfdPageContext *ctx, double *lat, double *lon)
{
    *lat = pos_rd(ctx, A380X_POS_K_FM_LAT);
    *lon = pos_rd(ctx, A380X_POS_K_FM_LON);
    return pos_flag(ctx, A380X_POS_K_FM_VALID) && !isnan(*lat) && !isnan(*lon);
}

/* ======================================================================================== */
/* POSITION/MONITOR (spec 06 section 1)                                                     */
/* ======================================================================================== */

static void pos_monitor_update(MfdPageContext *ctx, MfdPosMonitorState *s)
{
    double lat, lon, epu, rnp;
    int fm = pos_fm_position(ctx, &lat, &lon), u, nmix = 0;
    double mix_lat = 0.0, mix_lon = 0.0;

    s->nav_lost = !(pos_flag(ctx, A380X_POS_K_GPS_PRIMARY) && fm);
    epu = pos_rd(ctx, A380X_POS_K_EPU_NM);
    rnp = pos_rd(ctx, A380X_POS_K_RNP_NM);
    s->epu_valid = fm && !isnan(epu) && isfinite(epu);
    if (s->epu_valid) mfd_js_to_fixed(epu, 2, s->epu, sizeof(s->epu));
    else snprintf(s->epu, sizeof(s->epu), "-.--");
    pos_pad_end_nbsp(s->epu, sizeof(s->epu), 5);
    s->accuracy_high = s->epu_valid && !isnan(rnp) && epu <= rnp;

    if (fm) pos_coord(lat, lon, s->pos, sizeof(s->pos));
    else snprintf(s->pos, sizeof(s->pos), "%s", pos_no_position);

    for (u = 0; u < 3; ++u) {
        double ilat = pos_ir(ctx, u, A380X_POS_IR_LAT), ilon = pos_ir(ctx, u, A380X_POS_IR_LON);
        int valid = pos_ir(ctx, u, A380X_POS_IR_NAV_VALID) > 0.5 && !isnan(ilat) && !isnan(ilon);
        if (!s->frozen) {
            if (valid) pos_coord(ilat, ilon, s->ir[u], sizeof(s->ir[u]));
            else snprintf(s->ir[u], sizeof(s->ir[u]), "%s", pos_no_position);
        }
        /* deviations keep updating while frozen (FBW) */
        if (valid && fm) mfd_js_to_fixed(pos_gc_nm(ilat, ilon, lat, lon), 1, s->dev[u], sizeof(s->dev[u]));
        else snprintf(s->dev[u], sizeof(s->dev[u]), "--.-");
        if (valid) {
            mix_lat += ilat;
            mix_lon += ilon;
            ++nmix;
        }
    }
    if (!s->frozen) {
        static const unsigned int gps_key[2] = {A380X_POS_K_GPS1_VALID, A380X_POS_K_GPS2_VALID};
        A380XMfdNavaidData nav;
        double slat = pos_rd(ctx, A380X_POS_K_FM_LAT), slon = pos_rd(ctx, A380X_POS_K_FM_LON);
        int g;
        for (g = 0; g < 2; ++g) {
            if (pos_flag(ctx, gps_key[g]) && !isnan(slat) && !isnan(slon))
                pos_coord(slat, slon, s->gnss[g], sizeof(s->gnss[g]));
            else snprintf(s->gnss[g], sizeof(s->gnss[g]), "%s", pos_no_position);
        }
        if (nmix > 0) pos_coord(mix_lat / nmix, mix_lon / nmix, s->mixirs, sizeof(s->mixirs));
        else snprintf(s->mixirs, sizeof(s->mixirs), "%s", pos_no_position);
        /* [PORT] RADIO = NAVAIDS RADIO POSITION when a radio position exists */
        pos_read_navaids(ctx, &nav);
        if (fm && nav.selected[0].ident[0]) pos_coord(lat, lon, s->radio, sizeof(s->radio));
        else snprintf(s->radio, sizeof(s->radio), "%s", pos_no_position);
    }

    /* BRG / DIST TO ([PORT] quirk Q3: validity tested on the fix and the FM position) */
    s->brg_valid = pos_fix.valid && fm;
    if (s->brg_valid) {
        double dist = pos_gc_nm(lat, lon, pos_fix.lat, pos_fix.lon);
        double magvar = pos_rd(ctx, A380X_POS_K_MAGVAR);
        double brg = pos_gc_course(lat, lon, pos_fix.lat, pos_fix.lon) - (isnan(magvar) ? 0.0 : magvar);
        long b = lround(fmod(fmod(brg, 360.0) + 360.0, 360.0)) % 360;
        snprintf(s->brg, sizeof(s->brg), "%03ld", b);
        mfd_js_to_fixed(dist, dist > 9999.0 ? 0 : 1, s->dist, sizeof(s->dist));
        pos_pad_end_nbsp(s->dist, sizeof(s->dist), 6);
    } else {
        snprintf(s->brg, sizeof(s->brg), "---");
        snprintf(s->dist, sizeof(s->dist), "----.-");
    }
    s->have_data = 1;
}

static void pos_monitor_toggle_sensors(MfdPosMonitorState *s)
{
    s->sensors_visible = !s->sensors_visible;
    s->sensors_by_pilot = s->sensors_visible;
    if (!s->sensors_by_pilot) s->frozen = 0;
}

static void pos_monitor_toggle_frozen(MfdPageContext *ctx, MfdPosMonitorState *s)
{
    s->frozen = !s->frozen;
    if (s->frozen) {
        double utc = pos_rd(ctx, A380X_POS_K_UTC_S);
        s->frozen_hhmm = isnan(utc) ? -1 : (int)(fmod(utc, 86400.0) / 60.0);
        if (!s->sensors_visible) s->sensors_visible = 1;
    } else if (!s->sensors_by_pilot) {
        s->sensors_visible = 0;
    }
}

static void pos_monitor_page(MfdPageContext *ctx)
{
    MfdPosMonitorState *s = MFD_PAGE_STATE(ctx, MfdPosMonitorState);
    MfdUi *ui = ctx->ui;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    MfdFieldResult fr;
    double rnp;
    char t[64];
    int u;
    float adv24 = mfd_char_advance(24.0f), x;
    static const char *const ir_labels[3] = {NBSP "IRS1", NBSP "IRS2", NBSP "IRS3"};

    if (ctx->entered) {
        s->frozen_hhmm = -1;
        s->next_update = 0.0;
    }
    if (!s->have_data || ctx->now >= s->next_update) {
        pos_monitor_update(ctx, s);
        s->next_update = ctx->now + 0.5;
    }

    /* Row 1 (mfd-pos-top-row 138..196, margin-left 86): NAV PRIMARY (margin-top 13, padding 7). */
    mfd_text(ui, 93.0f, 182.5f, MFD_ANCHOR_LEFT, 30.0f, s->nav_lost ? MFD_COL_AMBER : MFD_COL_GREEN,
             s->nav_lost ? "NAV PRIMARY LOST" : "NAV PRIMARY");
    /* RNP container (margin-right 92, margin-top 7): label + field 155 px, content top 152. */
    mfd_text(ui, 453.9f, 177.7f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "RNP");
    rnp = pos_rd(ctx, A380X_POS_K_RNP_NM);
    mfd_field_desc_init(&fd, MFD_FMT_RNP);
    fd.width = 155.0f;
    fd.flags = MFD_FIELD_BIG_UNIT;
    memset(&fv, 0, sizeof(fv));
    fv.has_value = !isnan(rnp);
    fv.num = rnp;
    fv.is_computed = !pos_flag(ctx, A380X_POS_K_RNP_PILOT);
    fr = mfd_input_field(ui, MID(1), 514.0f, 152.0f, &fd, &fv, &out);
    if (fr == MFD_FIELD_COMMITTED) pos_cmd(ctx, A380X_POS_C_SET_RNP, out.num);
    else if (fr == MFD_FIELD_CLEARED) pos_cmd(ctx, A380X_POS_C_SET_RNP, NAN);

    /* Row 2 (200..244): ACCURACY (only when NAV PRIMARY LOST), EPU (margin-right 95). */
    if (s->nav_lost) {
        mfd_text(ui, 93.0f, 229.2f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "ACCURACY");
        mfd_text(ui, 93.0f + 8.0f * adv24 + 15.0f, 231.0f, MFD_ANCHOR_LEFT, 30.0f,
                 s->accuracy_high ? MFD_COL_GREEN : MFD_COL_AMBER, s->accuracy_high ? "HIGH" : "LOW");
    }
    mfd_text(ui, 453.7f, 229.2f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "EPU");
    mfd_text(ui, 535.8f, 231.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, s->epu);
    if (s->epu_valid) mfd_text(ui, 635.8f, 229.2f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_DARK_BLUE, "NM");

    /* POS1 (248..292, margin-bottom 14.5) and POS2 (306.5..350.5); POS2 = POS1 [FBW]. */
    mfd_text(ui, 16.0f, 277.2f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, NBSP "POS1");
    mfd_text(ui, 16.0f + 5.0f * adv24 + 15.0f, 279.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, s->pos);
    mfd_text(ui, 16.0f, 335.7f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, NBSP "POS2");
    mfd_text(ui, 16.0f + 5.0f * adv24 + 15.0f, 337.5f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, s->pos);
    pos_line(ctx, 5.0f, 353.5f, 744.96f);

    /* Sensor table (363.5..692.5), visibility only. */
    if (s->sensors_visible) {
        float lx = 17.0f, vx = 17.0f + 5.0f * adv24 + 15.0f;
        mfd_text(ui, lx, 392.7f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "GNSS1");
        mfd_text(ui, vx, 399.5f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, s->gnss[0]); /* FBW top:5px */
        mfd_text(ui, lx, 435.7f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "GNSS2");
        mfd_text(ui, vx, 437.5f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, s->gnss[1]);
        pos_line(ctx, 5.0f, 448.5f, 445.0f);
        for (u = 0; u < 3; ++u) {
            mfd_text(ui, lx, 487.7f + 43.0f * (float)u, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, ir_labels[u]);
            mfd_text(ui, vx, 489.5f + 43.0f * (float)u, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, s->ir[u]);
        }
        pos_line(ctx, 5.0f, 591.5f, 445.0f);
        mfd_text(ui, lx, 630.7f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "RADIO");
        mfd_text(ui, vx, 632.5f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, s->radio);
        mfd_text(ui, 3.0f, 677.7f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, "MIXIRS");
        mfd_text(ui, 3.0f + 6.0f * adv24 + 15.0f, 679.5f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, s->mixirs);
    }

    /* Deviation column (x 450 + margin 19; margin-top 27). GPS DESELECTED never visible [FBW-NA]. */
    snprintf(t, sizeof(t), "DEVIATION FROM %s", ctx->side == MFD_CAPT ? "POS1" : "POS2");
    mfd_text(ui, 469.0f, 456.7f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, t);
    for (u = 0; u < 3; ++u) {
        float vx = 469.0f + 4.0f * adv24 + 64.0f, b = 495.5f + 37.0f * (float)u;
        if (!s->sensors_visible)
            mfd_text(ui, 469.0f, b - 1.8f, MFD_ANCHOR_LEFT, 24.0f, MFD_COL_WHITE, ir_labels[u] + 2);
        mfd_text(ui, vx, b, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, s->dev[u]);
        if (strcmp(s->dev[u], "--.-") != 0)
            mfd_text(ui, vx + mfd_text_width(s->dev[u], 30.0f) + 6.0f, b - 1.8f, MFD_ANCHOR_LEFT, 24.0f,
                     MFD_COL_DARK_BLUE, "NM");
    }

    /* Frozen label: FBW position:fixed left 496 top 632 ([PORT] Q5: on its own side). */
    if (s->frozen) {
        float cx = 496.0f + 15.0f * adv24 * 0.5f;
        mfd_text(ui, cx, 651.2f, MFD_ANCHOR_CENTER, 24.0f, MFD_COL_WHITE, "POS DATA FROZEN");
        if (s->frozen_hhmm >= 0) snprintf(t, sizeof(t), " AT %02d:%02d", s->frozen_hhmm / 60, s->frozen_hhmm % 60);
        else snprintf(t, sizeof(t), " AT --:--");
        mfd_text(ui, cx, 675.2f, MFD_ANCHOR_CENTER, 24.0f, MFD_COL_WHITE, t);
    }

    /* Buttons row 692.5..750.5 (margin-bottom 19). */
    if (pos_row_button(ui, MID(2), mfd_rect(95.0f, 692.5f, 219.0f, 58.0f),
                       s->sensors_visible ? "HIDE\nPOS SENSORS" : "DISPLAY\nPOS SENSORS", NULL, 25.0f,
                       s->sensors_visible ? MFD_BTN_SELECTED : 0u))
        pos_monitor_toggle_sensors(s);
    if (pos_row_button(ui, MID(3), mfd_rect(496.0f, 692.5f, 212.0f, 58.0f),
                       s->frozen ? "UNFREEZE\nPOS DATA" : "FREEZE\nPOS DATA", "*", 10.0f,
                       s->frozen ? MFD_BTN_SELECTED : 0u)) {
        pos_monitor_toggle_frozen(ctx, s);
        s->next_update = 0.0;
    }
    pos_line(ctx, 5.0f, 769.5f, 744.96f);

    /* POSITION UPDATE (margin-top 18, disabled [FBW-NA]) and BRG / DIST TO. */
    mfd_button(ui, MID(4), mfd_rect(0.0f, 797.5f, 138.0f, 59.0f), "POSITION\nUPDATE", MFD_BTN_DISABLED);
    mfd_text(ui, 589.0f, 812.7f, MFD_ANCHOR_RIGHT, 22.0f, MFD_COL_WHITE, "BRG / DIST TO");
    mfd_field_desc_init(&fd, MFD_FMT_TXT_FIX);
    memset(&fv, 0, sizeof(fv));
    fv.has_value = pos_fix.valid;
    fv.text = pos_fix.valid ? pos_fix.ident : NULL;
    fv.is_computed = !pos_fix.valid;
    fr = mfd_input_field(ui, MID(5), 599.0f, 787.5f, &fd, &fv, &out); /* auto width 146 ([QUIRK] Q4) */
    if (fr == MFD_FIELD_COMMITTED) {
        double flat = NAN, flon = NAN, rlat, rlon;
        int fm = pos_fm_position(ctx, &rlat, &rlon);
        int found = pos_host_valid && pos_host.fix_lookup &&
                    pos_host.fix_lookup(pos_host.ctx, out.str, fm ? rlat : NAN, fm ? rlon : NAN, &flat, &flon);
        if (found) a380x_mfd_position_set_monitor_fix(out.str, flat, flon);
        else pos_error(ui, MFD_ERR_NOT_IN_DATABASE);
        s->next_update = 0.0;
    } else if (fr == MFD_FIELD_CLEARED) {
        a380x_mfd_position_set_monitor_fix(NULL, 0.0, 0.0);
        s->next_update = 0.0;
    }
    /* bearing / distance line (fr width 417 at x 351, margin-top 5): V25 baseline 852.5 */
    x = 351.0f;
    mfd_text(ui, x, 852.5f, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_GREEN, s->brg);
    x += mfd_text_width(s->brg, 25.0f) + 6.0f;
    mfd_text(ui, x, 852.5f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, s->brg_valid ? NBSP DEG : NBSP NBSP);
    x += 2.0f * mfd_char_advance(20.0f) + 20.0f;
    mfd_text(ui, x, 852.5f, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_GREEN, "/");
    x += mfd_char_advance(25.0f) + 14.0f;
    mfd_text(ui, x, 852.5f, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_GREEN, s->dist);
    x += mfd_text_width(s->dist, 25.0f) + 6.0f;
    mfd_text(ui, x, 852.5f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, s->brg_valid ? NBSP "NM" : NBSP NBSP NBSP);
    pos_line(ctx, 5.0f, 891.5f, 744.96f);

    /* Bottom row 907..950 (height 43). */
    if (mfd_button(ui, MID(6), mfd_rect(0.0f, 907.0f, 150.0f, 43.0f), "RETURN",
                   strcmp(ctx->uri->extra, "withReturn") == 0 ? 0u : MFD_BTN_HIDDEN))
        mfd_navigate_back(ctx);
    if (mfd_button(ui, MID(7), mfd_rect(335.0f, 907.0f, 149.0f, 43.0f), "NAVAIDS", 0))
        mfd_navigate(ctx, "fms/position/navaids/nav");
    mfd_button(ui, MID(8), mfd_rect(489.0f, 907.0f, 133.0f, 43.0f), "GNSS", MFD_BTN_DISABLED);
    if (mfd_button(ui, MID(9), mfd_rect(627.0f, 907.0f, 136.0f, 43.0f), "IRS", 0))
        mfd_navigate(ctx, "fms/position/irs");
}

/* ======================================================================================== */
/* POSITION/IRS (spec 06 section 2)                                                         */
/* ======================================================================================== */

static void pos_irs_update(MfdPageContext *ctx, MfdPosIrsState *s)
{
    int u, all_aligned = 1;
    double lat, lon, gs_any = NAN;
    int on_ground = pos_flag(ctx, A380X_POS_K_ON_GROUND);
    for (u = 0; u < 3; ++u) {
        int mode = (int)lround(isnan(pos_ir(ctx, u, A380X_POS_IR_MODE)) ? 0.0 : pos_ir(ctx, u, A380X_POS_IR_MODE));
        int aligning = pos_ir(ctx, u, A380X_POS_IR_ALIGNING) > 0.5;
        int nav_valid = pos_ir(ctx, u, A380X_POS_IR_NAV_VALID) > 0.5;
        int fault = pos_ir(ctx, u, A380X_POS_IR_FAULT) > 0.5;
        int hdg_req = pos_ir(ctx, u, A380X_POS_IR_HDG_REQUIRED) > 0.5;
        int locked = pos_ir(ctx, u, A380X_POS_IR_NAV_LOCKED) > 0.5;
        double gs = pos_ir(ctx, u, A380X_POS_IR_GS_KT);
        s->col3[u][0] = '\0';
        s->col4[u][0] = '\0';
        if (!isnan(gs)) gs_any = gs;
        if (mode == 0) {
            snprintf(s->status[u], sizeof(s->status[u]), "OFF"); /* [PORT] Q7: OFF clears col3/col4 */
            all_aligned = 0;
            continue;
        }
        if (fault) snprintf(s->status[u], sizeof(s->status[u]), "INVALID");
        else if (aligning) snprintf(s->status[u], sizeof(s->status[u]), "ALIGN");
        else if (mode == 2) snprintf(s->status[u], sizeof(s->status[u]), "ATT");
        else if (mode == 1 && nav_valid) snprintf(s->status[u], sizeof(s->status[u]), "NAV");
        else snprintf(s->status[u], sizeof(s->status[u]), "INVALID");
        if (strcmp(s->status[u], "NAV") != 0 && strcmp(s->status[u], "ATT") != 0) all_aligned = 0;
        if (aligning && !fault) {
            double rem = pos_ir(ctx, u, A380X_POS_IR_ALIGN_REMAIN_S);
            int m = isnan(rem) ? 0 : (int)ceil(rem / 60.0);
            if (m > 7) snprintf(s->col3[u], sizeof(s->col3[u]), "AVAIL IN > 7 MIN");
            else if (m >= 1) snprintf(s->col3[u], sizeof(s->col3[u]), "AVAIL IN %d MIN", m);
        }
        if (fault) snprintf(s->col4[u], sizeof(s->col4[u]), "IR FAULT");
        else if (hdg_req) snprintf(s->col4[u], sizeof(s->col4[u]), "ENTER HDG");
        else if (mode == 1 && on_ground && !nav_valid && !locked && !isnan(gs) && gs > 0.5)
            snprintf(s->col4[u], sizeof(s->col4[u]), "EXCESS MOTION");
    }
    (void)gs_any;
    s->aligned = all_aligned;
    if (pos_fm_position(ctx, &lat, &lon)) pos_coord(lat, lon, s->banner_pos, sizeof(s->banner_pos));
    else snprintf(s->banner_pos, sizeof(s->banner_pos), "%s", pos_no_position); /* [PORT] Q6 */
}

static void pos_value_unit_right(MfdUi *ui, float right, float row, const char *value, const char *unit)
{
    float uw = mfd_text_width(unit, 20.0f);
    mfd_text(ui, right - uw, row + 28.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, unit);
    mfd_text(ui, right - uw - 6.0f, row + 31.0f, MFD_ANCHOR_RIGHT, 30.0f, MFD_COL_GREEN, value);
}

static void pos_irs_data_block(MfdPageContext *ctx, MfdPosIrsState *s)
{
    MfdUi *ui = ctx->ui;
    int u = s->shown - 1;
    int nav_valid = pos_ir(ctx, u, A380X_POS_IR_NAV_VALID) > 0.5;
    int hdg_valid = pos_ir(ctx, u, A380X_POS_IR_HDG_VALID) > 0.5;
    double v, lat, lon;
    char t[40], t2[16];
    float x, w;
    MfdButtonDesc bd;
    static const float row_y[3] = {649.0f, 693.0f, 737.0f};
    /* flex 1 / 1.5 / 1 / 1 of the 734 px row: unit 163.11 */
    const float lab1 = 180.11f - 7.0f, val1 = 424.78f - 7.0f, lab2 = 587.89f - 7.0f, val2 = 751.0f - 7.0f;

    pos_outset_lightgrey(ctx->dl, mfd_rect(0.0f, 568.0f, 768.0f, 335.0f), 2.0f);
    mfd_button_desc_init(&bd, "FREEZE\nALL IRS");
    bd.flags = MFD_BTN_DISABLED; /* [FBW-NA] */
    mfd_button_ex(ui, IID(20), mfd_rect(17.0f, 572.0f, 0.0f, 62.0f), &bd);

    /* POSITION (align-self flex-end: bottom at 634) */
    lat = pos_ir(ctx, u, A380X_POS_IR_LAT);
    lon = pos_ir(ctx, u, A380X_POS_IR_LON);
    if (nav_valid && !isnan(lat) && !isnan(lon)) pos_coord(lat, lon, t, sizeof(t));
    else snprintf(t, sizeof(t), "%s", pos_no_position);
    w = mfd_text_width(t, 30.0f);
    x = 751.0f - w - 20.0f - mfd_text_width("POSITION", 20.0f);
    mfd_text(ui, x, 628.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "POSITION");
    mfd_text(ui, 751.0f - w, 628.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, t);

    /* row 1: T.TRK / T.HDG */
    mfd_text(ui, lab1, row_y[0] + 28.0f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "T.TRK");
    v = pos_rd(ctx, A380X_POS_K_TRUE_TRACK);
    if (nav_valid && !isnan(v)) mfd_js_to_fixed(fmod(v + 360.0, 360.0), 1, t, sizeof(t));
    else snprintf(t, sizeof(t), "---.-");
    pos_value_unit_right(ui, val1, row_y[0], t, DEG "T");
    mfd_text(ui, lab2, row_y[0] + 28.0f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "T.HDG");
    v = pos_rd(ctx, A380X_POS_K_TRUE_HDG);
    if (hdg_valid && !isnan(v)) mfd_js_to_fixed(fmod(v + 360.0, 360.0), 1, t, sizeof(t));
    else snprintf(t, sizeof(t), "---.-");
    pos_value_unit_right(ui, val2, row_y[0], t, DEG "T");

    /* row 2: GND SPD / MAG HDG */
    mfd_text(ui, lab1, row_y[1] + 28.0f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "GND SPD");
    v = pos_ir(ctx, u, A380X_POS_IR_GS_KT);
    if (nav_valid && !isnan(v)) mfd_js_to_fixed(v, 1, t, sizeof(t));
    else snprintf(t, sizeof(t), "---.-");
    pos_value_unit_right(ui, val1, row_y[1], t, "KT");
    mfd_text(ui, lab2, row_y[1] + 28.0f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "MAG HDG");
    v = pos_ir(ctx, u, A380X_POS_IR_HDG_MAG);
    if (hdg_valid && !isnan(v)) mfd_js_to_fixed(fmod(v + 360.0, 360.0), 1, t, sizeof(t));
    else snprintf(t, sizeof(t), "---.-");
    pos_value_unit_right(ui, val2, row_y[1], t, DEG NBSP);

    /* row 3: T.WIND dir° /spd KT, MAG VAR ([PORT] Q8: E positive) + 2 px rule */
    mfd_text(ui, lab1, row_y[2] + 28.0f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "T.WIND");
    {
        double dir = pos_rd(ctx, A380X_POS_K_WIND_DIR), spd = pos_rd(ctx, A380X_POS_K_WIND_KT);
        float r = val1, uw = mfd_text_width("KT", 20.0f);
        if (nav_valid && !isnan(dir) && !isnan(spd)) {
            snprintf(t, sizeof(t), "%03ld", lround(fmod(dir + 360.0, 360.0)) % 360);
            snprintf(t2, sizeof(t2), "%ld", lround(spd));
        } else {
            snprintf(t, sizeof(t), "---");
            snprintf(t2, sizeof(t2), "---");
        }
        mfd_text(ui, r - uw, row_y[2] + 28.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, "KT");
        r -= uw + 6.0f;
        mfd_text(ui, r, row_y[2] + 31.0f, MFD_ANCHOR_RIGHT, 30.0f, MFD_COL_GREEN, t2);
        r -= mfd_text_width(t2, 30.0f) + mfd_char_advance(20.0f);
        mfd_text(ui, r, row_y[2] + 28.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, DEG);
        r -= 6.0f;
        mfd_text(ui, r, row_y[2] + 31.0f, MFD_ANCHOR_RIGHT, 30.0f, MFD_COL_GREEN, t);
    }
    mfd_text(ui, lab2, row_y[2] + 28.0f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "MAG VAR");
    v = pos_rd(ctx, A380X_POS_K_MAGVAR);
    if (!isnan(v)) {
        mfd_js_to_fixed(fabs(v), 1, t, sizeof(t));
        pos_value_unit_right(ui, val2, row_y[2], t, v > 0.0 ? DEG "E" : DEG "W");
    } else {
        pos_value_unit_right(ui, val2, row_y[2], "--.-", DEG NBSP);
    }
    mfd_draw_fill_rect(ctx->dl, mfd_rect(17.0f, 781.0f, 734.0f, 2.0f), MFD_COL_CSS_LIGHTGREY);

    /* GPIRS POSITION / ACCURACY (fc align flex-end, padding-right 15 -> right edge 736) */
    mfd_text(ui, 213.4f, 826.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "GPIRS POSITION");
    if (pos_fm_position(ctx, &lat, &lon)) pos_coord(lat, lon, t, sizeof(t));
    else snprintf(t, sizeof(t), "%s", pos_no_position);
    mfd_text(ui, 404.0f, 829.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, t);
    mfd_text(ui, 288.7f, 870.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "ACCURACY");
    v = pos_rd(ctx, A380X_POS_K_EPU_NM);
    if (!isnan(v) && isfinite(v)) snprintf(t, sizeof(t), "%ld", lround(v * 6076.12)); /* [PORT] Q8 feet */
    else snprintf(t, sizeof(t), "----");
    mfd_text(ui, 704.0f, 873.0f, MFD_ANCHOR_RIGHT, 30.0f, MFD_COL_GREEN, t);
    mfd_text(ui, 704.0f, 870.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, "FT");
}

static void pos_irs_page(MfdPageContext *ctx)
{
    MfdPosIrsState *s = MFD_PAGE_STATE(ctx, MfdPosIrsState);
    MfdUi *ui = ctx->ui;
    MfdDrawList *dl = ctx->dl;
    MfdButtonDesc bd;
    float w, d1;
    int u, k, hdg_required = 0;
    static const float row_top[3] = {322.0f, 368.0f, 414.0f};
    static const float row_h[3] = {46.0f, 46.0f, 44.0f};
    static const char *const names[3] = {"IRS 1", "IRS 2", "IRS 3"};
    static const char *const btn[3] = {"IRS1", "IRS2", "IRS3"};

    if (ctx->entered) {
        s->shown = 1;
        s->next_update = 0.0;
    }
    if (ctx->now >= s->next_update || ctx->entered) {
        pos_irs_update(ctx, s);
        s->next_update = ctx->now + 1.0;
    }

    /* Banner (fr margin 15: 153..183). div 1 flex:1, div 2 not flex ([QUIRK] Q9) = text width. */
    w = mfd_text_width(s->banner_pos, 30.0f);
    d1 = 738.0f - w;
    mfd_text(ui, 15.0f + d1 * 0.5f, 174.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_AMBER,
             s->aligned ? "IRS ALIGNED ON GPS POS:" : "IRS ALIGNING ON GPS POS:");
    mfd_text(ui, 15.0f + d1, 177.0f, MFD_ANCHOR_LEFT, 30.0f, MFD_COL_GREEN, s->banner_pos);

    mfd_button_desc_init(&bd, "ALIGN ON\nOTHER REF");
    bd.flags = MFD_BTN_DISABLED; /* [FBW-NA] */
    mfd_button_ex(ui, IID(1), mfd_rect(0.0f, 198.0f, 0.0f, 62.0f), &bd);
    mfd_draw_fill_rect(dl, mfd_rect(0.0f, 280.0f, 768.0f, 2.0f), MFD_COL_CSS_LIGHTGREY);

    /* Status table (margin-top 40): 12/20/38/30 % columns, 2 px #868a9c borders. */
    for (u = 0; u < 3; ++u) {
        float y = row_top[u], h = row_h[u];
        mfd_draw_fill_rect(dl, mfd_rect(90.16f, y, 2.0f, h), MFD_COL_DARK_GREY);
        mfd_draw_fill_rect(dl, mfd_rect(243.76f, y, 2.0f, h), MFD_COL_DARK_GREY);
        mfd_draw_fill_rect(dl, mfd_rect(535.6f, y, 2.0f, h), MFD_COL_DARK_GREY);
        if (u < 2) mfd_draw_fill_rect(dl, mfd_rect(0.0f, y + h - 2.0f, 768.0f, 2.0f), MFD_COL_DARK_GREY);
        mfd_text(ui, 45.08f, y + 28.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, names[u]);
        mfd_text(ui, 167.96f, y + 31.0f, MFD_ANCHOR_CENTER, 30.0f, MFD_COL_GREEN, s->status[u]);
        mfd_text(ui, 390.68f, y + 29.5f, MFD_ANCHOR_CENTER, 25.0f, MFD_COL_GREEN, s->col3[u]);
        mfd_text(ui, 652.8f, y + 29.5f, MFD_ANCHOR_CENTER, 25.0f, MFD_COL_GREEN, s->col4[u]);
        if (pos_ir(ctx, u, A380X_POS_IR_HDG_REQUIRED) > 0.5) hdg_required = 1;
    }

    /* SET HDG ([PORT] visible when any IR needs a heading): row 468..505, flex-end. */
    if (hdg_required) {
        MfdFieldDesc fd;
        MfdFieldValue fv;
        MfdValue out;
        mfd_text(ui, 608.0f, 492.5f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, "SET HDG");
        mfd_field_desc_init(&fd, MFD_FMT_HEADING);
        fd.width = 150.0f;
        fd.flags = MFD_FIELD_MANDATORY;
        fd.align = MFD_ANCHOR_RIGHT;
        memset(&fv, 0, sizeof(fv));
        if (mfd_input_field(ui, IID(2), 618.0f, 468.0f, &fd, &fv, &out) == MFD_FIELD_COMMITTED) {
            int accepted = 0;
            for (u = 0; u < 3; ++u) {
                if (pos_ir(ctx, u, A380X_POS_IR_HDG_REQUIRED) > 0.5 && pos_host_valid && pos_host.adirs_enter_heading &&
                    pos_host.adirs_enter_heading(pos_host.ctx, u, out.num))
                    accepted = 1;
            }
            if (!accepted) pos_error(ui, MFD_ERR_NOT_ALLOWED);
            s->next_update = 0.0;
        }
    }

    /* IRS1/2/3 (centred row, 150 + 3 px gaps): 525..565. */
    for (k = 0; k < 3; ++k) {
        if (mfd_button(ui, IID(10 + k), mfd_rect(156.0f + 153.0f * (float)k, 525.0f, 150.0f, 40.0f), btn[k],
                       s->shown == k + 1 ? MFD_BTN_SELECTED : 0u))
            s->shown = s->shown == k + 1 ? 0 : k + 1;
    }
    if (s->shown >= 1 && s->shown <= 3) pos_irs_data_block(ctx, s);

    if (mfd_button(ui, IID(30), mfd_rect(0.0f, 910.0f, 145.0f, 40.0f), "RETURN", 0)) mfd_navigate_back(ctx);
}

/* ======================================================================================== */
/* POSITION/NAVAIDS (spec 06 section 3)                                                     */
/* ======================================================================================== */

static void pos_tune_result(MfdPageContext *ctx, int r, const char *ident)
{
    if (r == A380X_POS_TUNE_NOT_IN_DATABASE) pos_error(ctx->ui, MFD_ERR_NOT_IN_DATABASE);
    else if (r == A380X_POS_TUNE_NOT_ALLOWED) pos_error(ctx->ui, MFD_ERR_NOT_ALLOWED);
    else if (r == A380X_POS_TUNE_DESELECTED && ctx->msgs)
        mfd_msg_add(ctx->msgs, MFD_MSG_XXX_IS_DESELECTED, ident, NULL, NULL, NULL, NULL);
}

static int pos_tune_ident(int rcv, const char *ident)
{
    if (!pos_host_valid || !pos_host.tune_ident) return A380X_POS_TUNE_NOT_IN_DATABASE;
    return pos_host.tune_ident(pos_host.ctx, rcv, ident);
}

static int pos_tune_freq(int rcv, double mhz)
{
    if (!pos_host_valid || !pos_host.tune_freq) return A380X_POS_TUNE_NOT_ALLOWED;
    return pos_host.tune_freq(pos_host.ctx, rcv, mhz);
}

static int pos_set_course(int rcv, double deg)
{
    if (!pos_host_valid || !pos_host.set_course) return A380X_POS_TUNE_NOT_ALLOWED;
    return pos_host.set_course(pos_host.ctx, rcv, deg);
}

/* One receiver column of three fields (IDENT, FREQ, CRS). */
static void pos_receiver_fields(MfdPageContext *ctx, int rcv, const A380XMfdNavaidSlot *slot, float x, float y0,
                                int ls, int ls_locked, int id_base)
{
    MfdUi *ui = ctx->ui;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    MfdFieldResult fr;

    mfd_field_desc_init(&fd, MFD_FMT_TXT_NAVAID);
    fd.width = 125.0f;
    memset(&fv, 0, sizeof(fv));
    fv.has_value = slot->ident[0] != '\0';
    fv.text = slot->ident;
    fv.is_computed = !slot->ident_manual;
    fr = mfd_input_field(ui, NID(id_base), x, y0, &fd, &fv, &out);
    if (fr == MFD_FIELD_COMMITTED) {
        if (ls && ls_locked) pos_error(ui, MFD_ERR_NOT_ALLOWED); /* [PORT] Q11: stop */
        else pos_tune_result(ctx, pos_tune_ident(rcv, out.str), out.str);
    } else if (fr == MFD_FIELD_CLEARED && !ls) { /* LS: clearing ignored (FBW `v ? ... : false`) */
        pos_tune_result(ctx, pos_tune_ident(rcv, ""), "");
    }

    mfd_field_desc_init(&fd, ls ? MFD_FMT_FREQ_ILS : MFD_FMT_FREQ_VOR);
    fd.width = 125.0f;
    memset(&fv, 0, sizeof(fv));
    fv.has_value = !isnan(slot->freq_mhz);
    fv.num = slot->freq_mhz;
    fv.is_computed = !slot->freq_manual && !slot->ident_manual;
    fr = mfd_input_field(ui, NID(id_base + 1), x, y0 + 50.0f, &fd, &fv, &out);
    if (fr == MFD_FIELD_COMMITTED) {
        if (ls && ls_locked) pos_error(ui, MFD_ERR_NOT_ALLOWED);
        else pos_tune_result(ctx, pos_tune_freq(rcv, out.num), "");
    } else if (fr == MFD_FIELD_CLEARED && !ls) {
        pos_tune_result(ctx, pos_tune_freq(rcv, NAN), "");
    }

    mfd_field_desc_init(&fd, ls ? MFD_FMT_LS_COURSE : MFD_FMT_INBOUND_COURSE);
    fd.width = 125.0f;
    memset(&fv, 0, sizeof(fv));
    fv.has_value = !isnan(slot->course_deg);
    fv.num = slot->course_deg;
    fv.is_computed = ls && !slot->course_manual;
    fr = mfd_input_field(ui, NID(id_base + 2), x, y0 + 100.0f, &fd, &fv, &out);
    if (fr == MFD_FIELD_COMMITTED) pos_tune_result(ctx, pos_set_course(rcv, fabs(out.num)), ""); /* [PORT] Q10 */
    else if (fr == MFD_FIELD_CLEARED) pos_tune_result(ctx, pos_set_course(rcv, NAN), "");
}

static void pos_navaids_tuned(MfdPageContext *ctx, MfdPosNavaidsState *s)
{
    MfdUi *ui = ctx->ui;
    static const char *const labels[4] = {"IDENT", "FREQ", "CRS", "CLASS"};
    /* Columns (shrink-to-fit row 392.7 px centred in 14..754): VOR1 187.65..312.73, labels
     * 352.73..415.27, VOR2 455.27..580.35. Rows 50 px from y 186. */
    const float cx[2] = {250.19f, 517.81f};
    int i, k;
    for (i = 0; i < 2; ++i) {
        mfd_text(ui, cx[i], 217.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, i == 0 ? "VOR1" : "VOR2");
        pos_receiver_fields(ctx, i, &s->nav.vor[i], cx[i] - 62.5f, 242.5f, 0, 0, 10 + 10 * i);
        mfd_text(ui, cx[i], 418.5f, MFD_ANCHOR_CENTER, 25.0f, MFD_COL_GREEN, s->nav.vor[i].ident[0] ? s->nav.vor[i].cls : "");
    }
    for (k = 0; k < 4; ++k)
        mfd_text(ui, 384.0f, 267.0f + 50.0f * (float)k, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, labels[k]);
    /* 5 px spacer with 2 px darkgrey bottom border after the 20 px padding */
    mfd_draw_fill_rect(ctx->dl, mfd_rect(14.0f, 459.0f, 740.0f, 2.0f), MFD_COL_DARK_GREY);
}

static void pos_navaids_selected(MfdPageContext *ctx, MfdPosNavaidsState *s)
{
    MfdUi *ui = ctx->ui;
    MfdDrawList *dl = ctx->dl;
    char t[32];
    int k, n_desel = 0;
    double lat, lon;
    static const MfdMenuItem data_navaid[1] = {{"DATA NAVAID", 0}};
    static const float desel_x[3] = {14.0f, 134.5f, 255.0f};

    /* grid 25 / 32.5 / 32.5 % of 740: 14..199, 199..439.5, 439.5..680 */
    mfd_draw_fill_rect(dl, mfd_rect(14.0f, 225.0f, 666.0f, 1.0f), MFD_COL_LIGHT_GREY);
    mfd_draw_fill_rect(dl, mfd_rect(198.0f, 186.0f, 1.0f, 160.0f), MFD_COL_LIGHT_GREY);
    mfd_draw_fill_rect(dl, mfd_rect(438.5f, 186.0f, 1.0f, 160.0f), MFD_COL_LIGHT_GREY);
    mfd_text(ui, 106.0f, 211.5f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "IDENT");
    mfd_text(ui, 318.75f, 211.5f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "FREQ/CHAN");
    mfd_text(ui, 559.75f, 211.5f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "CLASS");
    for (k = 0; k < 3; ++k) {
        float y = 226.0f + 40.0f * (float)k;
        MfdButtonDesc bd;
        mfd_button_desc_init(&bd, s->nav.selected[k].ident);
        bd.menu = data_navaid;
        bd.menu_count = 1;
        bd.flags = MFD_BTN_DISABLED | (s->nav.selected[k].ident[0] ? 0u : MFD_BTN_HIDDEN);
        mfd_button_ex(ui, NID(40 + k), mfd_rect(52.5f, y, 107.0f, 40.0f), &bd);
        if (!isnan(s->nav.selected[k].freq_mhz) && s->nav.selected[k].ident[0]) {
            mfd_js_to_fixed(s->nav.selected[k].freq_mhz, 2, t, sizeof(t));
            mfd_text(ui, 318.75f, y + 27.5f, MFD_ANCHOR_CENTER, 25.0f, MFD_COL_GREEN, t);
        }
        mfd_text(ui, 559.75f, y + 27.5f, MFD_ANCHOR_CENTER, 25.0f, MFD_COL_GREEN, s->nav.selected[k].cls);
    }

    /* RADIO NAV MODE / POSITION (grid 190 + 320 at margin-left 80; label not right-justified [FBW]). */
    mfd_text(ui, 94.0f, 382.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "RADIO NAV MODE");
    mfd_text(ui, 94.0f, 427.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "RADIO POSITION");
    if (s->nav.selected[0].ident[0]) {
        mfd_text(ui, 304.0f, 386.0f, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_GREEN,
                 s->nav.selected[1].ident[0] ? "DME/DME" : "VOR/DME");
        if (pos_fm_position(ctx, &lat, &lon)) {
            pos_coord(lat, lon, t, sizeof(t));
            mfd_text(ui, 304.0f, 431.0f, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_GREEN, t);
        }
    }
    mfd_draw_fill_rect(dl, mfd_rect(14.0f, 458.0f, 740.0f, 1.0f), MFD_COL_CSS_LIGHTGREY);
    mfd_text(ui, 29.0f, 490.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "LIST OF DESELECTED NAVAIDS");

    for (k = 0; k < 6; ++k)
        if (pos_desel[k][0]) n_desel = k + 1;
    for (k = 0; k < 6; ++k) {
        MfdFieldDesc fd;
        MfdFieldValue fv;
        MfdValue out;
        MfdFieldResult fr;
        mfd_field_desc_init(&fd, MFD_FMT_TXT_NAVAID);
        fd.fmt.placeholder = "-";
        fd.flags = (k > 0 && !pos_desel[k - 1][0]) ? MFD_FIELD_DISABLED : 0u;
        memset(&fv, 0, sizeof(fv));
        fv.has_value = pos_desel[k][0] != '\0';
        fv.text = pos_desel[k];
        fr = mfd_input_field(ui, NID(50 + k), desel_x[k % 3], k < 3 ? 504.0f : 551.0f, &fd, &fv, &out);
        if (fr == MFD_FIELD_COMMITTED) {
            if (pos_host_valid && pos_host.navaid_exists && !pos_host.navaid_exists(pos_host.ctx, out.str)) {
                pos_error(ui, MFD_ERR_NOT_IN_DATABASE);
            } else {
                int slot = k < n_desel ? k : n_desel;
                snprintf(pos_desel[slot], sizeof(pos_desel[slot]), "%.7s", out.str);
            }
        } else if (fr == MFD_FIELD_CLEARED && k < 6) {
            int j;
            for (j = k; j < 5; ++j) memcpy(pos_desel[j], pos_desel[j + 1], sizeof(pos_desel[j]));
            pos_desel[5][0] = '\0';
        }
    }
}

static void pos_navaids_page(MfdPageContext *ctx)
{
    MfdPosNavaidsState *s = MFD_PAGE_STATE(ctx, MfdPosNavaidsState);
    MfdUi *ui = ctx->ui;
    static const char *const titles[2] = {"TUNED FOR DISPLAY", "SELECTED FOR FMS NAV"};
    static const char *const ls_labels[5] = {"IDENT", "FREQ/CHAN", "CRS", "SLOPE", "CLASS"};
    MfdTabsDesc td;
    MfdRect content, clip;
    float L;
    int k;
    char t[16];

    if (ctx->entered) {
        s->tab = strcmp(ctx->uri->extra, "nav") == 0 ? 1 : 0;
        s->show_return = strcmp(ctx->uri->extra, "withReturn") == 0 || strcmp(ctx->uri->extra, "nav") == 0;
    }
    pos_read_navaids(ctx, &s->nav);

    /* Tab container flex 1 1 auto shares the free space with the bottom spacer (natural FBW):
     * TUNED 138..542.5, SELECTED 138..606 (the LS block follows the tab container). */
    mfd_tabs_desc_init(&td);
    td.slant_deg = 25.0f;
    td.selected_text = MFD_COL_WHITE;
    if (s->tab == 0) {
        td.content_h = 364.5f;
        L = 542.5f;
    } else {
        td.content_h = 428.0f;
        L = 606.0f;
    }
    k = mfd_top_tabs(ui, NID(1), mfd_rect(0.0f, 138.0f, 768.0f, L - 138.0f), titles, 2, s->tab, &td, &content, &clip);
    if (k >= 0) s->tab = k;
    mfd_draw_clip_push(ctx->dl, clip);
    if (s->tab == 0) pos_navaids_tuned(ctx, s);
    else pos_navaids_selected(ctx, s);
    mfd_draw_clip_pop(ctx->dl);

    /* LS block (row centred: labels 135.07..247.93, column 267.93..392.93, button 407.93..632.93). */
    mfd_text(ui, 330.43f, L + 31.0f, MFD_ANCHOR_CENTER, 20.0f, MFD_COL_WHITE, "LS");
    for (k = 0; k < 5; ++k)
        mfd_text(ui, 247.93f, L + 50.0f * (float)(k + 1) + 31.0f, MFD_ANCHOR_RIGHT, 20.0f, MFD_COL_WHITE, ls_labels[k]);
    pos_receiver_fields(ctx, A380X_POS_RCV_LS, &s->nav.ls, 267.93f, L + 56.5f, 1, s->nav.ls_locked, 30);
    if (!isnan(s->nav.ls.slope_deg) && s->nav.ls.ident[0]) mfd_js_to_fixed(s->nav.ls.slope_deg, 1, t, sizeof(t));
    else snprintf(t, sizeof(t), "---");
    mfd_text(ui, 297.65f, L + 232.5f, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_GREEN, t);
    mfd_text(ui, 297.65f + mfd_text_width(t, 25.0f) + 6.0f, L + 231.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_DARK_BLUE, DEG);
    mfd_text(ui, 330.43f, L + 282.5f, MFD_ANCHOR_CENTER, 25.0f, MFD_COL_GREEN, s->nav.ls.ident[0] ? s->nav.ls.cls : "");
    pos_row_button(ui, NID(35), mfd_rect(407.93f, L + 119.0f, 225.0f, 62.0f), "DESELECT\nGLIDE", "*", 10.0f,
                   MFD_BTN_DISABLED); /* [FBW-NA] */

    if (mfd_button(ui, NID(2), mfd_rect(0.0f, 910.0f, 145.0f, 40.0f), "RETURN", s->show_return ? 0u : MFD_BTN_HIDDEN))
        mfd_navigate_back(ctx);
}

/* ======================================================================================== */

void a380x_mfd_page_position_register(void)
{
    mfd_register_page("fms/position/monitor", MFD_PAGE_ID_POSITION_MONITOR, "MONITOR", pos_monitor_page,
                      MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY);
    mfd_register_page("fms/position/irs", MFD_PAGE_ID_POSITION_IRS, "IRS", pos_irs_page,
                      MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY);
    mfd_register_page("fms/position/navaids", MFD_PAGE_ID_POSITION_NAVAIDS, "NAVAIDS", pos_navaids_page,
                      MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY);
}
