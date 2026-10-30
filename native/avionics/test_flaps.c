/* SPDX-License-Identifier: GPL-3.0-or-later
 * XPLM-free FLAPS lever / SFCC / flap-slat drive core (a380x_flaps.h): pull-out detents, gates at 1 and 3,
 * no intermediate positions, CSU out-of-detent hold, FBW SFCC 1 vs 1+F, automatic retraction and
 * re-extension with hysteresis, cruise baulk, alpha/speed lock, load relief, FPPU drive timing and
 * hydraulic dependency, SFCC words, PFD target logic, VFE, aileron droop and the X-Plane handle mapping.
 */
#include <stdio.h>
#include <stdlib.h>
#include "a380x_flaps.h"

static int checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); } } while (0)
#define NEAR(a, b, tol) (fabs((double)(a) - (double)(b)) <= (tol))

static A380XSfccInputs air(double ias) { A380XSfccInputs in = {ias, 5000.0, 4.0, 1, 0}; return in; }
static A380XSfccInputs ground(void) { A380XSfccInputs in = {0.0, 0.0, 0.0, 1, 1}; return in; }

/* One cockpit action: pull out, drag the hand from the lever to `to` in small steps, release. */
static void action(A380XFlapsLever *l, double to)
{
    double h = l->value, s = to >= h ? 0.05 : -0.05;
    a380x_flaps_lever_lift(l, 1.0);
    for (; s > 0 ? h <= to + 1e-9 : h >= to - 1e-9; h += s) a380x_flaps_lever_drag(l, h);
    a380x_flaps_lever_drag(l, to);
    a380x_flaps_lever_tick(l, 1.0);   /* mouse released: drag timeout */
}

static void test_pull_out_required(void)
{
    A380XFlapsLever l;
    a380x_flaps_lever_init(&l, 0);
    CHECK(l.latched && l.detent == 0 && l.actions == 0 && a380x_flaps_lever_csu(&l) == 0);
    a380x_flaps_lever_drag(&l, 0.7);                 /* not pulled out: the lever does not move */
    CHECK(l.value == 0.0 && l.latched && a380x_flaps_lever_input(&l) == 0.7);
    a380x_flaps_lever_drag(&l, 1.0);
    CHECK(l.value == 0.0);
    a380x_flaps_lever_lift(&l, 0.3);                 /* partial lift is still in the detent */
    CHECK(l.latched && l.value == 0.0);
    a380x_flaps_lever_lift(&l, 1.0);                 /* out of the detent: follows the hand (window 0..1) */
    CHECK(!l.latched && l.value == 1.0 && a380x_flaps_lever_csu(&l) == 1);
    a380x_flaps_lever_tick(&l, 0.3);
    CHECK(l.dragging && l.lock_anim == 1.0);         /* sleeve pulled */
    a380x_flaps_lever_tick(&l, 0.05);                /* 0.35 s idle: still held */
    CHECK(l.dragging && !l.latched);
    a380x_flaps_lever_tick(&l, 0.1);                 /* released (0.4 s timeout): springs into 1 */
    CHECK(!l.dragging && l.latched && l.detent == 1 && l.actions == 1 && a380x_flaps_lever_lift_input(&l) == 0.0);
    CHECK(NEAR(l.lock_anim, 0.5, 1e-9));             /* sleeve returns at 5 / s */
    a380x_flaps_lever_tick(&l, 0.06);
    CHECK(NEAR(l.lock_anim, 0.2, 1e-9));
    a380x_flaps_lever_tick(&l, 0.1);
    CHECK(l.lock_anim == 0.0);
}

