/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native FWS core: FwsCore.ts (pinned FlyByWire A380X revision, a380x_fws_tables.json) update()
 * alert life cycle, lines 5468-6155, on top of a380x_fws_signals (conditions) and a380x_fws_ecl
 * (ECL). Line references below are FwsCore.ts. Port deviations:
 *  - Activated abnormal NON-sensed procedures (ABN PROC) take part in the loop like FBW's
 *    ewdAbnormalNonSensed entries, but with FBW's default item vectors (all shown, none checked,
 *    all active), failure 1, no SD page, no phase inhibition and without the 0.6 s monitor delay;
 *    FBW's per-procedure conditional items of those ~30 crew-selected procedures are not ported.
 *  - The startup self-test delay (CONFIG_SELF_TEST_TIME) is not modelled: the FWS starts when powered.
 *  - eng1Or2TakeoffPower / eng3Or4TakeoffPower (ECL deferred auto-display) = any TLA >= 35 deg.
 * XPLM-free.
 */
#include <math.h>
#include <string.h>
#include "a380x_fws.h"

static int fws_has(const FwsEclKeyList *list, unsigned key) { return fws_ecl_keys_index(list, key) >= 0; }

void fws_init(Fws *f)
{
    memset(f, 0, sizeof(*f));
    fws_signals_init(&f->sig);
    fws_ecl_init(&f->ecl);
    fws_ecl_lists_init(&f->lists);
    fws_ecl_input_init(&f->ecl_in);
    f->sfail = -1;
    f->status.ecam_status_normal = 1;
}

/* One alert of the merged FBW ewdAbnormal dictionary ({...ewdAbnormalSensed, ...ewdAbnormalNonSensed}:
 * integer-like keys, so Object.entries is ascending id). */
typedef struct {
    unsigned id;
    int sig;                 /* signals descriptor index, -1 = activated abnormal non-sensed */
} FwsAlertRef;

static int fws_ref_cmp(const void *a, const void *b)
{
    unsigned x = ((const FwsAlertRef *)a)->id, y = ((const FwsAlertRef *)b)->id;
    return x < y ? -1 : x > y;
}

static int fws_build_refs(const Fws *f, FwsAlertRef *refs, int max)
{
    int n = 0, i, count = fws_signals_alert_count();
    for (i = 0; i < count && n < max; ++i) {
        const FwsAlertDesc *d = fws_signals_alert_desc(i);
        if (d && !d->deferred) { refs[n].id = d->id; refs[n].sig = i; ++n; }
    }
    for (i = 0; i < f->lists.active_abnormal_non_sensed.count && n < max; ++i) {
        unsigned id = f->lists.active_abnormal_non_sensed.keys[i];
        if (fws_signals_alert_index(id) >= 0) continue;   /* a sensed entry with the same id wins */
        refs[n].id = id; refs[n].sig = -1; ++n;
    }
    /* insertion sort: n is small and the sensed part is already ascending */
    for (i = 1; i < n; ++i) {
        FwsAlertRef v = refs[i];
        int j = i - 1;
        while (j >= 0 && fws_ref_cmp(&refs[j], &v) > 0) { refs[j + 1] = refs[j]; --j; }
        refs[j + 1] = v;
    }
    return n;
}

static int fws_ref_confirmed(const Fws *f, const FwsAlertRef *r)
{
    if (r->sig >= 0) return fws_signals_alert_confirmed(&f->sig, r->sig);
    return fws_has(&f->lists.active_abnormal_non_sensed, r->id);
}

/* Evaluation of one alert (non-sensed: FBW default vectors). */
static void fws_ref_eval(const Fws *f, const FwsAlertRef *r, const FwsProcedureDef *proc, FwsAlertEval *e)
{
    int i, n = proc ? proc->num_items : 0;
    if (r->sig >= 0) { fws_signals_alert_eval(&f->sig, r->sig, e); return; }
    memset(e, 0, sizeof(*e));
    e->active = 1;
    e->n_items = (unsigned char)(n > FWS_MAX_ITEMS ? FWS_MAX_ITEMS : n);
    for (i = 0; i < e->n_items; ++i) e->items_to_show[i] = 1;
}

