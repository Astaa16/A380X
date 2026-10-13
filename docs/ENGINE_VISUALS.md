# Engine materials and animation

The engine pass preserves the five existing engine attachments and the original
FBW paintwork, UVs and texture resolution. Albedos remain 4096×4096; normal and
authored material maps remain 2048×2048. It is installed into the local aircraft
package and retained in the canonical wing export directory.

## Materials

`tools/prepare_engine_materials.py` rebuilds the engine material maps directly
from the pinned KTX2 assets. Normal green is inverted from MSFS DirectX tangent
convention to X-Plane. The two nacelle maps use 30% tangent-XY amplitude to
retain seams without the source's heavy broad dents on painted cowling; fan
and core normals retain full amplitude. The existing glTF importer has
already converted UV V to `1−V`; bitmap rows are not flipped a second time.

The material map uses `max(10, COMP.blue)` for F0 and `255−COMP.green` for gloss.
All five engine objects enable `NORMAL_METALNESS`, `GLOBAL_specular 1`, and
unit `ATTR_shiny_rat`, preserving the source's distinction between painted
surfaces and metal. The right core has no authored COMP asset or binding, so
its map uses glTF's default metallic=1, roughness=1 rather than borrowing the
left core's incompatible texture layout. Thai Airways nacelle overrides retain
their paint, metal masks and gloss; only the dielectric F0 floor is restored.

Preparation updates the wing manifest for subsequent exports. The full exterior
export wrapper runs this step, and the Thai livery converter uses the same
engine F0 floor. Existing albedo pixels remain unchanged.

The local package build also reapplies the engine material pass directly from
source on full builds and wing refreshes. The final 30% nacelle-normal pass was
installed using `--package-only` because the MacGames build image refused new
writes. The installed package has the final finish; the cached build texture
PNGs retain the preceding full-strength version until preparation can run on
a writable volume. Superseded decoded normals were preserved under
`build/engine-update-20261006/superseded-decoded-normals` before retiring those
unused cache files. No source KTX2 assets were removed.

References: [X-Plane 12 material model](https://developer.x-plane.com/article/the-x-plane-12-material-model/),
[OBJ8 texture channels](https://developer.x-plane.com/article/obj8-file-format-specification/),
[MSFS conventions](https://docs.flightsimulator.com/html/Asset_Creation/3D_Models/General_Principles.htm).

## Movement

The four authored `N1_1_anim` through `N1_4_anim` clips drive the fan blades and
spinners from native `sim/flightmodel2/engines/prop_rotation_angle_deg[0..3]`.
The local simulator DataRefs description explicitly identifies this value as
the propeller or engine-fan visual angle. Rotation follows the simulator's
per-engine movement rather than a wall-clock animation.

`blender/normalize_engine_fan_rotation.py` collapses the verified quarter-turn
Euler tables into the authored fixed-axis revolution. This avoids interpolation
from +180 to −90 degrees and preserves the initial fan orientation.

The source `thrust_rev_1` and `thrust_rev_2` clips follow native
`sim/flightmodel2/engines/thrust_reverser_deploy_ratio[1]` and `[2]`. These are
engines 2 and 3. No exterior reverser animation reads index 0 or 3.
The original sleeves, blocker doors and their linkages provide the movement.
OBJ8 does not support animated scale: internal telescoping actuator rods retain
their rigid geometry while the authored translations and rotations are exported.
The main sleeves translate about 0.8 m; exact source rod extension remains a
converter limitation.

`tools/prepare_engine_animation_subset.py` allows a focused export of the five
engine materials using the existing normalized wing source. It retains the
source nodes, animation channels, skins and geometry buffers while excluding
unrelated wing mesh draws. The six animation mappings also apply to future
full wing exports.

The exporter prunes empty animation branches from these five engine variants.
This removes inherited wing/aileron references with no affected triangles.
Engine attachments therefore depend only on their native fan and reverser
datarefs. Pruning preserves geometry, materials and all effective draw matrices.

## Verification boundary

`tools/audit_engine_animations.py` verifies all 350,828 engine triangles,
including each corner UV and the oriented triangle multiset after any index
reordering. Maximum neutral geometry drift is 0.022 mm. Each fan dataref moves
only its 24,760 fan/spinner triangles at the correct physical nacelle; octant
samples check forward, fixed-axis rotation and exact full-turn closure. Each
inboard reverser moves 8,124 triangles; outboard engines and fan geometry stay
fixed. The installed comparison and pose results are saved in
`build/engine-update-20261006/installed-animation-verification.json` and
`fan-physical-index-verification.json`.

All 30 focused regression tests pass, including engine motion, material
conversion, safe file replacement, empty-branch pruning, OBJ key ordering and
preview evaluation. `final-engine-validation.json` verifies the installed
source-derived normals and native-only datarefs. The October 7 final whole
package check (`package-final-validation.json`) reports zero errors, with the
existing Lua-runtime and missing custom-FMOD-bank warnings. Blender images are offline previews, not
X-Plane screenshots. Reload the aircraft for native visual acceptance. This
pass does not add fan blur discs or alter thrust physics, reverser interlocks,
or cockpit control behavior.
