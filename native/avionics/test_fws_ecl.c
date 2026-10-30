/* SPDX-License-Identifier: GPL-3.0-or-later
 * ECL controller + EWD composition tests. Expected values are derived from the pinned FBW sources:
 *   FNC = systems-host/CpiomC/FlightWarningSystem/FwsNormalChecklists.ts
 *   FAS = .../FwsAbnormalSensed.ts, FANS = .../FwsAbnormalNonSensed.ts, CORE = .../FwsCore.ts
 *   PLG = instruments/src/MsfsAvionicsCommon/EcamMessages/ProcedureLinesGenerator.ts
 *   WNC/WAS/WANS/WAC = instruments/src/EWD/elements/WdNormalChecklists.tsx, WdAbnormalSensedProcedures.tsx,
 *   WdAbnormalNonSensed.tsx, WdAbstractChecklistComponent.tsx
 * The test drives the FWS like FwsCore.update(): buttons -> CLEAR/RECALL -> core list changes -> update.
 */
#include "a380x_fws_ecl.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++checks;                                                                    \
        if (!(cond)) {                                                               \
            ++failures;                                                              \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                            \
    } while (0)

#define CHECK_STR(actual, expected)                                                                     \
    do {                                                                                                \
        const char *a_ = (actual), *e_ = (expected);                                                    \
        ++checks;                                                                                       \
        if (!a_ || strcmp(a_, e_) != 0) {                                                               \
            ++failures;                                                                                 \
            fprintf(stderr, "%s:%d: got \"%s\" expected \"%s\"\n", __FILE__, __LINE__, a_ ? a_ : "(null)", e_); \
        }                                                                                               \
    } while (0)

static FwsEcl ecl;
static FwsEclCoreLists lists;
static FwsEclInput in;
static FwsEclEwdInput ewd;
static FwsEclWd wd;

enum { K_NONE, K_CLR, K_RCL, K_CL, K_CHECK, K_UP, K_DOWN, K_ABN_PROC };

static const char *nb(int n, const char *s)
{
    static char buf[8][256];
    static int slot;
    char *b = buf[slot++ & 7];
    b[0] = 0;
    while (n-- > 0) strcat(b, "\xc2\xa0");
    strcat(b, s);
    return b;
}

static void setup(void)
{
    fws_ecl_init(&ecl);
    fws_ecl_lists_init(&lists);
    fws_ecl_input_init(&in);
    memset(&ewd, 0, sizeof(ewd));
    in.flight_phase = 1;
    in.aircraft_on_ground = 1;
    in.ecam_status_normal = 1;
    ewd.fws_avail = 1;
    ewd.on_ground = 1;
}

/* One FWS cycle (FwsCore.update order) followed by an EWD draw. */
static unsigned cycle(int key)
{
    FwsEcpButtons b;
    unsigned events;
    int i;
    memset(&b, 0, sizeof(b));
    b.clr = key == K_CLR;
    b.rcl = key == K_RCL;
    b.cl = key == K_CL;
    b.check_rh = key == K_CHECK;
    b.up = key == K_UP;
    b.down = key == K_DOWN;
    b.abn_proc = key == K_ABN_PROC;
    fws_ecl_buttons(&ecl, &b);
    events = fws_ecl_clear_and_recall(&ecl, &lists, &in);
    events |= fws_ecl_update(&ecl, &lists, &in);
    /* abnormalUpdatedItems is cleared by the core at the start of its next loop (CORE:5622) */
    for (i = 0; i < lists.presented_abnormal.count; ++i) {
        lists.presented_abnormal.entries[i].has_updated_items = 0;
        memset(lists.presented_abnormal.entries[i].updated_items, 0, FWS_MAX_ITEMS);
    }
    ewd.now = in.now;
    fws_ecl_compose_wd(&ecl, &lists, &ewd, &wd);
    return events;
}

/* press = one cycle with the button held, one with it released (pulse nodes fire on the rising edge) */
static unsigned press(int key)
{
    unsigned events = cycle(key);
    return events | cycle(K_NONE);
}

