/* SPDX-License-Identifier: GPL-3.0-or-later
 * A380 FLAPS lever, slat/flap control computers (SFCC) and the flap/slat drives: XPLM-free core
 * (docs/FLAPS_LEVER.md). X-Plane side: a380x_flaps.inc.
 *
 * Lever. Five positions 0 / 1 / 2 / 3 / FULL = 0..4 (lever units). The lever sits latched in a detent;
 * it moves only after the pilot has pulled the handle out of the detent (lift >= 0.5, X-Plane's
 * lift-detent drag manipulator writes the lift). Pulling it out starts one ACTION whose travel is
 * limited by the gates at 1 and 3: from 0 the lever reaches 1, from 1 or 2 it reaches 0..3 / 1..3, from 3
 * it reaches 1..FULL, from FULL 3..FULL. To pass a gate the lever must be dropped into it and pulled out
 * again. The lever never rests between positions: releasing it (lift back to 0, or the drag ending)
 * springs it into the nearest position of the current window. Commands and hardware set positions as
 * complete actions.
 *
 * SFCC. Translation of FlyByWire fbw-a380x/src/wasm/systems/a380_systems/src/hydraulic/sfcc/mod.rs
 * (pinned 2baa2b35) SlatFlapControlComputer::generate_configuration with its thresholds: CONF 1 vs 1+F
 * (205 / 212 kt, ground), automatic retraction 1+F -> 1 above 212 kt and re-extension below 205 kt,
 * cruise baulk (265.5 kt / FL220), alpha/speed lock (155 / 161 kt, 9.5 / 9.2 deg AoA) and the flap load
 * relief system (FULL->3 184.5 / 179.5 kt, 3->2S 198.5 / 193.5 kt, 2->1+F 222.5 / 217.5 kt). The CSU
 * handle index is the last valid detent of the lever (FBW's FLAPS_HANDLE_INDEX), so an action that
 * passes out of detent behaves as FBW's discrete handle.
 *
 * Drives. FBW FlapSlatAssembly (fbw-common hydraulic/flap_slat.rs, A380 parameters in a380_systems
 * hydraulic/mod.rs): FPPU (synchro) angles, flaps 0.07 rad/s and slats 0.08 rad/s of control arm at full
 * pressure (x 314.98/140 synchro ratio = 9.024 / 10.313 FPPU deg/s), 0.3 s speed filter, half the
 * speed per hydraulic side, 6.69 deg positioning / 0.18 deg target thresholds, FPPU -> surface angle
 * tables. Flaps: green + yellow, slats: green (as FBW).
 */
#ifndef A380X_FLAPS_H
#define A380X_FLAPS_H
#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------- constants */
#define A380X_FLAPS_FULL 4                 /* lever positions 0..4 */
#define A380X_FLAPS_OUT_OF_DETENT (-1)
#define A380X_FLAPS_LIFT_OUT 0.5           /* lift (0..1) that takes the lever out of its detent */
#define A380X_FLAPS_DETENT_TOLERANCE 0.08  /* lever units: the CSU reads the detent within +-0.08 (~1 deg) */
#define A380X_FLAPS_SEAT_TOLERANCE 0.25    /* lift dropped within +-0.25 of a position: it seats there */
#define A380X_FLAPS_DRAG_TIMEOUT_S 0.4     /* wall time without manipulator writes = released */
#define A380X_FLAPS_LOCK_RATE 5.0          /* pull-out sleeve animation, full travel in 0.2 s */
#define A380X_FLAPS_ANIM_RATE 6.0          /* 3D lever following a command / hardware step, positions per s */

enum { A380X_CONF0, A380X_CONF1, A380X_CONF1F, A380X_CONF2, A380X_CONF2S, A380X_CONF3, A380X_CONFFULL,
       A380X_CONF_COUNT };

/* FBW SFCC thresholds (mod.rs SlatFlapControlComputer). */
#define A380X_SFCC_HANDLE_ONE_CONF_KT 205.0
#define A380X_SFCC_CONF1F_TO_CONF1_KT 212.0
#define A380X_SFCC_CRUISE_BAULK_KT 265.5
#define A380X_SFCC_CRUISE_BAULK_FT 22000.0
#define A380X_SFCC_ALPHA_LOCK_IN_KT 155.0
#define A380X_SFCC_ALPHA_LOCK_OUT_KT 161.0
#define A380X_SFCC_ALPHA_LOCK_IN_DEG 9.5
#define A380X_SFCC_ALPHA_LOCK_OUT_DEG 9.2
#define A380X_SFCC_FULL_TO_3_KT 184.5
#define A380X_SFCC_3_TO_2S_KT 198.5
#define A380X_SFCC_2_TO_1F_KT 222.5
#define A380X_SFCC_1F_TO_2_KT 217.5
#define A380X_SFCC_2S_TO_3_KT 193.5
#define A380X_SFCC_3_TO_FULL_KT 179.5

