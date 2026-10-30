"""Shared pixel layout for the A380X native X-Plane cockpit panel."""

from __future__ import annotations


PANEL_SIZE = (2048, 2048)

# Preserve all existing display pixel positions in the first 1024 columns.
# The EWD uses the full 768x1024 FBW canvas in the additional atlas space.
# Each display is isolated so its pixels cannot bleed into neighboring regions.
# AFS CP glass: one full-width 1024-texel slice per physical LCD. 1024 x 212 matches
# the authored glass aspect (0.101 x 0.0206 m, 4.8:1), so the FBW FCU fonts are drawn
# 1:1 at their rasterised size with no horizontal stretch. The slices use the
# otherwise unused atlas band above the EWD; every other instrument stays fixed.
FCU_SCREEN_HEIGHT = 212
FCU_PANEL_RECT = (0, 1280, 2048, 1280 + FCU_SCREEN_HEIGHT)
FCU_LCD_PANEL_RECTS = ((0, 1280, 1024, 1280 + FCU_SCREEN_HEIGHT),
                       (1024, 1280, 2048, 1280 + FCU_SCREEN_HEIGHT))
FCU_INSTRUMENT_Y = FCU_PANEL_RECT[1] + 1
PFD_PANEL_RECTS = (
    (0, 32, 320, 448),
    (704, 32, 1024, 448),
)
ND_PANEL_RECTS = (
    (328, 230, 508, 464),
    (516, 230, 696, 464),
)
EWD_PANEL_RECT = (1024, 0, 1792, 1024)
CLOCK_PANEL_RECT = (332, 40, 420, 132)
CLOCK_CHR_PANEL_RECT = (332, 104, 420, 132)
CLOCK_UTC_PANEL_RECT = (332, 72, 420, 100)
CLOCK_ET_PANEL_RECT = (332, 40, 420, 68)
# Authored clock openings, ordered CHR / UTC / ET. Source X is reversed
# by the aircraft attachment, as on the other cockpit displays.
CLOCK_SCREEN_SURFACES = (
    (-.227323, -.1947965, 2.6485668, 32.9075695, 2.6601965, 32.9118),
    (-.2334152, -.1890337, 2.6272800, 32.8998255, 2.6396445, 32.9043252),
    (-.2281214, -.1952054, 2.6071843, 32.8925171, 2.6188146, 32.8967499),
)

# One shared flyPad render target is mapped onto both authored tablet faces.
EFB_PANEL_RECT = (152, 516, 872, 1016)

# The source SCREEN_AUTOPILOT quad sits behind the FCU face. Its bounding box
# includes the opaque EFIS divider bars, so copying that full face would paint
# across the bezels. These inner opening bounds come from Plane.049 in the
# authored FCU mesh, in source coordinates before the 180-degree attachment.
EFIS_BARO_SCREEN_SURFACES = (
    (0.25023, 0.27972, 2.98355, 32.75400, 3.00109, 32.76053),
    (-0.27972, -0.25023, 2.98355, 32.75400, 3.00109, 32.76053),
)
# Column order is CSTR/WPT/VORD/NDB/ARPT above and VOR1/WX/TERR/TRAF/VOR2
# below. Positive source X is screen-left after the aircraft attachment.
EFIS_UPPER_SCREEN_X_BOUNDS = (
    (0.13047, 0.23739), (-0.23739, -0.13047),
)
EFIS_LOWER_SCREEN_X_BOUNDS = (
    ((0.21909, 0.23739), (0.19387, 0.21497), (0.17299, 0.19387),
     (0.15296, 0.17299), (0.13023, 0.14853)),
    ((-0.14853, -0.13023), (-0.17299, -0.15296), (-0.19387, -0.17299),
     (-0.21497, -0.19387), (-0.23739, -0.21909)),
)
EFIS_UPPER_SCREEN_YZ = (2.99426, 32.75799, 3.00094, 32.76048)
EFIS_LOWER_SCREEN_YZ = (2.98378, 32.75408, 2.99068, 32.75666)

