# Ground-test evidence and remaining limits

## 2026-10-05 FBW nosewheel contact fix

The installed `A380X-XP12.acf` now uses a -17.95 ft nose-wheel contact point
and a 20.75 ft cached equilibrium height. Its three moving nose-gear OBJ
attachments are lowered 4.085226378 ft in the ACF; the fixed gear bay stays
at the fuselage datum. The deployed tire mesh and undeflected physics contact
now differ by less than 0.01 mm in the static audit. A copy of the previous
ACF is `build/reports/acf-before-nosegear-contact-fix-20261005.acf`.

X-Plane 12.4.3 loaded the exact FBW package from
`Aircraft/A380X-XP12/A380X-XP12.acf` in a fresh flight. The runtime cached
height read back 6.3246002 m (20.75 ft), confirming the revised ACF was
loaded. With random failures disabled, all five `on_ground` values were 1;
the nose tire initially reported about 503,008 N and 0.23864 m deflection.
The aircraft was stationary (groundspeed about 0.000058 m/s) and `has_crashed`
was 0. A second KSEA runway-start check with the parking brake set recorded
seven near-stationary HTTP samples in
`build/reports/nosegear-ksea-parked-20261005.json`. All five gear remained
`on_ground=1`; settled nose force was about 79,700 N. The daylight external
screenshot `Output/screenshots/A380X-XP12 - 2026-10-05 14.01.08.png` shows
the nose wheel at the runway surface.

This closes the observed unloaded/floating nosewheel at this static test point.
It does **not** pass the broader stance criterion: parked pitch was about
+2.407 degrees nose up and roll about +1.654 degrees, above the 1-degree
targets. Taxi, takeoff, landing, and
strut/bay geometry through retraction remain untested. The UDP RREF probe
received no packets in this session, so there is no multi-second force trace;
the seven HTTP readbacks cover the second parked sample. X-Plane shut down
cleanly after the first run and was restarted for the KSEA check.

## 2026-10-05 update

The earlier generated ACF was a second **unverified** stance candidate: body
contact -16.17 ft, nose contact -17.05 ft, equilibrium height 19.85 ft.
`tools/test_development_build.py` passes, and the package validator reports
zero errors. The initial candidate used -15.75 ft nose contact and 18.97 ft
equilibrium height. Its fresh X-Plane ground probe at UAAA runway 05R reduced
pitch from the baseline 2.071 degrees to 1.032 degrees, but still had zero
nose-tire force and `on_ground[0]=0`. The report is
`build/reports/ground-calibration-diagnostics-20261005.json`.

A second ground-start produced 1.390 degrees pitch and an unloaded nose, but
its runtime gear node and equilibrium-height datarefs were **bit-identical to
the first candidate**. That was a cached-model observation, not a test of the
second ACF. A forced aircraft reload began loading the new file but the native
avionics plugin refused to restart because the EFB rejected command names
retained by X-Plane across reloads. The EFB now reuses those commands and its
headless test covers an init/shutdown/init cycle. Internal free space then fell
below 1 GB, so no second live ground or visual test was attempted. The
current static mesh audit puts the deployed nose tire 0.971 m above its new
physics contact point; visible tire alignment remains unresolved. Do not
claim ground-stance, takeoff, or flight-model validation from this candidate.

Status recorded 2026-10-04. **This is not a flight-test pass or a claim that the aircraft is ready to fly.** An earlier telemetry session was stopped because memory/swap pressure left less than 500 MB free on the system disk. Later, separately scoped startup and visual checks are recorded below; the unresolved physics findings still stand.

## Startup-crash correction

A later package regression copied MSFS's 14.30 ft static-CG-height field into
X-Plane's cached equilibrium-height field.  Those fields do not have the same
meaning: X-Plane needs the aircraft-reference CG height above the ground-contact
plane.  With the source CG 2.8 ft above its datum and the body-main contact
plane 15.75 ft below it, the correct value is 18.55 ft (5.65404 m).  The lower
value initialized the aircraft about 1.30 m inside the runway, produced a
repeatable approximately 16.13 g impulse, collapsed the gear, and set
`sim/flightmodel2/misc/has_crashed=1` during startup.

