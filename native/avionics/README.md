# A380X FBW-derived avionics plugin

This aircraft-local X-Plane 12 plugin is the runtime boundary for the avionics
port. It translates the pinned FlyByWire A380 FMA priority and flight-phase
logic into simulator-independent C, reads only raw X-Plane sensor, engine,
electrical, and failure inputs, and draws both PFD and ND pairs, the center EWD, plus the flyPad
into the aircraft panel texture. Both authored tablets map the flyPad pixels
and expose page, action, and power touch zones. X-Plane lists the runtime as
`A380X FBW Avionics + flyPad` in Plugin Admin. It reads the side-specific
X-Plane EFIS mode and range selector positions as cockpit inputs; it does not
consume X-Plane's stock FMS map, flight-director, VNAV, or autopilot mode status.

The PFD is a native OpenGL translation of the pinned open-source FlyByWire A380
PFD coordinate geometry and algorithms, including its attitude mask, tapes,
FMA columns, heading ribbon, nonlinear pitch and vertical-speed mappings, and
lower display area. The installed FlightFactor A350 was observation-only; no
FlightFactor code, secured script, binary, texture, font, private dataref, or
other packaged file is used.

The current navigation display is deliberately partial because the FBW FMS,
FMGC, and navigation-database layer has not yet been ported. `FMS MAP PARTIAL`
is rendered instead of substituting X-Plane's stock moving map. The physical
EFIS selectors now switch the native ND between ROSE LS, ROSE VOR, ROSE NAV,
ARC, and PLAN compass presentations and show the selected range. LS/VOR source
data is explicitly marked unavailable in those ND modes.

The center EWD uses the authored `SCREEN_DU_EWD` face and a full 768 x 1024
region of the 2048 x 2048 cockpit atlas. Its four engine columns translate
the pinned FBW THR, N1 and EGT geometry, colors and embedded EIS font. Live
N1 is normalized to THR using the imported A380X FADEC idle/thrust-limit
tables and current environmental/bleed inputs; gauges respond to engine
state, reverse, unavailable inputs and over-limit values. The lower ECAM
area shows supported live engine/gear alerts and APU, seat-belt, parking-brake,
external-power and anti-ice memos. Full FWS procedures, electronic checklists,
and automatic thrust-rating selection remain unported. Optional producer
contracts enable rating, throttle-target and autothrust cues without inventing
their values. See [EWD development](../../docs/EWD_DEVELOPMENT.md) for the exact
boundary, differences and validation. The separate System Display remains dark.

Both physical Display Unit Control Panels now command aircraft-local brightness
channels. The PFD and ND knobs dim each side's rendered screen independently,
including a fully dark zero setting. The ND outer WX/TERR and MFD channels are
registered for their respective future display layers; OIT, PFD/ND exchange,
and RECONF remain unavailable. See `../../docs/COCKPIT_CONTROLS.md` for the
knob and dataref mapping.

The two MFDs are plugin cockpit devices (`a380x.xp12.mfd.captain` /
`a380x.xp12.mfd.first_officer`, 768 x 1024) driven by the native FMS, navigation
database and flight-plan core (`a380x_mfd*.c`, `a380x_fms*.c`, `a380x_navdata.c`).
The device API needs **X-Plane 12.1 or newer** (12.4.0+ for reliable 3D hover and
drag); it is resolved with `XPLMFindSymbol`, so the plugin still loads on 12.0 and
the MFDs then open only as plain windows. See `../../docs/MFD_DEVELOPMENT.md`.

Build from a clean output directory:

```sh
python3 native/avionics/build.py \
  --sdk /path/to/XPSDK430/SDK \
  --output-dir /path/to/empty/staging-directory
```

The build compiles and runs the pure core tests before producing universal
macOS and Windows x86-64 plugin binaries.

The FCU now uses the unmodified pinned FlyByWire A380 `A380FgOuterLoops` C++
laws in `vendor/fbw`, plus a C command/mode adapter and X-Plane actuator
boundary. One shared state drives the physical FCU, PFD bugs, FMA and FD.
Source hashes and the GPL license are retained with the import. This does not
import the complete PRIM/FMS/FADEC network or autoland. See
`../../docs/AUTOPILOT_DEVELOPMENT.md` for the explicit boundary and controls.

The build also runs numerical guidance, actual PFD/FCU rendering, and XPLM
boundary tests and checks native command/dataref declarations against the
cockpit package contract. Generated SVG previews are offline render tests.

The three-unit ADIRS now drives the authored overhead controls and selected
PFD/ND/guidance validity. See [ADIRS operation and model boundary](../../docs/ADIRS.md)
for alignment, independent IR/ADR switching, ATT heading entry, source 3 and
airborne NAV lockout.
