/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X FMS message catalogue and shared message queue (WP-B). Translation of the pinned FlyByWire
 * NXSystemMessages.ts (NXSystemMessages, NXFictionalMessages, ATCCOMMessages) and of the FMC / ATCCOM queue
 * semantics (FlightManagementComputer.ts addMessageToQueue, removeMessageFromQueue,
 * clearLatestFmsErrorMessage, updateMessageQueue; AtcDatalinkSystem.ts), with the [PORT] decisions of
 * docs/mfd-spec/02-data-formats-messages.md §10.2 and §12 (Q13, Q15, Q16). No allocation.
 */
#include "a380x_fms_messages.h"

#include <string.h>

_Static_assert(MFD_MSG_COUNT == 41 + 26 + 27, "NX (41) + fictional (26) + ATCCOM (27) messages");

/* clang-format off */
const MfdMsgDef mfd_msg_defs[MFD_MSG_COUNT] = {
    /* NXSystemMessages (MSG:80-122) */
    [MFD_MSG_AWY_WPT_DISAGREE]             = {"AIRWAY / WPT DISAGREE", 0, 0, NULL},
    [MFD_MSG_CRZ_FL_ABOVE_MAX_FL]          = {"CRZ FL ABOVE MAX FL", 1, 0, NULL},
    [MFD_MSG_CANCEL_ATIS_UPDATE]           = {"CANCEL AUTO UPDATE FIRST", 0, 0, NULL},
    [MFD_MSG_CHECK_MIN_FUEL_AT_DEST]       = {"CHECK MIN FUEL AT DEST", 1, 0, NULL},
    [MFD_MSG_CHECK_SPD_MODE]               = {"CHECK SPD MODE", 1, 0, NULL},
    [MFD_MSG_CHECK_TO_DATA]                = {"CHECK T.O. DATA", 1, 1, NULL},
    [MFD_MSG_CHECK_ZFW]                    = {"CHECK ZFW", 1, 1, NULL},
    [MFD_MSG_COM_FPLN_RECEIVED]            = {"COMPANY F-PLN RECEIVED\nWAITING FOR INSERTION", 1, 0, NULL},
    [MFD_MSG_COM_DATALINK_NOT_AVAIL]       = {"COM DATALINK NOT AVAIL", 0, 0, NULL},
    [MFD_MSG_CSTR_DEL_UP_TO_WPT]           = {"CONSTRAINTS BEFORE WWWWW : DELETED", 1, 0, "WWWWW"},
    [MFD_MSG_DATABASE_CODING_ERROR]        = {"DATABASE CODING ERROR", 1, 0, NULL},
    [MFD_MSG_DEST_EFOB_BELOW_MIN]          = {"DEST EFOB BELOW MIN", 1, 1, NULL},
    [MFD_MSG_ENTER_DEST_DATA]              = {"ENTER DEST DATA", 1, 1, NULL},
    [MFD_MSG_ENTRY_OUT_OF_RANGE]           = {"ENTRY OUT OF RANGE", 0, 0, NULL},
    [MFD_MSG_FORMAT_ERROR]                 = {"FORMAT ERROR", 0, 0, NULL},
    [MFD_MSG_FPLN_ELEMENT_RETAINED]        = {"F-PLN ELEMENT RETAINED", 0, 0, NULL},
    [MFD_MSG_INITIALIZE_ZFW_ZFWCG]         = {"INITIALIZE ZFW / ZFWCG", 1, 1, NULL},
    [MFD_MSG_NEW_ACC_ALT]                  = {"NEW ACCEL ALT: HHHHH", 1, 0, "HHHHH"},
    [MFD_MSG_NEW_CRZ_ALT]                  = {"NEW CRZ ALT: HHHHH", 1, 0, "HHHHH"},
    [MFD_MSG_NEW_THR_RED_ALT]              = {"NEW THR RED ALT: HHHHH", 1, 0, "HHHHH"},
    [MFD_MSG_NO_INTERSECTION_FOUND]        = {"NO INTERSECTION FOUND", 0, 0, NULL},
    [MFD_MSG_NOT_ALLOWED]                  = {"NOT ALLOWED", 0, 0, NULL},
    [MFD_MSG_NOT_ALLOWED_IN_NAV]           = {"NOT ALLOWED IN NAV", 0, 0, NULL},
    [MFD_MSG_NOT_IN_DATABASE]              = {"NOT IN DATABASE", 0, 0, NULL},
    [MFD_MSG_RECEIVED_CPNY_FPLN_NOT_VALID] = {"RECEIVED COMPANY F-PLN NOT VALID", 1, 0, NULL},
    [MFD_MSG_RWY_LS_DISAGREE]              = {"RUNWAY / LS DISAGREE", 1, 1, NULL},
    [MFD_MSG_SET_HOLD_SPD]                 = {"SET HOLD SPD", 1, 0, NULL},
    [MFD_MSG_TD_REACHED]                   = {"T/D REACHED", 1, 0, NULL},
    [MFD_MSG_SPD_LIM_EXCEEDED]             = {"SPD LIMIT EXCEEDED", 1, 1, NULL},
    [MFD_MSG_TO_SPEED_TOO_LOW]             = {"T.O SPEED TOO LOW - CHECK TOW & T.O DATA", 1, 1, NULL},
    [MFD_MSG_V_TO_DISAGREE]                = {"V1/VR/V2 DISAGREE", 1, 1, NULL},
    [MFD_MSG_XXX_IS_DESELECTED]            = {"XXXX IS DESELECTED", 0, 0, "XXXX"},
    [MFD_MSG_STEP_ABOVE_MAX_FL]            = {"STEP ABOVE MAX FL", 1, 0, NULL},
    [MFD_MSG_STEP_AHEAD]                   = {"STEP AHEAD", 1, 0, NULL},
    [MFD_MSG_STEP_DELETED]                 = {"STEP DELETED", 1, 0, NULL},
    [MFD_MSG_TOO_STEEP_PATH_AHEAD]         = {"TOO STEEP PATH AHEAD", 1, 0, NULL},
    [MFD_MSG_NAV_PRIMARY]                  = {"NAV PRIMARY", 1, 0, NULL},
    [MFD_MSG_NAV_PRIMARY_LOST]             = {"NAV PRIMARY LOST", 1, 1, NULL},
    [MFD_MSG_SQWK_CODE_NOT_VALID]          = {"SQWK CODE NOT VALID", 0, 0, NULL},
    [MFD_MSG_LRC_IN_USE]                   = {"LRC MODE IN USE", 0, 0, NULL},
    [MFD_MSG_LAT_DISCONTINUITY_AHEAD]      = {"LATERAL DISCONTINUITY AHEAD", 1, 1, NULL},

    /* NXFictionalMessages (MSG:124-151) */
    [MFD_MSG_NOT_YET_IMPLEMENTED]          = {"NOT YET IMPLEMENTED", 0, 0, NULL},
    [MFD_MSG_NO_NAVIGRAPH_USER]            = {"NO NAVIGRAPH USER", 0, 0, NULL},
    [MFD_MSG_INTERNAL_ERROR]               = {"INTERNAL ERROR", 0, 0, NULL},
    [MFD_MSG_NO_AIRPORT_SPECIFIED]         = {"NO AIRPORT SPECIFIED", 0, 0, NULL},
    [MFD_MSG_FLT_NBR_IN_USE]               = {"FLT NBR IN USE", 0, 0, NULL},
    [MFD_MSG_FLT_NBR_MISSING]              = {"ENTER ATC FLT NBR", 0, 0, NULL},
    [MFD_MSG_RECIPIENT_NOT_FOUND]          = {"RECIPIENT NOT FOUND", 0, 0, NULL},
    [MFD_MSG_AUTH_ERR]                     = {"AUTH ERR", 0, 0, NULL},
    [MFD_MSG_INVALID_MSG]                  = {"INVALID MSG", 0, 0, NULL},
    [MFD_MSG_UNKNOWN_DOWNLINK_ERR]         = {"UNKNOWN DOWNLINK ERR", 0, 0, NULL},
    [MFD_MSG_TELEX_NOT_ENABLED]            = {"TELEX NOT ENABLED", 0, 0, NULL},
    [MFD_MSG_FREE_TEXT_DISABLED]           = {"FREE TEXT DISABLED", 0, 0, NULL},
    [MFD_MSG_FREE_TEXT_ENABLED]            = {"FREE TEXT ENABLED", 0, 0, NULL},
    [MFD_MSG_ENABLED_FLT_NBR_IN_USE]       = {"ENABLED. FLT NBR IN USE", 0, 0, NULL},
    [MFD_MSG_NO_ORIGIN_APT]                = {"NO ORIGIN AIRPORT", 0, 0, NULL},
    [MFD_MSG_NO_ORIGIN_SET]                = {"NO ORIGIN SET", 0, 0, NULL},
    [MFD_MSG_SECOND_INDEX_NOT_FOUND]       = {"2ND INDEX NOT FOUND", 0, 0, NULL},
    [MFD_MSG_FIRST_INDEX_NOT_FOUND]        = {"1ST INDEX NOT FOUND", 0, 0, NULL},
    [MFD_MSG_NO_REF_WPT]                   = {"NO REF WAYPOINT", 0, 0, NULL},
    [MFD_MSG_NO_WPT_INFOS]                 = {"NO WAYPOINT INFOS", 0, 0, NULL},
    [MFD_MSG_EMPTY_MESSAGE]                = {"", 0, 0, NULL},
    [MFD_MSG_RELOAD_PLANE_APPLY]           = {"RELOAD A/C TO APPLY", 1, 1, NULL},
    [MFD_MSG_NO_ACARS_CONNECTION]          = {"NO ACARS CONNECTION", 0, 0, NULL},
    [MFD_MSG_UNKNOWN_ATSU_MESSAGE]         = {"UNKNOWN ATSU MESSAGE", 0, 0, NULL},
    [MFD_MSG_REVERSE_PROXY]                = {"REVERSE PROXY ERROR", 0, 0, NULL},
    [MFD_MSG_SIMBRIEF_NO_USER]             = {"NO SIMBRIEF PILOT ID PROVIDED", 0, 0, NULL},

    /* ATCCOMMessages (MSG:157-185) */
    [MFD_MSG_ATC_CANCEL_AUTO_UPDATE_FIRST]                 = {"CANCEL AUTO UPDATE FIRST", 0, 0, NULL},
    [MFD_MSG_ATC_COM_DATALINK_NOT_AVAIL]                   = {"COM DATALINK NOT AVAIL", 0, 0, NULL},
    [MFD_MSG_ATC_DATIS_GROUND_MSG]                         = {"D-ATIS GROUND MSG", 0, 0, NULL},
    [MFD_MSG_ATC_DATIS_NO_REPLY]                           = {"D-ATIS NO REPLY", 0, 0, NULL},
    [MFD_MSG_ATC_DATIS_RECEIVED]                           = {"D-ATIS RECEIVED", 0, 0, NULL},
    [MFD_MSG_ATC_DATIS_SEND_FAILED]                        = {"D-ATIS SEND FAILED", 0, 0, NULL},
    [MFD_MSG_ATC_DATIS_UPDATED]                            = {"D-ATIS UPDATED - READ AGAIN", 0, 0, NULL},
    [MFD_MSG_ATC_DATIS_USED_OFFSIDE]                       = {"D-ATIS USED OFFSIDE", 0, 0, NULL},
    [MFD_MSG_ATC_ENTRY_OUT_OF_RANGE]                       = {"ENTRY OUT OF RANGE", 0, 0, NULL},
    [MFD_MSG_ATC_FORMAT_ERROR]                             = {"FORMAT ERROR", 0, 0, NULL},
    [MFD_MSG_ATC_IDENTICAL_DATIS_REQUEST]                  = {"IDENTICAL D-ATIS REQUEST", 0, 0, NULL},
    [MFD_MSG_ATC_LAST_MSG_ELEMENT]                         = {"LAST MSG ELEMENT", 0, 0, NULL},
    [MFD_MSG_ATC_MAILBOX_FULL]                             = {"MAILBOX FULL - SEND OR CANCEL SOME MSG", 0, 0, NULL},
    [MFD_MSG_ATC_MSG_ABORTED_ACTIVE_ATC_DISCONNECTED]      = {"MSG ABORTED - ACTIVE ATC DISCONNECTED", 0, 0, NULL},
    [MFD_MSG_ATC_MSG_ABORTED_NOT_SUPPORTED_BY_CURRENT_ATC] = {"MSG ABORTED - NOT SUPPORTED BY CURRENT ATC", 0, 0, NULL},
    [MFD_MSG_ATC_MSG_RECORD_LOST]                          = {"MSG RECORD LOST", 0, 0, NULL},
    [MFD_MSG_ATC_MSG_RECORD_USED_OFFSIDE]                  = {"MSG RECORD USED OFFSIDE", 0, 0, NULL},
    [MFD_MSG_ATC_NEW_DATIS_GROUND_MSG]                     = {"NEW D-ATIS GROUND MSG - READ AGAIN", 0, 0, NULL},
    [MFD_MSG_ATC_NO_SYS_DATA]                              = {"NO SYS DATA", 0, 0, NULL},
    [MFD_MSG_ATC_NOTIF_NOT_AVAIL_ACFT_POS_NOT_AVAIL]       = {"NOTIFICATION NOT AVAIL - ACFT POSITION NOT AVAIL", 0, 0, NULL},
    [MFD_MSG_ATC_NOTIF_NOT_AVAIL_CHCK_FLT_NBR]             = {"NOTIFICATION NOT AVAIL - CHECK FLT NBR IN FMS INIT PAGE", 0, 0, NULL},
    [MFD_MSG_ATC_NOTIF_NOT_AVAIL_CHCK_FROM_TO]             = {"NOTIFICATION NOT AVAIL - CHECK FROM/TO IN FMS INIT PAGE", 0, 0, NULL},
    [MFD_MSG_ATC_NOTIF_NOT_AVAIL_WITH_THIS_ATC_CTR]        = {"NOTIFICATION NOT AVAIL - WITH THIS ATC CENTER", 0, 0, NULL},
    [MFD_MSG_ATC_PLEASE_WAIT_UPDATE_IN_PROGRESS]           = {"PLEASE WAIT: UPDATE IN PROGRESS", 0, 0, NULL},
    [MFD_MSG_ATC_PRINTER_NOT_AVAIL]                        = {"PRINTER NOT AVAIL", 0, 0, NULL},
    [MFD_MSG_ATC_PRINTING]                                 = {"PRINTING", 0, 0, NULL},
    [MFD_MSG_ATC_SENDING_MAYDAY_WILL_SWITCH_ADSC_TO_EMERGENCY] = {"SENDING MAYDAY WILL SWITCH ADS-C TO EMERGENCY", 0, 0, NULL},
};
/* clang-format on */