/* FBW flaps_channel.rs / slats_channel.rs demanded FPPU angles per configuration. */
static const double a380x_flaps_demand_fppu[A380X_CONF_COUNT] = {0.0, 0.0, 215.68, 259.28, 259.28, 297.59, 338.99};
static const double a380x_slats_demand_fppu[A380X_CONF_COUNT] = {0.0, 286.48, 286.48, 286.48, 327.39, 327.39, 327.39};
/* FBW a380_systems hydraulic/mod.rs FLAP/SLAT_FPPU_TO_SURFACE_ANGLE_BREAKPTS / _DEGREES. */
static const double a380x_flap_fppu_points[7] = {0.0, 122.2, 215.68, 259.28, 279.84, 297.59, 338.99};
static const double a380x_flap_deg_points[7] = {0.0, 0.0, 8.0, 17.0, 22.0, 26.0, 33.0};
static const double a380x_slat_fppu_points[3] = {0.0, 286.48, 327.39};
static const double a380x_slat_deg_points[3] = {0.0, 20.0, 23.0};
#define A380X_FLAPS_MAX_FPPU 338.99
#define A380X_SLATS_MAX_FPPU 327.39
#define A380X_FLAPS_MAX_DEG 33.0
#define A380X_SLATS_MAX_DEG 23.0
/* Full-pressure FPPU speeds: control-arm rad/s x 180/pi x (314.98 / 140). */
#define A380X_FLAPS_FPPU_SPEED (0.07 * 57.29577951308232 * 314.98 / 140.0)
#define A380X_SLATS_FPPU_SPEED (0.08 * 57.29577951308232 * 314.98 / 140.0)
#define A380X_FLAPS_POSITIONING_DEG 6.69
#define A380X_FLAPS_TARGET_DEG 0.18
#define A380X_FLAPS_SPEED_TAU_S 0.3
#define A380X_FLAPS_LOW_SPEED 0.18           /* low-speed drive: 18 % of the high speed */
#define A380X_FLAPS_DECEL_PER_30HZ 0.976     /* FBW per-update deceleration, referenced to 30 Hz */
/* FBW hydraulic speed factor: brake release 500 psi, A380 target 5250 psi. */
#define A380X_FLAPS_BRAKE_PSI 500.0
#define A380X_FLAPS_TARGET_PSI 5250.0
/* PRIM aileron droop (A380PrimComputerFctl: 5 deg when the flaps are not retracted, 1 deg/s). */
#define A380X_FLAPS_DROOP_DEG 5.0
#define A380X_FLAPS_DROOP_RATE 1.0
#define A380X_FLAPS_RETRACTED_FPPU 10.063
/* PerformanceConstants.ts: VMO-clean, VfeF1, VfeF1F, VfeF2, VfeF3, VfeFF. */
static const double a380x_flaps_vfe_table[6] = {0.0, 263.0, 222.0, 220.0, 196.0, 182.0};
static const double a380x_flaps_vfe_next_table[6] = {263.0, 222.0, 220.0, 196.0, 182.0, 0.0};
/* ACF detent k = FlapsConf k (tools/flaps_lever_cockpit.py ACF_CHANGES). */
static const double a380x_flaps_xp_flap_deg[A380X_CONF_COUNT] = {0.0, 0.0, 8.0, 17.0, 17.0, 26.0, 33.0};
static const int a380x_flaps_lever_of_conf[A380X_CONF_COUNT] = {0, 1, 1, 2, 2, 3, 4};

static inline double a380x_flaps_clamp(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }

static inline double a380x_flaps_interp(const double *x, const double *y, int n, double v)
{
    int i;
    if (!(v > x[0])) return y[0];
    for (i = 1; i < n; ++i)
        if (v <= x[i]) return x[i] > x[i - 1] ? y[i - 1] + (y[i] - y[i - 1]) * (v - x[i - 1]) / (x[i] - x[i - 1]) : y[i];
    return y[n - 1];
}
static inline double a380x_flap_deg_of_fppu(double fppu) { return a380x_flaps_interp(a380x_flap_fppu_points, a380x_flap_deg_points, 7, fppu); }
static inline double a380x_slat_deg_of_fppu(double fppu) { return a380x_flaps_interp(a380x_slat_fppu_points, a380x_slat_deg_points, 3, fppu); }

