# ND graphics quality and verification

This change targets the aircraft-local Mac xp-kit ND in X-Plane 12. It retains
the original FlyByWire Airbus ND artwork, EIS fonts, colors, compass, modes,
route geometry and OANS controls. The native fallback and other platforms do
not acquire the WebKit renderer through this change.

## Actual rendering pipeline and causes

The original ND is SVG, HTML and dynamically drawn Canvas geometry. Fonts are
the bundled EIS faces; airport surfaces come from local X-Plane scenery
apt.dat geometry. There is no low-resolution photographic map to sharpen.

Before this change, the WebKit view was 768 × 1024 CSS pixels, but its snapshot
and shared-memory buffer were 384 × 512 pixels. The cockpit device was already
768 × 1024. Thus text, symbols, routes, compass ticks and airport geometry all
lost half their resolution in each direction before a bilinear enlargement.
Increasing only the airport detail could not repair that loss.

The authored cockpit ND quads have full [0, 1] UV coverage. Their measured
widths are approximately 158.6 and 158.8 mm; surface heights are approximately
209.6 and 210.4 mm. The portrait proportions are consistent with the intended
3:4 display. No geometry or UV replacement is needed for this quality change.
The older panel-atlas fallback draws into 180 × 234 pixels and remains a
fallback. It is not the full-resolution device path described here.

Other identified limitations were an unconditional 20 Hz ND snapshot/upload
path, a DOM cursor included in those expensive snapshots, overlapping optional
navigation labels, and airport curves using eight straight chords regardless
of curvature. Texture allocation was already persistent: mouse movement did
not itself call glTexImage2D every frame.

## Rendering and interaction changes

Each ND now has a separate 768 × 1024 RGBA buffer in shared protocol v5. The
helper rasterizes the original vector/text content at this resolution. It
copies a 1:1 snapshot without another resample; a Retina-sized WebKit result
is resolved with CoreGraphics high-quality interpolation. Its bitmap memory
and CoreGraphics context are reused.

The cockpit upload uses a persistent GL_RGBA8 texture, GL_LINEAR minification
and magnification, and GL_CLAMP_TO_EDGE. It updates with glTexSubImage2D only
when a completed snapshot changes. No sharpening shader, enlarged source
bitmap, nearest-neighbor text scaling or per-frame mipmap generation is used.
WebKit supplies edge antialiasing at the actual display resolution. Linear
sampling handles the perspective presentation in the cockpit without making
the source image a quarter of the required pixel count.

Both ND textures together use 6 MiB rather than 1.5 MiB. The shared ND pixel
storage increases by 4.5 MiB. A full-motion 20 Hz pair can transfer 120 MiB/s
of pixels; that is a cost, not a claimed optimization. Unchanged displays use
dirty-state caching with a 500 ms safety refresh, while actual heading,
position, mode, range, zoom, pan, menu, hover and asynchronous loading changes
can update at the existing host rate. Health heartbeats continue while cached.

The yellow cursor is now a small native geometry overlay over the retained
map texture, with the original 80-pixel proportions and first-officer rotation.
Moving it requests a device draw without a WebKit capture or texture upload.
Crossing an interactive hover target, dragging, clicking or opening a menu
still invalidates the affected map content. Ordered button-down/up events
are retained; unchanged hover positions are coalesced. Right-drag retains
the existing camera-look behavior.

The dedicated device records its own presented frame number. Legacy atlas
uploads cannot consume its redraw request. Helper exit clears held input and
requests a fallback redraw on both NDs. The offscreen helper holds a standard
macOS activity token only while aircraft inputs are fresh, releasing it on
stale input and allowing idle system sleep. This guards against background
App Nap; it is not a confirmed diagnosis of the earlier live-session stall.

The EIS fonts and aviation colors remain. Explicit text/SVG precision rules
and removal of unnecessary compositor transforms prevent avoidable softness.
Canvas route strokes use consistent round joins/caps and a 2-pixel weight.
Canvas clipping is saved and restored each frame. Text placement uses cached
font measurements and a screen-space collision grid, reserving visible
navigation symbols first. Active-leg labels take priority over optional
database labels; symbols and routes are never removed by decluttering.
Constraints and hover boxes follow their relocated label. OANS preserves
upstream zoom/visibility rules and prioritizes runway/selected/user markings
over optional stand/taxi text without synchronous DOM layout reads.

Airport surfaces are regenerated from source curves using adaptive 0.20 m
subdivision rather than bitmap scaling. The worker and scenery cache remain
outside the mouse path. See [airport geometry measurements](ND_AIRPORT_GEOMETRY_QUALITY.md).

Airport zoom now offers 0.05, 0.1, 0.2, 0.5, 1, 2 and 5 NM, defaulting to
0.1 NM instead of 1 NM. The values describe the original 500-pixel compass
diameter, not radius; the full 768-pixel view spans approximately 284 m at
the default and 142 m at 0.05 NM. This is ten and twenty times closer than
the former default. The original ring continues to show half the diameter.
Layer/label thresholds retain their physical meaning after inserting ranges.
Source ATC guidance lines with valid taxiway IDs now participate in labels.

The original airport renderer cached whole-airport bitmaps at roughly one
pixel per metre. At the new closest range those would be enlarged about
5.4 times. A separate cached 768×768 viewport therefore redraws source
Path2D geometry directly at 0.2 NM and closer. It uses the original map
projection/heading/pan transform, preserves compound even-odd polygon holes,
culls offscreen features and caps guidance strokes at 2.5 display pixels.
Dynamic BTV remains above the viewport; the new canvas does not intercept
input. The extra RGBA canvas is approximately 2.25 MiB per ND. Its cached
paths and rendered pixels are reused when only the pointer moves.

