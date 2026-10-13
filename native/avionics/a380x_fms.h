/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X FMS core shared by both MFDs (work package C).
 *
 * Simulator-independent C11 translation of the pinned FlyByWire A380X FMC logic
 * (FlightManagementComputer.ts, FmcAircraftInterface.ts, fmgc.ts, FlightPhaseManager,
 * A380FlightPlanPerformanceData) as specified in docs/mfd-spec/07-fmc-logic.md,
 * 03-fms-init-fuel-data-sec-msg.md (INIT, FUEL&LOAD, SEC INDEX) and 04-fms-perf.md (PERF).
 * No XPLM, no OpenGL, no heap allocation: an A380XFms is a plain value the integrator owns.
 *
 * ------------------------------------------------------------------------------------------
 * QUICK GUIDE FOR PAGE DEVELOPERS
 * ------------------------------------------------------------------------------------------
 *  1. Which plan does my page show?
 *       int plan = a380x_fms_loaded_plan(fms, A380X_FMS_PLAN_ACTIVE);   // ACTIVE or TMPY
 *       int plan = a380x_fms_loaded_plan(fms, A380X_FMS_SEC_PLAN(n));   // SECn, or -1 if absent
 *
 *  2. Numeric fields and computed values use ONE id space (A380XFmsField):
 *       A380XFmsValue v = a380x_fms_get(fms, plan, A380X_FMS_F_ZFW);
 *         v.has == 0      -> dashes, or amber boxes if the MANDATORY flag is set
 *         v.v             -> value in storage units (kg, ft, kt, FL, %MAC, degC, min, enum index)
 *         v.entered       -> FBW "enteredByPilot": 1 = large font (27 px), 0 = small font (22 px)
 *         v.src           -> where the value comes from (pilot/computed/default/database)
 *         v.colour        -> colour class for STATIC values (GREEN / WHITE on SEC / AMBER alert)
 *       unsigned fl = a380x_fms_field_flags(fms, plan, A380X_FMS_F_ZFW, A380X_FMS_PAGE_FUEL_LOAD);
 *         A380X_FMS_FF_DISABLED / _INACTIVE / _MANDATORY / _HIDDEN / _CLEARABLE / _TMPY / _SEC ...
 *       MfdFmt fmt; a380x_fms_field_fmt(fms, plan, A380X_FMS_F_ZFW, &fmt);
 *         -> format kind + range (+ trans altitude for ALT/FL fields, + metric flag) ready for the
 *            generic InputField widget (spec 12 §5).
 *
 *  3. Entering / clearing a value (after the UI format parser accepted the text):
 *       MfdFmsError err;
 *       if (!a380x_fms_enter(fms, plan, A380X_FMS_F_ZFW, kg, &err) && err.type != MFD_ERR_NONE)
 *           mfd_ui_error(ui, &err);                      // NOT ALLOWED, ENTRY OUT OF RANGE, ...
 *       a380x_fms_clear(fms, plan, A380X_FMS_F_TAXI, &err);   // CLR -> back to computed/default
 *     A rejected entry with err.type == MFD_ERR_NONE is a SILENT rejection (FBW handler returned
 *     false): the field just keeps its old value.  Entry functions never push the error message
 *     themselves; side-effect messages (CHECK T.O. DATA, CRZ FL ABOVE MAX FL, ...) are queued by
 *     the FMS into fms->msgs.
 *
 *  4. Text fields (FLT NBR, FROM, TO, ALTN, runway/approach idents, SEC "CREATED" line):
 *       char buf[32]; a380x_fms_get_text(fms, plan, A380X_FMS_T_FROM, buf, sizeof buf);
 *       a380x_fms_enter_text(fms, plan, A380X_FMS_T_FROM, "EGLL", &err);   // NOT IN DATABASE ...
 *
 *  5. Buttons / actions: a380x_fms_confirm_to_speeds(), a380x_fms_activate_approach(),
 *     a380x_fms_clear_engine_out(), a380x_fms_reset() (DATA/STATUS SWAP), SEC INDEX operations
 *     a380x_fms_sec_*(), TMPY a380x_fms_tmpy_insert()/erase().
 *
 *  6. Messages: the shared queue is fms->msgs (a380x_fms_messages.h).  Footer / MSG LIST / CLEAR
 *     INFO operate on it directly.
 *
 *  7. Everything else the pages may need from the aircraft (position, GPS/IRS state, FCU target,
 *     phase, EO, units) is available through a380x_fms_inputs(), a380x_fms_phase(),
 *     a380x_fms_engine_out(), fms->cfg.metric_units.  Never read X-Plane directly from a page.
 *
 * Units everywhere: masses kg, altitudes ft, flight levels FL (hundreds of ft), speeds kt CAS,
 * Mach dimensionless, temperatures degC, CG %MAC, distances NM (T.O SHIFT and runway length m),
 * times as documented per item.  "No value" is NAN for doubles and -1 for enums/indices.
 */
#ifndef A380X_FMS_H
#define A380X_FMS_H

#include <stddef.h>

#include "a380x_mfd_formats.h"
#include "a380x_fms_messages.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================================== */
/* Plans, phases, enumerations                                                                 */
/* ========================================================================================== */

/* FlightPlanIndex (spec 07 §2.3).  SEC n (1..3) is plan index n + 2. */
enum {
    A380X_FMS_PLAN_ACTIVE = 0,
    A380X_FMS_PLAN_TMPY = 1,
    A380X_FMS_PLAN_UPLINK = 2, /* reserved: CPNY F-PLN uplink is out of scope (button disabled) */
    A380X_FMS_PLAN_SEC1 = 3,
    A380X_FMS_PLAN_SEC2 = 4,
    A380X_FMS_PLAN_SEC3 = 5,
    A380X_FMS_NUM_PLANS = 6
};
#define A380X_FMS_SEC_PLAN(n) (A380X_FMS_PLAN_SEC1 + (n) - 1) /* n = 1..3 */

/* FmgcFlightPhase (spec 07 §2.1).  NOT the FWS phase 1..12 of a380x_avionics_core. */
typedef enum {
    A380X_FMS_PHASE_PREFLIGHT = 0,
    A380X_FMS_PHASE_TAKEOFF = 1,
    A380X_FMS_PHASE_CLIMB = 2,
    A380X_FMS_PHASE_CRUISE = 3,
    A380X_FMS_PHASE_DESCENT = 4,
    A380X_FMS_PHASE_APPROACH = 5,
    A380X_FMS_PHASE_GOAROUND = 6,
    A380X_FMS_PHASE_DONE = 7
} A380XFmsPhase;

typedef enum { A380X_FMS_CI_LRC = 0, A380X_FMS_CI_ECON = 1 } A380XFmsCiMode;
typedef enum { A380X_FMS_TO_TOGA = 0, A380X_FMS_TO_FLEX = 1, A380X_FMS_TO_DERATED = 2 } A380XFmsToPower;
typedef enum { A380X_FMS_PACKS_OFF_APU = 0, A380X_FMS_PACKS_ON = 1 } A380XFmsPacks;
typedef enum { A380X_FMS_AI_OFF = 0, A380X_FMS_AI_ENG_ONLY = 1, A380X_FMS_AI_ENG_WING = 2 } A380XFmsAntiIce;
/* LDG CONF radio index on PERF APPR (spec 04 §9.3): 0 = CONF 3, 1 = FULL. */
typedef enum { A380X_FMS_LDG_CONF3 = 0, A380X_FMS_LDG_FULL = 1 } A380XFmsLdgConf;

