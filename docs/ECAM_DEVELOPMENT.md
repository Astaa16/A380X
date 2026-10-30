# Lower ECAM / SD integration

ENG's native 3D button prototype was installed without a backup on 2026-10-19.
Its visible cap/face OBJ animation and persistent green selection indication use
independent native datarefs. See [ENG implementation and actual-cockpit test](ECAM_ENG_BUTTON.md).
Only ENG has this new physical animation/selection-light treatment; expansion
requires its human cockpit test first. The older build and live-test evidence
below is historical, not proof of the new ENG mouse/animation acceptance.

The local mockup remains at <http://127.0.0.1:8384/>. Following the user's review, native cockpit integration was installed without a backup from `/Users/sava/Desktop/A380/build-ecam-cockpit-20261016-v4` on 2026-10-16.

Installed Mac plugin SHA-256: `249dcd05adb7eb878cae09115da82d41d7c7c76afa32096a96745bd8cebdb357`.

The actual-cockpit mouse check is **pending**. Static validation and command/API tests are not proof that a human click landed on the intended 3D mesh. X-Plane has now loaded the installed build; its SD device is bound, powered and receiving live inputs, with zero helper errors.

## Native implementation and limits

- One `a380x.xp12.ecam.sd` cockpit device is mapped to the previously blank lower SD surface, 768 × 1024, bus 1 / rheostat channel 18.
- All 16 SD page selections and 36 ECAM commands are registered. The 15 physical page keys, 14 action keys and both SD/EWD brightness knobs use their authored cockpit meshes. CRUISE, next-page and popup remain menu/API functions, not invented physical keys.
- Each invisible click target is reduced to 12 bounding-box triangles; old cockpit vertices/indices and unrelated controls are preserved.
- ENGINE/APU use the native C renderer with per-value validity. Remaining system synoptics use an aircraft-local, out-of-process WebKit renderer built from the reviewed FBW components; CRUISE uses the reviewed placement with explicit live/unknown values. No web server is required. Protocol v4 adds one SD channel while preserving the previous PFD/ND/MFD channel indices.
- The renderer's native entry has no preview fixture/scenario imports. Inputs are read from X-Plane. Missing system producers render XX/amber, and the source's hardcoded demo values are not installed as live aircraft telemetry. A380-specific hydraulic units, full fuel topology, cabin sensors, independent F/CTL-computer status and other systems still need aircraft producers; a page being selectable does not mean its whole system is simulated.
- CLR/RCL/EMER CANC operate the existing native EWD warning queue without repairing the underlying failure. C/L, up/down and both check keys operate manually acknowledged pinned FBW checklist entries. ABN PROC lists existing native alerts only. **Full Airbus FWS/abnormal procedures and full T.O CONFIG computation are not implemented**; T.O CONFIG explicitly reports this rather than a false NORMAL result. VIDEO remains NOT AVAIL.
- ALL advances while held and stops on End/disable. SD snapshots are change-cached, and no SD cursor-motion transport exists. The MFD cursor-only snapshot avoidance is preserved.

Build the native assets with `ECAM_NATIVE=1 ECAM_PREVIEW_OUTPUT=/path/ecam-native node tools/build_ecam_preview.cjs`, then pass `ECAM_NATIVE_BUILD=/path/ecam-native` to `native/avionics/build.py --xpkit --ecam-source <authored cockpit glTF>` using a fresh internal-disk output directory. Install only while X-Plane is closed, with `tools/install_xp_kit_cockpit.py <staging> --no-backup` if explicitly requested.

### Native verification

- Native adapter: 92 checks, including every registered ECP command, all page IDs, held ALL/release, warning clear/recall, checklist navigation and independent brightness.
- Native C SD renderer: 2,316 checks, six ENGINE/APU fixtures with valid/unavailable inputs.
- Existing MFD adapter: 451 checks; existing EWD render fixtures still pass.
- Actual packaged WebKit helper, isolated inputs: all 16 SD pages captured, zero reported errors; static MFDs captured once while PFD/ND channels kept updating. Evidence: `/Users/sava/Desktop/A380/ecam-native-cycle-v4/helper-cache-validation.json`. This is not a live cockpit/FPS test.
- Installed package validation: zero errors; both executable signatures and installed binary hashes verified. Evidence: `/Users/sava/Desktop/A380/ecam-package-validation.json`.
- Live X-Plane display-only check: all 16 native page commands changed the selected page and caused device draws; helper-rendered pages also advanced the presented-frame counter. Original page selection restored; zero renderer errors. Evidence: `/Users/sava/Desktop/A380/ecam-live-pages-check.json`. No flight controls were changed. The broader ECAM action test was not run because the aircraft was moving (approximately 25 m/s).
- `tools/check_ecam_cockpit.py --output <json>` exercises ECAM command/device routing in the stationary, powered project aircraft. `--observe 120` is a read-only observation of human ECP actions. Only the latter, used with actual physical meshes and closed popups, supports mouse-picking evidence.

