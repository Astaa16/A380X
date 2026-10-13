# MFD spec 08: X-Plane cockpit device (plugin avionics API) for the two MFDs

Status: research result and implementation spec for WP-J (spec 12 section 8): the X-Plane adapter
(`native/avionics/a380x_mfd_adapter.inc`), the cockpit OBJ changes and the build flags. It covers how the two
768 x 1024 MFDs become plugin-created cockpit devices that the user clicks in the 3D cockpit, and optionally in a 2D
popup. Specs 01-07 and 12 define the MFD itself. This file only defines how that MFD gets onto the screens and how
input reaches it.

Target environment, checked on 2026-10-06:

* X-Plane 12.4.3-r2 (build 124311, Metal) at `/Users/sava/Library/Application Support/Steam/steamapps/common/X-Plane 12`.
  Source: first line of its `Log.txt`.
* SDK 4.3 headers at `/Volumes/MacGames/A380X-XPlane12-build/sdk/XPSDK430/SDK/CHeaders/XPLM`.
* The plugin is built today with `-DXPLM200 -DXPLM210 -DXPLM300 -DXPLM400` (`native/avionics/build.py:276-277`).

## 0. Conventions

Each factual statement carries one of two tags:

* **[VERIFIED: source]**: read in the source named, or reproduced by a build in `/private/tmp/a380x-xpdevice/`.
* **[INFERRED]**: a conclusion drawn from verified facts or from general X-Plane behaviour, but not shown directly.
  Every INFERRED item that matters for correctness is listed again in section 11 (open risks) with a test.

Source keys used in tags:

| Key | Source |
|---|---|
| H | SDK 4.3 `CHeaders/XPLM/XPLMDisplay.h` (line numbers given as `H:nnn`). Online copy: <https://developer.x-plane.com/sdk/XPLMDisplay/> (same text) |
| D | SDK 4.3 `XPLMDefs.h` |
| U | SDK 4.3 `XPLMUtilities.h` |
| R | SDK 4.3 `README.txt` (SDK release notes) |
| OBJ8 | OBJ8 File Format Specification, revision 03/22/25: <https://developer.x-plane.com/article/obj8-file-format-specification/> |
| LS | Laminar sample plugin `X-Plane/avionics-tests` (commit b5b72ce, 2024-09-04): <https://github.com/X-Plane/avionics-tests>, `src/custom_device.c`, `src/plugin.c` |
| XPP | XPPython3 Avionics API docs (v4.7.1): <https://xppython3.readthedocs.io/en/latest/development/modules/display_avionics.html>. These record observed sim behaviour. |
| XPPK | XPPython3 key sniffer docs: <https://xppython3.readthedocs.io/en/latest/development/modules/display_sniffer.html> |
| GL | Plugin Guidance for OpenGL Drawing: <https://developer.x-plane.com/article/plugin-guidance-for-opengl-drawing/> |
| N121 | X-Plane 12.1.0 release notes: <https://www.x-plane.com/kb/x-plane-12-1-0-release-notes/> |
| N1211 | 12.1.1 release notes: <https://www.x-plane.com/kb/x-plane-12-1-1-release-notes/> |
| N1212 | 12.1.2 release notes: <https://www.x-plane.com/kb/x-plane-12-1-2-release-notes/> |
| N122 | 12.2.0 release notes: <https://www.x-plane.com/kb/x-plane-12-2-0-release-notes/> |
| N123 | 12.3.0 release notes: <https://www.x-plane.com/kb/x-plane-12-3-0-release-notes/> |
| N124 | 12.4.0 release notes: <https://www.x-plane.com/kb/x-plane-12-4-0-release-notes/> |
| N1241 | 12.4.1 release notes: <https://www.x-plane.com/kb/x-plane-12-4-1-release-notes/> |
| N1243 | 12.4.3 release notes: <https://www.x-plane.com/kb/x-plane-12-4-3-release-notes/> |
| N1244 | 12.4.4 beta release notes, SDK 440 beta 3: <https://www.x-plane.com/kb/x-plane-12-4-4-release-notes/> |
| DEV121 | "A Few Notes on 12.1.0 for Developers": <https://developer.x-plane.com/2024/02/a-few-notes-on-12-1-0-for-developers/> |
| TL | Installed ToLiss A320: `Aircraft/ToLissA320/aac_psu_a320_cockpit.obj:116356-116357`, `objects/aac_fap_screen.obj`, `a320.acf` (`_obja/0`, `_obja/74`). Device owner: `Resources/plugins/AdvancedCabin/64/mac.xpl`, whose imports include `XPLMCreateAvionicsEx`, `XPLMAvionicsNeedsDrawing`, `XPLMDestroyAvionics` and `XPLMGetAvionicsBusVoltsRatio` (`nm -u`) |
| LR | Installed Laminar OBJs: `Cirrus SR22/objects/garmins.obj`, `Airbus A330-300/objects/cockpit/FMS.obj`, `Boeing 737-800/objects/FMS_screens.obj` |
| X2B | XPlane2Blender master (ee03ed6): `io_xplane2blender/xplane_props.py:2243-2290, 2391-2402`, `xplane_types/xplane_material.py:241-256` |
| KBD | "Universal FMC Keyboard Input" plugin: <https://github.com/6639835/X-Plane-KeyboardInput>, `src/xplane/plugin.cpp:135,229`, `README.md` |
| DR | Installed `Resources/plugins/DataRefs.txt` (12.4.3) |
| LOG | Installed `Log.txt` (12.4.3) |
| P | This repository (paths given) and the installed `Aircraft/A380X-XP12/A380X-XP12.acf` |
| TDS | TDS forum thread, anecdotal: <https://www.tdssim.com/forum/thread-1776.html> |
| B | Throwaway builds in `/private/tmp/a380x-xpdevice/skeleton/` (clang universal mac, mingw win64, both against SDK 4.3) |

## 1. Summary of decisions

1. Each MFD is one plugin device, created with `XPLMCreateAvionicsEx`: `a380x.xp12.mfd.captain` and
   `a380x.xp12.mfd.first_officer`. The screen is 768 x 1024 and the device uses `drawOnDemand = 1`.
2. The visible screen quads go in a **new, separate aircraft-attached OBJ** with an opaque black `TEXTURE`, tagged
   `ATTR_cockpit_device`. The click quads go in the **cockpit object** (`a380x_cockpit_controls.obj`) as draw-disabled
   quads tagged with both `ATTR_cockpit_device` and `ATTR_manip_device`. Without `ATTR_manip_device`, no 3D click ever
   reaches the plugin.
3. Input reaches the core (spec 01 section 12.5) as follows:
   * Touch and drag come from `screenTouchCallback`.
   * Hover comes from `screenCursorCallback` and `XPLMIsCursorOverAvionics`.
   * The wheel comes from `screenScrollCallback`.
   * PC keys come from the avionics `keyboardCallback` when the popup is up, and otherwise from an after-windows key
     sniffer that is active only while a field is focused.
4. The display list is built in the flight loop. `XPLMAvionicsNeedsDrawing` is called only when the list changed. The
   draw callback only executes GL.
5. Brightness comes from the existing DU knobs (`a380x/xp12/du/<side>_mfd`) and MFD power, through
   `brightnessCallback`.
6. Avionics functions are resolved with `XPLMFindSymbol`. The binary keeps no hard import, and fallbacks are possible.
   The types are compiled with `-DXPLM410=1`.

---

## 2. OBJ8: mapping geometry to a plugin device

### 2.1 `ATTR_cockpit_device`

```
ATTR_cockpit_device <name> <bus> <lighting channel> <auto_adjust>
...TRIS...
ATTR_no_cockpit
```

* Syntax, end marker and scope: "changes the lit texture of the object to the screen of a given GPS device for an
  aircraft; it is legal only in aircraft-attached objects. Use ATTR_no_cockpit when done." [VERIFIED: OBJ8,
  ATTR_cockpit_device entry, "New in 1100"]
* `<name>` is the plugin's `deviceID` string. The OBJ8 entry only lists the old Laminar names (GNS430_1 to G1000_PFD2).
  The SDK header states that the device ID string "is the string the OBJ file must reference when marking polygons
  with ATTR_cockpit_device" [VERIFIED: H:733-740]. A shipping aircraft does exactly this with a plugin ID:
  `ATTR_cockpit_device aac.enhanced.fap 3 0 1` [VERIFIED: TL]. XPlane2Blender also has a "plugin device, Device ID"
  option [VERIFIED: X2B].
* `deviceID` rules: at most 64 characters, no spaces, and unique ("you cannot re-use an ID that X-Plane or another
  plugin provides"). The string is copied by `XPLMCreateAvionicsEx` [VERIFIED: H:733-740].
* `<bus>` is "a little-endian bitfield, indicating all the electrical system bus indices (zero-based) that this device
  gets power from" [VERIFIED: OBJ8]. X-Plane 12 has 6 buses (`sim/cockpit2/electrical/bus_volts float[6]`)
  [VERIFIED: DR line 4213]. XPlane2Blender writes `sum(bus_i << i)` over 6 buses [VERIFIED: X2B]. Examples: ToLiss `3`
  (buses 0+1), Laminar A330 MCDU_1 `8` (bus 3) [VERIFIED: TL, LR].
* `<lighting channel>` is "the zero-based index for the instrument lighting rheostats that control the screen's
  brightness. (Cockpit device lighting is not subject to ATTR_lit_level.)" [VERIFIED: OBJ8]. The rheostats are
  `sim/cockpit2/switches/instrument_brightness_ratio float[32]`, where "0 = default pilot, 1 = default copilot,
  2-15 = custom" [VERIFIED: DR line 5130]. Laminar's SR22 uses `-1` for both G1000 screens [VERIFIED: LR]. That `-1`
  means "not bound to any rheostat, the device keeps its own" is [INFERRED]; the header supports this reading for
  unbound devices at H:802-806.
* `<auto_adjust>`: "1 if the screen brightness increases to be readable during the day, and 0 if screen brightness is
  'washed out' by the sun" [VERIFIED: OBJ8].
* XPlane2Blender appends an optional 5th parameter, the luminance in nits, for X-Plane 12 exports [VERIFIED: X2B].
  The OBJ8 spec does not document it [VERIFIED: OBJ8]. **Do not use it** [INFERRED].

### 2.2 What the device replaces, and coexistence with the panel texture

* "When ATTR_cockpit_device is used, the image of the screen for that GPS device replaces the lit texture; the albedo
  and normal map are retained from the object itself. ATTR_cockpit_device is not a seperate panel mode. The same .obj
  can have ATTR_cockpit_devices and use of the panel texture" [VERIFIED: OBJ8, "Texture application"].
* Therefore **no `COCKPIT_REGION` header and no `ATTR_cockpit_region` are needed** for a device. Panel geometry
  (`ATTR_cockpit`, `ATTR_cockpit_region`) and device geometry can share one OBJ. Each block ends with
  `ATTR_no_cockpit` [VERIFIED: OBJ8]. None of the Laminar or ToLiss device OBJs has a region or a `TEXTURE_LIT`
  [VERIFIED: LR, TL].
* **Consequence for this aircraft:** the device quad's UVs (0..1, section 2.3) also sample the OBJ's own albedo
  `TEXTURE`. The cockpit object `a380x_cockpit_controls.obj` uses `TEXTURE ../../../cockpit_3d/fcu_labels.png` (the
  FCU label atlas) [VERIFIED: P `tools/build_cockpit_controls.py:1023-1031`]. A visible device quad in that OBJ would
  show the whole label atlas as daylight albedo [INFERRED]. So the visible quads go in a separate object whose albedo is
  opaque black. ToLiss does exactly this: `objects/aac_fap_screen.obj` uses `TEXTURE aac_fap_screen_black.png`,
  `GLOBAL_cockpit_lit`, `ATTR_cockpit_device aac.enhanced.fap 3 0 1`, `TRIS 0 6`, `ATTR_no_cockpit`, attached as
  `_obja/74` with `_obj_flags 13` (not the cockpit object) [VERIFIED: TL].
* `ATTR_light_level` does not affect device lighting [VERIFIED: OBJ8]. Keep the device block outside the builder's lamp
  `ATTR_light_level` sections anyway.

### 2.3 UV mapping

* OBJ texture coordinates: "The first one represents a ratio from the left to the right side of the texture, the second
  from the bottom to the top" [VERIFIED: OBJ8, "Object syntax"].
* **UV (0,0)..(1,1) covers the full device screen.** UV (0,0) is the bottom-left of the device, the same as device
  texel (0,0). Evidence: the SR22 G1000 PFD/MFD quads use exactly u 0..1 and v 0..1, and the ToLiss device touch quad
  has corners (0,0), (1,0), (1,1), (0,1) [VERIFIED: LR, TL, decoded from VT/IDX]. Device texels have their origin at the
  bottom-left with y up [VERIFIED: H:298-305].
* A touch maps as `x = u * screenWidth`, `y = v * screenHeight`, in integer texels [INFERRED from the two facts above].
  Partial UV ranges crop the image: the A330 MCDU quads use u 0.005..1.04 and v 0.05..0.97 [VERIFIED: LR].
* **Handedness trap in this aircraft.** The controls object is attached with `psi 180`, and the builder states:
  "Source X is reversed by the aircraft attachment's 180-degree heading rotation, so map the lower source X to the
  panel's right" [VERIFIED: P `tools/build_cockpit_controls.py:766-770`, `tools/avionics_panel_layout.py:110-112`].
  Build MFD quads from four explicit corners **as seen from the seat**: bottom-left, top-left, top-right, bottom-right,
  with UV (0,0), (0,1), (1,1), (1,0). This is the `add_efb_surface` pattern, which "need[s] no U reversal" [VERIFIED: P
  `tools/build_cockpit_controls.py:839-861`]. If U is mirrored, touches come back mirrored.

### 2.4 Clicks need `ATTR_manip_device` (critical)

* **3D clicks do NOT reach `screenTouchCallback` automatically.** "While these always work when the device is
  popped-up in its window, you must add a `ATTR_manip_device` manipulator on top of your screen in order to receive
  mouse events from the 3D cockpit." [VERIFIED: H:289-293]. XPP repeats this [VERIFIED: XPP].
* Syntax: `ATTR_manip_device <cursor> <device> <tooltip>`. It was new in 12.1.0 and "creates a touch screen
  manipulator for a cockpit device". `<device>` "can be any of X-Plane's own devices ... or a device created by a plugin
  using the XPLMCreateAvionics API". "The manipulator must be the same shape, and UV-mapped the same way as the screen
  of the cockpit device for coordinates to be passed to the simulator and plugins correctly." "The manipulator must also
  be tagged with ATTR_cockpit_device for X-Plane to recognise it correctly." [VERIFIED: OBJ8, ATTR_manip_device entry;
  changelog "Changes for 1210" and revision 12/09/24]
