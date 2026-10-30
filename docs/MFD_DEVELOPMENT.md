# Native A380 MFD, KCCU and FMS

The two A380 MFDs are drawn by the aircraft-local `A380XAvionics` plugin as X-Plane plugin
cockpit devices. The MFD pages are a C translation of the pinned FlyByWire A380X MFD; the FMS,
navigation database and flight-plan store are native as well. Specs live in `docs/mfd-spec/`
(01-07 behaviour, 08 X-Plane device, 09/09b integration and geometry, 12 architecture).
The macOS `--xpkit` build now renders these native page ports through an aircraft-local
WebKit/xp-kit component backend at full 768×1024 resolution. It is not a second upstream
JavaScript FMC. Input and FMS state remain native; OpenGL is the automatic fallback.
See [the renderer integration](../native/avionics/xp-kit-mfd/README.md).

## Requirements

- X-Plane **12.1.0 or newer** for the cockpit-device API (XPLM410). 12.4.0+ is recommended:
  earlier versions miss device drag events (12.3), 3D hover/cursor fixes (12.4.0) and VR touch
  coordinates (12.4.3).
- The avionics functions are resolved with `XPLMFindSymbol`; on X-Plane 12.0, or if device
  creation fails, the plugin still loads and each MFD opens in a plain floating window instead
  (fallback F0, `Log.txt`: "MFD opens in a window").
- Navigation data: X-Plane's `Custom Data/` (Navigraph/AIRAC) overrides
  `Resources/default data/` file by file. It is loaded once at plugin enable (~0.6 s, ~18 MB).

## Runtime pieces

| File | Role |
|---|---|
| `a380x_mfd*.c`, `a380x_mfd_page_*.c`, `a380x_mfd_pages.c` | display list, widgets, frame, all pages, registration |
| `a380x_mfd_render.inc` | OpenGL executor of the display list (font atlas `a380x_mfd_font.h`) |
| `a380x_mfd_adapter.inc` | devices, draw-on-demand, touch/drag/wheel/hover, brightness, popups, KCCU, keyboard |
| `a380x_fms_adapter.inc` | navdata + flight plans, FMS inputs, FMS outputs to FWS/PFD/EWD/guidance, `a380x/xp12/fms/*` |
| `a380x_mfd_systems_host.inc` | SURV states and transponder law, FCU/EFIS BKUP, POSITION (EPU/RNP, navaid tuner), D-ATIS |

Frame order in the single flight loop: ADIRS → FMS update (previous-frame guidance) → FWS phase
(V1 from the FMS; no V1 = never "above V1") → guidance (managed speed from the FMS) → ... →
MFD frame (both sides) → changed display lists sent to WebKit in the xp-kit build →
device redraws for completed textures (or native draw-on-demand when using OpenGL alone).
Stock X-Plane FMS/autopilot mode datarefs are never consumed.

## Cockpit

- Visible screens: `objects/interior/cockpit-panels/a380x_mfd_screens.obj` (black albedo,
  `ATTR_cockpit_device <id> 1 14|15 1`), attached right after the dark display faces.
- Touch: two draw-disabled quads in `a380x_cockpit_controls.obj` with `ATTR_cockpit_device` +
  `ATTR_manip_device`. Click, drag, wheel and hover go to the plugin; the MFD draws its own
  yellow cursor (the OS cursor is hidden over a powered screen).
- KCCU: 136 click targets (both pedestal keyboards, wheel, select button, trackball = pop-up,
  KBD/CCD switches) bound to `a380x/xp12/kccu/<captain|first_officer>/<key>`.
- Brightness / off: DU control panel MFD knobs (`a380x/xp12/du/<side>_mfd`); 0 = DU off (black,
  input ignored). The knob is mirrored into `instrument_brightness_ratio[14|15]`.

## Commands and datarefs

