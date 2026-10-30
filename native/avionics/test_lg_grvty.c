/* SPDX-License-Identifier: GPL-3.0-or-later
 * Unit tests of the XPLM-free L/G GRVTY core (a380x_lg_grvty.h): FBW mip.xml interlocks, the
 * A380GravityExtension handle timing (50 deg/s, 200 deg/s, 0..1080 deg, floor(angle/360)), the
 * hydraulic-controller thresholds, cold & dark, and the X-Plane gear-handle ownership.
 *
 *   clang -std=c11 -O2 -Wall -Wextra -Werror test_lg_grvty.c -lm -o test_lg_grvty && ./test_lg_grvty
 */
#include "a380x_lg_grvty.h"

#include <math.h>
#include <stdio.h>

static int failures, checks;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        ++checks;                                                              \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);           \
            printf(__VA_ARGS__);                                               \
            printf("\n");                                                      \
        }                                                                      \
    } while (0)

static void open_all(A380XGrvty *g)
{
    if (!g->master_guard) a380x_grvty_master_toggle(g);
    if (!g->guard[0]) a380x_grvty_guard_toggle(g, 0);
    if (!g->guard[1]) a380x_grvty_guard_toggle(g, 1);
}

/* Simulated time until the handle reaches `turns` (or `limit` seconds). */
static double time_to_turns(A380XGrvty *g, int turns, double dt, double limit)
{
    double t = 0.0;
    while (a380x_grvty_turns(g) != turns && t < limit) { a380x_grvty_update(g, dt, (float)dt); t += dt; }
    return t;
}

static void test_cold_and_dark(void)
{
    A380XGrvty g;
    printf("[cold & dark]\n");
    a380x_grvty_init(&g);
    CHECK(!g.master_guard && !g.guard[0] && !g.guard[1], "covers and guards closed");
    CHECK(g.switch_pos == A380X_GRVTY_OFF, "switch OFF (mip.xml START_STATE 1)");
    CHECK(g.handle_angle_deg == 0.0 && a380x_grvty_turns(&g) == 0, "handle at rest");
    CHECK(!a380x_grvty_hyd_isolated(&g) && !a380x_grvty_doors_released(&g) && !a380x_grvty_gears_released(&g),
          "normal hydraulics, uplocks engaged");
    CHECK(g.master_anim == 0.0f && g.guard_anim[0] == 0.0f && g.guard_anim[1] == 0.0f && g.switch_anim == 1.0f,
          "animation at the OFF / closed poses");
    a380x_grvty_update(&g, 60.0, 1.0f);
    CHECK(g.handle_angle_deg == 0.0 && g.switch_anim == 1.0f, "OFF holds the handle");
}