static void test_gates(void)
{
    A380XFlapsLever l;
    int lo, hi, k;
    static const int windows[5][2] = {{0, 1}, {0, 3}, {1, 3}, {1, 4}, {3, 4}};
    for (k = 0; k <= 4; ++k) {
        a380x_flaps_gate_window(k, &lo, &hi);
        CHECK(lo == windows[k][0] && hi == windows[k][1]);
    }
    a380x_flaps_lever_init(&l, 0);
    action(&l, 4.0);                                 /* 0 -> FULL in one action stops at the gate 1 */
    CHECK(l.detent == 1 && l.value == 1.0);
    action(&l, 4.0);                                 /* 1 -> FULL stops at the gate 3 */
    CHECK(l.detent == 3);
    action(&l, 4.0);
    CHECK(l.detent == 4);
    action(&l, 0.0);                                 /* FULL -> 0 stops at 3 */
    CHECK(l.detent == 3);
    action(&l, 0.0);                                 /* 3 -> 0 stops at 1 */
    CHECK(l.detent == 1);
    action(&l, 0.0);
    CHECK(l.detent == 0 && l.actions == 6);
    action(&l, 2.0);                                 /* 0 -> 2: gate at 1 */
    CHECK(l.detent == 1);
    action(&l, 2.0);                                 /* 1 -> 2 needs no gate */
    CHECK(l.detent == 2);
    /* Dropping the handle into the gate and pulling again continues in the same drag. */
    a380x_flaps_lever_init(&l, 0);
    a380x_flaps_lever_lift(&l, 1.0);
    a380x_flaps_lever_drag(&l, 0.5); a380x_flaps_lever_drag(&l, 1.0); a380x_flaps_lever_drag(&l, 2.5);
    CHECK(l.value == 1.0 && !l.latched);             /* held at the gate */
    a380x_flaps_lever_lift(&l, 0.0);                 /* drops into 1 */
    CHECK(l.latched && l.detent == 1);
    a380x_flaps_lever_lift(&l, 1.0);                 /* pulled out again: window 0..3 */
    a380x_flaps_lever_drag(&l, 2.6);
    a380x_flaps_lever_tick(&l, 0.01);
    CHECK(!l.latched && NEAR(l.value, 2.6, 1e-12) && l.anim == l.value);   /* dragged: no lag */
    a380x_flaps_lever_drag(&l, 3.9);
    CHECK(l.value == 3.0);
}

static void test_no_intermediate_positions(void)
{
    A380XFlapsLever l;
    a380x_flaps_lever_init(&l, 1);
    a380x_flaps_lever_lift(&l, 1.0);
    a380x_flaps_lever_drag(&l, 1.6);
    CHECK(a380x_flaps_lever_csu(&l) == A380X_FLAPS_OUT_OF_DETENT);
    a380x_flaps_lever_lift(&l, 0.0);                 /* too far from a position: still out (X-Plane forbids it) */
    CHECK(!l.latched);
    a380x_flaps_lever_tick(&l, 0.5);                 /* released: springs to the nearest position */
    CHECK(l.latched && l.detent == 2 && l.value == 2.0);
    a380x_flaps_lever_lift(&l, 1.0);
    a380x_flaps_lever_drag(&l, 1.3);
    a380x_flaps_lever_tick(&l, 0.5);
    CHECK(l.detent == 1);
    a380x_flaps_lever_lift(&l, 1.0);
    a380x_flaps_lever_drag(&l, 1.85);
    a380x_flaps_lever_lift(&l, 0.2);                 /* within 0.25: seats in 2 */
    CHECK(l.latched && l.detent == 2);
    CHECK(a380x_flaps_lever_csu(&l) == 2);
    l.value = 2.05; CHECK(a380x_flaps_lever_csu(&l) == 2);
    l.value = 2.1; CHECK(a380x_flaps_lever_csu(&l) == A380X_FLAPS_OUT_OF_DETENT);
    l.value = 2.0;
    /* Nothing to NaN. */
    a380x_flaps_lever_drag(&l, NAN); a380x_flaps_lever_lift(&l, NAN);
    CHECK(l.value == 2.0 && l.latched);
}