/* Is this deferred procedure (signals index) triggered by abnormal procedure id? */
static int fws_deferred_from(const FwsProcedureDef *dproc, unsigned id)
{
    unsigned k;
    for (k = 0; dproc && k < dproc->num_from_abnormal_procs; ++k) if (dproc->from_abnormal_procs[k] == id) return 1;
    return 0;
}

static void fws_state_arrays(FwsChecklistState *st, unsigned id, int n, const unsigned char *checked,
                             const unsigned char *show, const unsigned char *active,
                             const FwsAlertEval *e)
{
    int i;
    memset(st, 0, sizeof(*st));
    st->id = id;
    for (i = 0; i < n && i < FWS_MAX_ITEMS; ++i) {
        st->items_checked[i] = checked[i]; st->items_to_show[i] = show[i]; st->items_active[i] = active[i];
        if (e && e->has_items_timer) {
            st->timestamp_valid[i] = e->timer_valid[i];
            st->items_timestamp[i] = e->items_timer_s[i];
        }
    }
}

/* MASTER CAUT/WARN buttons (5475-5482); requestMasterWarningFromApOff is owned by the signals. */
static void fws_master_buttons(Fws *f, const FwsInputs *in, int *caution_pressed)
{
    int caution = in->master_caution_pb && !f->master_caution_pb_last;
    int warning = in->master_warning_pb && !f->master_warning_pb_last;
    f->master_caution_pb_last = in->master_caution_pb;
    f->master_warning_pb_last = in->master_warning_pb;
    if (caution) f->request_master_caution_from_faults = 0;
    if (warning) {
        f->request_master_warning_from_faults = f->non_cancellable_warning_count > 0;
        f->aural_crc_active = f->non_cancellable_warning_count > 0;
    }
    *caution_pressed = caution;
}

static void fws_fill_ecl_input(Fws *f, const FwsInputs *in, const FwsCycleInput *cycle)
{
    int k, i;
    FwsEclInput *e = &f->ecl_in;
    unsigned char qnh_set, slats_extended;
    e->now = f->clock;
    e->flight_phase = f->out.flight_phase;
    e->aircraft_on_ground = f->out.on_ground;
    e->ecam_status_normal = f->status.ecam_status_normal;
    e->adr_pressure_altitude = in->adr_alt_valid[0] ? in->adr_alt_ft[0] : 0.0;
    /* approachAutoDisplayQnhSetPulseNode / ...SlatsExtendedPulseNode (6153-6154): rising edges */
    qnh_set = !cycle->baro_std;
    slats_extended = in->flaps_handle_index > 0;
    e->approach_auto_display_pulse = (unsigned char)((qnh_set && !f->baro_std_last) ||
                                                     (slats_extended && !f->flaps_handle_last));
    f->baro_std_last = qnh_set;
    f->flaps_handle_last = slats_extended;
    e->takeoff_power = 0;
    if (in->tla_valid) for (i = 0; i < 4; ++i) if (in->tla_deg[i] >= 35.0f) e->takeoff_power = 1;
    for (k = 0; k < FWS_NORMAL_PROCEDURE_COUNT; ++k) {
        signed char sensed[FWS_MAX_ITEMS];
        int n = fws_signals_normal_sensed(&f->sig, fws_normal_procedures[k].id, sensed, FWS_MAX_ITEMS);
        for (i = 0; i < FWS_MAX_ITEMS; ++i) e->normal_sensed[k][i] = i < n ? sensed[i] : -1;
    }
}

