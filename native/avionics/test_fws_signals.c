/* SPDX-License-Identifier: GPL-3.0-or-later
 * Mock-input tests of a380x_fws_signals (flight phases, memos, alerts, items, lists, SD page).
 * Expected values come from the pinned FlyByWire sources; references are
 * fbw-a380x/src/systems/systems-host/CpiomC/FlightWarningSystem/<file>:<line>.
 *
 *   clang -std=c11 -O2 -Wall -Wextra -Werror test_fws_signals.c a380x_fws_signals.c -lm -o test_fws_signals
 *   ./test_fws_signals
 */
#include "a380x_fws_signals.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail, g_checks;
#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_fail++;                                                         \
            printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);          \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
        }                                                                     \
    } while (0)

static FwsSignals S;
static FwsInputs I;
static double T;
static const float DT = 0.125f; /* FwsCore UpdateThrottler(125) */

static void step(double seconds)
{
    int n = (int)(seconds / DT + 0.5), i;
    if (n < 1)
        n = 1;
    for (i = 0; i < n; i++) {
        T += DT;
        fws_signals_update(&S, &I, T, DT);
    }
}

static int idx(unsigned id)
{
    int i = fws_signals_alert_index(id);
    if (i < 0)
        printf("unknown alert %u\n", id);
    return i;
}
static int act(unsigned id) { return fws_signals_alert_active(&S, idx(id)); }
static int conf(unsigned id) { return fws_signals_alert_confirmed(&S, idx(id)); }
static int phase(void)
{
    FwsSignalOutputs o;
    fws_signals_outputs(&S, &o);
    return o.flight_phase;
}
static void eval(unsigned id, FwsAlertEval *e) { fws_signals_alert_eval(&S, idx(id), e); }

static int memo_right_has(unsigned code)
{
    unsigned m[10];
    int n = fws_signals_memos_right(&S, m, 10), i;
    for (i = 0; i < n; i++)
        if (m[i] == code)
            return 1;
    return 0;
}

static int list_eq(const unsigned *got, int n_got, const unsigned *exp, int n_exp)
{
    int i;
    if (n_got != n_exp)
        return 0;
    for (i = 0; i < n_got; i++)
        if (got[i] != exp[i])
            return 0;
    return 1;
}

static void print_list(const char *name, const unsigned *v, int n)
{
    int i;
    printf("  %s:", name);
    for (i = 0; i < n; i++)
        printf(" %u", v[i]);
    printf("\n");
}

static int idlist_has(const FwsIdList *l, unsigned id)
{
    unsigned i;
    for (i = 0; i < l->n; i++)
        if (l->id[i] == id)
            return 1;
    return 0;
}

/* Count active, confirmed procedure alerts that are not phase inhibited (deferred procedures and
 * non-procedure keys excluded). */
static int count_unexpected(unsigned *first)
{
    int n = 0, i;
    for (i = 0; i < fws_signals_alert_count(); i++) {
        const FwsAlertDesc *d = fws_signals_alert_desc(i);
        if (d->deferred)
            continue;
        if (fws_signals_alert_confirmed(&S, i) && !(d->phase_inhib & FWS_PHASE_BIT(phase()))) {
            if (!n && first)
                *first = d->id;
            n++;
        }
    }
    return n;
}

/* ------------------------------------------------------------------------------------------ */
/* aircraft states                                                                            */
/* ------------------------------------------------------------------------------------------ */
static void reset_sim(void)
{
    int i;
    fws_signals_init(&S);
    fws_inputs_defaults(&I);
    T = 0.0;
    I.elec_valid = 1;
    I.eng_valid = 1;
    I.fuel_valid = 1;
    I.hyd_valid = 1;
    I.gear_valid = 1;
    I.brakes_valid = 1;
    I.flap_slat_valid = 1;
    I.pitch_trim_valid = 1;
    I.pitch_trim_deg = 2.0f;
    I.rudder_trim_valid = 1;
    I.tla_valid = 1;
    I.n1_idle_pct = 19.0f;
    for (i = 0; i < 4; i++) {
        I.feed_tank_kg[i] = 20000.0f;
        I.eng_pump_section_low[i][0] = I.eng_pump_section_low[i][1] = 1; /* engines stopped */
        I.gen_potential_normal[i] = 0;
    }
    I.fuel_on_board_kg = 120000.0f;
    I.hyd_green_psi = I.hyd_yellow_psi = 0.0f;
    I.all_gear_uplocked = 0;
    for (i = 0; i < 3; i++) {
        I.adr_cas_valid[i] = I.adr_mach_valid[i] = I.adr_alt_valid[i] = 1;
        I.adr_alt_ft[i] = 100.0f;
        I.ir_gs_valid[i] = 1;
    }
}

static void engines(int run)
{
    int i;
    for (i = 0; i < 4; i++) {
        I.eng_master_on[i] = (unsigned char)run;
        I.eng_state[i] = run ? FWS_ENG_ON : FWS_ENG_OFF;
        I.n1_pct[i] = run ? 20.0f : 0.0f;
        I.n3_pct[i] = run ? 65.0f : 0.0f;
        I.egt_c[i] = run ? 450.0f : 20.0f;
        I.gen_potential_normal[i] = (unsigned char)run;
        I.eng_pump_section_low[i][0] = I.eng_pump_section_low[i][1] = (unsigned char)!run;
    }
    I.hyd_green_psi = I.hyd_yellow_psi = run ? 5000.0f : 0.0f;
}

static void cas(float kt)
{
    int i;
    for (i = 0; i < 3; i++) {
        I.adr_cas_kt[i] = kt;
        I.ir_gs_kt[i] = kt;
    }
}

static void ra(float ft)
{
    int i;
    for (i = 0; i < 3; i++) {
        I.ra_ft[i] = ft;
        I.ra_ssm[i] = ft > 8192.0f ? FWS_SSM_NCD : FWS_SSM_NORMAL;
    }
}

static void alt(float ft)
{
    int i;
    for (i = 0; i < 3; i++)
        I.adr_alt_ft[i] = ft;
}