static FwsChecklistState fresh_state(const FwsProcedureDef *proc, int activated)
{
    FwsChecklistState st;
    int i;
    memset(&st, 0, sizeof(st));
    st.id = proc->id;
    st.procedure_activated = (unsigned char)activated;
    for (i = 0; i < proc->num_items; ++i) st.items_active[i] = st.items_to_show[i] = 1;
    return st;
}

/* FwsCore 5688-5696 + 5644: a new abnormal procedure */
static FwsEclProcEntry *core_present(unsigned id, int failure, int timestamps_defined)
{
    const FwsProcedureDef *proc = fws_abnormal_procedure(id);
    FwsChecklistState st = fresh_state(proc, 1);
    FwsEclProcEntry *entry;
    fws_lines_conditional_active_items(proc, st.items_checked, st.items_active, NULL, NULL, st.items_to_show, in.now);
    entry = fws_ecl_map_set(&lists.presented_abnormal, &st, timestamps_defined);
    entry->failure = (unsigned char)failure;
    fws_ecl_keys_push(&lists.presented_failures, id);
    fws_ecl_keys_push(&lists.all_current_failures, id);
    return entry;
}

/* FwsCore 5729-5762: sensed item update of a presented procedure */
static void core_sense(unsigned id, int item, int checked)
{
    FwsEclProcEntry *entry = fws_ecl_map_get(&lists.presented_abnormal, id);
    entry->state.items_checked[item] = (unsigned char)checked;
    entry->has_updated_items = 1;
    entry->updated_items[item] = 1;
    fws_ecl_map_set(&lists.presented_abnormal, &entry->state, entry->timestamps_defined);
}

/* FwsCore 5897-5908: cleared procedures move to the cleared map */
static void core_move_cleared(void)
{
    int i;
    for (i = lists.presented_abnormal.count - 1; i >= 0; --i) {
        FwsEclProcEntry *entry = &lists.presented_abnormal.entries[i];
        if (fws_ecl_keys_index(&lists.recall_failures, entry->state.id) >= 0) {
            FwsEclProcEntry *c = fws_ecl_map_set(&lists.cleared_abnormal, &entry->state, entry->timestamps_defined);
            c->failure = entry->failure;
            fws_ecl_map_delete(&lists.presented_abnormal, entry->state.id);
        }
    }
}

