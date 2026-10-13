# MFD spec 12: native architecture and module contracts

Status: normative for the implementation. Specs 01-11 describe *what* to build; this file fixes *how the C code
is partitioned* so several developers can work in parallel without editing the same files.

Precedence when specs disagree:

1. This file.
2. `01-framework-widgets.md` for the frame, fonts, metrics, palette, widgets and input model. Its text-metric model
   (line box = 1.0 x font-size; header 0..98, title bar 98..138, page body 138..950, footer 950..1024) is normative.
   Page specs that assumed another line-height model (for example `04-fms-perf.md` §0.2 used a content top of 124)
   must be re-based on the frame spec's 138..950 body.
3. The page specs (03-06) for page content.
4. `07-fmc-logic.md` for FMS behaviour and the mapping onto existing port interfaces.

## 1. Principles

* **Simulator independence.** Everything except the renderer and the X-Plane adapter is plain C11 with no XPLM and
  no OpenGL includes, exactly like `a380x_avionics_core.c` and `a380x_guidance.c`. It must compile with the
  repository flags `-std=c11 -O2 -Wall -Wextra -Werror` on clang (macOS) and MinGW gcc (Windows).
* **No per-frame heap allocation.** Fixed-capacity arrays sized for the worst page. Navdata loading may allocate
  (once, or per airport on demand) and must free everything in shutdown.
* **One FMS, two MFDs.** A single `A380XFms` state is shared by both sides. UI state (navigation, menus, focus,
  edit buffer, cursor) is per side.
* **Faithful FBW translation first**, with the `[PORT]` fixes recommended in the specs. Where FBW shows a
  placeholder, the port may implement the real A380 behaviour only when the data exists in X-Plane; otherwise keep the
  FBW placeholder (dashes / disabled / NOT AVAIL). Never invent data.
* **Testable offline.** Every page must render through the same display list to SVG in `test_mfd_render.c`.

## 2. Coordinates, colours, text

* Canvas: 768 x 1024 per side, origin top-left, y down, float pixels (`MfdRect {x, y, w, h}`).
* Colours: `MfdColor` enum over the palette of spec 01 §3 (renderer owns the RGB table).
* Fonts: `MFD_FONT_SZ` (FBW-Display-EIS-A380-SlashedZero) and `MFD_FONT_ECAM` (plain-zero EIS). Both monospace:
  advance = `0.626953 * size`, baseline = `0.7998 * size` below line top. Strings are UTF-8; the only non-ASCII
  code points used are `°` U+00B0, `▯` U+25AF, `□` U+25A1, `◇` U+25C7, `←` / `→` / `↑` / `↓` must be drawn as
  polygons, not glyphs. Text width = code-point count x advance.
* Text anchor: left / centre / right at the given baseline point.

## 3. Module and file ownership

All paths are under `native/avionics/`. The owner column names the work package (WP) in §8 that creates the file.
Nobody edits a file owned by another WP; request changes through the integrator (WP-J).

| File | Kind | Content | WP |
|---|---|---|---|
| `a380x_mfd_types.h` | header | sides, keys, colours, fonts, rects, display list types, ids | arch |
| `a380x_mfd_draw.h` / `.c` | core | display-list builder: primitives, text, bevels, clip, layers | A |
| `a380x_mfd_ui.h` / `.c` | core | per-side UI state, event queue, hit list, every widget of spec 01 §13, cursor | A |
| `a380x_mfd.h` / `.c` | core | MFD top level: two sides, router/URI model, page registry, header/title/footer, overlays (MSG LIST, NOT AVAIL, NOT FOUND, DUPLICATE NAMES host), KCCU page keys, CLR INFO, DU on/off | A |
| `a380x_mfd_formats.h` / `.c` | core | all data-entry formats (spec 02), descriptor dispatch, error codes | B |
| `a380x_fms_messages.h` / `.c` | core | FMS message catalogue + shared queue semantics (spec 02 §10, 07 §7) | B |
| `a380x_fms_speeds.h` / `.c` | core | A380 speed tables and algorithms (spec 04 App. A, 07 §5) | C |
| `a380x_fms.h` / `.c` | core | FMS state: phase manager, INIT/FUEL/PERF stores per plan, weights/fuel, defaults, entry functions, outputs | C |
| `a380x_navdata.h` / `.c` | core | X-Plane navdata files: fixes, navaids, airways, CIFP procedures/runways, cycle (spec 11); XPLM lookups via callbacks | D |
| `a380x_fms_fpln.h` / `.c` | core | flight-plan model and operations: active/TMPY/SEC1-3, legs, discontinuities, procedures, DIR TO, airways, holds, constraints, distance/track computations (spec 05 §2) | D |
| `a380x_mfd_page_init.c`, `a380x_mfd_page_fuel.c`, `a380x_mfd_page_data.c`, `a380x_mfd_page_sec.c` | core | INIT, FUEL&LOAD, DATA STATUS/AIRPORT, SEC INDEX (spec 03) | E |
| `a380x_mfd_page_perf.c` | core | PERF all tabs (spec 04) | F |
| `a380x_mfd_page_fpln.c`, `a380x_mfd_page_fpln_rev.c` | core | F-PLN, revisions menu, INSERT NEXT WPT, NEW DEST, DEP, ARR, AIRWAYS, DIR TO, HOLD, VERT REV, FIX INFO, DUPLICATE NAMES (spec 05) | G |
| `a380x_mfd_page_position.c`, `a380x_mfd_page_surv.c`, `a380x_mfd_page_atccom.c`, `a380x_mfd_page_fcubkup.c` | core | POSITION MONITOR/IRS/NAVAIDS, SURV, ATCCOM, FCU BKUP AFS CP / EFIS CP (spec 06, 10) | H |
| `a380x_mfd_font.h`, `tools/build_mfd_font.py` | generated | MFD glyph atlas (both families) | I |
| `a380x_mfd_render.inc` | GL | executes a display list with OpenGL 2D + MFD font atlas | I |
| `test_mfd_render.c` | test | SVG backend + renders every page in representative states | I |
| `a380x_mfd_adapter.inc` | XPLM | cockpit devices, popups, KCCU commands, keyboard capture, MFD/KCCU datarefs (split made, spec 09) | J |
| `a380x_fms_adapter.inc`, `a380x_mfd_systems_host.inc` | XPLM | navdata + flight-plan store, FMS inputs from sim, FMS outputs to FWS/PFD/EWD/guidance, `a380x/xp12/fms/*`, systems host (SURV, FCU BKUP, POSITION, ATCCOM D-ATIS) | J |
| `a380x_mfd_pages.c`, `test_mfd_adapter.c` | core / test | page registration; mock-XPLM adapter test | J |
| `test_mfd_ui.c`, `test_mfd_formats.c`, `test_fms_core.c`, `test_navdata.c` (+ `test_data/` fixtures) | tests | owned by A, B, C, D respectively | A-D |