The generator now writes `acf/_h_eqlbm=18.55` and a neutral cached pitch.  A
fresh X-Plane process then loaded the aircraft at UAAA runway 23R with the
runtime equilibrium-height dataref reading 5.65404 m.  Over the sampled
7.66-23.91 seconds of simulator time, `has_crashed` remained zero, all five
deployment ratios remained one, normal load stayed between 0.99923 and
0.99944 g, and random failures read back disabled.  The preceding in-process
reload also remained uncrashed past 154.98 seconds of total runtime.  This
accepts the startup-crash repair only; it does not close the unloaded nosewheel,
ground-attitude, taxi, takeoff, or landing findings below.  Third-party plugins
were active in both checks.

## Ground-stance correction

The generated ACF now declares the two body-main assemblies as six-wheel
bogies instead of inheriting the reference 747's four-wheel truck type. All
five physics legs are vertical and their truck angles are neutral. The cached
pose now uses the 18.55 ft CG-to-contact-plane height required by X-Plane and a
neutral pitch; the suspension solver remains responsible for settling the
aircraft. This addresses the incorrect aft support geometry without moving the
source-derived CG or using force overrides.

Fresh pre-change telemetry reproduced the visible defect: the nose gear had
`on_ground[0]=0`, indexes 1-4 were loaded, and pitch was approximately 1.7
degrees nose-up. The rebuilt package passes structural validation and the full
Python suite. The fresh startup check above confirms that the corrected package
no longer crashes during initialization, but it did not capture a complete
tire-force distribution and is not a native stance or load acceptance result.

### Separate physics and rendered-wheel mismatches

The earlier ACF placed its nose-tire contact at -15.08 ft in the aircraft
datum, while the body-main contacts were at -15.75 ft. Even at zero pitch,
the nose contact was therefore 0.67 ft (0.204216 m) *above* the body-main
plane. The 2.07117° nose-up pose in `final-ground-probe.jsonl` adds a further
~1.155 m of rigid-body height difference across the nose/body gear positions:
the calculated nose-to-body contact-height difference is 1.359 m before tire
and strut compression. This explains why the previous nose tire could be
unloaded; it is not a direct measurement of clearance above uneven terrain.
FlyByWire's source `flight_model.cfg` specifies `static_pitch = -0.13°` at rest,
so the observed +2.07117° is far from its intended ground attitude. Its
`static_cg_height = 14.30 ft` has a different simulator meaning and must not
replace X-Plane's equilibrium-height value; that mistake previously caused a
startup crash.
X-Plane's active truck-wheel layout, tire pressure, and strut deflection also
matter. Its Plane Maker gear fields specify the attachment arm, extended angle,
leg length, tire radius, and wheel layout separately.

There was also a distinct rendered-gear alignment error. The deployed OBJ8
nose-tire mesh bottom was Y=-4.225983 m, whereas the earlier ACF's physical
nose contact was Y=-4.596384 m: the visible mesh was 0.370401 m above the
physical contact point. The wing/body main tire mesh bottoms were within about
0.03-0.04 m of their source contact heights. A loaded physics nose tire alone
cannot resolve the visible floating wheel; the OBJ attachment/pose must match
the final ACF tire contact independently.

Run `python3 tools/ground_gear_diagnostics.py output/A380X-XP12/A380X-XP12.acf
--probe /path/to/fresh-ground-probe.jsonl` after every gear edit. It derives
neutral contact coordinates from the ACF and reports the nose/body rigid height
difference at the measured pitch. Add `--nose-visible-bottom-m` with a measured
deployed OBJ8 tire-bottom Y to check rendered/physics alignment. This calculation
is diagnostic; X-Plane is the authority for compression and contact forces.
The previous `final-ground-probe.jsonl` fails the tool's stationary ground
stance criterion because the nose wheel is unloaded and pitch exceeds 1°.

