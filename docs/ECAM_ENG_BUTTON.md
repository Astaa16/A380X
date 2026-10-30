# ENG: native 3D ECAM pushbutton test

Scope: ENG only. The remaining ECAM buttons retain their existing behavior until
the ENG prototype has passed an actual-cockpit mouse/animation test. No backup is
requested or created by the installation command below. This is an aircraft-local
native C/X-Plane OBJ implementation, not the website mockup and not a Lua-only
animation.

## Installed milestone — 2026-10-19

Installed to `output/A380X-XP12` from `build/eng-button-cockpit-20261019-v3`,
while X-Plane was closed, using `--no-backup`. The installer reported
`backup: null`. Installed plugin SHA-256:
`3a2ef09606bb2a1294697cb1a60b2d9600102d596ca931355045e94194738b6d`.

Both native executable signatures, all four installed ENG OBJ hashes, the two
selection-atlas hashes and all 173 native source records match the final build.
ENG's body and both face layers are now actually enclosed in OBJ translation
animations, with a separately controlled green emissive face on the physical
panel. These are installed simulator assets, not a browser-only preview.

The full native build passed, including 171 SD-adapter checks, 2,316 native SD
render checks, 451 MFD-adapter checks and 2,107 MFD-UI checks. Additional Python
tests passed: eight ENG geometry, twelve native-asset reuse, six existing SD
geometry, five installer-write and 36 existing cockpit-control tests.
Installed aircraft static validation also passed with zero errors. Its two
pre-existing warnings concern the global FlyWithLua aileron visual driver and
the missing custom FMOD sound bank; neither is an ENG button dependency.

Actual-cockpit acceptance is still **pending user observation**. No claim is made
that a browser test, a command/API test or static validation proves the ENG mouse
click or visible press-and-return. Do not expand to HYD until this test passes.

## Inspection and staged implementation plan

The connected project is `/Volumes/Untitled/A380X-XPlane12/xplane-port`.
The installed simulator's `Log.txt` identifies X-Plane 12.4.3-r2, build 124311.
The aircraft's SDK headers under `build/sdk/SDK/CHeaders/XPLM` were checked against
the official SDK documentation. No authored `.blend` file was found in this
project inspection; the available model source is the FBW cockpit glTF and the
existing exported cockpit OBJ files. Do not describe this change as an edit to
an unavailable Blender project.

1. Inspect the real ENG mesh, child legends/selection face, authored translation
   track, exported draw ranges, textures and existing click target.
2. Add the reusable native pulse controller and test one ENG command path, frame
   timing, repeated clicks, automatic return and safe resets.
3. Animate only ENG's existing visible body and child faces in their current OBJ
   files. Preserve vertex/index tables, UVs, textures and mounting geometry.
4. Add the independent green selection face driven by the actual lower SD state.
   Keep the existing compact, command-based cockpit click target.
5. Build and test the plugin/geometry, then install while X-Plane is closed with
   `--no-backup`. Check installed file hashes and package validation.
6. In X-Plane, perform the physical ENG click acceptance test below. API commands
   and static geometry tests alone do not demonstrate mouse picking or the visual
   result in the user's cockpit.
7. Only after that test, apply the component to HYD and other page buttons using
   each button's measured mesh/axis and intended selection logic. ALL, CLR/RCL,
   C/L, ABN PROC, MORE and brightness controls are not ordinary page selectors;
   their command semantics and illumination require individual review.

## Reusable controller and actual data contract

The reusable motion component is `native/avionics/a380x_ecam_button.h`; its
`EcamButtonConfig`/`EcamButtonMotion` configuration keeps button identity, label,
page and timing separate from runtime motion. ENG is configured with a 0.10 s
press and 0.15 s return. The physical distance is the geometry's authored stroke,
approximately 0.624 mm, not a change to the normalized animation state.

The native bridge is `native/avionics/a380x_sd_adapter.inc`:

| Kind | Exact name | Purpose |
| --- | --- | --- |
| Existing command | `a380x/xp12/ecam/sd/engine` | Select/display ENGINE and trigger one physical pulse on Begin |
| New read-only float dataref | `a380x/xp12/ecam/buttons/eng/press_ratio` | 0 = rest, 1 = full inward stroke; returns automatically |
| New read-only float dataref | `a380x/xp12/ecam/buttons/eng/selected` | 0/1 persistent green indication from actual SD state |
| Existing read-only dataref | `a380x/xp12/ecam/sd/active_page` | Current SD page; ENGINE = 0 |
| Existing diagnostic datarefs | `a380x/xp12/ecam/sd/action_count`, `.../last_action` | Observe command arrival; ENGINE action = 0 |

