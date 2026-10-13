/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X MFD page registry for the plugin (integrator-owned, docs/mfd-spec/12-architecture.md
 * sections 3 and 6, docs/mfd-spec/09-port-integration.md section 1.1).
 *
 * Calls every page module's register function once at start-up. Each page module is compiled in
 * by build.py, which defines A380X_MFD_HAVE_PAGE_<NAME>=1 for every module it links. The final
 * plugin build requires all of them (build.py refuses a partial set unless --allow-missing-pages
 * is given for an incremental integration build); a page that is not linked simply stays
 * unrouted (NOT FOUND, spec 01 section 10.2).
 *
 * Simulator independent: no XPLM, no OpenGL.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "a380x_mfd.h"
#include "a380x_mfd_page_data.h"

/* Register functions of the page modules (spec 12 section 3). Declared here so this file does
 * not depend on the page headers' internals. */
void a380x_mfd_page_data_register(void);
#if defined(A380X_MFD_HAVE_PAGE_INIT)
void a380x_mfd_page_init_register(void);
#endif
#if defined(A380X_MFD_HAVE_PAGE_FUEL)
void a380x_mfd_page_fuel_register(void);
#endif
#if defined(A380X_MFD_HAVE_PAGE_SEC)
void a380x_mfd_page_sec_register(void);
#endif
#if defined(A380X_MFD_HAVE_PAGE_PERF)
void a380x_mfd_page_perf_register(void);
#endif
#if defined(A380X_MFD_HAVE_PAGE_FPLN)
void a380x_mfd_page_fpln_register(void);
#endif
#if defined(A380X_MFD_HAVE_PAGE_FPLN_REV)
void a380x_mfd_page_fpln_rev_register(void);
#endif
#if defined(A380X_MFD_HAVE_PAGE_POSITION)
void a380x_mfd_page_position_register(void);
#endif
#if defined(A380X_MFD_HAVE_PAGE_SURV)
void a380x_mfd_page_surv_register(void);
#endif
#if defined(A380X_MFD_HAVE_PAGE_ATCCOM)
void a380x_mfd_page_atccom_register(void);
#endif
#if defined(A380X_MFD_HAVE_PAGE_FCUBKUP)
void a380x_mfd_page_fcubkup_register(void);
#endif

void a380x_mfd_pages_register(void);
int a380x_mfd_pages_linked(char *names, size_t n);

/* Registration order matters only for identical patterns (later wins). DATA first, then the
 * FMS page families, then the other systems. */
void a380x_mfd_pages_register(void)
{
    mfd_clear_pages();
    a380x_mfd_page_data_register();
#if defined(A380X_MFD_HAVE_PAGE_INIT)
    a380x_mfd_page_init_register();
#endif
#if defined(A380X_MFD_HAVE_PAGE_FUEL)
    a380x_mfd_page_fuel_register();
#endif
#if defined(A380X_MFD_HAVE_PAGE_SEC)
    a380x_mfd_page_sec_register();
#endif
#if defined(A380X_MFD_HAVE_PAGE_PERF)
    a380x_mfd_page_perf_register();
#endif
#if defined(A380X_MFD_HAVE_PAGE_FPLN)
    a380x_mfd_page_fpln_register();
#endif
#if defined(A380X_MFD_HAVE_PAGE_FPLN_REV)
    a380x_mfd_page_fpln_rev_register();
#endif
#if defined(A380X_MFD_HAVE_PAGE_POSITION)
    a380x_mfd_page_position_register();
#endif
#if defined(A380X_MFD_HAVE_PAGE_SURV)
    a380x_mfd_page_surv_register();
#endif
#if defined(A380X_MFD_HAVE_PAGE_ATCCOM)
    a380x_mfd_page_atccom_register();
#endif
#if defined(A380X_MFD_HAVE_PAGE_FCUBKUP)
    a380x_mfd_page_fcubkup_register();
#endif
}

/* Comma-separated list of the linked page modules (for the log line). Returns the count. */
int a380x_mfd_pages_linked(char *names, size_t n)
{
    static const char *const linked[] = {
        "data",
#if defined(A380X_MFD_HAVE_PAGE_INIT)
        "init",
#endif
#if defined(A380X_MFD_HAVE_PAGE_FUEL)
        "fuel",
#endif
#if defined(A380X_MFD_HAVE_PAGE_SEC)
        "sec",
#endif
#if defined(A380X_MFD_HAVE_PAGE_PERF)
        "perf",
#endif
#if defined(A380X_MFD_HAVE_PAGE_FPLN)
        "fpln",
#endif
#if defined(A380X_MFD_HAVE_PAGE_FPLN_REV)
        "fpln_rev",
#endif
#if defined(A380X_MFD_HAVE_PAGE_POSITION)
        "position",
#endif
#if defined(A380X_MFD_HAVE_PAGE_SURV)
        "surv",
#endif
#if defined(A380X_MFD_HAVE_PAGE_ATCCOM)
        "atccom",
#endif
#if defined(A380X_MFD_HAVE_PAGE_FCUBKUP)
        "fcubkup",
#endif
    };
    size_t i, used = 0;
    if (names && n) names[0] = '\0';
    for (i = 0; i < sizeof(linked) / sizeof(linked[0]); ++i) {
        if (names && used + 1 < n) {
            int w = snprintf(names + used, n - used, "%s%s", i ? "," : "", linked[i]);
            if (w > 0) used += (size_t)w;
            if (used >= n) used = n - 1;
        }
    }
    return (int)(sizeof(linked) / sizeof(linked[0]));
}
