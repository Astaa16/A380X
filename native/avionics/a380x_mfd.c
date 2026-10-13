/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD top level: sides, router, page registry, header, title bar, footer, overlays.
 * See a380x_mfd.h and docs/mfd-spec/01-framework-widgets.md sections 1, 5-10.
 */
#include "a380x_mfd.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define NOT_FOUND_BACK_S 0.5
#define MFD_NOT_FOUND_TITLE "NOT FOUND" /* spec 01 section 10.2 [PORT]: no "ERROR 404" */

/* Frame widget ids */
#define ID_SYS_SELECTOR MFD_ID(MFD_SCOPE_HEADER, 1)
#define ID_PAGE_SELECTOR(i) MFD_ID(MFD_SCOPE_HEADER, 10 + (i))
#define ID_FOOTER_BUTTON MFD_ID(MFD_SCOPE_FOOTER, 1)
#define ID_MSG_LIST_CLOSE MFD_ID(MFD_SCOPE_OVERLAY, 1)
#define ID_MSG_LIST_BLOCK MFD_ID(MFD_SCOPE_OVERLAY, 2)
#define ID_NOT_AVAIL_BLOCK MFD_ID(MFD_SCOPE_OVERLAY, 3)

/* ======================================================================================== */
/* URI                                                                                      */
/* ======================================================================================== */

