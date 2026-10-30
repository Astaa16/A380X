/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native electronic checklist (ECL) of the FlyByWire A380X FWS + its EWD composition.
 *
 * Ports (pinned revision in a380x_fws_tables.json):
 *  - systems-host/CpiomC/FlightWarningSystem/FwsNormalChecklists.ts      (class, lines 44-531)
 *  - systems-host/CpiomC/FlightWarningSystem/FwsAbnormalSensed.ts        (class part, lines 92-366)
 *  - systems-host/CpiomC/FlightWarningSystem/FwsAbnormalNonSensed.ts     (class part, lines 15-161)
 *  - systems-host/CpiomC/FlightWarningSystem/FwsCore.ts                  ECP keys (2868-2908, 2939-2947,
 *    6210-6218), CLEAR AND RECALL (5511-5536), ECL orchestration (6155-6208), resets (2414-2452)
 *  - instruments/src/EWD/elements/WdNormalChecklists.tsx, WdAbnormalSensedProcedures.tsx,
 *    WdAbnormalNonSensed.tsx, WdAbstractChecklistComponent.tsx, WdCpiomFailedFallbackChecklistComponent.tsx
 *    and the visibility logic of EWD.tsx (lines 71-130).
 *
 * Ownership: the FWS core owns FwsEclCoreLists (the FwsCore Map/array/Set members the ECL classes read and
 * mutate) and passes it by pointer to every call; the ECL mutates it exactly where FBW does (item checks,
 * CLEAR, RECALL, ABN PROC activation, resets). The core must change the maps only through fws_ecl_map_*
 * (they keep MapSubject insertion order and the serial/revision the ECL uses to replay FBW's map
 * subscriptions). FwsEcl holds the ECL's own state; it is plain data (may be copied between calls).
 *
 * Per FWS cycle the core calls, in FBW order:
 *   fws_ecl_buttons()           where FwsCore samples L:A32NX_BTN_* (start of update)
 *   fws_ecl_clear_and_recall()  at "CLEAR AND RECALL" (FwsCore 5511), before its abnormal-procedure loop
 *   ... core abnormal/deferred loop (fws_ecl_map_set/delete, abnormal updated items) ...
 *   fws_ecl_update()            at normalChecklists/abnormalSensed/abnormalNonSensed.update() (FwsCore 6155)
 * and the EWD side calls fws_ecl_compose_wd() whenever it draws.
 * XPLM-free plain C11.
 */
#ifndef A380X_FWS_ECL_H
#define A380X_FWS_ECL_H

#include "a380x_fws_lines.h"

#define FWS_ECL_MAX_PROCS 32              /* bound of each core map (presented/cleared/deferred) */
#define FWS_ECL_MAX_KEYS 64               /* bound of each core key list */
#define FWS_ECL_CPIOM_FALLBACK_LINES 21   /* WdCpiomFailedFallbackChecklistComponent: "21 lines" */
#define FWS_ECL_RESET_ALL (-1000)         /* FwsNormalChecklists.reset(null) */

/* ---------------------------------------------------------------------------------------------- */
/* Core-owned lists (FwsCore members)                                                              */
/* ---------------------------------------------------------------------------------------------- */

/* One value of a JS Map<string, ChecklistState> plus the core data the ECL needs about that key. */
typedef struct {
    FwsChecklistState state;                 /* state.id is the key */
    unsigned char timestamps_defined;        /* ChecklistState.itemsTimeStamp !== undefined */
    /* presented/cleared abnormal: ewdAbnormal[id].failure (2 caution, 3 warning, ...) for the EWD sort */
    unsigned char failure;
    /* presented abnormal: FwsCore.abnormalUpdatedItems for this FWS cycle (cleared by the core each cycle,
     * FwsCore 5622). has_updated_items = .has(id), updated_items[i] = .get(id).includes(i). */
    unsigned char has_updated_items;
    unsigned char updated_items[FWS_MAX_ITEMS];
    /* active deferred: allEwdDeferredProcs[id].whichItemsActive (present + current value) and
     * whichItemsChecked().length, read when the pilot (de)activates the deferred procedure. */
    unsigned char has_which_items_active;
    unsigned char which_items_active[FWS_MAX_ITEMS];
    unsigned char which_items_checked_length;
    unsigned serial;                         /* set on insertion by fws_ecl_map_set */
    unsigned revision;                       /* bumped by every fws_ecl_map_set (MapSubject.setValue) */
} FwsEclProcEntry;

