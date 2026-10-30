/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native port of the FlyByWire A380X ProcedureLinesGenerator
 * (fbw-a380x/src/systems/instruments/src/MsfsAvionicsCommon/EcamMessages/ProcedureLinesGenerator.ts,
 *  pinned revision in a380x_fws_tables.json).
 *
 * One FwsProcLines is one `new ProcedureLinesGenerator(...)`: it owns a copy of the ChecklistState it was
 * constructed with (FBW keeps a reference that is only replaced by checkSelected() or by its owner), its own
 * selectedItemIndex and the optional callbacks of the FBW constructor. Behaviour, special indices, texts,
 * padding and line flags are FBW's; the only substitution is the clock: `now` (seconds, any epoch shared
 * with ChecklistState.items_timestamp) replaces Date.now() (milliseconds).
 *
 * Text is UTF-8 (NBSP = "\xc2\xa0"). Character counts (padding, substring(0, 40)) count characters like
 * JS .length does for the BMP-only FBW strings, never bytes.
 * XPLM-free plain C11.
 */
#ifndef A380X_FWS_LINES_H
#define A380X_FWS_LINES_H

#include <stddef.h>
#include "a380x_fws_types.h"
#include "a380x_fws_tables.h"

/* ProcedureLinesGenerator.ts lines 42-51. -1 is reserved for moveDown (start before the top of the list). */
#define FWS_SPECIAL_INDEX_ACTIVATE (-2)
#define FWS_SPECIAL_INDEX_DEFERRED_PROC_COMPLETE (-3)
#define FWS_SPECIAL_INDEX_DEFERRED_PROC_RECALL (-4)
#define FWS_SPECIAL_INDEX_NORMAL_CL_COMPLETE (-5)
#define FWS_SPECIAL_INDEX_NORMAL_RESET (-6)
#define FWS_SPECIAL_INDEX_CLEAR (-7)
#define FWS_SPECIAL_INDEX_DEACTIVATE (-8)
#define FWS_HIGHEST_SPECIAL_INDEX FWS_SPECIAL_INDEX_ACTIVATE
#define FWS_SPECIAL_INDEX_DEFERRED_PAGE_CLEAR (-99)

/* Upper bound of toLineData(): title, recommendation, ACTIVATE, every item plus its CONFIRM line,
 * two special lines and the closing empty line. */
#define FWS_LINES_MAX (2 * FWS_MAX_ITEMS + 8)

/* checkSelected() return mask: which FBW constructor callback was invoked. */
enum {
    FWS_LINES_EVENT_ITEM_CHECKED = 1u << 0,      /* itemCheckedCallback */
    FWS_LINES_EVENT_CLEARED_OR_RESET = 1u << 1,  /* procedureClearedOrResetCallback (CLEAR, RESET, ACTIVATE) */
    FWS_LINES_EVENT_COMPLETED = 1u << 2          /* procedureCompletedCallback (C/L COMPLETE, DEFERRED PROC ...) */
};

typedef struct FwsProcLines FwsProcLines;

/* FBW callback signature `(newState: ChecklistState) => void`. new_state is checkSelected's clState: the
 * callback may modify it (FwsNormalChecklists rewrites itemsActive on ACTIVATE) and the generator adopts it
 * afterwards, exactly like the shared JS object. */
typedef void (*FwsProcLinesCallback)(void *ctx, FwsProcLines *gen, FwsChecklistState *new_state);

/* Called whenever selected_item_index actually changes (Subject semantics); used for FBW's
 * `selectedItemIndex.pipe(...)`. */
typedef void (*FwsProcLinesSelectedHook)(void *ctx, FwsProcLines *gen);

