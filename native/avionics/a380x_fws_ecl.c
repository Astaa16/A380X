/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native ECL controller + EWD composition, see a380x_fws_ecl.h for the FBW sources and the call order.
 * FBW paths are relative to fbw-a380x/src/systems/; line numbers refer to the pinned revision.
 */
#include "a380x_fws_ecl.h"

#include <string.h>

#define NBSP "\xc2\xa0"
#define DEPARTURE_CHANGE_ID 1000006u /* FwsNormalChecklists.ts:230 */

static const FwsEclInput ecl_no_input; /* used while resetting (no input bound) */

/* Table lookups go through a380x_fws_lines.c so this translation unit does not get its own copy of the
 * abnormal/deferred tables (a380x_fws_tables.h is header-only). */
static const FwsProcedureDef *abn_proc(unsigned id) { return fws_lines_find_procedure(FWS_PROC_ABNORMAL, id); }
static const FwsProcedureDef *def_proc(unsigned id) { return fws_lines_find_procedure(FWS_PROC_DEFERRED, id); }

/* ============================================================================================== */
/* Core lists                                                                                      */
/* ============================================================================================== */
void fws_ecl_lists_init(FwsEclCoreLists *lists) { memset(lists, 0, sizeof(*lists)); }

FwsEclProcEntry *fws_ecl_map_get(FwsEclProcMap *map, unsigned id)
{
    int i;
    for (i = 0; i < map->count; ++i)
        if (map->entries[i].state.id == id) return &map->entries[i];
    return NULL;
}

const FwsEclProcEntry *fws_ecl_map_find(const FwsEclProcMap *map, unsigned id)
{
    int i;
    for (i = 0; i < map->count; ++i)
        if (map->entries[i].state.id == id) return &map->entries[i];
    return NULL;
}

FwsEclProcEntry *fws_ecl_map_set(FwsEclProcMap *map, const FwsChecklistState *state, int timestamps_defined)
{
    FwsEclProcEntry *entry = fws_ecl_map_get(map, state->id);
    if (!entry) {
        if (map->count >= FWS_ECL_MAX_PROCS) return NULL;
        entry = &map->entries[map->count++];
        memset(entry, 0, sizeof(*entry));
        entry->serial = ++map->next_serial;
    }
    if (&entry->state != state) entry->state = *state;
    entry->timestamps_defined = (unsigned char)(timestamps_defined != 0);
    ++entry->revision;
    return entry;
}

int fws_ecl_map_delete(FwsEclProcMap *map, unsigned id)
{
    int i;
    for (i = 0; i < map->count; ++i) {
        if (map->entries[i].state.id == id) {
            memmove(&map->entries[i], &map->entries[i + 1], (size_t)(map->count - i - 1) * sizeof(map->entries[0]));
            --map->count;
            return 1;
        }
    }
    return 0;
}

void fws_ecl_map_clear(FwsEclProcMap *map) { map->count = 0; }

int fws_ecl_keys_index(const FwsEclKeyList *list, unsigned key)
{
    int i;
    for (i = 0; i < list->count; ++i)
        if (list->keys[i] == key) return i;
    return -1;
}

int fws_ecl_keys_push(FwsEclKeyList *list, unsigned key)
{
    if (list->count >= FWS_ECL_MAX_KEYS) return 0;
    list->keys[list->count++] = key;
    return 1;
}

void fws_ecl_keys_splice(FwsEclKeyList *list, int index)
{
    if (index < 0) index = list->count + index < 0 ? 0 : list->count + index; /* JS: splice(-1, 1) = last */
    if (index >= list->count) return;
    memmove(&list->keys[index], &list->keys[index + 1], (size_t)(list->count - index - 1) * sizeof(unsigned));
    --list->count;
}

int fws_ecl_set_add(FwsEclKeyList *set, unsigned key)
{
    return fws_ecl_keys_index(set, key) >= 0 ? 1 : fws_ecl_keys_push(set, key);
}

int fws_ecl_set_delete(FwsEclKeyList *set, unsigned key)
{
    int i = fws_ecl_keys_index(set, key);
    if (i < 0) return 0;
    fws_ecl_keys_splice(set, i);
    return 1;
}

void fws_ecl_input_init(FwsEclInput *input)
{
    memset(input, 0, sizeof(*input));
    memset(input->normal_sensed, -1, sizeof(input->normal_sensed));
    input->ecam_status_normal = 1;
}

static unsigned map_signature(const FwsEclProcMap *map)
{
    unsigned h = 2166136261u ^ (unsigned)map->count;
    int i;
    for (i = 0; i < map->count; ++i) {
        h = (h ^ map->entries[i].serial) * 16777619u;
        h = (h ^ map->entries[i].revision) * 16777619u;
    }
    return h;
}

/* FwsAbnormalSensed.compareAbnormalProceduresByPriority (FwsAbnormalSensed.ts:114-128) */
static int abnormal_priority(const FwsEclProcEntry *entry)
{
    const FwsProcedureDef *proc = abn_proc(entry->state.id);
    unsigned id = entry->state.id;
    int secondary = proc ? strncmp(proc->key, "9998", 4) == 0 : id / 100000u == 9998u; /* substring(0, 4) */
    if (secondary) return 0;
    if (proc && proc->sensed == 0) return 4; /* non-sensed procedures at the top */
    return entry->failure;
}

int fws_ecl_sorted_abnormal(const FwsEclCoreLists *lists, int order[FWS_ECL_MAX_PROCS])
{
    const FwsEclProcMap *map = &lists->presented_abnormal;
    int n = map->count, i, j;
    for (i = 0; i < n; ++i) order[i] = i;
    for (i = 1; i < n; ++i) { /* stable insertion sort, descending priority (Array.prototype.sort is stable) */
        int v = order[i], pv = abnormal_priority(&map->entries[v]);
        for (j = i - 1; j >= 0 && abnormal_priority(&map->entries[order[j]]) < pv; --j) order[j + 1] = order[j];
        order[j + 1] = v;
    }
    return n;
}

/* ============================================================================================== */
/* Binding                                                                                         */
/* ============================================================================================== */
static void ecl_bind(FwsEcl *e, FwsEclCoreLists *lists, const FwsEclInput *input)
{
    int i;
    double now;
    e->lists = lists;
    e->input = input ? input : &ecl_no_input;
    e->events = 0;
    now = e->input->now;
    e->normal_gen.ctx = e;
    e->normal_gen.now = now;
    for (i = 0; i < FWS_ECL_MAX_PROCS; ++i) {
        e->deferred_gens[i].ctx = e;
        e->deferred_gens[i].now = now;
    }
    for (i = 0; i < FWS_ECL_MAX_PROCS + 1; ++i) {
        e->abn[i].gen.ctx = e;
        e->abn[i].gen.now = now;
    }
}

/* ============================================================================================== */
/* FwsAbnormalSensed (FwsAbnormalSensed.ts:92-335)                                                 */
/* ============================================================================================== */
static void abn_sync(FwsEcl *e);
static void normal_open_if_deferred_applicable(FwsEcl *e);

static int abn_slot_index(const FwsEcl *e, const FwsProcLines *g)
{
    int i;
    for (i = 0; i < FWS_ECL_MAX_PROCS + 1; ++i)
        if (&e->abn[i].gen == g) return i;
    return -1;
}

static FwsProcLines *abn_active_gen(FwsEcl *e)
{
    return e->abn_active >= 0 ? &e->abn[e->abn_active].gen : NULL;
}

/* procedureIsActive = this.activeProcedureId.map((id) => value.id === id) */
static void abn_refresh_active(FwsEcl *e)
{
    int i;
    for (i = 0; i < FWS_ECL_MAX_PROCS + 1; ++i)
        e->abn[i].gen.procedure_is_active =
            (unsigned char)(e->active_procedure_valid && e->abn[i].gen.procedure_id == e->active_procedure_id);
}

static void abn_scroll_to_selected_line(FwsEcl *e) /* :263-269 */
{
    int n;
    FwsProcLines *g = abn_active_gen(e);
    if (!g) return;
    abn_refresh_active(e);
    n = fws_lines_num_lines_until_selected(g) - FWS_WD_NUM_LINES + 2;
    e->abn_show_from_line = n > 0 ? n : 0;
}

static void abn_set_selected(FwsEcl *e, int value) /* selectedItemIndex.sub(scrollToSelectedLine) */
{
    if (e->abn_selected_item_index == value) return;
    e->abn_selected_item_index = value;
    abn_scroll_to_selected_line(e);
}

static void abn_selected_hook(void *ctx, FwsProcLines *g) { abn_set_selected((FwsEcl *)ctx, g->selected_item_index); }

static void abn_pipe(FwsEcl *e, FwsProcLines *g) /* activeProcedure.selectedItemIndex.pipe(selectedItemIndex) */
{
    g->selected_changed = abn_selected_hook;
    g->ctx = e;
    abn_set_selected(e, g->selected_item_index);
}