/* FMA vertical modes needed by the phase / speed / cruise-level logic (spec 07 §9.4).
 * The adapter maps A380XB1Message onto these.  [PORT] SRS and SRS GA are not ported in the FCU;
 * the adapter reports SRS while the TOGA/FLEX lever predicate holds on ground (takeoff) and
 * SRS_GA while it holds airborne in APPROACH (spec 07 §2.1 port input mapping). */
typedef enum {
    A380X_FMS_VM_NONE = 0,
    A380X_FMS_VM_SRS, A380X_FMS_VM_SRS_GA,
    A380X_FMS_VM_CLB, A380X_FMS_VM_OP_CLB, A380X_FMS_VM_DES, A380X_FMS_VM_OP_DES,
    A380X_FMS_VM_ALT, A380X_FMS_VM_ALT_STAR, A380X_FMS_VM_ALT_CST, A380X_FMS_VM_ALT_CST_STAR,
    A380X_FMS_VM_ALT_CRZ, A380X_FMS_VM_ALT_CRZ_STAR,
    A380X_FMS_VM_VS, A380X_FMS_VM_FPA,
    A380X_FMS_VM_GS, A380X_FMS_VM_FINAL, A380X_FMS_VM_FLARE, A380X_FMS_VM_LAND, A380X_FMS_VM_TCAS
} A380XFmsVerticalMode;

/* FMA lateral modes (spec 07 §9.4, PRIM FG word 4). */
typedef enum {
    A380X_FMS_LM_NONE = 0,
    A380X_FMS_LM_HDG, A380X_FMS_LM_TRK, A380X_FMS_LM_NAV,
    A380X_FMS_LM_LOC_CPT, A380X_FMS_LM_LOC_TRACK, A380X_FMS_LM_LAND, A380X_FMS_LM_FLARE,
    A380X_FMS_LM_ROLLOUT, A380X_FMS_LM_RWY, A380X_FMS_LM_RWY_TRK, A380X_FMS_LM_GA_TRK
} A380XFmsLateralMode;

/* FMC-initiated navigation of the MFDs ([PORT] applies to BOTH sides, quirk 15). */
typedef enum {
    A380X_FMS_NAV_NONE = 0,
    A380X_FMS_NAV_DATA_STATUS, /* fms/data/status: DONE phase entry, FMS reset */
    A380X_FMS_NAV_PERF         /* fms/active/perf: engine-out detection */
} A380XFmsNavRequest;

/* ========================================================================================== */
/* Configuration (port config file; spec 07 §6.1, §9.4)                                        */
/* ========================================================================================== */

typedef struct {
    double thr_red_agl_ft;   /* CONFIG_THR_RED_ALT, default 1500 (400..5000) */
    double accel_agl_ft;     /* CONFIG_ACCEL_ALT, default 1500 (400..10000) */
    double eo_accel_agl_ft;  /* CONFIG_ENG_OUT_ACCEL_ALT, default 1500 (400..10000) */
    int    metric_units;     /* 1 = T and M (default), 0 = KLB and FT (a380x/xp12/config/metric_units) */
    int    auto_step_climb;  /* AUTO_STEP_CLIMB, default 0 = DISABLED */
} A380XFmsConfig;

void a380x_fms_default_config(A380XFmsConfig *cfg);

/* ========================================================================================== */
/* Integrator hooks                                                                            */
/* ========================================================================================== */

/* Airport data from the navigation database (WP-D navdata, through the adapter). */
typedef struct {
    int    found;           /* 1 = airport exists */
    char   ident[8];        /* ICAO, NUL terminated */
    double elevation_ft;    /* NAN if unknown */
    double lat_deg, lon_deg;/* airport reference point; NAN if unknown */
    double trans_alt_ft;    /* apt.dat 1302 transition_alt; NAN if none */
    double trans_level_fl;  /* apt.dat 1302 transition_level / 100; NAN if none */
} A380XFmsAirportInfo;

/* Navdata lookup hook supplied by the integrator.  airport_lookup returns 1 and fills *out when
 * the airport exists, 0 otherwise.  With airport_lookup == NULL every 4-letter ident is accepted
 * (tests / no database) with unknown elevation and transition data. */
typedef struct {
    void *ctx;
    int (*airport_lookup)(void *ctx, const char *icao, A380XFmsAirportInfo *out);
} A380XFmsNavHooks;

/* Runway as seen by the FMS (origin or destination runway of a plan). */
typedef struct {
    int    valid;
    char   ident[8];               /* designation without ICAO, e.g. "27L" */
    double threshold_elev_ft;      /* NAN if unknown */
    double length_m;               /* NAN if unknown (T.O SHIFT range) */
    double magnetic_bearing_deg;   /* NAN if unknown (approach wind components) */
    double threshold_lat_deg, threshold_lon_deg; /* NAN if unknown */
} A380XFmsRunwayInfo;

/* Approach procedure type codes (spec 04 §9.1).  RADIO minimum exists only for ILS and GLS. */
typedef enum {
    A380X_FMS_APPR_NONE = 0, A380X_FMS_APPR_ILS, A380X_FMS_APPR_GLS, A380X_FMS_APPR_IGS,
    A380X_FMS_APPR_LOC, A380X_FMS_APPR_LOC_BC, A380X_FMS_APPR_LDA, A380X_FMS_APPR_SDF,
    A380X_FMS_APPR_GPS, A380X_FMS_APPR_RNAV, A380X_FMS_APPR_VOR, A380X_FMS_APPR_NDB,
    A380X_FMS_APPR_FMS
} A380XFmsApproachType;

typedef struct {
    int  valid;
    A380XFmsApproachType type;
    char name[12];                 /* display name, e.g. "ILS27L", "RNV09-Y" */
} A380XFmsApproachInfo;

/* Opaque lateral flight-plan store (a380x_fms_fpln.h, WP-D).  This module never dereferences it. */
struct A380XFlightPlans;

/* Flight-plan hooks.  Every member is optional (NULL = feature not available yet).
 *
 * Queries are polled every 250 ms cycle and by getters; they must be cheap and must not call back
 * into the FMS.  Operations are invoked by the FMS entry points below so that a page only ever
 * calls ONE function per pilot action (the FMS performs the data-store part and asks the plan
 * module to perform the lateral part).
 *
 * Contract for the plan module (WP-D/G):
 *   - When a destructive F-PLN edit creates the lateral TMPY, call a380x_fms_tmpy_begin() (the FMS
 *     also detects it through plan_exists(TMPY) on the next cycle, as a safety net).
 *   - TMPY INSERT / ERASE must go through a380x_fms_tmpy_insert()/a380x_fms_tmpy_erase().
 *   - SEC INDEX copy/init/delete/swap must go through a380x_fms_sec_*().
 *   - A destination change made by the plan module (NEW DEST) is detected through city_pair() and
 *     triggers the destination defaults (missed THR RED/ACC, TRANS FL).                         */
