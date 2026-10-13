/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X FMS message catalogue and the queue shared by both MFDs.
 * Translation of the pinned FlyByWire NXSystemMessages.ts and FMC queue semantics
 * (docs/mfd-spec/02-data-formats-messages.md §10-11.5, 07-fmc-logic.md §7).
 */
#ifndef A380X_FMS_MESSAGES_H
#define A380X_FMS_MESSAGES_H

#include "a380x_mfd_formats.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    /* NXSystemMessages, in the order of spec 02 §10.5 */
    MFD_MSG_AWY_WPT_DISAGREE, MFD_MSG_CRZ_FL_ABOVE_MAX_FL, MFD_MSG_CANCEL_ATIS_UPDATE,
    MFD_MSG_CHECK_MIN_FUEL_AT_DEST, MFD_MSG_CHECK_SPD_MODE, MFD_MSG_CHECK_TO_DATA, MFD_MSG_CHECK_ZFW,
    MFD_MSG_COM_FPLN_RECEIVED, MFD_MSG_COM_DATALINK_NOT_AVAIL, MFD_MSG_CSTR_DEL_UP_TO_WPT,
    MFD_MSG_DATABASE_CODING_ERROR, MFD_MSG_DEST_EFOB_BELOW_MIN, MFD_MSG_ENTER_DEST_DATA,
    MFD_MSG_ENTRY_OUT_OF_RANGE, MFD_MSG_FORMAT_ERROR, MFD_MSG_FPLN_ELEMENT_RETAINED,
    MFD_MSG_INITIALIZE_ZFW_ZFWCG, MFD_MSG_NEW_ACC_ALT, MFD_MSG_NEW_CRZ_ALT, MFD_MSG_NEW_THR_RED_ALT,
    MFD_MSG_NO_INTERSECTION_FOUND, MFD_MSG_NOT_ALLOWED, MFD_MSG_NOT_ALLOWED_IN_NAV, MFD_MSG_NOT_IN_DATABASE,
    MFD_MSG_RECEIVED_CPNY_FPLN_NOT_VALID, MFD_MSG_RWY_LS_DISAGREE, MFD_MSG_SET_HOLD_SPD, MFD_MSG_TD_REACHED,
    MFD_MSG_SPD_LIM_EXCEEDED, MFD_MSG_TO_SPEED_TOO_LOW, MFD_MSG_V_TO_DISAGREE, MFD_MSG_XXX_IS_DESELECTED,
    MFD_MSG_STEP_ABOVE_MAX_FL, MFD_MSG_STEP_AHEAD, MFD_MSG_STEP_DELETED, MFD_MSG_TOO_STEEP_PATH_AHEAD,
    MFD_MSG_NAV_PRIMARY, MFD_MSG_NAV_PRIMARY_LOST, MFD_MSG_SQWK_CODE_NOT_VALID, MFD_MSG_LRC_IN_USE,
    MFD_MSG_LAT_DISCONTINUITY_AHEAD,
    MFD_MSG_NOT_YET_IMPLEMENTED,
    /* NXFictionalMessages (spec 02 §10.7), remaining entries in upstream order. All Type I white except
     * RELOAD A/C TO APPLY (Type II amber). */
    MFD_MSG_NO_NAVIGRAPH_USER, MFD_MSG_INTERNAL_ERROR, MFD_MSG_NO_AIRPORT_SPECIFIED, MFD_MSG_FLT_NBR_IN_USE,
    MFD_MSG_FLT_NBR_MISSING, MFD_MSG_RECIPIENT_NOT_FOUND, MFD_MSG_AUTH_ERR, MFD_MSG_INVALID_MSG,
    MFD_MSG_UNKNOWN_DOWNLINK_ERR, MFD_MSG_TELEX_NOT_ENABLED, MFD_MSG_FREE_TEXT_DISABLED,
    MFD_MSG_FREE_TEXT_ENABLED, MFD_MSG_ENABLED_FLT_NBR_IN_USE, MFD_MSG_NO_ORIGIN_APT, MFD_MSG_NO_ORIGIN_SET,
    MFD_MSG_SECOND_INDEX_NOT_FOUND, MFD_MSG_FIRST_INDEX_NOT_FOUND, MFD_MSG_NO_REF_WPT, MFD_MSG_NO_WPT_INFOS,
    MFD_MSG_EMPTY_MESSAGE, MFD_MSG_RELOAD_PLANE_APPLY, MFD_MSG_NO_ACARS_CONNECTION,
    MFD_MSG_UNKNOWN_ATSU_MESSAGE, MFD_MSG_REVERSE_PROXY, MFD_MSG_SIMBRIEF_NO_USER,
    /* ATCCOMMessages (spec 02 §10.8), upstream order. Type I-like, white; ATCCOM queue only. */
    MFD_MSG_ATC_CANCEL_AUTO_UPDATE_FIRST, MFD_MSG_ATC_COM_DATALINK_NOT_AVAIL, MFD_MSG_ATC_DATIS_GROUND_MSG,
    MFD_MSG_ATC_DATIS_NO_REPLY, MFD_MSG_ATC_DATIS_RECEIVED, MFD_MSG_ATC_DATIS_SEND_FAILED,
    MFD_MSG_ATC_DATIS_UPDATED, MFD_MSG_ATC_DATIS_USED_OFFSIDE, MFD_MSG_ATC_ENTRY_OUT_OF_RANGE,
    MFD_MSG_ATC_FORMAT_ERROR, MFD_MSG_ATC_IDENTICAL_DATIS_REQUEST, MFD_MSG_ATC_LAST_MSG_ELEMENT,
    MFD_MSG_ATC_MAILBOX_FULL, MFD_MSG_ATC_MSG_ABORTED_ACTIVE_ATC_DISCONNECTED,
    MFD_MSG_ATC_MSG_ABORTED_NOT_SUPPORTED_BY_CURRENT_ATC, MFD_MSG_ATC_MSG_RECORD_LOST,
    MFD_MSG_ATC_MSG_RECORD_USED_OFFSIDE, MFD_MSG_ATC_NEW_DATIS_GROUND_MSG, MFD_MSG_ATC_NO_SYS_DATA,
    MFD_MSG_ATC_NOTIF_NOT_AVAIL_ACFT_POS_NOT_AVAIL, MFD_MSG_ATC_NOTIF_NOT_AVAIL_CHCK_FLT_NBR,
    MFD_MSG_ATC_NOTIF_NOT_AVAIL_CHCK_FROM_TO, MFD_MSG_ATC_NOTIF_NOT_AVAIL_WITH_THIS_ATC_CTR,
    MFD_MSG_ATC_PLEASE_WAIT_UPDATE_IN_PROGRESS, MFD_MSG_ATC_PRINTER_NOT_AVAIL, MFD_MSG_ATC_PRINTING,
    MFD_MSG_ATC_SENDING_MAYDAY_WILL_SWITCH_ADSC_TO_EMERGENCY,
    MFD_MSG_COUNT
} MfdMsgId;