/* ---------------------------------------------------------------- lever */
typedef struct {
    double value;          /* lever position 0..4 (3D lever, CSU) */
    double hand;           /* last manipulator lever input */
    double lift;           /* last manipulator lift 0..1 */
    int latched;           /* seated in `detent` */
    int detent;            /* the seated position (last seated while out of detent) */
    int window_lo, window_hi;  /* travel window of the current action (gates at 1 and 3) */
    int dragging;
    double idle_s;
    double lock_anim;      /* pull-out sleeve 0..1 */
    double anim;           /* 3D lever: the lever while dragged, rate-limited toward it after a step */
    unsigned actions;      /* completed pull-out actions (diagnostics/tests) */
} A380XFlapsLever;

/* Gates at 1 and 3: one action from `from` stays within [lo, hi]. */
static inline void a380x_flaps_gate_window(int from, int *lo, int *hi)
{
    *lo = from >= 4 ? 3 : from >= 2 ? 1 : 0;
    *hi = from <= 0 ? 1 : from <= 2 ? 3 : 4;
}

static inline void a380x_flaps_lever_seat(A380XFlapsLever *l, int position)
{
    position = position < 0 ? 0 : position > A380X_FLAPS_FULL ? A380X_FLAPS_FULL : position;
    if (!l->latched) ++l->actions;
    l->latched = 1;
    l->detent = position;
    l->value = position;
    a380x_flaps_gate_window(position, &l->window_lo, &l->window_hi);
}

static inline void a380x_flaps_lever_init(A380XFlapsLever *l, int position)
{
    memset(l, 0, sizeof(*l));
    l->latched = 1;   /* no action counted */
    a380x_flaps_lever_seat(l, position);
    l->hand = l->anim = l->value;
}

/* Nearest position of the current window (the spring of a released lever). */
static inline int a380x_flaps_lever_nearest(const A380XFlapsLever *l)
{
    int k = (int)lround(a380x_flaps_clamp(l->value, 0.0, A380X_FLAPS_FULL));
    return k < l->window_lo ? l->window_lo : k > l->window_hi ? l->window_hi : k;
}

static inline void a380x_flaps_lever_begin(A380XFlapsLever *l)
{
    if (l->dragging) return;
    l->dragging = 1;
    l->hand = l->value;
    l->lift = l->latched ? 0.0 : l->lift;
}

/* Manipulator: lever (rotation) input in lever units. A latched lever ignores it. */
static inline void a380x_flaps_lever_drag(A380XFlapsLever *l, double hand)
{
    if (!isfinite(hand)) return;
    a380x_flaps_lever_begin(l);
    l->hand = a380x_flaps_clamp(hand, 0.0, A380X_FLAPS_FULL);
    l->idle_s = 0.0;
    if (!l->latched) l->value = a380x_flaps_clamp(l->hand, l->window_lo, l->window_hi);
}

/* Manipulator: lift (pull-out) input 0..1. Pulling out of a detent starts an action; letting the lift
 * drop at (or near) a position seats the lever there. */
static inline void a380x_flaps_lever_lift(A380XFlapsLever *l, double lift)
{
    if (!isfinite(lift)) return;
    a380x_flaps_lever_begin(l);
    l->lift = a380x_flaps_clamp(lift, 0.0, 1.0);
    l->idle_s = 0.0;
    if (l->latched && l->lift >= A380X_FLAPS_LIFT_OUT) {
        l->latched = 0;
        a380x_flaps_gate_window(l->detent, &l->window_lo, &l->window_hi);
        l->value = a380x_flaps_clamp(l->hand, l->window_lo, l->window_hi);
    } else if (!l->latched && l->lift < A380X_FLAPS_LIFT_OUT) {
        int k = a380x_flaps_lever_nearest(l);
        if (fabs(l->value - k) <= A380X_FLAPS_SEAT_TOLERANCE) a380x_flaps_lever_seat(l, k);
    }
}

/* Released: the handle drops, the lever springs into the nearest position of its window. */
static inline void a380x_flaps_lever_end_drag(A380XFlapsLever *l)
{
    if (!l->latched) a380x_flaps_lever_seat(l, a380x_flaps_lever_nearest(l));
    l->dragging = 0;
    l->lift = 0.0;
    l->hand = l->value;
    l->idle_s = 0.0;
}

/* Complete action to `position` (commands, hardware): ends a drag, ignores the gates. */
static inline void a380x_flaps_lever_set(A380XFlapsLever *l, int position)
{
    if (l->dragging) a380x_flaps_lever_end_drag(l);
    l->latched = 0;
    a380x_flaps_lever_seat(l, position);
    l->hand = l->value;
}

/* One position down (+1, extend) or up (-1, retract): a complete action. */
static inline int a380x_flaps_lever_step(A380XFlapsLever *l, int direction)
{
    int from;
    if (l->dragging) a380x_flaps_lever_end_drag(l);
    from = l->detent;
    a380x_flaps_lever_set(l, from + (direction > 0 ? 1 : -1));
    return l->detent != from;
}

