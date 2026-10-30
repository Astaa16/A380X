/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Headless MFD render test: a software rasteriser for the MFD display list (anti-aliased convex
 * polygons and lines, snapped rectangles, alpha glyph blits from a380x_mfd_font.h, per-command
 * clip, layer order) that writes 768x1024 PPM previews of representative MFD states.
 *
 * Usage: test_mfd_render <output-dir> [cycle.json]
 * Link: a380x_mfd.c a380x_mfd_ui.c a380x_mfd_draw.c a380x_mfd_page_data.c a380x_mfd_formats.c
 *       a380x_fms_messages.c -lm
 */
#include "a380x_mfd.h"
#include "a380x_mfd_page_data.h"
#include "a380x_mfd_font.h"
#include "a380x_xpkit_shared.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================================================================================== */
/* Software rasteriser                                                                      */
/* ======================================================================================== */

static unsigned char g_img[MFD_W * MFD_H * 3];
static unsigned char g_atlas[MFD_FONT_ATLAS_W * MFD_FONT_ATLAS_H];
static uint16_t g_order[MFD_DRAW_MAX_CMDS];

typedef struct { int x0, y0, x1, y1; } IRect;

static void blend(int x, int y, uint32_t rgb, float a)
{
    unsigned char *p;
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    p = &g_img[(y * MFD_W + x) * 3];
    p[0] = (unsigned char)((float)p[0] + (float)((int)((rgb >> 16) & 0xff) - (int)p[0]) * a + 0.5f);
    p[1] = (unsigned char)((float)p[1] + (float)((int)((rgb >> 8) & 0xff) - (int)p[1]) * a + 0.5f);
    p[2] = (unsigned char)((float)p[2] + (float)((int)(rgb & 0xff) - (int)p[2]) * a + 0.5f);
}

static IRect clip_irect(MfdRect c)
{
    IRect r;
    r.x0 = (int)floorf(c.x + 0.5f);
    r.y0 = (int)floorf(c.y + 0.5f);
    r.x1 = (int)floorf(c.x + c.w + 0.5f);
    r.y1 = (int)floorf(c.y + c.h + 0.5f);
    if (r.x0 < 0) r.x0 = 0;
    if (r.y0 < 0) r.y0 = 0;
    if (r.x1 > MFD_W) r.x1 = MFD_W;
    if (r.y1 > MFD_H) r.y1 = MFD_H;
    return r;
}

/* Filled rectangles snap to integer pixels (round half up), spec 01 section 0.2. */
static void fill_rect(float x, float y, float w, float h, IRect c, uint32_t rgb)
{
    int x0 = (int)floorf(x + 0.5f), y0 = (int)floorf(y + 0.5f);
    int x1 = (int)floorf(x + w + 0.5f), y1 = (int)floorf(y + h + 0.5f), i, j;
    if (x0 < c.x0) x0 = c.x0;
    if (y0 < c.y0) y0 = c.y0;
    if (x1 > c.x1) x1 = c.x1;
    if (y1 > c.y1) y1 = c.y1;
    for (j = y0; j < y1; ++j)
        for (i = x0; i < x1; ++i) blend(i, j, rgb, 1.0f);
}