typedef struct {
    struct A380XFlightPlans *plans;

    /* ---- queries ---- */
    int      (*plan_exists)(struct A380XFlightPlans *p, int plan);
    unsigned (*plan_version)(struct A380XFlightPlans *p, int plan);
    /* Copies origin/destination/alternate ICAO ("" when none).  Returns 1 if the plan exists. */
    int      (*city_pair)(struct A380XFlightPlans *p, int plan, char origin[8], char dest[8],
                          char altn[8]);
    int      (*origin_runway)(struct A380XFlightPlans *p, int plan, A380XFmsRunwayInfo *out);
    int      (*destination_runway)(struct A380XFlightPlans *p, int plan, A380XFmsRunwayInfo *out);
    int      (*approach)(struct A380XFlightPlans *p, int plan, A380XFmsApproachInfo *out);
    /* Along-track distance PPOS -> destination in NM, NAN if unknown.  Without this hook the FMS
     * uses the great-circle PPOS -> DEST distance (spec 07 §2.1 port mapping). */
    double   (*distance_to_destination_nm)(struct A380XFlightPlans *p, int plan);
    /* Lowest climb altitude constraint (max alt of CLB constraints before the missed approach),
     * ft, NAN if none: THR RED / ACC reconciliation (spec 07 §6.1). */
    double   (*lowest_climb_constraint_ft)(struct A380XFlightPlans *p, int plan);
    /* Speed constraint for the managed target (spec 07 §5.7), kt, NAN if none.  phase is the
     * FMGC phase (CLIMB/GOAROUND: climb constraint, DESCENT: descent constraint). */
    double   (*speed_constraint_kt)(struct A380XFlightPlans *p, int phase);
    /* Managed-profile altitudes of the active leg for the FG altitude constraint (spec 07 §6.3).
     * Returns 1 and sets *climb_alt / *descent_alt (NAN when not finite). */
    int      (*constraint_altitudes)(struct A380XFlightPlans *p, double *climb_alt_ft,
                                     double *descent_alt_ft);
    /* 1 if neither plan has an active leg, or both have the same TO leg at the same index
     * (canActivateOrSwapSecondary, spec 03 §8.4). */
    int      (*active_legs_compatible)(struct A380XFlightPlans *p, int sec_plan);

    /* ---- operations (return 1 on success) ---- */
    int  (*new_city_pair)(struct A380XFlightPlans *p, int plan, const char *from, const char *to,
                          const char *altn_or_null);
    int  (*set_alternate)(struct A380XFlightPlans *p, int plan, const char *altn_or_null);
    int  (*tmpy_insert)(struct A380XFlightPlans *p);
    int  (*tmpy_erase)(struct A380XFlightPlans *p);
    int  (*sec_copy_from_active)(struct A380XFlightPlans *p, int sec_plan);
    int  (*sec_copy_from_sec)(struct A380XFlightPlans *p, int src_plan, int dst_plan);
    int  (*sec_init)(struct A380XFlightPlans *p, int sec_plan);
    int  (*sec_delete)(struct A380XFlightPlans *p, int sec_plan);
    int  (*sec_swap)(struct A380XFlightPlans *p, int sec_plan);
    /* Engine-out entry: delete cruise steps at/after the active leg, return the number deleted. */
    int  (*delete_cruise_steps)(struct A380XFlightPlans *p);
    /* DONE / FMS reset: delete every plan and recreate an empty active plan. */
    void (*reset_all)(struct A380XFlightPlans *p);
} A380XFmsPlanHooks;

/* ========================================================================================== */
/* Inputs (spec 07 §9.4), filled by the adapter every frame                                    */
/* ========================================================================================== */

typedef struct {
    /* time */
    double utc_seconds;          /* UTC seconds of day [0, 86400); NAN if unknown */
    /* power and FMC health (spec 07 §8.2) */
    int    fmc_powered[3];       /* FMC A/B/C bus powered (until DC modelling: avionics_power_on) */
    int    fmc_failed[3];        /* a380x/xp12/fms/fmc_{a,b,c}_failed */
    int    fmc_reset[3];         /* a380x/xp12/fms/fmc_{a,b,c}_reset (reset panel pulled) */
    int    switching_knob;       /* FMS switching knob: 0 BOTH ON 2, 1 NORM, 2 BOTH ON 1 */
    /* navigation */
    int    position_valid;
    double lat_deg, lon_deg;
    int    gps_primary;          /* fms_nav_gps_primary: GPS not failed and position available */
    int    irs_aligned;          /* at least one IR in NAV */
    double ground_speed_kt;
    double true_track_deg, magnetic_variation_deg;
    /* air data (ADR) */
    int    adr_valid;            /* CAS / altitude normal operation */
    double cas_kt;               /* 0 when NCD */
    double mach, tas_kt;
    double baro_alt_ft;          /* baro-corrected indicated altitude ("INDICATED ALTITUDE") */
    double pressure_alt_ft;      /* ADR pressure altitude */
    double vertical_speed_fpm;
    int    sat_valid;
    double sat_c;
    int    static_pressure_valid;
    double static_pressure_hpa;  /* corrected average static pressure; 1013.25 if unknown */
    int    radio_alt_valid;
    double radio_alt_ft;
    int    wind_valid;
    double wind_dir_true_deg, wind_speed_kt;
    /* FQMS (spec 07 §4.1 port sources) */
    int    fqms_gw_valid;
    double fqms_gw_kg;           /* sim/flightmodel/weight/m_total */
    int    fqms_fob_valid;
    double fqms_fob_kg;          /* sim/flightmodel/weight/m_fuel_total */
    int    fqms_cg_valid;
    double fqms_cg_pct;          /* %MAC; leave invalid until a W&B source exists */
    /* engines */
    double n1_pct[4], n2_pct[4]; /* engines 1..4 -> index 0..3 */
    int    engine_starting[4];   /* L:A32NX_ENGINE_STATE == 2 equivalent (starter running) */
    /* aircraft configuration */
    int    on_ground;            /* nose gear compressed (LGCIU) */
    int    flap_lever;           /* 0..4 = 0, 1, 2, 3, FULL */
    double spoiler_ratio;        /* 0..1 speedbrake extension (VLS increment) */
    /* FCU / guidance (previous frame) */
    int    fcu_alt_valid;
    double fcu_selected_alt_ft;
    int    fcu_speed_is_mach;
    double fcu_selected_speed_kt, fcu_selected_mach;
    int    speed_managed;        /* 1 managed, 0 selected */
    A380XFmsVerticalMode vertical_mode;
    A380XFmsLateralMode  lateral_mode;
    double fcu_vs_fpm, fcu_fpa_deg; /* selected V/S and FPA (sign used by the DES logic) */
    /* VNAV hand-over (not ported: leave 0) */
    int    vnav_decel_reached;   /* L:A32NX_FM_ENABLE_APPROACH_PHASE */
    /* spawn / UI context */
    int    cold_and_dark_spawn;  /* 1 unless the flight was loaded with engines running */
    int    mfd_on_active_init_fuel_perf; /* either MFD shows fms/active/{init,fuel-load,perf} */
    int    efis_baro_inhg[2];    /* per side QNH unit for the PERF APPR placeholder */
} A380XFmsInputs;

/* ========================================================================================== */
/* Outputs (spec 07 §10.3, spec 04 §15) - recomputed every 250 ms cycle                        */
/* ========================================================================================== */

