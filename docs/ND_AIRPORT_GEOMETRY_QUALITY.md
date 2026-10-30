# ND airport geometry quality

The airport geometry provider previously used exactly eight chords for every
curved apt.dat span. This produced visible facets on long bends while spending
vertices on short and nearly straight spans. The provider also flattened painted
centreline chains twice during conversion.

`tools/nd_airport_preview.py` now uses adaptive de Casteljau subdivision in the
airport's projected metre coordinates. Both control points must lie within
0.20 m of the finite chord before accepting that chord. The convex-hull property
bounds the curve's chord error; distance to the finite segment also preserves
collinear handles that overshoot their endpoints. A maximum of ten subdivision
levels limits pathological input to 1,024 chords per source span. The error
criterion can be exceeded if that safety limit is reached; neither measured
airport reached it.

Consecutive duplicate points are removed with a 1 mm tolerance. Original open
endpoints, polygon closure, inner rings and WED split-handle semantics are
preserved. Curves are generated from the source geometry, rather than from an
upscaled bitmap. There is no texture allocation or geometry regeneration in a
mouse handler. The existing native airport worker still performs scenery parsing
outside the X-Plane main loop; its caller retains the existing scene cache.

Pavement and painted-feature descriptions are retained as `source_name`, without
turning scenery descriptions into taxiway identifiers. Painted centreline chains
retain their source paint codes in `source_line_codes`, and split at actual paint
style changes. Only codes 1, 7, 51 and 57 remain taxi centreline features. Roadway,
hold-line, light-only and unknown codes remain excluded. Actual ATC taxiway
identifiers remain in the existing `idlin` property. These are additive properties
on the existing FeatureCollection schema. No buildings or other geometry were
invented. Record meanings and source-name semantics follow the
[X-Plane apt.dat 1200 specification](https://developer.x-plane.com/article/airport-data-apt-dat-12-00-file-format-specification/).

## Local measurements

Measurements used the installed X-Plane Global Airports apt.dat on this machine,
the exact saved pre-change parser, and the updated parser. Conversion times are
medians of nine interleaved before/after runs on already loaded airport blocks.
They measure Python conversion cost, excluding scenery-file scanning, JSON
encoding and browser rendering. They are **not simulator FPS measurements**.

| Measurement | EGLL before | EGLL after | UAAA before | UAAA after |
| --- | ---: | ---: | ---: | ---: |
| Pavement vertices | 10,602 | 8,109 | 688 | 631 |
| Taxi centreline vertices | 873 | 873 | 181 | 184 |
| Compact airport JSON bytes | 600,257 | 500,307 | 70,756 | 69,005 |
| Median conversion ms | 80.709 | 48.952 | 11.136 | 6.179 |
| Maximum sampled curve/chord error, m | 1.115 | 0.144 | 0.802 | 0.140 |

Curve errors compare 129 analytical sample points per source cubic against the
nearest output chord. All 855 EGLL and 52 UAAA rendered curved spans were checked.
These sampled errors provide a numerical comparison, rather than a claim about
screen pixels or simulator frame timing. The analytical control-hull criterion
provides the separate 0.20 m subdivision bound, subject to the depth limit above.

Detailed counts, raw timings, source block hashes and parser hashes are saved in
`build/nd-quality-20261020/airport-geometry-measurements.json`. The exact old parser
is `build/nd-quality-20261020/before/xp-kit/nd_airport_preview.py`.

## Verification

`python3 -m unittest discover -s tools -p 'test_nd_airport_preview.py' -v`
passes 16 tests. Coverage includes independent dense analytical curve-error
checks, preservation of collinear overshoot, bounded pathological input,
deduplication, ring holes and closure, exact source endpoints, source paint-style
changes, scenery priority, projection round trips, runway displacement and the
read-only preview HTTP boundary. Before/after whole-ND screenshots and simulator
cursor/FPS checks belong to the renderer integration report; this parser report
does not claim those tests were completed.

## Package and preview refresh evidence

The candidate package's `xp-kit/nd_airport_preview.py` and the native ND package
probe both match the source parser byte for byte, SHA-256
`53381da18de4989e4ba2cc4b2082f08196580e2afd083fcfb1154d8d2ecf4035`.
The pre-install output aircraft still contained the baseline parser when this
check ran; aircraft installation is handled by the overall graphics integration.

The browser fixture cache was stale despite having unchanged scenery-block
hashes: it still held 10,602 EGLL and 688 UAAA pavement vertices. It was refreshed
through the existing `tools/serve_xp_kit_nd_trial.py --refresh-airports --port 0`
entrypoint, then the server was stopped normally. The resulting
`build/xp-kit-nd-test/airports.json` contains 8,109 EGLL and 631 UAAA pavement
vertices. No scenery source was changed.

A targeted Chromium OANS smoke check loaded both airports from that cache at
0.2, 1 and 5 NM, verified the adaptive pavement counts reached the actual OANS
feature collection, exercised pan and recenter, and confirmed that zoom and
cursor/pan activity reused the same loaded geometry object. It passed with no
JavaScript errors or failed requests. Six actual browser captures and the raw
report are saved under `build/nd-quality-20261020/airport-browser-smoke/`.
The Almaty 0.2 NM and Heathrow 1 NM captures were visually inspected. These are
browser smoke captures, not simulator screenshots or a before/after comparison.