static void test_commands_and_hardware(void)
{
    A380XFlapsLever l;
    a380x_flaps_lever_init(&l, 0);
    CHECK(a380x_flaps_lever_step(&l, 1) && l.detent == 1);
    CHECK(a380x_flaps_lever_step(&l, 1) && l.detent == 2);
    CHECK(a380x_flaps_lever_step(&l, -1) && l.detent == 1);
    CHECK(a380x_flaps_lever_step(&l, -1) && l.detent == 0);
    CHECK(!a380x_flaps_lever_step(&l, -1) && l.detent == 0);   /* end stop */
    a380x_flaps_lever_set(&l, 4);                              /* hardware / down-full: no gates */
    CHECK(l.detent == 4 && l.latched && l.value == 4.0 && l.anim == 0.0);
    a380x_flaps_lever_tick(&l, 0.5);                           /* the 3D lever travels at 6 positions / s */
    CHECK(NEAR(l.anim, 3.0, 1e-9));
    a380x_flaps_lever_tick(&l, 0.2);
    CHECK(l.anim == 4.0);
    CHECK(!a380x_flaps_lever_step(&l, 1));
    /* A command during a drag ends it first. */
    a380x_flaps_lever_lift(&l, 1.0); a380x_flaps_lever_drag(&l, 3.4);
    CHECK(a380x_flaps_lever_step(&l, -1) && l.detent == 2 && !l.dragging);
    /* Hardware axis: ratio x 4 with hysteresis. */
    CHECK(a380x_flaps_hardware_position(0.5, 0) == 2);
    CHECK(a380x_flaps_hardware_position(0.36, 1) == 1);         /* 1.44: within 0.6 of 1 */
    CHECK(a380x_flaps_hardware_position(0.41, 1) == 2);         /* 1.64 */
    CHECK(a380x_flaps_hardware_position(1.2, 0) == 4 && a380x_flaps_hardware_position(-1, 3) == 0);
    CHECK(a380x_flaps_hardware_position(NAN, 3) == 3);
}

/* Runs the SFCC with the lever held at `handle` for `frames` frames. */
static int sfcc_run(A380XSfcc *s, int handle, A380XSfccInputs in, int frames)
{
    int i;
    for (i = 0; i < frames; ++i) a380x_sfcc_update(s, handle, &in);
    return s->conf;
}

static void test_sfcc_conf1(void)
{
    A380XSfcc s;
    a380x_sfcc_init(&s, 0, A380X_CONF0);
    CHECK(sfcc_run(&s, 1, ground(), 2) == A380X_CONF1F);       /* on the ground: 1+F */
    a380x_sfcc_init(&s, 0, A380X_CONF0);
    CHECK(sfcc_run(&s, 1, air(230), 1) == A380X_CONF1);        /* 0 -> 1 in flight above 205 kt: 1 */
    CHECK(sfcc_run(&s, 1, air(230), 5) == A380X_CONF1);
    CHECK(s.handle == 1 && !s.relief && !s.cruise_baulk);
    CHECK(sfcc_run(&s, 1, air(208), 5) == A380X_CONF1);        /* 205..212: holds */
    CHECK(sfcc_run(&s, 1, air(204.9), 1) == A380X_CONF1F);     /* below 205: re-extends to 1+F */
    CHECK(sfcc_run(&s, 1, air(210), 5) == A380X_CONF1F);       /* hysteresis: still 1+F at 210 */
    CHECK(sfcc_run(&s, 1, air(212.1), 1) == A380X_CONF1);      /* automatic retraction above 212 kt */
    a380x_sfcc_init(&s, 0, A380X_CONF0);
    CHECK(sfcc_run(&s, 1, air(200), 1) == A380X_CONF1F);       /* 0 -> 1 below 205 kt: 1+F */
    /* 2 -> 1: 1+F at or below 212 kt, 1 above. */
    a380x_sfcc_init(&s, 2, A380X_CONF2);
    CHECK(sfcc_run(&s, 1, air(212), 1) == A380X_CONF1F);
    a380x_sfcc_init(&s, 2, A380X_CONF2);
    CHECK(sfcc_run(&s, 1, air(213), 1) == A380X_CONF1);
    /* Take-off: 1+F on the ground, the ARS retracts the flaps at 212 kt in the climb. */
    a380x_sfcc_init(&s, 1, A380X_CONF1F);
    CHECK(sfcc_run(&s, 1, ground(), 3) == A380X_CONF1F);
    CHECK(sfcc_run(&s, 1, air(180), 3) == A380X_CONF1F);
    CHECK(sfcc_run(&s, 1, air(212.5), 1) == A380X_CONF1);
    CHECK(a380x_sfcc_status_word(&s) & (1u << 25));            /* bit 26: CONF 1 (PFD "1") */
}

