// SPDX-License-Identifier: GPL-3.0
#ifndef A380X_EWD_FADEC_H
#define A380X_EWD_FADEC_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct A380xEwdFadecLimits {
  double idle_n1;
  double toga_n1;
  double max_n1;
  double reverse_n1;
  double climb_n1;
  double mct_n1;
} A380xEwdFadecLimits;

/* Calculate N1 percentages from the pinned A380X FADEC equations and tables.
 * pack_count is 0..2; upstream applies a single derate if either pack is active.
 * engine_anti_ice and wing_anti_ice are boolean (0 or 1).
 * max_n1 includes the EWD's +0.6 N1 pack compensation; toga_n1 is the FADEC
 * TOGA limit before that compensation. reverse_n1 is a positive N1 magnitude.
 *
 * This stateless API does not model the FADEC FLEX-to-CLB latch/transition.
 * It supplies the upstream non-FLEX limits; full engine control is separate.
 * Returns 1 on success, or 0 with zeroed output for invalid inputs/domain.
 */
int a380x_ewd_fadec_limits(double temp_c,
                           double pressure_hpa,
                           double pressure_altitude_ft,
                           double mach,
                           int pack_count,
                           int engine_anti_ice,
                           int wing_anti_ice,
                           A380xEwdFadecLimits *out);

#ifdef __cplusplus
}
#endif

#endif