/* Convex polygon with 4x4 supersampled coverage. */
static void fill_convex(const MfdPoint *p, int n, IRect c, uint32_t rgb)
{
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f, area = 0.0f;
    int i, x, y, sx, sy, x0, y0, x1, y1;
    for (i = 0; i < n; ++i) {
        const MfdPoint *a = &p[i], *b = &p[(i + 1) % n];
        area += a->x * b->y - b->x * a->y;
        if (a->x < minx) minx = a->x;
        if (a->y < miny) miny = a->y;
        if (a->x > maxx) maxx = a->x;
        if (a->y > maxy) maxy = a->y;
    }
    if (fabsf(area) < 1e-6f) return;
    x0 = (int)floorf(minx); y0 = (int)floorf(miny);
    x1 = (int)ceilf(maxx); y1 = (int)ceilf(maxy);
    if (x0 < c.x0) x0 = c.x0;
    if (y0 < c.y0) y0 = c.y0;
    if (x1 > c.x1) x1 = c.x1;
    if (y1 > c.y1) y1 = c.y1;
    for (y = y0; y < y1; ++y) {
        for (x = x0; x < x1; ++x) {
            int hits = 0;
            for (sy = 0; sy < 4; ++sy) {
                for (sx = 0; sx < 4; ++sx) {
                    float px = (float)x + (sx + 0.5f) / 4.0f, py = (float)y + (sy + 0.5f) / 4.0f;
                    int inside = 1;
                    for (i = 0; i < n && inside; ++i) {
                        const MfdPoint *a = &p[i], *b = &p[(i + 1) % n];
                        float e = (b->x - a->x) * (py - a->y) - (b->y - a->y) * (px - a->x);
                        if (area > 0.0f ? e < 0.0f : e > 0.0f) inside = 0;
                    }
                    hits += inside;
                }
            }
            if (hits) blend(x, y, rgb, (float)hits / 16.0f);
        }
    }
}

static void stroke_segment(float x1, float y1, float x2, float y2, float w, IRect c, uint32_t rgb)
{
    float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy), nx, ny;
    MfdPoint q[4];
    if (len < 1e-6f) return;
    nx = -dy / len * w * 0.5f;
    ny = dx / len * w * 0.5f;
    q[0].x = x1 + nx; q[0].y = y1 + ny; q[1].x = x2 + nx; q[1].y = y2 + ny;
    q[2].x = x2 - nx; q[2].y = y2 - ny; q[3].x = x1 - nx; q[3].y = y1 - ny;
    fill_convex(q, 4, c, rgb);
}

static void draw_text(const MfdCmd *cmd, const char *s, size_t n, IRect c, uint32_t rgb)
{
    float scale, width = (float)mfd_utf8_countn(s, n) * MFD_FONT_ADVANCE * cmd->size;
    int si = mfd_font_size_index(cmd->size, &scale);
    float pen = cmd->a[0] - width * (cmd->anchor == MFD_ANCHOR_CENTER ? 0.5f : cmd->anchor == MFD_ANCHOR_RIGHT ? 1.0f : 0.0f);
    float adv = MFD_FONT_ADVANCE * cmd->size;
    const char *p = s, *end = s + n;
    while (p < end && *p) {
        unsigned cp = mfd_utf8_next(&p);
        int slot = mfd_font_slot(cp, cmd->font == MFD_FONT_ECAM);
        if (slot >= 0) {
            const MfdFontGlyph *g = &mfd_font_glyphs[si * MFD_FONT_SLOTS + slot];
            if (g->w) {
                if (scale > 0.999f && scale < 1.001f) {
                    int ox = (int)floorf(pen + 0.5f) + g->left, oy = (int)floorf(cmd->a[1] + 0.5f) + g->top, i, j;
                    for (j = 0; j < g->h; ++j) {
                        int y = oy + j;
                        if (y < c.y0 || y >= c.y1) continue;
                        for (i = 0; i < g->w; ++i) {
                            int x = ox + i;
                            unsigned a;
                            if (x < c.x0 || x >= c.x1) continue;
                            a = g_atlas[(g->y + j) * MFD_FONT_ATLAS_W + g->x + i];
                            if (a) blend(x, y, rgb, a / 255.0f);
                        }
                    }
                } else { /* scaled strip: bilinear sample */
                    float gx = pen + g->left * scale, gy = cmd->a[1] + g->top * scale;
                    int x0 = (int)floorf(gx), y0 = (int)floorf(gy);
                    int x1 = (int)ceilf(gx + g->w * scale), y1 = (int)ceilf(gy + g->h * scale), x, y;
                    for (y = y0; y < y1; ++y) {
                        if (y < c.y0 || y >= c.y1) continue;
                        for (x = x0; x < x1; ++x) {
                            float u = ((float)x + 0.5f - gx) / scale - 0.5f, v = ((float)y + 0.5f - gy) / scale - 0.5f;
                            int iu = (int)floorf(u), iv = (int)floorf(v), k;
                            float fu = u - iu, fv = v - iv, acc = 0.0f;
                            if (x < c.x0 || x >= c.x1) continue;
                            for (k = 0; k < 4; ++k) {
                                int tu = iu + (k & 1), tv = iv + (k >> 1);
                                float wgt = ((k & 1) ? fu : 1.0f - fu) * ((k >> 1) ? fv : 1.0f - fv);
                                if (tu < 0 || tv < 0 || tu >= g->w || tv >= g->h) continue;
                                acc += wgt * g_atlas[(g->y + tv) * MFD_FONT_ATLAS_W + g->x + tu];
                            }
                            if (acc > 0.5f) blend(x, y, rgb, acc / 255.0f);
                        }
                    }
                }
            }
        }
        pen += adv;
    }
}