static void test_interlocks(void)
{
    A380XGrvty g;
    printf("[interlocks]\n");
    a380x_grvty_init(&g);
    CHECK(!a380x_grvty_guard_toggle(&g, 0) && !g.guard[0], "guard 1 needs the master guard");
    CHECK(!a380x_grvty_guard_toggle(&g, 1) && !g.guard[1], "guard 2 needs the master guard");
    CHECK(!a380x_grvty_switch_step(&g, -1) && g.switch_pos == A380X_GRVTY_OFF, "RESET needs the master guard");
    CHECK(!a380x_grvty_switch_step(&g, 1) && g.switch_pos == A380X_GRVTY_OFF, "DOWN needs the master guard");
    CHECK(a380x_grvty_master_toggle(&g) && g.master_guard, "master cover opens unconditionally");
    CHECK(a380x_grvty_switch_step(&g, -1) && g.switch_pos == A380X_GRVTY_RESET, "RESET with master open");
    CHECK(!a380x_grvty_switch_step(&g, -1) && g.switch_pos == A380X_GRVTY_RESET, "no position above RESET");
    CHECK(a380x_grvty_switch_step(&g, 1) && g.switch_pos == A380X_GRVTY_OFF, "back to OFF with master open");
    CHECK(!a380x_grvty_switch_step(&g, 1) && g.switch_pos == A380X_GRVTY_OFF, "DOWN refused, both guards closed");
    CHECK(a380x_grvty_guard_toggle(&g, 0) && g.guard[0], "guard 1 opens with master open");
    CHECK(!a380x_grvty_switch_step(&g, 1) && g.switch_pos == A380X_GRVTY_OFF, "DOWN refused, guard 2 closed");
    CHECK(a380x_grvty_guard_toggle(&g, 0) && !g.guard[0], "guard 1 closes again");
    CHECK(a380x_grvty_guard_toggle(&g, 1) && g.guard[1], "guard 2 opens");
    CHECK(!a380x_grvty_switch_step(&g, 1) && g.switch_pos == A380X_GRVTY_OFF, "DOWN refused, guard 1 closed");
    CHECK(a380x_grvty_guard_toggle(&g, 0) && g.guard[0], "both guards open");
    CHECK(a380x_grvty_switch_allowed(&g, A380X_GRVTY_DOWN), "CODE_POS_2_VERIF true");
    CHECK(a380x_grvty_switch_step(&g, 1) && g.switch_pos == A380X_GRVTY_DOWN, "DOWN accepted");
    CHECK(!a380x_grvty_switch_step(&g, 1) && g.switch_pos == A380X_GRVTY_DOWN, "no position below DOWN");
    /* FBW guards verify only the master guard: they may close over a selected DOWN. */
    CHECK(a380x_grvty_guard_toggle(&g, 1) && !g.guard[1] && g.switch_pos == A380X_GRVTY_DOWN,
          "guard closes, DOWN stays selected until the crew moves it");
    CHECK(a380x_grvty_switch_step(&g, -1) && g.switch_pos == A380X_GRVTY_OFF, "DOWN -> OFF needs only the master");
    CHECK(!a380x_grvty_switch_set(&g, A380X_GRVTY_DOWN), "and back to DOWN needs both guards again");
    a380x_grvty_guard_toggle(&g, 1);
    a380x_grvty_switch_set(&g, A380X_GRVTY_DOWN);
    CHECK(a380x_grvty_master_toggle(&g) && !g.master_guard && g.switch_pos == A380X_GRVTY_DOWN,
          "LOCK1 LEFT_SINGLE_CODE closes the cover unconditionally");
    CHECK(!a380x_grvty_switch_step(&g, -1) && !a380x_grvty_guard_toggle(&g, 0) && g.guard[0],
          "nothing under a closed cover moves");
    CHECK(!a380x_grvty_switch_set(&g, 7) && !a380x_grvty_switch_set(&g, -1) && !a380x_grvty_guard_toggle(&g, 2),
          "out-of-range requests refused");
}

static void test_refused_move_does_not_animate(void)
{
    A380XGrvty g;
    printf("[refused moves do not animate]\n");
    a380x_grvty_init(&g);
    a380x_grvty_master_toggle(&g);
    a380x_grvty_update(&g, 0.0, 1.0f);
    CHECK(g.master_anim == 1.0f, "cover fully open after its swing");
    CHECK(!a380x_grvty_switch_step(&g, 1), "DOWN refused");
    a380x_grvty_update(&g, 0.0, 0.05f);
    CHECK(g.switch_anim == 1.0f && g.switch_pos == A380X_GRVTY_OFF, "switch stays at OFF");
    a380x_grvty_master_toggle(&g);
    CHECK(!a380x_grvty_guard_toggle(&g, 0), "guard refused under the closed cover");
    a380x_grvty_update(&g, 0.0, 0.11f);
    CHECK(g.guard_anim[0] == 0.0f && g.master_anim > 0.4f && g.master_anim < 0.6f,
          "guard still, cover half way at 0.11 s of 0.22 s (%g)", g.master_anim);
    a380x_grvty_update(&g, 0.0, 0.2f);
    CHECK(g.master_anim == 0.0f, "cover closed");
    open_all(&g);
    CHECK(a380x_grvty_switch_step(&g, 1), "DOWN accepted");
    a380x_grvty_update(&g, 0.0, 0.05f);
    CHECK(fabsf(g.switch_anim - 1.5f) < 1e-5f, "switch half way through its 0.1 s throw (%g)", g.switch_anim);
    a380x_grvty_update(&g, 0.0, 0.05f);
    CHECK(g.switch_anim == 2.0f, "switch at DOWN");
}

