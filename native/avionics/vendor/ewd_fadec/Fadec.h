// Copyright (c) 2023-2026 FlyByWire Simulations
// SPDX-License-Identifier: GPL-3.0
// Simulator-independent declaration subset of the pinned fadec_common/Fadec.h.
// See manifest.json. The implementations in Fadec.cpp are unmodified upstream.

#ifndef A380X_EWD_VENDOR_FADEC_H
#define A380X_EWD_VENDOR_FADEC_H

#include <cmath>

class Fadec {
 public:
  static double interpolate(double x, double x0, double x1, double y0, double y1);
  static double cas2mach(double cas, double ambientPressure);
};

#endif
