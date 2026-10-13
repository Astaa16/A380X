/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD SEC INDEX. Translation of the pinned FBW pages/FMS/SEC/MfdFmsSecIndex.tsx and
 * MfdFmsSecIndex.scss (docs/mfd-spec/03-fms-init-fuel-data-sec-msg.md section 8), border-box layout
 * inside the TopTabNavigator content box of the 138 px body (spec 03 boxes re-based +4 px; the
 * lower button table keeps its flex-grow centring in the 4 px shorter remainder).
 *
 * The SEC operations are FMS-core calls (a380x_fms_sec_*); the route overview is read from the
 * lateral plan store the FMS is hooked to (fms->fpln.plans, a380x_fms_fpln.h).
 */
#include "a380x_mfd_page_sec.h"
#include "a380x_mfd_page_init.h"
#include "a380x_fms_fpln.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SEC_ID(n) MFD_ID(MFD_PAGE_ID_SEC_INDEX, (n))
#define SEC_TAB_IDS 100 /* per-tab widget id stride */

/* ---- Route overview (MfdFmsSecIndexDataStore.onNewData, :93-118) --------------------------- */

static int sec_is_runway(const A380XFplnLeg *l)
{
    return l->wp_desc == A380X_WPDESC_RUNWAY;
}

/* Builds the overview of SEC plan `plan`. Returns the leg count of the lateral plan, or -1 when
 * no lateral store is hooked. */
static int sec_build_overview(const A380XFms *fms, int plan, MfdSecIndexState *st)
{
    const A380XFplnRoute *r;
    const A380XFplnLeg *legs[A380X_FPLN_MAX_ELEMENTS];
    int n = 0, i, end;

    st->route_count = 0;
    if (!fms->fpln.plans) return -1;
    r = a380x_fpln_route(fms->fpln.plans, plan, 0);
    if (!r) return 0;
    end = r->first_missed_appr_index < r->count ? r->first_missed_appr_index : r->count;
    for (i = 0; i < end; ++i) {
        if (a380x_fpln_is_disco(&r->el[i])) continue;
        legs[n++] = &r->el[i];
    }
    for (i = 0; i < n && st->route_count < MFD_SEC_ROUTE_MAX; ++i) {
        const A380XFplnLeg *leg = legs[i], *next = i + 1 < n ? legs[i + 1] : NULL;
        MfdSecRouteEntry *e;
        if (next && strcmp(next->annotation, leg->annotation) == 0 && leg->annotation[0] != '\0' &&
            !sec_is_runway(leg))
            continue;
        if ((r->origin_icao[0] && strcmp(leg->ident, r->origin_icao) == 0) ||
            (r->dest_icao[0] && strcmp(leg->ident, r->dest_icao) == 0))
            continue;
        if (next && leg->procedure_ident[0] && strcmp(next->procedure_ident, leg->procedure_ident) == 0 &&
            strcmp(next->annotation, leg->annotation) != 0)
            continue;
        e = &st->route[st->route_count++];
        if (leg->annotation[0] == '\0') {
            snprintf(e->annotation, sizeof(e->annotation), "DIR");
            snprintf(e->ident, sizeof(e->ident), "%s", leg->ident);
        } else if (sec_is_runway(leg)) {
            snprintf(e->annotation, sizeof(e->annotation), "RWY");
            snprintf(e->ident, sizeof(e->ident), "%s", strlen(leg->ident) > 4 ? leg->ident + 4 : "");
        } else {
            snprintf(e->annotation, sizeof(e->annotation), "%s", leg->annotation);
            snprintf(e->ident, sizeof(e->ident), "%s", leg->ident);
        }
    }
    return r->count;
}

/* ---- Drawing helpers ------------------------------------------------------------------------ */

/* Button whose label is a 1- or 2-line block followed by '*' 10 px to its right (FBW
 * "<span>LABEL</span><span>*</span>" in a space-between row, padding-right 2). */