static void test_handle_timing(void)
{
    A380XGrvty g;
    double t1, t2, t3, dt = 0.001;
    printf("[handle timing]\n");
    a380x_grvty_init(&g);
    open_all(&g);
    a380x_grvty_switch_set(&g, A380X_GRVTY_DOWN);
    t1 = time_to_turns(&g, 1, dt, 60.0);
    t2 = t1 + time_to_turns(&g, 2, dt, 60.0);
    t3 = t2 + time_to_turns(&g, 3, dt, 60.0);
    CHECK(fabs(t1 - 7.2) < 0.0025, "1 turn after 360/50 = 7.2 s (%g)", t1);
    CHECK(fabs(t2 - 14.4) < 0.0025, "2 turns after 14.4 s (%g)", t2);
    CHECK(fabs(t3 - 21.6) < 0.0025, "3 turns after 21.6 s (%g)", t3);
    a380x_grvty_update(&g, 100.0, 0.0f);
    CHECK(g.handle_angle_deg == 1080.0 && a380x_grvty_turns(&g) == 3, "clamped at 3 x 360 deg");
    CHECK(a380x_grvty_hyd_isolated(&g) && a380x_grvty_doors_released(&g) && a380x_grvty_gears_released(&g),
          "all releases at 3 turns");
    /* RESET at 200 deg/s: below 2 turns after 1.8 s, below 1 turn after 3.6 s, at rest after 5.4 s. */
    a380x_grvty_switch_set(&g, A380X_GRVTY_RESET);
    t2 = time_to_turns(&g, 1, dt, 60.0);
    CHECK(fabs(t2 - 1.8) < 0.0025, "RESET: 1 turn after (1080 - 720) / 200 = 1.8 s (%g)", t2);
    t1 = t2 + time_to_turns(&g, 0, dt, 60.0);
    CHECK(fabs(t1 - 3.6) < 0.0025, "RESET: below 1 turn after 3.6 s (%g)", t1);
    a380x_grvty_update(&g, 1.8, 0.0f);
    CHECK(g.handle_angle_deg == 0.0, "RESET: at rest after 5.4 s (%g)", g.handle_angle_deg);
    a380x_grvty_update(&g, 10.0, 0.0f);
    CHECK(g.handle_angle_deg == 0.0, "clamped at 0");
    /* OFF holds an intermediate angle; DOWN continues from it. */
    a380x_grvty_switch_set(&g, A380X_GRVTY_OFF);
    a380x_grvty_switch_set(&g, A380X_GRVTY_DOWN);
    a380x_grvty_update(&g, 10.0, 0.0f);
    a380x_grvty_switch_set(&g, A380X_GRVTY_OFF);
    a380x_grvty_update(&g, 30.0, 0.0f);
    CHECK(fabs(g.handle_angle_deg - 500.0) < 1e-9 && a380x_grvty_turns(&g) == 1 &&
          a380x_grvty_hyd_isolated(&g) && !a380x_grvty_doors_released(&g),
          "OFF keeps 500 deg (1 turn): hydraulics stay isolated");
    a380x_grvty_update(&g, 0.0, 5.0f);
    CHECK(fabs(g.handle_angle_deg - 500.0) < 1e-9, "paused (sim dt 0): handle still");
    a380x_grvty_update(&g, NAN, NAN);
    CHECK(fabs(g.handle_angle_deg - 500.0) < 1e-9 && isfinite(g.switch_anim), "non-finite time ignored");
}

static A380XGrvtyLeverActions step(A380XGrvtyLever *l, A380XGrvty *g, double dt, int *handle, int *crew)
{
    A380XGrvtyLeverActions a;
    a380x_grvty_update(g, dt, (float)dt);
    a = a380x_grvty_lever_step(l, g, *handle, crew);
    if (a.write_handle >= 0) *handle = a.write_handle;
    return a;
}

