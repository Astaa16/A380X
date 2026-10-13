# A380X to X-Plane 12 port workspace

This directory is an independent A380X conversion workbench using the downloaded FlyByWire source and MSFS glTF assets. No third-party X-Plane A380 is used as a carrier or dependency.

## Complete X-Plane aircraft

The complete installable aircraft is in [`output/A380X-XP12/`](output/A380X-XP12/). Models, textures, liveries, and runtime plugins are stored with Git LFS because the package is approximately 2.6 GB and contains files larger than GitHub's normal 100 MiB limit.

Clone it with Git LFS enabled:

```sh
git lfs install
git clone https://github.com/Sava227/A380X.git
```

Then copy the resulting `A380X/output/A380X-XP12` folder into X-Plane 12's `Aircraft` directory. A browser-generated source archive may contain LFS pointer files instead of the actual aircraft assets, so cloning with Git LFS is the reliable installation method.

## Landing-gear repair: start here

The editable X-Plane aircraft files are deliberately collected in [`acf/`](acf/README.md) so a contributor can work on the landing gear without downloading the multi-gigabyte generated package.

- **Aircraft's current file to fix:** [`output/A380X-XP12/A380X-XP12.acf`](output/A380X-XP12/A380X-XP12.acf)
- **Small standalone copy:** [`acf/A380X-XP12-current.acf`](acf/A380X-XP12-current.acf)
- **Before the nose-wheel contact fix:** [`acf/acf-before-nosegear-contact-fix-20261005.acf`](acf/acf-before-nosegear-contact-fix-20261005.acf)
- **Before ground calibration:** [`acf/acf-before-ground-calibration.acf`](acf/acf-before-ground-calibration.acf)
- **Known defect, evidence, acceptance criteria, and next telemetry:** [`docs/GROUND_TEST_NOTES.md`](docs/GROUND_TEST_NOTES.md)
- **Repeatable diagnostic:** [`tools/ground_gear_diagnostics.py`](tools/ground_gear_diagnostics.py)

The remaining issue is not just door animation: the current aircraft still needs a proper five-leg load/stance validation, including pitch and roll within one degree, positive settled nose-wheel force, consistent rendered tire contact, and later taxi/takeoff/landing tests. Use a fresh X-Plane aircraft load after each ACF edit because an in-process reload can retain cached gear geometry.

## What is ready

- Pressure-aware exterior flight controls now settle on the ground: six ailerons and four elevators droop gradually, rudders move slowly with local wind, and spoilers hold without pressure. In-flight engine loss does not select the parked pose. See `docs/HYDRAULIC_SURFACES.md` for the native-pressure inputs, packaged runtime and limits of this visual adapter.