static void tla(float deg)
{
    int i;
    for (i = 0; i < 4; i++)
        I.tla_deg[i] = deg;
}

static void airborne(int air)
{
    I.sim_on_ground = (unsigned char)!air;
    I.main_gear_compressed = (unsigned char)!air;
}

static void gear(int down)
{
    I.gear_lever_down = (unsigned char)down;
    I.nose_downlocked = I.main_left_downlocked = I.main_right_downlocked = (unsigned char)down;
    I.all_gear_uplocked = (unsigned char)!down;
    I.gear_extended_ratio = down ? 1.0f : 0.0f;
    I.nose_gear_ratio = down ? 1.0f : 0.0f;
}

static void flaps(int handle, float flap_deg, float slat_deg)
{
    I.flaps_handle_index = (unsigned char)handle;
    I.flap_angle_deg = flap_deg;
    I.slat_angle_deg = slat_deg;
}

/* phase 8 cruise state used by many alert tests */
static void cruise(void)
{
    reset_sim();
    engines(1);
    airborne(1);
    gear(0);
    flaps(0, 0, 0);
    tla(25.0f);
    cas(290.0f);
    alt(35000.0f);
    ra(9000.0f);
    I.fms_valid = 1;
    I.v1_kt = 0.0f;
    I.gnd_splr_armed = 0;
    step(5.0);
}

/* ------------------------------------------------------------------------------------------ */
static void test_defaults(void)
{
    unsigned first = 0, m[10];
    int i, n_active = 0;
    printf("[defaults]\n");
    fws_signals_init(&S);
    fws_inputs_defaults(&I);
    T = 0.0;
    step(10.0);
    for (i = 0; i < fws_signals_alert_count(); i++)
        if (!fws_signals_alert_desc(i)->deferred && fws_signals_alert_active(&S, i))
            n_active++;
    CHECK(n_active == 0, "%d alerts active with the healthy defaults", n_active);
    CHECK(count_unexpected(&first) == 0, "first %u", first);
    CHECK(phase() == 1, "phase %d", phase());
    CHECK(fws_signals_memos_right(&S, m, 10) == 0, "right memos with defaults");
    CHECK(fws_signals_memos_left(&S, m, 10) == 0, "left memos with defaults");
    CHECK(S.lists.ecam_status_normal, "STS not normal");
    /* deferred procedures are Subject.create(true) (FwsAbnormalSensed:4841) */
    CHECK(act(210700001u) && act(320700001u), "deferred procs are always active");
}

static void test_descriptors(void)
{
    const FwsAlertDesc *d;
    int i, prev = 0, n_fbw = 0, n_ext = 0, n_def = 0;
    printf("[descriptors]\n");
    for (i = 0; i < fws_signals_alert_count(); i++) {
        d = fws_signals_alert_desc(i);
        CHECK(d->n_items <= FWS_MAX_ITEMS, "%u", d->id);
        if (!d->deferred) {
            /* Object.entries order = ascending integer keys */
            CHECK((int)(d->id > (unsigned)prev) || i == 0, "table not sorted at %u", d->id);
            prev = (int)d->id;
        }
        if (d->deferred) n_def++;
        else if (d->port_extension) n_ext++;
        else n_fbw++;
    }
    printf("  %d FBW sensed alerts, %d port extensions, %d deferred procedures\n", n_fbw, n_ext, n_def);
    /* PACK 1 OFF flightPhaseInhib [1,2,3,4,5,6,7,9,10,11,12] (FwsAbnormalSensed:489) */
    d = fws_signals_alert_desc(idx(211800011u));
    CHECK(d->phase_inhib == (0x1FFEu & ~FWS_PHASE_BIT(8)), "PACK 1 OFF inhib %x", d->phase_inhib);
    CHECK(d->not_active_when[0] == 211800009u && d->not_active_when[1] == 211800021u, "PACK 1 OFF suppression");
    /* L/G NOT DOWN NO CANCEL: cancel false (FwsAbnormalSensed:3694) */
    d = fws_signals_alert_desc(idx(320800037u));
    CHECK(d->cancel == 0 && d->failure == 3 && d->sys_page == FWS_SD_WHEEL, "L/G NOT DOWN descriptor");
    /* AP OFF: monitorConfirmTime 0 (FwsAbnormalSensed:1674) */
    d = fws_signals_alert_desc(idx(220800001u));
    CHECK(d->monitor_confirm_s == 0.0f && d->phase_inhib == 0 && d->has_aural, "AP OFF descriptor");
    /* ENG FIRE phase inhibition [5, 6] (FwsAbnormalSensed:1979) */
    d = fws_signals_alert_desc(idx(260800005u));
    CHECK(d->phase_inhib == (FWS_PHASE_BIT(5) | FWS_PHASE_BIT(6)) && d->recommendation == 1, "ENG 1 FIRE descriptor");
    /* A/THR OFF memo caution nonProcedureKey (FwsAbnormalSensed:1700) */
    CHECK(fws_signals_alert_desc(idx(220000002u))->non_procedure, "220000002 non procedure");
}

