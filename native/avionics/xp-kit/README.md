# FlyByWire xp-kit PFD trial

This is the first runnable library integration in **this** A380X project.
It runs the supplied FlyByWire A380 PFD TypeScript bundle through the supplied
`xp-kit` library and Chromium runtime. The captain and first officer displays
have separate documents, instrument indices and display-unit IDs (0 and 3).

The trial has two data sources: explicit seeded scenarios, or GET-only reads
from this project's native X-Plane ADIRS, brightness and sensor datarefs.
Live mode checks that the loaded `.acf` resolves to
`xplane-port/output/A380X-XP12/A380X-XP12.acf`. Another A380 installation is
rejected. Missing sensors, a changed aircraft and a lost connection return an
invalid sample; live mode never substitutes demo data. The browser drops a
stale sample after 1.5 seconds even if a request stalls.

## Start

From `xplane-port`:

```sh
python3 tools/serve_xp_kit_trial.py
```

Open <http://127.0.0.1:8381/>. The status shows **DEMO DATA**. Buttons exercise
level flight, a climb, a turn, captain IR/ADR failure and loss of bus power.

For this aircraft already loaded in X-Plane with its local Web API enabled:

```sh
python3 tools/serve_xp_kit_trial.py --live
```

The live host reads the simulator only. Instrument writes stay in the browser's
local variable store. There is no flight-load, command, actuator or dataref-write
endpoint in the host.

## Assets and source

The source supplied for this trial is:
`/Users/sava/Downloads/Flybywire-A380X-for-Xplane-12-main`.
The library is `xp-kit/`; the actual A380 instruments are under `fbw/xplane/`.
The `msfs-avionics-mirror` directory in this download contains no usable SDK
sources. The separate Rust systems library is not loaded by this trial.

Generated assets, original bundle backup, hashes, browser test results and the
preview are in `build/xp-kit-pfd-test/`. To refresh the runtime, library, fonts
and image assets after building the PFD:

```sh
python3 tools/serve_xp_kit_trial.py --prepare /path/to/Flybywire-A380X-for-Xplane-12-main
```

The PFD bundle was built with the source checkout's Mach configuration, with
`XP_KIT` pointing to the built library's `dist` directory. Mach must be pinned
to **1.0.3**: 1.2.1 ignores this configuration's top-level plugin field, and its
own TypeScript loader prevents the library transform. The download's
`package.json` now uses the exact version; the type-check hook uses `pnpm exec`.

The focused bundle command, from the supplied `fbw/xplane` directory, is:

```sh
XP_KIT=/path/to/source/xp-kit/dist FBW_TYPECHECK=0 \
  pnpm exec mach build --config fbw-a380x/mach.config.js --work-in-config-dir \
  --bundles /path/to/xplane-port/build/xp-kit-pfd-test \
  --skip-simulator-package --filter PFD
```

This is a trial build. The library's own strict TypeScript check passed.
The full instrument type-check was attempted with `FBW_TYPECHECK=1` and is
blocked by the downloaded dependency tree's stub `@types/minimatch` package;
it is not reported as a passing check. The SDK-free typing setup also needs
validation before production adoption.

Preparation applies one recorded source-output correction: the altitude tape
in `VerticalTape.tsx:88` omits the closing `)` in its initial SVG transform.
The generator requires one exact match, preserves `bundle.original.js`, and
records both the correction and source hashes in `trial-manifest.json`.
It does not change the upstream generated flight-control models.

## Verification

```sh
python3 -m unittest discover -s tools -p test_xp_kit_trial.py -v
node tools/test_xp_kit_trial.cjs
```

The browser test requires Playwright. Set `CHROME_PATH` when using an installed
Chromium browser, and `NODE_PATH` when using an external package directory.
It starts and stops its own loopback server and browser. It verifies two PFD
boots, pitch/bank/airspeed response, side-specific failure, IR3 routing, power
loss/restoration, a paused simulation clock, stale-feed blanking, missing assets
and script errors. The separate Python tests check aircraft identity, caching,
missing/non-finite readings and disconnection behavior against a fake API.

## Current boundary

The existing native cockpit plugin remains the installed 3D display renderer.
This trial is a browser PFD host; these JavaScript instruments do not yet draw
onto the aircraft's cockpit texture. It is not a complete avionics replacement.
FWS, PRIM, managed flight guidance, radio tuning, flight planning and computed
envelope speeds are not supplied here. Their ARINC words keep failure status;
the original PFD therefore shows missing-system indications and an empty FMA.

The current aircraft has a shared native electrical bus, and the trial follows
that existing model. The third radio-altimeter channel stays invalid; only the
two real simulator channels are mapped. Both barometric correction slots use
each native ADR's published altitude. Live-mode clocks use elapsed simulator
time, not a fabricated flight-calendar date. Local browser writes have no
effect on the native FCU, ADIRS selectors or simulator.

The next integration step is a macOS display host that binds these library
views to the authored PFD screen faces and the native command/dataref contract.
It must explicitly resolve renderer ownership and systems validity before
being enabled alongside the current `A380XAvionics` plugin.