* `<cursor>` is one of `four_arrows hand button rotate_small rotate_small_left rotate_small_right rotate_medium
  rotate_medium_left rotate_medium_right rotate_large rotate_large_left rotate_large_right up_down down up left_right
  right left arrow` [VERIFIED: OBJ8, "Manipulation commands"]. The device `screenCursorCallback` decides the cursor while
  the pointer is over the screen [VERIFIED: H:444-458]. So `<cursor>` only shows when that callback returns
  `xplm_CursorDefault` [INFERRED].
* Only the **cockpit object** can hold manipulators: "Only triangles in the cockpit object can be in the manipulation
  plane" [VERIFIED: OBJ8]. In this aircraft that object is `interior/cockpit-controls/a380x_cockpit_controls.obj`
  (`_obja/118`, `_obj_flags 2061`, where bit 2048 is the cockpit role). `validate_aircraft.py` rejects more than one
  cockpit object [VERIFIED: P acf lines 39973-39985, `tools/build_development_aircraft.py:454-458`,
  `tools/validate_aircraft.py:316-318`].
* **A draw-disabled touch quad works.** ToLiss ships this sequence in its cockpit object [VERIFIED: TL]:
  `ATTR_draw_disable` / `ATTR_cockpit_device aac.enhanced.fap 3 0 1` / `ATTR_manip_device hand aac.enhanced.fap Flight
  attendant panel` / `TRIS 81384 6` / `ATTR_manip_none` / `ATTR_no_cockpit`. The visible screen of that device lives in
  the non-cockpit `aac_fap_screen.obj`. That this works in the sim is [INFERRED]: it is a shipping product with a
  plugin that creates the device. This repository already uses `ATTR_draw_disable` click meshes for every cockpit
  control [VERIFIED: P `tools/build_cockpit_controls.py:1046-1082`].
* A triangle has exactly one manipulator ("any given triangle can only have one manipulation associated with it"
  [VERIFIED: OBJ8]). The nearest manipulated triangle wins. `ATTR_manip_none` lets clicks "track through to any mesh
  behind this one", and click-testing costs CPU per triangle [VERIFIED: OBJ8]. Keep each touch quad to 2 triangles, and
  keep no other manipulator geometry in front of it.
* `ATTR_cockpit_region` "sets the current manipulation to panel clicking" [VERIFIED: OBJ8]. The builder's
  PFD/ND/EWD/EFB screen quads are therefore panel-click manipulators [VERIFIED: P `build_cockpit_controls.py:1088-1090`].
  An MFD touch quad must not sit behind one of them. They do not overlap today [INFERRED].
* Hidden device manipulators: the 12.4.4 beta notes say "Click-enabled FBO screens will no longer consume mouse clicks
  when hidden (XPD-18347)" [VERIFIED: N1244]. So on 12.4.3 and older, an `ANIM_hide`-hidden device touch quad probably
  still eats clicks [INFERRED]. Do not hide the touch quad. Ignore input in the plugin when the DU is off.

### 2.5 OBJ8 snippets

The numbers below are **stand-in geometry**: the CAPT PFD bounds from `PFD_SCREEN_SURFACES[0]`, reordered as
BL/TL/TR/BR as seen from the seat. They show the orientation and normal rules with real coordinates. The integrator
substitutes the MFD screen bounds (section 10.3).

**(a) Visible screens: new attached object** `objects/interior/cockpit-panels/a380x_mfd_screens.obj`
(`_obj_flags 13`, `_v10_is_internal 1`, the same transform as `_obja/117/118`: x 0, y 0, z 115.108, psi 180):

```
I
800
OBJ

TEXTURE	../../../textures/interior/cockpit-panels/mfd_screen_black.png
GLOBAL_cockpit_lit
POINT_COUNTS	4	0	0	6

# CAPT MFD (stand-in coordinates). Corners as seen from the seat: BL, TL, TR, BR. UV (0,0) = device bottom-left.
VT	0.638610	2.591478	32.896912	0.00000	0.33644	-0.94171	0	0
VT	0.638610	2.788757	32.967392	0.00000	0.33644	-0.94171	0	1
VT	0.480048	2.788757	32.967392	0.00000	0.33644	-0.94171	1	1
VT	0.480048	2.591478	32.896912	0.00000	0.33644	-0.94171	1	0

IDX	0
IDX	1
IDX	2
IDX	0
IDX	2
IDX	3

ATTR_poly_os	2
ATTR_cockpit_device	a380x.xp12.mfd.captain	1	14	1
TRIS	0	6
ATTR_no_cockpit
ATTR_poly_os	0
```

The F/O quad is identical apart from its own 4 vertices and `ATTR_cockpit_device a380x.xp12.mfd.first_officer 1 15 1`.
Notes on this snippet:

* The index order (BL, TL, TR) and (BL, TR, BR) is the order `add_efb_surface` already ships [VERIFIED: P
  `build_cockpit_controls.py:860`].
* The normal is `cross(BR - BL, TL - BL)`, the same as the builder's `surface_normal` [VERIFIED: P
  `build_cockpit_controls.py:811-823`].
* `mfd_screen_black.png` is a new opaque 8 x 8 black PNG.
* `ATTR_poly_os 2` matches the existing screen overlays above `screen_powered_off_display_faces.obj` [VERIFIED: P
  `build_cockpit_controls.py:1088`].

**(b) Click quads: inside the existing draw-disabled manipulator block of the cockpit object**
`a380x_cockpit_controls.obj`. They have the same corners and UVs, raised 1 mm along the normal:

```
ATTR_draw_disable
# ... existing control manipulators ...
ATTR_cockpit_device	a380x.xp12.mfd.captain	1	14	1
ATTR_manip_device	arrow	a380x.xp12.mfd.captain	CAPT MFD
TRIS	<capt_touch_start>	6
ATTR_manip_none
ATTR_no_cockpit
ATTR_cockpit_device	a380x.xp12.mfd.first_officer	1	15	1
ATTR_manip_device	arrow	a380x.xp12.mfd.first_officer	F/O MFD
TRIS	<fo_touch_start>	6
ATTR_manip_none
ATTR_no_cockpit
ATTR_draw_enable
```

CAPT touch vertices for the stand-in geometry (each is the visible vertex + 0.001 m x normal):

```
VT	0.638610	2.591814	32.895970	0.00000	0.33644	-0.94171	0	0
VT	0.638610	2.789094	32.966450	0.00000	0.33644	-0.94171	0	1
VT	0.480048	2.789094	32.966450	0.00000	0.33644	-0.94171	1	1
VT	0.480048	2.591814	32.895970	0.00000	0.33644	-0.94171	1	0
```

The order of the touch block matters: first `ATTR_cockpit_device`, then `ATTR_manip_device`, then `TRIS`, then
`ATTR_manip_none`, then `ATTR_no_cockpit`. This is the ToLiss order [VERIFIED: TL]. The OBJ8 spec requires the device
tag but does not prescribe an order [VERIFIED: OBJ8].

---

## 3. Draw callback