/* ------------------------------------------------------------------------------------------ */
static void test_flight(void)
{
    int seq[32], n_seq = 0, last = -1, i;
    unsigned m[10], first = 0;
    int nm, unexpected = 0;
    static const int exp_seq[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 1};
    FwsSignalOutputs o;

#define TRACK()                                                    \
    do {                                                           \
        if (phase() != last && n_seq < 32) {                       \
            last = phase();                                        \
            seq[n_seq++] = last;                                   \
        }                                                          \
        if (count_unexpected(&first))                              \
            unexpected++;                                          \
    } while (0)
#define RUN(sec)                                                   \
    do {                                                           \
        double t_ = 0;                                             \
        while (t_ < (sec)) {                                       \
            step(DT);                                              \
            t_ += DT;                                              \
            TRACK();                                               \
        }                                                          \
    } while (0)

    printf("[normal flight]\n");
    reset_sim();
    /* cold & dark with EXT PWR, APU running, parking brake set */
    I.ext_connected = 1;
    I.ext_contactor_closed[0] = 1;
    I.ext_avail[0] = I.ext_pb_on[0] = 1;
    I.apu_master_on = 1;
    I.apu_avail = 1;
    I.apu_n_valid = 1;
    I.apu_n_pct = 100.0f;
    I.park_brake_set = 1;
    I.beacon_on = 0;
    RUN(5.0);
    CHECK(phase() == 1, "cold & dark phase %d", phase());
    /* Right memos: dictionary order [ELEC EXT PWR 241000002 (<=1 engine running, FwsMemos:341-357),
     * PARK BRK ON 320000002 (:441-447), APU AVAIL 000017001 (:158-169)], mapOrder() with an empty order
     * reverses it, all green (priority 3) (FwsCore:5934-5985, 6236-6253) */
    nm = fws_signals_memos_right(&S, m, 10);
    {
        static const unsigned exp[] = {17001u, 320000002u, 241000002u};
        CHECK(list_eq(m, nm, exp, 3), "cold & dark right memos");
        if (!list_eq(m, nm, exp, 3))
            print_list("got", m, nm);
    }
    /* SD: phase 1 -> DOOR; APU master on and N > 95 with no timer running (FwsSystemDisplayLogic:168-174) */
    fws_signals_outputs(&S, &o);
    CHECK(o.sd_page_when_unselected == FWS_SD_DOOR, "SD %d", o.sd_page_when_unselected);
    /* BEFORE START normal checklist: BEACON sensed (FwsNormalChecklists:541) */
    {
        signed char v[9];
        I.beacon_on = 1;
        step(DT);
        CHECK(fws_signals_normal_sensed(&S, 1000002u, v, 9) == 3 && v[0] == -1 && v[2] == 1, "beacon sensed");
    }

    /* engine start: phase 2 only once oneEngineRunningConf (30 s, FwsFlightPhases:187) clears phase 1
     * (mixed phases keep the current one, :326-335) */
    I.ext_contactor_closed[0] = 0;
    I.ext_connected = 0;
    I.apu_master_on = 0;
    I.apu_avail = 0;
    I.apu_n_pct = 0.0f;
    engines(1);
    RUN(10.0);
    CHECK(phase() == 1, "phase %d 10 s after engine start", phase());
    RUN(21.0);
    CHECK(phase() == 2, "phase %d 31 s after engine start", phase());
    fws_signals_outputs(&S, &o);
    /* SD in phase 2: ecamFCTLTimer starts at 20 s (FwsSystemDisplayLogic:39, 179-190) */
    CHECK(o.sd_page_when_unselected == FWS_SD_FCTL, "SD phase 2 %d", o.sd_page_when_unselected);
    CHECK(!o.to_memo, "no T.O memo yet");
    /* T.O memo: phase 2 and all engines running for 120 s (FwsFlightPhases:355-363) */
    RUN(90.0);
    fws_signals_outputs(&S, &o);
    CHECK(o.to_memo, "T.O memo after 121 s");
    CHECK(o.sd_page_when_unselected == FWS_SD_WHEEL, "SD phase 2 after 20 s %d", o.sd_page_when_unselected);
    nm = fws_signals_memos_left(&S, m, 10);
    {
        /* whichCodeToReturn (FwsMemos:625-641): belts off, splr not armed, flaps 0, no RTO, no T.O CONFIG */
        static const unsigned exp[] = {1001u, 1002u, 1006u, 1008u, 1010u, 1012u};
        CHECK(list_eq(m, nm, exp, 6), "T.O memo not ready");
        if (!list_eq(m, nm, exp, 6))
            print_list("got", m, nm);
    }
    /* SEAT BELTS memo inhibited while the T.O memo is shown (FwsMemos:452-459) */
    I.seat_belts_on = 1;
    I.gnd_splr_armed = 1;
    flaps(2, 17.0f, 20.0f);
    I.autobrake_mode = 6;
    I.fms_valid = 1;
    I.v1_kt = 150.0f;
    I.fms_to_flaps = 2;
    I.park_brake_set = 0;
    RUN(1.0);
    CHECK(!memo_right_has(335000001u) && !memo_right_has(271000001u), "SEAT BELTS / GND SPLRs memos inhibited");
    /* T.O CONFIG pressed: toConfigNormal (FwsCore:5486-5510) */
    I.to_config_pb = 1;
    RUN(0.5);
    I.to_config_pb = 0;
    RUN(1.0);
    nm = fws_signals_memos_left(&S, m, 10);
    {
        static const unsigned exp[] = {1001u, 1003u, 1007u, 1009u, 1011u, 1013u};
        CHECK(list_eq(m, nm, exp, 6), "T.O memo ready");
        if (!list_eq(m, nm, exp, 6))
            print_list("got", m, nm);
    }
    {
        /* TAXI checklist sensed items (FwsNormalChecklists:547-559) */
        signed char v[9];
        int n = fws_signals_normal_sensed(&S, 1000004u, v, 9);
        CHECK(n == 9 && v[0] == -1 && v[3] == 0 && v[4] == 1 && v[5] == 1 && v[6] == 1 && v[7] == 1 && v[8] == 1,
              "TAXI sensed");
    }

    /* take-off */
    tla(45.0f);
    RUN(1.0);
    CHECK(phase() == 3, "T.O power phase %d", phase());
    fws_signals_outputs(&S, &o);
    CHECK(o.sd_page_when_unselected == FWS_SD_ENG, "SD phase 3");
    CHECK(o.show_takeoff_inhibit == 0, "T.O INHIBIT needs 3 s");
    RUN(3.0);
    CHECK(memo_right_has(314000001u), "T.O INHIBIT memo (toInhibitTimer 3 s, FwsCore:2984)");
    cas(100.0f);
    RUN(2.0);
    CHECK(phase() == 4, "phase %d at 100 kt", phase());
    cas(160.0f); /* V1 150 + 3 (FwsFlightPhases:221-226) */
    RUN(1.0);
    CHECK(phase() == 5, "phase %d above V1", phase());
    cas(170.0f);
    airborne(1);
    ra(30.0f);
    I.gnd_splr_armed = 0;
    RUN(1.0);
    CHECK(phase() == 6, "phase %d at lift-off", phase());
    ra(600.0f);
    gear(0);
    RUN(2.0);
    CHECK(phase() == 7, "phase %d above 400 ft", phase());
    ra(2000.0f);
    alt(4000.0f);
    tla(25.0f);
    flaps(0, 0.0f, 0.0f);
    cas(250.0f);
    RUN(2.0);
    CHECK(phase() == 8, "phase %d above 1500 ft", phase());
    ra(9000.0f);
    alt(35000.0f);
    cas(280.0f);
    RUN(70.0);
    fws_signals_outputs(&S, &o);
    CHECK(o.sd_page_when_unselected == FWS_SD_CRZ, "SD cruise %d", o.sd_page_when_unselected);
    CHECK(!o.ldg_memo, "no LDG memo in cruise");

    /* approach: LDG memo needs RA > 2200 for 1 s with valid RAs, then < 2000 (FwsFlightPhases:368-414) */
    alt(3000.0f);
    cas(180.0f);
    ra(2500.0f);
    RUN(2.0);
    gear(1);
    flaps(4, 33.0f, 23.0f);
    I.gnd_splr_armed = 1;
    ra(1900.0f);
    RUN(1.0);
    fws_signals_outputs(&S, &o);
    CHECK(phase() == 8 && o.ldg_memo, "LDG memo below 2000 ft (phase %d)", phase());
    nm = fws_signals_memos_left(&S, m, 10);
    {
        /* LDG memo (FwsMemos:651-669): belts on, gear downlocked, splr armed, FULL (no CONF 3 landing) */
        static const unsigned exp[] = {2001u, 2003u, 2007u, 2009u, 2011u};
        CHECK(list_eq(m, nm, exp, 5), "LDG memo");
        if (!list_eq(m, nm, exp, 5))
            print_list("got", m, nm);
    }
    CHECK(o.sd_page_when_unselected == FWS_SD_WHEEL, "SD gear down below 16000 ft");
    tla(0.0f);
    ra(700.0f);
    cas(150.0f);
    RUN(2.0);
    CHECK(phase() == 9, "phase %d below 800 ft", phase());
    RUN(4.0);
    CHECK(memo_right_has(314000002u), "LDG INHIBIT memo");
    /* touch-down */
    airborne(0);
    ra(0.0f);
    cas(130.0f);
    RUN(2.0);
    CHECK(phase() == 10, "phase %d at touch-down", phase());
    cas(60.0f);
    RUN(3.0);
    CHECK(phase() == 11, "phase %d below 80 kt", phase());
    /* shutdown: phase 12 once no engine runs for 30 s, phase 1 after the 300 s monostable
     * (FwsFlightPhases:281-289) */
    cas(0.0f);
    I.park_brake_set = 1;
    engines(0);
    RUN(31.0);
    CHECK(phase() == 12, "phase %d after shutdown", phase());
    {
        /* PARKING checklist: ENGINES OFF sensed, FUEL PUMPs not (pumps on) (FwsNormalChecklists:582-592) */
        signed char v[4];
        fws_signals_normal_sensed(&S, 1000013u, v, 4);
        CHECK(v[0] == -1 && v[1] == 1 && v[3] == 0, "PARKING sensed");
        for (i = 0; i < 4; i++)
            I.feed_pump_main_on[i] = I.feed_pump_stby_on[i] = 0;
        step(DT);
        fws_signals_normal_sensed(&S, 1000013u, v, 4);
        CHECK(v[3] == 1, "PARKING fuel pumps off sensed");
    }
    RUN(301.0);
    CHECK(phase() == 1, "phase %d 300 s after phase 12", phase());

    CHECK(n_seq == 13 && memcmp(seq, exp_seq, sizeof exp_seq) == 0, "phase sequence");
    printf("  phase sequence:");
    for (i = 0; i < n_seq; i++)
        printf(" %d", seq[i]);
    printf("\n");
    CHECK(unexpected == 0, "%d cycles with an unexpected confirmed alert (first %u)", unexpected, first);
#undef RUN
#undef TRACK
}