typedef struct {
    int    valid;                       /* 0 while all FMCs are inop/reset: consumers must ignore */
    int    flight_phase;                /* 0..7 -> a380x/xp12/fms/flight_phase */
    /* T.O data (Active plan) */
    double v1_knots, vr_knots, v2_knots;   /* 0 = none */
    double to_flex_temp_c;              /* 0 = none, 0.1 = FLEX 0 degC (L:A32NX_AIRLINER_TO_FLEX_TEMP) */
    int    to_power_setting;            /* 0 TOGA, 1 FLEX, 2 DERATED */
    int    to_flaps;                    /* 1..3, -1 none (DISCRETE_WORD_2) */
    double to_ths_for_pct;              /* NAN none (TO_PITCH_TRIM) */
    int    to_speeds_not_inserted, vspeeds_disagree, to_speeds_too_low; /* DISCRETE_WORD_3 */
    /* THR RED / ACC (phase switched words, spec 07 §6.1) */
    double thr_red_alt_ft, acc_alt_ft, eo_acc_alt_ft;
    int    thr_red_alt_valid, acc_alt_valid, eo_acc_alt_valid;
    /* explicit takeoff and missed values (quirk 11) */
    double to_thr_red_alt_ft, to_acc_alt_ft, to_eo_acc_alt_ft;
    int    to_thr_red_alt_valid, to_acc_alt_valid, to_eo_acc_alt_valid;
    double missed_thr_red_alt_ft, missed_acc_alt_ft, missed_eo_acc_alt_ft;
    int    missed_thr_red_alt_valid, missed_acc_alt_valid, missed_eo_acc_alt_valid;
    /* transition */
    double trans_alt_ft, trans_level_fl;
    int    trans_alt_valid, trans_level_valid;
    /* cruise / constraints */
    double cruise_altitude_ft;          /* -1 none (L:A32NX_AIRLINER_CRUISE_ALTITUDE) */
    double fg_altitude_constraint_ft;   /* 0 none */
    /* managed speed (spec 07 §5.7) */
    int    managed_speed_valid;
    double managed_speed_knots;         /* CAS target (Mach targets converted to CAS) */
    int    managed_speed_is_mach;
    double managed_mach;                /* valid when managed_speed_is_mach */
    double presel_speed_knots, presel_mach; /* -1 none (armed preselection) */
    unsigned presel_activate_seq;       /* ++ on K:A32NX.FMS_PRESET_SPD_ACTIVATE */
    /* FMS speeds (current configuration, spec 07 §5.4); 0 = none */
    double green_dot_knots, f_speed_knots, s_speed_knots;
    double vls_knots, vs_knots, vmax_knots, vfe_next_knots;
    /* approach speeds (spec 07 §5.5); 0 = none */
    double approach_vapp_knots, approach_vls_knots, approach_vref_knots;
    double approach_f_knots, approach_s_knots, approach_gd_knots;
    int    landing_conf3;
    double approach_headwind_knots;     /* + headwind */
    int    approach_headwind_valid;
    /* minimums (spec 07 §9.2) */
    double dh_ft;                       /* -1 none, 0 NO DH, > 0 ft (L:AIRLINER_DECISION_HEIGHT) */
    double mda_ft;                      /* -1 none */
    int    minimums_in_range, dh_valid, mda_valid, no_dh;
    /* destination */
    double landing_elevation_ft, dest_lat_deg, dest_lon_deg;
    int    landing_elevation_valid, dest_pos_valid;
    double dest_qnh_hpa;                /* NAN none (L:A32NX_DESTINATION_QNH) */
    /* weights */
    double gross_weight_kg, zfw_kg, zfw_cg_pct; /* NAN none */
    int    pax_number;                  /* -1 none */
    /* predictions (VNAV not ported: available == 0) */
    int    predictions_available;
    double remaining_flight_time_s;     /* NAN none */
    /* messages / flags */
    int    dest_efob_below_min;
    int    msg_check_speed_mode, msg_set_hold_speed, msg_td_reached;
    unsigned nd_fm_message_flags[2];    /* L, R (bit 6 NAV PRIMARY, bit 9 NAV PRIMARY LOST) */
    /* health (spec 07 §8) */
    int    fmc_healthy[3];              /* A, B, C */
    int    fms_failed[2];               /* L (capt), R (f/o) */
    /* state */
    int    engine_out, engines_were_started, cold_and_dark_spawn;
    double goaround_init_speed_kt;      /* NAN until a go-around */
    double climb_pred_ref_ft;           /* CLB PRED TO reference, NAN none */
    /* idents (FBW event bus fms* topics) */
    char   flight_number[12], origin[8], destination[8], alternate[8], dep_runway[8], ldg_runway[8];
    /* FMC-initiated MFD navigation: the MFD module navigates both sides when the seq changes */
    int    nav_request;                 /* A380XFmsNavRequest */
    unsigned nav_request_seq;
} A380XFmsOutputs;

/* ========================================================================================== */
/* Display / entry item ids                                                                    */
/* ========================================================================================== */

/* One id per numeric pilot entry and per computed display value.  Comment format:
 *   unit; entry format and range; [RO] = read-only computed value; side effects. */