For a replacement native stance check, load the *new* ACF in a fresh X-Plane
flight, verify its aircraft identity and runtime ACF gear coordinates, disable
random failures, then capture at least five fresh, unpaused seconds after
settling. Require all five gear `on_ground` flags, a positive nose tire force
above 1000 N throughout the settled window, no collapse/crash, speed below
0.1 m/s, and pitch/roll each within 1°. A later taxi and takeoff run must
demonstrate brake release, increasing speed, controlled rotation, and positive
climb separately. A visual external-view check must show all tire meshes seated
on the same runway surface without gear-strut or tail clipping. The probe can
reject an obvious stance regression; it does not prove dynamic flight quality.

## Later stationary visual check

After the user freed internal space and closed X-Plane, the rebuilt tail passed
neutral/full-trim/elevator geometry bounds checks and was installed. The agent
then ran one short native visual check, with random failures explicitly disabled
and existing failures repaired. `native-tail-corrected.png` confirms stabilizers
at the aft fuselage; `native-cockpit-right.png` confirms the normal rotate-right
command moves the 3-D cockpit view (approximately 57 degrees). These screenshots,
`native-tail-check.json`, and the clean-exit `native-tail-check-session.log` are
in the external reports directory. No takeoff or landing was tested.

Internal free space was about 3.1 GB before launch and 1.8 GB by shutdown. The
simulator exited cleanly; no forced process termination was used. This session
still loaded third-party plugins and is not an isolated-plugin acceptance test.

The user's pre-test preferences had `_use_MTBF 1` and `_real_MTBF_hours 0.000000`.
`user-session-before-tail-install.log` explicitly records repeated random
failures, including flap-actuator and hard-right yaw failures. This is a native
failure-setting problem; it does not establish the cause of every model defect.
The test switch-off was saved by clean shutdown, then the original global switch
was restored pending approval for an all-aircraft preference change. Disable and
verify random failures after every future initialization before interpreting
flight-control behavior.

### Subsequent offline model update

After that simulator session ended, the six missing cockpit wall/window/sideboard
materials, a wing neutral-skin-pose repair, and a later horizontal-tail animation
repair were exported and checked offline. That intermediate tail version used
four actual elevator-degree outputs and a right-palette permutation; symmetry
and bounds checks alone missed incorrectly assigned moving parts. It has since
been superseded by the v3 one-based joint-index correction, complete elevator
panels, pilot-pitch visual fallback, and source-calibrated trim angles described
in `CONTROL_ANIMATION_NOTES.md`. The earlier native screenshots do not validate
this new revision's movement.
The wing change preserves all 739,929 triangles and every checked UV/animation
mapping, closing 2,153 tested neutral seams to within 0.064 mm. The cockpit adds
260,386 source triangles with all 18 texture files present. These later changes
are not represented by the native screenshots above; they still need a native
recheck. Deployed flap/slat behavior, mixed-weight dynamic deformation and ground
stance remain unresolved. No additional simulator launch was made for this update
because internal free space remained about 2 GB.

## Recorded evidence

Reports are on `/Volumes/MacGames/A380X-XPlane12-build/reports/`:

- `final-ground-probe.jsonl`: fresh-loaded reference CG values were 2.8 ft vertical and 99.108 ft longitudinal; equilibrium start height was 5.65404 m. The explicit native random-failure switch read back zero, with MTBF 5000 hours. The final sampled simulator time was 145.726 s despite a 240 s wall-clock capture; distinguish wall time from simulator time.
- Root subsequently observed `has_crashed=0` through about 224 s of simulator time with random failures disabled. This supports ground-session stability only, not taxi, takeoff, landing, or flight handling.
- The settled sample had mass 461780.09 kg, longitudinal CG offset 0.37928 m, pitch 2.07117 degrees, groundspeed approximately 0.000007 m/s, normal load approximately 0.9992 g, and all five deployment ratios equal to one. All sampled gear-collapse and wing-failure flags were zero.
- **Nosewheel remained unloaded:** `on_ground[0]=0`, zero nose tire force and deflection, while gear indexes 1–4 were loaded. This unresolved stance is not a successful landing-gear validation.
- `final-ground-forces.jsonl`: native ground normal force approximately 4.521 MN, ground pitch moment approximately -1.594 MN m, mass-loading pitch moment approximately +1.594 MN m, and near-zero aerodynamic/propulsive force and moment. Sampled plugin-added normal force and pitch moment were zero.