/* ------------------------------------------------------------------------------------------ */
static void test_eng_fire(void)
{
    FwsAlertEval e;
    printf("[ENG 1 FIRE]\n");
    cruise();
    I.eng_fire_detected[0] = 1;
    step(DT);
    CHECK(act(260800005u) && !act(260800009u), "in-flight variant (FwsAbnormalSensed:1981-1985)");
    CHECK(!conf(260800005u), "monitor confirm 0.6 s");
    step(0.75);
    CHECK(conf(260800005u), "confirmed");
    eval(260800005u, &e);
    /* whichItemsToShow [1,1,1,apuAvail,firePb,1,1,1,1], whichItemsChecked (:1987-2008) */
    CHECK(e.n_items == 9 && e.items_to_show[3] == 0 && e.items_to_show[4] == 0, "show");
    CHECK(!e.items_checked[0] && !e.items_checked[1] && !e.items_checked[2] && e.items_checked[3], "checked start");
    CHECK(e.aural == FWS_AURAL_CRC, "CRC until the fire pb is released (FwsCore:5214)");
    /* THR LEVER 1 IDLE */
    I.tla_deg[0] = 0.0f;
    step(DT);
    eval(260800005u, &e);
    CHECK(e.items_checked[0], "thrust lever idle checked");
    /* ENG MASTER 1 OFF: 10 s agent clock starts (FwsCore:5235-5240) */
    I.eng_master_on[0] = 0;
    step(DT);
    eval(260800005u, &e);
    CHECK(e.items_checked[1] && e.has_items_timer && e.timer_valid[5] && fabs(e.items_timer_s[5] - T) < 1e-9,
          "master off + agent 1 timer");
    /* FIRE pb released: item 2 checked, item 0 reset, item 4 shown, aural stops */
    I.eng_fire_pb_released[0] = 1;
    step(DT);
    eval(260800005u, &e);
    CHECK(e.items_checked[2] && !e.items_checked[0] && e.items_to_show[4] && e.aural == FWS_AURAL_NONE, "fire pb");
    /* AGENT 1 discharged: 30 s clock (FwsCore:5241-5246) */
    I.eng_agent_discharged[0][0] = 1;
    step(DT);
    eval(260800005u, &e);
    CHECK(e.items_checked[5] && !e.timer_valid[5] && e.timer_valid[7], "agent 1");
    /* LAND ASAP limitation (FwsLimitations:28-32, landAsap FwsCore:5540) */
    step(1.1);
    CHECK(idlist_has(&S.lists.limitations_all_phases, 1u) && idlist_has(&S.lists.limitations_pfd, 1u), "LAND ASAP");
    /* APU BLEED DO NOT USE limitation follows fireButtonEng1 (FwsLimitations:45-49) */
    CHECK(idlist_has(&S.lists.limitations_all_phases, 260400001u), "APU BLEED DO NOT USE");

    printf("[ENG 2 FIRE on ground]\n");
    reset_sim();
    engines(1);
    tla(20.0f);
    step(40.0);
    I.eng_fire_detected[1] = 1;
    step(1.0);
    CHECK(act(260800010u) && !act(260800006u), "on-ground variant");
    eval(260800010u, &e);
    CHECK(e.n_items == 16 && !e.items_checked[0], "16 items (FwsAbnormalSensed:2121-2163)");
    tla(0.0f);
    I.park_brake_set = 1;
    step(DT);
    eval(260800010u, &e);
    CHECK(e.items_checked[0] && e.items_checked[1] && e.items_checked[2], "levers idle, park brake");
    I.fire_test_pb = 1;
    step(DT);
    eval(260800010u, &e);
    CHECK(!e.items_to_show[0], "items hidden during FIRE TEST");
}

