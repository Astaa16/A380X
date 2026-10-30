/* SPDX-License-Identifier: GPL-3.0-or-later
 * ISIS 1 core tests: FBW ISISlegacy logic (power-up self-test, standby ride-through, BUGS page,
 * MENU page, baro QNH/STD/units, LS, brightness, ATT RST, failure flags) and a pixel preview.
 * The preview replays the exact display list the plugin hands to OpenGL, with GL's own rules:
 * pixel-centre sampling, interpolated vertex colour/alpha, GL_LINEAR alpha atlas, SRC_ALPHA blend.
 *
 *   clang -std=c11 -O2 -Wall -Wextra -Werror test_isis.c -lm -o test_isis
 *   ./test_isis <out-dir>     writes isis-<state>.ppm (1024 x 1024)
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "a380x_isis.h"
#include "a380x_isis_font.h"
#include "a380x_isis.inc"

static IsisList list;
static float image[ISIS_H][ISIS_W][3];
static unsigned char *atlas;
static int checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static float atlas_sample(float u, float v)
{
    float x = u * ISIS_FONT_WIDTH - 0.5f, y = v * ISIS_FONT_HEIGHT - 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y);
    float fx = x - (float)x0, fy = y - (float)y0, sum = 0.0f;
    for (int j = 0; j < 2; ++j) for (int i = 0; i < 2; ++i) {
        int xi = x0 + i, yi = y0 + j;
        float w = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
        if (xi < 0) xi = 0;
        if (yi < 0) yi = 0;
        if (xi >= ISIS_FONT_WIDTH) xi = ISIS_FONT_WIDTH - 1;
        if (yi >= ISIS_FONT_HEIGHT) yi = ISIS_FONT_HEIGHT - 1;
        sum += w * atlas[yi * ISIS_FONT_WIDTH + xi] / 255.0f;
    }
    return sum;
}

static void raster_triangle(const IsisVertex *a, const IsisVertex *b, const IsisVertex *c, int textured)
{
    float area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
    if (fabsf(area) < 1e-8f) return;
    int x0 = (int)floorf(fminf(a->x, fminf(b->x, c->x))), x1 = (int)ceilf(fmaxf(a->x, fmaxf(b->x, c->x)));
    int y0 = (int)floorf(fminf(a->y, fminf(b->y, c->y))), y1 = (int)ceilf(fmaxf(a->y, fmaxf(b->y, c->y)));
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > ISIS_W) x1 = ISIS_W;
    if (y1 > ISIS_H) y1 = ISIS_H;
    for (int y = y0; y < y1; ++y) for (int x = x0; x < x1; ++x) {
        float px = (float)x + 0.5f, py = (float)y + 0.5f;
        float w0 = ((b->x - px) * (c->y - py) - (b->y - py) * (c->x - px)) / area;
        float w1 = ((c->x - px) * (a->y - py) - (c->y - py) * (a->x - px)) / area;
        float w2 = 1.0f - w0 - w1;
        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
        if ((w0 == 0 || w1 == 0 || w2 == 0) && (x + y) % 2) continue; /* share edges once */
        float alpha = w0 * a->a + w1 * b->a + w2 * c->a;
        if (textured) alpha *= atlas_sample(w0 * a->u + w1 * b->u + w2 * c->u, w0 * a->v + w1 * b->v + w2 * c->v);
        if (alpha <= 0) continue;
        if (alpha > 1) alpha = 1;
        float r = w0 * a->r + w1 * b->r + w2 * c->r, g = w0 * a->g + w1 * b->g + w2 * c->g;
        float bl = w0 * a->b + w1 * b->b + w2 * c->b;
        image[y][x][0] = image[y][x][0] * (1 - alpha) + r * alpha;
        image[y][x][1] = image[y][x][1] * (1 - alpha) + g * alpha;
        image[y][x][2] = image[y][x][2] * (1 - alpha) + bl * alpha;
    }
}

static void raster_list(const IsisList *l)
{
    memset(image, 0, sizeof(image)); /* the device draw callback clears to black */
    for (int b = 0; b < l->batch_count; ++b) {
        const IsisBatch *batch = &l->batch[b];
        for (int i = 0; i < batch->count; i += 3)
            raster_triangle(&l->vertex[batch->first + i], &l->vertex[batch->first + i + 1],
                            &l->vertex[batch->first + i + 2], batch->textured);
    }
}

