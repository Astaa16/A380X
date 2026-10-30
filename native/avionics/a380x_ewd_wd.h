/* SPDX-License-Identifier: GPL-3.0-or-later
 * Lower EWD (warning display, WD) view: what the native FWS publishes for one frame, already
 * resolved to FBW formatted strings. Layout and semantics follow the pinned FlyByWire A380X
 * EWD.tsx WarningDisplayArea: WdLimitations + WdMemos, or one 18-line ECL window
 * (WdAbstractChecklistComponent), and the STS area (FAILURE PENDING / STS / ADV boxes).
 * Strings point to static FWS tables or to the view's own storage; XPLM-free.
 */
#ifndef A380X_EWD_WD_H
#define A380X_EWD_WD_H
#include "a380x_fws_types.h"

#define EWD_WD_MEMO_LINES 10   /* WdMemos memo_left_1..10 / memo_right_1..10 */
#define EWD_WD_LIMIT_LINES 10

enum { EWD_WD_MEMOS, EWD_WD_PROCEDURES };

typedef struct {
    int valid;                 /* 0: legacy EwdData memo/warning lists are drawn instead */
    int mode;                  /* EWD_WD_MEMOS (limitations + memos) or EWD_WD_PROCEDURES */
    const char *memo_left[EWD_WD_MEMO_LINES], *memo_right[EWD_WD_MEMO_LINES];
    int memo_left_count, memo_right_count;
    const char *limit_all[EWD_WD_LIMIT_LINES], *limit_appr_ldg[EWD_WD_LIMIT_LINES];
    int limit_all_count, limit_appr_ldg_count;
    FwsWdLine line[FWS_WD_NUM_LINES];
    int selected_line;          /* index into line[] or -1 */
    int overflow_up, overflow_down;
    int sts_area_visible;       /* nor(abnormal sensed, abnormal non-sensed) */
    int failure_pending, sts, adv, sts_deferred; /* sts_deferred: "STS & DEFRD PROC" */
} EwdWdView;

#endif
