/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD FUEL&LOAD page: ACTIVE/FUEL&LOAD and SECn/FUEL&LOAD (fms/ * /fuel-load), translated
 * from the pinned FBW pages/FMS/MfdFmsFuelLoad.tsx / .scss
 * (docs/mfd-spec/03-fms-init-fuel-data-sec-msg.md section 4).
 */
#ifndef A380X_MFD_PAGE_FUEL_H
#define A380X_MFD_PAGE_FUEL_H

#include "a380x_mfd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int plan; /* loaded plan of the last frame (-1 = none) */
} MfdFuelState;
MFD_PAGE_STATE_CHECK(MfdFuelState);

/* Register fms/ * /fuel-load (ACTIVE and SEC1..3). */
void a380x_mfd_page_fuel_register(void);

#ifdef __cplusplus
}
#endif

#endif