static void test_normal_flow(void)
{
    setup();
    cycle(K_NONE);
    CHECK(wd.view == FWS_ECL_VIEW_NONE && wd.memos_limitations_visible && wd.sts_area_visible);
    CHECK(ecl.pub.fws_normal_checklists_id == 0 && ecl.selected_line == 0);

    /* C/L: navigateToChecklist(0) + toggle (FNC:410-413), shown by the orchestration (CORE:6175-6186) */
    press(K_CL);
    CHECK(ecl.show_checklist_requested && ecl.checklist_shown && ecl.pub.fws_show_normal_checklists);
    CHECK(wd.view == FWS_ECL_VIEW_NORMAL && !wd.memos_limitations_visible);
    /* WNC:68-115: CHECKLISTS + the 10 non-deferred checklists (no deferred procedure active) */
    CHECK(wd.total_lines == 11);
    CHECK_STR(wd.lines[0].text, "CHECKLISTS");
    CHECK_STR(wd.lines[1].text, "COCKPIT PREPARATION");
    CHECK_STR(wd.lines[6].text, "<<DEPARTURE CHANGE>>");
    CHECK(wd.lines[6].style == FWS_STYLE_COMPLETED_CHECKLIST && wd.lines[6].checked); /* onlyActivatedByRequest */
    CHECK_STR(wd.lines[7].text, "APPROACH");
    CHECK(wd.lines[7].item_index == 9 && wd.lines[10].last_line);
    CHECK(wd.selected_line == 1 && ecl.pub.fws_active_item == 0);
    /* overview itemsToShow of the deferred pages follows hasDeferred after a full update (FNC:518-529) */
    CHECK(ecl.overview_items_to_show[6] == 0 && ecl.overview_items_to_show[9] == 1);

    press(K_DOWN); /* FNC:318-327 */
    CHECK(ecl.selected_line == 1 && wd.selected_line == 2);
    press(K_CHECK); /* FNC:464-466 -> BEFORE START */
    CHECK(ecl.checklist_id == 1000002u && ecl.pub.fws_normal_checklists_id == 1000002u);
    CHECK(ecl.selected_line == 0 && wd.total_lines == 7 && wd.selected_line == 1);
    CHECK_STR(wd.lines[0].text, "BEFORE START");

    press(K_CHECK); /* PLG:309-324: item 0 checked, moveDown */
    CHECK(ecl.normal_state[1].items_checked[0] && ecl.selected_line == 1);
    CHECK_STR(wd.lines[1].text, "-PARKING BRAKE : ____");
    CHECK(wd.lines[1].checked && wd.selected_line == 2);
    press(K_CHECK);
    CHECK(ecl.normal_state[1].items_checked[1] && ecl.selected_line == 2);

    /* BEACON sensed on (FNC:541-542): selected sensed item checked -> moveDown -> C/L COMPLETE (FNC:497-499) */
    in.normal_sensed[1][2] = 1;
    cycle(K_NONE);
    CHECK(ecl.normal_state[1].items_checked[2] && ecl.selected_line == FWS_SPECIAL_INDEX_NORMAL_CL_COMPLETE);
    CHECK_STR(wd.lines[3].text, "-BEACON ON");
    CHECK(wd.selected_line == 4 && wd.lines[4].item_index == FWS_SPECIAL_INDEX_NORMAL_CL_COMPLETE);

    /* C/L COMPLETE: procedureCompletedCallback closes the ECL (FNC:121-124) */
    press(K_CHECK);
    CHECK(ecl.normal_state[1].procedure_completed && !ecl.show_checklist_requested);
    CHECK(wd.view == FWS_ECL_VIEW_NONE && !ecl.pub.fws_show_normal_checklists);

    /* reopen: overview, first incomplete checklist selected (FNC:274-283) */
    press(K_CL);
    CHECK(ecl.checklist_id == 0 && ecl.selected_line == 0 && wd.view == FWS_ECL_VIEW_NORMAL);
    CHECK(wd.lines[2].style == FWS_STYLE_COMPLETED_CHECKLIST && wd.lines[2].checked);

    /* BEFORE START again: completed -> RESET selected (PLG:364-366); RESET resets following checklists */
    press(K_DOWN);
    press(K_CHECK);
    CHECK(ecl.selected_line == FWS_SPECIAL_INDEX_NORMAL_RESET);
    ecl.normal_state[3].items_checked[0] = 1; /* TAXI item checked earlier */
    press(K_CHECK);
    CHECK(!ecl.normal_state[1].procedure_completed && !ecl.normal_state[1].items_checked[0]);
    CHECK(ecl.normal_state[1].items_checked[2]);  /* sensed item kept (PLG:333) */
    CHECK(!ecl.normal_state[3].items_checked[0]); /* reset(index) clears following checklists (FNC:117-120) */
    CHECK(ecl.manual_checklist_reset && ecl.selected_line == 0);
    in.flight_phase = 2; /* FNC:218-224 */
    cycle(K_NONE);
    CHECK(!ecl.manual_checklist_reset);

    /* CLR: checklist -> overview -> closed (CORE:5516-5517, FNC:372-378) */
    press(K_CLR);
    CHECK(ecl.checklist_id == 0 && ecl.show_checklist_requested);
    press(K_CLR);
    CHECK(!ecl.show_checklist_requested && wd.view == FWS_ECL_VIEW_NONE);

    /* takeoff power resets from after DEPARTURE CHANGE (FNC:226-234, 342-363) */
    ecl.normal_state[9].items_checked[0] = 1; /* APPROACH */
    ecl.normal_state[1].items_checked[0] = 1;
    in.takeoff_power = 1;
    cycle(K_NONE);
    CHECK(!ecl.normal_state[9].items_checked[0] && ecl.normal_state[1].items_checked[0]);
}

