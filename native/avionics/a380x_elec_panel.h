/* SPDX-License-Identifier: GPL-3.0-or-later
 * A380 ELEC and EMER ELEC PWR overhead pushbuttons: XPLM-free state machine.
 *
 * Owns only what the crew can set on the panel: latched pb-sw positions, the momentary
 * EXT PWR / DRIVE / RAT MAN ON pushbuttons, the guard covers, the latched IDG (DRIVE)
 * disconnection, press travel and the lamp logic. It does not model the electrical
 * network: the inputs (lamp power, faults, external-power availability, ground/engine
 * state for the maintenance reset) are supplied by the adapter. Phase 1
 * (a380x_elec_panel.inc) fills them from X-Plane; the future A380 network replaces the
 * adapter coupling and keeps this panel (see docs/ELEC_PANEL.md, "Phase 2 hooks").
 *
 * The control order matches tools/elec_panel_contract.py CONTROLS.
 */
#ifndef A380X_ELEC_PANEL_H
#define A380X_ELEC_PANEL_H
#include <math.h>
#include <string.h>
#include "a380x_control_motion.h"

enum {
    A380X_ELEC_BAT1, A380X_ELEC_BAT_ESS, A380X_ELEC_BAT2, A380X_ELEC_BAT_APU, A380X_ELEC_ELMU,
    A380X_ELEC_AC_ESS_FEED, A380X_ELEC_PAX_SYS, A380X_ELEC_GALLEY, A380X_ELEC_COMM1,
    A380X_ELEC_COMM2, A380X_ELEC_BUS_TIE,
    A380X_ELEC_EXT1, A380X_ELEC_EXT2, A380X_ELEC_EXT3, A380X_ELEC_EXT4,
    A380X_ELEC_APU_GEN_A, A380X_ELEC_APU_GEN_B,
    A380X_ELEC_GEN1, A380X_ELEC_GEN2, A380X_ELEC_GEN3, A380X_ELEC_GEN4,
    A380X_ELEC_DRIVE1, A380X_ELEC_DRIVE2, A380X_ELEC_DRIVE3, A380X_ELEC_DRIVE4,
    A380X_ELEC_RAT_MAN_ON, A380X_ELEC_RAT_FAULT,
    A380X_ELEC_CONTROL_COUNT
};
enum {
    A380X_ELEC_LATCH, A380X_ELEC_EXT, A380X_ELEC_GUARDED_LATCH,
    A380X_ELEC_GUARDED_MOMENTARY, A380X_ELEC_INDICATOR
};
enum {
    A380X_ELEC_COVER_AC_ESS_FEED, A380X_ELEC_COVER_DRIVE1, A380X_ELEC_COVER_DRIVE2,
    A380X_ELEC_COVER_DRIVE3, A380X_ELEC_COVER_DRIVE4, A380X_ELEC_COVER_RAT_MAN_ON,
    A380X_ELEC_COVER_COUNT
};
enum {
    A380X_ELEC_LEGEND_NONE, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_OFF, A380X_ELEC_LEGEND_ALTN,
    A380X_ELEC_LEGEND_DISC, A380X_ELEC_LEGEND_AVAIL, A380X_ELEC_LEGEND_ON, A380X_ELEC_LEGEND_COUNT
};
/* Event bits returned by the press/release calls and collected for cover motion. */
#define A380X_ELEC_EVENT_PRESS 1u
#define A380X_ELEC_EVENT_RELEASE 2u
#define A380X_ELEC_COVER_ACTIVE 0.6f    /* guarded pb reachable (and its click target shown) above this */
#define A380X_ELEC_COVER_SECONDS 0.22f  /* full guard swing when animated by a click/command */
#define A380X_ELEC_COVER_UNLATCH 0.05f  /* leaving the closed detent: open snap sound */
#define A380X_ELEC_COVER_SEATED 0.02f   /* reaching the closed detent: close snap sound */
#define A380X_ELEC_DIM_LEVEL 0.3f

typedef struct {
    const char *id;
    unsigned char kind, seq1, seq2, has_fault;
    signed char cover;
} A380XElecControlInfo;

