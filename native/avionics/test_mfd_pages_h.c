/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * WP-H page test: POSITION, SURV, ATCCOM and FCU BKUP pages rendered with the software rasteriser of
 * test_mfd_render.c (included with its main renamed) against a fake A380XMfdSystemsHost, plus
 * headless interaction tests (squawk entry / SQWK CODE NOT VALID, TCAS mode, FCU BKUP push/pull,
 * typed targets and arrows, EFIS ND mode, RNP / fix entry, D-ATIS request flow).
 *
 * Usage: test_mfd_pages_h <ppm-output-dir>
 * Link: a380x_mfd.c a380x_mfd_ui.c a380x_mfd_draw.c a380x_mfd_page_data.c a380x_mfd_formats.c
 *       a380x_fms_messages.c a380x_mfd_page_position.c a380x_mfd_page_surv.c a380x_mfd_page_atccom.c
 *       a380x_mfd_page_fcubkup.c -lm
 */
#define main a380x_test_mfd_render_main
#include "test_mfd_render.c"
#undef main

#include "a380x_mfd_page_atccom.h"
#include "a380x_mfd_page_fcubkup.h"
#include "a380x_mfd_page_position.h"
#include "a380x_mfd_page_surv.h"

static int h_fail;
#define CHECK(c)                                                                          \
    do {                                                                                  \
        if (!(c)) {                                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c);         \
            h_fail++;                                                                     \
        }                                                                                 \
    } while (0)

/* ======================================================================================== */
/* Fake systems host                                                                        */
/* ======================================================================================== */

#define H_MAX_KV 512
static struct { unsigned int key; double v; } h_kv[H_MAX_KV];
static int h_nkv;

typedef struct { MfdSide side; unsigned int cmd; double v; } HCmd;
static HCmd h_log[512];
static int h_nlog;

static void hset(unsigned int key, double v)
{
    int i;
    for (i = 0; i < h_nkv; ++i) {
        if (h_kv[i].key == key) {
            h_kv[i].v = v;
            return;
        }
    }
    if (h_nkv < H_MAX_KV) {
        h_kv[h_nkv].key = key;
        h_kv[h_nkv].v = v;
        h_nkv++;
    }
}

static double hget(void *ctx, MfdSide side, unsigned int key)
{
    int i;
    (void)ctx;
    (void)side;
    for (i = 0; i < h_nkv; ++i)
        if (h_kv[i].key == key) return h_kv[i].v;
    return NAN;
}

static void hcmd(void *ctx, MfdSide side, unsigned int cmd, double v)
{
    (void)ctx;
    if (h_nlog < 512) {
        h_log[h_nlog].side = side;
        h_log[h_nlog].cmd = cmd;
        h_log[h_nlog].v = v;
        h_nlog++;
    }
    /* SURV state ids are also keys: the adapter would store them */
    if (cmd >= A380X_SURV_XPDR_AUTO && cmd <= A380X_SURV_XPDR_TCAS_SYS) hset(cmd, v);
    if (cmd == A380X_SURV_C_XPDR_CODE) hset(A380X_SURV_K_XPDR_CODE, v);
}

static int h_count(unsigned int cmd, double v)
{
    int i, n = 0;
    for (i = 0; i < h_nlog; ++i)
        if (h_log[i].cmd == cmd && (isnan(v) ? isnan(h_log[i].v) : h_log[i].v == v)) ++n;
    return n;
}

static int h_count_any(unsigned int cmd)
{
    int i, n = 0;
    for (i = 0; i < h_nlog; ++i)
        if (h_log[i].cmd == cmd) ++n;
    return n;
}

static double h_last(unsigned int cmd)
{
    int i;
    for (i = h_nlog - 1; i >= 0; --i)
        if (h_log[i].cmd == cmd) return h_log[i].v;
    return NAN;
}

/* ---- POSITION host ---- */