static void test_lever_lockout(void)
{
    A380XGrvty g;
    A380XGrvtyLever l = {0};
    A380XGrvtyLeverActions a;
    int handle = 0, crew = 0, emer_on = 0, emer_off = 0, owned_from = -1, owned_to = -1, i;
    double t = 0.0, dt = 0.01;
    printf("[lever lockout, FBW procedure: lever UP, GRVTY DOWN, lever DOWN, RESET, OFF]\n");
    a380x_grvty_init(&g);
    a = step(&l, &g, dt, &handle, &crew);
    CHECK(!l.owned && a.write_handle < 0 && !a.emer_on && !a.emer_off && crew == 0, "normal: lever = X-Plane handle");
    handle = 1;
    step(&l, &g, dt, &handle, &crew);
    CHECK(crew == 1 && !l.owned, "X-Plane handle changes are the lever while not isolated");
    CHECK(!a380x_grvty_lever_command(&l, A380X_GRVTY_LEVER_UP, &crew) && crew == 1,
          "commands pass through to X-Plane while not isolated");
    handle = 0;
    step(&l, &g, dt, &handle, &crew);
    open_all(&g);
    a380x_grvty_switch_set(&g, A380X_GRVTY_DOWN);
    for (i = 0; i < 2600; ++i) {       /* 26 s of DOWN */
        a = step(&l, &g, dt, &handle, &crew);
        t += dt;
        if (l.owned && owned_from < 0) owned_from = i;
        emer_on += a.emer_on; emer_off += a.emer_off;
        if (i == 1000) {               /* ~10 s: crew moves the lever DOWN: no hydraulic effect */
            CHECK(a380x_grvty_lever_command(&l, A380X_GRVTY_LEVER_DOWN, &crew) && crew == 1 && handle == 0,
                  "lever DOWN at 1 turn moves only the crew lever");
            CHECK(a380x_grvty_lever_command(&l, A380X_GRVTY_LEVER_UP, &crew) && crew == 0, "and back UP");
            CHECK(a380x_grvty_lever_command(&l, A380X_GRVTY_LEVER_OFF, &crew) && crew == 0, "no OFF on the A380 lever");
        }
        if (i == 1500) {               /* ~15 s: a hardware writer sets X-Plane's handle */
            handle = 1;
            a = step(&l, &g, 0.0, &handle, &crew);
            CHECK(a.write_handle == 0 && handle == 0 && crew == 1, "external handle write restored, kept as crew lever");
            crew = 0;
        }
    }
    CHECK(owned_from >= 0 && fabs((owned_from + 1) * dt - 7.2) < 0.015, "lockout starts at 7.2 s (%g)",
          (owned_from + 1) * dt);
    CHECK(emer_on == 1 && !emer_off && l.emer_override, "override engaged once at 3 turns");
    CHECK(handle == 1 && crew == 0, "gear selected down by the uplock release, lever still UP");
    CHECK(a380x_grvty_lever_command(&l, A380X_GRVTY_LEVER_DOWN, &crew) && crew == 1, "procedure: lever DOWN");
    a380x_grvty_switch_set(&g, A380X_GRVTY_RESET);
    t = 0.0;
    for (i = 0; i < 600 && owned_to < 0; ++i) {
        a = step(&l, &g, dt, &handle, &crew);
        t += dt;
        emer_off += a.emer_off;
        if (!l.owned) {
            owned_to = i;
            CHECK(a.write_handle == 1 && a.emer_off, "handle = lever DOWN, then override released");
        }
    }
    CHECK(owned_to >= 0 && fabs(t - 3.6) < 0.015, "lockout ends 3.6 s after RESET (%g)", t);
    CHECK(handle == 1 && emer_off == 1 && !l.emer_override, "gear stays down: lever was already DOWN");
    a380x_grvty_switch_set(&g, A380X_GRVTY_OFF);
    for (i = 0; i < 100; ++i) {
        a = step(&l, &g, dt, &handle, &crew);
        CHECK(a.write_handle < 0 && !a.emer_on && !a.emer_off, "OFF: nothing written");
    }
    CHECK(!a380x_grvty_lever_command(&l, A380X_GRVTY_LEVER_TOGGLE, &crew), "lever drives X-Plane again");
}