typedef enum {
    /* ---- INIT (spec 03 §3.2, 07 §3.3) ---- */
    A380X_FMS_F_CRZ_FL,         /* FL; FlightLevelFormat 0..430 -> trySetCruiseFl (FCU floor, CRZ FL
                                   ABOVE MAX FL, NEW CRZ ALT, phase rule); 0 rejected silently; not clearable */
    A380X_FMS_F_CRZ_TEMP,       /* degC; CrzTempFormat -99..99; pilot ?? ISA(CRZ FL) */
    A380X_FMS_F_CI_MODE,        /* A380XFmsCiMode dropdown index (labels EO-LRC/EO-ECON when EO) */
    A380X_FMS_F_TROPO,          /* ft; TropoFormat 1000..60000; round10(pilot ?? 36090) */
    A380X_FMS_F_CI,             /* CostIndexFormat 0..999 */
    A380X_FMS_F_TRIP_WIND,      /* kt, + tail / - head; TripWindFormat -250..250 */

    /* ---- FUEL & LOAD (spec 03 §4.2-4.3, 07 §4) ---- */
    A380X_FMS_F_GW,             /* kg [RO]; dashes before engine start */
    A380X_FMS_F_CG,             /* %MAC [RO]; dashes before engine start or when 0/unknown */
    A380X_FMS_F_FOB,            /* kg [RO]; dashes before engine start */
    A380X_FMS_F_ZFW,            /* kg; WeightFormat 270000..373000; INITIALIZE ZFW resolution, LRC default,
                                   removes CHECK ZFW (Active); not clearable */
    A380X_FMS_F_ZFWCG,          /* %MAC; PercentageFormat 23..48; not clearable */
    A380X_FMS_F_BLOCK,          /* kg; WeightFormat 0..289982; hidden after engine start */
    A380X_FMS_F_TAXI,           /* kg; WeightFormat 0..9889; pilot ?? 1500 */
    A380X_FMS_F_TRIP,           /* kg [RO]; needs a destination prediction (VNAV) */
    A380X_FMS_F_TRIP_TIME,      /* s from present [RO]; needs a destination prediction */
    A380X_FMS_F_RTE_RSV,        /* kg; WeightFormat 0..50000; pilot ?? TRIP x pct; clears pilot % */
    A380X_FMS_F_RTE_RSV_PCT,    /* %; PercentageFormat 0..15; clears pilot RTE RSV fuel */
    A380X_FMS_F_ALTN_FUEL,      /* kg; WeightFormat 0..289982; pilot ?? 6500 (FBW placeholder) if ALTN */
    A380X_FMS_F_ALTN_TIME,      /* s [RO]; FBW placeholder: never available (--:--) */
    A380X_FMS_F_FINAL_FUEL,     /* kg; WeightFormat 0..100018; clears pilot FINAL time */
    A380X_FMS_F_FINAL_TIME,     /* min; TimeHHMMFormat 0..90; clears pilot FINAL fuel */
    A380X_FMS_F_PAX,            /* count; PaxNbrFormat 0..999; CLR ignored (FBW) */
    A380X_FMS_F_JTSN_GW,        /* kg [RO]; FBW placeholder: always dashes, always disabled */
    A380X_FMS_F_TOW,            /* kg [RO]; frozen at TAKEOFF */
    A380X_FMS_F_LW,             /* kg [RO]; needs TRIP */
    A380X_FMS_F_DEST_ETA,       /* s [RO]; PREFLIGHT: time span from present ("TIME"), else UTC
                                   seconds of day ("UTC"); Active/TMPY only */
    A380X_FMS_F_DEST_EFOB,      /* kg [RO]; colour AMBER when DEST EFOB BELOW MIN on Active */
    A380X_FMS_F_ALTN_ETA,       /* s [RO]; not available (--:--) [PORT] */
    A380X_FMS_F_ALTN_EFOB,      /* kg [RO]; max(DEST EFOB - ALTN fuel, -35000) (quirk 5 fixed) */
    A380X_FMS_F_MIN_DEST_FOB,   /* kg; WeightFormat 0..250000; pilot ?? ALTN + FINAL */
    A380X_FMS_F_EXTRA,          /* kg [RO]; needs a destination prediction */
    A380X_FMS_F_EXTRA_TIME,     /* s [RO]; [PORT] EXTRA / 200 kg per min, dashes if <= 0 */

    /* ---- PERF T.O (spec 04 §5) ---- */
    A380X_FMS_F_TO_SHIFT,       /* m; LengthFormat 1..origin runway length; disabled without runway */
    A380X_FMS_F_V1,             /* kt; SpeedKnotsFormat 90..340 */
    A380X_FMS_F_VR,
    A380X_FMS_F_V2,
    A380X_FMS_F_V1_TBC,         /* kt [RO]; speeds to be confirmed after a runway change */
    A380X_FMS_F_VR_TBC,
    A380X_FMS_F_V2_TBC,
    A380X_FMS_F_TO_F,           /* kt [RO]; current F (spec 07 §5.4); dashes for SEC */
    A380X_FMS_F_TO_S,           /* kt [RO]; current S */
    A380X_FMS_F_TO_GD,          /* kt [RO]; current green dot */
    A380X_FMS_F_TO_POWER,       /* A380XFmsToPower radio index; DERATED option always disabled */
    A380X_FMS_F_FLEX_TEMP,      /* degC; TemperatureFormat 0..99; visible only when FLEX */
    A380X_FMS_F_DERATED,        /* 0..4 = D01..D05; visible only when DERATED (unreachable) */
    A380X_FMS_F_TO_FLAPS,       /* 1..3 (dropdown index + 1) */
    A380X_FMS_F_THS_FOR,        /* %MAC; PercentageFormat 0..99.9; 0 and CLR ignored (FBW) */
    A380X_FMS_F_PACKS,          /* A380XFmsPacks; CHECK T.O. DATA if engines were started */
    A380X_FMS_F_ANTI_ICE,       /* A380XFmsAntiIce; CHECK T.O. DATA if engines were started */
    A380X_FMS_F_THR_RED,        /* ft; AltitudeOrFlightLevelFormat(trans alt) 0..43000; round10(pilot ?? default);
                                   [PORT] >= origin elev + 400, raises ACC when above it */
    A380X_FMS_F_ACC,            /* ft; as THR RED; [PORT] >= origin elev + 400 and >= THR RED */
    A380X_FMS_F_EO_ACC,         /* ft; as THR RED */
    A380X_FMS_F_TRANS_ALT,      /* ft; AltitudeFormat 1..43000; pilot ?? database (small font) */
    A380X_FMS_F_NOISE,          /* 0/1 noise enabled; NOISE button disabled (FBW-PH), CANCEL -> 0 */
    A380X_FMS_F_NOISE_N1,       /* %; PercentageFormat 40..110 */
    A380X_FMS_F_NOISE_SPD,      /* kt; 90..340 */
    A380X_FMS_F_NOISE_END,      /* ft; AltitudeOrFlightLevelFormat(trans alt) */

    /* ---- PERF CLB (spec 04 §6) ---- */
    A380X_FMS_F_CLB_DERATED,    /* 0 NONE, 1..5; always inactive (FBW-PH) */
    A380X_FMS_F_CLB_PRED_TO,    /* ft; AltitudeOrFlightLevelFormat(trans alt); pilot ?? automatic */
    A380X_FMS_F_PRESEL_CLB,     /* kt; 90..340; shown only before CLIMB */
    A380X_FMS_F_CLB_ECON_SPD,   /* kt [RO]; 290(1-d)+330d, d=(CI/999)^2, 250 if no CI */
    A380X_FMS_F_CLB_ECON_MACH,  /* Mach [RO]; 0.84 */
    A380X_FMS_F_CLB_SPD_LIM,    /* kt [RO]; climb speed limit (250) */
    A380X_FMS_F_CLB_SPD_LIM_ALT,/* ft [RO]; climb speed limit altitude (10000) */
    A380X_FMS_F_CLB_SPD_LIM_MACH,/* Mach [RO]; Mach of the limit speed at present conditions */

    /* ---- PERF CRZ (spec 04 §7) ---- */
    A380X_FMS_F_PRESEL_CRZ_MACH,/* Mach; SpeedMachFormat 0.1..0.89; same store as PRESEL_CRZ_SPD */
    A380X_FMS_F_PRESEL_CRZ_SPD, /* kt; 90..340; same store as PRESEL_CRZ_MACH */
    A380X_FMS_F_CRZ_ECON_SPD,   /* kt [RO]; 310 */
    A380X_FMS_F_CRZ_ECON_MACH,  /* Mach [RO]; 0.85 */
    A380X_FMS_F_REC_MAX_FL,     /* FL [RO]; Active/TMPY only; title strip REC MAX */
    A380X_FMS_F_OPT_FL,         /* FL [RO]; null if EO or phase > CRUISE */
    A380X_FMS_F_EO_MAX_FL,      /* FL [RO] */

    /* ---- PERF DES (spec 04 §8) ---- */
    A380X_FMS_F_DES_CABIN_RATE, /* ft/min; DescentRateFormat -999..-100; default -350 */
    A380X_FMS_F_DES_MANAGED_MACH,/* Mach; 0.1..0.89; pilot ?? 0.84 */
    A380X_FMS_F_DES_MANAGED_SPD,/* kt; 90..340; pilot ?? ECON DES */
    A380X_FMS_F_DES_SPD_LIM,    /* kt [RO] (250) */
    A380X_FMS_F_DES_SPD_LIM_ALT,/* ft [RO] (10000) */

    /* ---- PERF APPR (spec 04 §9) ---- */
    A380X_FMS_F_APPR_WIND_DIR,  /* deg magnetic; WindDirectionFormat 0..359 */
    A380X_FMS_F_APPR_WIND_SPD,  /* kt; WindSpeedFormat 0..250 */
    A380X_FMS_F_APPR_HEADWIND,  /* kt [RO]; + head / - tail (label HD/TL, display abs) */
    A380X_FMS_F_APPR_CROSSWIND, /* kt [RO]; absolute value */
    A380X_FMS_F_APPR_TEMP,      /* degC; TemperatureFormat -99..99 */
    A380X_FMS_F_APPR_QNH,       /* hPa, or inHg if < 100 (as entered); CLR ignored (FBW) */
    A380X_FMS_F_APPR_BARO,      /* ft; AltitudeFormat 0..43000 (MDA) */
    A380X_FMS_F_APPR_RADIO,     /* ft; RadioAltitudeFormat; 0 = NO DH; ILS/GLS only */
    A380X_FMS_F_APPR_GD,        /* kt [RO]; approach green dot at destination elevation */
    A380X_FMS_F_APPR_S,         /* kt [RO] */
    A380X_FMS_F_APPR_F,         /* kt [RO] */
    A380X_FMS_F_APPR_VREF,      /* kt [RO] (always the FULL table) */
    A380X_FMS_F_APPR_VLS,       /* kt [RO] */
    A380X_FMS_F_LDG_CONF,       /* A380XFmsLdgConf radio index (0 = CONF 3, 1 = FULL) */
    A380X_FMS_F_VAPP,           /* kt; SpeedKnotsFormat 90..340; pilot ?? computed */
    A380X_FMS_F_TRANS_FL,       /* FL; FlightLevelFormat 0..430; pilot ?? database (small font) */
    A380X_FMS_F_VERT_DEV,       /* ft [RO]; VNAV not ported: never available (+-----) */
    A380X_FMS_F_DEST_DIST,      /* NM [RO]; distance to destination (mandatory OAT/QNH rule) */

    /* ---- PERF GA (spec 04 §10) ---- */
    A380X_FMS_F_GA_THR_RED,     /* ft; AltitudeOrFlightLevelFormat(trans FL) */
    A380X_FMS_F_GA_ACC,
    A380X_FMS_F_GA_EO_ACC,
    A380X_FMS_F_GA_TRANS,       /* ft [RO]; TRANS FL x 100 */

    /* ---- DATA/STATUS (spec 03 §5.3) ---- */
    A380X_FMS_F_FUEL_PENALTY,   /* %; FuelPenaltyPercentFormat 0..999.9 (plan independent);
                                   entered = value > 0; drops error details (FBW) */

    /* ---- misc ---- */
    A380X_FMS_F_ALTN_CRZ_FL,    /* FL [RO]; FBW alternate cruise level from DEST->ALTN distance */

    A380X_FMS_F_COUNT
} A380XFmsField;