- The engine visual pass restores native fan/spinner rotation on all four engines and authored thrust-reverser motion only on engines 2 and 3. Engine materials now use corrected tangent normals, enabled metallic response and full authored gloss, with the original 4K paint and Thai livery artwork preserved. Focused geometry, independence and package checks plus offline previews are recorded under `build/engine-update-20261006`; reload in X-Plane for native appearance acceptance. See `docs/ENGINE_VISUALS.md`.
- Upstream source is pinned at `2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2`.
- Large-file assets are pinned at `5f07408bfec09c66dd6b5c5e484027f64664fb43`.
- The A380-only large-file checkout is complete and clean on the SD card. It contains 22 glTF files with 1,264 animations and 3,142 animation channels; see `output/A380X-XP12/large-assets-animation-inventory.json`.
- `blender/inventory_gltf.py` inventories glTF animation channels without Blender.
- `blender/convert_gltf_animations.py` imports glTF animations, attaches current XPlane2Blender dataref properties/keyframes, marks export collections as X-Plane 12 aircraft parts, saves a `.blend`, and writes a review report.
- `FlyWithLua/Scripts/A380X_XP12_FBW.lua` publishes custom A380X datarefs, ports the FBW confirmation/memory logic pattern and flight-phase structure, and supplies an experimental G-command/roll-rate control law.
- `tools/build_development_aircraft.py` produces an independent A388 `.acf` package using an openly licensed ACF/airfoil scaffold with every original visual and cockpit attachment removed.
- The first FMOD integration slice defines eleven guarded X-Plane attachments for four engines, wind, flap motion, and five tires. The FBW Wwise banks were parsed non-destructively; 22 RTPC-linked Trent event graphs and 90 unique engine samples are staged as verified loop-tagged WAV authoring sources with hashes, loop points, and a compact audition index. A compiled FMOD bank and a sound-specific redistribution-rights decision are still required before custom sound is enabled or published.
- Blender 4.5 LTS and XPlane2Blender are installed on the external `MacGames` build volume. The converted exterior currently includes five fuselage texture regions, windows/accessories, four wing regions, pylons, fans, and left/right engine shells and cores.
- Generated aircraft assets live at `output/A380X-XP12` on the SD card. Heavy Blender exports, textures and test recordings live on the external `MacGames` volume. The old `runtime-smoke-test.json` records an earlier partial exterior build and is not evidence that the current cockpit, flight model or avionics have passed acceptance tests.
- The October 4 repair pipeline adds a textured cockpit shell, detailed MIP/FCU/overhead/pedestal geometry and labels, a cabin shell, the separate FlyByWire passenger-seat/suite model, and all five landing-gear assemblies. It separates multi-material mesh faces before OBJ export; earlier builds overlaid entire meshes using mismatched textures.
- MSFS primitive index ranges/base-vertex offsets are expanded before import. Source winding is checked against normals and converted to standard glTF winding, then XPlane2Blender emits clockwise OBJ8 faces. The validator now detects reversed-face regressions. These fixes address hollow-looking surfaces and the malformed cockpit display meshes, not avionics behavior.
- The generic 2-D jet artwork is replaced with a transparent custom panel. This removal was verified in X-Plane's forward view. The current Mac's keyboard profile uses **Shift+0** for 3-D cockpit command-look, **E/Q** to pan right/left, and **R/F** to tilt up/down. Shift+9 is bound to wingman view on this profile; do not use it as cockpit guidance. The right-arrow key translates the head rather than rotating it.
- `tools/build_local_mac.sh` packages the latest local exports. `tools/export_exterior_mac.sh` rebuilds the exterior and gear. Focused `--refresh-group cockpit-controls` builds now preflight only their control/panel inputs and preserve unselected ACF attachments, so a temporarily missing unrelated cabin texture cannot block a display refresh. `tools/validate_aircraft.py` checks package-local texture references, native animation datarefs, geometry/index structure, and the finished A388 package's 1024x1024 panel, aircraft-local avionics provider, bilateral PFD/ND/flyPad targets, independent pilot/copilot raw-data channels, cockpit-region UV quads, and the two MFD cockpit-device screens and touch quads. Blender launchers now return failure when a Python export fails.
- Inverted elevator-trim mappings are normalized into low-to-high OBJ8 key tables after export, without changing paired transform values. The validator rejects descending keys. The offline preview evaluates nested animations in a neutral gear-down pose and culls backfaces, instead of silently displaying raw bone-local coordinates.
- The current October 4 model package has 104 OBJ attachments, 8,451,401 vertices and 10,003,972 triangles, including the shared cockpit/cabin-control attachment, sixteen detailed cabin attachments, and six passenger-seat/screen attachments. The installed door-assembly checks pass with no errors; whole-aircraft validation must also be rerun after the separate FCU/provider integration. The passenger-seat group contributes 2,551,508 vertices and 3,324,386 triangles. The upper-deck doorway repair includes 13,296 fixed jamb/header triangles, 396 curved wall-lining triangles, and 38,262 moving inner-panel triangles across all six openings. Identical vertex reuse previously reduced the 79-object model files from approximately 1.1 GB to 443 MB; the restored cockpit walls, seats, detailed cabin, and upper-deck door groups add geometry. The animation-aware offline cockpit render confirms visible front panels/labels and correctly bounded dark screens; it is not an X-Plane runtime screenshot or avionics test.
- The horizontal-tail v3 repair corrects the source's one-based joint indices, replacing the previous incorrect right-palette permutation. Four complete elevator panels now follow pitch instead of only small end pieces; fixed stabilizer skins follow trim only. Independent offline checks reduce the former 1.067 m separation to 0.00312 mm while preserving all 445,150 fuselage triangles/UV associations and unrelated rudder/gear movement. The elevator visuals still use the live pilot pitch command as a fallback, not independent FBW actuators or aerodynamic feedback; see `docs/CONTROL_ANIMATION_NOTES.md`.
- The local exterior wrapper enables `--repair-fbw-tail-bindshape` and `--rigid-fbw-tail-panels`. These are restricted to ten meshes in two audited skins, retain the verified neutral geometry, and preserve the original joint/inverse-bind palettes and upstream assets. Stabilizer trim now uses 21 measured source angles in X-Plane's positive-leading-edge-up convention, rather than the previous reversed guessed ±10-degree scale. ACF limits and flight physics are unchanged. The offline-verified update is installed on the SD card with backed-up originals; native movement verification remains required.
- The subsequent wing export evaluates the selected static wing-flex pose before separating rigid and blended faces. Previously those pieces used different neutral poses: 2,153 checked cross-draw locations had gaps larger than 1 cm, with a verified shared source boundary separated by 0.527 m. They now coincide within 0.064 mm. All 739,929 wing triangles, per-corner UVs, partitions and animation mappings are preserved. `tools/export_exterior_mac.sh` enables `--bake-static-skin-pose` for wings only. This repairs neutral seams, not deployed flap/slat skinning or live wing flex.
- Six omitted opaque FBW cockpit wall, window-structure and sideboard materials add 260,386 source triangles and 18 checked textures. `tools/prepare_cockpit_wall_restoration.py` guards source coverage and updates the panel manifest without changing existing variants; `tools/rebuild_interior_geometry.py` then includes them in future exports. The right-looking offline preview verifies their presence alongside the existing FCU, overhead and main panel. Both this restoration and the latest neutral-wing export await another native visual check.
- Twenty additional opaque FBW cockpit detail materials restore the flight-deck seats, seat belts, leather, keyboards, microphones, rudder cables and aft fittings. `tools/prepare_cockpit_detail_restoration.py` checks the 1,066,684 source triangles and prevents overlap with existing export groups. The refreshed panel export contains 46 variants alongside seven existing decal/screen objects; the installed aircraft passes whole-package static validation, including all OBJ texture paths. These surfaces still need visual inspection in X-Plane after reloading the aircraft.
- The later flap/aileron repair decodes the two optimized wing skins' one-based joint palette, correcting control surfaces attached to neighbouring bones. All six main flap panels now move as complete rigid skins: 13,524 triangles follow flap deployment, including 8,476 previously frozen blended triangles. The actual exported paths stay within 32 mm of the corrected source skinning at all 41 authored flap poses, with neutral error below 0.04 mm. All six aileron directions were checked; right roll lowers the left trailing edges and raises the right. This remains an offline-verified, bounded approximation, not exact skinning or a native flight-test pass. See `docs/CONTROL_ANIMATION_NOTES.md` for evidence and remaining droop/spoiler limitations.
- Both authored PFD and ND pairs are now owned by the aircraft-local `A380XAvionics` plugin. Its simulator-independent C core translates the pinned FlyByWire A380 FMA message priority and FWS flight-phase state machine. X-Plane supplies raw pilot/copilot sensor and failure inputs plus the physical EFIS mode and range selector positions. No stock X-Plane moving map, flight-director, VNAV, GPSS or autopilot-mode status drives these displays. The NDs now show side-specific ROSE, ARC, and PLAN compass presentations, while the FMS map remains partial until the FBW FMGC, FMS and navigation database are ported; see `docs/AVIONICS_DEVELOPMENT.md`.
- The native `A380XAvionics` plugin draws its existing ten-page control panel on both cockpit tablets and a new eight-tab A380 ISCS floating window opened from the top-level **ISCS** menu. The popup follows the supplied teal/cyan layout without ToLiss branding or the `1:1` marker. The two tablets retain upright screen UVs and 34 tap-style touch targets. The plugin uses A380-842 reference limits of 510,000 kg MTOW, 395,000 kg MLW, 373,000 kg MZFW, and 323,546 kg maximum fuel. Payload, nine-tank fuel loading, ground services, pushback, selected failures, and all 17 modeled passenger/cockpit doors have native controls. Static cargo doors and unsupported external services are identified as unavailable; see `docs/EFB_DEVELOPMENT.md`.
- The aircraft-local native plugin now also drives the physical glareshield side panels: independent CHRONO timers, priority and master alert indications, conditional AUTOLAND warning, ATC MSG timing, speaker volume, and radio PTT routing. The side panels' missing upstream system inputs and live-test limits are recorded in `docs/GLARESHIELD_SIDE_PANELS.md`.
- The former `FlyWithLua/Scripts/A380X_XP12_Autopilot.lua` FCU window remains in source for historical tests but is no longer packaged because it delegates managed guidance to stock X-Plane GPSS/VNAV modes. The physical FCU now drives the custom state adapter and imported A380 outer-loop guidance laws; see `docs/AUTOPILOT_DEVELOPMENT.md`.
- The spoiler update restores 11,816 frozen skin triangles across all sixteen panels while preserving 46 fixed-hardware triangles. Eight native degree-valued channels now supply speedbrake-only motion inboard and combined roll/speedbrake motion elsewhere, with flat travel limits and five restored left linkage animations. Actual exported movement stays within 14 mm of sampled FBW source poses; neutral geometry and all wing UVs are preserved within export precision. This adds no Lua dependency or flight-model changes. Exact FBW flap-dependent scheduling, independent hydraulics and ground-spoiler triggers remain unfinished, and native testing is still required.
- All seven previously static landing-gear door clips now use the nearest native deployment ratio. The five large forward nose, wing, and body door groups close at both gear endpoints and open from 12% through 88% travel; the two auxiliary body doors and existing aft nose doors remain open with gear down. Offline OBJ8 evaluation confirms symmetric travel and endpoint closure, and the refreshed package passes structural validation. This is ratio-based visual sequencing rather than a port of FBW hydraulic timing or interlocks; see `docs/LANDING_GEAR_DOORS.md`.
- The landing-gear bays now have a reproducible material pass. The fuselage export includes both previously omitted bay materials: the main bays use FBW's authored albedo, normal, and PBR-derived material/gloss maps, while the secondary bay hardware retains its source-authored plain light finish. The two variants stay separate from the fuselage skins so OBJ8 texture state cannot bleed across materials; this adds no geometry, lighting, decals, or hydraulic behavior.
- Interaction now covers 139 targets: 34 flyPad page/action/power regions across both tablets, 89 configured cockpit/FCU/EFIS/overhead/throttle targets including the cockpit door, ten main-deck passenger-door targets, and six upper-deck passenger-door targets. The native SPD, HDG, ALT, and V/S targets are adjustable from the 3D FCU knobs; SPD/MACH and HDG/TRK are clickable toggles; and the original A380 altitude-increment knob now changes the native step between 100 and 1000 feet. Both EFIS panels now expose side-specific WX, TERR, TRAF, CSTR, WPT, VOR D, NDB, and ARPT buttons plus range, mode, and barometer knobs. Existing painted legends stay visible and every target has a descriptive mouse-hover sign. The throttle pass adds independently draggable engine 1-4 thrust levers, native reverse range on the A380's inboard engines 2/3, both autothrust-disconnect buttons, and four engine-master OFF/RUN switches. The overhead pass covers batteries, generators, APU master/start, anti-ice, bleed air/packs, main feed pumps, exterior lights, and passenger signs; those 32 controls now use their original FBW push/rocker motion driven by the same native X-Plane states. The main- and upper-deck passenger targets plus the cockpit-door target use independent native door commands. Main-deck interior/exterior assemblies, upper-deck exterior leaves, and the authored cockpit door follow their matching ratios. The click OBJ is regenerated from named FBW meshes by `tools/build_cockpit_controls.py`; see `docs/COCKPIT_CONTROLS.md`, `docs/EFB_DEVELOPMENT.md`, and `docs/CABIN_INTERIOR.md`.
- The detailed cabin now uses FBW's LOD00 geometry instead of the stripped LOD01 shell. It adds the forward and aft inter-deck staircases, structural decks, main-deck carpet and seam layers, cabin monuments and lavatory fixtures, main/upper-deck galleys, signs, and detailed passenger doors. The ten animated main-deck doors contribute 191,980 animated interior triangles plus approximately 34,606 exterior triangles across the fuselage materials. The six upper-deck exterior leaves now plug outward and translate forward parallel to the fuselage, with two-sided cabin-facing surfaces. Their restored inner skins, lower panels, and hardware follow that same motion. The fixed jambs, headers, and new curved wall lining close the gaps around all six openings; the LOD00 cockpit keeps its authored cockpit-door clip. Cargo doors, lavatory doors, and cabin fixtures remain static; see `docs/CABIN_INTERIOR.md`.

