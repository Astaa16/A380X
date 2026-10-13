/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD ATCCOM pages and the offline ATC datalink model. Translation of the pinned FBW
 * MFD/ATCCOM/AtcDatalinkSystem.ts and pages/ATCCOM/{MfdAtccomConnect, MfdAtccomMsgRecord*,
 * MfdAtccomDAtis, DAtisBlock, MfdAtccomDAtisReceived}.tsx (spec 06 sections 6-9).
 *
 * Geometry: FBW constants under border-box. Under border-box CONNECT, MSG RECORD and D-ATIS
 * RECEIVED fit the body as authored. D-ATIS LIST: the three 250 px blocks end at 888 and FBW puts
 * PRINT ALL / UPDATE ALL at 891..953; [PORT] fit: the two buttons are raised 3 px (888..950) so they
 * do not run under the footer. The NOT YET IMPLEMENTED veil is not drawn ([PORT] Q16).
 */
#include "a380x_mfd_page_atccom.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define AID(n) MFD_ID(MFD_PAGE_ID_ATCCOM_CONNECT, (n))
#define RID(n) MFD_ID(MFD_PAGE_ID_ATCCOM_MSG_RECORD, (n))
#define DID(n) MFD_ID(MFD_PAGE_ID_ATCCOM_DATIS, (n))

#define ATC_STATIONS 8
#define ATC_AUTO_UPDATE_S 60.0

/* ======================================================================================== */
/* Datalink model (AtcDatalinkSystem + MessageStorage, port subset)                          */
/* ======================================================================================== */

typedef struct {
    char letter;
    char time[8];                       /* "HHMMZ" */
    char text[A380X_DATIS_TEXT_LEN];
} AtcReport;

typedef struct {
    char icao[5];
    int count;                          /* 0..2, rep[0] newest */
    AtcReport rep[2];
    unsigned int stamp;                 /* LRU */
} AtcStation;

typedef struct {
    char icao[5];
    int type;
    int requested, autoupdate;
    char last_read;
    char status[24];
    uint32_t pending_id;
    int pending_auto;
    double next_auto;
    int hide_report;                    /* FBW: ICAO change clears letter / time / text until updated */
} AtcBlock;

static A380XMfdAtccomHost atc_host;
static int atc_host_valid;
static AtcBlock atc_blocks[3];
static AtcStation atc_stations[ATC_STATIONS];
static unsigned int atc_stamp;
static uint32_t atc_next_id = 1;
static char atc_city[3][8];
static int atc_city_seen;
static double atc_now = -1.0, atc_utc = NAN;
static MfdMsgQueue *atc_q;

static void atc_init_block(int i, const char *icao)
{
    AtcBlock *b = &atc_blocks[i];
    memset(b, 0, sizeof(*b));
    if (icao) snprintf(b->icao, sizeof(b->icao), "%.4s", icao);
    b->type = i == 0 ? A380X_ATIS_DEP : A380X_ATIS_ARR;
}

void a380x_mfd_atccom_reset(void)
{
    int i;
    for (i = 0; i < 3; ++i) atc_init_block(i, NULL);
    memset(atc_stations, 0, sizeof(atc_stations));
    memset(atc_city, 0, sizeof(atc_city));
    atc_city_seen = 0;
}

void a380x_mfd_atccom_set_host(const A380XMfdAtccomHost *host)
{
    if (host) {
        atc_host = *host;
        atc_host_valid = 1;
    } else {
        memset(&atc_host, 0, sizeof(atc_host));
        atc_host_valid = 0;
    }
}

static AtcStation *atc_station(const char *icao, int create)
{
    int i, lru = 0;
    if (!icao || !icao[0]) return NULL;
    for (i = 0; i < ATC_STATIONS; ++i)
        if (atc_stations[i].icao[0] && strcmp(atc_stations[i].icao, icao) == 0) return &atc_stations[i];
    if (!create) return NULL;
    for (i = 0; i < ATC_STATIONS; ++i) {
        if (!atc_stations[i].icao[0]) {
            lru = i;
            break;
        }
        if (atc_stations[i].stamp < atc_stations[lru].stamp) lru = i;
    }
    memset(&atc_stations[lru], 0, sizeof(atc_stations[lru]));
    snprintf(atc_stations[lru].icao, sizeof(atc_stations[lru].icao), "%.4s", icao);
    atc_stations[lru].stamp = ++atc_stamp;
    return &atc_stations[lru];
}

static void atc_add_message(MfdMsgId id)
{
    if (atc_q) mfd_msg_add(atc_q, id, NULL, NULL, NULL, NULL, NULL);
}