static void hp_navaids(void *ctx, MfdSide side, A380XMfdNavaidData *n)
{
    (void)ctx;
    (void)side;
    snprintf(n->vor[0].ident, sizeof(n->vor[0].ident), "BIG");
    n->vor[0].freq_mhz = 115.10;
    n->vor[0].course_deg = 92.0;
    snprintf(n->vor[0].cls, sizeof(n->vor[0].cls), "VOR/DME");
    n->vor[0].ident_manual = 1;
    n->vor[1].freq_mhz = 113.60;
    n->vor[1].freq_manual = 1;
    snprintf(n->ls.ident, sizeof(n->ls.ident), "ILL");
    n->ls.freq_mhz = 109.50;
    n->ls.course_deg = 271.0;
    n->ls.slope_deg = 3.0;
    snprintf(n->ls.cls, sizeof(n->ls.cls), "ILS/DME");
    snprintf(n->selected[0].ident, sizeof(n->selected[0].ident), "BIG");
    n->selected[0].freq_mhz = 115.10;
    snprintf(n->selected[0].cls, sizeof(n->selected[0].cls), "VOR/DME");
    snprintf(n->selected[1].ident, sizeof(n->selected[1].ident), "LON");
    n->selected[1].freq_mhz = 113.60;
    snprintf(n->selected[1].cls, sizeof(n->selected[1].cls), "VOR/DME");
}

static char hp_last_tune[16];
static int hp_tune_ident(void *ctx, int rcv, const char *ident)
{
    (void)ctx;
    snprintf(hp_last_tune, sizeof(hp_last_tune), "%d:%s", rcv, ident);
    if (strcmp(ident, "XXX") == 0) return A380X_POS_TUNE_NOT_IN_DATABASE;
    if (strcmp(ident, "DES") == 0) return A380X_POS_TUNE_DESELECTED;
    return A380X_POS_TUNE_OK;
}

static int hp_fix(void *ctx, const char *ident, double rlat, double rlon, double *lat, double *lon)
{
    (void)ctx;
    (void)rlat;
    (void)rlon;
    if (strcmp(ident, "OCK") == 0) {
        *lat = 51.305; *lon = -0.4472;
        return 1;
    }
    return 0;
}

static int hp_heading(void *ctx, int unit, double hdg)
{
    (void)ctx;
    hset(A380X_POS_K_IR(unit, A380X_POS_IR_HDG_REQUIRED), 0.0);
    return hdg >= 0.0;
}

/* ---- ATCCOM host ---- */

static struct { char icao[8]; int type, overwrite; uint32_t id; } ha_req[16];
static int ha_nreq;

static void ha_request(void *ctx, const char *icao, int type, int overwrite, uint32_t id)
{
    (void)ctx;
    if (ha_nreq < 16) {
        snprintf(ha_req[ha_nreq].icao, sizeof(ha_req[ha_nreq].icao), "%s", icao);
        ha_req[ha_nreq].type = type;
        ha_req[ha_nreq].overwrite = overwrite;
        ha_req[ha_nreq].id = id;
        ha_nreq++;
    }
}

static void ha_city(void *ctx, char o[8], char d[8], char a[8])
{
    (void)ctx;
    snprintf(o, 8, "EGLL");
    snprintf(d, 8, "LFPG");
    snprintf(a, 8, "LFPO");
}

static MfdMsgQueue g_atcq;