Existing files touched only by WP-J (integrator): `a380x_avionics.c`, `a380x_avionics_core.c/.h` (V1 fix),
`a380x_pfd_data.h`, `a380x_pfd_inputs.inc`, `a380x_fcu_adapter.inc`, `a380x_guidance.h`, `a380x_ewd_data.inc`,
`a380x_efis_nd.inc`, `build.py`, `README.md`, `tools/avionics_panel_layout.py`, `tools/avionics_provider.py`,
`tools/build_cockpit_controls.py`, `tools/test_cockpit_controls.py`, `tools/validate_aircraft.py`,
`config/cockpit_controls.json`, docs.

## 4. Display list (`a380x_mfd_types.h`, `a380x_mfd_draw.c`)

* Fixed-capacity command buffer per side (`MFD_DRAW_MAX_CMDS` 4096, `MFD_DRAW_MAX_POINTS` 16384,
  `MFD_DRAW_MAX_TEXT` 32768 bytes).
* Commands: filled rect, stroked rect (line width), line, polyline (open/closed), filled convex polygon,
  filled triangle, text (font, size, colour, anchor, baseline point, UTF-8 string), clip push/pop (axis-aligned
  scissor).
* Every command carries a **layer** (z of spec 01 §5.2: 0 page, 5 overflow, 10 context/dialog, 20 overlay,
  60 menu, 99 cursor). The renderer draws by ascending layer, preserving emission order inside a layer.
* Overflow of any buffer sets a sticky `overflowed` flag (tests assert it is never set); commands are dropped, never
  written out of bounds.

## 5. UI model (`a380x_mfd_ui.h/.c`)

Immediate-mode GUI with **last-frame hit testing**:

1. The adapter (or a test) pushes raw events into the side's queue at any time: pointer move (canvas px), button
   down/up, wheel, key (spec 01 §12.5 `MfdKey`), pointer leave.
2. At the start of a frame `mfd_ui_begin()` resolves queued events against the **previous frame's hit list**
   (ordered by layer, then emission order; topmost wins) and produces this frame's interaction facts: `hovered_id`,
   `pressed_id`, `clicked_id`, wheel target, keys for the focused field, outside-click closing of menus, mouse-down
   blur/commit of the focused field (spec 01 §11.3).
3. Pages call widgets. A widget registers its hit rect (with its layer) for the *next* frame and returns its
   interaction result for *this* frame (e.g. `mfd_button()` returns 1 when `clicked_id == id`).
4. `mfd_ui_end()` swaps hit lists, draws the cursor (layer 99) and advances timers.

Widget ids are `uint32_t`: `MFD_ID(scope, n)` = `(scope << 16) | n` with scope = page id or a frame scope. Lists
use `MFD_ID(scope, base + row)`.