static void atc_send(int i, int auto_update)
{
    AtcBlock *b = &atc_blocks[i];
    uint32_t id = atc_next_id++;
    b->requested = 1;
    b->pending_id = id;
    b->pending_auto = auto_update;
    if (!auto_update) snprintf(b->status, sizeof(b->status), "SENDING");
    if (atc_host_valid && atc_host.atis_request) {
        atc_host.atis_request(atc_host.ctx, b->icao, b->type, auto_update ? 0 : 1, id);
    } else { /* no provider = datis_source off: ComFailed */
        A380XDatisAnswer a;
        memset(&a, 0, sizeof(a));
        a.request_id = id;
        a.status = A380X_DATIS_COM_FAILED;
        snprintf(a.icao, sizeof(a.icao), "%s", b->icao);
        a380x_mfd_atccom_deliver(&a);
    }
}

/* requestAtis(index): ignored if no ICAO or already requested. */
static void atc_request(int i)
{
    if (i < 0 || i > 2 || !atc_blocks[i].icao[0] || atc_blocks[i].requested) return;
    atc_send(i, 0);
}

void a380x_mfd_atccom_deliver(const A380XDatisAnswer *a)
{
    int i;
    AtcBlock *b = NULL;
    if (!a) return;
    for (i = 0; i < 3; ++i) {
        if (atc_blocks[i].requested && atc_blocks[i].pending_id == a->request_id) {
            b = &atc_blocks[i];
            break;
        }
    }
    if (!b) return; /* stale answer (block re-initialised) */
    b->requested = 0;
    b->pending_id = 0;
    if (a->status == A380X_DATIS_NEW && a->letter) {
        AtcStation *st = atc_station(b->icao, 1);
        int explicit_req = !b->pending_auto;
        int same = st->count > 0 && st->rep[0].letter == a->letter;
        if (!same || explicit_req) {
            AtcReport *r;
            if (!same) { /* prepend, keep 2 */
                st->rep[1] = st->rep[0];
                if (st->count < 2) st->count++;
            }
            r = &st->rep[0];
            memset(r, 0, sizeof(*r));
            r->letter = a->letter;
            if (!isnan(atc_utc)) {
                unsigned int m = (unsigned int)(fmod(fabs(atc_utc), 86400.0) / 60.0) % 1440u;
                snprintf(r->time, sizeof(r->time), "%02u%02uZ", m / 60u, m % 60u);
            }
            for (i = 0; a->text[i] && i < A380X_DATIS_TEXT_LEN - 1; ++i)
                r->text[i] = (char)toupper((unsigned char)a->text[i]);
            r->text[i] = '\0';
            st->stamp = ++atc_stamp;
            b->hide_report = 0;
        }
        if (explicit_req) b->status[0] = '\0';
    } else if (!b->pending_auto) {
        if (a->status == A380X_DATIS_COM_FAILED) {
            snprintf(b->status, sizeof(b->status), "SEND\nFAILED");
        } else {
            snprintf(b->status, sizeof(b->status), "NO REPLY");
            atc_add_message(MFD_MSG_ATC_DATIS_NO_REPLY);
        }
    }
    b->pending_auto = 0;
}

void a380x_mfd_atccom_update(double now_s, double utc_s, MfdMsgQueue *q)
{
    int i;
    if (q) atc_q = q;
    if (!isnan(utc_s)) atc_utc = utc_s;
    if (now_s == atc_now) return;
    atc_now = now_s;
    /* initAtis on fmsOrigin / fmsDestination / fmsAlternate changes */
    if (atc_host_valid && atc_host.city_pair) {
        char c[3][8];
        memset(c, 0, sizeof(c));
        atc_host.city_pair(atc_host.ctx, c[0], c[1], c[2]);
        for (i = 0; i < 3; ++i) {
            c[i][7] = '\0';
            if (!atc_city_seen || strcmp(c[i], atc_city[i]) != 0) {
                if (atc_city_seen || c[i][0]) atc_init_block(i, c[i]);
                memcpy(atc_city[i], c[i], sizeof(atc_city[i]));
            }
        }
        atc_city_seen = 1;
    }
    if (atc_host_valid && atc_host.atis_poll) {
        A380XDatisAnswer a;
        int guard = 0;
        while (guard++ < 8 && atc_host.atis_poll(atc_host.ctx, &a)) a380x_mfd_atccom_deliver(&a);
    }
    /* auto update every 60 s for ARR blocks (ATC.ts:1023-1048) */
    for (i = 0; i < 3; ++i) {
        AtcBlock *b = &atc_blocks[i];
        if (!b->autoupdate || b->type != A380X_ATIS_ARR || !b->icao[0]) continue;
        if (now_s >= b->next_auto) {
            b->next_auto = now_s + ATC_AUTO_UPDATE_S;
            if (!b->requested) atc_send(i, 1);
        }
    }
}