static void test_abnormal_flow(void)
{
    unsigned events;
    setup();
    in.aircraft_on_ground = 0;
    in.ecam_status_normal = 0;
    in.now = 100.0;
    ewd.on_ground = 0;
    core_present(260800001u, 3, 1); /* APU FIRE, whichItemsTimer -> itemsTimeStamp defined */
    cycle(K_NONE);
    /* FAS:195 active = first sorted, :218 selectFirst; CORE:6188-6198 */
    CHECK(ecl.active_procedure_valid && ecl.active_procedure_id == 260800001u && ecl.abnormal_shown);
    CHECK(ecl.pub.fws_active_procedure == 260800001u && ecl.pub.fws_active_item == 0 && ecl.pub.fws_show_abn_sensed);
    CHECK(ecl.abn[ecl.abn_active].gen.recommendation == FWS_RECOMMENDATION_LAND_ASAP); /* not on ground (FAS:172) */
    CHECK(wd.view == FWS_ECL_VIEW_ABN_SENSED && wd.total_lines == 7 && wd.selected_line == 2);
    CHECK(!wd.sts_area_visible);
    CHECK_STR(wd.lines[1].text, "LAND ASAP");
    CHECK(wd.lines[5].item_index == FWS_SPECIAL_INDEX_CLEAR && wd.lines[5].last_line); /* WAS:67 index === 0 */

    /* APU FIRE P/B pushed: auto-move-down (FAS:315-334) */
    core_sense(260800001u, 0, 1);
    cycle(K_NONE);
    CHECK(ecl.abn_selected_item_index == 1 && ecl.pub.fws_active_item == 1 && wd.selected_line == 3);
    CHECK_STR(wd.lines[2].text, "-APU FIRE P/B : PUSH");

    /* agent discharge timer (PLG:706-717): 30 - floor(112 - 100) = 18 */
    {
        FwsEclProcEntry *e = fws_ecl_map_get(&lists.presented_abnormal, 260800001u);
        e->state.timestamp_valid[1] = 1;
        e->state.items_timestamp[1] = 100.0;
        e->has_updated_items = 1;
        e->updated_items[1] = 1;
        fws_ecl_map_set(&lists.presented_abnormal, &e->state, 1);
    }
    in.now = 112.0;
    cycle(K_NONE);
    CHECK(strstr(wd.lines[3].text, "-APU AGENT AFTER 18 S : ") == wd.lines[3].text);
    CHECK(ecl.abn_selected_item_index == 1); /* not checked: no auto-move */

    core_sense(260800001u, 1, 1);
    cycle(K_NONE);
    CHECK(ecl.abn_selected_item_index == 2);
    core_sense(260800001u, 2, 1);
    cycle(K_NONE);
    CHECK(ecl.abn_selected_item_index == FWS_SPECIAL_INDEX_CLEAR && wd.selected_line == 5);

    /* CLEAR: clearActiveProcedure (FAS:236-261): last failure + STATUS not normal -> SD STS page */
    events = press(K_CHECK);
    CHECK(events & FWS_ECL_EVENT_SD_STATUS_PAGE);
    CHECK(lists.presented_failures.count == 0 && lists.recall_failures.count == 1 &&
          lists.recall_failures.keys[0] == 260800001u);
    core_move_cleared();
    cycle(K_NONE);
    CHECK(!ecl.active_procedure_valid && !ecl.show_abnormal_sensed_requested && !ecl.abnormal_shown);
    CHECK(wd.view == FWS_ECL_VIEW_NONE);
    CHECK(ecl.pub.fws_active_procedure == 260800001u); /* published value persists (bus semantics) */

    /* RCL (CORE:5523-5536): recalled with its checked state, all done -> CLEAR selected */
    press(K_RCL);
    CHECK(lists.presented_abnormal.count == 1 && lists.cleared_abnormal.count == 0 &&
          lists.presented_failures.count == 1);
    CHECK(ecl.abnormal_shown && ecl.abn_selected_item_index == FWS_SPECIAL_INDEX_CLEAR);
    CHECK(wd.view == FWS_ECL_VIEW_ABN_SENSED && wd.lines[wd.selected_line].item_index == FWS_SPECIAL_INDEX_CLEAR);

    /* CLR key also clears the active procedure (CORE:5513-5515) */
    events = press(K_CLR);
    CHECK(lists.presented_failures.count == 0 && lists.recall_failures.count == 1);
}