Terrain/weather imagery and TCAS classification are not implemented by this
port. Their existing INOP/STBY indications remain; this work does not add
invented overlays. Existing brightness/power behavior remains the supported
day/night mechanism.

## Source and asset ownership

| Files | Responsibility |
| --- | --- |
| `native/avionics/a380x_xpkit_shared.h`, `tools/xp_kit_transport.py` | Protocol v5, ND dimensions and diagnostics |
| `native/avionics/a380x_xpkit_host.m` | Persistent raster contexts, full-resolution capture, dirty capture and timing |
| `native/avionics/a380x_xpkit_adapter.inc` | Persistent textures, uploads and device redraw scheduling |
| `native/avionics/a380x_xpkit_nd_input.inc` | Native cursor, ordered input and device callbacks |
| `native/avionics/xp-kit-nd/native-bridge.js` | Visual invalidation and original UI event routing |
| `native/avionics/xp-kit-nd/quality-layout.mjs`, `quality.css` | Collision-aware labels and precise presentation |
| `native/avionics/xp-kit-nd/close-zoom.mjs` | Bounded source-geometry renderer for close airport zoom |
| `tools/nd_quality_adapter.cjs`, `tools/build_xp_kit_nd.cjs` | Guarded upstream source transforms and reproducible ND bundle |
| `tools/nd_airport_preview.py` | Source-based airport curve generation |
| `tools/package_xp_kit_cockpit.py` | Verified ND-only asset refresh, preserving other display HTML |
| `native/avionics/build.py` | Native build and input verification |
| `tools/probe_nd_quality.py`, `tools/check_nd_quality_live.py` | Separate isolated-helper and live-simulator evidence |

The generated asset is `xp-kit/nd.html` plus its bundled fonts and local
airport provider. Existing EIS font assets are reused rather than substituted.
The cockpit OBJ and panel texture do not require visual changes. Installation
uses the existing checked installer, with a backup and signed binary/hash
verification.

## Verification scope

Native compilation and production input tests, C/Python ABI checks, airport
parser tests, label collision tests, and native bridge tests pass. Browser
integration covers all five ND modes, seven navigation ranges, OANS 0.2–5 NM,
independent display power/failure, pan/recenter, context menu and markers,
airport panel, and UAAA/EGLL switching, without JavaScript errors, missing
assets or external requests. These checks do not prove physical OS mouse
interaction in the simulator.

The subsequent close-zoom suite checks all seven airport zoom levels, physical
alignment in ARC/ROSE/PLAN with pan (maximum measured projection difference
2.2e-10 pixels), fixed canvas dimensions, source canvas reuse, BTV ordering,
actual original panel-arrow clicks, drag and recenter. Pointer movement left
geometry draw and path-cache counts unchanged (172 draws / 105 cached paths
in that fixture). These are browser integration checks; the real browser
capture and report are under `build/nd-quality-20261020/close-zoom-browser-review`.

The bundled ND builds successfully. The full upstream TypeScript project
check still requires missing MSFS environment declarations; the saved
`typecheck.txt` records this limitation rather than a passing full-project
typecheck.

Real isolated-helper captures and measurements are stored under
`build/nd-quality-20261020/before/captures` and `after/captures`. They use the
actual old/new aircraft-local WebKit helper and identical fixtures. They are
not cockpit screenshots or simulator FPS. The baseline 384 × 512 raw captures
are also provided at a clearly labeled bilinear 768 × 1024 comparison size,
matching the old texture presentation. Raw images remain available.

Live results, installation details and paired measurements will be recorded
below after the simulator checks complete. Failed attempts are retained and
are not counted as passing runs.

## Isolated helper measurements

These are real single-run engineering samples with identical saved fixtures.
Each phase lasts approximately 20 seconds. They exclude X-Plane, cockpit GL
uploads, native cursor drawing, WebKit XPC/GPU process CPU and simulator FPS.
Some baseline extended phases overlapped compilation, so direct CPU comparisons
are not a controlled hardware benchmark. The exact measured helper hashes
and limitations are in `build/nd-quality-20261020/helper-comparison.json`.

| Scenario | Before snapshots, both sides | After snapshots, both sides | Before pixel transport MiB | After pixel transport MiB | Before direct helper CPU s | After direct helper CPU s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ARC idle | 801 | 120 | 600.75 | 360 | 5.07 | 4.70 |
| ARC cursor at 20 Hz | 801 | 78 | 600.75 | 234 | 5.11 | 4.24 |
| ARC changing navigation at 20 Hz | 800 | 801 | 600 | 2403 | 5.29 | 10.84 |
| OANS idle | 624 | 76 | 468 | 228 | 6.01 | 5.05 |
| OANS cursor at 20 Hz | 611 | 89 | 458.25 | 267 | 5.89 | 5.07 |

After cursor movement, both retained ND pixel buffers remained byte-identical
in the static ARC and OANS fixtures. The separate cursor does not require map
pixel changes. ARC cursor pixel transfer fell 61%, despite four times the
pixels in each snapshot. Continuous navigation still captured approximately
20 frames/s per side, with four times the pixel transfer and higher helper
CPU cost. This tradeoff is retained explicitly; the result is not a claim of
higher simulator FPS in motion. Mean full-resolution snapshot latency was
approximately 22–25 ms in these phases, including asynchronous WebKit capture
and copy rather than isolated GPU rendering time.