static void render(const MfdDrawList *dl)
{
    uint32_t i, n = mfd_draw_order(dl, g_order);
    memset(g_img, 0, sizeof(g_img));
    for (i = 0; i < n; ++i) {
        const MfdCmd *cmd = &dl->cmds[g_order[i]];
        IRect c = clip_irect(dl->clips[cmd->clip]);
        uint32_t rgb = mfd_color_rgb((MfdColor)cmd->color);
        const MfdPoint *pts = &dl->points[cmd->first];
        switch ((MfdCmdKind)cmd->kind) {
        case MFD_CMD_FILL_RECT:
            fill_rect(cmd->a[0], cmd->a[1], cmd->a[2], cmd->a[3], c, rgb);
            break;
        case MFD_CMD_STROKE_RECT: {
            float x = cmd->a[0], y = cmd->a[1], w = cmd->a[2], h = cmd->a[3], t = cmd->width;
            fill_rect(x, y, w, t, c, rgb);
            fill_rect(x, y + h - t, w, t, c, rgb);
            fill_rect(x, y + t, t, h - 2 * t, c, rgb);
            fill_rect(x + w - t, y + t, t, h - 2 * t, c, rgb);
            break;
        }
        case MFD_CMD_LINE:
            stroke_segment(cmd->a[0], cmd->a[1], cmd->a[2], cmd->a[3], cmd->width, c, rgb);
            break;
        case MFD_CMD_POLYLINE: {
            uint32_t k, segs = cmd->closed ? cmd->count : cmd->count - 1;
            for (k = 0; k < segs; ++k) {
                const MfdPoint *a = &pts[k], *b = &pts[(k + 1) % cmd->count];
                stroke_segment(a->x, a->y, b->x, b->y, cmd->width, c, rgb);
            }
            break;
        }
        case MFD_CMD_POLYGON:
        case MFD_CMD_TRIANGLE:
            fill_convex(pts, (int)cmd->count, c, rgb);
            break;
        case MFD_CMD_TEXT:
            draw_text(cmd, &dl->text[cmd->first], cmd->count, c, rgb);
            break;
        }
    }
}

static int write_ppm(const char *dir, const char *name)
{
    char path[1024];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%d %d\n255\n", MFD_W, MFD_H);
    fwrite(g_img, 1, sizeof(g_img), f);
    fclose(f);
    printf("wrote %s\n", path);
    return 1;
}

#ifndef A380X_MFD_RASTER_ONLY
/* ======================================================================================== */
/* Fixture                                                                                  */
/* ======================================================================================== */

static A380XMfd g_mfd;
static MfdMsgQueue g_q;
static A380XMfdInputs g_in;
static double g_t = 100.0;
static char g_cycle[8] = "2609";
static int g_wpt = 0;