static void abn_set_active_id(FwsEcl *e, int valid, unsigned id) /* activeProcedureId.set + sub (:209-221) */
{
    int i;
    if (valid == e->active_procedure_valid && (!valid || id == e->active_procedure_id)) return;
    e->active_procedure_valid = (unsigned char)(valid != 0);
    e->active_procedure_id = valid ? id : 0;
    abn_refresh_active(e);
    if (!valid) return;
    for (i = 0; i < FWS_ECL_MAX_PROCS + 1; ++i) {
        if (e->abn[i].alive && e->abn[i].gen.procedure_id == id) {
            e->abn_active = i;
            abn_pipe(e, &e->abn[i].gen);
        }
    }
    if (abn_active_gen(e)) fws_lines_select_first(abn_active_gen(e));
}

/* Tail of the presentedAbnormalProceduresList subscription (:185-196). */
static void abn_after_map_event(FwsEcl *e)
{
    int order[FWS_ECL_MAX_PROCS], n = fws_ecl_sorted_abnormal(e->lists, order), k, i;
    const FwsEclProcMap *map = &e->lists->presented_abnormal;
    for (k = 0; k < n; ++k) {
        if (e->active_procedure_valid && map->entries[order[k]].state.id == e->active_procedure_id) {
            for (i = 0; i < FWS_ECL_MAX_PROCS + 1; ++i) {
                if (e->abn[i].alive && e->abn[i].gen.procedure_id == e->active_procedure_id) {
                    e->abn_active = i;
                    abn_pipe(e, &e->abn[i].gen);
                    break;
                }
            }
        }
    }
    abn_set_active_id(e, n > 0, n > 0 ? map->entries[order[0]].state.id : 0);
}

static void abn_clear_active_procedure(FwsEcl *e, const FwsChecklistState *new_state) /* :236-261 */
{
    FwsEclCoreLists *l = e->lists;
    unsigned id;
    const FwsProcedureDef *proc;
    int i;
    if (!e->active_procedure_valid) return;
    id = e->active_procedure_id;
    if (l->presented_failures.count == 1) {
        if (!e->input->ecam_status_normal) e->events |= FWS_ECL_EVENT_SD_STATUS_PAGE; /* Call STS page on SD */
        normal_open_if_deferred_applicable(e); /* If there are deferred procedures, open ECL menu */
    }
    fws_ecl_keys_splice(&l->presented_failures, fws_ecl_keys_index(&l->presented_failures, id));
    /* Delete procedure completely if not-sensed procedure is de-activated */
    proc = abn_proc(id);
    if (new_state && !new_state->procedure_activated && proc && proc->sensed == 0) {
        fws_ecl_set_delete(&l->active_abnormal_non_sensed, id);
        fws_ecl_keys_splice(&l->all_current_failures, fws_ecl_keys_index(&l->all_current_failures, id));
    }
    l->recall_failures.count = 0;
    for (i = 0; i < l->all_current_failures.count; ++i)
        if (fws_ecl_keys_index(&l->presented_failures, l->all_current_failures.keys[i]) < 0)
            fws_ecl_keys_push(&l->recall_failures, l->all_current_failures.keys[i]);
}

/* (newState) => this.fws.presentedAbnormalProceduresList.setValue(value.id, newState) */
static void abn_item_checked(void *ctx, FwsProcLines *g, FwsChecklistState *new_state)
{
    FwsEcl *e = (FwsEcl *)ctx;
    int s = abn_slot_index(e, g);
    int existed = fws_ecl_map_get(&e->lists->presented_abnormal, g->procedure_id) != NULL;
    FwsEclProcEntry *entry;
    new_state->id = g->procedure_id;
    entry = fws_ecl_map_set(&e->lists->presented_abnormal, new_state, g->timestamps_defined);
    if (!entry) return;
    if (s >= 0) {
        if (!existed) entry->failure = e->abn[s].failure;
        /* Changed: this.procedures[i].checklistState = value (adopted by checkSelected) */
        if (e->abn[s].alive && entry->serial == e->abn[s].serial) e->abn[s].revision = entry->revision;
    }
    abn_sync(e);
    abn_after_map_event(e);
}

static void abn_cleared_or_reset(void *ctx, FwsProcLines *g, FwsChecklistState *new_state)
{
    (void)g;
    abn_clear_active_procedure((FwsEcl *)ctx, new_state); /* this.clearActiveProcedure.bind(this) */
}

/* Replays the MapSubject events of presentedAbnormalProceduresList since the last call (:131-198). */
static void abn_sync(FwsEcl *e)
{
    FwsEclProcMap *map = &e->lists->presented_abnormal;
    int i, j, changed = 0;
    for (i = 0; i < FWS_ECL_MAX_PROCS + 1; ++i) { /* Deleted */
        FwsEclAbnSlot *s = &e->abn[i];
        const FwsEclProcEntry *entry;
        if (!s->alive) continue;
        entry = fws_ecl_map_find(map, s->gen.procedure_id);
        if (!entry || entry->serial != s->serial) {
            s->alive = 0;
            changed = 1;
        }
    }
    for (i = 0; i < FWS_ECL_MAX_PROCS + 1; ++i) { /* Changed */
        FwsEclAbnSlot *s = &e->abn[i];
        const FwsEclProcEntry *entry;
        if (!s->alive) continue;
        entry = fws_ecl_map_find(map, s->gen.procedure_id);
        if (entry->revision != s->revision) {
            s->gen.checklist_state = entry->state;
            s->gen.timestamps_defined = entry->timestamps_defined;
            s->revision = entry->revision;
            s->failure = entry->failure;
            changed = 1;
        }
    }
    for (j = 0; j < map->count; ++j) { /* Added */
        const FwsEclProcEntry *entry = &map->entries[j];
        const FwsProcedureDef *proc;
        FwsEclAbnSlot *s = NULL;
        for (i = 0; i < FWS_ECL_MAX_PROCS + 1; ++i)
            if (e->abn[i].alive && e->abn[i].serial == entry->serial) break;
        if (i < FWS_ECL_MAX_PROCS + 1) continue;
        for (i = 0; i < FWS_ECL_MAX_PROCS + 1; ++i) {
            if (!e->abn[i].alive && i != e->abn_active) {
                s = &e->abn[i];
                break;
            }
        }
        if (!s) continue;
        proc = abn_proc(entry->state.id);
        fws_lines_init(&s->gen, entry->state.id, FWS_PROC_ABNORMAL, &entry->state, entry->timestamps_defined,
                       e->input->aircraft_on_ground || !proc ? FWS_RECOMMENDATION_NONE
                                                             : (FwsRecommendation)proc->recommendation,
                       0);
        s->gen.item_checked_adopts_state = 1;
        s->gen.item_checked_callback = abn_item_checked;
        s->gen.procedure_cleared_or_reset_callback = abn_cleared_or_reset;
        s->gen.procedure_completed_callback = NULL; /* () => {} */
        s->gen.ctx = e;
        s->gen.now = e->input->now;
        s->serial = entry->serial;
        s->revision = entry->revision;
        s->failure = entry->failure;
        s->alive = 1;
        changed = 1;
    }
    if (changed) {
        abn_refresh_active(e);
        abn_after_map_event(e);
    }
}

static void abn_check_selected(FwsEcl *e, FwsProcLines *g)
{
    int s = abn_slot_index(e, g);
    unsigned events = fws_lines_check_selected(g);
    if ((events & FWS_LINES_EVENT_ITEM_CHECKED) && s >= 0) {
        /* clState is the map value: later in-place mutations (conditionalActiveItems) are visible there */
        FwsEclProcEntry *entry = fws_ecl_map_get(&e->lists->presented_abnormal, g->procedure_id);
        if (entry && e->abn[s].alive && entry->serial == e->abn[s].serial) entry->state = g->checklist_state;
    }
}

static void abn_set_shown(FwsEcl *e, int shown) /* abnormalShown.set + sub (:201-207) */
{
    if ((e->abnormal_shown != 0) == (shown != 0)) return;
    e->abnormal_shown = (unsigned char)(shown != 0);
    if (shown && abn_active_gen(e)) fws_lines_select_first(abn_active_gen(e));
}

