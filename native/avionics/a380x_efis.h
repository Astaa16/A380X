/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef A380X_EFIS_H
#define A380X_EFIS_H
typedef struct {
    int cstr, wpt, vord, ndb, arpt, vor1, vor2, terr, traf, ls, vv;
    int wx_requested, taxi_requested, mode, range_index, zoom;
    int baro_std, baro_inhg, powered;
    float range_nm, qnh_hpa, baro_preset_hpa;
} EfisSide;
#endif