/* Wall-clock step: drag timeout, sleeve animation. */
static inline void a380x_flaps_lever_tick(A380XFlapsLever *l, double wall_dt)
{
    double target, step;
    if (!isfinite(wall_dt) || wall_dt < 0.0) wall_dt = 0.0;
    if (l->dragging) {
        l->idle_s += wall_dt;
        if (l->idle_s >= A380X_FLAPS_DRAG_TIMEOUT_S) a380x_flaps_lever_end_drag(l);
    }
    target = (!l->latched || (l->dragging && l->lift >= A380X_FLAPS_LIFT_OUT)) ? 1.0 : 0.0;
    step = A380X_FLAPS_LOCK_RATE * wall_dt;
    l->lock_anim = l->lock_anim < target ? fmin(target, l->lock_anim + step) : fmax(target, l->lock_anim - step);
    /* The hand moves the lever directly; a command, hardware step or spring is shown as a quick travel. */
    if (l->dragging && !l->latched) l->anim = l->value;
    else {
        step = A380X_FLAPS_ANIM_RATE * wall_dt;
        l->anim = l->anim < l->value ? fmin(l->value, l->anim + step) : fmax(l->value, l->anim - step);
    }
}

static inline double a380x_flaps_lever_input(const A380XFlapsLever *l) { return l->dragging ? l->hand : l->value; }
static inline double a380x_flaps_lever_lift_input(const A380XFlapsLever *l) { return l->dragging ? l->lift : 0.0; }

/* CSU: the detent the lever is in, or out of detent. */
static inline int a380x_flaps_lever_csu(const A380XFlapsLever *l)
{
    int k = (int)lround(a380x_flaps_clamp(l->value, 0.0, A380X_FLAPS_FULL));
    return fabs(l->value - k) <= A380X_FLAPS_DETENT_TOLERANCE ? k : A380X_FLAPS_OUT_OF_DETENT;
}

/* ---------------------------------------------------------------- SFCC */
typedef struct {
    double ias_kt, pressure_alt_ft, aoa_deg;
    int aoa_valid, on_ground;
} A380XSfccInputs;

typedef struct {
    int conf;              /* FBW FlapsConf (A380X_CONF*) */
    int handle;            /* CSU handle index 0..4 (last valid detent; FBW FLAPS_HANDLE_INDEX) */
    int previous_handle;
    int relief, cruise_baulk, alpha_lock;
} A380XSfcc;

static inline void a380x_sfcc_init(A380XSfcc *s, int handle, int conf)
{
    memset(s, 0, sizeof(*s));
    s->handle = s->previous_handle = handle < 0 ? 0 : handle > 4 ? 4 : handle;
    s->conf = conf < 0 ? 0 : conf >= A380X_CONF_COUNT ? A380X_CONF_COUNT - 1 : conf;
}

/* SlatFlapControlComputer::generate_configuration, arm for arm. */
static inline int a380x_sfcc_generate(const A380XSfcc *s, int prev, int cur, const A380XSfccInputs *in)
{
    double ias = in->ias_kt, aoa = in->aoa_valid ? in->aoa_deg : 0.0;
    int in_flight = !in->on_ground;
    if (cur == 1 && (prev == 0 || prev == 1) && (ias < A380X_SFCC_HANDLE_ONE_CONF_KT || in->on_ground))
        return A380X_CONF1F;
    if (cur == 1 && (prev == 0 || prev == 1) && s->conf == A380X_CONF0
        && (ias > A380X_SFCC_CRUISE_BAULK_KT || in->pressure_alt_ft > A380X_SFCC_CRUISE_BAULK_FT))
        return A380X_CONF0;
    if (prev == 0 && cur == 1) return A380X_CONF1;
    if (prev == 1 && cur == 1 && ias > A380X_SFCC_CONF1F_TO_CONF1_KT) return A380X_CONF1;
    if (prev == 1 && cur == 1) return s->conf;
    if (cur == 1 && ias <= A380X_SFCC_CONF1F_TO_CONF1_KT) return A380X_CONF1F;
    if (cur == 1) return A380X_CONF1;
    if (cur == 0 && in_flight && s->alpha_lock)
        return (ias > A380X_SFCC_ALPHA_LOCK_OUT_KT || aoa < A380X_SFCC_ALPHA_LOCK_OUT_DEG) ? A380X_CONF0 : s->conf;
    if (cur == 0 && prev >= 1 && prev <= 4 && in_flight
        && (ias < A380X_SFCC_ALPHA_LOCK_IN_KT || aoa > A380X_SFCC_ALPHA_LOCK_IN_DEG))
        return A380X_CONF1F;
    if (cur == 0) return A380X_CONF0;
    if (cur == 2 && (prev == 1 || prev == 2) && ias > A380X_SFCC_2_TO_1F_KT) return A380X_CONF1F;
    if (prev == 2 && cur == 2 && s->conf == A380X_CONF1F)
        return ias < A380X_SFCC_1F_TO_2_KT ? A380X_CONF2 : A380X_CONF1F;
    if (cur == 3 && (prev == 2 || prev == 3) && ias > A380X_SFCC_3_TO_2S_KT) return A380X_CONF2S;
    if (prev == 3 && cur == 3 && s->conf == A380X_CONF2S)
        return ias < A380X_SFCC_2S_TO_3_KT ? A380X_CONF3 : A380X_CONF2S;
    if (cur == 4 && (prev == 3 || prev == 4) && ias > A380X_SFCC_FULL_TO_3_KT) return A380X_CONF3;
    if (prev == 4 && cur == 4 && s->conf == A380X_CONF3)
        return ias < A380X_SFCC_3_TO_FULL_KT ? A380X_CONFFULL : A380X_CONF3;
    if (cur == 2 && prev != 2) return A380X_CONF2;
    if (cur == 3 && prev != 3) return A380X_CONF3;
    if (cur == 4 && prev != 4) return A380X_CONFFULL;
    return s->conf;
}