static void test_priority_and_windowing(void)
{
    int order[FWS_ECL_MAX_PROCS], step;
    setup();
    core_present(211800001u, 1, 0);   /* PACK 1 CTL 1 FAULT, failure 1 */
    core_present(999800001u, 2, 0);   /* secondary failure: priority 0 (FAS:122-124) */
    core_present(260800001u, 3, 1);   /* APU FIRE, warning */
    core_present(340900003u, 1, 0);   /* non-sensed: priority 4 */
    CHECK(fws_ecl_sorted_abnormal(&lists, order) == 4);
    CHECK(lists.presented_abnormal.entries[order[0]].state.id == 340900003u &&
          lists.presented_abnormal.entries[order[1]].state.id == 260800001u &&
          lists.presented_abnormal.entries[order[2]].state.id == 211800001u &&
          lists.presented_abnormal.entries[order[3]].state.id == 999800001u);
    cycle(K_NONE);
    CHECK(ecl.active_procedure_id == 340900003u);
    /* non-sensed abnormal: ACTIVATE line, recommendation captured on ground = none (FAS:172) */
    CHECK(wd.lines[1].item_index == FWS_SPECIAL_INDEX_ACTIVATE && wd.lines[1].checked);
    CHECK(wd.total_lines > FWS_WD_NUM_LINES && wd.overflow_down && !wd.overflow_up);

    /* scroll: showFromLine = max(0, numLinesUntilSelected - WD_NUM_LINES + 2) (FAS:263-269),
     * overflow arrows (WAC:43-48), window = lineData[showFromLine..+18) (WAC:66-83) */
    for (step = 0; step < 40; ++step) {
        FwsProcLines g;
        const FwsEclProcEntry *entry = fws_ecl_map_find(&lists.presented_abnormal, 340900003u);
        int until, expected_from;
        press(K_DOWN);
        fws_lines_init(&g, 340900003u, FWS_PROC_ABNORMAL, &entry->state, 0, FWS_RECOMMENDATION_NONE, 0);
        g.procedure_is_active = 1;
        g.selected_item_index = ecl.abn_selected_item_index;
        until = fws_lines_num_lines_until_selected(&g);
        expected_from = until - FWS_WD_NUM_LINES + 2 > 0 ? until - FWS_WD_NUM_LINES + 2 : 0;
        CHECK(ecl.abn_show_from_line == expected_from && ecl.pub.fws_show_from_line == expected_from);
        CHECK(wd.show_from_line == expected_from && wd.overflow_up == (expected_from > 0));
        CHECK(wd.overflow_down == (wd.total_lines > FWS_WD_NUM_LINES &&
                                   wd.total_lines - FWS_WD_NUM_LINES > expected_from));
        /* the EWD adds no lines above the first procedure, so the selection stays on window line 16 at most */
        if (until >= 0) CHECK(wd.selected_line == until - expected_from && wd.selected_line < FWS_WD_NUM_LINES - 1);
    }
    CHECK(ecl.abn_show_from_line > 0);
}

