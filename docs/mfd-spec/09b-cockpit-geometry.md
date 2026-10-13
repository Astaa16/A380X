# MFD spec 09b: cockpit geometry for the MFD screens and the KCCUs

Status: measured on 2026-10-06 from the pinned LOD00 cockpit glTF. Companion to `09-port-integration.md`, which
says how to emit these values. Everything here is read-only analysis. The script that produced every number is
in §7. It imports the loader from `tools/build_cockpit_controls.py` without modifying it.

Source model:
`upstream-aircraft-large-files/fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire/Part_Interior_Cockpit/model/A380_COCKPIT_LOD00.gltf`.
The buffer is split into `A380_COCKPIT_LOD00.bin.part01` (104,857,600 B) and `.part02` (855,024 B).
`build_cockpit_controls.load_buffer()` joins `*.part[0-9][0-9]` when the plain `.bin` is absent.

---

## 1. Coordinate convention (proved)

The values are **source OBJ coordinates**: glTF world coordinates (node TRS chain applied, metres), taken **before**
the aircraft attachment's 180° heading rotation. The cockpit-controls attachment is `P _obja/118`, with
`_v10_att_psi_ref 180`, `x/y = 0` and `_v10_att_z_acf_prt_ref 115.108`. The OBJ files store these coordinates
verbatim.

* +Y is up. The seated crew looks toward **+Z**; the main panel is at z ≈ 32.9 and the KCCUs are at z ≈ 32.5.
* **+X is the crew's LEFT** (the captain's side). This is the glTF convention: "+Z forward, −X right".
* A 6-tuple `(x0, x1, y0, z0, y1, z1)` in `tools/avionics_panel_layout.py` is exactly the axis-aligned bounding box
  of the screen node's four vertices: `(min x, max x, min y, min z, max y, max z)`.
  * Each screen is a 2-triangle quad with no yaw and no roll. Its lower edge is at (y0, z0) and its upper edge at
    (y1, z1).
  * The authored corners are therefore `BL = (x1, y0, z0)`, `TL = (x1, y1, z1)`, `TR = (x0, y1, z1)` and
    `BR = (x0, y0, z0)`. BL/TL/TR/BR are as seen by the seated pilot. The pilot's left is the larger X.

Reproduction of the published tuples (`six_tuple()` in §7 against `avionics_panel_layout.py`):

| Node | Computed `(x0, x1, y0, z0, y1, z1)` | max \|diff\| vs layout |
|---|---|---|
| `SCREEN_DU_PFDL` | (0.48004797, 0.63861006, 2.59147787, 32.89691162, 2.78875732, 32.96739197) | 5.0e-9 |
| `SCREEN_DU_PFDR` | (-0.63542348, -0.47652262, 2.59132934, 32.89664078, 2.78902411, 32.96726990) | 4.6e-9 |
| `SCREEN_DU_NDL` | (0.28937626, 0.44796520, 2.59150696, 32.89669418, 2.78888965, 32.96720886) | 4.7e-9 |
| `SCREEN_DU_NDR` | (-0.44501489, -0.28624520, 2.59113622, 32.89653778, 2.78925443, 32.96731567) | 4.0e-9 |
| `SCREEN_DU_EWD` | (-0.07425891, 0.08420876, 2.59033060, 32.90043259, 2.78766751, 32.97093201) | 3.3e-9 |

All five match to float32 rounding. Each is a 4-vertex, 2-triangle mesh with its own material. The convention is
confirmed.

---

## 2. MFD (and SD) screen surfaces

### 2.1 Constants to add to `tools/avionics_panel_layout.py`

```python
# Exact authored SCREEN_DU_MFDL/MFDR bounds, source coordinates before the
# aircraft attachment's 180-degree rotation (same convention as PFD/ND/EWD).
# Unlike the main-panel displays (19.4 deg back from vertical) the MFDs are
# tilted 54.0 deg back: never use add_quad's default 0.347/0.938 face basis.
MFD_SCREEN_SURFACES = (
    (0.11171729, 0.27178246, 2.42302714, 32.69813615, 2.54697620, 32.86913103),   # CAPT, SCREEN_DU_MFDL
    (-0.27272221, -0.11295360, 2.42319904, 32.69832317, 2.54691513, 32.86900612), # F/O,  SCREEN_DU_MFDR
)
# System Display between the MFDs (not part of this feature; recorded for later).
SD_SCREEN_SURFACE = (
    -0.08096574, 0.07879338, 2.42330256, 32.69827463, 2.54699698, 32.86892929,
)
MFD_SCREEN_SIZE = (768, 1024)   # device texels, portrait
# Spec 08 section 10.3 asks for explicit seat-view corners (BL, TL, TR, BR), like
# EFB_SCREEN_SURFACES. They are exactly the 6-tuple combinations of section 1:
MFD_SCREEN_CORNERS = tuple(
    ((x1, y0, z0), (x1, y1, z1), (x0, y1, z1), (x0, y0, z0))
    for x0, x1, y0, z0, y1, z1 in MFD_SCREEN_SURFACES
)
```

### 2.2 Corners as seen by the seated pilot (authored vertices, exact)

| Screen | bottom-left | top-left | top-right | bottom-right |
|---|---|---|---|---|
| `SCREEN_DU_MFDL` (CAPT) | (0.27178246, 2.42302714, 32.69813615) | (0.27178246, 2.54697620, 32.86913103) | (0.11171733, 2.54697620, 32.86913103) | (0.11171729, 2.42302714, 32.69813615) |
| `SCREEN_DU_MFDR` (F/O) | (-0.11295363, 2.42319904, 32.69832317) | (-0.11295360, 2.54691513, 32.86900612) | (-0.27272215, 2.54691513, 32.86900612) | (-0.27272221, 2.42319904, 32.69832317) |
| `SCREEN_DU_SD` | (0.07879335, 2.42330256, 32.69827463) | (0.07879338, 2.54699698, 32.86892929) | (-0.08096571, 2.54699698, 32.86892929) | (-0.08096574, 2.42330256, 32.69827463) |

The authored vertex order is BR, BL, TL, TR with triangles (0,1,2) and (0,2,3). This is the same order and winding
as `add_quad()` in `build_cockpit_controls.py`.

### 2.3 Plane, size, aspect, orientation

