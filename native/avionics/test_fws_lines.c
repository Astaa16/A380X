/* SPDX-License-Identifier: GPL-3.0-or-later
 * Tests of the generated FWS tables and the ProcedureLinesGenerator port.
 * Expected values are derived by hand from the pinned FBW sources (cited per check):
 *   EM/ = fbw-a380x/src/systems/instruments/src/MsfsAvionicsCommon/EcamMessages/
 *   PLG = EM/ProcedureLinesGenerator.ts
 */
#include "a380x_fws_lines.h"

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

#define CHECK_STR(actual, expected)                                                                  \
    do {                                                                                             \
        const char *a_ = (actual), *e_ = (expected);                                                 \
        ++checks;                                                                                    \
        if (!a_ || strcmp(a_, e_) != 0) {                                                            \
            ++failures;                                                                              \
            fprintf(stderr, "%s:%d: got \"%s\"\n%*s expected \"%s\"\n", __FILE__, __LINE__, a_ ? a_ : "(null)", \
                    (int)strlen(__FILE__) + 6, "", e_);                                              \
        }                                                                                            \
    } while (0)

/* n NBSPs followed by s */
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

static const char *dots(const char *prefix, int n, const char *suffix)
{
    static char buf[8][256];
    static int slot;
    char *b = buf[slot++ & 7];
    strcpy(b, prefix);
    while (n-- > 0) strcat(b, ".");
    strcat(b, suffix);
    return b;
}

static FwsChecklistState state_for(const FwsProcedureDef *proc, int activated)
{
    FwsChecklistState st;
    int i;
    memset(&st, 0, sizeof(st));
    st.id = proc->id;
    st.procedure_activated = (unsigned char)activated;
    for (i = 0; i < proc->num_items; ++i) {
        st.items_to_show[i] = 1;
        st.items_active[i] = 1;
    }
    return st;
}

