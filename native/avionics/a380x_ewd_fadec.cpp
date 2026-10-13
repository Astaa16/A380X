// Copyright (c) 2023-2026 FlyByWire Simulations
// SPDX-License-Identifier: GPL-3.0
// Native boundary derived from the pinned A380X EngineControl_A380X.cpp,
// FlyByWireInterface.cpp and EWD/ThrustGauge.tsx. See vendor/ewd_fadec/manifest.json.

#include "a380x_ewd_fadec.h"

#include <algorithm>
#include <cmath>

#include "vendor/ewd_fadec/EngineRatios.hpp"
#include "vendor/ewd_fadec/Table1502_A380X.hpp"
#include "vendor/ewd_fadec/ThrustLimits_A380X.hpp"

extern "C" int a380x_ewd_fadec_limits(double temp_c,
                                      double pressure_hpa,
                                      double pressure_altitude_ft,
                                      double mach,
                                      int pack_count,
                                      int engine_anti_ice,
                                      int wing_anti_ice,
                                      A380xEwdFadecLimits *out) {
  if (!out) return 0;
  *out = {};
  if (!std::isfinite(temp_c) || !std::isfinite(pressure_hpa) ||
      !std::isfinite(pressure_altitude_ft) || !std::isfinite(mach) ||
      temp_c <= -273.15 || pressure_hpa <= 0.0 || mach < 0.0 ||
      pack_count < 0 || pack_count > 2 || engine_anti_ice < 0 || engine_anti_ice > 1 ||
      wing_anti_ice < 0 || wing_anti_ice > 1) return 0;

  // Table1502's original search assumes CN3 falls inside its 13-row table.
  // Reject out-of-domain inputs before calling it rather than extrapolating
  // or allowing the upstream search to access a row outside the array.
  const double idle_cn3 = Table1502_A380X::iCN3(pressure_altitude_ft, mach);
  if (!std::isfinite(idle_cn3) || idle_cn3 < 16.012 || idle_cn3 >= 118.0) return 0;

  const int packs = pack_count > 0 ? 1 : 0;
  const double idle_cn1 = Table1502_A380X::iCN1(pressure_altitude_ft, mach, temp_c);
  const double idle = idle_cn1 * std::sqrt(EngineRatios::theta2(0, temp_c));

  // EngineControl_A380X::updateThrustLimits: same table calls and factors.
  const double altitude = std::min(16600.0, pressure_altitude_ft);
  const double to = ThrustLimits_A380X::limitN1(0, altitude, temp_c, pressure_hpa, 0,
                                               packs, engine_anti_ice, wing_anti_ice);
  const double ga = ThrustLimits_A380X::limitN1(1, altitude, temp_c, pressure_hpa, 0,
                                               packs, engine_anti_ice, wing_anti_ice);
  const double clb = ThrustLimits_A380X::limitN1(2, pressure_altitude_ft, temp_c, pressure_hpa, 0,
                                                packs, engine_anti_ice, wing_anti_ice);
  double mct = ThrustLimits_A380X::limitN1(3, pressure_altitude_ft, temp_c, pressure_hpa, 0,
                                         packs, engine_anti_ice, wing_anti_ice);
  const double mach_factor_low = std::max(0.0, std::min(1.0, (mach - 0.04) / 0.04));
  double toga = to + (ga - to) * mach_factor_low;

  // The stateful FLEX-to-CLB transition is intentionally outside this API.
  // These are the unlatched limits produced by the same upstream function.
  const double mach_factor = std::max(0.0, std::min(1.0, (mach - 0.37) / 0.05));
  const double altitude_factor_low = std::max(0.0, std::min(1.0, (pressure_altitude_ft - 16600) / 500));
  const double altitude_factor_high = std::max(0.0, std::min(1.0, (pressure_altitude_ft - 25000) / 500));

  if (pressure_altitude_ft >= 25000) {
    mct = std::max(clb, mct + (clb - mct) * altitude_factor_high);
    toga = mct;
  } else if (mct > toga) {
    mct = toga + (mct - toga) * std::min(1.0, altitude_factor_low + mach_factor);
    toga = mct;
  } else {
    toga = toga + (mct - toga) * std::min(1.0, altitude_factor_low + mach_factor);
  }

  // EWD adds back 0.6 N1 if either PACK discrete is active.
  const double maximum = toga + (packs ? 0.6 : 0.0);
  // FlyByWireInterface's pinned default THRUST_LIMIT_REVERSE_PERCENTAGE_TOGA.
  const double reverse = toga * 0.813;
  if (!std::isfinite(idle) || !std::isfinite(toga) || !std::isfinite(clb) ||
      !std::isfinite(mct) || !std::isfinite(maximum) || !std::isfinite(reverse) ||
      maximum <= idle || reverse <= idle) return 0;

  out->idle_n1 = idle;
  out->toga_n1 = toga;
  out->max_n1 = maximum;
  out->reverse_n1 = reverse;
  out->climb_n1 = clb;
  out->mct_n1 = mct;
  return 1;
}
