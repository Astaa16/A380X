/* SPDX-License-Identifier: GPL-3.0-or-later
 * Detented cockpit lever: XPLM-free core shared by the thrust and reverse levers
 * (docs/THRUST_LEVERS.md) and intended for the flaps lever and the speed brake.
 *
 * The OBJ manipulator (an X-Plane click-hold-drag ATTR_manip_drag_rotate that follows the lever
 * arc) writes the HAND position into a plugin dataref. This core turns the hand into the LEVER
 * position the aircraft uses and the 3D lever shows:
 *   - end stops: the lever is clamped to [min, max];
 *   - sticky (magnetic) detents: a hand that reaches or crosses a detent captures the lever there,
 *     and the lever stays exactly in the detent until the hand is more than `escape` away from it
 *     (extra drag to leave). A detent crossed with the hand already beyond `escape` (a fast,
 *     deliberate push) is passed, as a real gate is;
 *   - click events: one per entry of the lever into a detent's +-click_in band, re-armed only
 *     after the lever has left +-click_out (no chatter from hardware axis noise);
 *   - external positions (hardware axis, keyboard command, another plugin) are applied directly,
 *     without stickiness, but still produce click events.
 * A drag has no explicit end in X-Plane: it ends when the manipulator has not written for
 * `timeout` seconds of wall time, after which the hand follows the lever again, so the next click
 * starts where the lever is shown.
 */
#ifndef A380X_DETENT_LEVER_H
#define A380X_DETENT_LEVER_H
#include <math.h>
#include <string.h>

#define A380X_DETENT_LEVER_MAX 8
#define A380X_DETENT_LEVER_TIMEOUT_S 0.25

typedef struct {
    double position;   /* lever units */
    double capture;    /* hand within +-capture of the detent (or crossing it) captures; 0 = not sticky */
    double escape;     /* a captured lever stays until the hand is more than +-escape away (>= capture) */
    double click_in;   /* lever entering +-click_in of the detent: one click event */
    double click_out;  /* the click re-arms only after the lever has left +-click_out (>= click_in) */
} A380XDetent;

typedef struct {
    double min, max;
    A380XDetent detent[A380X_DETENT_LEVER_MAX];
    int count;
    double value;      /* lever position (aircraft input and 3D lever) */
    double hand;       /* last manipulator input while dragging */
    int captured;      /* sticky detent holding the lever during a drag, -1 none */
    int dragging;
    double idle_s;     /* wall time since the last manipulator write */
    unsigned armed;    /* bit k: detent k may click again */
    unsigned clicks;   /* bit k: detent k entered since the last a380x_detent_lever_take_clicks */
} A380XDetentLever;

static inline double a380x_detent_clamp(double x, double lo, double hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}

/* Clamp to the end stops; within 1e-6 of a stop IS the stop (rounding of the manipulator's float
 * arithmetic must never leave a lever a hair above IDLE, which would trip the interlocks). */
static inline double a380x_detent_stop(double x, double lo, double hi)
{
    x = a380x_detent_clamp(x, lo, hi);
    if (x - lo < 1e-6) return lo;
    if (hi - x < 1e-6) return hi;
    return x;
}

/* Click bookkeeping after every change of the lever position. */
static inline void a380x_detent_lever_bands(A380XDetentLever *l)
{
    int k;
    for (k = 0; k < l->count; ++k) {
        double d = fabs(l->value - l->detent[k].position);
        unsigned bit = 1u << k;
        if ((l->armed & bit) && d <= l->detent[k].click_in) {
            l->clicks |= bit;
            l->armed &= ~bit;
        } else if (!(l->armed & bit) && d > l->detent[k].click_out) {
            l->armed |= bit;
        }
    }
}

/* Arms every click whose band does not hold `value`, so initialisation never clicks. */
static inline void a380x_detent_lever_rearm(A380XDetentLever *l)
{
    int k;
    l->armed = 0;
    for (k = 0; k < l->count; ++k)
        if (fabs(l->value - l->detent[k].position) > l->detent[k].click_in) l->armed |= 1u << k;
    l->clicks = 0;
}