static void test_reset_with_lever_up_and_partial_episode(void)
{
    A380XGrvty g;
    A380XGrvtyLever l = {0};
    A380XGrvtyLeverActions a;
    int handle = 0, crew = 0, i, writes = 0, emer = 0;
    printf("[RESET with lever UP; episode stopped at 2 turns]\n");
    a380x_grvty_init(&g);
    open_all(&g);
    a380x_grvty_switch_set(&g, A380X_GRVTY_DOWN);
    for (i = 0; i < 2200; ++i) step(&l, &g, 0.01, &handle, &crew);
    CHECK(handle == 1 && crew == 0 && l.emer_override, "extended by gravity, lever UP");
    a380x_grvty_switch_set(&g, A380X_GRVTY_RESET);
    for (i = 0; i < 400; ++i) {
        a = step(&l, &g, 0.01, &handle, &crew);
        if (a.emer_off) CHECK(a.write_handle == 0, "FBW: hydraulics back with the lever UP retract the gear");
    }
    CHECK(!l.owned && handle == 0 && !l.emer_override, "handle follows the UP lever");
    /* DOWN to 2 turns only (doors released), then RESET: never selected down, no override. */
    a380x_grvty_switch_set(&g, A380X_GRVTY_OFF);
    a380x_grvty_switch_set(&g, A380X_GRVTY_DOWN);
    for (i = 0; i < 1500; ++i) {
        a = step(&l, &g, 0.01, &handle, &crew);
        writes += a.write_handle >= 0; emer += a.emer_on + a.emer_off;
    }
    CHECK(a380x_grvty_turns(&g) == 2 && a380x_grvty_doors_released(&g) && !a380x_grvty_gears_released(&g),
          "2 turns at 15 s");
    CHECK(l.owned && handle == 0 && !writes && !emer, "doors released, gear held up, nothing written");
    a380x_grvty_switch_set(&g, A380X_GRVTY_RESET);
    for (i = 0; i < 400; ++i) {
        a = step(&l, &g, 0.01, &handle, &crew);
        writes += a.write_handle >= 0; emer += a.emer_on + a.emer_off;
    }
    CHECK(!l.owned && writes == 1 && !emer && handle == 0, "episode end writes the lever once, no override");
}

static void test_release_and_abandon(void)
{
    A380XGrvty g;
    A380XGrvtyLever l = {0};
    A380XGrvtyLeverActions a;
    int handle = 0, crew = 0, i;
    printf("[disable / aircraft reload]\n");
    a380x_grvty_init(&g);
    open_all(&g);
    a380x_grvty_switch_set(&g, A380X_GRVTY_DOWN);
    for (i = 0; i < 2200; ++i) step(&l, &g, 0.01, &handle, &crew);
    a = a380x_grvty_lever_release(&l, 1);
    CHECK(a.write_handle == 1 && a.emer_off && !a.emer_on && !l.owned && !l.emer_override,
          "disable: handle = lever, override released");
    a = a380x_grvty_lever_release(&l, 1);
    CHECK(a.write_handle < 0 && !a.emer_off, "second release is a no-op");
    for (i = 0; i < 2; ++i) a = step(&l, &g, 0.01, &handle, &crew);
    CHECK(l.owned && l.emer_override, "re-enable at 3 turns re-engages");
    a = a380x_grvty_lever_abandon(&l);
    CHECK(a.write_handle < 0 && a.emer_off && !l.owned, "reload: override released, X-Plane's handle untouched");
}

int main(void)
{
    test_cold_and_dark();
    test_interlocks();
    test_refused_move_does_not_animate();
    test_handle_timing();
    test_lever_lockout();
    test_reset_with_lever_up_and_partial_episode();
    test_release_and_abandon();
    printf("%d checks, %d failures\n", checks, failures);
    if (failures) return 1;
    puts("L/G GRVTY core: interlocks, handle timing 7.2/14.4/21.6 s, RESET 200 deg/s, lever lockout and cold & dark passed");
    return 0;
}