static void test_tables(void)
{
    const FwsProcedureDef *p;
    const char *t;
    /* EM/index.ts:37 '000001001': ' \x1b<7m\x1b4mT.O\x1bm' -- "\x1b" "4m" must not merge into \x1b4 */
    t = fws_memo_text(1001u);
    CHECK_STR(t, " \x1b<7m\x1b" "4mT.O\x1bm");
    CHECK(t && strlen(t) == 13 && t[5] == 0x1b && t[6] == '4' && t[7] == 'm');
    CHECK_STR(fws_memo_find(1001u)->key, "000001001");
    CHECK_STR(fws_memo_text(709000001u), "\x1b<3mIGNITION");                /* index.ts:167 */
    CHECK_STR(fws_limitation_text(1u), "\x1b<2mLAND ASAP");                 /* index.ts:240 */
    CHECK_STR(fws_limitation_find(2u)->key, "2");                           /* index.ts:241 */
    CHECK_STR(fws_limitation_text(800400008u), "\x1b<5mTURB SPEED:");        /* index.ts:273 */
    /* index.ts:223 '\x1b<3m\xa0\xa0PITCH TRGT : 3°' -> UTF-8 NBSP and degree sign */
    CHECK_STR(fws_info_text(340200011u), "\x1b<3m\xc2\xa0\xc2\xa0PITCH TRGT : 3\xc2\xb0");
    CHECK_STR(fws_inop_sys_text(240300002u), "\x1b<4mAPU GEN A ");          /* index.ts:386 (trailing space) */
    CHECK(fws_inop_sys_text(340300043u) != NULL);                           /* out-of-order key, index.ts:598 */
    CHECK(fws_memo_text(12345u) == NULL);
    CHECK(fws_is_pfd_memo(FWS_ATHR_OFF_MEMO_ID) && FWS_ATHR_OFF_MEMO_ID == 220000002u); /* index.ts:172-181 */
    CHECK(fws_pfd_memo_display[0] == 6002u && FWS_PFD_MEMO_DISPLAY_COUNT == 6);
    /* Census: hand count of index.ts / AbnormalSensed (see tools/test_build_fws_tables.py for the regex census) */
    CHECK(FWS_LIMITATION_COUNT == 34 && FWS_NORMAL_PROCEDURE_COUNT == 14 && FWS_DEFERRED_PROCEDURE_COUNT == 8);
    CHECK(FWS_ABN_NON_SENSED_OVERVIEW_COUNT == 31 && FWS_ABN_SUB_MENU_COUNT == 6);

    /* EM/AbnormalSensed/ata26.ts:21-43 APU FIRE */
    p = fws_abnormal_procedure(260800001u);
    CHECK(p != NULL);
    if (!p) return;
    CHECK_STR(p->title, "\x1b<2m\x1b" "4mAPU FIRE");
    CHECK(p->sensed == 1 && p->recommendation == FWS_RECOMMENDATION_LAND_ASAP && p->num_items == 3);
    CHECK_STR(p->items[1].name, "APU AGENT");
    CHECK(p->items[1].has_time && p->items[1].time == 30 && fws_item_is_timed_action(&p->items[1]));
    CHECK(p->items[0].style == -1 && p->items[0].colon_if_completed == -1 && p->items[0].condition == -1);
    /* ata24.ts:40 double-quoted title with \x1b'm */
    CHECK_STR(fws_abnormal_procedure(240800003u)->title,
              "\x1b<4m\x1b" "4mELEC\x1bm \x1b'mAC BUS 1+2 & DC BUS 1 FAULT\x1bm");
    /* ata24.ts:58 { ...FMS_PRED_UNRELIABLE_CHECKLIST_ITEM } (index.ts:835-839) */
    p = fws_abnormal_procedure(240800003u);
    CHECK_STR(p->items[3].name, "FMS PRED UNRELIABLE");
    CHECK(p->items[3].sensed == 0 && p->items[3].style == FWS_STYLE_CHECKLIST_CONDITION &&
          p->items[3].condition == -1 && p->items[3].label_not_completed == NULL);
    /* ata27.ts:135 template literal `${'\xa0'.repeat(16)}WINDSHEAR, SPEED SPEED` */
    p = fws_abnormal_procedure(271800015u);
    CHECK_STR(p->items[1].name, nb(16, "WINDSHEAR, SPEED SPEED"));
    CHECK(p->items[1].style == FWS_STYLE_GREEN);
    /* ata34.ts 340900003 (non-sensed, 71 items) is the longest procedure */
    p = fws_abnormal_procedure(340900003u);
    CHECK(p->sensed == 0 && p->num_items == 71 && FWS_TABLES_MAX_ITEMS == 71);
    /* Secondary failures, secondary-failures.ts:20-24 */
    CHECK_STR(fws_abnormal_procedure(999800001u)->title, "\x1b<4m*F/CTL");
    /* Deferred, ata34.ts EcamDeferredProcAta34 340700002 */
    p = fws_deferred_procedure(340700002u);
    CHECK(p && p->deferred_type == FWS_DEFERRED_AT_TOP_OF_DESCENT && p->num_items == 11 &&
          p->num_from_abnormal_procs == 1 && p->from_abnormal_procs[0] == 340900003u);
    CHECK_STR(fws_deferred_procedure_type_to_string[FWS_DEFERRED_FOR_LANDING], "FOR LANDING");
    /* NormalProcedures.ts:6, :38-57, :153-156 */
    CHECK(fws_deferred_procedure_ids[0] == 1000007u && fws_deferred_procedure_ids[3] == 1000011u);
    p = fws_normal_procedure(1000002u);
    CHECK_STR(p->title, "BEFORE START");
    CHECK(p->num_items == 3 && p->items[2].colon_if_completed == 0 && p->items[2].sensed == 1);
    CHECK(fws_normal_procedure(1000006u)->only_activated_by_request == 1);
    CHECK(fws_normal_procedure_index(1000002u) == 1 && fws_deferred_procedure_id_index(1000009u) == 2);
    /* AbnormalNonSensedProcedures.ts:9, :15, :33 */
    CHECK(fws_abn_non_sensed_overview[0].id == 260900097u && fws_abn_non_sensed_overview[0].category == NULL);
    CHECK_STR(fws_abn_non_sensed_overview[6].category, "ENG");
    CHECK_STR(fws_abn_sub_menu_vector[5], "MISCELLANEOUS");
}

