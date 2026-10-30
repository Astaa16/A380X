/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A380X ISIS (Integrated Standby Instrument System): two units. One is the Standby Flight
 * Display (SFD, FBW ISISlegacy), the other the Standby Navigation Display (SND); MODE swaps.
 * Native translation of FlyByWire fbw-a380x/src/systems/instruments/src/ISISlegacy.
 *
 * The core (a380x_isis.inc) is XPLM- and GL-free: it owns the ISIS logic (display-unit power
 * state machine, BUGS page, MENU page, baro, LS, brightness, ATT RST) and turns FBW's 512-unit
 * SVG into a display list of anti-aliased triangles in 1024 x 1024 device texels (y down).
 * The X-Plane adapter (a380x_isis_adapter.inc) replays that list with OpenGL into the cockpit
 * device a380x.xp12.isis.1; test_isis.c replays the identical list with a software rasteriser.
 */
#ifndef A380X_ISIS_H
#define A380X_ISIS_H

#include <stdint.h>

enum { ISIS_W = 1024, ISIS_H = 1024, ISIS_MAX_VERTICES = 65536, ISIS_MAX_BATCHES = 2048 };

typedef struct { float x, y, u, v, r, g, b, a; } IsisVertex;     /* device texels, y down */
typedef struct { int first, count, textured; } IsisBatch;           /* GL_TRIANGLES runs */

typedef struct {
    IsisVertex vertex[ISIS_MAX_VERTICES];
    IsisBatch batch[ISIS_MAX_BATCHES];
    int vertex_count, batch_count, overflow, font_misses;
    float m[6];          /* SVG matrix a b c d e f: local -> device texels */
    float clip[4];       /* device x0 y0 x1 y1 */
    int clipped;
} IsisList;

typedef enum { ISIS_DU_OFF, ISIS_DU_SELFTEST, ISIS_DU_ON, ISIS_DU_STANDBY } IsisDuState;
typedef enum { ISIS_PAGE_PFD, ISIS_PAGE_BUGS, ISIS_PAGE_MENU } IsisPage;
typedef enum { ISIS_ROLE_SFD, ISIS_ROLE_SND } IsisRole;
typedef enum {
    ISIS_BTN_MODE, ISIS_BTN_LS, ISIS_BTN_PLUS, ISIS_BTN_MINUS, ISIS_BTN_MENU,
    ISIS_BTN_SET_PUSH, ISIS_BTN_SET_UP, ISIS_BTN_SET_DOWN, ISIS_BTN_COUNT
} IsisButton;
enum { ISIS_MENU_SPD1, ISIS_MENU_SPD2, ISIS_MENU_ALT, ISIS_MENU_METERS, ISIS_MENU_BARO_UNIT,
       ISIS_MENU_NAV, ISIS_MENU_FIX, ISIS_MENU_EXIT, ISIS_MENU_COUNT };
/* FBW Bug.ts order: ALT 0, ALT 1, SPD 0..3. FBW selects index 5 first. */
enum { ISIS_BUG_ALT0, ISIS_BUG_ALT1, ISIS_BUG_SPD0, ISIS_BUG_SPD1, ISIS_BUG_SPD2, ISIS_BUG_SPD3, ISIS_BUGS };

typedef struct {
    /* X-Plane sign conventions: pitch positive nose up, roll positive right wing down. */
    float ias_kts, altitude_ft, mach, pitch_deg, roll_deg, lateral_g;
    float baro_inhg, mda_ft, loc_dots, gs_dots;
    int spd_valid, alt_valid, att_valid, loc_valid, gs_valid;
    int dc_ess, dc_hot;  /* DC ESS bus, DC HOT 1 (battery) bus */
    /* SND: magnetic heading and the two VOR/DME receivers. */
    float heading_mag_deg;
    int heading_valid;
    struct {
        int received, localizer, has_dme, frequency_10khz;
        float relative_bearing_deg, dme_nm;
        char ident[8];
    } nav[2];
} IsisInputs;

typedef struct {
    IsisRole role;
    IsisDuState du;
    float du_timer;
    int du_timer_active;
    IsisPage page;
    float idle;
    int selected_bug, menu_index;
    int bug_value[ISIS_BUGS], bug_on[ISIS_BUGS];
    int ls_active, baro_std, baro_unit_inhg, metric_alt;
    float baro_saved_inhg;
    int baro_write;        /* request to the adapter: write baro_write_inhg to the standby setting */
    float baro_write_inhg;
    float ambient, manual_offset, auto_brightness, brightness;
    int plus_held, minus_held;
    float hold_timer;
    int menu_held, menu_hold_used;
    float menu_hold;
    int att_rst;
    float att_rst_timer;
    float slip_in, slip_out, slip_offset, slip_timer;
    int mach_visible;
    float clock;           /* blink phase source */
    unsigned button_count;
} IsisState;

#endif