static void abn_update(FwsEcl *e) /* :290-335 */
{
    const FwsEclInput *in = e->input;
    FwsEclProcMap *map = &e->lists->presented_abnormal;
    int i;
    e->show_abnormal_sensed_requested = (unsigned char)(map->count > 0);

    /* checkIfStsAutoDisplay (:271-284) */
    if (map->count == 0 && in->flight_phase == 8 && in->adr_pressure_altitude < 20000.0 && !in->ecam_status_normal &&
        in->approach_auto_display_pulse)
        e->events |= FWS_ECL_EVENT_SD_STATUS_PAGE;

    if (!e->abnormal_shown) return;
    if (e->pulse_down && abn_active_gen(e)) fws_lines_move_down(abn_active_gen(e), 1);
    if (e->pulse_up && abn_active_gen(e)) fws_lines_move_up(abn_active_gen(e));
    if (e->pulse_check && abn_active_gen(e)) abn_check_selected(e, abn_active_gen(e));

    /* Auto-move-down if currently marked item was sensed as completed */
    if (abn_active_gen(e)) {
        for (i = 0; i < map->count; ++i) {
            const FwsEclProcEntry *entry = &map->entries[i];
            if (!entry->has_updated_items) continue;
            if (e->active_procedure_valid && entry->state.id == e->active_procedure_id) {
                FwsProcLines *g = abn_active_gen(e);
                int sii = g->selected_item_index;
                if (sii >= 0 && sii < FWS_MAX_ITEMS && entry->updated_items[sii] && g->checklist_state.items_checked[sii])
                    fws_lines_move_down(g, 1);
            }
        }
    }
}

/* ============================================================================================== */
/* FwsNormalChecklists (FwsNormalChecklists.ts:44-531)                                             */
/* ============================================================================================== */
static FwsProcLines *normal_active_gen(FwsEcl *e)
{
    if (e->normal_active == -1) return &e->normal_gen;
    if (e->normal_active >= 0 && e->normal_active < e->num_deferred_gens) return &e->deferred_gens[e->normal_active];
    return NULL;
}

static void normal_set_selected(FwsEcl *e, int value) /* selectedLine.sub(scrollToSelectedLine) :259, :380 */
{
    int from;
    if (e->selected_line == value) return;
    e->selected_line = value;
    from = value - FWS_WD_NUM_LINES + 2;
    e->show_from_line = from > 0 ? from : 0;
}

static void normal_selected_hook(void *ctx, FwsProcLines *g) { normal_set_selected((FwsEcl *)ctx, g->selected_item_index); }

static void normal_pipe(FwsEcl *e, FwsProcLines *g)
{
    g->selected_changed = normal_selected_hook;
    g->ctx = e;
    normal_set_selected(e, g->selected_item_index);
}

static void normal_refresh_deferred_flags(FwsEcl *e) /* activeDeferredProcedureId.map((id) => id === proc.id) */
{
    int i;
    for (i = 0; i < e->num_deferred_gens; ++i)
        e->deferred_gens[i].procedure_is_active =
            (unsigned char)(e->active_deferred_valid && e->deferred_gens[i].procedure_id == e->active_deferred_id);
}

static void normal_reset(FwsEcl *e, int from_index) /* reset(fromId) :342-365 */
{
    int i, j;
    if (from_index == -1) return;
    e->manual_checklist_reset = (unsigned char)(from_index != FWS_ECL_RESET_ALL);
    for (i = from_index == FWS_ECL_RESET_ALL ? 0 : from_index + 1; i < FWS_NORMAL_PROCEDURE_COUNT; ++i) {
        const FwsProcedureDef *proc = &fws_normal_procedures[i];
        FwsChecklistState *st = &e->normal_state[i];
        st->procedure_completed = (unsigned char)(proc->only_activated_by_request == 1);
        st->procedure_activated = 1;
        for (j = 0; j < proc->num_items; ++j)
            if (!proc->items[j].sensed) st->items_checked[j] = 0;
    }
}

static void normal_set_active_deferred(FwsEcl *e, int valid, unsigned id); /* fwd */

static void normal_item_checked(void *ctx, FwsProcLines *g, FwsChecklistState *new_state)
{
    FwsEcl *e = (FwsEcl *)ctx;
    int idx = fws_normal_procedure_index(e->checklist_id);
    (void)g;
    if (idx >= 0) e->normal_state[idx] = *new_state; /* checklistState.setValue(checklistId, newState) */
}

static void normal_cleared_or_reset(void *ctx, FwsProcLines *g, FwsChecklistState *new_state)
{
    FwsEcl *e = (FwsEcl *)ctx;
    normal_item_checked(ctx, g, new_state);
    normal_reset(e, fws_normal_procedure_index(e->checklist_id));
}

static void normal_completed(void *ctx, FwsProcLines *g, FwsChecklistState *new_state)
{
    FwsEcl *e = (FwsEcl *)ctx;
    normal_item_checked(ctx, g, new_state);
    e->show_checklist_requested = 0;
}

static void deferred_map_changed(FwsEcl *e);

static void deferred_item_checked(void *ctx, FwsProcLines *g, FwsChecklistState *new_state)
{
    FwsEcl *e = (FwsEcl *)ctx;
    new_state->id = g->procedure_id;
    fws_ecl_map_set(&e->lists->active_deferred, new_state, g->timestamps_defined);
    deferred_map_changed(e);
}

static void deferred_cleared_or_reset(void *ctx, FwsProcLines *g, FwsChecklistState *new_state)
{
    /* Handle procedure activation/deactivation (FwsNormalChecklists.ts:157-167) */
    FwsEcl *e = (FwsEcl *)ctx;
    const FwsEclProcEntry *entry = fws_ecl_map_find(&e->lists->active_deferred, g->procedure_id);
    int i;
    if (entry && entry->has_which_items_active) {
        memcpy(new_state->items_active, entry->which_items_active, sizeof(new_state->items_active));
    } else {
        int length = entry ? entry->which_items_checked_length : (g->procedure ? g->procedure->num_items : 0);
        for (i = 0; i < FWS_MAX_ITEMS; ++i)
            new_state->items_active[i] = (unsigned char)(i < length ? new_state->procedure_activated : 0);
    }
    deferred_item_checked(ctx, g, new_state);
}

static void normal_on_checklist_id(FwsEcl *e) /* checklistId.sub (:105-181) */
{
    unsigned id = e->checklist_id;
    int idx = fws_normal_procedure_index(id), didx = fws_deferred_procedure_id_index(id), i;
    FwsEclProcMap *deferred = &e->lists->active_deferred;
    if (id != 0 && didx < 0 && idx >= 0) {
        FwsProcLines *g = &e->normal_gen;
        fws_lines_init(g, id, FWS_PROC_NORMAL, &e->normal_state[idx], 0, FWS_RECOMMENDATION_NONE, 0);
        g->procedure_is_active = 1; /* Subject.create(true) */
        g->item_checked_callback = normal_item_checked;
        g->procedure_cleared_or_reset_callback = normal_cleared_or_reset;
        g->procedure_completed_callback = normal_completed;
        g->ctx = e;
        g->now = e->input->now;
        e->normal_active = -1;
        normal_pipe(e, g);
    } else if (didx >= 0) {
        e->num_deferred_gens = 0;
        e->num_visible_deferred = 0;
        for (i = 0; i < deferred->count; ++i) {
            const FwsProcedureDef *proc = def_proc(deferred->entries[i].state.id);
            if (proc && proc->deferred_type == didx)
                e->visible_deferred_keys[e->num_visible_deferred++] = deferred->entries[i].state.id;
        }
        if (e->num_visible_deferred == 0) {
            normal_set_active_deferred(e, 0, 0);
            e->normal_active = -2;
            return; /* FBW returns before publishing fws_normal_checklists_id */
        }
        normal_set_active_deferred(e, 1, e->visible_deferred_keys[0]);
        for (i = 0; i < e->num_visible_deferred; ++i) {
            const FwsEclProcEntry *entry = fws_ecl_map_find(deferred, e->visible_deferred_keys[i]);
            FwsProcLines *g;
            if (!entry) continue;
            g = &e->deferred_gens[e->num_deferred_gens++];
            fws_lines_init(g, entry->state.id, FWS_PROC_DEFERRED, &entry->state, entry->timestamps_defined,
                           FWS_RECOMMENDATION_NONE, 0);
            g->item_checked_callback = deferred_item_checked;
            g->procedure_cleared_or_reset_callback = deferred_cleared_or_reset;
            g->procedure_completed_callback = deferred_item_checked;
            g->ctx = e;
            g->now = e->input->now;
        }
        normal_refresh_deferred_flags(e);
        e->normal_active = 0;
        normal_pipe(e, &e->deferred_gens[0]);
    }
    e->pub.fws_normal_checklists_id = id;
}

static void normal_set_checklist_id(FwsEcl *e, unsigned id)
{
    if (e->checklist_id == id) return;
    e->checklist_id = id;
    normal_on_checklist_id(e);
}

static void normal_set_active_deferred(FwsEcl *e, int valid, unsigned id) /* activeDeferredProcedureId (:206-216) */
{
    int i;
    if (valid == e->active_deferred_valid && (!valid || id == e->active_deferred_id)) return;
    e->active_deferred_valid = (unsigned char)(valid != 0);
    e->active_deferred_id = valid ? id : 0;
    normal_refresh_deferred_flags(e);
    if (valid) {
        for (i = 0; i < e->num_deferred_gens; ++i) {
            if (e->deferred_gens[i].procedure_id == id) {
                e->normal_active = i;
                normal_pipe(e, &e->deferred_gens[i]);
                fws_lines_select_first(&e->deferred_gens[i]);
                break;
            }
        }
    } else {
        normal_set_selected(e, FWS_SPECIAL_INDEX_DEFERRED_PAGE_CLEAR);
    }
}