int a380x_mfd_atccom_block(int index, A380XDatisBlockInfo *out)
{
    const AtcBlock *b;
    const AtcStation *st;
    if (index < 0 || index > 2 || !out) return 0;
    b = &atc_blocks[index];
    memset(out, 0, sizeof(*out));
    snprintf(out->icao, sizeof(out->icao), "%s", b->icao);
    out->type = b->type;
    out->requested = b->requested;
    out->autoupdate = b->autoupdate;
    snprintf(out->status, sizeof(out->status), "%s", b->status);
    st = atc_station(b->icao, 0);
    if (st && st->count > 0 && !b->hide_report) {
        out->letter = st->rep[0].letter;
        snprintf(out->time, sizeof(out->time), "%s", st->rep[0].time);
        out->report_count = st->count;
    }
    return 1;
}

static void atc_toggle_auto_update(int i)
{
    AtcBlock *b = &atc_blocks[i];
    if (b->type != A380X_ATIS_ARR || !b->icao[0]) return;
    b->autoupdate = !b->autoupdate;
    b->next_auto = atc_now + ATC_AUTO_UPDATE_S;
}

/* ======================================================================================== */
/* Page helpers                                                                             */
/* ======================================================================================== */

static void atc_tick(MfdPageContext *ctx)
{
    double utc = NAN;
    if (ctx->host && ctx->host->read_value) utc = ctx->host->read_value(ctx->host->ctx, ctx->side, A380X_ATC_K_UTC_S);
    a380x_mfd_atccom_update(ctx->now, utc, ctx->mfd ? ctx->mfd->atc_msgs : NULL);
}

/* Entry errors of D-ATIS fields go to the ATCCOM queue (showAtcErrorMessage): the UI queued the
 * NX message in the FMS queue during this field call; move it. */
static void atc_move_error(MfdPageContext *ctx, unsigned int v0)
{
    MfdMsgQueue *q = ctx->msgs, *aq = ctx->mfd ? ctx->mfd->atc_msgs : NULL;
    MfdMsgEntry e;
    const char *nl;
    if (!q || q->version == v0 || q->count == 0) return;
    e = q->e[0];
    if (e.cleared || (e.id != MFD_MSG_FORMAT_ERROR && e.id != MFD_MSG_ENTRY_OUT_OF_RANGE &&
                      e.id != MFD_MSG_NOT_IN_DATABASE && e.id != MFD_MSG_NOT_YET_IMPLEMENTED))
        return;
    mfd_msg_remove_text(q, e.text);
    nl = strchr(e.text, '\n');
    if (aq) mfd_msg_add(aq, e.id, NULL, nl ? nl + 1 : NULL, NULL, NULL, NULL);
}

/* AdscButton with the CONNECT overrides (.mfd-adsc-button width/height: border-box W x H, border 4
 * inset, padding 5). Active upper label green 30, active lower white 24, inactive Ecam 20 grey. */
static int atc_adsc_button(MfdUi *ui, MfdId id, MfdRect r, const char *lt, const char *lf, int state)
{
    MfdDrawList *dl = ui->dl;
    float up_fs = state ? 30.0f : 20.0f, lo_fs = state ? 20.0f : 24.0f, top, cx = r.x + r.w * 0.5f;
    int part;
    mfd_draw_box(dl, r, MFD_COL_BLACK, 4.0f, mfd_ui_is_hovered(ui, id) ? MFD_COL_CYAN : MFD_COL_LIGHT_GREY,
                 MFD_BEVEL_SUNKEN);
    top = r.y + 9.0f + ((r.h - 18.0f) - (up_fs + lo_fs)) * 0.5f;
    mfd_draw_text(dl, state ? MFD_FONT_SZ : MFD_FONT_ECAM, up_fs, state ? MFD_COL_GREEN : MFD_COL_DARK_GREY,
                  MFD_ANCHOR_CENTER, cx, top + MFD_FONT_BASELINE * up_fs, lt);
    mfd_draw_text(dl, state ? MFD_FONT_ECAM : MFD_FONT_SZ, lo_fs, state ? MFD_COL_DARK_GREY : MFD_COL_WHITE,
                  MFD_ANCHOR_CENTER, cx, top + up_fs + MFD_FONT_BASELINE * lo_fs, lf);
    mfd_ui_hit(ui, id, MFD_PART_MAIN, r);
    return mfd_ui_clicked_part(ui, id, &part) && part == MFD_PART_MAIN;
}

/* ======================================================================================== */
/* CONNECT (spec 06 section 7)                                                              */
/* ======================================================================================== */