## Development limits

This is an experimental development airframe, not a finished or flight-validated A380 simulation. Its A380-sized mass, wing, tail, engine, gear, and control-surface model has not completed takeoff, landing, performance, stability, or failure test cards. The corrected flight model has a 145.5-second recorded unpaused runway interval with random failures explicitly verified off, no recorded aircraft collapse, and 1.26 G peak recorded load. That run exposed an unloaded nose wheel and a nose-up ground attitude. The generated ACF now replaces the inherited 747 body trucks with six-wheel bogies, uses neutral truck geometry, and caches the correct 18.55 ft CG-to-contact-plane height. A fresh-process startup at UAAA runway 23R remained uncrashed through the sampled 23.91 seconds, with all five gear deployment ratios at one and normal load between 0.99923 and 0.99944 G; an in-process reload also remained uncrashed past 154.98 seconds. These are startup checks, not a takeoff/landing pass, a complete contact/load acceptance, or proof that third-party plugins were isolated.

An earlier native test was stopped after macOS swap pressure left less than 500 MB free internally; a later check fell to approximately 200 MB. After the user freed space, a short October 4 tail/camera check ran with about 3.1 GB free initially and 1.8 GB at completion, then exited cleanly. Heavy model assets and reports remain external. Monitor internal space before more simulator tests; moving the aircraft does not move macOS swap. An earlier shutdown crash marker without a matching stack report remains unattributed.