/* One SFCC cycle: `csu` is the lever's detent or A380X_FLAPS_OUT_OF_DETENT (the handle index holds). */
static inline void a380x_sfcc_update(A380XSfcc *s, int csu, const A380XSfccInputs *in)
{
    int h;
    s->previous_handle = s->handle;
    if (csu >= 0 && csu <= 4) s->handle = csu;
    s->conf = a380x_sfcc_generate(s, s->previous_handle, s->handle, in);
    h = s->handle;
    s->relief = (h == 2 && s->conf != A380X_CONF2) || (h == 3 && s->conf != A380X_CONF3)
             || (h == 4 && s->conf != A380X_CONFFULL);
    s->cruise_baulk = h == 1 && s->conf == A380X_CONF0;
    s->alpha_lock = h == 0 && (s->conf == A380X_CONF1 || s->conf == A380X_CONF1F);
}

/* ---------------------------------------------------------------- drives */
typedef struct {
    double fppu;        /* FPPU (synchro) angle, deg */
    double speed;       /* FPPU deg/s, signed */
    double max_speed;   /* filtered available speed */
    double full_speed;  /* FPPU deg/s at full pressure on both sides */
    double max_fppu;
} A380XFlapsDrive;

static inline void a380x_flaps_drive_init(A380XFlapsDrive *d, double full_speed, double max_fppu, double fppu)
{
    memset(d, 0, sizeof(*d));
    d->full_speed = full_speed;
    d->max_fppu = max_fppu;
    d->fppu = a380x_flaps_clamp(fppu, 0.0, max_fppu);
}

/* FBW FlapSlatAssembly::max_speed_factor_from_pressure (psi on the FBW 5250 psi scale). */
static inline double a380x_flaps_pressure_factor(double psi)
{
    double p = psi - A380X_FLAPS_BRAKE_PSI;
    if (!isfinite(psi) || psi <= A380X_FLAPS_BRAKE_PSI) return 0.0;
    return a380x_flaps_clamp(0.0004 * p * p / (A380X_FLAPS_TARGET_PSI - A380X_FLAPS_BRAKE_PSI), 0.0, 1.0);
}

/* `factor` = 0.5 x left-side factor + 0.5 x right-side factor (0..1). */
static inline void a380x_flaps_drive_update(A380XFlapsDrive *d, double demand, double factor, double dt)
{
    double error, step, theoretical;
    int active;
    if (!isfinite(dt) || dt <= 0.0) return;
    if (!isfinite(factor)) factor = 0.0;
    error = demand - d->fppu;
    active = fabs(error) >= A380X_FLAPS_TARGET_DEG;   /* POB solenoid energised */
    theoretical = active ? d->full_speed * a380x_flaps_clamp(factor, 0.0, 1.0) : 0.0;
    d->max_speed += (theoretical - d->max_speed) * dt / (dt + A380X_FLAPS_SPEED_TAU_S);
    if (theoretical == 0.0 && d->max_speed < 1e-3) d->max_speed = 0.0;   /* filter tail: stopped */
    if (fabs(error) >= A380X_FLAPS_POSITIONING_DEG) {
        d->speed = error > 0.0 ? d->max_speed : -d->max_speed;
    } else if (!active) {
        d->speed = 0.0;
        d->fppu = demand;   /* POB de-energised within 0.18 deg: settled (FBW leaves the residual) */
        return;
    } else {
        double minimum = A380X_FLAPS_LOW_SPEED * d->max_speed;
        double decayed = d->speed * pow(A380X_FLAPS_DECEL_PER_30HZ, dt * 30.0);
        if (fabs(decayed) >= minimum) d->speed = decayed;
        /* Low-speed drive toward the demand (FBW keeps its last speed; never away from the target). */
        if (d->speed * error <= 0.0) d->speed = error > 0.0 ? minimum : -minimum;
    }
    step = d->speed * dt;
    if (fabs(error) < A380X_FLAPS_POSITIONING_DEG && fabs(step) >= fabs(error)) {
        d->fppu = demand;   /* reaches the target within this step: POB de-energised */
        d->speed = 0.0;
    } else {
        d->fppu += step;
    }
    if (d->fppu < 0.0 || d->fppu > d->max_fppu) { d->fppu = a380x_flaps_clamp(d->fppu, 0.0, d->max_fppu); d->speed = 0.0; }
}