static void atc_connect_page(MfdPageContext *ctx)
{
    MfdAtcConnectState *s = MFD_PAGE_STATE(ctx, MfdAtcConnectState);
    MfdUi *ui = ctx->ui;
    MfdDropdownDesc dd;
    static const char *const no_values[1] = {""};
    int idx = -1;
    char dummy[8];

    atc_tick(ctx);
    /* row 1 (margin-top 30): col-1 305 (padding-right 20), col-2 167, col-3 296 */
    mfd_text(ui, 285.0f, 196.6f, MFD_ANCHOR_RIGHT, 22.0f, MFD_COL_WHITE, "NOTIFY TO ATC :");
    memset(&dd, 0, sizeof(dd));
    dd.values = no_values;
    dd.count = 0; /* [PORT] no ATC centre known offline */
    dd.selected = -1;
    dd.digits = 4;
    mfd_dropdown(ui, AID(1), mfd_rect(305.0f, 168.0f, 150.0f, 44.0f), &dd, &idx, dummy, sizeof(dummy));
    mfd_button(ui, AID(2), mfd_rect(555.0f, 170.0f, 120.0f, 40.0f), "NOTIFY", MFD_BTN_DISABLED);
    mfd_text(ui, 305.0f, 239.6f, MFD_ANCHOR_LEFT, 22.0f, MFD_COL_WHITE, "NOTIFY");

    /* section 1 (margin-top 60 -> 304, padding-bottom 20, border 476..478); ACTIVE / NEXT ATC empty */
    mfd_text(ui, 285.0f, 323.1f, MFD_ANCHOR_RIGHT, 22.0f, MFD_COL_WHITE, "ACTIVE ATC :");
    mfd_text(ui, 285.0f, 378.1f, MFD_ANCHOR_RIGHT, 22.0f, MFD_COL_WHITE, "NEXT ATC :");
    /* col-3 flex-shrunk to 482.4..768: 260 px buttons centred */
    mfd_button(ui, AID(3), mfd_rect(495.2f, 324.0f, 260.0f, 40.0f), "DISCONNECT ALL", MFD_BTN_DISABLED);
    mfd_button(ui, AID(4), mfd_rect(495.2f, 394.0f, 260.0f, 62.0f), "MODIFY\nMAX UPLINK DELAY", MFD_BTN_DISABLED);
    mfd_draw_fill_rect(ctx->dl, mfd_rect(0.0f, 476.0f, 768.0f, 2.0f), MFD_COL_LIGHT_GREY);

    /* ADS-C row (margin-top 30 -> 508): two 300 px blocks centred (84..384, 384..684) */
    mfd_text(ui, 234.0f, 525.6f, MFD_ANCHOR_CENTER, 22.0f, MFD_COL_WHITE, "ADS-C");
    if (atc_adsc_button(ui, AID(5), mfd_rect(134.0f, 540.0f, 200.0f, 70.0f), "ARMED", "OFF", s->adsc_armed))
        s->adsc_armed = !s->adsc_armed;
    mfd_text(ui, 534.0f, 525.6f, MFD_ANCHOR_CENTER, 22.0f, MFD_COL_WHITE, "ADS-C EMERGENCY");
    if (atc_adsc_button(ui, AID(6), mfd_rect(479.0f, 540.0f, 110.0f, 70.0f), "ARMED", "OFF", s->adsc_emer_armed))
        s->adsc_emer_armed = !s->adsc_emer_armed;

    /* connected ground stations (row 640..662) and the empty list box (672..937) */
    mfd_text(ui, 110.0f, 657.6f, MFD_ANCHOR_LEFT, 22.0f, MFD_COL_WHITE, "ADS-C CONNECTED GROUND STATIONS :");
    mfd_text(ui, 588.2f, 657.0f, MFD_ANCHOR_LEFT, 20.0f, MFD_COL_WHITE, "NONE");
    mfd_draw_stroke_rect(ctx->dl, mfd_rect(217.0f, 672.0f, 360.0f, 265.0f), 2.0f, MFD_COL_LIGHT_GREY);
}

/* ======================================================================================== */
/* MSG RECORD (spec 06 section 8)                                                           */
/* ======================================================================================== */

static void atc_msg_record_page(MfdPageContext *ctx)
{
    MfdUi *ui = ctx->ui;
    atc_tick(ctx);
    /* absolute frame top 90 (228), height 275 border-box, 1 px white top/bottom borders */
    mfd_draw_fill_rect(ctx->dl, mfd_rect(0.0f, 228.0f, 768.0f, 1.0f), MFD_COL_WHITE);
    mfd_draw_fill_rect(ctx->dl, mfd_rect(0.0f, 502.0f, 768.0f, 1.0f), MFD_COL_WHITE);
    if (mfd_button(ui, RID(1), mfd_rect(65.0f, 279.0f, 205.0f, 64.0f), "ALL MSG", 0))
        mfd_navigate(ctx, "atccom/msg-record/all-msg");
    if (mfd_button(ui, RID(2), mfd_rect(65.0f, 394.0f, 205.0f, 64.0f), "MONITORED MSG", 0))
        mfd_navigate(ctx, "atccom/msg-record/monitored-msg");
}

/* List frame shared by ALL MSG and MONITORED MSG: list 0..688 (flex-shrunk 710), scrollbar
 * 688..707 (margin 15 0), nav column 710..768 with four 42 px icon buttons from y 161. */