static int id_valid(MfdMsgId id) { return (int)id >= 0 && id < MFD_MSG_COUNT; }

int mfd_msg_is_atccom(MfdMsgId id) { return id_valid(id) && id >= MFD_MSG_FIRST_ATCCOM; }

/* Copies with truncation on a UTF-8 boundary; returns 1 when everything fitted. */
static int copy_text(char *dst, size_t n, const char *src)
{
    size_t len = strlen(src);
    int fits = 1;
    if (n == 0) return 0;
    if (len >= n) {
        fits = 0;
        len = n - 1;
        while (len > 0 && (((unsigned char)src[len]) & 0xC0u) == 0x80u) len--;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
    return fits;
}

int mfd_msg_build_text(MfdMsgId id, const char *token_value, const char *details, char *buf, size_t n)
{
    char tmp[256];
    const MfdMsgDef *d;
    int fits = 1;
    if (!buf || n == 0) return 0;
    buf[0] = '\0';
    if (!id_valid(id)) return 0;
    d = &mfd_msg_defs[id];
    copy_text(tmp, sizeof tmp, d->text);
    /* getModifiedMessage(t): t ? text.replace(replace, t) : text */
    if (d->token && d->token[0] && token_value && token_value[0]) {
        mfd_str_replace_first(tmp, sizeof tmp, d->token, token_value);
        if (strlen(tmp) >= sizeof tmp - 1) fits = 0;
    }
    if (details && details[0]) {
        size_t len = strlen(tmp);
        fits &= copy_text(tmp + len, sizeof tmp - len, "\n");
        len = strlen(tmp);
        fits &= copy_text(tmp + len, sizeof tmp - len, details);
    }
    fits &= copy_text(buf, n, tmp);
    return fits;
}

static void remove_at(MfdMsgQueue *q, int i)
{
    if (i < 0 || i >= q->count) return;
    if (i < q->count - 1) memmove(&q->e[i], &q->e[i + 1], (size_t)(q->count - i - 1) * sizeof q->e[0]);
    q->count--;
    memset(&q->e[q->count], 0, sizeof q->e[0]);
    q->version++;
}

void mfd_msg_init(MfdMsgQueue *q)
{
    if (!q) return;
    memset(q, 0, sizeof *q);
    q->version = 1;
}

void mfd_msg_reset(MfdMsgQueue *q)
{
    unsigned v;
    if (!q) return;
    v = q->version;
    memset(q, 0, sizeof *q);
    q->version = v + 1;
}

void mfd_msg_add(MfdMsgQueue *q, MfdMsgId id, const char *token_value, const char *details,
                 MfdMsgResolvedFn res, MfdMsgClearFn clr, void *ctx)
{
    MfdMsgEntry ent;
    const MfdMsgDef *d;
    int i;
    if (!q || !id_valid(id)) return;
    if (q->count < 0 || q->count > MFD_MSG_QUEUE_CAP) q->count = 0;
    d = &mfd_msg_defs[id];

    memset(&ent, 0, sizeof ent);
    ent.id = id;
    mfd_msg_build_text(id, token_value, details, ent.text, sizeof ent.text);
    ent.bg = d->amber ? MFD_MSG_BG_AMBER : MFD_MSG_BG_WHITE;
    ent.type2 = d->type2;
    ent.cleared = 0;
    ent.origin_side = 0;
    if (d->type2) { /* FBW: Type I entries get () => false / no-op */
        ent.is_resolved = res;
        ent.on_clear = clr;
        ent.ctx = ctx;
    }

    for (i = 0; i < q->count; i++) {
#ifdef MFD_MSG_DEDUP_UNCLEARED
        if (strcmp(q->e[i].text, ent.text) == 0) {
#else
        if (q->e[i].cleared && strcmp(q->e[i].text, ent.text) == 0) {
#endif
            remove_at(q, i);
            break;
        }
    }

    if (q->count >= MFD_MSG_QUEUE_CAP) { /* [PORT] Q15: drop the oldest cleared entry, else the oldest */
        int victim = q->count - 1;
        for (i = q->count - 1; i >= 0; i--) {
            if (q->e[i].cleared) {
                victim = i;
                break;
            }
        }
        remove_at(q, victim);
    }

    memmove(&q->e[1], &q->e[0], (size_t)q->count * sizeof q->e[0]);
    q->e[0] = ent;
    q->count++;
    q->version++;
}

void mfd_msg_show_error(MfdMsgQueue *q, const MfdFmsError *err, int with_details)
{
    const char *details;
    if (!q || !err) return;
    details = (with_details && err->details[0]) ? err->details : NULL;
    switch (err->type) {
    case MFD_ERR_ENTRY_OUT_OF_RANGE: mfd_msg_add(q, MFD_MSG_ENTRY_OUT_OF_RANGE, NULL, details, NULL, NULL, NULL); break;
    case MFD_ERR_FORMAT_ERROR: mfd_msg_add(q, MFD_MSG_FORMAT_ERROR, NULL, details, NULL, NULL, NULL); break;
    case MFD_ERR_NOT_IN_DATABASE: mfd_msg_add(q, MFD_MSG_NOT_IN_DATABASE, NULL, NULL, NULL, NULL, NULL); break;
    case MFD_ERR_NOT_YET_IMPLEMENTED: mfd_msg_add(q, MFD_MSG_NOT_YET_IMPLEMENTED, NULL, NULL, NULL, NULL, NULL); break;
    case MFD_ERR_NOT_ALLOWED: mfd_msg_add(q, MFD_MSG_NOT_ALLOWED, NULL, details, NULL, NULL, NULL); break;
    default: break;
    }
}

int mfd_msg_remove_text(MfdMsgQueue *q, const char *text)
{
    int i;
    if (!q || !text) return 0;
    for (i = 0; i < q->count; i++) {
        if (strcmp(q->e[i].text, text) == 0) {
            remove_at(q, i);
            return 1;
        }
    }
    return 0;
}

int mfd_msg_remove_id(MfdMsgQueue *q, MfdMsgId id)
{
    int i, removed = 0;
    if (!q) return 0;
    for (i = q->count - 1; i >= 0; i--) {
        if (q->e[i].id == id) {
            remove_at(q, i);
            removed++;
        }
    }
    return removed;
}

int mfd_msg_has_id(const MfdMsgQueue *q, MfdMsgId id)
{
    int i;
    if (!q) return 0;
    for (i = 0; i < q->count; i++)
        if (q->e[i].id == id) return 1;
    return 0;
}

void mfd_msg_clear_latest(MfdMsgQueue *q)
{
    int i;
    if (!q) return;
    for (i = 0; i < q->count; i++) {
        if (q->e[i].cleared) continue;
        if (q->e[i].type2) {
            MfdMsgClearFn fn = q->e[i].on_clear;
            void *ctx = q->e[i].ctx;
            q->e[i].cleared = 1;
            q->version++;
            if (fn) fn(ctx); /* after the state change, as FBW does */
        } else {
            remove_at(q, i);
        }
        return;
    }
}

void mfd_msg_update(MfdMsgQueue *q)
{
    int i;
    if (!q) return;
    for (i = q->count - 1; i >= 0; i--) { /* [PORT] Q16: backwards, no entry skipped */
        if (i >= q->count) continue;    /* a callback shrank the queue */
        if (q->e[i].type2 && q->e[i].is_resolved && q->e[i].is_resolved(q->e[i].ctx)) remove_at(q, i);
    }
}

const MfdMsgEntry *mfd_msg_footer_entry(const MfdMsgQueue *q)
{
    int i;
    if (!q) return NULL;
    for (i = 0; i < q->count; i++)
        if (!q->e[i].cleared) return &q->e[i];
    return NULL;
}

int mfd_msg_list(const MfdMsgQueue *q, const MfdMsgEntry **out, int max)
{
    int i, n;
    if (!q) return 0;
    if (!out) return q->count;
    n = q->count < max ? q->count : max;
    if (n < 0) n = 0;
    for (i = 0; i < n; i++) out[i] = &q->e[i];
    return n;
}