/* MapSubject<string, ChecklistState>: entries in insertion order. */
typedef struct {
    int count;
    unsigned next_serial;
    FwsEclProcEntry entries[FWS_ECL_MAX_PROCS];
} FwsEclProcMap;

/* string[] / SetSubject<number> of procedure ids, in order. */
typedef struct {
    int count;
    unsigned keys[FWS_ECL_MAX_KEYS];
} FwsEclKeyList;

typedef struct {
    FwsEclProcMap presented_abnormal;          /* presentedAbnormalProceduresList */
    FwsEclProcMap cleared_abnormal;            /* clearedAbnormalProceduresList */
    FwsEclProcMap active_deferred;             /* activeDeferredProceduresList */
    FwsEclKeyList presented_failures;          /* presentedFailures */
    FwsEclKeyList all_current_failures;        /* allCurrentFailures */
    FwsEclKeyList recall_failures;             /* recallFailures */
    FwsEclKeyList active_abnormal_non_sensed;  /* activeAbnormalNonSensedKeys (SetSubject) */
} FwsEclCoreLists;

void fws_ecl_lists_init(FwsEclCoreLists *lists);
FwsEclProcEntry *fws_ecl_map_get(FwsEclProcMap *map, unsigned id);
const FwsEclProcEntry *fws_ecl_map_find(const FwsEclProcMap *map, unsigned id);
/* setValue(state->id, state): replaces the state (other entry fields are kept, zero for a new key), bumps
 * the revision, appends new keys. Returns the entry (NULL when the map is full). */
FwsEclProcEntry *fws_ecl_map_set(FwsEclProcMap *map, const FwsChecklistState *state, int timestamps_defined);
int fws_ecl_map_delete(FwsEclProcMap *map, unsigned id);   /* 1 if deleted; keeps order */
void fws_ecl_map_clear(FwsEclProcMap *map);
int fws_ecl_keys_index(const FwsEclKeyList *list, unsigned key);  /* indexOf, -1 */
int fws_ecl_keys_push(FwsEclKeyList *list, unsigned key);        /* push; 0 when full */
void fws_ecl_keys_splice(FwsEclKeyList *list, int index);         /* splice(index, 1), JS negative index */
int fws_ecl_set_add(FwsEclKeyList *set, unsigned key);            /* SetSubject.add */
int fws_ecl_set_delete(FwsEclKeyList *set, unsigned key);         /* SetSubject.delete */

/* ---------------------------------------------------------------------------------------------- */
/* Inputs                                                                                          */
/* ---------------------------------------------------------------------------------------------- */

/* L:A32NX_BTN_* levels sampled by the FWS cycle (FwsCore 2868-2908). */
typedef struct {
    unsigned char clr, clr2;           /* A32NX_BTN_CLR / _CLR2 */
    unsigned char rcl;                 /* A32NX_BTN_RCL (ignored while ecp_failed, as FBW) */
    unsigned char cl;                  /* A32NX_BTN_CL */
    unsigned char check_lh, check_rh;  /* A32NX_BTN_CHECK_LH / _RH */
    unsigned char up, down;            /* A32NX_BTN_UP / _DOWN */
    unsigned char abn_proc;            /* A32NX_BTN_ABNPROC */
    unsigned char ecp_failed;          /* FwsCore.fwsEcpFailed */
} FwsEcpButtons;

typedef struct {
    double now;                        /* seconds; replaces Date.now() for timed items */
    int flight_phase;                  /* FwsCore.flightPhase */
    unsigned char aircraft_on_ground;  /* FwsCore.aircraftOnGround (recommendation capture) */
    /* FwsCore.ecamStatusNormal as currently held by the core: at clear_and_recall time that is the previous
     * cycle's value (FBW recomputes it at 6143, after CLEAR), at update time this cycle's. */
    unsigned char ecam_status_normal;
    double adr_pressure_altitude;      /* adrPressureAltitude ?? 0 */
    /* approachAutoDisplayQnhSetPulseNode.read() || approachAutoDisplaySlatsExtendedPulseNode.read() */
    unsigned char approach_auto_display_pulse;
    unsigned char takeoff_power;       /* eng1Or2TakeoffPower || eng3Or4TakeoffPower */
    /* FwsNormalChecklists.sensedItems[id].whichItemsChecked(), per normal checklist in ascending-id order
     * (fws_normal_procedures index): -1 = null (not sensed), 0 = false, 1 = true. */
    signed char normal_sensed[FWS_NORMAL_PROCEDURE_COUNT][FWS_MAX_ITEMS];
} FwsEclInput;