/* activeDeferredProceduresList.sub (:183-204): refresh the page if the active deferred procedure is gone. */
static void deferred_map_changed(FwsEcl *e)
{
    e->deferred_map_signature = map_signature(&e->lists->active_deferred);
    if (e->active_deferred_valid && !fws_ecl_map_find(&e->lists->active_deferred, e->active_deferred_id))
        normal_on_checklist_id(e); /* this.checklistId.notify() */
}

static void deferred_sync(FwsEcl *e)
{
    if (map_signature(&e->lists->active_deferred) != e->deferred_map_signature) deferred_map_changed(e);
}

static void normal_select_first(FwsEcl *e) /* :274-286 */
{
    if (e->checklist_id == 0) {
        int i, first = -1;
        for (i = 0; i < FWS_NORMAL_PROCEDURE_COUNT; ++i) {
            if (e->overview_items_to_show[i] && !e->normal_state[i].procedure_completed) {
                first = i;
                break;
            }
        }
        normal_set_selected(e, first != -1 ? first : 0);
    } else if (normal_active_gen(e)) {
        fws_lines_select_first(normal_active_gen(e));
    }
}

static int overview_shown_items(const FwsEcl *e, int *shown)
{
    int i, n = 0;
    for (i = 0; i < FWS_NORMAL_PROCEDURE_COUNT; ++i)
        if (e->overview_items_to_show[i]) shown[n++] = i;
    return n;
}

static int index_of_int(const int *values, int n, int v)
{
    int i;
    for (i = 0; i < n; ++i)
        if (values[i] == v) return i;
    return -1;
}

static int visible_deferred_index(const FwsEcl *e, unsigned id)
{
    int i;
    for (i = 0; i < e->num_visible_deferred; ++i)
        if (e->visible_deferred_keys[i] == id) return i;
    return -1;
}

static void normal_move_up(FwsEcl *e) /* :288-314 */
{
    if (e->checklist_id == 0) {
        int shown[FWS_NORMAL_PROCEDURE_COUNT], n = overview_shown_items(e, shown);
        int pos = index_of_int(shown, n, e->selected_line) - 1;
        int value = pos >= 0 && pos < n ? shown[pos] : 0; /* ?? 0 */
        normal_set_selected(e, value > 0 ? value : 0);
    } else if (fws_deferred_procedure_id_index(e->checklist_id) >= 0) {
        if (e->active_deferred_valid) {
            FwsProcLines *g = normal_active_gen(e);
            if (g && fws_lines_first_line_is_selected(g)) {
                int cur = visible_deferred_index(e, e->active_deferred_id);
                if (cur != -1 && cur > 0) normal_set_active_deferred(e, 1, e->visible_deferred_keys[cur - 1]);
            } else if (g) {
                fws_lines_move_up(g);
            }
        } else {
            /* CLEAR of page selected, select last procedure */
            normal_set_active_deferred(e, e->num_visible_deferred > 0,
                                       e->num_visible_deferred > 0
                                           ? e->visible_deferred_keys[e->num_visible_deferred - 1]
                                           : 0);
        }
    } else if (normal_active_gen(e)) {
        fws_lines_move_up(normal_active_gen(e));
    }
}

static void normal_move_down(FwsEcl *e, int skip_completed_sensed) /* :316-340 */
{
    FwsProcLines *g = normal_active_gen(e);
    if (e->checklist_id == 0) {
        int shown[FWS_NORMAL_PROCEDURE_COUNT], n = overview_shown_items(e, shown), pos, value;
        if (n == 0) return; /* FBW: Math.min(undefined, undefined) -> NaN; unreachable */
        pos = index_of_int(shown, n, e->selected_line) + 1;
        value = pos < n ? shown[pos] : shown[n - 1];
        normal_set_selected(e, value < shown[n - 1] ? value : shown[n - 1]);
    } else if (e->active_deferred_valid && g && fws_lines_last_line_is_selected(g)) {
        int cur = visible_deferred_index(e, e->active_deferred_id);
        if (cur != -1) {
            int has_next = cur < e->num_visible_deferred - 1;
            normal_set_active_deferred(e, has_next, has_next ? e->visible_deferred_keys[cur + 1] : 0);
        }
    } else if (g) {
        fws_lines_move_down(g, skip_completed_sensed);
    }
}

static void normal_navigate_to_checklist(FwsEcl *e, unsigned id) /* :367-370 */
{
    normal_set_checklist_id(e, id);
    normal_select_first(e);
}

static void normal_navigate_to_parent(FwsEcl *e) /* :372-378 */
{
    if (e->checklist_id == 0) e->show_checklist_requested = 0;
    else normal_navigate_to_checklist(e, 0);
}

static int any_true(const unsigned char *v, int n)
{
    int i;
    for (i = 0; i < n; ++i)
        if (v[i]) return 1;
    return 0;
}

static int any_false(const unsigned char *v, int n)
{
    int i;
    for (i = 0; i < n; ++i)
        if (!v[i]) return 1;
    return 0;
}

static void normal_open_if_deferred_applicable(FwsEcl *e) /* :400-407 */
{
    int applicable = any_true(e->has_deferred, FWS_DEFERRED_TYPE_COUNT) &&
                     any_false(e->deferred_is_completed, FWS_DEFERRED_TYPE_COUNT);
    if (applicable && !e->show_checklist_requested) {
        e->show_checklist_requested = 1;
        normal_navigate_to_checklist(e, 0);
    }
}

static void normal_check_selected(FwsEcl *e, FwsProcLines *g)
{
    unsigned events = fws_lines_check_selected(g);
    if (!(events & FWS_LINES_EVENT_ITEM_CHECKED)) return;
    /* The map holds clState itself; mirror the in-place mutations done after the callback. */
    if (g == &e->normal_gen) {
        int idx = fws_normal_procedure_index(e->checklist_id);
        if (idx >= 0 && fws_normal_procedures[idx].id == g->procedure_id) e->normal_state[idx] = g->checklist_state;
    } else {
        FwsEclProcEntry *entry = fws_ecl_map_get(&e->lists->active_deferred, g->procedure_id);
        if (entry) entry->state = g->checklist_state;
    }
}

static void normal_update(FwsEcl *e) /* update() :409-531 */
{
    const FwsEclInput *in = e->input;
    const FwsEclProcMap *deferred = &e->lists->active_deferred;
    int i, k;

    if (e->pulse_cl) {
        normal_navigate_to_checklist(e, 0);
        e->show_checklist_requested = (unsigned char)!e->show_checklist_requested;
    }

    /* Update deferred proc status */
    for (i = 0; i < FWS_DEFERRED_TYPE_COUNT; ++i) {
        e->has_deferred[i] = 0;
        e->deferred_is_completed[i] = 1;
    }
    for (i = 0; i < deferred->count; ++i) {
        const FwsProcedureDef *proc = def_proc(deferred->entries[i].state.id);
        int t = proc ? proc->deferred_type : -1;
        if (t < 0 || t >= FWS_DEFERRED_TYPE_COUNT) continue;
        e->has_deferred[t] = 1;
        if (!deferred->entries[i].state.procedure_completed) e->deferred_is_completed[t] = 0;
    }

    /* checkIfDeferredAutoDisplay (:384-398) */
    if (e->lists->presented_abnormal.count == 0 && in->flight_phase == 8 && in->adr_pressure_altitude < 20000.0 &&
        any_true(e->has_deferred, FWS_DEFERRED_TYPE_COUNT) &&
        any_false(e->deferred_is_completed, FWS_DEFERRED_TYPE_COUNT) && in->approach_auto_display_pulse &&
        !e->show_checklist_requested) {
        e->show_checklist_requested = 1;
        normal_navigate_to_checklist(e, 0);
    }

    if (!e->checklist_shown) return;

    if (e->pulse_down) normal_move_down(e, 1);
    if (e->pulse_up) normal_move_up(e);
    if (e->pulse_check) {
        if (e->checklist_id == 0) {
            if (e->selected_line >= 0 && e->selected_line < FWS_NORMAL_PROCEDURE_COUNT)
                normal_navigate_to_checklist(e, fws_normal_procedures[e->selected_line].id); /* Navigate to C/L */
        } else if (fws_deferred_procedure_id_index(e->checklist_id) >= 0 && !e->active_deferred_valid) {
            normal_navigate_to_checklist(e, 0);
        } else if (normal_active_gen(e)) {
            normal_check_selected(e, normal_active_gen(e));
        }
    }

    /* Update sensed items */
    for (k = 0; k < FWS_NORMAL_PROCEDURE_COUNT; ++k) {
        const FwsProcedureDef *proc = &fws_normal_procedures[k];
        FwsChecklistState *cl = &e->normal_state[k];
        FwsChecklistState next;
        int didx = fws_deferred_procedure_id_index(proc->id), changed_any = 0, selected_hit = 0, n = proc->num_items;
        unsigned char completed = didx != -1 ? e->deferred_is_completed[didx] : cl->procedure_completed;
        for (i = 0; i < n; ++i) {
            int v = in->normal_sensed[k][i];
            if (v != -1 && (v != 0) != (cl->items_checked[i] != 0)) {
                changed_any = 1;
                if (i == e->selected_line && v) selected_hit = 1;
            }
        }
        if (changed_any || (completed != 0) != (cl->procedure_completed != 0)) {
            if (selected_hit) normal_move_down(e, 1);
            next = *cl;
            next.procedure_completed = completed;
            for (i = 0; i < n; ++i)
                if (proc->items[i].sensed && in->normal_sensed[k][i] != -1)
                    next.items_checked[i] = (unsigned char)(in->normal_sensed[k][i] != 0);
            *cl = next;
        }
    }

    /* Overview: deferred procedure lines (indices of 1000007/8/9/11) */
    for (i = 0; i < FWS_DEFERRED_TYPE_COUNT; ++i) {
        int idx = fws_normal_procedure_index(fws_deferred_procedure_ids[i]);
        if (idx < 0) continue;
        e->overview_items_checked[idx] = e->deferred_is_completed[i];
        e->overview_items_to_show[idx] = e->has_deferred[i];
    }
}

