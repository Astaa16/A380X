/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD POSITION pages: POSITION/MONITOR, POSITION/IRS, POSITION/NAVAIDS (work package H).
 * Translation of the pinned FBW pages/FMS/POSITION/MfdFmsPosition{Monitor,Irs,Navaids}.tsx
 * (docs/mfd-spec/06-position-surv-atccom-fcubkup.md sections 1-3), laid out with the FBW constants
 * under MSFS/Coherent border-box sizing (spec 06 open question 1: they fit the 812 px body).
 *
 * AIRCRAFT DATA (no XPLM here)
 *   Numbers are read through A380XMfdSystemsHost.read_value(ctx, side, key) with the A380X_POS_K_*
 *   keys below (NAN = invalid / unknown), commands go through A380XMfdSystemsHost.command().
 *   Strings and calls that need an answer (navaid tuning by ident, fix lookup, ADIRS heading entry)
 *   go through the A380XMfdPositionHost table registered with a380x_mfd_page_position_set_host().
 *   Every pointer may be NULL (the pages then show dashes / reject entries silently).
 */
#ifndef A380X_MFD_PAGE_POSITION_H
#define A380X_MFD_PAGE_POSITION_H

#include "a380x_mfd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared id helper of the WP-H pages: systems-host keys / commands are (page id << 16) | n. */
#ifndef A380X_MFD_SYS_ID
#define A380X_MFD_SYS_ID(scope, n) ((unsigned int)((((unsigned int)(scope)) << 16) | (((unsigned int)(n)) & 0xffffu)))
#endif

/* ---- read_value keys (double; NAN = invalid). Source per spec 06 section 12. ------------------ */
#define A380X_POS_KEY(n) A380X_MFD_SYS_ID(MFD_PAGE_ID_POSITION_MONITOR, (n))
enum {
    A380X_POS_K_FM_VALID = A380X_POS_KEY(1),  /* FM position valid (spec 06 1.4): IR nav valid or GPS valid */
    A380X_POS_K_FM_LAT = A380X_POS_KEY(2),    /* FM position = sim/flightmodel/position/latitude (deg); also GNSS1/2 */
    A380X_POS_K_FM_LON = A380X_POS_KEY(3),    /* sim/flightmodel/position/longitude (deg) */
    A380X_POS_K_GPS_PRIMARY = A380X_POS_KEY(4), /* FMS core gps_primary (GPS1 or GPS2 valid) */
    A380X_POS_K_EPU_NM = A380X_POS_KEY(5),    /* FMS EPU model (spec 06 1.4); INFINITY/NAN = no FM position */
    A380X_POS_K_RNP_NM = A380X_POS_KEY(6),    /* active RNP (pilot or default), NM */
    A380X_POS_K_RNP_PILOT = A380X_POS_KEY(7), /* 1 = pilot-entered RNP (27 px), 0 = computed (22 px) */
    A380X_POS_K_GPS1_VALID = A380X_POS_KEY(8), /* gps_power == 1 && rel_gps != 6 && avionics powered */
    A380X_POS_K_GPS2_VALID = A380X_POS_KEY(9), /* gps2_power == 1 && rel_gps2 != 6 && avionics powered */
    A380X_POS_K_TRUE_TRACK = A380X_POS_KEY(10), /* sim/flightmodel/position/hpath (deg true) */
    A380X_POS_K_TRUE_HDG = A380X_POS_KEY(11), /* sim/flightmodel/position/true_psi */
    A380X_POS_K_MAGVAR = A380X_POS_KEY(12),   /* sim/flightmodel/position/magnetic_variation (true = mag + v) */
    A380X_POS_K_WIND_DIR = A380X_POS_KEY(13), /* sim/weather/aircraft/wind_now_direction_degt */
    A380X_POS_K_WIND_KT = A380X_POS_KEY(14),  /* sim/weather/aircraft/wind_now_speed_msc * 1.943844 */
    A380X_POS_K_ON_GROUND = A380X_POS_KEY(15), /* sim/flightmodel/failures/onground_any */
    A380X_POS_K_UTC_S = A380X_POS_KEY(16),    /* sim/time/zulu_time_sec */
    /* per IR unit u = 0..2: A380X_POS_K_IR(u, field), from a380x/xp12/adirs/ir{u+1}_<field> */
    A380X_POS_K_IR_BASE = A380X_POS_KEY(0x100)
};
enum {
    A380X_POS_IR_MODE = 0,        /* ir{n}_mode: 0 OFF, 1 NAV, 2 ATT */
    A380X_POS_IR_ALIGNING,        /* ir{n}_aligning */
    A380X_POS_IR_ALIGN_REMAIN_S,  /* ir{n}_alignment_remaining (s) */
    A380X_POS_IR_NAV_VALID,       /* ir{n}_nav_valid */
    A380X_POS_IR_HDG_VALID,       /* ir{n}_heading_valid */
    A380X_POS_IR_FAULT,           /* ir{n}_fault */
    A380X_POS_IR_HDG_REQUIRED,    /* ir{n}_heading_required */
    A380X_POS_IR_NAV_LOCKED,      /* ir{n}_nav_locked */
    A380X_POS_IR_LAT,             /* ir{n}_latitude_deg */
    A380X_POS_IR_LON,             /* ir{n}_longitude_deg */
    A380X_POS_IR_GS_KT,           /* ir{n}_ground_speed_knots */
    A380X_POS_IR_HDG_MAG,         /* ir{n}_heading_deg (magnetic) */
    A380X_POS_IR_FIELD_COUNT
};
#define A380X_POS_K_IR(u, field) (A380X_POS_K_IR_BASE + 16u * (unsigned int)(u) + (unsigned int)(field))

