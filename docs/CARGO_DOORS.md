# Cargo doors and holds

The aircraft package now includes the authored FlyByWire forward, aft and bulk
cargo interior geometry, its cargo albedo/normal/material maps and painted
markings, together with independent animations for all three cargo doors.
The upstream material named `Cargo_FWD` actually contains parts in all three
areas; its name is not used to restrict the restored geometry to the forward hold.

Open **ISCS → Ground Services → Cargo Holds**, then single-click **FWD**, **AFT**
or **BULK**. Each button reports CLOSED, MOVING or OPEN from its own actual native
door ratio. The controls are ground-only. The existing tablet's **ALL 20 DOORS**
also includes cargo; passenger doors and cockpit door keep their old channels.

| Door | Native ratio | Assignable native command | Motion |
| --- | --- | --- | --- |
| FWD | `sim/flightmodel2/misc/door_open_ratio[17]` | `sim/flight_controls/door_toggle_18` | Authored 21-key `fwd_door_cargo` hinge clip, including its child skins and fittings. |
| AFT | `sim/flightmodel2/misc/door_open_ratio[18]` | `sim/flight_controls/door_toggle_19` | Authored 21-key `aft_door_cargo` clip, including skins and lower fairing. |
| BULK | `sim/flightmodel2/misc/door_open_ratio[19]` | `sim/flight_controls/door_toggle_20` | Provisional 90° rigid inward/upward rotation about the measured upper edge; source provides a leaf but no animation clip. |

The door channels and array length were checked against the installed X-Plane
12 command/dataref catalogs. Native X-Plane owns the door travel; no second door
state or global plugin is introduced. The dormant generic `door_cargo` clip
targets meshless legacy nodes and remains at rest. Two previously omitted
textured hardware meshes are restored and follow their respective door.

The source-backed geometry contains 204,366 cargo surface triangles, 546 marking
triangles and 879 moving hardware triangles. The existing exterior door meshes
are preserved: only their draw/animation wrappers change. All original exterior
vertex positions, indices, UVs, textures, passenger/gear animations and cockpit
manipulators survive. Three cargo attachments are appended to the current ACF;
all pre-existing attachments and flight-model properties stay unchanged. The
editable `acf/A380X-XP12-current.acf` receives the same bounded attachment update.

## Source and scope

Geometry and forward/aft poses come from the pinned FBW `A380_FUSELAGE.gltf`.
The source remains read-only. The layout reference is Airbus's
[A380 Aircraft Characteristics, sections 2-6 and 2-7](https://www.aircraft.airbus.com/sites/g/files/jlcbta126/files/2024-11/AC_A380_1124.pdf).
The bulk hinge travel is a port approximation, not a measured or manufacturer
validated mechanism. Hydraulic actuators, pressure/flight interlocks outside
the ground UI, latches, cargo loading/ULD movement, fire protection and cargo
lighting are not simulated by this geometry update. Existing cargo weight input
continues to affect payload separately.

## Rebuild and installation

`tools/prepare_cargo.py --source <A380_FUSELAGE.gltf> --asset-root <upstream-aircraft-large-files> --build-dir <fresh staging directory>`
normalizes only selected geometry, converts authored textures and stages the
focused patch. It needs Python/Pillow and the source buffers, not Blender.
Add `--install` for the backed-up installation; `--native <verified native build>`
also installs the updated native ISCS/tablet code. A full or fuselage-only
`tools/build_local_mac.sh` refresh runs the cargo geometry preparation afterward.

`tools/install_cargo.py <geometry staging directory> --native <verified native build> --report <installation.json>`
preflights asset/binary hashes and the Mac signature, refuses while X-Plane is
running, saves originals and a rollback map, then installs atomically per file.
New cargo files are removed on rollback. Existing cockpit OBJ, calibration and
avionics HTML hashes are checked before/after. The Windows binary retains the
installed native-display build configuration; this cargo update does not switch
the aircraft to a different Windows display host.

Changed sources: `config/cargo_doors.json`, `config/animation_datarefs.json`,
`native/avionics/a380x_efb.c`, `a380x_efb_render.inc`, `a380x_iscs.inc`,
`test_efb_render.c`, `tools/build_cargo_geometry.py`, `prepare_cargo.py`,
`install_cargo.py`, `verify_cargo.py`, `test_cargo.py`,
`test_passenger_door_mapping.py`, and the focused build wrapper/documentation.

## Verification — 28 October 2026

Evidence is in `build/cargo-20261028/`: native build report and ISCS render,
35 focused Python tests, staged/installed geometry evaluation, installation
receipt, backup and whole-aircraft static validation. `cargo-geometry.png` is
an explicitly labelled offline OBJ8 geometry preview, not an X-Plane screenshot.

The actual exported OBJs are evaluated at 0, 0.25, 0.5, 0.75 and 1 on each
individual cargo channel. The checks cover closed-pose agreement, complete
moving-triangle counts, finite motion, channel independence and stationary
hold/fuselage structures. Native mock clicks cover open/close of each cargo
button, the all-twenty operation and the ground-only UI guard.

X-Plane remains closed at the user's request. Live visual clearance, mouse
interaction, timing, livery inspection, interior camera views and FPS remain
unverified; no simulator screenshots or performance figures are claimed.

The installed result is `output/A380X-XP12/`. The Steam simulator aircraft at
`/Users/sava/Library/Application Support/Steam/steamapps/common/X-Plane 12/Aircraft/A380X-XP12`
is now a separate older copy (not the previous symlink); it is left unchanged.
Use the complete updated port package when replacing that aircraft, preserving
local liveries/preferences. Final static validation passes all 129 attached
objects with zero errors. The installed asset hashes match the tested staging;
a repeat staging produces identical assets. The installed binary matches its
passing build receipt. An unrelated `a380x_fws_types.h` edit made after that
build is left intact and is identified separately in `verification.json`.