static void test_texts(void)
{
    const FwsProcedureDef *apu = fws_abnormal_procedure(260800001u);
    const FwsProcedureDef *fire = fws_abnormal_procedure(260800005u);
    const FwsProcedureDef *vent = fws_abnormal_procedure(212800018u);
    FwsChecklistState st = state_for(apu, 1);
    char buf[256];
    /* PLG:641-660: '-APU FIRE P/B' + ' ' + dots(40 - (4 + 13 + 0 + 2) = 21) + 'PUSH' */
    fws_lines_checklist_action_text(&apu->items[0], 0, 0, &st, 1, 0.0, buf, sizeof(buf));
    CHECK_STR(buf, dots("-APU FIRE P/B ", 21, "PUSH"));
    CHECK(fws_js_length(buf) == 39);
    /* completed: colonIfCompleted undefined -> ' : ' (PLG:647) */
    fws_lines_checklist_action_text(&apu->items[0], 0, 1, &st, 1, 0.0, buf, sizeof(buf));
    CHECK_STR(buf, "-APU FIRE P/B : PUSH");
    /* timed action, timestamp null -> ' AFTER 30 S :' (PLG:703-704, 717); padding 40-(5+23+2) = 10 */
    fws_lines_checklist_action_text(&apu->items[1], 1, 0, &st, 1, 0.0, buf, sizeof(buf));
    CHECK_STR(buf, dots("-APU AGENT AFTER 30 S : ", 10, "DISCH"));
    /* timestamp 100 s, now 112 s -> 30 - floor(12) = 18 (PLG:706-708) */
    st.timestamp_valid[1] = 1;
    st.items_timestamp[1] = 100.0;
    fws_lines_checklist_action_text(&apu->items[1], 1, 0, &st, 1, 112.4, buf, sizeof(buf));
    CHECK_STR(buf, dots("-APU AGENT AFTER 18 S : ", 10, "DISCH"));
    /* elapsed timed action -> null -> no time text, padding 40-(5+10+2) = 23 (PLG:713) */
    fws_lines_checklist_action_text(&apu->items[1], 1, 0, &st, 1, 131.0, buf, sizeof(buf));
    CHECK_STR(buf, dots("-APU AGENT ", 23, "DISCH"));
    /* itemsTimeStamp undefined -> getTimedItemLineText null (PLG:718-722) */
    fws_lines_checklist_action_text(&apu->items[1], 1, 0, &st, 0, 112.0, buf, sizeof(buf));
    CHECK_STR(buf, dots("-APU AGENT ", 23, "DISCH"));
    /* colonIfCompleted: false (NormalProcedures.ts:51-54) -> ' ' (PLG:645) */
    fws_lines_checklist_action_text(&fws_normal_procedure(1000002u)->items[2], 2, 1, &st, 0, 0.0, buf, sizeof(buf));
    CHECK_STR(buf, "-BEACON ON");
    /* labelCompleted 'NORM' (NormalProcedures.ts:129-135), level 1 */
    fws_lines_checklist_action_text(&fws_normal_procedure(1000004u)->items[8], 8, 1, &st, 0, 0.0, buf, sizeof(buf));
    CHECK_STR(buf, nb(1, "-T.O CONFIG NORM"));

    /* Timed condition ata26.ts:98-104 'STILL FIRE' time 30, appendTimeIfElapsed false (PLG:663-689) */
    st = state_for(fire, 1);
    fws_lines_checklist_condition_text(&fire->items[7], 7, 0, &st, 1, 0.0, buf, sizeof(buf));
    CHECK_STR(buf, ".IF STILL FIRE  AFTER 30 S :");  /* null timestamp */
    st.timestamp_valid[7] = 1;
    st.items_timestamp[7] = 100.0;
    fws_lines_checklist_condition_text(&fire->items[7], 7, 0, &st, 1, 120.0, buf, sizeof(buf));
    CHECK_STR(buf, ".IF STILL FIRE  AFTER 10 S :");
    fws_lines_checklist_condition_text(&fire->items[7], 7, 1, &st, 1, 131.0, buf, sizeof(buf));
    CHECK_STR(buf, ".AS STILL FIRE  :");             /* elapsed, no append -> ' :' */
    /* ata21-22-23.ts:777-783 timed condition of a procedure without whichItemsTimer: '.AS ... :' */
    fws_lines_checklist_condition_text(&vent->items[1], 1, 0, &st, 0, 0.0, buf, sizeof(buf));
    CHECK_STR(buf, ".AS VLV STILL FULL OPEN  :");
    /* WHEN with trailing colon (ata34.ts deferred 340700002 item 5, level 1): '.WHEN ... : ' (PLG:671-672) */
    fws_lines_checklist_condition_text(&fws_deferred_procedure(340700002u)->items[5], 5, 0, &st, 0, 0.0, buf,
                                       sizeof(buf));
    CHECK_STR(buf, nb(1, ".WHEN BELOW FL 250 : "));
}