The 3D cockpit has textured geometry and working custom-plugin PFDs and NDs on both pilot sides, a center EWD with live four-engine THR/N1/EGT, the original EIS font, and supported engine/gear alerts and system memos (see `docs/EWD_DEVELOPMENT.md` for the remaining FWS/ECL boundary), plus a native physical flyPad on both tablets. Both MFDs are X-Plane 12.1+ plugin cockpit devices drawn and touched through the same native plugin, driven by the pilot's mouse and both KCCUs; the separate system display (SD), OIT, RMP and other display faces remain dark. The display plugin is the beginning of an FBW avionics port, not a complete FMGC/FMS/PRIM/ARINC implementation: its FMA now reflects the custom guidance controller and its ND has no route or nav-database layer yet. Two hundred ninety-two configured cockpit and passenger-door controls (including 136 KCCU keys, scroll wheels, cursor-select buttons, trackballs and KBD/CCD switches), 34 flyPad touch regions and the two MFD device touch screens now have a development interaction pass and hover signs. The 32 mapped overhead controls, both EFIS panels, and all four engine-master switches move from their native X-Plane state; their annunciator lamps and Airbus system logic remain unfinished. EFIS selections intentionally do not invent the missing FBW route or navigation-symbol layers on the partial custom ND. The native FCU now owns selected targets, separate rotary/push/pull hit zones, all AFS buttons, and its four display windows. Imported FlyByWire A380 outer-loop guidance feeds the custom AP/FD and shared PFD state; see `docs/AUTOPILOT_DEVELOPMENT.md` for supported modes and the remaining FMS/autoland boundary. Seven physical FCU annunciator lamps now follow the native engagement state; mechanical knob travel remains unfinished. The thrust and reverse levers change native per-engine datarefs when dragged, but visible lever travel and control detents remain unfinished. The FlyByWire passenger seats and suites are included as static geometry; their two seatback-screen materials are deliberately dark powered-off faces. Cabin lighting and interaction beyond the mapped passenger and cockpit doors, broader cockpit emission/night lighting, transparent glass/weather effects and the broader manipulator set are not finished. Door click targets use the authored closed-position handle meshes; native door commands remain bindable after a door opens. Reviewed animations use native datarefs; unknown clips remain at source rest pose rather than depending on nonexistent custom datarefs. Mixed-weight deformations use bounded approximations where audited; exact FBW spoiler scheduling remains limited and is documented in the control-animation notes.

