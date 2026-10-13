# Detailed cabin, stairs, floors, and passenger doors

## Included geometry

The packaged cabin is built from the pinned FlyByWire
`A380_CABIN_LOD00.gltf`, not the earlier stripped LOD01 shell. The guarded
material selection contains fifteen OBJ8 attachments and 2,198,014 source
triangles:

- forward and aft inter-deck staircases;
- main- and upper-deck structural floors;
- the visible main-deck carpet plus carpet seam/edge detail;
- main-deck cabin fixtures, lavatory/monument assets, and premium/economy
  sidewalls;
- main- and upper-deck galleys and cabin signs;
- the detailed main-deck passenger-door assemblies;
- the authored fixed jamb, header, and threshold around all six upper-deck
  passenger-door openings.

Transparent passenger glass, rain overlays, transparent ceiling shades,
invisible cabin bounds, and the unclassified validator-default material remain
excluded. Lavatory fixtures and internal lavatory doors are static cabin
geometry; this pass does not simulate water, waste, occupancy, locks, smoke
detection, or service logic.

`tools/prepare_cabin_interior.py` verifies that every selected material remains
opaque and has triangles before preparing textures. `tools/export_cabin_mac.sh`
then creates an animation-preserving material subset and rebuilds only the
detailed cabin through the external Blender/XPlane2Blender toolchain.

## Main-deck passenger doors

The ten authored main-deck door clips are mapped one-to-one to native X-Plane
door ratios and commands:

| A380 door | Ratio dataref | Click/bind command |
| --- | --- | --- |
| M1L | `sim/flightmodel2/misc/door_open_ratio[0]` | `sim/flight_controls/door_toggle_1` |
| M1R | `sim/flightmodel2/misc/door_open_ratio[1]` | `sim/flight_controls/door_toggle_2` |
| M2L | `sim/flightmodel2/misc/door_open_ratio[2]` | `sim/flight_controls/door_toggle_3` |
| M2R | `sim/flightmodel2/misc/door_open_ratio[3]` | `sim/flight_controls/door_toggle_4` |
| M3L | `sim/flightmodel2/misc/door_open_ratio[4]` | `sim/flight_controls/door_toggle_5` |
| M3R | `sim/flightmodel2/misc/door_open_ratio[5]` | `sim/flight_controls/door_toggle_6` |
| M4L | `sim/flightmodel2/misc/door_open_ratio[6]` | `sim/flight_controls/door_toggle_7` |
| M4R | `sim/flightmodel2/misc/door_open_ratio[7]` | `sim/flight_controls/door_toggle_8` |
| M5L | `sim/flightmodel2/misc/door_open_ratio[8]` | `sim/flight_controls/door_toggle_9` |
| M5R | `sim/flightmodel2/misc/door_open_ratio[9]` | `sim/flight_controls/door_toggle_10` |

Both the interior assemblies and exterior fuselage skins follow these same
ratios. Offline OBJ8 inspection finds between 18,524 and 19,437 animated
interior triangles and between 3,405 and 3,504 animated exterior triangles per
door. The click object reuses each source `ANIM_DOOR_*_CLICK` mesh and exposes a
descriptive hover sign.

The hit meshes remain at their authored closed positions after a door opens.
The native `door_toggle_1` through `door_toggle_10` commands remain bindable for
closing from another view. Pressure interlocks, arming/disarming, slide logic,
sounds, and Airbus door-system indications are not implemented.

## Upper-deck and cockpit doors

The six exterior upper-deck door leaves and their handle click targets now use
independent native X-Plane door channels:

| A380 door | Ratio dataref | Click/bind command |
| --- | --- | --- |
| U1L | `sim/flightmodel2/misc/door_open_ratio[10]` | `sim/flight_controls/door_toggle_11` |
| U1R | `sim/flightmodel2/misc/door_open_ratio[11]` | `sim/flight_controls/door_toggle_12` |
| U2L | `sim/flightmodel2/misc/door_open_ratio[12]` | `sim/flight_controls/door_toggle_13` |
| U2R | `sim/flightmodel2/misc/door_open_ratio[13]` | `sim/flight_controls/door_toggle_14` |
| U3L | `sim/flightmodel2/misc/door_open_ratio[14]` | `sim/flight_controls/door_toggle_15` |
| U3R | `sim/flightmodel2/misc/door_open_ratio[15]` | `sim/flight_controls/door_toggle_16` |