| Topic | Fact |
|---|---|
| Prototype | `void drawCallback(void *refcon)`. One callback per device [VERIFIED: H:594-611]. |
| Coordinates | Texels. The origin is the bottom-left of the screen, x grows right, y grows up. Always draw at the declared size: X-Plane scales for popups and pop-outs [VERIFIED: H:298-305]. Canvas conversion for spec 01/12: `y_gl = 1024 - y_canvas` [VERIFIED: spec 01 section 0.2]. |
| GL state on entry | "OpenGL context will be correctly set up for you and OpenGL will be in panel coordinates for 2d drawing. The OpenGL state (texturing, etc.) will be unknown." [VERIFIED: H:602-604]. Call `XPLMSetGraphicsState` first [VERIFIED: GL "Use the XPLM APIs to manage state"]. In `xplm_Phase_Gauges` the existing code finds culling enabled with clockwise front faces [VERIFIED: P `a380x_avionics.c:330-333`]. In device callbacks the state is unknown, so disable culling and restore it afterwards [INFERRED]. |
| Clearing | "X-Plane does not clear your screen for you between calls ... you must call glClear()" [VERIFIED: H:604-606]. 12.3.0 fixed "the FBO for custom avionics was cleared before the draw callback (XPD-17072)" [VERIFIED: N123]. So the content persists from 12.3.0 on [INFERRED]. Always `glClear(GL_COLOR_BUFFER_BIT)` with alpha 1. |
| FBO | X-Plane binds a framebuffer of the declared `screenWidth x screenHeight` ("draw into the device's screen framebuffer", H:701-702; destroy "deallocates its screen's memory", H:770) [VERIFIED for the wording; exact size and the absence of depth buffer or MSAA are INFERRED]. Do not use depth testing. If an own FBO is ever bound, restore `sim/graphics/view/current_gl_fbo` [VERIFIED: GL, DR line 1959]. |
| Alpha | In popups "the screen is drawn over the bezel, and blends with whatever the background is" [VERIFIED: XPP]. Keep destination alpha = 1: clear with alpha 1, then `glColorMask(1,1,1,0)` while drawing. Plain `GL_SRC_ALPHA/ONE_MINUS_SRC_ALPHA` blending would leave anti-aliased text edges at alpha < 1 [INFERRED]. |
| drawOnDemand | `drawOnDemand = 1`: "X-Plane won't call your plugin to re-render the device's screen every frame ... call XPLMAvionicsNeedsDrawing(), and X-Plane will call you before rendering the next simulator frame" [VERIFIED: H:692-696, H:860-869]. The callback runs once after creation [VERIFIED: XPP]. With `drawOnDemand = 0`, `NeedsDrawing` has no effect [VERIFIED: H:866-867]. |
| Bezel | The bezel callback runs only when the popup is visible, every frame, unaffected by `drawOnDemand`. It receives the ambient RGB for tinting [VERIFIED: H:612-633, H:697-700; XPP]. |
| Metal/Vulkan | Plugin drawing is OpenGL only, under every backend [VERIFIED: GL]. Each dispatched draw callback "requires heavy synchronization in the case of Vulkan and Metal". Draw only when needed, and use callbacks "only for drawing, not for updating simulation state" [VERIFIED: GL]. Restore shaders, VBOs and non-2D texture bindings to 0 [VERIFIED: GL]. `XPLMDrawString` may change state [VERIFIED: GL]. |
| Scissor / viewport | `sim/graphics/view/is_reverse_y` reports a DirectX/Metal-style flipped framebuffer, and the transform stack hides it [VERIFIED: GL, DR line 1957]. `glScissor`/`glViewport` use framebuffer coordinates, so a clip rect could flip under reverse-Y [INFERRED]. Derive scissor boxes from `sim/graphics/view/viewport` and `is_reverse_y`, or clip on the CPU, and test (risk R2). |
| Profiling | Avionics callbacks are counted in Plugin Admin > Performance from 12.1.0b7 (XPD-15610) [VERIFIED: N121]. |
| Future | 12.4.4 (beta) adds a backend-independent "Panel Graphics API" and native Dear ImGui, which also cover avionics screens [VERIFIED: N1244]. Not needed now. A possible later migration off GL [INFERRED]. |

---

## 4. Mouse, touch, cursor and wheel callbacks

* **Prototype:** `int (*)(int x, int y, XPLMMouseStatus status, void *refcon)`, used for `screenTouchCallback`,
  `screenRightTouchCallback`, `bezelClickCallback` and `bezelRightClickCallback` [VERIFIED: H:407-423].
* **Units and origin:** texels. Screen callbacks use the screen's bottom-left as origin, bezel callbacks the bezel's
  bottom-left. The 3D cockpit and the scaled popup report the same units [VERIFIED: H:298-305; XPP]. Spec 01 section
  12.5 converts with `y_canvas = 1023 - y`, which is the integer pixel row. The skeleton passes the pixel centre
  `(1023 - y) + 0.5`.
* **Sequence:** `xplm_MouseDown` (1), then zero or more `xplm_MouseDrag` (2), then exactly one `xplm_MouseUp` (3). "you
  are guaranteed to not receive a drag or mouse-up event without first receiving the corresponding mouse-down"
  [VERIFIED: D:510-531].
  * "If you return 0 on MouseDown, you will not receive MouseDrag or MouseUp events" [VERIFIED: XPP].
  * Custom devices did not send `xplm_MouseDrag` before 12.3.0 (XPD-17440) [VERIFIED: N123].
* **Return value:** 1 consumes the event. 0 lets X-Plane process it, which is meaningful for stock devices [VERIFIED:
  H:411-416]. For the MFD screen, always return 1 from left touches.
  * For right touches, return 0 so that right-drag view look still works over the screen [INFERRED].
  * For the wheel, return 1 only when the MFD scrolled something, otherwise 0 so the view zoom still works [INFERRED].
* **Out-of-range coordinates:** 12.3.0 fixed "a crash when dragging the mouse cursor out of a custom avionics device".
  12.4.0 fixed "a crash when right-click dragging out of a custom device manipulator" [VERIFIED: N123, N124]. Drags can
  therefore leave the screen, and x/y can fall outside 0..767 / 0..1023. Clamp them [INFERRED].
* **Wheel:** `int (*)(int x, int y, int wheel, int clicks, void *refcon)`. `wheel` is 0 for the vertical axis and 1 for
  the horizontal axis. `clicks` is how far the wheel turned since the last callback [VERIFIED: H:425-443]. Positive means
  wheel up / away from the user [INFERRED, not documented]. 12.1.0b5 fixed wrong x/y in `bezelScroll` (XPD-15442)
  [VERIFIED: N121].
* **Cursor:** `XPLMCursorStatus (*)(int x, int y, void *refcon)`. Return `xplm_CursorDefault` to let X-Plane choose,
  or another value to force a cursor [VERIFIED: H:444-458].
  * `xplm_CursorHidden` (1) hides the OS cursor so the plugin can draw its own. Never show or hide the cursor with OS
    calls [VERIFIED: H:1125-1137, D:391-400].
  * 12.4.0 "Fixed 3d mouse cursor callbacks of custom devices" [VERIFIED: N124]. 12.1.0 fixed "Avionics API cursor
    callbacks ignore Arrow cursor" (XPD-15261) [VERIFIED: N121].
  * Extended cursor shapes need `XPLM420`, which ships with 12.3 [VERIFIED: R 4.2.0 notes; D:402-494].
  * The cursor callback is the hover source in the 3D cockpit, and the KCCU cursor of spec 01 section 11.1 is drawn
    from it [INFERRED from the callback contract plus the 12.4.0 fix].
* **`XPLMIsCursorOverAvionics(handle, &x, &y)`**: returns 1 "if the mouse is currently over the screen" and fills
  device coordinates [VERIFIED: H:843-857].
  * XPP observed that it returns coordinates only for a popped-up or popped-out device with focus [VERIFIED: XPP]. That
    note was probably written before 12.4.0, which "Fixed function XPLMIsCursorOverAvionics" [VERIFIED: N124].
  * 12.1.0 also fixed "returns value over Bezel" (XPD-15475) [VERIFIED: N121].
  * Laminar's sample calls it inside the screen draw callback for hover highlighting [VERIFIED: LS].
  * Whether it works in the 3D cockpit on 12.4.3, and whether it depends on an `ATTR_manip_device` hit, is
    [INFERRED / untested] (risk R1).
* **Popup edge zones:** "edges of popups are reserved for X-Plane window manipulation (i.e., resize and move) ...
  This includes ... the 25 pixels or so at the top of the popup window." No events arrive there [VERIFIED: XPP]. That
  is why the bezel has a 40 px top margin (section 6).
* **VR:** before 12.4.3, custom devices did not receive correct touchscreen coordinates from VR controllers
  (XPD-18009) [VERIFIED: N1243].

---

## 5. Keyboard

### 5.1 What the API offers

* `keyboardCallback` is "called when your device is popped up and you've requested to capture the keyboard"
  [VERIFIED: H:463-464].
  * Prototype: `int (*)(char key, XPLMKeyFlags flags, char vkey, void *refcon, int losingFocus)` [VERIFIED: H:459-474].
  * `losingFocus = 1` means the focus moved away [VERIFIED: XPP].
* `XPLMTakeAvionicsKeyboardFocus(handle)` "gives keyboard focus to the popup window of a custom cockpit device, if it is
  visible" [VERIFIED: H:914-920]. XPP: "The avionics device needs to be either in a popup, or popped out" [VERIFIED:
  XPP]. `XPLMHasAvionicsKeyboardFocus` reports the state [VERIFIED: H:923-931].
* **Conclusion:** there is no API that routes the PC keyboard to a device that is shown only in the 3D cockpit
  [VERIFIED by absence in H; INFERRED as a design fact].
* To give the keys back to X-Plane, call `XPLMTakeKeyboardFocus(0)`. It "remove[s] keyboard focus from any
  plugin-created windows" [VERIFIED: H:1886-1895, H:1889]. That it also clears avionics popup focus is [INFERRED].

### 5.2 PC typing while an MFD field is focused in the 3D cockpit

**Recommended: a key sniffer registered after the window system** (`XPLMRegisterKeySniffer(cb, 0, refcon)`) that stays
passive unless a capture is active.

* API contract [VERIFIED: H:1956-2010 (H:1987 for "ALMOST ALWAYS"), XPPK]:
  * The sniffer returns 1 to pass a key downstream and 0 to consume it.
  * `inBeforeWindows = 0` receives only the keys that the window system did not consume. "You should ALMOST ALWAYS sniff
    non-control keys after the window system"; consuming keys before the windows "will produce very weird results".
* The header names key sniffers as the mechanism for non-windowed text entry ("the MUI library uses a key sniffer to do
  pop-up text entry") [VERIFIED: H:1963-1966].
* An open-source FMC-typing plugin uses exactly this approach [VERIFIED: KBD]:
  * `XPLMRegisterKeySniffer(&key_callback, 0, this)`;
  * it handles key-down only;
  * Control- and Alt-modified shortcuts pass through.
* vkeys are declared `char` but the `XPLM_VK_*` macros are unsigned. Cast to `unsigned char` before comparing
  [VERIFIED: H:1971-1975, D:256-369].
* Key mapping (spec 01 section 12.5):
  * Letters and digits by vkey: `XPLM_VK_A..Z`, `XPLM_VK_0..9`, `XPLM_VK_NUMPAD0..9`.
  * Symbols by character: `' ' / . + -`, with `,` mapped to `.`.
  * `XPLM_VK_BACK`/`DELETE` give BACKSPACE.
  * `XPLM_VK_RETURN`/`ENTER`/`NUMPAD_ENT` give ENT.
  * `XPLM_VK_ESCAPE` gives ESC.
  * Key flags: `xplm_ShiftFlag 1`, `xplm_OptionAltFlag 2`, `xplm_ControlFlag 4`, `xplm_DownFlag 8`, `xplm_UpFlag 16`
    [VERIFIED: D:173-185].

Pitfalls and rules. All are [INFERRED] unless tagged otherwise.

1. **Never register before windows.** That would steal keys from focused text fields: X-Plane dialogs, chat, and other
   plugins' windows, including our own EFB and ADIRS windows, which use `XPLMTakeKeyboardFocus` [VERIFIED: P
   `a380x_adirs_window.inc:23-24,204`].
2. **While capturing, consume both the down and the up event of every mapped key.** Otherwise X-Plane receives an
   orphan key-up for a key that may be bound to a held command (for example the brakes on `B`).
3. **Pass through** keys with Ctrl/Cmd or Alt, and unmapped keys (F-keys, arrows, Tab).
4. **Single owner.** Only one side captures at a time: the most recently focused side. The other side keeps its field
   focus for KCCU keys only.
5. **Release capture** on any of these:
   * ENT or ESC;
   * field blur or commit;
   * page navigation (spec 01 section 12.4);
   * MFD power loss;
   * the popup opening (switch to avionics focus);
   * plugin disable or aircraft reload;
   * **20 s without a keystroke** (spec 01 section 13.4.7 [PORT]).

   Re-capture only on a new focus event. The core exposes a focus serial number that changes on every (re)focus,
   including a click into an already focused field.
6. **Expected side effect:** while capturing, sim shortcuts on plain letters stop working (pause, gear and so on). The
   visible caret is the user's cue. That is why the watchdog exists.
7. Do not take focus every frame. Route focus on the focus edge only, so that X-Plane or another window can take focus
   back.