static void write_ppm(const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/isis-%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n%d %d\n255\n", ISIS_W, ISIS_H);
    for (int y = 0; y < ISIS_H; ++y) for (int x = 0; x < ISIS_W; ++x) for (int c = 0; c < 3; ++c)
        fputc((int)lroundf(fminf(1, fmaxf(0, image[y][x][c])) * 255), f);
    fclose(f);
}

static float pixel(int x, int y, int c) { return image[y][x][c]; }

static void shot(const char *dir, const char *name, const IsisState *s, const IsisInputs *in)
{
    isis_build(&list, s, in);
    CHECK(!list.overflow);
    CHECK(list.font_misses == 0);
    raster_list(&list);
    if (dir) write_ppm(dir, name);
}

static IsisInputs flight(float ias, float alt, float pitch, float roll)
{
    IsisInputs in;
    memset(&in, 0, sizeof(in));
    in.ias_kts = ias; in.altitude_ft = alt; in.pitch_deg = pitch; in.roll_deg = roll;
    in.mach = ias / 600.0f; in.baro_inhg = ISIS_STD_INHG; in.dc_ess = 1; in.dc_hot = 1;
    in.spd_valid = in.alt_valid = in.att_valid = 1;
    return in;
}

static void apply_baro(IsisState *s, IsisInputs *in)
{
    if (s->baro_write) { in->baro_inhg = s->baro_write_inhg; s->baro_write = 0; }
}

static void press(IsisState *s, IsisInputs *in, IsisButton b)
{
    isis_button(s, in, b, 1); isis_button(s, in, b, 0); apply_baro(s, in);
}

static void run(IsisState *s, const IsisInputs *in, float seconds)
{
    for (float t = 0; t < seconds; t += 0.05f) isis_update(s, in, 0.05f);
}

static void test_power(void)
{
    IsisState s;
    IsisInputs in = flight(0, 0, 0, 0);
    in.dc_ess = 0; in.dc_hot = 1;
    isis_reset(&s, 0);
    CHECK(s.du == ISIS_DU_OFF);
    run(&s, &in, 1);
    CHECK(s.du == ISIS_DU_OFF);              /* DC HOT alone needs IAS > 50 */
    in.ias_kts = 60; run(&s, &in, 0.1f);
    CHECK(s.du == ISIS_DU_SELFTEST);
    in.ias_kts = 0; in.dc_ess = 1;
    run(&s, &in, 89.0f);
    CHECK(s.du == ISIS_DU_SELFTEST);
    run(&s, &in, 1.5f);
    CHECK(s.du == ISIS_DU_ON);
    in.dc_ess = 0; run(&s, &in, 5);
    CHECK(s.du == ISIS_DU_STANDBY);          /* 10 s ride-through keeps the display */
    in.dc_ess = 1; run(&s, &in, 0.1f);
    CHECK(s.du == ISIS_DU_ON);
    in.dc_ess = 0; run(&s, &in, 10.5f);
    CHECK(s.du == ISIS_DU_OFF);
    in.dc_ess = 1; run(&s, &in, 10);
    CHECK(s.du == ISIS_DU_SELFTEST && s.du_timer > 79 && s.du_timer < 81);
    isis_reset(&s, 1);
    CHECK(s.du == ISIS_DU_ON);               /* aircraft loaded powered: no self-test */
}