/* ============================================================================================== */
/* FwsAbnormalNonSensed (FwsAbnormalNonSensed.ts:15-146)                                           */
/* ============================================================================================== */
static int same_category(const char *a, const char *b)
{
    if (!a || !b) return a == b;
    return strcmp(a, b) == 0;
}

static int ns_count_category(const char *category)
{
    int i, n = 0;
    for (i = 0; i < FWS_ABN_NON_SENSED_OVERVIEW_COUNT; ++i)
        if (same_category(fws_abn_non_sensed_overview[i].category, category)) ++n;
    return n;
}

static int ns_menu_size(const FwsEcl *e) /* getAbnormalNonSensedMenuSize */
{
    if (e->abn_non_sensed_id == 0) return ns_count_category(NULL) + FWS_ABN_SUB_MENU_COUNT;
    if (e->abn_non_sensed_id <= FWS_ABN_SUB_MENU_COUNT)
        return ns_count_category(fws_abn_sub_menu_vector[e->abn_non_sensed_id - 1]);
    return 0;
}

static void ns_set_id(FwsEcl *e, unsigned id) /* checklistId.sub (:36-43) */
{
    if (e->abn_non_sensed_id == id) return;
    e->abn_non_sensed_id = id;
    e->pub.fws_abn_non_sensed_id = id;
    if (id > 10)
        e->pub.fws_abn_non_sensed_current_active =
            (unsigned char)(fws_ecl_keys_index(&e->lists->active_abnormal_non_sensed, id) >= 0);
}

static void ns_navigate_to_parent(FwsEcl *e) /* :76-83 */
{
    if (e->abn_non_sensed_id == 0) {
        e->show_abn_proc_requested = 0;
    } else {
        ns_set_id(e, 0);
        e->abn_non_sensed_selected = 0;
    }
}

static void ns_update(FwsEcl *e) /* :85-140 */
{
    if (e->pulse_abn_proc) {
        ns_set_id(e, 0);
        e->abn_non_sensed_selected = 0;
        e->show_abn_proc_requested = (unsigned char)!e->show_abn_proc_requested;
    }
    if (!e->abn_proc_shown) return;

    if (e->pulse_down && e->abn_non_sensed_id <= 10) { /* moveDown */
        int v = e->abn_non_sensed_selected + 1, max = ns_menu_size(e) - 1;
        e->abn_non_sensed_selected = v < max ? v : max;
    }
    if (e->pulse_up && e->abn_non_sensed_id <= 10) { /* moveUp */
        int v = e->abn_non_sensed_selected - 1;
        e->abn_non_sensed_selected = v > 0 ? v : 0;
    }
    if (e->pulse_check) {
        int sel = e->abn_non_sensed_selected;
        if (e->abn_non_sensed_id == 0) {
            int skip = ns_count_category(NULL);
            if (sel >= skip && sel < ns_menu_size(e)) {
                ns_set_id(e, (unsigned)(sel - skip + 1)); /* Call sub menu */
                e->abn_non_sensed_selected = 0;
            } else if (sel < skip) {
                ns_set_id(e, sel >= 0 ? fws_abn_non_sensed_overview[sel].id : 0); /* Preview procedure */
                e->abn_non_sensed_selected = -1;
            }
        } else if (e->abn_non_sensed_id <= 10) {
            int start = -1, i, index;
            const char *category = e->abn_non_sensed_id <= FWS_ABN_SUB_MENU_COUNT
                                       ? fws_abn_sub_menu_vector[e->abn_non_sensed_id - 1]
                                       : NULL;
            for (i = 0; i < FWS_ABN_NON_SENSED_OVERVIEW_COUNT && category; ++i) {
                if (same_category(fws_abn_non_sensed_overview[i].category, category)) {
                    start = i;
                    break;
                }
            }
            index = start + sel;
            if (start >= 0 && index >= 0 && index < FWS_ABN_NON_SENSED_OVERVIEW_COUNT) { /* FBW would throw */
                ns_set_id(e, fws_abn_non_sensed_overview[index].id); /* Preview procedure */
                e->abn_non_sensed_selected = -1;
            }
        } else {
            /* Activate or de-activate the non-sensed procedure and return to abnormal procs */
            FwsEclKeyList *keys = &e->lists->active_abnormal_non_sensed;
            if (fws_ecl_keys_index(keys, e->abn_non_sensed_id) < 0) fws_ecl_set_add(keys, e->abn_non_sensed_id);
            else fws_ecl_set_delete(keys, e->abn_non_sensed_id);
            e->abn_non_sensed_selected = 0;
            e->show_abn_proc_requested = 0;
        }
    }
}

/* ============================================================================================== */
/* Public API                                                                                      */
/* ============================================================================================== */
void fws_ecl_init(FwsEcl *e)
{
    int i, j;
    memset(e, 0, sizeof(*e));
    e->normal_active = -2;
    e->abn_active = -1;
    e->abn_selected_item_index = 1; /* FwsAbnormalSensed.ts:105 */
    for (i = 0; i < FWS_NORMAL_PROCEDURE_COUNT; ++i) { /* Populate checklistState (:236-257) */
        const FwsProcedureDef *proc = &fws_normal_procedures[i];
        FwsChecklistState *st = &e->normal_state[i];
        st->id = proc->id;
        st->procedure_completed = (unsigned char)(proc->only_activated_by_request == 1);
        st->procedure_activated = 1;
        for (j = 0; j < proc->num_items; ++j) {
            st->items_active[j] = 1;
            st->items_to_show[j] = 1;
        }
        e->overview_items_to_show[i] = 1;
    }
    e->deferred_map_signature = 2166136261u; /* map_signature() of an empty map */
    /* Initial publications / EWD ConsumerSubject defaults are all zero. */
}

void fws_ecl_buttons(FwsEcl *e, const FwsEcpButtons *b)
{
#define FWS_ECL_PULSE(name, level)                                     \
    do {                                                               \
        unsigned char v_ = (unsigned char)((level) != 0);              \
        e->pulse_##name = (unsigned char)(v_ && !e->last_##name);      \
        e->last_##name = v_;                                           \
    } while (0)
    FWS_ECL_PULSE(clr, b->clr || b->clr2);
    FWS_ECL_PULSE(rcl, b->rcl && !b->ecp_failed);
    FWS_ECL_PULSE(cl, b->cl);
    FWS_ECL_PULSE(check, b->check_lh || b->check_rh);
    FWS_ECL_PULSE(up, b->up);
    FWS_ECL_PULSE(down, b->down);
    FWS_ECL_PULSE(abn_proc, b->abn_proc);
#undef FWS_ECL_PULSE
}

unsigned fws_ecl_clear_and_recall(FwsEcl *e, FwsEclCoreLists *lists, const FwsEclInput *input)
{
    ecl_bind(e, lists, input);
    abn_sync(e);
    deferred_sync(e);
    if (e->pulse_clr) { /* FwsCore.ts:5512-5521 */
        if (e->abnormal_shown) abn_clear_active_procedure(e, NULL); /* delete the first failure */
        else if (e->checklist_shown) normal_navigate_to_parent(e);
        else if (e->abn_proc_shown) ns_navigate_to_parent(e);
    }
    if (e->pulse_rcl && lists->recall_failures.count > 0) { /* FwsCore.ts:5523-5536 */
        unsigned key = lists->recall_failures.keys[0];
        FwsEclProcEntry *recalled;
        fws_ecl_keys_splice(&lists->recall_failures, 0);
        fws_ecl_keys_push(&lists->presented_failures, key);
        recalled = fws_ecl_map_get(&lists->cleared_abnormal, key);
        if (recalled) {
            FwsEclProcEntry copy = *recalled;
            FwsEclProcEntry *entry = fws_ecl_map_set(&lists->presented_abnormal, &copy.state, copy.timestamps_defined);
            if (entry) entry->failure = copy.failure;
            fws_ecl_map_delete(&lists->cleared_abnormal, key);
            abn_sync(e);
        }
    }
    return e->events;
}

