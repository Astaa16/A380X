/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380 FMS operating speeds (see a380x_fms_speeds.h).  Tables verbatim from the pinned
 * FlyByWire fbw-a380x/src/systems/shared/src/OperatingSpeeds.tsx (spec 04 Appendix A).
 */
#include "a380x_fms_speeds.h"

#include <math.h>
#include <stddef.h>

#define A380X_SPEEDS_PI 3.14159265358979323846

#define NW 8
static const double W_KEYS[NW] = {250000, 300000, 350000, 400000, 450000, 500000, 550000, 600000};
static const double CG_KEYS[2] = {29.0, 43.0};

/* VLS_APPR_CONF[conf] (cg, W): rows CG 29 / 43. */
static const double VLS_APPR[5][2 * NW] = {
    /* CONF 1 */
    {128, 140, 151, 162, 172, 182, 191, 200,
     126, 137, 148, 158, 168, 177, 186, 195},
    /* CONF 1+F */
    {123, 133, 144, 154, 163, 172, 180, 188,
     123, 130, 140, 150, 159, 167, 176, 183},
    /* CONF 2 */
    {123, 128, 138, 148, 157, 165, 173, 181,
     123, 125, 135, 144, 153, 161, 169, 176},
    /* CONF 3 */
    {123, 124, 134, 144, 152, 161, 169, 176,
     123, 123, 131, 140, 148, 156, 164, 171},
    /* CONF FULL */
    {123, 123, 131, 140, 148, 157, 165, 173,
     123, 123, 127, 136, 144, 152, 160, 168},
};

#define NV0 12
static const double VLS0_ALT[NV0] = {0, 10000, 15000, 20000, 25000, 30000, 32000, 34000,
                                     36000, 38000, 40000, 43000};
static const double VLS0[NV0 * NW] = {
    149, 163, 176, 188, 199, 210, 220, 231, /*     0 */
    149, 163, 176, 189, 202, 216, 231, 243, /* 10000 */
    149, 163, 178, 192, 209, 222, 234, 246, /* 15000 */
    149, 165, 183, 199, 212, 226, 239, 252, /* 20000 */
    151, 171, 187, 202, 217, 231, 246, 260, /* 25000 */
    158, 174, 191, 207, 224, 239, 256, 270, /* 30000 */
    158, 176, 193, 210, 227, 244, 259, 270, /* 32000 */
    159, 178, 196, 213, 231, 247, 259, 270, /* 34000 */
    161, 180, 198, 217, 235, 247, 259, 275, /* 36000 */
    162, 182, 202, 222, 235, 247, 259, 275, /* 38000 */
    164, 185, 206, 222, 235, 250, 265, 270, /* 40000 */
    168, 191, 208, 223, 235, 250, 250, 250, /* 43000 */
};

#define NGD 4
static const double GD_ALT[NGD] = {10000, 20000, 30000, 40000};
static const double GD[NGD * NW] = {
    160, 175, 190, 205, 216, 228, 239, 252,
    161, 178, 196, 219, 237, 256, 267, 277,
    186, 208, 224, 241, 256, 272, 284, 292,
    203, 220, 236, 252, 252, 260, 260, 260,
};

static const double F2[2 * NW] = {144, 150, 162, 173, 184, 191, 191, 191,
                                  144, 146, 158, 169, 179, 189, 191, 191};
static const double F3[2 * NW] = {133, 133, 143, 153, 163, 171, 177, 177,
                                  133, 133, 139, 149, 158, 167, 175, 177};
static const double S_SPD[NW] = {146, 160, 173, 185, 196, 207, 215, 215};

#define NVMC 10
static const double VMC_ALT[NVMC] = {-2000, 0, 2000, 4000, 6000, 8000, 10000, 12000, 14100, 15100};
static const double VMCA[NVMC] = {115, 114, 114, 113, 112, 109, 106, 103, 99, 97};
static const double VMCG[NVMC] = {117, 116, 116, 115, 114, 112, 109, 106, 102, 101};

/* vfeFS: Vmo, CONF 1, 1+F, 2, 3, FULL */
static const double VFE_FS[6] = {A380X_VMO_KT, A380X_VFE_CONF1_KT, A380X_VFE_CONF1F_KT,
                                 A380X_VFE_CONF2_KT, A380X_VFE_CONF3_KT, A380X_VFE_CONFFULL_KT};

double a380x_js_round(double x)
{
    if (!isfinite(x)) return x;
    double r = floor(x);
    return (x - r >= 0.5) ? r + 1.0 : r;
}

double a380x_lerp1(const double *keys, const double *vals, int n, double x)
{
    if (n <= 0) return NAN;
    if (isnan(x)) return NAN;
    if (n == 1 || x <= keys[0]) return vals[0];
    if (x >= keys[n - 1]) return vals[n - 1];
    for (int i = 1; i < n; i++) {
        if (x <= keys[i]) {
            double t = (x - keys[i - 1]) / (keys[i] - keys[i - 1]);
            return vals[i - 1] + (vals[i] - vals[i - 1]) * t;
        }
    }
    return vals[n - 1];
}