/* The abnormal procedures loop and its bookkeeping (5538-5930). */
static void fws_life_cycle(Fws *f, int caution_pressed, float dt)
{
    static FwsAlertRef refs[FWS_SIG_MAX_ALERTS + FWS_ECL_MAX_KEYS];
    static FwsActiveAlertRef sts_refs[FWS_SIG_MAX_ALERTS + FWS_ECL_MAX_KEYS];
    static unsigned char sts_checked[FWS_SIG_MAX_ALERTS + FWS_ECL_MAX_KEYS][FWS_MAX_ITEMS];
    FwsEclKeyList failure_keys = f->lists.presented_failures, recall_keys = f->lists.recall_failures;
    FwsEclKeyList all_failure_keys, crc_keys, deferred_keys;
    int n_refs = fws_build_refs(f, refs, (int)(sizeof(refs) / sizeof(refs[0])));
    int phase = f->out.flight_phase, failure_system_count = 0, new_sc = 0, any_sc = 0, n_sts = 0, r, i;
    int count = fws_signals_alert_count();
    memset(&all_failure_keys, 0, sizeof(all_failure_keys));
    memset(&crc_keys, 0, sizeof(crc_keys));
    memset(&deferred_keys, 0, sizeof(deferred_keys));

    /* Update memos and failures list in case failure has been resolved (5605-5613) */
    for (i = 0; i < failure_keys.count;) {
        unsigned id = failure_keys.keys[i];
        int confirmed = 0;
        for (r = 0; r < n_refs; ++r) if (refs[r].id == id) { confirmed = fws_ref_confirmed(f, &refs[r]); break; }
        if (!confirmed) fws_ecl_keys_splice(&failure_keys, i); else ++i;
    }
    for (i = 0; i < recall_keys.count;) {
        unsigned id = recall_keys.keys[i];
        int confirmed = 0;
        for (r = 0; r < n_refs; ++r) if (refs[r].id == id) { confirmed = fws_ref_confirmed(f, &refs[r]); break; }
        if (!confirmed) fws_ecl_keys_splice(&recall_keys, i); else ++i;
    }
    f->lists.recall_failures = recall_keys;
    f->non_cancellable_warning_count = 0;
    for (i = 0; i < f->lists.presented_abnormal.count; ++i) {
        f->lists.presented_abnormal.entries[i].has_updated_items = 0;
        memset(f->lists.presented_abnormal.entries[i].updated_items, 0, FWS_MAX_ITEMS);
    }

    for (r = 0; r < n_refs; ++r) {
        const FwsAlertRef *ref = &refs[r];
        const FwsAlertDesc *desc = ref->sig >= 0 ? fws_signals_alert_desc(ref->sig) : NULL;
        const FwsProcedureDef *proc = fws_lines_find_procedure(FWS_PROC_ABNORMAL, ref->id);
        int is_procedure = desc ? !desc->non_procedure : 1;
        int failure = desc ? desc->failure : 1;
        int new_warning = is_procedure ? (!fws_has(&f->lists.presented_failures, ref->id) &&
                                          !fws_has(&recall_keys, ref->id))
                                       : !fws_has(&f->lists.all_current_failures, ref->id);
        int has_aural = desc && desc->has_aural;
        FwsAlertEval eval;
        int evaluated = 0;
        if (!proc && is_procedure) continue;                 /* "Procedure of id does not exist" */
        if (has_aural) { fws_ref_eval(f, ref, proc, &eval); evaluated = 1; }
        if (!(new_warning && desc && (desc->phase_inhib & FWS_PHASE_BIT(phase)))) {
            if (fws_ref_confirmed(f, ref)) {
                if (new_warning) {
                    if (is_procedure) fws_ecl_keys_push(&failure_keys, ref->id);
                    if (failure == 3) f->request_master_warning_from_faults = 1;
                    if (failure == 2) f->request_master_caution_from_faults = 1;
                }
                if (is_procedure) {
                    int n = proc->num_items > FWS_MAX_ITEMS ? FWS_MAX_ITEMS : proc->num_items, k;
                    unsigned char checked[FWS_MAX_ITEMS] = {0}, show[FWS_MAX_ITEMS], active[FWS_MAX_ITEMS];
                    const double *timer = NULL;
                    const unsigned char *timer_valid = NULL;
                    FwsEclProcEntry *prev, *prev_cleared;
                    if (!evaluated) { fws_ref_eval(f, ref, proc, &eval); evaluated = 1; }
                    for (k = 0; k < n; ++k) {
                        checked[k] = proc->items[k].sensed ? (eval.items_checked[k] != 0) : 0;
                        show[k] = k < eval.n_items ? eval.items_to_show[k] : 1;
                        active[k] = eval.has_items_active ? eval.items_active[k] : 1;
                    }
                    if (eval.has_items_timer) { timer = eval.items_timer_s; timer_valid = eval.timer_valid; }
                    fws_lines_conditional_active_items(proc, checked, active, timer, timer_valid, show, f->clock);
                    prev = fws_ecl_map_get(&f->lists.presented_abnormal, ref->id);
                    prev_cleared = fws_ecl_map_get(&f->lists.cleared_abnormal, ref->id);
                    if (!prev && !prev_cleared) {
                        FwsChecklistState st;
                        FwsEclProcEntry *entry;
                        int d;
                        fws_state_arrays(&st, ref->id, n, checked, show, active, &eval);
                        st.procedure_activated = 1;
                        entry = fws_ecl_map_set(&f->lists.presented_abnormal, &st, eval.has_items_timer);
                        if (entry) entry->failure = (unsigned char)(failure > 0 ? failure : 0);
                        /* deferred procedures triggered by this alert (5695-5725) */
                        for (d = 0; d < count; ++d) {
                            const FwsAlertDesc *dd = fws_signals_alert_desc(d);
                            const FwsProcedureDef *dproc;
                            if (!dd || !dd->deferred || !fws_signals_alert_active(&f->sig, d)) continue;
                            dproc = fws_lines_find_procedure(FWS_PROC_DEFERRED, dd->id);
                            if (dproc && fws_deferred_from(dproc, ref->id)) {
                                FwsAlertEval de;
                                FwsChecklistState ds;
                                unsigned char dchecked[FWS_MAX_ITEMS], dshow[FWS_MAX_ITEMS], dactive[FWS_MAX_ITEMS];
                                int dn = dproc->num_items > FWS_MAX_ITEMS ? FWS_MAX_ITEMS : dproc->num_items, m;
                                FwsEclProcEntry *de_entry;
                                fws_signals_alert_eval(&f->sig, d, &de);
                                for (m = 0; m < dn; ++m) {
                                    dactive[m] = 0;
                                    dchecked[m] = de.items_checked[m] != 0;
                                    dshow[m] = m < de.n_items ? de.items_to_show[m] : 1;
                                }
                                fws_lines_conditional_active_items(dproc, dchecked, dactive, NULL, NULL, dshow, f->clock);
                                fws_state_arrays(&ds, dd->id, dn, dchecked, dshow, dactive, NULL);
                                de_entry = fws_ecl_map_set(&f->lists.active_deferred, &ds, 0);
                                if (de_entry) {
                                    de_entry->has_which_items_active = de.has_items_active;
                                    memcpy(de_entry->which_items_active, de.items_active, FWS_MAX_ITEMS);
                                    de_entry->which_items_checked_length = (unsigned char)dn;
                                }
                            }
                        }
                    } else if (prev) {
                        unsigned char fused[FWS_MAX_ITEMS], updated[FWS_MAX_ITEMS] = {0};
                        int any = 0;
                        for (k = 0; k < n; ++k)
                            fused[k] = proc->items[k].sensed ? checked[k] : prev->state.items_checked[k];
                        fws_lines_conditional_active_items(proc, fused, active, timer, timer_valid, show, f->clock);
                        for (k = 0; k < n; ++k) {
                            if (prev->state.items_to_show[k] != show[k] || prev->state.items_active[k] != active[k] ||
                                (prev->state.items_checked[k] != fused[k] && proc->items[k].sensed) ||
                                (timer && prev->timestamps_defined &&
                                 (prev->state.timestamp_valid[k] != timer_valid[k] ||
                                  (timer_valid[k] && prev->state.items_timestamp[k] != timer[k])))) {
                                updated[k] = 1; any = 1;
                            }
                        }
                        prev->has_updated_items = 1;            /* abnormalUpdatedItems.set(key, []) */
                        memcpy(prev->updated_items, updated, FWS_MAX_ITEMS);
                        if (any) {
                            FwsChecklistState st = prev->state;
                            for (k = 0; k < n; ++k) {
                                st.items_checked[k] = fused[k];
                                st.items_active[k] = active[k];
                                st.items_to_show[k] = show[k];
                                if (prev->timestamps_defined) {
                                    st.timestamp_valid[k] = timer ? timer_valid[k] : 0;
                                    st.items_timestamp[k] = timer && timer_valid[k] ? timer[k] : 0.0;
                                }
                            }
                            fws_ecl_map_set(&f->lists.presented_abnormal, &st, prev->timestamps_defined);
                        }
                    }
                }
                if (desc && !desc->cancel && failure == 3) ++f->non_cancellable_warning_count;
                if (!has_aural && failure == 3) {
                    if (new_warning) f->aural_crc_active = 1;
                    fws_ecl_keys_push(&crc_keys, ref->id);
                }
                if (!has_aural && failure == 2) {
                    if (new_warning) new_sc = 1;
                    any_sc = 1;
                }
                if (is_procedure) {
                    const FwsEclProcEntry *pe = fws_ecl_map_find(&f->lists.presented_abnormal, ref->id);
                    const FwsEclProcEntry *ce = fws_ecl_map_find(&f->lists.cleared_abnormal, ref->id);
                    const FwsEclProcEntry *src = pe ? pe : ce;
                    fws_ecl_keys_push(&all_failure_keys, ref->id);
                    if (n_sts < (int)(sizeof(sts_refs) / sizeof(sts_refs[0]))) {
                        sts_refs[n_sts].id = ref->id;
                        if (src) {
                            memcpy(sts_checked[n_sts], src->state.items_checked, FWS_MAX_ITEMS);
                            sts_refs[n_sts].checked = sts_checked[n_sts];
                            sts_refs[n_sts].n_checked = proc->num_items;
                        } else { sts_refs[n_sts].checked = NULL; sts_refs[n_sts].n_checked = 0; }
                        ++n_sts;
                    }
                    if (!fws_has(&recall_keys, ref->id) && desc && desc->sys_page > -1) ++failure_system_count;
                } else fws_ecl_keys_push(&all_failure_keys, ref->id);
            }
        }
        /* auralWarning overrides (5815-5831) */
        if (has_aural) {
            if (eval.aural == FWS_AURAL_CRC) {
                if (!fws_has(&f->aural_crc_keys, ref->id)) f->aural_crc_active = 1;
                fws_ecl_keys_push(&crc_keys, ref->id);
            }
            if (eval.aural == FWS_AURAL_SINGLE_CHIME) {
                if (new_warning) new_sc = 1;
                any_sc = 1;
            }
        }
    }

    /* Update deferred procedures (5834-5875) */
    for (i = 0; i < f->lists.active_deferred.count; ++i) {
        FwsEclProcEntry *entry = &f->lists.active_deferred.entries[i];
        const FwsProcedureDef *dproc = fws_lines_find_procedure(FWS_PROC_DEFERRED, entry->state.id);
        int d = fws_signals_alert_index(entry->state.id), n, k, any = 0;
        FwsAlertEval de;
        unsigned char checked[FWS_MAX_ITEMS], show[FWS_MAX_ITEMS], active[FWS_MAX_ITEMS], fused[FWS_MAX_ITEMS];
        if (!dproc || d < 0) continue;
        n = dproc->num_items > FWS_MAX_ITEMS ? FWS_MAX_ITEMS : dproc->num_items;
        fws_signals_alert_eval(&f->sig, d, &de);
        for (k = 0; k < n; ++k) {
            checked[k] = dproc->items[k].sensed ? de.items_checked[k] != 0 : 0;
            show[k] = k < de.n_items ? de.items_to_show[k] : 1;
            active[k] = de.has_items_active ? (entry->state.procedure_activated ? de.items_active[k] : 0)
                                            : entry->state.procedure_activated;
            fused[k] = dproc->items[k].sensed ? checked[k] : entry->state.items_checked[k];
        }
        fws_lines_conditional_active_items(dproc, fused, active, NULL, NULL, show, f->clock);
        for (k = 0; k < n; ++k)
            if (entry->state.items_to_show[k] != show[k] || entry->state.items_active[k] != active[k] ||
                (entry->state.items_checked[k] != fused[k] && dproc->items[k].sensed)) any = 1;
        entry->has_which_items_active = de.has_items_active;
        memcpy(entry->which_items_active, de.items_active, FWS_MAX_ITEMS);
        entry->which_items_checked_length = (unsigned char)n;
        if (any) {
            FwsChecklistState st = entry->state;
            for (k = 0; k < n; ++k) { st.items_checked[k] = fused[k]; st.items_active[k] = active[k]; st.items_to_show[k] = show[k]; }
            fws_ecl_map_set(&f->lists.active_deferred, &st, 0);
        }
    }
    /* Retrieve all active deferred procedure keys, delete inactive (5877-5893) */
    for (i = 0; i < all_failure_keys.count; ++i) {
        int d;
        for (d = 0; d < count; ++d) {
            const FwsAlertDesc *dd = fws_signals_alert_desc(d);
            if (dd && dd->deferred && fws_signals_alert_active(&f->sig, d) &&
                fws_deferred_from(fws_lines_find_procedure(FWS_PROC_DEFERRED, dd->id), all_failure_keys.keys[i]))
                fws_ecl_keys_push(&deferred_keys, dd->id);
        }
    }
    for (i = 0; i < f->lists.active_deferred.count;) {
        unsigned id = f->lists.active_deferred.entries[i].state.id;
        if (!fws_has(&deferred_keys, id)) fws_ecl_map_delete(&f->lists.active_deferred, id); else ++i;
    }
    /* Delete inactive failures from internal map (5895-5913) */
    for (i = 0; i < f->lists.presented_abnormal.count;) {
        FwsEclProcEntry *e = &f->lists.presented_abnormal.entries[i];
        unsigned id = e->state.id;
        if (!fws_has(&all_failure_keys, id) || fws_has(&f->lists.recall_failures, id)) {
            if (fws_has(&f->lists.recall_failures, id)) {
                FwsEclProcEntry *c = fws_ecl_map_set(&f->lists.cleared_abnormal, &e->state, e->timestamps_defined);
                if (c) c->failure = e->failure;
            } else fws_ecl_map_delete(&f->lists.cleared_abnormal, id);
            fws_ecl_map_delete(&f->lists.presented_abnormal, id);
        } else ++i;
    }
    for (i = 0; i < f->lists.cleared_abnormal.count;) {
        unsigned id = f->lists.cleared_abnormal.entries[i].state.id;
        if (!fws_has(&all_failure_keys, id)) fws_ecl_map_delete(&f->lists.cleared_abnormal, id); else ++i;
    }
    /* Aurals (5915-5926): singleChimeMtrig = NXLogicTriggeredMonostableNode(2), memory set*. */
    f->aural_crc_keys = crc_keys;
    if (crc_keys.count == 0) f->aural_crc_active = 0;
    {
        /* NXLogicTriggeredMonostableNode(2): rising edge (re)arms a 2 s output when idle. */
        int sc_out, set, reset;
        if (f->single_chime_timer > 0) f->single_chime_timer = fmax(f->single_chime_timer - dt, 0.0);
        if (f->single_chime_timer == 0 && new_sc && !f->single_chime_previous) f->single_chime_timer = 2.0;
        f->single_chime_previous = (unsigned char)new_sc;
        sc_out = f->single_chime_timer > 0;
        /* NXLogicMemoryNode(false).write(set, reset): setStar = false, so reset wins a tie. */
        set = sc_out;
        reset = !sc_out || caution_pressed || !any_sc;
        if (set && reset) f->single_chime_memory = 0;
        else if (set) f->single_chime_memory = 1;
        else if (reset) f->single_chime_memory = 0;
        f->aural_single_chime = f->single_chime_memory;
    }
    f->lists.all_current_failures = all_failure_keys;
    f->lists.presented_failures = failure_keys;

    /* Memos (5935-5985: ordering and priority are in the signals) */
    f->memo_left_count = fws_signals_memos_left(&f->sig, f->memo_left, FWS_MEMO_LINES);
    f->memo_right_count = fws_signals_memos_right(&f->sig, f->memo_right, FWS_MEMO_LINES);
    f->memo_pfd_count = fws_signals_memos_pfd(&f->sig, f->memo_pfd, FWS_PFD_MEMO_LINES);
    /* INOP SYS / INFO / LIMITATIONS + deprecated per-alert lists, LAND ASAP/ANSA (5784-6151) */
    fws_signals_status_lists(&f->sig, sts_refs, n_sts, &f->status);

    /* Reset master caution / warning light if appropriate (6029-6045) */
    {
        int caution = 0, warning = 0;
        for (i = 0; i < all_failure_keys.count; ++i) {
            int idx = fws_signals_alert_index(all_failure_keys.keys[i]);
            const FwsAlertDesc *d = idx >= 0 ? fws_signals_alert_desc(idx) : NULL;
            if (d && d->failure == 2) caution = 1;
            if (d && d->failure == 3) warning = 1;
        }
        if (!caution) f->request_master_caution_from_faults = 0;
        if (!warning && f->non_cancellable_warning_count == 0) f->request_master_warning_from_faults = 0;
    }
    f->master_caution = f->request_master_caution_from_faults;
    f->master_warning = f->request_master_warning_from_faults || f->out.master_warning_from_ap_off;
    f->failure_active = f->lists.presented_failures.count > 0;

    /* L:A32NX_ECAM_SFAIL (6047-6063): sysPage of the highest-priority presented failure */
    if (failure_system_count == 0) f->sfail = -1;
    else {
        int order[FWS_ECL_MAX_PROCS], n = fws_ecl_sorted_abnormal(&f->lists, order);
        if (n > 0) {
            int idx = fws_signals_alert_index(f->lists.presented_abnormal.entries[order[0]].state.id);
            const FwsAlertDesc *d = idx >= 0 ? fws_signals_alert_desc(idx) : NULL;
            if (d && d->sys_page > -1) f->sfail = d->sys_page;
        }
    }
}

