/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pop-up window geometry for the 768x1024 cockpit devices (PFD, ND), like the MFD's
 * mfda_ensure_popup_geometry: without it X-Plane opened them stretched over the whole screen.
 * Keeps a pilot-sized, on-screen window and only restores the 3:4 shape; otherwise 80 % of the
 * screen height, captain on the left and first officer on the right.
 */
#ifndef A380X_AVIONICS_POPUP_H
#define A380X_AVIONICS_POPUP_H

#include "XPLMDisplay.h"

typedef void (*A380XPopupGetGeom)(XPLMAvionicsID, int *, int *, int *, int *);
typedef void (*A380XPopupSetGeom)(XPLMAvionicsID, int, int, int, int);

static void a380x_fit_popup(XPLMAvionicsID id, int side, A380XPopupGetGeom get, A380XPopupSetGeom set)
{
    int l = 0, t = 0, r = 0, b = 0, sl, st, sr, sb, max_h, h, w, left, top;
    if (!id || !get || !set) return;
    get(id, &l, &t, &r, &b);
    XPLMGetScreenBoundsGlobal(&sl, &st, &sr, &sb);
    max_h = (int)((float)(st - sb) * 0.8f);
    h = t - b;
    if (h < 200 || h > max_h) h = max_h;
    w = h * 768 / 1024;
    left = (r - l >= 100 && l >= sl && l + w <= sr) ? l : (side ? sr - 40 - w : sl + 40);
    top = (r - l >= 100 && t <= st && t - h >= sb) ? t : st - 60;
    set(id, left, top, left + w, top - h);
}

#endif
