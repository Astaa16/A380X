/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef A380X_EFIS_H
#define A380X_EFIS_H
typedef struct {
    int cstr, wpt, vord, ndb, arpt, vor1, vor2, terr, traf, ls, vv;
    int wx_requested, taxi_requested, mode, range_index, zoom;
    int baro_std, baro_inhg, powered;
    float range_nm, qnh_hpa, baro_preset_hpa;
    int baro_preset_visible; /* STD with a QNH preselected by turning the knob (FBW BaroPresetVisible) */
    /* Mechanical feedback (a380x_control_motion.h): 13 momentary keys in action order
     * CSTR WPT VOR.D NDB ARPT VOR1 VOR2 TERR TRAF WX TAXI LS VV, then the baro shaft
     * (+1 pushed = STD, -1 pulled = QNH). */
    float pressed[14];
} EfisSide;
#endif