static void test_bugs(void)
{
    IsisState s;
    IsisInputs in = flight(250, 10000, 0, 0);
    isis_reset(&s, 1);
    press(&s, &in, ISIS_BTN_MODE);
    CHECK(s.page == ISIS_PAGE_PFD);          /* MODE is the SFD/SND swap (pair level) */
    press(&s, &in, ISIS_BTN_MENU);
    press(&s, &in, ISIS_BTN_SET_PUSH);       /* MENU > SET SPD BUG 1 = FBW BUGS page */
    CHECK(s.page == ISIS_PAGE_BUGS && s.selected_bug == ISIS_BUG_SPD3);
    press(&s, &in, ISIS_BTN_SET_UP);
    CHECK(s.bug_value[ISIS_BUG_SPD3] == 30); /* inactive bug does not move */
    press(&s, &in, ISIS_BTN_SET_PUSH);
    CHECK(s.bug_on[ISIS_BUG_SPD3]);
    for (int i = 0; i < 110; ++i) press(&s, &in, ISIS_BTN_SET_UP);
    CHECK(s.bug_value[ISIS_BUG_SPD3] == 140);
    press(&s, &in, ISIS_BTN_SET_DOWN);
    CHECK(s.bug_value[ISIS_BUG_SPD3] == 139);
    press(&s, &in, ISIS_BTN_PLUS);
    CHECK(s.selected_bug == ISIS_BUG_ALT0);  /* (7 + 5) % 6 */
    press(&s, &in, ISIS_BTN_MINUS);
    press(&s, &in, ISIS_BTN_MINUS);
    CHECK(s.selected_bug == ISIS_BUG_SPD2);  /* (5 + i) % 6 */
    s.selected_bug = ISIS_BUG_ALT1;
    press(&s, &in, ISIS_BTN_SET_PUSH);
    press(&s, &in, ISIS_BTN_SET_DOWN);
    CHECK(s.bug_value[ISIS_BUG_ALT1] == 100); /* minimum 100 ft */
    for (int i = 0; i < 9; ++i) press(&s, &in, ISIS_BTN_SET_UP);
    CHECK(s.bug_value[ISIS_BUG_ALT1] == 1000);
    CHECK(s.brightness == 0.5f);             /* + / - select bugs, not brightness */
    run(&s, &in, 14.0f);
    CHECK(s.page == ISIS_PAGE_BUGS);
    run(&s, &in, 1.5f);
    CHECK(s.page == ISIS_PAGE_PFD);          /* 15 s without input */
}

static void test_pair(void)
{
    IsisState u[2];
    IsisInputs in = flight(250, 10000, 0, 0);
    isis_reset(&u[0], 1); isis_reset(&u[1], 1);
    isis_pair_update(u);
    CHECK(u[0].role == ISIS_ROLE_SFD && u[1].role == ISIS_ROLE_SND);
    /* The SND ignores the SFD-only controls, keeps its own brightness. */
    press(&u[1], &in, ISIS_BTN_LS); press(&u[1], &in, ISIS_BTN_MENU); press(&u[1], &in, ISIS_BTN_SET_UP);
    CHECK(!u[1].ls_active && u[1].page == ISIS_PAGE_PFD && !u[1].baro_write);
    isis_button(&u[1], &in, ISIS_BTN_PLUS, 1); isis_button(&u[1], &in, ISIS_BTN_PLUS, 0);
    CHECK(u[1].brightness > u[0].brightness);
    /* SFD settings follow the role. */
    u[0].ls_active = 1; u[0].metric_alt = 1; u[0].bug_on[ISIS_BUG_SPD3] = 1; u[0].bug_value[ISIS_BUG_SPD3] = 150;
    isis_pair_swap(u);
    CHECK(u[0].role == ISIS_ROLE_SND && u[1].role == ISIS_ROLE_SFD);
    CHECK(u[1].ls_active && u[1].metric_alt && u[1].bug_on[ISIS_BUG_SPD3] && u[1].bug_value[ISIS_BUG_SPD3] == 150);
    isis_pair_swap(u);
    CHECK(u[0].role == ISIS_ROLE_SFD);
    /* Exactly one SFD: a dead SFD hands over to the live SND. */
    u[0].du = ISIS_DU_OFF;
    isis_pair_update(u);
    CHECK(u[1].role == ISIS_ROLE_SFD && u[0].role == ISIS_ROLE_SND);
    u[1].role = ISIS_ROLE_SFD; u[0].role = ISIS_ROLE_SFD;
    isis_pair_update(u);
    CHECK(u[0].role != u[1].role);
}