static void test_conditional_active_items(void)
{
    const FwsProcedureDef *fire = fws_abnormal_procedure(260800005u);
    const FwsProcedureDef *rsvr = fws_abnormal_procedure(290800019u);
    FwsChecklistState st = state_for(rsvr, 1);
    /* ata29-30.ts:231-260: unchecked condition (L0) deactivates its L1 children (PLG:121-150) */
    fws_lines_conditional_active_items(rsvr, st.items_checked, st.items_active, NULL, NULL, st.items_to_show, 0.0);
    CHECK(st.items_active[0] == 1 && st.items_active[1] == 0 && st.items_active[2] == 0 && st.items_active[3] == 0);
    st.items_checked[0] = 1;
    fws_lines_conditional_active_items(rsvr, st.items_checked, st.items_active, NULL, NULL, st.items_to_show, 0.0);
    CHECK(st.items_active[1] == 1 && st.items_active[3] == 1);
    /* itemsToShow undefined -> no parent conditions evaluated (PLG:130-131) */
    st.items_checked[0] = 0;
    fws_lines_conditional_active_items(rsvr, st.items_checked, st.items_active, NULL, NULL, NULL, 0.0);
    CHECK(st.items_active[1] == 1);

    /* timed condition: elapsed timestamp activates the child and forces checked (PLG:134-148) */
    st = state_for(fire, 1);
    st.timestamp_valid[7] = 1;
    st.items_timestamp[7] = 100.0;
    fws_lines_conditional_active_items(fire, st.items_checked, st.items_active, st.items_timestamp,
                                       st.timestamp_valid, st.items_to_show, 120.0);
    CHECK(st.items_checked[7] == 0 && st.items_active[8] == 0);
    fws_lines_conditional_active_items(fire, st.items_checked, st.items_active, st.items_timestamp,
                                       st.timestamp_valid, st.items_to_show, 130.0); /* (30 > 30) is false */
    CHECK(st.items_checked[7] == 0);
    fws_lines_conditional_active_items(fire, st.items_checked, st.items_active, st.items_timestamp,
                                       st.timestamp_valid, st.items_to_show, 131.0);
    CHECK(st.items_checked[7] == 1 && st.items_active[8] == 1);
}