/* Detents must be ordered by position; at most A380X_DETENT_LEVER_MAX. */
static inline void a380x_detent_lever_init(A380XDetentLever *l, double min, double max,
                                           const A380XDetent *detents, int count, double value)
{
    memset(l, 0, sizeof(*l));
    l->min = min;
    l->max = max;
    l->count = count < 0 ? 0 : count > A380X_DETENT_LEVER_MAX ? A380X_DETENT_LEVER_MAX : count;
    if (l->count) memcpy(l->detent, detents, sizeof(A380XDetent) * (size_t)l->count);
    l->value = l->hand = a380x_detent_stop(isfinite(value) ? value : min, min, max);
    l->captured = -1;
    a380x_detent_lever_rearm(l);
}

/* Sticky detent the resting lever is exactly in (a new drag starts captured there). */
static inline int a380x_detent_lever_resting(const A380XDetentLever *l)
{
    int k;
    for (k = 0; k < l->count; ++k)
        if (l->detent[k].capture > 0.0 && fabs(l->value - l->detent[k].position) < 1e-9) return k;
    return -1;
}

/* Value the manipulator dataref reports: the hand while dragging, else the lever. */
static inline double a380x_detent_lever_input(const A380XDetentLever *l)
{
    return l->dragging ? l->hand : l->value;
}

/* A manipulator write: hand position in lever units. */
static inline void a380x_detent_lever_drag(A380XDetentLever *l, double hand)
{
    double previous;
    int k, best = -1;
    if (!isfinite(hand)) return;
    hand = a380x_detent_stop(hand, l->min, l->max);
    if (!l->dragging) {
        l->dragging = 1;
        l->hand = l->value;
        l->captured = a380x_detent_lever_resting(l);
    }
    previous = l->hand;
    l->hand = hand;
    l->idle_s = 0.0;
    if (l->captured >= 0) {
        const A380XDetent *d = &l->detent[l->captured];
        if (fabs(hand - d->position) <= d->escape) {
            l->value = d->position;
            a380x_detent_lever_bands(l);
            return;
        }
        l->captured = -1;   /* broke free: the lever jumps to the hand */
    }
    /* First sticky detent on the path previous -> hand (or reached without crossing) that the
     * hand is still within escape of; a detent passed by more than escape is a deliberate push. */
    for (k = 0; k < l->count; ++k) {
        const A380XDetent *d = &l->detent[k];
        int crossed, reached;
        if (d->capture <= 0.0) continue;
        crossed = (previous - d->position) * (hand - d->position) <= 0.0 && previous != hand;
        reached = fabs(hand - d->position) <= d->capture;
        if (!(crossed || reached) || fabs(hand - d->position) > d->escape) continue;
        if (best < 0 || fabs(d->position - previous) < fabs(l->detent[best].position - previous)) best = k;
    }
    if (best >= 0) {
        l->captured = best;
        l->value = l->detent[best].position;
    } else {
        l->value = hand;
    }
    a380x_detent_lever_bands(l);
}

/* The manipulator wrote while the lever is mechanically locked (interlock): the hand is tracked,
 * the lever does not move. */
static inline void a380x_detent_lever_drag_locked(A380XDetentLever *l, double hand)
{
    if (!isfinite(hand)) return;
    if (!l->dragging) { l->dragging = 1; l->captured = -1; }
    l->hand = a380x_detent_stop(hand, l->min, l->max);
    l->idle_s = 0.0;
}

/* End of a drag: the hand follows the lever again. */
static inline void a380x_detent_lever_end_drag(A380XDetentLever *l)
{
    l->dragging = 0;
    l->captured = -1;
    l->hand = l->value;
    l->idle_s = 0.0;
}

/* External position (hardware axis, command, interlock): applied without stickiness; an ongoing
 * cockpit drag is ended. */
static inline void a380x_detent_lever_set(A380XDetentLever *l, double value)
{
    if (!isfinite(value)) return;
    l->value = a380x_detent_stop(value, l->min, l->max);
    a380x_detent_lever_end_drag(l);
    a380x_detent_lever_bands(l);
}

/* Wall-clock step: ends a drag the manipulator stopped writing. */
static inline void a380x_detent_lever_tick(A380XDetentLever *l, double wall_dt)
{
    if (!l->dragging || !isfinite(wall_dt) || wall_dt < 0.0) return;
    l->idle_s += wall_dt;
    if (l->idle_s >= A380X_DETENT_LEVER_TIMEOUT_S) a380x_detent_lever_end_drag(l);
}

/* Detents entered since the last call (bit k = detent k); clears them. */
static inline unsigned a380x_detent_lever_take_clicks(A380XDetentLever *l)
{
    unsigned clicks = l->clicks;
    l->clicks = 0;
    return clicks;
}

#endif
