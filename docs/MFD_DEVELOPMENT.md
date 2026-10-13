# Native A380 MFD, KCCU and FMS

The two A380 MFDs are drawn by the aircraft-local `A380XAvionics` plugin as X-Plane plugin
cockpit devices. The MFD pages are a C translation of the pinned FlyByWire A380X MFD; the FMS,
navigation database and flight-plan store are native as well. Specs live in `docs/mfd-spec/`
(01-07 behaviour, 08 X-Plane device, 09/09b integration and geometry, 12 architecture).

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
MFD frame (both sides) → `XPLMAvionicsNeedsDrawing` only for a side whose display list changed.
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

## Known limits

- No VNAV/LNAV predictions or managed lateral/vertical guidance: managed speed only.
- The F-PLN is not yet mirrored to the ND (the ND still reads the stock X-Plane FMS route).
- LS tuning uses NAV1/NAV2 (the port's PFD/ILS inputs), shared with VOR1/VOR2 (spec 06 §3.1
  interim allocation).
- D-ATIS is synthesised from the simulator weather at the airport (`XPLMGetWeatherAtLocation`);
  it is not an internet ATIS. TERR SYS OFF does not yet gate the ND terrain layer.
- `pc_keyboard_capture` is not persisted between sessions.