The latest offline evidence is in `/Volumes/MacGames/A380X-XPlane12-build/reports/`:
`wing-controls-neutral-preview.png`, `wing-full-flap-panels-preview.png`,
`wing-right-roll-preview.png`, `wing-controls-panels-export-vs-source.json`,
`wing-controls-package-validation.json`, and `cockpit-restored-walls-right-preview.png`.
The previous wing export is retained in `xplane-obj/wings-before-neutral-pose-20261004`
and the pre-tail export in `xplane-obj/fuselage-before-tail-repair-20261004` on that
build volume. The pre-control-repair wing group is also retained in
`xplane-obj/wings-before-control-repair-20261004`, and the complete previous flap/
aileron build in `xplane-obj/wings-before-spoiler-repair-20261004`. The spoiler
candidate was promoted to `xplane-obj/wings-corrected`; reports retain the original
candidate path for provenance. New evidence includes `spoilers-export-vs-source.json`,
`wing-spoiler-native-matrix-candidate.json`, `wing-spoilers-package-validation.json`
and `wing-spoilers-left-deployed-preview.png`. No upstream FBW assets were modified.

The current flyPad/cockpit-control and passenger-door refresh passed all 253 pure-Python tests and
the EFB Lua mock suite. The preceding FCU refresh also passed its Lua mock suites, and the
spoiler refresh passed 20 small Blender regressions.
The installed ACF SHA256 is unchanged before/after this refresh. Final structural
validation has zero errors and the existing missing-FMOD-bank warning. Internal
free space fell to about 742 MiB during the offline work, despite external assets;
all Blender jobs have exited. Free internal space before launching X-Plane again.

