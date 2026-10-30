# Center right panel in the X-Plane port

The aircraft's center right panel uses the FBW cockpit meshes, aircraft-local
commands and datarefs under `a380x/xp12/center_right/`, and the native avionics
plugin. The aircraft installed as `Aircraft/A380X-XP12` points at this project's
`output/A380X-XP12`. Clock controls use `a380x/xp12/clock/`; see
[CENTER_RIGHT_CLOCK.md](CENTER_RIGHT_CLOCK.md).

## Gear indications and lever

The lever commands X-Plane's gear handle, except while the L/G GRVTY gravity
extension isolates the gear hydraulics (one or more handle turns): the lever
then moves without commanding the gear and `gear_lever` follows the crew lever,
see [LG_GRVTY.md](LG_GRVTY.md). Five independent native deploy
ratios drive nose, left/right wing and left/right body indications. Each lamp
is green when downlocked with the lever DOWN, dark when uplocked with the lever
UP, and red UNLK in transit or when its lock state disagrees with the lever.
The port treats ratios at or above 0.99 as downlocked and at or below 0.01 as
uplocked. Nonfinite values cannot indicate a valid lock.

The ACF's 20-second gear cycle is the normal extension/retraction delay. If
any gear has not reached its selected lock after that delay, the EWD displays
`L/G GEAR NOT LOCKED DOWN` or `L/G GEAR NOT LOCKED UP`. Changing the lever starts
a new delay. In landing configuration (flap-handle ratio at least 0.5), either
valid pilot radio-altimeter channel below 750 ft triggers `L/G GEAR NOT DOWN`
and the flashing red arrow if any gear is not downlocked. The arrow is
independent of lever position and alternates on/off every half second.

## Accumulator and brake gauge

The top indication uses the native read-only
`sim/cockpit2/hydraulics/indicators/brake_accumulator_pressure_ratio`, scaled
from 0–1 to the face's 0–5000 PSI range. The amber arc is 0–3840 PSI, the green
arc is 4872–5000 PSI, and the intermediate band is white.

X-Plane exposes one accumulator for this aircraft. There are no independent
left/right BLG accumulator samples, so the indication cannot choose the
lower of two separately modeled accumulator pressures. The bottom two
needle inputs are actual native body-wheel braking fractions from
`sim/flightmodel2/gear/tire_part_brake[3]` and `[4]`; they are **not independent
caliper-pressure sensors**. The exported values are integer thousandths,
0–1000, named `brake_left_ratio_milli` and `brake_right_ratio_milli`.

`accus_reinflate` requests the aircraft's configured native electric hydraulic
pump. The request remains active until the observed accumulator reaches the
green zone or the pump is switched off. The plugin never writes accumulator
pressure; charging still depends on X-Plane's electrical and hydraulic
systems. A pump already on before the request is left on after completion.
An otherwise-off pump started by this request is switched off at completion,
plugin disable or plugin shutdown. This is a native pump adaptation, not a
port of the full FBW BLG accumulator hydraulic circuit.

## Autobrake and RTO

The physical selector has six detents: DISARM, BTV, LO, 2, 3 and HI. LO through
HI use X-Plane's four landing autobrake modes. Their final deceleration
settings are taken from the pinned FBW `A380AutobrakeController`: 2.0, 2.5,
3.0 and 3.5 m/s². The plugin converts those targets to native positive-g
units and restores the prior target array on shutdown. X-Plane supplies
engagement/ramp timing and RTO braking; the complete FBW deceleration
governor has not been ported.

Arming requires anti-skid ON, native normal braking available, and at least
one powered valid attitude/heading channel. Normal-brake availability uses
the FBW BCU pressure hysteresis: either native engine hydraulic feed above
2176 PSI with both pedals below 20% enables it; pressure below 1305 PSI or a
left/right brake failure removes it. The current scaffold uses 3000-PSI
engine hydraulic systems 1/2. Selected ADIRS attitude/heading validity now supplies this requirement; see
[ADIRS](ADIRS.md).

Landing modes can arm on ground or in flight. Losing an arming condition
returns the selector to DISARM and removes the armed/active indication.
Pressing RTO on ground toggles RTO and returns the landing selector to
DISARM; it cannot arm in flight and disarms on liftoff. RTO's physical pressed
state follows command begin/end independently of its ARM light.

