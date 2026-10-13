# Hydraulic surface settling — 2026-10-06

The exterior flight-control animation now reads actual X-Plane hydraulic
pressure. With the aircraft on the ground and pressure lost, all six ailerons
and all four elevator panels slowly settle down. The two rudder panels slowly
move downstream with the local wind. Spoilers hold their last supported
position and ignore roll/speedbrake commands until control power returns.
A cold, unpressurized load starts spoilers closed, even with a raised lever.

This is a pressure-aware exterior animation adapter. It does not replace
X-Plane's hydraulic or aerodynamic solver, alter pilot inputs, or write native
flight-control deflections. It is not a complete port of the A380 Green/Yellow
networks, electric actuator backup, individual actuator failures or RAT logic.
The installed aircraft's existing flight physics are retained.

## Pressure, motion and flight behavior

The adapter reads all three native `sim/cockpit2/hydraulics/indicators/`
`hydraulic_pressure_1`, `_2`, `_3` channels. They use Plane Maker pressure units,
not normalized ratios. This package configures nominal engine pressure of
3000 PSI. Available pressure is the maximum channel; 450 PSI releases the
surfaces and 900 PSI restores support, with hysteresis between the thresholds.
This models common support availability, not per-actuator source distribution.
Engine count is deliberately not a pressure proxy: running engines with failed
pumps do not prevent settling, and electric pumps can maintain pressure with
the engines stopped. The pressure thresholds and settling rates are development
approximations for this ACF, not certified A380 values.

Ground settling uses damped, rate-limited motion. Both wings settle trailing-edge
down despite their opposite animation signs. Elevators settle down separately
from stabilizer trim. Rates are limited to 0.045 clip ratio/second for ailerons,
0.04 for elevators and 0.05 for rudders. Most droop develops over tens of
seconds. Calm wind holds the rudder's previous free position. Wind direction
is relative to true aircraft heading: with the nose north, wind from the east
pushes the rudder trailing edge left. Stronger wind produces greater deflection;
there is no arbitrary oscillation. Ground airflow from 10 through 35 m/s
progressively unloads gravity droop. Pressure restoration returns surfaces
smoothly to their control commands, including spoilers.

Airborne loss of engine thrust does not activate parked droop. The primary
surface animations continue to follow native controls and the existing three-panel
aileron response; unsupported spoilers continue to hold their last positions. This preserves the intended visible failure behavior without
claiming the adapter simulates electrical emergency power. Airbus describes
RAT supply to EHA/EBHA during total engine flameout in
[The A380 Flight Control Electrohydrostatic Actuators](https://icas.org/icas_archive/ICAS2006/PAPERS/048.PDF),
PDF page 7. The fixed vertical fin is unchanged, and the horizontal stabilizer
continues to follow its existing trim input; only elevators and rudder panels
have passive motion.

The relaxed clip pose is derived from pinned FlyByWire revision
`2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2`,
`fbw-a380x/src/wasm/systems/a380_systems/src/hydraulic/mod.rs` (aileron/elevator
factories and slow damping comment), and the corresponding WASM surface
aspects. Left ailerons use +20/30, right ailerons -20/30 and elevators -20/30.
These are source animation ratios; the exported mesh's geometric angle can
differ from the actuator-command angle. The source spoiler actuator restricts
upward movement without pressure; the user's requested parked behavior holds
all spoiler positions, including a spoiler left raised before shutdown.

## Runtime and build

The existing `A380XAileronDatarefs` aircraft plugin now registers 22 scalar
Float datarefs in `XPluginStart`: six legacy aileron-command outputs and sixteen
final surface-animation outputs under `a380x/xp12/hydraulics/`. The legacy
dance driver remains active. `A380X_XP12_Hydraulics.lua` reads those six values,
native pitch/yaw/spoiler channels, pressure, ground state and weather, then
runs the independent `a380x_hydraulic_surfaces.lua` state machine.

All four Lua files are packaged and required in global FlyWithLua. The provider
manifest lists exact names, types, ranges and hashes. The native bootstrap
clamps ratios to -1..1 and spoiler degrees to 0..65; all values reject NaN and
infinity. Missing references/types disable startup rather than creating late
OBJ datarefs. Pauses, invalid data and discontinuous frame periods hold the
last valid pose. The adapter is guarded to this A388 package and never writes
to native controls or overrides.

`config/animation_datarefs.json` persists the bindings for future exports.
The guarded rebinder changes animation-header dataref tokens only: mesh data,
keyframes, trim, flaps, gear and cockpit control mappings are retained.
`tools/install_hydraulic_surfaces.py` installs the staged update with original
hash verification, backups and rollback, while X-Plane is closed.

## Verification

`tests/hydraulic_surfaces_test.lua` exercises real state transitions, wind,
pressure recovery, airborne failure, frame timing and pause handling.
`tests/hydraulic_visual_mock_test.lua` exercises the installed wrapper contract,
including native-write prohibition. Native SDK mocks cover all 22 registration,
collision and rollback positions, plus scalar ranges. The geometry staging
report checks 510 token-only edits across 20 OBJ files. The independent
`tools/audit_hydraulic_surfaces.py` evaluates 38 affected draw cases and 2,830
unrelated draw comparisons, confirming downward droop and both rudder wind
directions while retaining neutral geometry.

These tests establish offline behavior and packaging. A simulator observation
is separate evidence and must not be inferred from a compiled plugin or mock.

## Installed — 2026-10-07

The update is installed in the aircraft package and global FlyWithLua runtime.
The native Mac universal and Windows x64 bootstrap binaries are included. The
installer verified all hashes and preserved the ACF. Originals and installation
evidence are in `build/hydraulics-backup-20261007`; independent geometry evidence
is in `build/hydraulics-candidate-v2/motion-report.json`. Five engine objects
were independently cleaned during preparation and are preserved unchanged.

A full X-Plane restart loads the new 22-reference bootstrap. Native simulator
observation has not yet been performed for this update.

Future builds also translate legacy cached exterior bindings during scoped
packaging. This was checked against all 20 installed objects byte-for-byte;
cached source exports and their material directives are unchanged. Package
validation reports zero errors.