double a380x_lerp2(const double *k1, int n1, const double *k2, int n2, const double *vals,
                   double a, double b)
{
    if (n1 <= 0 || n2 <= 0 || isnan(a) || isnan(b)) return NAN;
    int i0, i1;
    double t;
    if (n1 == 1 || a <= k1[0]) {
        i0 = i1 = 0; t = 0.0;
    } else if (a >= k1[n1 - 1]) {
        i0 = i1 = n1 - 1; t = 0.0;
    } else {
        i1 = 1;
        while (i1 < n1 - 1 && a > k1[i1]) i1++;
        i0 = i1 - 1;
        t = (a - k1[i0]) / (k1[i1] - k1[i0]);
    }
    double r0 = a380x_lerp1(k2, vals + (size_t)i0 * (size_t)n2, n2, b);
    double r1 = a380x_lerp1(k2, vals + (size_t)i1 * (size_t)n2, n2, b);
    return r0 + (r1 - r0) * t;
}

double a380x_speeds_vls_conf0(double alt_ft, double mass_kg)
{
    return a380x_lerp2(VLS0_ALT, NV0, W_KEYS, NW, VLS0, alt_ft, mass_kg);
}

double a380x_speeds_vls_appr(int appr_conf, double cg_pct, double mass_kg)
{
    if (appr_conf == 0) return a380x_speeds_vls_conf0(0.0, mass_kg);
    if (appr_conf < 1 || appr_conf > 5) return NAN; /* FBW throws */
    return a380x_lerp2(CG_KEYS, 2, W_KEYS, NW, VLS_APPR[appr_conf - 1], cg_pct, mass_kg);
}

double a380x_speeds_green_dot(double alt_ft, double mass_kg)
{
    return a380x_lerp2(GD_ALT, NGD, W_KEYS, NW, GD, alt_ft, mass_kg);
}

double a380x_speeds_f2(double cg_pct, double mass_kg)
{
    return a380x_lerp2(CG_KEYS, 2, W_KEYS, NW, F2, cg_pct, mass_kg);
}

double a380x_speeds_f3(double cg_pct, double mass_kg)
{
    return a380x_lerp2(CG_KEYS, 2, W_KEYS, NW, F3, cg_pct, mass_kg);
}

double a380x_speeds_s(double mass_kg)
{
    return a380x_lerp1(W_KEYS, S_SPD, NW, mass_kg);
}

/* FBW interpolateTable() returns vmca[0] below the range for both tables and uses lerpAngle;
 * the check is disabled (quirk 7), so a plain clamped interpolation is used here. */
double a380x_speeds_vmca(double press_alt_ft)
{
    return a380x_lerp1(VMC_ALT, VMCA, NVMC, press_alt_ft);
}

double a380x_speeds_vmcg(double press_alt_ft)
{
    return a380x_lerp1(VMC_ALT, VMCG, NVMC, press_alt_ft);
}

static double valid_pressure(double p)
{
    return (isfinite(p) && p > 0.0) ? p : 1013.25;
}

double a380x_mach_to_cas(double mach, double static_pressure_hpa)
{
    double p = valid_pressure(static_pressure_hpa);
    double qc_p = pow(0.2 * mach * mach + 1.0, 3.5) - 1.0;
    double x = pow((p / 1013.25) * qc_p + 1.0, 1.0 / 3.5) - 1.0;
    return 1479.1 * sqrt(x > 0.0 ? x : 0.0);
}

double a380x_cas_to_mach(double cas_kt, double static_pressure_hpa)
{
    double p = valid_pressure(static_pressure_hpa);
    double c = cas_kt / 1479.1;
    double qc_p0 = pow(1.0 + c * c, 3.5) - 1.0;
    double x = pow(qc_p0 * (1013.25 / p) + 1.0, 1.0 / 3.5) - 1.0;
    return sqrt(5.0 * (x > 0.0 ? x : 0.0));
}

double a380x_vmo_mmo(double static_pressure_hpa)
{
    return fmin(A380X_VMO_KT, a380x_mach_to_cas(A380X_MMO, static_pressure_hpa));
}

double a380x_isa_temp_c(double alt_ft)
{
    return fmin(alt_ft, 36089.0) * -0.0019812 + 15.0;
}

double a380x_rec_max_alt_ft(double gw, double disa)
{
    double alt = 52442.0 - 1.24194943e-2 * gw - 3.50813174e-8 * gw * gw - 53.7792269 * disa
                 - 1.87989779e-4 * gw * disa - 4.46181779 * disa * disa;
    return a380x_js_round(alt / 500.0) * 500.0;
}

double a380x_angle_diff_deg(double a, double b)
{
    return 180.0 - fabs(fabs(a - b) - 180.0);
}

double a380x_headwind_kt(double wind_kt, double wind_dir_deg, double rwy_brg_deg)
{
    return wind_kt * cos(a380x_angle_diff_deg(wind_dir_deg, rwy_brg_deg) * (A380X_SPEEDS_PI / 180.0));
}