static void test_abnormal_lines(void)
{
    const FwsProcedureDef *apu = fws_abnormal_procedure(260800001u);
    FwsChecklistState st = state_for(apu, 1);
    FwsProcLines g;
    FwsWdLine lines[FWS_LINES_MAX];
    int n;
    unsigned events;
    fws_lines_init(&g, 260800001u, FWS_PROC_ABNORMAL, &st, 1, FWS_RECOMMENDATION_LAND_ASAP, 0);
    g.procedure_is_active = 1;
    g.now = 0.0;
    n = fws_lines_to_line_data(&g, lines, FWS_LINES_MAX);
    /* PLG:428-437 title, :441-451 LAND ASAP, :468-543 items, :545-556 CLEAR, :621-630 empty */
    CHECK(n == 7);
    CHECK_STR(lines[0].text, "\x1b<2m\x1b" "4mAPU FIRE");
    CHECK(lines[0].style == FWS_STYLE_HEADLINE && lines[0].abnormal_procedure && lines[0].first_line &&
          !lines[0].last_line && !lines[0].has_item_index);
    CHECK_STR(lines[1].text, "LAND ASAP");
    CHECK(lines[1].style == FWS_STYLE_LAND_ASAP);
    CHECK_STR(lines[2].text, dots("-APU FIRE P/B ", 21, "PUSH"));
    CHECK(lines[2].has_item_index && lines[2].item_index == 0 && lines[2].sensed && lines[2].procedure_id == 260800001u);
    CHECK(lines[2].style == FWS_STYLE_CHECKLIST_ITEM && !lines[2].inactive && !lines[2].first_line);
    CHECK_STR(lines[3].text, dots("-APU AGENT AFTER 30 S : ", 10, "DISCH"));
    CHECK_STR(lines[5].text, nb(34, "CLEAR"));
    CHECK(lines[5].item_index == FWS_SPECIAL_INDEX_CLEAR && !lines[5].sensed && !lines[5].last_line);
    CHECK(lines[6].text[0] == 0 && lines[6].first_line && lines[6].last_line && lines[6].active_procedure);

    /* not active: title (lastLine), '...' and the empty line (PLG:606-618) */
    g.procedure_is_active = 0;
    n = fws_lines_to_line_data(&g, lines, FWS_LINES_MAX);
    CHECK(n == 3 && lines[0].last_line && lines[1].style == FWS_STYLE_OMISSION_DOTS);
    CHECK_STR(lines[1].text, "...");
    g.procedure_is_active = 1;

    /* navigation PLG:354-376, 250-289 */
    fws_lines_select_first(&g);
    CHECK(g.selected_item_index == 0 && fws_lines_num_lines_until_selected(&g) == 2);
    fws_lines_move_down(&g, 1);
    CHECK(g.selected_item_index == 1);
    fws_lines_move_down(&g, 1);
    fws_lines_move_down(&g, 1);
    CHECK(g.selected_item_index == FWS_SPECIAL_INDEX_CLEAR && fws_lines_last_line_is_selected(&g));
    fws_lines_move_up(&g);
    CHECK(g.selected_item_index == 2);
    /* sensed items cannot be checked manually (PLG:304-308) */
    events = fws_lines_check_selected(&g);
    CHECK(events == 0 && g.checklist_state.items_checked[2] == 0);
    /* sensed + checked items are skipped by selectFirst (PLG:400) */
    g.checklist_state.items_checked[0] = 1;
    fws_lines_select_first(&g);
    CHECK(g.selected_item_index == 1);
    n = fws_lines_to_line_data(&g, lines, FWS_LINES_MAX);
    CHECK_STR(lines[2].text, "-APU FIRE P/B : PUSH");
    CHECK(lines[2].checked);
    /* CLEAR fires procedureClearedOrResetCallback (PLG:325-326) */
    g.selected_item_index = FWS_SPECIAL_INDEX_CLEAR;
    CHECK(fws_lines_check_selected(&g) == FWS_LINES_EVENT_CLEARED_OR_RESET);
    /* items hidden by itemsToShow are not listed for abnormal procedures (PLG:469-474) */
    g.checklist_state.items_to_show[1] = 0;
    CHECK(fws_lines_to_line_data(&g, lines, FWS_LINES_MAX) == 6);
}

static int adopt_calls;
static void count_callback(void *ctx, FwsProcLines *gen, FwsChecklistState *st)
{
    (void)ctx;
    (void)gen;
    (void)st;
    ++adopt_calls;
}

