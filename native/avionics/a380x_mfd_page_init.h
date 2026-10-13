/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD FMS INIT page: ACTIVE/INIT and SECn/INIT (fms/ * /init), translated from the pinned
 * FBW pages/FMS/MfdFmsInit.tsx / MfdFmsInit.scss (docs/mfd-spec/03-fms-init-fuel-data-sec-msg.md
 * section 3). Every field is bound to the FMS core (a380x_fms.h).
 *
 * This header also exports the small FMS-binding helpers shared by the INIT, FUEL&LOAD and
 * SEC INDEX page modules (a380x_mfd_page_fuel.c, a380x_mfd_page_sec.c) and the CPNY F-PLN
 * REQUEST button model (shared/CpnyFplnButtonUtils.tsx, spec 03 section 3.5).
 */
#ifndef A380X_MFD_PAGE_INIT_H
#define A380X_MFD_PAGE_INIT_H

#include "a380x_mfd.h"
#include "a380x_fms.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int last_plan; /* loaded plan of the previous frame (diagnostics) */
} MfdInitState;
MFD_PAGE_STATE_CHECK(MfdInitState);

/* Register fms/ * /init (ACTIVE and SEC1..3). */
void a380x_mfd_page_init_register(void);

/* ---- CPNY F-PLN REQUEST (spec 03 section 3.5) ------------------------------------------------
 * The port has no datalink / SimBrief uplink yet. With no hooks installed the button behaves as
 * FBW does while nothing is available: label "CPNY F-PLN\nREQUEST", enabled per the FBW rule
 * (ACTIVE: disabled after engine start, once the active plan has FROM/TO, or while a request for a
 * SEC is pending), and a click queues COM DATALINK NOT AVAIL. An integrator that implements the
 * SimBrief request installs the hooks below (all members optional). */
typedef struct {
    void *ctx;
    int  (*available)(void *ctx);         /* cpnyFplnAvailable (uplink received, waiting insertion) */
    int  (*in_progress)(void *ctx);       /* cpnyFplnUplinkInProgress */
    int  (*requested_for)(void *ctx);     /* cpnyFplnRequestedForPlan: plan index, or -1 */
    void (*request)(void *ctx, int plan); /* cpnyFplnRequest(plan): queues its own messages */
    void (*insert)(void *ctx, int plan);  /* INSERT*  (insertCpnyFpln) */
    void (*clear)(void *ctx);             /* CLEAR*   (uplinkDelete; available := 0) */
} A380XMfdCpnyFplnHooks;

void a380x_mfd_cpny_fpln_set_hooks(const A380XMfdCpnyFplnHooks *hooks); /* NULL = none */

/* Draws the CPNY F-PLN button (175 x 62, no arrow) for `plan` (FBW FlightPlanIndex, the [PORT]
 * fix of spec 03 section 3.5 uses the loaded plan for label, enable and menu) and runs it. */
void a380x_mfd_cpny_fpln_button(MfdPageContext *ctx, MfdId id, MfdRect r, int plan);

/* ---- Shared FMS field bindings ----------------------------------------------------------------
 * Numeric field bound to an FMS item: value, enteredByPilot (27/22 px), MANDATORY amber boxes,
 * DISABLED / INACTIVE / HIDDEN / CLEARABLE / TMPY from a380x_fms_field_flags(), the format from
 * a380x_fms_field_fmt(); a validated entry goes to a380x_fms_enter() / a380x_fms_clear() and an
 * FMS rejection is routed to the footer with mfd_ui_error(). Returns the widget result (the entry
 * has already been applied). width 0 = automatic. Hidden fields draw nothing. */
MfdFieldResult a380x_mfd_fms_field(MfdPageContext *ctx, MfdId id, int plan, A380XFmsField f,
                                   A380XFmsPage page, float x, float y, float width, MfdAnchor align);
/* Same for text items (FLT NBR, FROM, TO, ALTN, CPNY RTE, ALTN RTE). kind = MFD_FMT_TXT_*. */
MfdFieldResult a380x_mfd_fms_text_field(MfdPageContext *ctx, MfdId id, int plan, A380XFmsText t,
                                        A380XFmsPage page, MfdFmtKind kind, float x, float y, float width,
                                        MfdAnchor align);
/* COST INDEX MODE dropdown (LRC/ECON, EO-LRC/EO-ECON when engine out) bound to A380X_FMS_F_CI_MODE. */
void a380x_mfd_fms_ci_mode_dropdown(MfdPageContext *ctx, MfdId id, int plan, A380XFmsPage page, MfdRect r);

/* Colour of a static FMS value: AMBER alert, WHITE on SEC plans (.mfd-value.sec), else GREEN. */
MfdColor a380x_mfd_fms_value_colour(const A380XFms *fms, int plan, const A380XFmsValue *v);
/* Weight in the display unit with one decimal ("418.5"), or "---.-". */
void a380x_mfd_fms_weight_text(int has, double kg, int metric, char *buf, size_t n);
/* Duration / clock time in seconds as "HH:MM" (hours modulo 24 when `clock`), or "--:--". */
void a380x_mfd_fms_hhmm_text(int has, double seconds, int clock, char *buf, size_t n);
/* 1 = metric (T), 0 = imperial (KLB): the FMS configuration, else the MFD input. */
int  a380x_mfd_fms_metric(const MfdPageContext *ctx);

#ifdef __cplusplus
}
#endif

#endif
