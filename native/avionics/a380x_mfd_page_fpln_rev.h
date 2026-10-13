/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD F-PLN page family, part 2 (WP-G): F-PLN/DEPARTURE, F-PLN/ARRIVAL, F-PLN/AIRWAYS,
 * F-PLN/DIRECT-TO, F-PLN/HOLD, F-PLN/VERT REV (RTA placeholder, SPD, CMS placeholder, ALT,
 * STEP ALTs), F-PLN/FIX INFO and the DUPLICATE NAMES dialog (docs/mfd-spec/05-fms-fpln.md sections
 * 7-15). Translation of the pinned FBW MfdFmsFplnDep/Arr/Airways/DirectTo/Hold/VertRev/FixInfo/
 * DuplicateNames.tsx and FlightPlanFooter.tsx.
 */
#ifndef A380X_MFD_PAGE_FPLN_REV_H
#define A380X_MFD_PAGE_FPLN_REV_H

#include "a380x_mfd_page_fpln.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Page states ------------------------------------------------------------------------------ */

typedef struct {
    int awy_scroll;            /* first AIRWAYS line shown ([PORT] scrolling) */
    int dup_wait;              /* TO entry waiting for DUPLICATE NAMES */
    int dup_line;
} MfdFplnAwyState;
MFD_PAGE_STATE_CHECK(MfdFplnAwyState);

typedef struct {
    int option;                /* 0 DIRECT, 1 WITH ABEAM, 2 CRS IN, 3 CRS OUT */
    int selected;              /* index in the dropdown list, -1 */
    char typed[A380X_NAV_IDENT_LEN]; /* free-text target ident ("" = none) */
    int dup_wait;
} MfdFplnDirState;
MFD_PAGE_STATE_CHECK(MfdFplnDirState);

typedef struct {
    int param;                 /* 0 TIME, 1 DIST (leg defining parameter radio) */
    int param_init;
} MfdFplnHoldState;
MFD_PAGE_STATE_CHECK(MfdFplnHoldState);

typedef struct {
    int tab;                   /* 0 RTA, 1 SPD, 2 CMS, 3 ALT, 4 STEP ALTs */
    int wpt;                   /* selected leg index (SPD/ALT tabs), -1 */
    int wpt_altn;
    int cstr_type_sel;         /* 0 none, 1 CLB, 2 DES (amber radio when the type is unknown) */
    int alt_option;            /* -1 none, 0 AT, 1 AT OR ABOVE, 2 AT OR BELOW */
    int step_start;            /* STEP ALTs scroll */
    int step_new_wpt;          /* leg index chosen in the empty row, -1 */
    int step_reject_leg;       /* STEP NOT ALLOWED AT <ident> (-1 none) */
    uint32_t last_version;
} MfdFplnVrevState;
MFD_PAGE_STATE_CHECK(MfdFplnVrevState);

typedef struct {
    int tab;                   /* FIX 1..4 -> 0..3 */
    int dup_wait;
} MfdFplnFixState;
MFD_PAGE_STATE_CHECK(MfdFplnFixState);

/* Register DEPARTURE, ARRIVAL, AIRWAYS, HOLD, VERT REV (fms/<cat>/...), DIRECT-TO and FIX INFO
 * (fms/active/...). */
void a380x_mfd_page_fpln_rev_register(void);

/* ---- DUPLICATE NAMES (spec 05 section 14) -------------------------------------------------------
 * The MFD frame has no overlay host for it, so the F-PLN family hosts it: a page that needs a
 * choice opens it with a requester tag and then calls a380x_mfd_fpln_dup_run() every frame (at the
 * end of its page function). The dialog is an overlay (layer 20) over the title bar and the body
 * (y 98..950). Navigation (ctx->entered) or a new request cancels a pending one. */
#define A380X_MFD_DUP_MAX 32
void a380x_mfd_fpln_dup_open(MfdSide side, unsigned requester, const A380XNavFix *items, int n);
int  a380x_mfd_fpln_dup_is_open(MfdSide side);
/* Draws the dialog while open. Returns 1 and *out when this requester's request was answered with
 * a row, -1 when it was cancelled (RETURN), 0 otherwise. */
int  a380x_mfd_fpln_dup_run(MfdPageContext *ctx, unsigned requester, A380XNavFix *out);

/* ETT (estimated take-off time) entered on VERT REV RTA, seconds of day, NAN = none. The FMS has
 * no store for it yet; the F-PLN page uses it for the PREFLIGHT time base. */
double a380x_mfd_fpln_ett(void);

#ifdef __cplusplus
}
#endif

#endif