/* ---------------------------------------------------------------- system */
typedef struct {
    A380XFlapsLever lever;
    A380XSfcc sfcc;
    A380XFlapsDrive flaps, slats;
    double droop_deg;
} A380XFlaps;

/* Lever at `position`, SFCC in `conf`, surfaces settled in `conf`. */
static inline void a380x_flaps_init(A380XFlaps *f, int position, int conf)
{
    memset(f, 0, sizeof(*f));
    a380x_flaps_lever_init(&f->lever, position);
    a380x_sfcc_init(&f->sfcc, f->lever.detent, conf);
    a380x_flaps_drive_init(&f->flaps, A380X_FLAPS_FPPU_SPEED, A380X_FLAPS_MAX_FPPU, a380x_flaps_demand_fppu[f->sfcc.conf]);
    a380x_flaps_drive_init(&f->slats, A380X_SLATS_FPPU_SPEED, A380X_SLATS_MAX_FPPU, a380x_slats_demand_fppu[f->sfcc.conf]);
    f->droop_deg = f->flaps.fppu > A380X_FLAPS_RETRACTED_FPPU ? A380X_FLAPS_DROOP_DEG : 0.0;
}

/* Simulated-time step: SFCC (also while paused: dt = 0 keeps the surfaces), drives, aileron droop. */
static inline void a380x_flaps_update(A380XFlaps *f, const A380XSfccInputs *in, double flap_factor,
                                      double slat_factor, double dt)
{
    double target, step;
    a380x_sfcc_update(&f->sfcc, a380x_flaps_lever_csu(&f->lever), in);
    if (!isfinite(dt) || dt <= 0.0) return;
    a380x_flaps_drive_update(&f->flaps, a380x_flaps_demand_fppu[f->sfcc.conf], flap_factor, dt);
    a380x_flaps_drive_update(&f->slats, a380x_slats_demand_fppu[f->sfcc.conf], slat_factor, dt);
    target = f->flaps.fppu > A380X_FLAPS_RETRACTED_FPPU ? A380X_FLAPS_DROOP_DEG : 0.0;
    step = A380X_FLAPS_DROOP_RATE * dt;
    f->droop_deg = f->droop_deg < target ? fmin(target, f->droop_deg + step) : fmax(target, f->droop_deg - step);
}

/* ---------------------------------------------------------------- SFCC words and displays */
#define A380X_BIT(n, condition) ((condition) ? (1u << ((n) - 1)) : 0u)

/* SFCC_n_SLAT_FLAP_SYSTEM_STATUS_WORD (mod.rs slat_flap_system_status_word). */
static inline unsigned a380x_sfcc_status_word(const A380XSfcc *s)
{
    int h = s->handle;
    return A380X_BIT(17, h == 0) | A380X_BIT(18, h == 1) | A380X_BIT(19, h == 2) | A380X_BIT(20, h == 3)
         | A380X_BIT(21, h == 4) | A380X_BIT(22, s->relief) | A380X_BIT(24, s->alpha_lock)
         | A380X_BIT(25, s->cruise_baulk) | A380X_BIT(26, s->conf == A380X_CONF1 || s->cruise_baulk)
         | A380X_BIT(28, 1) | A380X_BIT(29, 1);
}

/* SFCC_n_SLAT_FLAP_ACTUAL_POSITION_WORD (mod.rs slat_flap_actual_position_word). */
static inline unsigned a380x_sfcc_actual_position_word(double flap_fppu, double slat_fppu)
{
    return A380X_BIT(11, 1)
         | A380X_BIT(12, slat_fppu > -16.304 && slat_fppu <= 9.580)
         | A380X_BIT(13, slat_fppu >= 276.900 && slat_fppu <= 343.695)
         | A380X_BIT(14, slat_fppu >= 317.810 && slat_fppu <= 343.695)
         | A380X_BIT(15, slat_fppu >= 317.810 && slat_fppu <= 343.695)
         | A380X_BIT(18, 1)
         | A380X_BIT(19, flap_fppu > -10.504 && flap_fppu <= 10.063)
         | A380X_BIT(20, flap_fppu >= 208.033 && flap_fppu <= 349.495)
         | A380X_BIT(21, flap_fppu >= 251.633 && flap_fppu <= 349.495)
         | A380X_BIT(22, flap_fppu >= 289.943 && flap_fppu <= 349.495)
         | A380X_BIT(23, flap_fppu >= 331.343 && flap_fppu <= 349.495);
}

