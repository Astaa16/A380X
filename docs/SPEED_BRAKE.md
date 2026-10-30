# SPEED BRAKE lever, speed brakes and ground spoilers

The A380 SPEED BRAKE lever (push down to move, a notch at 1/2, pulled up at RET to ARM the ground spoilers)
and FlyByWire's PRIM speed brake and ground spoiler logic. Native code: `native/avionics/a380x_speedbrake.h`
(XPLM-free core) and `a380x_speedbrake.inc` (X-Plane adapter), unity-included from `a380x_avionics.c` after the
flaps lever. Geometry: `tools/speedbrake_lever_cockpit.py`, installed by `tools/install_speedbrake_lever.py`.
Nothing here has been flown in X-Plane yet.

Panel legend, top to bottom: GND SPLRS ARMED (above RET), RET, 1/2 (marks above and below), FULL.

## FlyByWire sources (pinned revision 2baa2b35 of `upstream-aircraft`)

| Behaviour | Source |
| --- | --- |
| Speed brake command: lever table, 5 deg/s, inhibition and its latch; ground spoilers armed / extension / retraction; wheel latch; phased lift dumping; spoiler lift; per-panel speed brake gains; 40 deg/s panel orders | `fbw-a380x/src/wasm/fbw_a380/src/model/A380PrimComputerFctl.cpp` (+ `_data.cpp` for every constant, Simulink parameter struct decoded field by field) |
| PRIM flap index 0 "0" / 1 "1" / 2 "1+F" / 3 "2" / 4 "3" / 5 "FULL" from the SFCC status word | `model/A380PrimComputerGeneralLogic.cpp` (bits 17, 18 + 26, 19, 20, 21) |
| Lever handle 0..1 + armed, key events (ON / OFF / TOGGLE / SET / AXIS / ARM_ON / ARM_OFF / ARM_TOGGLE) | `fbw_a380/src/SpoilersHandler.cpp` |
| `speed_brake_lever_pos` = -0.05 when armed, else the handle | `fbw_a380/src/FlyByWireInterface.cpp` (`updateSpoilers`, PRIM inputs) |
| FCDC discrete word 4 bit 27 = armed, bit 28 = lever > 0.9 | `fbw_a380/src/fcdc/Fcdc.cpp` |
| Lever positions O:Position 0 ARM, 1 RET, 2 1/2, 3 FULL, one drag reaching ARM past RET | `Part_Interior_Cockpit/model/behaviour/legacy/generated/A32NX_Interior_Handling.xml` `FBW_HANDLING_Lever_Spoilers_Template`, `behaviour/pedestal/pedestal.xml` |
| Lever clip, slot brush rig | `Part_Interior_Cockpit/model/A380_COCKPIT_LOD00.gltf` clip `lever_speed_brake`, skin 7 (meshes `Plane.083`, `Plane.084`) |
| PFD speed brake symbol, GND SPLRS armed triangle | `systems/instruments/src/PFD/LowerArea.tsx`, `PFDSimvarPublisher.tsx` (`L:A32NX_LEFT_SPOILER_1_COMMANDED_POSITION`, `L:A32NX_SPOILERS_ARMED`) |

## FBW logic (PRIM, arm for arm)

Degrees are positive up here (FBW orders are negative). `lever` = `speed_brake_lever_pos`, `TLA` = FBW
thrust lever angle in degrees (reverse negative).

- **Speed brake command.** `lever` through FBW's table 0 / 0.05 / 0.051 / 0.5 / 1 -> 0 / 0 / 2 / 33.75 / 45 deg
  (linear, `look1_binlxpw`), rate-limited to 5 deg/s. 1/2 gives 33.75 deg, FULL 45 deg of command.
- **Inhibition / automatic retraction.** Inhibited while high-AoA protection is active or any TLA >= 35 deg
  (FLX/MCT or TOGA). The inhibition is latched (reset-dominant S/R) until the lever has been below 0.05
  (retracted, or armed) for 5 s: the speed brakes retract at 5 deg/s and stay retracted, with the lever out,
  until the crew retracts the lever. FBW models no flaps-FULL inhibition: FULL reduces the gains instead.
