/* SPDX-License-Identifier: GPL-3.0-or-later
 * Mechanical feedback for momentary pushbuttons, push/pull knob shafts and endless
 * rotary knobs (EFIS, ISIS). Pure C, no XPLM: the adapters feed command phases and
 * wall time and publish `position` / the rotation angle as datarefs that drive the
 * authored OBJ translations and rotations.
 *
 * Press: rises on command Begin, falls on command End, with an ~80 ms eased travel each
 * way. A click whose Begin and End arrive in one frame still shows a full 80 ms press.
 * `direction` is +1 (push, into the panel) or -1 (pull) for knob shafts.
 * Rotation: accumulates detents continuously (no wrap at 360); the OBJ rotation uses
 * ANIM_keyframe_loop 360. It is renormalised by whole turns only far from zero.
 */
#ifndef A380X_CONTROL_MOTION_H
#define A380X_CONTROL_MOTION_H
#include <math.h>

#define A380X_PRESS_TRAVEL_S 0.08f

typedef struct {
    float position;   /* published: 0 rest .. +/-1 fully pressed / pulled */
    float progress;   /* 0..1 linear travel progress */
    float hold_until; /* minimum visible press after a click */
    float direction;
    unsigned char held;
} A380XPress;

static inline void a380x_press_begin(A380XPress *p, float now, float direction)
{
    p->held = 1;
    p->direction = direction < 0.0f ? -1.0f : 1.0f;
    p->hold_until = now + A380X_PRESS_TRAVEL_S;
}

static inline void a380x_press_end(A380XPress *p) { p->held = 0; }

static inline void a380x_press_update(A380XPress *p, float now, float dt)
{
    float target = (p->held || now < p->hold_until) ? 1.0f : 0.0f, step;
    if (!isfinite(dt) || dt < 0.0f) dt = 0.0f;
    step = fminf(dt, 0.1f) / A380X_PRESS_TRAVEL_S;
    p->progress = p->progress < target ? fminf(target, p->progress + step) : fmaxf(target, p->progress - step);
    /* smoothstep ease in/out */
    p->position = p->direction * p->progress * p->progress * (3.0f - 2.0f * p->progress);
    if (p->progress <= 0.0f) p->position = 0.0f;
}

static inline void a380x_knob_turn(float *angle, float detents, float degrees_per_detent)
{
    if (!isfinite(detents) || !isfinite(*angle)) return;
    *angle += detents * degrees_per_detent;
    if (fabsf(*angle) > 36000.0f) *angle = fmodf(*angle, 360.0f); /* whole turns: invisible */
}
#endif
