/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD top level (docs/mfd-spec/12-architecture.md section 6, docs/mfd-spec/01-framework-widgets.md
 * sections 1, 5-10): two sides sharing one FMS message queue and one opaque FMS, URI model and
 * navigation stack, page registry, header / title bar / footer, overlays (MSG LIST, FMS PAGE NOT
 * AVAIL, NOT FOUND), KCCU page keys, CLR INFO and DU on/off gating.
 *
 * ------------------------------------------------------------------------------------------
 * WRITING A PAGE (for the page work packages)
 * ------------------------------------------------------------------------------------------
 * 1. Declare the page-local state in your own header (a380x_mfd_page_<x>.h) and assert that it
 *    fits:      typedef struct { int tab; ... } MfdPerfState;
 *               MFD_PAGE_STATE_CHECK(MfdPerfState);
 * 2. Write the page function. It runs once per frame per side while the page is displayed and
 *    draws the page BODY (y 138..950). The frame draws the header, the title bar and the footer
 *    after it; overlays and header menus are drawn on top.
 *
 *    static void perf_page(MfdPageContext *ctx)
 *    {
 *        MfdPerfState *st = MFD_PAGE_STATE(ctx, MfdPerfState);   // zeroed on entry
 *        MfdUi *ui = ctx->ui;
 *        if (ctx->entered) st->tab = strcmp(ctx->uri->extra, "appr") == 0 ? 5 : 0;
 *        mfd_label(ui, MFD_TXT_LABEL, MFD_COL_COUNT, MFD_ANCHOR_LEFT, 20, 200, "V1");
 *        if (mfd_button(ui, MFD_ID(MFD_PAGE_ID_PERF, 1), mfd_rect(20, 900, 0, 0), "RETURN", 0))
 *            mfd_navigate_back(ctx);
 *    }
 *
 * 3. Register it (the integrator-owned a380x_mfd_pages.c calls every page module's register
 *    function once at start-up):
 *
 *    void a380x_mfd_page_perf_register(void)
 *    {
 *        mfd_register_page("fms/ * /perf", MFD_PAGE_ID_PERF, "PERF", perf_page,       (no spaces)
 *                          MFD_PAGE_FMS | MFD_PAGE_TITLE_CATEGORY | MFD_PAGE_TMPY);
 *    }
 *
 * Widget ids: MFD_ID(page_id, n). Page ids are the MfdPageId values below (one per page module
 * family; ids 1..0x7fff). Page functions must not keep pointers to ctx across frames.
 * ------------------------------------------------------------------------------------------
 */
#ifndef A380X_MFD_H
#define A380X_MFD_H

#include "a380x_mfd_types.h"
#include "a380x_mfd_draw.h"
#include "a380x_mfd_ui.h"
#include "a380x_mfd_formats.h"
#include "a380x_fms_messages.h"

