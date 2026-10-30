# ELEC and EMER ELEC PWR overhead panel (phase 1)

Phase 1 makes every pushbutton and guard of the A380 ELEC panel and of the EMER ELEC PWR
panel a working cockpit control: native pb-switch states, lit legends, authored press travel,
guards that open about their authored hinges, pointing-finger / grabbing-hand cursors and FBW
click sounds.

**Phase 2 is active.** The native A380 electrical network ([ELEC_SYSTEM.md](ELEC_SYSTEM.md),
spec [ELEC_SYSTEM_SPEC.md](ELEC_SYSTEM_SPEC.md)) takes the panel over through the names below:
`external_logic` is 1, the pbs feed the network, and the FAULT conditions and the supply of every
legend come from it (spec 4.2). The phase-1 stock coupling described in this file applies only
when the network could not register (for example a plugin registration failure); then the panel
couples minimally to the stock X-Plane electrical system as before.

Sources: native module `native/avionics/a380x_elec_panel.h` (XPLM-free state machine) and
`a380x_elec_panel.inc` (X-Plane adapter), contract `tools/elec_panel_contract.py`, cockpit
patch `tools/elec_cockpit_panel.py`, sounds `tools/stage_fbw_elec_sounds.py`.

## Controls

All names start with `a380x/xp12/elec/`. `pb/<id>` is the pushbutton position dataref
(writable int), `pb/<id>_press_ratio` the 0..1 cap travel (float), `lamp/<id>_upper` /
`lamp/<id>_lower` the lit SEQ1 / SEQ2 legend (float 0..1: 0 dark, 1 bright, 0.3 DIM).
Latched pb-switches have `<id>_toggle` (held like the real button: press on begin, release on
end), `<id>_on` (pushed in) and `<id>_off` (released). Momentary buttons have `<id>_press`.

| Panel caption | id | FBW node | Kind | States (`pb/<id>`) | Upper lamp | Lower lamp | Phase-1 coupling |
| --- | --- | --- | --- | --- | --- | --- | --- |
| BAT 1 | `bat1` | PUSH_OVHD_ELEC_BAT1 | pb-sw | 1 AUTO / 0 OFF | FAULT amber | OFF white | `battery_on[0]` |
| ESS BAT | `bat_ess` | PUSH_OVHD_ELEC_BATESS | pb-sw | AUTO / OFF | FAULT | OFF | `battery_on[2]` if the ACF has 3 batteries, else state only |
| BAT 2 | `bat2` | PUSH_OVHD_ELEC_BAT2 | pb-sw | AUTO / OFF | FAULT | OFF | `battery_on[1]` if the ACF has 2 batteries |
| APU BAT | `bat_apu` | PUSH_OVHD_ELEC_BATAPU | pb-sw | AUTO / OFF | FAULT | OFF | `battery_on[3]` if the ACF has 4 batteries |
| ELMU | `elmu` | PUSH_OVHD_ELEC_ELMU | pb-sw | AUTO / OFF | (FAULT: `lamp/elmu_upper` published, no authored face) | OFF | state only |
| AC ESS FEED (guarded) | `ac_ess_feed` | PUSH_OVHD_ELEC_ACESSFEED | guarded pb-sw | 1 AUTO / 0 ALTN | FAULT | ALTN white | state only |
| PAX SYS | `pax_sys` | PUSH_OVHD_ELEC_GALYCAB (3rd position) | pb-sw | 1 ON / 0 OFF | — | OFF | state only |
| GALLEY | `galley` | PUSH_OVHD_ELEC_PAX (4th position) | pb-sw | AUTO / OFF | — | OFF | state only |
| COMMERCIAL 1, 2 | `comm1`, `comm2` | PUSH_OVHD_ELEC_COMMERCIAL, _COMMERCIAL2 | pb-sw | AUTO / OFF | — | OFF | state only |
| BUS TIE | `bus_tie` | PUSH_OVHD_ELEC_BUSTIE | pb-sw | AUTO / OFF | — | OFF | state only (no load shedding / tie logic) |
| EXT 1-4 | `ext1`..`ext4` | PUSH_OVHD_ELEC_EXTPWR, _EXTPWR2..4 | momentary | 1 ON / 0 not ON | AVAIL green | ON blue | ON of any → `GPU_generator_on` |
| APU GEN A, B | `apu_gen_a`, `apu_gen_b` | PUSH_OVHD_ELEC_APUGEN, _APUGEN2 | pb-sw | AUTO / OFF | FAULT | OFF | A or B → `APU_generator_on` |
| GEN 1-4 | `gen1`..`gen4` | PUSH_OVHD_ELEC_GEN1..4 | pb-sw | AUTO / OFF | FAULT | OFF | `generator_on[n-1]` (held off while DRIVE n is DISC) |
| DRIVE 1-4 (guarded) | `drive1`..`drive4` | PUSH_OVHD_ELEC_IDG1..4 | guarded momentary | `pb/driveN` read-only: 1 = disconnected (latched) | FAULT | DISC white | forces its generator off |
| RAT MAN ON (guarded) | `rat_man_on` | PUSH_OVHD_EMERELECPWR_MANON | guarded momentary | 1 = deployment requested (latched, writable) | — | — | state only (phase 2 deploys the RAT) |
| RAT & EMER GEN | `rat_fault` | PUSH_OVHD_EMERELECPWR_RAT | indicator, not pressable | — | FAULT amber | — | never lit in phase 1 (no source) |