static int sec_star_button(MfdUi *ui, MfdId id, MfdRect r, const char *l1, const char *l2, MfdColor color,
                           uint32_t flags)
{
    MfdButtonDesc bd;
    float bw, block_w, x0, top, cx;
    int lines = l2 ? 2 : 1, clicked;
    MfdColor c;

    mfd_button_desc_init(&bd, "");
    bd.flags = flags;
    bd.pad_right = 2.0f;
    clicked = mfd_button_ex(ui, id, r, &bd) == MFD_BUTTON_CLICKED;
    if (flags & MFD_BTN_HIDDEN) return 0;
    c = (flags & MFD_BTN_DISABLED) ? MFD_COL_DARK_GREY : color;
    bw = mfd_text_width(l1, 22.0f);
    if (l2 && mfd_text_width(l2, 22.0f) > bw) bw = mfd_text_width(l2, 22.0f);
    block_w = bw + 10.0f + mfd_text_width("*", 22.0f);
    x0 = r.x + 14.0f + ((r.w - 18.0f) - block_w) * 0.5f; /* content box 14 .. w-4 */
    top = r.y + 11.0f + (r.h - 18.0f - 22.0f * (float)lines) * 0.5f;
    cx = x0 + bw * 0.5f;
    mfd_text(ui, cx, mfd_baseline(top, 22.0f), MFD_ANCHOR_CENTER, 22.0f, c, l1);
    if (l2) mfd_text(ui, cx, mfd_baseline(top + 22.0f, 22.0f), MFD_ANCHOR_CENTER, 22.0f, c, l2);
    mfd_text(ui, x0 + bw + 10.0f, mfd_baseline(top + 11.0f * (float)(lines - 1), 22.0f), MFD_ANCHOR_LEFT, 22.0f, c,
             "*");
    return clicked;
}

/* [PORT] TMPY title tag on SEC INDEX whenever a temporary plan exists (spec 03 section 1.2). The
 * frame only tags `active` pages, so the page draws it above the title bar. */
static void sec_tmpy_tag(MfdDrawList *dl)
{
    MfdLayer old = mfd_draw_get_layer(dl);
    float w = mfd_text_width("TMPY", MFD_FS_TITLE) + 4.0f, x = (668.0f + 768.0f - w) * 0.5f;
    mfd_draw_set_layer(dl, MFD_LAYER_OVERFLOW);
    mfd_draw_fill_rect(dl, mfd_rect(x, 101.0f, w, 34.0f), MFD_COL_YELLOW);
    mfd_draw_text(dl, MFD_FONT_SZ, MFD_FS_TITLE, MFD_COL_TITLE_TEXT, MFD_ANCHOR_LEFT, x + 2.0f, 127.0f, "TMPY");
    mfd_draw_set_layer(dl, old);
}

/* ---- One SEC tab ------------------------------------------------------------------------------ */