static void test_menu_and_att(void)
{
    IsisState s;
    IsisInputs in = flight(250, 10000, 0, 0);
    isis_reset(&s, 1);
    press(&s, &in, ISIS_BTN_MENU);
    CHECK(s.page == ISIS_PAGE_MENU && s.menu_index == ISIS_MENU_SPD1);
    press(&s, &in, ISIS_BTN_SET_PUSH);
    CHECK(s.page == ISIS_PAGE_BUGS && s.selected_bug == ISIS_BUG_SPD3);
    press(&s, &in, ISIS_BTN_MENU);
    for (int i = 0; i < ISIS_MENU_METERS; ++i) press(&s, &in, ISIS_BTN_SET_UP);
    press(&s, &in, ISIS_BTN_SET_PUSH);
    CHECK(s.metric_alt && s.page == ISIS_PAGE_MENU);
    press(&s, &in, ISIS_BTN_SET_UP);
    press(&s, &in, ISIS_BTN_SET_PUSH);
    CHECK(s.baro_unit_inhg);
    press(&s, &in, ISIS_BTN_MINUS);
    CHECK(s.menu_index == ISIS_MENU_NAV);
    press(&s, &in, ISIS_BTN_SET_PUSH);       /* NOT AVAIL: no effect */
    CHECK(s.page == ISIS_PAGE_MENU && s.menu_index == ISIS_MENU_NAV);
    press(&s, &in, ISIS_BTN_PLUS);
    press(&s, &in, ISIS_BTN_PLUS);
    press(&s, &in, ISIS_BTN_PLUS);
    press(&s, &in, ISIS_BTN_PLUS);
    press(&s, &in, ISIS_BTN_PLUS);
    press(&s, &in, ISIS_BTN_PLUS);
    CHECK(s.menu_index == ISIS_MENU_EXIT);   /* wraps upward from SPD 1 */
    press(&s, &in, ISIS_BTN_SET_PUSH);
    CHECK(s.page == ISIS_PAGE_PFD);
    /* MENU held 2 s: ATT RST, 10 s flag, and no MENU page. */
    isis_button(&s, &in, ISIS_BTN_MENU, 1);
    run(&s, &in, 2.1f);
    CHECK(s.att_rst && s.att_rst_timer > 9.0f);
    isis_button(&s, &in, ISIS_BTN_MENU, 0);
    CHECK(s.page == ISIS_PAGE_PFD);
    run(&s, &in, 10.2f);
    CHECK(!s.att_rst);
}

static void test_baro_ls_brightness(void)
{
    IsisState s;
    IsisInputs in = flight(250, 10000, 0, 0);
    isis_reset(&s, 1);
    press(&s, &in, ISIS_BTN_SET_UP);
    CHECK(lroundf(in.baro_inhg * ISIS_HPA_PER_INHG) == 1014);
    press(&s, &in, ISIS_BTN_SET_DOWN);
    press(&s, &in, ISIS_BTN_SET_DOWN);
    CHECK(lroundf(in.baro_inhg * ISIS_HPA_PER_INHG) == 1012);
    press(&s, &in, ISIS_BTN_SET_PUSH);
    CHECK(s.baro_std && fabsf(in.baro_inhg - ISIS_STD_INHG) < 1e-5f);
    press(&s, &in, ISIS_BTN_SET_UP);
    CHECK(fabsf(in.baro_inhg - ISIS_STD_INHG) < 1e-5f); /* STD: knob inactive */
    press(&s, &in, ISIS_BTN_SET_PUSH);
    CHECK(!s.baro_std && lroundf(in.baro_inhg * ISIS_HPA_PER_INHG) == 1012);
    s.baro_unit_inhg = 1;
    press(&s, &in, ISIS_BTN_SET_UP);
    CHECK(fabsf(in.baro_inhg - 29.89f) < 1e-4f); /* 1012 hPa = 29.884 inHg, +0.01 */
    press(&s, &in, ISIS_BTN_LS);
    CHECK(s.ls_active);
    press(&s, &in, ISIS_BTN_LS);
    CHECK(!s.ls_active);
    s.ambient = 0.6f; run(&s, &in, 0.1f);
    CHECK(fabsf(s.brightness - 0.6f) < 1e-5f);
    isis_button(&s, &in, ISIS_BTN_PLUS, 1);
    CHECK(fabsf(s.brightness - 0.625f) < 1e-5f);
    run(&s, &in, 0.31f);
    CHECK(fabsf(s.brightness - 0.675f) < 1e-4f);
    run(&s, &in, 5);
    CHECK(fabsf(s.brightness - ISIS_BRIGHT_MAX) < 1e-5f);
    isis_button(&s, &in, ISIS_BTN_PLUS, 0);
    isis_button(&s, &in, ISIS_BTN_MINUS, 1);
    run(&s, &in, 10);
    isis_button(&s, &in, ISIS_BTN_MINUS, 0);
    CHECK(fabsf(s.brightness - ISIS_BRIGHT_MIN) < 1e-5f);
    in.mach = 0.52f; run(&s, &in, 0.1f); CHECK(s.mach_visible);
    in.mach = 0.47f; run(&s, &in, 0.1f); CHECK(s.mach_visible);
    in.mach = 0.44f; run(&s, &in, 0.1f); CHECK(!s.mach_visible);
    in.lateral_g = 0.1f; run(&s, &in, 10);
    CHECK(s.slip_offset < -9.0f && s.slip_offset > -10.5f);
}