The FBW model names the third top-row button GALYCAB and the fourth PAX; the mapping above
follows their position on the panel (+X of the source glTF is the crew's left).

Other names: `ext/connected` (writable int, ground power connected = all four AVAIL),
`ext/avail1..4`, `fail/<id>` (writable fault inputs for `bat1`, `bat_ess`, `bat2`, `bat_apu`,
`elmu`, `ac_ess_feed`, `apu_gen_a/b`, `gen1..4`, `drive1..4`, `rat_fault`), `cover/<id>` (guard
open ratio 0..1, writable float, ids `ac_ess_feed`, `drive1..4`, `rat_man_on`), `ann_lt`
(writable: 0 TEST, 1 BRT default, 2 DIM), `lamp_power`, `external_logic`, `stock_battery_slots`.
Commands also include `drive1..4_reset`, `drive_reset_all`, `gpu_connect_toggle` and per guard
`<id>_cover_toggle`, `<id>_cover_open`, `<id>_cover_close`. 84 commands and 130 datarefs in
total, all registered in the avionics provider manifest.

### Behaviour

* Latched pb-switches: a press toggles the state; the cap travels in and returns (Airbus
  pb-switches look the same in and out), the legend shows the state. Holding the button holds
  the cap in; a one-frame click still shows an 80 ms press.
* Guarded buttons react only while their guard is at least 60 % open (native check and the
  click target is hidden below that). A guard closing over a held button releases it.
* DRIVE: a press disconnects the IDG; DISC stays latched and pressing again does nothing.
  `driveN_reset` / `drive_reset_all` reconnect it only on the ground with that engine stopped
  (ground maintenance). The disconnected IDG keeps `generator_on[n-1]` at 0 even if a stock
  command turns it on.
* EXT n: when external power is connected AVAIL (green) lights on all four; a press makes that
  receptacle ON (blue) and the stock GPU supplies the aircraft; pressing again turns it off.
  X-Plane exposes no "GPU cart connected" state, so connection comes from the flyPad /
  ground-services request (`sim/ground_ops/toggle_gpu_request` is observed: first request
  connects, the next removes), from `gpu_connect_toggle`, from writing `ext/connected`, or
  from the stock GPU being switched on elsewhere (then EXT 1 shows ON). Ground power
  disconnects when airborne or above 2 m/s ground speed.
* Hardware and presets: the adapter observes the stock switches every frame. A change made by
  `sim/electrical/battery_1_toggle`, `generator_n_on/off`, `APU_generator_on/off`, `GPU_on/off`
  or the flyPad presets is adopted by the matching pushbutton.
* FAULT lamps use only real failures: BAT n ← `sim/operation/failures/rel_batterN` (slots that
  exist), GEN n ← `rel_genera(n-1)`, plus the writable `fail/<id>` inputs. APU GEN, AC ESS FEED,
  ELMU, DRIVE and RAT & EMER GEN have no X-Plane failure source and stay dark unless phase 2
  or `fail/<id>` drives them.
* Lamps need annunciator power. With the network (phase 2) every legend has its own bus
  (SEQ1 / SEQ2 supply in `A380XElecInputs.lamp_powered`, e.g. GEN 1/2 on DC ESS, GEN 3/4 on DC 2,
  EXT AVAIL on ground power or DC 1/DC 2, COMMERCIAL 1 on DC 1 + DC ESS); in phase-1 fallback any
  stock `bus_volts` ≥ 18 V. ANN LT TEST lights every authored legend whose bus is powered; DIM
  uses 0.3. The INT LT ANN LT switch now drives it (clickable, `a380x/xp12/ovhd/ann_lt_up/_down`);
  `ann_lt` and `a380x/xp12/ovhd/ann_lt` are the same state. On batteries only DC ESS (BAT 1 **and**
  ESS) powers legends: BAT 2 / APU BAT OFF are on DC 2 and stay dark until AC exists (FBW). The
  opt-in `a380x/xp12/ovhd/settings/lamp_supply = 1` (not FBW) powers every legend whenever any DC
  bus is live or a battery pb is AUTO. All other overhead legends: [OVERHEAD_LAMPS.md](OVERHEAD_LAMPS.md).
* Aircraft (re)load: guards close, IDGs reconnect, latches return to AUTO and the stock states
  are adopted (cold ACF: BAT 1 OFF, others as X-Plane reports them).

The ACF currently has one native battery, so only BAT 1 drives stock power in phase 1; BAT 2,
ESS BAT and APU BAT are panel states (`stock_battery_slots` reports the count).

## Cockpit geometry

`tools/elec_cockpit_panel.py` patched the installed objects in place
(report `build/reports/elec-panel-20261021-first-run.json`, `elec-panel-20261021.json`):

* Press travel: 20 pushbutton meshes in `panel_a380_cockpit_ovhd_knobs_4k.obj` and their 39 legend
  decals in `decal_a380_pushbutton_decal.obj` got the authored FBW translation
  (1.49 mm into the panel, world (0, 1.36, 0.63) mm; RAT MAN ON 2.56 mm). The six legacy BAT 1/2,
  GEN 1-4 blocks that followed the latched stock switch were retargeted to `pb/<id>_press_ratio`
  (also the empty blocks in `decal_a380x_cockpit_decal_emis.obj`).
* Guards rotate about the authored FBW clip axes: DRIVE 1-4 and AC ESS FEED about world +X at
  their aft hinge, 80.86° (pivots: DRIVE1 (0.1141, 3.4408, 32.2934), DRIVE2 (0.0579, 3.4407,
  32.2935), DRIVE3 (-0.0537, 3.4408, 32.2933), DRIVE4 (-0.1089, 3.4408, 32.2934), AC ESS FEED
  (-0.0357, 3.4717, 32.2262)); RAT MAN ON round guard about (0.991, -0.121, -0.055), 61.43°,
  pivot (0.1927, 3.4476, 32.2652). Opening swings the free edge down toward the crew.
* `a380x_cockpit_controls.obj`: 45 lamp quads on the authored SEQ faces (lifted 0.4 mm,
  ATTR_light_level by the lamp dataref, hidden when dark, moving with the cap), 20 draw-disabled
  click boxes fitted to the caps (guarded ones hidden below 60 % guard), 6 guard click boxes
  rotating with the guards; the 6 legacy BAT/GEN click meshes now call the native commands.
* Legend cells: FAULT and OFF reuse the ADIRS cells 47/48 of `cockpit_3d/fcu_labels*.png`;
  ALTN (59), DISC (60), AVAIL (61, green) and ON (62, blue) were drawn into the previously
  empty last row (all other atlas pixels unchanged; `tools/build_fcu_panel.py` regenerates
  the same cells).
* Cursors: pushbuttons use `button` (pointing finger). Guards use `hand`: a click opens a closed
  guard fully or closes an open one; holding the mouse button and dragging down/up swings the
  guard with the hand (140 px = full travel) and on release it flips open past half travel or
  falls closed. This is a command manipulator with plugin-side mouse tracking because X-Plane
  drag manipulators do not report a plain click; in VR the click toggles.

Offline previews: `docs/control-anim-previews/elec-panel-rest.png` and
`elec-panel-guards-open-pressed-ann-test.png` (`tools/preview_elec_panel.py`).

## Sounds

`plugins/A380XAvionics/sounds/elec_*.pcm` (raw PCM16 mono 44.1 kHz) are played on the interior
bus, positioned at the panel (0.53 m right, 0.26 m up, 0.26 m forward of the captain's default
eye point, rotated with the aircraft; unplaced if the camera is not near the panel): press on
command begin, release on command end (two variants each, alternating), guard open when it
leaves the closed detent, guard close when it seats.

| File | Wwise event ID | Random container | Media ID | Role |
| --- | --- | --- | --- | --- |
| elec_pb_press_a/b.pcm | 3703968208 | 248233517 | 136777599 / 632899204 | press (cap travel + latch) |
| elec_pb_release_a/b.pcm | 1674450798 | 492115419 | 578881204 / 1000562486 | release |
| elec_guard_open.pcm / elec_guard_close.pcm | 2073564337 | 897668303 | 897566525 / 976977767 | guard snap |

All from `FBW_A320_NEO_1.PC.PCK` of the FlyByWire A380X package (hashes and levels in
`native/avionics/sounds/elec_sounds_provenance.json`). The optimized banks carry no event names
and the FNV IDs of the names referenced by the FBW cockpit (`pb1on`, `pb1off`, `mpb1on`,
`firepbguard`, ...) do not occur in them, so these were selected from the event graph and the
acoustic profile (double-transient ~90 ms pushbutton pairs; single-impact ~0.2 s snap for the
guard). The samples are decoded only (no synthesis, edit or normalisation). Licence: taken from
the GPL-3.0 FlyByWire repository, which has no separate sound-asset licence; confirm
redistribution rights for the audio with FlyByWire before public distribution (as the README
already notes for FBW sounds).

## Phase 2 hooks (now used by a380x_elec_adapter.inc)

Implemented 2026-10-21: the adapter sets `external_logic`, reads the pb states, writes
`elec_io.network_fault[]` (GEN FAULT = line contactor open with pb ON, AC ESS FEED FAULT = AC EMER
unpowered, RAT & EMER GEN FAULT in the emergency gap, BAT / DRIVE / APU GEN / ELMU from failures)
and `elec_io.lamp_powered[][]` (per-legend supply). The ground-power flag `ext/connected` and the
X-Plane ground-service request are shared by both phases; with the network the stock battery,
generator, APU generator, GPU and avionics commands are intercepted and act on these pbs, and the
GPU is removed when airborne or rolling (> 1 m/s). flyPad presets use authority writes (no sound).

* Set `external_logic` to 1 (or, in-plugin, keep `elec_io.external_logic`): the adapter then
  never writes or reads the stock battery/generator/APU-generator/GPU switches, failures or bus
  volts; lamp power comes from `lamp_power`, ext availability from `ext/connected`, faults from
  `fail/<id>`.
* Read the crew controls from `pb/<id>` (or `A380XElecPanel.normal[]`, `.ext_on[]`, `.drive_disc[]`,
  `.rat_requested` in `a380x_elec_panel.h`); feed `A380XElecInputs` (`lamp_power`,
  `annunciator_test/dim`, `fault[]`, `ext_available[]`, `on_ground`, `engine_running[]`) from the
  network; lamps then follow `a380x_elec_lamp()`. Table 4.2 of the spec (e.g. GEN FAULT = line
  contactor open with pb ON, AC ESS FEED FAULT = AC EMER unpowered) belongs in those inputs.
* `pb/<id>` writes are authority writes (no guard, no animation) for presets / cold & dark;
  `<id>_on/_off` commands animate and respect the guards.
* `pb/rat_man_on` is the latched RAT deployment request; `drive<n>_reset` is the ground
  maintenance reset.
* Not on this panel's FBW model: EMER GEN TEST, GEN 1 LINE and the BAT selector/voltmeter.

## Verification

Build (`native/avionics/build.py`) runs `test_elec_panel.c` (latches, guards, DRIVE latch and
maintenance reset, EXT, RAT, lamps, travel, ANN LT TEST) and `test_elec_panel_adapter.c`
(mock XPLM: registry equals `tools/elec_panel_contract.py`, stock coupling and adoption of
hardware/preset changes, ground-service observation, guard click and mouse drag, sound
placement and positioning maths, external logic, reload, disable, re-registration and failure
cleanup), and checks the registry and sound set against the contract.
`tools/test_elec_cockpit_panel.py` covers the contract, synthetic patch idempotency, legend
cells, the cursor classification and the installed objects. No in-simulator test was run.

2026-10-22 (X-Plane 12.4.4): the legends were never drawn, being alpha-blended and painted over by
the later-drawn legend decal object; they are now alpha-tested opaque (`tools/patch_lamp_depth.py`,
see docs/OVERHEAD_LAMPS.md).

### In-simulator checklist

1. Load cold & dark: ELEC panel dark. Press BAT 1: click sounds at the overhead, cap moves,
   stock battery on, OFF legends of buttons that are OFF light white.
2. Hover any pushbutton: pointing finger; hover a DRIVE / AC ESS FEED / RAT MAN ON guard: hand.
   Click a guard: it swings open; click again: closes. Hold and drag down: it follows.
3. With a guard closed the button under it cannot be pressed; open it, press DRIVE 1: DISC white,
   GEN 1 stock generator off; `drive1_reset` on ground restores it.
4. Request ground power from the flyPad: EXT 1-4 AVAIL green; press EXT 2: ON blue, aircraft
   on ground power; press again: AVAIL.
5. Move the ANN LT switch to TEST (or set `a380x/xp12/elec/ann_lt` to 0): every legend lights; check text reads upright and colours
   (amber FAULT, white OFF/ALTN/DISC, green AVAIL, blue ON).
6. Bind a joystick button to `sim/electrical/battery_1_toggle`: BAT 1 follows.