The four loaded values of `tire_vertical_force_n_mtr` sum to approximately 1.561 MN, unlike the total native ground force. Do not assume a wheel-count multiplier resolves this without measuring it. The installed dataref catalogue describes this array as vertical force on each gear (newtons), despite its misleading `newton_meters` units label.

## Interpretation, not a proven cause

`M_mass` is the native pitch moment due to asymmetric loading. Its measured equivalent arm, about 0.353 m, is close to the measured longitudinal CG offset. The inherited fuel distribution also lowers the loaded CG, so the aircraft's positive pitch affects the gravity moment. This is consistent with normal mass-loading accounting; it is not evidence of a separate torque fault.

Runtime attachment offsets place the wing mains 3.13944 m aft of the reference CG and the body mains 6.61416 m aft. The loaded CG is still ahead of both main-gear pairs. Four ordinary aft supports alone do not explain the small measured net ground pitch moment. Unreported ground contact, runtime geometry, or force-array aggregation remain hypotheses. Do not shift the source-derived CG to hide this discrepancy.

The active invisible physics bodies are fuselage 0 and nacelles 21–24; the active wing slots are 0–11. Static inspection found no obvious ground intersection in these shapes at the assumed pose, but did not establish actual simulator contact points. Other body slots are disabled; wing slots 12 and above are removed because leaving disabled nonzero wing geometry previously reached a native airfoil lookup with an uninitialized pointer. Invisibility is not a physics-disable setting.

## Geometry provenance and remaining scaffold values

The source FBW `flight_model.cfg` gives empty CG `(16, 0, 2.8)` ft in MSFS coordinates. With the conversion's 115.108 ft nose datum, X-Plane CG is `(longitudinal=99.108, vertical=2.8)` ft.

| Gear | Source longitudinal / lateral / contact vertical (ft) | X-Plane attachment Z / Y (ft) | Radius / leg (ft) |
| --- | --- | --- | --- |
| Nose | 99.15 / 0 / -15.08 | 15.958 / -4.341 | 2.239 / 8.5 |
| Wing pair | 5.7 / +/-23 / -15.63 | 109.408 / -5.474 | 2.356 / 7.8 |
| Body pair | -5.7 / +/-11.5 / -15.75 | 120.808 / -6.394 | 2.356 / 7.0 |

These strut lengths are conversion assumptions, not validated A380 suspension geometry. The physics legs are vertical so the listed attachment, radius, and leg values reproduce the source contact points directly. The source contact plane gives an initial ground pitch of approximately -0.366 degrees. The following scaffold values also remain to validate:

- Wing-main gear uses X-Plane's four-wheel truck type and body-main gear uses its six-wheel truck type. This replaces the inherited four-wheel 747 definition on the body bogies.
- Eagle-claw angles are neutral on all five assemblies; shock-to-tire deflection ratio is zero.
- Global truck longitudinal separation is 3 tire radii, about 2.154 m with the main tire radius; lateral separation is 3 half-tire widths. This spacing alone cannot place a wing-main front axle ahead of the measured CG.
- Custom gear damping and deflection curves are disabled; stored custom values are not a validated model. Fuel tank positions/distribution and inertia radii are still scaffold values, not a completed FBW physics conversion.

## Next telemetry, after resources are restored

Use a fresh aircraft initialization/reload and verify the loaded aircraft and reference geometry before interpreting a new sample. Earlier differently named probes were bit-identical despite edited files because the simulator retained its previously loaded model.