- `a380x/xp12/mfd/<side>/popup` (toggle) and `/popout`; also in the aircraft menu "A380X MFD".
- `a380x/xp12/kccu/<side>/<key>`: `a`..`z`, `0`..`9`, `dot`, `plusminus`, `ent`, `backspace`,
  `sp`, `slash`, `esc`, `esc2`, `up`/`down`/`left`/`right`, `dir`, `perf`, `init`, `navaid`,
  `mailbox`, `fpln`, `dest`, `secindex`, `surv`, `atccom`, `nd`, `kbd`, `clrinfo`, `rewind`,
  `forward`, plus `wheel_up`, `wheel_down`, `click` (button at the MFD cursor),
  `kbd_switch_toggle`, `ccd_switch_toggle`. Bind them to hardware freely.
- `a380x/xp12/kccu/<side>/kbd_on|ccd_on` (int, writable), `a380x/xp12/mfd/<side>/active_page`
  (bytes, URI), `a380x/xp12/mfd/<side>/keyboard_captured`.
- `a380x/xp12/mfd/pc_keyboard_capture` (writable): 0 never, **1 auto (default)**, 2 FBW (only
  while the KCCU KBD switch is OFF). While a field is being edited on a powered MFD the PC
  keyboard types into it (letters, digits, `.`, `/`, `+`, `-`, space, Backspace, Enter, Esc);
  Ctrl/Cmd/Alt shortcuts and F-keys always reach X-Plane. Capture ends on Enter/Esc, blur,
  navigation, DU off or 20 s without a keystroke. With the pop-up open, keys go to the pop-up.
- `a380x/xp12/fms/*` (spec 07 §10.3; writable: `switching_knob`, `fmc_{a,b,c}_failed`,
  `fmc_{a,b,c}_reset`), `a380x/xp12/surv/*` (SURV states, writable).

## How to test in the simulator

1. Quit X-Plane before refreshing the package (the install is a symlink; replacing a loaded
   plugin is unsafe). Start X-Plane, load the A380, cold & dark or engines running.
2. `Log.txt` should show "A380X MFD: 11 page modules linked", the navdata AIRAC line and no
   "device not bound" line ~10 s after load.
3. Cockpit view of the lower centre pedestal: both MFDs show DATA/STATUS with the AIRAC dates
   once avionics power is on and the DU MFD knob is above 0. Hover shows the yellow cursor.
4. Click the MFD (or press KCCU INIT) → INIT, type FROM/TO with the PC keyboard or the KCCU,
   then FUEL&LOAD (ZFW), PERF T.O (V1/VR/V2, FLEX) and F-PLN (departure/arrival, airways,
   DIR TO). V-speeds then appear on the PFD speed tape, FLEX on the EWD with a FLX rating.
5. Pop-ups: aircraft menu "A380X MFD" or commands `a380x/xp12/mfd/<side>/popup|popout`.
6. Corner check: clicks at the four screen corners must land on the matching MFD corner (a
   mirrored response would mean wrong UVs).

## Cockpit input and cursor correction installed 16 October 2026

`build/xp-kit-mfd-cockpit-20261016-d` is installed in the simulator-linked aircraft.
The user requested a direct installation without backup. Both MFD touch twins
now use a 3 mm normal offset, putting them 1 mm
in front of the 2 mm visible screen. The installer refreshes only their eight
vertex rows, preserving unrelated cockpit geometry and edits. Geometry tests
and package validation guard this placement and the unchanged UVs/winding.

Native PC keyboard arrows now reach menu navigation. A held KCCU select no
longer loses its press when a hover callback is absent. The native adapter suite
passes 371 checks, including quick taps through the registered device callbacks
on both seats, all five header menus and selection of real linked pages. These
are mocked-SDK boundary tests, not proof of clicks inside a running simulator.

Read-only `a380x/xp12/mfd/<side>/` diagnostics now include `device_bound`,
`touch_down_count`, `touch_up_count`, `cursor_x`, `cursor_y`, `cursor_visible`,
`menu_open` and `events_dropped`. `tools/check_mfd_cockpit_input.py` observes
actual touchscreen callback/menu/page transitions without injecting input or
modifying a flight plan. Close MFD popups and use the 3D cockpit for that test.