/* Catalogue sections (first id of each group). */
#define MFD_MSG_FIRST_FICTIONAL MFD_MSG_NOT_YET_IMPLEMENTED
#define MFD_MSG_FIRST_ATCCOM    MFD_MSG_ATC_CANCEL_AUTO_UPDATE_FIRST

typedef struct {
    const char   *text;  /* verbatim, may contain '\n' */
    unsigned char type2; /* 1 = Type II */
    unsigned char amber;
    const char   *token; /* "WWWWW", "HHHHH", "XXXX" or NULL */
} MfdMsgDef;
extern const MfdMsgDef mfd_msg_defs[MFD_MSG_COUNT];

typedef enum { MFD_MSG_BG_WHITE = 0, MFD_MSG_BG_AMBER, MFD_MSG_BG_CYAN } MfdMsgBg;
typedef int  (*MfdMsgResolvedFn)(void *ctx);
typedef void (*MfdMsgClearFn)(void *ctx);

typedef struct {
    MfdMsgId id;
    char     text[128];
    MfdMsgBg bg;
    unsigned char type2, cleared, origin_side;
    MfdMsgResolvedFn is_resolved; /* NULL = never */
    MfdMsgClearFn    on_clear;    /* NULL = none */
    void    *ctx;
} MfdMsgEntry;

#define MFD_MSG_QUEUE_CAP 16
typedef struct {
    MfdMsgEntry e[MFD_MSG_QUEUE_CAP]; /* e[0] = newest */
    int      count;
    unsigned version;                 /* incremented on every change */
} MfdMsgQueue;