- **Panel gains** by PRIM flap index, faded at 0.03 /s, x the command (deg at FULL lever):

  | Spoilers | 0 / 1 | 1+F | 2 | 3 | FULL |
  | --- | --- | --- | --- | --- | --- |
  | 1-2 | 20 | 12 | 9 | 8 | 6 |
  | 3-5 | 20 | 17 | 9 | 3 | 0 |
  | 6-8 | 45 | 21 | 17 | 10 | 10 |

  Panel orders move at 40 deg/s. Roll spoiler terms are not part of this module (X-Plane's).
- **Ground spoilers armed** = `lever` < -0.02 (the lever pulled up at RET).
- **Extension condition** = ((`lever` > 0.05 or armed) and at least three TLA < 20 deg) or (TLA 2 or 3 < 0,
  reverse selected on an inner engine, and at least three TLA <= 35 deg). So they extend when armed, when the
  lever was left extended, or with reverse selected though not armed.
- **Extension** (full, 50 deg at 17 deg/s) when the condition holds and the wheels spin up: wheel speed >= 72 kt
  on both sides (left body or wing gear, and right body or wing gear) while the wheel latch is set; then held
  as long as the condition holds. The wheel latch is set by the touchdown pulse or by the wheels < 23 kt on
  both sides, and reset by the lift-off pulse. A rejected take-off therefore extends them above 72 kt wheel
  speed (the latch was set at the standstill before the roll), not below.
- **Retraction**: the condition lost: the lever disarmed (pushed down to RET) without reverse, or two thrust
  levers advanced to 20 deg or more (and no inner reverse with three levers at or below MCT). 17 deg/s.
- **Partial extension (phased lift dumping)**: present in FBW's model (15 deg at 5 deg/s, for armed with
  TLA 1 and 2 <= 25 deg, or the condition), but both of its trigger pulses are wired to constant `false`
  (`PulseNode4` / `PulseNode5` of a `false` input), so it never activates. The core keeps that structure and
  behaves as FBW: no partial extension.
- **Spoiler lift**: on the ground with an SFCC position word showing the flaps below 1+F (bit 20 clear), every
  speed brake panel order is at least 5 deg (5 deg/s). FBW has no comment on it; it is reproduced as is.

## Lever model and the X-Plane mechanism

The handle is continuous 0 (RET) .. 1 (FULL) with the 1/2 notch, plus ARM (pulled up at RET).

- **Push down to move.** The real lever must be pushed down before it moves. An OBJ8 drag manipulator has no
  push-down dimension: the lift of `ATTR_manip_drag_rotate` + `ATTR_axis_detent_range` (used for the flaps
  pull-out) is "always away from the rotation center" (OBJ8 specification), the opposite direction. Holding
  the lever with the mouse is therefore the push: while a drag is in progress the handle is shown pushed
  down 5 mm (`lift_anim` -1) and only then does it follow the hand; released, it springs back up and stays
  where it is (it can rest anywhere between RET and FULL). Nothing else moves it out of RET: commands and
  hardware are complete lever actions.
- **1/2 notch.** Sticky detent of the shared `a380x_detent_lever.h`: the hand within 0.03 of 1/2 captures the
  lever, which stays exactly at 1/2 until the hand is 0.08 away; a deliberate fast push passes it. Released
  within 0.03 of RET, the lever seats in RET.
- **ARM.** One click-hold-drag along the lever arc (`lever_input` -0.25 .. 1) continues 0.25 beyond RET toward
  ARM, as FBW's single drag reaches ARM past RET. A drag that starts at RET and goes 0.15 beyond RET pulls the
  lever up (it rises with the hand) and latches ARM; a drag that arrives at RET from 1/2 stops there (pull up
  needs a new action at RET). An armed lever dragged aft by more than 0.02 is pushed down out of ARM
  (disarmed, FBW `SPOILERS_ARM_OFF` on drag) and moves on.