static void host_status(void *ctx, A380XMfdStatusData *st)
{
    (void)ctx;
    snprintf(st->airframe, sizeof(st->airframe), "A380-800");
    snprintf(st->engine, sizeof(st->engine), "TRENT 972");
    mfd_navdb_serial("XP", g_cycle, st->navdb_serial, sizeof(st->navdb_serial));
    mfd_airac_cycle_dates(g_cycle, st->navdb_active, sizeof(st->navdb_active));
    snprintf(st->navdb_second, sizeof(st->navdb_second), "%s", st->navdb_active);
    st->stored_waypoints = g_wpt;
}

static int host_airport(void *ctx, const char *icao, A380XMfdAirport *a)
{
    (void)ctx;
    if (strcmp(icao, "EGLL") != 0) return 0;
    memset(a, 0, sizeof(*a));
    snprintf(a->icao, sizeof(a->icao), "EGLL");
    snprintf(a->name, sizeof(a->name), "LONDON HEATHROW");
    a->lat = 51.4775; a->lon = -0.461389;
    a->runway_count = 4;
    snprintf(a->rwy[0].ident, 6, "09L"); a->rwy[0].length_m = 3902; snprintf(a->rwy[0].ls_ident, 6, "IAA");
    snprintf(a->rwy[1].ident, 6, "09R"); a->rwy[1].length_m = 3660; snprintf(a->rwy[1].ls_ident, 6, "IBB");
    snprintf(a->rwy[2].ident, 6, "27L"); a->rwy[2].length_m = 3660; snprintf(a->rwy[2].ls_ident, 6, "ILL");
    snprintf(a->rwy[3].ident, 6, "27R"); a->rwy[3].length_m = 3902;
    return 1;
}

static void frame(void)
{
    g_in.now = g_t;
    a380x_mfd_frame(&g_mfd, &g_in);
    g_t += 0.02;
    assert(!a380x_mfd_display_list(&g_mfd, MFD_CAPT)->overflowed);
}

static void frames(int n) { while (n-- > 0) frame(); }

static void move(float x, float y) { mfd_pointer_move(&g_mfd, MFD_CAPT, x, y); frame(); }

static void click(float x, float y)
{
    mfd_pointer_move(&g_mfd, MFD_CAPT, x, y);
    frame();
    mfd_pointer_button(&g_mfd, MFD_CAPT, 1);
    frame();
    mfd_pointer_button(&g_mfd, MFD_CAPT, 0);
    frame();
}

static void key(MfdKey k, char ch) { mfd_key(&g_mfd, MFD_CAPT, k, ch); frame(); }
static void type(const char *s) { while (*s) key(MFD_KEY_CHAR, *s++); }

static void leave(void) { mfd_pointer_leave(&g_mfd, MFD_CAPT); frame(); }

static void reset(void)
{
    mfd_msg_init(&g_q);
    memset(&g_in, 0, sizeof(g_in));
    g_in.du_on[0] = g_in.du_on[1] = 1;
    g_in.fms_knob = 1;
    g_in.fmc_a_healthy = g_in.fmc_b_healthy = 1;
    g_in.metric_units = 1;
    {
        A380XMfdSystemsHost host;
        memset(&host, 0, sizeof(host));
        host.read_status = host_status;
        host.airport_lookup = host_airport;
        a380x_mfd_init(&g_mfd, NULL, &g_q, &host);
    }
    frames(2);
}

static void snap(const char *dir, const char *name)
{
    /* The same page fixtures are exercised by the actual WebKit renderer. */
    A380XXpKitMfd *fixture=calloc(1,sizeof(*fixture));
    assert(fixture);
    fixture->powered=1;atomic_store(&fixture->sequence,2);
    snprintf(fixture->uri,sizeof(fixture->uri),"%s",a380x_mfd_active_uri(&g_mfd,MFD_CAPT));
    memcpy(&fixture->list,a380x_mfd_display_list(&g_mfd,MFD_CAPT),sizeof(fixture->list));
    assert(!fixture->list.overflowed);
    char path[1024];snprintf(path,sizeof(path),"%s/%s.mfd",dir,name);
    FILE *out=fopen(path,"wb");assert(out);
    assert(fwrite(fixture,sizeof(*fixture),1,out)==1);fclose(out);free(fixture);
    render(a380x_mfd_display_list(&g_mfd, MFD_CAPT));
    if (!write_ppm(dir, name)) {
        fprintf(stderr, "cannot write %s\n", name);
        exit(1);
    }
}