8. OS key repeat probably arrives as repeated down events, which gives a repeating backspace. Test it (risk R5).

**Alternative, not recommended:** an invisible XPLM window plus `XPLMTakeKeyboardFocus`. Modern windows can take focus
[VERIFIED: H:1886-1895]. Whether a hidden or zero-size window keeps focus, how it behaves in VR and in pop-out lists,
and how it reacts to clicks elsewhere are all unknown [INFERRED risk]. It also adds a window to every screen. The
sniffer has none of these unknowns.

### 5.3 Popup keyboard

* When a field gets focus and that side's popup or pop-out is visible, call `XPLMTakeAvionicsKeyboardFocus`. Keys then
  arrive in `keyboardCallback`.
* Return 1 for mapped keys and 0 for the rest.
* On blur, call `XPLMTakeKeyboardFocus(0)`.
* Laminar's sample returns 0 and only logs [VERIFIED: LS].

---

## 6. Popup and pop-out windows

* **Size parameters:**
  * `bezelWidth`/`bezelHeight` give the **full** bezel image, screen included.
  * `screenOffsetX/Y` give the screen's bottom-left inside the bezel.
  * Laminar's sample uses `bezelWidth = 2*50 + 480` with offset 50 [VERIFIED: LS]. XPP draws the same diagram and says
    "The size of the bezel must be at least as much as the screen + offset" [VERIFIED: XPP].
  * Chosen for the MFD: bezel 800 x 1080, screen offset (16, 16). That leaves 16 px at the sides and bottom and 40 px
    at the top for X-Plane's move, close and pop-out band [INFERRED sizing from the XPP edge note].
* **Initial state and saved positions:** a device is created hidden. "Because X-Plane automatically remembers window
  position, if the deviceID has been used before, its position will be initialized to whatever is stored" in
  `Output/preferences/Miscellaneous.prf` [VERIFIED: XPP]. "Full pop-up support, including window position saving"
  [VERIFIED: N121]. Keep the device IDs stable forever.
* **Show and hide:**
  * `XPLMSetAvionicsPopupVisible(h, 1/0)` shows or hides the popup [VERIFIED: H:873-880].
  * Hiding also closes a popped-out window. The draw callbacks keep running while the popup is hidden [VERIFIED: XPP].
  * `XPLMPopOutAvionics(h)` opens an OS window [VERIFIED: H:894-900]. Its title is the `deviceName` [VERIFIED: XPP;
    H:741-743].
* **Geometry:**
  * `XPLMGet/SetAvionicsGeometry` take (left, top, right, bottom) "in the X-Plane coordinate system" [VERIFIED:
    H:934-962]. That this is the global desktop boxel space of `XPLMGetScreenBoundsGlobal` is [INFERRED].
  * `SetAvionicsGeometry` is ignored while the device is popped out. `XPLMGet/SetAvionicsGeometryOS` work only while
    popped out [VERIFIED: XPP].
  * The flipped top/bottom and wrong popped-out geometry bugs were fixed in 12.1.0 (XPD-15251, XPD-15437, XPD-15438)
    [VERIFIED: N121].
  * Set a default geometry only when the stored one is missing or off-screen. Size it to 80 % of the desktop height with
    the bezel aspect, using `XPLMGetScreenBoundsGlobal` [INFERRED policy].
* **Pop-out window behaviour:**
  * 12.1.2 "Removed forced aspect ratio for avionics pop out screens (XPD-16003)", so users can stretch the window
    [VERIFIED: N1212].
  * 12.4.0 fixed "a huge FPS penalty when popping out an avionics instrument (XPD-16890)" [VERIFIED: N124].
  * 12.4.3 fixed custom devices popped out in VR (XPD-17503) [VERIFIED: N1243].
* **Dedicated screens:** from 12.3.0, "any plugin-defined avionics display can now be assigned directly to a
  dedicated hardware screen" [VERIFIED: N123]. Home-cockpit users get this for free.
* **Commands:**
  * No command is auto-created for plugin devices [INFERRED]. Laminar's sample creates
    `laminar/avionics_test/show_popup` and `.../show_popout` and calls `XPLMSetAvionicsPopupVisible(id, 1)` and
    `XPLMPopOutAvionics(id)` on `xplm_CommandBegin` [VERIFIED: LS].
  * Ours: `a380x/xp12/mfd/captain/popup` (toggle), `a380x/xp12/mfd/captain/popout`, and the same pair for
    `first_officer`.
  * Add menu items under `XPLMFindAircraftMenu()` with `XPLMAppendMenuItemWithCommand` [VERIFIED: SDK `XPLMMenus.h`
    lines 103-117, 184-199].

---

## 7. Brightness, bus and lighting binding

* **Callback:** `float (*)(float inRheoValue, float inAmbientBrightness, float inBusVoltsRatio, void *refcon)`
  [VERIFIED: H:634-665]. It returns "the ratio of the screen's maximum brightness that the simulator should use when
  displaying the screen in the 3D cockpit". The inputs are:
  * `inRheoValue`: the bound rheostat value;
  * `inAmbientBrightness`: "the value ... that the callback should return for the screen to be at a usable brightness
    based on ambient light";
  * `inBusVoltsRatio`: the bus voltage ratio, or -1 if the device is not bound.
* Passing NULL selects "X-Plane's default behaviour" [VERIFIED: H:730-731]. The default presumably combines the
  rheostat, the bus and the auto-adjust flag [INFERRED].
* **Laminar sample:** `if (bus >= 0 && bus*28 < 19) return 0; return rheo;` [VERIFIED: LS].
* **Rheostat helpers:** `XPLMSetAvionicsBrightnessRheo` / `XPLMGetAvionicsBrightnessRheo`. When the device is bound,
  they write or read `instrument_brightness_ratio[channel]`. Otherwise the device keeps its own value [VERIFIED:
  H:789-830]. 12.1.0 fixed "Cannot change brightness of avionics devices not bound to an aircraft" (XPD-15280)
  [VERIFIED: N121].
* **Bus voltage:** `XPLMGetAvionicsBusVoltsRatio` returns the bus ratio or -1 when unbound [VERIFIED: H:831-842]. It was
  fixed in 12.1.0b5 (XPD-15508) [VERIFIED: N121].
* **Pop-out brightness:** 12.2.0 made "Pop-out avionics brightness ... independent of the current camera view"
  (XPD-16669) [VERIFIED: N122]. Whether `brightnessCallback` also scales popups and pop-outs is [INFERRED unknown]
  (risk R3). Do not also dim inside the draw callback, or the 3D screen is dimmed twice.
* **Driving it from our own DU knob:**
  * The knobs already exist as `du_channels[side*4 + DU_MFD].level` (0..1). They are published as
    `a380x/xp12/du/captain_mfd` and `a380x/xp12/du/first_officer_mfd`, with `_up`/`_down` commands [VERIFIED: P
    `native/avionics/a380x_du.inc:12-22,60-75`].
  * The callback returns `powered ? clamp(knob * ambient) : 0`, with `ambient` replaced by 1 when it is 0. The exact
    law is a tuning item [INFERRED].
  * Bus and rheostat inputs are ignored, because the aircraft logic owns MFD power.
  * Also mirror the knob into the bound rheostat slot with `XPLMSetAvionicsBrightnessRheo` whenever it changes. This
    keeps `sim/cockpit2/switches/instrument_brightness_ratio[14/15]` coherent for hardware and other add-ons.
  * Nothing in the plugin writes `instrument_brightness_ratio` today [VERIFIED: P, grep of `native/avionics`].
* **OBJ parameters:**
  * Bus bitfield `1` (bus 0). Check that bus 0 is powered whenever the cockpit has power in this ACF [INFERRED].
  * Lighting channels `14` (CAPT) and `15` (F/O), both in the unused "custom" range [VERIFIED: DR line 5130].
  * `auto_adjust` `1`.

---

## 8. Lifecycle, versions, fallback and compile flags

### 8.1 Lifecycle

* **When to create:** "You can call this at any time: if an aircraft referencing your device is loaded before your
  plugin, the simulator will make sure to retroactively map your display into it" [VERIFIED: H:753-756].
  * In this installation the aircraft is loaded before its plugins: "Loading airplane number 0 with
    Aircraft/A380X-XP12/A380X-XP12.acf" comes before "Fetching plugins for .../A380X-XP12/plugins" [VERIFIED: LOG lines
    406, 417]. Retroactive mapping is therefore the normal path for us.
* **When to destroy:** "When you are done with the device, and at least before your plugin is unloaded, you should
  destroy the device using XPLMDestroyAvionics()" [VERIFIED: H:757-760].
  * Destroying removes any visible popup [VERIFIED: XPP].
  * Laminar's sample creates in `XPluginEnable` and destroys in `XPluginDisable` [VERIFIED: LS]. Use the same symmetry.
    The device then never exists while the plugin is disabled. 12.1.0 fixed "Crash when mousing over avionics with
    avionics plugin disabled" (XPD-15224) [VERIFIED: N121].
* **Lifecycle bugs fixed before 12.4.3** [VERIFIED: N121, N122, N1243]:
  * a crash on plugin reload when more than one plugin publishes a device (XPD-15283, 12.1.0);
  * `XPLMDestroyAvionics` crash (XPD-16895, 12.2.0);
  * `XPLMIsAvionicsPoppedOut` crash during plugin disable (XPD-18049, 12.4.3).

  Still avoid popup queries inside `XPluginDisable`, and just destroy.
* **`XPLMIsAvionicsBound(h)`:** 1 when the current aircraft uses the device [VERIFIED: H:778-788]. It "no longer
  return[s] 1 at all times" from 12.2.0 (XPD-16615) [VERIFIED: N122]. Use it as an OBJ/ID mismatch diagnostic about
  10 s after load.
* **Create failure:** `XPLMCreateAvionicsEx` returns NULL on a duplicate ID or a wrong `structSize` [INFERRED from
  H:733-740 and the `XPLMRegisterAvionicsCallbacksEx` contract at H:555-568]. Log it and fall back.

### 8.2 Versions