# EFIS CP LCDs, per side: one 1536 x 104 texel strip per legend row (the physical
# rows are ~15:1) and a 384 x 244 baro window (1.58:1), in the free atlas band above
# the AFS CP slices. Upper-row cells split their strip evenly; lower-row cells follow
# the three physical lower LCD segments (VOR1 | WX TERR TRAF | VOR2) by source X.
EFIS_ROW_W, EFIS_ROW_H = 1536, 104
EFIS_BARO_W, EFIS_BARO_H = 384, 244
EFIS_UPPER_ROW_RECTS = ((0, 1500, 1536, 1604), (0, 1724, 1536, 1828))
EFIS_LOWER_ROW_RECTS = ((0, 1612, 1536, 1716), (0, 1836, 1536, 1940))
EFIS_BARO_PANEL_RECTS = ((1544, 1500, 1928, 1744), (1544, 1752, 1928, 1996))


def _efis_cells(row, fractions):
    left, bottom, right, top = row
    width = right - left
    return tuple((left + round(a * width), bottom, left + round(b * width), top) for a, b in fractions)


def _efis_lower_fractions(side):
    columns = EFIS_LOWER_SCREEN_X_BOUNDS[side]
    high = max(hi for _lo, hi in columns)
    low = min(lo for lo, _hi in columns)
    # Positive source X is screen-left after the attachment.
    return tuple(((high - hi) / (high - low), (high - lo) / (high - low)) for lo, hi in columns)


EFIS_UPPER_PANEL_RECTS = tuple(
    _efis_cells(EFIS_UPPER_ROW_RECTS[side], tuple((i / 5, (i + 1) / 5) for i in range(5)))
    for side in range(2))
EFIS_LOWER_PANEL_RECTS = tuple(
    _efis_cells(EFIS_LOWER_ROW_RECTS[side], _efis_lower_fractions(side)) for side in range(2))

# Authored screen corners ordered bottom-left, top-left, top-right,
# bottom-right as seen on each tablet. The FBW source uses opposite vertex
# order on the two sides, hence the explicit per-side ordering.
EFB_SCREEN_SURFACES = (
    (
        (1.01069588197, 2.76022740268, 32.3279214481),
        (1.03427849100, 2.91305992302, 32.3448056244),
        (0.90358815812, 2.91340435995, 32.5241681129),
        (0.88000803711, 2.76057024041, 32.5072837937),
    ),
    (
        (-0.87699422037, 2.76717630544, 32.5088743878),
        (-0.91275825841, 2.91690977430, 32.5312384826),
        (-1.03651189729, 2.91486673821, 32.3470291217),
        (-1.00074553800, 2.76513522571, 32.3246685157),
    ),
)

EFB_PAGE_NAMES = (
    'dashboard', 'dispatch', 'ground', 'performance', 'navigation',
    'atc', 'failures', 'checklists', 'presets', 'settings',
)

# Panel-pixel rectangles shared with the native aircraft-control renderer.
# Ten page tabs run horizontally below the title bar. The OBJ builder maps
# their pixels directly onto the authored bottom-left/top-left screen corners.
EFB_PAGE_TOUCH_RECTS = tuple(
    (160 + index * 70, 940, 228 + index * 70, 986)
    for index in range(len(EFB_PAGE_NAMES))
)
EFB_ACTION_TOUCH_RECTS = (
    (170, 594, 390, 638), (400, 594, 620, 638), (630, 594, 850, 638),
    (170, 538, 390, 582), (400, 538, 620, 582), (630, 538, 850, 582),
)
EFB_POWER_TOUCH_RECT = (825, 986, 858, 1013)

# Exact authored SCREEN_DU_PFDL/PFDR bounds in the normalized FBW cockpit.
# Values are source OBJ coordinates before the aircraft attachment's 180-degree
# heading rotation.  Positive source X is the captain's side after attachment.
PFD_SCREEN_SURFACES = (
    (0.48004797, 0.63861006, 2.59147787, 32.89691162, 2.78875732, 32.96739197),
    (-0.63542348, -0.47652262, 2.59132934, 32.89664078, 2.78902411, 32.96726990),
)