static void h_values(void)
{
    int u;
    h_nkv = 0;
    /* POSITION */
    hset(A380X_POS_K_FM_VALID, 1); hset(A380X_POS_K_FM_LAT, 51.4706); hset(A380X_POS_K_FM_LON, -0.4619);
    hset(A380X_POS_K_GPS_PRIMARY, 1); hset(A380X_POS_K_EPU_NM, 0.05); hset(A380X_POS_K_RNP_NM, 1.0);
    hset(A380X_POS_K_RNP_PILOT, 0); hset(A380X_POS_K_GPS1_VALID, 1); hset(A380X_POS_K_GPS2_VALID, 1);
    hset(A380X_POS_K_TRUE_TRACK, 271.4); hset(A380X_POS_K_TRUE_HDG, 271.0); hset(A380X_POS_K_MAGVAR, -0.8);
    hset(A380X_POS_K_WIND_DIR, 240.0); hset(A380X_POS_K_WIND_KT, 12.0); hset(A380X_POS_K_ON_GROUND, 1);
    hset(A380X_POS_K_UTC_S, 13.0 * 3600.0 + 42.0 * 60.0);
    for (u = 0; u < 3; ++u) {
        hset(A380X_POS_K_IR(u, A380X_POS_IR_MODE), 1);
        hset(A380X_POS_K_IR(u, A380X_POS_IR_NAV_VALID), 1);
        hset(A380X_POS_K_IR(u, A380X_POS_IR_HDG_VALID), 1);
        hset(A380X_POS_K_IR(u, A380X_POS_IR_LAT), 51.4706 + 0.001 * u);
        hset(A380X_POS_K_IR(u, A380X_POS_IR_LON), -0.4619);
        hset(A380X_POS_K_IR(u, A380X_POS_IR_GS_KT), 0.0);
        hset(A380X_POS_K_IR(u, A380X_POS_IR_HDG_MAG), 271.8);
    }
    /* SURV */
    hset(A380X_SURV_K_XPDR_CODE, 2000); hset(A380X_SURV_K_XPDR_MODE, 5); hset(A380X_SURV_K_XPDR_FAILED, 0);
    hset(A380X_SURV_K_TCAS_FAULT, 0); hset(A380X_SURV_K_ON_GROUND, 1);
    hset(A380X_SURV_XPDR_AUTO, 1); hset(A380X_SURV_ALT_RPTG, 1); hset(A380X_SURV_TCAS_ALERT_LEVEL, 2);
    hset(A380X_SURV_TCAS_ALT_SELECT, 0); hset(A380X_SURV_WXR_TAWS_SYS, 1); hset(A380X_SURV_XPDR_TCAS_SYS, 1);
    /* FCU */
    hset(A380X_FCUB_K_POWERED, 1); hset(A380X_FCUB_K_SELECTED_SPEED, 250); hset(A380X_FCUB_K_SELECTED_MACH, 0.84);
    hset(A380X_FCUB_K_SELECTED_HEADING, 271); hset(A380X_FCUB_K_SELECTED_ALTITUDE, 5000);
    hset(A380X_FCUB_K_SELECTED_VS, 1500); hset(A380X_FCUB_K_SPEED_MANAGED, 1); hset(A380X_FCUB_K_HEADING_MANAGED, 0);
    hset(A380X_FCUB_K_ALTITUDE_MANAGED, 1); hset(A380X_FCUB_K_ALT_STEP_FT, 100); hset(A380X_FCUB_K_AP1, 1);
    hset(A380X_FCUB_K_FD, 1); hset(A380X_FCUB_K_ATHR, 1); hset(A380X_FCUB_K_VS_DASHES, 1);
    /* EFIS */
    hset(A380X_EFIS_K_POWERED, 1); hset(A380X_EFIS_K_MODE, 3); hset(A380X_EFIS_K_RANGE_INDEX, 3);
    hset(A380X_EFIS_K_BARO_PRESET_HPA, 1013.0); hset(A380X_EFIS_K_QNH_HPA, 1013.0); hset(A380X_EFIS_K_LS, 1);
    hset(A380X_EFIS_K_CSTR, 1); hset(A380X_EFIS_K_TERR, 1); hset(A380X_EFIS_K_TRAF, 1); hset(A380X_EFIS_K_VOR1, 1);
    /* ATCCOM */
    hset(A380X_ATC_K_UTC_S, 13.0 * 3600.0 + 42.0 * 60.0);
}

static void h_reset(void)
{
    A380XMfdSystemsHost host;
    A380XMfdPositionHost ph;
    A380XMfdAtccomHost ah;
    mfd_msg_init(&g_q);
    mfd_msg_init(&g_atcq);
    memset(&g_in, 0, sizeof(g_in));
    g_in.du_on[0] = g_in.du_on[1] = 1;
    g_in.fms_knob = 1;
    g_in.fmc_a_healthy = g_in.fmc_b_healthy = 1;
    g_in.metric_units = 1;
    snprintf(g_in.atc_callsign, sizeof(g_in.atc_callsign), "AIB380");
    memset(&host, 0, sizeof(host));
    host.read_status = host_status;
    host.command = hcmd;
    host.read_value = hget;
    a380x_mfd_init(&g_mfd, NULL, &g_q, &host);
    g_mfd.atc_msgs = &g_atcq;
    memset(&ph, 0, sizeof(ph));
    ph.read_navaids = hp_navaids;
    ph.tune_ident = hp_tune_ident;
    ph.fix_lookup = hp_fix;
    ph.adirs_enter_heading = hp_heading;
    a380x_mfd_page_position_set_host(&ph);
    memset(&ah, 0, sizeof(ah));
    ah.atis_request = ha_request;
    ah.city_pair = ha_city;
    a380x_mfd_atccom_set_host(&ah);
    h_nlog = 0;
    ha_nreq = 0;
    frames(2);
}