These custom names are plugin outputs registered with `XPLMRegisterDataAccessor`,
not assumed stock simulator datarefs. The outputs are read-only, with a float read
callback and no write callback. Initialization detects name collisions, and
shutdown unregisters the accessors.

The existing command handler executes the page action only on
`xplm_CommandBegin`. Continue and End do not repeat the ENG action or hold its mesh
down. Motion uses a smoothstep inward segment followed by a smoothstep return;
elapsed seconds, not a fixed per-frame increment, advance the state. Time left
over at a segment boundary is carried into the next segment. Rapid new clicks
restart from the current physical position, without jumping back to rest, while
each new Begin still executes one action. Aircraft reload, plugin disable and
shutdown clear transient motion. Invalid timing inputs are handled safely.

Motion updates every flight-loop call before the SD renderer's 0.25 s data-update
early return. The elapsed time supplied is time since this plugin's callback,
not time since an arbitrary other plugin's flight-loop callback. No texture
capture or browser mouse transport is needed to move this 3D button.

The green output is independent of `press_ratio`: it is on when this plugin and
the SD are powered, ENGINE is the actual selected page, and no checklist or
abnormal overlay replaces that page. Repeated ENG selection preserves the
existing manual/AUTO-selection behavior; if ENGINE remains the displayed page,
its indicator remains lit. Selecting another page, powering down, or showing a
replacement checklist/abnormal page extinguishes it. Initialization and ENG
activation produce concise `XPLMDebugString` diagnostics rather than logging on
every cursor move or animation frame.

## Actual mesh and Blender setup

The source is
`/Volumes/Untitled/A380X-XPlane12/upstream-aircraft-large-files/fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire/Part_Interior_Cockpit/model/A380_COCKPIT_LOD00.gltf`.
Its ENG nodes are:

- `PUSH_ECAM_ENG`: movable body; material
  `A380_COCKPIT_PEDESTAL_KNOBS02_4k`.
- Child `PUSH_ECAM_ENG_SEQ1`: authored legend/emissive face; material
  `A380X_COCKPIT_DECAL_EMIS`.
- Child `PUSH_ECAM_ENG_SEQ2`: authored low-visibility selection face; material
  `A380X_COCKPIT_DECAL_LOVIS`. The FBW ECAM panel model definition identifies SEQ2
  with its page-selection emissive state; it is not inferred solely from its name.

The authored parent translation endpoints in glTF coordinates, in meters, are:

```text
rest     (0.089665152133, 2.401948213577, 32.311965942383)
pressed  (0.089665152133, 2.401326417923, 32.312015533447)
delta    (0, -0.000621795654, +0.000049591064)
```

The parent's quaternion is `(-0.039500929415, 0, 0, 0.999219596386)`;
the stroke is aligned with its local **-Y**, normal to the button face, not an
arbitrary global vertical translation. The original glTF animation uses STEP;
the native pulse supplies the smooth inward/return timing instead.

For subsequent Blender authoring:

1. Import the supplied glTF at meter scale. Retain the parent transform and both
   child face offsets; do not reset their transforms independently. If an exporter
   requires applied transforms, preserve world positions and re-derive the stroke
   from the two transformed endpoints rather than copying an axis by eye.
2. Keep the stationary ECAM panel/housing separate. Put the movable body and its
   legend/selection faces in one button-motion group. Use an Empty or body origin
   at the button's rest center as a clear local reference; translation itself does
   not depend on a rotation pivot.
3. Align the motion group's local pressing axis with the authored local -Y. Keep
   scale uniform. Translate the whole moving group through the measured 0.624 mm
   stroke; do not rotate the cap or move surrounding mounting parts.
4. Bind the export's translation endpoints to `press_ratio` values 0 and 1.
   Blender timeline frames describe the dataref mapping, not the required real
   time of the click. The native controller supplies real-time 0.10/0.15 s timing.
5. Keep the green face as a separate emissive/selection component, parented to the
   moving cap so it follows its physical stroke. Bind its illumination to
   `selected`, not to `press_ratio` and not to the mouse-held state.
6. Use a simple separate hit mesh for the normal command manipulator. Do not turn
   the entire detailed button/panel into a drag manipulator or interactive mesh.
   Export to the existing cockpit-interactive object; do not add a second ACF
   cockpit-interactive attachment.