/* ---- commands: host->command(ctx, side, cmd, value) ------------------------------------------ */
enum {
    A380X_POS_C_SET_RNP = A380X_POS_KEY(0x200) /* value = RNP NM (0.01..20), NAN = clear (back to default) */
};

/* ---- navaid data (POSITION/NAVAIDS, spec 06 section 3.1) ---------------------------------------- */
#define A380X_POS_NAVAID_IDENT 8
typedef struct {
    char ident[A380X_POS_NAVAID_IDENT]; /* "" = none */
    double freq_mhz;                    /* NAN = none */
    double course_deg;                  /* NAN = none */
    char cls[12];                       /* "VOR/DME", "DME", "VOR/TAC", "ILS/DME", "ILS", "" */
    int ident_manual;                   /* 1 = tuned by ident (27 px ident) */
    int freq_manual;                    /* 1 = tuned by frequency only (27 px frequency) */
    int course_manual;                  /* 1 = pilot course */
    double slope_deg;                   /* LS only: glide slope angle, NAN = none */
} A380XMfdNavaidSlot;

typedef struct {
    A380XMfdNavaidSlot vor[2];          /* VOR1 = NAV1, VOR2 = NAV2 (spec 06 3.1 receiver allocation) */
    A380XMfdNavaidSlot ls;              /* MMR (NAV3 + NAV4) */
    int ls_locked;                      /* MMR tuning locked (LOC/GS captured) -> NOT ALLOWED */
    struct { char ident[A380X_POS_NAVAID_IDENT]; double freq_mhz; char cls[12]; } selected[3]; /* SELECTED FOR FMS NAV */
} A380XMfdNavaidData;

/* Result codes of the tuning callbacks. */
enum {
    A380X_POS_TUNE_OK = 1,
    A380X_POS_TUNE_NOT_IN_DATABASE = 0,
    A380X_POS_TUNE_NOT_ALLOWED = -1,
    A380X_POS_TUNE_DESELECTED = -2 /* ident is in the deselected list: "XXXX IS DESELECTED" */
};
/* Receivers for the tuning callbacks. */
enum { A380X_POS_RCV_VOR1 = 0, A380X_POS_RCV_VOR2 = 1, A380X_POS_RCV_LS = 2 };