/* PFD LowerArea.tsx target text index: 0 "0", 1 "1", 2 "1+F", 3 "2", 4 "3", 5 "FULL". */
static inline int a380x_flaps_display_config(const A380XSfcc *s)
{
    switch (s->handle) {
    case 0: return 0;
    case 1: return (s->conf == A380X_CONF1 || s->cruise_baulk) ? 1 : 2;
    case 2: return 3;
    case 3: return 4;
    default: return 5;
    }
}

/* LowerArea.tsx slatsTargetPos / flapsTargetPos (cyan target marker index, -1 = surfaces in place). */
static inline int a380x_flaps_slat_target(const A380XSfcc *s, double slat_fppu)
{
    int h = s->handle;
    if (h == 0 && slat_fppu > 6.1) return 0;
    if ((h == 1 || h == 2) && (slat_fppu < 276.0 || slat_fppu > 296.0)) return 1;
    if ((h == 3 || h == 4) && (slat_fppu < 317.0 || slat_fppu > 337.0)) return 2;
    return -1;
}
static inline int a380x_flaps_flap_target(const A380XSfcc *s, double flap_fppu)
{
    int h = s->handle, retract = s->conf == A380X_CONF1 || s->cruise_baulk, out = flap_fppu > 5.0;
    if ((h == 0 || (h == 1 && retract)) && out) return 0;
    if (h == 1 && !retract && (flap_fppu < 210.0 || flap_fppu > 220.0)) return 1;
    if (h == 2 && (flap_fppu < 254.0 || flap_fppu > 264.0)) return 2;
    if (h == 3 && (flap_fppu < 292.0 || flap_fppu > 302.0)) return 3;
    if (h == 4 && (flap_fppu < 333.0 || flap_fppu > 343.0)) return 4;
    return -1;
}

/* VFE index (0 clean, 1 "1", 2 "1+F", 3 "2", 4 "3", 5 "FULL") of an SFCC configuration (2S flies at
 * the flaps-17 limit) and of the actual surfaces (the enclosing upper position while moving). */
static inline int a380x_flaps_vfe_index_of_conf(int conf)
{
    static const int index[A380X_CONF_COUNT] = {0, 1, 2, 3, 3, 4, 5};
    return conf >= 0 && conf < A380X_CONF_COUNT ? index[conf] : 0;
}
static inline int a380x_flaps_vfe_index_of_surfaces(double flap_fppu, double slat_fppu)
{
    if (flap_fppu > 297.59 + 0.5) return 5;
    if (flap_fppu > 259.28 + 0.5) return 4;
    if (flap_fppu > 215.68 + 0.5) return 3;
    if (flap_fppu > A380X_FLAPS_RETRACTED_FPPU) return 2;
    return slat_fppu > 9.580 ? 1 : 0;
}
/* Current VFE (kt, 0 = clean: VMO/MMO apply) = the more restrictive of commanded and actual. */
static inline double a380x_flaps_vfe(const A380XFlaps *f)
{
    int a = a380x_flaps_vfe_index_of_conf(f->sfcc.conf);
    int b = a380x_flaps_vfe_index_of_surfaces(f->flaps.fppu, f->slats.fppu);
    return a380x_flaps_vfe_table[a > b ? a : b];
}
static inline double a380x_flaps_vfe_next(const A380XFlaps *f)
{
    return a380x_flaps_vfe_next_table[a380x_flaps_display_config(&f->sfcc)];
}

/* ---------------------------------------------------------------- X-Plane handle mapping */
/* The ACF carries the seven SFCC configurations as detents 0..6 (flaps 0/0/8/17/17/26/33). */
static inline int a380x_flaps_xp_layout_ok(int detents, const float *flap_dn, int count)
{
    int k;
    if (detents != A380X_CONF_COUNT - 1 || count < A380X_CONF_COUNT) return 0;
    for (k = 0; k < A380X_CONF_COUNT; ++k)
        if (!isfinite(flap_dn[k]) || fabs(flap_dn[k] - a380x_flaps_xp_flap_deg[k]) > 0.01) return 0;
    return 1;
}