/* Text items. */
typedef enum {
    A380X_FMS_T_FLT_NBR,        /* 1..10 chars; not clearable; disabled without plan */
    A380X_FMS_T_FROM,           /* ICAO; FROM+TO -> new city pair (NOT IN DATABASE if unknown) */
    A380X_FMS_T_TO,             /* ICAO */
    A380X_FMS_T_ALTN,           /* ICAO, or "NONE" (also CLR) to delete; get: "NONE" when FROM/TO
                                   exist without alternate, "" otherwise */
    A380X_FMS_T_CPNY_RTE,       /* get: "NONE" once FROM/TO exist; always disabled (FBW-PH) */
    A380X_FMS_T_ALTN_RTE,       /* same */
    A380X_FMS_T_ORIGIN_RWY,     /* get: departure runway designation ("27L") or "" */
    A380X_FMS_T_DEST_RWY,       /* get: landing runway designation or "" */
    A380X_FMS_T_APPROACH,       /* get: approach name ("ILS27L") or "" */
    A380X_FMS_T_CALLSIGN,       /* get: ATC callsign (header) = Active flight number */
    A380X_FMS_T_SEC_CREATED,    /* get (SEC plans): "CREATED\xc2\xa0\xc2\xa0HHMM (IMPORT ACTIVE)" etc. */
    A380X_FMS_T_COUNT
} A380XFmsText;

/* Page context for page-dependent field rules. */
typedef enum {
    A380X_FMS_PAGE_ANY = 0,     /* least restrictive variant (used by the entry functions) */
    A380X_FMS_PAGE_INIT,
    A380X_FMS_PAGE_FUEL_LOAD,
    A380X_FMS_PAGE_PERF_TO,
    A380X_FMS_PAGE_PERF_CLB,
    A380X_FMS_PAGE_PERF_CRZ,
    A380X_FMS_PAGE_PERF_DES,
    A380X_FMS_PAGE_PERF_APPR,
    A380X_FMS_PAGE_PERF_GA,
    A380X_FMS_PAGE_DATA_STATUS
} A380XFmsPage;

/* Field flags (a380x_fms_field_flags / a380x_fms_text_flags). */
enum {
    A380X_FMS_FF_DISABLED  = 1u << 0, /* FBW `disabled`: grey face, no entry */
    A380X_FMS_FF_INACTIVE  = 1u << 1, /* FBW `inactive`: drawn as a static GREEN value, no entry */
    A380X_FMS_FF_MANDATORY = 1u << 2, /* amber U+25AF boxes while the value is absent */
    A380X_FMS_FF_HIDDEN    = 1u << 3, /* visibility:hidden (keeps its slot) */
    A380X_FMS_FF_CLEARABLE = 1u << 4, /* CLR allowed (canBeCleared) */
    A380X_FMS_FF_READONLY  = 1u << 5, /* computed value, never an entry */
    A380X_FMS_FF_TMPY      = 1u << 6, /* loaded plan is TMPY: entered text YELLOW */
    A380X_FMS_FF_SEC       = 1u << 7, /* loaded plan is a SEC: static values WHITE */
    A380X_FMS_FF_NA        = 1u << 8  /* not available in this port/FBW (always dashes) */
};

typedef enum {
    A380X_FMS_SRC_NONE = 0,  /* no value */
    A380X_FMS_SRC_PILOT,     /* pilot entry */
    A380X_FMS_SRC_COMPUTED,  /* FMS computation */
    A380X_FMS_SRC_DEFAULT,   /* default / airline (AMI) value */
    A380X_FMS_SRC_DATABASE   /* navigation database (TRANS ALT/FL) */
} A380XFmsSource;

/* Colour class for STATIC values (labels/values drawn with .mfd-value).  Input fields use the
 * widget rules (CYAN, YELLOW on TMPY, amber boxes) and only look at `entered`. */
typedef enum {
    A380X_FMS_COLOUR_GREEN = 0, /* normal value of the Active/TMPY plan */
    A380X_FMS_COLOUR_WHITE,     /* value of a SEC plan (.mfd-value.sec) */
    A380X_FMS_COLOUR_AMBER      /* alert, e.g. DEST EFOB below MIN DEST FOB */
} A380XFmsColour;

typedef struct {
    double        v;        /* storage units; NAN when has == 0 */
    unsigned char has;      /* 1 = value available */
    unsigned char entered;  /* FBW enteredByPilot: 1 = 27 px, 0 = 22 px (computed/default/db) */
    unsigned char src;      /* A380XFmsSource */
    unsigned char colour;   /* A380XFmsColour */
} A380XFmsValue;

/* SEC INDEX tab data (spec 03 §8.3).  The route overview comes from the plan module. */
typedef enum {
    A380X_FMS_PLANF_NONE = 0,
    A380X_FMS_PLANF_COPIED_FROM_ACTIVE = 1u << 0,  /* " (IMPORT ACTIVE)" */
    A380X_FMS_PLANF_SWAPPED_WITH_ACTIVE = 1u << 1, /* " (SWAP ACTIVE)" */
    A380X_FMS_PLANF_MANUAL = 1u << 2,              /* " (MANUAL)" */
    A380X_FMS_PLANF_CPNY = 1u << 3,                /* " (CPNY F-PLN)" */
    A380X_FMS_PLANF_ATC = 1u << 4                  /* " (ATC F-PLN)" */
} A380XFmsPlanFlags;

typedef struct {
    int      exists;        /* data store exists (and the lateral plan, when plan_exists is hooked) */
    int      has_city_pair; /* origin and destination set */
    char     from[8], to[8];
    unsigned flags;         /* A380XFmsPlanFlags */
    int      modified;      /* "-MODIFIED" suffix */
    double   created_utc_s; /* NAN if unknown */
    int      can_swap;      /* SWAP ACTIVE * visible (canActivateOrSwapSecondary) */
} A380XFmsSecInfo;

/* Destination prediction from a (future) VNAV predictor (spec 07 §4.8). */
typedef struct {
    int    valid;
    double distance_nm;     /* along track */
    double seconds;         /* time from present */
    double efob_kg;         /* estimated fuel on board at destination */
} A380XFmsDestPrediction;