static void go(const char *uri)
{
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, uri);
    frames(2);
}

static void commit_wait(void)
{
    key(MFD_KEY_ENT, 0);
    g_t += 0.6;
    frames(2);
}

static char g_dir[512];

static void shot(const char *name)
{
    leave();
    snap(g_dir, name);
}

/* ======================================================================================== */
/* Tests and previews                                                                       */
/* ======================================================================================== */

static void test_surv(void)
{
    int before;
    h_values();
    h_reset();
    go("surv/controls");
    shot("surv_controls");

    /* squawk entry: field 30.3..130.3 x 220..257 */
    click(80.0f, 238.0f);
    type("7700");
    commit_wait();
    CHECK(h_count(A380X_SURV_C_XPDR_CODE, 7700.0) == 1);
    /* 0000 accepted ([PORT] Q15) */
    click(80.0f, 238.0f);
    type("0000");
    commit_wait();
    CHECK(h_count(A380X_SURV_C_XPDR_CODE, 0.0) == 1);
    /* SQWK CODE NOT VALID (digit 8) */
    before = h_count_any(A380X_SURV_C_XPDR_CODE);
    click(80.0f, 238.0f);
    type("7800");
    commit_wait();
    CHECK(h_count_any(A380X_SURV_C_XPDR_CODE) == before);
    CHECK(mfd_msg_has_id(&g_q, MFD_MSG_SQWK_CODE_NOT_VALID));
    CHECK(!mfd_msg_has_id(&g_q, MFD_MSG_FORMAT_ERROR));
    shot("surv_controls_sqwk_not_valid");
    mfd_msg_init(&g_q);

    /* IDENT */
    click(80.0f, 282.0f);
    CHECK(h_count_any(A380X_SURV_C_XPDR_IDENT) == 1);
    /* TCAS: TA ONLY row (x 383.4, rows 223/267/311) -> alert level 1 */
    click(450.0f, 284.0f);
    CHECK(h_count(A380X_SURV_TCAS_ALERT_LEVEL, 1.0) == 1);
    /* STBY -> 0 */
    click(450.0f, 328.0f);
    CHECK(h_count(A380X_SURV_TCAS_ALERT_LEVEL, 0.0) == 1);
    /* ABV -> tcas alt select 1 */
    click(640.0f, 284.0f);
    CHECK(h_count(A380X_SURV_TCAS_ALT_SELECT, 1.0) == 1);
    /* XPDR STBY -> xpdr_auto 0 */
    click(200.0f, 349.0f);
    CHECK(h_count(A380X_SURV_XPDR_AUTO, 0.0) == 1);
    /* TERR SYS -> off */
    click(130.0f, 880.0f);
    CHECK(h_count(A380X_SURV_TERR_SYS_OFF, 1.0) == 1);
    /* TCAS disabled while the XPDR is in STBY: clicks ignored */
    hset(A380X_SURV_K_XPDR_MODE, 1);
    frames(2);
    before = h_count_any(A380X_SURV_TCAS_ALERT_LEVEL);
    click(450.0f, 240.0f);
    CHECK(h_count_any(A380X_SURV_TCAS_ALERT_LEVEL) == before);
    shot("surv_controls_stby");
    /* DEFAULT SETTINGS */
    click(695.0f, 880.0f);
    CHECK(h_last(A380X_SURV_XPDR_AUTO) == 1.0 && h_last(A380X_SURV_TCAS_ALERT_LEVEL) == 2.0);
    CHECK(h_last(A380X_SURV_TERR_SYS_OFF) == 0.0 && h_last(A380X_SURV_GPWS_SYS_OFF) == 0.0);

    h_values();
    go("surv/status-switching");
    shot("surv_status");
    click(185.0f, 183.0f); /* upper SYS 1 (active) -> 0 */
    CHECK(h_count(A380X_SURV_WXR_TAWS_SYS, 0.0) == 1);
    click(583.0f, 529.0f); /* lower SYS 2 -> xpdr_tcas_sys 2 */
    CHECK(h_count(A380X_SURV_XPDR_TCAS_SYS, 2.0) == 1);
}