static void test_sfcc_cruise_baulk(void)
{
    A380XSfcc s;
    A380XSfccInputs high = air(250);
    a380x_sfcc_init(&s, 0, A380X_CONF0);
    CHECK(sfcc_run(&s, 1, air(270), 3) == A380X_CONF0 && s.cruise_baulk);
    CHECK(sfcc_run(&s, 1, air(260), 1) == A380X_CONF1 && !s.cruise_baulk);   /* below 265.5 kt */
    high.pressure_alt_ft = 25000.0;
    a380x_sfcc_init(&s, 0, A380X_CONF0);
    CHECK(sfcc_run(&s, 1, high, 3) == A380X_CONF0 && s.cruise_baulk);
    CHECK(a380x_sfcc_status_word(&s) & (1u << 24));            /* bit 25 */
    /* Overspeeding CONF 1 does not baulk (only a selection from CONF 0 is baulked). */
    a380x_sfcc_init(&s, 1, A380X_CONF1);
    CHECK(sfcc_run(&s, 1, air(270), 3) == A380X_CONF1);
}

static void test_sfcc_alpha_lock(void)
{
    A380XSfcc s;
    A380XSfccInputs in = air(150);
    /* FBW config_test_alpha_lock_speed: 1 -> 0 below 155 kt locks (bit 24), 200 kt releases. */
    a380x_sfcc_init(&s, 1, A380X_CONF1F);
    in.aoa_deg = 9.4;
    CHECK(sfcc_run(&s, 0, in, 1) == A380X_CONF1F && s.alpha_lock);
    CHECK(a380x_sfcc_status_word(&s) & (1u << 23));
    CHECK(sfcc_run(&s, 0, in, 5) == A380X_CONF1F && s.alpha_lock);
    in.ias_kt = 160.0;
    CHECK(sfcc_run(&s, 0, in, 5) == A380X_CONF1F);             /* lock-out above 161 kt ... */
    in.ias_kt = 161.5;
    CHECK(sfcc_run(&s, 0, in, 1) == A380X_CONF0 && !s.alpha_lock);
    /* ... OR below 9.2 deg AoA (FBW lock-out is an OR: at a normal AoA the speed lock holds one cycle). */
    a380x_sfcc_init(&s, 1, A380X_CONF1F);
    in = air(150); in.aoa_deg = 5.0;
    CHECK(sfcc_run(&s, 0, in, 1) == A380X_CONF1F && s.alpha_lock);
    CHECK(sfcc_run(&s, 0, in, 1) == A380X_CONF0 && !s.alpha_lock);
    /* FBW config_test_alpha_lock_aoa: 200 kt, 10 deg locks (one cycle), 9.1 deg releases. */
    in = air(200); in.aoa_deg = 10.0;
    a380x_sfcc_init(&s, 1, A380X_CONF1);
    CHECK(sfcc_run(&s, 0, in, 1) == A380X_CONF1F && s.alpha_lock);
    in.aoa_deg = 9.1;
    CHECK(sfcc_run(&s, 0, in, 1) == A380X_CONF0);
    /* The lock holds while AoA >= 9.2 AND IAS <= 161 kt. */
    in = air(158); in.aoa_deg = 11.0;
    a380x_sfcc_init(&s, 1, A380X_CONF1F);
    CHECK(sfcc_run(&s, 0, in, 30) == A380X_CONF1F && s.alpha_lock);
    in.aoa_deg = 9.3;
    CHECK(sfcc_run(&s, 0, in, 5) == A380X_CONF1F);
    a380x_sfcc_init(&s, 3, A380X_CONF3);                       /* from 3 also locks */
    CHECK(sfcc_run(&s, 0, air(150), 1) == A380X_CONF1F);
    a380x_sfcc_init(&s, 1, A380X_CONF1F);                      /* never on the ground */
    CHECK(sfcc_run(&s, 0, ground(), 1) == A380X_CONF0 && !s.alpha_lock);
    a380x_sfcc_init(&s, 1, A380X_CONF1);
    CHECK(sfcc_run(&s, 0, air(250), 1) == A380X_CONF0);        /* normal retraction */
    in = air(250); in.aoa_valid = 0;                           /* invalid AoA reads 0 (FBW unwrap_or_default) */
    a380x_sfcc_init(&s, 1, A380X_CONF1);
    CHECK(sfcc_run(&s, 0, in, 1) == A380X_CONF0);
}