static const A380XElecControlInfo a380x_elec_controls[A380X_ELEC_CONTROL_COUNT] = {
    {"bat1", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_OFF, 1, -1},
    {"bat_ess", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_OFF, 1, -1},
    {"bat2", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_OFF, 1, -1},
    {"bat_apu", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_OFF, 1, -1},
    /* ELMU has a FAULT legend on the aircraft but no authored face in the FBW model. */
    {"elmu", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_NONE, A380X_ELEC_LEGEND_OFF, 1, -1},
    {"ac_ess_feed", A380X_ELEC_GUARDED_LATCH, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_ALTN, 1,
     A380X_ELEC_COVER_AC_ESS_FEED},
    {"pax_sys", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_NONE, A380X_ELEC_LEGEND_OFF, 0, -1},
    {"galley", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_NONE, A380X_ELEC_LEGEND_OFF, 0, -1},
    {"comm1", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_NONE, A380X_ELEC_LEGEND_OFF, 0, -1},
    {"comm2", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_NONE, A380X_ELEC_LEGEND_OFF, 0, -1},
    {"bus_tie", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_NONE, A380X_ELEC_LEGEND_OFF, 0, -1},
    {"ext1", A380X_ELEC_EXT, A380X_ELEC_LEGEND_AVAIL, A380X_ELEC_LEGEND_ON, 0, -1},
    {"ext2", A380X_ELEC_EXT, A380X_ELEC_LEGEND_AVAIL, A380X_ELEC_LEGEND_ON, 0, -1},
    {"ext3", A380X_ELEC_EXT, A380X_ELEC_LEGEND_AVAIL, A380X_ELEC_LEGEND_ON, 0, -1},
    {"ext4", A380X_ELEC_EXT, A380X_ELEC_LEGEND_AVAIL, A380X_ELEC_LEGEND_ON, 0, -1},
    {"apu_gen_a", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_OFF, 1, -1},
    {"apu_gen_b", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_OFF, 1, -1},
    {"gen1", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_OFF, 1, -1},
    {"gen2", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_OFF, 1, -1},
    {"gen3", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_OFF, 1, -1},
    {"gen4", A380X_ELEC_LATCH, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_OFF, 1, -1},
    {"drive1", A380X_ELEC_GUARDED_MOMENTARY, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_DISC, 1,
     A380X_ELEC_COVER_DRIVE1},
    {"drive2", A380X_ELEC_GUARDED_MOMENTARY, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_DISC, 1,
     A380X_ELEC_COVER_DRIVE2},
    {"drive3", A380X_ELEC_GUARDED_MOMENTARY, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_DISC, 1,
     A380X_ELEC_COVER_DRIVE3},
    {"drive4", A380X_ELEC_GUARDED_MOMENTARY, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_DISC, 1,
     A380X_ELEC_COVER_DRIVE4},
    {"rat_man_on", A380X_ELEC_GUARDED_MOMENTARY, A380X_ELEC_LEGEND_NONE, A380X_ELEC_LEGEND_NONE, 0,
     A380X_ELEC_COVER_RAT_MAN_ON},
    {"rat_fault", A380X_ELEC_INDICATOR, A380X_ELEC_LEGEND_FAULT, A380X_ELEC_LEGEND_NONE, 1, -1},
};
static const char *const a380x_elec_cover_ids[A380X_ELEC_COVER_COUNT] = {
    "ac_ess_feed", "drive1", "drive2", "drive3", "drive4", "rat_man_on"
};
static const char *const a380x_elec_legend_ids[A380X_ELEC_LEGEND_COUNT] = {
    "", "fault", "off", "altn", "disc", "avail", "on"
};

typedef struct {
    unsigned char normal[A380X_ELEC_CONTROL_COUNT]; /* latched pb-sw pushed in (AUTO / ON / AUTO feed) */
    unsigned char held[A380X_ELEC_CONTROL_COUNT];
    unsigned char ext_on[4];       /* EXT n ON (blue): this receptacle supplies the network */
    unsigned char drive_disc[4];   /* IDG n disconnected; latched until the ground maintenance reset */
    unsigned char rat_requested;   /* MAN ON pressed: RAT deployment request for the network model */
    A380XPress press[A380X_ELEC_CONTROL_COUNT];
    float cover[A380X_ELEC_COVER_COUNT];        /* 0 closed .. 1 fully open */
    float cover_target[A380X_ELEC_COVER_COUNT];
    unsigned char cover_unlatched[A380X_ELEC_COVER_COUNT];
    unsigned int cover_opened, cover_closed;     /* bit per cover, collected until taken */
} A380XElecPanel;

typedef struct {
    int lamp_power;          /* annunciator supply (DC) present */
    int annunciator_test;    /* ANN LT TEST: every authored legend lit */
    int annunciator_dim;     /* ANN LT DIM */
    int on_ground;
    unsigned char fault[A380X_ELEC_CONTROL_COUNT];
    unsigned char ext_available[4];
    unsigned char engine_running[4];
    /* Phase 2: each legend has its own supply bus (spec 4.2). When per_lamp_power is set,
     * lamp_powered[c][0] powers the SEQ1 (upper) legend and [c][1] the SEQ2 (lower) one
     * (ELMU's FAULT, which has no authored face, uses [0]); lamp_power is then ignored. */
    int per_lamp_power;
    unsigned char lamp_powered[A380X_ELEC_CONTROL_COUNT][2];
} A380XElecInputs;