/* ---- InputField gallery page ---------------------------------------------------------------- */

static MfdFieldValue g_fv[10];

static void gallery_page(MfdPageContext *ctx)
{
    static const char *const labels[] = {"MANDATORY", "MANDATORY TIME", "ENTERED", "COMPUTED", "FOCUSED/CARET",
                                         "VALIDATING", "DISABLED", "INACTIVE", "TMPY", "FL OVERFLOW"};
    static const MfdFmtKind kinds[] = {MFD_FMT_TXT_AIRPORT, MFD_FMT_TIME_HHMM, MFD_FMT_SPEED_KT, MFD_FMT_WEIGHT,
                                       MFD_FMT_COST_INDEX, MFD_FMT_SPEED_KT, MFD_FMT_SPEED_KT, MFD_FMT_SPEED_KT,
                                       MFD_FMT_CRZ_TEMP, MFD_FMT_FLIGHT_LEVEL};
    static const uint32_t flags[] = {MFD_FIELD_MANDATORY, MFD_FIELD_MANDATORY, 0, 0, 0, 0, MFD_FIELD_DISABLED,
                                     MFD_FIELD_INACTIVE, MFD_FIELD_TMPY, 0};
    int i;
    for (i = 0; i < 10; ++i) {
        MfdFieldDesc d;
        MfdValue out;
        float y = 160.0f + 75.0f * (float)i;
        mfd_field_desc_init(&d, kinds[i]);
        d.flags = flags[i];
        mfd_label(ctx->ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_RIGHT, 330.0f, y + 8.5f, labels[i]);
        if (mfd_input_field(ctx->ui, MFD_ID(MFD_PAGE_ID_TEST, 10 + i), 350.0f, y, &d, &g_fv[i], &out) ==
            MFD_FIELD_COMMITTED) {
            g_fv[i].has_value = 1;
            g_fv[i].is_computed = 0;
            g_fv[i].num = out.num;
        }
    }
}

static void gallery_values(void)
{
    memset(g_fv, 0, sizeof(g_fv));
    g_fv[2].has_value = 1; g_fv[2].num = 250;
    g_fv[3].has_value = 1; g_fv[3].is_computed = 1; g_fv[3].num = 418500;
    g_fv[5].has_value = 1; g_fv[5].num = 160;
    g_fv[6].has_value = 1; g_fv[6].num = 140;
    g_fv[7].has_value = 1; g_fv[7].num = 145;
    g_fv[8].has_value = 1; g_fv[8].num = -54;
    g_fv[9].has_value = 1; g_fv[9].num = 350;
}