/* X-Plane flap handle request for an SFCC configuration. Unknown ACF tables (`layout_ok` 0): the handle
 * position whose interpolated flap angle is the configuration's angle (CONF 1 -> the first detent). */
static inline double a380x_flaps_xp_request(int conf, int layout_ok, int detents, const float *flap_dn, int count)
{
    double deg;
    int k;
    if (conf < 0) conf = 0;
    if (conf >= A380X_CONF_COUNT) conf = A380X_CONF_COUNT - 1;
    if (layout_ok) return conf / (double)(A380X_CONF_COUNT - 1);
    if (detents < 1 || count <= detents) return conf ? 1.0 : 0.0;
    deg = a380x_flaps_xp_flap_deg[conf];
    if (conf == 0) return 0.0;
    if (deg <= 0.0) return 1.0 / detents;
    for (k = 1; k <= detents; ++k)
        if (flap_dn[k] >= deg && flap_dn[k] > flap_dn[k - 1])
            return a380x_flaps_clamp((k - 1 + (deg - flap_dn[k - 1]) / (flap_dn[k] - flap_dn[k - 1])) / detents, 0.0, 1.0);
    return 1.0;
}

/* A foreign X-Plane handle value (hardware axis, another plugin, preset) as a lever position:
 * ratio x 4, with 0.6 positions of hysteresis around the current position against axis noise. */
static inline int a380x_flaps_hardware_position(double ratio, int current)
{
    double p;
    if (!isfinite(ratio)) return current;
    p = a380x_flaps_clamp(ratio, 0.0, 1.0) * A380X_FLAPS_FULL;
    if (fabs(p - current) <= 0.6) return current;
    return (int)lround(p);
}

/* A loaded X-Plane handle (aircraft load, replay end) as an SFCC configuration. */
static inline int a380x_flaps_conf_of_request(double ratio, int layout_ok)
{
    static const int legacy[A380X_FLAPS_FULL + 1] = {A380X_CONF0, A380X_CONF1F, A380X_CONF2, A380X_CONF3,
                                                     A380X_CONFFULL};
    if (!isfinite(ratio)) return A380X_CONF0;
    ratio = a380x_flaps_clamp(ratio, 0.0, 1.0);
    if (layout_ok) return (int)lround(ratio * (A380X_CONF_COUNT - 1));
    return legacy[(int)lround(ratio * A380X_FLAPS_FULL)];
}

/* ---------------------------------------------------------------- feed to the displays and the FWS */
typedef struct {
    int ready;               /* the flaps module drives X-Plane's flaps */
    int handle_index;        /* FBW FLAPS_HANDLE_INDEX 0..4 */
    int conf;                /* FBW FlapsConf 0..6 */
    int display_config;      /* PFD text 0 "0" .. 5 "FULL" */
    int relief, alpha_lock, cruise_baulk;
    int slat_target, flap_target;   /* PFD cyan target markers, -1 = none */
    double flap_fppu, slat_fppu, flap_deg, slat_deg;
    double flap_demand_deg, slat_demand_deg;
    double vfe_knots, vfe_next_knots;   /* 0 = none */
    unsigned status_word, actual_position_word;
} A380XFlapsFeed;

static inline void a380x_flaps_feed(const A380XFlaps *f, A380XFlapsFeed *out)
{
    memset(out, 0, sizeof(*out));
    out->ready = 1;
    out->handle_index = f->sfcc.handle;
    out->conf = f->sfcc.conf;
    out->display_config = a380x_flaps_display_config(&f->sfcc);
    out->relief = f->sfcc.relief;
    out->alpha_lock = f->sfcc.alpha_lock;
    out->cruise_baulk = f->sfcc.cruise_baulk;
    out->flap_fppu = f->flaps.fppu;
    out->slat_fppu = f->slats.fppu;
    out->flap_deg = a380x_flap_deg_of_fppu(f->flaps.fppu);
    out->slat_deg = a380x_slat_deg_of_fppu(f->slats.fppu);
    out->flap_demand_deg = a380x_flap_deg_of_fppu(a380x_flaps_demand_fppu[f->sfcc.conf]);
    out->slat_demand_deg = a380x_slat_deg_of_fppu(a380x_slats_demand_fppu[f->sfcc.conf]);
    out->slat_target = a380x_flaps_slat_target(&f->sfcc, f->slats.fppu);
    out->flap_target = a380x_flaps_flap_target(&f->sfcc, f->flaps.fppu);
    out->vfe_knots = a380x_flaps_vfe(f);
    out->vfe_next_knots = a380x_flaps_vfe_next(f);
    out->status_word = a380x_sfcc_status_word(&f->sfcc);
    out->actual_position_word = a380x_sfcc_actual_position_word(f->flaps.fppu, f->slats.fppu);
}

#endif
