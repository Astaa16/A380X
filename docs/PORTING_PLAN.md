# Porting plan and technical boundary

## Source and license boundary

The FlyByWire aircraft repository states that original source code is GPL-3.0, original 3D assets are CC BY-NC 4.0, and any assets governed by Microsoft's Game Content Usage Rules retain those rules. Keep this port GPL-3.0, preserve attribution, keep it non-commercial, and audit every model/texture before redistribution. A locally converted file is not automatically safe to publish.

Pinned inputs:

- `flybywiresim/aircraft`: `2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2`
- `flybywiresim/aircraft-large-files`: `5f07408bfec09c66dd6b5c5e484027f64664fb43`
- XPlane2Blender syntax checked against `ee03ed6a96601199d7bccf3d65fb1378664e828e`
- `mSparks43/747-400` ACF/airfoil scaffold: `041c9ffa4b2b0c72a4aad36643759b7cace836c0` (CC BY-NC-SA 4.0); its visual objects and cockpit were removed

## Conversion map

| MSFS/FBW component | X-Plane 12 destination | State |
|---|---|---|
| glTF meshes/materials | Blender, then X-Plane OBJ8 | Main fuselage/wing/engine exterior exported; remaining materials and gear parts pending |
| glTF time animations | XPlane2Blender dataref keyframes | Automated name mapping with a review report |
| TypeScript logic primitives | FlyWithLua custom datarefs | Confirmation and S/R memory patterns ported |
| FWS flight phases | `a380x/xp12/fws/flight_phase` | Simplified, testable skeleton |
| C++/Simulink flight controls | X-Plane surface override | Experimental Lua G-command and roll-rate law |
| MSFS aircraft/flight-model CFG | Plane Maker `.acf` | Development A388 model built and runtime-loaded; aerodynamic tuning and flight test pending |
| MSFS SDK/Coherent displays | Aircraft-local native plugin with a simulator-independent translated core | Captain and first-officer PFD/ND faces are owned by `A380XAvionics`; the pinned FBW FMA priority and FWS flight-phase primitives are translated to C. X-Plane EFIS mode and range datarefs carry physical selector positions, while stock map/FD/VNAV/GPSS mode logic is absent. FMGC/FMS/nav-database/ARINC producers and the other display units are not yet ported. |
| FlyByWire React flyPad EFB | Native `A380XAvionics` panel renderer plus guarded X-Plane boundary adapter | FBW navigation/page model, A380 limits, payload/fuel/ground/radio/failure/checklist/preset functions, battery timing and TOD calculator translated; both 3-D tablets show the shared physical surface with 34 touch targets. MSFS-only external services remain unavailable. |
| Wwise/MSFS sounds | X-Plane FMOD sound bank | Initial SND/event map, inventory, package gating, and tests ready; compiled bank and licensed recordings pending |
| MSFS interactions/behaviors | X-Plane commands/manipulators | Not ported |

## Milestones to a flyable aircraft

1. Tune and validate the current A380-842 aerodynamic and propulsion model against licensed references and test cards.
2. Finish the remaining exterior materials and gear parts, then optimize OBJ/texture size.
3. Map every animation to an X-Plane dataref or a published custom dataref; eliminate fallback mappings.
4. Define gear geometry, steering, brakes, control surfaces, slats/flaps, spoilers, and stabilizer in Plane Maker.
5. Tune the control law against test cards: ground checks, slow flight, clean/high-lift G limits, bank response, flare, engine-out, and override release.
6. Port aircraft systems in bounded modules. Preserve units, validity states, update rates, and failure behavior.
7. Rebuild displays against an X-Plane-supported rendering stack; FBW MSFS instruments cannot execute unchanged.
8. Create FMOD sounds, cockpit regions, manipulators, lighting, rain/wiper data, icons, and liveries.
9. Test on macOS arm64/x86_64 and Windows x64 with X-Plane 12, then perform the license audit before release.

## Flight-control safety notes

The override writes all primary surface groups it assumes responsibility for and releases the override whenever the target A380 is not loaded. X-Plane documentation notes that an aircraft using `override_control_surfaces` must also directly actuate the trimmable horizontal stabilizer. Stabilizer/trim control is intentionally not enabled yet because its sign, range, rate, and Plane Maker geometry must be established first.