static inline int a380x_elec_is_latch(int c)
{
    return c >= 0 && c < A380X_ELEC_CONTROL_COUNT && (a380x_elec_controls[c].kind == A380X_ELEC_LATCH ||
        a380x_elec_controls[c].kind == A380X_ELEC_GUARDED_LATCH);
}
static inline float a380x_elec_clamp01(float value)
{
    return isfinite(value) ? fminf(1.0f, fmaxf(0.0f, value)) : 0.0f;
}

/* Cold & dark panel: every latched pb-sw pushed in (AUTO), EXT not ON, IDGs connected,
 * guards closed, RAT not requested. The adapter then adopts the simulator's stock states. */
static inline void a380x_elec_init(A380XElecPanel *p)
{
    int c;
    memset(p, 0, sizeof(*p));
    for (c = 0; c < A380X_ELEC_CONTROL_COUNT; ++c) p->normal[c] = (unsigned char)a380x_elec_is_latch(c);
}

static inline int a380x_elec_guard_open(const A380XElecPanel *p, int c)
{
    int cover = a380x_elec_controls[c].cover;
    return cover < 0 || p->cover[cover] >= A380X_ELEC_COVER_ACTIVE;
}

/* Mouse/command press (CommandBegin). Returns event bits (0: rejected, nothing moves). */
static inline unsigned int a380x_elec_press(A380XElecPanel *p, const A380XElecInputs *in, int c, float now)
{
    const A380XElecControlInfo *info;
    if (c < 0 || c >= A380X_ELEC_CONTROL_COUNT) return 0;
    info = &a380x_elec_controls[c];
    if (info->kind == A380X_ELEC_INDICATOR || p->held[c] || !a380x_elec_guard_open(p, c)) return 0;
    p->held[c] = 1;
    a380x_press_begin(&p->press[c], now, 1.0f);
    switch (info->kind) {
    case A380X_ELEC_LATCH: case A380X_ELEC_GUARDED_LATCH:
        p->normal[c] = (unsigned char)!p->normal[c];
        break;
    case A380X_ELEC_EXT: {
        int n = c - A380X_ELEC_EXT1;
        if (in && in->ext_available[n]) p->ext_on[n] = (unsigned char)!p->ext_on[n];
        break;
    }
    default:
        if (c >= A380X_ELEC_DRIVE1 && c <= A380X_ELEC_DRIVE4) p->drive_disc[c - A380X_ELEC_DRIVE1] = 1;
        else if (c == A380X_ELEC_RAT_MAN_ON) p->rat_requested = 1;
        break;
    }
    return A380X_ELEC_EVENT_PRESS;
}

/* Mouse/command release (CommandEnd). The cap returns; latched states stay. */
static inline unsigned int a380x_elec_release(A380XElecPanel *p, int c)
{
    if (c < 0 || c >= A380X_ELEC_CONTROL_COUNT || !p->held[c]) return 0;
    p->held[c] = 0;
    a380x_press_end(&p->press[c]);
    return A380X_ELEC_EVENT_RELEASE;
}

/* Explicit AUTO/ON or OFF/ALTN selection (hardware, presets). A change shows one press. */
static inline unsigned int a380x_elec_set(A380XElecPanel *p, int c, int normal, float now)
{
    if (!a380x_elec_is_latch(c) || !a380x_elec_guard_open(p, c) || p->normal[c] == (normal != 0)) return 0;
    p->normal[c] = (unsigned char)(normal != 0);
    a380x_press_begin(&p->press[c], now, 1.0f);
    if (!p->held[c]) a380x_press_end(&p->press[c]);
    return A380X_ELEC_EVENT_PRESS | A380X_ELEC_EVENT_RELEASE;
}

/* IDG reconnection is a ground maintenance action with the engine stopped. */
static inline int a380x_elec_drive_reset(A380XElecPanel *p, const A380XElecInputs *in, int n)
{
    if (n < 0 || n > 3 || !in || !in->on_ground || in->engine_running[n] || !p->drive_disc[n]) return 0;
    p->drive_disc[n] = 0;
    return 1;
}