void fws_update(Fws *f, const FwsInputs *in, const FwsCycleInput *cycle, int powered, double clock_s, float dt)
{
    FwsInputs sig_in;
    int caution_pressed = 0;
    unsigned events = 0;
    if (!powered) {
        if (f->powered) fws_init(f);
        f->powered = 0;
        return;
    }
    if (!isfinite(dt) || dt < 0.0f) dt = 0.0f;
    f->clock = clock_s;
    if (!f->powered) {
        f->powered = 1;
        fws_ecl_startup_reset(&f->ecl, &f->lists);     /* startupCompleted (2414-2427) */
        f->started = 1;
    }
    f->sd_status_request = 0;
    fws_ecl_buttons(&f->ecl, &cycle->ecp);
    sig_in = *in;
    sig_in.sd_fail_page = f->sfail;                    /* previous cycle's SFAIL */
    fws_signals_update(&f->sig, &sig_in, clock_s, dt);
    fws_signals_outputs(&f->sig, &f->out);
    fws_master_buttons(f, in, &caution_pressed);
    fws_fill_ecl_input(f, in, cycle);
    events |= fws_ecl_clear_and_recall(&f->ecl, &f->lists, &f->ecl_in);
    fws_life_cycle(f, caution_pressed, dt);
    f->sts_indication = (unsigned char)(!f->status.ecam_status_normal && !cycle->sd_status_shown);
    f->ecl_in.ecam_status_normal = f->status.ecam_status_normal;
    events |= fws_ecl_update(&f->ecl, &f->lists, &f->ecl_in);
    if (f->out.checklist_reset_50min) fws_ecl_shutdown_reset(&f->ecl, &f->lists);
    if (events & FWS_ECL_EVENT_SD_STATUS_PAGE) f->sd_status_request = 1;
}

