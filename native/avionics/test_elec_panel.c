/* SPDX-License-Identifier: GPL-3.0-or-later
 * Pure ELEC overhead panel state machine: latches, guards, DRIVE latch, EXT, lamps, travel.
 */
#include <assert.h>
#include <stdio.h>
#include "a380x_elec_panel.h"

static void step(A380XElecPanel *p, const A380XElecInputs *in, float *now, float seconds)
{
    while (seconds > 0.0f) {
        float dt = seconds > 0.02f ? 0.02f : seconds;
        *now += dt; seconds -= dt;
        a380x_elec_update(p, in, *now, dt);
    }
}

static void test_latched_pb_sw_and_lamps(void)
{
    A380XElecPanel p; A380XElecInputs in; float now = 0.0f; int c;
    a380x_elec_init(&p); memset(&in, 0, sizeof(in));
    for (c = 0; c < A380X_ELEC_CONTROL_COUNT; ++c) assert(p.normal[c] == (unsigned char)a380x_elec_is_latch(c));
    /* No power: dark panel whatever the state. */
    assert(a380x_elec_press(&p, &in, A380X_ELEC_BAT1, now) == A380X_ELEC_EVENT_PRESS);
    assert(!p.normal[A380X_ELEC_BAT1] && a380x_elec_lamp(&p, &in, A380X_ELEC_BAT1, A380X_ELEC_LEGEND_OFF) == 0.0f);
    assert(a380x_elec_press(&p, &in, A380X_ELEC_BAT1, now) == 0);             /* held: no repeat toggle */
    step(&p, &in, &now, 0.2f);
    assert(p.press[A380X_ELEC_BAT1].position == 1.0f);                        /* held fully in */
    assert(a380x_elec_release(&p, A380X_ELEC_BAT1) == A380X_ELEC_EVENT_RELEASE);
    assert(a380x_elec_release(&p, A380X_ELEC_BAT1) == 0);
    step(&p, &in, &now, 0.2f);
    assert(p.press[A380X_ELEC_BAT1].position == 0.0f);                        /* in/out look the same */
    in.lamp_power = 1;
    assert(a380x_elec_lamp(&p, &in, A380X_ELEC_BAT1, A380X_ELEC_LEGEND_OFF) == 1.0f);  /* white OFF */
    assert(a380x_elec_lamp(&p, &in, A380X_ELEC_BAT1, A380X_ELEC_LEGEND_FAULT) == 0.0f);
    in.fault[A380X_ELEC_BAT1] = 1;
    assert(a380x_elec_lamp(&p, &in, A380X_ELEC_BAT1, A380X_ELEC_LEGEND_FAULT) == 1.0f); /* amber FAULT */
    in.annunciator_dim = 1;
    assert(fabsf(a380x_elec_lamp(&p, &in, A380X_ELEC_BAT1, A380X_ELEC_LEGEND_OFF) - A380X_ELEC_DIM_LEVEL) < 1e-6f);
    in.annunciator_dim = 0; in.fault[A380X_ELEC_BAT1] = 0;
    /* A quick click (Begin+End in one frame) still shows a visible press. */
    a380x_elec_press(&p, &in, A380X_ELEC_BAT1, now); a380x_elec_release(&p, A380X_ELEC_BAT1);
    step(&p, &in, &now, 0.06f);
    assert(p.press[A380X_ELEC_BAT1].position > 0.5f && p.normal[A380X_ELEC_BAT1]);
    step(&p, &in, &now, 0.3f);
    assert(p.press[A380X_ELEC_BAT1].position == 0.0f);
    assert(a380x_elec_lamp(&p, &in, A380X_ELEC_BAT1, A380X_ELEC_LEGEND_OFF) == 0.0f);
    /* Explicit selection only animates on a change. */
    assert(a380x_elec_set(&p, A380X_ELEC_GEN2, 1, now) == 0);
    assert(a380x_elec_set(&p, A380X_ELEC_GEN2, 0, now) == (A380X_ELEC_EVENT_PRESS | A380X_ELEC_EVENT_RELEASE));
    assert(!p.normal[A380X_ELEC_GEN2]);
    /* Panel legends that exist: PAX SYS / BUS TIE have OFF only; ELMU publishes FAULT without a face. */
    assert(a380x_elec_has_legend(A380X_ELEC_PAX_SYS, A380X_ELEC_LEGEND_OFF));
    assert(!a380x_elec_has_legend(A380X_ELEC_PAX_SYS, A380X_ELEC_LEGEND_FAULT));
    assert(a380x_elec_has_legend(A380X_ELEC_ELMU, A380X_ELEC_LEGEND_FAULT));
    assert(!a380x_elec_has_legend(A380X_ELEC_BUS_TIE, A380X_ELEC_LEGEND_FAULT));
    /* The RAT & EMER GEN light is an indicator, never a button. */
    assert(a380x_elec_press(&p, &in, A380X_ELEC_RAT_FAULT, now) == 0);
    in.fault[A380X_ELEC_RAT_FAULT] = 1;
    assert(a380x_elec_lamp(&p, &in, A380X_ELEC_RAT_FAULT, A380X_ELEC_LEGEND_FAULT) == 1.0f);
    /* ANN LT TEST: every legend, no invented ones. */
    in.annunciator_test = 1;
    for (c = 0; c < A380X_ELEC_CONTROL_COUNT; ++c) {
        int legend;
        for (legend = A380X_ELEC_LEGEND_FAULT; legend < A380X_ELEC_LEGEND_COUNT; ++legend)
            assert((a380x_elec_lamp(&p, &in, c, legend) > 0.0f) == a380x_elec_has_legend(c, legend));
    }
}