static void test_condition_lines(void)
{
    const FwsProcedureDef *rsvr = fws_abnormal_procedure(290800019u);
    FwsChecklistState st = state_for(rsvr, 1);
    FwsProcLines g;
    FwsWdLine lines[FWS_LINES_MAX];
    int n;
    fws_lines_conditional_active_items(rsvr, st.items_checked, st.items_active, NULL, NULL, st.items_to_show, 0.0);
    fws_lines_init(&g, rsvr->id, FWS_PROC_ABNORMAL, &st, 0, FWS_RECOMMENDATION_NONE, 0);
    g.procedure_is_active = 1;
    n = fws_lines_to_line_data(&g, lines, FWS_LINES_MAX);
    /* title, condition, CONFIRM, 3 items, CLEAR, empty */
    CHECK(n == 8);
    CHECK_STR(lines[1].text, ".IF G SYS PRESSURE FLUCTUATES  :");
    CHECK(lines[1].style == FWS_STYLE_CHECKLIST_CONDITION && lines[1].sensed && !lines[1].has_item_index);
    CHECK_STR(lines[2].text, "CONFIRM G SYS PRESSURE FLUCTUATES"); /* PLG:519-541 */
    CHECK(!lines[2].sensed && lines[2].has_item_index && lines[2].item_index == 0);
    /* level 1, inactive child: NBSP + action text, padding 40 - (3 + 21 + 1 + 2) = 13 (PLG:653-656) */
    CHECK_STR(lines[3].text, dots(nb(1, "-G ENG 1 PMP A AND B "), 13, "OFF"));
    CHECK(lines[3].style == FWS_STYLE_CHECKLIST_ITEM_INACTIVE && lines[3].inactive);

    /* checkSelected on the condition: conditionalActiveItems, then moveDown (PLG:309-324) */
    fws_lines_select_first(&g);
    CHECK(g.selected_item_index == 0);
    g.item_checked_callback = count_callback;
    g.item_checked_adopts_state = 1; /* FwsAbnormalSensed map subscription adopts clState before moveDown */
    adopt_calls = 0;
    CHECK(fws_lines_check_selected(&g) == FWS_LINES_EVENT_ITEM_CHECKED && adopt_calls == 1);
    CHECK(g.checklist_state.items_checked[0] == 1 && g.checklist_state.items_active[1] == 1);
    CHECK(g.selected_item_index == 1);
    n = fws_lines_to_line_data(&g, lines, FWS_LINES_MAX);
    CHECK_STR(lines[1].text, ".AS G SYS PRESSURE FLUCTUATES  :");

    /* without adoption (normal/deferred owners) moveDown still sees the old itemsActive -> CLEAR */
    fws_lines_init(&g, rsvr->id, FWS_PROC_ABNORMAL, &st, 0, FWS_RECOMMENDATION_NONE, 0);
    g.procedure_is_active = 1;
    fws_lines_select_first(&g);
    fws_lines_check_selected(&g);
    CHECK(g.selected_item_index == FWS_SPECIAL_INDEX_CLEAR && g.checklist_state.items_active[1] == 1);
}

