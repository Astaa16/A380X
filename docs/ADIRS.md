# ADIRS in the X-Plane 12 A380X port

The native aircraft plugin owns three independent ADIRUs and the authored
3D overhead controls. Each selector has OFF, NAV and ATT detents. Mouse/VR
commands rotate the original knobs; six IR/ADR pushbuttons and their legends
follow the original press travel. OFF indications are white, FAULT amber.
The ADIRS window also provides clickable OFF/NAV/ATT detents and IR/ADR
pushbuttons, using the same state as the 3D overhead. A push toggles once;
holding or dragging does not repeatedly toggle it.
Selector and button motion continues while the simulator is paused; alignment
and heading timers follow simulator time and stop in pause/replay.

## Operation

For a cold start, supply electrical power and select IR 1, 2, then 3 to NAV.
The order is a procedure, not an artificial interlock. All three units align
independently while stationary (at most 0.5 kt). Movement restarts unfinished
alignment; taxiing after completion does not lose alignment. ON BATT self-test
illuminates for five seconds at IR startup. It remains illuminated when an
active unit uses the native battery-fed bus without observed generator/APU/GPU
supply. Engine-running or airborne aircraft loads initialize all three units
aligned in NAV; cold loads initialize OFF.

The IR pushbutton inhibits IR outputs without disabling the ADR or clearing an
otherwise running internal alignment. The ADR pushbutton inhibits ADR outputs
without affecting IR. The rotary OFF position removes both branches of that
unit. IR and ADR failures and their lamps are independent. Units 1 and 2 feed
their own side; unit 3 remains running and can be selected independently for
ADR and IR through **Plugins → ADIRS → Open / Close**, or the **ADIRS** button
in the ISCS Startup section. Source selection is manual; a fault does not silently
switch either side to unit 3. The receiving PFD retains its barometric setting.

ATT supplies attitude after 28 seconds and heading after crew entry. In the
ADIRS window, click the unit's heading field, type magnetic heading 000–360,
and press Enter or its ENTER button. Heading follows subsequent gyro turns;
the window requests a fresh entry after 10 minutes. Track, vertical speed,
position, flight-path/vector and speed-trend indications are unavailable in
ATT. The port also has heading entry on MFD FMS → POSITION → IRS when an IR
requests a heading. The ADIRS window supports later heading updates as well.

Leaving NAV for ATT or OFF airborne locks NAV for the rest of that loaded
flight, including subsequent touchdown. Switching back to NAV or pressing
ALIGN NOW cannot bypass that lock. Loading a new aircraft/flight resets it.
**ALIGN NOW** is a simulator convenience available only stationary on ground,
with healthy powered units already selected to NAV.

## Model boundary

NAV alignment uses the pinned FBW latitude schedule: 300/cos(latitude) seconds
through 60°, 600 seconds through 73°, and 1020 seconds above 73°. Initial NAV
alignment is unavailable beyond 82°. The five-second immediate battery test
follows this task's requested behavior. ADR measurements become available
without a separate ADR startup delay.

This is a native simulator model, not proprietary ToLiss code or a complete
port of the FBW ADIRS/electrical network. X-Plane provides three pitot/static
instrument samples and shared motion measurements. The aircraft package uses
a shared electrical bus, so all three units currently share its supply; the
core supports independent supplies. Native ADC failures cover units 1/2, AHRS
failures cover IR 1/2, and independently writable failure inputs cover all
three units. Native standby pitot/static failures affect ADR 3. The ADR 3 Mach
sample uses native aircraft Mach because X-Plane has no standby Mach indicator.
Barometric correction on backup source uses the port's 1000 ft/inHg
approximation. Overspeed uses the aircraft's native Vne/Mmo limits, not a full
Airbus envelope computer.

The PFDs, NDs, flight guidance and autobrake availability consume selected
ADIRS validity. Invalid sources remove their associated speed/altitude,
attitude/heading, vertical-speed, track and navigation indications. Radio
identification/DME remains independent, while rotating radio needles require
heading. Native raw sensor outputs remain published with explicit validity
flags for future system consumers; there is no inertial drift or independent
sensor-noise model and the native FMS remains a separate system consumer.

## Interfaces

All names start with `a380x/xp12/adirs/`. Unit numbers below are 1–3.

- `irN_mode_up`, `irN_mode_down`: bounded selector commands.
- `irN_toggle`, `adrN_toggle`: independent pushbutton commands.
- `captain_ir_toggle`, `captain_adr_toggle`, `first_officer_ir_toggle`,
  `first_officer_adr_toggle`: own-side / unit 3 source selection.
- `window_toggle`, `align_now`: heading/source window and ground alignment.
- `irN_mode`: OFF=0, NAV=1, ATT=2; `irN_knob`: continuous 0–2 motion.
- `irN_pressed`, `adrN_pressed`: continuous 0–1 press travel.
- `irN_off`, `irN_fault`, `adrN_off`, `adrN_fault`, `on_batt`: powered lamps.
- `irN_failure`, `adrN_failure`: writable independent simulated faults (0/1).
- `irN_nav_valid`, `irN_attitude_valid`, `irN_heading_valid`, `adrN_valid`:
  validity required before consuming the corresponding published measurements.
- `irN_nav_locked`, `irN_aligning`, `irN_alignment_remaining`,
  `irN_heading_required`, `irN_heading_reminder`, `irN_heading_age`: status.
- `captain_ir_source`, `captain_adr_source`, `first_officer_ir_source`,
  `first_officer_adr_source`: **zero-based** unit index 0, 1 or 2.

The provider manifest lists all measurement datarefs with unit-bearing names.
No ON BATT face exists in the supplied cockpit mesh or reference screenshot;
ON BATT therefore appears in the ADIRS window and EWD memo area.

## Validation

The build runs pure state-machine tests, production-adapter XPLM mock tests,
window interaction/lifecycle tests, existing avionics tests and display render
tests. Packaging tests verify command/dataref contracts, matching click and
visual motion, all 21 moving meshes, twelve lamp faces, and byte-identical
vertex/index/UV/texture payloads. SHA-named backups and patch reports are in
`build/backups/adirs-20261006` and `build/reports/adirs-*.json`.

The October 7 update adds direct popup clicks for all nine controls and
verifies press/release cancellation in the production window tests. Physical
OBJ command regions were audited in `proof/ADIRS-click-bindings.json` in the
workspace. Live mouse verification was not completed: the simulator UI
inspection timed out and X-Plane subsequently shut down.