static void atc_record_list_frame(MfdPageContext *ctx, int id_base)
{
    MfdUi *ui = ctx->ui;
    MfdRect sb = mfd_rect(688.0f, 153.0f, 19.4f, 712.0f);
    static const MfdIcon icons[4] = {MFD_ICON_DOUBLE_UP, MFD_ICON_SINGLE_UP, MFD_ICON_SINGLE_DOWN, MFD_ICON_DOUBLE_DOWN};
    int k;
    mfd_draw_box(ctx->dl, sb, MFD_COL_SURV_STATUS_BG, 2.0f, MFD_COL_LIGHT_GREY, MFD_BEVEL_RAISED);
    for (k = 0; k < 4; ++k) /* [PORT] offline record is empty: the list always fits */
        mfd_icon_button(ui, RID(id_base + k), mfd_rect(710.0f, 161.0f + 42.0f * (float)k, 42.0f, 42.0f), icons[k], 1);
}

static void atc_all_msg_page(MfdPageContext *ctx)
{
    atc_tick(ctx);
    atc_record_list_frame(ctx, 10);
    mfd_button(ctx->ui, RID(20), mfd_rect(0.0f, 883.0f, 190.0f, 64.0f), "ERASE ALL", MFD_BTN_DISABLED);
    mfd_button(ctx->ui, RID(21), mfd_rect(578.0f, 880.0f, 190.0f, 64.0f), "PRINT", MFD_BTN_DISABLED);
}

static void atc_monitored_msg_page(MfdPageContext *ctx)
{
    atc_tick(ctx);
    atc_record_list_frame(ctx, 30);
}

static void atc_expand_page(MfdPageContext *ctx)
{
    atc_tick(ctx);
    if (mfd_button(ctx->ui, RID(40), mfd_rect(0.0f, 883.0f, 190.0f, 64.0f), "RETURN\nTO LIST", 0))
        mfd_navigate(ctx, "atccom/msg-record/all-msg");
    mfd_button(ctx->ui, RID(41), mfd_rect(578.0f, 880.0f, 190.0f, 64.0f), "PRINT", MFD_BTN_DISABLED);
}

/* ======================================================================================== */
/* D-ATIS LIST (spec 06 sections 9.1-9.2)                                                   */
/* ======================================================================================== */

/* truncateAtis: greedy words into 5 lines (DAtisBlock.datisBlocklineLengths 44,44,44,44,29; the FBW
 * test `len + word <= max - 1` ignores the separating space), "  ......" when truncated. */
static int atc_truncate(const char *text, char lines[5][64])
{
    static const int maxlen[5] = {44, 44, 44, 44, 29};
    const char *p = text;
    int li, truncated;
    memset(lines, 0, 5 * 64);
    for (li = 0; li < 5; ++li) {
        for (;;) {
            const char *w;
            size_t wl, ll;
            while (*p == ' ') ++p;
            if (!*p) break;
            w = p;
            while (*p && *p != ' ') ++p;
            wl = (size_t)(p - w);
            ll = strlen(lines[li]);
            if ((int)(ll + wl) > maxlen[li] - 1 || ll + wl + 2 > 63) {
                p = w;
                break;
            }
            if (ll) lines[li][ll++] = ' ';
            memcpy(lines[li] + ll, w, wl);
            lines[li][ll + wl] = '\0';
        }
    }
    while (*p == ' ') ++p;
    truncated = *p != '\0';
    if (truncated) {
        size_t ll = strlen(lines[4]);
        snprintf(lines[4] + ll, 64 - ll, "  ......");
    }
    return truncated;
}

static void atc_new_atis_icon(MfdDrawList *dl, float x, float y)
{
    /* NewAtisIcon.tsx: 40 x 30 white card, 1.5 px black frame and envelope lines (viewBox 40 x 28) */
    float sy = 30.0f / 28.0f;
    mfd_draw_fill_rect(dl, mfd_rect(x, y, 40.0f, 30.0f), MFD_COL_WHITE);
    mfd_draw_stroke_rect(dl, mfd_rect(x + 1.25f, y + 1.25f * sy, 37.5f, 26.5f * sy), 1.5f, MFD_COL_BLACK);
    {
        MfdPoint p[4];
        p[0].x = x + 2.0f; p[0].y = y + 2.0f * sy;
        p[1].x = x + 18.0f; p[1].y = y + 17.0f * sy;
        p[2].x = x + 22.0f; p[2].y = y + 17.0f * sy;
        p[3].x = x + 38.0f; p[3].y = y + 2.0f * sy;
        mfd_draw_polyline(dl, p, 4, 1.5f, 0, MFD_COL_BLACK);
    }
    mfd_draw_line(dl, x + 2.0f, y + 27.0f * sy, x + 15.8f, y + 15.3f * sy, 1.5f, MFD_COL_BLACK);
    mfd_draw_line(dl, x + 38.0f, y + 27.0f * sy, x + 24.2f, y + 15.3f * sy, 1.5f, MFD_COL_BLACK);
}

