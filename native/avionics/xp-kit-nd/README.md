# ND and OANS browser review

This is the review stage requested before aircraft integration. It compiles the
supplied FlyByWire A380X `ND/instrument.tsx` through the supplied **xp-kit** library,
including the real navigation-display and OANC components, rather than redrawing
them as a generic HTML mockup. FlyByWire sources and xp-kit remain unmodified.

This page remains an isolated browser demo. Following the user's approval on
2026-10-16, the separate [native ND/OANS integration](NATIVE.md) adds these components
to the actual cockpit; its live bridge does not link the demo fixtures.

## Run

From the `xplane-port` directory:

```sh
python3 tools/serve_xp_kit_nd_trial.py
```

Open <http://127.0.0.1:8382/>. The server only binds to loopback. Restart with
`--refresh-airports` to rebuild the cached airport data after scenery changes.
`--port 0` chooses an ephemeral port for automated tests.

## Review controls

- Captain ND: ARC, ROSE NAV, PLAN, ROSE VOR and ROSE LS; 10–640 NM range;
  flight-plan/VOR/airport symbols; heading/wind fixture controls; IR failure and power.
- First-officer OANS: PLAN/ARC/ROSE, 0.05–5 NM zoom (0.1 NM default), drag-to-pan, center on aircraft,
  double-click context menu for flags/crosses, erase markers and the original
  FlyByWire airport control panel.
- Almaty (UAAA) and Heathrow (EGLL) airport geometry is loaded from the user's
  enabled X-Plane scenery in scenery-pack priority order, with Global Airports
  as fallback. Coordinate provenance and source-block hashes are included in
  `build/xp-kit-nd-test/airports.json`.

Flight position, sensor words, route, navaid symbols, wind and time are explicit
demo fixtures, not live simulator or FMS data. Radio navigation modes have no
received VOR/ILS signal fixture; WXR, live traffic, terrain and vertical predictions
are not supplied. The original display's INOP/standby indications are retained.

Airport geometry is **not an operational AMDB**: pavement, runway ends, displaced
thresholds, known painted-centreline curves, background ATC taxi edges and ramp
stands are converted from `apt.dat`. Curved ATC edges are drawn straight; their
source taxiway identifiers remain available in the original MAP DATA search.
Runway distances are geometric, not published declared distances. Magnetic
variation is zero in the fixture. No buildings, taxi clearances, hold-short
guarantees, A380 wingspan approvals or BTV stopping predictions are invented. Painted-line names are not
assumed to be authoritative taxiway designators. Record definitions follow the
[X-Plane 12 airport-data specification](https://developer.x-plane.com/article/airport-data-apt-dat-12-00-file-format-specification/).

## Build and adapters

```sh
node tools/build_xp_kit_nd.cjs
```

`FBW_SOURCE` can override the supplied source directory. `ND_TOOLCHAIN` can
override the dependency directory, currently
`/Users/sava/.cache/a380-nd-preview-tools-20261016`. The pinned toolchain manifest
is `native/avionics/xp-kit-nd/package.json`; it can be installed in a separate
directory outside the aircraft/source tree. The build uses the original Mach
plugins and the original `app/js` browser runtime. Generated files stay under
`build/xp-kit-nd-test`, which is not an aircraft installation directory.

The build records these small, preview-only source adapters in `manifest.json`:

1. Redirect the AMDB provider to the local read-only scenery endpoints.
2. Add one post-backplane fixture hook; wait for the asynchronous airport catalogue
   before selecting its airport. Fixture changes do not reload the map.
3. Replace invalid no-op `rotateX(0deg)` SVG attributes with `rotate(0)` in the
   original ND/OANC compass groups for Chromium/WebKit compatibility.
4. Use the supplied PNG icons where the original control panel requests missing
   SVGs, and label its map database `LOCAL SCENERY`.

Adapters fail the build if their expected source locations change. They are not
patches to the installed aircraft. The demo bridge is not suitable for cockpit
installation; the separately packaged `native-bridge.js` supplies the live boundary.

## Checks

```sh
python3 -m unittest discover -s tools -p test_nd_airport_preview.py -v
node tools/test_xp_kit_nd_trial.cjs
```

The browser test needs `playwright` available in `NODE_PATH` and Chrome (or
`CHROME_PATH`). It launches its own loopback server and checks modes/ranges,
source isolation, power, OANS loading/zoom/pan/markers/panel/airport changes,
responsive layout, console errors, missing assets and external network requests.
Reports and screenshots are written to `build/xp-kit-nd-test/`.

The build also attempts the original ND TypeScript project with `FBW_TYPECHECK=1`.
This upstream typecheck is currently **blocked** by unresolved MSFS SDK typings
and the source checkout's absent full dependency tree. See `typecheck.txt`;
runtime browser checks are separate and do not constitute a passing upstream
typecheck or an in-simulator validation.

FlyByWire, xp-kit and these adapters retain GPL-3.0 licensing. The bundled license
is served at `/LICENSE`.