static void test_sfcc_load_relief(void)
{
    A380XSfcc s;
    a380x_sfcc_init(&s, 3, A380X_CONF3);
    CHECK(sfcc_run(&s, 4, air(190), 2) == A380X_CONF3 && s.relief);     /* FULL above 184.5: stays 3 */
    CHECK(sfcc_run(&s, 4, air(180), 5) == A380X_CONF3 && s.relief);     /* re-extension below 179.5 */
    CHECK(sfcc_run(&s, 4, air(179), 1) == A380X_CONFFULL && !s.relief);
    CHECK(sfcc_run(&s, 4, air(184), 3) == A380X_CONFFULL);
    CHECK(sfcc_run(&s, 4, air(185), 1) == A380X_CONF3 && s.relief);     /* overspeed: retracts to 3 */
    a380x_sfcc_init(&s, 2, A380X_CONF2);
    CHECK(sfcc_run(&s, 3, air(200), 2) == A380X_CONF2S && s.relief);    /* 3 above 198.5: 2S */
    CHECK(sfcc_run(&s, 3, air(195), 5) == A380X_CONF2S);
    CHECK(sfcc_run(&s, 3, air(193), 1) == A380X_CONF3 && !s.relief);
    CHECK(sfcc_run(&s, 3, air(199), 1) == A380X_CONF2S);
    a380x_sfcc_init(&s, 1, A380X_CONF1F);
    CHECK(sfcc_run(&s, 2, air(225), 1) == A380X_CONF1F && s.relief);    /* 2 above 222.5: 1+F */
    CHECK(sfcc_run(&s, 2, air(220), 5) == A380X_CONF1F);
    CHECK(sfcc_run(&s, 2, air(217), 1) == A380X_CONF2 && !s.relief);
    CHECK(a380x_sfcc_status_word(&s) & (1u << 18));                     /* bit 19: handle 2 */
    /* Within the bands a selection is honoured. */
    a380x_sfcc_init(&s, 1, A380X_CONF1F);
    CHECK(sfcc_run(&s, 2, air(210), 3) == A380X_CONF2);
    a380x_sfcc_init(&s, 0, A380X_CONF0);
    CHECK(sfcc_run(&s, 4, ground(), 2) == A380X_CONFFULL);              /* hardware jump 0 -> FULL */
}

