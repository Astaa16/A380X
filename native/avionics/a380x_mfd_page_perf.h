/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD FMS PERF page: ACTIVE/PERF and SECn/PERF (fms/ * /perf[/to|clb|crz|des|appr|ga]).
 * Translation of the pinned FBW pages/FMS/MfdFmsPerf.tsx (docs/mfd-spec/04-fms-perf.md), laid out
 * with MSFS/Coherent border-box sizing on the spec 01 frame (page body y 138..950).
 *
 * All data and validation come from the FMS core (a380x_fms.h); the page only draws, routes pilot
 * entries to a380x_fms_enter()/a380x_fms_clear() and keeps its page-local state below.
 */
#ifndef A380X_MFD_PAGE_PERF_H
#define A380X_MFD_PAGE_PERF_H

#include "a380x_mfd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Phase tab indices (FlightPhaseTabIndex = FMGC phase - 1). */
enum {
    MFD_PERF_TAB_TO = 0,
    MFD_PERF_TAB_CLB = 1,
    MFD_PERF_TAB_CRZ = 2,
    MFD_PERF_TAB_DES = 3,
    MFD_PERF_TAB_APPR = 4,
    MFD_PERF_TAB_GA = 5,
    MFD_PERF_TAB_COUNT = 6
};

/* Page dialogs (only one at a time). */
enum {
    MFD_PERF_DLG_NONE = 0,
    MFD_PERF_DLG_ACTIVATE_APPR = 1,
    MFD_PERF_DLG_CLEAR_EO = 2,
    MFD_PERF_DLG_DERATED = 3
};

typedef struct {
    int tab;              /* MFD_PERF_TAB_* currently selected */
    int prev_phase;       /* FMGC phase seen on the previous frame (tab follow rule, spec 04 §4.1) */
    int dialog;           /* MFD_PERF_DLG_* */
    int derated_prev;     /* DERATED dialog: value restored by CANCEL (-1 none) */
    int derated_next;     /* DERATED dialog: value stored by CONFIRM */
    int des_pred_has;     /* DES PRED TO: page-local value (FBW-PH, not stored in the FMS) */
    double des_pred_ft;
} MfdPerfState;
MFD_PAGE_STATE_CHECK(MfdPerfState);

/* Register fms/ * /perf (ACTIVE and SEC1..3). */
void a380x_mfd_page_perf_register(void);

/* Tab for a URI extra ("to", "clb", "crz", "des", "appr", "ga"); -1 if none/unknown. */
int mfd_perf_tab_from_extra(const char *extra);

/* Tab-follow rule of spec 04 §4.1: returns the new selected tab after an FMGC phase change
 * (old_phase -> new_phase) while `selected` was shown; aoc = loaded plan is Active or a copy. */
int mfd_perf_follow_phase(int selected, int old_phase, int new_phase, int aoc);

#ifdef __cplusplus
}
#endif

#endif