unsigned fws_ecl_update(FwsEcl *e, FwsEclCoreLists *lists, const FwsEclInput *in)
{
    ecl_bind(e, lists, in);
    /* Subscriptions on core subjects (FwsNormalChecklists.ts:218-234) */
    if (!e->inputs_seen) {
        e->inputs_seen = 1;
        e->flight_phase_last = in->flight_phase;
        e->takeoff_power_last = (unsigned char)(in->takeoff_power != 0);
    } else {
        if (in->flight_phase != e->flight_phase_last) {
            e->flight_phase_last = in->flight_phase;
            if (in->flight_phase != 1) e->manual_checklist_reset = 0;
        }
        if ((in->takeoff_power != 0) != e->takeoff_power_last) {
            e->takeoff_power_last = (unsigned char)(in->takeoff_power != 0);
            if (in->takeoff_power) normal_reset(e, fws_normal_procedure_index(DEPARTURE_CHANGE_ID));
        }
    }
    /* Map subscriptions fired by the core's abnormal/deferred loop */
    abn_sync(e);
    deferred_sync(e);

    normal_update(e);
    abn_update(e);
    ns_update(e);

    /* Orchestrate display of normal or abnormal proc display (FwsCore.ts:6165-6208) */
    if (e->show_abn_proc_requested) {
        e->abn_proc_shown = 1;
        e->checklist_shown = 0;
        abn_set_shown(e, 0);
        e->pub.fws_active_item = e->abn_non_sensed_selected;
        e->pub.fws_show_from_line = e->abn_non_sensed_show_from_line;
        e->pub.fws_show_failure_pending = 0;
    } else if (e->show_checklist_requested) {
        e->abn_proc_shown = 0;
        e->checklist_shown = 1;
        abn_set_shown(e, 0);
        e->pub.fws_active_item = e->selected_line;
        if (e->active_deferred_valid) e->pub.fws_active_procedure = e->active_deferred_id;
        e->pub.fws_show_from_line = e->show_from_line;
        e->pub.fws_show_failure_pending = e->show_abnormal_sensed_requested;
    } else if (e->show_abnormal_sensed_requested) {
        e->abn_proc_shown = 0;
        e->checklist_shown = 0;
        abn_set_shown(e, 1);
        if (e->active_procedure_valid) {
            e->pub.fws_active_item = e->abn_selected_item_index;
            e->pub.fws_active_procedure = e->active_procedure_id;
        }
        e->pub.fws_show_from_line = e->abn_show_from_line;
        e->pub.fws_show_failure_pending = 0;
    } else {
        e->abn_proc_shown = 0;
        e->checklist_shown = 0;
        abn_set_shown(e, 0);
        e->pub.fws_show_failure_pending = 0;
    }
    e->pub.fws_show_abn_non_sensed = e->abn_proc_shown;
    e->pub.fws_show_abn_sensed = e->abnormal_shown;
    e->pub.fws_show_normal_checklists = e->checklist_shown;
    return e->events;
}

static void ecl_reset_tail(FwsEcl *e, FwsEclCoreLists *lists)
{
    /* abnormalNonSensed.reset() (FwsAbnormalNonSensed.ts:142-146) */
    lists->active_abnormal_non_sensed.count = 0;
    e->show_abn_proc_requested = 0;
    /* activeDeferredProceduresList.clear() */
    fws_ecl_map_clear(&lists->active_deferred);
    deferred_sync(e);
    /* abnormalSensed.reset() (FwsAbnormalSensed.ts:337-339) */
    e->show_abnormal_sensed_requested = 0;
    fws_ecl_map_clear(&lists->presented_abnormal);
    abn_sync(e);
    fws_ecl_map_clear(&lists->cleared_abnormal);
    lists->all_current_failures.count = 0;
    lists->presented_failures.count = 0;
    lists->recall_failures.count = 0;
}

void fws_ecl_startup_reset(FwsEcl *e, FwsEclCoreLists *lists) /* FwsCore.ts:2414-2426 */
{
    ecl_bind(e, lists, NULL);
    normal_reset(e, FWS_ECL_RESET_ALL);
    ecl_reset_tail(e, lists);
}

void fws_ecl_shutdown_reset(FwsEcl *e, FwsEclCoreLists *lists) /* FwsCore.ts:2436-2452 */
{
    ecl_bind(e, lists, NULL);
    if (!e->manual_checklist_reset) normal_reset(e, FWS_ECL_RESET_ALL);
    ecl_reset_tail(e, lists);
}

/* ============================================================================================== */
/* EWD composition                                                                                 */
/* ============================================================================================== */
typedef struct {
    FwsEclWd *out;
    int active_line;
    int index, count;
} Window;

/* WdAbstractChecklistComponent.updateChecklists (:66-100), streamed */
static void win_push(Window *w, const FwsWdLine *line)
{
    if (w->index >= w->out->show_from_line && w->count < FWS_WD_NUM_LINES) {
        w->out->lines[w->count] = *line;
        w->out->line_selected[w->count] = (unsigned char)(line->has_item_index && line->active_procedure
                                                              ? line->item_index == w->active_line
                                                              : 0);
        ++w->count;
    }
    ++w->index;
}

static void win_push_gen(Window *w, const FwsProcLines *g)
{
    FwsWdLine lines[FWS_LINES_MAX];
    int n = fws_lines_to_line_data(g, lines, FWS_LINES_MAX), i;
    for (i = 0; i < n; ++i) win_push(w, &lines[i]);
}

static FwsWdLine wd_line(int active, int abnormal, int sensed, int checked, const char *text, int style, int first,
                         int last)
{
    FwsWdLine l;
    memset(&l, 0, sizeof(l));
    l.active_procedure = (unsigned char)active;
    l.abnormal_procedure = (unsigned char)abnormal;
    l.sensed = (unsigned char)sensed;
    l.checked = (unsigned char)checked;
    fws_js_prefix(l.text, sizeof(l.text), text, (size_t)-1);
    l.style = (unsigned char)style;
    l.first_line = (unsigned char)first;
    l.last_line = (unsigned char)last;
    l.special = FWS_SPECIAL_NONE;
    return l;
}

static void line_index(FwsWdLine *l, int index)
{
    l->item_index = index;
    l->has_item_index = 1;
}

static void cat(char *buf, size_t size, const char *s)
{
    size_t len = strlen(buf);
    fws_js_prefix(buf + len, size - len, s, (size_t)-1);
}