# Exact authored SCREEN_DU_NDL/NDR bounds, in the same source coordinates.
ND_SCREEN_SURFACES = (
    (0.28937626, 0.44796520, 2.59150696, 32.89669418, 2.78888965, 32.96720886),
    (-0.44501489, -0.28624520, 2.59113622, 32.89653778, 2.78925443, 32.96731567),
)

# Exact authored SCREEN_DU_EWD bounds in the normalized cockpit source.
EWD_SCREEN_SURFACE = (
    -0.07425891, 0.08420876, 2.59033060, 32.90043259,
    2.78766751, 32.97093201,
)

# Exact authored SCREEN_DU_MFDL/MFDR bounds, source coordinates before the
# aircraft attachment's 180-degree rotation (same convention as PFD/ND/EWD;
# docs/mfd-spec/09b-cockpit-geometry.md section 2.1).
# Unlike the main-panel displays (19.4 deg back from vertical) the MFDs are
# tilted 54.0 deg back: never use add_quad's default 0.347/0.938 face basis.
MFD_SCREEN_SURFACES = (
    (0.11171729, 0.27178246, 2.42302714, 32.69813615, 2.54697620, 32.86913103),   # CAPT, SCREEN_DU_MFDL
    (-0.27272221, -0.11295360, 2.42319904, 32.69832317, 2.54691513, 32.86900612),  # F/O,  SCREEN_DU_MFDR
)
# System Display between the MFDs (not part of the MFD feature; recorded for later).
SD_SCREEN_SURFACE = (
    -0.08096574, 0.07879338, 2.42330256, 32.69827463, 2.54699698, 32.86892929,
)
MFD_SCREEN_SIZE = (768, 1024)   # device texels, portrait
# Explicit seat-view corners (BL, TL, TR, BR), like EFB_SCREEN_SURFACES. They
# are exactly the 6-tuple combinations: positive source X is the crew's left.
MFD_SCREEN_CORNERS = tuple(
    ((x1, y0, z0), (x1, y1, z1), (x0, y1, z1), (x0, y0, z0))
    for x0, x1, y0, z0, y1, z1 in MFD_SCREEN_SURFACES
)

# X-Plane plugin cockpit devices (docs/mfd-spec/08-xplane-cockpit-device.md
# section 10.1). The IDs must equal avionics_provider.MFD_DEVICES.
MFD_DEVICE_IDS = ("a380x.xp12.mfd.captain", "a380x.xp12.mfd.first_officer")
MFD_DEVICE_BUS = 1                 # bitfield: bus 0
MFD_LIGHTING_CHANNELS = (14, 15)   # instrument_brightness_ratio custom slots
MFD_AUTO_ADJUST = 1

# Exact authored SCREEN_ISIS_1 face (material SCREEN_ISIS_1 of DYN_SCREEN, vertices 0-3) in the
# same source convention as PFD_SCREEN_SURFACES (tools/isis_cockpit_device.py --check re-measures
# it and SCREEN_DU_PFDL). 65.8 x 64.7 mm, tilted 19.4 deg back like the main panel. ISIS 2 is not
# fitted (FBW panel.cfg leaves SCREEN_ISIS_2 disabled).
ISIS_SCREEN_SURFACE = (0.12583287, 0.19165216, 2.73086953, 32.94198859, 2.79184201, 32.96387410)
ISIS_SCREEN_SIZE = (1024, 1024)   # device texels: FBW's 512-unit SVG at 2 texels per unit
# Lower unit: SCREEN_ISIS_2 face (DYN_SCREEN vertices 4-7). FBW leaves its gauge disabled; this
# port drives it as the second ISIS (standby navigation display by default).
ISIS2_SCREEN_SURFACE = (0.12655064, 0.19165975, 2.64409503, 32.91099411, 2.7044096, 32.93264347)
