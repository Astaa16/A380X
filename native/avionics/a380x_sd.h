/* SPDX-License-Identifier: GPL-3.0-or-later
 * Initial native ECAM System Display. No simulator calls or synthetic values.
 * Layout reference: pinned FBW SD/Pages/Engine and SD/Pages/Apu.
 */
#ifndef A380X_SD_H
#define A380X_SD_H
#include "a380x_mfd_draw.h"

enum { SD_ENGINE, SD_APU, SD_BLEED, SD_COND, SD_PRESS, SD_DOOR, SD_ELAC,
       SD_ELDC, SD_FUEL, SD_WHEEL, SD_HYD, SD_FCTL, SD_CB, SD_CRUISE, SD_STS, SD_VIDEO, SD_PAGES };
typedef struct { float value; int valid; } SdValue;
typedef struct {
    int active, starting;
    SdValue n2, n3, fuel_flow_kg_h, oil_quantity_qt, oil_temperature_c, oil_pressure_psi;
    SdValue vibration[3], nacelle_temperature_c;
} SdEngine;
typedef struct {
    int powered;
    SdEngine engine[4];
    SdValue apu_n1, apu_n2, apu_egt_c, apu_bleed_psi, apu_fuel_used_kg;
    SdValue apu_gen_load[2], apu_gen_volts[2], apu_gen_hz[2];
    int apu_available, apu_bleed_open, apu_bleed_valid;
} SdData;

SdValue a380x_sd_value(float value, int supplied, float minimum, float maximum);
int a380x_sd_page_supported(int page);
void a380x_sd_draw(const SdData *data, int page, MfdDrawList *out);
#endif