void fws_ecl_input_init(FwsEclInput *input);   /* zero, normal_sensed all -1 */

/* ---------------------------------------------------------------------------------------------- */
/* ECL state                                                                                       */
/* ---------------------------------------------------------------------------------------------- */

/* FwsEvents as last published (bus semantics: a value persists until published again). */
typedef struct {
    int fws_active_item;
    unsigned fws_active_procedure;     /* 0 = FBW's initial '0' */
    int fws_show_from_line;
    unsigned char fws_show_normal_checklists;
    unsigned fws_normal_checklists_id;
    unsigned char fws_show_abn_sensed;
    unsigned char fws_show_abn_non_sensed;
    unsigned fws_abn_non_sensed_id;
    unsigned char fws_abn_non_sensed_current_active;
    unsigned char fws_show_failure_pending;
} FwsEclPublished;

/* Returned by clear_and_recall/update: effects FBW applies outside the ECL. */
enum {
    FWS_ECL_EVENT_SD_STATUS_PAGE = 1u << 0   /* SimVar L:A32NX_ECAM_SD_CURRENT_PAGE_INDEX := SdPages.Status */
};

typedef struct {
    FwsProcLines gen;
    unsigned serial, revision;
    unsigned char alive;
    unsigned char failure;
} FwsEclAbnSlot;

typedef struct {
    /* ECP pulse nodes (NXLogicPulseNode, rising edge) */
    unsigned char pulse_clr, pulse_rcl, pulse_cl, pulse_check, pulse_up, pulse_down, pulse_abn_proc;
    unsigned char last_clr, last_rcl, last_cl, last_check, last_up, last_down, last_abn_proc;

    /* FwsNormalChecklists */
    unsigned char checklist_shown, show_checklist_requested;
    unsigned checklist_id;                         /* 0 = overview */
    int selected_line, show_from_line;
    FwsChecklistState normal_state[FWS_NORMAL_PROCEDURE_COUNT];  /* checklistState map, ascending ids */
    unsigned char overview_items_checked[FWS_NORMAL_PROCEDURE_COUNT];  /* checklistState.getValue(0) */
    unsigned char overview_items_to_show[FWS_NORMAL_PROCEDURE_COUNT];
    unsigned char has_deferred[FWS_DEFERRED_TYPE_COUNT], deferred_is_completed[FWS_DEFERRED_TYPE_COUNT];
    unsigned char active_deferred_valid;           /* activeDeferredProcedureId !== null */
    unsigned active_deferred_id;
    int num_visible_deferred;
    unsigned visible_deferred_keys[FWS_ECL_MAX_PROCS];
    int num_deferred_gens;
    FwsProcLines deferred_gens[FWS_ECL_MAX_PROCS];
    FwsProcLines normal_gen;
    int normal_active;                             /* activeProcedure: -2 null, -1 normal_gen, >= 0 deferred */
    unsigned char manual_checklist_reset;          /* FwsCore.manualCheckListReset (owned here) */
    unsigned char inputs_seen, takeoff_power_last;
    int flight_phase_last;
    unsigned deferred_map_signature;               /* last seen activeDeferredProceduresList content */

    /* FwsAbnormalSensed */
    unsigned char abnormal_shown, show_abnormal_sensed_requested;
    unsigned char active_procedure_valid;          /* activeProcedureId !== null */
    unsigned active_procedure_id;
    int abn_selected_item_index, abn_show_from_line;
    FwsEclAbnSlot abn[FWS_ECL_MAX_PROCS + 1];      /* this.procedures (+1: a deleted active generator) */
    int abn_active;                                /* activeProcedure slot, -1 undefined */

    /* FwsAbnormalNonSensed */
    unsigned char abn_proc_shown, show_abn_proc_requested;
    unsigned abn_non_sensed_id;                    /* 0 overview, 1-10 sub-menus, else procedure id */
    int abn_non_sensed_selected, abn_non_sensed_show_from_line;

    FwsEclPublished pub;

    /* bound for the duration of one API call */
    FwsEclCoreLists *lists;
    const FwsEclInput *input;
    unsigned events;
} FwsEcl;

