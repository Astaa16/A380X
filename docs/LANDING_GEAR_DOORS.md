# Landing-gear door animations — 2026-10-04

The fuselage export now animates every landing-gear door clip present in the
pinned glTF from native X-Plane gear deployment ratios. This is an offline-checked visual
sequence; it does not port the FBW hydraulic actuators, timing, faults, or
door/gear interlocks.

## Mapping and sequence

| FBW clip | Visible mechanism | X-Plane input | Gear-up | Transit | Gear-down |
| --- | --- | --- | --- | --- | --- |
| `c_gear_door1` | Forward nose doors | `deploy_ratio[0]` | Closed | Open | Closed |
| `c_gear` | Aft nose doors and nose-gear deployment | `deploy_ratio[0]` | Closed | Moving | Open |
| `l_wing_gear_door` / `r_wing_gear_door` | Large wing-gear doors | `deploy_ratio[1]` / `[2]` | Closed | Open | Closed |
| `l_body_gear_door` / `r_body_gear_door` | Large body-gear doors | `deploy_ratio[3]` / `[4]` | Closed | Open | Closed |
| `l_blg_inner_door` / `r_blg_inner_door` | Auxiliary body-gear doors | `deploy_ratio[3]` / `[4]` | Closed | Moving | Open |

All paths use the `sim/flightmodel2/gear/` prefix. The primary-door rules
sample the source closed pose at deployment 0 and 1, and the fully open pose at
0.12 and 0.88. The doors therefore open early, stay open through most of leg
travel, and close near the opposite endpoint during either extension or
retraction. The auxiliary body doors follow the full 0..1 clip and remain open
with their gear extended, matching the source system's endpoint behavior. The
aft nose doors were already part of the combined nose-gear clip and also remain
open when extended.

`blender/animation_sampling.py` supports these non-monotonic mechanisms with
explicit `(source_fraction, dataref_value)` sample points. Export frames may
revisit an authored source pose, but dataref keys must be finite and strictly
monotonic so the generated OBJ8 tables remain valid. No custom dataref, Lua
provider, or control override is introduced.

## Offline evidence

The corrected export has 27 animated fuselage targets rather than 15, and 12
fallback mappings rather than 24. The twelve newly animated bone targets cover
all seven previously omitted door clips.

At the fully open transit pose, the exported OBJ8 paths move:

- 2,964–3,698 nose-door triangles, depending on nose-leg progress, with
  1.74856 m maximum vertex travel;
- 1,721 left and 1,867 right wing-door triangles, with 2.71528 m maximum
  travel; and
- 1,362 triangles on each body-gear side, with 2.68960 m maximum travel.

At deployment 1, both large wing-door groups return to their closed source
pose within floating-point precision. On each body side only the intended
197-triangle auxiliary door remains displaced. The nose group retains 734
moving aft-door triangles while its forward doors return closed. Orthographic
underside previews confirm left/right symmetry and the intended open and
gear-down poses.

Evidence under `/Volumes/MacGames/A380X-XPlane12-build/reports/`:

- `fuselage-gear-doors-candidate.json`: conversion targets, sample tables, and
  emitted OBJ checks;
- `gear-doors-{nose,left-wing,right-wing,left-body,right-body}-motion.json`:
  strict OBJ8 motion evaluation with every other dataref explicitly fixed;
- `gear-doors-main-transit-underview.png`,
  `gear-doors-main-down-underview.png`, and
  `gear-doors-nose-transit-underview.png`: offline previews, not simulator
  screenshots; and
- `gear-doors-package-validation.json`: installed package validation with zero
  errors. Its warnings are the existing global aileron-driver requirement and
  missing custom FMOD bank.

The checked export was promoted to `xplane-obj/fuselage-corrected` and refreshed
into the SD-card aircraft package. The previous fuselage export is recoverable
at `xplane-obj/fuselage-before-gear-doors-20261004`. Validation covered 206
pure-Python tests. X-Plane runtime observation remains required before treating
the timings or clearances as flight-accepted.