static void test_guards_and_drive_latch(void)
{
    A380XElecPanel p; A380XElecInputs in; float now = 0.0f; int n;
    a380x_elec_init(&p); memset(&in, 0, sizeof(in)); in.lamp_power = 1;
    /* Guard closed: the DRIVE pb cannot be reached. */
    assert(a380x_elec_press(&p, &in, A380X_ELEC_DRIVE1, now) == 0 && !p.drive_disc[0]);
    assert(a380x_elec_press(&p, &in, A380X_ELEC_AC_ESS_FEED, now) == 0 && p.normal[A380X_ELEC_AC_ESS_FEED]);
    assert(a380x_elec_set(&p, A380X_ELEC_AC_ESS_FEED, 0, now) == 0);
    /* Click: guard swings fully open over 0.22 s with one open snap. */
    a380x_elec_cover_toggle(&p, A380X_ELEC_COVER_DRIVE1);
    step(&p, &in, &now, 0.1f);
    assert(p.cover[A380X_ELEC_COVER_DRIVE1] > 0.3f && p.cover[A380X_ELEC_COVER_DRIVE1] < 0.6f);
    assert(p.cover_opened == (1u << A380X_ELEC_COVER_DRIVE1));
    p.cover_opened = 0;
    step(&p, &in, &now, 0.2f);
    assert(p.cover[A380X_ELEC_COVER_DRIVE1] == 1.0f && p.cover_opened == 0 && p.cover_closed == 0);
    /* DRIVE: momentary press, DISC latched (white), irreversible by the pushbutton. */
    assert(a380x_elec_press(&p, &in, A380X_ELEC_DRIVE1, now) == A380X_ELEC_EVENT_PRESS);
    assert(p.drive_disc[0] && a380x_elec_lamp(&p, &in, A380X_ELEC_DRIVE1, A380X_ELEC_LEGEND_DISC) == 1.0f);
    a380x_elec_release(&p, A380X_ELEC_DRIVE1); step(&p, &in, &now, 0.3f);
    assert(p.press[A380X_ELEC_DRIVE1].position == 0.0f);
    a380x_elec_press(&p, &in, A380X_ELEC_DRIVE1, now); a380x_elec_release(&p, A380X_ELEC_DRIVE1);
    assert(p.drive_disc[0]);
    /* Reconnection: ground maintenance only, engine stopped. */
    assert(!a380x_elec_drive_reset(&p, &in, 0));             /* airborne */
    in.on_ground = 1; in.engine_running[0] = 1;
    assert(!a380x_elec_drive_reset(&p, &in, 0));             /* engine running */
    in.engine_running[0] = 0;
    assert(a380x_elec_drive_reset(&p, &in, 0) && !p.drive_disc[0]);
    assert(!a380x_elec_drive_reset(&p, &in, 0));             /* nothing to reset */
    /* Click again: guard closes with one seated snap; closing over a held pb releases it. */
    a380x_elec_press(&p, &in, A380X_ELEC_DRIVE1, now);
    a380x_elec_cover_toggle(&p, A380X_ELEC_COVER_DRIVE1);
    step(&p, &in, &now, 0.3f);
    assert(p.cover[A380X_ELEC_COVER_DRIVE1] == 0.0f && p.cover_closed == (1u << A380X_ELEC_COVER_DRIVE1));
    assert(!p.held[A380X_ELEC_DRIVE1]);
    /* Drag: the guard follows the hand, then settles past half travel open, else closed. */
    a380x_elec_cover_set(&p, A380X_ELEC_COVER_AC_ESS_FEED, 0.45f);
    assert(p.cover[A380X_ELEC_COVER_AC_ESS_FEED] == 0.45f);
    a380x_elec_cover_settle(&p, A380X_ELEC_COVER_AC_ESS_FEED); step(&p, &in, &now, 0.3f);
    assert(p.cover[A380X_ELEC_COVER_AC_ESS_FEED] == 0.0f);
    a380x_elec_cover_set(&p, A380X_ELEC_COVER_AC_ESS_FEED, 0.55f);
    a380x_elec_cover_settle(&p, A380X_ELEC_COVER_AC_ESS_FEED); step(&p, &in, &now, 0.3f);
    assert(p.cover[A380X_ELEC_COVER_AC_ESS_FEED] == 1.0f);
    a380x_elec_cover_set(&p, A380X_ELEC_COVER_AC_ESS_FEED, NAN);
    assert(p.cover[A380X_ELEC_COVER_AC_ESS_FEED] == 0.0f);
    a380x_elec_cover_set(&p, A380X_ELEC_COVER_AC_ESS_FEED, 2.0f);
    /* AC ESS FEED: guarded latched pb-sw, ALTN white. */
    assert(a380x_elec_press(&p, &in, A380X_ELEC_AC_ESS_FEED, now) == A380X_ELEC_EVENT_PRESS);
    assert(!p.normal[A380X_ELEC_AC_ESS_FEED]);
    assert(a380x_elec_lamp(&p, &in, A380X_ELEC_AC_ESS_FEED, A380X_ELEC_LEGEND_ALTN) == 1.0f);
    /* RAT MAN ON latches the deployment request. */
    assert(a380x_elec_press(&p, &in, A380X_ELEC_RAT_MAN_ON, now) == 0 && !p.rat_requested);
    a380x_elec_cover_target(&p, A380X_ELEC_COVER_RAT_MAN_ON, 1.0f); step(&p, &in, &now, 0.3f);
    assert(a380x_elec_press(&p, &in, A380X_ELEC_RAT_MAN_ON, now) && p.rat_requested);
    for (n = 0; n < 4; ++n) assert(a380x_elec_controls[A380X_ELEC_DRIVE1 + n].cover == A380X_ELEC_COVER_DRIVE1 + n);
}