static void test_fcubkup(void)
{
    int n;
    h_values();
    h_reset();
    go("fcubkup/afs");
    shot("fcubkup_afs");
    /* SPD PUSH / PULL */
    click(50.0f, 325.0f);
    CHECK(h_count(A380X_FCUB_C_SPEED_PUSH, A380X_MFD_ACT_ONCE) == 1);
    click(140.0f, 325.0f);
    CHECK(h_count(A380X_FCUB_C_SPEED_PULL, A380X_MFD_ACT_ONCE) == 1);
    /* typed SPD target */
    click(88.0f, 206.0f);
    type("280");
    commit_wait();
    CHECK(h_count(A380X_FCUB_C_SET_SPEED, 280.0) == 1);
    /* out of range -> ENTRY OUT OF RANGE, no command */
    n = h_count_any(A380X_FCUB_C_SET_SPEED);
    click(88.0f, 206.0f);
    type("50");
    commit_wait();
    CHECK(h_count_any(A380X_FCUB_C_SET_SPEED) == n);
    CHECK(mfd_msg_has_id(&g_q, MFD_MSG_ENTRY_OUT_OF_RANGE));
    shot("fcubkup_afs_range_error");
    mfd_msg_init(&g_q);
    /* SPD + arrow: press = BEGIN, release = END */
    click(135.0f, 268.0f);
    CHECK(h_count(A380X_FCUB_C_SPEED_UP, A380X_MFD_ACT_BEGIN) == 1);
    CHECK(h_count(A380X_FCUB_C_SPEED_UP, A380X_MFD_ACT_END) == 1);
    /* held arrow released when navigating away */
    mfd_pointer_move(&g_mfd, MFD_CAPT, 41.0f, 268.0f);
    frame();
    mfd_pointer_button(&g_mfd, MFD_CAPT, 1);
    frame();
    CHECK(h_count(A380X_FCUB_C_SPEED_DOWN, A380X_MFD_ACT_BEGIN) == 1);
    a380x_mfd_navigate(&g_mfd, MFD_CAPT, "fcubkup/efis");
    frame();
    a380x_mfd_page_fcubkup_release_held(&g_mfd);
    CHECK(h_count(A380X_FCUB_C_SPEED_DOWN, A380X_MFD_ACT_END) == 1);
    mfd_pointer_button(&g_mfd, MFD_CAPT, 0);
    frame();
    go("fcubkup/afs");
    /* typed V/S (dashes) -> rounded to 50 */
    click(664.0f, 206.0f);
    type("-1520");
    commit_wait();
    CHECK(h_count(A380X_FCUB_C_SET_VS, -1500.0) == 1);
    /* HDG 360 -> 0 */
    click(280.0f, 206.0f);
    type("360");
    commit_wait();
    CHECK(h_count(A380X_FCUB_C_SET_HEADING, 0.0) == 1);
    /* AP2 pb, 1000 segment */
    click(383.0f, 487.0f);
    CHECK(h_count(A380X_FCUB_C_AP2, A380X_MFD_ACT_ONCE) == 1);
    click(516.0f, 377.0f);
    CHECK(h_count(A380X_FCUB_C_ALT_STEP_TOGGLE, A380X_MFD_ACT_ONCE) == 1);
    click(436.0f, 377.0f); /* 100 already selected: no command */
    CHECK(h_count(A380X_FCUB_C_ALT_STEP_TOGGLE, A380X_MFD_ACT_ONCE) == 1);
    /* unpowered: nothing reacts */
    hset(A380X_FCUB_K_POWERED, 0);
    frames(2);
    n = h_nlog;
    click(50.0f, 325.0f);
    CHECK(h_nlog == n);
    shot("fcubkup_afs_unpowered");
    hset(A380X_FCUB_K_POWERED, 1);

    go("fcubkup/efis");
    shot("fcubkup_efis");
    /* ND MODE ARC (3) -> PLAN (4): one mode_up */
    click(100.0f, 525.0f);
    CHECK(h_count(A380X_EFIS_C_MODE_UP, A380X_MFD_ACT_ONCE) == 1);
    /* ND RANGE 40 (3) -> 10 (1): two range_down */
    click(330.0f, 408.0f);
    CHECK(h_count(A380X_EFIS_C_RANGE_DOWN, A380X_MFD_ACT_ONCE) == 2);
    /* BARO 1020 -> SET_BARO_HPA */
    click(96.0f, 211.0f);
    type("1020");
    commit_wait();
    CHECK(h_count(A380X_EFIS_C_SET_BARO_HPA, 1020.0) == 1);
    /* STD */
    click(50.0f, 267.0f);
    CHECK(h_count(A380X_EFIS_C_BARO_STD, A380X_MFD_ACT_ONCE) == 1);
    /* WPT */
    click(615.0f, 384.0f);
    CHECK(h_count(A380X_EFIS_C_WPT, A380X_MFD_ACT_ONCE) == 1);
}

