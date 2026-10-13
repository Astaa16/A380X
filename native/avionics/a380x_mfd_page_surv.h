/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD SURV pages: SURV/CONTROLS and SURV/STATUS & SWITCHING (work package H).
 * Translation of the pinned FBW pages/SURV/MfdSurvControls.tsx and MfdSurvStatusSwitching.tsx
 * (docs/mfd-spec/06-position-surv-atccom-fcubkup.md sections 4-5) with border-box geometry.
 *
 * AIRCRAFT DATA (no XPLM here): everything goes through A380XMfdSystemsHost.
 *   read_value(ctx, side, A380X_SURV_*)      -> current value (NAN = unknown)
 *   command(ctx, side, A380X_SURV_*, value)  -> for the SURV STATE ids: set the state to `value`
 *   command(ctx, side, A380X_SURV_C_*, value)-> transponder actions
 * The SURV states are one aircraft system shared by both MFDs; the adapter owns them, publishes
 * them as a380x/xp12/surv/<name> and runs the transponder mode law of spec 06 section 4.2
 * (sim/cockpit2/radios/actuators/transponder_mode) every frame.
 */
#ifndef A380X_MFD_PAGE_SURV_H
#define A380X_MFD_PAGE_SURV_H

#include "a380x_mfd.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef A380X_MFD_SYS_ID
#define A380X_MFD_SYS_ID(scope, n) ((unsigned int)((((unsigned int)(scope)) << 16) | (((unsigned int)(n)) & 0xffffu)))
#endif
#define A380X_SURV_ID(n) A380X_MFD_SYS_ID(MFD_PAGE_ID_SURV_CONTROLS, (n))

enum {
    /* ---- SURV states: read_value key AND command id (value = new state). a380x/xp12/surv/<name> ---- */
    A380X_SURV_XPDR_AUTO = A380X_SURV_ID(1),        /* xpdr_auto: 1 AUTO, 0 STBY (default 1, 0 cold & dark) */
    A380X_SURV_ALT_RPTG = A380X_SURV_ID(2),         /* alt_rptg_request: 1 ON (default) */
    A380X_SURV_TCAS_ALERT_LEVEL = A380X_SURV_ID(3), /* tcas_alert_level: 2 TA/RA, 1 TA ONLY, 0 STBY */
    A380X_SURV_TCAS_ALT_SELECT = A380X_SURV_ID(4),  /* tcas_alt_select: 0 NORM, 1 ABV, 2 BLW
                                                      -> sim/cockpit2/radios/actuators/tcas_filter 0/2/3 */
    A380X_SURV_TERR_SYS_OFF = A380X_SURV_ID(5),     /* terr_sys_off (ND terrain layer gated, spec 06 4.2) */
    A380X_SURV_GPWS_SYS_OFF = A380X_SURV_ID(6),     /* gpws_sys_off */
    A380X_SURV_GS_MODE_OFF = A380X_SURV_ID(7),      /* gs_mode_off */
    A380X_SURV_FLAP_MODE_OFF = A380X_SURV_ID(8),    /* flap_mode_off */
    A380X_SURV_WXR_TAWS_SYS = A380X_SURV_ID(9),     /* wxr_taws_sys: 0 none, 1, 2 (default 1) */
    A380X_SURV_XPDR_TCAS_SYS = A380X_SURV_ID(10),   /* xpdr_tcas_sys: 1, 2 (default 1) [PORT] */

    /* ---- read-only keys ---- */
    A380X_SURV_K_XPDR_CODE = A380X_SURV_ID(32),     /* sim/cockpit2/radios/actuators/transponder_code (0..7777) */
    A380X_SURV_K_XPDR_MODE = A380X_SURV_ID(33),     /* sim/cockpit2/radios/actuators/transponder_mode 0..7 */
    A380X_SURV_K_XPDR_FAILED = A380X_SURV_ID(34),   /* sim/operation/failures/rel_xpndr == 6 */
    A380X_SURV_K_TCAS_FAULT = A380X_SURV_ID(35),    /* !avionics powered || rel_xpndr == 6 */
    A380X_SURV_K_ON_GROUND = A380X_SURV_ID(36),     /* sim/flightmodel/failures/onground_any */
    A380X_SURV_K_TERR1_FAILED = A380X_SURV_ID(37),  /* no TAWS failure model: 0 */
    A380X_SURV_K_TERR2_FAILED = A380X_SURV_ID(38),
    A380X_SURV_K_GPWS1_FAILED = A380X_SURV_ID(39),
    A380X_SURV_K_GPWS2_FAILED = A380X_SURV_ID(40),

    /* ---- actions ---- */
    A380X_SURV_C_XPDR_CODE = A380X_SURV_ID(64),     /* value = code as decimal digits (0..7777, 0000 accepted
                                                      [PORT] Q15) -> transponder_code */
    A380X_SURV_C_XPDR_IDENT = A380X_SURV_ID(65)     /* XPLMCommandOnce(sim/transponder/transponder_ident) */
};

typedef struct {
    int unused;
} MfdSurvControlsState;
MFD_PAGE_STATE_CHECK(MfdSurvControlsState);

/* Registers surv/controls and surv/status-switching. */
void a380x_mfd_page_surv_register(void);

#ifdef __cplusplus
}
#endif

#endif