void mfd_msg_init(MfdMsgQueue *q);
void mfd_msg_add(MfdMsgQueue *q, MfdMsgId id, const char *token_value, const char *details,
                 MfdMsgResolvedFn res, MfdMsgClearFn clr, void *ctx);
/* showFmsErrorMessage: maps an MfdFmsError to FORMAT ERROR / ENTRY OUT OF RANGE / NOT IN DATABASE /
 * NOT ALLOWED / NOT YET IMPLEMENTED, optionally with the second-line details. */
void mfd_msg_show_error(MfdMsgQueue *q, const MfdFmsError *err, int with_details);
int  mfd_msg_remove_text(MfdMsgQueue *q, const char *text);
int  mfd_msg_remove_id(MfdMsgQueue *q, MfdMsgId id);
int  mfd_msg_has_id(const MfdMsgQueue *q, MfdMsgId id);
void mfd_msg_clear_latest(MfdMsgQueue *q);                      /* CLEAR INFO */
void mfd_msg_update(MfdMsgQueue *q);                            /* every 250 ms FMS cycle */
const MfdMsgEntry *mfd_msg_footer_entry(const MfdMsgQueue *q);  /* first uncleared or NULL */
int  mfd_msg_list(const MfdMsgQueue *q, const MfdMsgEntry **out, int max);

/* ==== WP-B additions (appended; the declarations above are frozen) ============================
 *
 * Contract of the declarations above, as implemented in a380x_fms_messages.c (spec 02 §10.2):
 *
 * mfd_msg_init()         Empties the queue; version = 1.
 * mfd_msg_add()          text = def text with the first token replaced by token_value (only when the def has
 *                        a token and token_value is non-empty) + "\n" + details (when non-empty); truncated
 *                        to 127 bytes. bg from def->amber. Type II entries keep res/clr/ctx (NULL = never
 *                        resolved / no on-clear); Type I entries never store callbacks (FBW). The first entry
 *                        with identical text that is already cleared is removed; identical uncleared entries
 *                        stack (FBW; build with MFD_MSG_DEDUP_UNCLEARED to replace them instead, spec 07 §11-4).
 *                        The new entry goes to e[0]. When the queue is full, the oldest cleared entry is
 *                        dropped, else the oldest entry ([PORT] Q15). origin_side is 0 (both sides).
 * mfd_msg_show_error()   ENTRY OUT OF RANGE / FORMAT ERROR (+ details if with_details and non-empty),
 *                        NOT IN DATABASE, NOT YET IMPLEMENTED (never details), NOT ALLOWED (port extension,
 *                        details as for FORMAT ERROR); MFD_ERR_NONE / NULL: nothing. Used for the ATCCOM queue
 *                        too (FBW maps ATCCOM entry errors to the NX messages).
 * mfd_msg_remove_text()  removes the first entry whose text matches exactly; returns 1 if one was removed.
 * mfd_msg_remove_id()    [PORT] Q13: removes every entry with this id; returns the number removed.
 * mfd_msg_has_id()       1 if any entry (cleared or not) has this id.
 * mfd_msg_clear_latest() CLEAR INFO: first uncleared entry; Type I is deleted, Type II is marked cleared and
 *                        its on_clear(ctx) is called afterwards.
 * mfd_msg_update()       removes every Type II entry whose is_resolved(ctx) returns non-zero; iterates from the
 *                        oldest entry so no entry is skipped ([PORT] Q16).
 * mfd_msg_list()         copies up to max entry pointers, newest first (cleared and uncleared); returns the
 *                        number written, or the total count when out is NULL. Pointers are valid until the
 *                        next queue change.
 * version                incremented on every change (add, remove, clear, update, reset).
 */

/* [PORT] FMC reset / nav database swap: empty the queue but keep version monotonic. */
void mfd_msg_reset(MfdMsgQueue *q);
/* Builds the text mfd_msg_add() would store (for remove_text of substituted texts, tests, ...).
 * Returns 1 when the full text fitted into buf. */
int  mfd_msg_build_text(MfdMsgId id, const char *token_value, const char *details, char *buf, size_t n);
int  mfd_msg_is_atccom(MfdMsgId id);

#ifdef __cplusplus
}
#endif

#endif