- **Sound.** The thrust detent click (`thrust_detent_click.wav`, no new sound) on the 1/2 notch entry and on
  ARM latch / release, at the lever handle, one per frame (FBW plays `lever_speedbrakes`, not available).
- **Animation.** `lever_anim` follows the hand while dragged and travels at 4 handle units/s after a command,
  hardware change or seat; `lift_anim` moves at 8 /s (-1 pushed, 0 normal, +1 ARM, in between while
  pulling).

**Commands** (`a380x/xp12/speedbrake/`): `extend_one` / `retract_one` (ARM <-> RET <-> 1/2 <-> FULL, from between
positions to the next one; retracting from RET arms, as X-Plane does), `arm_toggle`, `arm` (RET + ARM),
`retract` (RET, disarmed). X-Plane's `sim/flight_controls/speed_brakes_down_one` (extend one),
`speed_brakes_up_one` (retract one; RET -> ARM), `speed_brakes_down_all` (FULL, disarmed: FBW SPOILERS_ON),
`speed_brakes_up_all` (RET, disarmed: SPOILERS_OFF), `speed_brakes_toggle` (FBW: disarmed, RET <-> FULL) and
`speed_brakes_arm_toggle` (FBW ARM_TOGGLE) are intercepted and consumed while the plugin runs.