See `docs/GROUND_TEST_NOTES.md` for the startup-crash repair, pre-fix gear telemetry, stance correction, and exact next probes. The current package passes structural validation. The short stationary native checks verify clean initialization, tail placement, and that the 3-D cockpit can pan right using the normal rotate-view command; they do not validate the remaining gear-stance issue, flight performance, or all animation states. The preceding macOS avionics binary loaded with the A380 in X-Plane 12.4.3 and registered its native flyPad renderer and touch commands. The corrected-orientation, FBW-GUI binary supersedes it; a fresh load was stopped before aircraft-plugin startup when macOS free space fell to about 1.3 GiB. Visual tablet alignment and hand-click accuracy still require an in-cockpit acceptance pass; see `build/reports/efb-native-runtime-smoke.json`. Windows runtime behavior remains untested.

The user's October 4 12:01 screenshots subsequently confirmed the improved
exterior and visible FBW cockpit panels in X-Plane, while also confirming the
misplaced horizontal tail. This is visual feedback, not a new controlled flight
test. Their session log records a clean shutdown; random failures were active
during that session. No aircraft-local camera lock or right-pan restriction was
identified from the ACF/preferences audit. A subsequent 13:00 native screenshot
confirms command-look rotates right by approximately 57 degrees. Direct writes
to head angles did not persist, so use X-Plane's rotate-view controls rather
than relying on raw head-angle writes.