static void test_sfcc_out_of_detent(void)
{
    A380XFlaps f;
    A380XSfccInputs in = air(270);
    /* A drag from 0 to 1 at 270 kt passes out of detent: the handle index holds 0, then (0, 1): baulk. */
    a380x_flaps_init(&f, 0, A380X_CONF0);
    a380x_flaps_lever_lift(&f.lever, 1.0);
    a380x_flaps_lever_drag(&f.lever, 0.5);
    a380x_flaps_update(&f, &in, 1, 1, 0.02);
    CHECK(f.sfcc.handle == 0 && f.sfcc.conf == A380X_CONF0);
    a380x_flaps_lever_drag(&f.lever, 1.0);
    a380x_flaps_update(&f, &in, 1, 1, 0.02);
    CHECK(f.sfcc.handle == 1 && f.sfcc.conf == A380X_CONF0 && f.sfcc.cruise_baulk);
    /* 1 -> 3 at 200 kt through 2: (1,2) CONF 2 then (2,3) above 198.5: 2S. */
    a380x_flaps_init(&f, 1, A380X_CONF1F);
    in = air(200);
    a380x_flaps_lever_lift(&f.lever, 1.0);
    a380x_flaps_lever_drag(&f.lever, 1.5); a380x_flaps_update(&f, &in, 1, 1, 0.02);
    CHECK(f.sfcc.conf == A380X_CONF1F);
    a380x_flaps_lever_drag(&f.lever, 2.0); a380x_flaps_update(&f, &in, 1, 1, 0.02);
    CHECK(f.sfcc.handle == 2 && f.sfcc.conf == A380X_CONF2);
    a380x_flaps_lever_drag(&f.lever, 3.0); a380x_flaps_update(&f, &in, 1, 1, 0.02);
    CHECK(f.sfcc.handle == 3 && f.sfcc.conf == A380X_CONF2S);
}

/* Seconds until the drive reaches the demand (within 1e-9). */
static double travel(A380XFlapsDrive *d, double demand, double factor)
{
    double t = 0.0;
    while (fabs(d->fppu - demand) > 1e-9 && t < 600.0) { a380x_flaps_drive_update(d, demand, factor, 1.0 / 30.0); t += 1.0 / 30.0; }
    return t;
}

static void test_drives(void)
{
    A380XFlapsDrive flaps, slats;
    double t;
    CHECK(NEAR(A380X_FLAPS_FPPU_SPEED, 9.0237, 1e-3) && NEAR(A380X_SLATS_FPPU_SPEED, 10.3128, 1e-3));
    a380x_flaps_drive_init(&flaps, A380X_FLAPS_FPPU_SPEED, A380X_FLAPS_MAX_FPPU, 0.0);
    a380x_flaps_drive_init(&slats, A380X_SLATS_FPPU_SPEED, A380X_SLATS_MAX_FPPU, 0.0);
    t = travel(&slats, 286.48, 1.0);                           /* 0 -> 20 deg: ~27.8 s + filter / approach */
    CHECK(t > 27.5 && t < 30.5 && slats.fppu == 286.48 && slats.speed == 0.0);
    t = travel(&flaps, 215.68, 1.0);                           /* 0 -> 8 deg (1+F) */
    CHECK(t > 23.5 && t < 26.5);
    t = travel(&flaps, 338.99, 1.0);
    CHECK(t > 13.0 && t < 16.0 && NEAR(a380x_flap_deg_of_fppu(flaps.fppu), 33.0, 1e-9));
    t = travel(&flaps, 0.0, 1.0);                              /* full retraction ~37.6 s */
    CHECK(t > 37.5 && t < 40.5 && flaps.fppu == 0.0);
    t = travel(&flaps, 215.68, 0.5);                           /* one hydraulic side: half speed */
    CHECK(t > 47.5 && t < 51.0);
    a380x_flaps_drive_update(&flaps, 0.0, 0.0, 1.0);           /* no pressure: nothing moves */
    for (t = 0; t < 10.0; t += 0.1) a380x_flaps_drive_update(&flaps, 0.0, 0.0, 0.1);
    CHECK(flaps.fppu > 210.0);
    /* Pressure factor (FBW): 0 below 500 psi, full from ~3946 psi on the FBW 5250 psi scale. */
    CHECK(a380x_flaps_pressure_factor(400.0) == 0.0 && a380x_flaps_pressure_factor(5250.0) == 1.0);
    CHECK(NEAR(a380x_flaps_pressure_factor(2000.0), 0.0004 * 1500.0 * 1500.0 / 4750.0, 1e-12));
    CHECK(a380x_flaps_pressure_factor(NAN) == 0.0);
    /* Surface tables: the first 36 % of flap travel is translation only. */
    CHECK(a380x_flap_deg_of_fppu(100.0) == 0.0 && NEAR(a380x_flap_deg_of_fppu(259.28), 17.0, 1e-12));
    CHECK(NEAR(a380x_slat_deg_of_fppu(327.39), 23.0, 1e-12) && NEAR(a380x_slat_deg_of_fppu(143.24), 10.0, 1e-9));
}

