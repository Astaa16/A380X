# L/G GRVTY panel (landing-gear gravity extension)

The center-instrument-panel L/G GRVTY assembly: a striped master cover, two switch
guards and the 3-position RESET / OFF / DOWN switch, with FlyByWire's gravity-extension
handle and its effect on X-Plane's landing gear. Native code: `native/avionics/a380x_lg_grvty.h`
(XPLM-free core) and `a380x_lg_grvty.inc` (X-Plane adapter, unity build of the A380XAvionics
plugin). Geometry: `tools/grvty_cockpit_panel.py`. Nothing here has been flown in X-Plane yet.

## FlyByWire sources (pinned revision of `upstream-aircraft`)

| Behaviour | Source |
| --- | --- |
| Cover, guards, switch, interlocks, start state | `fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire/Part_Interior_Cockpit/model/behaviour/mip.xml` lines 122-188 |
| Handle angle and turns | `fbw-a380x/src/wasm/systems/a380_systems/src/hydraulic/mod.rs` `A380GravityExtension` (lines ~6991-7060) |
| Valves and uplocks per turn | same file, `A380GearHydraulicController::update_safety_and_vent_valve` / `update_uplocks` (lines ~3300-3335) |
| Gear consumption of those flags | `fbw-common/src/wasm/systems/systems/src/hydraulic/landing_gear.rs` (`GearSystemHydraulicSupply`, `GearSystemComponentAssembly::update_hydraulic_control`) |
| ECAM | `FwsAbnormalSensed.ts` 320700001 (`L:A32NX_LG_GRVTY_SWITCH_POS === 2`), `ata31-32-33.ts` (deferred L/G GRVTY EXTN and the L/G GEAR NOT LOCKED DOWN items), `WdCpiomFailedFallbackChecklistComponent.tsx` (static text, already ported) |
| SD WHEEL | `SD/Pages/Wheel/WheelPage.tsx`: `L/G GRVTY EXTN IN PROGRESS` is authored with `visibility="hidden"` and reads no variable, so nothing is published for it |

## Interlocks (mip.xml, enforced natively)

| Control | FBW variable | Moves when |
| --- | --- | --- |
| Striped master cover `GRVTY_LDG_LOCK1` | `L:A32NX_LG_GRVTY_MASTER_SWITCH_GUARD` | always (unconditional toggle) |
| Guard 1 `GRVTY_LDG_LOCK2` (crew left) | `L:A32NX_LG_GRVTY_SWITCH_GUARD_1` | master cover open |
| Guard 2 `GRVTY_LDG_LOCK3` (crew right) | `L:A32NX_LG_GRVTY_SWITCH_GUARD_2` | master cover open |
| Switch `GRVTY_LDG` to RESET (0) or OFF (1) | `L:A32NX_LG_GRVTY_SWITCH_POS` | master cover open |
| Switch to DOWN (2) | same | master cover and both guards open |

Cold & dark (plugin start and every aircraft load): cover and guards closed, switch OFF,
handle at 0 deg. A refused click is counted but changes nothing, so nothing animates.
As in FBW, the cover and guards can be closed over a selected DOWN; DOWN stays selected until
the crew moves the switch (which then needs the cover open again).

## Gravity-extension handle (A380GravityExtension)

The handle angle runs on simulated time (zero while paused or in replay, scaled by
`sim/time/sim_speed_actual`): +50 deg/s in DOWN, -200 deg/s in RESET, unchanged in OFF,
clamped to 0..1080 deg. Turns = floor(angle / 360).