static void test_non_sensed_flow(void)
{
    setup();
    press(K_ABN_PROC); /* FANS:86-90 */
    CHECK(ecl.abn_proc_shown && wd.view == FWS_ECL_VIEW_ABN_NON_SENSED && ecl.pub.fws_show_abn_non_sensed);
    /* WANS:25-70: header + 6 top-level procedures + 6 sub menus */
    CHECK(wd.total_lines == 13 && wd.selected_line == 1);
    CHECK_STR(wd.lines[0].text, "\x1b" "4mABNORMAL PROC\x1bm");
    CHECK_STR(wd.lines[1].text, nb(2, "\x1b<2mSMOKE / FUMES")); /* removeUnderline (WANS:12-15) */
    CHECK_STR(wd.lines[7].text, nb(2, "ENG"));
    CHECK(wd.lines[7].item_index == 6 && wd.lines[12].last_line);
    {
        int i;
        for (i = 0; i < 20; ++i) press(K_DOWN); /* moveDown clamps at menu size - 1 (FANS:70-74) */
    }
    CHECK(ecl.abn_non_sensed_selected == 11);
    {
        int i;
        for (i = 0; i < 5; ++i) press(K_UP);
    }
    CHECK(ecl.abn_non_sensed_selected == 6);
    press(K_CHECK); /* sub menu ENG (FANS:109-115) */
    CHECK(ecl.abn_non_sensed_id == 1 && ecl.pub.fws_abn_non_sensed_id == 1 && ecl.abn_non_sensed_selected == 0);
    CHECK(wd.total_lines == 4);
    CHECK_STR(wd.lines[1].text, nb(2, "\x1b" "4mENG\x1bm"));
    CHECK_STR(wd.lines[2].text, nb(4, "\x1b<4mRELIGHT IN FLIGHT"));
    press(K_CHECK); /* preview (FANS:121-128) */
    CHECK(ecl.abn_non_sensed_id == 700900001u && ecl.abn_non_sensed_selected == -1);
    CHECK(!ecl.pub.fws_abn_non_sensed_current_active);
    CHECK(wd.selected_line == 1 && wd.lines[1].item_index == -1 && !wd.lines[1].checked);
    CHECK_STR(wd.lines[1].text, nb(31, "ACTIVATE"));
    CHECK(wd.lines[2].inactive && wd.lines[2].item_index == 1);
    press(K_DOWN); /* no navigation in a preview (FANS:65, :71) */
    CHECK(ecl.abn_non_sensed_selected == -1);
    press(K_CHECK); /* activate + close (FANS:129-138) */
    CHECK(fws_ecl_keys_index(&lists.active_abnormal_non_sensed, 700900001u) >= 0);
    CHECK(!ecl.show_abn_proc_requested && wd.view == FWS_ECL_VIEW_NONE);

    /* CLR navigates to the parent page, then closes (FANS:76-83) */
    press(K_ABN_PROC);
    press(K_DOWN);
    press(K_DOWN);
    press(K_DOWN);
    press(K_DOWN);
    press(K_DOWN);
    press(K_DOWN);
    press(K_CHECK);
    CHECK(ecl.abn_non_sensed_id == 1);
    press(K_CLR);
    CHECK(ecl.abn_non_sensed_id == 0 && ecl.show_abn_proc_requested);
    press(K_CLR);
    CHECK(!ecl.show_abn_proc_requested);
}

