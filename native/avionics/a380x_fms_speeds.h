/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380 FMS operating speeds: lookup tables and algorithms.
 * Translation of the pinned FlyByWire fbw-a380x shared/src/OperatingSpeeds.tsx,
 * PerformanceConstants.ts and OperatingAltitudes.tsx plus the managed-speed laws of
 * MFD/FMC/fmgc.ts (docs/mfd-spec/07-fmc-logic.md §5, 04-fms-perf.md §12 and Appendix A).
 * Pure functions; no XPLM, no allocation.
 *
 * These are the FMS speeds.  The PFD speed tape uses the PRIM values (a380x_pfd_inputs.inc);
 * the FMS clean VLS table differs from the PRIM table in three cells (spec 07 §5.2).
 */
#ifndef A380X_FMS_SPEEDS_H
#define A380X_FMS_SPEEDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- PerformanceConstants.ts (spec 07 §5.1) ------------------------------------------- */
#define A380X_VMO_KT            340.0
#define A380X_MMO               0.89
#define A380X_VFE_CONF1_KT      263.0
#define A380X_VFE_CONF1F_KT     222.0
#define A380X_VFE_CONF2_KT      220.0
#define A380X_VFE_CONF3_KT      196.0
#define A380X_VFE_CONFFULL_KT   182.0
#define A380X_VMCL_KT           123.0
#define A380X_VMCL2_KT          147.0
#define A380X_VLE_KT            250.0
#define A380X_MLE               0.55
#define A380X_VLO_KT            250.0
#define A380X_MLO               0.55
#define A380X_VLO_GRAVITY_KT    220.0
#define A380X_MLO_GRAVITY       0.48
#define A380X_MAX_CERT_ALT_FT   43000.0
#define A380X_DEFAULT_TROPO_FT  36090.0
#define A380X_MIN_ZFWCG         23.0
#define A380X_MAX_ZFWCG         48.0
#define A380X_MIN_ZFW_KG        270000.0
#define A380X_MAX_ZFW_KG        373000.0
#define A380X_MIN_GW_KG         270000.0
#define A380X_MAX_GW_KG         512000.0
#define A380X_MAX_BLOCK_KG      289982.0
#define A380X_MAX_TAXI_KG       9889.0
#define A380X_MAX_RTE_RSV_KG    49986.0
#define A380X_MAX_RTE_RSV_PCT   15.0
#define A380X_MAX_JTSN_GW_KG    512000.0
#define A380X_MAX_ALTN_FUEL_KG  289982.0
#define A380X_MAX_FINAL_FUEL_KG 100018.0
#define A380X_MAX_MIN_DEST_KG   250000.0

/* ApproachConf (OperatingSpeeds.tsx): CONF_1 = 1, CONF_1F = 2, CONF_2 = 3, CONF_3 = 4, CONF_FULL = 5.
 * Flap lever n (1..4, CAS <= 212 kt for lever 1) maps to ApproachConf n + 1. */
enum {
    A380X_APPR_CONF_1 = 1, A380X_APPR_CONF_1F = 2, A380X_APPR_CONF_2 = 3,
    A380X_APPR_CONF_3 = 4, A380X_APPR_CONF_FULL = 5
};

/* FMGC phase values used by the algorithms (same numbering as A380XFmsPhase). */
#define A380X_SPEEDS_PHASE_TAKEOFF  1
#define A380X_SPEEDS_PHASE_APPROACH 5

/* ---- interpolation (MSFS SDK LerpLookupTable semantics: clamped, no extrapolation) ----- */
/* keys strictly increasing; n >= 1. */
double a380x_lerp1(const double *keys, const double *vals, int n, double x);
/* vals is row-major [n1][n2]; interpolates b along each row, then a across the rows. */
double a380x_lerp2(const double *k1, int n1, const double *k2, int n2, const double *vals,
                   double a, double b);

/* ---- lookup tables (spec 07 §5.2 / 04 Appendix A) ------------------------------------ */
double a380x_speeds_vls_appr(int appr_conf, double cg_pct, double mass_kg); /* conf 0 -> VLS_CONF_0(0 ft) */
double a380x_speeds_vls_conf0(double alt_ft, double mass_kg);
double a380x_speeds_green_dot(double alt_ft, double mass_kg);
double a380x_speeds_f2(double cg_pct, double mass_kg);
double a380x_speeds_f3(double cg_pct, double mass_kg);
double a380x_speeds_s(double mass_kg);
/* A320 VMCA/VMCG(pressure alt) tables kept for the disabled T.O SPEED TOO LOW check (quirk 7). */
double a380x_speeds_vmca(double press_alt_ft);
double a380x_speeds_vmcg(double press_alt_ft);