static void test_display_list(void)
{
    IsisState s;
    IsisInputs in = flight(250, 10000, 3, 10);
    uint64_t a, b;
    isis_reset(&s, 1);
    isis_build(&list, &s, &in); a = isis_list_hash(&list);
    isis_build(&list, &s, &in); b = isis_list_hash(&list);
    CHECK(a == b && list.vertex_count > 1000);
    in.altitude_ft = 10001;
    isis_build(&list, &s, &in);
    CHECK(isis_list_hash(&list) != a);
    s.du = ISIS_DU_OFF;
    isis_build(&list, &s, &in);
    CHECK(list.vertex_count == 0);
    /* Clipped tape content never leaves its FBW <svg> viewport. */
    s.du = ISIS_DU_ON; in.att_valid = 0; in.alt_valid = 0;
    isis_list_reset(&list);
    isis_scale(&list, 1.1f); isis_translate(&list, -30, -30);
    isis_draw_airspeed(&list, &s, &in);
    for (int i = 0; i < list.vertex_count; ++i) {
        const IsisVertex *v = &list.vertex[i];
        CHECK(v->x >= 2.2f * (12 - 30) - 0.01f);
        CHECK(v->x <= 2.2f * (145.211f - 30) + 1.5f);
    }
}

static void previews(const char *dir)
{
    IsisState s;
    IsisInputs in;
    /* 1: reference capture (on ground, 1500 ft field, QNH 1013). */
    isis_reset(&s, 1);
    in = flight(0, 1495, 0.4f, 0); in.mach = 0;
    run(&s, &in, 1);
    shot(dir, "reference", &s, &in);
    /* Sanity on the raster: sky blue above the aircraft symbol, earth below, black corners. */
    CHECK(pixel(300, 420, 2) > 0.9f && pixel(300, 420, 0) < 0.1f);
    CHECK(pixel(700, 700, 0) > 0.5f && pixel(700, 700, 2) < 0.1f);
    CHECK(pixel(5, 5, 0) == 0 && pixel(1018, 1018, 1) == 0);
    /* 2: climb, STD, Mach shown. */
    in = flight(285, 12340, 12, -15); in.mach = 0.58f; s.baro_std = 1;
    run(&s, &in, 1);
    shot(dir, "climb-std", &s, &in);
    /* 3: ILS approach: LS scales, speed + altitude bugs, hPa/inHg, metric altitude. */
    isis_reset(&s, 1);
    s.bug_on[ISIS_BUG_SPD3] = 1; s.bug_value[ISIS_BUG_SPD3] = 140;
    s.bug_on[ISIS_BUG_ALT0] = 1; s.bug_value[ISIS_BUG_ALT0] = 1000;
    s.ls_active = 1; s.baro_unit_inhg = 1; s.metric_alt = 1;
    in = flight(138, 1046, 2.5f, 4); in.baro_inhg = 30.12f;
    in.loc_valid = in.gs_valid = 1; in.loc_dots = 0.6f; in.gs_dots = -1.2f;
    run(&s, &in, 1);
    shot(dir, "approach-ls", &s, &in);
    /* 4: below sea level, deviation beyond 2 dots, steep turn. */
    in = flight(165, -350, -3, 32); in.loc_dots = 2.6f; in.gs_dots = -3;
    in.loc_valid = in.gs_valid = 1; in.mda_ft = 200;
    shot(dir, "negative-altitude", &s, &in);
    /* 5: excessive pitch arrows */
    isis_reset(&s, 1);
    in = flight(180, 8000, 45, 0);
    shot(dir, "pitch-45", &s, &in);
    /* 6: power-up self-test */
    isis_reset(&s, 0);
    in = flight(0, 120, 0, 0);
    run(&s, &in, 27.2f);
    CHECK(s.du == ISIS_DU_SELFTEST);
    shot(dir, "selftest", &s, &in);
    /* 7: BUGS page (MODE), 8: MENU page, 9: ATT RST, 10: failure flags */
    isis_reset(&s, 1);
    in = flight(250, 10000, 0, 0);
    s.bug_on[ISIS_BUG_SPD3] = 1; s.bug_value[ISIS_BUG_SPD3] = 140; s.bug_on[ISIS_BUG_ALT0] = 1; s.bug_value[ISIS_BUG_ALT0] = 3000;
    press(&s, &in, ISIS_BTN_MENU); press(&s, &in, ISIS_BTN_SET_PUSH); press(&s, &in, ISIS_BTN_MINUS);
    s.clock = 0.3f;
    shot(dir, "bugs", &s, &in);
    press(&s, &in, ISIS_BTN_MENU); press(&s, &in, ISIS_BTN_SET_UP); press(&s, &in, ISIS_BTN_SET_UP);
    s.metric_alt = 1;
    shot(dir, "menu", &s, &in);
    press(&s, &in, ISIS_BTN_MENU);
    isis_button(&s, &in, ISIS_BTN_MENU, 1); run(&s, &in, 4.1f); isis_button(&s, &in, ISIS_BTN_MENU, 0);
    shot(dir, "att-reset", &s, &in);
    run(&s, &in, 9);
    in.spd_valid = 0; in.alt_valid = 0; in.att_valid = 0;
    shot(dir, "failures", &s, &in);
    /* 11-12: ISIS 2 as SND: heading rose, VOR1/VOR2 pointers, idents and DME; no data. */
    isis_reset(&s, 1);
    s.role = ISIS_ROLE_SND;
    in = flight(250, 10000, 0, 0);
    in.heading_valid = 1; in.heading_mag_deg = 47.4f;
    in.nav[0].received = 1; in.nav[0].relative_bearing_deg = 28; in.nav[0].has_dme = 1; in.nav[0].dme_nm = 12.4f;
    snprintf(in.nav[0].ident, sizeof(in.nav[0].ident), "BOR"); in.nav[0].frequency_10khz = 11350;
    in.nav[1].received = 1; in.nav[1].relative_bearing_deg = -71; in.nav[1].has_dme = 1; in.nav[1].dme_nm = 48.2f;
    snprintf(in.nav[1].ident, sizeof(in.nav[1].ident), "TOU"); in.nav[1].frequency_10khz = 11780;
    shot(dir, "snd", &s, &in);
    CHECK(pixel(512, 584, 1) > 0.8f);        /* yellow aircraft symbol at the rose centre */
    in.heading_valid = 0; in.nav[0].received = 0; in.nav[1].received = 0; in.nav[1].frequency_10khz = 0;
    shot(dir, "snd-no-data", &s, &in);
}

int main(int argc, char **argv)
{
    size_t offset = 0;
    atlas = malloc((size_t)ISIS_FONT_WIDTH * ISIS_FONT_HEIGHT);
    assert(atlas);
    for (size_t i = 0; i < sizeof(isis_font_runs); i += 2) {
        memset(atlas + offset, isis_font_runs[i + 1], isis_font_runs[i]);
        offset += isis_font_runs[i];
    }
    CHECK(offset == (size_t)ISIS_FONT_WIDTH * ISIS_FONT_HEIGHT);
    test_power();
    test_bugs();
    test_pair();
    test_menu_and_att();
    test_baro_ls_brightness();
    test_display_list();
    previews(argc > 1 ? argv[1] : NULL);
    free(atlas);
    printf("ISIS core (2 units): %d checks passed; power/self-test, BUGS, MENU, baro, LS, brightness, ATT RST, "
           "flags, SFD/SND pair and 12 previews\n", checks);
    return 0;
}