static void test_deferred_flow(void)
{
    const FwsProcedureDef *d = fws_deferred_procedure(340700002u);
    FwsChecklistState st;
    FwsEclProcEntry *entry;
    int i;
    setup();
    st = fresh_state(d, 0);
    for (i = 0; i < d->num_items; ++i) st.items_active[i] = 0; /* CORE:5703 */
    entry = fws_ecl_map_set(&lists.active_deferred, &st, 0);
    entry->which_items_checked_length = (unsigned char)d->num_items;

    press(K_CL);
    CHECK(ecl.has_deferred[FWS_DEFERRED_AT_TOP_OF_DESCENT] && !ecl.deferred_is_completed[FWS_DEFERRED_AT_TOP_OF_DESCENT]);
    CHECK(ecl.overview_items_to_show[7] && !ecl.overview_items_to_show[6]);
    CHECK(wd.total_lines == 12);
    CHECK_STR(wd.lines[7].text, "AT TOP OF DESCENT : DEFERRED PROCEDURE"); /* WNC:85-110 */
    CHECK(wd.lines[7].style == FWS_STYLE_DEFERRED_PROCEDURE && !wd.lines[7].checked && wd.lines[7].item_index == 7);
    for (i = 0; i < 6; ++i) press(K_DOWN);
    CHECK(ecl.selected_line == 7);
    press(K_CHECK); /* deferred page (FNC:128-177) */
    CHECK(ecl.checklist_id == 1000008u && ecl.active_deferred_valid && ecl.active_deferred_id == 340700002u);
    CHECK(ecl.selected_line == FWS_SPECIAL_INDEX_ACTIVATE && ecl.pub.fws_active_procedure == 340700002u);
    /* WNC:123-178: header, empty line, procedure (18 lines), page CLEAR */
    CHECK(wd.total_lines == 21 && wd.overflow_down && !wd.overflow_up);
    CHECK_STR(wd.lines[0].text, "\x1b<4mAT TOP OF DESCENT : DEFERRED PROCEDURE \x1bm");
    CHECK(wd.selected_line == 3 && wd.lines[3].item_index == FWS_SPECIAL_INDEX_ACTIVATE);

    /* ACTIVATE: callback rewrites itemsActive before selectFirst (FNC:157-167, PLG:335-351) */
    press(K_CHECK);
    entry = fws_ecl_map_get(&lists.active_deferred, 340700002u);
    CHECK(entry->state.procedure_activated && entry->state.items_active[1] && entry->state.items_active[10]);
    CHECK(ecl.selected_line == 0 && ecl.pub.fws_active_item == 0);
    press(K_DOWN);
    CHECK(ecl.selected_line == 1);

    /* CHECK a non-sensed condition-child item writes the deferred map (FNC:154-156) */
    press(K_CHECK);
    CHECK(fws_ecl_map_get(&lists.active_deferred, 340700002u)->state.items_checked[1] && ecl.selected_line == 2);

    /* DEFERRED PROC COMPLETE: move to the last line, check (PLG:338-341), then page CLEAR (FNC:328-336) */
    for (i = 0; i < 9; ++i) press(K_DOWN); /* items 3..10, then the special line (PLG:260-272) */
    CHECK(ecl.selected_line == FWS_SPECIAL_INDEX_DEFERRED_PROC_COMPLETE);
    press(K_CHECK);
    CHECK(fws_ecl_map_get(&lists.active_deferred, 340700002u)->state.procedure_completed &&
          ecl.selected_line == FWS_SPECIAL_INDEX_DEFERRED_PROC_RECALL);
    CHECK(ecl.deferred_is_completed[FWS_DEFERRED_AT_TOP_OF_DESCENT]);
    press(K_DOWN);
    CHECK(!ecl.active_deferred_valid && ecl.selected_line == FWS_SPECIAL_INDEX_DEFERRED_PAGE_CLEAR);
    CHECK(wd.lines[wd.selected_line].item_index == FWS_SPECIAL_INDEX_DEFERRED_PAGE_CLEAR);
    press(K_DOWN); /* on page CLEAR the last procedure keeps receiving DOWN (FNC:337-339), no visible change */
    CHECK(ecl.selected_line == FWS_SPECIAL_INDEX_DEFERRED_PAGE_CLEAR);
    press(K_UP); /* FNC:305-310: CLEAR of page selected -> last procedure, selectFirst -> RECALL */
    CHECK(ecl.active_deferred_valid && ecl.selected_line == FWS_SPECIAL_INDEX_DEFERRED_PROC_RECALL);
    press(K_DOWN);
    press(K_CHECK); /* CHECK on page CLEAR -> overview (FNC:467-471) */
    CHECK(ecl.checklist_id == 0 && ecl.selected_line == 0);
    CHECK(wd.lines[7].style == FWS_STYLE_COMPLETED_DEFERRED_PROCEDURE && wd.lines[7].checked); /* WNC:87-91 */

    /* deleting the active deferred procedure refreshes the page (FNC:198-202) */
    for (i = 0; i < 6; ++i) press(K_DOWN);
    CHECK(ecl.selected_line == 7);
    press(K_CHECK);
    CHECK(ecl.checklist_id == 1000008u && ecl.active_deferred_valid &&
          ecl.selected_line == FWS_SPECIAL_INDEX_DEFERRED_PROC_RECALL);
    fws_ecl_map_delete(&lists.active_deferred, 340700002u);
    cycle(K_NONE);
    CHECK(!ecl.active_deferred_valid && ecl.selected_line == FWS_SPECIAL_INDEX_DEFERRED_PAGE_CLEAR);
    CHECK(ecl.pub.fws_normal_checklists_id == 1000008u); /* FNC:139-143 returns before publishing */
}