static void test_ap_off(void)
{
    FwsSignalOutputs o;
    unsigned m[10];
    int n;
    printf("[AP OFF]\n");
    cruise();
    I.ap_engaged = 1;
    step(2.0);
    /* involuntary: AP disengages without instinctive disconnect (FwsCore:3667-3712) */
    I.ap_engaged = 0;
    step(DT);
    fws_signals_outputs(&S, &o);
    CHECK(act(220800001u) && o.master_warning_from_ap_off && o.aural_cavalry_charge, "involuntary AP OFF");
    {
        FwsAlertEval e;
        eval(220800001u, &e);
        CHECK(e.aural == FWS_AURAL_CAVALRY_CHARGE, "AP OFF aural");
    }
    CHECK(memo_right_has(220000001u), "AP OFF memo (FwsMemos:203)");
    n = fws_signals_memos_pfd(&S, m, 3);
    CHECK(n >= 1 && m[0] == 220000001u, "AP OFF PFD memo (red, priority 1)");
    /* acknowledged with the instinctive disconnect pb (discPbPressedAfterDisconnection) */
    I.ap_instinctive_disc_count = 1;
    step(DT);
    I.ap_instinctive_disc_count = 0;
    step(DT);
    fws_signals_outputs(&S, &o);
    CHECK(!act(220800001u) && !o.master_warning_from_ap_off, "acknowledged");
    /* voluntary: instinctive disconnect then AP disengagement within 1.9 s, second press after the
     * first 0.3 s of cavalry charge acknowledges (autoPilotInstinctiveDiscPressedTwiceInLast1p9Sec) */
    I.ap_engaged = 1;
    step(2.0);
    I.ap_instinctive_disc_count = 1;
    step(DT);
    I.ap_instinctive_disc_count = 0;
    I.ap_engaged = 0;
    step(DT);
    fws_signals_outputs(&S, &o);
    CHECK(!act(220800001u) && memo_right_has(220000001u) && o.aural_cavalry_charge, "voluntary AP OFF: memo + CC");
    step(0.5);
    I.ap_instinctive_disc_count = 1;
    step(DT);
    I.ap_instinctive_disc_count = 0;
    step(DT);
    fws_signals_outputs(&S, &o);
    CHECK(!memo_right_has(220000001u) && !act(220800001u) && !o.aural_cavalry_charge, "second press ends it");
    /* without the second press the unacknowledged disconnection becomes the AP OFF warning once the
     * 1.9 s voluntary window ends (autoPilotOffInvoluntaryMemory, FwsCore:3707-3712) */
    I.ap_engaged = 1;
    step(2.0);
    I.ap_instinctive_disc_count = 1;
    step(DT);
    I.ap_instinctive_disc_count = 0;
    I.ap_engaged = 0;
    step(1.0);
    CHECK(!act(220800001u), "still voluntary at 1 s");
    step(1.5);
    CHECK(act(220800001u), "escalates after 1.9 s without acknowledgement");
}

static void test_elec(void)
{
    FwsAlertEval e;
    printf("[ELEC]\n");
    cruise();
    /* [EXT] GEN 1 FAULT from the native network failure */
    I.gen_fault[0] = 1;
    step(1.0);
    CHECK(conf(240800061u), "GEN 1 FAULT");
    eval(240800061u, &e);
    CHECK(e.n_items == 1 && !e.items_checked[0], "GEN 1 OFF not done");
    I.gen_pb_on[0] = 0;
    step(DT);
    eval(240800061u, &e);
    CHECK(act(240800061u) && e.items_checked[0] && !act(240800065u), "GEN 1 pb OFF checks the item, no GEN OFF");
    /* AC BUS 1 lost while the others are powered */
    I.ac_powered[0] = 0;
    step(1.0);
    CHECK(conf(240800002u) && !act(240800055u), "AC BUS 1 FAULT, no EMER CONFIG");
    /* all AC lost in flight: EMER CONFIG (red, LAND ASAP) instead of the single bus faults */
    I.ac_powered[1] = I.ac_powered[2] = I.ac_powered[3] = 0;
    step(1.0);
    CHECK(conf(240800055u) && !act(240800002u), "EMER CONFIG");
    eval(240800055u, &e);
    CHECK(e.n_items == 30 && !e.items_checked[0], "EMER CONFIG items");
    I.rat_stow_ratio = 1.0f;
    step(DT);
    eval(240800055u, &e);
    CHECK(e.items_checked[0] && memo_right_has(242000002u), "RAT MAN ON checked, RAT OUT memo (green in flight)");
    /* healthy network on ground: no ELEC alert */
    reset_sim();
    engines(1);
    step(40.0);
    CHECK(count_unexpected(NULL) == 0, "phase 2 healthy");
    /* DC 1 + DC ESS lost: DC BUS 1+ESS FAULT suppresses DC BUS 1 / DC ESS BUS FAULT */
    cruise();
    I.dc1_powered = 0;
    I.dc_ess_powered = 0;
    step(1.0);
    CHECK(conf(240800028u) && !conf(240800026u) && !conf(240800030u) && act(240800026u), "DC suppression");
}