double a380x_crosswind_kt(double wind_kt, double wind_dir_deg, double rwy_brg_deg)
{
    return a380x_headwind_kt(wind_kt, wind_dir_deg, rwy_brg_deg + 90.0);
}

double a380x_wind_component_add(double headwind_kt)
{
    double w = a380x_js_round(headwind_kt / 3.0);
    return fmax(fmin(15.0, w), 5.0);
}

double a380x_gs_mini_target(double vapp, double current_hw, double tower_hw, int lever, double vmax,
                            double vfen)
{
    double wind_diff = a380x_js_round((current_hw - tower_hw) / 3.0);
    double lim = a380x_js_round(lever == 4 ? vmax - 5.0 : vfen);
    return fmax(vapp, fmin(a380x_js_round(vapp + wind_diff), lim));
}

void a380x_operating_speeds(double m, double cg, double cas, int f_pos, int phase, double v2,
                            double alt, double wind, int ignore_spoilers, double spoiler_ratio,
                            double static_pressure_hpa, A380XOperatingSpeeds *out)
{
    static const double SPOILER_INC[6] = {25, 25, 7, 10, 10, 8};
    if (f_pos < 0) f_pos = 0;
    if (f_pos > 4) f_pos = 4;
    double vls;
    if (f_pos == 0) {
        vls = a380x_speeds_vls_conf0(alt, m);
        out->vmax = a380x_vmo_mmo(static_pressure_hpa);
        out->vfe_next = VFE_FS[1];
    } else if (f_pos == 1 && cas > 212.0) {
        vls = a380x_speeds_vls_appr(A380X_APPR_CONF_1, cg, m);
        out->vmax = VFE_FS[1];
        out->vfe_next = VFE_FS[2];
    } else {
        vls = a380x_speeds_vls_appr(f_pos + 1, cg, m);
        out->vmax = VFE_FS[f_pos + 1];
        out->vfe_next = (f_pos == 4) ? 0.0 : VFE_FS[f_pos + 2];
    }
    out->vapp = vls + a380x_wind_component_add(wind); /* raw table VLS */
    out->vref = a380x_speeds_vls_appr(A380X_APPR_CONF_FULL, cg, m);
    out->gd = a380x_speeds_green_dot(alt, m);
    out->vs1g = vls / 1.23;
    vls = fmax(vls, A380X_VMCL_KT);
    if (phase <= A380X_SPEEDS_PHASE_TAKEOFF) {
        vls = fmax(1.15 * out->vs1g, 1.05 * fmin(v2, A380X_VMCL_KT));
    } else if (f_pos == 1 && cas > 212.0) {
        vls = fmax(1.18 * out->vs1g, A380X_VMCL_KT);
    }
    /* Speed-brake increment.  [PORT] spoilers / MAXEXT[conf] = clamp(spoiler_ratio, 0, 1). */
    double ratio = spoiler_ratio;
    if (!isfinite(ratio) || ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    if (ratio > 0.0 && !ignore_spoilers) {
        int conf = f_pos + 1;
        if (f_pos == 1) conf = cas > 212.0 ? 1 : 2;
        else if (f_pos == 0) conf = 0;
        vls += SPOILER_INC[conf] * ratio;
    }
    out->vls = vls;
    double vs1g_conf0 = a380x_speeds_vls_conf0(alt, m) / 1.23;
    double vs1g_conf1 = a380x_speeds_vls_appr(A380X_APPR_CONF_1, cg, m) / 1.23; /* "1F" upstream */
    if (phase <= A380X_SPEEDS_PHASE_TAKEOFF) {
        out->f2 = fmax(1.18 * vs1g_conf1, A380X_VMCL_KT + 5.0);
        out->f3 = out->f2;
        out->s = 1.21 * vs1g_conf0;
    } else {
        out->f2 = a380x_speeds_f2(cg, m);
        out->f3 = a380x_speeds_f3(cg, m);
        out->s = a380x_speeds_s(m);
    }
}

double a380x_econ_climb_speed(double ci)
{
    if (isnan(ci)) return 250.0;
    double d = (ci / 999.0) * (ci / 999.0);
    return 290.0 * (1.0 - d) + 330.0 * d;
}

double a380x_econ_climb_mach(void) { return 0.84; }
double a380x_econ_cruise_speed(void) { return 310.0; }
double a380x_econ_cruise_mach(void) { return 0.85; }
double a380x_econ_descent_mach(void) { return 0.84; }

double a380x_econ_descent_speed(double ci)
{
    if (isnan(ci)) return 300.0;
    double d = ci / 999.0;
    return a380x_js_round(288.0 * (1.0 - d) + 300.0 * d);
}

int a380x_managed_targets(double v, double mach, int air_data_valid, double static_pressure_hpa,
                          double *cas_out)
{
    if (air_data_valid) {
        double vm = a380x_mach_to_cas(mach, static_pressure_hpa);
        if (v > vm) {
            *cas_out = vm;
            return 1;
        }
    }
    *cas_out = v;
    return 0;
}

double a380x_decel_dist_nm(double from_kt, double to_kt)
{
    double d = (from_kt - to_kt) * 0.15;
    return fmax(3.0, fmin(20.0, d));
}