struct FwsProcLines {
    unsigned procedure_id;                   /* procedureId (numeric value of the FBW key) */
    FwsProcedureType type;
    const FwsProcedureDef *procedure;        /* NULL when the id is unknown (FBW would throw) */
    FwsChecklistState checklist_state;       /* this.checklistState */
    unsigned char timestamps_defined;        /* checklistState.itemsTimeStamp !== undefined */
    unsigned char procedure_is_active;       /* procedureIsActive.get(); owners refresh it */
    unsigned char is_last_procedure;
    /* 1 when the item-checked callback synchronously replaces this.checklistState with clState
     * (FwsAbnormalSensed's map subscription does that before checkSelected() calls moveDown()). */
    unsigned char item_checked_adopts_state;
    signed char recommendation;              /* FwsRecommendation captured at construction */
    int selected_item_index;                 /* selectedItemIndex, initially 0 */
    double now;                              /* replaces Date.now(); seconds */
    FwsProcLinesCallback item_checked_callback;
    FwsProcLinesCallback procedure_cleared_or_reset_callback;
    FwsProcLinesCallback procedure_completed_callback;
    FwsProcLinesSelectedHook selected_changed;
    void *ctx;
};

/* EcamNormalProcedures[parseInt(id)] / EcamAbnormalProcedures[id] / EcamDeferredProcedures[id]. */
const FwsProcedureDef *fws_lines_find_procedure(FwsProcedureType type, unsigned id);

/* Constructor. state may be NULL (all arrays false, procedure not activated). Callbacks/hook start NULL. */
void fws_lines_init(FwsProcLines *gen, unsigned procedure_id, FwsProcedureType type,
                    const FwsChecklistState *state, int timestamps_defined,
                    FwsRecommendation recommendation, int is_last_procedure);

/* static conditionalActiveItems(proc, itemsChecked, itemsActive, itemsTimeStamp?, itemsToShow?).
 * items_timestamp/timestamp_valid NULL = itemsTimeStamp undefined; items_to_show NULL = undefined.
 * Mutates items_checked (timed conditions) and items_active like FBW. */
void fws_lines_conditional_active_items(const FwsProcedureDef *proc, unsigned char *items_checked,
                                        unsigned char *items_active, const double *items_timestamp,
                                        const unsigned char *timestamp_valid, const unsigned char *items_to_show,
                                        double now);
int fws_lines_has_timed_item_elapsed(double timestamp, const FwsChecklistItem *item, double now);

int fws_lines_num_lines_until_selected(const FwsProcLines *gen);   /* -1 when not shown */
int fws_lines_first_line_is_selected(const FwsProcLines *gen);
int fws_lines_last_line_is_selected(const FwsProcLines *gen);
void fws_lines_move_up(FwsProcLines *gen);
void fws_lines_move_down(FwsProcLines *gen, int skip_completed_sensed);  /* FBW default: 1 */
unsigned fws_lines_check_selected(FwsProcLines *gen);                    /* FWS_LINES_EVENT_* mask */
void fws_lines_select_first(FwsProcLines *gen);

/* toLineData(); returns the number of lines written (<= max_lines, FWS_LINES_MAX always suffices). */
int fws_lines_to_line_data(const FwsProcLines *gen, FwsWdLine *out, int max_lines);

/* static getChecklistActionText / getChecklistConditionText (no truncation; out is NUL-terminated and cut
 * on a character boundary if size is too small). */
void fws_lines_checklist_action_text(const FwsChecklistItem *item, int item_index, int item_complete,
                                     const FwsChecklistState *state, int timestamps_defined, double now,
                                     char *out, size_t size);
void fws_lines_checklist_condition_text(const FwsChecklistItem *item, int item_index, int item_complete,
                                        const FwsChecklistState *state, int timestamps_defined, double now,
                                        char *out, size_t size);

/* Item predicates of index.ts. */
int fws_item_is_action(const FwsChecklistItem *item);           /* isChecklistAction */
int fws_item_is_timed_action(const FwsChecklistItem *item);     /* isTimedCheckListAction */
int fws_item_is_condition(const FwsChecklistItem *item);        /* isChecklistCondition */
int fws_item_is_timed_condition(const FwsChecklistItem *item);  /* isChecklistTimedCondition */
int fws_item_is_timed(const FwsChecklistItem *item);            /* isTimedItem */
int fws_item_is_headline(const FwsChecklistItem *item);         /* isChecklistHeadline (FBW comma bug kept) */

/* JS .length of a UTF-8 string (BMP only) and a copy of its first max_chars characters. */
size_t fws_js_length(const char *s);
void fws_js_prefix(char *out, size_t size, const char *s, size_t max_chars);

#endif