* API availability: SDK 4.1.0, "requires X-Plane 12.1.0 or newer" [VERIFIED: R].
* XPLM version numbers:
  * 410 for 12.1.x;
  * 420 for 12.3.x ("4.2.0 ... requires X-Plane 12.3.0 or newer");
  * 12.4.0 "incorrectly identifies itself as version 421", corrected to 430 in 12.4.1 [VERIFIED: R; N1241 "Fixed SDK
    version numbering (XPD-17840)"].
* **Practical minimum for reliable 3D touch is 12.4.0** (XPLM >= 421). It brings drag events (12.3.0), the 3D cursor
  callback fix, the `XPLMIsCursorOverAvionics` fix and the right-drag crash fix (all 12.4.0). VR touch coordinates need
  12.4.3 [VERIFIED: N123, N124, N1243].
* Policy: create the devices when XPLM >= 410. When XPLM < 421, log a warning about known input bugs.

### 8.3 Linking and fallback

* The SDK 4.3 `XPLM.framework` stub and `XPLM_64.lib` export every avionics symbol [VERIFIED: B, `nm -gU` and
  `strings`].
* Calling them directly records hard imports. The direct build of the skeleton imports 14 `_XPLM*Avionics*` symbols
  [VERIFIED: B, `nm -u`]. On a sim without them, dyld or the Windows loader refuses the whole plugin, so PFD, ND and EWD
  would be lost too [INFERRED: standard loader behaviour].
* **Use `XPLMFindSymbol`** for all avionics entry points [VERIFIED API: U:376-404, "You can use XPLMFindSymbol to
  utilize newer SDK API features without requiring newer versions of the SDK ... as your minimum"]:
  * Gate on `XPLMGetVersions` (XPLM >= 410) and on non-NULL pointers.
  * The skeleton built this way has **zero** avionics imports on mac (universal) and on win64 (mingw) [VERIFIED: B].
  * It also makes the adapter unit-testable with a mocked `XPLMFindSymbol`, like the existing mocked XPLM tests
    [VERIFIED: P `test_adirs_window.c:59-76`].
* **Compile flags:** the types (`XPLMCreateAvionics_t`, the callback typedefs) are declared only under
  `#if defined(XPLM410)`, nested inside `XPLM400` [VERIFIED: H:277, 407ff].
  * Add `-DXPLM410=1` to `common` in `build.py`, and to any syntax or test build that includes the adapter.
    `-DXPLM301..303` are not needed.
  * `a380x_avionics.c` and `a380x_efb.c` compile cleanly with `-Wall -Wextra -Werror` plus `-DXPLM410=1`, and also with
    `-DXPLM420=1` [VERIFIED: B, `clang -fsyntax-only`].
  * `-DXPLM420` is only needed for the extended cursor enums.

---

## 9. Known bugs and limitations, 12.1 to 12.4.x

| Version fixed | Issue | Relevance |
|---|---|---|
| 12.1.0 betas | `IsCursorOverAvionics` returned values over the bezel (XPD-15475); bezelScroll had wrong x/y (XPD-15442); `GetAvionicsBusVoltsRatio` was broken (XPD-15508); `PopOutAvionics` crashed on stock devices (XPD-15434); geometry flips (XPD-15251, 15437, 15438); Arrow cursor ignored (XPD-15261); popup touch marks stayed visible (XPD-15198); crash on reload with more than one device plugin (XPD-15283); brightness of unbound devices (XPD-15280); popped-out title wrong (XPD-15281) [VERIFIED: N121] | Fixed on target |
| 12.1.1 | "Avionics don't stay black anymore when changing the aircraft or livery" [VERIFIED: N1211] | Fixed |
| 12.1.2 | Forced pop-out aspect ratio removed (XPD-16003) [VERIFIED: N1212] | Users can stretch the pop-out |
| 12.2.0 | `XPLMDestroyAvionics` crash (XPD-16895); `IsAvionicsBound` always 1 (XPD-16615); pop-out brightness depended on the camera view (XPD-16669) [VERIFIED: N122] | Fixed |
| 12.3.0 | Custom devices sent no `xplm_MouseDrag` (XPD-17440); crash when dragging out of a custom device; device FBO was cleared before the draw callback (XPD-17072); new cursors not shown (XPD-17176); popup instruments without a screen ignored clicks (XPD-17029) [VERIFIED: N123] | Drag needs 12.3.0 or newer |
| 12.4.0 | `XPLMIsCursorOverAvionics` fixed; 3D cursor callbacks of custom devices fixed; crash on right-click drag out of a device manipulator; pop-out FPS penalty (XPD-16890) [VERIFIED: N124] | Hover and cursor need 12.4.0 or newer |
| 12.4.3 | VR controller touch coordinates on custom devices (XPD-18009); custom devices popped out in VR (XPD-17503); `IsAvionicsPoppedOut` crash during plugin disable (XPD-18049) [VERIFIED: N1243] | VR needs 12.4.3 |
| 12.4.4 beta | Hidden click-enabled FBO screens still consumed clicks (XPD-18347); new Panel Graphics and ImGui APIs [VERIFIED: N1244] | Do not rely on `ANIM_hide` for touch quads on 12.4.3 |
| open | Plugin drawing goes through GL with a per-callback sync cost on Metal and Vulkan [VERIFIED: GL]. Anecdote: an LCD drawn through `panel.png` cost about 15 FPS, against no loss with custom devices [TDS; INFERRED / anecdotal] | Use `drawOnDemand`; consider moving the PFD/ND/EWD to devices later |

---

## 10. Recommendation for this aircraft

### 10.1 Devices

| | CAPT | F/O |
|---|---|---|
| `deviceID` | `a380x.xp12.mfd.captain` | `a380x.xp12.mfd.first_officer` |
| `deviceName` | `A380X MFD CAPT` | `A380X MFD F/O` |
| screen | 768 x 1024 | 768 x 1024 |
| bezel / offset | 800 x 1080 / (16, 16) | same |
| OBJ bus / channel / auto | `1` / `14` / `1` | `1` / `15` / `1` |
| brightness | `du_channels[0*4+DU_MFD]` x power | `du_channels[1*4+DU_MFD]` x power |
| popup commands | `a380x/xp12/mfd/captain/popup`, `.../popout` | `a380x/xp12/mfd/first_officer/popup`, `.../popout` |

* `drawOnDemand = 1`.
* The display list is built in the existing `flight_loop` (spec 12 sections 4-5). Compare each new list with the last
  drawn one, and call `XPLMAvionicsNeedsDrawing` only on a change. Input events, the hover cursor, the caret blink and
  the 5 s cursor hide all change the list, so all are covered.
* The draw callback only runs the WP-I GL renderer. It clears, keeps alpha at 1, draws in y-up, and draws nothing when
  the screen is unpowered.

### 10.2 Code placement (spec 12 file ownership)

* `native/avionics/a380x_mfd_adapter.inc` (WP-J): the `XPLMFindSymbol` table, both devices, all callbacks, commands,
  the key sniffer, the keyboard policy and the brightness. It is included by `a380x_avionics.c`.
  * `XPluginEnable` calls `mfd_devices_enable()` (today it only sets `plugin_enabled`).
  * `XPluginDisable` calls `mfd_devices_disable()` first.
  * `flight_loop` calls `mfd_devices_update(dt)`. Do not register a second flight loop. The standalone skeleton has one
    only so it can run as a separate plugin.
* `build.py`: add `-DXPLM410=1`. Add a `test_mfd_adapter.c` with mocked `XPLMFindSymbol`/`XPLMCreateAvionicsEx` that
  checks:
  * the coordinate conversion (texel to canvas: 4 corners);
  * the keyboard routing (sniffer versus popup focus, the watchdog, consumption of down and up);
  * the brightness law;
  * the fallback when a symbol is missing.
* Keep the existing `xplm_Phase_Gauges` panel drawing for PFD, ND, EWD, FCU and EFB unchanged. The MFD never goes into
  the 2048 x 2048 panel texture.

### 10.3 Cockpit OBJ, builder and ACF

1. `tools/avionics_panel_layout.py`: add `MFD_SCREEN_SURFACES` as four explicit corners per side (BL, TL, TR, BR as seen
   from the seat), taken from the FBW model's MFD screen meshes the same way as `PFD_SCREEN_SURFACES` and
   `EFB_SCREEN_SURFACES`. Add `MFD_DEVICE_IDS` and `MFD_LIGHTING_CHANNELS`.
2. `tools/build_cockpit_controls.py`:
   * Inside the `ATTR_draw_disable ... ATTR_draw_enable` block, emit per side the snippet in section 2.5 (b): device
     tag, `ATTR_manip_device arrow <id> <label>`, 2 triangles raised 1 mm, `ATTR_manip_none`, `ATTR_no_cockpit`.
   * Also write the new visible object `objects/interior/cockpit-panels/a380x_mfd_screens.obj` (section 2.5 (a)),
     either from this builder or from a sibling one.
3. Add the asset `textures/interior/cockpit-panels/mfd_screen_black.png`: opaque black, 8 x 8.
4. `tools/build_development_aircraft.py`: attach `a380x_mfd_screens.obj` right after
   `screen_powered_off_display_faces.obj`, with `_obj_flags 13`, `_v10_is_internal 1`, psi 180, z 115.108. Update
   `tools/test_cockpit_draw_order.py` accordingly.
5. `tools/validate_aircraft.py` and `tools/test_cockpit_controls.py`:
   * every `ATTR_manip_device` id is preceded by an `ATTR_cockpit_device` with the same id, inside the cockpit object;
   * both OBJs use the same id, bus, channel and UV corners;
   * the ids equal the plugin's ids (add them to the avionics provider manifest);
   * no other manipulator triangle lies in front of an MFD touch quad.

### 10.4 Hover and cursor

* `screenCursorCallback` returns `xplm_CursorHidden` while the MFD is powered, so the MFD draws its yellow KCCU cursor
  (spec 01 section 11.1). When unpowered it returns `xplm_CursorDefault`, which shows the OBJ cursor `arrow`.
* Pointer position: `XPLMIsCursorOverAvionics` in the flight loop is the primary source, and the cursor callback is the
  secondary source.
* "Leave" = neither source reported the pointer this frame. On leave, call `mfd_pointer_leave`.

### 10.5 PC keyboard capture policy

This resolves spec 01 open question 6.

* Preference dataref `a380x/xp12/mfd/pc_keyboard_capture` (int, writable, saved with the aircraft prefs):
  * `0` = never;
  * `1` = **auto, the default**: capture while a field or free-text dropdown is focused on a powered MFD;
  * `2` = FBW behaviour: as `1`, but only while that side's KCCU `KBD` switch is OFF ("Touchscreen" mode).
* Read-only `a380x/xp12/mfd/<side>/keyboard_captured` for diagnostics.
* Routing: if the popup or pop-out is visible, use `XPLMTakeAvionicsKeyboardFocus` and `keyboardCallback`. Otherwise
  use the after-windows sniffer. Both share one key map.
* Release rules: section 5.2, rule 5.
* KCCU key commands (spec 01 section 12.5) work independently of all of this.

### 10.6 Fallback ladder

* **F0. API absent or device creation failed** (XPLM < 410, a NULL symbol, or NULL from create):
  * Do not create devices.
  * Open the MFD in a modern XPLM window, using the same core and renderer. The `popup` commands open that window, as
    with the EFB and ADIRS windows (`a380x_efb_window.inc`). Clicks and keys come through normal window callbacks.
  * The 3D MFD shows the existing powered-off face.
  * Log one line.
* **F1. The device works but 3D touch does not on some sim version.**
  * Put an `ATTR_manip_command button a380x/xp12/mfd/<side>/popup` on the MFD **bezel frame** (not on the screen).
  * The device popup always gets clicks [VERIFIED: H:289-292].
  * Keep `ATTR_manip_device` on the screen.
* **F1b. The idea in the request: `ATTR_manip_command` on the screen quad, with the handler reading
  `XPLMIsCursorOverAvionics` for coordinates.** This is **not recommended until tested.**
  * The header ties 3D mouse events to `ATTR_manip_device` [VERIFIED: H:289-293].
  * XPP saw `IsCursorOverAvionics` report only popup coordinates [VERIFIED: XPP].
  * So with a command manipulator on top it most likely returns 0 [INFERRED]. Testing it in the harness takes a minute
    (section 10.7, step 6).
  * If it does work, map command Begin, Continue and End to Down, Drag and Up.
* **F2. Last resort: a grid of `ATTR_manip_command` cells** with one command per cell.
  * 32 px cells need 768 commands and 1536 click-tested triangles per side; 16 px cells need four times that.
  * It is coarse and costs CPU [VERIFIED cost model: OBJ8]. Avoid it.

### 10.7 Verification plan (before WP-J integration)

1. Build the skeleton (section 12) standalone with `-DMFD_SKELETON_STANDALONE`. Install it as
   `plugins/A380XMfdDeviceTest/mac_x64/A380XMfdDeviceTest.xpl` in a **copy** of the installed aircraft.
2. In that copy:
   * hand-add the CAPT visible quad (a new object plus the black texture);
   * hand-add the touch quad in `a380x_cockpit_controls.obj` (section 2.5, real MFD coordinates);
   * hand-add the ACF attachment.
3. Check the binding and the coordinate mapping:
   * `Log.txt` has no "device not bound" line;
   * the screen shows the green test grid;
   * clicks at the four corners log canvas coordinates near (0,0), (768,0), (0,1024) and (768,1024).
4. Check the input paths:
   * drag logs a moving cross;
   * the wheel logs `wheel n`;
   * the OS cursor hides over the screen;
   * the cross follows hover without a click (needs the 12.4.0 cursor fixes).
5. Click the top half and type. The text shows "kbd:SNIFFER" and the typed text. Check these, then repeat the same in
   the popup ("kbd:POPUP"):
   * Enter releases the capture;
   * after 20 s idle the sim keys work again;
   * Ctrl and Cmd shortcuts still work.
6. F1b probe: temporarily swap `ATTR_manip_device` for `ATTR_manip_command button sim/none/none` and log
   `XPLMIsCursorOverAvionics` from a flight loop while clicking.
7. Brightness:
   * set the knob to 0 and to 1, and compare day and night;
   * check whether the popup follows the brightness (risk R3).
8. Platform checks:
   * Metal (this Mac) and Vulkan (Windows): text and clipping are correct, and the image is not upside down;
   * Plugin Admin > Performance shows a small cost with `drawOnDemand`;
   * VR controller touch (12.4.3).

---

## 11. Open risks

| # | Risk | Mitigation / test |
|---|---|---|
| R1 | Hover in the 3D cockpit (`XPLMIsCursorOverAvionics` and the cursor callback) on 12.4.3 is documented as fixed but untested here. | Test 10.7 step 4. Clicks alone still work, and the KCCU cursor then appears only on press. |
| R2 | `glScissor` and viewport inside device callbacks under Metal or Vulkan reverse-Y. | Derive the scissor from `sim/graphics/view/viewport` and `is_reverse_y`, or clip on the CPU. Test 10.7 step 8. |
| R3 | The brightness-callback law and whether popups and pop-outs follow it. | Tune in the sim. Never also dim in the draw callback. |
| R4 | A hidden (`ANIM_hide`) device manipulator still eats clicks on 12.4.3 or older (XPD-18347). | Never hide the touch quads. Gate input in code. |
| R5 | Key sniffer details: Cmd mapped to `xplm_ControlFlag` on mac, key repeat, stuck capture. | Tests in 10.7 step 5, the 20 s watchdog, and the preference to switch it off. |
| R6 | Albedo bleed or reflections on the visible quad if its texture is not opaque black, or if `GLOBAL_specular` is set too high. | Use a separate object with a black PNG (section 2.2). |
| R7 | The F1b fallback (command manipulator plus `IsCursorOverAvionics`) is probably non-functional. | Probe in 10.7 step 6. F0 and F1 do not depend on it. |
| R8 | Wrong U handedness from the 180-degree attachment gives mirrored touches. | Use the explicit seat-view corners, and the corner test in 10.7 step 3. |
| R9 | Popup default geometry versus geometry saved in `Miscellaneous.prf`. | Only set the geometry when it is missing or off-screen. |
| R10 | Whether X-Plane uses the device texture's alpha. | Force alpha 1 (section 3). |
| R11 | The 12.4.4 SDK 440 beta has breaking changes in the new Panel Graphics and ImGui drawing. | Not used. Re-check the GL avionics callbacks on 12.4.4 final. |

---

## 12. C skeleton (one device, all callbacks)

The file below compiles with `-std=c11 -Wall -Wextra -Werror` in four configurations [VERIFIED: B]:

* clang universal mac with the `XPLMFindSymbol` loader: no avionics imports;
* the same with `-DMFD_DIRECT_XPLM410`: 14 imports;
* library mode, without stubs;
* mingw win64: no avionics imports.

Without `-DXPLM410=1` it stops with the `#error`. With `-DMFD_SKELETON_STANDALONE` it is a complete test plugin, and
its test-pattern "core" stands in for the MFD core of spec 12.

Build command used (mac):

```
clang -std=c11 -O2 -Wall -Wextra -Werror -Wno-deprecated-declarations -fvisibility=hidden \
  -DAPL=1 -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM400=1 -DXPLM410=1 -DMFD_SKELETON_STANDALONE \
  -I "$SDK/CHeaders/XPLM" -arch arm64 -arch x86_64 -mmacosx-version-min=11.0 -dynamiclib \
  a380x_mfd_device_skeleton.c -F "$SDK/Libraries/Mac" -framework XPLM -framework OpenGL -o mac.xpl
```

```c
/* a380x_mfd_device_skeleton.c - ONE plugin-created X-Plane cockpit device (768 x 1024 MFD).
 *
 * Reference skeleton for spec 08 (docs/mfd-spec/08-xplane-cockpit-device.md).
 * - Types come from the SDK 4.1+ headers, so compile with -DXPLM400=1 -DXPLM410=1.
 * - Every avionics entry point is resolved at run time with XPLMFindSymbol, so the
 *   binary has no link-time import of XPLMCreateAvionicsEx & co. and still loads on
 *   X-Plane 12.0 (it then returns 0 from mfd_device_enable and the caller falls back).
 *   Define MFD_DIRECT_XPLM410 to call the SDK directly instead (hard 12.1+ dependency).
 * - Define MFD_SKELETON_STANDALONE to build it as a tiny test plugin: the "core" below
 *   is then a test pattern (grid, touch cross, last key) so the 3D click path can be
 *   verified in the cockpit before the real MFD core exists.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#if APL
#include <OpenGL/gl.h>
#elif IBM
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif
#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#if !defined(XPLM410)
#error "compile with -DXPLM400=1 -DXPLM410=1 (types only when calls go through XPLMFindSymbol)"
#endif

/* ------------------------------------------------------------------------------------------
 * Simulator-independent MFD core interface (spec 01 section 12.5, spec 12 section 5).
 * Canvas coordinates: 768 x 1024, origin top-left, y down.
 * ---------------------------------------------------------------------------------------- */
typedef enum { MFD_CAPT = 0, MFD_FO = 1 } MfdSide;
typedef enum { MFD_KEY_CHAR, MFD_KEY_ENT, MFD_KEY_BACKSPACE, MFD_KEY_ESC } MfdKey;
void mfd_pointer_move(MfdSide s, float x, float y);
void mfd_pointer_button(MfdSide s, int down);
int  mfd_pointer_wheel(MfdSide s, int clicks);          /* +1 = wheel up; 1 = used (list/page scrolled) */
void mfd_pointer_leave(MfdSide s);
void mfd_key(MfdSide s, MfdKey k, char ch);
unsigned mfd_keyboard_focus_serial(MfdSide s);         /* 0 = no field focused; changes on every (re)focus */
int  mfd_build_frame(MfdSide s, double now);            /* flight loop: rebuild display list, 1 = changed */
void mfd_gl_render(MfdSide s);                          /* draw callback: execute display list, y-up GL */
int  mfd_screen_powered(MfdSide s);                     /* DU powered and not switched off */
float mfd_du_knob(MfdSide s);                           /* a380x/xp12/du/<side>_mfd, 0..1 */

/* ------------------------------------------------------------------------------------------
 * Avionics entry points (XPLMDisplay.h, SDK 4.1+). Resolved at run time.
 * ---------------------------------------------------------------------------------------- */
typedef XPLMAvionicsID (*PfnCreate)(XPLMCreateAvionics_t *);
typedef void (*PfnHandle)(XPLMAvionicsID);
typedef int  (*PfnQuery)(XPLMAvionicsID);
typedef void (*PfnSetInt)(XPLMAvionicsID, int);
typedef void (*PfnSetFloat)(XPLMAvionicsID, float);
typedef int  (*PfnCursorOver)(XPLMAvionicsID, int *, int *);
typedef void (*PfnGetGeom)(XPLMAvionicsID, int *, int *, int *, int *);
typedef void (*PfnSetGeom)(XPLMAvionicsID, int, int, int, int);

static struct {
    PfnCreate create;
    PfnHandle destroy, needs_drawing, pop_out, take_keyboard;
    PfnQuery is_bound, is_popup_visible, is_popped_out, has_keyboard;
    PfnSetInt set_popup_visible;
    PfnSetFloat set_rheo;
    PfnCursorOver cursor_over;
    PfnGetGeom get_geometry;
    PfnSetGeom set_geometry;
    int xplm_version;
} av;

static int av_load(void)
{
    int xp_version = 0;
    XPLMHostApplicationID host;
    XPLMGetVersions(&xp_version, &av.xplm_version, &host);
    if (av.xplm_version < 410) return 0;          /* SDK 4.1 = X-Plane 12.1.0 */
#if defined(MFD_DIRECT_XPLM410)
    av.create = XPLMCreateAvionicsEx;          av.destroy = XPLMDestroyAvionics;
    av.needs_drawing = XPLMAvionicsNeedsDrawing; av.pop_out = XPLMPopOutAvionics;
    av.take_keyboard = XPLMTakeAvionicsKeyboardFocus;
    av.is_bound = XPLMIsAvionicsBound;         av.is_popup_visible = XPLMIsAvionicsPopupVisible;
    av.is_popped_out = XPLMIsAvionicsPoppedOut; av.has_keyboard = XPLMHasAvionicsKeyboardFocus;
    av.set_popup_visible = XPLMSetAvionicsPopupVisible;
    av.set_rheo = XPLMSetAvionicsBrightnessRheo; av.cursor_over = XPLMIsCursorOverAvionics;
    av.get_geometry = XPLMGetAvionicsGeometry; av.set_geometry = XPLMSetAvionicsGeometry;
#else
#define AV_LOAD(field, type, name) \
    if ((av.field = (type)XPLMFindSymbol(name)) == NULL) return 0
    AV_LOAD(create, PfnCreate, "XPLMCreateAvionicsEx");
    AV_LOAD(destroy, PfnHandle, "XPLMDestroyAvionics");
    AV_LOAD(needs_drawing, PfnHandle, "XPLMAvionicsNeedsDrawing");
    AV_LOAD(pop_out, PfnHandle, "XPLMPopOutAvionics");
    AV_LOAD(take_keyboard, PfnHandle, "XPLMTakeAvionicsKeyboardFocus");
    AV_LOAD(is_bound, PfnQuery, "XPLMIsAvionicsBound");
    AV_LOAD(is_popup_visible, PfnQuery, "XPLMIsAvionicsPopupVisible");
    AV_LOAD(is_popped_out, PfnQuery, "XPLMIsAvionicsPoppedOut");
    AV_LOAD(has_keyboard, PfnQuery, "XPLMHasAvionicsKeyboardFocus");
    AV_LOAD(set_popup_visible, PfnSetInt, "XPLMSetAvionicsPopupVisible");
    AV_LOAD(set_rheo, PfnSetFloat, "XPLMSetAvionicsBrightnessRheo");
    AV_LOAD(cursor_over, PfnCursorOver, "XPLMIsCursorOverAvionics");
    AV_LOAD(get_geometry, PfnGetGeom, "XPLMGetAvionicsGeometry");
    AV_LOAD(set_geometry, PfnSetGeom, "XPLMSetAvionicsGeometry");
#undef AV_LOAD
#endif
    return 1;
}

/* ------------------------------------------------------------------------------------------
 * Device geometry. Popup bezel: 16 px sides/bottom, 40 px top (X-Plane reserves the popup
 * edges and ~25 px at the top for move/resize/close/pop-out controls).
 * ---------------------------------------------------------------------------------------- */
enum { MFD_W = 768, MFD_H = 1024, BEZEL_SIDE = 16, BEZEL_TOP = 40,
       BEZEL_W = MFD_W + 2 * BEZEL_SIDE, BEZEL_H = MFD_H + BEZEL_SIDE + BEZEL_TOP };

typedef struct {
    MfdSide side;
    const char *device_id;      /* must match ATTR_cockpit_device / ATTR_manip_device in the OBJs */
    const char *device_name;    /* popped-out window title */
    const char *popup_command, *popout_command;
    XPLMAvionicsID handle;
    XPLMCommandRef popup_cmd, popout_cmd;
    int pointer_inside, last_x, last_y;   /* device texels, y-up */
    int cursor_seen;                      /* screen cursor callback ran since last flight loop */
    int bound_warned;
    unsigned kbd_serial;                  /* last seen mfd_keyboard_focus_serial() */
    int kbd_popup;                        /* popup visibility when capture was last routed */
    float last_knob;
    float since_load;
} MfdDevice;

static MfdDevice g_dev = {
    MFD_CAPT, "a380x.xp12.mfd.captain", "A380X MFD CAPT",
    "a380x/xp12/mfd/captain/popup", "a380x/xp12/mfd/captain/popout",
    NULL, NULL, NULL, 0, -1, -1, 0, 0, 0u, 0, -1.0f, 0.0f
};

/* PC keyboard capture state (one owner for the whole plugin). */
static int g_capture_side = -1;       /* -1 = off, else MfdSide captured through the key sniffer */
static float g_capture_idle;          /* seconds since last captured key (spec 01: 20 s watchdog) */
static int g_sniffer_registered;
static XPLMFlightLoopID g_loop;

static void redraw(MfdDevice *d) { if (d->handle) av.needs_drawing(d->handle); }

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* Device texel (x right, y up, origin bottom-left) -> canvas pixel centre (y down). */
static void to_canvas(int x, int y, float *cx, float *cy)
{
    *cx = clampf((float)x + 0.5f, 0.0f, (float)MFD_W);
    *cy = clampf((float)(MFD_H - 1 - y) + 0.5f, 0.0f, (float)MFD_H);
}

/* ------------------------------------------------------------------------------------------
 * Draw callbacks
 * ---------------------------------------------------------------------------------------- */
static void screen_draw(void *refcon)
{
    MfdDevice *d = (MfdDevice *)refcon;
    GLboolean cull = glIsEnabled(GL_CULL_FACE);
    /* fog, texture units, lighting, alpha test, alpha blend, depth test, depth write */
    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);                 /* X-Plane does not clear the device FBO */
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE); /* keep alpha = 1: screen must not blend */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (mfd_screen_powered(d->side)) mfd_gl_render(d->side);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    if (cull) glEnable(GL_CULL_FACE);
}

static void bezel_draw(float r, float g, float b, void *refcon)
{
    (void)refcon;
    /* Popup only (called every frame while the popup is visible). Ambient-tinted grey frame,
     * opaque black under the screen so the screen never blends with the sim behind it. */
    XPLMSetGraphicsState(0, 0, 0, 0, 0, 0, 0);
    glColor3f(0.18f * r, 0.19f * g, 0.20f * b);
    glBegin(GL_QUADS);
    glVertex2i(0, 0); glVertex2i(BEZEL_W, 0); glVertex2i(BEZEL_W, BEZEL_H); glVertex2i(0, BEZEL_H);
    glColor3f(0.0f, 0.0f, 0.0f);
    glVertex2i(BEZEL_SIDE, BEZEL_SIDE); glVertex2i(BEZEL_SIDE + MFD_W, BEZEL_SIDE);
    glVertex2i(BEZEL_SIDE + MFD_W, BEZEL_SIDE + MFD_H); glVertex2i(BEZEL_SIDE, BEZEL_SIDE + MFD_H);
    glEnd();
}

/* ------------------------------------------------------------------------------------------
 * Mouse callbacks (3D cockpit through ATTR_manip_device, and the 2D popup / pop-out)
 * ---------------------------------------------------------------------------------------- */
static int screen_touch(int x, int y, XPLMMouseStatus status, void *refcon)
{
    MfdDevice *d = (MfdDevice *)refcon;
    float cx, cy;
    to_canvas(x, y, &cx, &cy);
    mfd_pointer_move(d->side, cx, cy);
    d->pointer_inside = 1; d->last_x = x; d->last_y = y;
    if (status == xplm_MouseDown) mfd_pointer_button(d->side, 1);
    else if (status == xplm_MouseUp) mfd_pointer_button(d->side, 0);
    /* xplm_MouseDrag: position update only. */
    redraw(d);
    return 1;   /* always consume: returning 0 on MouseDown suppresses Drag/Up */
}

static int screen_right_touch(int x, int y, XPLMMouseStatus status, void *refcon)
{
    (void)x; (void)y; (void)status; (void)refcon;
    return 0;   /* no secondary button on the A380 MFD: leave right-drag (view look) to X-Plane */
}

static int screen_scroll(int x, int y, int wheel, int clicks, void *refcon)
{
    MfdDevice *d = (MfdDevice *)refcon;
    float cx, cy;
    int used = 0;
    to_canvas(x, y, &cx, &cy);
    mfd_pointer_move(d->side, cx, cy);
    if (wheel == 0 && clicks != 0) used = mfd_pointer_wheel(d->side, clicks);
    redraw(d);
    return used;   /* 0 lets X-Plane use the wheel (view zoom) when the MFD does not scroll */
}

static XPLMCursorStatus screen_cursor(int x, int y, void *refcon)
{
    MfdDevice *d = (MfdDevice *)refcon;
    d->cursor_seen = 1;
    if (!d->pointer_inside || x != d->last_x || y != d->last_y) {
        float cx, cy;
        to_canvas(x, y, &cx, &cy);
        mfd_pointer_move(d->side, cx, cy);
        d->pointer_inside = 1; d->last_x = x; d->last_y = y;
        redraw(d);
    }
    /* The MFD draws its own KCCU cursor (spec 01 section 11.1). */
    return mfd_screen_powered(d->side) ? xplm_CursorHidden : xplm_CursorDefault;
}

static int bezel_click(int x, int y, XPLMMouseStatus status, void *refcon)
{
    (void)x; (void)y; (void)status; (void)refcon;
    return 1;
}

static int bezel_scroll(int x, int y, int wheel, int clicks, void *refcon)
{
    (void)x; (void)y; (void)wheel; (void)clicks; (void)refcon;
    return 1;
}

static XPLMCursorStatus bezel_cursor(int x, int y, void *refcon)
{
    (void)x; (void)y; (void)refcon;
    return xplm_CursorDefault;
}

/* ------------------------------------------------------------------------------------------
 * Keyboard: one mapping shared by the popup keyboardCallback and the 3D key sniffer.
 * Returns 1 when the key belongs to the MFD (caller consumes it).
 * ---------------------------------------------------------------------------------------- */
static int map_key(char ch, char vkey_signed, MfdKey *key, char *out)
{
    unsigned char vkey = (unsigned char)vkey_signed;   /* XPLM vkeys are declared signed */
    *out = 0;
    if (vkey >= XPLM_VK_A && vkey <= XPLM_VK_Z) { *key = MFD_KEY_CHAR; *out = (char)('A' + vkey - XPLM_VK_A); return 1; }
    if (vkey >= XPLM_VK_0 && vkey <= XPLM_VK_9 && ch >= '0' && ch <= '9') { *key = MFD_KEY_CHAR; *out = ch; return 1; }
    if (vkey >= XPLM_VK_NUMPAD0 && vkey <= XPLM_VK_NUMPAD9) { *key = MFD_KEY_CHAR; *out = (char)('0' + vkey - XPLM_VK_NUMPAD0); return 1; }
    if (vkey == XPLM_VK_BACK || vkey == XPLM_VK_DELETE) { *key = MFD_KEY_BACKSPACE; return 1; }
    if (vkey == XPLM_VK_RETURN || vkey == XPLM_VK_ENTER || vkey == XPLM_VK_NUMPAD_ENT) { *key = MFD_KEY_ENT; return 1; }
    if (vkey == XPLM_VK_ESCAPE) { *key = MFD_KEY_ESC; return 1; }
    switch (ch) {                                       /* layout-dependent symbols by character */
    case ' ': case '/': case '.': case '+': case '-': *key = MFD_KEY_CHAR; *out = ch; return 1;
    case ',': *key = MFD_KEY_CHAR; *out = '.'; return 1;
    default: return 0;
    }
}

static void capture_release(void) { g_capture_side = -1; g_capture_idle = 0.0f; }

/* Popup / pop-out keyboard (only while the popup has avionics keyboard focus). */
static int popup_keyboard(char ch, XPLMKeyFlags flags, char vkey, void *refcon, int losing_focus)
{
    MfdDevice *d = (MfdDevice *)refcon;
    MfdKey key; char out;
    if (losing_focus) return 1;
    if (flags & (xplm_ControlFlag | xplm_OptionAltFlag)) return 0;
    if (!map_key(ch, vkey, &key, &out)) return 0;
    if (flags & xplm_DownFlag) { mfd_key(d->side, key, out); redraw(d); }
    return 1;
}

/* 3D cockpit: registered AFTER windows (inBeforeWindows = 0), passive unless capturing. */
static int key_sniffer(char ch, XPLMKeyFlags flags, char vkey, void *refcon)
{
    MfdDevice *d = (MfdDevice *)refcon;
    MfdKey key; char out;
    if (g_capture_side != (int)d->side) return 1;                  /* not ours: pass */
    if (flags & (xplm_ControlFlag | xplm_OptionAltFlag)) return 1;  /* keep sim shortcuts */
    if (!map_key(ch, vkey, &key, &out)) return 1;                   /* F-keys, arrows...: pass */
    if (flags & xplm_DownFlag) {
        mfd_key(d->side, key, out);
        g_capture_idle = 0.0f;
        if (key == MFD_KEY_ENT || key == MFD_KEY_ESC) capture_release();
        redraw(d);
    }
    return 0;                                                       /* consume down AND up */
}

/* ------------------------------------------------------------------------------------------
 * Brightness (3D cockpit screen luminance). Returning 0 makes the 3D screen black.
 * ---------------------------------------------------------------------------------------- */
static float screen_brightness(float rheo, float ambient, float bus_ratio, void *refcon)
{
    MfdDevice *d = (MfdDevice *)refcon;
    float knob = mfd_du_knob(d->side);
    (void)rheo; (void)bus_ratio;   /* power is decided by the aircraft logic, not the XP bus */
    if (!mfd_screen_powered(d->side) || knob <= 0.0f) return 0.0f;
    /* ambient = "usable" level for the current light; scale it by the DU knob. Tune in sim. */
    return clampf(knob * (ambient > 0.0f ? ambient : 1.0f), 0.0f, 1.0f);
}

/* ------------------------------------------------------------------------------------------
 * Popup commands
 * ---------------------------------------------------------------------------------------- */
static void ensure_popup_geometry(MfdDevice *d)
{
    int l, t, r, b, sl, st, sr, sb, h, w, left, top;
    av.get_geometry(d->handle, &l, &t, &r, &b);
    XPLMGetScreenBoundsGlobal(&sl, &st, &sr, &sb);
    if (r - l >= 100 && t - b >= 100 && l >= sl && r <= sr && b >= sb && t <= st)
        return;                              /* saved (Miscellaneous.prf) or user geometry */
    h = (int)((float)(st - sb) * 0.8f);
    w = h * BEZEL_W / BEZEL_H;
    left = d->side == MFD_CAPT ? sl + 40 : sr - 40 - w;
    top = st - 60;
    av.set_geometry(d->handle, left, top, left + w, top - h);
}

static int popup_command(XPLMCommandRef cmd, XPLMCommandPhase phase, void *refcon)
{
    MfdDevice *d = (MfdDevice *)refcon;
    (void)cmd;
    if (phase != xplm_CommandBegin || !d->handle) return 1;
    if (av.is_popup_visible(d->handle)) {
        av.set_popup_visible(d->handle, 0);   /* also closes a popped-out window */
    } else {
        ensure_popup_geometry(d);
        av.set_popup_visible(d->handle, 1);
    }
    redraw(d);
    return 0;
}

static int popout_command(XPLMCommandRef cmd, XPLMCommandPhase phase, void *refcon)
{
    MfdDevice *d = (MfdDevice *)refcon;
    (void)cmd;
    if (phase != xplm_CommandBegin || !d->handle) return 1;
    av.pop_out(d->handle);
    redraw(d);
    return 0;
}

/* ------------------------------------------------------------------------------------------
 * Per-frame housekeeping (flight loop: never do this work inside draw callbacks)
 * ---------------------------------------------------------------------------------------- */
static float device_flight_loop(float since_last_call, float since_last_loop, int counter, void *refcon)
{
    MfdDevice *d = &g_dev;
    int x, y, popup;
    unsigned serial;
    (void)since_last_loop; (void)counter; (void)refcon;
    if (!d->handle) return -1.0f;
    d->since_load += since_last_call;

    /* Hover: authoritative query (fixed in 12.4.0), cursor callback as a second source. */
    if (av.cursor_over(d->handle, &x, &y)) {
        if (!d->pointer_inside || x != d->last_x || y != d->last_y) {
            float cx, cy;
            to_canvas(x, y, &cx, &cy);
            mfd_pointer_move(d->side, cx, cy);
            d->pointer_inside = 1; d->last_x = x; d->last_y = y;
            redraw(d);
        }
    } else if (d->pointer_inside && !d->cursor_seen) {
        mfd_pointer_leave(d->side);
        d->pointer_inside = 0;
        redraw(d);
    }
    d->cursor_seen = 0;

    /* Content: rebuild the display list here; only redraw the device when it changed. */
    if (mfd_build_frame(d->side, (double)XPLMGetElapsedTime())) redraw(d);

    /* Keep sim/cockpit2/switches/instrument_brightness_ratio[channel] in step with our knob. */
    if (fabsf(mfd_du_knob(d->side) - d->last_knob) > 0.001f) {
        d->last_knob = mfd_du_knob(d->side);
        av.set_rheo(d->handle, d->last_knob);
    }

    /* PC keyboard policy (edge-triggered so X-Plane/other windows can take focus back):
     * popup visible -> avionics keyboard focus; 3D cockpit -> key sniffer capture. */
    popup = av.is_popup_visible(d->handle);
    serial = mfd_screen_powered(d->side) ? mfd_keyboard_focus_serial(d->side) : 0u;
    if (serial != 0u && (serial != d->kbd_serial || popup != d->kbd_popup)) {   /* (re)focused */
        if (popup) {
            av.take_keyboard(d->handle);
            if (g_capture_side == (int)d->side) capture_release();
        } else {
            g_capture_side = (int)d->side;   /* last focused side owns the PC keyboard */
            g_capture_idle = 0.0f;
        }
    } else if (serial == 0u && d->kbd_serial != 0u) {                         /* blurred */
        if (g_capture_side == (int)d->side) capture_release();
        if (popup && av.has_keyboard(d->handle)) XPLMTakeKeyboardFocus(0);   /* back to X-Plane */
    }
    d->kbd_serial = serial;
    d->kbd_popup = popup;
    if (g_capture_side == (int)d->side) {
        g_capture_idle += since_last_call;
        if (g_capture_idle > 20.0f) capture_release();   /* spec 01 section 13.4.7 watchdog */
    }

    /* Diagnostics: the OBJ must reference the device id (fixed to be meaningful in 12.2.0). */
    if (!d->bound_warned && d->since_load > 10.0f && !av.is_bound(d->handle)) {
        XPLMDebugString("A380X MFD: device not bound - check ATTR_cockpit_device ids in the OBJs\n");
        d->bound_warned = 1;
    }
    return -1.0f;
}

/* ------------------------------------------------------------------------------------------
 * Lifecycle. Call from XPluginEnable / XPluginDisable (Laminar sample does the same).
 * ---------------------------------------------------------------------------------------- */
int mfd_device_enable(void)
{
    MfdDevice *d = &g_dev;
    XPLMCreateAvionics_t p;
    XPLMCreateFlightLoop_t loop;
    if (!av_load()) {
        XPLMDebugString("A380X MFD: avionics device API unavailable - using fallback\n");
        return 0;
    }
    memset(&p, 0, sizeof(p));
    p.structSize = (int)sizeof(p);
    p.screenWidth = MFD_W;           p.screenHeight = MFD_H;
    p.bezelWidth = BEZEL_W;          p.bezelHeight = BEZEL_H;
    p.screenOffsetX = BEZEL_SIDE;    p.screenOffsetY = BEZEL_SIDE;
    p.drawOnDemand = 1;
    p.bezelDrawCallback = bezel_draw;
    p.drawCallback = screen_draw;
    p.bezelClickCallback = bezel_click;
    p.bezelRightClickCallback = bezel_click;
    p.bezelScrollCallback = bezel_scroll;
    p.bezelCursorCallback = bezel_cursor;
    p.screenTouchCallback = screen_touch;
    p.screenRightTouchCallback = screen_right_touch;
    p.screenScrollCallback = screen_scroll;
    p.screenCursorCallback = screen_cursor;
    p.keyboardCallback = popup_keyboard;
    p.brightnessCallback = screen_brightness;
    p.deviceID = (char *)d->device_id;
    p.deviceName = (char *)d->device_name;
    p.refcon = d;
    d->handle = av.create(&p);
    if (!d->handle) {
        XPLMDebugString("A380X MFD: XPLMCreateAvionicsEx failed (duplicate id or struct size)\n");
        return 0;
    }
    d->pointer_inside = 0; d->last_knob = -1.0f; d->since_load = 0.0f; d->bound_warned = 0;
    d->kbd_serial = 0u; d->kbd_popup = 0;

    d->popup_cmd = XPLMCreateCommand(d->popup_command, "Show/hide CAPT MFD popup");
    d->popout_cmd = XPLMCreateCommand(d->popout_command, "Pop out CAPT MFD window");
    XPLMRegisterCommandHandler(d->popup_cmd, popup_command, 1, d);
    XPLMRegisterCommandHandler(d->popout_cmd, popout_command, 1, d);

    g_sniffer_registered = XPLMRegisterKeySniffer(key_sniffer, 0, d);

    memset(&loop, 0, sizeof(loop));
    loop.structSize = (int)sizeof(loop);
    loop.phase = xplm_FlightLoop_Phase_AfterFlightModel;
    loop.callbackFunc = device_flight_loop;
    g_loop = XPLMCreateFlightLoop(&loop);
    XPLMScheduleFlightLoop(g_loop, -1.0f, 1);
    redraw(d);
    return 1;
}

void mfd_device_disable(void)
{
    MfdDevice *d = &g_dev;
    capture_release();
    if (g_loop) { XPLMDestroyFlightLoop(g_loop); g_loop = NULL; }
    if (g_sniffer_registered) { XPLMUnregisterKeySniffer(key_sniffer, 0, d); g_sniffer_registered = 0; }
    if (d->popup_cmd) XPLMUnregisterCommandHandler(d->popup_cmd, popup_command, 1, d);
    if (d->popout_cmd) XPLMUnregisterCommandHandler(d->popout_cmd, popout_command, 1, d);
    d->popup_cmd = d->popout_cmd = NULL;
    if (d->handle) { av.destroy(d->handle); d->handle = NULL; }   /* also removes the popup */
}

/* ------------------------------------------------------------------------------------------
 * Standalone test plugin: a test-pattern "core" so ATTR_manip_device can be verified alone.
 * ---------------------------------------------------------------------------------------- */
#if defined(MFD_SKELETON_STANDALONE)
static float t_x = -1.0f, t_y = -1.0f;
static int t_down, t_inside;
static unsigned t_focus;                      /* focus serial: 0 = no field focused */
static char t_text[25];
static int t_dirty = 1;

void mfd_pointer_move(MfdSide s, float x, float y) { (void)s; t_x = x; t_y = y; t_inside = 1; }
void mfd_pointer_button(MfdSide s, int down)
{
    char msg[96];
    (void)s;
    t_down = down;
    if (down) t_focus = t_y < 512.0f ? t_focus + 1u : 0u;   /* top half = "text field" */
    snprintf(msg, sizeof(msg), "A380X MFD test: %s at canvas %.1f,%.1f\n", down ? "down" : "up", t_x, t_y);
    XPLMDebugString(msg);
}
int mfd_pointer_wheel(MfdSide s, int clicks)
{
    char msg[64];
    (void)s;
    snprintf(msg, sizeof(msg), "A380X MFD test: wheel %d\n", clicks);
    XPLMDebugString(msg);
    return 1;
}
void mfd_pointer_leave(MfdSide s) { (void)s; t_inside = 0; }
void mfd_key(MfdSide s, MfdKey k, char ch)
{
    size_t n = strlen(t_text);
    (void)s;
    if (k == MFD_KEY_CHAR && n + 1 < sizeof(t_text)) { t_text[n] = ch; t_text[n + 1] = 0; }
    else if (k == MFD_KEY_BACKSPACE && n) t_text[n - 1] = 0;
    else if (k == MFD_KEY_ENT || k == MFD_KEY_ESC) t_focus = 0u;
}
unsigned mfd_keyboard_focus_serial(MfdSide s) { (void)s; return t_focus; }
int mfd_build_frame(MfdSide s, double now) { (void)s; (void)now; int r = t_dirty; t_dirty = 0; return r; }
int mfd_screen_powered(MfdSide s) { (void)s; return 1; }
float mfd_du_knob(MfdSide s) { (void)s; return 1.0f; }
void mfd_gl_render(MfdSide s)
{
    float white[3] = { 1, 1, 1 }, cyan[3] = { 0, 1, 1 };
    char line[96];
    int i;
    (void)s;
    glColor3f(0.0f, 0.25f, 0.0f);
    glBegin(GL_LINES);
    for (i = 0; i <= MFD_W; i += 64) { glVertex2i(i, 0); glVertex2i(i, MFD_H); }
    for (i = 0; i <= MFD_H; i += 64) { glVertex2i(0, i); glVertex2i(MFD_W, i); }
    glEnd();
    glColor3f(0.1f, 0.1f, 0.4f);              /* top half: "text field" */
    glBegin(GL_QUADS);
    glVertex2i(0, 512); glVertex2i(MFD_W, 512); glVertex2i(MFD_W, MFD_H); glVertex2i(0, MFD_H);
    glEnd();
    if (t_inside) {                           /* cursor: canvas y-down -> GL y-up */
        float gx = t_x, gy = (float)MFD_H - t_y;
        glColor3f(1.0f, t_down ? 0.0f : 1.0f, 0.0f);
        glBegin(GL_LINES);
        glVertex2f(gx - 40, gy); glVertex2f(gx + 40, gy);
        glVertex2f(gx, gy - 40); glVertex2f(gx, gy + 40);
        glEnd();
    }
    snprintf(line, sizeof(line), "canvas %.1f, %.1f  %s  kbd:%s", t_x, t_y,
             t_down ? "DOWN" : "up", g_capture_side >= 0 ? "SNIFFER" : t_focus ? "POPUP" : "off");
    XPLMDrawString(white, 20, 40, line, NULL, xplmFont_Proportional);
    XPLMDrawString(cyan, 20, 900, t_text[0] ? t_text : "click here and type", NULL, xplmFont_Proportional);
    t_dirty = 0;
}

PLUGIN_API int XPluginStart(char *name, char *sig, char *desc)
{
    strcpy(name, "A380X MFD device test");
    strcpy(sig, "org.a380x.xp12.mfd-device-test");
    strcpy(desc, "Cockpit device / ATTR_manip_device test harness");
    return 1;
}
PLUGIN_API void XPluginStop(void) {}
PLUGIN_API int XPluginEnable(void) { mfd_device_enable(); return 1; }
PLUGIN_API void XPluginDisable(void) { mfd_device_disable(); }
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, int msg, void *param)
{
    (void)from; (void)msg; (void)param;
}
#endif /* MFD_SKELETON_STANDALONE */
```