static float gallery_y(int i) { return 160.0f + 75.0f * (float)i + 18.0f; }

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    MfdUi *ui;
    if (argc > 2) { /* cycle.json: {"cycle":"2609",...} */
        FILE *f = fopen(argv[2], "rb");
        char buf[256] = {0};
        if (f) {
            const char *p;
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            buf[n] = '\0';
            p = strstr(buf, "\"cycle\"");
            if (p && (p = strchr(p + 7, '"')) != NULL) snprintf(g_cycle, sizeof(g_cycle), "%.4s", p + 1);
        }
    }
    if (!mfd_font_decode(g_atlas)) {
        fprintf(stderr, "font decode failed\n");
        return 1;
    }
    {
        char d[16];
        assert(mfd_airac_cycle_dates("2410", d, sizeof(d)) && strcmp(d, "03OCT-31OCT") == 0);
        assert(mfd_airac_cycle_dates("2609", d, sizeof(d)) && strcmp(d, "03SEP-01OCT") == 0);
    }
    a380x_mfd_page_data_register();
    mfd_register_page("fms/test/fields", MFD_PAGE_ID_TEST, "FIELDS", gallery_page,
                      MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY);

    /* 1. DATA/STATUS, ACFT STATUS (pinned FBW layout) */
    reset();
    leave();
    snap(dir, "data_status");

    /* 2. Legacy layout of the reference screenshot (for the side-by-side comparison) */
    mfd_page_data_set_legacy_layout(1);
    frame();
    snap(dir, "data_status_legacy");
    mfd_page_data_set_legacy_layout(0);

    /* 3. FMS P/N tab */
    click(571.0f, 158.0f);
    leave();
    snap(dir, "data_status_fms_pn");

    /* 4. ACTIVE menu open over DATA/STATUS, pointer on PERF */
    reset();
    click(96.0f, 71.0f);
    move(80.0f, 156.0f);
    snap(dir, "active_menu");

    /* 5. System selector open */
    reset();
    click(100.0f, 22.0f);
    move(60.0f, 99.0f);
    snap(dir, "system_selector");

    /* 6. InputField gallery: caret + validating */
    reset();
    gallery_values();
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, "fms/test/fields");
    frames(2);
    click(380.0f, gallery_y(4));
    type("8");
    ui = &g_mfd.sides[MFD_CAPT].ui;
    /* white-box: put the SPEED field in the 500 ms validating state while the CI field edits */
    ui->val[0].id = MFD_ID(MFD_PAGE_ID_TEST, 15);
    ui->val[0].until = g_t + 10.0;
    ui->val[0].status = MFD_PARSE_OK;
    snprintf(ui->val[0].text, sizeof(ui->val[0].text), "180");
    while (fmod(g_t - ui->edit_t0, 1.0) < 0.6) frame(); /* caret block visible phase */
    leave();
    snap(dir, "fields");

    /* 7. Gallery: focused (value selected) and FL overflow editing */
    reset();
    gallery_values();
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, "fms/test/fields");
    frames(2);
    click(380.0f, gallery_y(2));
    leave();
    snap(dir, "fields_selected");
    click(380.0f, gallery_y(9));
    type("FL39");
    while (fmod(g_t - ui->edit_t0, 1.0) < 0.6) frame();
    leave();
    snap(dir, "fields_overflow");

    /* 8. NOT FOUND (before the 500 ms auto-back) */
    reset();
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, "fms/active/nonexistent");
    frames(2);
    leave();
    snap(dir, "not_found");

    /* 9. MSG LIST overlay with two messages */
    reset();
    {
        MfdFmsError err;
        mfd_msg_add(&g_q, MFD_MSG_NOT_IN_DATABASE, NULL, NULL, NULL, NULL, NULL);
        mfd_err_set_range(&err, "0.0", "999.9", "%");
        mfd_msg_show_error(&g_q, &err, 1);
        mfd_msg_add(&g_q, MFD_MSG_CHECK_MIN_FUEL_AT_DEST, NULL, NULL, NULL, NULL, NULL);
        mfd_msg_clear_latest(&g_q); /* type II: stays in the list as cleared */
    }
    a380x_mfd_open_msg_list(&g_mfd, MFD_CAPT);
    frames(2);
    leave();
    snap(dir, "msg_list");

    /* 10. DATA/AIRPORT with EGLL */
    reset();
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, "fms/data/airport");
    frames(2);
    click(386.0f, 224.0f);
    type("EGLL");
    key(MFD_KEY_ENT, 0);
    g_t += 0.6;
    frames(3);
    leave();
    snap(dir, "data_airport");

    /* 11. DATA/STATUS with a footer message and a stored waypoint (DELETE ALL enabled) */
    reset();
    g_wpt = 1;
    mfd_msg_add(&g_q, MFD_MSG_CHECK_TO_DATA, NULL, NULL, NULL, NULL, NULL);
    click(330.0f, 540.0f); /* FUEL PENALTY field focused */
    leave();
    snap(dir, "data_status_message");
    g_wpt = 0;

    printf("test_mfd_render: OK\n");
    return 0;
}
#endif /* !A380X_MFD_RASTER_ONLY */
