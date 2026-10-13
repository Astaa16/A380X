/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD F-PLN page family, part 1 (WP-G): ACTIVE/F-PLN and SECn/F-PLN, the lateral revision
 * context menu, the INSERT NEXT WPT and NEW DEST windows (docs/mfd-spec/05-fms-fpln.md sections 1,
 * 3-6), plus the helpers shared with the revision pages of a380x_mfd_page_fpln_rev.c.
 * Translation of the pinned FBW MfdFmsFpln.tsx, FplnRevisionsMenu.tsx, InsertNextWptFrom.tsx and
 * DestinationWindow.tsx on the spec 01 frame (body 138..950, border-box widgets).
 *
 * Data: the lateral plans come from a380x_fms_fpln.h (A380XFlightPlans), reached through
 * ctx->fms->fpln.plans unless the integrator/test installs an environment with
 * a380x_mfd_fpln_set_env(). FMS data (TA/TL, phase, UTC, destination predictions) comes from
 * a380x_fms.h when ctx->fms is set; without an FMS the pages fall back to the plan's database
 * values and show dashes.
 */
#ifndef A380X_MFD_PAGE_FPLN_H
#define A380X_MFD_PAGE_FPLN_H

#include "a380x_mfd.h"
#include "a380x_fms_fpln.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- F-PLN page state ------------------------------------------------------------------------ */

typedef struct {
    int from;                  /* list index shown in slot 0 (displayFplnFromLineIndex) */
    int efob_mode;             /* displayEfobAndWind */
    int pending_extra;         /* 1 = "top", 2 = "dest" (applied once the line list is known) */
    /* lateral revision menu */
    int rev_open;
    int rev_line;              /* list index of the revised line (ident drawn selected) */
    int rev_lplan;             /* lateral plan index the menu was opened on */
    int rev_altn, rev_leg, rev_type;
    uint32_t rev_version;
    /* windows: 0 none, 1 INSERT NEXT WPT, 2 NEW DEST */
    int window;
    int dup_wait;              /* INSERT NEXT WPT entry waiting for DUPLICATE NAMES */
    /* change detection (rebuild / re-clamp) */
    uint32_t last_version;
    int last_active, last_lplan;
} MfdFplnState;
MFD_PAGE_STATE_CHECK(MfdFplnState);

/* Register fms/<cat>/f-pln (cat = active, sec1..3). */
void a380x_mfd_page_fpln_register(void);

/* ---- Environment (integrator / tests) --------------------------------------------------------- */

typedef struct {
    A380XFlightPlans *plans;          /* NULL = ctx->fms->fpln.plans */
    const A380XFplnPredFeed *feed;    /* VNAV prediction feed; NULL = phase 1 (dashes) */
    int aircraft_valid;               /* 1 = use `aircraft` instead of the FMS inputs */
    A380XFplnAircraft aircraft;
} A380XMfdFplnEnv;

/* Copies *env (NULL resets to the defaults). */
void a380x_mfd_fpln_set_env(const A380XMfdFplnEnv *env);

/* ---- Helpers shared by the F-PLN family (a380x_mfd_page_fpln_rev.c) -------------------------- */

/* Revised-waypoint context (FMC revisedLegIndex / revisedLegPlanIndex / revisedLegIsAltn), one per
 * side. `plan` is the URI plan (0 = active category, 3..5 = SEC1..3); the lateral plan it refers to
 * is a380x_mfd_fpln_lateral_plan(ctx) at use time (TMPY when it exists). */
typedef struct {
    int valid;
    int plan;
    int altn;
    int leg;
} A380XMfdFplnRevised;

void a380x_mfd_fpln_set_revised(MfdSide side, int plan, int altn, int leg);
void a380x_mfd_fpln_reset_revised(MfdSide side);
A380XMfdFplnRevised a380x_mfd_fpln_revised(MfdSide side);

A380XFlightPlans *a380x_mfd_fpln_store(const MfdPageContext *ctx);
/* Lateral plan shown by the page: category active -> TMPY if it exists else ACTIVE; secN -> 2+N if
 * it exists; -1 when there is none. */
int  a380x_mfd_fpln_lateral_plan(const MfdPageContext *ctx);
/* Plan index to pass to the a380x_fpln_* operations for the page's category (0 or 3..5). */
int  a380x_mfd_fpln_edit_plan(const MfdPageContext *ctx);
void a380x_mfd_fpln_aircraft(const MfdPageContext *ctx, A380XFplnAircraft *out);
int  a380x_mfd_fpln_phase(const MfdPageContext *ctx);       /* FMGC phase, 0 without FMS */
double a380x_mfd_fpln_utc(const MfdPageContext *ctx);       /* UTC seconds of day, NAN unknown */
/* Transition altitude (ft) and level (ft = FL x 100) of a lateral plan: FMS values, else the plan's
 * database values; NAN when unknown. */
void a380x_mfd_fpln_trans(const MfdPageContext *ctx, int lplan, double *ta_ft, double *tl_ft);
/* Result of an a380x_fpln_* operation: on success, notifies the FMS of a new TMPY
 * (a380x_fms_tmpy_begin); on failure, queues the FMS message (NOT IN DATABASE, NOT ALLOWED,
 * NO INTERSECTION FOUND). Returns 1 on success. */
int  a380x_mfd_fpln_result(MfdPageContext *ctx, int rc);
/* TMPY INSERT / ERASE through the FMS (data stores + lateral hook), or directly on the plan store
 * when the FMS has no plan hooks. Return 1 on success. */
int  a380x_mfd_fpln_tmpy_insert(MfdPageContext *ctx);
int  a380x_mfd_fpln_tmpy_erase(MfdPageContext *ctx);
/* getOrCreateWaypoint (spec 05 section 5): place, runway (ICAO+RWY), lat/lon, PBD, PBX. Returns the
 * number of candidates written (> 1 -> DUPLICATE NAMES); 0 after queueing FORMAT ERROR / NOT IN
 * DATABASE / NOT YET IMPLEMENTED. Candidates are sorted nearest first from the aircraft. */
int  a380x_mfd_fpln_resolve_place(MfdPageContext *ctx, const char *text, A380XNavFix *out, int max);
/* "51°28.6N/000°27.1W" */
void a380x_mfd_fpln_format_latlon(double lat, double lon, char *buf, size_t n);
/* Button with a centred label column (one or two lines) and a "*" at the right (FBW label
 * `<span>L1<br/>L2</span><span>*</span>`); r.w / r.h <= 0 = auto. Returns 1 when clicked. */
int  a380x_mfd_fpln_star_button(MfdUi *ui, MfdId id, MfdRect r, const char *l1, const char *l2, MfdColor c,
                                int disabled);
/* Plan colour: SEC white, TMPY yellow, ACTIVE green. */
MfdColor a380x_mfd_fpln_plan_colour(int lplan);

#ifdef __cplusplus
}
#endif

#endif
