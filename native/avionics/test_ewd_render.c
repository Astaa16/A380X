/* SPDX-License-Identifier: GPL-3.0-or-later
 * Headless regression tests and SVG previews for the actual native EWD renderer.
 * Optional arguments: inactive.svg normal.svg warning.svg.
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { float r, g, b; } Color;
typedef enum { CAPTURE_RECT, CAPTURE_LINE, CAPTURE_TEXT, CAPTURE_POLYGON } CaptureKind;
typedef struct {
    CaptureKind kind;
    float x1, y1, x2, y2, width, size, spacing;
    Color color;
    int anchor, polygon;
    char text[128];
} Capture;
typedef struct {
    float x[512], y[512];
    size_t count;
} CapturedPolygon;

static Capture captures[16000];
static size_t capture_count;
static float line_width = 1;
static CapturedPolygon polygons[64];
static size_t polygon_count;
static Color fill_color;
static int polygon_open;

#define GL_TRIANGLE_FAN 0x0006

static Capture *capture(CaptureKind kind, float x1, float y1,
                        float x2, float y2, Color color)
{
    Capture *item;
    assert(capture_count < sizeof(captures) / sizeof(captures[0]));
    assert(isfinite(x1) && isfinite(y1) && isfinite(x2) && isfinite(y2));
    assert(isfinite(color.r) && isfinite(color.g) && isfinite(color.b));
    item = &captures[capture_count++];
    memset(item, 0, sizeof(*item));
    item->kind = kind;
    item->x1 = x1; item->y1 = y1; item->x2 = x2; item->y2 = y2;
    item->color = color;
    item->width = line_width;
    return item;
}

static void glLineWidth(float width)
{
    assert(isfinite(width) && width > 0);
    line_width = width;
}

static void rectangle(float left, float bottom, float right, float top, Color color)
{
    capture(CAPTURE_RECT, left, bottom, right, top, color);
}

static void line(float x1, float y1, float x2, float y2, Color color)
{
    capture(CAPTURE_LINE, x1, y1, x2, y2, color);
}

static void glColor3f(float red, float green, float blue)
{
    fill_color = (Color){red, green, blue};
}

static void glBegin(unsigned mode)
{
    assert(mode == GL_TRIANGLE_FAN && !polygon_open);
    assert(polygon_count < sizeof(polygons) / sizeof(polygons[0]));
    polygons[polygon_count].count = 0;
    polygon_open = 1;
}

static void glVertex2f(float x, float y)
{
    CapturedPolygon *polygon = &polygons[polygon_count];
    assert(polygon_open && isfinite(x) && isfinite(y));
    assert(polygon->count < sizeof(polygon->x) / sizeof(polygon->x[0]));
    polygon->x[polygon->count] = x;
    polygon->y[polygon->count++] = y;
}

static void glEnd(void)
{
    CapturedPolygon *polygon = &polygons[polygon_count];
    Capture *item;
    assert(polygon_open && polygon->count >= 3);
    item = capture(CAPTURE_POLYGON, polygon->x[0], polygon->y[0],
                   polygon->x[0], polygon->y[0], fill_color);
    item->polygon = (int)polygon_count++;
    polygon_open = 0;
}

static void ewd_font_draw(float x, float y, const char *text, float size,
                          Color color, float spacing, int anchor)
{
    Capture *item = capture(CAPTURE_TEXT, x, y, x, y, color);
    assert(size > 0 && isfinite(size) && isfinite(spacing));
    assert(anchor >= 0 && anchor <= 2);
    assert(strlen(text) < sizeof(item->text));
    item->size = size; item->spacing = spacing; item->anchor = anchor;
    strcpy(item->text, text);
}

#include "a380x_ewd_render.inc"

static int close_to(float a, float b) { return fabsf(a - b) < .02f; }
static int same_color(Color a, Color b)
{
    return fabsf(a.r - b.r) < .00001f && fabsf(a.g - b.g) < .00001f
        && fabsf(a.b - b.b) < .00001f;
}

static void render(const EwdData *data)
{
    capture_count = 0;
    polygon_count = 0;
    polygon_open = 0;
    line_width = 1;
    draw_ewd(data, 0, 0);
    assert(!polygon_open);
}

static size_t text_count(const char *text)
{
    size_t i, count = 0;
    for (i = 0; i < capture_count; ++i)
        if (captures[i].kind == CAPTURE_TEXT && !strcmp(captures[i].text, text)) ++count;
    return count;
}

/* Expectations use FBW's top-left source coordinates, while the capture uses GL. */
static const Capture *require_text(const char *text, float x, float y, Color color)
{
    size_t i;
    for (i = 0; i < capture_count; ++i) {
        const Capture *item = &captures[i];
        if (item->kind == CAPTURE_TEXT && !strcmp(item->text, text)
                && close_to(item->x1, x) && close_to(item->y1, 1024 - y)
                && same_color(item->color, color)) return item;
    }
    fprintf(stderr, "Missing EWD text '%s' at (%.2f, %.2f)\n", text, x, y);
    abort();
}