static void compose_normal(const FwsEcl *e, const FwsEclCoreLists *lists, double now, Window *w) /* WdNormalChecklists */
{
    const FwsEclProcMap *deferred = &lists->active_deferred;
    unsigned id = e->pub.fws_normal_checklists_id;
    int idx = fws_normal_procedure_index(id), didx = fws_deferred_procedure_id_index(id), i, t;
    unsigned char has_deferred[FWS_DEFERRED_TYPE_COUNT], deferred_is_completed[FWS_DEFERRED_TYPE_COUNT];
    char buf[FWS_TEXT_MAX];

    for (t = 0; t < FWS_DEFERRED_TYPE_COUNT; ++t) {
        has_deferred[t] = 0;
        deferred_is_completed[t] = 1; /* every(): note FBW also requires every entry to be of this type */
        for (i = 0; i < deferred->count; ++i) {
            const FwsProcedureDef *proc = def_proc(deferred->entries[i].state.id);
            int is_type = proc && proc->deferred_type == t;
            if (is_type) has_deferred[t] = 1;
            if (!(is_type && deferred->entries[i].state.procedure_completed)) deferred_is_completed[t] = 0;
        }
    }

    if (id == 0) { /* Render overview page */
        FwsWdLine l = wd_line(1, 0, 1, 0, "CHECKLISTS", FWS_STYLE_HEADLINE, 1, 0);
        win_push(w, &l);
        for (i = 0; i < FWS_NORMAL_PROCEDURE_COUNT; ++i) {
            const FwsChecklistState *st = &e->normal_state[i];
            int d = fws_deferred_procedure_id_index(fws_normal_procedures[i].id), style, checked = 0, display = 1;
            if (d >= 0) {
                style = st->procedure_completed ? FWS_STYLE_COMPLETED_DEFERRED_PROCEDURE : FWS_STYLE_DEFERRED_PROCEDURE;
                checked = deferred_is_completed[d];
                display = has_deferred[d];
            } else {
                style = st->procedure_completed ? FWS_STYLE_COMPLETED_CHECKLIST : FWS_STYLE_CHECKLIST_ITEM;
                checked = st->procedure_completed;
            }
            if (display) {
                l = wd_line(1, 0, 1, checked, fws_normal_procedures[i].title, style, 0,
                            i == FWS_NORMAL_PROCEDURE_COUNT - 1);
                line_index(&l, i);
                win_push(w, &l);
            }
        }
    } else if (idx >= 0 && didx < 0) {
        FwsProcLines g;
        fws_lines_init(&g, id, FWS_PROC_NORMAL, &e->normal_state[idx], 0, FWS_RECOMMENDATION_NONE, 0);
        g.procedure_is_active = 1;
        g.now = now;
        win_push_gen(w, &g);
    } else if (idx >= 0 && didx >= 0) { /* Deferred procedures */
        FwsWdLine l;
        int visible[FWS_ECL_MAX_PROCS], nv = 0;
        buf[0] = 0;
        cat(buf, sizeof(buf), e->normal_state[idx].procedure_completed ? "\x1b<7m" : "\x1b<4m");
        cat(buf, sizeof(buf), fws_normal_procedures[idx].title);
        cat(buf, sizeof(buf), " \x1bm");
        l = wd_line(1, 1, 1, 0, buf, FWS_STYLE_HEADLINE, 1, 0);
        win_push(w, &l);
        l = wd_line(1, 1, 1, 0, "", FWS_STYLE_CHECKLIST_ITEM, 0, 0);
        win_push(w, &l);
        for (i = 0; i < deferred->count; ++i) {
            const FwsProcedureDef *proc = def_proc(deferred->entries[i].state.id);
            if (proc && proc->deferred_type == didx) visible[nv++] = i;
        }
        for (i = 0; i < nv; ++i) {
            const FwsEclProcEntry *entry = &deferred->entries[visible[i]];
            FwsProcLines g;
            fws_lines_init(&g, entry->state.id, FWS_PROC_DEFERRED, &entry->state, 0, FWS_RECOMMENDATION_NONE,
                           i == nv - 1);
            g.procedure_is_active = (unsigned char)(entry->state.id == e->pub.fws_active_procedure);
            g.now = now;
            win_push_gen(w, &g);
        }
        buf[0] = 0;
        for (i = 0; i < 34; ++i) cat(buf, sizeof(buf), NBSP);
        cat(buf, sizeof(buf), "CLEAR");
        l = wd_line(1, 1, 0, 0, buf, FWS_STYLE_CHECKLIST_ITEM, 0, 1);
        line_index(&l, FWS_SPECIAL_INDEX_DEFERRED_PAGE_CLEAR);
        win_push(w, &l);
    }
}

static void compose_abnormal(const FwsEcl *e, const FwsEclCoreLists *lists, const FwsEclEwdInput *in, Window *w)
{
    if (in->fws_avail) { /* WdAbnormalSensedProcedures.updateChecklists (:52-119) */
        int order[FWS_ECL_MAX_PROCS], n = fws_ecl_sorted_abnormal(lists, order), i;
        for (i = 0; i < n; ++i) {
            const FwsEclProcEntry *entry = &lists->presented_abnormal.entries[order[i]];
            const FwsProcedureDef *proc = abn_proc(entry->state.id);
            FwsProcLines g;
            fws_lines_init(&g, entry->state.id, FWS_PROC_ABNORMAL, &entry->state, entry->timestamps_defined,
                           proc ? (FwsRecommendation)proc->recommendation : FWS_RECOMMENDATION_NONE, i == 0);
            g.procedure_is_active = (unsigned char)(entry->state.id == e->pub.fws_active_procedure);
            g.now = in->now;
            win_push_gen(w, &g);
        }
    } else if (!(in->on_ground && in->groundspeed >= 50.0)) {
        /* FWS 1+2 failed, show fallback */
        int other = in->cpiom_a_failed || in->cpiom_b_failed || in->cpiom_d_failed || in->cpiom_e_failed ||
                    in->cpiom_f_failed || in->cpiom_g_failed;
        unsigned id = ((in->cpiom_c_failed || (!in->fws_avail && in->fcdc12_failed)) && !other) ? 314800003u
                                                                                               : 314800004u;
        FwsChecklistState st;
        FwsProcLines g;
        int i;
        memset(&st, 0, sizeof(st));
        st.id = id;
        st.procedure_activated = 1;
        for (i = 0; i < 10; ++i) st.items_active[i] = st.items_to_show[i] = 1; /* ten-entry arrays */
        fws_lines_init(&g, id, FWS_PROC_FWS_FAILED_FALLBACK, &st, 0, FWS_RECOMMENDATION_NONE, 0);
        g.procedure_is_active = 0;
        g.now = in->now;
        win_push_gen(w, &g);
    }
}

/* WdAbnormalNonSensed.tsx removeUnderline: text.replace(/\x1b4m[\s\S]*\x1bm /gm, '') (greedy) */
static void remove_underline(char *out, size_t size, const char *text)
{
    const char *start = strstr(text, "\x1b" "4m"), *end = NULL, *p;
    if (start) {
        for (p = start + 3; (p = strstr(p, "\x1bm ")) != NULL; ++p) end = p;
    }
    if (!start || !end) {
        fws_js_prefix(out, size, text, (size_t)-1);
        return;
    }
    out[0] = 0;
    if ((size_t)(start - text) < size) {
        memcpy(out, text, (size_t)(start - text));
        out[start - text] = 0;
    }
    cat(out, size, end + 3);
}

static void compose_non_sensed(const FwsEcl *e, Window *w) /* WdAbnormalNonSensedProcedures.updateChecklists */
{
    unsigned id = e->pub.fws_abn_non_sensed_id;
    int activated = e->pub.fws_abn_non_sensed_current_active;
    char buf[FWS_TEXT_MAX], title[FWS_TEXT_MAX];
    FwsWdLine l;
    int i;
    if (id == 0) {
        int num_null = ns_count_category(NULL), num_items = num_null + FWS_ABN_SUB_MENU_COUNT;
        l = wd_line(1, 1, 1, 0, "\x1b" "4mABNORMAL PROC\x1bm", FWS_STYLE_HEADLINE, 1, 0);
        win_push(w, &l);
        for (i = 0; i < FWS_ABN_NON_SENSED_OVERVIEW_COUNT; ++i) {
            const FwsProcedureDef *proc = abn_proc(fws_abn_non_sensed_overview[i].id);
            if (fws_abn_non_sensed_overview[i].category != NULL) continue;
            remove_underline(title, sizeof(title), proc && proc->title ? proc->title : "");
            strcpy(buf, NBSP NBSP);
            cat(buf, sizeof(buf), title);
            l = wd_line(1, 1, 1, 0, buf, FWS_STYLE_HEADLINE, 0, i == num_items - 1);
            line_index(&l, i);
            win_push(w, &l);
        }
        for (i = 0; i < FWS_ABN_SUB_MENU_COUNT; ++i) {
            strcpy(buf, NBSP NBSP);
            cat(buf, sizeof(buf), fws_abn_sub_menu_vector[i]);
            l = wd_line(1, 1, 1, 0, buf, FWS_STYLE_HEADLINE, 0, i == FWS_ABN_SUB_MENU_COUNT - 1);
            line_index(&l, i + num_null);
            win_push(w, &l);
        }
    } else if (id <= 10) {
        const char *category = id <= FWS_ABN_SUB_MENU_COUNT ? fws_abn_sub_menu_vector[id - 1] : "undefined";
        int n = 0, num_items = ns_count_category(category);
        l = wd_line(1, 1, 1, 0, "\x1b" "4mABNORMAL PROC\x1bm", FWS_STYLE_HEADLINE, 1, 0);
        win_push(w, &l);
        strcpy(buf, NBSP NBSP "\x1b" "4m");
        cat(buf, sizeof(buf), category);
        cat(buf, sizeof(buf), "\x1bm");
        l = wd_line(1, 1, 1, 0, buf, FWS_STYLE_HEADLINE, 0, 0);
        win_push(w, &l);
        for (i = 0; i < FWS_ABN_NON_SENSED_OVERVIEW_COUNT; ++i) {
            const FwsProcedureDef *proc;
            if (!same_category(fws_abn_non_sensed_overview[i].category, category)) continue;
            proc = abn_proc(fws_abn_non_sensed_overview[i].id);
            remove_underline(title, sizeof(title), proc && proc->title ? proc->title : "");
            strcpy(buf, NBSP NBSP NBSP NBSP);
            cat(buf, sizeof(buf), title);
            l = wd_line(1, 1, 1, 0, buf, FWS_STYLE_HEADLINE, 0, n == num_items - 1);
            line_index(&l, n);
            win_push(w, &l);
            ++n;
        }
    } else {
        const FwsProcedureDef *abn = abn_proc(id);
        if (!abn) return;
        l = wd_line(1, 1, 1, 0, abn->title, FWS_STYLE_HEADLINE, 1, 0);
        win_push(w, &l);
        buf[0] = 0;
        for (i = 0; i < 31; ++i) cat(buf, sizeof(buf), NBSP);
        cat(buf, sizeof(buf), "ACTIVATE");
        l = wd_line(1, 1, 0, activated, buf, FWS_STYLE_CHECKLIST_ITEM, 0, abn->num_items == 0);
        line_index(&l, -1);
        win_push(w, &l);
        for (i = 0; i < abn->num_items; ++i) {
            const FwsChecklistItem *item = &abn->items[i];
            char text[256];
            int k;
            text[0] = 0;
            for (k = 0; k < item->level; ++k) cat(text, sizeof(text), NBSP);
            if (fws_item_is_action(item)) {
                cat(text, sizeof(text), "-");
                cat(text, sizeof(text), item->name);
                if (*item->label_not_completed) { /* Pad to 39 characters max */
                    long padding = 39 - (long)(fws_js_length(item->label_not_completed) + fws_js_length(item->name) +
                                               item->level + 2);
                    cat(text, sizeof(text), " ");
                    for (; padding > 0; --padding) cat(text, sizeof(text), ".");
                    cat(text, sizeof(text), item->label_not_completed);
                }
            } else if (fws_item_is_condition(item)) {
                if (strncmp(item->name, "WHEN", 4) == 0) {
                    cat(text, sizeof(text), ".");
                    cat(text, sizeof(text), item->name);
                } else {
                    cat(text, sizeof(text), ".IF ");
                    cat(text, sizeof(text), strncmp(item->name, "IF", 2) == 0 ? item->name + 2 : item->name);
                }
            } else {
                cat(text, sizeof(text), item->name);
            }
            l = wd_line(1, 1, item->sensed, 0, "", item->style >= 0 ? item->style : FWS_STYLE_CHECKLIST_ITEM, 0,
                        i == abn->num_items - 1);
            fws_js_prefix(l.text, sizeof(l.text), text, 39);
            line_index(&l, i + 1);
            l.inactive = (unsigned char)!activated;
            win_push(w, &l);
        }
    }
}