| Property | MFDL | MFDR | SD |
|---|---|---|---|
| Authored normal (toward crew) | (0, 0.809347, −0.587331) | same | same |
| Screen "up" (direction of increasing device y) | (0, 0.587331, 0.809347) | same | same |
| Screen "right" (pilot's right) | (−1, 0, 0) | same | same |
| Tilt | face 54.03° back from vertical (35.97° above horizontal); yaw 0; roll 0 | same | same |
| Centre | (0.191750, 2.485002, 32.783634) | (−0.192838, 2.485057, 32.783665) | (−0.001086, 2.485150, 32.783602) |
| Width × height | 160.07 × 211.19 mm | 159.77 × 210.80 mm | 159.76 × 210.77 mm |
| Aspect w/h | 0.7579 | 0.7579 | 0.7580 |
| Planarity | 0.11 mm | 0.12 mm | 0.12 mm |
| Orientation | **portrait** | portrait | portrait |
| Texel pitch at 768 × 1024 | 4.798 px/mm horizontal, 4.849 px/mm vertical | 4.807 / 4.858 | – |

* **Aspect against 768 × 1024 (0.75):** mapping the whole device texture onto the face stretches it horizontally by
  1.06 %. The PFD has the same stretch (0.7569). Do not letterbox.
* **Position.** All three lower displays sit below the main-panel row (PFD/ND/EWD bottom edge y ≈ 2.59) and above
  the KCCUs. Their bottom edge is at z ≈ 32.698 and the KCCU front edge is at z ≈ 32.645.
* **Normal check.** The normal derived from the 6-tuple (`vertical = unit(0, y1−y0, z1−z0)`,
  `normal = (0, vertical.z, −vertical.y)`) is within 0.03° of the authored normal for both MFDs.
* **Wrong default basis.** `add_quad()`'s default basis (`normal (0, 0.347, −0.938)`, `vertical (0, 0.938, 0.347)`)
  describes a face tilted 20.3° back. Using it for an MFD would put the quad's top and bottom edges about ±58 mm out
  of the screen plane. For the PFD/ND/EWD the same default is only 0.86° off their real 19.44°: about ±1.6 mm at the
  edges, which the 2 mm offset absorbs.

### 2.4 Backlight and glass faces

| Node | Material | Size | Offset along normal from the screen face |
|---|---|---|---|
| `SCREEN_BACKLIGHT_DU_MFDL` | `SCREEN_DISPLAY_BACKLIGHT` | 160.16 × 211.32 mm | +0.55 mm (toward crew) |
| `SCREEN_BACKLIGHT_DU_MFDR` | `SCREEN_DISPLAY_BACKLIGHT` | 159.86 × 210.93 mm | +0.66 mm |
| `SCREEN_BACKLIGHT_DU_SD` | `SCREEN_DISPLAY_BACKLIGHT` | 159.85 × 210.89 mm | +0.43 mm |

All three are 4-vertex quads with the same normal as their screen.

---

## 3. Device-texture UV orientation (upright, not mirrored)

### 3.1 Required mapping

X-Plane plugin devices draw in texels with the origin at the **bottom-left and y up** (`XPLMDisplay.h:298-305`). On
an `ATTR_cockpit_device` triangle, UV (0,0) is device texel (0,0) and UV (1,1) is the top-right texel (768, 1024).
For an upright, unmirrored image the corners as seen by the pilot must therefore get:

| Corner (seated view) | Source X side | UV |
|---|---|---|
| bottom-left | larger X (crew-left) | (0, 0) |
| top-left | larger X | (0, 1) |
| top-right | smaller X (crew-right) | (1, 1) |
| bottom-right | smaller X | (1, 0) |

### 3.2 Why U is reversed against source X: the PFD precedent

`add_quad()` builds vertex 0 at `cx − width/2` (the **smaller** source X), then `cx + width/2`, then the upper two.
Source +X is crew-left, so vertex 0 is the pilot's **bottom-right**. The PFD/ND/EWD/clock/EFIS calls therefore pass
`uv = (panel_right, panel_bottom, panel_left, panel_top)` (`build_cockpit_controls.py:772-783`). The comment there
says: "Source X is reversed by the aircraft attachment's 180-degree heading rotation, so map the lower source X to
the panel's right. … Reversing V here turns the live PFD upside down."

V is not reversed. Vertices 0/1 are on the physically lower edge (−`face_vertical`), and both panel and device v
grow upward.

The flyPad is the counter-example that proves the rule. `EFB_SCREEN_SURFACES` lists explicit corners already ordered
BL, TL, TR, BR in the seated view, so `add_efb_surface()` uses U = left→right with **no** reversal
(`build_cockpit_controls.py:839-861`).

For the MFD device the same rule gives `uv = (u0, v0, u1, v1) = (1.0, 0.0, 0.0, 1.0)` when calling `add_quad`.

### 3.3 Independent evidence

* **Authored MFD UVs.** The authored MFD faces already carry MSFS UVs with this orientation, as exported in the
  packaged `objects/interior/cockpit-panels/screen_powered_off_display_faces.obj`.
  * MFDL: u = 0 at x = 0.27178 (crew-left) and u = 0.4675 at x = 0.11172. v = 0 on the lower edge and 1 on the upper
    edge.
  * MFDR: u = 0.5327 at x = −0.11295 (crew-left) and u = 1.0 at x = −0.27272.
  * This is the single 1646-px MSFS texture shared by both MFDs (spec 01 §1.1). CAPT occupies u 0–0.4675 and F/O
    occupies u 0.5327–1. Both read left→right as the pilot sees them, with v up.
* **Laminar's G1000 device quads.** `Aircraft/Laminar Research/Cirrus SR22/objects/garmins.obj` uses
  `ATTR_cockpit_device G1000_PFD1 4 -1 1`. In aircraft coordinates (+X right), u = 0 is at the smaller X (the
  pilot's left) and v = 0 at the lower edge. UVs span exactly 0..1.

### 3.4 The proposed quad (prototype output, §7.2)

Spec 08 (§2.5 and §10.3) is normative for **where** the quads go. The visible device quad goes in a new attached
object, `a380x_mfd_screens.obj`. Its touch twin, raised 1 mm, goes in the draw-disabled block of
`a380x_cockpit_controls.obj`. Spec 08 also writes the indices as (BL, TL, TR) and (BL, TR, BR).

The corner positions, normal and UVs below are valid for either placement and either vertex order:

* For the visible quad, offset the corners along the normal by 2 mm, the same as the PFD quads.
* For the touch quad, use 1 mm, as spec 08 asks.

The quad uses `add_quad(centre, x1−x0, hypot(y1−y0, z1−z0), uv=(1,0,0,1), offset 0.002, normal, vertical)` with the
6-tuple-derived basis.

| Vertex | CAPT `a380x.xp12.mfd.captain` (x, y, z) | F/O `a380x.xp12.mfd.first_officer` (x, y, z) | UV | Seated view |
|---|---|---|---|---|
| 0 | (0.11171729, 2.42464646, 32.69696235) | (−0.27272221, 2.42481839, 32.69714942) | (1, 0) | bottom-right |
| 1 | (0.27178246, 2.42464646, 32.69696235) | (−0.11295360, 2.42481839, 32.69714942) | (0, 0) | bottom-left |
| 2 | (0.27178246, 2.54859552, 32.86795723) | (−0.11295360, 2.54853448, 32.86783237) | (0, 1) | top-left |
| 3 | (0.11171729, 2.54859552, 32.86795723) | (−0.27272221, 2.54853448, 32.86783237) | (1, 1) | top-right |

* Normals: CAPT (0, 0.809660, −0.586899); F/O (0, 0.809676, −0.586877).
* Indices are (0,1,2) and (0,2,3). `cross(b−a, c−a)·normal` is −0.0338 for CAPT and −0.0337 for F/O, so both quads
  are clockwise. That is the OBJ8 front face that `validate_aircraft.py:438-453` expects.
* Every corner sits 1.94–2.06 mm in front of the authored face along the normal, like the PFD quads.
* Device mouse coordinates come back through the same UVs: texel x = u·768 and texel y = v·1024 (y up). The MFD core
  converts with `y_canvas = 1023 − y` (spec 01 §12.5).

---

## 4. Are the MFD faces exported as dark faces today?

* **`SCREEN_DU_MFDL` and `SCREEN_DU_MFDR`: yes.** Both use the single material `SCREEN_DU_MFD`.
  * `blender/prepare_dark_display_faces.py:20-26` lists `SCREEN_DU_MFD` in `SCREEN_MATERIALS`, together with
    `SCREEN_DU_SD`, the PFD/ND/EWD, `SCREEN_EFB`, the OITs, the ISIS units, the RMPs and others. The manifest
    `/Volumes/MacGames/A380X-XPlane12-build/manifests/cockpit-screen-exports.json` confirms the list.
  * The packaged `objects/interior/cockpit-panels/screen_powered_off_display_faces.obj` contains exactly four vertices
    at each MFD's authored corners. It is opaque, textured `powered_off_display_faces.png` (RGB 5, 6, 8) and drawn
    as attachment 117.
  * The cockpit-controls OBJ (attachment 118) is drawn after it, so the device quad overlays the dark face the same
    way the PFD/ND quads do.
  * **Keep the dark face.** It is what shows when the device is not drawn, and when the plugin is absent. A DU that is
    off draws black, which looks the same.
* **`SCREEN_BACKLIGHT_DU_MFDL` and `SCREEN_BACKLIGHT_DU_MFDR`: no.** Their material `SCREEN_DISPLAY_BACKLIGHT` is not
  in any export manifest: `cockpit-exports`, `cockpit-panel-exports`, `cockpit-decal-exports` and
  `cockpit-screen-exports` were all checked. The display-glass materials (`A380_DISPLAY_GLASS_SET1-4`, `_MISC`,
  `DISPLAY_RAINBOW_01_CHECKED`) are not exported either.
  * Nothing sits between the crew and the screen face. A scan of all six packaged `decal_*.obj` files plus
    `panel_a380_cockpit_pedestal01_4k.obj`, `mip03` and `mip04` found **no vertex** inside either MFD opening within
    0–4 mm in front of the face.
* **Config.** `config/cockpit_controls.json` has no screen nodes, and `test_cockpit_controls.py:517-518` asserts that
  no `SCREEN_EFB*` node is configured. No config change is needed for the dark faces.

---

## 5. KCCU geometry

### 5.1 Overview

The two KCCUs sit on the pedestal directly aft of the MFD/SD row. Supporting nodes:

* `KCCU` is the single body mesh for both units (material `A380_COCKPIT_PEDESTAL01_4k`, 17,995 tris). Its bounds are
  lo (−0.2544, 2.3881, 32.4287) and hi (0.2544, 2.5020, 32.6450).
* `KCCU_COVER_BALL` holds both trackball rings (6,066 tris): lo (−0.2258, 2.4115, 32.4809), hi (0.2258, 2.4798,
  32.5690).
* `PUSH_KCCUL_WHEEL.001` is an empty node with no mesh and no children (an animation helper). Ignore it.

Every interactive node is a root node named `PUSH_KCCU{L|R}_<KEY>`:

* **L** = captain (+X side) and **R** = first officer (−X side).
* Each node has an indexed-triangle mesh in `A380_COCKPIT_PEDESTAL_KNOBS01_4k`. That geometry is already drawn by
  the static export `objects/interior/cockpit-panels/panel_a380_cockpit_pedestal_knobs01_4k.obj`, which has no KCCU
  animation.
* The 63 legend keys each have a `…_SEQ1` sibling: an emissive legend decal in `A380X_COCKPIT_DECAL_EMIS`, 32 tris.
  Never use a `SEQ1` node as a click target.
* 143 KCCU nodes carry glTF clips (`<node>_ANIM`):
  * keys: a 2-key translation, pressed by 1.15 mm along the key axis;
  * `WHEEL`: an 11-key full rotation;
  * `KBD_ON_OFF` / `CCD_ON_OFF`: a 3-key rotation about X from −23.0° (the rest pose) to +14.9° (two-position
    levers).

Per side there are 74 interactive nodes: 63 legend keys, the trackball, the wheel, the side button, two switches and
six blank caps.

**Layout.** The table below groups keys by z row, from the MFD toward the crew. Within each row, keys are listed from
the crew's left to right; listing order is decreasing x on both sides.

| z (row) | Keys, crew-left → right (both units) |
|---|---|
| 32.631 / 32.629 / 32.628 | `UP`; `ESC`; `DIR PERF INIT NAVAID MAILBOX` |
| 32.623 / 32.616 | `LEFT RIGHT`; `CLRINFO DOWN FPLN DEST SECINDEX SURV ATCCOM ND` |
| 32.603 | `Q W E R T Y U I O P BACKSPACE` |
| 32.590 | `A S D F G H J K L BLANK1` |
| 32.577 | `SLASH Z X C V B N M SP ENT` |
| 32.560–32.518 | numeric pad `1 2 3` / `4 5 6` / `7 8 9` / `DOT 0 PLUSMINUS`, with `ESC2 KBD` and `REWIND FORWARD` beside it and `WHEEL` (z 32.548) |
| 32.504–32.490 | `BLANK2 BLANK3`, `BALL` (z 32.499), `BLANK4 BLANK5 BLANK6` |
| 32.470 | `LBUTTON` (outboard side face) |
| 32.453 | `KBD_ON_OFF` and `CCD_ON_OFF` levers (aft face) |

* **The upper key field is translated, not mirrored.** The function, QWERTY and page-key field is an identical copy:
  CAPT Q = 0.2416 → F/O Q = −0.0876, an offset of −0.3292 m. Reading order is the same on both units.
* **The lower cursor-control block is mirrored about the centreline.** This covers the numeric pad, `ESC2`/`KBD`,
  `REWIND`/`FORWARD`, wheel, ball, `LBUTTON` and the two levers.
  * The numeric pad is inboard on both units (CAPT x 0.097–0.126, F/O x −0.097 to −0.126). It still reads `1 2 3`
    left to right on both.
  * The trackball (x ±0.1883) and `LBUTTON` (x ±0.2423) are outboard on both.
  * `CCD_ON_OFF` is outboard of `KBD_ON_OFF` on both (CAPT CCD 0.1252 / KBD 0.1006; F/O CCD −0.1258 / KBD −0.1005).
* **Slope.** The key field rises toward the MFD by about 15°, from the DOT row (y 2.4165, z 32.518) to the DIR row
  (y 2.4453, z 32.628).

### 5.2 Special controls

| Node | What it is | Geometry (CAPT; F/O mirrors x) | Clip |
|---|---|---|---|
| `PUSH_KCCU{L,R}_BALL` | Trackball | Ø 45.2 mm sphere, centre (±0.1883, 2.4353, 32.4990). Only the top cap protrudes through `KCCU_COVER_BALL` (y max of the ring ≈ 2.4798). 960 tris. | none |
| `PUSH_KCCU{L,R}_WHEEL` | Scroll wheel. The disc is 17.6 mm thick along X and about 31 mm in diameter in Y/Z, so it rolls fore/aft about the lateral axis. | centre (±0.1645, 2.4097, 32.5482), 508 tris | `…_WHEEL_ANIM`, rotation, 11 keys |
| `PUSH_KCCU{L,R}_LBUTTON` | Cursor select / click button on the outboard side face | 7.1 (x) × 35.8 (y) × 23.0 (z) mm, centre (±0.2423, 2.4403, 32.4699), 479 tris | none |
| `PUSH_KCCU{L,R}_KBD_ON_OFF` | KBD ON/OFF two-position lever (FBW `L:A32NX_KCCU_<L/R>_KBD_ON_OFF`) | 5.2 × 16.6 × 9.7 mm, CAPT (0.1006, 2.4078, 32.4529), F/O (−0.1005, 2.4077, 32.4531), 594 tris | rotation about X, −23.0° → +14.9° (F/O −24.0° → +15.3°) |
| `PUSH_KCCU{L,R}_CCD_ON_OFF` | CCD ON/OFF two-position lever (FBW `L:A32NX_KCCU_<L/R>_CCD_ON_OFF`) | 5.2 × 16.6 × 9.7 mm, CAPT (0.1252, 2.4078, 32.4531), F/O (−0.1258, 2.4077, 32.4529), 594 tris | rotation about X, same as KBD |
| `PUSH_KCCU{L,R}_BLANK1..6` | Blank caps (no FBW event) | see table §5.3 | keys |

### 5.3 Every KCCU node, both sides

The centre is the bounding-box centre. Bounds are `centre ± size/2`; the sizes are identical on both sides to within
0.2 mm. "tris" is the mesh triangle count, with the same value on both sides. The legend node is
`PUSH_KCCU{L,R}_<KEY>_SEQ1`.

| Key (`PUSH_KCCU{L,R}_…`) | CAPT `L` centre (x, y, z) | F/O `R` centre (x, y, z) | bbox size x×y×z mm | tris | legend node |
|---|---|---|---|---|---|
| `0` | (0.1114, 2.4165, 32.5176) | (-0.1115, 2.4165, 32.5178) | 10.3×4.3×10.6 | 252 | `_SEQ1` |
| `1` | (0.1263, 2.4204, 32.5597) | (-0.0966, 2.4204, 32.5599) | 10.3×4.3×10.6 | 252 | `_SEQ1` |
| `2` | (0.1114, 2.4204, 32.5597) | (-0.1115, 2.4204, 32.5599) | 10.3×4.3×10.6 | 252 | `_SEQ1` |
| `3` | (0.0965, 2.4204, 32.5597) | (-0.1263, 2.4204, 32.5599) | 10.3×4.3×10.6 | 252 | `_SEQ1` |
| `4` | (0.1263, 2.4191, 32.5456) | (-0.0966, 2.4191, 32.5459) | 10.3×4.3×10.6 | 252 | `_SEQ1` |
| `5` | (0.1114, 2.4191, 32.5456) | (-0.1115, 2.4191, 32.5459) | 10.3×4.3×10.6 | 252 | `_SEQ1` |
| `6` | (0.0965, 2.4191, 32.5456) | (-0.1263, 2.4191, 32.5459) | 10.3×4.3×10.6 | 252 | `_SEQ1` |
| `7` | (0.1263, 2.4178, 32.5316) | (-0.0966, 2.4178, 32.5318) | 10.3×4.3×10.6 | 252 | `_SEQ1` |
| `8` | (0.1114, 2.4178, 32.5316) | (-0.1115, 2.4178, 32.5318) | 10.3×4.3×10.6 | 252 | `_SEQ1` |
| `9` | (0.0965, 2.4178, 32.5316) | (-0.1263, 2.4178, 32.5318) | 10.3×4.3×10.6 | 252 | `_SEQ1` |
| `A` | (0.2345, 2.4294, 32.5899) | (-0.0948, 2.4294, 32.5899) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `B` | (0.1678, 2.4243, 32.5774) | (-0.1614, 2.4243, 32.5774) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `C` | (0.1973, 2.4243, 32.5774) | (-0.1319, 2.4243, 32.5774) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `D` | (0.2049, 2.4294, 32.5899) | (-0.1243, 2.4294, 32.5899) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `E` | (0.2121, 2.4345, 32.6031) | (-0.1171, 2.4345, 32.6031) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `F` | (0.1901, 2.4294, 32.5899) | (-0.1391, 2.4294, 32.5899) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `G` | (0.1754, 2.4294, 32.5899) | (-0.1538, 2.4294, 32.5899) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `H` | (0.1606, 2.4294, 32.5899) | (-0.1686, 2.4294, 32.5899) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `I` | (0.1382, 2.4346, 32.6030) | (-0.1910, 2.4346, 32.6030) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `J` | (0.1458, 2.4294, 32.5899) | (-0.1834, 2.4294, 32.5899) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `K` | (0.1311, 2.4294, 32.5899) | (-0.1981, 2.4294, 32.5899) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `L` | (0.1163, 2.4294, 32.5899) | (-0.2129, 2.4294, 32.5899) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `M` | (0.1382, 2.4243, 32.5774) | (-0.1910, 2.4243, 32.5774) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `N` | (0.1530, 2.4243, 32.5774) | (-0.1762, 2.4243, 32.5774) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `O` | (0.1235, 2.4346, 32.6030) | (-0.2057, 2.4346, 32.6030) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `P` | (0.1087, 2.4345, 32.6031) | (-0.2205, 2.4345, 32.6031) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `Q` | (0.2416, 2.4345, 32.6031) | (-0.0876, 2.4345, 32.6031) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `R` | (0.1973, 2.4345, 32.6031) | (-0.1319, 2.4345, 32.6031) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `S` | (0.2197, 2.4294, 32.5899) | (-0.1095, 2.4294, 32.5899) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `T` | (0.1826, 2.4345, 32.6031) | (-0.1466, 2.4345, 32.6031) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `U` | (0.1530, 2.4346, 32.6030) | (-0.1762, 2.4346, 32.6030) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `V` | (0.1826, 2.4243, 32.5774) | (-0.1467, 2.4243, 32.5774) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `W` | (0.2269, 2.4346, 32.6030) | (-0.1023, 2.4346, 32.6030) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `X` | (0.2121, 2.4243, 32.5774) | (-0.1171, 2.4243, 32.5774) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `Y` | (0.1678, 2.4345, 32.6031) | (-0.1614, 2.4345, 32.6031) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `Z` | (0.2269, 2.4243, 32.5774) | (-0.1023, 2.4243, 32.5774) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `DOT` | (0.1263, 2.4165, 32.5176) | (-0.0966, 2.4165, 32.5178) | 10.3×4.3×10.6 | 252 | `_SEQ1` |
| `PLUSMINUS` | (0.0965, 2.4165, 32.5176) | (-0.1263, 2.4165, 32.5178) | 10.3×4.3×10.6 | 252 | `_SEQ1` |
| `ENT` | (0.1025, 2.4251, 32.5771) | (-0.2267, 2.4251, 32.5771) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `BACKSPACE` | (0.0918, 2.4347, 32.6026) | (-0.2374, 2.4347, 32.6026) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `SP` | (0.1217, 2.4247, 32.5769) | (-0.2075, 2.4247, 32.5769) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `SLASH` | (0.2416, 2.4243, 32.5774) | (-0.0876, 2.4243, 32.5774) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `ESC` | (0.2316, 2.4443, 32.6290) | (-0.0976, 2.4443, 32.6290) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `ESC2` | (0.2323, 2.4208, 32.5593) | (-0.2176, 2.4208, 32.5593) | 10.0×3.7×9.8 | 216 | `_SEQ1` |
| `UP` | (0.2096, 2.4453, 32.6306) | (-0.1196, 2.4453, 32.6306) | 11.7×4.6×7.2 | 180 | `_SEQ1` |
| `DOWN` | (0.2096, 2.4395, 32.6158) | (-0.1196, 2.4395, 32.6158) | 11.7×4.6×7.2 | 180 | `_SEQ1` |
| `LEFT` | (0.2176, 2.4424, 32.6231) | (-0.1116, 2.4424, 32.6231) | 7.0×6.3×11.7 | 180 | `_SEQ1` |
| `RIGHT` | (0.2016, 2.4424, 32.6231) | (-0.1276, 2.4424, 32.6231) | 7.0×6.3×11.7 | 180 | `_SEQ1` |
| `DIR` | (0.1860, 2.4453, 32.6284) | (-0.1432, 2.4453, 32.6284) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `PERF` | (0.1673, 2.4453, 32.6284) | (-0.1619, 2.4453, 32.6284) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `INIT` | (0.1488, 2.4453, 32.6284) | (-0.1804, 2.4453, 32.6284) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `NAVAID` | (0.1299, 2.4453, 32.6284) | (-0.1993, 2.4453, 32.6284) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `MAILBOX` | (0.1109, 2.4453, 32.6284) | (-0.2183, 2.4453, 32.6284) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `FPLN` | (0.1860, 2.4403, 32.6156) | (-0.1432, 2.4403, 32.6156) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `DEST` | (0.1673, 2.4403, 32.6156) | (-0.1619, 2.4403, 32.6156) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `SECINDEX` | (0.1488, 2.4403, 32.6156) | (-0.1804, 2.4403, 32.6156) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `SURV` | (0.1299, 2.4403, 32.6156) | (-0.1993, 2.4403, 32.6156) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `ATCCOM` | (0.1109, 2.4403, 32.6156) | (-0.2183, 2.4403, 32.6156) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `ND` | (0.0940, 2.4402, 32.6159) | (-0.2352, 2.4402, 32.6159) | 10.0×6.2×9.9 | 216 | `_SEQ1` |
| `KBD` | (0.2177, 2.4207, 32.5593) | (-0.2322, 2.4207, 32.5593) | 10.0×3.7×9.8 | 216 | `_SEQ1` |
| `CLRINFO` | (0.2336, 2.4396, 32.6159) | (-0.0956, 2.4396, 32.6159) | 14.0×5.8×9.9 | 216 | `_SEQ1` |
| `REWIND` | (0.2322, 2.4196, 32.5454) | (-0.2176, 2.4196, 32.5454) | 10.0×3.7×9.8 | 216 | `_SEQ1` |
| `FORWARD` | (0.2177, 2.4195, 32.5454) | (-0.2322, 2.4195, 32.5454) | 10.0×3.7×9.8 | 216 | `_SEQ1` |
| `WHEEL` | (0.1645, 2.4097, 32.5482) | (-0.1645, 2.4097, 32.5482) | 17.6×30.6×31.1 | 508 | – |
| `LBUTTON` | (0.2423, 2.4403, 32.4699) | (-0.2423, 2.4403, 32.4699) | 7.1×35.8×23.0 | 479 | – |
| `BALL` | (0.1883, 2.4353, 32.4990) | (-0.1883, 2.4353, 32.4990) | 45.2×45.2×45.2 | 960 | – |
| `KBD_ON_OFF` | (0.1006, 2.4078, 32.4529) | (-0.1005, 2.4077, 32.4531) | 5.2×16.6×9.7 | 594 | – |
| `CCD_ON_OFF` | (0.1252, 2.4078, 32.4531) | (-0.1258, 2.4077, 32.4529) | 5.2×16.6×9.7 | 594 | – |
| `BLANK1` | (0.0990, 2.4296, 32.5900) | (-0.2302, 2.4296, 32.5900) | 14.0×5.8×9.9 | 216 | – |
| `BLANK2` | (0.1238, 2.4161, 32.5044) | (-0.0983, 2.4161, 32.5044) | 14.0×3.2×9.9 | 216 | – |
| `BLANK3` | (0.0983, 2.4161, 32.5041) | (-0.1239, 2.4161, 32.5041) | 14.0×3.2×9.9 | 216 | – |
| `BLANK4` | (0.1257, 2.4145, 32.4904) | (-0.0965, 2.4145, 32.4904) | 10.0×3.7×9.8 | 216 | – |
| `BLANK5` | (0.1113, 2.4145, 32.4904) | (-0.1112, 2.4145, 32.4904) | 10.0×3.7×9.8 | 216 | – |
| `BLANK6` | (0.0964, 2.4145, 32.4904) | (-0.1258, 2.4145, 32.4904) | 10.0×3.7×9.8 | 216 | – |

The table rows were generated from `geometry.json` (§7.1, `kccu` key).

Click-target cost. Mapping every node except the six blanks per side (136 nodes in total) adds **34,062 triangles and
27,527 vertices**: about 2.73 MB of OBJ text. This was measured by a dry run of the unmodified `build_obj()`
(spec 09 §4.4).

---

## 6. Pull-out keyboards (OIT)

* **Captain.** 88 named key nodes `PUSH_KBD_CPT_*` exist, plus their `_SEQ1`/`_SEQ2` legends: a full PC layout with
  F1–F12, arrows, CTRL/ALT/SHIFT, OIS MENU/XFR and mouse buttons.
  * They are children of an animated pull-out chain `Cube.034 → Circle.017 → Cube.041 → KEYBOARD_LH`.
  * Their rest pose is the **stowed** position under the main panel: bounds lo (0.3853, 2.5169, 32.8917), hi
    (0.6749, 2.5525, 32.9818).
  * In total they hold 17,789 tris and 19,662 vertices.
* **First officer.** There are **no** `PUSH_KBD_FO_*` nodes. The F/O keyboard (`KEYBOARD_LH.001`, 134 descendant
  nodes) has no individually named keys; it uses merged `Keyboard_base.*` meshes.
* Consequences are discussed in spec 09 §4.6. In short: do not map these keyboards to the MFD.

---

## 7. Scripts (inline, reproducible)

Location: `/private/tmp/a380x-geometry/` (throwaway). Nothing in the port is modified. The scripts set
`sys.dont_write_bytecode = True`, so no `__pycache__` is written into `tools/`; the existing `.pyc` timestamps were
checked afterwards. A run takes about 30 s and reads the 105 MB buffer once.

```sh
cd /private/tmp/a380x-geometry && python3 cockpit_geometry.py > run.txt   # writes geometry.json
python3 mfd_quad_proto.py                                                 # §3.4 prototype
```

### 7.1 `cockpit_geometry.py`

```python
#!/usr/bin/env python3
"""Read-only geometry probe for the A380X MFD/KCCU integration spec (09b).

Reuses the glTF loader of tools/build_cockpit_controls.py by import (no project
file is modified; bytecode writing is disabled so no __pycache__ appears in
tools/). All coordinates are source glTF/OBJ coordinates *before* the aircraft
attachment's 180-degree heading rotation (the convention of
tools/avionics_panel_layout.py): +Y up, the seated crew faces +Z, and +X is the
crew's LEFT (captain side).
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

sys.dont_write_bytecode = True
PORT = Path("/Volumes/Savs' drive/A380X-XPlane12/xplane-port")
sys.path.insert(0, str(PORT / "tools"))
import build_cockpit_controls as bcc  # noqa: E402
import avionics_panel_layout as layout  # noqa: E402

SOURCE = Path(
    "/Volumes/Savs' drive/A380X-XPlane12/upstream-aircraft-large-files/fbw-a380x/src/base/"
    "flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire/"
    "Part_Interior_Cockpit/model/A380_COCKPIT_LOD00.gltf")


class Model:
    def __init__(self, path: Path):
        self.doc = json.loads(path.read_text(encoding="utf-8"))
        self.buffers = [bcc.load_buffer(path, b["uri"]) for b in self.doc["buffers"]]
        self.nodes = self.doc["nodes"]
        self.names = {n.get("name"): i for i, n in enumerate(self.nodes) if n.get("name")}
        self.parents = {c: i for i, n in enumerate(self.nodes) for c in n.get("children", [])}
        self._world: dict[int, list[list[float]]] = {}

    def world(self, index: int):
        if index not in self._world:
            local = bcc.local_matrix(self.nodes[index])
            self._world[index] = (bcc.matrix_multiply(self.world(self.parents[index]), local)
                                  if index in self.parents else local)
        return self._world[index]

    def mesh(self, name: str):
        """World positions, world normals, triangle index list, material names."""
        index = self.names[name]
        node = self.nodes[index]
        if "mesh" not in node:
            return [], [], [], []
        matrix = self.world(index)
        points, normals, tris, mats = [], [], [], []
        for prim in self.doc["meshes"][node["mesh"]]["primitives"]:
            base = len(points)
            pos = bcc.accessor_values(self.doc, self.buffers, prim["attributes"]["POSITION"])
            nor = bcc.accessor_values(self.doc, self.buffers, prim["attributes"]["NORMAL"])
            points += [bcc.transform_point(matrix, p) for p in pos]
            normals += [bcc.transform_normal(matrix, n) for n in nor]
            if "indices" in prim:
                ids = [int(v[0]) for v in bcc.accessor_values(self.doc, self.buffers, prim["indices"])]
                tris += [tuple(base + i for i in ids[k:k + 3]) for k in range(0, len(ids), 3)]
            if "material" in prim:
                mats.append(self.doc["materials"][prim["material"]]["name"])
        return points, normals, tris, mats


def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def add(a, b): return tuple(x + y for x, y in zip(a, b))
def mul(a, s): return tuple(x * s for x in a)
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm(a): return math.sqrt(dot(a, a))
def unit(a): return mul(a, 1.0 / norm(a))


def bounds(points):
    lo = tuple(min(p[i] for p in points) for i in range(3))
    hi = tuple(max(p[i] for p in points) for i in range(3))
    return lo, hi


def six_tuple(points):
    """Reproduce avionics_panel_layout's (x0, x1, y0, z0, y1, z1) screen bounds."""
    lo, hi = bounds(points)
    return (lo[0], hi[0], lo[1], lo[2], hi[1], hi[2])


def screen_frame(points, normals):
    """Seated-pilot frame of a planar screen.

    normal  = mean authored normal (points out of the screen toward the crew),
    right   = pilot's right in the screen plane = up_world x normal, normalised
              (crew faces +Z, +X is crew-left, so right ~ -X),
    up      = normal x right (screen 'up', the direction of increasing device y).
    Corners are the extreme points of the outline in (right, up) coordinates.
    """
    n = unit(tuple(sum(v[i] for v in normals) for i in range(3)))
    right = unit(cross((0.0, 1.0, 0.0), n))
    up = cross(n, right)
    c = tuple(sum(p[i] for p in points) / len(points) for i in range(3))
    r = [dot(sub(p, c), right) for p in points]
    u = [dot(sub(p, c), up) for p in points]
    d = [dot(sub(p, c), n) for p in points]
    r0, r1, u0, u1 = min(r), max(r), min(u), max(u)

    def at(rr, uu):
        return add(c, add(mul(right, rr), mul(up, uu)))
    corners = {"bottom_left": at(r0, u0), "top_left": at(r0, u1),
               "top_right": at(r1, u1), "bottom_right": at(r1, u0)}
    return {"centre": c, "normal": n, "right": right, "up": up,
            "width_m": r1 - r0, "height_m": u1 - u0,
            "planarity_m": max(d) - min(d), "corners": corners,
            "tilt_from_vertical_deg": math.degrees(math.asin(max(-1.0, min(1.0, n[1])))),
            "yaw_deg": math.degrees(math.atan2(-n[0], -n[2]))}


def fmt(v, digits=6):
    if isinstance(v, (tuple, list)):
        return "(" + ", ".join(fmt(x, digits) for x in v) + ")"
    return f"{v:.{digits}f}"


def main():
    m = Model(SOURCE)
    out = {}
    print("== Convention check: recompute the published tuples ==")
    published = {
        "SCREEN_DU_PFDL": layout.PFD_SCREEN_SURFACES[0], "SCREEN_DU_PFDR": layout.PFD_SCREEN_SURFACES[1],
        "SCREEN_DU_NDL": layout.ND_SCREEN_SURFACES[0], "SCREEN_DU_NDR": layout.ND_SCREEN_SURFACES[1],
        "SCREEN_DU_EWD": layout.EWD_SCREEN_SURFACE,
    }
    for name, expected in published.items():
        pts, nrm, tris, mats = m.mesh(name)
        got = six_tuple(pts)
        err = max(abs(a - b) for a, b in zip(got, expected))
        print(f"{name:16s} verts={len(pts):3d} tris={len(tris):3d} mats={mats} max|diff|={err:.2e}")
        print(f"   computed {fmt(got, 8)}")
        print(f"   layout   {fmt(expected, 8)}")
        out[name] = {"six_tuple": got, "max_abs_diff_vs_layout": err}

    print("\n== Screens (MFD, SD, and references) ==")
    for name in ("SCREEN_DU_MFDL", "SCREEN_DU_MFDR", "SCREEN_DU_SD", "SCREEN_DU_PFDL",
                 "SCREEN_DU_EWD", "SCREEN_BACKLIGHT_DU_MFDL", "SCREEN_BACKLIGHT_DU_MFDR",
                 "SCREEN_BACKLIGHT_DU_SD", "SCREEN_BACKLIGHT_DU_PFDL"):
        pts, nrm, tris, mats = m.mesh(name)
        idx = m.names[name]
        parent = m.nodes[m.parents[idx]].get("name") if idx in m.parents else None
        f = screen_frame(pts, nrm)
        tup = six_tuple(pts)
        print(f"\n{name}: verts={len(pts)} tris={len(tris)} materials={mats} parent={parent}")
        print(f"  six_tuple (x0,x1,y0,z0,y1,z1) = {fmt(tup, 8)}")
        print(f"  centre={fmt(f['centre'])} normal={fmt(f['normal'])}")
        print(f"  right={fmt(f['right'])} up={fmt(f['up'])}")
        print(f"  width={f['width_m']*1000:.2f} mm height={f['height_m']*1000:.2f} mm "
              f"aspect w/h={f['width_m']/f['height_m']:.4f} (768/1024=0.75) "
              f"planarity={f['planarity_m']*1000:.3f} mm")
        print(f"  normal elevation above horizontal={f['tilt_from_vertical_deg']:.2f} deg "
              f"(face tilted back from vertical by that angle); yaw={f['yaw_deg']:.2f} deg")
        for k, v in f["corners"].items():
            print(f"  {k:13s} {fmt(v, 8)}")
        out[name] = {"six_tuple": tup, **{k: v for k, v in f.items()},
                     "materials": mats, "vertices": len(pts), "triangles": len(tris),
                     "parent": parent}

    print("\n== KCCU nodes ==")
    kccu = {}
    for side in ("L", "R"):
        names = sorted(n for n in m.names if n.startswith(f"PUSH_KCCU{side}_"))
        for name in names:
            pts, nrm, tris, mats = m.mesh(name)
            if not pts:
                print(f"{name:28s} (no mesh)")
                continue
            lo, hi = bounds(pts)
            c = tuple((a + b) / 2 for a, b in zip(lo, hi))
            kccu[name] = {"lo": lo, "hi": hi, "centre": c, "tris": len(tris), "materials": mats}
            print(f"{name:28s} tris={len(tris):4d} centre={fmt(c, 4)} size_mm="
                  f"({(hi[0]-lo[0])*1000:.1f}, {(hi[1]-lo[1])*1000:.1f}, {(hi[2]-lo[2])*1000:.1f}) mats={mats}")
    out["kccu"] = kccu
    for name in ("KCCU", "KCCU_COVER_BALL", "PUSH_KCCUL_WHEEL.001"):
        if name in m.names:
            pts, nrm, tris, mats = m.mesh(name)
            idx = m.names[name]
            parent = m.nodes[m.parents[idx]].get("name") if idx in m.parents else None
            if pts:
                lo, hi = bounds(pts)
                print(f"{name}: parent={parent} tris={len(tris)} lo={fmt(lo,4)} hi={fmt(hi,4)} mats={mats}")
            else:
                print(f"{name}: parent={parent} children={[m.nodes[c].get('name') for c in m.nodes[idx].get('children', [])][:8]}")

    print("\n== Captain pull-out keyboard (PUSH_KBD_CPT_*) summary ==")
    kbd = sorted(n for n in m.names if n.startswith("PUSH_KBD_CPT_") and not n.endswith(("_SEQ1", "_SEQ2")))
    allpts = []
    for name in kbd:
        pts, *_ = m.mesh(name)
        allpts += pts
    if allpts:
        lo, hi = bounds(allpts)
        print(f"{len(kbd)} key nodes; bounds lo={fmt(lo,4)} hi={fmt(hi,4)}")
    print("FO keyboard key nodes:", sorted(n for n in m.names if n.startswith("PUSH_KBD_FO_"))[:5] or "none")

    # Animations that target KCCU nodes (key travel clips).
    targets = {}
    for anim in m.doc.get("animations", []):
        for ch in anim.get("channels", []):
            node = ch.get("target", {}).get("node")
            if node is not None:
                nm = m.nodes[node].get("name", "")
                if nm.startswith(("PUSH_KCCU", "KCCU")):
                    targets.setdefault(nm, set()).add(anim.get("name"))
    print(f"\nKCCU nodes with glTF animation channels: {len(targets)}")
    for nm in sorted(targets)[:12]:
        print(f"  {nm}: {sorted(targets[nm])[:3]}")

    Path("/private/tmp/a380x-geometry/geometry.json").write_text(
        json.dumps(out, indent=1, default=list) + "\n")


if __name__ == "__main__":
    main()
```

Output excerpt (MFDL):

```text
SCREEN_DU_MFDL: verts=4 tris=2 materials=['SCREEN_DU_MFD'] parent=None
  six_tuple (x0,x1,y0,z0,y1,z1) = (0.11171729, 0.27178246, 2.42302714, 32.69813615, 2.54697620, 32.86913103)
  centre=(0.191750, 2.485002, 32.783634) normal=(0.000000, 0.809347, -0.587331)
  right=(-1.000000, 0.000000, 0.000000) up=(0.000000, 0.587331, 0.809347)
  width=160.07 mm height=211.19 mm aspect w/h=0.7579 (768/1024=0.75) planarity=0.113 mm
  normal elevation above horizontal=54.03 deg (face tilted back from vertical by that angle); yaw=-0.00 deg
```

### 7.2 `mfd_quad_proto.py` (device quad, UVs and winding)

`add_quad` is a closure inside `build_obj()`, so its arithmetic is copied here verbatim rather than imported.

```python
"""Prototype of the proposed MFD device quad: same math as build_cockpit_controls.add_quad
(copied, not imported, because add_quad is a closure inside build_obj)."""
import math, json
MFD_SCREEN_SURFACES = (
    (0.11171729, 0.27178246, 2.42302714, 32.69813615, 2.54697620, 32.86913103),
    (-0.27272221, -0.11295360, 2.42319904, 32.69832317, 2.54691513, 32.86900612),
)
DEVICES = ('a380x.xp12.mfd.captain', 'a380x.xp12.mfd.first_officer')
geo = json.load(open('geometry.json'))
def add_quad(center, width, height, uv, surface_offset, face_normal, face_vertical):
    cx, cy, cz = center
    cx += face_normal[0]*surface_offset; cy += face_normal[1]*surface_offset; cz += face_normal[2]*surface_offset
    hw, hh = width/2, height/2
    u0, v0, u1, v1 = uv
    pts = ((cx-hw, cy-face_vertical[1]*hh, cz-face_vertical[2]*hh, u0, v0),
           (cx+hw, cy-face_vertical[1]*hh, cz-face_vertical[2]*hh, u1, v0),
           (cx+hw, cy+face_vertical[1]*hh, cz+face_vertical[2]*hh, u1, v1),
           (cx-hw, cy+face_vertical[1]*hh, cz+face_vertical[2]*hh, u0, v1))
    return [(*p[:3], *face_normal, p[3], p[4]) for p in pts], (0, 1, 2, 0, 2, 3)
for side, (surface, device) in enumerate(zip(MFD_SCREEN_SURFACES, DEVICES)):
    x0, x1, y0, z0, y1, z1 = surface
    h = math.hypot(y1-y0, z1-z0)
    vertical = (0.0, (y1-y0)/h, (z1-z0)/h)
    normal = (0.0, vertical[2], -vertical[1])
    verts, idx = add_quad(((x0+x1)/2, (y0+y1)/2, (z0+z1)/2), x1-x0, h, (1.0, 0.0, 0.0, 1.0), 0.002, normal, vertical)
    name = 'SCREEN_DU_MFDL' if side == 0 else 'SCREEN_DU_MFDR'
    authored_n = geo[name]['normal']
    print(device, 'normal', [round(v, 6) for v in normal], 'authored', [round(v, 6) for v in authored_n],
          'angle_deg', round(math.degrees(math.acos(sum(a*b for a, b in zip(normal, authored_n)))), 4))
    # Seated-pilot classification: +X is crew-left in source coordinates.
    for v in verts:
        horiz = 'left' if v[0] > (x0+x1)/2 else 'right'
        vert = 'top' if v[1] > (y0+y1)/2 else 'bottom'
        print(f'   VT {v[0]:.8f} {v[1]:.8f} {v[2]:.8f}  {v[3]:.6f} {v[4]:.6f} {v[5]:.6f}  uv=({v[6]:g},{v[7]:g})  {vert}-{horiz} as seen')
    # OBJ8 front faces are clockwise; validate_aircraft uses cross(b-a,c-a).normal < 0.
    for t in (idx[:3], idx[3:]):
        a, b, c = (verts[i][:3] for i in t)
        u = [b[i]-a[i] for i in range(3)]; w = [c[i]-a[i] for i in range(3)]
        cr = (u[1]*w[2]-u[2]*w[1], u[2]*w[0]-u[0]*w[2], u[0]*w[1]-u[1]*w[0])
        print('   triangle', t, 'cross.normal', round(sum(cr[i]*normal[i] for i in range(3)), 9), '(<0 = clockwise front face)')
    # Offset of each corner from the authored corner (expect 2 mm along the normal).
    corners = geo[name]['corners']
    order = {'bottom_right': 0, 'bottom_left': 1, 'top_left': 2, 'top_right': 3}
    for k, i in order.items():
        d = [verts[i][a] - corners[k][a] for a in range(3)]
        print(f'   {k:12s} delta_mm={[round(x*1000, 3) for x in d]} along_normal_mm={round(sum(d[a]*normal[a] for a in range(3))*1000, 3)}')
```

Output (CAPT; F/O analogous):

```text
a380x.xp12.mfd.captain normal [0.0, 0.80966, -0.586899] authored [0.0, 0.809347, -0.587331] angle_deg 0.0306
   VT 0.11171729 2.42464646 32.69696235  0.000000 0.809660 -0.586899  uv=(1,0)  bottom-right as seen
   VT 0.27178246 2.42464646 32.69696235  0.000000 0.809660 -0.586899  uv=(0,0)  bottom-left as seen
   VT 0.27178246 2.54859552 32.86795723  0.000000 0.809660 -0.586899  uv=(0,1)  top-left as seen
   VT 0.11171729 2.54859552 32.86795723  0.000000 0.809660 -0.586899  uv=(1,1)  top-right as seen
   triangle (0, 1, 2) cross.normal -0.033804695 (<0 = clockwise front face)
   triangle (0, 2, 3) cross.normal -0.033804695 (<0 = clockwise front face)
   bottom_right delta_mm=[-0.0, 1.665, -1.207] along_normal_mm=2.056
   bottom_left  delta_mm=[0.0, 1.665, -1.207] along_normal_mm=2.056
   top_left     delta_mm=[0.0, 1.574, -1.141] along_normal_mm=1.944
   top_right    delta_mm=[-0.0, 1.574, -1.141] along_normal_mm=1.944
```

The other throwaway scripts in the same folder are:

* `anim_probe.py`: KCCU and keyboard clips and the parent chains in §5–§6.
* `kccu_cost.py`: triangle and vertex totals.
* `kccu_plan.py`: generates the proposed KCCU config and dry-runs `build_obj()`; listed in spec 09 §4.4.