static void require_line(float x1, float y1, float x2, float y2, Color color, float width)
{
    size_t i;
    for (i = 0; i < capture_count; ++i) {
        const Capture *item = &captures[i];
        if (item->kind == CAPTURE_LINE && close_to(item->x1, x1)
                && close_to(item->y1, 1024 - y1) && close_to(item->x2, x2)
                && close_to(item->y2, 1024 - y2) && same_color(item->color, color)
                && close_to(item->width, width)) return;
    }
    fprintf(stderr, "Missing EWD line (%.2f, %.2f)-(%.2f, %.2f)\n", x1, y1, x2, y2);
    abort();
}

static void require_rect(float x, float y, float width, float height, Color color)
{
    size_t i;
    for (i = 0; i < capture_count; ++i) {
        const Capture *item = &captures[i];
        if (item->kind == CAPTURE_RECT && close_to(item->x1, x)
                && close_to(item->y1, 1024 - y - height)
                && close_to(item->x2, x + width) && close_to(item->y2, 1024 - y)
                && same_color(item->color, color)) return;
    }
    fprintf(stderr, "Missing EWD rectangle (%.2f, %.2f, %.2f, %.2f)\n", x, y, width, height);
    abort();
}

static void require_canvas_bounds(void)
{
    size_t i;
    for (i = 0; i < capture_count; ++i) {
        const Capture *item = &captures[i];
        assert(item->x1 >= 0 && item->x1 <= 768);
        assert(item->x2 >= 0 && item->x2 <= 768);
        assert(item->y1 >= 0 && item->y1 <= 1024);
        assert(item->y2 >= 0 && item->y2 <= 1024);
        if (item->kind == CAPTURE_POLYGON) {
            const CapturedPolygon *polygon = &polygons[item->polygon];
            size_t vertex;
            for (vertex = 0; vertex < polygon->count; ++vertex) {
                assert(polygon->x[vertex] >= 0 && polygon->x[vertex] <= 768);
                assert(polygon->y[vertex] >= 0 && polygon->y[vertex] <= 1024);
            }
        }
    }
    require_rect(0, 0, 768, 1024, (Color){0, 0, 0});
    require_rect(8, 379, 752, 4, (Color){196.f / 255, 198.f / 255, 207.f / 255});
}

static void set_message(EwdLine *target, const char *text, int color)
{
    assert(strlen(text) < sizeof(target->text));
    strcpy(target->text, text);
    target->color = color;
}

static EwdData inactive_data(void)
{
    EwdData data = {0};
    data.powered = 1;
    data.memo_count = 4;
    set_message(&data.memo[0], "APU BLEED", EWD_COLOR_GREEN);
    set_message(&data.memo[1], "SEAT BELTS", EWD_COLOR_GREEN);
    set_message(&data.memo[2], "PARK BRK ON", EWD_COLOR_GREEN);
    set_message(&data.memo[3], "ELEC EXT PWR", EWD_COLOR_GREEN);
    return data;
}