static void copy_part(char *dst, size_t n, const char *src, size_t len)
{
    if (len >= n) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

int mfd_uri_parse(const char *s, MfdUri *out)
{
    const char *p, *slash;
    size_t len;
    memset(out, 0, sizeof(*out));
    if (!s || !*s) return 0;
    len = strlen(s);
    if (len >= MFD_URI_LEN) return 0;
    memcpy(out->uri, s, len + 1);
    p = s;
    slash = strchr(p, '/');
    copy_part(out->sys, sizeof(out->sys), p, slash ? (size_t)(slash - p) : strlen(p));
    if (!slash) return 1;
    p = slash + 1;
    slash = strchr(p, '/');
    copy_part(out->category, sizeof(out->category), p, slash ? (size_t)(slash - p) : strlen(p));
    if (!slash) return 1;
    p = slash + 1;
    slash = strchr(p, '/');
    copy_part(out->page, sizeof(out->page), p, slash ? (size_t)(slash - p) : strlen(p));
    if (!slash) return 1;
    copy_part(out->extra, sizeof(out->extra), slash + 1, strlen(slash + 1));
    return 1;
}

static int is_plan_category(const char *c)
{
    return strcmp(c, "active") == 0 || strcmp(c, "sec1") == 0 || strcmp(c, "sec2") == 0 || strcmp(c, "sec3") == 0;
}

static int plan_index(const char *c)
{
    if (strcmp(c, "active") == 0) return 0;
    if (strcmp(c, "sec1") == 0) return 3;
    if (strcmp(c, "sec2") == 0) return 4;
    if (strcmp(c, "sec3") == 0) return 5;
    return -1;
}

/* ======================================================================================== */
/* Registry                                                                                 */
/* ======================================================================================== */

typedef struct {
    MfdUri pat; /* sys / category / page of the pattern */
    uint16_t id;
    const char *title;
    MfdPageFn fn;
    uint32_t flags;
} PageEntry;

static PageEntry g_pages[MFD_MAX_PAGES];
static int g_page_count;

int mfd_register_page(const char *pattern, uint16_t page_id, const char *title, MfdPageFn fn, uint32_t flags)
{
    MfdUri pat;
    int i;
    if (!mfd_uri_parse(pattern, &pat) || !fn) return 0;
    for (i = 0; i < g_page_count; ++i) {
        if (strcmp(g_pages[i].pat.uri, pat.uri) == 0) break;
    }
    if (i == g_page_count) {
        if (g_page_count >= MFD_MAX_PAGES) return 0;
        g_page_count++;
    }
    g_pages[i].pat = pat;
    g_pages[i].id = page_id;
    g_pages[i].title = title ? title : "";
    g_pages[i].fn = fn;
    g_pages[i].flags = flags;
    return 1;
}

void mfd_clear_pages(void)
{
    g_page_count = 0;
}

int mfd_page_count(void)
{
    return g_page_count;
}

static int category_matches(const char *pattern, const char *cat)
{
    if (strcmp(pattern, "*") == 0) return is_plan_category(cat);
    return strcmp(pattern, cat) == 0;
}

static int find_page(const MfdUri *u)
{
    int i;
    for (i = 0; i < g_page_count; ++i) {
        const MfdUri *p = &g_pages[i].pat;
        if (strcmp(p->sys, u->sys) == 0 && category_matches(p->category, u->category) &&
            strcmp(p->page, u->page) == 0)
            return i;
    }
    return -1;
}

/* Known-unimplemented FBW routes rendered as stub pages (spec 01 section 10.2 [PORT]). */
typedef struct { const char *sys, *cat, *page, *title; int fms; } StubEntry;
static const StubEntry g_stubs[] = {
    {"fcubkup", "afs", NULL, "AFS CP", 0},
    {"fcubkup", "efis", NULL, "EFIS CP", 0},
    {"atccom", "request", NULL, "REQUEST", 0},
    {"atccom", "report-modify", NULL, "REPORT & MODIFY", 0},
    {"atccom", "emer", NULL, "EMER", 0},
    {"fms", "*", "wind", "WIND", 1},
    {"fms", "*", "what-if", "WHAT IF", 1},
};

static int find_stub(const MfdUri *u)
{
    size_t i;
    for (i = 0; i < sizeof(g_stubs) / sizeof(g_stubs[0]); ++i) {
        const StubEntry *e = &g_stubs[i];
        if (strcmp(e->sys, u->sys) != 0 || !category_matches(e->cat, u->category)) continue;
        if (e->page && strcmp(e->page, u->page) != 0) continue;
        return (int)i;
    }
    return -1;
}

/* ======================================================================================== */
/* Navigation                                                                               */
/* ======================================================================================== */

static void enter_uri(A380XMfd *m, A380XMfdSide *S, const char *uri)
{
    mfd_uri_parse(uri, &S->uri);
    S->page_index = find_page(&S->uri);
    S->stub_index = -1;
    if (S->page_index < 0) {
        S->stub_index = find_stub(&S->uri);
        if (S->stub_index >= 0) S->page_index = -2;
    }
    mfd_ui_reset_page(&S->ui); /* focus dropped without validation, menus closed (header rebuilt) */
    memset(S->page_state, 0, sizeof(S->page_state));
    S->entered = 1;
    S->entered_at = m->now;
    S->msg_list_open = 0; /* [PORT] quirk 9: MSG LIST closes on navigation */
    S->title_override[0] = '\0';
}

static void navigate_now(A380XMfd *m, A380XMfdSide *S, const char *uri, int force)
{
    if (!uri || !*uri || strlen(uri) >= MFD_URI_LEN) return;
    if (strcmp(uri, "back") == 0) {
        if (S->depth < 2) return;
        S->depth--;
        enter_uri(m, S, S->stack[S->depth - 1]);
        return;
    }
    if (!force && strcmp(uri, S->uri.uri) == 0 && strcmp(uri, "fms/active/f-pln/top") != 0 &&
        strcmp(uri, "fms/active/perf") != 0)
        return;
    if (S->depth >= MFD_NAV_STACK_CAP) { /* [PORT] quirk 16: cap 32, drop the oldest */
        memmove(S->stack[0], S->stack[1], sizeof(S->stack[0]) * (MFD_NAV_STACK_CAP - 1));
        S->depth = MFD_NAV_STACK_CAP - 1;
    }
    snprintf(S->stack[S->depth], MFD_URI_LEN, "%s", uri);
    S->depth++;
    enter_uri(m, S, uri);
}

static void request_nav(A380XMfdSide *S, const char *uri, int force)
{
    if (!uri) return;
    snprintf(S->pending, sizeof(S->pending), "%s", uri);
    S->has_pending = 1;
    S->pending_force = force;
}

static void apply_pending(A380XMfd *m, A380XMfdSide *S)
{
    if (!S->has_pending) return;
    S->has_pending = 0;
    navigate_now(m, S, S->pending, S->pending_force);
}

void mfd_navigate(MfdPageContext *ctx, const char *uri)
{
    request_nav(ctx->side_state, uri, 0);
}

void mfd_navigate_back(MfdPageContext *ctx)
{
    request_nav(ctx->side_state, "back", 0);
}

int mfd_can_go_back(const MfdPageContext *ctx)
{
    return ctx->side_state->depth > 1;
}

void mfd_navigate_all(MfdPageContext *ctx, const char *uri)
{
    int s;
    for (s = 0; s < MFD_SIDE_COUNT; ++s) request_nav(&ctx->mfd->sides[s], uri, 1);
}

void mfd_page_set_title(MfdPageContext *ctx, const char *title)
{
    snprintf(ctx->side_state->title_override, sizeof(ctx->side_state->title_override), "%s", title ? title : "");
}

void a380x_mfd_navigate(A380XMfd *m, MfdSide s, const char *uri)
{
    if ((int)s < 0 || (int)s >= MFD_SIDE_COUNT) return;
    request_nav(&m->sides[s], uri, 0);
}

void a380x_mfd_open_msg_list(A380XMfd *m, MfdSide s)
{
    if ((int)s >= 0 && (int)s < MFD_SIDE_COUNT) m->sides[s].msg_list_open = 1;
}

/* ======================================================================================== */
/* Init / input                                                                             */
/* ======================================================================================== */

void a380x_mfd_init(A380XMfd *m, struct A380XFms *fms, MfdMsgQueue *msgs, const A380XMfdSystemsHost *host)
{
    int s;
    memset(m, 0, sizeof(*m));
    m->fms = fms;
    m->msgs = msgs;
    if (host) m->host = *host;
    m->in.fms_knob = 1;
    m->in.fmc_a_healthy = m->in.fmc_b_healthy = 1;
    m->in.metric_units = 1;
    for (s = 0; s < MFD_SIDE_COUNT; ++s) {
        A380XMfdSide *S = &m->sides[s];
        S->side = (MfdSide)s;
        mfd_draw_reset(&S->dl);
        mfd_ui_init(&S->ui, (MfdSide)s, &S->dl, msgs);
        navigate_now(m, S, "fms/data/status", 1);
    }
}

static int side_ok(const A380XMfd *m, MfdSide s)
{
    return (int)s >= 0 && (int)s < MFD_SIDE_COUNT && m->in.du_on[s];
}

void mfd_pointer_move(A380XMfd *m, MfdSide s, float x, float y)
{
    MfdEvent ev;
    if (!side_ok(m, s)) return;
    memset(&ev, 0, sizeof(ev));
    ev.kind = MFD_EV_MOVE; ev.x = x; ev.y = y;
    mfd_ui_push_event(&m->sides[s].ui, &ev);
}

void mfd_pointer_button(A380XMfd *m, MfdSide s, int down)
{
    MfdEvent ev;
    if (!side_ok(m, s)) return;
    memset(&ev, 0, sizeof(ev));
    ev.kind = down ? MFD_EV_DOWN : MFD_EV_UP;
    mfd_ui_push_event(&m->sides[s].ui, &ev);
}

void mfd_pointer_wheel(A380XMfd *m, MfdSide s, int clicks)
{
    MfdEvent ev;
    if (!side_ok(m, s) || clicks == 0) return;
    memset(&ev, 0, sizeof(ev));
    ev.kind = MFD_EV_WHEEL;
    ev.wheel = (int8_t)(clicks > 100 ? 100 : clicks < -100 ? -100 : clicks);
    mfd_ui_push_event(&m->sides[s].ui, &ev);
}

void mfd_pointer_leave(A380XMfd *m, MfdSide s)
{
    MfdEvent ev;
    if (!side_ok(m, s)) return;
    memset(&ev, 0, sizeof(ev));
    ev.kind = MFD_EV_LEAVE;
    mfd_ui_push_event(&m->sides[s].ui, &ev);
}

void mfd_key(A380XMfd *m, MfdSide s, MfdKey k, char ch)
{
    MfdEvent ev;
    if (!side_ok(m, s) || (int)k < 0 || k >= MFD_KEY_COUNT) return; /* [PORT] quirk 17 */
    memset(&ev, 0, sizeof(ev));
    ev.kind = MFD_EV_KEY; ev.key = (uint8_t)k; ev.ch = ch;
    mfd_ui_push_event(&m->sides[s].ui, &ev);
}

const char *a380x_mfd_active_uri(const A380XMfd *m, MfdSide s)
{
    if ((int)s < 0 || (int)s >= MFD_SIDE_COUNT) return "";
    return m->sides[s].uri.uri;
}

int a380x_mfd_wants_keyboard(const A380XMfd *m, MfdSide s)
{
    if (!side_ok(m, s)) return 0;
    return mfd_ui_wants_keyboard(&m->sides[s].ui);
}

const MfdDrawList *a380x_mfd_display_list(const A380XMfd *m, MfdSide s)
{
    if ((int)s < 0 || (int)s >= MFD_SIDE_COUNT) return NULL;
    return &m->sides[s].dl;
}

const char *a380x_mfd_fms_label(const A380XMfdInputs *in, MfdSide s)
{
    int a = in->fmc_a_healthy, b = in->fmc_b_healthy;
    switch (in->fms_knob) {
    case 0: return b ? "FMS 2" : "FMS 2-C";
    case 1: return s == MFD_CAPT ? (a ? "FMS 1" : "FMS 1-C") : (b ? "FMS 2" : "FMS 2-C");
    case 2: return a ? "FMS 1" : "FMS 1-C";
    default: return "FMS 1";
    }
}

/* ======================================================================================== */
/* Frame: title bar                                                                         */
/* ======================================================================================== */

static void title_tag(MfdDrawList *dl, float x0, float x1, const char *text, MfdColor bg)
{
    float w = mfd_text_width(text, MFD_FS_TITLE) + 4.0f;
    float x = (x0 + x1 - w) * 0.5f;
    mfd_draw_fill_rect(dl, mfd_rect(x, 101.0f, w, 34.0f), bg);
    mfd_draw_text(dl, MFD_FONT_SZ, MFD_FS_TITLE, MFD_COL_TITLE_TEXT, MFD_ANCHOR_LEFT, x + 2.0f, 127.0f, text);
}

/* ActivePageTitleBar (spec 01 section 7). */
static void draw_title_bar(MfdDrawList *dl, const char *title, int fms_style, int eo, int tmpy, int penalty)
{
    float t1 = fms_style ? 594.0f : 598.0f, eo0 = fms_style ? 596.0f : 598.0f;
    mfd_draw_fill_rect(dl, mfd_rect(0.0f, MFD_TITLE_Y0, t1, 40.0f), MFD_COL_DARK_GREY);
    mfd_draw_fill_rect(dl, mfd_rect(eo0, MFD_TITLE_Y0, 70.0f, 40.0f), MFD_COL_DARK_GREY);
    mfd_draw_fill_rect(dl, mfd_rect(668.0f, MFD_TITLE_Y0, 100.0f, 40.0f), MFD_COL_DARK_GREY);
    if (!fms_style) mfd_draw_fill_rect(dl, mfd_rect(594.0f, MFD_TITLE_Y0, 74.0f, 40.0f), MFD_COL_DARK_GREY);
    mfd_draw_clip_push(dl, mfd_rect(0.0f, MFD_TITLE_Y0, t1, 40.0f));
    mfd_draw_text(dl, MFD_FONT_SZ, MFD_FS_TITLE, MFD_COL_TITLE_TEXT, MFD_ANCHOR_LEFT, 7.0f, 125.0f, title);
    mfd_draw_clip_pop(dl);
    if (fms_style && penalty) {
        float w = mfd_text_width("PENALTY", MFD_FS_TITLE) + 4.0f;
        mfd_draw_fill_rect(dl, mfd_rect(584.0f - w, 101.0f, w, 34.0f), MFD_COL_WHITE);
        mfd_draw_text(dl, MFD_FONT_SZ, MFD_FS_TITLE, MFD_COL_TITLE_TEXT, MFD_ANCHOR_LEFT, 586.0f - w, 127.0f,
                      "PENALTY");
    }
    if (fms_style && eo) title_tag(dl, eo0, eo0 + 70.0f, "EO", MFD_COL_AMBER);
    if (fms_style && tmpy) title_tag(dl, 668.0f, 768.0f, "TMPY", MFD_COL_YELLOW);
}

/* ======================================================================================== */
/* Frame: header (spec 01 section 6)                                                        */
/* ======================================================================================== */

typedef struct {
    const char *label;
    const MfdMenuItem *items;
    const char *const *uris;
    int n;
    float x0, x1;
    uint32_t flags;
} SelectorDef;

static const MfdMenuItem FMS_ACTIVE_ITEMS[] = {{"F-PLN", 0}, {"PERF", 0}, {"FUEL&LOAD", 0}, {"WIND", 1}, {"INIT", 0}};
static const char *const FMS_ACTIVE_URIS[] = {"fms/active/f-pln", "fms/active/perf", "fms/active/fuel-load",
                                              "fms/active/wind", "fms/active/init"};
static const MfdMenuItem FMS_POSITION_ITEMS[] = {{"MONITOR", 0}, {"REPORT", 1}, {"NAVAIDS", 0},
                                                 {"IRS", 0}, {"GNSS", 1}, {"TIME", 1}};
static const char *const FMS_POSITION_URIS[] = {"fms/position/monitor", "fms/position/report",
                                                "fms/position/navaids", "fms/position/irs",
                                                "fms/position/gnss", "fms/position/time"};
static const MfdMenuItem FMS_SEC_ITEMS[] = {{"SEC 1", 0}, {"SEC 2", 0}, {"SEC 3", 0}};
static const char *const FMS_SEC_URIS[] = {"fms/sec/index/1", "fms/sec/index/2", "fms/sec/index/3"};
static const MfdMenuItem FMS_DATA_ITEMS[] = {{"STATUS", 0}, {"WAYPOINT", 1}, {"NAVAID", 1},
                                             {"ROUTE", 1}, {"AIRPORT", 0}, {"PRINTER", 1}};
static const char *const FMS_DATA_URIS[] = {"fms/data/status", "fms/data/waypoint", "fms/data/navaid",
                                            "fms/data/route", "fms/data/airport", "fms/data/printer"};

static const MfdMenuItem ONE_ITEM[] = {{"", 0}};
static const char *const ATCCOM_URIS[] = {"atccom/connect", "atccom/request", "atccom/report-modify/position",
                                          "atccom/msg-record", "atccom/d-atis/list", "atccom/emer"};
static const char *const SURV_URIS[] = {"surv/controls", "surv/status-switching"};
static const char *const FCUBKUP_URIS[] = {"fcubkup/afs", "fcubkup/efis"};

static int category_is(const MfdUri *u, const char *a)
{
    return strcmp(u->category, a) == 0;
}

static void draw_header(A380XMfd *m, A380XMfdSide *S)
{
    MfdUi *ui = &S->ui;
    const MfdUri *u = &S->uri;
    const char *values[4];
    MfdDropdownDesc dd;
    int sel = 0, idx = -1, i;
    char dummy[4];

    /* Row 1: system selector + callsign */
    values[0] = a380x_mfd_fms_label(&m->in, S->side);
    values[1] = "ATCCOM";
    values[2] = "SURV";
    values[3] = "FCU BKUP";
    if (strcmp(u->sys, "atccom") == 0) sel = 1;
    else if (strcmp(u->sys, "surv") == 0) sel = 2;
    else if (strcmp(u->sys, "fcubkup") == 0) sel = 3;
    memset(&dd, 0, sizeof(dd));
    dd.values = values;
    dd.count = 4;
    dd.selected = sel;
    dd.align_left = 1;
    if (mfd_dropdown(ui, ID_SYS_SELECTOR, mfd_rect(0.0f, 0.0f, 192.0f, 44.0f), &dd, &idx, dummy, sizeof(dummy)) &&
        idx >= 0) {
        static const char *const targets[] = {"fms/active/init", "atccom/connect", "surv/controls", "fcubkup/afs"};
        request_nav(S, targets[idx], 0);
    }
    mfd_draw_text(&S->dl, MFD_FONT_ECAM, 24.0f, MFD_COL_WHITE, MFD_ANCHOR_LEFT, 566.0f, 23.2f,
                  m->in.atc_callsign[0] ? m->in.atc_callsign : "----------");

    /* Row 2: page selectors */
    if (strcmp(u->sys, "atccom") == 0) {
        static const char *const labels[] = {"CONNECT", "REQUEST", "REPORT\n& MODIFY", "MSG\nRECORD", "D-ATIS", "EMER"};
        static const char *const cats[] = {"connect", "request", "report-modify", "msg-record", "d-atis", "emer"};
        static const float xs[] = {0.0f, 128.5f, 257.1f, 403.1f, 524.7f, 646.4f, 768.0f};
        for (i = 0; i < 6; ++i) {
            MfdMenuItem item = {labels[i], 0};
            if (mfd_page_selector(ui, ID_PAGE_SELECTOR(i), mfd_rect(xs[i], 44.0f, xs[i + 1] - xs[i], 54.0f), labels[i],
                                  &item, 1, category_is(u, cats[i]), i == 5 ? MFD_PSEL_EMER : 0u) == 0)
                request_nav(S, ATCCOM_URIS[i], 0);
        }
    } else if (strcmp(u->sys, "surv") == 0) {
        if (mfd_page_selector(ui, ID_PAGE_SELECTOR(0), mfd_rect(0.0f, 44.0f, 192.0f, 54.0f), "CONTROLS", ONE_ITEM, 1,
                              category_is(u, "controls"), 0u) == 0)
            request_nav(S, SURV_URIS[0], 0);
        if (mfd_page_selector(ui, ID_PAGE_SELECTOR(1), mfd_rect(192.0f, 44.0f, 307.2f, 54.0f), "STATUS & SWITCHING",
                              ONE_ITEM, 1, category_is(u, "status-switching"), 0u) == 0)
            request_nav(S, SURV_URIS[1], 0);
    } else if (strcmp(u->sys, "fcubkup") == 0) {
        if (mfd_page_selector(ui, ID_PAGE_SELECTOR(0), mfd_rect(0.0f, 44.0f, 192.0f, 54.0f), "AFS CP", ONE_ITEM, 1,
                              category_is(u, "afs"), 0u) == 0)
            request_nav(S, FCUBKUP_URIS[0], 0);
        if (mfd_page_selector(ui, ID_PAGE_SELECTOR(1), mfd_rect(192.0f, 44.0f, 192.0f, 54.0f), "EFIS CP", ONE_ITEM, 1,
                              category_is(u, "efis"), 0u) == 0)
            request_nav(S, FCUBKUP_URIS[1], 0);
    } else {
        SelectorDef defs[4];
        defs[0].label = "ACTIVE"; defs[0].items = FMS_ACTIVE_ITEMS; defs[0].uris = FMS_ACTIVE_URIS; defs[0].n = 5;
        defs[1].label = "POSITION"; defs[1].items = FMS_POSITION_ITEMS; defs[1].uris = FMS_POSITION_URIS; defs[1].n = 6;
        defs[2].label = "SEC INDEX"; defs[2].items = FMS_SEC_ITEMS; defs[2].uris = FMS_SEC_URIS; defs[2].n = 3;
        defs[3].label = "DATA"; defs[3].items = FMS_DATA_ITEMS; defs[3].uris = FMS_DATA_URIS; defs[3].n = 6;
        for (i = 0; i < 4; ++i) {
            int active, k;
            if (i == 0) active = category_is(u, "active");
            else if (i == 1) active = category_is(u, "position");
            else if (i == 2) active = category_is(u, "sec") || category_is(u, "sec1") || category_is(u, "sec2") ||
                                      category_is(u, "sec3");
            else active = category_is(u, "data");
            if (strcmp(u->sys, "fms") != 0) active = 0;
            k = mfd_page_selector(ui, ID_PAGE_SELECTOR(i), mfd_rect(192.0f * (float)i, 44.0f, 192.0f, 54.0f),
                                  defs[i].label, defs[i].items, defs[i].n, active, 0u);
            if (k >= 0) request_nav(S, defs[i].uris[k], 0);
        }
    }
}

/* ======================================================================================== */
/* Frame: footer and message wrapping (spec 01 section 8)                                   */
/* ======================================================================================== */

/* Split text at '\n' and wrap at spaces so that no line exceeds max_chars. */
static int wrap_text(const char *text, int max_chars, char lines[][64], int max_lines)
{
    int n = 0;
    const char *p = text ? text : "";
    while (*p && n < max_lines) {
        const char *nl = strchr(p, '\n');
        size_t seg = nl ? (size_t)(nl - p) : strlen(p);
        while (seg > 0 && n < max_lines) {
            size_t take = seg;
            if ((int)mfd_utf8_countn(p, seg) > max_chars) {
                size_t cut = mfd_utf8_offset(p, max_chars), k = cut;
                while (k > 0 && p[k] != ' ') --k;
                take = k > 0 ? k : cut;
            }
            copy_part(lines[n++], 64, p, take);
            p += take;
            seg -= take;
            while (seg > 0 && *p == ' ') { ++p; --seg; }
        }
        if (nl) p = nl + 1;
        else break;
    }
    return n;
}

static void draw_footer(A380XMfd *m, A380XMfdSide *S, int atccom)
{
    MfdUi *ui = &S->ui;
    MfdDrawList *dl = &S->dl;
    MfdMsgQueue *q = atccom ? m->atc_msgs : m->msgs;
    const MfdMsgEntry *e = q ? mfd_msg_footer_entry(q) : NULL;
    MfdButtonDesc bd;

    mfd_draw_fill_rect(dl, mfd_rect(0.0f, MFD_FOOTER_Y0, (float)MFD_W, 2.0f), MFD_COL_DARKER_GREY);
    mfd_button_desc_init(&bd, (e || atccom) ? "CLEAR\nINFO" : "MSG\nLIST");
    if (mfd_button_ex(ui, ID_FOOTER_BUTTON, mfd_rect(5.0f, 957.0f, 100.0f, 62.0f), &bd) == MFD_BUTTON_CLICKED) {
        if (e) mfd_msg_clear_latest(q);
        else if (!atccom) S->msg_list_open = 1;
    }
    if (e) {
        char lines[4][64];
        int n = wrap_text(e->text, 39, lines, 2), i;
        float maxw = 0.0f, top;
        MfdColor bg = e->bg == MFD_MSG_BG_AMBER ? MFD_COL_AMBER : e->bg == MFD_MSG_BG_CYAN ? MFD_COL_CYAN
                                                                                            : MFD_COL_WHITE;
        for (i = 0; i < n; ++i) {
            float w = mfd_text_width(lines[i], MFD_FS_FOOTER_MSG);
            if (w > maxw) maxw = w;
        }
        top = 988.0f - 13.0f * (float)n;
        mfd_draw_fill_rect(dl, mfd_rect(120.0f, top, maxw, 26.0f * (float)n), bg);
        for (i = 0; i < n; ++i) {
            mfd_draw_text(dl, MFD_FONT_SZ, MFD_FS_FOOTER_MSG, MFD_COL_BLACK, MFD_ANCHOR_LEFT, 120.0f,
                          top + 26.0f * (float)i + MFD_FONT_BASELINE * MFD_FS_FOOTER_MSG, lines[i]);
        }
    }
}

/* ======================================================================================== */
/* Overlays (spec 01 section 9)                                                             */
/* ======================================================================================== */

static void draw_msg_list(A380XMfd *m, A380XMfdSide *S)
{
    MfdUi *ui = &S->ui;
    MfdDrawList *dl = &S->dl;
    MfdLayer old = mfd_draw_get_layer(dl);
    const MfdMsgEntry *rows[4];
    int n = m->msgs ? mfd_msg_list(m->msgs, rows, 4) : 0, k;

    mfd_draw_set_layer(dl, MFD_LAYER_OVERLAY);
    mfd_ui_hit(ui, ID_MSG_LIST_BLOCK, MFD_PART_BLOCKER, mfd_rect(0.0f, MFD_TITLE_Y0, (float)MFD_W, 852.0f));
    mfd_draw_fill_rect(dl, mfd_rect(0.0f, MFD_TITLE_Y0, (float)MFD_W, 852.0f), MFD_COL_BLACK);
    draw_title_bar(dl, "MESSAGE LIST", 1, m->in.engine_out, 0, 0);
    mfd_draw_fill_rect(dl, mfd_rect(75.0f, 213.0f, 618.0f, 2.0f), MFD_COL_WHITE);
    for (k = 0; k < n && k < 4; ++k) {
        char lines[4][64];
        int nl = wrap_text(rows[k]->text, 37, lines, 2), i;
        float y0 = 215.0f + 80.0f * (float)k;
        /* [PORT] spec 03 section 9.3: draw the text in its message colour; '\n' breaks the line. */
        MfdColor c = rows[k]->bg == MFD_MSG_BG_AMBER ? MFD_COL_AMBER : MFD_COL_WHITE;
        for (i = 0; i < nl; ++i) {
            mfd_draw_text(dl, MFD_FONT_SZ, 26.0f, c, MFD_ANCHOR_LEFT, 80.0f,
                          y0 + 5.0f + 26.0f * (float)i + MFD_FONT_BASELINE * 26.0f, lines[i]);
        }
        mfd_draw_fill_rect(dl, mfd_rect(75.0f, y0 + 78.0f, 618.0f, 2.0f), MFD_COL_WHITE);
    }
    if (mfd_button(ui, ID_MSG_LIST_CLOSE, mfd_rect(0.0f, 910.0f, 0.0f, 40.0f), "CLOSE", 0)) S->msg_list_open = 0;
    mfd_draw_set_layer(dl, old);
}

static void draw_not_avail(A380XMfdSide *S)
{
    MfdDrawList *dl = &S->dl;
    MfdLayer old = mfd_draw_get_layer(dl);
    mfd_draw_set_layer(dl, MFD_LAYER_OVERLAY);
    mfd_ui_hit(&S->ui, ID_NOT_AVAIL_BLOCK, MFD_PART_BLOCKER, mfd_rect(0.0f, MFD_BODY_Y0, (float)MFD_W, 812.0f));
    mfd_draw_fill_rect(dl, mfd_rect(0.0f, MFD_BODY_Y0, (float)MFD_W, 812.0f), MFD_COL_BLACK);
    mfd_draw_text(dl, MFD_FONT_SZ, 24.0f, MFD_COL_AMBER, MFD_ANCHOR_CENTER, 384.0f, 551.2f, "FMS PAGE NOT AVAIL");
    mfd_draw_set_layer(dl, old);
}

/* ======================================================================================== */
/* KCCU page keys (spec 01 section 10.4)                                                    */
/* ======================================================================================== */

static void handle_page_keys(A380XMfd *m, A380XMfdSide *S)
{
    MfdKey k;
    char ch;
    const char *uri = NULL;
    if (!mfd_ui_peek_key(&S->ui, &k, &ch)) return;
    switch (k) {
    case MFD_KEY_DIR: uri = "fms/active/f-pln-direct-to"; break;
    case MFD_KEY_PERF: uri = "fms/active/perf"; break;
    case MFD_KEY_INIT: uri = "fms/active/init"; break;
    case MFD_KEY_NAVAID: uri = "fms/position/navaids"; break;
    case MFD_KEY_FPLN: uri = "fms/active/f-pln/top"; break;
    case MFD_KEY_DEST: uri = "fms/active/f-pln/dest"; break;
    case MFD_KEY_SECINDEX: uri = "fms/sec/index"; break;
    case MFD_KEY_SURV: uri = "surv/controls"; break;
    case MFD_KEY_ATCCOM: uri = "atccom/connect"; break;
    case MFD_KEY_CLRINFO:
        if (m->msgs) mfd_msg_clear_latest(m->msgs);
        mfd_ui_consume_key(&S->ui);
        return;
    case MFD_KEY_MAILBOX: case MFD_KEY_ND: case MFD_KEY_KBD: case MFD_KEY_REWIND: case MFD_KEY_FORWARD:
        mfd_ui_consume_key(&S->ui); /* no MFD function [FBW-NA] */
        return;
    default:
        return; /* editing keys go to the focused field */
    }
    mfd_ui_consume_key(&S->ui);
    request_nav(S, uri, 0);
}

/* ======================================================================================== */
/* Frame                                                                                    */
/* ======================================================================================== */

static void upper_copy(char *dst, size_t n, const char *src)
{
    size_t i;
    for (i = 0; i + 1 < n && src[i]; ++i) dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = '\0';
}

static void side_frame(A380XMfd *m, A380XMfdSide *S)
{
    MfdUi *ui = &S->ui;
    MfdPageContext ctx;
    const char *title = MFD_NOT_FOUND_TITLE;
    uint32_t flags = 0;
    char title_buf[96];
    int not_avail, eo, tmpy, penalty;

    mfd_draw_reset(&S->dl);
    if (!m->in.du_on[S->side]) {
        mfd_ui_suspend(ui);
        return;
    }
    ui->kbd_mode = m->in.kbd_on[S->side];
    mfd_ui_begin(ui, m->now);
    apply_pending(m, S);
    handle_page_keys(m, S);
    apply_pending(m, S);

    not_avail = m->in.fms_failed[S->side] && strcmp(S->uri.sys, "fms") == 0;

    memset(&ctx, 0, sizeof(ctx));
    ctx.mfd = m;
    ctx.side_state = S;
    ctx.side = S->side;
    ctx.ui = ui;
    ctx.dl = &S->dl;
    ctx.fms = m->fms;
    ctx.msgs = m->msgs;
    ctx.host = &m->host;
    ctx.in = &m->in;
    ctx.uri = &S->uri;
    ctx.plan = plan_index(S->uri.category);
    ctx.entered = S->entered;
    ctx.now = m->now;
    ctx.dt = ui->dt;
    ctx.page_time = m->now - S->entered_at;
    ctx.state = S->page_state;
    ctx.body = mfd_rect(0.0f, MFD_BODY_Y0, (float)MFD_W, MFD_BODY_Y1 - MFD_BODY_Y0);
    if (S->page_index >= 0) {
        const PageEntry *pe = &g_pages[S->page_index];
        ctx.page_id = pe->id;
        ctx.flags = pe->flags;
        ctx.title = pe->title;
        flags = pe->flags;
        title = pe->title;
        if (!not_avail) pe->fn(&ctx);
    } else if (S->page_index == -2) {
        const StubEntry *se = &g_stubs[S->stub_index];
        ctx.page_id = MFD_PAGE_ID_STUB;
        flags = se->fms ? (MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY) : 0u;
        title = se->title;
    } else {
        ctx.page_id = MFD_PAGE_ID_NOT_FOUND;
    }
    if (not_avail && ui->focus_id) mfd_ui_reset_page(ui);
    S->entered = 0;

    /* Title bar */
    if (!(flags & MFD_PAGE_NO_TITLE)) {
        int fms_style = (flags & MFD_PAGE_FMS) != 0;
        if (S->title_override[0]) {
            snprintf(title_buf, sizeof(title_buf), "%s", S->title_override);
        } else if (flags & MFD_PAGE_TITLE_CATEGORY) {
            char cat[24];
            upper_copy(cat, sizeof(cat), S->uri.category);
            snprintf(title_buf, sizeof(title_buf), "%s/%s", cat, title);
        } else {
            snprintf(title_buf, sizeof(title_buf), "%s", title);
        }
        eo = fms_style && m->in.engine_out;
        tmpy = fms_style && (flags & MFD_PAGE_TMPY) && strcmp(S->uri.sys, "fms") == 0 &&
               strcmp(S->uri.category, "active") == 0 && m->in.tmpy_exists;
        penalty = fms_style && (flags & MFD_PAGE_PENALTY) && strcmp(S->uri.sys, "fms") == 0 &&
                  m->in.fuel_penalty_active;
        draw_title_bar(&S->dl, title_buf, fms_style, eo, tmpy, penalty);
    }
    S->title_override[0] = '\0';
    if (!(flags & MFD_PAGE_NO_FOOTER)) draw_footer(m, S, (flags & MFD_PAGE_ATCCOM_FOOTER) != 0);
    draw_header(m, S);
    if (not_avail) draw_not_avail(S);
    if (S->msg_list_open) draw_msg_list(m, S);
    mfd_ui_end(ui);

    /* NOT FOUND: back after 500 ms (spec 01 section 9.4). */
    if (S->page_index == -1 && !S->has_pending && m->now - S->entered_at >= NOT_FOUND_BACK_S && S->depth >= 2)
        request_nav(S, "back", 0);
    apply_pending(m, S);
}

void a380x_mfd_frame(A380XMfd *m, const A380XMfdInputs *in)
{
    int s;
    if (in) m->in = *in;
    m->now = m->in.now;
    for (s = 0; s < MFD_SIDE_COUNT; ++s) side_frame(m, &m->sides[s]);
}