The source fuselage supplies separate upper-door and handle meshes but no
animation clips. `blender/synthetic_rigid_animations.py` therefore creates a
reviewable rigid hierarchy for each door: a 0.55 m outward plug movement over
the first 20 percent of travel, followed by a 1.3 m forward translation while
the leaf remains parallel to the fuselage. This follows the visible path of
the authored main-deck doors instead of turning the complete upper-door
mechanism edge-on. The exterior leaves and handles are exported two-sided so
their cabin-facing backs do not disappear when open.

The cabin source also contains a complete fixed U1L opening surround, but it
was assigned the source's generic fallback material alongside unrelated
objects. `tools/prepare_upper_deck_door_surrounds.py` selects its two authored
jamb/header meshes (2,216 source triangles) and five final inner door skins,
including the lower slide-pack face (6,377 triangles). The overlapping
`.004_BACKUP` skin is excluded.
The Blender conversion mirrors and aligns both groups to U1L/R, U2L/R, and
U3L/R. The frame stays fixed; the inner skins follow the same native channels
and motion as the exterior leaves.

The source also contains 18 unparented upper-door hardware meshes: selected
`PAX_DOOR_M3L.033` through `.049` nodes and two click/handle nodes. These had
previously been exported only at U1L, static and single-sided. The conversion
now copies them to all six stations: `.036`, `.043`, and `.048` stay on the
frame (5,555 triangles per station), while the other 15 nodes travel with the
door (12,352 triangles). All upper-door frame, skin, and hardware surfaces are
explicitly two-sided. This restores the closed interior face and prevents the
hardware from floating in an open doorway.

The source has a further missing cabin-wall section beside each doorway;
the aft gap at U1 is approximately 1.22 m wide. `blender/doorway_liners.py`
adds a fixed curved liner from the measured cabin-wall edges to the jamb and
ceiling. It keeps the portal clear, mirrors to the right-hand side, and stops
before the neighboring windows at U2 and U3. The liner profiles and extents
are explicit in `config/animation_datarefs.json` and are generated into the
same surround attachment. These are plain lining surfaces matching the
source's untextured upper-door panels, not new photographic textures.

The cockpit door keeps its authored `COCKPITDOOR_OPEN_ANIM` clip and follows
`sim/flightmodel2/misc/door_open_ratio[16]`. Its authored click mesh calls
`sim/flight_controls/door_toggle_17`. The detailed cockpit rebuild uses LOD00
because the LOD01 source had flattened this door into static geometry.

Cargo doors remain static. Pressure interlocks, arming/disarming, slide logic,
sounds, and Airbus door-system indications are not implemented.

## Verification

The October 4 door repair produced 104 attached OBJ files, 8,451,401 vertices,
and 10,003,972 triangles. The installed upper-door assembly check passed with
zero errors; each station has 2,216 fixed frame triangles, 66 lining triangles,
6,377 moving inner-panel triangles, and separately verified hardware. The
closed/open previews use the installed OBJ files. Full-aircraft validation
must also be rerun after the concurrent FCU/provider integration; that separate
contract was not yet consistent during this cabin refresh. Native
mouse hit testing, upper-door clearance, door timing, and view-dependent cabin
inspection still require an in-simulator check. Run
`tools/verify_upper_door_assemblies.py` against the packaged surround, passenger
door, and exterior fuselage objects to check all six complete assemblies at
five positions, including independent motion, stationary frames, two-sided
surfaces, and matching interior/exterior displacement.

For this package, pass `--liner-triangles U1L=66` for each of U1L, U1R, U2L,
U2R, U3L, and U3R. The results are recorded in
`build/reports/upper-door-assemblies-packaged.json`; the closed and open views
are `upper-door-final-0.png` and `upper-door-final-1.png` in the same directory.