/* ---- air data ------------------------------------------------------------------------- */
/* MathUtils.convertMachToKCas: 1479.1*sqrt(((p/1013.25)*((0.2M^2+1)^3.5-1)+1)^(1/3.5)-1). */
double a380x_mach_to_cas(double mach, double static_pressure_hpa);
/* Inverse of a380x_mach_to_cas (CAS kt -> Mach at the given static pressure). */
double a380x_cas_to_mach(double cas_kt, double static_pressure_hpa);
/* min(VMO, machToCas(MMO, p)); p <= 0 or NaN uses 1013.25 hPa. */
double a380x_vmo_mmo(double static_pressure_hpa);
/* ISA temperature (OperatingAltitudes.tsx): 15 - 0.0019812 * min(alt, 36089) degC. */
double a380x_isa_temp_c(double alt_ft);
/* calculateRecommendedMaxAltitude rounded to 500 ft (NOT capped; caller applies min(., 43000)). */
double a380x_rec_max_alt_ft(double gw_kg, double disa_c);

/* ---- wind (spec 07 §5.5) ----------------------------------------------------------------- */
double a380x_angle_diff_deg(double a, double b);                   /* 180 - ||a-b| - 180| */
double a380x_headwind_kt(double wind_kt, double wind_dir_deg, double rwy_brg_deg);
double a380x_crosswind_kt(double wind_kt, double wind_dir_deg, double rwy_brg_deg); /* signed */
double a380x_wind_component_add(double headwind_kt);               /* clamp(round(hw/3), 5, 15) */
/* GS mini target (PRIM utility): max(vapp, min(round(vapp + windDiff), round(lever FULL ? vmax-5 : vfen))),
 * windDiff = round((current_hw - tower_hw)/3). */
double a380x_gs_mini_target(double vapp, double current_hw, double tower_hw, int lever, double vmax,
                            double vfen);

/* ---- A380OperatingSpeeds (spec 07 §5.3) ------------------------------------------------- */
typedef struct {
    double vs1g, vls, vapp, vref, f2, f3, s, gd, vmax, vfe_next;
} A380XOperatingSpeeds;

/* m kg, cg %MAC, cas kt, f_pos flap lever 0..4, phase FMGC phase, v2 kt, alt ft, wind = tower
 * headwind kt; spoiler_ratio = [PORT] spoilers / MAXEXT[conf] in 0..1 (ignored if ignore_spoilers);
 * static_pressure_hpa for VMO/MMO. */
void a380x_operating_speeds(double m, double cg, double cas, int f_pos, int phase, double v2,
                            double alt, double wind, int ignore_spoilers, double spoiler_ratio,
                            double static_pressure_hpa, A380XOperatingSpeeds *out);

/* ---- managed speed laws (fmgc.ts, spec 07 §5.6) ----------------------------------------- */
/* ci NAN = no cost index. */
double a380x_econ_climb_speed(double ci);     /* 290(1-d)+330d, d=(CI/999)^2; 250 without CI */
double a380x_econ_climb_mach(void);           /* 0.84 */
double a380x_econ_cruise_speed(void);         /* 310 */
double a380x_econ_cruise_mach(void);          /* 0.85 */
double a380x_econ_descent_speed(double ci);   /* round(288(1-d)+300d), d=CI/999; 300 without CI */
double a380x_econ_descent_mach(void);         /* 0.84 */
/* getManagedTargets: if air data valid and v > machToCas(mach, p) -> Mach target.  Returns 1 when the
 * target is Mach; *cas_out is the CAS target (the converted Mach when Mach). */
int    a380x_managed_targets(double v, double mach, int air_data_valid, double static_pressure_hpa,
                             double *cas_out);
/* decelDist(a, b) = clamp((a - b) * 0.15, 3, 20) NM (FAI.ts). */
double a380x_decel_dist_nm(double from_kt, double to_kt);

/* JS Math.round (floor(x + 0.5)). */
double a380x_js_round(double x);

#ifdef __cplusplus
}
#endif

#endif
