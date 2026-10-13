/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD SEC INDEX page (fms/sec/index[/1|/2|/3]), translated from the pinned FBW
 * pages/FMS/SEC/MfdFmsSecIndex.tsx / .scss (docs/mfd-spec/03-fms-init-fuel-data-sec-msg.md
 * section 8): per-SEC tabs with the city pair, the CREATED line, the airway/fix route overview and
 * the IMPORT / CPNY F-PLN / F-PLN / PERF / FUEL&LOAD / INIT / DELETE* / SWAP ACTIVE* actions.
 */
#ifndef A380X_MFD_PAGE_SEC_H
#define A380X_MFD_PAGE_SEC_H

#include "a380x_mfd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MFD_SEC_ROUTE_LINES 11                      /* NUM_ROUTE_LINES_DISPLAYED */
#define MFD_SEC_ROUTE_MAX 200                       /* overview entries kept per SEC */

typedef struct {
    char annotation[16];  /* "DIR", "RWY", airway or procedure name (UTF-8) */
    char ident[12];
} MfdSecRouteEntry;

typedef struct {
    int tab;                       /* 0..2 = SEC 1..3 */
    int start_line[3];             /* routeStartFromLine per tab */
    int route_count;               /* overview entries of the displayed tab */
    MfdSecRouteEntry route[MFD_SEC_ROUTE_MAX];
} MfdSecIndexState;
MFD_PAGE_STATE_CHECK(MfdSecIndexState);

/* Register fms/sec/index (extra "1".."3" selects the initial tab). */
void a380x_mfd_page_sec_register(void);

#ifdef __cplusplus
}
#endif

#endif