static void test_hyd(void)
{
    FwsAlertEval e;
    printf("[HYD G SYS LO PRESS]\n");
    cruise();
    I.hyd_green_psi = 1000.0f;
    I.eng_pump_section_low[0][0] = I.eng_pump_section_low[0][1] = 1;
    I.eng_pump_section_low[1][0] = I.eng_pump_section_low[1][1] = 1;
    step(DT);
    /* eng1Or2RunningAndPhaseConfirmationNode (1 s) gates greenAbnormLoPressure (FwsCore:3218-3230) */
    CHECK(act(290800035u), "green lo immediately (engines already confirmed running)");
    step(1.0);
    CHECK(conf(290800035u) && !act(290800039u), "G SYS LO PRESS confirmed");
    /* ENG PMP PRESS LO suppressed by greenYellowAbnormLoPressure only, not by green lo */
    CHECK(act(290800005u), "G ENG 1 PMP A PRESS LO (FwsCore:3302-3308)");
    eval(290800035u, &e);
    CHECK(e.n_items == 8 && !e.items_to_show[0] && e.items_to_show[1], "show (FwsAbnormalSensed:3460-3469)");
    CHECK(act(999800001u) && act(999800003u), "*F/CTL and *WHEEL secondary failures");
    step(1.0);
    {
        /* per-alert lists (FwsAbnormalSensed:3471-3477) then FwsInopSys entries (FwsCore:5987-6006) */
        static const unsigned inop[] = {290100001u, 320300023u, 290300021u};
        static const unsigned lim_appr[] = {320400001u, 290400001u, 290400002u, 320400002u, 320400003u};
        CHECK(list_eq(S.lists.inop_sys_all_phases.id, (int)S.lists.inop_sys_all_phases.n, inop, 3), "inop all");
        if (!list_eq(S.lists.inop_sys_all_phases.id, (int)S.lists.inop_sys_all_phases.n, inop, 3))
            print_list("got", S.lists.inop_sys_all_phases.id, (int)S.lists.inop_sys_all_phases.n);
        CHECK(list_eq(S.lists.limitations_appr_ldg.id, (int)S.lists.limitations_appr_ldg.n, lim_appr, 5), "lim");
        CHECK(idlist_has(&S.lists.information, 220200011u) && idlist_has(&S.lists.inop_sys_appr_ldg, 290100003u),
              "info / inop appr");
        CHECK(!S.lists.ecam_status_normal, "STS");
    }
    /* yellow too: G + Y SYS LO PRESS suppresses both singles */
    I.hyd_yellow_psi = 1000.0f;
    step(1.0);
    CHECK(conf(290800039u) && !conf(290800035u) && !conf(290800036u), "G+Y suppression");
    CHECK(!act(290800005u), "pump alerts suppressed by G+Y lo press");
}

static void test_fuel(void)
{
    FwsAlertEval e;
    int i;
    printf("[FUEL]\n");
    cruise();
    I.feed_tank_kg[0] = 1000.0f;
    step(29.0);
    CHECK(!act(281800023u), "FEED TK 1 LEVEL LO needs 30 s (FwsCore:4553)");
    step(1.5);
    CHECK(act(281800023u), "FEED TK 1 LEVEL LO");
    step(1.0);
    CHECK(conf(281800023u), "confirmed");
    for (i = 1; i < 4; i++)
        I.feed_tank_kg[i] = 1000.0f;
    step(32.0);
    CHECK(conf(281800002u) && !conf(281800023u) && !conf(281800102u), "ALL FEED TKs suppresses the others");
    eval(281800002u, &e);
    CHECK(e.n_items == 7 && !e.items_checked[0] && e.items_checked[1], "pumps on, crossfeeds closed");
    for (i = 0; i < 4; i++)
        I.crossfeed_open[i] = 1;
    step(DT);
    eval(281800002u, &e);
    /* CROSSFEED OPEN memo: 280000001 (green) allowed in phase 8, 280000013 (amber) inhibited
     * (FwsMemos:52-59, 101-108) */
    CHECK(e.items_checked[0] && memo_right_has(280000001u) && !memo_right_has(280000013u), "crossfeed");
    /* LAND ANSA from the limitation of ALL FEED TKs LEVEL LO (FwsAbnormalSensed:3080) */
    CHECK(idlist_has(&S.lists.limitations_all_phases, 2u), "LAND ANSA");

    printf("[FUEL pumps]\n");
    reset_sim();
    engines(1);
    step(40.0);
    I.feed_pump_main_fault[0] = 1;
    step(1.0);
    CHECK(conf(281800031u) && fws_signals_alert_desc(idx(281800031u))->failure == 1, "MAIN PMP FAULT (level 1)");
    I.feed_pump_stby_valid = 1;
    I.feed_pump_stby_fault[0] = 1;
    step(1.0);
    CHECK(conf(281800027u) && !conf(281800031u), "MAIN + STBY PMPs FAULT suppresses MAIN PMP FAULT");
}

