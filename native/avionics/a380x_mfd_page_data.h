/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD DATA pages: DATA/STATUS (ACFT STATUS, FMS P/N) and DATA/AIRPORT
 * (docs/mfd-spec/03-fms-init-fuel-data-sec-msg.md sections 5 and 6).
 */
#ifndef A380X_MFD_PAGE_DATA_H
#define A380X_MFD_PAGE_DATA_H

#include "a380x_mfd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int tab;                   /* 0 = ACFT STATUS, 1 = FMS P/N */
    int swap_dialog;           /* SWAP ? confirmation visible */
    A380XMfdStatusData st;     /* last snapshot from the host */
} MfdDataStatusState;
MFD_PAGE_STATE_CHECK(MfdDataStatusState);

typedef struct {
    int tab;                   /* 0 = DATABASE ARPTs, 1 = PILOT STORED RWYs */
    char icao[6];              /* entered ARPT IDENT ("" = none) */
    int lookup;                /* 1 = waiting for the host (async), 0 = idle */
    int found;                 /* airport loaded */
    int selected;              /* runway detail index, -1 = list */
    A380XMfdAirport apt;
} MfdDataAirportState;
MFD_PAGE_STATE_CHECK(MfdDataAirportState);

/* Register fms/data/status and fms/data/airport. */
void a380x_mfd_page_data_register(void);

/* AIRAC helpers for the adapter (spec 03 section 5.4).
 * "2609" -> "03SEP-01OCT" (returns 1), invalid cycle -> 0. */
int mfd_airac_cycle_dates(const char *cycle, char *out, size_t n);
/* "XP" + cycle + "0001" (FBW: provider prefix + cycle + "0001"). */
void mfd_navdb_serial(const char *prefix, const char *cycle, char *out, size_t n);

/* Port option: lay out ACFT STATUS like the older FBW build of the reference screenshot
 * (A380-800 / ENGINE TRENT 972 / IDLE-PERF box at the right, no FUEL PENALTY row).
 * Default 0 = pinned FBW layout. */
void mfd_page_data_set_legacy_layout(int legacy);

#ifdef __cplusplus
}
#endif

#endif