static EwdData normal_data(void)
{
    static const float n1[] = {61.1f, 72.2f, 83.3f, 94.4f};
    static const float thrust[] = {21.1f, 43.2f, 65.3f, 87.4f};
    static const float egt[] = {420, 530, 640, 750};
    EwdData data = {0};
    int i;
    data.powered = data.thrust_limit_valid = 1;
    data.thrust_limit = 95.7f;
    strcpy(data.thrust_mode, "CLB");
    strcpy(data.bleed, "PACKS");
    for (i = 0; i < 4; ++i) {
        EwdEngine *engine = &data.engine[i];
        engine->active = engine->active_valid = 1;
        engine->running = engine->running_valid = 1;
        engine->available_thrust = 100;
        engine->available_valid = 1;
        engine->n1 = n1[i]; engine->egt = egt[i]; engine->thrust = thrust[i];
        engine->n1_valid = engine->egt_valid = engine->thrust_valid = 1;
        engine->commanded_thrust = thrust[i] + 3;
        engine->throttle = thrust[i] + 2;
        engine->commanded_valid = engine->throttle_valid = 1;
    }
    data.memo_count = 2;
    set_message(&data.memo[0], "SEAT BELTS", EWD_COLOR_GREEN);
    set_message(&data.memo[1], "ENG A.ICE", EWD_COLOR_GREEN);
    return data;
}

static EwdData warning_data(void)
{
    EwdData data = normal_data();
    data.engine[1].egt = 945;
    data.engine[1].fire = 1;
    data.warning_count = 3;
    set_message(&data.warning[0], "ENG 2 FIRE", EWD_COLOR_RED);
    set_message(&data.warning[1], "THR LEVER 2.....IDLE", EWD_COLOR_CYAN);
    set_message(&data.warning[2], "ENG MASTER 2.....OFF", EWD_COLOR_CYAN);
    return data;
}

static void test_unpowered(void)
{
    EwdData data = normal_data();
    data.powered = 0;
    render(&data);
    assert(capture_count == 1);
    require_rect(0, 0, 768, 1024, (Color){0, 0, 0});
}

static void test_inactive_reference(void)
{
    static const float x[] = {93, 262, 497, 668};
    EwdData data = inactive_data();
    int i;
    assert(same_color(EWD_WHITE, (Color){1, 1, 1}));
    assert(same_color(EWD_GREEN, (Color){0, 1, 0}));
    assert(same_color(EWD_CYAN, (Color){0, 1, 1}));
    assert(same_color(EWD_AMBER, (Color){230.f / 255, 128.f / 255, 0}));
    assert(same_color(EWD_RED, (Color){1, 0, 0}));
    assert(same_color(EWD_GREY, (Color){79.f / 255, 84.f / 255, 103.f / 255}));
    render(&data);
    assert(text_count("THR XX") == 4);
    assert(text_count("XX") == 9); /* Thrust limit plus N1 and EGT for every engine. */
    for (i = 0; i < 4; ++i) {
        require_text("THR XX", x[i] + 57, 75, EWD_AMBER);
        require_text("XX", x[i] + 48, 236, EWD_AMBER);
        require_text("XX", x[i] + 21, 349.7f, EWD_AMBER);
    }
    require_text("THR", 386, 121, EWD_WHITE);
    require_text("N1", 386, 220, EWD_WHITE);
    require_text("EGT", 384, 316, EWD_WHITE);
    require_text("\xc2\xb0" "C", 384, 339, EWD_CYAN);
    require_canvas_bounds();
}

static void require_thrust_needle(float engine_x, float thrust, int reverse)
{
    float angle = (reverse ? 230 - thrust : 230 + 2.2f * thrust) * (float)M_PI / 180;
    float radius = reverse ? 58 : 64;
    require_line(engine_x + 2, 123,
                 engine_x + 2 + sinf(angle) * radius * 1.1f,
                 123 - cosf(angle) * radius * 1.1f, EWD_GREEN, 3);
}

static void require_egt_needle(float engine_x, float egt, Color color)
{
    float angle = (270 + .18f * egt) * (float)M_PI / 180;
    require_line(engine_x + 4 + sinf(angle) * 68 * .75f,
                 338 - cosf(angle) * 68 * .75f,
                 engine_x + 4 + sinf(angle) * 68 * 1.15f,
                 338 - cosf(angle) * 68 * 1.15f, color, 3);
}

static const CapturedPolygon *thrust_pie(float engine_x)
{
    size_t i;
    for (i = 0; i < capture_count; ++i) {
        const Capture *item = &captures[i];
        if (item->kind == CAPTURE_POLYGON && same_color(item->color, EWD_GREY)
                && close_to(item->x1, engine_x + 2) && close_to(item->y1, 1024 - 123))
            return &polygons[item->polygon];
    }
    return NULL;
}