static void test_gear(void)
{
    FwsSignalOutputs o;
    printf("[L/G NOT DOWN]\n");
    cruise();
    tla(0.0f);
    alt(2000.0f);
    cas(170.0f);
    ra(600.0f);
    flaps(4, 33.0f, 23.0f); /* flapsInConf3OrFull (FwsCore:4733) */
    step(DT);
    /* below750Condition, lgNotDownNoCancel starts one cycle after the reset pulse (FwsCore:5013-5021) */
    step(DT);
    CHECK(act(320800037u) && !act(320800038u), "NO CANCEL variant");
    fws_signals_outputs(&S, &o);
    CHECK(o.lg_lever_red_arrow, "red arrow");
    flaps(1, 8.0f, 20.0f);
    step(DT);
    CHECK(!act(320800037u) && act(320800038u), "CANCEL variant (lgNotDown, FwsCore:5030-5038)");
    gear(1);
    step(DT);
    CHECK(!act(320800038u), "gear down");
    printf("[L/G NOT LOCKED DOWN, ext]\n");
    I.main_left_downlocked = 0;
    step(29.0);
    CHECK(!act(320800039u), "30 s");
    step(1.5);
    CHECK(act(320800039u), "GEAR NOT LOCKED DOWN");
}

/* L/G GRVTY switch (mip.xml L:A32NX_LG_GRVTY_SWITCH_POS 0 RESET, 1 OFF, 2 DOWN). */
static void test_lg_grvty(void)
{
    FwsAlertEval e;
    printf("[L/G GRVTY switch items]\n");
    cruise();
    CHECK(I.lg_grvty_switch_pos == 1, "defaults: switch OFF");
    eval(320700001u, &e);
    /* FwsAbnormalSensed 320700001: [-, LEVER UP, GRVTY_SWITCH_POS === 2, all downlocked, LEVER DOWN] */
    CHECK(e.n_items == 5 && !e.items_checked[0] && e.items_checked[1] && !e.items_checked[2] &&
          !e.items_checked[3] && !e.items_checked[4], "lever UP, switch OFF");
    I.lg_grvty_switch_pos = 0;
    step(DT);
    eval(320700001u, &e);
    CHECK(!e.items_checked[2], "RESET is not DOWN");
    I.lg_grvty_switch_pos = 2;
    step(DT);
    eval(320700001u, &e);
    CHECK(e.items_checked[2] && e.items_checked[1], "L/G GRVTY DOWN sensed");
    gear(1);
    step(DT);
    eval(320700001u, &e);
    CHECK(e.items_checked[2] && e.items_checked[3] && e.items_checked[4] && !e.items_checked[1],
          "gear locked down, lever DOWN, switch still DOWN");
    /* Port L/G GEAR NOT LOCKED DOWN items: [2] LEVER UP, [3] L/G GRVTY DOWN, [4] LEVER DOWN */
    gear(0);
    I.lg_grvty_switch_pos = 2;
    step(DT);
    eval(320800039u, &e);
    CHECK(e.items_checked[2] && e.items_checked[3] && !e.items_checked[4], "320800039 L/G GRVTY DOWN sensed");
    I.lg_grvty_switch_pos = 1;
    step(DT);
    eval(320800039u, &e);
    CHECK(!e.items_checked[3], "320800039 switch OFF");
}

static void test_to_config(void)
{
    FwsAlertEval e;
    printf("[T.O CONFIG]\n");
    reset_sim();
    engines(1);
    I.fms_valid = 1;
    step(40.0);
    CHECK(phase() == 2, "phase 2");
    flaps(0, 0.0f, 0.0f);
    I.park_brake_set = 1;
    step(1.0);
    CHECK(!act(272800002u), "no warning without T.O CONFIG");
    /* T.O CONFIG pressed: toConfigTestHeldMin1s5Pulse && flapsNotTo (FwsCore:4741-4747) */
    I.to_config_pb = 1;
    step(DT);
    CHECK(act(272800002u) && act(272800001u), "FLAPS / SLAT NOT IN T.O CONFIG");
    eval(272800002u, &e);
    CHECK(e.aural == FWS_AURAL_CRC, "CRC");
    I.to_config_pb = 0;
    step(1.0);
    CHECK(act(272800002u), "held min 1.5 s");
    step(1.0);
    CHECK(!act(272800002u), "released");
    /* phase 3 with flaps 0 and the parking brake set */
    tla(45.0f);
    step(1.0);
    CHECK(phase() == 3, "phase 3");
    CHECK(act(272800002u) && act(320800030u), "FLAPS NOT IN T.O CONFIG + CONFIG PARK BRK ON in phase 3");
    CHECK(!(fws_signals_alert_desc(idx(320800030u))->phase_inhib & FWS_PHASE_BIT(3)), "phase 3 allowed");
    flaps(2, 17.0f, 20.0f);
    I.park_brake_set = 0;
    step(DT);
    CHECK(!act(272800002u) && !act(320800030u), "reset by config");
    /* pitch trim out of the green band */
    I.pitch_trim_deg = 7.0f;
    step(DT);
    CHECK(act(271800003u), "PITCH TRIM NOT IN TO RANGE");
    I.pitch_trim_deg = 2.0f;
    /* INHIBITED BY DOORS: cabin door open with TLA 2 at T.O power (FwsCore:4400-4415), allowed in
     * phase 3 (FwsAbnormalSensed:1135) */
    I.main_door_open[0] = 1;
    step(1.0);
    CHECK(conf(213800009u) && !(fws_signals_alert_desc(idx(213800009u))->phase_inhib & FWS_PHASE_BIT(3)),
          "INHIBITED BY DOORS");
    CHECK(act(520800017u), "MAIN 1L NOT CLOSED active (inhibited in phase 3)");
}

static void test_inhibition(void)
{
    int i, n = 0;
    printf("[inhibition]\n");
    /* ENG 1 BLEED OFF [3,4,5,6,7,9,10] (FwsAbnormalSensed:828): active in phase 8 and 2, inhibited in 3 */
    cruise();
    I.eng_bleed_pb_auto[0] = 0;
    step(1.0);
    CHECK(conf(211800052u) && !(fws_signals_alert_desc(idx(211800052u))->phase_inhib & FWS_PHASE_BIT(8)), "ph 8");
    CHECK(idlist_has(&S.lists.inop_sys_all_phases, 260300002u), "ENG 1 BLEED inop sys (per-alert list)");
    reset_sim();
    engines(1);
    I.eng_bleed_pb_auto[0] = 0;
    step(40.0);
    tla(45.0f);
    step(1.0);
    CHECK(phase() == 3 && act(211800052u) &&
              (fws_signals_alert_desc(idx(211800052u))->phase_inhib & FWS_PHASE_BIT(3)),
          "active but inhibited in phase 3");
    /* default STS assembly drops phase-inhibited alerts */
    CHECK(!idlist_has(&S.lists.inop_sys_all_phases, 260300002u), "inhibited alert not in STS lists");
    for (i = 0; i < fws_signals_alert_count(); i++)
        if (fws_signals_alert_desc(i)->phase_inhib & ~0x1FFEu)
            n++;
    CHECK(n == 0, "phase masks only use bits 1..12");
}