/* Per-waypoint prediction for the F-PLN page (UTC/SPD/ALT/EFOB columns).  Not available in this
 * port phase: a380x_fms_waypoint_prediction() returns 0 so the page draws dashes. */
typedef struct {
    int    valid;
    double utc_seconds, efob_kg, speed_kt, mach, altitude_ft;
} A380XFmsWaypointPrediction;

/* POSITION MONITOR reference fix (FmgcData.positionMonitorFix). */
typedef struct {
    int    valid;
    char   ident[8];
    double lat_deg, lon_deg;
} A380XFmsPositionFix;

/* ========================================================================================== */
/* State (public so it can live in a static; pages must use the functions below)               */
/* ========================================================================================== */

/* Per-plan performance data store (spec 07 §2.4).  NAN = null, -1 = null enum. Masses in kg. */
typedef struct {
    /* INIT */
    double crz_fl, crz_temp_isa, crz_temp_pilot;
    double ci; int ci_mode;
    double tropo_pilot;
    double trip_wind;
    /* T.O */
    double v1, vr, v2;
    double to_shift_m;
    int    to_flaps;
    double ths_for;
    int    to_power;
    double flex_temp;
    int    derated;
    int    packs, anti_ice;
    double thr_red_pilot, thr_red_default;
    double acc_pilot, acc_default;
    double eo_acc_pilot, eo_acc_default;
    double missed_thr_red_pilot, missed_thr_red_default;
    double missed_acc_pilot, missed_acc_default;
    double missed_eo_acc_pilot, missed_eo_acc_default;
    double trans_alt_db, trans_alt_pilot;
    double trans_level_db, trans_level_pilot;
    int    noise_enabled;
    double noise_n1, noise_speed, noise_end_alt;
    int    climb_derated;
    double clb_lim_speed, clb_lim_alt; int clb_lim_pilot;
    double des_lim_speed, des_lim_alt; int des_lim_pilot;
    /* speeds */
    double presel_clb, presel_crz;         /* presel_crz: kt if > 1, Mach if <= 1 */
    double des_spd_pilot, des_mach_pilot;
    double des_cabin_rate;
    /* APPR */
    double appr_qnh, appr_temp, appr_wind_dir, appr_wind_mag;
    int    appr_wind_pilot;
    double vapp_pilot;
    double baro_min, radio_min;
    int    ldg_conf3;
    /* FUEL & LOAD (kg) */
    double zfw, zfwcg, block;
    double tow;
    double taxi_pilot, taxi_default;
    double rsv_pilot, rsv_pct_pilot, rsv_pct_default;
    double altn_fuel_pilot, altn_fuel_calc;
    double final_fuel_pilot, final_time_pilot, final_time_default;
    double final_time_calc, final_fuel_calc;
    double min_dest_fob_pilot;
    double pax;
} A380XFmsPerf;

typedef struct {
    int      exists;
    unsigned flags;              /* A380XFmsPlanFlags */
    unsigned version;            /* ++ on every change of this data store */
    double   created_utc_s;
    unsigned lateral_version0;   /* plan_version() at creation (-MODIFIED detection) */
    int      modified;
    char     flight_number[12];
    A380XFmsAirportInfo origin, dest, altn; /* found == 0 -> none */
    char     pending_from[8], pending_to[8];/* INIT page-local FROM/TO until both are set */
    unsigned linked;             /* SEC: keys still piped from ACTIVE (A380X_FMS_LINK_* bits) */
    A380XFmsPerf perf;
} A380XFmsPlanData;

/* FmgcData (spec 07 §2.5) - not stored per plan. */
typedef struct {
    char   atc_callsign[12];
    double trip_fuel_at_preflight_kg;
    int    dest_efob_below_min;
    char   vspeeds_for_runway[8];
    int    vspeeds_for_runway_set;
    double v1_tbc, vr_tbc, v2_tbc;
    double f_speed, s_speed, gd_speed;              /* current (ceil), NAN none */
    double appr_gd, appr_s, appr_f, appr_vapp, appr_vref, appr_vls;
    double clb_pred_pilot, clb_pred_auto;
    int    engine_out;
    double fuel_penalty_pct;
    A380XFmsPositionFix position_monitor_fix;
} A380XFmsFmgcData;

typedef struct A380XFms {
    A380XFmsConfig    cfg;
    A380XFmsNavHooks  nav;
    A380XFmsPlanHooks fpln;
    A380XFmsInputs    in;          /* copy of the last inputs (pages may read via a380x_fms_inputs) */
    MfdMsgQueue       msgs;        /* shared FMS message queue (both MFDs) */
    A380XFmsPlanData  plans[A380X_FMS_NUM_PLANS];
    A380XFmsFmgcData  data;
    A380XFmsDestPrediction dest_pred;
    A380XFmsOutputs   out;

    /* ---- private state (do not touch) ---- */
    int    have_inputs, initialized, was_reset, healthy;
    int    phase;
    double acc_100ms, acc_250ms;
    double sim_time_s;
    int    engines_were_started, prev_engines_were_started;
    int    is_reset;
    int    cold_and_dark;          /* L:A32NX_COLD_AND_DARK_SPAWN equivalent */
    /* phase manager */
    double done_timer, appr_ground_timer, to_timer;
    double acc_alt_msl, eo_acc_alt_msl;
    int    pending_push_pull, pending_alt_turn;
    int    vs_knob_pending; double vs_knob_timer;
    /* latched L:var style outputs */
    double cruise_alt_var, flex_var, goaround_init_speed;
    double presel_spd_var, presel_mach_var;
    /* message / edge state */
    int    zfw_init_displayed, init_msg_settable;
    int    prev_zfw_cg_undefined;
    double prev_active_zfw;
    int    dest_data_checked_in_cruise; double dest_data_timer;
    int    prev_min_fuel_below;
    int    prev_dest_efob_msg; double alt_in_climb_timer;
    int    vspeed_disagree, to_not_inserted;
    int    speed_limit_exceeded;
    int    prev_gps_primary, nav_primary_seen;
    int    prev_flying_not_all_eng;
    /* FCU-driven cruise level change (spec 07 §6.4) */
    int    fcu_crz_armed; double fcu_crz_timer;
    /* speeds */
    double speed_vs1g, speed_vls, speed_vmax, speed_vfen;
    int    managed_is_mach;
} A380XFms;

/* ========================================================================================== */
/* Lifecycle                                                                                   */
/* ========================================================================================== */

/* Initialise to the power-up state (empty ACTIVE plan, PREFLIGHT, empty queue).  cfg/nav/fpln may
 * be NULL (defaults / no hooks).  The structures are copied. */
void a380x_fms_init(A380XFms *fms, const A380XFmsConfig *cfg, const A380XFmsNavHooks *nav,
                    const A380XFmsPlanHooks *fpln);
void a380x_fms_set_config(A380XFms *fms, const A380XFmsConfig *cfg);
void a380x_fms_set_hooks(A380XFms *fms, const A380XFmsNavHooks *nav, const A380XFmsPlanHooks *fpln);

/* Run the FMS: phase evaluation at 10 Hz and the heavy block every 250 ms (spec 07 §1.2-1.3).
 * Call once per frame BEFORE the guidance update (spec 07 §1.4). */
void a380x_fms_update(A380XFms *fms, const A380XFmsInputs *in, double dt);

/* FMS reset (DATA/STATUS SWAP -> CONFIRM, or all FMCs reset/inop): every plan reset, FmgcData
 * reset, outputs re-initialised, queue emptied ([PORT]), MFDs navigate to DATA/STATUS. */