The user's saved global failure settings were enabled with a zero-hour MTBF.
Random failures were disabled and existing failures repaired for the short
visual check. Clean shutdown saved that temporary switch change; the original
global switch was restored afterward, pending permission to change all-aircraft
failure preferences. Future test sessions must explicitly disable failures.

FBW's displays depend on the MSFS avionics SDK and Coherent runtime, so they cannot run in X-Plane merely by translating JavaScript syntax. Electrical, hydraulic, pneumatic, flight-management, cockpit interaction, lighting, and display work remains. The FMOD attachment layer is scaffolded but no custom bank is packaged yet; see `docs/FMOD_DEVELOPMENT.md`.

Do not distribute a build until the license/asset audit in `docs/PORTING_PLAN.md` is complete. The code is GPL-3.0; original FBW 3D assets are CC BY-NC 4.0 and therefore non-commercial.

## PFD and FCU guidance update

The native plugin now compiles the unmodified FlyByWire `A380FgOuterLoops`
guidance laws from the pinned upstream source, with GPL license and a hashed
source manifest. FCU selected targets, active/armed modes, FD commands and
AP/A-THR status feed both PFDs and all four native FCU windows. The physical
FCU has independent rotation/push/pull hit regions, TRUE/MAG and METER controls,
and side-specific LS selectors. The PFD adds FD/FPV, actual selected bugs, speed
trend, ILS scales, QNH/STD, metric altitude, and corrected heading/bank geometry.

This is a selected-guidance and raw-ILS integration. The complete PRIM/FMS/FADEC
network, managed route producer, takeoff/go-around guidance and autoland are
not complete. Port-specific actuator gains and autothrust require in-simulator
flight validation. Build/test evidence is separate from native simulator
acceptance; see `docs/AUTOPILOT_DEVELOPMENT.md` and `docs/AVIONICS_DEVELOPMENT.md`.

## Layout

```text
xplane-port/
  blender/                    glTF inventory and animation conversion
  config/                     reviewed animation-name to dataref rules
  fmod/                       X-Plane 12 starter project, generated SND, and staged FBW Trent authoring sources
  FlyWithLua/Scripts/         cross-platform experimental systems/control law
  output/A380X-XP12/          eventual X-Plane aircraft package
  docs/                       license, architecture, and remaining work
  install_mac.sh              links the Lua bridge into the local X-Plane install
  install_windows.ps1         copies the Lua bridge on Windows
  run_blender_mac.sh          headless Blender conversion entry point
  run_blender_windows.ps1     equivalent Windows entry point
```

## Blender conversion

Install Blender and enable the official XPlane2Blender add-on. The current Mac toolchain is kept on the external build volume. On macOS:

```sh
./run_blender_mac.sh "/path/to/A380_PART.gltf"
```

On Windows PowerShell:

```powershell
.\run_blender_windows.ps1 -InputPath "E:\path\to\A380_PART.gltf"
```

The conversion report flags fallback datarefs. Review and replace those mappings before treating any animation as finished; glTF animation time alone does not identify the simulator variable that drove the original animation.

After a fuselage rebuild, check neutral tail placement before packaging. The
following envelope is a coarse displacement guard in source OBJ coordinates,
not an aerodynamic tolerance or a substitute for viewing trim/elevator travel:

```sh
python3 tools/check_animation_bounds.py \
  /Volumes/MacGames/A380X-XPlane12-build/xplane-obj/fuselage-corrected/a380x_fuse4.obj \
  --filter-dataref sim/flightmodel2/controls/stabilizer_deflection_degrees --value 0 \
  --minimum -17 -2 -40 --maximum 17 12 -18
```

## Experimental flight controls

The Lua bridge only targets aircraft with ICAO `A388` whose folder path contains `A380X-XP12`. It starts disabled until the independent Plane Maker model's stabilizer and surface definitions are verified. For controlled development tests, toggle it with `a380x/xp12/fbw/toggle`. It releases `override_control_surfaces` when disabled, paused, unloaded, or on exit.

The law is deliberately a starting point. Do not regard it as a validated representation of the real A380 or the complete FBW control-law implementation.