The native cursor is now drawn over a cached WebKit page. The transport test
checks 1,000 cursor positions without a page update; real menu/page changes still
update the texture. The actual helper fixture probe captured each static MFD
once in 20 seconds, with fresh heartbeats and no errors. This is not a measured
in-simulator frame-rate result.

The original backup attempt stalled in macOS fcopyfile allocation. The cancelled
process subsequently exited. The installer now uses atomic streamed copies to
avoid that path. The direct installation completed and its binary hashes,
signatures, provider registry and touch geometry were verified. Reproduction:

```sh
python3 tools/install_xp_kit_cockpit.py build/xp-kit-mfd-cockpit-20261016-d --no-backup
```

Real 3D cockpit dropdown interaction and cursor FPS still require a simulator run.

### Off-screen release coordinates (22 October, X-Plane 12.4.4)

After the X-Plane 12.4.4 update the 3D cockpit reports every MouseUp at the off-screen
texel (-16,-16) instead of (0,0) (log of 21 Oct 22:36: `DOWN texel=219,780` on a
field, then `UP texel=-16,-16 canvas=0,1024`, `inside=0 click=00000000`), so no field
ever took keyboard focus. `mfda_positionless()` now treats (0,0) and any texel outside
the 768x1024 screen as a release/drag without position: the gesture finishes at its last
valid position. The xp-kit ND touch handler applies the same rule. Regression:
`test_mfd_adapter.c` `test_zero_coordinate_release_and_data_entry` (both sentinels, and a
drag away followed by an off-screen release still does not click).

### Native click tracing (16 October follow-up)

The running `-d` build received paired captain MFD callbacks, but no open menu
was observed. The user's cockpit screenshot places the yellow cursor inside
ACTIVE, consistent with the hover coordinates. This does not establish the
actual press/release coordinates: the hover diagnostic can change after a tap.

The `-e` staging build retains `last_down_x/y`, `last_up_x/y`, `press_target`,
`click_target`, `click_count` and `menu_open_count` per seat as read-only datarefs.
Short `A380X MFD input` / `resolve` log traces record raw SDK coordinates and
native hit resolution for six frames after a press/release. There is no
per-mouse-motion logging, snapshot or input injection. A touch callback also
counts as cursor presence for that flight-loop frame. The observer uses
parallel reads and persistent menu counters so transient openings are not lost.
This diagnostic build must not be described as a verified dropdown fix.

The click-tracing build is now installed from the internal-disk staging folder
`/Users/sava/Desktop/A380/build-mfd-click-trace-20261016-e2`. On the next check,
Untitled was already mounted, writable and responsive, with no blocked unmount
or write processes. No forced eject, filesystem-service restart, repair, or
Mac restart was performed. Installation used `--no-backup` and completed with
`backup: null`.

Installed Mac plugin SHA-256:
`0626f90100a67100b1ada4263873120a8c6932eea7013aa0475423302212a394`.
The installed plugin/helper signatures and build hashes, provider contracts,
451 native input checks, 36 cockpit-control tests and 5 atomic-installer tests
passed. Both cockpit OBJ hashes stayed unchanged. The installer now also
skips unchanged text/geometry files instead of rewriting them on every install.
Actual 3D cockpit dropdown operation still needs a run with this tracing build.

## Remaining aircraft-system limits

- No VNAV/LNAV predictions or managed lateral/vertical guidance: managed speed only.
- The xp-kit ND reads the native active F-PLN and draws supported straight TF/DF/CF legs;
  discontinuities are not bridged and full turn/hold/VNAV geometry is not invented.
- LS tuning uses NAV1/NAV2 (the port's PFD/ILS inputs), shared with VOR1/VOR2 (spec 06 §3.1
  interim allocation).
- D-ATIS is synthesised from the simulator weather at the airport (`XPLMGetWeatherAtLocation`);
  it is not an internet ATIS. TERR SYS OFF does not yet gate the ND terrain layer.
- `pc_keyboard_capture` is not persisted between sessions.