/* WdCpiomFailedFallbackChecklistComponent.tsx:39-114 */
static void compose_cpiom_fallback(const FwsEclEwdInput *in, FwsEclFallbackLine *out)
{
    static const struct {
        const char *text;
        unsigned char style;
        signed char cpiom; /* -1 always visible, else 'A'..'G' */
    } rows[FWS_ECL_CPIOM_FALLBACK_LINES] = {
        {"\x1b<4m\x1b" "4mFWS\x1bm FWS 1+2 & CPIOM FAULT", FWS_STYLE_HEADLINE, -1},
        {"-DESCENT TO FL 100/MEA ................INITIATE", FWS_STYLE_CYAN, 'A'},
        {"CAB PRESS MAN MODES : DO NOT USE", FWS_STYLE_CYAN, 'B'},
        {"IN DES,ABV 7000FT: CAB ALT REGULATED TO 7000FT", FWS_STYLE_CYAN, 'B'},
        {"BLW 7000FT: CAB ALT=A/C ALT: AVOID HI DES RATE", FWS_STYLE_CYAN, 'B'},
        {"-RAM AIR ....................................ON", FWS_STYLE_CYAN, 'A'},
        {"-ALL CROSSFEEDS .............................ON", FWS_STYLE_CYAN, 'F'},
        {"-OUTR TK XFR....MAN / EMER OUTR TK XFR", FWS_STYLE_CYAN, 'F'},
        {"INR TKS + MID TKS + TRIM TK:NOT USABLE", FWS_STYLE_CYAN, 'F'},
        {"ATC COM VOICE ONLY", FWS_STYLE_CYAN, 'D'},
        {"L/G GRVTY EXTN ONLY (MAX SPEED: 220KT)", FWS_STYLE_CYAN, 'G'},
        {"-LDG DIST: COMPUTE", FWS_STYLE_CYAN, -1},
        {"-ECAM SD & OVHD PNL ....................MONITOR", FWS_STYLE_CYAN, -1},
        {NBSP NBSP NBSP "FUNCTIONS NOT AVAIL:", FWS_STYLE_WHITE, -1},
        {NBSP NBSP NBSP "ENG BLEED 1+2+3+4 / WING A-ICE", FWS_STYLE_AMBER, 'A'},
        {NBSP NBSP NBSP "FQMS 1+2 / JETTISON", FWS_STYLE_AMBER, 'F'},
        {NBSP NBSP NBSP "LG CTL 1+2 / A-SKID / N/W + B/W STEER", FWS_STYLE_AMBER, 'G'},
        {NBSP NBSP NBSP "FWD + AFT VENT CTL / TEMP CTL", FWS_STYLE_AMBER, 'B'},
        {NBSP NBSP NBSP "ECAM WARNINGS & CAUTIONS / ABN PROCEDURES", FWS_STYLE_AMBER, -1},
        {NBSP NBSP NBSP "LIMITATIONS & STATUS / NORM C/L & MEMOS", FWS_STYLE_AMBER, -1},
        {NBSP NBSP NBSP "ALT ALERT & AUTO CALLOUT / F/CTL INDICATIONS", FWS_STYLE_AMBER, -1},
    };
    int i;
    for (i = 0; i < FWS_ECL_CPIOM_FALLBACK_LINES; ++i) {
        int visible = 1;
        switch (rows[i].cpiom) {
        case 'A': visible = in->cpiom_a_failed; break;
        case 'B': visible = in->cpiom_b_failed; break;
        case 'D': visible = in->cpiom_d_failed; break;
        case 'F': visible = in->cpiom_f_failed; break;
        case 'G': visible = in->cpiom_g_failed; break;
        default: break;
        }
        out[i].text = rows[i].text;
        out[i].style = rows[i].style;
        out[i].visible = (unsigned char)(visible != 0);
    }
}

void fws_ecl_compose_wd(const FwsEcl *e, const FwsEclCoreLists *lists, const FwsEclEwdInput *in, FwsEclWd *out)
{
    Window w;
    int fws_failed = !in->fws_avail, i;
    int other_cpiom = in->cpiom_a_failed || in->cpiom_b_failed || in->cpiom_d_failed || in->cpiom_e_failed ||
                      in->cpiom_f_failed || in->cpiom_g_failed;
    int cpiom_fallback = in->cpiom_c_failed && other_cpiom;                              /* EWD.tsx:78-82 */
    int normal_visible = e->pub.fws_show_normal_checklists && in->fws_avail;            /* :72-76 */
    int abn_visible = (e->pub.fws_show_abn_sensed || fws_failed) && !cpiom_fallback;    /* :85-90 */
    int non_sensed_visible = e->pub.fws_show_abn_non_sensed && in->fws_avail;           /* :93-97 */

    memset(out, 0, sizeof(*out));
    out->memos_limitations_visible = (unsigned char)!(e->pub.fws_show_normal_checklists || e->pub.fws_show_abn_sensed ||
                                                      e->pub.fws_show_abn_non_sensed || fws_failed); /* :123-129 */
    out->sts_area_visible = (unsigned char)!(abn_visible || non_sensed_visible);                       /* :99-103 */
    out->view = cpiom_fallback       ? FWS_ECL_VIEW_CPIOM_FALLBACK
                : abn_visible        ? FWS_ECL_VIEW_ABN_SENSED
                : normal_visible     ? FWS_ECL_VIEW_NORMAL
                : non_sensed_visible ? FWS_ECL_VIEW_ABN_NON_SENSED
                                     : FWS_ECL_VIEW_NONE;
    compose_cpiom_fallback(in, out->cpiom_fallback);

    out->show_from_line = e->pub.fws_show_from_line;
    w.out = out;
    w.active_line = e->pub.fws_active_item;
    w.index = 0;
    w.count = 0;
    if (out->view == FWS_ECL_VIEW_NORMAL) compose_normal(e, lists, in->now, &w);
    else if (out->view == FWS_ECL_VIEW_ABN_SENSED) compose_abnormal(e, lists, in, &w);
    else if (out->view == FWS_ECL_VIEW_ABN_NON_SENSED) compose_non_sensed(e, &w);

    out->total_lines = w.index;
    for (i = w.count; i < FWS_WD_NUM_LINES; ++i) { /* Fill remaining lines blank */
        out->lines[i] = wd_line(0, 0, 1, 0, "", FWS_STYLE_CHECKLIST_ITEM, 1, 1);
        out->lines[i].special = FWS_SPECIAL_EMPTY;
        out->line_selected[i] = 0;
    }
    out->selected_line = -1;
    for (i = 0; i < FWS_WD_NUM_LINES; ++i) {
        if (out->line_selected[i]) {
            out->selected_line = i;
            break;
        }
    }
    out->overflow_up = (unsigned char)(out->show_from_line > 0);
    out->overflow_down = (unsigned char)(out->total_lines > FWS_WD_NUM_LINES &&
                                         out->total_lines - FWS_WD_NUM_LINES > out->show_from_line);
}