This implementation patches the actual exported OBJ draw groups directly, using
the inspected source motion and existing textures; Blender is not required at
runtime. A Lua callback alone would not move this existing static geometry.

## OBJ manipulator, animation and indicator

The ordinary mouse path remains a command manipulator on the compact invisible
ENG target in `a380x_cockpit_controls.obj`:

```text
ATTR_draw_disable
ATTR_manip_command hand a380x/xp12/ecam/sd/engine ECAM - ENG
TRIS <existing compact target start> 36
ATTR_manip_none
ATTR_draw_enable
```

That target receives a normal click; there is no drag axis, drag distance or
mouse-hold requirement. The detailed visible meshes are not click-tested.

The moving body and both child draw ranges are enclosed in an animation block;
the generated file uses explicit dataref keyframes:

```text
ANIM_begin
ANIM_trans_begin a380x/xp12/ecam/buttons/eng/press_ratio
ANIM_trans_key 0 0 0 0
ANIM_trans_key 1 0 -0.000621795654 0.000049591064
ANIM_trans_end
TRIS <original ENG draw start> <original ENG draw count>
ANIM_end
```

`tools/ecam_button_geometry.py` creates the patch. Its optional `press_distance`
configuration rescales the authored inward vector without changing its axis;
the inspected default stroke is retained for this first test. It refuses an
ambiguous source grouping, mismatched draw ranges, an outward-facing stroke or
a changed stroke on an already-patched aircraft. Reapplying the same patch is
a byte-identical no-op.

In these existing cockpit OBJ files, exported coordinates retain that source
vector; the aircraft's existing 180-degree attachment rotation is left intact.
The generator checks the exact authored draw ranges before writing:

- `objects/interior/cockpit-panels/panel_a380_cockpit_pedestal_knobs02_4k.obj`:
  body, 1,008 existing indices.
- `objects/interior/cockpit-panels/decal_a380x_cockpit_decal_emis.obj`:
  SEQ1 legend, six existing indices.
- `objects/interior/cockpit-panels/decal_a380x_cockpit_decal_lovis.obj`:
  SEQ2 selection face, six existing indices.

Do not copy this vector into a differently oriented Blender/OBJ attachment
without converting both endpoints to that attachment's coordinate system.

The green face uses an ordinary textured emissive mesh, not an SD cockpit-device
texture. It appends two non-interactive triangles to the existing cockpit-controls
object, with the existing green atlas cell sampled at UV `(0.0625, 0.6875)` and
`TEXTURE_LIT fcu_labels_LIT.png`. The overlay uses the authored SEQ2 face, about
8.063 by 2.342 mm, lifted outward 0.06 mm to avoid z-fighting; no duplicate button
body, new aircraft attachment or ACF change is needed.
Within the same physical animation, `ATTR_light_level 0 1
a380x/xp12/ecam/buttons/eng/selected` drives that face's lit contribution; its
unselected overlay is hidden so green albedo does not remain visible when off.
Attributes are restored after the face. The original body/legend UVs and
textures remain unchanged. In modern HDR/PBR rendering, do not rely solely on
legacy `ATTR_emission_rgb`; a lit texture supplies the actual emissive content.

## Build, install without backup, and test

The install path is the existing `output/A380X-XP12` aircraft reached by the
simulator's `Aircraft/A380X-XP12` link. The ENG build stages in
`build/eng-button-cockpit-20261019-v3` on the external drive. The initial
internal-disk build ran out of free space and was not installed. Its partial
generated output was moved to `build/a380x-eng-20261019-build-01`; it is not an
installable release. From the project root, the current build command is:

```sh
python3 native/avionics/build.py \
  --sdk build/sdk/SDK \
  --output-dir build/eng-button-cockpit-20261019-v3 \
  --datarefs "/Users/sava/Library/Application Support/Steam/steamapps/common/X-Plane 12/Resources/plugins/DataRefs.txt" \
  --xpkit \
  --xpkit-assets output/A380X-XP12/plugins/A380XAvionics/mac_x64/xp-kit \
  --ecam-source "/Volumes/Untitled/A380X-XPlane12/upstream-aircraft-large-files/fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire/Part_Interior_Cockpit/model/A380_COCKPIT_LOD00.gltf" \
  --eng-button
```