static void test_position(void)
{
    h_values();
    h_reset();
    a380x_mfd_page_position_reset();
    go("fms/position/monitor");
    /* RNP entry -> command */
    click(590.0f, 170.0f);
    type("0.3");
    commit_wait();
    CHECK(h_count(A380X_POS_C_SET_RNP, 0.3) == 1);
    /* BRG / DIST TO fix */
    click(672.0f, 806.0f);
    type("OCK");
    commit_wait();
    {
        char id[8];
        double lat, lon;
        CHECK(a380x_mfd_position_monitor_fix(id, &lat, &lon) && strcmp(id, "OCK") == 0);
    }
    click(672.0f, 806.0f);
    type("ZZZZ");
    commit_wait();
    CHECK(mfd_msg_has_id(&g_q, MFD_MSG_NOT_IN_DATABASE));
    mfd_msg_init(&g_q);
    g_t += 1.0;
    frames(2);
    shot("position_monitor");
    /* sensors + freeze */
    click(500.0f, 721.0f); /* FREEZE */
    hset(A380X_POS_K_GPS_PRIMARY, 0);
    hset(A380X_POS_K_RNP_NM, 0.02);
    g_t += 1.0;
    frames(2);
    shot("position_monitor_frozen_lost");
    h_values();

    /* IRS: IR1 aligning 5 min, IR2 NAV, IR3 ATT heading required */
    hset(A380X_POS_K_IR(0, A380X_POS_IR_NAV_VALID), 0);
    hset(A380X_POS_K_IR(0, A380X_POS_IR_ALIGNING), 1);
    hset(A380X_POS_K_IR(0, A380X_POS_IR_ALIGN_REMAIN_S), 290);
    hset(A380X_POS_K_IR(2, A380X_POS_IR_MODE), 2);
    hset(A380X_POS_K_IR(2, A380X_POS_IR_NAV_VALID), 0);
    hset(A380X_POS_K_IR(2, A380X_POS_IR_HDG_REQUIRED), 1);
    go("fms/position/irs");
    click(384.0f, 545.0f); /* show IRS2 */
    shot("position_irs");
    click(690.0f, 486.0f); /* SET HDG */
    type("271");
    commit_wait();
    CHECK(hget(NULL, MFD_CAPT, A380X_POS_K_IR(2, A380X_POS_IR_HDG_REQUIRED)) == 0.0);
    h_values();

    go("fms/position/navaids/withReturn");
    shot("position_navaids_tuned");
    click(250.0f, 261.0f); /* VOR1 ident */
    type("OCK");
    commit_wait();
    CHECK(strcmp(hp_last_tune, "0:OCK") == 0);
    click(250.0f, 261.0f);
    type("DES");
    commit_wait();
    CHECK(mfd_msg_has_id(&g_q, MFD_MSG_XXX_IS_DESELECTED));
    mfd_msg_init(&g_q);
    go("fms/position/navaids/nav");
    click(60.0f, 522.0f); /* deselect list slot 1 */
    type("LON");
    commit_wait();
    CHECK(strcmp(a380x_mfd_position_deselected(0), "LON") == 0);
    shot("position_navaids_selected");
}