Required widgets (geometry and states per spec 01 §13): bevel primitive, Button (incl. menu button with item list,
asterisk/arrow variants, selected/opened/disabled), IconButton (arrows up/down/left/right, ECL-style), InputField
(complete state machine of spec 01 §13.4 including mandatory amber boxes, computed small green/cyan, entered cyan,
inactive, disabled, TMPY yellow, overflow, alignment, leading/trailing units, caret, validating grey,
ESC-cancels port fix), DropdownMenu (incl. free-text filtering and wheel scrolling), PageSelectorDropdownMenu,
TopTabNavigator (slanted tabs), RadioButtonGroup, ContextMenu, ConfirmationDialog, SURV widgets, labels/values
helpers, horizontal rules, frames.

InputField contract: the page passes an `MfdFieldDesc` (format id + flags) and the current value (`MfdFieldValue`:
`has_value`, `is_computed`, numeric or text payload). The widget returns `MFD_FIELD_NONE`, `MFD_FIELD_COMMITTED`
(parsed value in an out-parameter, already validated by the format) or `MFD_FIELD_CLEARED`. Format errors are pushed
to the shared FMS message queue by the UI (type I message, spec 02 §1.7); the page never sees invalid input. Range
checks that depend on FMS state (e.g. V1 <= VR) are done by the FMS entry function, which returns an error code
that the page forwards with `mfd_ui_error(ui, err)`.

## 6. MFD top level and pages (`a380x_mfd.h/.c`)

* URI strings exactly as FBW (`fms/active/perf`, `fms/sec2/f-pln`, `surv/controls`, `fcubkup/afs`, ...), parsed
  into `sys / category / page / extra`. Navigation stack capped at 32 (spec 01 §10, quirk 16).
* Page registry: a static table of `{uri pattern, MfdPageId, title, MfdPageFn}`. A page function has the signature
  `void page(MfdPageContext *ctx)` and gets: the side's `MfdUi`, the shared `A380XFms *`, `A380XMfdSide *` (uri,
  plan index for active/sec1-3, page-local persistent state union), `entered` (first frame after navigation),
  `now`, and helpers to navigate.
* Page-local state lives in `A380XMfdSide.page_state` (a union of per-page structs declared in `a380x_mfd.h`). Each
  page WP owns its struct definition in a small header it creates (`a380x_mfd_page_<x>.h`), and `a380x_mfd.h`
  includes those headers to size the union. To keep the build independent of page progress, `a380x_mfd.h` initially
  reserves a fixed 16 KiB byte array and each page asserts at compile time that its struct fits.
* Header, title bar, footer, MSG LIST, NOT AVAIL, NOT FOUND are drawn by `a380x_mfd.c`; FCU BKUP and ATCCOM
  stub routes that FBW renders as NOT FOUND are now real pages (WP-H).

## 7. FMS, navdata and adapter boundaries

* `A380XFms` (WP-C) owns: FMGC flight phase (0..7), per-plan data stores (ACTIVE, TMPY, SEC1-3) for INIT, FUEL&LOAD
  and PERF, the message queue (via WP-B module), weights/fuel computations, speed computations (WP-C speeds), defaults
  (THR RED/ACC/TRANS), outputs (`A380XFmsOutputs`, spec 07 §10.3), and a pointer to the flight-plan store (WP-D).
* `A380XFmsInputs` (spec 07 §9.4) is filled by the adapter each frame: time, position, altitudes, speeds, weights,
  fuel, engine state, FCU/guidance state, IRS/GPS state, radios, transponder/TCAS/WXR/TAWS state.
* Navdata (WP-D) reads files only. Airport reference points/elevation and anything the files lack come through an
  `A380XNavdataHost` callback table (the adapter implements it with `XPLMFindNavAid`/`XPLMGetNavAidInfo`; tests use a
  fake). File roots are passed in (X-Plane system path + `Custom Data` override precedence, spec 11).
* Commands sent from MFD pages to aircraft systems (FCU BKUP knobs/buttons, EFIS CP, SURV transponder/TCAS/WXR/TAWS
  settings) go through an `A380XMfdSystemsHost` callback table implemented by the adapter, so pages stay XPLM-free.

## 8. Work packages

| WP | Scope |
|---|---|
| arch | `a380x_mfd_types.h` and **all public headers** listed in §3, with complete type and function declarations and doc comments; this file's decisions; a compile-only smoke test that includes every header. |
| A | draw + ui + mfd top level + frame pages + `test_mfd_ui.c` |
| B | formats + messages + `test_mfd_formats.c` |
| C | speeds + FMS core + `test_fms_core.c` |
| D | navdata + flight plan + `test_navdata.c` + small fixtures |
| E | INIT, FUEL&LOAD, DATA, SEC INDEX pages |
| F | PERF page |
| G | F-PLN family pages |
| H | POSITION, SURV, ATCCOM, FCU BKUP pages |
| I | font atlas generator + GL renderer + SVG render test of every page |
| J | X-Plane adapter, integration into the plugin, build/test contracts, cockpit OBJ (MFD device quads + KCCU click targets), provider manifest, validation, docs |

Each WP compiles its files with `clang -std=c11 -Wall -Wextra -Werror -fsyntax-only` against the headers, and runs
its own unit test where it has one. Final linking and the full `build.py` run happen in integration.