static void atc_auto_update_icon(MfdDrawList *dl, float x, float y)
{
    /* AutoUpdateIcon.tsx (viewBox 40 x 37 drawn 38 x 38): arc r 18.24 about (20, 18.5) from the lower
     * right (36.8, 25.84) clockwise round to (37.24, 12.54), 3 px #0f0, and a filled arrow head. */
    float s = 0.95f, cx = x + 20.0f * s, cy = y + 18.5f * s;
    float a0 = atan2f(25.84f - 18.5f, 36.8f - 20.0f), a1 = atan2f(12.54f - 18.5f, 37.24f - 20.0f) + 2.0f * (float)M_PI;
    mfd_draw_arc(dl, cx, cy, 18.24f * s, a0, a1, 3.0f, MFD_COL_GREEN);
    mfd_draw_triangle(dl, x + 30.5f * s, y + 13.68f * s, x + 39.0f * s, y + 13.68f * s, x + 39.0f * s, y + 3.8f * s,
                      MFD_COL_GREEN);
}

static void atc_datis_block(MfdPageContext *ctx, int i, int entered)
{
    MfdUi *ui = ctx->ui;
    AtcBlock *b = &atc_blocks[i];
    const AtcStation *st;
    const AtcReport *rep = NULL;
    float T = 138.0f + 250.0f * (float)i;
    MfdFieldDesc fd;
    MfdFieldValue fv;
    MfdValue out;
    MfdDropdownDesc dd;
    static const char *const types[2] = {"DEP", "ARR"};
    int idx = -1, icao_empty = b->icao[0] == '\0', status_visible, menu_visible, k;
    unsigned int v0 = ctx->msgs ? ctx->msgs->version : 0u;
    char dummy[8], lines[5][64];
    MfdButtonDesc bd;
    MfdMenuItem items[4];

    if (entered) b->hide_report = 0; /* FBW onAfterRender: updateAtisData(icao) */
    st = atc_station(b->icao, 0);
    if (st && st->count > 0 && !b->hide_report) rep = &st->rep[0];

    /* ICAO field (absolute top 12, 106 x 40 container; 37 px field centred) */
    mfd_field_desc_init(&fd, MFD_FMT_TXT_AIRPORT);
    fd.width = 106.0f;
    memset(&fv, 0, sizeof(fv));
    fv.has_value = !icao_empty;
    fv.text = b->icao;
    {
        MfdFieldResult fr = mfd_input_field(ui, DID(10 + 20 * i), 10.0f, T + 13.5f, &fd, &fv, &out);
        atc_move_error(ctx, v0);
        if (fr == MFD_FIELD_COMMITTED || fr == MFD_FIELD_CLEARED) {
            int type = b->type;
            atc_init_block(i, fr == MFD_FIELD_COMMITTED ? out.str : NULL);
            b->type = type;
            b->hide_report = 1;
            icao_empty = b->icao[0] == '\0';
            rep = NULL;
        }
    }
    /* DEP / ARR dropdown (relative top 5 / left 115 inside the header row) */
    memset(&dd, 0, sizeof(dd));
    dd.values = types;
    dd.count = 2;
    dd.selected = b->type;
    dd.align_left = 1;
    dd.digits = 3;
    if (mfd_dropdown(ui, DID(11 + 20 * i), mfd_rect(120.0f, T + 10.0f, 106.0f, 44.0f), &dd, &idx, dummy, sizeof(dummy)) &&
        idx >= 0) {
        b->type = idx;
        if (b->type != A380X_ATIS_ARR) b->autoupdate = 0;
    }
    if (rep) {
        char l[2] = {rep->letter, '\0'};
        mfd_text(ui, 232.0f, T + 37.6f, MFD_ANCHOR_LEFT, 22.0f, MFD_COL_WHITE, l);
        mfd_text(ui, 264.0f, T + 37.6f, MFD_ANCHOR_LEFT, 22.0f, MFD_COL_WHITE, rep->time);
        if (rep->letter != b->last_read) atc_new_atis_icon(ctx->dl, 340.0f, T + 17.0f);
    }
    if (b->autoupdate) atc_auto_update_icon(ctx->dl, 386.0f, T + 13.0f);

    status_visible = b->status[0] != '\0';
    menu_visible = !status_visible && !rep; /* dropdownMenuVisible: status null and no text */
    mfd_button_desc_init(&bd, b->status);
    bd.flags = MFD_BTN_HIGHLIGHTED | (status_visible ? 0u : MFD_BTN_HIDDEN);
    mfd_button_ex(ui, DID(12 + 20 * i), mfd_rect(469.0f, T + 7.0f, 131.0f, 54.0f), &bd);

    items[0].label = "UPDATE"; items[0].disabled = icao_empty;
    items[1].label = "AUTO UPDATE"; items[1].disabled = b->type == A380X_ATIS_DEP || icao_empty;
    items[2].label = "PRINT"; items[2].disabled = 1;
    items[3].label = "AUTO PRINT"; items[3].disabled = 1;
    mfd_button_desc_init(&bd, "UPDATE\nOR PRINT");
    bd.menu = items;
    bd.menu_count = 4;
    bd.flags = MFD_BTN_MENU_RIGHT | (menu_visible ? MFD_BTN_HIDDEN : 0u);
    k = mfd_button_ex(ui, DID(13 + 20 * i), mfd_rect(600.0f, T + 7.0f, 159.0f, 54.0f), &bd);
    if (k == 0) atc_request(i);
    else if (k == 1) atc_toggle_auto_update(i);

    if (mfd_button(ui, DID(14 + 20 * i), mfd_rect(600.0f, T + 7.0f, 159.0f, 54.0f), "SEND\nREQUEST",
                   menu_visible ? (icao_empty ? MFD_BTN_DISABLED : 0u) : MFD_BTN_HIDDEN))
        atc_request(i);
    if (mfd_button(ui, DID(15 + 20 * i), mfd_rect(600.0f, T + 61.0f, 159.0f, 54.0f), "AUTO\nUPDATE",
                   menu_visible ? ((b->type == A380X_ATIS_DEP || icao_empty) ? MFD_BTN_DISABLED : 0u) : MFD_BTN_HIDDEN))
        atc_toggle_auto_update(i);

    if (rep) {
        atc_truncate(rep->text, lines);
        for (k = 0; k < 5; ++k)
            mfd_text(ui, 12.0f, T + 87.1f + 38.0f * (float)k, MFD_ANCHOR_LEFT, 27.0f, MFD_COL_WHITE, lines[k]);
        if (mfd_button(ui, DID(16 + 20 * i), mfd_rect(652.0f, T + 213.0f, 61.0f, 33.0f), ">>>", 0)) {
            char uri[48];
            b->last_read = rep->letter;
            snprintf(uri, sizeof(uri), "atccom/d-atis/received/%d", i);
            mfd_navigate(ctx, uri);
        }
    }
    mfd_draw_fill_rect(ctx->dl, mfd_rect(0.0f, T + 249.0f, 768.0f, 1.0f), MFD_COL_LIGHT_GREY);
}