static void test_adr(void)
{
    FwsAlertEval e;
    unsigned out[8];
    int n;
    printf("[ADR]\n");
    cruise();
    I.adr_fault[0] = 1;
    step(1.0);
    CHECK(conf(340800001u), "ADR 1 FAULT");
    eval(340800001u, &e);
    CHECK(e.n_items == 4 && !e.items_checked[0] && !e.items_checked[1] && e.items_checked[2], "items");
    n = fws_signals_alert_list(&S, idx(340800001u), FWS_LIST_INOP_ALL, e.items_checked, e.n_items, out, 8);
    {
        static const unsigned exp[] = {340300004u, 340300011u, 340300001u};
        CHECK(list_eq(out, n, exp, 3), "ADR 1 inop list (FwsAbnormalSensed:3737-3741)");
    }
    I.air_data_knob = 0;
    I.adr_pb_on[0] = 0;
    step(DT);
    eval(340800001u, &e);
    CHECK(e.items_checked[0] && e.items_checked[1] && memo_right_has(340068001u), "CAPT ON 3, ADR 1 OFF, ADIRS SWTG");
    I.adr_fault[1] = 1;
    step(1.0);
    CHECK(conf(340800004u) && !conf(340800001u), "ADR 1+2 suppresses ADR 1");
}

static void test_cab_press_eng(void)
{
    FwsAlertEval e;
    printf("[EXCESS CAB ALT / ENG ext]\n");
    cruise();
    I.cabin_alt_valid = 1;
    I.cabin_alt_ft = 8000.0f;
    I.cabin_diff_psi = 8.0f;
    step(1.0);
    CHECK(!act(213800001u), "cabin 8000 ft");
    /* CPCS is_excessive_alt: > 9550 ft and 1000 ft above the departure / landing elevations */
    I.cabin_alt_ft = 11000.0f;
    step(1.0);
    CHECK(conf(213800001u), "EXCESS CAB ALT");
    eval(213800001u, &e);
    /* FL 350: items 3..11 (emergency descent) shown, 1..2 not (FwsAbnormalSensed:995-1018) */
    CHECK(e.n_items == 23 && e.items_to_show[0] && !e.items_to_show[1] && e.items_to_show[3], "show");
    tla(0.0f);
    I.speedbrake_lever_out = 1;
    step(DT);
    eval(213800001u, &e);
    CHECK(e.items_checked[6] && e.items_checked[7], "levers idle, speed brake");
    CHECK(idlist_has(&S.lists.limitations_all_phases, 210400002u), "limitation 210400002");
    /* [EXT] engine parameters */
    cruise();
    I.oil_press_low[2] = 1;
    I.eng_limits_valid = 1;
    I.egt_red_c = 900.0f;
    I.n1_red_pct = 100.0f;
    I.n3_red_pct = 100.0f;
    I.egt_c[0] = 950.0f;
    step(1.0);
    CHECK(conf(701800087u) && conf(701800009u) && !act(701800073u), "OIL PRESS LO 3, EGT OVER LIMIT 1");
}

static void test_memos_misc(void)
{
    unsigned m[10];
    int n;
    printf("[memos]\n");
    reset_sim();
    /* IR IN ALIGN 5 MN, no engine running: row 4 (FwsCore.adirsMessage1) */
    I.ir_in_align[0] = I.ir_in_align[1] = I.ir_in_align[2] = 1;
    I.ir_align_remaining_s = 290.0f;
    I.eng_mode_selector = 2;
    I.eng_anti_ice_on[0] = 1;
    step(1.0);
    CHECK(memo_right_has(340003005u), "IR IN ALIGN 5 MN");
    I.ir_align_remaining_s = 100.0f;
    step(DT);
    CHECK(memo_right_has(340003103u), "IR IN ALIGN 2 MN (adirsMessage2)");
    n = fws_signals_memos_right(&S, m, 10);
    {
        /* dictionary order [ENG A-ICE 300000001, IR IN ALIGN 340003103, IGNITION 709000001], reversed */
        static const unsigned exp[] = {709000001u, 340003103u, 300000001u};
        CHECK(list_eq(m, n, exp, 3), "order");
        if (!list_eq(m, n, exp, 3))
            print_list("got", m, n);
    }
    n = fws_signals_memos_pfd(&S, m, 3);
    CHECK(n == 1 && m[0] == 300000001u, "PFD memo ENG A-ICE (pfdMemoDisplay)");
    /* TCAS STBY memo after 3 s, not in phase 1 (FwsMemos:537-543) */
    I.tcas_mode = 0;
    step(4.0);
    CHECK(!memo_right_has(343000001u), "TCAS STBY memo inhibited in phase 1");
    cruise();
    I.tcas_mode = 0;
    step(4.0);
    CHECK(memo_right_has(343000001u) && conf(341800019u), "TCAS STBY memo + alert in phase 8");
    /* STROBE LT OFF only in phase 8 */
    I.strobe_off = 1;
    step(DT);
    CHECK(memo_right_has(333000001u), "STROBE LT OFF");
}

int main(void)
{
    test_defaults();
    test_descriptors();
    test_flight();
    test_eng_fire();
    test_ap_off();
    test_elec();
    test_hyd();
    test_fuel();
    test_gear();
    test_lg_grvty();
    test_to_config();
    test_inhibition();
    test_adr();
    test_cab_press_eng();
    test_memos_misc();
    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