static void require_thrust_pie(float engine_x, float available)
{
    const CapturedPolygon *pie = thrust_pie(engine_x);
    float first_angle = 239.24f * (float)M_PI / 180;
    float last_angle = (230 + available * 2.2f) * (float)M_PI / 180;
    size_t i;
    assert(pie != NULL && pie->count > 3);
    assert(close_to(pie->x[1], engine_x + 2 + 64 * sinf(first_angle)));
    assert(close_to(pie->y[1], 1024 - 123 + 64 * cosf(first_angle)));
    assert(close_to(pie->x[pie->count - 1], engine_x + 2 + 64 * sinf(last_angle)));
    assert(close_to(pie->y[pie->count - 1], 1024 - 123 + 64 * cosf(last_angle)));
    /* It fills the whole source-radius sector from its center, not a rim band. */
    for (i = 1; i < pie->count; ++i)
        assert(close_to(hypotf(pie->x[i] - pie->x[0], pie->y[i] - pie->y[0]), 64));
}

static void test_live_independent_parameters(void)
{
    static const float x[] = {93, 262, 497, 668};
    static const char *const n1[] = {"61", "72", "83", "94"};
    static const char *const thrust[] = {"21", "43", "65", "87"};
    static const char *const fraction[] = {"1", "2", "3", "4"};
    static const char *const egt[] = {"420", "530", "640", "750"};
    EwdData data = normal_data();
    int i;
    render(&data);
    assert(text_count("THR XX") == 0 && text_count("XX") == 0);
    require_text("CLB", 330, 30, EWD_CYAN);
    require_text("95", 399, 28, EWD_GREEN);
    require_text("7", 431, 28, EWD_GREEN);
    assert(close_to(require_text("PACKS", 750, 30, EWD_GREEN)->size, 26));
    for (i = 0; i < 4; ++i) {
        require_text(thrust[i], x[i] + 50, 170, EWD_GREEN);
        require_text(fraction[i], x[i] + 80, 170, EWD_GREEN);
        require_text(n1[i], x[i] + 6, 236, EWD_GREEN);
        require_text(fraction[i], x[i] + 36, 236, EWD_GREEN);
        require_text(egt[i], x[i] + 37, 349.7f, EWD_GREEN);
        require_thrust_needle(x[i], data.engine[i].thrust, 0);
        require_egt_needle(x[i], data.engine[i].egt, EWD_GREEN);
        require_thrust_pie(x[i], 100);
    }
    require_canvas_bounds();
}

static void test_available_thrust_sector_and_annunciation(void)
{
    EwdData data = normal_data();
    data.engine[0].available_thrust = 60;
    render(&data);
    require_thrust_pie(93, 60);
    data.engine[0].running = 0;
    render(&data);
    assert(thrust_pie(93) == NULL);
    data.engine[0].avail = data.engine[0].avail_valid = 1;
    render(&data);
    require_thrust_pie(93, 60);
    require_text("AVAIL", 51, 118, EWD_GREEN);
    data.engine[0].available_valid = 0;
    render(&data);
    assert(thrust_pie(93) == NULL);
    require_canvas_bounds();
}

static void test_egt_overlimit_colors(void)
{
    static const float x[] = {93, 262, 497, 668};
    static const float egt[] = {849, 851, 900, 999};
    static const char *const readout[] = {"849", "851", "900", "999"};
    const Color colors[] = {EWD_GREEN, EWD_AMBER, EWD_RED, EWD_RED};
    EwdData data = normal_data();
    int i;
    for (i = 0; i < 4; ++i) data.engine[i].egt = egt[i];
    render(&data);
    for (i = 0; i < 4; ++i) {
        require_text(readout[i], x[i] + 37, 349.7f, colors[i]);
        require_egt_needle(x[i], egt[i], colors[i]);
    }
    require_line(497 - 74, 113, 497 - 74, 41, EWD_AMBER, 2);
    require_line(668 - 74, 113, 668 - 74, 41, EWD_AMBER, 2);
    require_canvas_bounds();
}