static void atc_datis_list_page(MfdPageContext *ctx)
{
    int i;
    atc_tick(ctx);
    for (i = 0; i < 3; ++i) atc_datis_block(ctx, i, ctx->entered);
    mfd_button(ctx->ui, DID(1), mfd_rect(388.0f, 888.0f, 190.0f, 62.0f), "PRINT\nALL", MFD_BTN_DISABLED);
    if (mfd_button(ctx->ui, DID(2), mfd_rect(578.0f, 888.0f, 190.0f, 62.0f), "UPDATE\nALL", 0))
        for (i = 0; i < 3; ++i) atc_request(i); /* updateAllAtis */
}

/* ======================================================================================== */
/* D-ATIS RECEIVED (spec 06 section 9.3)                                                    */
/* ======================================================================================== */

#define ATC_ROWS_PER_PAGE 18
#define ATC_MAX_ROWS 40

/* processAtisToArray: rows of <= 39 characters (FBW test ignores the separating space). */
static int atc_rows(const char *text, char rows[ATC_MAX_ROWS][48])
{
    const char *p = text;
    int n = 0;
    memset(rows, 0, ATC_MAX_ROWS * 48);
    if (!text || !*text) return 0;
    n = 1;
    for (;;) {
        const char *w;
        size_t wl, ll;
        while (*p == ' ') ++p;
        if (!*p) break;
        w = p;
        while (*p && *p != ' ') ++p;
        wl = (size_t)(p - w);
        if (wl > 40) wl = 40;
        ll = strlen(rows[n - 1]);
        if (ll + wl > 39) {
            if (n >= ATC_MAX_ROWS) break;
            ++n;
            ll = 0;
        }
        if (ll) rows[n - 1][ll++] = ' ';
        memcpy(rows[n - 1] + ll, w, wl);
        rows[n - 1][ll + wl] = '\0';
    }
    return n;
}

