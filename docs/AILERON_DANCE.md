# Three-panel aileron response — 2026-10-04

Update 2026-10-06: the six outputs below now feed the pressure-aware final
surface adapter. See [Hydraulic surface settling](HYDRAULIC_SURFACES.md) for
ground droop, wind-driven rudders, spoiler holds and the four-file runtime.
The following sections describe the original dance layer and its history.

This is a visual-only adaptation of the pinned FBW command-distribution logic.
It does not change the X-Plane flight model, joystick input, autopilot, native
surface deflections or control overrides. It is not a complete A380 normal-law,
load-alleviation, hydraulic, droop or high-speed scheduling implementation.

## Reference and source

The user's [reference video](https://www.youtube.com/watch?v=Azy3M8V77o4),
“What you may not know about this planes aileron dance”, describes different
travel between the three panels and a 350 ms middle-panel delay. FBW also
describes its triple-aileron simulation in
[A380X: Full Speed Ahead](https://flybywiresim.com/notams/a380x-full-speed-ahead/).
The implementation uses the downloaded GPL source, not a guessed oscillation
or a motion trace copied from the video.

FBW revision: `2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2`.
In `fbw-a380x/src/wasm/fbw_a380/src/model/A380PrimComputerFctl.cpp`, lines
2435–2473 distribute the airborne aileron command:

- Rate-limit half the command to 12 degrees/second, then multiply it by 2.5.
- Inboard follows that amplified command; middle follows it after 350 ms.
- Outboard follows the unamplified half-command plus saturation overflow.
- Limit individual commands and apply the source's 30 degrees/second surface
  rate limit. Ground mode bypasses the split/delay.

The visual adapter substitutes `-30 * sim/flightmodel2/controls/roll_ratio`
for the source's normal-law command. This native reference includes stability
augmentation and is read only. Positive input retains the existing right-roll
visual direction: left trailing edges down, right trailing edges up. The module
publishes source-clip ratios using the FBW hydraulic/MSFS conversion's degree/30
normalization. It does **not** reinterpret the authored clip's approximately
25-degree geometric endpoints as the source command limit.

Small steady airborne inputs produce 2.5 times as much inboard/middle travel as
outboard travel; saturation changes that relationship near full input. At a
direction reversal, inboard and middle can temporarily point in different
directions. Neutral input does not generate arbitrary movement. Actual animation
depends on changing effective roll commands, not directly on gust sensors.

The source's sampled delay is adapted to a timestamp-interpolated 350 ms buffer
with at most 10 ms integration steps. Pause holds the pose. Reload and invalid
or greater-than-one-second frame periods seed a steady current-input pose,
preventing stale delay history. These initialization/interpolation details are
explicit adaptations, not claims of bit-identical simulator behavior.

## Geometry and build preservation

Six existing bones are used: `WING_AILERON_12/13/14.L/R`, corresponding to the
inboard/middle/outboard clips. No geometry was rebuilt. The source-checked
rebinder changes exactly 18 rotation-header dataref tokens across
`a380x_wing1.obj` and `a380x_wing2.obj`; all other bytes in all 11 wing OBJs are
unchanged. Existing 201-key curves, vertices, UVs, indices, flap and spoiler
animations are retained. Future Blender rebuilds use the same six explicit
rules in `config/animation_datarefs.json`.

There are 3,320 main aileron triangles plus 10,944 attached outboard discharger
triangles across eight draw calls. Only those 14,264 triangles follow the six
new inputs. The wing group still has 739,929 triangles.

## Required runtime, installation and fallback

This variant requires **FlyWithLua NG** (reviewed installed API: 2.8.12) plus:

- `plugins/A380XAileronDatarefs`: aircraft-local early-registration plugin,
  with a universal Apple Silicon/Intel Mac binary and a Windows x64 binary.
- `FlyWithLua/Modules/a380x_aileron_dance.lua`: pure Lua 5.1-compatible core.
- `FlyWithLua/Scripts/A380X_XP12_Ailerons.lua`: guarded visual-only provider.

These are separate from the optional experimental FBW control-law script.
Do not enable that law for the aileron animations. Six custom scalar Float
datarefs are registered under `a380x/xp12/visual/aileron_`, with suffixes
`l_inboard_ratio`, `l_midboard_ratio`, `l_outboard_ratio` and their right-side
equivalents. The exact six-name contract and SHA256 hashes are packaged in
`animation-providers/aileron-dance-v1.json`. Unknown custom datarefs remain
validation errors; the validator does not exempt an entire namespace.

With X-Plane closed, the cross-platform installer is:

```sh
python3 tools/install_aileron_visuals.py --aircraft /path/to/A380X-XP12 --xplane /path/to/X-Plane-12
```

It copies only the two visual files into the simulator's global FlyWithLua
Modules/Scripts folders, refuses different existing files, and verifies them
against the package. The package also includes `INSTALL_AILERON_VISUALS.txt`
for manual Mac/Windows installation. No experimental control-law enablement,
global simulator preference change or native surface write is part of this step.

The driver only operates for ICAO A388 in an `A380X-XP12` path component. Guards
and callback error containment prevent the new driver from issuing native
writes. The aircraft plugin registers all six scalar Floats in `XPluginStart`,
before attached OBJ loading, and holds cached visual values only. Lua binds
that complete set and never creates it late. Missing/partial sets, wrong types
and duplicate active callback namespaces are rejected. The plugin itself fails
before registration if any of the six names already exists. Fully restart the
simulator when upgrading from the original Lua-created-reference version.

If FlyWithLua is missing or stopped, these visual ailerons cannot animate
normally. Cached datarefs can retain their last values after a Lua failure, so
do not assume a stopped driver always produces a neutral pose. Keep the complete previous
native-input wing export at
`/Volumes/MacGames/A380X-XPlane12-build/xplane-obj/wings-before-aileron-dance-20261004`.
Restoring that export and refreshing the wing package restores native-only
aileron animation; it does not require the visual driver. Do not rebuild the
native fallback with the new custom mapping until intentionally re-enabling
the dance variant.

## Verification and limits

Core tests run under standalone Lua 5.1.5 and 5.4.6. Host mocks replay actual
registered callback strings, prohibit native writes, and cover missing APIs,
invalid input, aircraft switching, pause and runtime errors. They are not an
actual FlyWithLua session.

An independent offline integration audit feeds 181 Lua-generated samples over
three seconds into the **actual exported OBJ animation tables**. It verifies
neutral byte/geometry parity, middle-panel delay (first 60 Hz frame after 350 ms
is 366.7 ms), opposite physical wing directions, reversal with opposing
inboard/middle motion at +300 ms, neutral return and unchanged unrelated draws.
All 20,815 unrelated draw comparisons are exact, including the spoiler branches.

Evidence under `/Volumes/MacGames/A380X-XPlane12-build/`:

- `reports/aileron-dance-bindings.json`: original/output hashes and exact edits.
- `reports/aileron-dance-export-audit.json`: actual OBJ temporal/geometry audit.
- `animation-audit/audit_aileron_dance_export.py`: reproducible integration check.
- `reports/aileron-dance-core-lua51.log` and `aileron-dance-core-lua54.log`.
- `reports/aileron-dance-wrapper-lua51.log` and `aileron-dance-wrapper-lua54.log`.

Native Mac runtime acceptance and Windows runtime testing remain pending.
For a controlled in-simulator check, verify failures are off, start with centered
controls, then use small left/right inputs in flight: inboard should respond
first, middle later, and outboard with less travel. On the ground the deliberate
bypass moves all three together. No full flight-control fidelity is claimed.

## Installed update — 2026-10-04

With the user-confirmed X-Plane process closed, the scoped packager installed
only the two wing OBJ changes, provider files and provider manifest information.
The ACF SHA256 remains
`99a4cbaddfa1d934435c09ef5089c44d19d04c492a1bea157da693812fd36168`;
referenced wing textures were unchanged. The prior installed wing files and
build manifest are backed up at
`/Volumes/MacGames/A380X-XPlane12-build/aileron-dance-package-backup-20261004`.
The native export is retained at the fallback path above; the audited candidate
has been promoted to `xplane-obj/wings-corrected` on the same external volume.

The wrapper and core are installed in the Mac simulator's global FlyWithLua
folders. Their hashes match the packaged versions. This verifies installation,
not execution in the simulator. The experimental FBW control-law source and
simulator preferences were not changed.

Final checks: 186 Python tool tests passed, Lua 5.1/5.4 core and host-contract
tests passed, and installed-aircraft static validation reported zero errors.
Its warnings identify the required Lua runtime and the existing absent custom
FMOD sound bank. Installation evidence:

- `reports/aileron-dance-package-installation.json`
- `reports/aileron-dance-runtime-installation.json`
- `reports/aileron-dance-runtime-verification.json`
- `reports/aileron-dance-package-validation.json`
- `reports/aileron-dance-python-tests.log`

To repeat the export integration audit after promotion, pass
`--baseline /Volumes/MacGames/A380X-XPlane12-build/xplane-obj/wings-before-aileron-dance-20261004`
and `--candidate /Volumes/MacGames/A380X-XPlane12-build/xplane-obj/wings-corrected`.

## Stationary-aileron regression repair

The user reported no visible motion after the original installation. Logs
confirmed Lua created all six datarefs, but this did not establish successful
OBJ binding. The original pure-Lua registration occurred after aircraft loading;
[X-Plane requires aircraft OBJ datarefs to be registered in XPluginStart](https://developer.x-plane.com/article/deferredinitialization/).
The new aircraft-local native component meets that startup requirement without
any flight-model overrides. Its callbacks only read/write six cached floats.

There was also a definite timing bug: `SIM_PERIOD` belongs to another scripting
environment and was absent from the installed FlyWithLua environment. A nil
period reset the animation state every frame, eliminating the intended delay.
The wrapper now reads `sim/operation/misc/frame_rate_period` explicitly. Tests
raise an error if the unsupported global is accessed and replay actual callbacks
through 350 ms. This timing issue alone does not explain all stationary geometry.

Read-only localhost probes under `reports/aileron-stall-live-probe-*.json`
captured initialization and neutral controls, not a valid moving-input trace;
therefore they do not prove the runtime failure's sole cause or the repair's
success. The updated wrapper logs its first frame and first nonzero output plus
SDK readback exactly once per script load to help establish that chain.

The native build and 10 mocked-SDK lifecycle tests are recorded in
`native/aileron_datarefs/dist/build-report.json`. Native Mac/Windows execution
and visible motion require a new in-simulator test. The backed-up, scoped upgrade
is performed only after full simulator shutdown with
`tools/upgrade_aileron_provider.py`; it does not modify any OBJ, ACF or texture.

### Repair installation completed

After the user's explicit request to update the plane, X-Plane was confirmed
closed and the repair was installed into the SD-card aircraft and the simulator's
active FlyWithLua folders. The packaged Lua copy had already advanced to an
intermediate timing fix, while the global copy was still the original
`726f3d4c...` version. The updater verified that older global copy against the
previous successful installation report before replacing it; arbitrary changed
scripts still cause the updater to stop before writes.

The installed wrapper now has SHA256
`4b8159a0a6f324e3a99d0c886cceb107c74f70b7a5a980691a021d9bba42f6ea`.
Both early-registration binaries are included in the aircraft plugin folder.
The ACF hash is unchanged. Previous files and the exact update report are at
`/Volumes/MacGames/A380X-XPlane12-build/aileron-startup-repair-backup-20261004`.
Installed-file verification is recorded in
`reports/aileron-startup-repair-runtime-verification.json`, and static aircraft
validation in `reports/aileron-startup-repair-package-validation.json`.
No simulator restart or visible-motion acceptance test is claimed by this
installation record.
