/* SPDX-License-Identifier: GPL-3.0-or-later
 * FWS core life cycle (a380x_fws.c) over the real signals, ECL, tables and line generator:
 * memos into the EWD view, a warning presented with MASTER WARN + CRC + SD fail page, the
 * procedure on the WD, resolution, C/L normal checklists.
 *   clang -std=c11 -O2 -Wall -Wextra -Werror a380x_fws.c a380x_fws_signals.c a380x_fws_ecl.c \
 *         a380x_fws_lines.c test_fws_core.c -lm -o test_fws_core && ./test_fws_core
 */
#include <stdio.h>
#include <string.h>
#include "a380x_fws.h"

static int failures, checks;
#define CHECK(cond, ...) do { ++checks; if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

static Fws F;
static FwsInputs I;
static FwsCycleInput C;
static double T;
static EwdWdView V;
#define DT 0.05f

static void step(void) { T += DT; fws_update(&F, &I, &C, 1, T, DT); fws_ewd_view(&F, &V); }
static void run(double seconds) { double end = T + seconds; while (T < end) step(); }
static void press(unsigned char *button) { *button = 1; step(); *button = 0; step(); }

static void healthy_ground(void)
{
    int i;
    fws_init(&F);
    memset(&C, 0, sizeof(C));
    C.baro_std = 1;
    fws_inputs_defaults(&I);
    T = 0;
    I.elec_valid = I.eng_valid = I.fuel_valid = I.hyd_valid = I.gear_valid = I.brakes_valid = 1;
    I.flap_slat_valid = I.pitch_trim_valid = I.rudder_trim_valid = I.tla_valid = 1;
    I.pitch_trim_deg = 2.0f;
    I.n1_idle_pct = 19.0f;
    for (i = 0; i < 4; i++) {
        I.feed_tank_kg[i] = 20000.0f;
        I.eng_pump_section_low[i][0] = I.eng_pump_section_low[i][1] = 1;   /* engines stopped */
    }
    I.ext_connected = 1;
    I.ext_contactor_closed[0] = 1;
    I.ext_avail[0] = I.ext_pb_on[0] = 1;
    I.apu_master_on = I.apu_avail = I.apu_n_valid = 1;
    I.apu_n_pct = 100.0f;
    I.park_brake_set = 1;
}

static int view_has_line(const char *needle)
{
    int i;
    for (i = 0; i < FWS_WD_NUM_LINES; ++i) if (strstr(V.line[i].text, needle)) return 1;
    return 0;
}
static int memo_right_has(const char *needle)
{
    int i;
    for (i = 0; i < V.memo_right_count; ++i) if (V.memo_right[i] && strstr(V.memo_right[i], needle)) return 1;
    return 0;
}

int main(void)
{
    healthy_ground();
    run(5.0);
    /* Cold & dark on EXT PWR + APU: no failure, memos shown (FwsCore 5935-5985 through the signals). */
    CHECK(F.out.flight_phase == 1, "phase %d", F.out.flight_phase);
    CHECK(!F.master_warning && !F.master_caution, "no master lights");
    CHECK(F.lists.presented_failures.count == 0, "no presented failure (%d)", F.lists.presented_failures.count);
    CHECK(V.valid && V.mode == EWD_WD_MEMOS, "memo view");
    CHECK(memo_right_has("APU AVAIL") && memo_right_has("PARK BRK ON") && memo_right_has("ELEC EXT PWR"),
          "right memos APU AVAIL / PARK BRK ON / ELEC EXT PWR (%d)", V.memo_right_count);
    CHECK(F.sfail == -1, "no SFAIL");

    /* ENG 1 FIRE on ground (260800009): level 3 -> MASTER WARN + CRC, presented, WD procedure. */
    I.eng_fire_detected[0] = 1;
    run(2.0);
    CHECK(fws_ecl_keys_index(&F.lists.presented_failures, 260800009u) >= 0, "ENG 1 FIRE presented");
    CHECK(F.master_warning, "MASTER WARN on");
    CHECK(F.aural_crc_active, "CRC on");
    CHECK(F.failure_active, "ECAM failure active");
    CHECK(F.sfail == FWS_SD_ENG, "SFAIL ENG page (%d)", F.sfail);
    CHECK(V.mode == EWD_WD_PROCEDURES, "procedure view (%d)", V.mode);
    CHECK(view_has_line("ENG 1 FIRE"), "ENG 1 FIRE headline on the WD");
    CHECK(V.selected_line >= 0, "an item is selected (%d)", V.selected_line);
    CHECK(!V.sts_area_visible, "STS area hidden while an abnormal procedure is shown");

    /* Fire out: the alert is no longer confirmed -> removed, lights and CRC off, memos back. */
    I.eng_fire_detected[0] = 0;
    run(2.0);
    CHECK(fws_ecl_keys_index(&F.lists.presented_failures, 260800009u) < 0, "ENG 1 FIRE removed");
    CHECK(F.lists.presented_abnormal.count == 0, "presented map empty (%d)", F.lists.presented_abnormal.count);
    CHECK(!F.master_warning && !F.aural_crc_active, "MASTER WARN / CRC off");
    CHECK(V.mode == EWD_WD_MEMOS, "memo view again");

    /* C/L: normal checklists on the WD (FwsNormalChecklists), C/L again closes them. */
    press(&C.ecp.cl);
    run(0.2);
    CHECK(F.ecl.pub.fws_show_normal_checklists, "normal checklists shown");
    CHECK(V.mode == EWD_WD_PROCEDURES, "checklist view");
    CHECK(view_has_line("BEFORE START") || view_has_line("COCKPIT PREPARATION"), "checklist overview lines");
    press(&C.ecp.cl);
    run(0.2);
    CHECK(!F.ecl.pub.fws_show_normal_checklists, "normal checklists closed");
    CHECK(V.mode == EWD_WD_MEMOS, "memo view after C/L");

    /* Unpowered FWS publishes nothing and resets. */
    fws_update(&F, &I, &C, 0, T, DT);
    fws_ewd_view(&F, &V);
    CHECK(!V.valid, "no view without power");

    printf("test_fws_core: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
