/* SPDX-License-Identifier: GPL-3.0-or-later
 * Display-only snapshot: raw sensors stay in Side, guidance comes from the
 * aircraft-local controller. No X-Plane stock AP/FD mode state is consumed.
 */
#ifndef A380X_PFD_DATA_H
#define A380X_PFD_DATA_H

typedef struct {
    double selected_speed_knots, selected_mach, selected_heading_deg;
    double selected_altitude_ft, selected_vs_fpm, selected_fpa_deg;
    int speed_managed, heading_managed, altitude_managed;
    int mach_mode, trk_fpa_mode, metric_altitude;
    int true_heading;
    double heading_reference_offset_deg;
    int ap1, ap2, fd1, fd2, athr_armed, athr_active;
    int loc_armed, gs_armed, altitude_armed;
    int guidance_valid, guidance_fault;
    int managed_unavailable, mode_reversion, disconnect_warning;
    /* Positive pitch error commands nose-up; positive roll error commands right. */
    double fd_pitch_error_deg, fd_roll_error_deg;
    /* Optional performance values. Zero means not available, never a placeholder. */
    double vmax_knots, vls_knots, v1_knots, vr_knots, green_dot_knots;
    double vfe_next_knots, f_speed_knots, s_speed_knots;
    double alpha_prot_knots, alpha_limit_knots, stall_warning_knots;
    /* Native configuration snapshot. Ratios are actual surface deployment,
     * separate from the requested flap position and speedbrake arming. */
    int on_ground, flap_handle_index, flap_config;
    int flap_valid, slat_valid, speedbrake_valid, spoilers_armed, gear_down;
    int flap_fault, slat_fault, stall_warning, envelope_valid;
    /* Only the bounded clean, speedbrakes-stowed PRIM performance lookup.
     * This does not imply an active alpha-protection law. */
    int clean_performance_valid;
    double flap_ratio, slat_ratio, spoiler_ratio, flap_target_ratio, slat_target_ratio;
    /* T.O V2 from the native FMS (0 = none). V1/VR above come from the FMS as well. */
    double v2_knots;
} A380XPfdGuidance;

/* Native FMS outputs consumed by the existing PFD/guidance boundary (spec 07 section 10.2).
 * Filled by the MFD/FMS adapter every frame; all zero = no FMS data (never a placeholder). */
typedef struct {
    int valid;                       /* FMS healthy and outputs computed */
    double v1_knots, vr_knots, v2_knots; /* 0 = none */
    int managed_speed_valid, managed_speed_is_mach;
    double managed_speed_knots, managed_mach;
} A380XFmsFeed;

#endif