void a380x_fms_reset(A380XFms *fms);

const A380XFmsOutputs *a380x_fms_outputs(const A380XFms *fms);
const A380XFmsInputs  *a380x_fms_inputs(const A380XFms *fms);
int  a380x_fms_phase(const A380XFms *fms);
int  a380x_fms_engine_out(const A380XFms *fms);
int  a380x_fms_engines_were_started(const A380XFms *fms);
/* "FMS 1" / "FMS 1-C" / "FMS 2" / "FMS 2-C" header source label for side 0 (capt) / 1 (f/o). */
const char *a380x_fms_header_source(const A380XFms *fms, int side);
/* FMS PAGE NOT AVAIL overlay for side 0/1 (FMS_L_FAILED / FMS_R_FAILED). */
int  a380x_fms_side_failed(const A380XFms *fms, int side);

/* ========================================================================================== */
/* Plans                                                                                        */
/* ========================================================================================== */

int  a380x_fms_plan_exists(const A380XFms *fms, int plan);
/* The plan a page shows for its URI category: ACTIVE -> TMPY when a temporary plan exists;
 * SECn -> SECn or -1 when it does not exist (spec 03 §1.3).  May lazily create the TMPY data
 * store when the plan module has a lateral TMPY. */
int  a380x_fms_loaded_plan(A380XFms *fms, int uri_plan);
/* isActiveOrCopiedFromActive (AOC): ACTIVE, TMPY, or a SEC created by IMPORT ACTIVE. */
int  a380x_fms_is_active_or_copy(const A380XFms *fms, int plan);
/* hasActiveFlightPlan: ACTIVE has origin and destination. */
int  a380x_fms_has_active_flight_plan(const A380XFms *fms);
unsigned a380x_fms_plan_version(const A380XFms *fms, int plan);

/* TMPY data-store lifecycle (spec 07 §2.3). */
int  a380x_fms_tmpy_exists(const A380XFms *fms);
void a380x_fms_tmpy_begin(A380XFms *fms);   /* copy ACTIVE -> TMPY data (no-op if it exists) */
int  a380x_fms_tmpy_insert(A380XFms *fms);  /* lateral insert hook + TMPY -> ACTIVE, delete TMPY */
int  a380x_fms_tmpy_erase(A380XFms *fms);   /* lateral erase hook + delete TMPY */

/* SEC INDEX (spec 03 §8.4); n = 1..3.  Return 1 on success. */
int  a380x_fms_sec_exists(const A380XFms *fms, int n);
int  a380x_fms_sec_copy_from_active(A380XFms *fms, int n);          /* IMPORT ACTIVE* */
int  a380x_fms_sec_copy_from_sec(A380XFms *fms, int src_n, int n);  /* IMPORT SEC m* */
int  a380x_fms_sec_init(A380XFms *fms, int n);                      /* INIT on an absent SEC */
int  a380x_fms_sec_delete(A380XFms *fms, int n);                    /* DELETE* */
int  a380x_fms_sec_can_swap(const A380XFms *fms, int n);            /* SWAP ACTIVE* visible */
int  a380x_fms_sec_swap(A380XFms *fms, int n);                      /* SWAP ACTIVE* */
void a380x_fms_sec_info(const A380XFms *fms, int n, A380XFmsSecInfo *out);

/* ========================================================================================== */
/* Values, flags, formats, entries                                                             */
/* ========================================================================================== */

A380XFmsValue a380x_fms_get(const A380XFms *fms, int plan, A380XFmsField f);
unsigned      a380x_fms_field_flags(const A380XFms *fms, int plan, A380XFmsField f, A380XFmsPage page);
/* Option enable state for radio/dropdown fields (TO_POWER: TOGA/FLEX iff phase < TAKEOFF,
 * DERATED never; other fields: all options enabled). */
int           a380x_fms_option_enabled(const A380XFms *fms, int plan, A380XFmsField f, int option);
/* Fill the generic input-field format descriptor (kind, range in storage units, trans altitude
 * for ALT/FL fields, metric flag, err_details).  Read-only items get kind MFD_FMT_COUNT. */
void          a380x_fms_field_fmt(const A380XFms *fms, int plan, A380XFmsField f, MfdFmt *out);

/* Entry functions.  value is in storage units (the MfdValue.num produced by the format).
 * Return 1 if accepted, 0 if rejected; on rejection err->type says which message to show
 * (MFD_ERR_NONE = silent).  err may be NULL. */
int a380x_fms_enter(A380XFms *fms, int plan, A380XFmsField f, double value, MfdFmsError *err);
int a380x_fms_clear(A380XFms *fms, int plan, A380XFmsField f, MfdFmsError *err);

int      a380x_fms_get_text(const A380XFms *fms, int plan, A380XFmsText t, char *buf, size_t n);
unsigned a380x_fms_text_flags(const A380XFms *fms, int plan, A380XFmsText t, A380XFmsPage page);
/* s == NULL or "" means CLR.  FROM/TO/ALTN are validated through the navdata hook. */
int      a380x_fms_enter_text(A380XFms *fms, int plan, A380XFmsText t, const char *s,
                              MfdFmsError *err);

/* ========================================================================================== */
/* Actions                                                                                      */
/* ========================================================================================== */

/* PERF T.O: CONFIRM T.O SPDs button ([PORT] quirk 6: shown when any TBC speed exists and V1, VR
 * and V2 are all absent). */
int  a380x_fms_should_show_confirm_to_speeds(const A380XFms *fms, int plan);
int  a380x_fms_confirm_to_speeds(A380XFms *fms, int plan);
/* PERF footer ACTIVATE APPR (visible on Active/TMPY in CLB, CRZ, DES, GA). */
int  a380x_fms_can_activate_approach(const A380XFms *fms, int plan);
int  a380x_fms_activate_approach(A380XFms *fms);              /* tryGoInApproachPhase */
void a380x_fms_clear_engine_out(A380XFms *fms);               /* CLEAR EO -> CONFIRM */
/* PERF T.O CANCEL NOISE button. */
void a380x_fms_cancel_noise(A380XFms *fms, int plan);

/* FCU knob events, called by the adapter AFTER the guidance command was applied (spec 07 §2.1). */
void a380x_fms_on_fcu_alt_push_pull(A380XFms *fms);
void a380x_fms_on_fcu_alt_turn(A380XFms *fms);
void a380x_fms_on_fcu_vs_knob(A380XFms *fms);

/* POSITION MONITOR fix (page resolves the ident through navdata). */
void a380x_fms_set_position_monitor_fix(A380XFms *fms, const char *ident, double lat, double lon);
void a380x_fms_clear_position_monitor_fix(A380XFms *fms);
const A380XFmsPositionFix *a380x_fms_position_monitor_fix(const A380XFms *fms);

/* VNAV hand-over (later work package).  Until called, TRIP/EFOB/ETA/EXTRA/LW show dashes. */
void a380x_fms_set_destination_prediction(A380XFms *fms, double distance_nm, double seconds,
                                          double efob_kg);
void a380x_fms_clear_destination_prediction(A380XFms *fms);
int  a380x_fms_waypoint_prediction(const A380XFms *fms, int plan, int leg_index,
                                   A380XFmsWaypointPrediction *out);

/* Pure helpers shared with pages. */
double a380x_fms_isa_temp(double alt_ft);                    /* 15 - 0.0019812 * min(alt, 36089) */
double a380x_fms_qnh_to_hpa(double qnh);                     /* inHg (< 100) -> hPa */
double a380x_fms_great_circle_nm(double lat1, double lon1, double lat2, double lon2);

#ifdef __cplusplus
}
#endif

#endif