typedef struct {
    void *ctx;
    /* NAVAIDS snapshot (called every frame while POSITION/NAVAIDS or MONITOR is shown). */
    void (*read_navaids)(void *ctx, MfdSide side, A380XMfdNavaidData *out);
    /* Manual tuning. ident "" = clear the manual ident (back to auto); freq NAN / course NAN = clear.
     * VOR: nav_frequency_hz[0..1], nav_obs_deg_mag_pilot/_copilot; LS: NAV3+NAV4 (spec 06 3.1). */
    int (*tune_ident)(void *ctx, int receiver, const char *ident);
    int (*tune_freq)(void *ctx, int receiver, double freq_mhz);
    int (*set_course)(void *ctx, int receiver, double course_deg);
    /* Navaid existence for the deselection list (1 found, 0 not in database). */
    int (*navaid_exists)(void *ctx, const char *ident);
    /* Fix / waypoint / navaid / airport lookup for BRG / DIST TO (1 found; nearest of duplicates). */
    int (*fix_lookup)(void *ctx, const char *ident, double ref_lat, double ref_lon, double *lat, double *lon);
    /* ATT-mode heading entry: a380x_adirs_enter_heading(unit 0..2, magnetic deg); 1 accepted. */
    int (*adirs_enter_heading)(void *ctx, int unit, double mag_heading_deg);
} A380XMfdPositionHost;

/* Register the POSITION host table (copied). NULL removes it. */
void a380x_mfd_page_position_set_host(const A380XMfdPositionHost *host);

/* Shared FMS data owned by this module (survives page navigation, shared by both MFDs). */
int  a380x_mfd_position_monitor_fix(char ident[8], double *lat, double *lon); /* 1 when set */
void a380x_mfd_position_set_monitor_fix(const char *ident_or_null, double lat, double lon);
/* Deselected navaid list (max 6, "" = empty slot) for the navaid tuner. */
const char *a380x_mfd_position_deselected(int i);
/* FMS reset (DATA/STATUS SWAP, DONE phase): clears the monitor fix and the deselected list. */
void a380x_mfd_page_position_reset(void);

/* ---- page-local state ------------------------------------------------------------------------- */
#define MFD_POS_COORD_LEN 28

typedef struct {
    int sensors_visible, sensors_by_pilot, frozen;
    int frozen_hhmm;            /* UTC minutes of day at freeze, -1 unknown */
    double next_update;         /* 2 Hz refresh */
    int have_data;
    char gnss[2][MFD_POS_COORD_LEN], ir[3][MFD_POS_COORD_LEN], mixirs[MFD_POS_COORD_LEN],
        radio[MFD_POS_COORD_LEN], pos[MFD_POS_COORD_LEN];
    char dev[3][8];
    char epu[12];
    int epu_valid, nav_lost, accuracy_high;
    char brg[8], dist[16];
    int brg_valid;
} MfdPosMonitorState;
MFD_PAGE_STATE_CHECK(MfdPosMonitorState);

typedef struct {
    int shown;                  /* 0 = NONE, 1..3 = IRS1..3 (initial 1) */
    double next_update;         /* 1 Hz refresh */
    int aligned;
    char banner_pos[MFD_POS_COORD_LEN];
    char status[3][12], col3[3][24], col4[3][16];
} MfdPosIrsState;
MFD_PAGE_STATE_CHECK(MfdPosIrsState);

typedef struct {
    int tab;                    /* 0 TUNED FOR DISPLAY, 1 SELECTED FOR FMS NAV */
    int show_return;
    A380XMfdNavaidData nav;
} MfdPosNavaidsState;
MFD_PAGE_STATE_CHECK(MfdPosNavaidsState);

/* Registers fms/position/monitor, fms/position/irs, fms/position/navaids. */
void a380x_mfd_page_position_register(void);

#ifdef __cplusplus
}
#endif

#endif