static void test_system_and_droop(void)
{
    A380XFlaps f;
    A380XSfccInputs in = ground();
    int i;
    a380x_flaps_init(&f, 0, A380X_CONF0);
    CHECK(f.droop_deg == 0.0 && f.flaps.fppu == 0.0);
    CHECK(a380x_flaps_lever_step(&f.lever, 1));
    for (i = 0; i < 40 * 30; ++i) a380x_flaps_update(&f, &in, 1.0, 1.0, 1.0 / 30.0);
    CHECK(f.sfcc.conf == A380X_CONF1F && f.flaps.fppu == 215.68 && f.slats.fppu == 286.48);
    CHECK(f.droop_deg == A380X_FLAPS_DROOP_DEG);
    /* Take-off: the automatic retraction above 212 kt retracts the flaps (slats stay at 20). */
    in = air(215);
    for (i = 0; i < 40 * 30; ++i) a380x_flaps_update(&f, &in, 1.0, 1.0, 1.0 / 30.0);
    CHECK(f.sfcc.conf == A380X_CONF1 && f.flaps.fppu == 0.0 && f.slats.fppu == 286.48 && f.droop_deg == 0.0);
    /* Paused (dt 0): the SFCC runs, the surfaces hold. */
    in = air(200);
    a380x_flaps_update(&f, &in, 1.0, 1.0, 0.0);
    CHECK(f.sfcc.conf == A380X_CONF1F && f.flaps.fppu == 0.0);
    a380x_flaps_update(&f, &in, 1.0, 1.0, 1.0);
    CHECK(f.flaps.fppu > 0.0 && f.droop_deg == 0.0);           /* still in the translation band */
}

static void test_words_and_displays(void)
{
    A380XFlaps f;
    A380XFlapsFeed feed;
    unsigned w;
    a380x_flaps_init(&f, 1, A380X_CONF1F);
    w = a380x_sfcc_status_word(&f.sfcc);
    CHECK((w & (1u << 17)) && !(w & (1u << 16)) && (w & (1u << 27)) && (w & (1u << 28)) && !(w & (1u << 25)));
    w = a380x_sfcc_actual_position_word(215.68, 286.48);
    CHECK((w & (1u << 12)) && !(w & (1u << 13)) && !(w & (1u << 18)) && (w & (1u << 19)) && !(w & (1u << 20)));
    w = a380x_sfcc_actual_position_word(0.0, 0.0);
    CHECK((w & (1u << 11)) && (w & (1u << 18)) && (w & 1u << 10) && (w & 1u << 17));
    w = a380x_sfcc_actual_position_word(338.99, 327.39);
    CHECK((w & (1u << 13)) && (w & (1u << 14)) && (w & (1u << 21)) && (w & (1u << 22)));
    a380x_flaps_feed(&f, &feed);
    CHECK(feed.ready && feed.handle_index == 1 && feed.display_config == 2 && feed.slat_target == -1
          && feed.flap_target == -1 && NEAR(feed.flap_deg, 8.0, 1e-9) && NEAR(feed.slat_deg, 20.0, 1e-9));
    CHECK(feed.vfe_knots == 222.0 && feed.vfe_next_knots == 220.0);
    f.sfcc.conf = A380X_CONF1;                                 /* "1": flaps retracting (target 0) */
    a380x_flaps_feed(&f, &feed);
    CHECK(feed.display_config == 1 && feed.flap_target == 0 && feed.vfe_knots == 222.0);   /* actual 1+F limits */
    f.flaps.fppu = 0.0;
    a380x_flaps_feed(&f, &feed);
    CHECK(feed.flap_target == -1 && feed.vfe_knots == 263.0 && feed.vfe_next_knots == 222.0);
    a380x_flaps_init(&f, 0, A380X_CONF0);
    f.lever.detent = 3; f.sfcc.handle = 3; f.sfcc.conf = A380X_CONF2S; f.sfcc.relief = 1;
    f.flaps.fppu = 259.28; f.slats.fppu = 327.39;
    a380x_flaps_feed(&f, &feed);
    CHECK(feed.display_config == 4 && feed.relief && feed.flap_target == 3 && feed.slat_target == -1);
    CHECK(feed.vfe_knots == 220.0);                            /* 2S flies at the flaps-17 limit */
    f.sfcc.handle = 0; f.sfcc.conf = A380X_CONF0; f.flaps.fppu = 100; f.slats.fppu = 200;
    a380x_flaps_feed(&f, &feed);
    CHECK(feed.slat_target == 0 && feed.flap_target == 0 && feed.vfe_knots == 222.0);
}

