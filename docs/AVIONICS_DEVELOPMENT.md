# FlyByWire-derived PFD, FCU, ND, EWD and flyPad runtime

See [PFD implementation and verification](PFD_IMPLEMENTATION.md) for the
6 October native PFD update, connected inputs and remaining aircraft-system limits.

The aircraft-local `A380XAvionics` plugin owns both PFD/ND pairs, the center EWD, all four FCU
display windows, and both physical flyPads. X-Plane supplies raw sensors and
actuator interfaces. The stock EFIS mode and range datarefs carry the physical
selector positions into the native ND; stock moving map, flight director and
autopilot mode flags do not drive the custom flight displays.

The PFD renderer translates the pinned FlyByWire A380 PFD's 158.75 × 211.6
coordinate geometry and nonlinear pitch/vertical-speed scales to native
OpenGL. FlyByWire's actual A380 flight-guidance outer-loop C++ laws are now
compiled into the plugin. The [FCU/guidance notes](AUTOPILOT_DEVELOPMENT.md)
describe the controls, imported files and remaining computer-network boundary.

## Data ownership

- `a380x_avionics_core.c`: translated FlyByWire FMA text priority, FWS phases
  and existing flyPad calculation primitives.
- `a380x_guidance.c`: FCU selections, active/armed modes, channel state, altitude
  and ILS capture, failure/reversion handling and port-specific actuator loop.
- `a380x_fbw_laws.cpp` and `vendor/fbw/`: native bridge and unmodified imported
  A380 AP/FD outer-loop implementation, with source hashes and GPL license.
- `a380x_fcu_adapter.inc`: side-specific X-Plane sensor acquisition, registered
  FCU commands/datarefs and strictly scoped control overrides.
- `a380x_pfd_render.inc` and `a380x_pfd_data.h`: display snapshot and renderer.
- `a380x_fcu_render.inc`: selected/managed FCU digits, signs, dashes and dots.
- `a380x_ewd_render.inc` and `a380x_ewd_data.inc`: full-resolution EWD with
  animated THR/N1/EGT indications and supported live ECAM warnings/memos.
  The [EWD notes](EWD_DEVELOPMENT.md) describe source geometry, FADEC
  normalization and the remaining FWS/checklist boundary.
- `a380x_lighting.inc`: four independent glareshield underside commands and
  readable levels; the two outer levels drive left/right panel spotlights.
- `a380x_efb.c`: existing touch-enabled flyPad runtime.
- `a380x_mfd*.c`, `a380x_mfd_page_*.c`: simulator-independent MFD core (display
  list, widgets, frame, pages); `a380x_mfd_render.inc` executes the display list
  with OpenGL.
- `a380x_fms.c`, `a380x_fms_speeds.c`, `a380x_fms_messages.c`: native FMS (phases,
  INIT/FUEL/PERF stores, speeds, messages). `a380x_navdata.c` and
  `a380x_fms_fpln.c`: X-Plane navigation files and the flight-plan store.
- `a380x_mfd_adapter.inc`: MFD cockpit devices, popups, KCCU commands, PC
  keyboard capture, MFD/KCCU datarefs.
- `a380x_fms_adapter.inc` + `a380x_mfd_systems_host.inc`: FMS inputs from raw
  sim data and port systems, FMS outputs (FWS V1, PFD V1/VR/V2, managed speed for
  the native guidance, DH, EWD FLEX, `a380x/xp12/fms/*`), SURV states and the
  transponder law, navaid tuner, D-ATIS synthesis, FCU/EFIS BKUP channel.
  See [MFD development](MFD_DEVELOPMENT.md).

The PFD's FMA now comes from actual selected guidance and engagement state;
it is no longer inferred from vertical motion, gear and throttle position.
The PFD presents active and armed modes, AP1/AP2, FD and A/THR status, timed
mode-change boxes, selected speed/heading/altitude/VS markers, FD pitch/roll
bars, TRK/FPA flight-path vector, magnetic/true heading, speed trend, radio
height, QNH/STD, metric altitude, and side-specific ILS scales and receiver
information. Speed/attitude/altitude/heading/radio-height failures suppress
invalid values and show flags. Optional characteristic speeds appear only
when valid data is supplied. The moving heading ticks now scroll at fixed
five-degree intervals and the bank pointer uses the correct top-arc position.

`FMS GUIDANCE N/A` and `AP OFF` in the lower display area are explicit adapter
advisories, not fabricated Airbus FMA modes. Autoland capacity annunciations
are absent because the port does not implement the required system network.
The ND remains a partial compass/track display (`FMS MAP PARTIAL`), pending the
separate FMS route/navigation-database port. Full EWD/ECAM alerting, terrain/weather/TCAS,
full electrical/ARINC/redundancy logic and display switching remain outside
this PFD/FCU implementation.

## Build and package

```sh
python3 native/avionics/build.py \
  --sdk /path/to/XPSDK430/SDK --output-dir /path/to/fresh/staging
```

The build runs C/C++ controller and render tests, validates source and command
contracts, builds universal macOS and Windows x86-64 plugins, verifies exports
and records source/binary hashes. Promote the successful staging build to
`native/avionics/dist` before the focused cockpit refresh:

```sh
./tools/build_local_mac.sh --refresh-group cockpit-controls
python3 tools/validate_aircraft.py output/A380X-XP12 \
  --datarefs "/path/to/X-Plane 12/Resources/plugins/DataRefs.txt" \
  --commands "/path/to/X-Plane 12/Resources/plugins/Commands.txt"
```

Tests of the actual renderer produce climb and ILS previews; they verify
geometry, state-dependent symbology and failure blanking without needing an
OpenGL window. These are offline renderings, not X-Plane screenshots. Native
cockpit compositing, mouse hit testing and aircraft flight response still
require simulator acceptance. The historical reports in `build/reports`
apply only to the binary hashes they record.

Sources: [FlyByWire A380 source](https://github.com/flybywiresim/aircraft/tree/2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2/fbw-a380x),
[X-Plane drawing rules](https://developer.x-plane.com/article/drawingrules/),
[OBJ8 cockpit regions](https://developer.x-plane.com/article/obj8-file-format-specification/).
