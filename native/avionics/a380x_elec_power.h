/* SPDX-License-Identifier: GPL-3.0-or-later
 * Per-bus power for the port's consumers (docs/ELEC_SYSTEM_SPEC.md section 6).
 *
 * The X-Plane adapter (a380x_elec_adapter.inc) publishes the native network into
 * a380x_power once per frame, before any consumer runs. Every consumer asks
 * a380x_power_*() with its legacy criterion as the fallback: when the network is not
 * running (registration failed, or a unit test that includes one module on its own),
 * a380x_power.available is 0 and the previous behaviour is kept unchanged.
 *
 * Header-only and XPLM-free; one instance per translation unit (the plugin is a unity
 * build of a380x_avionics.c).
 */
#ifndef A380X_ELEC_POWER_H
#define A380X_ELEC_POWER_H
#include <stdint.h>
#include "a380x_elec.h"

typedef struct {
    int available;                                   /* native network running */
    uint32_t bus;                                    /* A380X_ELN_BIT(bus) */
    unsigned char du_state[A380X_ELN_DU_COUNT];      /* A380XElnDuState */
    unsigned char isis_powered, fcu_powered, adiru_primary[3], adiru_backup[3];
    int spawn_mode;
} A380XPowerView;

static A380XPowerView a380x_power;

static inline int a380x_power_bus(int bus, int fallback)
{
    if (!a380x_power.available) return fallback != 0;
    return bus >= 0 && bus < A380X_ELN_BUS_COUNT && ((a380x_power.bus >> bus) & 1u) != 0;
}
static inline int a380x_power_any(uint32_t mask, int fallback)
{
    return a380x_power.available ? (a380x_power.bus & mask) != 0 : fallback != 0;
}
/* Display content visible (CDS state On). */
static inline int a380x_power_du_on(int du, int fallback)
{
    if (!a380x_power.available) return fallback != 0;
    return du >= 0 && du < A380X_ELN_DU_COUNT && a380x_power.du_state[du] == A380X_ELN_DU_ON;
}
/* Display lit: Thales boot, self-test or On (the boot/self-test screens are drawn by the DU). */
static inline int a380x_power_du_lit(int du, int fallback)
{
    int st;
    if (!a380x_power.available) return fallback != 0;
    if (du < 0 || du >= A380X_ELN_DU_COUNT) return 0;
    st = a380x_power.du_state[du];
    return st == A380X_ELN_DU_ON || st == A380X_ELN_DU_BOOT || st == A380X_ELN_DU_SELFTEST;
}
static inline int a380x_power_du_state(int du)
{
    if (!a380x_power.available || du < 0 || du >= A380X_ELN_DU_COUNT) return -1;
    return a380x_power.du_state[du];
}
static inline int a380x_power_fcu(int fallback)
{
    return a380x_power.available ? a380x_power.fcu_powered != 0 : fallback != 0;
}
static inline int a380x_power_isis(int fallback)
{
    return a380x_power.available ? a380x_power.isis_powered != 0 : fallback != 0;
}
#endif