static void test_fallbacks(void)
{
    setup();
    cycle(K_NONE);
    /* FWS 1+2 failed in flight: WAS:71-116 FWS 1+2 FAULT procedure (EWD.tsx:85-90) */
    ewd.fws_avail = 0;
    ewd.on_ground = 0;
    fws_ecl_compose_wd(&ecl, &lists, &ewd, &wd);
    CHECK(wd.view == FWS_ECL_VIEW_ABN_SENSED && !wd.memos_limitations_visible);
    CHECK_STR(wd.lines[0].text, "\x1b<4m\x1b" "4mFWS\x1bm FWS 1+2 FAULT");
    ewd.fcdc12_failed = 1;
    fws_ecl_compose_wd(&ecl, &lists, &ewd, &wd);
    CHECK_STR(wd.lines[0].text, "\x1b<4m\x1b" "4mFWS\x1bm FWS 1+2 & FCDC 1+2 FAULT");
    /* on ground above 50 kt: nothing (WAS:71) */
    ewd.on_ground = 1;
    ewd.groundspeed = 60.0;
    fws_ecl_compose_wd(&ecl, &lists, &ewd, &wd);
    CHECK(wd.view == FWS_ECL_VIEW_ABN_SENSED && wd.total_lines == 0 && wd.lines[0].special == FWS_SPECIAL_EMPTY);
    /* CPIOM C + another CPIOM failed: dedicated fallback page (EWD.tsx:78-82) */
    ewd.cpiom_c_failed = 1;
    ewd.cpiom_a_failed = 1;
    fws_ecl_compose_wd(&ecl, &lists, &ewd, &wd);
    CHECK(wd.view == FWS_ECL_VIEW_CPIOM_FALLBACK);
    CHECK_STR(wd.cpiom_fallback[0].text, "\x1b<4m\x1b" "4mFWS\x1bm FWS 1+2 & CPIOM FAULT");
    CHECK(wd.cpiom_fallback[1].visible && !wd.cpiom_fallback[2].visible && wd.cpiom_fallback[11].visible);
    CHECK(wd.cpiom_fallback[13].style == FWS_STYLE_WHITE && wd.cpiom_fallback[14].style == FWS_STYLE_AMBER);
}

static void test_lists(void)
{
    FwsEclKeyList l;
    memset(&l, 0, sizeof(l));
    fws_ecl_keys_push(&l, 1);
    fws_ecl_keys_push(&l, 2);
    fws_ecl_keys_push(&l, 3);
    fws_ecl_keys_splice(&l, fws_ecl_keys_index(&l, 99)); /* JS splice(-1, 1) removes the last element */
    CHECK(l.count == 2 && l.keys[1] == 2);
    fws_ecl_set_add(&l, 1);
    CHECK(l.count == 2);
}

int main(void)
{
    test_lists();
    test_normal_flow();
    test_abnormal_flow();
    test_priority_and_windowing();
    test_non_sensed_flow();
    test_deferred_flow();
    test_fallbacks();
    if (failures) {
        fprintf(stderr, "test_fws_ecl: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("test_fws_ecl: %d checks passed\n", checks);
    return 0;
}
