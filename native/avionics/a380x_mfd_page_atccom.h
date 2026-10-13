/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD ATCCOM pages (work package H): CONNECT, MSG RECORD (+ ALL MSG, MONITORED MSG, EXPAND),
 * D-ATIS LIST and RECEIVED, the REQUEST / REPORT & MODIFY / EMER stubs, and the offline ATC datalink
 * model of docs/mfd-spec/06-position-surv-atccom-fcubkup.md sections 6-9 (translation of the pinned
 * FBW MFD/ATCCOM/AtcDatalinkSystem.ts and pages/ATCCOM pages, border-box geometry).
 *
 * DATALINK MODEL ([PORT] spec 06 6.2): CPDLC / ADS-C are NOT AVAILABLE (no ground stations, empty
 * message record). D-ATIS works through an ATIS provider implemented by the adapter (synthesised
 * from X-Plane weather, spec 06 9.4): the model calls A380XMfdAtccomHost.atis_request() and the
 * adapter answers asynchronously with a380x_mfd_atccom_deliver() (or through atis_poll()).
 * Without a provider every request ends in SEND FAILED (datis_source = off).
 *
 * The model is one aircraft system shared by both MFDs (module state). Messages go to the ATCCOM
 * queue A380XMfd.atc_msgs (shown by the ATCCOM footer of the D-ATIS pages).
 */
#ifndef A380X_MFD_PAGE_ATCCOM_H
#define A380X_MFD_PAGE_ATCCOM_H

#include "a380x_mfd.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef A380X_MFD_SYS_ID
#define A380X_MFD_SYS_ID(scope, n) ((unsigned int)((((unsigned int)(scope)) << 16) | (((unsigned int)(n)) & 0xffffu)))
#endif

/* read_value key (A380XMfdSystemsHost) used for the report timestamps: sim/time/zulu_time_sec. */
#define A380X_ATC_K_UTC_S A380X_MFD_SYS_ID(MFD_PAGE_ID_ATCCOM_CONNECT, 1)

/* ATIS types (FBW AtisType). */
enum { A380X_ATIS_DEP = 0, A380X_ATIS_ARR = 1 };

/* Answer codes (FBW AtsuStatusCodes subset). */
enum {
    A380X_DATIS_NEW = 0,       /* NewAtisReceived: text + letter valid */
    A380X_DATIS_NO_REPLY = 1,  /* NoAtisReceived: unknown ICAO / no METAR -> NO REPLY + "D-ATIS NO REPLY" */
    A380X_DATIS_COM_FAILED = 2 /* ComFailed: SEND FAILED (datis_source = off) */
};

#define A380X_DATIS_TEXT_LEN 512

/* D-ATIS answer filled by the adapter (spec 06 9.4 steps 1-7). */
typedef struct {
    uint32_t request_id;       /* id passed to atis_request() */
    int status;                /* A380X_DATIS_* */
    char icao[5];
    char letter;               /* information letter 'A'..'Z' (NEW only) */
    char text[A380X_DATIS_TEXT_LEN]; /* full report, upper case, single spaces (NEW only) */
} A380XDatisAnswer;

typedef struct {
    void *ctx;
    /* Asynchronous D-ATIS request (FBW atcRequestAtis). overwrite = 1 for explicit requests,
     * 0 for the 60 s auto update. The adapter answers after its link delay (3 s). */
    void (*atis_request)(void *ctx, const char *icao, int type, int overwrite, uint32_t request_id);
    /* Optional polling alternative to a380x_mfd_atccom_deliver(): 1 = *out filled. */
    int (*atis_poll)(void *ctx, A380XDatisAnswer *out);
    /* Active flight plan origin / destination / alternate ("" when none): D-ATIS block init. */
    void (*city_pair)(void *ctx, char origin[8], char dest[8], char altn[8]);
} A380XMfdAtccomHost;

void a380x_mfd_atccom_set_host(const A380XMfdAtccomHost *host);
/* Run the datalink model: city-pair initialisation, auto updates (60 s), polled answers. Call once per
 * frame from the adapter (the ATCCOM pages also call it; repeated calls with the same `now` are cheap).
 * atc_q may be NULL (messages dropped). utc_s = sim/time/zulu_time_sec (NAN unknown). */
void a380x_mfd_atccom_update(double now_s, double utc_s, MfdMsgQueue *atc_q);
/* Deliver an answer (any thread-free context, normally the adapter's flight loop). */
void a380x_mfd_atccom_deliver(const A380XDatisAnswer *answer);
/* ATC reset (FBW atcResetData / FMS reset): blocks and stored reports cleared. */
void a380x_mfd_atccom_reset(void);

/* Read-only view of a D-ATIS block (tests, cockpit datarefs). */
typedef struct {
    char icao[5];
    int type, requested, autoupdate;
    char status[24];           /* "", "SENDING", "SEND\nFAILED", "NO REPLY" */
    char letter;               /* latest report letter or 0 */
    char time[8];              /* "HHMMZ" of the latest report or "" */
    int report_count;
} A380XDatisBlockInfo;
int a380x_mfd_atccom_block(int index, A380XDatisBlockInfo *out);

/* ---- page-local state ------------------------------------------------------------------------ */
typedef struct {
    int adsc_armed, adsc_emer_armed; /* local toggles [FBW] */
} MfdAtcConnectState;
MFD_PAGE_STATE_CHECK(MfdAtcConnectState);

typedef struct {
    int tab;                   /* 0 LAST, 1 PREVIOUS */
    int page[2];               /* 1-based page per tab */
} MfdAtcReceivedState;
MFD_PAGE_STATE_CHECK(MfdAtcReceivedState);

/* Registers atccom/connect, atccom/msg-record[/all-msg|/monitored-msg|/all-msg-expand],
 * atccom/d-atis/list, atccom/d-atis/received, atccom/request, atccom/report-modify[/position],
 * atccom/emer. */
void a380x_mfd_page_atccom_register(void);

#ifdef __cplusplus
}
#endif

#endif