## Web review: build and run

Use Node with esbuild, sass, React 18 and React DOM 18 available in `ND_TOOLCHAIN/node_modules` (the same local toolchain as the ND review, with the two React packages added).

```sh
node --test tools/test_ecam_preview.mjs
ECAM_PREVIEW_OUTPUT=/path/on/internal/disk/ecam-review node tools/build_ecam_preview.cjs
python3 tools/serve_ecam_preview.py --directory /path/on/internal/disk/ecam-review --port 8384
```

`FBW_SOURCE` selects the existing FlyByWire source checkout; `ND_TOOLCHAIN` selects the dependency directory. Defaults follow the established ND preview. Keep build products on the internal disk because the aircraft's external volume has previously stalled. The server binds only `127.0.0.1`, disables directory listings, and sets `connect-src 'none'`. There is no server-side action API or simulator uplink.

For this review, the built files are `/Users/sava/Desktop/A380/ecam-review`, served at <http://127.0.0.1:8384/>.

## Reused FlyByWire source

- React `SD/Pages`: ENGINE, APU, BLEED, COND, PRESS, DOOR/OXY, ELEC AC/DC, FUEL, WHEEL, HYD, CB and STATUS.
- xp-kit-transformed `SDv2/Pages`: F/CTL, CRUISE and VIDEO. VIDEO retains the upstream `NOT AVAIL` indication.
- Original EIS font, SD images and display stylesheet.

The build manifest lists the original source files and their hashes. Upstream files are read, not edited. Review-only adapters supply synthetic SimVars / ARINC words, link the original FUEL and WHEEL MORE states, and default omitted F/CTL indicator x/y coordinates to zero. An unused instrument base-class stub lets xp-kit's widget exports load without starting an instrument/backplane.

The source includes provisional or hardcoded indications (for example engine vibration, nacelle temperature and oxygen quantities). These remain a visual reference, not a completed aircraft systems model. They must not be mistaken for validated X-Plane values.

## ECAM controls in the website

All 15 physical page keys select a page and illuminate the selected key. Pressing the selected key returns to automatic phase selection, except STS, which toggles the review detail area. CRUISE is reachable through AUTO, ALL or the compact review selector; it is not invented as a physical ECP key.

ALL advances immediately, then once per second while held. Release, pointer cancellation, lost capture, blur or document visibility changes stop the scan. Keyboard Enter/Space is also supported. Display power and brightness are independent.

MORE exposes the original WHEEL and FUEL detail paths. Synthetic engine-oil, hydraulic, door and brake alerts select the associated page. Both CLR keys, RCL/RCL LAST, EMER CANC, T.O CONFIG, C/L, ABN PROC, up/down and both check keys operate the explicitly labelled **adjacent test area**. This exercises the button routing; it is not the aircraft FWS, a full abnormal-procedure database, or a real takeoff configuration calculation.

No mouse-move handler drives React state, rendering or network traffic. Fixture changes notify hooks only on deliberate control actions; the original FUEL switch-fixture interval is cleaned up when that page unmounts. This does not establish native X-Plane frame rate.

## Verification

- Eight deterministic state-model tests: page reachability/toggle/AUTO, held ALL/wrap/release, brightness/power, clear/recall/cancel, checklist/config/MORE/reset, independent fixture values, scenario changes, STS and invalid inputs.
- Browser-click review of all 15 physical page keys in taxi, ground and cruise conditions; no page error or non-finite SVG coordinates after fixes.
- CRUISE AUTO checked separately with synthetic 6,500 ft cabin altitude / 8.1 PSI differential.
- WHEEL MORE changes the original brake-control layer from hidden to visible.
- Browser checks of both CLR/check keys, recall-last, cancel, checklist navigation, brightness endpoints, power and a 2.3-second ALL hold.

These web checks remain separate from native cockpit input, live aircraft-system coverage, and full FWS support.