static void test_normal_lines(void)
{
    const FwsProcedureDef *bs = fws_normal_procedure(1000002u);
    FwsChecklistState st = state_for(bs, 1);
    FwsProcLines g;
    FwsWdLine lines[FWS_LINES_MAX];
    int n;
    unsigned events;
    fws_lines_init(&g, bs->id, FWS_PROC_NORMAL, &st, 0, FWS_RECOMMENDATION_NONE, 0);
    g.procedure_is_active = 1;
    n = fws_lines_to_line_data(&g, lines, FWS_LINES_MAX);
    /* NormalProcedures.ts:37-57 + PLG:557-578 */
    CHECK(n == 7);
    CHECK_STR(lines[0].text, "BEFORE START");
    CHECK(!lines[0].abnormal_procedure && lines[0].first_line && !lines[0].last_line);
    CHECK_STR(lines[1].text, dots("-PARKING BRAKE ", 20, "____"));
    CHECK_STR(lines[2].text, dots("-T.O SPEEDS & THRUST ", 7, "____ (BOTH)"));
    CHECK_STR(lines[3].text, dots("-BEACON ", 29, "ON"));
    CHECK(!lines[1].sensed && lines[3].sensed);
    CHECK_STR(lines[4].text, nb(27, "C/L COMPLETE"));
    CHECK(lines[4].item_index == FWS_SPECIAL_INDEX_NORMAL_CL_COMPLETE && !lines[4].checked);
    CHECK_STR(lines[5].text, nb(34, "RESET"));
    CHECK(lines[5].item_index == FWS_SPECIAL_INDEX_NORMAL_RESET && lines[5].last_line);

    fws_lines_select_first(&g);
    CHECK(g.selected_item_index == 0);
    CHECK(fws_lines_check_selected(&g) == FWS_LINES_EVENT_ITEM_CHECKED && g.selected_item_index == 1);
    fws_lines_check_selected(&g);
    CHECK(g.selected_item_index == 2 && g.checklist_state.items_checked[1]);
    fws_lines_move_down(&g, 1);
    CHECK(g.selected_item_index == FWS_SPECIAL_INDEX_NORMAL_CL_COMPLETE);
    events = fws_lines_check_selected(&g); /* PLG:327-330: non-sensed checked, procedure completed */
    CHECK(events == FWS_LINES_EVENT_COMPLETED && g.checklist_state.procedure_completed);
    CHECK(g.checklist_state.items_checked[0] && !g.checklist_state.items_checked[2]);
    n = fws_lines_to_line_data(&g, lines, FWS_LINES_MAX);
    CHECK_STR(lines[1].text, "-PARKING BRAKE : ____");
    CHECK(lines[4].checked);
    fws_lines_move_down(&g, 1); /* PLG:276-277 */
    CHECK(g.selected_item_index == FWS_SPECIAL_INDEX_NORMAL_RESET);
    fws_lines_move_up(&g);
    CHECK(g.selected_item_index == FWS_SPECIAL_INDEX_NORMAL_CL_COMPLETE);
    fws_lines_move_down(&g, 1);
    events = fws_lines_check_selected(&g); /* PLG:331-334 then selectFirst (PLG:349-351) */
    CHECK(events == FWS_LINES_EVENT_CLEARED_OR_RESET && !g.checklist_state.procedure_completed);
    CHECK(!g.checklist_state.items_checked[0] && g.selected_item_index == 0);
    /* completed checklist: selectFirst selects RESET (PLG:364-366) */
    g.checklist_state.procedure_completed = 1;
    fws_lines_select_first(&g);
    CHECK(g.selected_item_index == FWS_SPECIAL_INDEX_NORMAL_RESET);
    /* TAXI 'T.O' SubHeadline is not a headline (index.ts:694 comma bug): '\xa0T.O', not selectable */
    st = state_for(fws_normal_procedure(1000004u), 1);
    fws_lines_init(&g, 1000004u, FWS_PROC_NORMAL, &st, 0, FWS_RECOMMENDATION_NONE, 0);
    g.procedure_is_active = 1;
    n = fws_lines_to_line_data(&g, lines, FWS_LINES_MAX);
    CHECK_STR(lines[4].text, nb(1, "T.O"));
    CHECK(lines[4].style == FWS_STYLE_SUB_HEADLINE);
    g.selected_item_index = 2; /* RADAR -> skips the SubHeadline (nonSelectableItemStyles, PLG:90-97) */
    fws_lines_move_down(&g, 0);
    CHECK(g.selected_item_index == 4);
}