static void test_external_power(void)
{
    A380XElecPanel p; A380XElecInputs in; float now = 0.0f;
    a380x_elec_init(&p); memset(&in, 0, sizeof(in)); in.lamp_power = 1;
    /* Not available: a press only moves the cap. */
    assert(a380x_elec_press(&p, &in, A380X_ELEC_EXT2, now) == A380X_ELEC_EVENT_PRESS);
    a380x_elec_release(&p, A380X_ELEC_EXT2);
    assert(!p.ext_on[1] && a380x_elec_lamp(&p, &in, A380X_ELEC_EXT2, A380X_ELEC_LEGEND_AVAIL) == 0.0f);
    in.ext_available[1] = 1;
    assert(a380x_elec_lamp(&p, &in, A380X_ELEC_EXT2, A380X_ELEC_LEGEND_AVAIL) == 1.0f);   /* green AVAIL */
    a380x_elec_press(&p, &in, A380X_ELEC_EXT2, now); a380x_elec_release(&p, A380X_ELEC_EXT2);
    assert(p.ext_on[1] && a380x_elec_lamp(&p, &in, A380X_ELEC_EXT2, A380X_ELEC_LEGEND_ON) == 1.0f); /* blue ON */
    assert(a380x_elec_lamp(&p, &in, A380X_ELEC_EXT2, A380X_ELEC_LEGEND_AVAIL) == 0.0f);
    a380x_elec_press(&p, &in, A380X_ELEC_EXT2, now); a380x_elec_release(&p, A380X_ELEC_EXT2);
    assert(!p.ext_on[1]);
    a380x_elec_press(&p, &in, A380X_ELEC_EXT2, now); a380x_elec_release(&p, A380X_ELEC_EXT2);
    in.ext_available[1] = 0; step(&p, &in, &now, 0.02f);
    assert(!p.ext_on[1] && a380x_elec_lamp(&p, &in, A380X_ELEC_EXT2, A380X_ELEC_LEGEND_ON) == 0.0f);
    assert(!a380x_elec_set(&p, A380X_ELEC_EXT1, 1, now));     /* EXT is not a latch */
}

int main(void)
{
    test_latched_pb_sw_and_lamps();
    test_guards_and_drive_latch();
    test_external_power();
    puts("ELEC panel core: pb-sw latches, guards, DRIVE latch/maintenance reset, EXT, RAT, lamps and travel passed");
    return 0;
}