static inline void a380x_elec_cover_target(A380XElecPanel *p, int cover, float target)
{
    if (cover >= 0 && cover < A380X_ELEC_COVER_COUNT) p->cover_target[cover] = a380x_elec_clamp01(target);
}
/* Click: a closed (or closing) guard swings fully open, an open one closes. */
static inline void a380x_elec_cover_toggle(A380XElecPanel *p, int cover)
{
    if (cover >= 0 && cover < A380X_ELEC_COVER_COUNT)
        a380x_elec_cover_target(p, cover, p->cover_target[cover] >= 0.5f ? 0.0f : 1.0f);
}
/* Direct position (mouse drag, writable dataref): the guard follows the hand. */
static inline void a380x_elec_cover_set(A380XElecPanel *p, int cover, float value)
{
    if (cover < 0 || cover >= A380X_ELEC_COVER_COUNT) return;
    p->cover[cover] = p->cover_target[cover] = a380x_elec_clamp01(value);
}
/* Drag released: past half travel the guard flips over centre and stays open. */
static inline void a380x_elec_cover_settle(A380XElecPanel *p, int cover)
{
    if (cover >= 0 && cover < A380X_ELEC_COVER_COUNT)
        a380x_elec_cover_target(p, cover, p->cover[cover] >= 0.5f ? 1.0f : 0.0f);
}

static inline void a380x_elec_update(A380XElecPanel *p, const A380XElecInputs *in, float now, float dt)
{
    int c, n;
    if (!isfinite(dt) || dt < 0.0f) dt = 0.0f;
    for (n = 0; n < A380X_ELEC_COVER_COUNT; ++n) {
        float step = fminf(dt, 0.1f) / A380X_ELEC_COVER_SECONDS, target = p->cover_target[n];
        p->cover[n] = p->cover[n] < target ? fminf(target, p->cover[n] + step) : fmaxf(target, p->cover[n] - step);
        if (!p->cover_unlatched[n] && p->cover[n] > A380X_ELEC_COVER_UNLATCH) {
            p->cover_unlatched[n] = 1; p->cover_opened |= 1u << n;
        } else if (p->cover_unlatched[n] && p->cover[n] <= A380X_ELEC_COVER_SEATED) {
            p->cover_unlatched[n] = 0; p->cover_closed |= 1u << n;
        }
    }
    for (c = 0; c < A380X_ELEC_CONTROL_COUNT; ++c) {
        /* A guard closed over a held button pushes the finger off it. */
        if (p->held[c] && !a380x_elec_guard_open(p, c)) a380x_elec_release(p, c);
        a380x_press_update(&p->press[c], now, dt);
    }
    for (n = 0; n < 4; ++n) if (!in || !in->ext_available[n]) p->ext_on[n] = 0;
}

static inline int a380x_elec_has_legend(int c, int legend)
{
    if (c < 0 || c >= A380X_ELEC_CONTROL_COUNT || legend <= A380X_ELEC_LEGEND_NONE) return 0;
    if (legend == A380X_ELEC_LEGEND_FAULT && a380x_elec_controls[c].has_fault) return 1;
    return a380x_elec_controls[c].seq1 == legend || a380x_elec_controls[c].seq2 == legend;
}

/* Lamp brightness 0..1 (0 = dark). */
static inline float a380x_elec_lamp(const A380XElecPanel *p, const A380XElecInputs *in, int c, int legend)
{
    int lit = 0, powered;
    float level;
    if (!in || !a380x_elec_has_legend(c, legend)) return 0.0f;
    powered = in->per_lamp_power ? in->lamp_powered[c][a380x_elec_controls[c].seq2 == legend && legend != A380X_ELEC_LEGEND_FAULT ? 1 : 0]
                                 : in->lamp_power;
    if (!powered) return 0.0f;
    level = in->annunciator_dim ? A380X_ELEC_DIM_LEVEL : 1.0f;
    if (in->annunciator_test) return level;
    switch (legend) {
    case A380X_ELEC_LEGEND_FAULT: lit = in->fault[c] != 0; break;
    case A380X_ELEC_LEGEND_OFF: case A380X_ELEC_LEGEND_ALTN: lit = a380x_elec_is_latch(c) && !p->normal[c]; break;
    case A380X_ELEC_LEGEND_DISC: lit = p->drive_disc[c - A380X_ELEC_DRIVE1]; break;
    case A380X_ELEC_LEGEND_AVAIL:
        lit = in->ext_available[c - A380X_ELEC_EXT1] && !p->ext_on[c - A380X_ELEC_EXT1]; break;
    case A380X_ELEC_LEGEND_ON:
        lit = in->ext_available[c - A380X_ELEC_EXT1] && p->ext_on[c - A380X_ELEC_EXT1]; break;
    default: break;
    }
    return lit ? level : 0.0f;
}
#endif