The build requires a fresh output directory. Use a new path for any later
rebuild; do not delete or reuse the validated staging directory casually.
`--eng-button` stages the inspected ENG body/face animation and selection light.
`--xpkit-assets` reuses the 28 already-installed native display assets, verifying
their bytes against the prior installed build report before copying them into
the new stage. The plugin and native WebKit helper are compiled afresh. This
native-code/geometry change therefore does not depend on the deleted JavaScript
development workspace or require rebuilding the website. No preview fixtures or
synthetic values are substituted for live native display assets.

Native SD/controller tests pass 171 checks, including ENG's automatic return,
separate selected output, immediate-release/missing-End handling, repeated clicks,
power/reload/disable resets, and equal elapsed-time behavior across frame rates.
The final build and installation are confirmed in the milestone above; these
controller tests do not prove the visible mouse path. Package validation is saved
as `build/eng-button-cockpit-20261019-v3/installed-validation.json`.

Validate without changing the running aircraft, then quit X-Plane before
installation. With a successfully validated staged build:

```sh
python3 tools/install_xp_kit_cockpit.py build/eng-button-cockpit-20261019-v3 --check-only --no-backup
# Only after X-Plane has quit:
python3 tools/install_xp_kit_cockpit.py build/eng-button-cockpit-20261019-v3 --no-backup
```

The installer refuses replacement while X-Plane is running and checks staged
binary/asset hashes and signatures. `--no-backup` explicitly suppresses the
backup copy. Do not install an older or partially failed staging directory.
Relaunch X-Plane and load `Aircraft/A380X-XP12/A380X-XP12.acf`; restarting macOS
is not required. The website need not be open or running.

Test in a stationary, powered aircraft, using the actual cockpit ECP and no
ECAM popup over the panel:

- Select a different SD page first. Click ENG once and immediately release;
  do not drag. ENGINE must appear exactly once.
- Observe the actual ENG cap and both face layers move inward, without sideways
  motion, clipping into the wrong location, duplicating or detaching. The cap
  must return automatically in about 0.25 s total even if the mouse moves away.
- Confirm the green ENG indication remains on after the cap returns. Click APU
  or HYD using its current control: the page must change and ENG's light must go
  off. Their own new physical animations/lights are not part of this ENG-only
  milestone.
- Re-click ENG at least ten times, including rapid clicks and moving away just
  after clicking. Each Begin must cause one action; no button may become stuck.
- Hold ENG down for one second: it must still complete one physical pulse and
  one activation without waiting for release. Release and click again.
- Repeat at low and high available frame rates. Motion duration must be based
  on elapsed seconds; no system can display intermediate frames during an actual
  long rendering stall, but the cap must return to the correct position afterward.
- Reload the aircraft and toggle plugin enable only while stationary. No stale
  press may survive reload/disable, and the light must follow the restored page
  and power state rather than an old click.
- Confirm cursor movement over the panel does not trigger repeated actions or
  expensive render captures. The compact ENG target must stay clickable.
- Check `Log.txt` for successful ENG motion/ref registration and an ENG event.
  Diagnostic API values can confirm command/state routing, but are not proof
  that the visible cockpit click/animation passed.

Acceptance remains **pending human observation in the actual simulator** until
the complete first-click sequence, green-light persistence and off-on-other-page
behavior have been observed. No browser screenshot or command-only test is
substituted for that check. HYD/all-panel acceptance comes after the ENG milestone.

## Verified primary references

- [Laminar OBJ8 specification](https://developer.x-plane.com/article/obj8-file-format-specification/):
  translation blocks, command manipulators, light-level attributes and lit textures.
- [Laminar manipulator guide](https://developer.x-plane.com/article/manipulators/):
  command-click behavior, separate from axis-drag manipulators.
- [SDK command phases](https://developer.x-plane.com/sdk/XPLMCommandPhase/) and
  [command callback](https://developer.x-plane.com/sdk/XPLMCommandCallback_f/):
  Begin/Continue/End and callback return semantics.
- [SDK custom dataref registration](https://developer.x-plane.com/sdk/XPLMRegisterDataAccessor/):
  registered float getters and read-only accessor setup.
- [SDK processing/timing](https://developer.x-plane.com/sdk/XPLMProcessing/):
  callback elapsed-time meanings and scheduling; no drawing from the flight loop.
- [Laminar additive lighting](https://developer.x-plane.com/article/additive-lighting/):
  emissive texture illumination rather than a mouse-state tint.