static void test_invalid_engine_values(void)
{
    const float invalid[] = {NAN, INFINITY, -INFINITY, NAN};
    EwdData data = normal_data();
    int i;
    for (i = 0; i < 4; ++i) {
        data.engine[i].thrust = data.engine[i].n1 = data.engine[i].egt = invalid[i];
        data.engine[i].commanded_thrust = data.engine[i].throttle = invalid[i];
    }
    data.thrust_limit = NAN;
    render(&data);
    assert(text_count("THR XX") == 4 && text_count("XX") == 9);
    require_canvas_bounds(); /* Every capture also rejects nonfinite coordinates. */
    data = normal_data();
    for (i = 0; i < 4; ++i)
        data.engine[i].n1_valid = data.engine[i].egt_valid = data.engine[i].thrust_valid = 0;
    render(&data);
    assert(text_count("THR XX") == 4 && text_count("XX") == 8);
    require_canvas_bounds();
    data = normal_data();
    data.engine[0].n1 = NAN;
    data.engine[1].egt = INFINITY;
    data.engine[2].thrust = NAN;
    data.engine[3].throttle = data.engine[3].commanded_thrust = INFINITY;
    render(&data);
    require_text("XX", 129, 236, EWD_AMBER);
    require_text("XX", 283, 349.7f, EWD_AMBER);
    require_text("THR XX", 554, 75, EWD_AMBER);
    /* Lost THR must preserve a valid engine-3 N1 in its fallback gauge. */
    require_text("83", 545, 236, EWD_GREEN);
    require_text("87", 718, 170, EWD_GREEN);
    require_canvas_bounds();
}

static void test_reverse_only_on_inner_engines(void)
{
    EwdData data = normal_data();
    int i;
    for (i = 0; i < 4; ++i) data.engine[i].reverse = 1;
    data.engine[1].reverse_deployed = 1;
    render(&data);
    assert(text_count("REV 1") == 0 && text_count("REV 4") == 0);
    require_text("REV 2", 221, 118, EWD_GREEN);
    require_text("REV 3", 456, 118, EWD_AMBER);
    require_thrust_needle(93, data.engine[0].thrust, 0);
    require_thrust_needle(262, data.engine[1].thrust, 1);
    require_thrust_needle(497, data.engine[2].thrust, 1);
    require_thrust_needle(668, data.engine[3].thrust, 0);
    require_canvas_bounds();
    data.engine[1].reverse = data.engine[2].reverse = 0;
    render(&data);
    /* An impossible outboard reverse bit must not suppress the thrust limit. */
    require_text("CLB", 330, 30, EWD_CYAN);
}

static void test_dynamic_ecam_columns_and_warning_priority(void)
{
    const Color divider = {211.f / 255, 211.f / 255, 211.f / 255};
    EwdData data = inactive_data();
    int i;
    render(&data);
    for (i = 0; i < data.memo_count; ++i) {
        const Capture *item = require_text(data.memo[i].text, 392, 419 + i * 30, EWD_GREEN);
        assert(close_to(item->size, 24) && close_to(item->spacing, 1.75f) && item->anchor == 0);
    }
    require_rect(383, 395, 2, 123, divider);
    data.memo_count = 1;
    set_message(&data.memo[0], "T.O INHIBIT", EWD_COLOR_CYAN);
    data.limit_count = 2;
    set_message(&data.limit[0], "LIMITATION", EWD_COLOR_WHITE);
    set_message(&data.limit[1], "MAX SPEED 250 KT", EWD_COLOR_AMBER);
    render(&data);
    assert(text_count("APU BLEED") == 0 && text_count("SEAT BELTS") == 0);
    require_text("T.O INHIBIT", 392, 419, EWD_CYAN);
    require_text("LIMITATION", 8, 419, EWD_WHITE);
    require_text("MAX SPEED 250 KT", 8, 449, EWD_AMBER);
    require_rect(383, 395, 2, 63, divider);
    data.warning_count = 2;
    set_message(&data.warning[0], "ENG 2 FIRE", EWD_COLOR_RED);
    set_message(&data.warning[1], "THR LEVER 2.....IDLE", EWD_COLOR_CYAN);
    render(&data);
    assert(text_count("T.O INHIBIT") == 0 && text_count("LIMITATION") == 0);
    require_text("ENG 2 FIRE", 30, 419, EWD_RED);
    require_text("THR LEVER 2.....IDLE", 30, 453, EWD_CYAN);
    require_canvas_bounds();
    data.warning_count = 0;
    render(&data);
    require_text("T.O INHIBIT", 392, 419, EWD_CYAN);
    assert(text_count("ENG 2 FIRE") == 0);
}