static void sec_tab(MfdPageContext *ctx, MfdSecIndexState *st, int n, MfdRect content)
{
    MfdUi *ui = ctx->ui;
    A380XFms *fms = ctx->fms;
    A380XFmsSecInfo info;
    int plan = A380X_FMS_SEC_PLAN(n), legs, exists, can_swap, i, base_id = 10 + SEC_TAB_IDS * (n - 1);
    int *line = &st->start_line[n - 1], k;
    char t[96], uri[MFD_URI_LEN];
    float x0 = content.x, y0 = content.y; /* inner area origin (spec 14, 182) */
    float rx = x0 + 6.0f, ry = y0 + 75.0f + 21.0f, col_w = 543.0f / 4.0f, row_h = 458.0f / 11.0f;
    float lower_top = y0 + 575.0f, lower_bot = content.y + content.h, ly, bx = x0 + 567.5f;
    MfdMenuItem import_items[3];
    int src[3];
    MfdButtonDesc bd;

    a380x_fms_sec_info(fms, n, &info);
    legs = sec_build_overview(fms, plan, st);
    exists = info.exists && (legs < 0 ? info.has_city_pair : legs > 0); /* hasSecondary && legCount > 0 */
    can_swap = exists && info.can_swap;
    if (!exists) st->route_count = 0;
    if (*line > 0 && (st->route_count + 1) / 2 - *line < MFD_SEC_ROUTE_LINES)
        *line = (st->route_count + 1) / 2 > MFD_SEC_ROUTE_LINES ? (st->route_count + 1) / 2 - MFD_SEC_ROUTE_LINES : 0;

    /* Title: city pair (28) and CREATED line (20) in a 75 px column. */
    if (exists && info.has_city_pair) {
        snprintf(t, sizeof(t), "%s / %s", info.from, info.to);
        mfd_label(ui, MFD_TXT_LABEL_BIGGEST, MFD_COL_WHITE, MFD_ANCHOR_CENTER, x0 + 370.0f, y0 + 4.75f, t);
    }
    if (exists && a380x_fms_get_text(fms, plan, A380X_FMS_T_SEC_CREATED, t, sizeof(t)))
        mfd_label(ui, MFD_TXT_LABEL, MFD_COL_WHITE, MFD_ANCHOR_LEFT, x0, y0 + 46.25f, t);

    /* Route overview: box 14..569 x 257..757, grid 4 x 11 inside 20 px / 5 px padding. */
    mfd_frame_rect(ui, mfd_rect(x0, y0 + 75.0f, 555.0f, 500.0f), 1.0f, MFD_COL_LIGHT_GREY);
    for (i = 0; i < MFD_SEC_ROUTE_LINES * 2; ++i) {
        int e = *line * 2 + i, row = i / 2, c = (i % 2) * 2;
        float cy = ry + row_h * (float)row + (row_h - 28.0f) * 0.5f, cx = rx + col_w * (float)c;
        if (e >= st->route_count) break;
        mfd_hrule(ui, cx, cx + 2.0f * col_w, cy + 27.0f, 1.0f, MFD_COL_LIGHT_GREY);
        mfd_text(ui, cx, mfd_baseline(cy, 22.0f), MFD_ANCHOR_LEFT, 22.0f, MFD_COL_WHITE, st->route[e].annotation);
        mfd_text(ui, cx + col_w, mfd_baseline(cy, 26.0f), MFD_ANCHOR_LEFT, 26.0f, MFD_COL_WHITE, st->route[e].ident);
    }

    /* Right column: IMPORT, CPNY F-PLN (top), F-PLN .. WHAT IF (bottom-aligned at 757). */
    src[0] = 0;
    src[1] = n == 1 ? 2 : 1;
    src[2] = n == 3 ? 2 : 3;
    import_items[0].label = "ACTIVE*";
    import_items[0].disabled = !a380x_fms_plan_exists(fms, A380X_FMS_PLAN_ACTIVE);
    import_items[1].label = src[1] == 1 ? "SEC 1*" : "SEC 2*";
    import_items[1].disabled = !a380x_fms_sec_exists(fms, src[1]); /* [PORT] grey out absent sources */
    import_items[2].label = src[2] == 2 ? "SEC 2*" : "SEC 3*";
    import_items[2].disabled = !a380x_fms_sec_exists(fms, src[2]);
    mfd_button_desc_init(&bd, "IMPORT");
    bd.menu = import_items;
    bd.menu_count = 3;
    k = mfd_button_ex(ui, SEC_ID(base_id + 1), mfd_rect(x0 + 560.0f, y0 + 75.0f, 175.0f, 40.0f), &bd);
    if (k == 0) a380x_fms_sec_copy_from_active(fms, n);
    else if (k == 1 || k == 2) a380x_fms_sec_copy_from_sec(fms, src[k], n);
    a380x_mfd_cpny_fpln_button(ctx, SEC_ID(base_id + 2), mfd_rect(x0 + 560.0f, y0 + 120.0f, 175.0f, 62.0f), plan);

    if (mfd_button(ui, SEC_ID(base_id + 3), mfd_rect(bx, y0 + 310.0f, 160.0f, 40.0f), "F-PLN", 0)) {
        snprintf(uri, sizeof(uri), "fms/sec%d/f-pln", n);
        mfd_navigate(ctx, uri);
    }
    if (mfd_button(ui, SEC_ID(base_id + 4), mfd_rect(bx, y0 + 355.0f, 160.0f, 40.0f), "PERF",
                   exists ? 0u : MFD_BTN_DISABLED)) {
        snprintf(uri, sizeof(uri), "fms/sec%d/perf", n);
        mfd_navigate(ctx, uri);
    }
    mfd_button(ui, SEC_ID(base_id + 5), mfd_rect(bx, y0 + 400.0f, 160.0f, 40.0f), "WIND", MFD_BTN_DISABLED);
    if (mfd_button(ui, SEC_ID(base_id + 6), mfd_rect(bx, y0 + 445.0f, 160.0f, 40.0f), "FUEL&LOAD",
                   exists ? 0u : MFD_BTN_DISABLED)) {
        snprintf(uri, sizeof(uri), "fms/sec%d/fuel-load", n);
        mfd_navigate(ctx, uri);
    }
    if (mfd_button(ui, SEC_ID(base_id + 7), mfd_rect(bx, y0 + 490.0f, 160.0f, 40.0f), "INIT", 0)) {
        if (!a380x_fms_sec_exists(fms, n)) a380x_fms_sec_init(fms, n);
        snprintf(uri, sizeof(uri), "fms/sec%d/init", n);
        mfd_navigate(ctx, uri);
    }
    mfd_button(ui, SEC_ID(base_id + 8), mfd_rect(bx, y0 + 535.0f, 160.0f, 40.0f), "WHAT IF", MFD_BTN_DISABLED);

    /* Lower table (flex-grow, centred): DELETE* | scroll | SWAP ACTIVE* / XFR TO MAILBOX; PRINT*. */
    ly = lower_top + ((lower_bot - lower_top) - 150.0f) * 0.5f;
    if (sec_star_button(ui, SEC_ID(base_id + 9), mfd_rect(x0, ly, 160.0f, 60.0f), "DELETE", NULL, MFD_COL_WHITE,
                        exists ? 0u : MFD_BTN_DISABLED)) {
        a380x_fms_sec_delete(fms, n);
        *line = 0;
    }
    if (mfd_icon_button(ui, SEC_ID(base_id + 10), mfd_rect(x0 + 215.0f, ly, 60.0f, 60.0f), MFD_ICON_DOUBLE_DOWN,
                        (st->route_count + 1) / 2 - *line <= MFD_SEC_ROUTE_LINES))
        (*line)++;
    if (mfd_icon_button(ui, SEC_ID(base_id + 11), mfd_rect(x0 + 280.0f, ly, 60.0f, 60.0f), MFD_ICON_DOUBLE_UP,
                        *line <= 0) && *line > 0)
        (*line)--;
    if (sec_star_button(ui, SEC_ID(base_id + 12), mfd_rect(x0 + 395.0f, ly, 160.0f, 60.0f), "SWAP", "ACTIVE",
                        MFD_COL_AMBER, can_swap ? 0u : MFD_BTN_HIDDEN))
        a380x_fms_sec_swap(fms, n);
    mfd_button(ui, SEC_ID(base_id + 13), mfd_rect(x0 + 395.0f, ly + 90.0f, 160.0f, 60.0f), "XFR TO\nMAILBOX",
               MFD_BTN_DISABLED);
    sec_star_button(ui, SEC_ID(base_id + 14), mfd_rect(bx, lower_bot - 75.0f, 160.0f, 60.0f), "PRINT", NULL,
                    MFD_COL_WHITE, MFD_BTN_DISABLED);
}