#ifdef __cplusplus
extern "C" {
#endif

struct A380XFms; /* FMS core (a380x_fms.h, WP-C). Opaque here; pages that need it include a380x_fms.h. */

/* ---- Page ids (widget-id scopes). Append, never renumber. ------------------------------------ */

typedef enum {
    MFD_PAGE_ID_NONE = 0,
    MFD_PAGE_ID_NOT_FOUND = 1,
    MFD_PAGE_ID_STUB = 2,
    MFD_PAGE_ID_DATA_STATUS = 10,
    MFD_PAGE_ID_DATA_AIRPORT = 11,
    MFD_PAGE_ID_INIT = 20,
    MFD_PAGE_ID_FUEL_LOAD = 21,
    MFD_PAGE_ID_SEC_INDEX = 22,
    MFD_PAGE_ID_PERF = 30,
    MFD_PAGE_ID_FPLN = 40,
    MFD_PAGE_ID_FPLN_AIRWAYS = 41,
    MFD_PAGE_ID_FPLN_DEPARTURE = 42,
    MFD_PAGE_ID_FPLN_ARRIVAL = 43,
    MFD_PAGE_ID_FPLN_DIRECT_TO = 44,
    MFD_PAGE_ID_FPLN_VERT_REV = 45,
    MFD_PAGE_ID_FPLN_HOLD = 46,
    MFD_PAGE_ID_FPLN_FIX_INFO = 47,
    MFD_PAGE_ID_FPLN_DUPLICATE = 48,
    MFD_PAGE_ID_POSITION_MONITOR = 50,
    MFD_PAGE_ID_POSITION_IRS = 51,
    MFD_PAGE_ID_POSITION_NAVAIDS = 52,
    MFD_PAGE_ID_SURV_CONTROLS = 60,
    MFD_PAGE_ID_SURV_STATUS = 61,
    MFD_PAGE_ID_ATCCOM_CONNECT = 70,
    MFD_PAGE_ID_ATCCOM_MSG_RECORD = 71,
    MFD_PAGE_ID_ATCCOM_DATIS = 72,
    MFD_PAGE_ID_FCUBKUP_AFS = 80,
    MFD_PAGE_ID_FCUBKUP_EFIS = 81,
    MFD_PAGE_ID_TEST = 0x7f00
} MfdPageId;

/* ---- URI model (spec 01 section 10.1) ------------------------------------------------------- */

#define MFD_URI_LEN 64
#define MFD_NAV_STACK_CAP 32

typedef struct {
    char uri[MFD_URI_LEN];      /* full URI, e.g. "fms/data/status/acft-status" */
    char sys[16];               /* "fms" | "atccom" | "surv" | "fcubkup" */
    char category[24];          /* "active", "sec1", "data", "position", "sec", "controls", ... */
    char page[32];              /* "" when absent */
    char extra[MFD_URI_LEN];    /* parts 3.. joined with '/', "" when absent */
} MfdUri;

/* Split a URI into sys / category / page / extra. Returns 0 if it is empty or too long. */
int mfd_uri_parse(const char *s, MfdUri *out);

/* ---- Aircraft data hooks (no XPLM in pages) ------------------------------------------------- */

/* Per-frame inputs filled by the adapter (a380x_mfd_adapter.inc) or a test. */
typedef struct {
    double now;                     /* seconds, monotonic */
    unsigned char du_on[MFD_SIDE_COUNT];      /* display unit in state On (spec 01 section 1.3) */
    unsigned char fms_failed[MFD_SIDE_COUNT]; /* FMS PAGE NOT AVAIL condition (spec 03 section 10) */
    unsigned char kbd_on[MFD_SIDE_COUNT];     /* KCCU KBD switch ON = KCCU mode (no PC capture) */
    int fms_knob;                   /* FMS switching knob: 0 = FO, 1 = NORM, 2 = CAPT */
    unsigned char fmc_a_healthy, fmc_b_healthy;
    char atc_callsign[12];          /* "" = none -> "----------" */
    unsigned char engine_out;       /* EO tag */
    unsigned char tmpy_exists;      /* active temporary flight plan exists (TMPY tag) */
    unsigned char fuel_penalty_active; /* PENALTY tag */
    unsigned char metric_units;     /* 1 = kg / m (a380x/xp12/config/metric_units) */
    int flight_phase;               /* FMGC phase 0..7 */
} A380XMfdInputs;

/* DATA/STATUS snapshot (spec 03 section 5). Strings are complete display texts. */
typedef struct {
    char airframe[16];              /* "A380-800" */
    char engine[16];                /* "TRENT 972" */
    float idle_factor, perf_factor; /* AMI idle/perf factors (0.0) */
    char navdb_serial[16];          /* "XP26090001" ("" = unknown) */
    char navdb_active[16];          /* "03SEP-01OCT" */
    char navdb_second[16];          /* FBW: same as active */
    int stored_waypoints, stored_routes, stored_navaids, stored_runways;
    unsigned char fuel_penalty_has; /* FUEL PENALTY value present */
    unsigned char fuel_penalty_pilot; /* entered by pilot (> 0) -> 27 px, else 22 px */
    double fuel_penalty_pct;
} A380XMfdStatusData;

#define A380X_MFD_MAX_RUNWAYS 48
typedef struct {
    char ident[6];                  /* designation "27L" */
    double lat, lon;                /* threshold */
    float elevation_ft;             /* threshold elevation (NAN unknown) */
    float length_m;                 /* NAN unknown */
    float course_deg;               /* true course, NAN unknown */
    char ls_ident[6];               /* "" = no LS */
} A380XMfdRunway;

typedef struct {
    char icao[6];
    char name[48];
    double lat, lon;
    int runway_count;
    A380XMfdRunway rwy[A380X_MFD_MAX_RUNWAYS];
} A380XMfdAirport;

/* Callbacks implemented by the adapter (or a test). Any pointer may be NULL. */
typedef struct {
    void *ctx;
    /* DATA/STATUS */
    void (*read_status)(void *ctx, A380XMfdStatusData *out);
    /* Returns 1 on success, 0 with *err filled (FMS range checks). has_value = 0 clears. */
    int  (*set_fuel_penalty)(void *ctx, int has_value, double pct, MfdFmsError *err);
    void (*swap_nav_database)(void *ctx);  /* SWAP * CONFIRM = FMS reset (spec 03 section 5.3) */
    void (*delete_all_stored)(void *ctx);  /* DELETE ALL * */
    /* DATA/AIRPORT: 1 found, 0 not in database, -1 still loading (call again next frame). */
    int  (*airport_lookup)(void *ctx, const char *icao, A380XMfdAirport *out);
    /* Generic aircraft-systems channel for SURV / FCU BKUP / EFIS CP pages (spec 12 section 7).
     * cmd / key enumerations are defined by the page work package that uses them. */
    void   (*command)(void *ctx, MfdSide side, unsigned int cmd, double value);
    double (*read_value)(void *ctx, MfdSide side, unsigned int key);
} A380XMfdSystemsHost;

/* ---- Pages ---------------------------------------------------------------------------------- */

#define MFD_PAGE_STATE_SIZE 16384
#define MFD_MAX_PAGES 64

/* Registration flags */
#define MFD_PAGE_FMS 0x0001u            /* FMS-subsystem title bar (2 px gaps; EO / TMPY / PENALTY) */
#define MFD_PAGE_TITLE_CATEGORY 0x0002u /* title = UPPER(category) + "/" + title (FmsPage) */
#define MFD_PAGE_TMPY 0x0004u           /* TMPY tag when category "active" and a TMPY plan exists */
#define MFD_PAGE_PENALTY 0x0008u        /* PENALTY tag when the fuel penalty is active */
#define MFD_PAGE_NO_FOOTER 0x0010u      /* page draws its own footer (or none) */
#define MFD_PAGE_ATCCOM_FOOTER 0x0020u  /* ATCCOM footer (CLEAR INFO over the ATC queue) */
#define MFD_PAGE_NO_TITLE 0x0040u       /* page draws its own title bar */

typedef struct A380XMfd A380XMfd;
typedef struct A380XMfdSide A380XMfdSide;
typedef struct MfdPageContext MfdPageContext;

/* A page draws its body for one side and one frame. */
typedef void (*MfdPageFn)(MfdPageContext *ctx);

struct MfdPageContext {
    A380XMfd *mfd;
    A380XMfdSide *side_state;
    MfdSide side;
    MfdUi *ui;                      /* widgets: a380x_mfd_ui.h */
    MfdDrawList *dl;                /* == ui->dl, for raw primitives */
    struct A380XFms *fms;           /* shared FMS core (may be NULL in tests) */
    MfdMsgQueue *msgs;              /* shared FMS message queue */
    const A380XMfdSystemsHost *host;
    const A380XMfdInputs *in;
    const MfdUri *uri;              /* current URI (category selects the plan) */
    int plan;                       /* FBW FlightPlanIndex: 0 ACTIVE, 3..5 SEC1..3, -1 not a plan page */
    int entered;                    /* 1 on the first frame after (re)navigation */
    double now, dt;                 /* seconds */
    double page_time;               /* seconds since the page was entered */
    void *state;                    /* MFD_PAGE_STATE_SIZE bytes, 16-byte aligned, zeroed on entry */
    uint16_t page_id;
    uint32_t flags;                 /* registration flags */
    const char *title;              /* registered title */
    MfdRect body;                   /* 0, 138, 768, 812 */
};

/* Compile-time check that a page state struct fits (use at file scope in the page header). */
#define MFD_PAGE_STATE_CHECK(Type) \
    _Static_assert(sizeof(Type) <= MFD_PAGE_STATE_SIZE, #Type " exceeds MFD_PAGE_STATE_SIZE")
/* Typed access to the page state (also checks the size at compile time). */
#define MFD_PAGE_STATE(ctx, Type) \
    ((void)sizeof(char[(sizeof(Type) <= MFD_PAGE_STATE_SIZE) ? 1 : -1]), (Type *)(ctx)->state)

/* Register a page. pattern = "sys/category[/page]" where category "*" matches active, sec1,
 * sec2 and sec3 (e.g. "fms/ * /perf" without spaces). The lookup key of a URI is
 * sys/category/page; `extra` is never part of the key. Later registrations of the same
 * pattern replace earlier ones. Returns 1 on success, 0 if the table is full. */
int  mfd_register_page(const char *pattern, uint16_t page_id, const char *title, MfdPageFn fn, uint32_t flags);
void mfd_clear_pages(void);
int  mfd_page_count(void);

/* Navigation from a page: deferred to the end of the frame. "back" pops the stack. */
void mfd_navigate(MfdPageContext *ctx, const char *uri);
void mfd_navigate_back(MfdPageContext *ctx);
int  mfd_can_go_back(const MfdPageContext *ctx);
/* Navigate both sides (e.g. FMS reset / DONE phase, spec 01 section 1.1 [PORT]). Forced reload. */
void mfd_navigate_all(MfdPageContext *ctx, const char *uri);
/* Override the title-bar text for this frame (e.g. "MSG RECORD/ALL MSG"). */
void mfd_page_set_title(MfdPageContext *ctx, const char *title);

/* ---- MFD instance ---------------------------------------------------------------------------- */

struct A380XMfdSide {
    MfdSide side;
    MfdUi ui;
    MfdDrawList dl;
    MfdUri uri;
    char stack[MFD_NAV_STACK_CAP][MFD_URI_LEN];
    int depth;
    int page_index;                 /* registry index; -1 = NOT FOUND, -2 = stub */
    int stub_index;
    int entered;
    double entered_at;
    char pending[MFD_URI_LEN];
    int has_pending, pending_force;
    int msg_list_open;
    char title_override[64];
    _Alignas(16) unsigned char page_state[MFD_PAGE_STATE_SIZE];
};

struct A380XMfd {
    A380XMfdSide sides[MFD_SIDE_COUNT];
    struct A380XFms *fms;
    MfdMsgQueue *msgs;              /* shared FMS queue (owned by the FMS core) */
    MfdMsgQueue *atc_msgs;          /* ATCCOM queue for the D-ATIS footer (may be NULL) */
    A380XMfdSystemsHost host;
    A380XMfdInputs in;
    double now;
};

/* The struct is large (two display lists): allocate it statically or once at plugin start.
 * Both sides start on fms/data/status (spec 01 section 1.1). */
void a380x_mfd_init(A380XMfd *m, struct A380XFms *fms, MfdMsgQueue *msgs, const A380XMfdSystemsHost *host);
/* Run one frame for both sides: input resolution, pages, frame, overlays, cursor. */
void a380x_mfd_frame(A380XMfd *m, const A380XMfdInputs *in);
/* Display list of the last frame (empty while the DU is off). */
const MfdDrawList *a380x_mfd_display_list(const A380XMfd *m, MfdSide s);

/* Raw input (spec 01 section 12.5). Canvas px, y down; the adapter converts X-Plane's y-up
 * device coordinates with y_canvas = 1023 - y. Ignored while the DU is off. */
void mfd_pointer_move(A380XMfd *m, MfdSide s, float x, float y);
void mfd_pointer_button(A380XMfd *m, MfdSide s, int down);
void mfd_pointer_wheel(A380XMfd *m, MfdSide s, int clicks);
void mfd_pointer_leave(A380XMfd *m, MfdSide s);
void mfd_key(A380XMfd *m, MfdSide s, MfdKey k, char ch);

/* Outputs for the adapter. */
const char *a380x_mfd_active_uri(const A380XMfd *m, MfdSide s);  /* dataref .../active_page */
int  a380x_mfd_wants_keyboard(const A380XMfd *m, MfdSide s);      /* PC keyboard capture */
/* External navigation request (FMS reset / DONE, duplicate names): applied next frame. */
void a380x_mfd_navigate(A380XMfd *m, MfdSide s, const char *uri);
void a380x_mfd_open_msg_list(A380XMfd *m, MfdSide s);
/* System-selector label for item 0 (spec 01 section 1.2): "FMS 1", "FMS 2", "FMS 1-C", "FMS 2-C". */
const char *a380x_mfd_fms_label(const A380XMfdInputs *in, MfdSide s);

#ifdef __cplusplus
}
#endif

#endif
