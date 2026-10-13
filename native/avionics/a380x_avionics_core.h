/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Simulator-independent translations of the FlyByWire A380 FMA priority and
 * FWS flight-phase primitives.  X-Plane integration belongs in
 * a380x_avionics.c; this file deliberately has no XPLM dependency.
 */
#ifndef A380X_AVIONICS_CORE_H
#define A380X_AVIONICS_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    A380X_A1A2_NONE = 0, A380X_A1A2_MAN_TOGA, A380X_A1A2_MAN_GA_SOFT,
    A380X_A1A2_MAN_FLEX, A380X_A1A2_MAN_MCT, A380X_A1A2_MAN_THR,
    A380X_A1A2_SPEED, A380X_A1A2_MACH, A380X_A1A2_THR_MCT,
    A380X_A1A2_THR_CLB, A380X_A1A2_THR_LVR, A380X_A1A2_THR_IDLE,
    A380X_A1A2_A_FLOOR, A380X_A1A2_TOGA_LK, A380X_A1A2_BTV,
    A380X_A1A2_BRK_LO, A380X_A1A2_BRK_2, A380X_A1A2_BRK_3,
    A380X_A1A2_BRK_HI, A380X_A1A2_BRK_RTO
} A380XA1A2Message;

typedef enum {
    A380X_A3_NONE = 0, A380X_A3_THR_LK, A380X_A3_LVR_TOGA,
    A380X_A3_LVR_CLB, A380X_A3_LVR_MCT, A380X_A3_LVR_ASYM,
    A380X_A3_BRK_RTO
} A380XA3Message;

typedef enum {
    A380X_B1_NONE = 0, A380X_B1_GS, A380X_B1_F_GS, A380X_B1_GS_STAR,
    A380X_B1_F_GS_STAR, A380X_B1_SRS, A380X_B1_TCAS,
    A380X_B1_APP_DES, A380X_B1_DES, A380X_B1_OP_DES, A380X_B1_CLB,
    A380X_B1_OP_CLB, A380X_B1_ALT, A380X_B1_ALT_STAR,
    A380X_B1_ALT_CST, A380X_B1_ALT_CST_STAR, A380X_B1_ALT_CRZ,
    A380X_B1_ALT_CRZ_STAR, A380X_B1_FPA, A380X_B1_VS
} A380XB1Message;

typedef enum {
    A380X_C1_NONE = 0, A380X_C1_GA_TRK, A380X_C1_LOC_BC_STAR,
    A380X_C1_LOC_STAR, A380X_C1_F_LOC_STAR, A380X_C1_HDG,
    A380X_C1_RWY, A380X_C1_RWY_TRK, A380X_C1_TRACK, A380X_C1_LOC_BC,
    A380X_C1_LOC, A380X_C1_F_LOC, A380X_C1_NAV
} A380XC1Message;

typedef enum {
    A380X_D1D2_NONE = 0, A380X_D1D2_LAND_2, A380X_D1D2_LAND_3_SINGLE,
    A380X_D1D2_LAND_3_DUAL, A380X_D1D2_APPR_1, A380X_D1D2_LAND_1,
    A380X_D1D2_F_APP, A380X_D1D2_F_APP_RAW, A380X_D1D2_RAW_ONLY
} A380XD1D2Message;

typedef struct {
    int at_engaged, at_active;
    int man_toga, man_flex, mct, manual_thrust, speed, mach, climb;
    int idle, alpha_floor, toga_lock;
    int autobrake_active, autobrake_mode;
} A380XA1A2Inputs;

typedef struct {
    int thrust_locked, climb_demand, mct_demand, asymmetric_thrust;
    int autobrake_active, autobrake_mode;
} A380XA3Inputs;

typedef struct {
    int gs_track, gs_capture, descent, climb, pitch_takeoff, pitch_goaround;
    int open_descent, open_climb, alt_hold, alt_acquire, fpa, vertical_speed;
    int app_descent, tcas, altitude_constraint, cruise_altitude;
} A380XB1Inputs;

typedef struct {
    int goaround_track, backbeam, loc_capture, loc_track, heading;
    int runway, runway_localizer, runway_track, track, navigation;
} A380XC1Inputs;

typedef struct {
    int land_mode_armed, land_mode_active, land2_capacity;
    int land3_single_capacity, land3_dual_capacity, approach1_condition;
} A380XD1D2Inputs;

typedef struct {
    double timer;
    int previous;
    int initialized;
    int output;
    int rising;
    double delay;
} A380XConfirmNode;

typedef struct {
    int value;
    int set_has_priority;
} A380XMemoryNode;

typedef struct {
    A380XConfirmNode ground_confirm;
    A380XConfirmNode engine_confirm;
    A380XMemoryNode above_80;
    A380XMemoryNode above_v1;
    double shutdown_timer;
    int phase;
} A380XFlightPhaseState;

typedef struct {
    int on_ground;
    int running_engines;
    int engines_at_takeoff_power;
    double indicated_airspeed;
    double radio_height_ft;
    double configured_v1;
} A380XFlightPhaseInputs;

A380XA1A2Message a380x_compute_a1a2(const A380XA1A2Inputs *in);
A380XA3Message a380x_compute_a3(const A380XA3Inputs *in);
A380XB1Message a380x_compute_b1(const A380XB1Inputs *in);
A380XC1Message a380x_compute_c1(const A380XC1Inputs *in);
A380XD1D2Message a380x_compute_d1d2(const A380XD1D2Inputs *in);

void a380x_flight_phase_init(A380XFlightPhaseState *state);
int a380x_flight_phase_update(A380XFlightPhaseState *state,
                              const A380XFlightPhaseInputs *in,
                              double dt_seconds);

const char *a380x_a1a2_text(A380XA1A2Message message);
const char *a380x_a3_text(A380XA3Message message);
const char *a380x_b1_text(A380XB1Message message);
const char *a380x_c1_text(A380XC1Message message);
const char *a380x_d1d2_text(A380XD1D2Message message);

/* FlyByWire flyPad service/TOD calculator and tablet battery timing. */
double a380x_efb_average_ground_speed(double current_altitude_ft,
                                      double target_altitude_ft,
                                      double current_ground_speed_knots);
double a380x_efb_tod_distance_nm(double current_altitude_ft,
                                 double target_altitude_ft,
                                 double descent_angle_degrees);
double a380x_efb_battery_level(double current_percent,
                               int external_power_available,
                               double dt_seconds);

#ifdef __cplusplus
}
#endif
#endif