static void xml_text(FILE *file, const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    for (; *cursor; ++cursor) {
        switch (*cursor) {
            case '&': fputs("&amp;", file); break;
            case '<': fputs("&lt;", file); break;
            case '>': fputs("&gt;", file); break;
            case '"': fputs("&quot;", file); break;
            default: fputc(*cursor, file); break;
        }
    }
}

static unsigned color_byte(float value) { return (unsigned)lroundf(value * 255); }

static void svg_preview(const char *path, const EwdData *data)
{
    static const char *const anchors[] = {"start", "middle", "end"};
    size_t i;
    FILE *file = fopen(path, "w");
    if (!file) { perror(path); exit(EXIT_FAILURE); }
    render(data);
    fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"768\" height=\"1024\" "
          "viewBox=\"0 0 768 1024\">\n"
          "<style>@font-face{font-family:'Ecam';src:url('EWD_FONT_DATA_URI') format('truetype');}"
          "text{font-family:'Ecam',monospace;font-weight:normal;}</style>\n", file);
    for (i = 0; i < capture_count; ++i) {
        const Capture *item = &captures[i];
        unsigned r = color_byte(item->color.r), g = color_byte(item->color.g), b = color_byte(item->color.b);
        if (item->kind == CAPTURE_RECT)
            fprintf(file, "<rect x=\"%.3f\" y=\"%.3f\" width=\"%.3f\" height=\"%.3f\" "
                    "fill=\"#%02x%02x%02x\"/>\n", item->x1, 1024 - item->y2,
                    item->x2 - item->x1, item->y2 - item->y1, r, g, b);
        else if (item->kind == CAPTURE_LINE)
            fprintf(file, "<line x1=\"%.3f\" y1=\"%.3f\" x2=\"%.3f\" y2=\"%.3f\" "
                    "stroke=\"#%02x%02x%02x\" stroke-width=\"%.3f\"/>\n",
                    item->x1, 1024 - item->y1, item->x2, 1024 - item->y2, r, g, b, item->width);
        else if (item->kind == CAPTURE_POLYGON) {
            const CapturedPolygon *polygon = &polygons[item->polygon];
            size_t vertex;
            fprintf(file, "<polygon fill=\"#%02x%02x%02x\" points=\"", r, g, b);
            for (vertex = 0; vertex < polygon->count; ++vertex)
                fprintf(file, "%.3f,%.3f ", polygon->x[vertex], 1024 - polygon->y[vertex]);
            fputs("\"/>\n", file);
        } else {
            fprintf(file, "<text x=\"%.3f\" y=\"%.3f\" font-family=\"Ecam\" font-size=\"%.3f\" "
                    "letter-spacing=\"%.3f\" text-anchor=\"%s\" fill=\"#%02x%02x%02x\">",
                    item->x1, 1024 - item->y1, item->size, item->spacing, anchors[item->anchor], r, g, b);
            xml_text(file, item->text);
            fputs("</text>\n", file);
        }
    }
    fputs("</svg>\n", file);
    if (fclose(file) != 0) { perror(path); exit(EXIT_FAILURE); }
}

int main(int argc, char **argv)
{
    EwdData inactive = inactive_data(), normal = normal_data(), warning = warning_data();
    test_unpowered();
    test_inactive_reference();
    test_live_independent_parameters();
    test_available_thrust_sector_and_annunciation();
    test_egt_overlimit_colors();
    test_invalid_engine_values();
    test_reverse_only_on_inner_engines();
    test_dynamic_ecam_columns_and_warning_priority();
    if (argc > 1) svg_preview(argv[1], &inactive);
    if (argc > 2) svg_preview(argv[2], &normal);
    if (argc > 3) svg_preview(argv[3], &warning);
    puts("a380x EWD renderer tests passed (8 scenarios)");
    return 0;
}