The port has no OANS runway-exit BTV guidance. Selecting BTV immediately uses
BRK HI while preserving the BTV detent: `autobrake_selected=1`,
`autobrake_armed=5`, `btv_reverted=1`. This lets the FMA show the effective HI
mode without claiming BTV exit guidance. Native external autobrake selection
and native disarming are reflected back into the cockpit selector.

X-Plane does not publish an autobrake-active flag. `autobrake_active` is a
conservative inference: the aircraft is on ground, a mode is armed, actual
body-wheel braking is present, ground spoilers are deployed, and there is
no pedal/master-cylinder brake demand. After activation, spoiler retraction
or pilot pedal override disarms. The FBW pedal thresholds are used: landing
modes disarm above 53% on either pedal or 11% on both; RTO uses 77% on either
or 53% on both. These source thresholds are marked provisional for the A380
in the upstream code.

## Anti-skid

The switch changes the native anti-lock capability and never changes the
nose-wheel-steering switch. OFF selects the native no-ABS braking behavior,
sets `emergency_braking`, and disarms autobrake/RTO. `antiskid_available`
requires the switch ON and native normal or accumulator-backed braking
pressure. Separate FBW normal/alternate/emergency hydraulic circuits are not
implemented by this panel adapter.

## Public control values

All center-right outputs are read-only integer datarefs. Existing gear lamp
and warning names remain unchanged.

| Dataref suffix | Value |
| --- | --- |
| `autobrake_selected` | Physical detent: 0 DISARM, 1 BTV, 2 LO, 3 level 2, 4 level 3, 5 HI |
| `autobrake_armed` | Effective mode: 0 DISARM, 2 LO, 3 level 2, 4 level 3, 5 HI, 6 RTO |
| `rto_arm`, `rto_pressed` | Armed and physically held states, each 0/1 |
| `btv_reverted`, `autobrake_active` | HI fallback and inferred active indication, each 0/1 |
| `normal_brakes_available`, `autobrake_available` | Native adapter availability states, each 0/1 |
| `antiskid_on`, `antiskid_available`, `emergency_braking` | Switch and braking states, each 0/1 |
| `body_accu_psi`, `body_accu_zone` | Scaled native pressure; zone 0 amber, 1 intermediate, 2 green |
| `brake_left_ratio_milli`, `brake_right_ratio_milli` | Actual native body-wheel braking, 0–1000 |
| `reinflate_pressed`, `reinflate_active` | Physically held button and running pump request, each 0/1 |

The center-right command suffixes are `gear_toggle`, `autobrake_up`,
`autobrake_down`, `rto_toggle`, `antiskid_toggle` and `accus_reinflate`.

## Verification

The existing textured cockpit export carries source-driven motion for the
autobrake and anti-skid selectors, all three gauge needles, both clock knobs,
RTO, CHR, RST and DATE buttons, and moving button captions. The existing
landing-gear lever animation uses the native center-right gear-handle state.
The invisible mouse targets follow the same authored motion. The patch tool
checks exact source triangle counts and preserves every vertex, index, UV and
texture reference. Corresponding mappings are in `config/animation_datarefs.json`
for future full exports.

The lower lamp row is ordered left wing, left body, right body, right wing.
Source meshes `_1` and `_3` each contain two separate lens faces; the live
overlays use the individual authored faces. The flashing warning arrow is
offset toward the camera from the actual gear-panel plane. CHR, UTC and ET
are mapped separately over the three clock openings; the analog gauge uses
its authored needles and printed pressure arcs.

The production control code is exercised through the XPLM mock lifecycle
harness, including three aircraft reloads, gear delays, split gear states,
exact pressure-zone boundaries, hydraulic/inertial/anti-skid/brake-failure
arming loss, ground-only RTO, BTV-to-HI effective mode, pedal override,
spoiler disarming, independent wheel-brake inputs, held pushbuttons, native
pump ownership, and restoration of native deceleration targets and ABS.
These checks do not constitute an in-simulator flight or visual test.
Cockpit animation/click alignment and native hydraulic charging require an
in-simulator check after loading the rebuilt aircraft.