void fws_ewd_view(const Fws *f, EwdWdView *out)
{
    FwsEclEwdInput in;
    FwsEclWd wd;
    int i;
    memset(out, 0, sizeof(*out));
    if (!f->powered) return;
    memset(&in, 0, sizeof(in));
    in.now = f->clock;
    in.fws_avail = 1;
    in.on_ground = f->out.on_ground;
    for (i = 0; i < 3; ++i) if (f->sig.in.ir_gs_valid[i]) { in.groundspeed = f->sig.in.ir_gs_kt[i]; break; }
    fws_ecl_compose_wd(&f->ecl, &f->lists, &in, &wd);
    out->valid = 1;
    out->mode = wd.view == FWS_ECL_VIEW_NONE || wd.view == FWS_ECL_VIEW_CPIOM_FALLBACK ? EWD_WD_MEMOS : EWD_WD_PROCEDURES;
    if (out->mode == EWD_WD_PROCEDURES) {
        memcpy(out->line, wd.lines, sizeof(out->line));
        out->selected_line = wd.selected_line;
        out->overflow_up = wd.overflow_up;
        out->overflow_down = wd.overflow_down;
    } else if (wd.memos_limitations_visible) {
        out->selected_line = -1;
        for (i = 0; i < f->memo_left_count && out->memo_left_count < EWD_WD_MEMO_LINES; ++i)
            if ((out->memo_left[out->memo_left_count] = fws_memo_text(f->memo_left[i]))) ++out->memo_left_count;
        for (i = 0; i < f->memo_right_count && out->memo_right_count < EWD_WD_MEMO_LINES; ++i)
            if ((out->memo_right[out->memo_right_count] = fws_memo_text(f->memo_right[i]))) ++out->memo_right_count;
        for (i = 0; i < (int)f->status.limitations_all_phases.n && out->limit_all_count < EWD_WD_LIMIT_LINES; ++i)
            if ((out->limit_all[out->limit_all_count] = fws_limitation_text(f->status.limitations_all_phases.id[i])))
                ++out->limit_all_count;
        for (i = 0; i < (int)f->status.limitations_appr_ldg.n && out->limit_appr_ldg_count < EWD_WD_LIMIT_LINES; ++i)
            if ((out->limit_appr_ldg[out->limit_appr_ldg_count] = fws_limitation_text(f->status.limitations_appr_ldg.id[i])))
                ++out->limit_appr_ldg_count;
    } else out->selected_line = -1;
    out->sts_area_visible = wd.sts_area_visible;
    out->failure_pending = f->ecl.pub.fws_show_failure_pending;
    out->sts = f->sts_indication;
    out->sts_deferred = f->sts_indication && f->lists.active_deferred.count > 0;
    out->adv = 0;   /* fws_show_adv_indication is never published by the pinned FwsCore */
}