static void test_xplane_mapping(void)
{
    static const float a380[10] = {0, 0, 8, 17, 17, 26, 33, 0, 0, 0};
    static const float legacy[10] = {0, 1, 5, 10, 20, 25, 40, 0, 0, 0};
    int k;
    CHECK(a380x_flaps_xp_layout_ok(6, a380, 10) && !a380x_flaps_xp_layout_ok(6, legacy, 10));
    CHECK(!a380x_flaps_xp_layout_ok(4, a380, 10) && !a380x_flaps_xp_layout_ok(6, a380, 6));
    for (k = 0; k < A380X_CONF_COUNT; ++k) {
        CHECK(NEAR(a380x_flaps_xp_request(k, 1, 6, a380, 10), k / 6.0, 1e-12));
        CHECK(a380x_flaps_conf_of_request(k / 6.0, 1) == k);
    }
    CHECK(a380x_flaps_xp_request(A380X_CONF0, 0, 6, legacy, 10) == 0.0);
    CHECK(NEAR(a380x_flaps_xp_request(A380X_CONF1, 0, 6, legacy, 10), 1.0 / 6.0, 1e-12));
    CHECK(NEAR(a380x_flaps_xp_request(A380X_CONF1F, 0, 6, legacy, 10), 2.6 / 6.0, 1e-12));   /* 8 deg */
    CHECK(NEAR(a380x_flaps_xp_request(A380X_CONFFULL, 0, 6, legacy, 10), (5.0 + 8.0 / 15.0) / 6.0, 1e-12));  /* 33 deg */
    CHECK(a380x_flaps_conf_of_request(0.5, 0) == A380X_CONF2 && a380x_flaps_conf_of_request(0.25, 0) == A380X_CONF1F);
    CHECK(a380x_flaps_conf_of_request(NAN, 1) == A380X_CONF0);
    for (k = 0; k < A380X_CONF_COUNT; ++k)
        CHECK(a380x_flaps_lever_of_conf[k] == (k == 0 ? 0 : k <= 2 ? 1 : k <= 4 ? 2 : k == 5 ? 3 : 4));
}

int main(void)
{
    test_pull_out_required();
    test_gates();
    test_no_intermediate_positions();
    test_commands_and_hardware();
    test_sfcc_conf1();
    test_sfcc_cruise_baulk();
    test_sfcc_alpha_lock();
    test_sfcc_load_relief();
    test_sfcc_out_of_detent();
    test_drives();
    test_system_and_droop();
    test_words_and_displays();
    test_xplane_mapping();
    if (failures) {
        fprintf(stderr, "%d of %d flaps checks failed\n", failures, checks);
        return 1;
    }
    printf("Flaps core: %d checks passed (pull-out, gates 1/3, no intermediate positions, SFCC 1/1+F, ARS, "
           "cruise baulk, alpha lock, load relief, FPPU drives, words, displays, X-Plane mapping)\n", checks);
    return 0;
}