static void test_deferred_lines(void)
{
    const FwsProcedureDef *d = fws_deferred_procedure(340700002u);
    FwsChecklistState st = state_for(d, 0);
    FwsProcLines g;
    FwsWdLine lines[FWS_LINES_MAX];
    int n, i;
    for (i = 0; i < d->num_items; ++i) st.items_active[i] = 0; /* FwsCore.ts:5703 "not activated, hence all false" */
    fws_lines_init(&g, d->id, FWS_PROC_DEFERRED, &st, 0, FWS_RECOMMENDATION_NONE, 0);
    g.procedure_is_active = 1;
    n = fws_lines_to_line_data(&g, lines, FWS_LINES_MAX);
    /* title + ACTIVATE + 11 items + 3 CONFIRM + DEFERRED PROC COMPLETE + empty (PLG:416-426, 454-466, 593-603) */
    CHECK(n == 18);
    CHECK_STR(lines[0].text, "\x1b<4m> AIR DATA SELECTION");
    CHECK(!lines[0].first_line && lines[0].abnormal_procedure);
    CHECK_STR(lines[1].text, nb(31, "ACTIVATE"));
    CHECK(lines[1].item_index == FWS_SPECIAL_INDEX_ACTIVATE && !lines[1].checked);
    CHECK_STR(lines[2].text, ".IF REMAINING AIR DATA RELIABLE :  ");
    CHECK_STR(lines[3].text, "CONFIRM REMAINING AIR DATA RELIABLE ");
    CHECK_STR(lines[4].text, dots(nb(1, "-RELIABLE AIR DATA "), 15, "USE"));
    CHECK(lines[4].inactive && lines[4].style == FWS_STYLE_CHECKLIST_ITEM_INACTIVE);
    CHECK_STR(lines[9].text, nb(1, ".WHEN BELOW FL 250 : "));
    CHECK_STR(lines[10].text, nb(1, "CONFIRM  BELOW FL 250 ")); /* PLG:521-529 */
    CHECK_STR(lines[13].text, nb(3, "FPV / VV AVAIL")); /* level 2 + '\xa0name' (PLG:478, 501) */
    CHECK(lines[13].style == FWS_STYLE_GREEN && lines[13].inactive);
    CHECK_STR(lines[16].text, nb(17, "DEFERRED PROC COMPLETE"));
    CHECK(lines[16].item_index == FWS_SPECIAL_INDEX_DEFERRED_PROC_COMPLETE && lines[16].inactive);
    CHECK(lines[17].active_procedure && !lines[17].first_line && !lines[17].last_line);
    /* not activated: everything goes to ACTIVATE (PLG:250-257, 354-360, 187-191) */
    fws_lines_move_down(&g, 1);
    CHECK(g.selected_item_index == FWS_SPECIAL_INDEX_ACTIVATE && fws_lines_first_line_is_selected(&g) &&
          fws_lines_last_line_is_selected(&g));
    /* completed: DEFERRED PROC RECALL replaces items (PLG:469-473, 580-591) */
    g.checklist_state.procedure_completed = 1;
    g.checklist_state.procedure_activated = 1;
    n = fws_lines_to_line_data(&g, lines, FWS_LINES_MAX);
    CHECK(n == 3);
    CHECK_STR(lines[0].text, "\x1b<7m> AIR DATA SELECTION");
    CHECK_STR(lines[1].text, nb(19, "DEFERRED PROC RECALL"));
    fws_lines_select_first(&g);
    CHECK(g.selected_item_index == FWS_SPECIAL_INDEX_DEFERRED_PROC_RECALL);
    CHECK(fws_lines_check_selected(&g) == FWS_LINES_EVENT_COMPLETED && !g.checklist_state.procedure_completed &&
          g.selected_item_index == FWS_SPECIAL_INDEX_DEFERRED_PROC_COMPLETE);
    /* activated: firstLineIsSelected returns getActualShownItems()[0] = NaN (falsy) (PLG:175-177) */
    CHECK(!fws_lines_first_line_is_selected(&g));
}

static void test_fallback_lines(void)
{
    /* WdAbnormalSensedProcedures.tsx:100-114: FWS 1+2 FAULT fallback, ten-entry arrays */
    const FwsProcedureDef *p = fws_abnormal_procedure(314800004u);
    FwsChecklistState st;
    FwsProcLines g;
    FwsWdLine lines[FWS_LINES_MAX];
    int n, i, expected = 2; /* title + empty */
    memset(&st, 0, sizeof(st));
    st.id = 314800004u;
    st.procedure_activated = 1;
    for (i = 0; i < 10; ++i) st.items_active[i] = st.items_to_show[i] = 1;
    for (i = 0; i < p->num_items && i < 10; ++i) expected += 1 + (fws_item_is_condition(&p->items[i]) && !p->items[i].sensed);
    fws_lines_init(&g, 314800004u, FWS_PROC_FWS_FAILED_FALLBACK, &st, 0, FWS_RECOMMENDATION_NONE, 0);
    n = fws_lines_to_line_data(&g, lines, FWS_LINES_MAX);
    CHECK(n == expected);
    CHECK_STR(lines[0].text, "\x1b<4m\x1b" "4mFWS\x1bm FWS 1+2 FAULT");
    CHECK(lines[1].first_line && lines[1].last_line); /* PLG:512-513 */
}

int main(void)
{
    test_tables();
    test_texts();
    test_conditional_active_items();
    test_abnormal_lines();
    test_condition_lines();
    test_normal_lines();
    test_deferred_lines();
    test_fallback_lines();
    if (failures) {
        fprintf(stderr, "test_fws_lines: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("test_fws_lines: %d checks passed\n", checks);
    return 0;
}
