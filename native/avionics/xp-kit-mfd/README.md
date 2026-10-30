# Native MFD pages through xp-kit / WebKit

Both existing MFD cockpit devices now receive full-resolution 768×1024 textures
from the same aircraft-local WebKit helper as the PFD and ND. There is no browser
tab, server or network dependency. Six views share version-3 shared memory.

## What is and is not being ported

This renderer uses **the existing native C translations of FlyByWire's MFD pages**,
not a newly started upstream JavaScript FMC or recompiled original MFD TSX tree.
`xp-kit`'s real `Widget`/`Kit` component entry point hosts a Canvas2D backend for
their drawing commands. Original FlyByWire EIS fonts are loaded locally. Clipping,
stable layer order, palettes and baseline coordinates follow the native page
contract in `docs/mfd-spec/12-architecture.md`.

The native FMS, performance, navigation and page-controller code remains the
single source of truth. Both seats share its active/TMPY/secondary plans. Physical
MFD clicks, dragging, wheels, hover, PC keyboard capture, KCCU keys and DU power
continue through the existing X-Plane device adapter. No second browser-only plan
or JavaScript autopilot has been added. The ND continues reading this same native
active flight plan. Native OpenGL drawing is retained as an automatic fallback
if the helper is absent, dead or stale.

## Available page families

- INIT; FUEL&LOAD; DATA/STATUS and DATA/AIRPORT; messages and SEC INDEX/SEC1–3.
- PERF T.O, CLB, CRZ, DES, APPR and GA; validated pilot entries and native A380
  speed/performance calculations already supported by the core.
- F-PLN; departure, arrival and approach selection; airway entry; DIRECT TO;
  revision menus, INSERT NEXT WPT, NEW DEST, HOLD, vertical constraints/step
  altitudes, FIX INFO and duplicate-name selection.
- POSITION MONITOR/IRS/NAVAIDS; SURV controls/status; FCU BKUP AFS/EFIS panels;
  ATCCOM connect/message records and local-weather D-ATIS.

All 11 native page modules and their 32 registered route patterns remain linked.
This does **not** make every displayed aircraft function complete: upstream/port
WIND, WHAT IF, and unsupported ATC request/report/emergency workflows retain
NOT AVAIL indications. No real CPDLC service, full VNAV/LNAV prediction engine,
automatic route-following/autoland, weather/terrain raster or BTV brake control
is claimed. D-ATIS is generated from local simulator weather, not an official ATIS.

## Use

Use the physical KCCU **INIT**, **F-PLN**, **PERF**, **DIR**, **NAVAID**, **SEC INDEX**,
**SURV** and **ATCCOM** keys or the MFD header menus. Click a cyan field, type with
the PC keyboard/KCCU and confirm with **ENT**. Invalid entries are rejected by the
native FMS. Use TMPY INSERT/ERASE to commit/discard route revisions.

The aircraft menu **A380X MFD**, or commands
`a380x/xp12/mfd/<captain|first_officer>/popup` and `/popout`, opens larger views.
The corresponding DU MFD knob controls brightness; zero blanks the side and
disables its input. Yellow cursors are drawn by the native page controller.

Health datarefs append to the existing PFD/ND contract:

- `a380x/xp12/xpkit/captain_mfd_active`, `first_officer_mfd_active`
- `a380x/xp12/xpkit/captain_mfd_presented_frames`, `first_officer_mfd_presented_frames`

## Build and verification

```sh
node tools/build_xp_kit_mfd.cjs
python3 native/avionics/build.py --sdk build/sdk/SDK --xpkit --output-dir build/fresh-mfd-build
python3 tools/install_xp_kit_cockpit.py build/fresh-mfd-build
```

The existing PFD/ND bundles must also have been built before packaging. Always
quit X-Plane before installation. The installer checks asset hashes and signatures,
backs up the previous Mac package and leaves the Windows binary untouched.

Installed staging: `build/xp-kit-mfd-cockpit-20261016-d`.
Installed directly with `--no-backup` at the user's request. Existing earlier
backups were not deleted. Binary hashes/signatures and cockpit touch geometry
were verified after installation.

The full native build passes the existing suites, including 2,107 UI checks,
1,879 format checks, 564 FMS checks, 399 navdata checks, 172 PERF checks and the
flight-plan/page/adapter interaction suites. `tools/test_xp_kit_mfd.cjs` tests all
seven drawing primitives, clipping/layers/fonts, power-off and malformed packets.
`build/xp-kit-mfd-render-validation/renderer-validation.json` records 76 actual
native page-test states rendered through the new component backend, with no
JavaScript errors or external requests. The fixture set includes expected
NOT FOUND/NOT AVAIL and UI-test pages; its 39 distinct URIs are not 39 new systems.

The real native WebKit helper probe also renders all six views with zero errors,
including populated PERF and F-PLN fixtures at full resolution. Offline fixtures
are **not** packaged into the aircraft. `tools/check_xp_kit_mfd.py` is the separate
live ground check for channel health, device uploads, KCCU routing and DU blanking;
it does not enter or replace the user's flight plan. A live result is not implied
by these offline checks.

Cursor motion now stays in a small native overlay, outside the WebKit texture.
The 371-check adapter suite includes 1,000 pointer positions without page-packet
updates. The real helper probe captures each unchanged MFD once in 20 seconds,
with fresh liveness heartbeats and zero errors. See
`build/xp-kit-mfd-cache-probe/helper-cache-validation.json`.
Actual 3D cockpit dropdown interaction and cursor FPS still require live verification.

The original MFD TypeScript check was attempted with `FBW_TYPECHECK=1`, but remains
blocked by the checkout's missing MSFS SDK/other upstream dependencies. The
component-only build avoids those unused MSFS runtime dependencies; it does not
claim a successful upstream MFD/FMC build.