**Hardware axis / other writers.** A value of X-Plane's handle the plugin did not write is a lever action:
< -0.25 (X-Plane's ARMED -0.5) arms; otherwise the handle position (within 0.02 of RET or 1/2 that position,
changes below 0.005 ignored; a position above RET disarms, 0 keeps ARM, as FBW's axis event). It is never
written back as such; the next frame X-Plane's handle carries the surface order again.

## Speed brake path: before and now

**Before.** `sim/cockpit2/controls/speedbrake_ratio` was the lever (X-Plane's handle, -0.5 armed). X-Plane's
flight model deflected its three groups (`speedbrake1` = FBW panels 1-2, `spoiler1` = 3-6, `spoiler2` = 7-8,
docs/CONTROL_ANIMATION_NOTES.md) from it per the ACF: 45 deg x handle in flight (`acf/_sbrk1_up_flit`,
`_spoi1/2_with_flit_sbrk`), 50 / 50 / 45 deg on the ground (`_sbrk1_up_grnd`, `_spoi1/2_with_grnd_sbrk`), plus
roll. The ground spoilers were X-Plane's: armed (-0.5) and `acf/_sbrk_on_td_EQ 1` deployed them at touchdown,
by X-Plane's own rules. The FlyWithLua driver `A380X_XP12_Hydraulics.lua` copies X-Plane's per-wing
`sim/flightmodel2/wing/speedbrake1_deg` / `spoiler1_deg` / `spoiler2_deg` (with hydraulic hold) into
`a380x/xp12/hydraulics/spoiler_*_deg`, which drive the exterior panels and the SD F/CTL page. Nothing native
computed speed brake or ground spoiler logic; the cockpit lever was animated on X-Plane's handle (ARM pose at
0) and could not be clicked.

**Now.** The plugin owns the lever; X-Plane's handle carries FBW's surface order:
`speedbrake_ratio` = mean of the eight FBW panel orders / 45 deg in flight, / 50 deg on the ground, 0..1,
never -0.5. X-Plane's flight model, the Lua exterior driver and the SD follow it unchanged. Full lever, clean,
in flight: 0.653 (29.4 deg on every X-Plane group, FBW 20 / 20 / 45 deg); ground spoilers: 1.0 (X-Plane's
ground schedule). X-Plane's own touchdown auto-deployment never acts (the handle is never ARMED) and is also
switched off while the plugin drives (`sim/aircraft/specialcontrols/acf_autosbrkEQ` 0, the value restored on
disable / reload). No ACF change.

Plugin inputs: wheel speed = X-Plane tire rotation x tire radius of the four main gears (left / right by the
sign of `acf_gear_xnodef`, body = inboard, wing = outboard; this ACF: gear 1/2 wing, 3/4 body); TLA from the
thrust module (`a380x/xp12/thrust/tla_N`), else X-Plane's throttle (45 deg x ratio, reverse -6..-20 deg);
on-ground `onground_any`; flap index and the 1+F bit from the FLAPS module feed; high-AoA protection = A.FLOOR
latched (the port has no PRIM protection flag).

## Consumers

| Consumer | Now reads |
| --- | --- |
| FWS (`a380x_fws_adapter.inc`) | `gnd_splr_armed` = lever ARM, `speedbrake_lever_out` = handle > 0.05 (the port's documented threshold, docs/FWS_SIGNALS.md; FBW's FCDC bit 28 uses > 0.9) from the module; X-Plane's handle only without it. Memos GND SPLRS ARMED, T.O / LDG memos, T.O CONFIG speed brakes, SPEED BRAKES STILL EXTENDED, GND SPLRS NOT ARMED unchanged in the FWS core |
| Native PFD (`a380x_pfd_inputs.inc`) | armed triangle from the lever, speed brake symbol and FMS VLS increment from the extension fraction (max of command / 45 and ground spoiler / 50) |
| xp-kit PFD (FBW's own) | `L:A32NX_SPOILERS_ARMED`, `L:A32NX_SPOILERS_HANDLE_POSITION`, `L:A32NX_LEFT_SPOILER_1_COMMANDED_POSITION` (panel 1 order, deg) |
| flyPad checklist GND SPLRS ARMED (`a380x_efb.c`) | `a380x/xp12/speedbrake/ground_spoilers_armed` (looked up lazily), X-Plane's handle without it |
| flyPad presets "Spoiler Arm / Disarm" (`a380x_elec_adapter.inc`) | read `ground_spoilers_armed`, act with the `arm` / `retract` commands; X-Plane's handle without the module |
| SD F/CTL (`a380x_sd_data.inc`) | unchanged: `a380x/xp12/hydraulics/spoiler_*_deg`, which now follow FBW's order through X-Plane's surfaces. FBW's SD F/CTL page has no GND SPLRS / speed brake text |
| Autobrake ACTIV (`a380x_center_right.inc`) | unchanged: X-Plane's actual `sbrkrat` (surfaces deployed) |
| Thrust | unchanged; its TLA (reverse negative) drives "reverse selected" |

Still naming X-Plane's handle as the lever, not runtime: `tools/sound_port_map.py` / `fmod/a380x-sound-port-map.json`
(sound planning; FBW's Spoiler0/50/100/ARM/DISARM events) and the uninstalled `FlyWithLua/Scripts/A380X_XP12_EFB.lua`.

## Cockpit geometry

**What the export had.** `LEVER_SPEEDBRAKE` (1092 indices at 38046, material PEDESTAL_MISC01) in
`cockpit_a380_cockpit_pedestal_misc01_4k.obj`, node-local vertices under the clip's 20 translation + 20
rotation keys on X-Plane's `speedbrake_ratio` 0..1 (so the lever showed the ARM pose at 0 and the stem drew
the clip's dwell at RET). The clip: key 0 = ARM (the node rest pose, 13.71 mm up along the lever), keys 1..19
down; RET angle 13.06 deg about +X held to key 9, then RET -> FULL 24.68 deg (-11.62 deg at key 19); 1/2 is the
angular midpoint, key 14 (0.79 deg). FBW's mapping of O:Position 0..3 onto the clip is not in the pinned
sources; the port uses the angular midpoint for 1/2.

The `SPD_LVR_*` rig is the skin of `Plane.083` / `Plane.084`, the two brush strips lining the slot (140
triangles each, a one-based ASOBO joint palette, every vertex on one joint): 17 inner-edge joints per side
move 2-5 mm outward / up as the stem passes, joints 3-5 close behind it when the lever leaves RET, the side
and end joints stay. OBJ8 has no skinning: the export wrote the 26 joints as empty ANIM blocks (left alone,
they draw nothing) and the strips as 280 static triangles in their mesh space, SPEED_BRAKE_PANEL's frame (the
rig parent SPD_LVR cancels that node's translation), i.e. 32 m from the cockpit near the object origin. The
slot had no brush.

**Staged** (`tools/speedbrake_lever_cockpit.py`; markers `# A380X SPEED BRAKE LEVER V1 ...`):

| OBJ | Change |
| --- | --- |
| `cockpit_a380_cockpit_pedestal_misc01_4k.obj` | The lever block re-bound: static translation to the normal (RET) height, rotation on `lever_anim` (11 keys = clip keys 9..19, absolute angles), translation on `lift_anim` in the lever's frame (-1: 5 mm down, 0, +1: the clip's 13.71 mm ARM pull-up). The 280 brush triangles: the static SPEED_BRAKE_PANEL translation (0.1467, 2.3889, 32.1719) puts them in the slot (each joint's vertices then lie within 2.2 mm of the joint), and each triangle follows the mean of its three vertices' clip translations relative to RET (11 keys on `lever_anim`): 104 motion groups, 167 blocks in the original draw order, at most 4.6 mm, a rigid approximation of the skin (gaps up to about 3 mm between neighbours, at the stem). VT/IDX/UV/texture payload, every other line (thrust and flaps patches included) and the drawn index ranges unchanged (checked). The lever's vertices are verified against the glTF (1e-5 m) and the exported keys against the clip. |
| `a380x_cockpit_controls.obj` | Appended after `# A380X SPEED BRAKE LEVER V1`: a draw-disabled box around the handle (2 mm margin) animated with the lever and the lift, carrying `ATTR_manip_drag_rotate` about the lever pivot, axis -X, `lever_input` -0.25 .. 1, angles -6.08 .. 24.68 deg relative to RET (ARM zone extrapolated), `ATTR_manip_keyframe` at the clip's angles 0, 0.1 .. 0.9. No lift / detent ranges (see above). |

`config/animation_datarefs.json` maps `lever_speed_brake` to `a380x/xp12/speedbrake/lever_anim` over the clip's
RET..FULL range (source 9/19 .. 1), so a future export binds the same rotation; the lift and the brushes are
this tool's.

## Datarefs and commands (`a380x/xp12/speedbrake/`)

Writable: `lever_input` (-0.25 .. 1, manipulator). Read-only integers: `ground_spoilers_armed`,
`lever_position` (FBW O:Position: 0 ARM, 1 RET, 2 1/2, 3 FULL, -1 between), `lever_held`,
`speed_brake_inhibited`, `ground_spoilers_out`, `phased_lift_dumping`, `spoiler_lift_active`, `wheel_latch`.
Read-only floats: `handle_position` (FBW SPOILERS_HANDLE_POSITION), `lever_anim`, `lift_anim`,
`speed_brake_command_deg`, `ground_spoiler_command_deg`, `spoiler_lift_deg`, `spoiler_1_deg` ..
`spoiler_8_deg` (FBW panel orders, both wings), `extension_ratio`, `xp_handle_ratio`. Commands: `extend_one`,
`retract_one`, `arm_toggle`, `arm`, `retract`. The list is `SPEEDBRAKE_COMMANDS` / `SPEEDBRAKE_DATAREFS` in
`tools/avionics_provider.py`; `build.py` fails when the native registry differs.

## Tests, build and installation

`native/avionics/build.py` runs `test_speedbrake.c` (149 checks: push-down, notch capture / escape / fast pass,
RET seat, ARM only from RET, partial pull, disarm by pushing down, commands and FBW events, hardware snaps /
noise / ARMED, the lever table, 5 deg/s, inhibition latch and its 5 s reset, alpha protection, gains by flap
index, landing extension at 72 kt both sides, held, retraction by thrust and by disarming, unarmed reverse,
outer reverse ignored, lever left extended, RTO above / below 72 kt, the wheel latch at lift-off and the
touchdown pulse, inert phased lift dumping, spoiler lift, X-Plane mapping, the feed), its MinGW `-Werror`
compile check, and `test_speedbrake_adapter.c` (mock XPLM: registry, first sync, the drag with notch / ARM /
clicks, the six intercepted stock commands and the plugin commands, hardware adoption without fighting, the
surface order in X-Plane's handle and `acf_autosbrkEQ` off / restored, landing and reverse ground spoilers,
thrust-module and fallback TLA, A.FLOOR inhibition, FLAPS-feed gains and spoiler lift, pause, replay,
disable, reload, shutdown). Raw inputs are checked against `DataRefs.txt`, the intercepted commands against
`Commands.txt`; results in `build-report.json` (`speed_brake`). `tools/test_speedbrake_lever_cockpit.py` covers
the contracts, the animation mapping, the manipulator, the authored clip, the brush rig, the click target,
the real pedestal rebind (idempotent, thrust / flaps patches untouched) and the installer.

Install with X-Plane closed, in this order: geometry (`tools/install_speedbrake_lever.py --aircraft
output/A380X-XP12 --geometry-stage <stage>`; backs up to `build/backups/speedbrake-lever-<timestamp>`), then the
plugin (`tools/install_xp_kit_cockpit.py <build>`; it writes the new commands and datarefs into the provider
manifest, which the new animation and manipulator datarefs need), then `tools/validate_aircraft.py`.

## Unverified in the simulator

- The drag: X-Plane's mapping of the mouse to the arc on this short lever seen from above, the ARM zone
  (0.15 beyond RET = 3.6 deg of hand travel) and the notch feel; the hit box position.
- X-Plane's deflection for handle values below 1 on the ground (the mapping assumes linear 0..50 deg there,
  X-Plane documents 1..1.5 of `sbrkrat` as "extra deployment when on ground"), and that `acf_autosbrkEQ` is
  the touchdown auto-deployment switch.
- That a hardware speedbrake axis writes X-Plane's handle only when moved: an axis written every frame would
  hold the lever at the axis and overwrite the surface order (no automatic ground spoilers).
- X-Plane's tire rotation in the air (the wheel latch also sets on the touchdown pulse, so landings work
  either way) and its value at the moment of touchdown.
- The brush placement and motion (offline geometry checks only), the lever click sound level.
- The xp-kit PFD's use of the three new L: variables (passed through as numbers).

## Deviations from FBW and decisions for the user

- **High-AoA protection** is not computed in the port (X-Plane flies its own laws); A.FLOOR latched stands in
  for FBW's `high_alpha_prot_active` in the inhibition. TLA >= 35 deg is FBW's.
- **Surfaces through one X-Plane handle.** X-Plane has one speedbrake ratio for its three groups, so the FBW
  panel orders are applied as their mean (clean full lever: 29.4 deg everywhere instead of 20 / 20 / 45 deg;
  FULL flaps: about 5 deg instead of 6 / 0 / 10). The per-panel FBW orders are published
  (`spoiler_N_deg`) but the exterior and the SD show X-Plane's surfaces (with roll), as before. Mapping the
  panels individually would need the Lua exterior driver and X-Plane's surfaces to be driven separately.
- **Push-down** is the mouse hold (no OBJ8 push dimension) and its 5 mm travel is not authored by FBW.
- **1/2 position** at the clip's angular midpoint (key 14); FBW's MSFS template mapping is not in the sources.
- **Brushes**: a rigid per-triangle approximation of the skin (OBJ8 has no skinning). The mesh is drawn as
  modelled at RET; its inverse bind matrices describe a closed brush at joints 3-5, which the port does not
  apply (it would double the parting at RET).
- **FWS lever-out threshold** stays the port's 0.05 (FBW's FCDC bit 28 is lever > 0.9).
- **Spoiler lift** (5 deg on the ground below 1+F) is reproduced from FBW without a documented A380 basis;
  remove it in `a380x_spoilers_update` if it looks wrong in the sim.
- **Retract one at RET arms** (X-Plane's convention for `speed_brakes_up_one`); FBW has no such event.
- X-Plane's per-group ground maxima are 50 / 50 / 45 deg (ACF), FBW's ground spoilers 50 deg on every panel.