/* Constructors of the three FBW classes (initial checklist states, initial publications). */
void fws_ecl_init(FwsEcl *ecl);
/* Sample the ECP buttons and write the pulse nodes. */
void fws_ecl_buttons(FwsEcl *ecl, const FwsEcpButtons *buttons);
/* FwsCore "CLEAR AND RECALL". Returns FWS_ECL_EVENT_* bits. */
unsigned fws_ecl_clear_and_recall(FwsEcl *ecl, FwsEclCoreLists *lists, const FwsEclInput *input);
/* normalChecklists.update(); abnormalSensed.update(); abnormalNonSensed.update(); orchestration+publish. */
unsigned fws_ecl_update(FwsEcl *ecl, FwsEclCoreLists *lists, const FwsEclInput *input);
/* FwsCore startupCompleted handler: ECL resets + clears of the core lists (FwsCore 2418-2426). */
void fws_ecl_startup_reset(FwsEcl *ecl, FwsEclCoreLists *lists);
/* FwsCore shutDownFor50MinutesCheckListReset handler (FwsCore 2439-2449). */
void fws_ecl_shutdown_reset(FwsEcl *ecl, FwsEclCoreLists *lists);
/* EWD sort of the presented abnormal procedures (FwsAbnormalSensed.compareAbnormalProceduresByPriority);
 * writes indices into lists->presented_abnormal.entries, returns the count. */
int fws_ecl_sorted_abnormal(const FwsEclCoreLists *lists, int order[FWS_ECL_MAX_PROCS]);

/* ---------------------------------------------------------------------------------------------- */
/* EWD composition                                                                                 */
/* ---------------------------------------------------------------------------------------------- */
typedef enum {
    FWS_ECL_VIEW_NONE,            /* memos / limitations */
    FWS_ECL_VIEW_NORMAL,          /* WdNormalChecklists */
    FWS_ECL_VIEW_ABN_SENSED,      /* WdAbnormalSensedProcedures (incl. FWS 1+2 FAULT fallback) */
    FWS_ECL_VIEW_ABN_NON_SENSED,  /* WdAbnormalNonSensedProcedures */
    FWS_ECL_VIEW_CPIOM_FALLBACK   /* WdCpiomFailedFallbackChecklistComponent */
} FwsEclView;

typedef struct {
    double now;                    /* EWD clock for timed items (same epoch as the FWS timestamps) */
    unsigned char fws_avail;       /* FwsEwdAvailabilityChecker.fwsAvail */
    unsigned char cpiom_a_failed, cpiom_b_failed, cpiom_c_failed, cpiom_d_failed;
    unsigned char cpiom_e_failed, cpiom_f_failed, cpiom_g_failed;   /* CpiomEwdAvailabilityChecker */
    unsigned char fcdc12_failed;   /* FCDC 1+2 discrete word 1 both invalid */
    unsigned char on_ground;       /* nose_gear_compressed_1 || nose_gear_compressed_2 */
    double groundspeed;            /* first valid IR ground speed */
} FwsEclEwdInput;

typedef struct {
    const char *text;              /* the headline (index 0) uses FWC escape codes */
    unsigned char style;           /* FWS_STYLE_HEADLINE (index 0), FWS_STYLE_CYAN / _WHITE / _AMBER */
    unsigned char visible;         /* .invisible = visibility:hidden (the 30 px slot is kept) */
} FwsEclFallbackLine;

typedef struct {
    FwsEclView view;
    unsigned char memos_limitations_visible;   /* EWD.memosLimitationVisible */
    unsigned char sts_area_visible;            /* EWD.stsAreaVisibility */
    FwsWdLine lines[FWS_WD_NUM_LINES];         /* WdAbstractChecklistComponent window */
    unsigned char line_selected[FWS_WD_NUM_LINES];
    int selected_line;                         /* first selected window line, -1 none */
    int show_from_line, total_lines;
    unsigned char overflow_up, overflow_down;
    FwsEclFallbackLine cpiom_fallback[FWS_ECL_CPIOM_FALLBACK_LINES];
} FwsEclWd;

void fws_ecl_compose_wd(const FwsEcl *ecl, const FwsEclCoreLists *lists, const FwsEclEwdInput *input,
                        FwsEclWd *out);

#endif