| Turns | FBW effect | Time from rest in DOWN | Port effect |
| --- | --- | --- | --- |
| >= 1 | cut-off valve closed, vent valves open: no gear manifold pressure, the lever moves nothing | 7.2 s | X-Plane's gear handle held; the lever is disconnected |
| >= 2 | door uplocks mechanically released | 14.4 s | `grvty_doors_released` (doors stay slaved to X-Plane's gear deployment) |
| >= 3 | gear uplocks released: the gear falls and locks down | 21.6 s | gear selected down + X-Plane emergency override |

From 3 turns, RESET takes 1.8 s to drop below 2 turns, 3.6 s to drop below one turn (the
hydraulics and the lever are back) and 5.4 s to return to rest. Selecting OFF part-way holds
the angle; a handle held at one or more turns keeps the hydraulics isolated.

## X-Plane gear mapping (chosen mechanism and evidence)

1. **Lever lockout (>= 1 turn).** The plugin takes ownership of
   `sim/cockpit2/controls/gear_handle_down` at the value it had. The cockpit lever click
   (`a380x/xp12/center_right/gear_toggle`) and the stock lever commands
   `sim/flight_controls/landing_gear_toggle/up/down/off` (intercepted with "before" handlers and
   consumed only while owned) move only the crew lever. A direct write to the handle by another
   plugin or hardware is taken as a crew lever movement and the held value is restored on the
   same frame. `a380x/xp12/center_right/gear_lever` (lever animation, lamps) and the FWS
   `gear_lever_down` follow the crew lever, as FBW's LGCIU still reads the lever.
2. **Uplock release (>= 3 turns).** The held handle becomes DOWN and
   `sim/flight_controls/landing_gear_emer_on` ("Landing gear emergency override down") is
   commanded once. Evidence: Laminar Research's own Airbus A330-300
   (`Aircraft/Laminar Research/Airbus A330-300/plugins/xlua/scripts/A333.switches/A333.switches.lua`,
   `A333_ldg_grav_extn_dnCMDhandler` / `_upCMDhandler`) implements its gravity-extension switch
   with exactly `landing_gear_emer_on` / `landing_gear_emer_off`, on an ACF whose hydraulic gear
   fails retracted (`_hyd_gear_fail_mode 0`), i.e. the override is X-Plane's way to lower a
   hydraulic gear without hydraulic pressure. The handle is also written DOWN so that the gear
   comes down whichever way X-Plane resolves the override (with or without hydraulics).
3. **RESET (below one turn).** The handle takes the crew lever position first, then
   `landing_gear_emer_off` releases the override. With the lever already DOWN the gear never
   retracts. With the lever UP the gear retracts if X-Plane has hydraulic pressure, which is
   FBW's behaviour (cut-off valve open, LGCIU commands the lever position).
4. **Plugin disable / stop:** handle = crew lever, then the override is released. **Aircraft
   load:** the panel goes cold & dark and an engaged override is released without touching the
   handle X-Plane has just set. **Replay:** nothing is written.

Datarefs.txt has no dataref for the emergency override state, so the plugin only tracks the
override it engaged itself (`grvty_emer_override`). `build.py` verifies the raw datarefs
against the installed `DataRefs.txt` and the six stock commands against the sibling
`Commands.txt`.

### Unverified (no simulator test was possible)

- How X-Plane 12 moves the gear under `landing_gear_emer_on`: speed (the ACF's 20 s gear cycle
  or a free-fall rate), whether the doors follow, and whether it depends on hydraulic pressure.
- The ACF has `_hyd_gear 3` and `_hyd_gear_fail_mode 2` with `_hyd_gear_fail_speed 200`
  ("fail down below 200 kt"): on a hydraulic loss X-Plane may lower the gear by itself below
  200 kt, before any gravity extension. ToLiss and Laminar Airbus ACFs use fail mode 0 (gear
  stays retracted). Not changed here (ACF outside this feature); worth reviewing.
- Whether any other plugin or hardware driver fights the held handle every frame (the plugin
  restores it each frame and logs nothing).
- Cockpit click alignment, manipulator depth order and the authored poses in X-Plane.

## Deviations from FBW

- Gear doors have no separate actuators in the port (see
  [LANDING_GEAR_DOORS.md](LANDING_GEAR_DOORS.md)): the 2-turn door release is published but the
  3D doors, and the SD door indications, follow X-Plane's gear deployment.
- A gear already in transit when the hydraulics are isolated completes the travel X-Plane was
  commanding instead of stopping and trailing.
- FBW returns hydraulic control to the lever as soon as the handle is below one turn, i.e. while
  the switch is still at RESET; the port does the same (the real-aircraft rule "switch back at OFF
  before using the lever" is crew procedure, not modelled as an extra lockout).
- The port's own L/G GEAR NOT LOCKED UP alert (not implemented in FBW) can trigger while the gear
  is gravity-extended with the lever UP, before the procedure's lever DOWN.
- Cover and guard clips are STEP keys in the source; the port swings them in 0.22 s (as the ELEC
  guards) and throws the switch in 0.1 s per position.

## FWS

`FwsInputs.lg_grvty_switch_pos` feeds:
- 320700001 (deferred L/G GRVTY EXTN): items `[-, L/G LEVER UP, L/G GRVTY DOWN
  (switch == 2), all gear down-locked, L/G LEVER DOWN]`, as FwsAbnormalSensed.ts.
- 320800039 (port L/G GEAR NOT LOCKED DOWN): its sensed `L/G GRVTY .... DOWN` item.
`gear_lever_down` is now the crew lever (`center_right.gear_lever`).

## Cockpit geometry and clicks

Staged by `tools/grvty_cockpit_panel.py` from the pinned glTF
(`tools/patch_cockpit_materials.py` `DEFAULT_SOURCE`); unit test
`tools/test_grvty_cockpit_panel.py`.

| Part | Source nodes (material) | OBJ | Dataref | Poses |
| --- | --- | --- | --- | --- |
| Switch | `SWITCH_GRVTY` subtree: `GRVTY_LDG`, `Cylinder.105/106`, `SWITCH_OVHD_EXTLT_WING.001` (MIP_KNOBS_02) | `panel_a380_cockpit_mip_knobs_02_4k.obj` | `grvty_switch_anim` 0 RESET, 1 OFF, 2 DOWN | 0, 16.07, 32.15 deg about the hinge (RESET is the source rest pose) |
| Guard 1 / 2 | `GRVTY_LDG_LOCK2` / `LOCK3` (MIP_KNOBS_02) | same | `grvty_guard_1_anim` / `_2_anim` 0..1 | 89.4 / 86.9 deg |
| Master cover | `Plane.160` + `GRVTY_LDG_LOCK_COVER` (MIP03) | `panel_a380_cockpit_mip03_4k.obj` | `grvty_master_guard_anim` 0..1 | 76.7 deg |

Only the exact source triangle ranges are wrapped in ANIM blocks (checked: VT/IDX/UV/texture
payload, every other line, the drawn index ranges, and that the ranges were static). The
controls object gets append-only draw-disabled 12-triangle boxes after `# A380X LG GRVTY V1`,
each animated with its part: the authored `GRVTY_LDG_LOCK1` click spot (cover, hand cursor,
always active), each guard (hand cursor) and the switch (`ATTR_manip_command_switch_up_down`,
upper half = `grvty_switch_up` toward RESET, lower half = `grvty_switch_down` toward DOWN, one
detent per click, like the port's other 3-position switches). The guards and switch boxes are
hidden while `grvty_master_guard` is 0.

## Datarefs and commands (`a380x/xp12/gear/`)

Read-only integers: `grvty_master_guard`, `grvty_guard_1`, `grvty_guard_2`, `grvty_switch_pos`,
`grvty_handle_turns`, `grvty_hyd_isolated`, `grvty_doors_released`, `grvty_gears_released`,
`grvty_lever_disconnected`, `grvty_emer_override`. Read-only floats: `grvty_handle_angle_deg`,
`grvty_master_guard_anim`, `grvty_guard_1_anim`, `grvty_guard_2_anim`, `grvty_switch_anim`.
Commands: `grvty_master_guard_toggle`, `grvty_guard_1_toggle`, `grvty_guard_2_toggle`,
`grvty_switch_up`, `grvty_switch_down`. The list is `GRVTY_COMMANDS` / `GRVTY_DATAREFS` in
`tools/avionics_provider.py`; `build.py` fails when the native registry differs.

## Tests and installation

`native/avionics/build.py` runs `test_lg_grvty.c` (interlocks, 7.2 / 14.4 / 21.6 s, RESET
200 deg/s, lever lockout window, override ordering, cold & dark), its MinGW `-Werror` compile
check, `test_lg_grvty_adapter.c` (mock XPLM with the production center-right adapter: registry,
interlocks on the commands, pause / sim speed / replay, stock command interception, hardware
handle writes, override ordering, disable, reload, shutdown) and the FWS signal tests, and
records the results and the contracts in `build-report.json` (`lg_grvty`).

Install order: geometry first (`tools/install_grvty_panel.py --aircraft output/A380X-XP12
--geometry-stage <stage>`; refuses if any of the three OBJs changed since staging), then the
plugin (`tools/install_xp_kit_cockpit.py <build>`), which also writes the new commands and
datarefs into `avionics-providers/fbw-avionics-v1.json`. Then `tools/validate_aircraft.py`.