static void atc_datis_received_page(MfdPageContext *ctx)
{
    MfdAtcReceivedState *s = MFD_PAGE_STATE(ctx, MfdAtcReceivedState);
    MfdUi *ui = ctx->ui;
    static const char *const titles[2] = {"LAST", "PREVIOUS"};
    static char rows[ATC_MAX_ROWS][48];
    MfdTabsDesc td;
    MfdRect content, clip;
    int idx = ctx->uri->extra[0] ? ctx->uri->extra[0] - '0' : 0, n = 0, pages, k, first;
    const AtcStation *st;
    char t[16];

    atc_tick(ctx);
    if (ctx->entered) {
        s->tab = 0;
        s->page[0] = s->page[1] = 1;
    }
    if (idx < 0 || idx > 2) idx = 0;
    st = atc_station(atc_blocks[idx].icao, 0);

    mfd_tabs_desc_init(&td);
    td.slant_deg = 27.0f;
    td.selected_text = MFD_COL_WHITE;
    td.content_h = 704.0f;
    k = mfd_top_tabs(ui, DID(100), mfd_rect(0.0f, 138.0f, 768.0f, 744.0f), titles, 2, s->tab, &td, &content, &clip);
    if (k >= 0) s->tab = k;
    if (st && s->tab < st->count) n = atc_rows(st->rep[s->tab].text, rows); /* [PORT] Q18: guarded */
    pages = (n + ATC_ROWS_PER_PAGE - 1) / ATC_ROWS_PER_PAGE;
    if (s->page[s->tab] < 1) s->page[s->tab] = 1;
    if (pages > 0 && s->page[s->tab] > pages) s->page[s->tab] = pages;
    first = (s->page[s->tab] - 1) * ATC_ROWS_PER_PAGE;
    mfd_draw_clip_push(ctx->dl, clip);
    for (k = 0; k < ATC_ROWS_PER_PAGE && first + k < n; ++k) /* [PORT] Q18: one 39-char row per line */
        mfd_text(ui, 14.0f, 220.5f + 38.0f * (float)k, MFD_ANCHOR_LEFT, 25.0f, MFD_COL_WHITE, rows[first + k]);
    if (pages > 1) {
        if (mfd_icon_button(ui, DID(101), mfd_rect(698.0f, 223.0f, 40.0f, 40.0f), MFD_ICON_DOUBLE_UP, s->page[s->tab] <= 1))
            s->page[s->tab]--;
        if (mfd_icon_button(ui, DID(102), mfd_rect(698.0f, 263.0f, 40.0f, 40.0f), MFD_ICON_DOUBLE_DOWN,
                            s->page[s->tab] >= pages))
            s->page[s->tab]++;
        mfd_text(ui, 737.0f, 830.8f, MFD_ANCHOR_RIGHT, 21.0f, MFD_COL_WHITE, "PGE");
        snprintf(t, sizeof(t), "%d/%d", s->page[s->tab], pages);
        mfd_text(ui, 737.0f, 851.8f, MFD_ANCHOR_RIGHT, 21.0f, MFD_COL_WHITE, t);
    }
    mfd_draw_clip_pop(ctx->dl);

    if (mfd_button(ui, DID(103), mfd_rect(0.0f, 885.0f, 190.0f, 62.0f), "RETURN\nTO LIST", 0))
        mfd_navigate(ctx, "atccom/d-atis/list");
    mfd_button(ui, DID(104), mfd_rect(578.0f, 884.0f, 190.0f, 62.0f), "PRINT", MFD_BTN_DISABLED);
}

/* ======================================================================================== */
/* REQUEST / REPORT & MODIFY / EMER stubs (spec 06 6.2: empty body, standard footer)        */
/* ======================================================================================== */

static void atc_stub_page(MfdPageContext *ctx)
{
    atc_tick(ctx);
}

void a380x_mfd_page_atccom_register(void)
{
    mfd_register_page("atccom/connect", MFD_PAGE_ID_ATCCOM_CONNECT, "CONNECT", atc_connect_page, 0u);
    mfd_register_page("atccom/msg-record", MFD_PAGE_ID_ATCCOM_MSG_RECORD, "MSG RECORD", atc_msg_record_page, 0u);
    mfd_register_page("atccom/msg-record/all-msg", MFD_PAGE_ID_ATCCOM_MSG_RECORD, "MSG RECORD/ALL MSG",
                      atc_all_msg_page, 0u);
    mfd_register_page("atccom/msg-record/monitored-msg", MFD_PAGE_ID_ATCCOM_MSG_RECORD, "MSG RECORD/MONITORED MSG",
                      atc_monitored_msg_page, 0u);
    mfd_register_page("atccom/msg-record/all-msg-expand", MFD_PAGE_ID_ATCCOM_MSG_RECORD, "MSG RECORD/ALL MSG/EXPAND",
                      atc_expand_page, 0u);
    mfd_register_page("atccom/d-atis/list", MFD_PAGE_ID_ATCCOM_DATIS, "D-ATIS/LIST", atc_datis_list_page,
                      MFD_PAGE_ATCCOM_FOOTER);
    mfd_register_page("atccom/d-atis/received", MFD_PAGE_ID_ATCCOM_DATIS, "D-ATIS/RECEIVED", atc_datis_received_page,
                      MFD_PAGE_ATCCOM_FOOTER);
    mfd_register_page("atccom/request", MFD_PAGE_ID_ATCCOM_CONNECT, "REQUEST", atc_stub_page, 0u);
    mfd_register_page("atccom/report-modify", MFD_PAGE_ID_ATCCOM_CONNECT, "REPORT & MODIFY", atc_stub_page, 0u);
    mfd_register_page("atccom/report-modify/position", MFD_PAGE_ID_ATCCOM_CONNECT, "REPORT & MODIFY", atc_stub_page, 0u);
    mfd_register_page("atccom/emer", MFD_PAGE_ID_ATCCOM_CONNECT, "EMER", atc_stub_page, 0u);
}
