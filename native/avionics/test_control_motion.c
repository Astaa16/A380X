/* SPDX-License-Identifier: GPL-3.0-or-later
 * Momentary press / push-pull / endless knob feedback (a380x_control_motion.h). */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "a380x_control_motion.h"

static void run(A380XPress *p, float *now, float seconds)
{
    for (float t = 0; t < seconds - 1e-6f; t += 0.01f) { *now += 0.01f; a380x_press_update(p, *now, 0.01f); }
}

int main(void)
{
    A380XPress p;
    float now = 0, angle = 0;
    memset(&p, 0, sizeof(p));
    /* Held: eased 80 ms travel in, stays in while held, eased 80 ms out after End. */
    a380x_press_begin(&p, now, 1);
    run(&p, &now, 0.04f);
    assert(p.position > 0.4f && p.position < 0.6f);
    run(&p, &now, 0.05f);
    assert(p.position == 1.0f);
    run(&p, &now, 1.0f);
    assert(p.position == 1.0f);
    a380x_press_end(&p);
    run(&p, &now, 0.04f);
    assert(p.position > 0.4f && p.position < 0.6f);
    run(&p, &now, 0.05f);
    assert(p.position == 0.0f);
    /* Click: Begin and End in one frame still shows a full press, then springs back. */
    a380x_press_begin(&p, now, 1);
    a380x_press_end(&p);
    run(&p, &now, 0.08f);
    assert(p.position > 0.95f);
    run(&p, &now, 0.1f);
    assert(p.position == 0.0f);
    /* Pull: negative travel. */
    a380x_press_begin(&p, now, -1);
    run(&p, &now, 0.1f);
    assert(p.position == -1.0f);
    a380x_press_end(&p);
    run(&p, &now, 0.1f);
    assert(p.position == 0.0f);
    /* Endless knob: continuous through 360 in both directions, renormalised by whole turns. */
    for (int i = 0; i < 37; ++i) a380x_knob_turn(&angle, 1, 10);
    assert(fabsf(angle - 370.0f) < 1e-4f);
    a380x_knob_turn(&angle, -40, 10);
    assert(fabsf(angle + 30.0f) < 1e-4f);
    angle = 35995;
    a380x_knob_turn(&angle, 1, 10);
    assert(fabsf(fmodf(angle, 360.0f) - fmodf(36005.0f, 360.0f)) < 1e-2f && fabsf(angle) < 360.0f);
    puts("control motion: momentary press, click pulse, push/pull and endless knob tests passed");
    return 0;
}