Capture together, preferably in a single short run:

- Identity and freshness: `sim/aircraft/view/acf_relative_path`, `sim/time/total_running_time_sec`, `sim/time/total_flight_time_sec`, `sim/time/paused`.
- Failure controls: `sim/operation/failures/enable_random_failures`, `sim/operation/failures/mean_time_between_failure_hrs`, `sim/flightmodel2/misc/has_crashed`.
- Mass and pose: `sim/flightmodel/weight/m_total`, `sim/flightmodel/weight/m_fuel[0..8]`, `sim/flightmodel2/misc/cg_offset_z`, `sim/aircraft/weight/acf_cgY_original`, `sim/aircraft/weight/acf_cgZ_original`, `sim/flightmodel/position/theta`, `sim/flightmodel/position/phi`, `sim/flightmodel/position/local_y`, `sim/flightmodel/position/y_agl`, `sim/flightmodel/position/groundspeed`.
- Runtime gear geometry, indexes 0–4: `sim/aircraft/parts/acf_gear_type`, `acf_gear_leglen`, `acf_gear_ynodef`, `acf_gear_znodef` under the same prefix; `sim/flightmodel/parts/tire_x_no_deflection`, `tire_y_no_deflection`, `tire_z_no_deflection` under that prefix; `sim/flightmodel2/gear/eagle_claw_angle_deg`.
- Contact, indexes 0–9: `sim/flightmodel2/gear/deploy_ratio`, `on_ground`, `is_chocked`, `tire_vertical_deflection_mtr`, `tire_vertical_force_n_mtr` under that prefix. Also capture all 48 entries of `sim/flightmodel/forces/wing_ground_scrape_f` and `wing_ground_scrape_v`.
- Forces: `sim/flightmodel/forces/fnrml_gear`, `faxil_gear`, `M_gear`, `M_mass`, `M_total`, `fnrml_aero`, `M_aero`, `fnrml_prop`, `M_prop`, `fnrml_plug_acf`, `M_plug_acf` under that prefix.
- Overrides: `sim/operation/override/override_gear_forces`, `override_forces`, `override_groundplane`, `override_planepath` under that prefix. Read only; do not use force overrides to mask the problem.

Resolve current dataref names and types from the installed `Resources/plugins/DataRefs.txt` before subscribing. The bracket ranges above mean separate array elements, not literal dataref names.

## Test isolation and transport caveats

The launch log contained `--safe_mode=PLG`, but also loaded Navigraph Simlink, AdvancedCabin, AviTab, BetterPushback, FSExperience, FlyWithLua, X-Camera, openSAM, winwing, and AEP-Live. **This was not a plugin-isolated test.** Zero sampled plugin-force datarefs do not exclude plugins writing other simulator state.

An earlier log explicitly recorded the user confirming quit before shutdown/unload and a final crash marker. No matching new native crash stack was found; do not attribute that shutdown marker to the aircraft. `full-model-pre-final-load.log` preserves that session.

One ground-start API request loaded the aircraft but closed the HTTP response with `RemoteDisconnected`. The helper now treats that POST outcome as **initialization unconfirmed**, never repeats the POST, and still attempts the explicit random-failure disable/readback. Successful readback confirms only the switch in the current simulator session, not aircraft identity or completion of a new flight. If that follow-up fails, its error propagates. All other initialization errors continue to propagate.

## Primary references

- [Laminar: Weight & Balance and Load Stations](https://developer.x-plane.com/article/weightbalance-and-load-stations/) — payload moments and CG handling.
- [Laminar: X-Plane 12 Flight Model Report](https://developer.x-plane.com/article/x-plane-12-flight-model-report/) — mass distribution, suspension, and eagle-claw mechanics.
- [Laminar: Plane Maker Manual](https://developer.x-plane.com/manuals/planemaker/) — gear positioning and wheel-separation units.
- Installed `Resources/plugins/DataRefs.txt` — exact native dataref names, descriptions, and types for the tested version.
