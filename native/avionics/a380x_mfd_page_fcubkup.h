/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD FCU BKUP pages: AFS CP and EFIS CP (work package H). FBW has no such pages (404,
 * [FBW-NA]); this is the [PORT] design of docs/mfd-spec/06-position-surv-atccom-fcubkup.md
 * sections 10-11, built from FBW MFD widgets with border-box sizes.
 *
 * The MFD page and the 3D FCU drive the same controller: every control maps to an EXISTING port
 * command a380x/xp12/fcu/<name> or a380x/xp12/efis/<captain|first_officer>/<name>, issued through
 * A380XMfdSystemsHost.command(ctx, side, cmd, action) with action = A380X_MFD_ACT_*:
 *   ONCE  -> XPLMCommandOnce
 *   BEGIN -> XPLMCommandBegin (press of a -/+ arrow; the handler's own repeat timer runs)
 *   END   -> XPLMCommandEnd   (release)
 * Typed targets use A380X_FCUB_C_SET_* (value = target): the adapter applies fcu_apply(<KNOB>_ROTATE, 0)
 * (collapses dashes like a first detent) and then fcu_apply(<KNOB>_ROTATE, (target - selected) / unit)
 * (spec 06 section 13). The EFIS commands use the MFD side (CAPT -> captain, F/O -> first_officer).
 * Window state is read with read_value(ctx, side, key): A380X_FCUB_K_* (a380x/xp12/fcu/<name> or the
 * guidance state) and A380X_EFIS_K_* (a380x/xp12/efis/<side>/<name>).
 */
#ifndef A380X_MFD_PAGE_FCUBKUP_H
#define A380X_MFD_PAGE_FCUBKUP_H

#include "a380x_mfd.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef A380X_MFD_SYS_ID
#define A380X_MFD_SYS_ID(scope, n) ((unsigned int)((((unsigned int)(scope)) << 16) | (((unsigned int)(n)) & 0xffffu)))
#endif
#define A380X_FCUB_ID(n) A380X_MFD_SYS_ID(MFD_PAGE_ID_FCUBKUP_AFS, (n))
#define A380X_EFIS_ID(n) A380X_MFD_SYS_ID(MFD_PAGE_ID_FCUBKUP_EFIS, (n))

/* Command actions (value argument of host->command for the command ids below). */
#define A380X_MFD_ACT_ONCE 0.0
#define A380X_MFD_ACT_BEGIN 1.0
#define A380X_MFD_ACT_END 2.0

/* ---- AFS CP commands: a380x/xp12/fcu/<name> (a380x_fcu_adapter.inc fcu_commands[]) ------------- */
enum {
    A380X_FCUB_C_SPEED_UP = A380X_FCUB_ID(1),        /* speed_up */
    A380X_FCUB_C_SPEED_DOWN = A380X_FCUB_ID(2),      /* speed_down */
    A380X_FCUB_C_SPEED_PUSH = A380X_FCUB_ID(3),      /* speed_push */
    A380X_FCUB_C_SPEED_PULL = A380X_FCUB_ID(4),      /* speed_pull */
    A380X_FCUB_C_HEADING_UP = A380X_FCUB_ID(5),      /* heading_up */
    A380X_FCUB_C_HEADING_DOWN = A380X_FCUB_ID(6),    /* heading_down */
    A380X_FCUB_C_HEADING_PUSH = A380X_FCUB_ID(7),    /* heading_push */
    A380X_FCUB_C_HEADING_PULL = A380X_FCUB_ID(8),    /* heading_pull */
    A380X_FCUB_C_ALTITUDE_UP = A380X_FCUB_ID(9),     /* altitude_up */
    A380X_FCUB_C_ALTITUDE_DOWN = A380X_FCUB_ID(10),  /* altitude_down */
    A380X_FCUB_C_ALTITUDE_PUSH = A380X_FCUB_ID(11),  /* altitude_push */
    A380X_FCUB_C_ALTITUDE_PULL = A380X_FCUB_ID(12),  /* altitude_pull */
    A380X_FCUB_C_VS_UP = A380X_FCUB_ID(13),          /* vertical_speed_up */
    A380X_FCUB_C_VS_DOWN = A380X_FCUB_ID(14),        /* vertical_speed_down */
    A380X_FCUB_C_VS_PUSH = A380X_FCUB_ID(15),        /* vertical_speed_push (drawn disabled: A380 no effect) */
    A380X_FCUB_C_VS_PULL = A380X_FCUB_ID(16),        /* vertical_speed_pull */
    A380X_FCUB_C_ALT_STEP_TOGGLE = A380X_FCUB_ID(17),/* altitude_step_toggle */
    A380X_FCUB_C_AP1 = A380X_FCUB_ID(18),            /* ap1_toggle */
    A380X_FCUB_C_AP2 = A380X_FCUB_ID(19),            /* ap2_toggle */
    A380X_FCUB_C_FD = A380X_FCUB_ID(20),             /* fd_toggle */
    A380X_FCUB_C_ATHR = A380X_FCUB_ID(21),           /* athr_toggle */
    A380X_FCUB_C_SPD_MACH = A380X_FCUB_ID(22),       /* speed_mach_toggle */
    A380X_FCUB_C_TRK_FPA = A380X_FCUB_ID(23),        /* trk_fpa_toggle */
    A380X_FCUB_C_ALT_HOLD = A380X_FCUB_ID(24),       /* altitude_hold (ALT pb) */
    A380X_FCUB_C_LOC = A380X_FCUB_ID(25),            /* loc_toggle */
    A380X_FCUB_C_APPR = A380X_FCUB_ID(26),           /* approach_toggle */
    A380X_FCUB_C_TRUE_MAG = A380X_FCUB_ID(27),       /* true_mag_toggle */
    A380X_FCUB_C_METRIC = A380X_FCUB_ID(28),         /* metric_toggle */
    /* typed targets (value = target in the window unit) */
    A380X_FCUB_C_SET_SPEED = A380X_FCUB_ID(40),      /* kt     -> A380X_FCU_SPD_ROTATE (1 kt detents) */
    A380X_FCUB_C_SET_MACH = A380X_FCUB_ID(41),       /* Mach   -> A380X_FCU_SPD_ROTATE (0.01 detents) */
    A380X_FCUB_C_SET_HEADING = A380X_FCUB_ID(42),    /* deg    -> A380X_FCU_HDG_ROTATE (wrap +-180) */
    A380X_FCUB_C_SET_ALTITUDE = A380X_FCUB_ID(43),   /* ft     -> A380X_FCU_ALT_ROTATE (/ altitude_step_ft) */
    A380X_FCUB_C_SET_VS = A380X_FCUB_ID(44),         /* ft/min -> A380X_FCU_VS_ROTATE (/ 50) */
    A380X_FCUB_C_SET_FPA = A380X_FCUB_ID(45)         /* deg    -> A380X_FCU_VS_ROTATE (/ 0.1) */
};

/* ---- AFS CP state keys (a380x/xp12/fcu/<name>, guidance state) ------------------------------- */
enum {
    A380X_FCUB_K_POWERED = A380X_FCUB_ID(64),        /* guidance_inputs.powered */
    A380X_FCUB_K_SELECTED_SPEED = A380X_FCUB_ID(65), /* selected_speed (kt) */
    A380X_FCUB_K_SELECTED_MACH = A380X_FCUB_ID(66),  /* selected_mach */
    A380X_FCUB_K_SELECTED_HEADING = A380X_FCUB_ID(67), /* selected_heading (deg) */
    A380X_FCUB_K_SELECTED_ALTITUDE = A380X_FCUB_ID(68),/* selected_altitude (ft) */
    A380X_FCUB_K_SELECTED_VS = A380X_FCUB_ID(69),    /* selected_vertical_speed (ft/min) */
    A380X_FCUB_K_SELECTED_FPA = A380X_FCUB_ID(70),   /* selected_fpa (deg) */
    A380X_FCUB_K_SPEED_MANAGED = A380X_FCUB_ID(71),  /* speed_managed */
    A380X_FCUB_K_HEADING_MANAGED = A380X_FCUB_ID(72),/* heading_managed */
    A380X_FCUB_K_ALTITUDE_MANAGED = A380X_FCUB_ID(73),/* altitude_managed */
    A380X_FCUB_K_MACH_MODE = A380X_FCUB_ID(74),      /* mach_mode */
    A380X_FCUB_K_TRK_FPA = A380X_FCUB_ID(75),        /* trk_fpa */
    A380X_FCUB_K_TRUE_HEADING = A380X_FCUB_ID(76),   /* true_heading */
    A380X_FCUB_K_METRIC_ALT = A380X_FCUB_ID(77),     /* metric_altitude */
    A380X_FCUB_K_AP1 = A380X_FCUB_ID(78),            /* ap1_active */
    A380X_FCUB_K_AP2 = A380X_FCUB_ID(79),            /* ap2_active */
    A380X_FCUB_K_FD = A380X_FCUB_ID(80),             /* fd_active */
    A380X_FCUB_K_ATHR = A380X_FCUB_ID(81),           /* athr_armed || athr_active */
    A380X_FCUB_K_LOC = A380X_FCUB_ID(82),            /* loc_active */
    A380X_FCUB_K_APPR = A380X_FCUB_ID(83),           /* approach_active */
    A380X_FCUB_K_ALT_STEP_FT = A380X_FCUB_ID(84),    /* altitude_step_ft (100 / 1000) */
    A380X_FCUB_K_ALT_HOLD = A380X_FCUB_ID(85),       /* altitude_hold_active */
    A380X_FCUB_K_SPEED_DASHES = A380X_FCUB_ID(86),   /* guidance.speed_dashes */
    A380X_FCUB_K_HEADING_DASHES = A380X_FCUB_ID(87), /* guidance.heading_dashes */
    A380X_FCUB_K_VS_DASHES = A380X_FCUB_ID(88)       /* guidance.vs_dashes */
};

/* ---- EFIS CP: commands a380x/xp12/efis/<side>/<name>, keys a380x/xp12/efis/<side>/<name> -------- */
enum {
    A380X_EFIS_C_CSTR = A380X_EFIS_ID(1), A380X_EFIS_C_WPT, A380X_EFIS_C_VORD, A380X_EFIS_C_NDB, A380X_EFIS_C_ARPT,
    A380X_EFIS_C_VOR1, A380X_EFIS_C_VOR2, A380X_EFIS_C_TERR, A380X_EFIS_C_TRAF, A380X_EFIS_C_WX,
    A380X_EFIS_C_TAXI, A380X_EFIS_C_LS, A380X_EFIS_C_VV, A380X_EFIS_C_MODE_UP, A380X_EFIS_C_MODE_DOWN,
    A380X_EFIS_C_RANGE_UP, A380X_EFIS_C_RANGE_DOWN, A380X_EFIS_C_BARO_UP, A380X_EFIS_C_BARO_DOWN,
    A380X_EFIS_C_BARO_STD, A380X_EFIS_C_BARO_QNH, A380X_EFIS_C_BARO_UNIT,
    /* = cstr_toggle, wpt_toggle, vord_toggle, ndb_toggle, arpt_toggle, vor1_toggle, vor2_toggle,
     *   terr_toggle, traf_toggle, wx_toggle, taxi_toggle, ls_toggle, vv_toggle, mode_up, mode_down,
     *   range_up, range_down, baro_up, baro_down, baro_std, baro_qnh, baro_unit_toggle
     *   (same order as efis_action_names[] in a380x_efis.inc) */
    A380X_EFIS_C_SET_BARO_HPA = A380X_EFIS_ID(40)    /* value hPa: writes barometer_setting_in_hg_{pilot|copilot}
                                                        = hPa / 33.8638867 (adopted as preset by efis_update) */
};
enum {
    A380X_EFIS_K_CSTR = A380X_EFIS_ID(64), A380X_EFIS_K_WPT, A380X_EFIS_K_VORD, A380X_EFIS_K_NDB, A380X_EFIS_K_ARPT,
    A380X_EFIS_K_VOR1, A380X_EFIS_K_VOR2, A380X_EFIS_K_TERR, A380X_EFIS_K_TRAF, A380X_EFIS_K_LS, A380X_EFIS_K_VV,
    A380X_EFIS_K_WX_REQUESTED, A380X_EFIS_K_TAXI_REQUESTED, A380X_EFIS_K_MODE, A380X_EFIS_K_RANGE_INDEX,
    A380X_EFIS_K_ZOOM, A380X_EFIS_K_BARO_STD, A380X_EFIS_K_BARO_INHG, A380X_EFIS_K_POWERED, A380X_EFIS_K_RANGE_NM,
    A380X_EFIS_K_QNH_HPA, A380X_EFIS_K_BARO_PRESET_HPA
    /* = EfisSide fields cstr, wpt, vord, ndb, arpt, vor1, vor2, terr, traf, ls, vv, wx_requested,
     *   taxi_requested, mode, range_index, zoom, baro_std, baro_inhg, powered, range_nm, qnh_hpa,
     *   baro_preset_hpa (a380x_efis.h, published as a380x/xp12/efis/<side>/<field>) */
};

/* Release any -/+ arrow still held when its page is no longer shown (sends ACT_END). The pages do this
 * themselves while displayed; the integrator calls this once per frame after a380x_mfd_frame() so a
 * navigation away from FCU BKUP during a press never leaves a command begun. */
void a380x_mfd_page_fcubkup_release_held(A380XMfd *m);

typedef struct {
    int unused;
} MfdFcubState;
MFD_PAGE_STATE_CHECK(MfdFcubState);

/* Registers fcubkup/afs and fcubkup/efis (replacing the frame stubs). */
void a380x_mfd_page_fcubkup_register(void);

#ifdef __cplusplus
}
#endif

#endif
