/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X FMS core shared by both MFDs (see a380x_fms.h).  Translation of the pinned FlyByWire
 * A380X FMC (docs/mfd-spec/07-fmc-logic.md, 03 §2-4/§8, 04 §12-15) with the [PORT] decisions of
 * spec 07 §11.  No XPLM, no OpenGL, no heap allocation.
 */
#include "a380x_fms.h"
#include "a380x_fms_speeds.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------------------------ */
/* constants                                                                                   */
/* ------------------------------------------------------------------------------------------ */

#define FMS_STEP_S            0.1     /* onUpdate cadence (FMC.ts:441-449) */
#define FMS_CYCLE_S           0.25    /* FMS_CYCLE_TIME (FMC.ts:83) */
#define LOWEST_FUEL_KG        (-35000.0) /* LOWEST_FUEL_ESTIMATE -77161.8 lb (spec 07 §4.5) */
#define FINAL_BURN_KG_MIN     200.0   /* FBW placeholder 0.2 t/min (spec 07 §4.6) */
#define ALTN_FUEL_PH_KG       6500.0  /* FBW placeholder 6.5 t (spec 07 §4.6) */
#define DEFAULT_TAXI_KG       1500.0
#define DEFAULT_RSV_PCT       5.0
#define DEFAULT_FINAL_MIN     30.0
#define DEFAULT_V2_KT         150.0
#define DEFAULT_CG_PCT        35.0
#define EARTH_RADIUS_NM       3440.065
#define FMS_PI                3.14159265358979323846
#define NBSP                  "\xc2\xa0"

/* Piped keys (spec 07 §2.3/§2.4): P = always, P* = only before engine start. */
enum {
    LINK_CRZ_FL = 1u << 0, LINK_CRZ_TEMP_ISA = 1u << 1, LINK_CRZ_TEMP = 1u << 2,
    LINK_CI = 1u << 3, LINK_CI_MODE = 1u << 4, LINK_TROPO = 1u << 5, LINK_RSV = 1u << 6,
    LINK_RSV_PCT = 1u << 7, LINK_FINAL_FUEL = 1u << 8, LINK_FINAL_TIME = 1u << 9, LINK_PAX = 1u << 10,
    LINK_ZFW = 1u << 11, LINK_ZFWCG = 1u << 12, LINK_BLOCK = 1u << 13, LINK_MIN_DEST = 1u << 14
};
#define LINK_P      (LINK_CRZ_FL | LINK_CRZ_TEMP_ISA | LINK_CRZ_TEMP | LINK_CI | LINK_CI_MODE | \
                     LINK_TROPO | LINK_RSV | LINK_RSV_PCT | LINK_FINAL_FUEL | LINK_FINAL_TIME | LINK_PAX)
#define LINK_PSTAR  (LINK_ZFW | LINK_ZFWCG | LINK_BLOCK | LINK_MIN_DEST)

/* ------------------------------------------------------------------------------------------ */
/* small helpers                                                                               */
/* ------------------------------------------------------------------------------------------ */

static int has(double x) { return !isnan(x); }
static double nz(double x) { return isnan(x) ? 0.0 : x; }
static double coalesce(double a, double b) { return isnan(a) ? b : a; }
static double round10(double x) { return isnan(x) ? NAN : a380x_js_round(x / 10.0) * 10.0; }