static void sec_index_page(MfdPageContext *ctx)
{
    static const char *const titles[] = {"SEC 1", "SEC 2", "SEC 3"};
    MfdSecIndexState *st = MFD_PAGE_STATE(ctx, MfdSecIndexState);
    MfdTabsDesc td;
    MfdRect content, clip;
    int k;

    if (!ctx->fms) return;
    if (ctx->entered) {
        const char *e = ctx->uri->extra;
        st->tab = (e[0] >= '1' && e[0] <= '3' && e[1] == '\0') ? e[0] - '1' : 0;
    }
    if (a380x_fms_tmpy_exists(ctx->fms)) sec_tmpy_tag(ctx->dl);

    mfd_tabs_desc_init(&td);
    td.slant_deg = 25.0f;
    td.selected_text = MFD_COL_WHITE;
    k = mfd_top_tabs(ctx->ui, SEC_ID(1), ctx->body, titles, 3, st->tab, &td, &content, &clip);
    if (k >= 0 && k < 3) st->tab = k; /* the tab changes, not the URI */
    mfd_draw_clip_push(ctx->dl, clip);
    sec_tab(ctx, st, st->tab + 1, content);
    mfd_draw_clip_pop(ctx->dl);
}

void a380x_mfd_page_sec_register(void)
{
    /* [PORT] title "SEC INDEX" as on the aircraft (FBW: SEC/SEC INDEX). */
    mfd_register_page("fms/sec/index", MFD_PAGE_ID_SEC_INDEX, "SEC INDEX", sec_index_page, MFD_PAGE_FMS);
}

#undef SEC_ID
#undef SEC_TAB_IDS