static void test_atccom(void)
{
    A380XDatisAnswer a;
    A380XDatisBlockInfo bi;
    h_values();
    h_reset();
    a380x_mfd_atccom_reset();
    go("atccom/connect");
    click(234.0f, 575.0f); /* ADS-C ARMED */
    shot("atccom_connect");
    go("atccom/msg-record");
    shot("atccom_msg_record");
    click(160.0f, 310.0f);
    frames(2);
    CHECK(strcmp(a380x_mfd_active_uri(&g_mfd, MFD_CAPT), "atccom/msg-record/all-msg") == 0);
    shot("atccom_msg_record_all");
    go("atccom/request");
    shot("atccom_request_stub");

    go("atccom/d-atis/list");
    CHECK(a380x_mfd_atccom_block(1, &bi) && strcmp(bi.icao, "LFPG") == 0 && bi.type == A380X_ATIS_ARR);
    /* SEND REQUEST on block 1 (600..759 x T+7..T+61, T = 388) */
    click(680.0f, 420.0f);
    CHECK(ha_nreq == 1 && strcmp(ha_req[0].icao, "LFPG") == 0 && ha_req[0].type == A380X_ATIS_ARR &&
          ha_req[0].overwrite == 1);
    CHECK(a380x_mfd_atccom_block(1, &bi) && strcmp(bi.status, "SENDING") == 0);
    shot("atccom_datis_sending");
    memset(&a, 0, sizeof(a));
    a.request_id = ha_req[0].id;
    a.status = A380X_DATIS_NEW;
    snprintf(a.icao, sizeof(a.icao), "LFPG");
    a.letter = 'C';
    snprintf(a.text, sizeof(a.text),
             "LFPG ARR ATIS INFO C 1330Z. EXPECT ILS APPROACH RWY 27R. ARR RWY 27R. TRANSITION LEVEL FL070. "
             "WIND 250 DEG 14 KT GUSTS 26 KT. VIS 10 KM OR MORE. -RA. CLOUD BKN 1800 FT OVC 3500 FT. TEMP 12 "
             "DEWPOINT 09. QNH 1009 HPA. ACKNOWLEDGE INFO C ON INITIAL CONTACT.");
    a380x_mfd_atccom_deliver(&a);
    /* block 0 (EGLL DEP): NO REPLY */
    click(680.0f, 172.0f);
    CHECK(ha_nreq == 2 && strcmp(ha_req[1].icao, "EGLL") == 0);
    memset(&a, 0, sizeof(a));
    a.request_id = ha_req[1].id;
    a.status = A380X_DATIS_NO_REPLY;
    a380x_mfd_atccom_deliver(&a);
    frames(2);
    CHECK(a380x_mfd_atccom_block(1, &bi) && bi.letter == 'C' && strcmp(bi.time, "1342Z") == 0 && bi.status[0] == 0);
    CHECK(a380x_mfd_atccom_block(0, &bi) && strcmp(bi.status, "NO REPLY") == 0);
    CHECK(mfd_msg_has_id(&g_atcq, MFD_MSG_ATC_DATIS_NO_REPLY));
    /* auto update on block 2 (LFPO ARR) via its AUTO UPDATE button */
    click(680.0f, 724.0f);
    CHECK(a380x_mfd_atccom_block(2, &bi) && bi.autoupdate);
    shot("atccom_datis_list");
    /* >>> on block 1 -> RECEIVED */
    click(682.0f, 617.0f);
    frames(2);
    CHECK(strcmp(a380x_mfd_active_uri(&g_mfd, MFD_CAPT), "atccom/d-atis/received/1") == 0);
    shot("atccom_datis_received");
    /* entry error of the ICAO field goes to the ATCCOM queue */
    go("atccom/d-atis/list");
    mfd_msg_init(&g_atcq);
    click(63.0f, 170.0f);
    type("EG");
    commit_wait();
    CHECK(mfd_msg_has_id(&g_atcq, MFD_MSG_FORMAT_ERROR) && !mfd_msg_has_id(&g_q, MFD_MSG_FORMAT_ERROR));
    shot("atccom_datis_format_error");
}

int main(int argc, char **argv)
{
    snprintf(g_dir, sizeof(g_dir), "%s", argc > 1 ? argv[1] : ".");
    if (!mfd_font_decode(g_atlas)) {
        fprintf(stderr, "font decode failed\n");
        return 1;
    }
    a380x_mfd_page_data_register();
    a380x_mfd_page_position_register();
    a380x_mfd_page_surv_register();
    a380x_mfd_page_atccom_register();
    a380x_mfd_page_fcubkup_register();

    test_surv();
    test_fcubkup();
    test_position();
    test_atccom();

    if (h_fail) {
        fprintf(stderr, "test_mfd_pages_h: %d check(s) FAILED\n", h_fail);
        return 1;
    }
    printf("test_mfd_pages_h: OK\n");
    return 0;
}