static void scopy(char *dst, size_t n, const char *src)
{
    if (n == 0) return;
    if (!src) src = "";
    size_t i = 0;
    for (; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static void err_clear(MfdFmsError *e)
{
    if (!e) return;
    e->type = MFD_ERR_NONE;
    e->details[0] = '\0';
}

static int err_set(MfdFmsError *e, MfdFmsErrorType t, const char *details)
{
    if (e) {
        e->type = t;
        scopy(e->details, sizeof e->details, details ? details : "");
    }
    return 0;
}

static int plan_valid_index(int plan) { return plan >= 0 && plan < A380X_FMS_NUM_PLANS; }
static int is_sec(int plan) { return plan >= A380X_FMS_PLAN_SEC1 && plan <= A380X_FMS_PLAN_SEC3; }
static int is_at(int plan) { return plan == A380X_FMS_PLAN_ACTIVE || plan == A380X_FMS_PLAN_TMPY; }

static int plan_ok(const A380XFms *f, int plan)
{
    return plan_valid_index(plan) && f->plans[plan].exists;
}

static A380XFmsPerf *PD(A380XFms *f, int plan) { return &f->plans[plan].perf; }
static const A380XFmsPerf *CPD(const A380XFms *f, int plan) { return &f->plans[plan].perf; }
static const A380XFmsPerf *ACT(const A380XFms *f) { return &f->plans[A380X_FMS_PLAN_ACTIVE].perf; }

static double now_utc(const A380XFms *f) { return f->in.utc_seconds; }

double a380x_fms_isa_temp(double alt_ft) { return a380x_isa_temp_c(alt_ft); }

double a380x_fms_qnh_to_hpa(double qnh)
{
    if (isnan(qnh)) return NAN;
    return qnh < 100.0 ? qnh * 33.863886666667 : qnh;
}

double a380x_fms_great_circle_nm(double lat1, double lon1, double lat2, double lon2)
{
    if (isnan(lat1) || isnan(lon1) || isnan(lat2) || isnan(lon2)) return NAN;
    double r = FMS_PI / 180.0;
    double dlat = (lat2 - lat1) * r, dlon = (lon2 - lon1) * r;
    double a = sin(dlat / 2) * sin(dlat / 2) + cos(lat1 * r) * cos(lat2 * r) * sin(dlon / 2) * sin(dlon / 2);
    return 2.0 * EARTH_RADIUS_NM * atan2(sqrt(a), sqrt(fmax(0.0, 1.0 - a)));
}

/* ------------------------------------------------------------------------------------------ */
/* data-store defaults                                                                         */
/* ------------------------------------------------------------------------------------------ */

static void perf_defaults(A380XFmsPerf *d)
{
    d->crz_fl = d->crz_temp_isa = d->crz_temp_pilot = NAN;
    d->ci = NAN; d->ci_mode = A380X_FMS_CI_ECON;
    d->tropo_pilot = NAN; d->trip_wind = NAN;
    d->v1 = d->vr = d->v2 = NAN;
    d->to_shift_m = NAN; d->to_flaps = -1; d->ths_for = NAN;
    d->to_power = A380X_FMS_TO_TOGA; d->flex_temp = NAN; d->derated = 0;
    d->packs = A380X_FMS_PACKS_ON; d->anti_ice = A380X_FMS_AI_OFF;
    d->thr_red_pilot = d->thr_red_default = NAN;
    d->acc_pilot = d->acc_default = NAN;
    d->eo_acc_pilot = d->eo_acc_default = NAN;
    d->missed_thr_red_pilot = d->missed_thr_red_default = NAN;
    d->missed_acc_pilot = d->missed_acc_default = NAN;
    d->missed_eo_acc_pilot = d->missed_eo_acc_default = NAN;
    d->trans_alt_db = d->trans_alt_pilot = NAN;
    d->trans_level_db = d->trans_level_pilot = NAN;
    d->noise_enabled = 0; d->noise_n1 = d->noise_speed = d->noise_end_alt = NAN;
    d->climb_derated = 0;
    d->clb_lim_speed = 250.0; d->clb_lim_alt = 10000.0; d->clb_lim_pilot = 0;
    d->des_lim_speed = 250.0; d->des_lim_alt = 10000.0; d->des_lim_pilot = 0;
    d->presel_clb = d->presel_crz = NAN;
    d->des_spd_pilot = d->des_mach_pilot = NAN;
    d->des_cabin_rate = -350.0;
    d->appr_qnh = d->appr_temp = d->appr_wind_dir = d->appr_wind_mag = NAN;
    d->appr_wind_pilot = 0;
    d->vapp_pilot = NAN; d->baro_min = d->radio_min = NAN; d->ldg_conf3 = 0;
    d->zfw = d->zfwcg = d->block = NAN; d->tow = NAN;
    d->taxi_pilot = NAN; d->taxi_default = DEFAULT_TAXI_KG;
    d->rsv_pilot = NAN; d->rsv_pct_pilot = NAN; d->rsv_pct_default = DEFAULT_RSV_PCT;
    d->altn_fuel_pilot = NAN; d->altn_fuel_calc = 0.0;
    d->final_fuel_pilot = d->final_time_pilot = NAN; d->final_time_default = DEFAULT_FINAL_MIN;
    d->final_time_calc = d->final_fuel_calc = NAN;
    d->min_dest_fob_pilot = NAN; d->pax = NAN;
}

static void airport_clear(A380XFmsAirportInfo *a)
{
    memset(a, 0, sizeof *a);
    a->elevation_ft = a->lat_deg = a->lon_deg = a->trans_alt_ft = a->trans_level_fl = NAN;
}

static void plan_clear(A380XFmsPlanData *p, int exists)
{
    unsigned v = p->version;
    memset(p, 0, sizeof *p);
    p->exists = exists;
    p->version = v + 1;
    p->created_utc_s = NAN;
    airport_clear(&p->origin);
    airport_clear(&p->dest);
    airport_clear(&p->altn);
    perf_defaults(&p->perf);
}

static void runway_clear(A380XFmsRunwayInfo *r)
{
    memset(r, 0, sizeof *r);
    r->threshold_elev_ft = r->length_m = r->magnetic_bearing_deg = NAN;
    r->threshold_lat_deg = r->threshold_lon_deg = NAN;
}

static void fmgc_reset(A380XFmsFmgcData *d)
{
    memset(d, 0, sizeof *d);
    d->trip_fuel_at_preflight_kg = NAN;
    d->v1_tbc = d->vr_tbc = d->v2_tbc = NAN;
    d->f_speed = d->s_speed = d->gd_speed = NAN;
    d->appr_gd = d->appr_s = d->appr_f = d->appr_vapp = d->appr_vref = d->appr_vls = NAN;
    d->clb_pred_pilot = d->clb_pred_auto = NAN;
    d->fuel_penalty_pct = NAN;
    d->position_monitor_fix.lat_deg = d->position_monitor_fix.lon_deg = NAN;
}

void a380x_fms_default_config(A380XFmsConfig *cfg)
{
    cfg->thr_red_agl_ft = 1500.0;
    cfg->accel_agl_ft = 1500.0;
    cfg->eo_accel_agl_ft = 1500.0;
    cfg->metric_units = 1;
    cfg->auto_step_climb = 0;
}

/* ------------------------------------------------------------------------------------------ */
/* message helpers                                                                             */
/* ------------------------------------------------------------------------------------------ */

static void msg_add(A380XFms *f, MfdMsgId id, const char *token, MfdMsgResolvedFn res)
{
    mfd_msg_add(&f->msgs, id, token, NULL, res, NULL, f);
}

static int msg_present_uncleared(const A380XFms *f, MfdMsgId id, int *cleared)
{
    const MfdMsgEntry *rows[MFD_MSG_QUEUE_CAP];
    int n = mfd_msg_list(&f->msgs, rows, MFD_MSG_QUEUE_CAP);
    int found = 0, all_cleared = 1;
    for (int i = 0; i < n; i++) {
        if (rows[i]->id == id) {
            found = 1;
            if (!rows[i]->cleared) all_cleared = 0;
        }
    }
    if (cleared) *cleared = found ? all_cleared : 0;
    return found;
}

/* ------------------------------------------------------------------------------------------ */
/* plan module queries                                                                         */
/* ------------------------------------------------------------------------------------------ */

static int origin_runway(const A380XFms *f, int plan, A380XFmsRunwayInfo *r)
{
    runway_clear(r);
    if (!plan_ok(f, plan) || !f->fpln.origin_runway) return 0;
    if (!f->fpln.origin_runway(f->fpln.plans, plan, r)) {
        runway_clear(r);
        return 0;
    }
    return r->valid;
}

static int dest_runway(const A380XFms *f, int plan, A380XFmsRunwayInfo *r)
{
    runway_clear(r);
    if (!plan_ok(f, plan) || !f->fpln.destination_runway) return 0;
    if (!f->fpln.destination_runway(f->fpln.plans, plan, r)) {
        runway_clear(r);
        return 0;
    }
    return r->valid;
}

static int approach_info(const A380XFms *f, int plan, A380XFmsApproachInfo *a)
{
    memset(a, 0, sizeof *a);
    if (!plan_ok(f, plan) || !f->fpln.approach) return 0;
    if (!f->fpln.approach(f->fpln.plans, plan, a)) {
        memset(a, 0, sizeof *a);
        return 0;
    }
    return a->valid;
}

static double dest_lat(const A380XFms *f, int plan, double *lon)
{
    A380XFmsRunwayInfo r;
    if (dest_runway(f, plan, &r) && has(r.threshold_lat_deg) && has(r.threshold_lon_deg)) {
        *lon = r.threshold_lon_deg;
        return r.threshold_lat_deg;
    }
    const A380XFmsAirportInfo *d = &f->plans[plan].dest;
    if (d->found && has(d->lat_deg) && has(d->lon_deg)) {
        *lon = d->lon_deg;
        return d->lat_deg;
    }
    *lon = NAN;
    return NAN;
}

/* Along-track distance to destination, falling back to the great circle PPOS -> DEST. */
static double dist_to_dest(const A380XFms *f, int plan)
{
    if (!plan_ok(f, plan) || !f->plans[plan].dest.found) return NAN;
    if (f->fpln.distance_to_destination_nm) {
        double d = f->fpln.distance_to_destination_nm(f->fpln.plans, plan);
        if (has(d)) return d;
    }
    if (!f->in.position_valid) return NAN;
    double lon, lat = dest_lat(f, plan, &lon);
    return a380x_fms_great_circle_nm(f->in.lat_deg, f->in.lon_deg, lat, lon);
}

/* getDestinationElevation: runway threshold, else airport, else 0. */
static double dest_elevation(const A380XFms *f, int plan, int *valid)
{
    A380XFmsRunwayInfo r;
    if (dest_runway(f, plan, &r) && has(r.threshold_elev_ft)) {
        if (valid) *valid = 1;
        return r.threshold_elev_ft;
    }
    const A380XFmsAirportInfo *d = &f->plans[plan].dest;
    if (d->found && has(d->elevation_ft)) {
        if (valid) *valid = 1;
        return d->elevation_ft;
    }
    if (valid) *valid = 0;
    return 0.0;
}

/* ------------------------------------------------------------------------------------------ */
/* effective values (spec 07 §2.4 "Eff." column)                                               */
/* ------------------------------------------------------------------------------------------ */

static double eff_thr_red(const A380XFmsPerf *d) { return round10(coalesce(d->thr_red_pilot, d->thr_red_default)); }
static double eff_acc(const A380XFmsPerf *d) { return round10(coalesce(d->acc_pilot, d->acc_default)); }
static double eff_eo_acc(const A380XFmsPerf *d) { return round10(coalesce(d->eo_acc_pilot, d->eo_acc_default)); }
static double eff_m_thr_red(const A380XFmsPerf *d) { return round10(coalesce(d->missed_thr_red_pilot, d->missed_thr_red_default)); }
static double eff_m_acc(const A380XFmsPerf *d) { return round10(coalesce(d->missed_acc_pilot, d->missed_acc_default)); }
static double eff_m_eo_acc(const A380XFmsPerf *d) { return round10(coalesce(d->missed_eo_acc_pilot, d->missed_eo_acc_default)); }
static double eff_trans_alt(const A380XFmsPerf *d) { return round10(coalesce(d->trans_alt_pilot, d->trans_alt_db)); }
static double eff_trans_level(const A380XFmsPerf *d)
{
    double v = coalesce(d->trans_level_pilot, d->trans_level_db);
    return isnan(v) ? NAN : a380x_js_round(v);
}
static double eff_taxi(const A380XFmsPerf *d) { return coalesce(d->taxi_pilot, d->taxi_default); }
static double eff_rsv_pct(const A380XFmsPerf *d) { return coalesce(d->rsv_pct_pilot, d->rsv_pct_default); }
static double eff_altn_fuel(const A380XFmsPerf *d) { return coalesce(d->altn_fuel_pilot, d->altn_fuel_calc); }
static double eff_final_time(const A380XFmsPerf *d)
{
    return coalesce(d->final_time_pilot, coalesce(d->final_time_calc, d->final_time_default));
}
static double eff_final_fuel(const A380XFmsPerf *d) { return coalesce(d->final_fuel_pilot, d->final_fuel_calc); }
static double eff_min_dest(const A380XFmsPerf *d)
{
    if (has(d->min_dest_fob_pilot)) return d->min_dest_fob_pilot;
    double a = eff_altn_fuel(d), fi = eff_final_fuel(d);
    return (has(a) && has(fi)) ? a + fi : NAN;
}
static double eff_des_spd(const A380XFmsPerf *d)
{
    return has(d->des_spd_pilot) ? d->des_spd_pilot : a380x_econ_descent_speed(d->ci);
}
static double eff_des_mach(const A380XFmsPerf *d)
{
    return has(d->des_mach_pilot) ? d->des_mach_pilot : a380x_econ_descent_mach();
}
static int clb_limit(const A380XFmsPerf *d, double *spd, double *alt)
{
    if (d->clb_lim_speed > 0 && d->clb_lim_alt > 0 && has(d->clb_lim_speed) && has(d->clb_lim_alt)) {
        *spd = d->clb_lim_speed; *alt = d->clb_lim_alt; return 1;
    }
    return 0;
}
static int des_limit(const A380XFmsPerf *d, double *spd, double *alt)
{
    if (d->des_lim_speed > 0 && d->des_lim_alt > 0 && has(d->des_lim_speed) && has(d->des_lim_alt)) {
        *spd = d->des_lim_speed; *alt = d->des_lim_alt; return 1;
    }
    return 0;
}
static int dest_data_entered(const A380XFmsPerf *d)
{
    return has(d->appr_qnh) && has(d->appr_temp) && has(d->appr_wind_dir) && has(d->appr_wind_mag);
}

/* ------------------------------------------------------------------------------------------ */
/* aircraft state predicates                                                                   */
/* ------------------------------------------------------------------------------------------ */

static int engine_on(const A380XFms *f, int i) { return f->in.n2_pct[i] > 20.0; }
static int any_engine_on(const A380XFms *f)
{
    return engine_on(f, 0) || engine_on(f, 1) || engine_on(f, 2) || engine_on(f, 3);
}
static int all_engines_on(const A380XFms *f)
{
    return engine_on(f, 0) && engine_on(f, 1) && engine_on(f, 2) && engine_on(f, 3);
}
/* flightphase.isAnEngineOn (engines 2 and 3, quirk 12). */
static int phase_engine_on(const A380XFms *f) { return engine_on(f, 1) || engine_on(f, 2); }

static double static_pressure(const A380XFms *f)
{
    return (f->in.static_pressure_valid && f->in.static_pressure_hpa > 0) ? f->in.static_pressure_hpa : 1013.25;
}

/* ------------------------------------------------------------------------------------------ */
/* weights and fuel (spec 07 §4)                                                               */
/* ------------------------------------------------------------------------------------------ */

/* getFOB (kg): FQMS FOB when an engine runs, else the plan BLOCK. */
static double fob_kg(const A380XFms *f, int plan)
{
    if (any_engine_on(f)) {
        return (f->in.fqms_fob_valid && has(f->in.fqms_fob_kg)) ? f->in.fqms_fob_kg : NAN;
    }
    return plan_ok(f, plan) ? CPD(f, plan)->block : NAN;
}

/* getGrossWeightKg: FQMS GW, else ZFW + FOB (both required); 0 -> null. */
static double gw_kg(const A380XFms *f, int plan)
{
    if (f->in.fqms_gw_valid && has(f->in.fqms_gw_kg) && f->in.fqms_gw_kg > 0) return f->in.fqms_gw_kg;
    if (!plan_ok(f, plan)) return NAN;
    double zfw = CPD(f, plan)->zfw, fob = fob_kg(f, plan);
    if (isnan(zfw) || isnan(fob)) return NAN;
    double gw = zfw + fob;
    return gw != 0.0 ? gw : NAN;
}

static double gw_cg(const A380XFms *f)
{
    return (f->in.fqms_cg_valid && has(f->in.fqms_cg_pct)) ? f->in.fqms_cg_pct : NAN;
}

/* getTripFuel: ACTIVE only and only with a destination prediction. */
static double trip_kg(const A380XFms *f, int plan)
{
    if (plan != A380X_FMS_PLAN_ACTIVE || !f->dest_pred.valid) return NAN;
    double fob = fob_kg(f, A380X_FMS_PLAN_ACTIVE);
    if (isnan(fob) || isnan(f->dest_pred.efob_kg)) return NAN;
    return fob - f->dest_pred.efob_kg;
}

static double lw_kg(const A380XFms *f, int plan)
{
    if (!plan_ok(f, plan)) return NAN;
    double tow = CPD(f, plan)->tow, gw = gw_kg(f, A380X_FMS_PLAN_ACTIVE), tf = trip_kg(f, plan);
    if (!f->engines_were_started) {
        return (has(tow) && tow != 0 && has(tf) && tf != 0) ? tow - tf : NAN;
    }
    if (has(gw) && has(tf) && tf != 0 && f->phase >= A380X_FMS_PHASE_TAKEOFF) return gw - tf;
    if (has(gw)) return gw - nz(tf) - nz(eff_taxi(CPD(f, plan)));
    return NAN;
}

static double rte_rsv_kg(const A380XFms *f, int plan, double trip)
{
    const A380XFmsPerf *d = CPD(f, plan);
    if (has(d->rsv_pilot)) return d->rsv_pilot;
    if (has(trip)) {
        double pct = eff_rsv_pct(d);
        return has(pct) ? trip * pct / 100.0 : NAN;
    }
    return NAN;
}

static double dest_efob_kg(const A380XFms *f, int plan)
{
    if (plan != A380X_FMS_PLAN_ACTIVE || !f->dest_pred.valid) return NAN;
    return f->dest_pred.efob_kg;
}

static double extra_kg(const A380XFms *f, int plan)
{
    if (plan != A380X_FMS_PLAN_ACTIVE || !f->dest_pred.valid) return NAN;
    const A380XFmsPerf *d = CPD(f, plan);
    if (f->phase == A380X_FMS_PHASE_PREFLIGHT) {
        double trip = trip_kg(f, plan);
        if (isnan(trip)) return NAN;
        double fuel = f->engines_were_started ? fob_kg(f, plan) : nz(d->block);
        double v = nz(fuel) - nz(eff_taxi(d)) - trip - nz(eff_min_dest(d)) - nz(rte_rsv_kg(f, plan, trip));
        return fmax(v, LOWEST_FUEL_KG);
    }
    return fmax(f->dest_pred.efob_kg - nz(eff_min_dest(d)), LOWEST_FUEL_KG);
}

/* calculateTakeoffWeight (spec 07 §4.3) - [PORT] every cycle for every plan. */
static void calc_tow(A380XFms *f, int plan)
{
    A380XFmsPerf *d = PD(f, plan);
    int aoc = a380x_fms_is_active_or_copy(f, plan);
    if (aoc && f->phase >= A380X_FMS_PHASE_TAKEOFF) return; /* frozen */
    if (!aoc || !f->engines_were_started) {
        double taxi = eff_taxi(d);
        d->tow = (has(d->zfw) && has(d->block) && has(taxi)) ? d->zfw + d->block - taxi : NAN;
    } else {
        double gw = gw_kg(f, A380X_FMS_PLAN_ACTIVE);
        d->tow = has(gw) ? gw - nz(eff_taxi(d)) : NAN;
    }
}

/* calculateFinalAndAlternateFuel (spec 07 §4.6, FBW placeholders) - every cycle. */
static void calc_final_altn(A380XFms *f, int plan)
{
    A380XFmsPlanData *p = &f->plans[plan];
    A380XFmsPerf *d = &p->perf;
    int has_altn = p->altn.found;
    d->altn_fuel_calc = has_altn ? ALTN_FUEL_PH_KG : 0.0;
    if (!has_altn) d->altn_fuel_pilot = NAN;
    if (has(d->final_fuel_pilot)) {
        d->final_time_calc = d->final_fuel_pilot / FINAL_BURN_KG_MIN;
        d->final_fuel_calc = NAN;
    } else {
        d->final_time_calc = NAN;
        double t = eff_final_time(d);
        d->final_fuel_calc = has(t) ? t * FINAL_BURN_KG_MIN : NAN;
    }
}

static double rec_max_fl(const A380XFms *f, int plan)
{
    if (plan != A380X_FMS_PLAN_ACTIVE && plan != A380X_FMS_PLAN_TMPY) return NAN;
    double gw = gw_kg(f, A380X_FMS_PLAN_ACTIVE);
    if (isnan(gw)) return NAN;
    double disa = f->in.sat_valid ? f->in.sat_c - a380x_isa_temp_c(f->in.baro_alt_ft) : 0.0;
    return fmin(a380x_rec_max_alt_ft(gw, disa), A380X_MAX_CERT_ALT_FT) / 100.0;
}

/* ------------------------------------------------------------------------------------------ */
/* origin / destination defaults (spec 07 §6.1)                                                */
/* ------------------------------------------------------------------------------------------ */

static void lookup_airport(const A380XFms *f, const char *icao, A380XFmsAirportInfo *out, int *found)
{
    airport_clear(out);
    *found = 0;
    if (!icao || !icao[0]) return;
    if (f->nav.airport_lookup) {
        A380XFmsAirportInfo tmp;
        airport_clear(&tmp);
        if (f->nav.airport_lookup(f->nav.ctx, icao, &tmp) && tmp.found) {
            *out = tmp;
            out->found = 1;
            scopy(out->ident, sizeof out->ident, icao);
            *found = 1;
        }
        return;
    }
    out->found = 1;
    scopy(out->ident, sizeof out->ident, icao);
    *found = 1;
}

static void apply_origin(A380XFms *f, int plan, const A380XFmsAirportInfo *apt)
{
    A380XFmsPlanData *p = &f->plans[plan];
    A380XFmsPerf *d = &p->perf;
    if (apt && apt->found) p->origin = *apt; else airport_clear(&p->origin);
    double ref = (apt && apt->found) ? apt->elevation_ft : NAN;
    d->thr_red_default = has(ref) ? ref + f->cfg.thr_red_agl_ft : NAN;
    d->acc_default = has(ref) ? ref + f->cfg.accel_agl_ft : NAN;
    d->eo_acc_default = has(ref) ? ref + f->cfg.eo_accel_agl_ft : NAN;
    d->thr_red_pilot = d->acc_pilot = d->eo_acc_pilot = NAN;
    d->trans_alt_db = (apt && apt->found) ? apt->trans_alt_ft : NAN;
    p->version++;
}

static void apply_dest(A380XFms *f, int plan, const A380XFmsAirportInfo *apt)
{
    A380XFmsPlanData *p = &f->plans[plan];
    A380XFmsPerf *d = &p->perf;
    if (apt && apt->found) p->dest = *apt; else airport_clear(&p->dest);
    double ref = (apt && apt->found) ? apt->elevation_ft : NAN;
    d->missed_thr_red_default = has(ref) ? ref + f->cfg.thr_red_agl_ft : NAN;
    d->missed_acc_default = has(ref) ? ref + f->cfg.accel_agl_ft : NAN;
    d->missed_eo_acc_default = has(ref) ? ref + f->cfg.eo_accel_agl_ft : NAN;
    d->missed_thr_red_pilot = d->missed_acc_pilot = d->missed_eo_acc_pilot = NAN;
    d->trans_level_db = (apt && apt->found) ? apt->trans_level_fl : NAN;
    p->version++;
}

/* ------------------------------------------------------------------------------------------ */
/* message resolution callbacks (ctx = A380XFms *)                                             */
/* ------------------------------------------------------------------------------------------ */

static int res_zfw_defined(void *ctx)
{
    const A380XFms *f = ctx;
    return has(ACT(f)->zfw) && has(ACT(f)->zfwcg);
}
static int res_dest_data(void *ctx) { return dest_data_entered(ACT((const A380XFms *)ctx)); }
static int res_vr_set(void *ctx) { return has(ACT((const A380XFms *)ctx)->vr); }

static int vspeeds_valid(const A380XFms *f)
{
    const A380XFmsPerf *d = ACT(f);
    double v1 = d->v1, vr = d->vr, v2 = d->v2;
    int s1 = has(v1) && v1 != 0, sr = has(vr) && vr != 0, s2 = has(v2) && v2 != 0;
    return (s1 && sr ? v1 <= vr : 1) && (sr && s2 ? vr <= v2 : 1) && (s1 && s2 ? v1 <= v2 : 1);
}
static int res_vspeeds_valid(void *ctx) { return vspeeds_valid((const A380XFms *)ctx); }

static int min_fuel_below(const A380XFms *f)
{
    const A380XFmsPerf *d = ACT(f);
    double m = eff_min_dest(d), a = eff_altn_fuel(d), fi = eff_final_fuel(d);
    return has(m) && has(a) && has(fi) && m < a + fi;
}
static int res_min_fuel(void *ctx) { return !min_fuel_below((const A380XFms *)ctx); }
static int res_dest_efob(void *ctx) { return !((const A380XFms *)ctx)->prev_dest_efob_msg; }
static int res_spd_lim(void *ctx) { return !((const A380XFms *)ctx)->speed_limit_exceeded; }
static int res_check_spd_mode(void *ctx)
{
    /* [PORT] spec 02 §10.2: resolved when speed is managed or a preselected speed exists. */
    const A380XFms *f = ctx;
    return f->in.speed_managed || f->presel_spd_var > 0 || f->presel_mach_var > 0;
}

/* ------------------------------------------------------------------------------------------ */
/* flight phase manager (spec 07 §2.1-2.2)                                                     */
/* ------------------------------------------------------------------------------------------ */

static void update_presel(A380XFms *f, double v)
{
    if (isnan(v) || v <= 0) {
        f->presel_spd_var = f->presel_mach_var = -1.0;
    } else if (v < 1.0) {
        f->presel_mach_var = v; f->presel_spd_var = -1.0;
    } else {
        f->presel_spd_var = v; f->presel_mach_var = -1.0;
    }
}

static void activate_presel(A380XFms *f) { f->out.presel_activate_seq++; }

static void nav_request(A380XFms *f, A380XFmsNavRequest r)
{
    f->out.nav_request = r;
    f->out.nav_request_seq++;
}

static void reset_all_plans(A380XFms *f)
{
    if (f->fpln.reset_all) f->fpln.reset_all(f->fpln.plans);
    for (int i = 0; i < A380X_FMS_NUM_PLANS; i++) plan_clear(&f->plans[i], i == A380X_FMS_PLAN_ACTIVE);
    f->plans[A380X_FMS_PLAN_ACTIVE].created_utc_s = now_utc(f);
}

static void init_sim_vars(A380XFms *f)
{
    f->cruise_alt_var = -1.0;
    f->presel_spd_var = f->presel_mach_var = -1.0;
    f->flex_var = 0.0;
    f->out.managed_speed_valid = 0;
    f->out.fg_altitude_constraint_ft = 0.0;
}

static void enter_engine_out(A380XFms *f)
{
    A380XFmsPerf *d = PD(f, A380X_FMS_PLAN_ACTIVE);
    d->presel_clb = d->presel_crz = NAN;
    if (f->fpln.delete_cruise_steps && f->fpln.delete_cruise_steps(f->fpln.plans) > 0) {
        msg_add(f, MFD_MSG_STEP_DELETED, NULL, NULL);
    }
    nav_request(f, A380X_FMS_NAV_PERF);
}

static void on_phase_changed(A380XFms *f, int prev, int next)
{
    A380XFmsPlanData *ap = &f->plans[A380X_FMS_PLAN_ACTIVE];
    A380XFmsPerf *d = &ap->perf;
    f->is_reset = 0;
    switch (next) {
    case A380X_FMS_PHASE_TAKEOFF: {
        f->dest_data_checked_in_cruise = 0;
        int changed = 0;
        if (!(eff_acc(d) != 0 && has(eff_acc(d)))) {
            d->acc_pilot = f->in.baro_alt_ft + f->cfg.accel_agl_ft;
            changed = 1;
        }
        if (!(eff_eo_acc(d) != 0 && has(eff_eo_acc(d)))) {
            d->eo_acc_pilot = f->in.baro_alt_ft + f->cfg.accel_agl_ft;
            changed = 1;
        }
        (void)changed;
        f->data.trip_fuel_at_preflight_kg = nz(trip_kg(f, A380X_FMS_PLAN_ACTIVE));
        d->tow = gw_kg(f, A380X_FMS_PLAN_ACTIVE);
        d->taxi_pilot = NAN;
        d->rsv_pilot = NAN;
        d->rsv_pct_pilot = 0.0;
        f->data.clb_pred_auto = f->in.fcu_alt_valid ? f->in.fcu_selected_alt_ft : NAN;
        if (has(d->presel_clb) && d->presel_clb != 0) update_presel(f, d->presel_clb);
        f->cold_and_dark = 0;
        ap->version++;
        break;
    }
    case A380X_FMS_PHASE_CLIMB:
        f->dest_data_checked_in_cruise = 0;
        if (prev == A380X_FMS_PHASE_TAKEOFF && has(d->presel_clb) && d->presel_clb != 0) activate_presel(f);
        update_presel(f, d->presel_crz);
        if (f->in.fcu_alt_valid && !(has(d->crz_fl) && d->crz_fl != 0)) {
            d->crz_fl = f->in.fcu_selected_alt_ft / 100.0;
            f->cruise_alt_var = f->in.fcu_selected_alt_ft;
            ap->version++;
        }
        break;
    case A380X_FMS_PHASE_CRUISE:
        f->dest_data_checked_in_cruise = 0;
        if (prev == A380X_FMS_PHASE_CLIMB && has(d->presel_crz) && d->presel_crz != 0) activate_presel(f);
        break;
    case A380X_FMS_PHASE_DESCENT:
        mfd_msg_remove_id(&f->msgs, MFD_MSG_NEW_CRZ_ALT); /* quirk 3: remove by id */
        if (!dest_data_entered(d)) msg_add(f, MFD_MSG_ENTER_DEST_DATA, NULL, res_dest_data);
        /* triggerCheckSpeedModeMessage(null) */
        if (!f->in.speed_managed && !mfd_msg_has_id(&f->msgs, MFD_MSG_CHECK_SPD_MODE)) {
            msg_add(f, MFD_MSG_CHECK_SPD_MODE, NULL, res_check_spd_mode);
        }
        update_presel(f, NAN);
        d->crz_fl = NAN;
        ap->version++;
        break;
    case A380X_FMS_PHASE_APPROACH:
        mfd_msg_remove_id(&f->msgs, MFD_MSG_NEW_CRZ_ALT);
        if (!dest_data_entered(d)) msg_add(f, MFD_MSG_ENTER_DEST_DATA, NULL, res_dest_data);
        f->flex_var = 0.0;
        break;
    case A380X_FMS_PHASE_GOAROUND:
        f->goaround_init_speed = f->in.cas_kt;
        if (!(eff_m_acc(d) != 0 && has(eff_m_acc(d)))) d->missed_acc_pilot = f->in.baro_alt_ft + f->cfg.eo_accel_agl_ft;
        if (!(eff_m_eo_acc(d) != 0 && has(eff_m_eo_acc(d))))
            d->missed_eo_acc_pilot = f->in.baro_alt_ft + f->cfg.eo_accel_agl_ft;
        f->flex_var = 0.0;
        ap->version++;
        break;
    case A380X_FMS_PHASE_DONE:
        reset_all_plans(f);
        fmgc_reset(&f->data);
        init_sim_vars(f);
        mfd_msg_clear_latest(&f->msgs);
        a380x_fms_clear_destination_prediction(f);
        f->cold_and_dark = 1;
        nav_request(f, A380X_FMS_NAV_DATA_STATUS); /* [PORT] both MFDs */
        f->is_reset = 1;
        break;
    default:
        break;
    }
}

static void change_phase(A380XFms *f, int next)
{
    if (next == f->phase) return;
    int prev = f->phase;
    /* Phase init() latches (Phase.ts:65-77) using the previous cycle's outputs. */
    if (next == A380X_FMS_PHASE_TAKEOFF) {
        const A380XFmsPerf *d = ACT(f);
        double acc = eff_acc(d), eo = eff_eo_acc(d);
        f->acc_alt_msl = has(acc) ? acc : f->in.baro_alt_ft + f->cfg.accel_agl_ft;
        f->eo_acc_alt_msl = has(eo) ? eo : f->in.baro_alt_ft + f->cfg.accel_agl_ft;
    }
    f->phase = next;
    f->to_timer = f->done_timer = f->appr_ground_timer = 0.0;
    on_phase_changed(f, prev, next);
}

/* canInitiateDes (FPM.ts:29-40). */
static int can_initiate_des(const A380XFms *f)
{
    double d = dist_to_dest(f, A380X_FMS_PLAN_ACTIVE);
    if (isnan(d)) d = -1.0;
    double fl = a380x_js_round(f->in.baro_alt_ft / 100.0);
    double fcu_fl = f->in.fcu_selected_alt_ft / 100.0;
    double crz_fl = f->cruise_alt_var / 100.0;
    if (!(crz_fl > 0)) return 0;
    if ((d < 200 || fl < 200) && fcu_fl < crz_fl && fcu_fl < fl) return 1;
    if (d >= 200 && fl > 200 && fcu_fl <= 200) return 1;
    return 0;
}

static void handle_new_crz_fl(A380XFms *f, double new_fl)
{
    double fl = a380x_js_round(f->in.baro_alt_ft / 100.0);
    if (f->phase == A380X_FMS_PHASE_APPROACH) {
        change_phase(f, A380X_FMS_PHASE_CLIMB);
    } else if (f->phase == A380X_FMS_PHASE_DESCENT && fl < new_fl) {
        change_phase(f, A380X_FMS_PHASE_CLIMB);
    } else if ((f->phase == A380X_FMS_PHASE_CLIMB || f->phase == A380X_FMS_PHASE_DESCENT) && fl > new_fl) {
        change_phase(f, A380X_FMS_PHASE_CRUISE);
    }
}

static void handle_new_destination(A380XFms *f)
{
    if (f->phase != A380X_FMS_PHASE_GOAROUND) return;
    const A380XFmsPerf *d = ACT(f);
    double acc = f->data.engine_out ? eff_m_eo_acc(d) : eff_m_acc(d);
    if (has(acc) && f->in.baro_alt_ft > acc) change_phase(f, A380X_FMS_PHASE_CLIMB);
}

/* onStepClimbDescent (FAI.ts:1315-1343). */
static void step_climb_descent(A380XFms *f)
{
    int clb = f->phase == A380X_FMS_PHASE_CLIMB, crz = f->phase == A380X_FMS_PHASE_CRUISE;
    if (!clb && !crz) return;
    if (!f->in.fcu_alt_valid) return;
    A380XFmsPerf *d = PD(f, A380X_FMS_PLAN_ACTIVE);
    double target = f->in.fcu_selected_alt_ft / 100.0;
    if ((clb && target > nz(d->crz_fl)) || (crz && target != d->crz_fl)) {
        char tok[16];
        snprintf(tok, sizeof tok, "%.0f", f->in.fcu_selected_alt_ft);
        msg_add(f, MFD_MSG_NEW_CRZ_ALT, tok, NULL);
        d->crz_fl = target;
        f->cruise_alt_var = f->in.fcu_selected_alt_ft;
        f->plans[A380X_FMS_PLAN_ACTIVE].version++;
    }
}

static void handle_alt_push_pull(A380XFms *f)
{
    if (f->phase == A380X_FMS_PHASE_TAKEOFF) {
        change_phase(f, A380X_FMS_PHASE_CLIMB);
    } else if ((f->phase == A380X_FMS_PHASE_CLIMB || f->phase == A380X_FMS_PHASE_CRUISE) && can_initiate_des(f)) {
        change_phase(f, A380X_FMS_PHASE_DESCENT);
    }
}

static int vs_or_fpa_negative(const A380XFms *f)
{
    return (f->in.vertical_mode == A380X_FMS_VM_VS && f->in.fcu_vs_fpm < 0) ||
           (f->in.vertical_mode == A380X_FMS_VM_FPA && f->in.fcu_fpa_deg < 0);
}

static void handle_alt_turn(A380XFms *f)
{
    if (f->phase != A380X_FMS_PHASE_CRUISE) return;
    A380XFmsVerticalMode vm = f->in.vertical_mode;
    if ((vm == A380X_FMS_VM_OP_DES || vm == A380X_FMS_VM_DES || vs_or_fpa_negative(f)) && can_initiate_des(f)) {
        change_phase(f, A380X_FMS_PHASE_DESCENT);
    }
}

static void handle_vs_knob_delayed(A380XFms *f)
{
    if ((f->phase == A380X_FMS_PHASE_CLIMB || f->phase == A380X_FMS_PHASE_CRUISE) && vs_or_fpa_negative(f) &&
        can_initiate_des(f)) {
        change_phase(f, A380X_FMS_PHASE_DESCENT);
    } else {
        step_climb_descent(f);
    }
}

static int is_open_or_managed_vmode(A380XFmsVerticalMode vm)
{
    return vm == A380X_FMS_VM_OP_CLB || vm == A380X_FMS_VM_OP_DES || vm == A380X_FMS_VM_CLB || vm == A380X_FMS_VM_DES;
}

static void phase_spawn_init(A380XFms *f)
{
    int target;
    if (f->in.on_ground) target = A380X_FMS_PHASE_PREFLIGHT;
    else if (f->in.baro_alt_ft > 18000.0) target = A380X_FMS_PHASE_CRUISE;
    else if (f->in.flap_lever > 0) target = A380X_FMS_PHASE_APPROACH;
    else target = A380X_FMS_PHASE_CLIMB;
    f->phase = A380X_FMS_PHASE_PREFLIGHT;
    if (target != A380X_FMS_PHASE_PREFLIGHT) change_phase(f, target);
}

/* One 100 ms step of FlightPhaseManager.shouldActivateNextPhase. */
static void phase_step(A380XFms *f, double dt)
{
    const A380XFmsInputs *in = &f->in;
    /* event handling (mode / FCU edges collapsed into one call per step) */
    if (f->pending_push_pull) {
        f->pending_push_pull = 0;
        handle_alt_push_pull(f);
        step_climb_descent(f);
    }
    if (f->pending_alt_turn) {
        f->pending_alt_turn = 0;
        handle_alt_turn(f);
    }
    if (f->vs_knob_pending) {
        f->vs_knob_timer += dt;
        if (f->vs_knob_timer >= 0.1 - 1e-9) {
            f->vs_knob_pending = 0;
            handle_vs_knob_delayed(f);
        }
    }

    /* confirmation timers */
    int to_cond = (in->vertical_mode == A380X_FMS_VM_SRS && in->n1_pct[0] >= 70.0 && in->n1_pct[1] >= 70.0) ||
                  fabs(in->ground_speed_kt) > 90.0;
    f->to_timer = (f->phase == A380X_FMS_PHASE_PREFLIGHT && to_cond) ? f->to_timer + dt : 0.0;
    int done_cond = in->on_ground && !phase_engine_on(f);
    f->done_timer = done_cond ? f->done_timer + dt : 0.0;
    f->appr_ground_timer = in->on_ground ? f->appr_ground_timer + dt : 0.0;

    for (int guard = 0; guard < 8; guard++) {
        int next = -1;
        switch (f->phase) {
        case A380X_FMS_PHASE_PREFLIGHT:
            if (f->to_timer >= 0.2 - 1e-9) next = A380X_FMS_PHASE_TAKEOFF;
            break;
        case A380X_FMS_PHASE_TAKEOFF: {
            int eo = f->data.engine_out;
            if (!eo && in->adr_valid) {
                if (in->baro_alt_ft > f->acc_alt_msl) next = A380X_FMS_PHASE_CLIMB;
            } else if (eo && in->adr_valid && has(f->data.gd_speed) && in->cas_kt >= f->data.gd_speed) {
                if (in->baro_alt_ft > f->eo_acc_alt_msl) next = A380X_FMS_PHASE_CLIMB;
            } else {
                A380XFmsVerticalMode vm = in->vertical_mode;
                if (vm == A380X_FMS_VM_VS || vm == A380X_FMS_VM_FPA || vm == A380X_FMS_VM_ALT ||
                    vm == A380X_FMS_VM_ALT_STAR || vm == A380X_FMS_VM_ALT_CST || vm == A380X_FMS_VM_ALT_CST_STAR ||
                    vm == A380X_FMS_VM_OP_CLB || vm == A380X_FMS_VM_CLB)
                    next = A380X_FMS_PHASE_CLIMB;
            }
            break;
        }
        case A380X_FMS_PHASE_CLIMB: {
            double crz = f->cruise_alt_var / 100.0;
            if (crz > 0 && in->adr_valid && a380x_js_round(in->pressure_alt_ft / 100.0) >= crz)
                next = A380X_FMS_PHASE_CRUISE;
            break;
        }
        case A380X_FMS_PHASE_DESCENT: {
            double crz = f->cruise_alt_var / 100.0;
            double fl = a380x_js_round(in->pressure_alt_ft / 100.0);
            if (in->adr_valid && in->fcu_alt_valid && fl == crz && in->fcu_selected_alt_ft / 100.0 == fl)
                next = A380X_FMS_PHASE_CRUISE;
            else if (in->vnav_decel_reached)
                next = A380X_FMS_PHASE_APPROACH;
            break;
        }
        case A380X_FMS_PHASE_APPROACH:
            if (in->vertical_mode == A380X_FMS_VM_SRS_GA) next = A380X_FMS_PHASE_GOAROUND;
            else if (f->appr_ground_timer >= 30.0 - 1e-9 || !phase_engine_on(f)) next = A380X_FMS_PHASE_DONE;
            break;
        case A380X_FMS_PHASE_DONE:
            if (f->is_reset && in->mfd_on_active_init_fuel_perf) next = A380X_FMS_PHASE_PREFLIGHT;
            break;
        default:
            break;
        }
        if (next < 0 && f->phase != A380X_FMS_PHASE_PREFLIGHT && f->phase != A380X_FMS_PHASE_DONE &&
            f->done_timer >= 30.0 - 1e-9)
            next = A380X_FMS_PHASE_DONE;
        if (next < 0) break;
        change_phase(f, next);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* speeds (spec 07 §5.4-5.5)                                                                   */
/* ------------------------------------------------------------------------------------------ */

static double tower_headwind(const A380XFms *f, int *valid)
{
    const A380XFmsPerf *d = ACT(f);
    A380XFmsRunwayInfo r;
    if (has(d->appr_wind_dir) && has(d->appr_wind_mag) && dest_runway(f, A380X_FMS_PLAN_ACTIVE, &r) &&
        has(r.magnetic_bearing_deg)) {
        if (valid) *valid = 1;
        return a380x_headwind_kt(d->appr_wind_mag, d->appr_wind_dir, r.magnetic_bearing_deg);
    }
    if (valid) *valid = 0;
    return 0.0;
}

static void update_perf_speeds(A380XFms *f)
{
    const A380XFmsPerf *d = ACT(f);
    double gw = gw_kg(f, A380X_FMS_PLAN_ACTIVE);
    if (isnan(gw)) gw = A380X_MAX_ZFW_KG + nz(fob_kg(f, A380X_FMS_PLAN_ACTIVE));
    double cg = coalesce(gw_cg(f), DEFAULT_CG_PCT);
    double lw = lw_kg(f, A380X_FMS_PLAN_ACTIVE);
    if (f->phase >= A380X_FMS_PHASE_APPROACH || isnan(lw)) lw = gw;
    double hw = tower_headwind(f, NULL);
    double v2 = has(d->v2) ? d->v2 : DEFAULT_V2_KT;
    double p = static_pressure(f);

    A380XOperatingSpeeds ap;
    a380x_operating_speeds(lw, cg, 0.0, d->ldg_conf3 ? 3 : 4, A380X_FMS_PHASE_APPROACH, v2,
                           dest_elevation(f, A380X_FMS_PLAN_ACTIVE, NULL), hw, 1, 0.0, p, &ap);
    f->data.appr_vls = ceil(ap.vls);
    f->data.appr_vref = ceil(ap.vref);
    f->data.appr_gd = ceil(ap.gd);
    f->data.appr_s = ceil(ap.s);
    f->data.appr_f = ceil(ap.f2);
    f->data.appr_vapp = has(d->vapp_pilot) ? d->vapp_pilot : ceil(ap.vapp);

    if (f->in.adr_valid) {
        int lever = f->in.flap_lever;
        A380XOperatingSpeeds s;
        a380x_operating_speeds(gw, cg, f->in.cas_kt, lever, f->phase, v2, f->in.pressure_alt_ft, hw, 0,
                               f->in.spoiler_ratio, p, &s);
        f->speed_vs1g = a380x_js_round(s.vs1g);
        f->speed_vls = a380x_js_round(s.vls);
        if (f->phase == A380X_FMS_PHASE_PREFLIGHT) {
            f->data.f_speed = ceil(fmax(s.f2, A380X_VMCL_KT + 5.0));
        } else if (lever == 2) {
            f->data.f_speed = ceil(fmax(s.f2, A380X_VMCL_KT + 15.0));
        } else if (lever == 3) {
            f->data.f_speed = ceil(fmax(s.f3, A380X_VMCL_KT + 10.0));
        }
        f->data.s_speed = ceil(s.s);
        f->data.gd_speed = ceil(s.gd);
        f->speed_vmax = a380x_js_round(s.vmax);
        f->speed_vfen = a380x_js_round(s.vfe_next);
    }
}

/* Managed speed target (spec 07 §5.7). */
static double speed_constraint(const A380XFms *f)
{
    A380XFmsLateralMode lm = f->in.lateral_mode;
    int managed = lm == A380X_FMS_LM_NAV || lm == A380X_FMS_LM_LOC_CPT || lm == A380X_FMS_LM_LOC_TRACK ||
                  lm == A380X_FMS_LM_LAND || lm == A380X_FMS_LM_FLARE || lm == A380X_FMS_LM_ROLLOUT;
    if (!managed || !f->fpln.speed_constraint_kt) return INFINITY;
    double c = f->fpln.speed_constraint_kt(f->fpln.plans, f->phase);
    return (has(c) && c > 0) ? c : INFINITY;
}

static void update_managed_speed(A380XFms *f)
{
    const A380XFmsPerf *d = ACT(f);
    double v = NAN;
    int is_mach = 0;
    double mach = NAN;
    int air_valid = f->in.sat_valid && f->in.static_pressure_valid;
    double p = static_pressure(f);
    double lim_spd, lim_alt;
    switch (f->phase) {
    case A380X_FMS_PHASE_PREFLIGHT:
    case A380X_FMS_PHASE_TAKEOFF:
        v = d->v2;
        break;
    case A380X_FMS_PHASE_CLIMB: {
        double s = a380x_econ_climb_speed(d->ci);
        if (clb_limit(d, &lim_spd, &lim_alt) && f->in.baro_alt_ft < lim_alt) s = fmin(s, lim_spd);
        s = fmin(s, speed_constraint(f));
        if (!all_engines_on(f)) {
            int opclb = f->in.vertical_mode == A380X_FMS_VM_OP_CLB || f->in.vertical_mode == A380X_FMS_VM_CLB;
            if (opclb && has(f->data.gd_speed) && f->data.gd_speed != 0) {
                double cas = (f->in.adr_valid && f->in.cas_kt > 0) ? f->in.cas_kt : NAN;
                s = has(cas) ? cas - (cas - f->data.gd_speed) * FMS_CYCLE_S : f->data.gd_speed;
            }
        }
        mach = a380x_econ_climb_mach();
        is_mach = a380x_managed_targets(s, mach, air_valid, p, &v);
        break;
    }
    case A380X_FMS_PHASE_CRUISE: {
        double s = a380x_econ_cruise_speed();
        if (clb_limit(d, &lim_spd, &lim_alt) && f->in.baro_alt_ft < lim_alt) s = fmin(s, lim_spd);
        mach = a380x_econ_cruise_mach();
        is_mach = a380x_managed_targets(s, mach, air_valid, p, &v);
        break;
    }
    case A380X_FMS_PHASE_DESCENT: {
        /* [PORT] FBW reads the target from VNAV (not ported): use the managed descent law, the
         * descent speed limit below its altitude and the descent speed constraint. */
        double s = eff_des_spd(d);
        if (des_limit(d, &lim_spd, &lim_alt) && f->in.baro_alt_ft < lim_alt) s = fmin(s, lim_spd);
        s = fmin(s, speed_constraint(f));
        mach = eff_des_mach(d);
        is_mach = a380x_managed_targets(s, mach, air_valid, p, &v);
        break;
    }
    case A380X_FMS_PHASE_APPROACH:
        v = f->data.appr_vapp;
        break;
    case A380X_FMS_PHASE_GOAROUND: {
        double s = fmin(has(f->data.gd_speed) ? f->data.gd_speed : INFINITY, speed_constraint(f));
        v = isfinite(s) ? s : NAN;
        break;
    }
    default:
        break;
    }
    f->managed_is_mach = is_mach;
    f->out.managed_speed_valid = has(v) && v > 0;
    f->out.managed_speed_knots = f->out.managed_speed_valid ? v : 0.0;
    f->out.managed_speed_is_mach = is_mach;
    f->out.managed_mach = is_mach ? mach : NAN;
}

/* ------------------------------------------------------------------------------------------ */
/* plan module synchronisation                                                                 */
/* ------------------------------------------------------------------------------------------ */

static void copy_plan_data(A380XFms *f, int dst, int src)
{
    unsigned v = f->plans[dst].version;
    f->plans[dst] = f->plans[src];
    f->plans[dst].version = v + 1;
    f->plans[dst].linked = 0;
}

static void sync_city_pair(A380XFms *f, int plan)
{
    if (!f->fpln.city_pair || !plan_ok(f, plan)) return;
    char o[8] = "", de[8] = "", al[8] = "";
    if (!f->fpln.city_pair(f->fpln.plans, plan, o, de, al)) return;
    o[7] = de[7] = al[7] = '\0';
    A380XFmsPlanData *p = &f->plans[plan];
    A380XFmsAirportInfo apt;
    int found;
    if (strcmp(o, p->origin.found ? p->origin.ident : "") != 0) {
        lookup_airport(f, o, &apt, &found);
        if (!found && o[0]) { airport_clear(&apt); apt.found = 1; scopy(apt.ident, sizeof apt.ident, o); }
        apply_origin(f, plan, o[0] ? &apt : NULL);
    }
    if (strcmp(de, p->dest.found ? p->dest.ident : "") != 0) {
        lookup_airport(f, de, &apt, &found);
        if (!found && de[0]) { airport_clear(&apt); apt.found = 1; scopy(apt.ident, sizeof apt.ident, de); }
        apply_dest(f, plan, de[0] ? &apt : NULL);
        if (plan == A380X_FMS_PLAN_ACTIVE) handle_new_destination(f);
    }
    if (strcmp(al, p->altn.found ? p->altn.ident : "") != 0) {
        lookup_airport(f, al, &apt, &found);
        if (!found && al[0]) { airport_clear(&apt); apt.found = 1; scopy(apt.ident, sizeof apt.ident, al); }
        if (al[0]) p->altn = apt; else airport_clear(&p->altn);
        p->version++;
    }
}

static void sync_plans(A380XFms *f)
{
    if (f->fpln.plan_exists) {
        int lat_tmpy = f->fpln.plan_exists(f->fpln.plans, A380X_FMS_PLAN_TMPY);
        if (lat_tmpy && !f->plans[A380X_FMS_PLAN_TMPY].exists) a380x_fms_tmpy_begin(f);
        else if (!lat_tmpy && f->plans[A380X_FMS_PLAN_TMPY].exists) plan_clear(&f->plans[A380X_FMS_PLAN_TMPY], 0);
        for (int n = 1; n <= 3; n++) {
            int idx = A380X_FMS_SEC_PLAN(n);
            int lat = f->fpln.plan_exists(f->fpln.plans, idx);
            if (lat && !f->plans[idx].exists) {
                plan_clear(&f->plans[idx], 1);
                f->plans[idx].flags = A380X_FMS_PLANF_MANUAL;
                f->plans[idx].created_utc_s = now_utc(f);
            } else if (!lat && f->plans[idx].exists) {
                plan_clear(&f->plans[idx], 0);
            }
        }
    }
    for (int i = 0; i < A380X_FMS_NUM_PLANS; i++) {
        if (!f->plans[i].exists) continue;
        sync_city_pair(f, i);
        if (is_sec(i) && f->fpln.plan_version &&
            f->fpln.plan_version(f->fpln.plans, i) != f->plans[i].lateral_version0)
            f->plans[i].modified = 1;
    }
}

/* Active -> SEC piped keys (spec 07 §2.3 [PORT]). */
static void pipe_linked(A380XFms *f)
{
    const A380XFmsPerf *a = ACT(f);
    for (int n = 1; n <= 3; n++) {
        A380XFmsPlanData *s = &f->plans[A380X_FMS_SEC_PLAN(n)];
        if (!s->exists || !s->linked) continue;
        A380XFmsPerf *d = &s->perf;
        unsigned l = s->linked;
        if (l & LINK_CRZ_FL) d->crz_fl = a->crz_fl;
        if (l & LINK_CRZ_TEMP_ISA) d->crz_temp_isa = a->crz_temp_isa;
        if (l & LINK_CRZ_TEMP) d->crz_temp_pilot = a->crz_temp_pilot;
        if (l & LINK_CI) d->ci = a->ci;
        if (l & LINK_CI_MODE) d->ci_mode = a->ci_mode;
        if (l & LINK_TROPO) d->tropo_pilot = a->tropo_pilot;
        if (l & LINK_RSV) d->rsv_pilot = a->rsv_pilot;
        if (l & LINK_RSV_PCT) d->rsv_pct_pilot = a->rsv_pct_pilot;
        if (l & LINK_FINAL_FUEL) d->final_fuel_pilot = a->final_fuel_pilot;
        if (l & LINK_FINAL_TIME) d->final_time_pilot = a->final_time_pilot;
        if (l & LINK_PAX) d->pax = a->pax;
        if (l & LINK_ZFW) d->zfw = a->zfw;
        if (l & LINK_ZFWCG) d->zfwcg = a->zfwcg;
        if (l & LINK_BLOCK) d->block = a->block;
        if (l & LINK_MIN_DEST) d->min_dest_fob_pilot = a->min_dest_fob_pilot;
    }
}

/* ------------------------------------------------------------------------------------------ */
/* derived data (entries call this immediately; the cycle calls it every 250 ms)               */
/* ------------------------------------------------------------------------------------------ */

static void compute_derived(A380XFms *f)
{
    pipe_linked(f);
    for (int i = 0; i < A380X_FMS_NUM_PLANS; i++) {
        if (!f->plans[i].exists) continue;
        A380XFmsPerf *d = PD(f, i);
        /* CRZ TEMP ISA default ([PORT] spec 03 §3.2: only feeds the computed value). */
        if (has(d->crz_fl)) d->crz_temp_isa = a380x_isa_temp_c(d->crz_fl * 100.0);
        calc_final_altn(f, i);
        calc_tow(f, i);
    }
    /* callsign mirror (updateFmsData) */
    scopy(f->data.atc_callsign, sizeof f->data.atc_callsign, f->plans[A380X_FMS_PLAN_ACTIVE].flight_number);
    /* CLB PRED TO automatic reference */
    if (f->phase == A380X_FMS_PHASE_CLIMB) f->data.clb_pred_auto = f->in.fcu_alt_valid ? f->in.fcu_selected_alt_ft : NAN;
    else if (f->phase > A380X_FMS_PHASE_CLIMB) f->data.clb_pred_auto = NAN;
    update_perf_speeds(f);
}

/* ------------------------------------------------------------------------------------------ */
/* cyclic checks (spec 07 §1.3 steps a-u and 3.4)                                              */
/* ------------------------------------------------------------------------------------------ */

static void check_vspeed_runway(A380XFms *f)
{
    A380XFmsRunwayInfo r;
    if (!origin_runway(f, A380X_FMS_PLAN_ACTIVE, &r) || !r.ident[0]) return;
    if (!f->data.vspeeds_for_runway_set) {
        scopy(f->data.vspeeds_for_runway, sizeof f->data.vspeeds_for_runway, r.ident);
        f->data.vspeeds_for_runway_set = 1;
        return;
    }
    if (strcmp(f->data.vspeeds_for_runway, r.ident) != 0) {
        A380XFmsPerf *d = PD(f, A380X_FMS_PLAN_ACTIVE);
        scopy(f->data.vspeeds_for_runway, sizeof f->data.vspeeds_for_runway, r.ident);
        f->data.v1_tbc = d->v1;
        f->data.vr_tbc = d->vr;
        f->data.v2_tbc = d->v2;
        d->v1 = d->vr = d->v2 = NAN;
        msg_add(f, MFD_MSG_CHECK_TO_DATA, NULL, res_vr_set);
        f->plans[A380X_FMS_PLAN_ACTIVE].version++;
    }
}

static void check_lrc_default(A380XFms *f)
{
    A380XFmsPerf *d = PD(f, A380X_FMS_PLAN_ACTIVE);
    if (f->plans[A380X_FMS_PLAN_ACTIVE].dest.found && isnan(d->ci)) {
        d->ci = 0.0;
        d->ci_mode = A380X_FMS_CI_LRC;
        msg_add(f, MFD_MSG_LRC_IN_USE, NULL, NULL);
        f->plans[A380X_FMS_PLAN_ACTIVE].version++;
    }
}

static void zfw_auto_init(A380XFms *f)
{
    /* FAI.ts:1252-1270, [PORT] ZFW = m_total - m_fuel_total; ZFWCG stays pilot entry (O-1). */
    A380XFmsPerf *d = PD(f, A380X_FMS_PLAN_ACTIVE);
    if (f->cold_and_dark) return;
    if ((has(d->zfw) && d->zfw != 0) || (has(d->zfwcg) && d->zfwcg != 0)) return;
    if (!(f->in.fqms_gw_valid && f->in.fqms_fob_valid)) return;
    double zfw = f->in.fqms_gw_kg - f->in.fqms_fob_kg;
    if (zfw > 0) d->zfw = zfw;
}

static void to_speed_checks(A380XFms *f)
{
    const A380XFmsPerf *d = ACT(f);
    f->to_not_inserted = !(has(d->v1) && d->v1 != 0) || !(has(d->vr) && d->vr != 0) || !(has(d->v2) && d->v2 != 0);
    int disagree = !vspeeds_valid(f);
    if (disagree != f->vspeed_disagree) {
        f->vspeed_disagree = disagree;
        if (disagree) msg_add(f, MFD_MSG_V_TO_DISAGREE, NULL, res_vspeeds_valid);
    }
}

static void check_dest_efob(A380XFms *f, double dt)
{
    double efob = dest_efob_kg(f, A380X_FMS_PLAN_ACTIVE);
    double min = eff_min_dest(ACT(f));
    if (has(efob) && has(min)) {
        if (f->data.dest_efob_below_min) f->data.dest_efob_below_min = (efob - min) / 1000.0 <= 0.3;
        else f->data.dest_efob_below_min = efob < min;
    } else {
        f->data.dest_efob_below_min = 0;
    }
    int alt_in_clb = f->phase == A380X_FMS_PHASE_CLIMB && f->in.vertical_mode == A380X_FMS_VM_ALT;
    f->alt_in_climb_timer = alt_in_clb ? f->alt_in_climb_timer + dt : 0.0;
    int ph = f->phase;
    int cond = f->data.dest_efob_below_min &&
               (ph == A380X_FMS_PHASE_CRUISE || ph == A380X_FMS_PHASE_DESCENT || f->alt_in_climb_timer >= 600.0 ||
                ((ph == A380X_FMS_PHASE_APPROACH || ph == A380X_FMS_PHASE_GOAROUND) &&
                 (f->in.radio_alt_valid ? f->in.radio_alt_ft : 0.0) > 800.0));
    if (cond && !f->prev_dest_efob_msg) {
        f->prev_dest_efob_msg = 1;
        msg_add(f, MFD_MSG_DEST_EFOB_BELOW_MIN, NULL, res_dest_efob);
    }
    f->prev_dest_efob_msg = cond;
}

static void check_engine_out(A380XFms *f)
{
    int flying = f->phase >= A380X_FMS_PHASE_TAKEOFF && f->phase < A380X_FMS_PHASE_DONE;
    int cond = flying && !all_engines_on(f);
    int pulse = cond && !f->prev_flying_not_all_eng;
    f->prev_flying_not_all_eng = cond;
    if (!f->data.engine_out && pulse) {
        f->data.engine_out = 1;
        enter_engine_out(f);
    }
    if (f->data.engine_out && all_engines_on(f)) f->data.engine_out = 0;
}

static void check_speed_limit(A380XFms *f)
{
    double spd, alt;
    int ok;
    const A380XFmsPerf *d = ACT(f);
    switch (f->phase) {
    case A380X_FMS_PHASE_CLIMB:
    case A380X_FMS_PHASE_CRUISE: ok = clb_limit(d, &spd, &alt); break;
    case A380X_FMS_PHASE_DESCENT: ok = des_limit(d, &spd, &alt); break;
    default: ok = 0; break;
    }
    if (!ok) {
        f->speed_limit_exceeded = 0;
        return;
    }
    if (f->speed_limit_exceeded) {
        if (!f->in.adr_valid || f->in.baro_alt_ft > alt || f->in.cas_kt <= spd + 5.0) {
            f->speed_limit_exceeded = 0;
            mfd_msg_remove_id(&f->msgs, MFD_MSG_SPD_LIM_EXCEEDED);
        }
    } else if (f->in.adr_valid && f->in.baro_alt_ft < alt - 150.0 && f->in.cas_kt > spd + 10.0) {
        f->speed_limit_exceeded = 1;
        msg_add(f, MFD_MSG_SPD_LIM_EXCEEDED, NULL, res_spd_lim);
    }
}

/* thrustReductionAccelerationChecks (spec 07 §6.1). */
static void thr_red_acc_reconcile(A380XFms *f)
{
    if (!f->fpln.lowest_climb_constraint_ft) return;
    double lc = round10(f->fpln.lowest_climb_constraint_ft(f->fpln.plans, A380X_FMS_PLAN_ACTIVE));
    if (!isfinite(lc)) return;
    A380XFmsPerf *d = PD(f, A380X_FMS_PLAN_ACTIVE);
    char tok[16];
    double acc = eff_acc(d);
    if (has(acc) && acc > lc) {
        if (has(d->acc_default)) d->acc_default = fmin(d->acc_default, lc);
        if (has(d->acc_pilot)) d->acc_pilot = fmin(d->acc_pilot, lc);
        snprintf(tok, sizeof tok, "%.0f", eff_acc(d));
        msg_add(f, MFD_MSG_NEW_ACC_ALT, tok, NULL);
    }
    double thr = eff_thr_red(d);
    if (has(thr) && thr > lc) {
        if (has(d->thr_red_default)) d->thr_red_default = fmin(d->thr_red_default, lc);
        if (has(d->thr_red_pilot)) d->thr_red_pilot = fmin(d->thr_red_pilot, lc);
        snprintf(tok, sizeof tok, "%.0f", eff_thr_red(d));
        msg_add(f, MFD_MSG_NEW_THR_RED_ALT, tok, NULL);
    }
}

static void check_zfw_params(A380XFms *f)
{
    if (f->in.engine_starting[1] || f->in.engine_starting[2]) {
        if (f->zfw_init_displayed < 1 && f->phase < A380X_FMS_PHASE_TAKEOFF) f->init_msg_settable = 1;
    }
    const A380XFmsPerf *d = ACT(f);
    int undefined = isnan(d->zfw) || isnan(d->zfwcg);
    if (any_engine_on(f) && undefined && f->init_msg_settable) {
        msg_add(f, MFD_MSG_INITIALIZE_ZFW_ZFWCG, NULL, res_zfw_defined);
        f->zfw_init_displayed++;
        f->init_msg_settable = 0;
    }
    if (!undefined && f->prev_zfw_cg_undefined) mfd_msg_remove_id(&f->msgs, MFD_MSG_INITIALIZE_ZFW_ZFWCG);
    f->prev_zfw_cg_undefined = undefined;
}

static void check_dest_data_cruise(A380XFms *f, double dt)
{
    if (f->phase != A380X_FMS_PHASE_CRUISE) {
        f->dest_data_timer = 0.0;
        return;
    }
    f->dest_data_timer += dt;
    if (f->dest_data_timer < 15.0) return;
    f->dest_data_timer = 0.0;
    if (f->dest_data_checked_in_cruise) return;
    double d = dist_to_dest(f, A380X_FMS_PLAN_ACTIVE);
    if (has(d) && d < 180.0) {
        f->dest_data_checked_in_cruise = 1;
        if (!dest_data_entered(ACT(f))) msg_add(f, MFD_MSG_ENTER_DEST_DATA, NULL, res_dest_data);
    }
}

static void check_nav_primary(A380XFms *f)
{
    int gps = f->in.gps_primary != 0;
    if (gps != f->prev_gps_primary) {
        if (gps) {
            mfd_msg_remove_id(&f->msgs, MFD_MSG_NAV_PRIMARY_LOST);
            msg_add(f, MFD_MSG_NAV_PRIMARY, NULL, NULL);
        } else if (f->nav_primary_seen) {
            mfd_msg_remove_id(&f->msgs, MFD_MSG_NAV_PRIMARY);
            msg_add(f, MFD_MSG_NAV_PRIMARY_LOST, NULL, NULL);
        }
        if (gps) f->nav_primary_seen = 1;
        f->prev_gps_primary = gps;
    }
    int cleared = 0;
    unsigned flags = 0;
    if (msg_present_uncleared(f, MFD_MSG_NAV_PRIMARY, &cleared) && !cleared) flags |= 1u << 6;
    if (mfd_msg_has_id(&f->msgs, MFD_MSG_NAV_PRIMARY_LOST)) flags |= 1u << 9;
    f->out.nd_fm_message_flags[0] = f->out.nd_fm_message_flags[1] = flags;
}

static void check_min_fuel(A380XFms *f)
{
    int below = min_fuel_below(f);
    if (below && !f->prev_min_fuel_below) msg_add(f, MFD_MSG_CHECK_MIN_FUEL_AT_DEST, NULL, res_min_fuel);
    f->prev_min_fuel_below = below;
}

/* checkCruiseLevelChangeDueToFcu (spec 07 §6.4).  [PORT] the 3 s confirmation is evaluated while
 * armed (FBW resets the confirm node on the first cycle, so the change never fires upstream). */
static void check_crz_level_fcu(A380XFms *f, double dt)
{
    if (!f->fcu_crz_armed) return;
    A380XFmsPerf *d = PD(f, A380X_FMS_PLAN_ACTIVE);
    int clb = f->phase == A380X_FMS_PHASE_CLIMB, crz = f->phase == A380X_FMS_PHASE_CRUISE;
    double fcu_fl = f->in.fcu_alt_valid ? f->in.fcu_selected_alt_ft / 100.0 : NAN;
    A380XFmsVerticalMode vm = f->in.vertical_mode;
    int suited = vm == A380X_FMS_VM_CLB || vm == A380X_FMS_VM_DES || vm == A380X_FMS_VM_OP_CLB ||
                 vm == A380X_FMS_VM_OP_DES || vm == A380X_FMS_VM_VS || vm == A380X_FMS_VM_FPA;
    if (has(fcu_fl) && ((clb && fcu_fl > nz(d->crz_fl)) || (crz && fcu_fl != d->crz_fl)) && suited) {
        f->fcu_crz_timer += dt;
        if (f->fcu_crz_timer >= 3.0 - 1e-9) {
            char tok[16];
            snprintf(tok, sizeof tok, "%.0f", f->in.fcu_selected_alt_ft);
            msg_add(f, MFD_MSG_NEW_CRZ_ALT, tok, NULL);
            d->crz_fl = fcu_fl;
            f->cruise_alt_var = f->in.fcu_selected_alt_ft;
            f->plans[A380X_FMS_PLAN_ACTIVE].version++;
            f->fcu_crz_armed = 0;
            f->fcu_crz_timer = 0.0;
        }
        return;
    }
    f->fcu_crz_armed = 0;
    f->fcu_crz_timer = 0.0;
}

/* ------------------------------------------------------------------------------------------ */
/* outputs                                                                                     */
/* ------------------------------------------------------------------------------------------ */

static double z_if_nan(double v) { return has(v) ? v : 0.0; }

static void update_outputs(A380XFms *f)
{
    A380XFmsOutputs *o = &f->out;
    const A380XFmsPlanData *ap = &f->plans[A380X_FMS_PLAN_ACTIVE];
    const A380XFmsPerf *d = &ap->perf;
    int ph = f->phase;
    o->valid = 1;
    o->flight_phase = ph;
    o->v1_knots = z_if_nan(d->v1);
    o->vr_knots = z_if_nan(d->vr);
    o->v2_knots = z_if_nan(d->v2);
    o->to_flex_temp_c = f->flex_var;
    o->to_power_setting = d->to_power;
    o->to_flaps = d->to_flaps;
    o->to_ths_for_pct = d->ths_for;
    o->to_speeds_not_inserted = f->to_not_inserted;
    o->vspeeds_disagree = f->vspeed_disagree;
    o->to_speeds_too_low = 0; /* [FBW-PH] disabled (quirk 7) */

    double tr = eff_thr_red(d), ac = eff_acc(d), eo = eff_eo_acc(d);
    double mtr = eff_m_thr_red(d), mac = eff_m_acc(d), meo = eff_m_eo_acc(d);
    o->to_thr_red_alt_ft = z_if_nan(tr); o->to_thr_red_alt_valid = has(tr);
    o->to_acc_alt_ft = z_if_nan(ac); o->to_acc_alt_valid = has(ac);
    o->to_eo_acc_alt_ft = z_if_nan(eo); o->to_eo_acc_alt_valid = has(eo);
    o->missed_thr_red_alt_ft = z_if_nan(mtr); o->missed_thr_red_alt_valid = has(mtr);
    o->missed_acc_alt_ft = z_if_nan(mac); o->missed_acc_alt_valid = has(mac);
    o->missed_eo_acc_alt_ft = z_if_nan(meo); o->missed_eo_acc_alt_valid = has(meo);
    if (ph <= A380X_FMS_PHASE_TAKEOFF) {
        o->thr_red_alt_ft = o->to_thr_red_alt_ft; o->thr_red_alt_valid = o->to_thr_red_alt_valid;
        o->acc_alt_ft = o->to_acc_alt_ft; o->acc_alt_valid = o->to_acc_alt_valid;
        o->eo_acc_alt_ft = o->to_eo_acc_alt_ft; o->eo_acc_alt_valid = o->to_eo_acc_alt_valid;
    } else if (ph == A380X_FMS_PHASE_GOAROUND) {
        o->thr_red_alt_ft = o->missed_thr_red_alt_ft; o->thr_red_alt_valid = o->missed_thr_red_alt_valid;
        o->acc_alt_ft = o->missed_acc_alt_ft; o->acc_alt_valid = o->missed_acc_alt_valid;
        o->eo_acc_alt_ft = o->missed_eo_acc_alt_ft; o->eo_acc_alt_valid = o->missed_eo_acc_alt_valid;
    } else {
        o->thr_red_alt_ft = o->acc_alt_ft = o->eo_acc_alt_ft = 0.0;
        o->thr_red_alt_valid = o->acc_alt_valid = o->eo_acc_alt_valid = 0;
    }
    double ta = eff_trans_alt(d), tl = eff_trans_level(d);
    o->trans_alt_ft = z_if_nan(ta); o->trans_alt_valid = has(ta);
    o->trans_level_fl = z_if_nan(tl); o->trans_level_valid = has(tl);
    o->cruise_altitude_ft = f->cruise_alt_var;

    /* FG altitude constraint (spec 07 §6.3) */
    o->fg_altitude_constraint_ft = 0.0;
    if (f->in.fcu_alt_valid && f->fpln.constraint_altitudes) {
        double ca = NAN, da = NAN;
        if (f->fpln.constraint_altitudes(f->fpln.plans, &ca, &da)) {
            double fcu = f->in.fcu_selected_alt_ft;
            if ((ph < A380X_FMS_PHASE_CRUISE || ph == A380X_FMS_PHASE_GOAROUND) && isfinite(ca) && ca < fcu)
                o->fg_altitude_constraint_ft = ca;
            if (ph > A380X_FMS_PHASE_CRUISE && ph < A380X_FMS_PHASE_GOAROUND && isfinite(da) && da > fcu)
                o->fg_altitude_constraint_ft = da;
        }
    }

    o->presel_speed_knots = f->presel_spd_var;
    o->presel_mach = f->presel_mach_var;
    o->green_dot_knots = z_if_nan(f->data.gd_speed);
    o->f_speed_knots = z_if_nan(f->data.f_speed);
    o->s_speed_knots = z_if_nan(f->data.s_speed);
    o->vls_knots = z_if_nan(f->speed_vls);
    o->vs_knots = z_if_nan(f->speed_vs1g);
    o->vmax_knots = z_if_nan(f->speed_vmax);
    o->vfe_next_knots = z_if_nan(f->speed_vfen);
    o->approach_vapp_knots = z_if_nan(f->data.appr_vapp);
    o->approach_vls_knots = z_if_nan(f->data.appr_vls);
    o->approach_vref_knots = z_if_nan(f->data.appr_vref);
    o->approach_f_knots = z_if_nan(f->data.appr_f);
    o->approach_s_knots = z_if_nan(f->data.appr_s);
    o->approach_gd_knots = z_if_nan(f->data.appr_gd);
    o->landing_conf3 = d->ldg_conf3;
    int hw_valid = 0;
    double hw = tower_headwind(f, &hw_valid);
    o->approach_headwind_knots = hw_valid ? hw : 0.0;
    o->approach_headwind_valid = hw_valid;

    /* minimums (FAI.ts:758-783) */
    double dist = dist_to_dest(f, A380X_FMS_PLAN_ACTIVE);
    int in_range = ph > A380X_FMS_PHASE_CRUISE || (ph == A380X_FMS_PHASE_CRUISE && (isnan(dist) || dist < 250.0));
    o->minimums_in_range = in_range;
    o->mda_ft = has(d->baro_min) ? d->baro_min : -1.0;
    o->dh_ft = has(d->radio_min) ? d->radio_min : -1.0;
    o->mda_valid = in_range && has(d->baro_min);
    o->dh_valid = in_range && !has(d->baro_min) && has(d->radio_min) && d->radio_min > 0;
    o->no_dh = in_range && has(d->radio_min) && d->radio_min == 0;

    int elev_valid = 0;
    double elev = dest_elevation(f, A380X_FMS_PLAN_ACTIVE, &elev_valid);
    o->landing_elevation_ft = elev_valid ? elev : 0.0;
    o->landing_elevation_valid = elev_valid; /* quirk 9 fixed */
    double lon, lat = dest_lat(f, A380X_FMS_PLAN_ACTIVE, &lon);
    o->dest_pos_valid = has(lat) && has(lon);
    o->dest_lat_deg = o->dest_pos_valid ? lat : 0.0;
    o->dest_lon_deg = o->dest_pos_valid ? lon : 0.0;
    o->dest_qnh_hpa = a380x_fms_qnh_to_hpa(d->appr_qnh);

    o->gross_weight_kg = gw_kg(f, A380X_FMS_PLAN_ACTIVE);
    o->zfw_kg = d->zfw;
    o->zfw_cg_pct = d->zfwcg;
    o->pax_number = has(d->pax) ? (int)d->pax : -1;
    o->predictions_available = f->dest_pred.valid;
    o->remaining_flight_time_s = f->dest_pred.valid ? f->dest_pred.seconds : NAN;
    o->dest_efob_below_min = f->data.dest_efob_below_min;
    int cleared = 0;
    o->msg_check_speed_mode = msg_present_uncleared(f, MFD_MSG_CHECK_SPD_MODE, &cleared) && !cleared;
    o->msg_set_hold_speed = 0; /* holding speed logic needs the HM leg data (not ported) */
    o->msg_td_reached = 0;     /* VNAV */
    o->engine_out = f->data.engine_out;
    o->engines_were_started = f->engines_were_started;
    o->cold_and_dark_spawn = f->cold_and_dark;
    o->goaround_init_speed_kt = f->goaround_init_speed;
    o->climb_pred_ref_ft = coalesce(f->data.clb_pred_pilot, f->data.clb_pred_auto);

    scopy(o->flight_number, sizeof o->flight_number, ap->flight_number);
    scopy(o->origin, sizeof o->origin, ap->origin.found ? ap->origin.ident : "");
    scopy(o->destination, sizeof o->destination, ap->dest.found ? ap->dest.ident : "");
    scopy(o->alternate, sizeof o->alternate, ap->altn.found ? ap->altn.ident : "");
    A380XFmsRunwayInfo r;
    scopy(o->dep_runway, sizeof o->dep_runway, origin_runway(f, A380X_FMS_PLAN_ACTIVE, &r) ? r.ident : "");
    scopy(o->ldg_runway, sizeof o->ldg_runway, dest_runway(f, A380X_FMS_PLAN_ACTIVE, &r) ? r.ident : "");
}

/* ------------------------------------------------------------------------------------------ */
/* heavy 250 ms cycle                                                                          */
/* ------------------------------------------------------------------------------------------ */

static void heavy_cycle(A380XFms *f, double dt)
{
    sync_plans(f);
    const A380XFmsPerf *d = ACT(f);
    int ph = f->phase;

    /* a. enginesWereStarted */
    f->engines_were_started = ph >= A380X_FMS_PHASE_TAKEOFF || (ph == A380X_FMS_PHASE_PREFLIGHT && any_engine_on(f));
    if (f->engines_were_started && !f->prev_engines_were_started) check_lrc_default(f);
    f->prev_engines_were_started = f->engines_were_started;

    zfw_auto_init(f);
    /* ZFW mirror subscription: LRC default whenever the Active ZFW changes to a value. */
    double zfw = d->zfw;
    if (has(zfw) && !(zfw == f->prev_active_zfw)) check_lrc_default(f);
    f->prev_active_zfw = zfw;

    check_vspeed_runway(f);
    compute_derived(f);   /* c-g: TOW, ALTN/FINAL, speeds (once per cycle, quirk 20) */
    to_speed_checks(f);   /* h */
    check_dest_efob(f, dt);  /* k, l */
    check_engine_out(f);  /* m */
    check_crz_level_fcu(f, dt); /* t */
    update_managed_speed(f);    /* u */
    check_zfw_params(f);
    check_min_fuel(f);
    check_dest_data_cruise(f, dt);
    check_nav_primary(f);
    mfd_msg_update(&f->msgs);
    check_speed_limit(f);
    thr_red_acc_reconcile(f);
    update_outputs(f);
}

/* ------------------------------------------------------------------------------------------ */
/* lifecycle                                                                                   */
/* ------------------------------------------------------------------------------------------ */

void a380x_fms_set_config(A380XFms *f, const A380XFmsConfig *cfg)
{
    if (cfg) f->cfg = *cfg;
    else a380x_fms_default_config(&f->cfg);
}

void a380x_fms_set_hooks(A380XFms *f, const A380XFmsNavHooks *nav, const A380XFmsPlanHooks *fpln)
{
    if (nav) f->nav = *nav; else memset(&f->nav, 0, sizeof f->nav);
    if (fpln) f->fpln = *fpln; else memset(&f->fpln, 0, sizeof f->fpln);
}

void a380x_fms_init(A380XFms *f, const A380XFmsConfig *cfg, const A380XFmsNavHooks *nav,
                    const A380XFmsPlanHooks *fpln)
{
    memset(f, 0, sizeof *f);
    a380x_fms_set_config(f, cfg);
    a380x_fms_set_hooks(f, nav, fpln);
    mfd_msg_init(&f->msgs);
    for (int i = 0; i < A380X_FMS_NUM_PLANS; i++) plan_clear(&f->plans[i], i == A380X_FMS_PLAN_ACTIVE);
    fmgc_reset(&f->data);
    f->in.utc_seconds = NAN;
    f->in.switching_knob = 1;
    a380x_fms_clear_destination_prediction(f);
    init_sim_vars(f);
    f->phase = A380X_FMS_PHASE_PREFLIGHT;
    f->is_reset = 1;
    f->cold_and_dark = 1;
    f->prev_active_zfw = NAN;
    f->prev_zfw_cg_undefined = 1;
    f->goaround_init_speed = NAN;
    f->speed_vs1g = f->speed_vls = f->speed_vmax = f->speed_vfen = NAN;
    f->out.valid = 0;
    f->out.to_flaps = -1;
    f->out.to_ths_for_pct = NAN;
    f->out.dest_qnh_hpa = f->out.gross_weight_kg = f->out.zfw_kg = f->out.zfw_cg_pct = NAN;
    f->out.remaining_flight_time_s = NAN;
    f->out.goaround_init_speed_kt = NAN;
    f->out.climb_pred_ref_ft = NAN;
    f->out.cruise_altitude_ft = -1.0;
    f->out.presel_speed_knots = f->out.presel_mach = -1.0;
    f->out.dh_ft = f->out.mda_ft = -1.0;
    f->out.pax_number = -1;
    f->out.managed_mach = NAN;
}

void a380x_fms_reset(A380XFms *f)
{
    reset_all_plans(f);
    fmgc_reset(&f->data);
    init_sim_vars(f);
    mfd_msg_reset(&f->msgs); /* [PORT] spec 02 §10.2: reset empties the queue */
    a380x_fms_clear_destination_prediction(f);
    f->speed_limit_exceeded = f->vspeed_disagree = 0;
    f->prev_dest_efob_msg = f->prev_min_fuel_below = 0;
    f->fcu_crz_armed = 0;
    f->prev_active_zfw = NAN;
    nav_request(f, A380X_FMS_NAV_DATA_STATUS);
}

static void update_health(A380XFms *f, int *all_reset, int *all_inop)
{
    int inop[3];
    for (int i = 0; i < 3; i++) {
        inop[i] = !f->in.fmc_powered[i] || f->in.fmc_reset[i] || f->in.fmc_failed[i];
        f->out.fmc_healthy[i] = !inop[i];
    }
    int k = f->in.switching_knob;
    f->out.fms_failed[0] = (k == 0) ? (inop[1] && inop[2]) : (inop[0] && inop[2]);
    f->out.fms_failed[1] = (k == 2) ? (inop[0] && inop[2]) : (inop[1] && inop[2]);
    *all_reset = f->in.fmc_reset[0] && f->in.fmc_reset[1] && f->in.fmc_reset[2]; /* quirk 1 fixed */
    *all_inop = inop[0] && inop[1] && inop[2];
}

void a380x_fms_update(A380XFms *f, const A380XFmsInputs *in, double dt)
{
    if (!in) return;
    double prev_fcu_alt = f->have_inputs ? f->in.fcu_selected_alt_ft : NAN;
    int prev_fcu_valid = f->have_inputs ? f->in.fcu_alt_valid : 0;
    A380XFmsVerticalMode prev_vm = f->have_inputs ? f->in.vertical_mode : A380X_FMS_VM_NONE;
    double prev_vs = f->in.fcu_vs_fpm, prev_fpa = f->in.fcu_fpa_deg;
    f->in = *in;
    if (!(dt > 0)) dt = 0.0;
    f->sim_time_s += dt;

    int all_reset = 0, all_inop = 0;
    update_health(f, &all_reset, &all_inop);
    if (all_reset || all_inop) {
        if (!f->was_reset) {
            a380x_fms_reset(f);
            f->was_reset = 1;
        }
        f->healthy = 0;
        f->out.valid = 0;
        f->have_inputs = 1;
        return;
    }
    f->was_reset = 0;
    f->healthy = 1;

    if (!f->initialized) {
        f->initialized = 1;
        f->cold_and_dark = in->cold_and_dark_spawn != 0;
        f->prev_gps_primary = 0;
        phase_spawn_init(f);
        prev_fcu_alt = in->fcu_selected_alt_ft;
        prev_fcu_valid = in->fcu_alt_valid;
        prev_vm = in->vertical_mode;
        prev_vs = in->fcu_vs_fpm;
        prev_fpa = in->fcu_fpa_deg;
        f->acc_250ms = FMS_CYCLE_S; /* run the heavy block on the first frame */
    }
    f->have_inputs = 1;

    /* FAI subscriptions: selected altitude change, open/managed or VS/FPA mode activation,
     * selected V/S or FPA change (FAI.ts:395-450). */
    if (in->fcu_alt_valid && (!prev_fcu_valid || in->fcu_selected_alt_ft != prev_fcu_alt)) {
        f->pending_alt_turn = 1;
        f->fcu_crz_armed = 1;
    }
    if (is_open_or_managed_vmode(in->vertical_mode) && !is_open_or_managed_vmode(prev_vm)) f->pending_push_pull = 1;
    int vsfpa = in->vertical_mode == A380X_FMS_VM_VS || in->vertical_mode == A380X_FMS_VM_FPA;
    int prev_vsfpa = prev_vm == A380X_FMS_VM_VS || prev_vm == A380X_FMS_VM_FPA;
    if ((vsfpa && !prev_vsfpa) || (vsfpa && (in->fcu_vs_fpm != prev_vs || in->fcu_fpa_deg != prev_fpa))) {
        f->vs_knob_pending = 1;
        f->vs_knob_timer = 0.0;
    }

    f->acc_100ms += dt;
    while (f->acc_100ms >= FMS_STEP_S - 1e-9) {
        f->acc_100ms -= FMS_STEP_S;
        phase_step(f, FMS_STEP_S);
    }
    f->acc_250ms += dt;
    if (f->acc_250ms >= FMS_CYCLE_S - 1e-9) {
        double cdt = f->acc_250ms;
        f->acc_250ms = 0.0;
        heavy_cycle(f, cdt);
    }
}

const A380XFmsOutputs *a380x_fms_outputs(const A380XFms *f) { return &f->out; }
const A380XFmsInputs *a380x_fms_inputs(const A380XFms *f) { return &f->in; }
int a380x_fms_phase(const A380XFms *f) { return f->phase; }
int a380x_fms_engine_out(const A380XFms *f) { return f->data.engine_out; }
int a380x_fms_engines_were_started(const A380XFms *f) { return f->engines_were_started; }

const char *a380x_fms_header_source(const A380XFms *f, int side)
{
    int k = f->in.switching_knob;
    int capt_src = (side == 0) ? (k != 0) : (k == 2);
    if (capt_src) return f->out.fmc_healthy[0] ? "FMS 1" : "FMS 1-C";
    return f->out.fmc_healthy[1] ? "FMS 2" : "FMS 2-C";
}

int a380x_fms_side_failed(const A380XFms *f, int side)
{
    return (side == 0 || side == 1) ? f->out.fms_failed[side] : 0;
}

/* ------------------------------------------------------------------------------------------ */
/* plans                                                                                       */
/* ------------------------------------------------------------------------------------------ */

int a380x_fms_plan_exists(const A380XFms *f, int plan) { return plan_ok(f, plan); }

int a380x_fms_is_active_or_copy(const A380XFms *f, int plan)
{
    if (!plan_valid_index(plan)) return 0;
    if (is_at(plan)) return 1;
    return f->plans[plan].exists && (f->plans[plan].flags & A380X_FMS_PLANF_COPIED_FROM_ACTIVE) != 0;
}

int a380x_fms_has_active_flight_plan(const A380XFms *f)
{
    const A380XFmsPlanData *p = &f->plans[A380X_FMS_PLAN_ACTIVE];
    return p->exists && p->origin.found && p->dest.found;
}

unsigned a380x_fms_plan_version(const A380XFms *f, int plan)
{
    return plan_valid_index(plan) ? f->plans[plan].version : 0;
}

int a380x_fms_tmpy_exists(const A380XFms *f) { return f->plans[A380X_FMS_PLAN_TMPY].exists; }

void a380x_fms_tmpy_begin(A380XFms *f)
{
    if (f->plans[A380X_FMS_PLAN_TMPY].exists) return;
    copy_plan_data(f, A380X_FMS_PLAN_TMPY, A380X_FMS_PLAN_ACTIVE);
    f->plans[A380X_FMS_PLAN_TMPY].flags = 0;
}

int a380x_fms_loaded_plan(A380XFms *f, int uri_plan)
{
    if (uri_plan == A380X_FMS_PLAN_ACTIVE || uri_plan == A380X_FMS_PLAN_TMPY) {
        if (!f->plans[A380X_FMS_PLAN_TMPY].exists && f->fpln.plan_exists &&
            f->fpln.plan_exists(f->fpln.plans, A380X_FMS_PLAN_TMPY))
            a380x_fms_tmpy_begin(f);
        return f->plans[A380X_FMS_PLAN_TMPY].exists ? A380X_FMS_PLAN_TMPY : A380X_FMS_PLAN_ACTIVE;
    }
    if (is_sec(uri_plan)) return f->plans[uri_plan].exists ? uri_plan : -1;
    return -1;
}

int a380x_fms_tmpy_insert(A380XFms *f)
{
    if (!f->plans[A380X_FMS_PLAN_TMPY].exists) return 0;
    if (f->fpln.tmpy_insert && !f->fpln.tmpy_insert(f->fpln.plans)) return 0;
    char old_dest[8];
    scopy(old_dest, sizeof old_dest, f->plans[A380X_FMS_PLAN_ACTIVE].dest.found ? f->plans[0].dest.ident : "");
    unsigned flags = f->plans[A380X_FMS_PLAN_ACTIVE].flags;
    double created = f->plans[A380X_FMS_PLAN_ACTIVE].created_utc_s;
    copy_plan_data(f, A380X_FMS_PLAN_ACTIVE, A380X_FMS_PLAN_TMPY);
    f->plans[A380X_FMS_PLAN_ACTIVE].flags = flags;
    f->plans[A380X_FMS_PLAN_ACTIVE].created_utc_s = created;
    plan_clear(&f->plans[A380X_FMS_PLAN_TMPY], 0);
    if (strcmp(old_dest, f->plans[0].dest.found ? f->plans[0].dest.ident : "") != 0) handle_new_destination(f);
    compute_derived(f);
    return 1;
}

int a380x_fms_tmpy_erase(A380XFms *f)
{
    if (!f->plans[A380X_FMS_PLAN_TMPY].exists) return 0;
    if (f->fpln.tmpy_erase && !f->fpln.tmpy_erase(f->fpln.plans)) return 0;
    plan_clear(&f->plans[A380X_FMS_PLAN_TMPY], 0);
    return 1;
}

int a380x_fms_sec_exists(const A380XFms *f, int n)
{
    return n >= 1 && n <= 3 && f->plans[A380X_FMS_SEC_PLAN(n)].exists;
}

static void sec_created(A380XFms *f, int idx, unsigned flags)
{
    A380XFmsPlanData *p = &f->plans[idx];
    p->exists = 1;
    p->flags = flags;
    p->created_utc_s = now_utc(f);
    p->modified = 0;
    p->lateral_version0 = f->fpln.plan_version ? f->fpln.plan_version(f->fpln.plans, idx) : 0;
    p->version++;
}

int a380x_fms_sec_copy_from_active(A380XFms *f, int n)
{
    if (n < 1 || n > 3) return 0;
    int idx = A380X_FMS_SEC_PLAN(n);
    if (f->fpln.sec_copy_from_active && !f->fpln.sec_copy_from_active(f->fpln.plans, idx)) return 0;
    copy_plan_data(f, idx, A380X_FMS_PLAN_ACTIVE);
    sec_created(f, idx, A380X_FMS_PLANF_COPIED_FROM_ACTIVE);
    unsigned link = LINK_P | (!f->engines_were_started ? LINK_PSTAR : 0u);
    /* pipe() keeps only the most recent subscription per key in the source (spec 07 §2.3). */
    for (int m = 1; m <= 3; m++)
        if (m != n) f->plans[A380X_FMS_SEC_PLAN(m)].linked &= ~link;
    f->plans[idx].linked = link;
    compute_derived(f);
    return 1;
}

int a380x_fms_sec_copy_from_sec(A380XFms *f, int src_n, int n)
{
    if (n < 1 || n > 3 || src_n < 1 || src_n > 3 || src_n == n) return 0;
    int src = A380X_FMS_SEC_PLAN(src_n), idx = A380X_FMS_SEC_PLAN(n);
    if (!f->plans[src].exists) return 0;
    if (f->fpln.sec_copy_from_sec && !f->fpln.sec_copy_from_sec(f->fpln.plans, src, idx)) return 0;
    unsigned flags = f->plans[src].flags;
    copy_plan_data(f, idx, src);
    sec_created(f, idx, flags);
    compute_derived(f);
    return 1;
}

int a380x_fms_sec_init(A380XFms *f, int n)
{
    if (n < 1 || n > 3) return 0;
    int idx = A380X_FMS_SEC_PLAN(n);
    if (f->plans[idx].exists) return 1;
    if (f->fpln.sec_init && !f->fpln.sec_init(f->fpln.plans, idx)) return 0;
    plan_clear(&f->plans[idx], 1);
    sec_created(f, idx, A380X_FMS_PLANF_MANUAL);
    return 1;
}

int a380x_fms_sec_delete(A380XFms *f, int n)
{
    if (n < 1 || n > 3) return 0;
    int idx = A380X_FMS_SEC_PLAN(n);
    if (!f->plans[idx].exists) return 0;
    if (f->fpln.sec_delete && !f->fpln.sec_delete(f->fpln.plans, idx)) return 0;
    plan_clear(&f->plans[idx], 0);
    return 1;
}

int a380x_fms_sec_can_swap(const A380XFms *f, int n)
{
    if (n < 1 || n > 3) return 0;
    int idx = A380X_FMS_SEC_PLAN(n);
    const A380XFmsPlanData *s = &f->plans[idx];
    if (!s->exists || f->plans[A380X_FMS_PLAN_TMPY].exists) return 0;
    if (!s->origin.found || !s->dest.found) return 0;
    A380XFmsLateralMode lm = f->in.lateral_mode;
    int hdg_trk = lm == A380X_FMS_LM_HDG || lm == A380X_FMS_LM_TRK;
    int legs = f->fpln.active_legs_compatible ? f->fpln.active_legs_compatible(f->fpln.plans, idx) : 0;
    return hdg_trk || legs || f->phase == A380X_FMS_PHASE_PREFLIGHT;
}

int a380x_fms_sec_swap(A380XFms *f, int n)
{
    if (!a380x_fms_sec_can_swap(f, n)) return 0;
    int idx = A380X_FMS_SEC_PLAN(n);
    A380XFmsPlanData *a = &f->plans[A380X_FMS_PLAN_ACTIVE], *s = &f->plans[idx];
    double zfw_diff = (has(a->perf.zfw) && has(s->perf.zfw)) ? fabs(a->perf.zfw - s->perf.zfw) / 1000.0 : NAN;
    double cg_diff = (has(a->perf.zfwcg) && has(s->perf.zfwcg)) ? fabs(a->perf.zfwcg - s->perf.zfwcg) : NAN;
    char old_dest[8];
    scopy(old_dest, sizeof old_dest, a->dest.found ? a->dest.ident : "");
    if (f->fpln.sec_swap && !f->fpln.sec_swap(f->fpln.plans, idx)) return 0;

    A380XFmsPlanData tmp = *a;
    unsigned va = a->version, vs = s->version;
    *a = *s;
    *s = tmp;
    a->version = va + 1;
    a->flags = 0;
    a->linked = 0;
    s->version = vs + 1;
    s->flags = A380X_FMS_PLANF_SWAPPED_WITH_ACTIVE;
    s->linked = 0;
    s->created_utc_s = now_utc(f);
    s->modified = 0;
    s->lateral_version0 = f->fpln.plan_version ? f->fpln.plan_version(f->fpln.plans, idx) : 0;
    for (int m = 1; m <= 3; m++) f->plans[A380X_FMS_SEC_PLAN(m)].linked = 0;

    /* persistActivePerfDataAcrossModification (spec 07 §2.3) */
    int before_start = !f->engines_were_started;
    const A380XFmsPerf *old = &s->perf;
    A380XFmsPerf *nw = &a->perf;
    if (!before_start || isnan(nw->zfw)) nw->zfw = old->zfw;
    if (!before_start || isnan(nw->zfwcg)) nw->zfwcg = old->zfwcg;
    if (!before_start || isnan(nw->block)) nw->block = old->block;
    if (isnan(nw->taxi_pilot)) nw->taxi_pilot = old->taxi_pilot;
    if (isnan(nw->ci)) nw->ci = old->ci;
    if (!a->flight_number[0]) scopy(a->flight_number, sizeof a->flight_number, s->flight_number);

    /* onSecondaryActivated (FMC.ts:937-961) */
    if (f->phase == A380X_FMS_PHASE_PREFLIGHT || f->phase == A380X_FMS_PHASE_DONE) {
        msg_add(f, MFD_MSG_CHECK_TO_DATA, NULL, NULL);
        /* [PORT] quirk 19 applied: FLEX var written only when FLEX is selected */
        f->flex_var = nw->to_power == A380X_FMS_TO_FLEX ? (nw->flex_temp == 0 ? 0.1 : nz(nw->flex_temp)) : 0.0;
    }
    if (has(zfw_diff) && zfw_diff > 5.0 && has(cg_diff) && cg_diff > 0.5) msg_add(f, MFD_MSG_CHECK_ZFW, NULL, NULL);
    if (old_dest[0] && strcmp(old_dest, a->dest.found ? a->dest.ident : "") != 0) handle_new_destination(f);
    f->prev_active_zfw = nw->zfw;
    compute_derived(f);
    return 1;
}

void a380x_fms_sec_info(const A380XFms *f, int n, A380XFmsSecInfo *out)
{
    memset(out, 0, sizeof *out);
    out->created_utc_s = NAN;
    if (n < 1 || n > 3) return;
    const A380XFmsPlanData *p = &f->plans[A380X_FMS_SEC_PLAN(n)];
    out->exists = p->exists;
    if (!p->exists) return;
    out->has_city_pair = p->origin.found && p->dest.found;
    if (out->has_city_pair) {
        scopy(out->from, sizeof out->from, p->origin.ident);
        scopy(out->to, sizeof out->to, p->dest.ident);
    }
    out->flags = p->flags;
    out->modified = p->modified;
    out->created_utc_s = p->created_utc_s;
    out->can_swap = a380x_fms_sec_can_swap(f, n);
}

/* ------------------------------------------------------------------------------------------ */
/* field model                                                                                 */
/* ------------------------------------------------------------------------------------------ */

static A380XFmsValue mkval(double v, int entered, int src)
{
    A380XFmsValue r;
    r.has = has(v) ? 1 : 0;
    r.v = r.has ? v : NAN;
    r.entered = r.has ? (unsigned char)(entered ? 1 : 0) : 0;
    r.src = r.has ? (unsigned char)src : (unsigned char)A380X_FMS_SRC_NONE;
    r.colour = A380X_FMS_COLOUR_GREEN;
    return r;
}

static A380XFmsValue pilot_or(double pilot, double other, int other_src)
{
    if (has(pilot)) return mkval(pilot, 1, A380X_FMS_SRC_PILOT);
    return mkval(other, 0, other_src);
}

static A380XFmsValue pilot_only(double v) { return mkval(v, 1, A380X_FMS_SRC_PILOT); }
static A380XFmsValue computed(double v) { return mkval(v, 0, A380X_FMS_SRC_COMPUTED); }

static A380XFmsValue rounded_pilot_or(double pilot, double def, int def_src)
{
    double e = round10(coalesce(pilot, def));
    return mkval(e, has(pilot), has(pilot) ? A380X_FMS_SRC_PILOT : def_src);
}

/* Speeds to be confirmed belong to the active plan (shown on Active/TMPY only). */
static double tbc_or_none(int plan, double v) { return is_at(plan) ? v : NAN; }

A380XFmsValue a380x_fms_get(const A380XFms *f, int plan, A380XFmsField fld)
{
    A380XFmsValue none = mkval(NAN, 0, 0);
    if (fld == A380X_FMS_F_FUEL_PENALTY) {
        double v = f->data.fuel_penalty_pct;
        A380XFmsValue r = mkval(v, has(v) && v > 0, A380X_FMS_SRC_PILOT);
        return r;
    }
    if (!plan_ok(f, plan)) return none;
    const A380XFmsPlanData *p = &f->plans[plan];
    const A380XFmsPerf *d = &p->perf;
    int at = is_at(plan);
    A380XFmsValue r = none;
    switch (fld) {
    /* INIT */
    case A380X_FMS_F_CRZ_FL: r = pilot_only(d->crz_fl); break;
    case A380X_FMS_F_CRZ_TEMP: r = pilot_or(d->crz_temp_pilot, d->crz_temp_isa, A380X_FMS_SRC_COMPUTED); break;
    case A380X_FMS_F_CI_MODE: r = pilot_only((double)d->ci_mode); break;
    case A380X_FMS_F_TROPO: r = rounded_pilot_or(d->tropo_pilot, A380X_DEFAULT_TROPO_FT, A380X_FMS_SRC_DEFAULT); break;
    case A380X_FMS_F_CI: r = pilot_only(d->ci); break;
    case A380X_FMS_F_TRIP_WIND: r = pilot_only(d->trip_wind); break;
    /* FUEL & LOAD */
    case A380X_FMS_F_GW: r = computed(f->engines_were_started ? gw_kg(f, plan) : NAN); break;
    case A380X_FMS_F_CG: {
        double cg = f->engines_were_started ? gw_cg(f) : NAN;
        r = computed((has(cg) && cg != 0) ? cg : NAN);
        break;
    }
    case A380X_FMS_F_FOB: r = computed(f->engines_were_started ? fob_kg(f, plan) : NAN); break;
    case A380X_FMS_F_ZFW: r = pilot_only(d->zfw); break;
    case A380X_FMS_F_ZFWCG: r = pilot_only(d->zfwcg); break;
    case A380X_FMS_F_BLOCK: r = pilot_only(d->block); break;
    case A380X_FMS_F_TAXI: {
        int tr_dis = f->phase >= A380X_FMS_PHASE_TAKEOFF && a380x_fms_is_active_or_copy(f, plan);
        r = tr_dis ? none : pilot_or(d->taxi_pilot, d->taxi_default, A380X_FMS_SRC_DEFAULT);
        break;
    }
    case A380X_FMS_F_TRIP: r = computed(trip_kg(f, plan)); break;
    case A380X_FMS_F_TRIP_TIME:
        r = computed((plan == A380X_FMS_PLAN_ACTIVE && f->dest_pred.valid) ? f->dest_pred.seconds : NAN);
        break;
    case A380X_FMS_F_RTE_RSV: {
        double v = rte_rsv_kg(f, plan, trip_kg(f, plan));
        r = mkval(v, has(d->rsv_pilot), has(d->rsv_pilot) ? A380X_FMS_SRC_PILOT : A380X_FMS_SRC_COMPUTED);
        break;
    }
    case A380X_FMS_F_RTE_RSV_PCT: {
        /* MfdFmsFuelLoad.tsx:246-262; enteredByPilot = !isRouteReserveFuelPilotEntered (FBW) */
        if (has(d->rsv_pct_pilot)) {
            r = mkval(d->rsv_pct_pilot, 1, A380X_FMS_SRC_PILOT);
        } else if (has(d->rsv_pilot)) {
            double trip = trip_kg(f, plan);
            r = mkval((has(trip) && trip != 0) ? d->rsv_pilot / trip * 100.0 : NAN, 0, A380X_FMS_SRC_COMPUTED);
        } else {
            r = mkval(d->rsv_pct_default, 1, A380X_FMS_SRC_DEFAULT);
        }
        break;
    }
    case A380X_FMS_F_ALTN_FUEL: r = pilot_or(d->altn_fuel_pilot, d->altn_fuel_calc, A380X_FMS_SRC_COMPUTED); break;
    case A380X_FMS_F_ALTN_TIME: r = none; break;
    case A380X_FMS_F_FINAL_FUEL: r = pilot_or(d->final_fuel_pilot, d->final_fuel_calc, A380X_FMS_SRC_COMPUTED); break;
    case A380X_FMS_F_FINAL_TIME: {
        if (has(d->final_time_pilot)) r = pilot_only(d->final_time_pilot);
        else if (has(d->final_time_calc)) r = computed(d->final_time_calc);
        else r = mkval(d->final_time_default, 0, A380X_FMS_SRC_DEFAULT);
        break;
    }
    case A380X_FMS_F_PAX: r = pilot_only(d->pax); break;
    case A380X_FMS_F_JTSN_GW: r = none; break;
    case A380X_FMS_F_TOW: r = computed(d->tow); break;
    case A380X_FMS_F_LW: r = computed(lw_kg(f, plan)); break;
    case A380X_FMS_F_DEST_ETA: {
        double v = NAN;
        if (at && f->dest_pred.valid && has(f->dest_pred.seconds)) {
            if (f->phase == A380X_FMS_PHASE_PREFLIGHT) v = f->dest_pred.seconds;
            else if (has(f->in.utc_seconds)) v = fmod(f->in.utc_seconds + f->dest_pred.seconds, 86400.0);
        }
        r = computed(v);
        break;
    }
    case A380X_FMS_F_DEST_EFOB:
        r = computed(dest_efob_kg(f, plan));
        if (r.has && plan == A380X_FMS_PLAN_ACTIVE && f->data.dest_efob_below_min) r.colour = A380X_FMS_COLOUR_AMBER;
        break;
    case A380X_FMS_F_ALTN_ETA: r = none; break;
    case A380X_FMS_F_ALTN_EFOB: {
        double e = dest_efob_kg(f, plan), a = eff_altn_fuel(d);
        r = computed((has(e) && has(a)) ? fmax(e - a, LOWEST_FUEL_KG) : NAN);
        break;
    }
    case A380X_FMS_F_MIN_DEST_FOB:
        r = pilot_or(d->min_dest_fob_pilot, eff_min_dest(d), A380X_FMS_SRC_COMPUTED);
        break;
    case A380X_FMS_F_EXTRA: r = computed(extra_kg(f, plan)); break;
    case A380X_FMS_F_EXTRA_TIME: {
        double e = extra_kg(f, plan);
        r = computed((has(e) && e > 0) ? e / FINAL_BURN_KG_MIN * 60.0 : NAN);
        break;
    }
    /* PERF T.O */
    case A380X_FMS_F_TO_SHIFT: r = pilot_only(d->to_shift_m); break;
    case A380X_FMS_F_V1: r = pilot_only(d->v1); break;
    case A380X_FMS_F_VR: r = pilot_only(d->vr); break;
    case A380X_FMS_F_V2: r = pilot_only(d->v2); break;
    case A380X_FMS_F_V1_TBC: r = computed(tbc_or_none(plan, f->data.v1_tbc)); break;
    case A380X_FMS_F_VR_TBC: r = computed(tbc_or_none(plan, f->data.vr_tbc)); break;
    case A380X_FMS_F_V2_TBC: r = computed(tbc_or_none(plan, f->data.v2_tbc)); break;
    case A380X_FMS_F_TO_F: r = computed(at ? f->data.f_speed : NAN); break;
    case A380X_FMS_F_TO_S: r = computed(at ? f->data.s_speed : NAN); break;
    case A380X_FMS_F_TO_GD: r = computed(at ? f->data.gd_speed : NAN); break;
    case A380X_FMS_F_TO_POWER: r = pilot_only((double)d->to_power); break;
    case A380X_FMS_F_FLEX_TEMP: r = pilot_only(d->flex_temp); break;
    case A380X_FMS_F_DERATED: r = pilot_only((double)d->derated); break;
    case A380X_FMS_F_TO_FLAPS: r = pilot_only(d->to_flaps >= 1 ? (double)d->to_flaps : NAN); break;
    case A380X_FMS_F_THS_FOR: r = pilot_only(d->ths_for); break;
    case A380X_FMS_F_PACKS: r = pilot_only((double)d->packs); break;
    case A380X_FMS_F_ANTI_ICE: r = pilot_only((double)d->anti_ice); break;
    case A380X_FMS_F_THR_RED: r = rounded_pilot_or(d->thr_red_pilot, d->thr_red_default, A380X_FMS_SRC_DEFAULT); break;
    case A380X_FMS_F_ACC: r = rounded_pilot_or(d->acc_pilot, d->acc_default, A380X_FMS_SRC_DEFAULT); break;
    case A380X_FMS_F_EO_ACC: r = rounded_pilot_or(d->eo_acc_pilot, d->eo_acc_default, A380X_FMS_SRC_DEFAULT); break;
    case A380X_FMS_F_TRANS_ALT:
        r = rounded_pilot_or(d->trans_alt_pilot, d->trans_alt_db, A380X_FMS_SRC_DATABASE);
        break;
    case A380X_FMS_F_NOISE: r = pilot_only((double)d->noise_enabled); break;
    case A380X_FMS_F_NOISE_N1: r = pilot_only(d->noise_n1); break;
    case A380X_FMS_F_NOISE_SPD: r = pilot_only(d->noise_speed); break;
    case A380X_FMS_F_NOISE_END: r = pilot_only(d->noise_end_alt); break;
    /* PERF CLB */
    case A380X_FMS_F_CLB_DERATED: r = pilot_only((double)d->climb_derated); break;
    case A380X_FMS_F_CLB_PRED_TO:
        r = at ? pilot_or(f->data.clb_pred_pilot, f->data.clb_pred_auto, A380X_FMS_SRC_COMPUTED) : none;
        break;
    case A380X_FMS_F_PRESEL_CLB: r = pilot_only(d->presel_clb); break;
    case A380X_FMS_F_CLB_ECON_SPD: r = computed(a380x_econ_climb_speed(d->ci)); break;
    case A380X_FMS_F_CLB_ECON_MACH: r = computed(a380x_econ_climb_mach()); break;
    case A380X_FMS_F_CLB_SPD_LIM:
    case A380X_FMS_F_CLB_SPD_LIM_ALT:
    case A380X_FMS_F_CLB_SPD_LIM_MACH: {
        double s, a;
        if (clb_limit(d, &s, &a)) {
            double v = fld == A380X_FMS_F_CLB_SPD_LIM ? s : fld == A380X_FMS_F_CLB_SPD_LIM_ALT ? a
                       : a380x_cas_to_mach(s, static_pressure(f));
            r = mkval(v, d->clb_lim_pilot, d->clb_lim_pilot ? A380X_FMS_SRC_PILOT : A380X_FMS_SRC_DEFAULT);
        }
        break;
    }
    /* PERF CRZ */
    case A380X_FMS_F_PRESEL_CRZ_MACH: r = pilot_only((has(d->presel_crz) && d->presel_crz < 1.0) ? d->presel_crz : NAN); break;
    case A380X_FMS_F_PRESEL_CRZ_SPD: r = pilot_only((has(d->presel_crz) && d->presel_crz > 1.0) ? d->presel_crz : NAN); break;
    case A380X_FMS_F_CRZ_ECON_SPD: r = computed(a380x_econ_cruise_speed()); break;
    case A380X_FMS_F_CRZ_ECON_MACH: r = computed(a380x_econ_cruise_mach()); break;
    case A380X_FMS_F_REC_MAX_FL: r = computed(rec_max_fl(f, plan)); break;
    case A380X_FMS_F_OPT_FL: {
        double m = rec_max_fl(f, plan);
        r = computed((has(m) && !f->data.engine_out && f->phase <= A380X_FMS_PHASE_CRUISE) ? floor(0.96 * m / 5.0) * 5.0 : NAN);
        break;
    }
    case A380X_FMS_F_EO_MAX_FL: {
        double m = rec_max_fl(f, plan);
        r = computed(has(m) ? floor(0.80 * m / 5.0) * 5.0 : NAN);
        break;
    }
    /* PERF DES */
    case A380X_FMS_F_DES_CABIN_RATE: r = pilot_only(d->des_cabin_rate); break;
    case A380X_FMS_F_DES_MANAGED_MACH:
        r = mkval(eff_des_mach(d), 1, has(d->des_mach_pilot) ? A380X_FMS_SRC_PILOT : A380X_FMS_SRC_COMPUTED);
        break;
    case A380X_FMS_F_DES_MANAGED_SPD:
        r = mkval(eff_des_spd(d), 1, has(d->des_spd_pilot) ? A380X_FMS_SRC_PILOT : A380X_FMS_SRC_COMPUTED);
        break;
    case A380X_FMS_F_DES_SPD_LIM:
    case A380X_FMS_F_DES_SPD_LIM_ALT: {
        double s, a;
        if (des_limit(d, &s, &a)) r = mkval(fld == A380X_FMS_F_DES_SPD_LIM ? s : a, d->des_lim_pilot,
                                            d->des_lim_pilot ? A380X_FMS_SRC_PILOT : A380X_FMS_SRC_DEFAULT);
        break;
    }
    /* PERF APPR */
    case A380X_FMS_F_APPR_WIND_DIR: r = pilot_only(d->appr_wind_dir); break;
    case A380X_FMS_F_APPR_WIND_SPD: r = pilot_only(d->appr_wind_mag); break;
    case A380X_FMS_F_APPR_HEADWIND:
    case A380X_FMS_F_APPR_CROSSWIND: {
        A380XFmsRunwayInfo rw;
        if (has(d->appr_wind_dir) && has(d->appr_wind_mag) && dest_runway(f, plan, &rw) && has(rw.magnetic_bearing_deg)) {
            double v = fld == A380X_FMS_F_APPR_HEADWIND
                           ? a380x_headwind_kt(d->appr_wind_mag, d->appr_wind_dir, rw.magnetic_bearing_deg)
                           : fabs(a380x_crosswind_kt(d->appr_wind_mag, d->appr_wind_dir, rw.magnetic_bearing_deg));
            r = computed(v);
        }
        break;
    }
    case A380X_FMS_F_APPR_TEMP: r = pilot_only(d->appr_temp); break;
    case A380X_FMS_F_APPR_QNH: r = pilot_only(d->appr_qnh); break;
    case A380X_FMS_F_APPR_BARO: r = pilot_only(d->baro_min); break;
    case A380X_FMS_F_APPR_RADIO: r = pilot_only(d->radio_min); break;
    case A380X_FMS_F_APPR_GD: r = computed(at ? f->data.appr_gd : NAN); break;
    case A380X_FMS_F_APPR_S: r = computed(at ? f->data.appr_s : NAN); break;
    case A380X_FMS_F_APPR_F: r = computed(at ? f->data.appr_f : NAN); break;
    case A380X_FMS_F_APPR_VREF: r = computed(at ? f->data.appr_vref : NAN); break;
    case A380X_FMS_F_APPR_VLS: r = computed(at ? f->data.appr_vls : NAN); break;
    case A380X_FMS_F_LDG_CONF: r = pilot_only(d->ldg_conf3 ? (double)A380X_FMS_LDG_CONF3 : (double)A380X_FMS_LDG_FULL); break;
    case A380X_FMS_F_VAPP:
        r = at ? pilot_or(d->vapp_pilot, f->data.appr_vapp, A380X_FMS_SRC_COMPUTED) : pilot_only(d->vapp_pilot);
        break;
    case A380X_FMS_F_TRANS_FL: {
        double v = coalesce(d->trans_level_pilot, d->trans_level_db);
        v = has(v) ? a380x_js_round(v) : NAN;
        r = mkval(v, has(d->trans_level_pilot), has(d->trans_level_pilot) ? A380X_FMS_SRC_PILOT : A380X_FMS_SRC_DATABASE);
        break;
    }
    case A380X_FMS_F_VERT_DEV: r = none; break;
    case A380X_FMS_F_DEST_DIST: r = computed(dist_to_dest(f, plan)); break;
    /* PERF GA */
    case A380X_FMS_F_GA_THR_RED:
        r = rounded_pilot_or(d->missed_thr_red_pilot, d->missed_thr_red_default, A380X_FMS_SRC_DEFAULT);
        break;
    case A380X_FMS_F_GA_ACC:
        r = rounded_pilot_or(d->missed_acc_pilot, d->missed_acc_default, A380X_FMS_SRC_DEFAULT);
        break;
    case A380X_FMS_F_GA_EO_ACC:
        r = rounded_pilot_or(d->missed_eo_acc_pilot, d->missed_eo_acc_default, A380X_FMS_SRC_DEFAULT);
        break;
    case A380X_FMS_F_GA_TRANS: {
        double tl = eff_trans_level(d);
        r = computed(has(tl) ? tl * 100.0 : NAN);
        break;
    }
    case A380X_FMS_F_ALTN_CRZ_FL: {
        double dd = (p->dest.found && p->altn.found)
                        ? a380x_fms_great_circle_nm(p->dest.lat_deg, p->dest.lon_deg, p->altn.lat_deg, p->altn.lon_deg)
                        : NAN;
        r = computed(isnan(dd) ? NAN : (dd > 300.0 ? 310.0 : dd > 150.0 ? 220.0 : 100.0));
        break;
    }
    default:
        break;
    }
    if (is_sec(plan) && r.colour == A380X_FMS_COLOUR_GREEN) r.colour = A380X_FMS_COLOUR_WHITE;
    return r;
}

/* ------------------------------------------------------------------------------------------ */
/* field flags (spec 03 §3.2/§4.2, 04 §5-10, 13.2)                                             */
/* ------------------------------------------------------------------------------------------ */

static int from_set(const A380XFmsPlanData *p) { return p->pending_from[0] || p->origin.found; }
static int to_set(const A380XFmsPlanData *p) { return p->pending_to[0] || p->dest.found; }

static int is_readonly(A380XFmsField fld)
{
    switch (fld) {
    case A380X_FMS_F_GW: case A380X_FMS_F_CG: case A380X_FMS_F_FOB: case A380X_FMS_F_TRIP:
    case A380X_FMS_F_TRIP_TIME: case A380X_FMS_F_ALTN_TIME: case A380X_FMS_F_JTSN_GW: case A380X_FMS_F_TOW:
    case A380X_FMS_F_LW: case A380X_FMS_F_DEST_ETA: case A380X_FMS_F_DEST_EFOB: case A380X_FMS_F_ALTN_ETA:
    case A380X_FMS_F_ALTN_EFOB: case A380X_FMS_F_EXTRA: case A380X_FMS_F_EXTRA_TIME:
    case A380X_FMS_F_V1_TBC: case A380X_FMS_F_VR_TBC: case A380X_FMS_F_V2_TBC:
    case A380X_FMS_F_TO_F: case A380X_FMS_F_TO_S: case A380X_FMS_F_TO_GD:
    case A380X_FMS_F_CLB_ECON_SPD: case A380X_FMS_F_CLB_ECON_MACH: case A380X_FMS_F_CLB_SPD_LIM:
    case A380X_FMS_F_CLB_SPD_LIM_ALT: case A380X_FMS_F_CLB_SPD_LIM_MACH:
    case A380X_FMS_F_CRZ_ECON_SPD: case A380X_FMS_F_CRZ_ECON_MACH: case A380X_FMS_F_REC_MAX_FL:
    case A380X_FMS_F_OPT_FL: case A380X_FMS_F_EO_MAX_FL: case A380X_FMS_F_DES_SPD_LIM:
    case A380X_FMS_F_DES_SPD_LIM_ALT: case A380X_FMS_F_APPR_HEADWIND: case A380X_FMS_F_APPR_CROSSWIND:
    case A380X_FMS_F_APPR_GD: case A380X_FMS_F_APPR_S: case A380X_FMS_F_APPR_F: case A380X_FMS_F_APPR_VREF:
    case A380X_FMS_F_APPR_VLS: case A380X_FMS_F_VERT_DEV: case A380X_FMS_F_DEST_DIST: case A380X_FMS_F_GA_TRANS:
    case A380X_FMS_F_ALTN_CRZ_FL:
        return 1;
    default:
        return 0;
    }
}

unsigned a380x_fms_field_flags(const A380XFms *f, int plan, A380XFmsField fld, A380XFmsPage page)
{
    if (fld < 0 || fld >= A380X_FMS_F_COUNT) return A380X_FMS_FF_DISABLED | A380X_FMS_FF_READONLY;
    if (fld == A380X_FMS_F_FUEL_PENALTY) return A380X_FMS_FF_CLEARABLE;
    unsigned fl = 0;
    if (is_readonly(fld)) fl |= A380X_FMS_FF_READONLY;
    if (!plan_ok(f, plan)) return fl | A380X_FMS_FF_DISABLED;
    if (plan == A380X_FMS_PLAN_TMPY) fl |= A380X_FMS_FF_TMPY;
    if (is_sec(plan)) fl |= A380X_FMS_FF_SEC;

    const A380XFmsPlanData *p = &f->plans[plan];
    const A380XFmsPerf *d = &p->perf;
    int ph = f->phase;
    int at = is_at(plan);
    int aoc = a380x_fms_is_active_or_copy(f, plan);
    int tmpy = plan == A380X_FMS_PLAN_TMPY;
    int to_inactive = aoc && ph >= A380X_FMS_PHASE_TAKEOFF;
    int at_after_clb = aoc && ph >= A380X_FMS_PHASE_CLIMB;
    int clb_inactive = aoc && ph > A380X_FMS_PHASE_CLIMB;
    int crz_inactive = aoc && ph > A380X_FMS_PHASE_CRUISE;
    int not_yet_clb = !aoc || ph < A380X_FMS_PHASE_CLIMB;
    int not_yet_crz = !aoc || ph < A380X_FMS_PHASE_CRUISE;
    int not_yet_des = !aoc || ph < A380X_FMS_PHASE_DESCENT;
    int tr_dis = aoc && ph >= A380X_FMS_PHASE_TAKEOFF;
    int no_from_to = !from_set(p) || !to_set(p);
    int mode_disabled;
    switch (page) {
    case A380X_FMS_PAGE_INIT: mode_disabled = no_from_to || (ph >= A380X_FMS_PHASE_DESCENT && aoc); break;
    case A380X_FMS_PAGE_FUEL_LOAD: mode_disabled = (aoc && ph >= A380X_FMS_PHASE_DESCENT) || !p->dest.found; break;
    default: mode_disabled = aoc && ph >= A380X_FMS_PHASE_DESCENT; break;
    }
    double dist = dist_to_dest(f, plan);
    int appr_mand = at && (isnan(dist) ? 0.0 : dist) <= 180.0;
    A380XFmsApproachInfo ai;
    approach_info(f, plan, &ai);

    switch (fld) {
    case A380X_FMS_F_CRZ_FL:
        if (tmpy || no_from_to) fl |= A380X_FMS_FF_DISABLED;
        if (ph < A380X_FMS_PHASE_DESCENT && at) fl |= A380X_FMS_FF_MANDATORY;
        break;
    case A380X_FMS_F_CRZ_TEMP:
        if (isnan(d->crz_fl)) fl |= A380X_FMS_FF_DISABLED;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_CI_MODE:
        if (mode_disabled) fl |= A380X_FMS_FF_DISABLED;
        break;
    case A380X_FMS_F_TROPO: fl |= A380X_FMS_FF_CLEARABLE; break;
    case A380X_FMS_F_CI:
        if (mode_disabled || d->ci_mode == A380X_FMS_CI_LRC) fl |= A380X_FMS_FF_DISABLED;
        if (page == A380X_FMS_PAGE_PERF_DES && crz_inactive) fl |= A380X_FMS_FF_INACTIVE;
        if (at) fl |= A380X_FMS_FF_MANDATORY;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_TRIP_WIND:
        if (no_from_to) fl |= A380X_FMS_FF_DISABLED;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_ZFW:
    case A380X_FMS_F_ZFWCG:
        if (at) fl |= A380X_FMS_FF_MANDATORY;
        break;
    case A380X_FMS_F_BLOCK:
        if (at) fl |= A380X_FMS_FF_MANDATORY;
        if (f->engines_were_started) fl |= A380X_FMS_FF_HIDDEN;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_TAXI:
    case A380X_FMS_F_RTE_RSV:
    case A380X_FMS_F_RTE_RSV_PCT:
        if (tr_dis) fl |= A380X_FMS_FF_DISABLED;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_ALTN_FUEL:
        if (!p->altn.found) fl |= A380X_FMS_FF_DISABLED;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_FINAL_FUEL:
    case A380X_FMS_F_FINAL_TIME:
    case A380X_FMS_F_MIN_DEST_FOB:
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_PAX:
        if (at) fl |= A380X_FMS_FF_MANDATORY;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_JTSN_GW:
        fl |= A380X_FMS_FF_DISABLED | A380X_FMS_FF_NA;
        if (!at) fl |= A380X_FMS_FF_HIDDEN;
        break;
    case A380X_FMS_F_ALTN_TIME:
    case A380X_FMS_F_ALTN_ETA:
        fl |= A380X_FMS_FF_NA;
        break;
    case A380X_FMS_F_TO_SHIFT: {
        A380XFmsRunwayInfo rw;
        if (!origin_runway(f, plan, &rw)) fl |= A380X_FMS_FF_DISABLED;
        if (to_inactive) fl |= A380X_FMS_FF_INACTIVE;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    }
    case A380X_FMS_F_V1:
    case A380X_FMS_F_VR:
    case A380X_FMS_F_V2:
        if (to_inactive) fl |= A380X_FMS_FF_INACTIVE;
        else if (at) fl |= A380X_FMS_FF_MANDATORY;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_TO_F:
    case A380X_FMS_F_TO_S:
    case A380X_FMS_F_TO_GD:
        if (a380x_fms_should_show_confirm_to_speeds(f, plan)) fl |= A380X_FMS_FF_HIDDEN;
        break;
    case A380X_FMS_F_V1_TBC:
    case A380X_FMS_F_VR_TBC:
    case A380X_FMS_F_V2_TBC:
        if (!a380x_fms_should_show_confirm_to_speeds(f, plan)) fl |= A380X_FMS_FF_HIDDEN;
        break;
    case A380X_FMS_F_TO_POWER:
        if (ph >= A380X_FMS_PHASE_TAKEOFF) fl |= A380X_FMS_FF_DISABLED;
        break;
    case A380X_FMS_F_FLEX_TEMP:
        if (d->to_power != A380X_FMS_TO_FLEX) fl |= A380X_FMS_FF_HIDDEN;
        if (to_inactive) fl |= A380X_FMS_FF_INACTIVE;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_DERATED:
        if (d->to_power != A380X_FMS_TO_DERATED) fl |= A380X_FMS_FF_HIDDEN;
        if (to_inactive) fl |= A380X_FMS_FF_INACTIVE;
        break;
    case A380X_FMS_F_TO_FLAPS:
    case A380X_FMS_F_PACKS:
    case A380X_FMS_F_ANTI_ICE:
        if (to_inactive) fl |= A380X_FMS_FF_INACTIVE;
        break;
    case A380X_FMS_F_THS_FOR:
        if (to_inactive) fl |= A380X_FMS_FF_INACTIVE;
        else if (at) fl |= A380X_FMS_FF_MANDATORY;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_THR_RED:
    case A380X_FMS_F_ACC:
    case A380X_FMS_F_NOISE_N1:
    case A380X_FMS_F_NOISE_SPD:
    case A380X_FMS_F_NOISE_END:
        if (page == A380X_FMS_PAGE_PERF_CLB) {
            if (at_after_clb) fl |= A380X_FMS_FF_INACTIVE | A380X_FMS_FF_HIDDEN; /* [PORT] spec 04 §17.1 */
        } else if (page == A380X_FMS_PAGE_ANY) {
            if (at_after_clb) fl |= A380X_FMS_FF_INACTIVE;
        } else if (to_inactive) {
            fl |= A380X_FMS_FF_INACTIVE;
        }
        if ((fld == A380X_FMS_F_NOISE_N1 || fld == A380X_FMS_F_NOISE_SPD || fld == A380X_FMS_F_NOISE_END) &&
            !d->noise_enabled)
            fl |= A380X_FMS_FF_HIDDEN;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_EO_ACC:
        if (to_inactive) fl |= A380X_FMS_FF_INACTIVE;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_TRANS_ALT:
    case A380X_FMS_F_TRANS_FL:
    case A380X_FMS_F_VAPP:
    case A380X_FMS_F_GA_THR_RED:
    case A380X_FMS_F_GA_ACC:
    case A380X_FMS_F_GA_EO_ACC:
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_NOISE:
        /* The NOISE button is always disabled (FBW-PH); CANCEL NOISE uses a380x_fms_cancel_noise(). */
        fl |= A380X_FMS_FF_DISABLED;
        if (to_inactive) fl |= A380X_FMS_FF_HIDDEN;
        break;
    case A380X_FMS_F_CLB_DERATED:
        fl |= A380X_FMS_FF_INACTIVE;
        break;
    case A380X_FMS_F_CLB_PRED_TO:
        if (!at) fl |= A380X_FMS_FF_HIDDEN;
        if (clb_inactive) fl |= A380X_FMS_FF_INACTIVE;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_PRESEL_CLB:
        if (at_after_clb) fl |= A380X_FMS_FF_INACTIVE;
        if (!not_yet_clb) fl |= A380X_FMS_FF_HIDDEN;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_PRESEL_CRZ_MACH:
    case A380X_FMS_F_PRESEL_CRZ_SPD:
        if (!not_yet_crz) fl |= A380X_FMS_FF_HIDDEN;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_DES_CABIN_RATE:
        if (!at) fl |= A380X_FMS_FF_HIDDEN;
        if (crz_inactive) fl |= A380X_FMS_FF_INACTIVE;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_DES_MANAGED_MACH:
        if (not_yet_des) fl |= A380X_FMS_FF_HIDDEN;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_DES_MANAGED_SPD:
        if (!not_yet_des && page != A380X_FMS_PAGE_ANY) fl |= A380X_FMS_FF_INACTIVE; /* static in DES */
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_APPR_WIND_DIR:
    case A380X_FMS_F_APPR_WIND_SPD:
        if (!p->dest.found) fl |= A380X_FMS_FF_DISABLED;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_APPR_TEMP:
    case A380X_FMS_F_APPR_QNH:
        if (!p->dest.found) fl |= A380X_FMS_FF_DISABLED;
        if (appr_mand) fl |= A380X_FMS_FF_MANDATORY;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_APPR_BARO:
        fl |= A380X_FMS_FF_CLEARABLE; /* spec 04 §9.2: always editable */
        break;
    case A380X_FMS_F_APPR_RADIO:
        if (!(ai.valid && (ai.type == A380X_FMS_APPR_ILS || ai.type == A380X_FMS_APPR_GLS))) fl |= A380X_FMS_FF_HIDDEN;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_F_APPR_VREF:
        if (!at) fl |= A380X_FMS_FF_HIDDEN;
        break;
    case A380X_FMS_F_VERT_DEV:
        if (!at) fl |= A380X_FMS_FF_HIDDEN;
        fl |= A380X_FMS_FF_NA;
        break;
    default:
        break;
    }
    return fl;
}

int a380x_fms_option_enabled(const A380XFms *f, int plan, A380XFmsField fld, int option)
{
    (void)plan;
    if (fld == A380X_FMS_F_TO_POWER) {
        if (option == A380X_FMS_TO_DERATED) return 0; /* [FBW-PH] */
        return f->phase < A380X_FMS_PHASE_TAKEOFF;
    }
    return 1;
}

/* ------------------------------------------------------------------------------------------ */
/* format descriptors                                                                          */
/* ------------------------------------------------------------------------------------------ */

static void fmt_set(MfdFmt *o, MfdFmtKind k, double lo, double hi)
{
    o->kind = k;
    o->min = lo;
    o->max = hi;
}

void a380x_fms_field_fmt(const A380XFms *f, int plan, A380XFmsField fld, MfdFmt *o)
{
    memset(o, 0, sizeof *o);
    o->kind = MFD_FMT_COUNT;
    o->metric = f->cfg.metric_units ? 1 : 0;
    o->qnh_is_hpa = 1;
    o->err_details = 1;
    const A380XFmsPerf *d = plan_ok(f, plan) ? CPD(f, plan) : NULL;
    double trans_alt = d ? eff_trans_alt(d) : NAN;
    double trans_fl = d ? eff_trans_level(d) : NAN;
    switch (fld) {
    case A380X_FMS_F_CRZ_FL: fmt_set(o, MFD_FMT_FLIGHT_LEVEL, 0, 430); break;
    case A380X_FMS_F_CRZ_TEMP: fmt_set(o, MFD_FMT_CRZ_TEMP, -99, 99); break;
    case A380X_FMS_F_TROPO: fmt_set(o, MFD_FMT_TROPO, 1000, 60000); break;
    case A380X_FMS_F_CI: fmt_set(o, MFD_FMT_COST_INDEX, 0, 999); break;
    case A380X_FMS_F_TRIP_WIND: fmt_set(o, MFD_FMT_TRIP_WIND, -250, 250); break;
    case A380X_FMS_F_ZFW: fmt_set(o, MFD_FMT_WEIGHT, A380X_MIN_ZFW_KG, A380X_MAX_ZFW_KG); break;
    case A380X_FMS_F_ZFWCG: fmt_set(o, MFD_FMT_PERCENT, A380X_MIN_ZFWCG, A380X_MAX_ZFWCG); break;
    case A380X_FMS_F_BLOCK: fmt_set(o, MFD_FMT_WEIGHT, 0, A380X_MAX_BLOCK_KG); break;
    case A380X_FMS_F_TAXI: fmt_set(o, MFD_FMT_WEIGHT, 0, A380X_MAX_TAXI_KG); break;
    case A380X_FMS_F_RTE_RSV: fmt_set(o, MFD_FMT_WEIGHT, 0, 50000); break; /* AMI rsvMin/rsvMax */
    case A380X_FMS_F_RTE_RSV_PCT: fmt_set(o, MFD_FMT_PERCENT, 0, A380X_MAX_RTE_RSV_PCT); break;
    case A380X_FMS_F_ALTN_FUEL: fmt_set(o, MFD_FMT_WEIGHT, 0, A380X_MAX_ALTN_FUEL_KG); break;
    case A380X_FMS_F_FINAL_FUEL: fmt_set(o, MFD_FMT_WEIGHT, 0, A380X_MAX_FINAL_FUEL_KG); break;
    case A380X_FMS_F_FINAL_TIME: fmt_set(o, MFD_FMT_TIME_HHMM, 0, 90); break;
    case A380X_FMS_F_PAX: fmt_set(o, MFD_FMT_PAX_NBR, 0, 999); break;
    case A380X_FMS_F_JTSN_GW: fmt_set(o, MFD_FMT_WEIGHT, 0, A380X_MAX_JTSN_GW_KG); break;
    case A380X_FMS_F_MIN_DEST_FOB: fmt_set(o, MFD_FMT_WEIGHT, 0, A380X_MAX_MIN_DEST_KG); break;
    case A380X_FMS_F_TO_SHIFT: {
        A380XFmsRunwayInfo rw;
        double len = (d && origin_runway(f, plan, &rw) && has(rw.length_m)) ? rw.length_m : 0.0;
        fmt_set(o, MFD_FMT_LENGTH, 1, len);
        break;
    }
    case A380X_FMS_F_V1:
    case A380X_FMS_F_VR:
    case A380X_FMS_F_V2:
    case A380X_FMS_F_NOISE_SPD:
    case A380X_FMS_F_PRESEL_CLB:
    case A380X_FMS_F_PRESEL_CRZ_SPD:
    case A380X_FMS_F_DES_MANAGED_SPD:
    case A380X_FMS_F_VAPP:
        fmt_set(o, MFD_FMT_SPEED_KT, 90, 340);
        break;
    case A380X_FMS_F_FLEX_TEMP: fmt_set(o, MFD_FMT_TEMPERATURE, 0, 99); break;
    case A380X_FMS_F_THS_FOR: fmt_set(o, MFD_FMT_PERCENT, 0, 99.9); break;
    case A380X_FMS_F_THR_RED:
    case A380X_FMS_F_ACC:
    case A380X_FMS_F_EO_ACC:
    case A380X_FMS_F_NOISE_END:
    case A380X_FMS_F_CLB_PRED_TO:
        fmt_set(o, MFD_FMT_ALT_OR_FL, 0, A380X_MAX_CERT_ALT_FT);
        o->has_trans = has(trans_alt);
        o->trans = has(trans_alt) ? trans_alt : 0.0;
        break;
    case A380X_FMS_F_GA_THR_RED:
    case A380X_FMS_F_GA_ACC:
    case A380X_FMS_F_GA_EO_ACC:
        fmt_set(o, MFD_FMT_ALT_OR_FL, 0, A380X_MAX_CERT_ALT_FT);
        o->has_trans = has(trans_fl);
        o->trans = has(trans_fl) ? trans_fl : 0.0;
        o->trans_is_fl = 1;
        break;
    case A380X_FMS_F_TRANS_ALT: fmt_set(o, MFD_FMT_ALTITUDE, 1, A380X_MAX_CERT_ALT_FT); break;
    case A380X_FMS_F_NOISE_N1: fmt_set(o, MFD_FMT_PERCENT, 40, 110); break;
    case A380X_FMS_F_PRESEL_CRZ_MACH:
    case A380X_FMS_F_DES_MANAGED_MACH:
        fmt_set(o, MFD_FMT_SPEED_MACH, 0.1, 0.89);
        break;
    case A380X_FMS_F_DES_CABIN_RATE: fmt_set(o, MFD_FMT_DESCENT_RATE, -999, -100); break;
    case A380X_FMS_F_APPR_WIND_DIR: fmt_set(o, MFD_FMT_WIND_DIR, 0, 359); break;
    case A380X_FMS_F_APPR_WIND_SPD: fmt_set(o, MFD_FMT_WIND_SPEED, 0, 250); break;
    case A380X_FMS_F_APPR_TEMP: fmt_set(o, MFD_FMT_TEMPERATURE, -99, 99); break;
    case A380X_FMS_F_APPR_QNH: fmt_set(o, MFD_FMT_QNH, 0, 0); break;
    case A380X_FMS_F_APPR_BARO: fmt_set(o, MFD_FMT_ALTITUDE, 0, A380X_MAX_CERT_ALT_FT); break;
    case A380X_FMS_F_APPR_RADIO: fmt_set(o, MFD_FMT_RADIO_ALT, 1, A380X_MAX_CERT_ALT_FT); break;
    case A380X_FMS_F_TRANS_FL: fmt_set(o, MFD_FMT_FLIGHT_LEVEL, 0, 430); break;
    case A380X_FMS_F_FUEL_PENALTY:
        fmt_set(o, MFD_FMT_FUEL_PENALTY, 0, 999.9);
        o->err_details = 0; /* MfdFmsDataStatus.tsx:154 drops the details */
        break;
    default:
        break; /* dropdowns, radios and read-only items */
    }
}

/* ------------------------------------------------------------------------------------------ */
/* entries                                                                                     */
/* ------------------------------------------------------------------------------------------ */

static int range_err(MfdFmsError *e, double lo, double hi, const char *unit)
{
    char buf[MFD_ERR_DETAILS_LEN];
    snprintf(buf, sizeof buf, "RNG: %g TO %g%s%s", lo, hi, unit && unit[0] ? " " : "", unit ? unit : "");
    return err_set(e, MFD_ERR_ENTRY_OUT_OF_RANGE, buf);
}

/* Defensive range check of numeric entries (the UI format normally rejects these already). */
static int check_static_range(const A380XFms *f, int plan, A380XFmsField fld, double v, MfdFmsError *err)
{
    MfdFmt fmt;
    a380x_fms_field_fmt(f, plan, fld, &fmt);
    switch (fmt.kind) {
    case MFD_FMT_COUNT:
    case MFD_FMT_QNH:
    case MFD_FMT_RADIO_ALT:
        return 1;
    case MFD_FMT_LENGTH:
        if (fmt.max > 0 && (v < fmt.min || v > fmt.max)) return err_set(err, MFD_ERR_ENTRY_OUT_OF_RANGE, "");
        return 1;
    default:
        if (v < fmt.min - 1e-9 || v > fmt.max + 1e-9) return range_err(err, fmt.min, fmt.max, "");
        return 1;
    }
}

/* trySetCruiseFl (FMC.ts:1804-1849). */
static int try_set_cruise_fl(A380XFms *f, int plan, double fl, MfdFmsError *err)
{
    if (!plan_ok(f, plan)) return 0;
    if (!isfinite(fl)) return err_set(err, MFD_ERR_FORMAT_ERROR, "");
    if (fl == 0) return 0; /* MfdFmsInit/PERF: `v ? ... : false` */
    if (fl <= 0 || fl > A380X_MAX_CERT_ALT_FT / 100.0) return err_set(err, MFD_ERR_ENTRY_OUT_OF_RANGE, "");
    double insert = fl;
    int new_crz_msg = 0;
    int active = plan == A380X_FMS_PLAN_ACTIVE;
    if (active && f->phase >= A380X_FMS_PHASE_CLIMB && f->phase < A380X_FMS_PHASE_DONE) {
        double fcu_fl = floor(fmax(0.0, f->in.fcu_alt_valid ? f->in.fcu_selected_alt_ft : 0.0) / 100.0);
        if (fl < fcu_fl) {
            insert = fcu_fl;
            new_crz_msg = f->phase == A380X_FMS_PHASE_CRUISE || f->phase == A380X_FMS_PHASE_DESCENT ||
                          f->phase == A380X_FMS_PHASE_APPROACH;
        }
    }
    PD(f, plan)->crz_fl = insert;
    PD(f, plan)->crz_temp_isa = a380x_isa_temp_c(insert * 100.0);
    double max_fl = rec_max_fl(f, plan);
    if (has(max_fl) && insert > max_fl) msg_add(f, MFD_MSG_CRZ_FL_ABOVE_MAX_FL, NULL, NULL);
    if (active) {
        f->cruise_alt_var = insert * 100.0;
        handle_new_crz_fl(f, insert);
        if (new_crz_msg) {
            char tok[16];
            snprintf(tok, sizeof tok, "%.0f", insert * 100.0);
            msg_add(f, MFD_MSG_NEW_CRZ_ALT, tok, NULL);
        }
    }
    return 1;
}

static void unlink_key(A380XFms *f, int plan, unsigned key)
{
    if (is_sec(plan)) f->plans[plan].linked &= ~key;
}

static void write_flex_var(A380XFms *f, int plan)
{
    if (plan != A380X_FMS_PLAN_ACTIVE) return; /* spec 04 §17.5 */
    const A380XFmsPerf *d = ACT(f);
    if (d->to_power == A380X_FMS_TO_FLEX) f->flex_var = d->flex_temp == 0 ? 0.1 : nz(d->flex_temp);
    else f->flex_var = 0.0;
}

/* Apply one entry (clear == 1: CLR, v ignored). */
static int apply_entry(A380XFms *f, int plan, A380XFmsField fld, double v, int clear, MfdFmsError *err)
{
    A380XFmsPlanData *p = &f->plans[plan];
    A380XFmsPerf *d = &p->perf;
    double x = clear ? NAN : v;
    if (!clear && !check_static_range(f, plan, fld, v, err)) return 0;
    switch (fld) {
    case A380X_FMS_F_CRZ_FL:
        if (clear) return 0;
        if (!try_set_cruise_fl(f, plan, v, err)) return 0;
        unlink_key(f, plan, LINK_CRZ_FL | LINK_CRZ_TEMP_ISA);
        return 1;
    case A380X_FMS_F_CRZ_TEMP: d->crz_temp_pilot = x; unlink_key(f, plan, LINK_CRZ_TEMP); return 1;
    case A380X_FMS_F_CI_MODE:
        if (clear || (v != A380X_FMS_CI_LRC && v != A380X_FMS_CI_ECON)) return 0;
        d->ci_mode = (int)v; unlink_key(f, plan, LINK_CI_MODE); return 1;
    case A380X_FMS_F_TROPO: d->tropo_pilot = x; unlink_key(f, plan, LINK_TROPO); return 1;
    case A380X_FMS_F_CI: d->ci = has(x) ? floor(x) : NAN; unlink_key(f, plan, LINK_CI); return 1;
    case A380X_FMS_F_TRIP_WIND: d->trip_wind = x; return 1;
    case A380X_FMS_F_ZFW:
        if (clear) return 0;
        d->zfw = v;
        unlink_key(f, plan, LINK_ZFW);
        if (plan == A380X_FMS_PLAN_ACTIVE) mfd_msg_remove_id(&f->msgs, MFD_MSG_CHECK_ZFW);
        return 1;
    case A380X_FMS_F_ZFWCG:
        if (clear) return 0;
        d->zfwcg = v; unlink_key(f, plan, LINK_ZFWCG); return 1;
    case A380X_FMS_F_BLOCK: d->block = x; unlink_key(f, plan, LINK_BLOCK); return 1;
    case A380X_FMS_F_TAXI: d->taxi_pilot = x; return 1;
    case A380X_FMS_F_RTE_RSV:
        d->rsv_pilot = x;
        if (!clear) d->rsv_pct_pilot = NAN;
        unlink_key(f, plan, LINK_RSV | LINK_RSV_PCT);
        return 1;
    case A380X_FMS_F_RTE_RSV_PCT:
        if (!clear) d->rsv_pilot = NAN;
        d->rsv_pct_pilot = x;
        unlink_key(f, plan, LINK_RSV | LINK_RSV_PCT);
        return 1;
    case A380X_FMS_F_ALTN_FUEL: d->altn_fuel_pilot = x; return 1;
    case A380X_FMS_F_FINAL_FUEL:
        d->final_fuel_pilot = x;
        if (!clear) d->final_time_pilot = NAN;
        unlink_key(f, plan, LINK_FINAL_FUEL | LINK_FINAL_TIME);
        return 1;
    case A380X_FMS_F_FINAL_TIME:
        if (!clear) d->final_fuel_pilot = NAN;
        d->final_time_pilot = x;
        unlink_key(f, plan, LINK_FINAL_FUEL | LINK_FINAL_TIME);
        return 1;
    case A380X_FMS_F_PAX:
        if (clear) return 0; /* FBW: paxNumber only set if not null */
        d->pax = floor(v); unlink_key(f, plan, LINK_PAX); return 1;
    case A380X_FMS_F_MIN_DEST_FOB: d->min_dest_fob_pilot = x; unlink_key(f, plan, LINK_MIN_DEST); return 1;
    case A380X_FMS_F_TO_SHIFT: {
        if (!clear) {
            A380XFmsRunwayInfo rw;
            if (!origin_runway(f, plan, &rw)) return err_set(err, MFD_ERR_NOT_ALLOWED, "");
            if (v < 1 || (has(rw.length_m) && v > rw.length_m)) return err_set(err, MFD_ERR_ENTRY_OUT_OF_RANGE, "");
        }
        d->to_shift_m = x;
        return 1;
    }
    case A380X_FMS_F_V1: d->v1 = x; return 1;
    case A380X_FMS_F_VR: d->vr = x; return 1;
    case A380X_FMS_F_V2: d->v2 = x; return 1;
    case A380X_FMS_F_TO_POWER:
        if (clear || (v != A380X_FMS_TO_TOGA && v != A380X_FMS_TO_FLEX)) return err_set(err, clear ? MFD_ERR_NONE : MFD_ERR_NOT_ALLOWED, "");
        d->to_power = (int)v;
        write_flex_var(f, plan);
        return 1;
    case A380X_FMS_F_FLEX_TEMP:
        d->flex_temp = x;
        write_flex_var(f, plan); /* quirk 19: only while FLEX is selected */
        return 1;
    case A380X_FMS_F_DERATED:
        if (clear || v < 0 || v > 4) return 0;
        d->derated = (int)v;
        return 1;
    case A380X_FMS_F_TO_FLAPS:
        if (clear || v < 1 || v > 3) return 0;
        d->to_flaps = (int)v;
        return 1;
    case A380X_FMS_F_THS_FOR:
        if (clear || v == 0) return 0; /* FBW `if (v)` */
        d->ths_for = v;
        return 1;
    case A380X_FMS_F_PACKS:
    case A380X_FMS_F_ANTI_ICE: {
        if (clear) return 0;
        int max = fld == A380X_FMS_F_PACKS ? 1 : 2;
        if (v < 0 || v > max) return 0;
        if (f->engines_were_started) msg_add(f, MFD_MSG_CHECK_TO_DATA, NULL, NULL);
        if (fld == A380X_FMS_F_PACKS) d->packs = (int)v; else d->anti_ice = (int)v;
        return 1;
    }
    case A380X_FMS_F_THR_RED: {
        if (!clear) {
            double elev = p->origin.found ? p->origin.elevation_ft : NAN;
            if (has(elev) && v < elev + 400.0) return err_set(err, MFD_ERR_ENTRY_OUT_OF_RANGE, ""); /* [PORT] */
            double acc = eff_acc(d);
            if (has(acc) && v > acc) d->acc_pilot = v; /* [PORT] ACC >= THR RED */
        }
        d->thr_red_pilot = x;
        return 1;
    }
    case A380X_FMS_F_ACC: {
        if (!clear) {
            double elev = p->origin.found ? p->origin.elevation_ft : NAN;
            if (has(elev) && v < elev + 400.0) return err_set(err, MFD_ERR_ENTRY_OUT_OF_RANGE, "");
            double thr = eff_thr_red(d);
            if (has(thr) && v < thr) return err_set(err, MFD_ERR_ENTRY_OUT_OF_RANGE, "");
        }
        d->acc_pilot = x;
        return 1;
    }
    case A380X_FMS_F_EO_ACC: d->eo_acc_pilot = x; return 1;
    case A380X_FMS_F_TRANS_ALT: d->trans_alt_pilot = x; return 1;
    case A380X_FMS_F_NOISE:
        if (clear || v != 0) return err_set(err, clear ? MFD_ERR_NONE : MFD_ERR_NOT_ALLOWED, "");
        d->noise_enabled = 0;
        return 1;
    case A380X_FMS_F_NOISE_N1: d->noise_n1 = x; return 1;
    case A380X_FMS_F_NOISE_SPD: d->noise_speed = x; return 1;
    case A380X_FMS_F_NOISE_END: d->noise_end_alt = x; return 1;
    case A380X_FMS_F_CLB_PRED_TO: f->data.clb_pred_pilot = x; return 1;
    case A380X_FMS_F_PRESEL_CLB: d->presel_clb = x; return 1;
    case A380X_FMS_F_PRESEL_CRZ_MACH:
    case A380X_FMS_F_PRESEL_CRZ_SPD:
        d->presel_crz = x;
        return 1;
    case A380X_FMS_F_DES_CABIN_RATE:
        d->des_cabin_rate = has(x) ? x : -350.0;
        return 1;
    case A380X_FMS_F_DES_MANAGED_MACH: d->des_mach_pilot = x; return 1;
    case A380X_FMS_F_DES_MANAGED_SPD: d->des_spd_pilot = x; return 1;
    case A380X_FMS_F_APPR_WIND_DIR: d->appr_wind_dir = x; d->appr_wind_pilot = 1; return 1;
    case A380X_FMS_F_APPR_WIND_SPD: d->appr_wind_mag = x; d->appr_wind_pilot = 1; return 1;
    case A380X_FMS_F_APPR_TEMP: d->appr_temp = x; return 1;
    case A380X_FMS_F_APPR_QNH:
        if (clear) return 0; /* FBW: null is ignored */
        d->appr_qnh = v;
        return 1;
    case A380X_FMS_F_APPR_BARO: d->baro_min = x; return 1;
    case A380X_FMS_F_APPR_RADIO: d->radio_min = x; return 1;
    case A380X_FMS_F_LDG_CONF:
        if (clear || (v != A380X_FMS_LDG_CONF3 && v != A380X_FMS_LDG_FULL)) return 0;
        d->ldg_conf3 = v == A380X_FMS_LDG_CONF3;
        return 1;
    case A380X_FMS_F_VAPP: d->vapp_pilot = x; return 1;
    case A380X_FMS_F_TRANS_FL: d->trans_level_pilot = x; return 1;
    case A380X_FMS_F_GA_THR_RED: d->missed_thr_red_pilot = x; return 1;
    case A380X_FMS_F_GA_ACC: d->missed_acc_pilot = x; return 1;
    case A380X_FMS_F_GA_EO_ACC: d->missed_eo_acc_pilot = x; return 1;
    default:
        return err_set(err, MFD_ERR_NOT_ALLOWED, "");
    }
}

static int do_entry(A380XFms *f, int plan, A380XFmsField fld, double v, int clear, MfdFmsError *err)
{
    err_clear(err);
    if (fld < 0 || fld >= A380X_FMS_F_COUNT) return err_set(err, MFD_ERR_NOT_ALLOWED, "");
    if (fld == A380X_FMS_F_FUEL_PENALTY) {
        if (!clear && (v < 0 || v > 999.9)) return err_set(err, MFD_ERR_ENTRY_OUT_OF_RANGE, "");
        f->data.fuel_penalty_pct = clear ? NAN : v;
        return 1;
    }
    if (!plan_ok(f, plan)) return err_set(err, MFD_ERR_NOT_ALLOWED, "");
    unsigned fl = a380x_fms_field_flags(f, plan, fld, A380X_FMS_PAGE_ANY);
    if (fl & (A380X_FMS_FF_READONLY | A380X_FMS_FF_DISABLED | A380X_FMS_FF_INACTIVE | A380X_FMS_FF_HIDDEN))
        return err_set(err, MFD_ERR_NOT_ALLOWED, "");
    if (clear && !(fl & A380X_FMS_FF_CLEARABLE)) return 0;
    if (!clear && isnan(v)) return err_set(err, MFD_ERR_FORMAT_ERROR, "");
    if (!apply_entry(f, plan, fld, v, clear, err)) return 0;
    f->plans[plan].version++;
    compute_derived(f);
    return 1;
}

int a380x_fms_enter(A380XFms *f, int plan, A380XFmsField fld, double value, MfdFmsError *err)
{
    return do_entry(f, plan, fld, value, 0, err);
}

int a380x_fms_clear(A380XFms *f, int plan, A380XFmsField fld, MfdFmsError *err)
{
    return do_entry(f, plan, fld, NAN, 1, err);
}

/* ------------------------------------------------------------------------------------------ */
/* text items                                                                                  */
/* ------------------------------------------------------------------------------------------ */

int a380x_fms_get_text(const A380XFms *f, int plan, A380XFmsText t, char *buf, size_t n)
{
    if (!buf || n == 0) return 0;
    buf[0] = '\0';
    if (t == A380X_FMS_T_CALLSIGN) {
        scopy(buf, n, f->data.atc_callsign);
        return buf[0] != '\0';
    }
    if (!plan_ok(f, plan)) return 0;
    const A380XFmsPlanData *p = &f->plans[plan];
    A380XFmsRunwayInfo rw;
    A380XFmsApproachInfo ai;
    switch (t) {
    case A380X_FMS_T_FLT_NBR: scopy(buf, n, p->flight_number); break;
    case A380X_FMS_T_FROM: scopy(buf, n, p->pending_from[0] ? p->pending_from : (p->origin.found ? p->origin.ident : "")); break;
    case A380X_FMS_T_TO: scopy(buf, n, p->pending_to[0] ? p->pending_to : (p->dest.found ? p->dest.ident : "")); break;
    case A380X_FMS_T_ALTN:
        if (p->altn.found) scopy(buf, n, p->altn.ident);
        else if (p->origin.found && p->dest.found) scopy(buf, n, "NONE");
        break;
    case A380X_FMS_T_CPNY_RTE:
    case A380X_FMS_T_ALTN_RTE:
        if (p->origin.found && p->dest.found) scopy(buf, n, "NONE");
        break;
    case A380X_FMS_T_ORIGIN_RWY: if (origin_runway(f, plan, &rw)) scopy(buf, n, rw.ident); break;
    case A380X_FMS_T_DEST_RWY: if (dest_runway(f, plan, &rw)) scopy(buf, n, rw.ident); break;
    case A380X_FMS_T_APPROACH: if (approach_info(f, plan, &ai)) scopy(buf, n, ai.name); break;
    case A380X_FMS_T_SEC_CREATED: {
        if (!is_sec(plan) || isnan(p->created_utc_s)) break;
        int mins = (int)floor(fmod(p->created_utc_s, 86400.0) / 60.0);
        const char *src = "";
        if (p->flags & A380X_FMS_PLANF_COPIED_FROM_ACTIVE) src = " (IMPORT ACTIVE";
        else if (p->flags & A380X_FMS_PLANF_SWAPPED_WITH_ACTIVE) src = " (SWAP ACTIVE";
        else if (p->flags & A380X_FMS_PLANF_MANUAL) src = " (MANUAL";
        else if (p->flags & A380X_FMS_PLANF_CPNY) src = " (CPNY F-PLN";
        else if (p->flags & A380X_FMS_PLANF_ATC) src = " (ATC F-PLN";
        int manual = (p->flags & A380X_FMS_PLANF_MANUAL) && !(p->flags & (A380X_FMS_PLANF_COPIED_FROM_ACTIVE |
                                                                          A380X_FMS_PLANF_SWAPPED_WITH_ACTIVE));
        snprintf(buf, n, "CREATED" NBSP NBSP "%02d%02d%s%s%s", mins / 60, mins % 60, src,
                 (src[0] && p->modified && !manual) ? "-MODIFIED" : "", src[0] ? ")" : "");
        break;
    }
    default:
        break;
    }
    return buf[0] != '\0';
}

unsigned a380x_fms_text_flags(const A380XFms *f, int plan, A380XFmsText t, A380XFmsPage page)
{
    (void)page;
    unsigned fl = 0;
    if (!plan_ok(f, plan)) return A380X_FMS_FF_DISABLED;
    if (plan == A380X_FMS_PLAN_TMPY) fl |= A380X_FMS_FF_TMPY;
    if (is_sec(plan)) fl |= A380X_FMS_FF_SEC;
    const A380XFmsPlanData *p = &f->plans[plan];
    int at = is_at(plan), tmpy = plan == A380X_FMS_PLAN_TMPY;
    int no_from_to = !from_set(p) || !to_set(p);
    switch (t) {
    case A380X_FMS_T_FLT_NBR:
        if (at) fl |= A380X_FMS_FF_MANDATORY;
        break;
    case A380X_FMS_T_FROM:
    case A380X_FMS_T_TO:
        if ((f->phase > A380X_FMS_PHASE_PREFLIGHT && a380x_fms_is_active_or_copy(f, plan)) || tmpy)
            fl |= A380X_FMS_FF_DISABLED;
        if (at) fl |= A380X_FMS_FF_MANDATORY;
        break;
    case A380X_FMS_T_ALTN:
        if (tmpy || no_from_to) fl |= A380X_FMS_FF_DISABLED;
        if (at) fl |= A380X_FMS_FF_MANDATORY;
        fl |= A380X_FMS_FF_CLEARABLE;
        break;
    case A380X_FMS_T_CPNY_RTE:
        fl |= A380X_FMS_FF_DISABLED;
        if (no_from_to && at) fl |= A380X_FMS_FF_MANDATORY;
        break;
    case A380X_FMS_T_ALTN_RTE:
        fl |= A380X_FMS_FF_DISABLED;
        break;
    default:
        fl |= A380X_FMS_FF_READONLY;
        break;
    }
    return fl;
}

static int valid_icao(const char *s)
{
    if (!s || strlen(s) != 4) return 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    return 1;
}

/* newCityPair (spec 07 §2.3, [PORT] spec 03 §3.7). */
static int new_city_pair(A380XFms *f, int plan, const A380XFmsAirportInfo *from, const A380XFmsAirportInfo *to,
                         MfdFmsError *err)
{
    A380XFmsPlanData *p = &f->plans[plan];
    A380XFmsAirportInfo altn = p->altn;
    const char *altn_id = altn.found ? altn.ident : NULL;
    if (f->fpln.new_city_pair && !f->fpln.new_city_pair(f->fpln.plans, plan, from->ident, to->ident, altn_id))
        return err_set(err, MFD_ERR_NOT_ALLOWED, "");
    if (plan == A380X_FMS_PLAN_ACTIVE && f->plans[A380X_FMS_PLAN_TMPY].exists)
        plan_clear(&f->plans[A380X_FMS_PLAN_TMPY], 0);
    char fltnbr[12];
    scopy(fltnbr, sizeof fltnbr, p->flight_number);
    unsigned linked_keep = 0;
    plan_clear(p, 1);
    scopy(p->flight_number, sizeof p->flight_number, fltnbr);
    p->linked = linked_keep;
    p->flags = is_sec(plan) ? A380X_FMS_PLANF_MANUAL : 0;
    p->created_utc_s = now_utc(f);
    p->lateral_version0 = f->fpln.plan_version ? f->fpln.plan_version(f->fpln.plans, plan) : 0;
    apply_origin(f, plan, from);
    apply_dest(f, plan, to);
    if (altn.found) p->altn = altn;
    if (plan == A380X_FMS_PLAN_ACTIVE) {
        f->data.vspeeds_for_runway_set = 0;
        f->data.vspeeds_for_runway[0] = '\0';
    }
    return 1;
}

int a380x_fms_enter_text(A380XFms *f, int plan, A380XFmsText t, const char *s, MfdFmsError *err)
{
    err_clear(err);
    int clear = !s || !s[0];
    if (!plan_ok(f, plan)) return err_set(err, MFD_ERR_NOT_ALLOWED, "");
    unsigned fl = a380x_fms_text_flags(f, plan, t, A380X_FMS_PAGE_ANY);
    if (fl & (A380X_FMS_FF_READONLY | A380X_FMS_FF_DISABLED)) return err_set(err, MFD_ERR_NOT_ALLOWED, "");
    A380XFmsPlanData *p = &f->plans[plan];
    int ok = 0;
    switch (t) {
    case A380X_FMS_T_FLT_NBR:
        if (clear) return 0; /* not clearable */
        if (strlen(s) > 10) return err_set(err, MFD_ERR_FORMAT_ERROR, "");
        scopy(p->flight_number, sizeof p->flight_number, s);
        ok = 1;
        break;
    case A380X_FMS_T_FROM:
    case A380X_FMS_T_TO: {
        if (clear) return 0;
        if (!valid_icao(s)) return err_set(err, MFD_ERR_FORMAT_ERROR, "FORMAT: AAAA");
        A380XFmsAirportInfo apt;
        int found;
        lookup_airport(f, s, &apt, &found);
        if (!found) return err_set(err, MFD_ERR_NOT_IN_DATABASE, ""); /* quirk 16 fixed */
        if (t == A380X_FMS_T_FROM) scopy(p->pending_from, sizeof p->pending_from, s);
        else scopy(p->pending_to, sizeof p->pending_to, s);
        char from_id[8], to_id[8];
        scopy(from_id, sizeof from_id, p->pending_from[0] ? p->pending_from : (p->origin.found ? p->origin.ident : ""));
        scopy(to_id, sizeof to_id, p->pending_to[0] ? p->pending_to : (p->dest.found ? p->dest.ident : ""));
        if (from_id[0] && to_id[0] &&
            (strcmp(from_id, p->origin.found ? p->origin.ident : "") != 0 ||
             strcmp(to_id, p->dest.found ? p->dest.ident : "") != 0)) {
            A380XFmsAirportInfo a_from, a_to;
            int f1, f2;
            lookup_airport(f, from_id, &a_from, &f1);
            lookup_airport(f, to_id, &a_to, &f2);
            if (!f1 || !f2) return err_set(err, MFD_ERR_NOT_IN_DATABASE, "");
            if (!new_city_pair(f, plan, &a_from, &a_to, err)) return 0;
            p->pending_from[0] = p->pending_to[0] = '\0';
        }
        ok = 1;
        break;
    }
    case A380X_FMS_T_ALTN: {
        if (clear || strcmp(s, "NONE") == 0) {
            if (f->fpln.set_alternate && !f->fpln.set_alternate(f->fpln.plans, plan, NULL))
                return err_set(err, MFD_ERR_NOT_ALLOWED, "");
            airport_clear(&p->altn);
            ok = 1;
            break;
        }
        if (!valid_icao(s)) return err_set(err, MFD_ERR_FORMAT_ERROR, "FORMAT: AAAA");
        A380XFmsAirportInfo apt;
        int found;
        lookup_airport(f, s, &apt, &found);
        if (!found) return err_set(err, MFD_ERR_NOT_IN_DATABASE, "");
        if (f->fpln.set_alternate && !f->fpln.set_alternate(f->fpln.plans, plan, s))
            return err_set(err, MFD_ERR_NOT_ALLOWED, "");
        p->altn = apt;
        ok = 1;
        break;
    }
    default:
        return err_set(err, MFD_ERR_NOT_ALLOWED, "");
    }
    if (ok) {
        p->version++;
        compute_derived(f);
    }
    return ok;
}

/* ------------------------------------------------------------------------------------------ */
/* actions                                                                                     */
/* ------------------------------------------------------------------------------------------ */

int a380x_fms_should_show_confirm_to_speeds(const A380XFms *f, int plan)
{
    if (!plan_ok(f, plan) || !is_at(plan)) return 0;
    const A380XFmsPerf *d = CPD(f, plan);
    int any_tbc = has(f->data.v1_tbc) || has(f->data.vr_tbc) || has(f->data.v2_tbc);
    return any_tbc && isnan(d->v1) && isnan(d->vr) && isnan(d->v2);
}

int a380x_fms_confirm_to_speeds(A380XFms *f, int plan)
{
    if (!a380x_fms_should_show_confirm_to_speeds(f, plan)) return 0;
    A380XFmsPerf *d = PD(f, plan);
    d->v1 = f->data.v1_tbc;
    d->vr = f->data.vr_tbc;
    d->v2 = f->data.v2_tbc;
    f->data.v1_tbc = f->data.vr_tbc = f->data.v2_tbc = NAN;
    f->plans[plan].version++;
    compute_derived(f);
    return 1;
}

int a380x_fms_can_activate_approach(const A380XFms *f, int plan)
{
    int ph = f->phase;
    return is_at(plan) && plan_ok(f, plan) &&
           (ph == A380X_FMS_PHASE_CLIMB || ph == A380X_FMS_PHASE_CRUISE || ph == A380X_FMS_PHASE_DESCENT ||
            ph == A380X_FMS_PHASE_GOAROUND);
}

int a380x_fms_activate_approach(A380XFms *f)
{
    int ph = f->phase;
    if (ph == A380X_FMS_PHASE_PREFLIGHT || ph == A380X_FMS_PHASE_TAKEOFF || ph == A380X_FMS_PHASE_DONE) return 0;
    if (ph != A380X_FMS_PHASE_APPROACH) change_phase(f, A380X_FMS_PHASE_APPROACH);
    return 1;
}

void a380x_fms_clear_engine_out(A380XFms *f)
{
    f->data.engine_out = 0;
}

void a380x_fms_cancel_noise(A380XFms *f, int plan)
{
    if (!plan_ok(f, plan)) return;
    PD(f, plan)->noise_enabled = 0;
    f->plans[plan].version++;
}

void a380x_fms_on_fcu_alt_push_pull(A380XFms *f) { f->pending_push_pull = 1; }
void a380x_fms_on_fcu_alt_turn(A380XFms *f) { f->pending_alt_turn = 1; f->fcu_crz_armed = 1; }
void a380x_fms_on_fcu_vs_knob(A380XFms *f) { f->vs_knob_pending = 1; f->vs_knob_timer = 0.0; }

void a380x_fms_set_position_monitor_fix(A380XFms *f, const char *ident, double lat, double lon)
{
    A380XFmsPositionFix *x = &f->data.position_monitor_fix;
    x->valid = ident && ident[0] && has(lat) && has(lon);
    scopy(x->ident, sizeof x->ident, x->valid ? ident : "");
    x->lat_deg = x->valid ? lat : NAN;
    x->lon_deg = x->valid ? lon : NAN;
}

void a380x_fms_clear_position_monitor_fix(A380XFms *f)
{
    a380x_fms_set_position_monitor_fix(f, NULL, NAN, NAN);
}

const A380XFmsPositionFix *a380x_fms_position_monitor_fix(const A380XFms *f)
{
    return &f->data.position_monitor_fix;
}

void a380x_fms_set_destination_prediction(A380XFms *f, double distance_nm, double seconds, double efob_kg)
{
    f->dest_pred.valid = has(efob_kg);
    f->dest_pred.distance_nm = distance_nm;
    f->dest_pred.seconds = seconds;
    f->dest_pred.efob_kg = efob_kg;
}

void a380x_fms_clear_destination_prediction(A380XFms *f)
{
    f->dest_pred.valid = 0;
    f->dest_pred.distance_nm = f->dest_pred.seconds = f->dest_pred.efob_kg = NAN;
}

int a380x_fms_waypoint_prediction(const A380XFms *f, int plan, int leg_index, A380XFmsWaypointPrediction *out)
{
    (void)f; (void)plan; (void)leg_index;
    if (out) {
        out->valid = 0;
        out->utc_seconds = out->efob_kg = out->speed_kt = out->mach = out->altitude_ft = NAN;
    }
    return 0; /* VNAV predictions are not ported yet (spec 07 §4.8) */
}
