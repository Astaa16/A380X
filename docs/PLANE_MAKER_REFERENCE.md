# Plane Maker reference sheet

This sheet extracts a small, reviewable set of A380-842 values from the pinned FlyByWire source. It is input material for authoring and tuning an X-Plane 12 `.acf`; it is not a substitute for Plane Maker work or flight-test validation. The machine-readable copy is `config/aircraft_reference.json`.

## Identification and operating limits

| Item | Reference value |
|---|---:|
| Variant / ICAO | A380-842 / A388 |
| Engines | 4 × Trent 972B-84 |
| MTOW | 510,000 kg |
| Maximum landing weight | 395,000 kg |
| Maximum zero-fuel weight | 373,000 kg |
| Empty/reference minimum ZFW | 300,007 kg |
| Maximum fuel | 323,546 kg |
| MMO | Mach 0.89 |
| Range reference | 8,000 nm |
| Clean load-factor envelope | −1.0 to +2.5 g |
| Flaps load-factor envelope | 0.0 to +2.0 g |

The airframe JSON contains `maxGw = 512000 kg`, while the performance envelope and MSFS flight model use 510,000 kg MTOW. The table uses 510,000 kg and records the mismatch for later review.

## Geometry and propulsion

| Item | Reference value |
|---|---:|
| Length | 72.72 m |
| Wheelbase | 31.9 m |
| Wing area | 845.046 m² |
| Wing span | 79.751 m |
| Root chord | 17.941 m |
| Sweep / dihedral / incidence | 33.5° / 5.6° / 2.0° |
| Root-to-tip twist | −5.5° |
| Horizontal-tail area / span | 148.924 m² / 30.358 m |
| Vertical-tail area / span | 260.166 m² / 14.591 m |
| Static thrust | 356.805 kN per engine |

The source moment-of-inertia values convert to approximately 82.05 million kg·m² pitch, 72.62 million kg·m² roll, and 119.62 million kg·m² yaw. Treat these as starting values because each simulator defines mass distribution and axes differently.

## Primary control references

The source limits are ±30° elevator, ±30° aileron, ±30° rudder, 50° ground spoiler, and 45° airborne spoiler. Stabilizer trim is 10° nose-up and 2° nose-down in the source configuration. X-Plane geometry and the Lua controller must agree on sign, travel, hinge placement, and surface count before the override is enabled for flight tests.

## Coordinate warning

The FBW/MSFS contact-point tuples are documented as longitudinal `z`, lateral `x`, vertical `y`, in feet. Plane Maker uses a different entry layout. Do not paste the source tuples directly: convert units, remap axes, check signs, and visually verify the gear against the exported OBJ origin. The five wheel-group reference points are retained verbatim in the JSON file so this conversion is explicit and auditable.

## Pinned source files

- `fbw-a380x/src/base/flybywire-aircraft-a380-842/config/a380x/a380-842/airframe.json5`
- `fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/common/config/aircraft.cfg`
- `fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/common/config/flight_model.cfg`
- `fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/common/config/engines.cfg`

All values above were extracted from FlyByWire revision `2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2` and should be rechecked if the upstream revision changes.
