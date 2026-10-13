# 04 — FMS `ACTIVE/PERF` page (MFD) — implementation spec

Status: implementation-ready spec for the native C MFD module (core + OpenGL renderer + X-Plane adapter).
Upstream: FlyByWire `2baa2b35` (GPL-3.0). Paths below are relative to
`upstream-aircraft/fbw-a380x/src/systems/` unless stated otherwise:

| Short name | File |
|---|---|
| `PERF` | `instruments/src/MFD/pages/FMS/MfdFmsPerf.tsx` (3529 lines) |
| `PERF.scss` | `instruments/src/MFD/pages/FMS/MfdFmsPerf.scss` |
| `FMC` | `instruments/src/MFD/FMC/FlightManagementComputer.ts` |
| `ACIF` | `instruments/src/MFD/FMC/FmcAircraftInterface.ts` |
| `FMGC` | `instruments/src/MFD/FMC/fmgc.ts` |
| `DEF` | `instruments/src/MFD/pages/common/DataEntryFormats.tsx` |
| `IF` | `instruments/src/MsfsAvionicsCommon/UiWidgets/InputField.tsx` |
| `TTN` | `instruments/src/MsfsAvionicsCommon/UiWidgets/TopTabNavigator.tsx` |
| `WS` | `instruments/src/MFD/pages/common/widget-style.scss` |
| `PAGE` | `instruments/src/MFD/pages/common/FmsPage.tsx` |
| `MSG` | `instruments/src/MFD/shared/NXSystemMessages.ts` |
| `OS` | `shared/src/OperatingSpeeds.tsx` |
| `OA` | `shared/src/OperatingAltitudes.tsx` |
| `PC` | `shared/src/PerformanceConstants.ts` |

Important source gap: the TypeScript alias `@fmgc/*` points to `fbw-a32nx/src/systems/fmgc/src`
(`fbw-a380x/src/systems/tsconfig.json:41`), which is **not present** in the pinned checkout
(`fbw-a32nx/` is absent). Everything that lives there (FlightPlanPerformanceData defaults, FlightPlan default
THR RED/ACC computation, FlightPhaseManager, VNAV predictions) is marked **[reconstructed]** below and must be
checked against FBW `fbw-a32nx/src/systems/fmgc/src` at the same commit before being treated as authoritative.

Labels used throughout:

* **[FBW]** — behaviour implemented by FBW at the pinned commit (cite file:line).
* **[FBW-PH]** — FBW placeholder / disabled / not computed.
* **[PORT]** — recommended behaviour for the X-Plane port (deviation or completion), with reason.

---

## 0. Conventions

### 0.1 Coordinates

* Canvas: FBW MFD canvas **768 × 1024 px**, origin **top-left, x right, y down** (CSS convention, kept as is).
  The renderer maps this canvas into the cockpit panel texture; nothing here is in texture pixels.
* All boxes are `x, y, w, h` (top-left + size) or `x0..x1 / y0..y1` in **absolute canvas px**, rounded to
  0.5 px. FBW positions come from CSS flex/grid; derivations are given in short notes. Tolerance ±2 px.
* `y_c` = vertical centre of a single text line. Text is always cap-centred on its line box (see 0.3), so the
  renderer can draw every label with `baseline = y_c + 0.40 × font_px`.
* `x` of a text item is its **left** edge unless written `right=` (right-aligned) or `cx=` (centred).

### 0.2 MFD frame (defined by the frame/header spec, repeated here only as assumptions)

The PERF page is mounted in `.mfd-page-container` between the FMS header + title bar and the MFD footer
(`MFD/MFD.tsx:398-414`, `PAGE:320-331`). With the font metrics of 0.3 the nominal frame is:

| Region | y range | Notes |
|---|---|---|
| System selector row + page selector row + title bar `ACTIVE/PERF` | 0 .. 124 | title text = `"<CATEGORY>/PERF"`, e.g. `ACTIVE/PERF`, `SEC1/PERF` (`PAGE:122`) |
| **PERF content box** | **124 .. 959** (h = 835) | `CY0 = 124`, `CY1 = 959` |
| MFD footer (MSG LIST / CLEAR INFO + message area) | 959 .. 1024 | not part of this page |

If the frame spec fixes different `CY0/CY1`, shift every absolute y in this document by `ΔCY0`; the PERF
internal layout is anchored to the top of the content box, the page footer row (§11) and the bottom rows of
every tab are anchored to `CY1` (they are pushed down by `flex-grow:1` spacers). Everything between stays
attached to the top.

Title-bar side markers come from the base class (`PAGE:84-104`): `EO` (amber block, when engine-out) and
`TMPY` (yellow block, when a temporary flight plan exists) are shown on PERF; `PENALTY` is **not** shown on PERF.

### 0.3 Font, metrics, colours

Font: `FBW-Display-EIS-A380-SlashedZero.ttf` (slashed zero!) for everything on this page
(`WS:3-7`, `style.scss:25`). Monospace. Measured from the TTF: unitsPerEm 4096, advance 2568
(**char width = 0.627 × font_px**), winAscent 3287 / winDescent 3 → **line box = 0.803 × font_px**,
baseline at 0.8025 × font_px from line top, cap height ≈ 0.766 em. Char widths used in this doc:
20 px → 12.5, 21 → 13.2, 22 → 13.75, 24 → 15, 25 → 15.6, 26 → 16.3, 27 → 16.9, 28 → 17.5, 30 → 18.8 px.

Port note [PORT]: the existing atlas `native/avionics/a380x_ewd_font.h` is built from the non-slashed font and
has an empty glyph for `?` (code 63). The MFD needs a regenerated atlas from the **SlashedZero** TTF including
`?` (dialogs `ACTIVATE APPR ?`, `CLEAR EO ?`), `°` (176), `*` and `▯` (U+25AF, mandatory boxes; or draw boxes
as rectangles, see 2.1).

Colour tokens (`MsfsAvionicsCommon/definitions.scss:9-22`):

| Token | Hex | Used for |
|---|---|---|
| WHITE | `#ffffff` | labels, unselected text, SEC values |
| GREEN | `#00ff00` | FMS values (`mfd-value`), inactive fields, active-phase tab title, mode labels in flight |
| CYAN | `#00ffff` | pilot-editable field text, computed field text (small), checked radio |
| AMBER | `#e68000` | mandatory boxes, EO labels, ACTIVATE APPR / CLEAR EO button text, EFOB below min |
| YELLOW | `#ffff00` | V-speeds "to be confirmed", CONFIRM T.O SPDs text, TMPY marker |
| MAGENTA | `#ff94ff` | not used on PERF (see §16) |
| DARK_BLUE | `#0c66f7` (`rgb(12,102,247)`) | units (`KT`, `FT`, `FL`, `°C`, `%`, `T`, `KLB`, `NM`, `FT/MN`, `M`) |
| LIGHT_GREY | `#c4c6cf` | borders, separators, validating text |
| CSS `lightgrey` | `#d3d3d3` | inline-style separators (`border-left: 1px solid lightgrey`, tab slant strokes) |
| DARK_GREY | `#868a9c` | disabled text/buttons/radios |
| DARKER_GREY | `#272525` | button/dropdown/unselected-tab background, disabled field background |
| BLACK | `#000000` | background |

Text classes (`WS:3-77`):

| Class | Size | Colour |
|---|---|---|
| `mfd-label` | 20 px | WHITE (`.green/.amber/.magenta` variants) |
| `mfd-label.bigger` | 24 px | — |
| `mfd-label.biggest` | 28 px | — |
| `mfd-label-unit` | 20 px | DARK_BLUE |
| `mfd-value` | 25 px | GREEN; `.sec` → WHITE; `.amber`; `.white`; `.tmpy` → YELLOW |
| `mfd-value.bigger` | 30 px | — |
| field text (`mfd-input-field-text-input`) | 27 px, line-height 27 px | CYAN |
| field text computed (`computedByFms`) | **22 px** | CYAN |
| button text (`mfd-button`) | 22 px | WHITE; disabled DARK_GREY |
| radio text | 26 px | WHITE; checked CYAN; disabled DARK_GREY |
| tab title | round(36 × 0.58) = **21 px** | see §4 |

Spacing classes: `mfd-spacing-right` = 15 px right margin; `mfd-unit-leading` = 6 px right margin;
`mfd-unit-trailing` = 6 px left margin; `mfd-label-value-container` = padding 7 px, flex row, items v-centred.

---

## 1. Page context and state

### 1.1 URIs and entry points

* `fms/active/perf`, `fms/sec1/perf`, `fms/sec2/perf`, `fms/sec3/perf` (`MfdPageDirectory.tsx:65-77`).
* Optional 4th URI segment selects the tab: `to | clb | crz | des | appr | ga` (`PERF:871-894`);
  e.g. INIT page button `T.O. PERF` navigates to `fms/<cat>/perf/to` (`MfdFmsInit.tsx:698-701`).
* KCCU `PERF` key → `fms/active/perf` (`MFD.tsx:246-248`). Header menu `ACTIVE ▸ PERF` → same
  (`FmsHeader.tsx:50`). `fms/active/perf` is force-reloaded even if already displayed (`MfdUiService.tsx:65`).
* FMC navigates here automatically on engine-out detection (`FMC:1717-1718`).

### 1.2 Loaded flight plan (`PAGE:188-275`)

| URI category | Loaded plan | `secActive` |
|---|---|---|
| `active` | Temporary if one exists, else Active | false |
| `secN` | Secondary N (if deleted while displayed → navigate to `active` page) | true (values WHITE) |

Derived predicates used everywhere (`PERF:97-266`). `phase` = FMGC flight phase
(`L:A32NX_FMGC_FLIGHT_PHASE`, enum below); `AOC` = loaded plan *is active or a copy of active*
(`isActiveOrCopiedFromActive()`); `AT` = loaded index is Active or Temporary.

| Predicate | Definition |
|---|---|
| `mandatoryAndActiveFpln` | `AT` |
| `visibleIfAT` | element `visibility` = visible iff `AT` (hidden keeps its space) |
| `toPageInactive` | `AOC && phase >= TAKEOFF` |
| `atOrAfterClimb` | `AOC && phase >= CLIMB` |
| `clbPageInactive` | `AOC && phase > CLIMB` |
| `crzPageInactive` | `AOC && phase > CRUISE` |
| `desPageInactive` | `AOC && phase > DESCENT` |
| `notYetInClimb` | `!AOC \|\| phase < CLIMB` |
| `notYetInCruise` | `!AOC \|\| phase < CRUISE` |
| `notYetInDescent` | `!AOC \|\| phase < DESCENT` |
| `notInDescent` | `!AOC \|\| phase != DESCENT` |
| `inFlight` | `CLIMB <= phase <= DESCENT` (no plan check) |
| `EO` | `fmgc.data.engineOut` |

`FmgcFlightPhase` (`shared/src/flightphase.ts:4-13`): `PREFLIGHT=0, TAKEOFF=1, CLIMB=2, CRUISE=3, DESCENT=4,
APPROACH=5, GOAROUND=6, DONE=7`. Tab index = phase − 1.

### 1.3 Refresh cadence [FBW]

* `drawPage()` at 1 Hz (`PERF:940-946`) and on every tab change (`PERF:1021`); it re-reads all
  performance data of the loaded plan (`PERF:812-863`) and recomputes the tab-specific values.
* `onNewData()` when the loaded plan version changes (polled every 500 ms, `PAGE:176-186`).
* VERT DEV at 0.5 Hz (`PERF:952-962`), only paused/resumed by flight-plan changes.

[PORT] Recompute everything every frame (or ≥4 Hz) from the core state; no visible behaviour depends on the
1 Hz quantisation.

### 1.4 Engine-out side effects visible on this page

* Title bar `EO` block, `EO MAX` label amber (§3), CI mode labels become `EO-LRC` / `EO-ECON`
  (`PERF:927-930`), CLB/CRZ line-1 mode label amber (§6, §7), `CLEAR EO` button (§11).
* On EO entry the FMC deletes preselected CLB/CRZ speeds and future cruise steps (`STEP DELETED`) and
  navigates to `fms/active/perf` (`FMC:1685-1719`). EO is set by `ACIF.checkEngineOut()` when flying and not
  all 4 engines N2 > 20 % (rising edge), cleared automatically when all engines run again (`ACIF:1726-1735`).

---

## 2. Widget states needed by this page (summary)

The generic widgets are specified in the common-widget spec; only the states PERF relies on are summarised.

### 2.1 Input field (`IF`, `WS:498-629`)

Geometry: container `border 2px inset LIGHT_GREY`, `padding 4px 2px`, content height 37 → **outer height 49 px**.
Inside, left→right: leading unit span (20 px DARK_BLUE, +6 px right margin even when empty), text area
(min width = `round(maxDigits × 18)` px, `IF:659-662`), trailing unit span (6 px left margin even when empty).
Outer width therefore = `2+2 + 6 + lead_w + max(text_w, 18·maxDigits) + 6 + trail_w + 2+2`.
If a page sets `containerStyle width:W`, outer width = `W + 8`.

| State | Container | Text |
|---|---|---|
| Pilot-entered value | bg BLACK, border LIGHT_GREY inset | CYAN 27 px |
| Computed value (`enteredByPilot=false`) | same | CYAN **22 px** |
| Empty, not mandatory | same | placeholder dashes, CYAN 27 px |
| Empty, **mandatory** (and not inactive/disabled) | same | every `-` of the placeholder replaced by `▯` (U+25AF), AMBER (`IF:370-381`) |
| Inactive (`inactive=true`) | border transparent, bg BLACK | GREEN (keeps 22/27 px rule), not focusable |
| Disabled | bg DARKER_GREY, border LIGHT_GREY | DARK_GREY, not focusable |
| Validating (≈500 ms after ENT) | — | LIGHT_GREY (`IF:383-441`) |
| Focused, before typing | — | text inverse: bg CYAN, text BLACK |
| Typing | — | CYAN text + blinking caret block 18×27 CYAN |
| Hover / KCCU cursor over | border CYAN | — |

Commit: on ENT (or blur) the text is parsed by the data-entry format (§13); on parse error the error message
is queued and the old value is kept; on success the handler stores the value. Empty input (CLR) = `null`
(clear pilot entry). ESC restores the previous value.

[PORT] Draw the mandatory `▯` as an AMBER rectangle outline 1.5 px, width 0.5 em, height = cap height,
in the character cell of each `-`.

### 2.2 Dropdown (`DropdownMenu.tsx`, `WS:137-223`)

Outer `border 2px outset LIGHT_GREY`, bg DARKER_GREY; inner black box height 40 → **outer height 44 px**;
right arrow cell 25 px wide with a 15×15 white down-triangle (grey `#808080` when disabled). Selected text uses
the input-field text style (27 px CYAN). Inactive: no border/arrow, text GREEN. Open list: bg DARKER_GREY,
items 20 px WHITE, padding 5×16, max height 275 px, item border 3 px CYAN on hover.

### 2.3 Button (`Button.tsx`, `WS:79-121`)

22 px text, `padding 9 12 5 12`, `border 2px outset LIGHT_GREY`, bg DARKER_GREY. Height: 1 line **35.7 px**,
2 lines **53.3 px**. Width = text width + 24 (+ borders 4). Disabled: text DARK_GREY, no action.
Buttons with `*` use a label `[two-line text block] 10px gap [*]`, `padding-right 2px`.

### 2.4 Radio group (`RadioButtonGroup.tsx`, `WS:364-461`)

Each option: label row (border 2 transparent) = circle 30 px + 2 px inset border (**34 px outer**),
15 px gap, 26 px text. Checked: CYAN disc of Ø22 inside a 4 px black ring, text CYAN. Disabled: circle filled
DARK_GREY, text DARK_GREY. Row height 38 px; `additionalVerticalSpacing=15` → 15 px top margin per row
(**53 px pitch**).

### 2.5 Confirmation dialog (`ConfirmationDialog.tsx`)

Absolute box, bg BLACK, `border 2px outset LIGHT_GREY`, padding 5, margin 5; title area padding-top 20 with a
20 px label (WHITE or AMBER); bottom row: `CANCEL` button (left) and `CONFIRM *` button (right,
`padding-right 6`). Modal for this page.

---

## 3. Top strip (all tabs)

Grid `margin 5; grid-template-columns 1fr 1fr 1fr` (`PERF:1365`): x 5..763, 3 columns of 252.7 px
(5..257.7, 257.7..510.3, 510.3..763). Row 1 height 37 (explicit, y 129..166), row 2 height 20 (y 166..186).

| Item | Geometry | Content / rules |
|---|---|---|
| `CRZ` label | x 25, y_c 147.5, 20 px WHITE | always |
| CRZ FL field | x 78, y 123, w 99, h 49 (overflows row by 6 px each side) | `FlightLevelFormat`: leading unit `FL`, 3 digits, e.g. `FL 350`. Mandatory (amber `▯▯▯`) iff loaded plan is **Active** and FMGC phase < DESCENT (`PERF:767-770`; evaluated on plan change). Never inactive. Handler `trySetCruiseFl` (§13.3) |
| `OPT` group | centred in col 2 (cx 384): `OPT` x 319 + `FL` x 371 + value x 402 (3 chars); y_c 147.5 | `visibleIfAT`. Value GREEN 25 px; `---` WHITE when not available |
| `REC MAX` group | right-aligned to x 743: `REC MAX` x 563, `FL` x 665, value x 696..743; y_c 147.5 | `visibleIfAT`. Same colour rule |
| `EO MAX` group | right-aligned to x 743: label `" EO MAX"` (leading NBSP) x 563, `FL` 665, value 696; y_c 176 | `visibleIfAT`. Label AMBER when EO, else WHITE |

Values (`PERF:1040-1051`, `FMC:665-696`, only for Active/Temporary; secondary plans keep `---`):

* REC MAX FL = `getRecMaxAltitude()/100`, string `toFixed(0)`; `null` → `---` WHITE.
* OPT FL = `floor(0.96 × RECMAX_FL / 5) × 5` if not EO and phase ≤ CRUISE, else `null` → `---` WHITE.
* EO MAX FL = `floor(0.8 × RECMAX_FL / 5) × 5`.
* Formulas in §12.6. [FBW-PH] OPT/EO MAX are FBW "magic" ratios (`FIXME remove magic`), not real optimum
  altitude computations.

---

## 4. Phase tab bar

`TopTabNavigator` (`TTN`) with titles `T.O | CLB | CRZ | DES | APPR | GA` (`PERF:1419-1427`), container margin
2 px → x 2..766. Bar height **36**, y **193..229**.

Geometry: tab width `W = (764 + 5×15)/6 = 139.83`, each tab overlaps the previous by 15 px
(`margin-right:-15`, `TTN:56`). Tab *i* spans `x = 2 + 124.83·i .. +139.83`:

| Tab | x0 | x1 | title cx |
|---|---|---|---|
| T.O | 2.0 | 141.8 | 71.9 |
| CLB | 126.8 | 266.7 | 196.8 |
| CRZ | 251.7 | 391.5 | 321.6 |
| DES | 376.5 | 516.3 | 446.4 |
| APPR | 501.3 | 641.2 | 571.3 |
| GA | 626.2 | 766.0 | 696.1 |

Each tab = left slant triangle (w = 36·tan30° = **20.78**) + label rectangle + right slant triangle (`TTN:51-111`):

* Left triangle polygon `(x0, y+36) (x0+20.78, y) (x0+20.78, y+36)`, right triangle mirrored. Slanted edge
  stroke 2 px `#d3d3d3`.
* Label rectangle: `border-top 2px LIGHT_GREY`, padding 6/0/2/0, title 21 px, centred; title `y_c ≈ 213.5`.
* **Unselected**: triangles and label fill DARKER_GREY; 2 px bottom line (LIGHT_GREY) across the whole tab
  at y 227..229.
* **Selected**: fill BLACK, **no** bottom line (opens into the content frame), drawn on top of neighbours
  (z-index 1). Draw order: unselected tabs left→right, then the selected tab.
* Title colour (`TTN:36-42`): **GREEN if the tab is the active-phase tab** (`highlightedTab = phase−1`,
  `PERF:154`; precedence over selection), else WHITE (selected and unselected alike; FBW sets an invalid
  `'white;'` for unselected tabs which falls back to the inherited white).
  PREFLIGHT (−1) and DONE (6) highlight nothing.

Tab content frame (`WS:347-355`): `border 2px outset LIGHT_GREY` on left (x 2..4), right (764..766) and
bottom (y 898.7..900.7); no top border (the unselected tabs' bottom lines form it). Content clipped
(`overflow:hidden`) to x 4..764, y 229..898.7.

* T.O, APPR, GA: padding 10 → **inner box x 14..754, y 239.1..888.7** (h 649.6).
* CLB, CRZ, DES: `padding-top:0; padding-left:0` → **inner box x 4..754, y 229.1..888.7** (h 659.6).

### 4.1 Tab selection state machine [FBW] (`PERF:870-925`)

```
on page open:
  if URI extra in {to,clb,crz,des,appr,ga}: selected = that tab
  else if loadedPlan.isActiveOrCopiedFromActive(): selected = clamp(phase,1,6) - 1
       (PREFLIGHT -> T.O, DONE -> GA)
  else: selected = T.O
on tab click (touch/KCCU): selected = clicked tab (any tab, any phase, any plan)
on FMGC phase change (old -> new):
  if previousPhase != null && previousPhase != 0      // FBW: `if (this.previousFmsFlightPhase)` => PREFLIGHT is falsy
     && selected + 1 == previousPhase && AOC
     && new in {TAKEOFF..GOAROUND}:
       selected = new - 1          // page "follows" the phase only if the crew was looking at the phase tab
  previousPhase = new
```

[PORT] Keep the same rule; treat PREFLIGHT as a valid previous phase (FBW quirk: going PREFLIGHT→TAKEOFF never
auto-switches; harmless because T.O is already shown).

---

## 5. T.O tab

Inner box x 14..754, y 239.1..888.7. Vertical flex column (`PERF:1428-2062`):

| Block | y range | Derivation |
|---|---|---|
| A — RWY / T.O SHIFT (`.mfd-fms-perf-to-first`, `flex:1`) | 239.1..303.1 | min-content 63 + 1 px border; flex share of the free space (71.5 px) is below its min → frozen at 64 |
| B — V-speeds / thrust (`.mfd-fms-perf-to-second`) | 303.1..518.1 | 3 rows × 63 + 15 + 5 margin = 204, + 10 padding-bottom + 1 border |
| C — FLAPS/THS/PACKS/ANTI ICE grid | 528.1..624.1 | margin-top 10, label row 16, field row 64, padding 15, border 1 |
| D — THR RED / ACCEL / NOISE grid | 634.1..784.1 | margin 10, 3 rows × 50 |
| spacer | 794.1..801.7 | remaining 7.5 px |
| E — TRANS / EO ACCEL / CPNY T.O REQUEST | 811.7..885.7 | margin 10, border-top 1, padding-top 10, row 63, margin-bottom 3 |

Separator lines (1 px LIGHT_GREY): y 302.1 (x 14..754), y 517.1 (x 14..754), x 428..429 (y 308.1..507.1,
right border of the V-speed block), y 623.1 (x 14..754), vertical `#d3d3d3` x 253.7 (y 528.1..608.1),
y 811.7 (x 16..752).

### 5.1 Block A — RWY, T.O SHIFT

| Item | Geometry | Rules |
|---|---|---|
| `RWY` label | x 29, y_c 270.6 | — |
| runway value | x 81.5, y_c 270.6, `mfd-value` (GREEN / WHITE if SEC) | `originRunway.ident.substring(4).padEnd(4,' ')` (FBW idents are `ICAO+RWY`, e.g. `EGLL27L` → `27L `); `---` if no origin runway (`PERF:775`) |
| `T.O SHIFT` label | right = 615, y_c 270.6 | — |
| T.O SHIFT field | x 630, y 246, w 117 (unit `FT`) / x 642.5, w 104.5 (unit `M`); right edge 747 | `LengthFormat(min 1, max = origin runway length m, unit per metric setting)`, 4 digits, trailing unit `M` or `FT` (`CONFIG_USING_METRIC_UNIT`). Stored in **metres**. Disabled when no origin runway; inactive when `toPageInactive`. Not mandatory. Key `takeoffShift` |

### 5.2 Block B — V-speeds, F/S/GD, thrust setting

V-speed grid (`.mfd-fms-perf-to-v-speeds`, `PERF.scss:43-52`): x 14..429, 3 auto columns with
`justify-content: space-between`, rows at y **318.1 / 381.1 / 444.1** (63 tall), row `y_c` **349.6 / 412.6 / 475.6**.

Normal state (no speeds to confirm):

| Col / row | Item | Geometry |
|---|---|---|
| 1 / r | label `V1`, `VR`, `V2` | x 21, y_c of row |
| 1 / r | V field (`SpeedKnotsFormat 90..340`, unit `KT`, right-aligned text) | x 61, y = y_c−24.5, w 99, h 49 |
| 3 / 1 | `F` label + value + `KT` | label x 305, value x 333 (3 chars), unit x 386; y_c 349.6 |
| 3 / 2 | `S` label + value + `KT` | same x, y_c 412.6 |
| 3 / 3 | green-dot symbol + value + `KT` | circle centre (311, 475.1), r 5, stroke 2 GREEN, black fill; value x 333; unit x 386; y_c 475.6 |

Values F/S/GD: `mfd-value` (GREEN, WHITE if SEC), text = `toFixed(0)` or `---` (`FmgcData.fmcFormatValue`,
`FMGC:73-75`). Source: `fmgc.data.flapRetractionSpeed / slatRetractionSpeed / greenDotSpeed` for Active/Temporary,
`null` (→ `---`) for SEC plans (`FMC:699-720`). Formulas §12.2.

V-field rules (`PERF:1459-1583`):

* Mandatory iff `AT` (amber `▯▯▯ KT` when empty); inactive (static GREEN) when `toPageInactive`.
* On commit: store `v1|vr|v2` in the loaded plan **and** write `L:AIRLINER_V1_SPEED|VR|V2` (kt) — note the
  simvar is written even when a SEC plan is loaded (FBW quirk; the FMC loop rewrites it from the active plan
  every cycle, `ACIF:559-567`, so it self-corrects). Clear writes `null`.
* No ordering check at entry; the disagree check is a separate message (§13.4).

**Speeds to be confirmed** (runway change, `PAGE:279-306`, `PERF:303-341`): when the *active* plan's origin
runway ident differs from `fmgc.data.vSpeedsForRunway` (first assignment just records the runway), the FMS
moves V1/VR/V2 into `v1/vr/v2ToBeConfirmed`, clears the plan's V-speeds (fields become amber boxes) and queues
`CHECK T.O. DATA` (resolved when VR is set again). While no V-speed is set and at least one *to-be-confirmed*
value exists:

* column 3 (F/S/GD) is hidden; column 2 shows the three pending values in **YELLOW 25 px** + `KT`
  (x 178 / unit x 231, row y_c);
* a `CONFIRM` / `T.O SPDs` `*` button (YELLOW text) is shown bottom-right of the grid:
  **x 266, y 453.7, w 152, h 53.3**. Press → copy the three pending values into V1/VR/V2 (plan + simvars) and
  clear the pending values (`PERF:1499-1523`).

**Thrust selection** — radio group in box x 429..629, y 303.1..468.1 (`PERF:1626-1637`):

| Row | Text | Circle centre | Text x | Enabled |
|---|---|---|---|---|
| 0 | `TOGA` | (463, 340.1) | 495 | iff `phase < TAKEOFF` |
| 1 | `FLEX` | (463, 393.1) | 495 | iff `phase < TAKEOFF` |
| 2 | `DERATED` | (463, 446.1) | 495 | **never** [FBW-PH] (`PERF:434-437`: third entry hard-coded `true`) |

(The enable rule uses the FMGC phase regardless of which plan is loaded.)
Selected index = `takeoffPowerSetting` (`TOGA=0, FLEX=1, DERATED=2`). On selection (`PERF:369-387`): store in the
loaded plan; if the loaded plan is **Active** write `L:A32NX_AIRLINER_TO_FLEX_TEMP` = FLEX temp (`0 → 0.1`,
`null → 0`) for FLEX, `0` for TOGA/DERATED.

| Item | Geometry | Rules |
|---|---|---|
| FLEX temp field | x 636, y 369.1, w 99, h 49 (vertically aligned with the FLEX radio) | Visible (not just enabled) only when FLEX selected. `TemperatureFormat 0..99`, display `+NN`, unit `°C`. Inactive when `toPageInactive`. Not mandatory. On commit: if loaded plan Active write `L:A32NX_AIRLINER_TO_FLEX_TEMP = (v==0 ? 0.1 : v ?? 0)`; store `flexTakeoffTemperature` |
| DERATED dropdown | x 629, y 425.1, w 100, h 44 | Visible only when DERATED selected (unreachable in FBW). Items `D01 D02 D03 D04 D05`; free text off; inactive when `toPageInactive`. Selecting opens the derated dialog |
| Derated dialog | FBW: x 434, y 308.1, w 339, h 179 (overflows the frame by 9 px) | Title `DERATED D0n` WHITE; CONFIRM → store `takeoffDeratedSetting = new`; CANCEL → store previous (`PERF:1604-1625`). [PORT] place at x 415 so it stays inside the frame |

### 5.3 Block C — FLAPS / THS FOR / PACKS / ANTI ICE (`PERF:1687-1808`)

Grid columns (auto, stretched): 14..119.1 | 119.1..253.7 | 253.7..268.7 (separator) | 268.7..498.8 | 498.8..753.9.

| Item | Geometry | Rules |
|---|---|---|
| labels `FLAPS`, `THS FOR`, `PACKS`, `ANTI ICE` | x 14 / 119 / 269 / 499, y_c 536.1 | WHITE 20 px |
| FLAPS dropdown | x 14, y 559.1, w 75, h 44 | items `1 2 3`, selected = `takeoffFlaps − 1` (`takeoffFlaps` = FlapConf 1..3; null/0 → no selection, shows `-`). Inactive when `toPageInactive`. On select: store `takeoffFlaps = idx+1`; if loaded Active → `ACIF.setTakeoffFlaps()` (FM DISCRETE_WORD_2 bits, §15). Not mandatory [FBW] |
| THS FOR field | x 119, y 559.1, w 104.5, h 49 | `PercentageFormat 0..99.9`, display `NN.N`, trailing `%`, right-aligned. **Mandatory iff `AT`** (`▯▯.▯%`). Inactive when `toPageInactive`. On commit **only if value truthy** (`if (v)`, `PERF:1734`): store `takeoffThsFor`, `ACIF.setTakeoffTrim(v)`. ⇒ 0.0 and CLR are silently ignored [FBW quirk] |
| PACKS dropdown | x 269, y 559.1, w 200, h 44 | items `OFF/APU`, `ON` (`TakeoffPacks 0/1`). Inactive when `toPageInactive`. On select: if `enginesWereStarted` queue `CHECK T.O. DATA`; store `takeoffPacks` |
| ANTI ICE dropdown | x 499, y 559.1, w 225, h 44 | items `OFF`, `ENG ONLY`, `ENG + WING` (`TakeoffAntiIce 0/1/2`). Same rules, key `takeoffAntiIce` |

`enginesWereStarted` = phase ≥ TAKEOFF, or PREFLIGHT and any engine N2 > 20 % (`FMC:1474-1481`).

### 5.4 Block D — THR RED / ACCEL / NOISE (`PERF:1809-1999`)

5 auto columns over x 14..734 (margin-right 20): 14..172.4 | 172.4..348.8 | 348.8..467.2 | 467.2..603.6 |
603.6..734. Rows: y 634.1 / 684.1 / 734.1, 50 px pitch.

| Item | Geometry | Rules |
|---|---|---|
| `THR RED` label | right = 139, y_c 659.1 | — |
| THR RED field | x 172.4, y 634.6, w 158, h 49 | `AltitudeOrFlightLevelFormat(transAlt)` (§13.1), right-aligned. Small CYAN 22 px when computed (default), 27 px when pilot-entered. Inactive when `toPageInactive`. Commit → `pilotThrustReductionAltitude` (CLR → back to default) |
| `ACCEL` label | right = 139, y_c 709.1 | — |
| ACCEL field | x 172.4, y 684.6, w 158, h 49 | same format; key `pilotAccelerationAltitude` |
| noise arrow + `N1` | arrow 35×35 at x 367.2, y 634.1; `N1` right = 452.2, y_c 651.6 | visible only when noise enabled |
| N1 field | x 467.2, y 634.1, w 118, h 49 | `PercentageFormat 40..110` (`NN.N%`); key `noiseN1`; inactive when `toPageInactive`; visible only when noise |
| noise arrow + `SPD` | arrow at x 367.2, y 684.1; `SPD` right = 452.2, y_c 701.6 | noise only |
| SPD field | x 467.2, y 684.1, w 118, h 49 | `SpeedKnotsFormat 90..340`; key `noiseSpeed`; noise only |
| `CANCEL` / `NOISE` button | x 604.4, y 638.5, w 110.5, h 53.3 | noise only; hidden when `toPageInactive`; press → `noiseEnabled=false` |
| `NOISE END` label | right = 139, y_c 759.1 | noise only |
| `NOISE` button | x 173, y 736.3, w 97, h 35.7 | shown when noise **off** and not `toPageInactive`. **Always disabled** [FBW-PH] (`PERF:1962`) |
| NOISE END field | x 181.6, y 734.1, w 158, h 49 | noise only; `AltitudeOrFlightLevelFormat(transAlt)`; key `noiseEndAltitude` |

Noise arrow polygon (viewBox 60×60 scaled to 35×35, WHITE fill):
`(0,28) (50,28) (50,20) (60,30) (50,40) (50,32) (0,32)` × 0.5833.

### 5.5 Block E — TRANS / EO ACCEL / CPNY T.O REQUEST (`PERF:2002-2061`)

Row y 822.7..885.7, y_c 854.2, `justify-content: space-between` over x 16..752.

| Item | Geometry | Rules |
|---|---|---|
| `TRANS` label | x 23, y_c 854.2 | — |
| TRANS ALT field | x 100.5, y 829.7, w 158, h 49 | `AltitudeFormat 1..43000`, unit `FT`; small font when value comes from the database (`transitionAltitudeIsFromDatabase`), large when pilot-entered. **Never inactive.** Commit → `pilotTransitionAltitude` then `ACIF.updateTransitionAltitudeLevel()` |
| `EO ACCEL` label | x 284.3, y_c 854.2 | — |
| EO ACCEL field | x 399.3, y 829.7, w 158, h 49 | `AltitudeOrFlightLevelFormat(transAlt)`; key `pilotEngineOutAccelerationAltitude`; inactive when `toPageInactive` |
| `CPNY T.O` / `REQUEST` button | x 577, y 827, w 174, h 53.3 | `visibleIfAT`; hidden when `toPageInactive`; **always disabled** [FBW-PH] |

---

## 6. CLB tab

Inner box x 4..754, y 229.1..888.7 (`PERF:2063-2502`).

### 6.1 Header row

| Item | Geometry | Rules |
|---|---|---|
| CI mode dropdown | x 11, y 238.6, w 175, h 44 | items `LRC`, `ECON` (`EO-LRC`, `EO-ECON` when EO). Index = `costIndexMode` (`LRC=0, ECON=1`). **Disabled** when `AOC && phase >= DESCENT`. Key `costIndexMode` |
| `CI` label | x 201, y_c 260.6 | — |
| CI field | x 241, y 236.1, w 83, h 49 | `CostIndexFormat 0..999`, no unit, centred text. **Disabled** when mode == LRC or (`AOC && phase >= DESCENT`). Key `costIndex` |
| `DERATED CLB` label | x 469.5, y_c 268.1 | — |
| DERATED CLB dropdown | x 622, y 246.1, w 125, h 44 | items `NONE 01 02 03 04 05` (`ClimbDerated 0..5`); **always inactive** [FBW-PH] → static GREEN text of the current value (`NONE` by default) |

(The CI group box has `margin-bottom 15`, so the header row is 78 px tall: y 229.1..307.1.)

### 6.2 Speed table (`.mfd-fms-perf-clb-grid`, columns 23 % / 22 % / 13 % / 42 %)

Columns: MODE x 4..176.5 (vertical separator 1 px at x 175.5), SPD 176.5..341.5 (cx 259), MACH 341.5..439
(cx 390.3), PRED 439..754 (cx 596.5). Every cell has a 1 px LIGHT_GREY bottom border; first-column cells have
the 1 px right border.

| Row | y range (pre-climb) | Content |
|---|---|---|
| H | 307.1..393.1 | `MODE` cx 90.3 / `SPD` cx 259 / `MACH` cx 390.3, all y_c 349.6. PRED cell (`visibleIfAT`): `PRED TO ` label x 510, y_c 325.1, field x 525, y 333.2, w 158, h 49 |
| L1 | 393.1..463.1 (70; 63.5 in flight) | mode label right = 165.5, y_c 428.1; SPD cell: input field (pre-climb) x 209.5, y 403.1, w 99, h 49 or value+`KT` centred cx 259; MACH value cx 390.3; PRED value cx 596.5 |
| L2 | 463.1..520.2 (57) | mode label right = 165.5, y_c 491.6; values centred as L1 |
| L3 | 520.2..551.3 (31) | cell padding 5/15: mode right = 160.5, y_c 535.7; values centred |
| filler | 551.3..591.3 | only the 1 px vertical separator in column 1 (40 px) |

[PORT] Keep row L1 fixed at 70 px in all phases (FBW shrinks it by 6.5 px in flight because the input field
disappears; the jump is an artefact).

PRED TO field (`PERF:2139-2155`): `AltitudeOrFlightLevelFormat(transAlt)`; value =
`climbPredictionsReference` = pilot entry ?? automatic (`FMGC:121-135`). Small font when automatic.
Inactive when `clbPageInactive`. Commit → `climbPredictionsReferencePilotEntry` (CLR → automatic).
Automatic value = FCU selected altitude while the loaded plan is Active and phase == CLIMB, else `null`
(set by `drawPage` while the CLB tab is shown, `PERF:1067-1075`, and once at TAKEOFF phase entry, `FMC:1262-1264`).

**Table content** (`PERF:1076-1149`). `SL` = climb speed limit (`climbSpeedLimitSpeed/Altitude`, default
250 kt / 10000 ft **[reconstructed]**), `ECON_SPD` = managed climb speed, `ECON_M` = `.84` (§12.4),
`fcu` = FCU target (kt if ≥ 1, Mach if < 1), `managed` = FCU speed managed (`fcuSpeedManaged == 1`),
`alt` = indicated altitude.

| Situation | L1 mode / SPD / MACH / PRED | L2 | L3 |
|---|---|---|---|
| phase < CLIMB | `PRESEL` / **input** `preselectedClimbSpeed` / – / – | `MANAGED` / `SL.speed` `KT` / – / `--:--   ----` | `ECON` / `ECON_SPD` `KT` / `ECON_M` / – |
| ≥ CLIMB, managed, `alt < SL.alt` | `MANAGED` / `SL.speed` / `.MM` of SL.speed / – | empty | `ECON` / `ECON_SPD` / `ECON_M` |
| ≥ CLIMB, managed, otherwise | `MANAGED` / `ECON_SPD` / `ECON_M` / – | empty | empty |
| ≥ CLIMB, selected | `SELECTED` / fcu kt or – / fcu Mach `.MM` or – / – | `MANAGED` / (`SL.speed`,`.MM`) if `alt < SL.alt` else (`ECON_SPD`,`ECON_M`) / – | `ECON` / `ECON_SPD` / `ECON_M` |

* Mach strings are `"." + toFixed(2).split('.')[1]` (e.g. `.84`). Speed strings `toFixed(0)`; the `KT` unit is
  drawn only if the speed string is non-empty. Values `mfd-value` (WHITE if SEC).
* Mach of `SL.speed` uses the sim `FROM KIAS TO MACH` at present conditions.
* PRESEL input: `SpeedKnotsFormat 90..340`, inactive when `atOrAfterClimb`, key `preselectedClimbSpeed`.
  Shown only when `notYetInClimb`, otherwise the static value + `KT` is shown in that cell.

Mode-label colours/sizes (`PERF:511-531`):

| Line | Colour | Size |
|---|---|---|
| L1 | AMBER if `inFlight && EO`; else GREEN if `(inFlight && !EO) \|\| (phase < CLIMB && preselectedClimbSpeed set)`; else WHITE | 28 px if `inFlight`, else 20 px |
| L2 | GREEN if `inFlight \|\| (phase < CLIMB && preselectedClimbSpeed == null)`; else WHITE | 20 px |
| L3 | WHITE | 20 px |

[FBW-PH] PRED columns (time/distance to the PRED TO altitude) are never computed (`TODO add predictions`);
only the placeholder `--:--   ----` appears pre-climb. The managed-speed reason (ECON / SPD LIM / CSTR) is not
displayed (`TODO`).

### 6.3 THR RED / ACCEL / NOISE / CLB SPD LIM grid (`PERF:2247-2443`)

Grid `padding-left 15; margin 10 20 10 0; 50 px rows`; columns over x 19..734: 19..199.2 | 199.2..372.4 |
372.4..487.6 | 487.6..620.8 | 620.8..734. Rows at y **601.3 / 651.3 / 701.3** (pre-climb layout).

| Item | Geometry | Rules |
|---|---|---|
| `THR RED` label | right = 144, y_c 618.8 | — |
| THR RED field | x 199.2, y 601.3, w 158, h 49 | as T.O; inactive when `atOrAfterClimb` |
| `ACCEL` label / field | right 144, y_c 668.8 / x 199.2, y 651.3 | as T.O; inactive when `atOrAfterClimb` |
| N1 arrow+label / N1 field | arrow x 387.6, y 601.3; `N1` right 472.6, y_c 618.8 / field x 487.6, y 601.3, w 118 | noise only; inactive when `atOrAfterClimb` |
| SPD arrow+label / SPD field | row 2 equivalents | noise only |
| `CANCEL`/`NOISE` button | x 622.2, y 624.6, w 110.5, h 53.3 | noise only; hidden when `toPageInactive` |
| `CLB SPD LIM` label | right = 169, y_c 718.8 | shown when noise off |
| speed-limit value | `SL.speed` x 206.2 + `KT` x 259.1, `/` x 291.1, `FL` x 313.7, `SL.alt/100` x 344.7; y_c 718.3 | `mfd-value`; shown when noise off. Uses `getClimbSpeedLimit()` (refreshed in `onNewData`) |
| `NOISE END` label / field | right 169 / x 199.2, y 701.3, w 158 | noise only |

Visibility of the whole grid: FBW hides it (`visibility:hidden`) when phase ≥ CLIMB, but only updates that
flag while the **T.O** tab is being drawn (`PERF:1062-1065`) — a bug. [PORT] Hide the grid whenever
`AOC && phase >= CLIMB`.

### 6.4 Bottom row (`PERF:2446-2501`)

Right-aligned flex row, y 822.7..885.7, y_c 854.2:

| Item | Geometry | Rules |
|---|---|---|
| `NOISE` button | x 217.1, y 836.3, w 97, h 35.7 | shown when noise off and not `toPageInactive`; **always disabled** [FBW-PH] |
| `TRANS` label | x 371.5, y_c 854.2 | — |
| TRANS ALT field | x 449, y 829.7, w 158, h 49 | same as T.O TRANS (shared datum) |
| `SPD CSTR` button | x 614, y 836.3, w 138, h 35.7 | disabled when `clbPageInactive`; → `fms/<cat>/f-pln-vert-rev/spd` |

---

## 7. CRZ tab

Inner box x 4..754, y 229.1..888.7 (`PERF:2503-2799`).

### 7.1 Header row (y 229.1..292.1)

CI mode dropdown x 11, y 238.6, w 175, h 44; `CI` label x 201, y_c 260.6; CI field x 241, y 236.1, w 83, h 49.
Rules identical to §6.1 (same data). No DERATED CLB.

### 7.2 Speed table (`.mfd-fms-perf-crz-grid`, 23 % / 13 % / 22 % / 42 %)

Columns: MODE 4..176.5 (separator x 175.5), **MACH** 176.5..274 (cx 225.3), **SPD** 274..439 (cx 356.5),
PRED 439..754 (cx 596.5). (Column order differs from CLB.)

| Row | y range | Content |
|---|---|---|
| H | 292.1..375.3 (83, STEP layout) | `MODE` / `MACH` / `SPD` labels; prediction block (below) |
| L1 | 375.3..445.3 (70) | mode right 165.5, y_c 410.3; MACH cell: **input** (pre-cruise) x 188.3, y 385.3, w 74, h 49; SPD cell: **input** x 307, y 385.3, w 99, h 49; PRED value + `NM` cx 596.5 |
| L2 | 445.3..502.4 (57) | `MANAGED` etc., y_c 473.9 |
| L3 | 502.4..527.4 (25) | always empty in FBW |
| LRC | 527.4..571.5 (44, no bottom border) | `LRC` right 170.5, y_c 549.4; MACH `.84` cx 225.3; SPD `---` cx 356.5 |
| MAX TURB | 571.5..615.6 | `MAX TURB` right 170.5, y_c 593.5; `.85`; `---` |

[FBW-PH] LRC `.84` / MAX TURB `.85` and their `---` speeds are hard-coded strings (`PERF:2710-2741`).

**Prediction block** in the header PRED cell (one of four blocks; `PERF:2556-2599`, logic `PERF:1151-1191`).
`pred = vnavDriver.getPerfCrzToPrediction()` (Active plan only, else none):

| Condition | Block | Geometry (cx 596.5) |
|---|---|---|
| `pred.reason == TopOfDescent` | `PRED TO` (20 px) + `T/D` (`mfd-value.bigger` 30 px) | y_c 321.2 |
| else if `pred.distanceFromPresentPosition < 20` | `STEP AHEAD` (20 px **GREEN**) | y_c 310.1 |
| else | line 1: `AT` + next-step waypoint ident (30 px value); line 2: `STEP TO` + `FL` + step FL (30 px) | y_c 314.2 / 345.2 |
| (never selected by FBW) | `DRIFT DOWN` / `TO` `FL` nnn | — [FBW-PH] |

Next cruise step: first leg at or after the active leg index carrying a `cruiseStep`; waypoint = that leg's
ident, step FL = `toAltitude/100` (`MfdFmsFplnVertRev.tsx:616-637`). Empty if none.
[PORT] Keep the header row height fixed at 83 px for all four blocks (FBW resizes the row: 83 / 59 / 37 px).

**Table content** (`PERF:1220-1273`). `CRZ_M` = managed cruise Mach `.85`, `CRZ_SPD` = 310 kt (§12.4).
`P` = `preselectedCruiseSpeed` (knots if > 1, Mach if < 1).

| Situation | L1 mode / MACH / SPD | L2 mode / MACH / SPD / PRED |
|---|---|---|
| phase < CRUISE | `PRESEL` / **input** (shows P if Mach) / **input** (shows P if kt) | `MANAGED` / `CRZ_M` / `---` `KT` / `--:--   ----` `NM` |
| ≥ CRUISE, managed | `MANAGED` / FCU in Mach ? `CRZ_M` : `.--` / FCU in Mach ? `---` : managed **climb** speed (FBW bug) | empty |
| ≥ CRUISE, selected | `SELECTED` / FCU Mach or – / FCU in Mach ? `---` : FCU kt | `MANAGED` / FCU in Mach ? `CRZ_M` : `.--` / FCU in Mach ? `---` : `CRZ_SPD` / – |

Inputs (`PERF:2613-2666`): shown while `notYetInCruise`; MACH input `SpeedMachFormat 0.1..0.89` (display `.NN`,
placeholder `.--`), SPD input `SpeedKnotsFormat 90..340`; both write the **same** key `preselectedCruiseSpeed`
(so entering one replaces the other); no inactive/mandatory state.

PRED column (`PERF:1193-1218`), only when `pred` has finite time and a distance:
`time = HH:MM` (`getEtaFromUtcOrPresent(dist<0 ? null : seconds, phase==PREFLIGHT)` — UTC ETA, or elapsed time
from now in PREFLIGHT; `--:--` if null), string = `time + distance.toFixed(0).padStart(6)`, unit `NM`
(unit drawn only if string non-empty). Before CRUISE it goes in L1 if P is set, else in L2; in CRUISE it goes in L1.

Mode-label colours (`PERF:602-622`): L1 AMBER if `inFlight && EO`; GREEN if `(inFlight && !EO) ||
(phase < CLIMB && P != null)`; 28 px if `inFlight`. L2 GREEN if `phase < CLIMB && managed && P == null`.

[PORT] In "≥ CRUISE, managed" show `CRZ_SPD` (managed cruise speed) instead of FBW's managed climb speed.

### 7.3 DEST row (CRZ and DES) (`PERF:2745-2798`, `PERF:1276-1291`)

Row y 850.0..885.7, y_c 867.8, `justify-content: space-between` over x 21..752 (padding-left 15):

| Item | CRZ x | DES x | Rules |
|---|---|---|---|
| `DEST` | 21 | 21 | `mfd-label.bigger` 24 px WHITE |
| dest ident | 129.6 | 152.9 | 24 px; GREEN if `AT` else WHITE; `----` if none |
| ETA | 238.2 | 284.7 | 24 px; same colour; `HH:MM` from `getDestinationPrediction().secondsFromPresent` (UTC, or from-now in PREFLIGHT); `--:--` |
| EFOB value | 368.8 | 438.6 | `mfd-value` 25 px (WHITE if SEC, AMBER if `destEfobBelowMinInActive && loaded==Active`); `NNN.N` (0.1 precision, t or klb), `---.-` if unknown |
| EFOB unit | 452.9 | 522.7 | `T` or `KLB` (metric setting) |
| `CMS` button | 521, y 850, w 69 | — | CRZ only; **always disabled** [FBW-PH] |
| `STEP ALTs` button | 600.3, w 152 | — | CRZ only; disabled when `crzPageInactive`; → `fms/<cat>/f-pln-vert-rev/step-alts` |
| `SPD CSTR` button | — | 614, w 138 | DES only; disabled when `desPageInactive`; → `fms/<cat>/f-pln-vert-rev/spd` |

ETA/EFOB are computed only when the loaded plan is Active and the CRZ or DES tab is drawn; otherwise the
initial `--:--` / `---.-` remain. EFOB = `getDestEFOB()` = VNAV destination prediction fuel (lb → t)
(`FMGC:458-467`). x positions assume unit `T`; with `KLB` the space-between gaps shrink by 6.25 px each.

---

## 8. DES tab

Inner box x 4..754, y 229.1..888.7 (`PERF:2800-3026`).

### 8.1 Header row (y 229.1..308.1)

| Item | Geometry | Rules |
|---|---|---|
| CI mode dropdown / `CI` / CI field | dropdown x 11, y 246.6; `CI` x 201, y_c 268.6; field x 241, y 244.1, w 83 | as §6.1, plus the CI field is **inactive** when `crzPageInactive` |
| `DES CABIN RATE` label | x 366, y_c 268.6 | `visibleIfAT` |
| cabin rate field | x 556, y 244.1, w 183, h 49 | `DescentRateFormat −999..−100`, 4 digits, unit `FT/MN`, right-aligned; any positive entry is negated (`350` → `-350`); display `value.toString()` (e.g. `-350`); inactive when `crzPageInactive`; key `descentCabinRate`. Default −350 **[reconstructed]** |

### 8.2 Speed table (crz-grid columns: MODE 4..176.5, MACH 176.5..274, SPD 274..439, PRED 439..754)

| Row | y range | Content |
|---|---|---|
| H | 308.1..394.2 | `MODE`/`MACH`/`SPD`; PRED cell (`visibleIfAT`): `PRED TO ` x 510, y_c 326.1; field x 525, y 334.2, w 158, h 49 |
| L1 | 394.2..464.2 (70) | mode right 165.5, y_c 429.2; MACH cell; SPD cell; PRED `--:-- ----` (hard-coded) cx 596.5 |
| L2 | 464.2..521.3 | only in "selected" situation |
| filler | 521.3..616.3 (95) | column-1 separator only, no bottom borders |

DES PRED TO field (`PERF:2878-2890`): `AltitudeOrFlightLevelFormat(transFl, isTransAltFlightLevel=true)` (FL
when value ≥ TRANS FL × 100), **disabled** when `notInDescent`. The value is page-local
(`desPredictionsReference`, not stored in the FMS, lost on page change) [FBW-PH]; predictions never computed.

**Table content** (`PERF:1293-1328`). `DES_SPD` = managed descent speed, `DES_M` = managed descent Mach (§12.4).

| Situation | L1 mode / MACH cell / SPD cell | L2 |
|---|---|---|
| phase < DESCENT | `MANAGED` / static empty / **input** showing `DES_SPD` (writes `pilotManagedDescentSpeed`) | empty |
| ≥ DESCENT, managed | `MANAGED` / **input** showing `DES_M` (writes `pilotManagedDescentMach`) / `DES_SPD` `KT` | empty |
| ≥ DESCENT, selected | `SELECTED` / **input** showing `DES_M` / FCU kt `KT` | `MANAGED` / `DES_M` / `DES_SPD` `KT` |

(The MACH input is shown when **in** descent and the SPD input when **not yet** in descent — `PERF:2904-2956`;
the inputs show the effective managed target, small/large font rule not applied → always 27 px.)
Inputs: MACH `SpeedMachFormat 0.1..0.89`, SPD `SpeedKnotsFormat 90..340`; CLR → pilot value null →
CI-based default.

Colours (`PERF:637-656`): L1 GREEN if `inFlight` (28 px if `inFlight`); L2 GREEN if `phase < CLIMB && managed`
(never true in practice).

DEST row: §7.3.

---

## 9. APPR tab

Inner box x 14..754, y 239.1..888.7 (`PERF:3027-3316`).

### 9.1 Header row (y 239.1..290.2, 1 px bottom border at y 289.2, x 14..754), y_c 264.1

| Item | x | Rules |
|---|---|---|
| `APPR` label | 29 | — |
| approach ident | 94 | `mfd-value`; `getApproachName(approach,false)` = type code + runway + `-<multipleIndicator>`; type codes: ILS→`ILS`, GLS→`GLS`, IGS→`IGS`, LOC→`LOC`, LOC BC→`BAC`, LDA→`LDA`, SDF→`SDF`, GPS→`GPS`, RNAV→`RNV`, VOR/VORDME/VORTAC→`VOR`, NDB/NDBDME→`NDB`, FMS→`RNAV` (`shared/utils.ts:22-52`). E.g. `ILS27L`, `RNV09-Y`. `-------` if none |
| dest ident | 371.7 | `mfd-value`; `----` if none |
| `LW` label | 602.4 | — |
| landing weight | 642.4 | `mfd-value`, `NNN.N` / `---.-`; §12.5 |
| unit | 726.5 | `T` / `KLB` |

### 9.2 Left column (x 14..425.1)

**Weather box** (border 1 px `#d3d3d3`): x 14..385.1, y 310.2..595.2.

| Item | Geometry | Rules |
|---|---|---|
| `MAG WIND` label | right = 152.6, y_c 353.7 | (label box shrinks from 125 to 102.6 px because the wind fields overflow; other rows use right = 175) |
| wind sub-box (border 1 `#d3d3d3`, padding 2) | x 167.6, y 326.2, w 201.5, h 55 | — |
| wind direction field | x 170.6, y 329.2, w 86.5, h 49 | `WindDirectionFormat 0..359`, `NNN` zero-padded, trailing `°`, centred text; disabled when no destination airport; key `approachWindDirection` |
| wind speed field | x 267.1, y 329.2, w 99, h 49 | `WindSpeedFormat 0..250`, `NNN` zero-padded, `KT`; disabled when no destination; key `approachWindMagnitude` |
| `HD`/`TL` label + value + `KT` | label x 45, value x 85, unit x 137.9; y_c 421.2 | label `TL` if headwind < 0 else `HD`; value `abs(HW)` 3-digit zero-padded or `---` |
| `CROSS` + value + `KT` | label x 192.9, value x 270.4, unit x 323.3; y_c 421.2 | `abs(XW)` 3-digit or `---` |
| `OAT` label | right = 175, y_c 490.7 | — |
| OAT field | x 190, y 466.2, w 133, h 49 | `TemperatureFormat −99..99`, `+NN`/`-NN`, `°C`; **mandatory** iff `approachParametersMandatory`; disabled when no destination; key `approachTemperature` |
| `QNH` label | right = 175, y_c 554.7 | — |
| QNH field | x 190, y 530.2, w 133, h 49 | `QnhFormat` (§13.1): placeholder `----` (FCU on hPa) or `--.--` (FCU on inHg, `ACIF:1792-1798`); **mandatory** iff `approachParametersMandatory`; disabled when no destination. Commit (non-null only): key `approachQnh` and `L:A32NX_DESTINATION_QNH` (hPa). CLR is a no-op [FBW quirk] |

`approachParametersMandatory = AT && (distanceToDestination ?? 0) <= 180 NM` (`PERF:1343-1344`) — note an unknown
distance counts as 0 ⇒ mandatory.
Head/cross wind (`FMC:464-489`): need destination runway and both wind entries, else `null`; formulas §12.3.
For the Active plan the headwind is the cached `approachHeadWindComponent` (updated on wind/runway change).

**MINIMUM box** (`PERF.scss:130-147`): border 1 px LIGHT_GREY, x 14..385.1, y 635.2..780.2
(precision approach) or 635.2..747.3 (non-precision: RADIO row has no field).
Legend `MINIMUM` 20 px WHITE on a black patch x 25..116.5, y 625.2..641.3 (interrupts the top border), text x 27,
y_c 633.3.

| Item | Geometry | Rules |
|---|---|---|
| `BARO` label | right = 175, y_c 675.7 | — |
| BARO field | x 190, y 651.2, w 158, h 49 | `AltitudeFormat 0..43000`, `FT`; key `approachBaroMinimum`; writes `L:AIRLINER_MINIMUM_DESCENT_ALTITUDE` (ft). Always editable (not mandatory) |
| `RADIO` label | right = 175, y_c 739.7 (723.3 non-precision) | text `RADIO` if approach type is ILS or GLS, else `-----` |
| RADIO field | x 190, y 715.2, w 158, h 49 | only for ILS/GLS. `RadioAltitudeFormat 1..43000` or `NO DH`/`NODH`/`NONE`/`NO` → stored 0, displayed `NO DH` (no unit). Key `approachRadioMinimum`; writes `L:AIRLINER_DECISION_HEIGHT` (null → −1) |

### 9.3 Right column (x 425.1..754, items centred at cx 589.6)

| Item | Geometry | Rules |
|---|---|---|
| green dot + value + `KT` | circle centre (561.6, 336.7) r 5; value x 583.1; unit x 636; y_c 337.2 | `approachGreenDotSpeed` |
| `S` + value | `S` right = 568.1, value x 583.1, y_c 371.3 | `approachSlatRetractionSpeed` |
| `F` + value | y_c 405.4 | `approachFlapRetractionSpeed` |
| `VREF` + value | `VREF` x 518.1, value x 583.1, y_c 447.4 | `visibleIfAT`; GREEN (not SEC-dependent) |
| landing-conf box | border 1 LIGHT_GREY: x 488.6, y 484.5, w 202, h 152.1 | — |
| radio `CONF 3` / `FULL` | circles (533.2, 519.5) / (533.2, 572.5); text x 565.2 | selected = FULL unless `approachFlapsThreeSelected`; on change store `approachFlapsThreeSelected = (index==0)`. Always enabled |
| `VLS` + value + `KT` | `VLS` x 524.4, value x 576.9, unit x 629.8, y_c 618.5 | `approachVls` |
| `VAPP` label | x 507.6, y_c 681.1 | — |
| VAPP field | x 572.6, y 656.6, w 99, h 49 | `SpeedKnotsFormat 90..340`; shows `approachVapp`; small font when computed, 27 px when pilot-entered (`pilotVapp != null`); key `pilotVapp` (CLR → computed). Not mandatory, never inactive |

All speed values: `mfd-value`, `toFixed(0)` / `---`; `null` for SEC plans (`FMC:723-766`). Formulas §12.3.

### 9.4 Bottom row (y 822.7..885.7, y_c 854.2)

| Item | Geometry | Rules |
|---|---|---|
| `TRANS` label | right = 168, y_c 854.2 | — |
| TRANS FL field | x 183, y 829.7, w 118, h 49 | `FlightLevelFormat` (leading `FL`, left-aligned text); small font when from database; commit → `pilotTransitionLevel` + `updateTransitionAltitudeLevel()` |
| `VERT DEV` label + value | value right = 737, label right = value_left − 15; y_c 854.2 | `visibleIfAT`; value `mfd-value`: `+NNNFT` / `-NNNFT` (VNAV linear deviation, ft, `toFixed(0)`) when Active plan and phase ≥ DESCENT, else `+-----` |

---

## 10. GA tab

Inner box x 14..754, y 239.1..888.7 (`PERF:3317-3429`).

| Item | Geometry | Rules |
|---|---|---|
| `F` + value + `KT` | `F` x 221, value x 248.5, unit x 301.4, y_c 316.1 | `approachFlapRetractionSpeed` |
| `S` + value | y_c 350.2 | `approachSlatRetractionSpeed` |
| green dot + value | circle centre (227, 383.8), value x 249, y_c 384.3 | `approachGreenDotSpeed` |
| `THR RED` label | right = 164, y_c 525.8 | — |
| GA THR RED field | x 179, y 501.3, w 158, h 49 | `AltitudeOrFlightLevelFormat(transFl×100)`; small font when default; key `pilotMissedThrustReductionAltitude`. Never inactive |
| `ACCEL` label / field | right 164, y_c 589.8 / x 179, y 565.3, w 158 | key `pilotMissedAccelerationAltitude` |
| `EO ACCEL` label / field | right 502, y_c 589.8 / x 517, y 565.3, w 158 | key `pilotMissedEngineOutAccelerationAltitude` |
| `TRANS` label | right = 171, y_c 871.7 | — |
| trans value | x 186, y_c 871.7 + unit `FT` after value + 6 px | read-only: TRANS FL × 100 (e.g. `18000 FT`) or `---` |

---

## 11. Page footer row (all tabs) (`PERF:3431-3516`)

Row y 902.7..956 (`margin 0 2 3 2`, items bottom-aligned at y 956), left-packed:

| Item | Geometry | Visible when | Action |
|---|---|---|---|
| `RETURN` | x 2, y 920.3, w 110.5, h 35.7 | always | navigate back (no-op if no history) |
| `ACTIVATE` / `APPR` `*` (AMBER text) | x 117.5, y 902.7, w 152, h 53.3 | `AT && phase ∈ {CLIMB, CRUISE, DESCENT, GOAROUND}` | open ACTIVATE APPR dialog |
| `POS MONITOR` | x 274.3, y 920.3, w 179, h 35.7 | `AT` | → `fms/position/monitor/withReturn` |
| `CLEAR` / `EO` `*` (AMBER text) | x 458.5, y 902.7, w 110.5, h 53.3 | EO | open CLEAR EO dialog |

Hidden items keep their slot (`visibility:hidden`), so positions never shift.

Dialogs (bottom-left anchored, overlay the footer row):

| Dialog | Box | Title | CONFIRM action |
|---|---|---|---|
| ACTIVATE APPR | x 2, y 778, w 294, h 179 | `ACTIVATE APPR ?` AMBER, cx 149, y_c 813 | `FMC.tryGoInApproachPhase()` → FlightPhaseManager (§12.8) |
| CLEAR EO | x 2, y 778, w 464, h 179 | `CLEAR EO ?` AMBER (y_c 813) + blank line + `(BACK TO ALL ENGs COMPUTATION)` WHITE (y_c 845), centred cx 234 | `fmgc.data.engineOut = false` |

Dialog buttons: `CANCEL` x 7, y 914.3, w 110.5, h 35.7; `CONFIRM *` right-aligned to the dialog content edge
(x 145.3 / 311.3, w 145.8). CANCEL just closes.

---

## 12. Computations (for the C core)

### 12.1 Lookup-table semantics

FBW `LerpLookupTable` (MSFS SDK): N-dimensional multilinear interpolation over the breakpoints, **clamped**
(no extrapolation) outside the key range. All speed tables are in Appendix A. Keys: weight in kg, CG in %MAC,
altitude in ft.

Inputs (`ACIF:1154-1249`):

* `GW` = FQMS gross weight (kg) if valid, else (ZFW + FOB) × 1000, where FOB = FQMS FOB if any engine N2 > 20 %
  else entered BLOCK (`FMGC:211-260`). If still null: `373000 + FOB×1000` (`maxZfw`, `ACIF:1158`).
* `CG` = FQMS GW CG (%MAC) or **35** if invalid.
* `ALT` = ADR pressure altitude (ft); takeoff F/S/GD are **not updated** while it is invalid.
* `flapLever` = `L:A32NX_FLAPS_HANDLE_INDEX` (0..4).
* `LW` = landing weight (§12.5); in APPROACH/GOAROUND phase or if LW is null, `GW` is used.
* `DEST_ELEV` = destination runway threshold elevation, else destination airport elevation, else 0.
* `HW_TWR` = tower headwind from the entered approach wind on the destination runway (§12.3), 0 if not available.
* Constants (`PC`): `Vmcl = 123 kt`, `Vmo = 340 kt`, `Mmo = 0.89`, `maxCertifiedAlt = 43000 ft`.

### 12.2 T.O tab F / S / green dot (current-weight values) [FBW]

With `phase ≤ TAKEOFF` (`OS:493-503`, `ACIF:1230-1244`):

```
vs1g_conf1 = VLS_APPR[CONF_1](CG, GW) / 1.23          // table named CONF_1 (FBW calls it "1F")
f_to       = max(1.18 * vs1g_conf1, Vmcl + 5)          // f2 = f3 = f_to in TAKEOFF/PREFLIGHT
s_to       = 1.21 * VLS_CONF_0(ALT, GW) / 1.23
gd         = GREEN_DOT(ALT, GW)

PREFLIGHT:            F = ceil(max(f_to, Vmcl + 5))
phase >= TAKEOFF:     if flapLever == 2: F = ceil(max(f2, Vmcl + 15))
                      if flapLever == 3: F = ceil(max(f3, Vmcl + 10))
                      otherwise F keeps its last value
                      (f2/f3 = F2_SPEED(CG,GW) / F3_SPEED(CG,GW) once phase > TAKEOFF)
S  = ceil(phase <= TAKEOFF ? s_to : S_SPEED(GW))
GD = ceil(gd)
```

Published as `L:A32NX_SPEEDS_F`, `L:A32NX_SPEEDS_S`, `L:A32NX_SPEEDS_GD` (0 when null) (`ACIF:307-315`).

### 12.3 APPR / GA speeds [FBW] (`ACIF:1165-1208`, `OS:437-462`)

```
conf     = approachFlapsThreeSelected ? CONF_3 : CONF_FULL
vls_tab  = VLS_APPR[conf](CG, W)                 // W = LW (or GW in APPR/GA phase or if LW null)
VLS      = ceil(max(vls_tab, Vmcl))
VAPP_c   = ceil(vls_tab + clamp(round(HW_TWR / 3), 5, 15))   // computed BEFORE the Vmcl max
VAPP     = pilotVapp ?? VAPP_c
VREF     = ceil(VLS_APPR[CONF_FULL](CG, W))        // no Vmcl clamp
GD_appr  = ceil(GREEN_DOT(DEST_ELEV, W))
S_appr   = ceil(S_SPEED(W))
F_appr   = ceil(F2_SPEED(CG, W))                    // F2 table even for CONF 3
```

Wind correction note: the 5 kt minimum applies even with a tailwind (`addWindComponent`, `OS:378-380`).

Wind components:

```
diff(a,b)  = 180 - | |a - b| - 180 |                       // degrees, 0..180
HW         = V * cos(diff(windDirMag, rwyMagBearing))        // + = headwind
XW         = V * cos(diff(windDirMag, rwyMagBearing + 90))   // displayed as |XW|
```

Shown: `HD|TL` + `abs(HW)` padded to 3, `CROSS` + `abs(XW)` padded to 3 (`PERF:685-693`).

Published: `L:A32NX_SPEEDS_VAPP` (0 if null), `L:A380X_FM_APPROACH_HEADWIND_COMPONENT` (ARINC word, see §15).
All of these are computed for the **Active** plan only; Temporary shows the same (active) values, SEC shows `---`.

Current-configuration VLS for the PFD-side FMS bus (not displayed on PERF, `OS:448-491`):
lever 0 → `VLS_CONF_0(ALT,GW)`; lever 1 and CAS > 212 → `max(1.18·VLS_APPR[CONF_1]/1.23, Vmcl)`;
otherwise `VLS_APPR[lever+1]`; then `max(·, Vmcl)`; in phase ≤ TAKEOFF `max(1.15·vs1g, 1.05·min(V2, Vmcl))`;
plus speed-brake increment `[25,25,7,10,10,8][conf] × spoiler/[20,20,12,9,8,6][conf]`.
Written to `L:A32NX_SPEEDS_VLS`/`VS`/`VMAX`/`VFEN` (rounded).

### 12.4 Managed speed targets displayed in the tables [FBW] (`FMGC:275-420`)

```
CLB speed = CI != null ? 290*(1-d) + 330*d, d = (CI/999)^2 : 250      // toFixed(0)
CLB Mach  = 0.84                                                      // constant [FBW-PH]
CRZ speed = 310                                                       // constant [FBW-PH]
CRZ Mach  = 0.85                                                      // constant [FBW-PH]
DES speed = pilotManagedDescentSpeed ?? (CI != null ? round(288*(1-d)+300*d), d = CI/999 : 300)
DES Mach  = pilotManagedDescentMach  ?? 0.84
```

Climb speed limit: `climbSpeedLimitSpeed` / `climbSpeedLimitAltitude` of the plan (both required);
default 250 kt / 10 000 ft **[reconstructed]**. In the CLB table the limit applies when
`INDICATED ALTITUDE < limit altitude`.

[PORT] These are FBW simplifications (no ECON model). Implement exactly as above for parity; a later ECON
model can replace the functions without UI changes.

### 12.5 Weights [FBW] (`FMC:551-616`)

```
TOW  (preflight, engines off or non-active plan) = (ZFW + BLOCK - TAXI) * 1000        // frozen at liftoff
TOW  (preflight, engines on, active)             = GW - TAXI*1000
TRIP (active only)                               = FOB*1000 - destPred.EFOB_kg          // null without VNAV
LW   = engines not started:  TOW - TRIP          (both required)
       phase >= TAKEOFF:     GW - TRIP
       else (engines on):    GW - TRIP - TAXI*1000
DEST EFOB (t) = destPred.estimatedFuelOnBoard [lb] -> kg / 1000
```

Display: kg → t or klb (`CONFIG_USING_METRIC_UNIT`), `/1000`, one decimal, `---.-` if unknown.

`destEfobBelowMinInActive` (`ACIF:1692-1707`): with min dest fuel `MIN` (t): set when `EFOB < MIN`, cleared when
`EFOB − MIN > 0.3` (hysteresis).

### 12.6 REC MAX / OPT / EO MAX [FBW] (`OA:12-40`, `FMC:665-696`)

```
ISA(alt)  = 15 - 0.0019812 * min(alt, 36089)          // °C
DISA      = SAT - ISA(current altitude)
recMaxAlt = min( round_to_500( 52442 - 1.24194943e-2*GW - 3.50813174e-8*GW^2
                              - 53.7792269*DISA - 1.87989779e-4*GW*DISA - 4.46181779*DISA^2 ), 43000 )
REC MAX FL = recMaxAlt / 100            (null if GW unknown or plan not Active/Temporary)
OPT FL     = floor(0.96 * REC_MAX_FL / 5) * 5   if !EO && phase <= CRUISE else null
EO MAX FL  = floor(0.80 * REC_MAX_FL / 5) * 5
```

### 12.7 Default thrust reduction / acceleration / transition values

[FBW] verified in this checkout:

* At TAKEOFF phase entry, if ACCEL (resp. EO ACCEL) is still null, the pilot value is set to
  `INDICATED ALTITUDE + CONFIG_ACCEL_ALT` (default `'1500'`) (`FMC:1236-1253`).
* At GOAROUND phase entry, if missed ACCEL / missed EO ACCEL is null, set to
  `INDICATED ALTITUDE + CONFIG_ENG_OUT_ACCEL_ALT` (default `'1500'`) (`FMC:1344-1362`).
* flyPad settings `CONFIG_THR_RED_ALT`, `CONFIG_ACCEL_ALT`, `CONFIG_ENG_OUT_ACCEL_ALT`: default 1500,
  accepted ranges 400..5000 / 400..10000 / 400..10000 ft AGL
  (`fbw-common/.../EFB/Settings/Pages/AircraftOptionsPinProgramsPage.tsx:26-67`).
* `NEW ACCEL ALT: HHHHH` / `NEW THR RED ALT: HHHHH` when the active plan reconciles ACC/THR RED with
  altitude constraints (`ACIF:454-474`).

**[reconstructed]** (fmgc `FlightPlan`/`FlightPlanPerformanceData`, absent): defaults are computed when the
origin (resp. destination) airport/runway is set:

```
ref_elev           = origin runway threshold elevation ?? origin airport elevation
default THR RED    = round10(ref_elev + CONFIG_THR_RED_ALT)
default ACCEL      = round10(ref_elev + CONFIG_ACCEL_ALT)
default EO ACCEL   = round10(ref_elev + CONFIG_ENG_OUT_ACCEL_ALT)
missed (GA) values = same formulas with the destination runway/airport elevation
effective value    = pilot ?? default ; *IsPilotEntered = (pilot != null)
TRANS ALT          = pilot ?? origin airport database transition altitude (null if none)
TRANS FL           = pilot ?? destination airport database transition level (FL) (null if none)
*IsFromDatabase    = (pilot == null && database != null)
```

[PORT] Implement exactly this; additionally (A380 FCOM behaviour, recommended) reject a pilot THR RED or
ACCEL lower than `origin elevation + 400 ft` with `ENTRY OUT OF RANGE`, and raise ACCEL to THR RED when
THR RED > ACCEL. If the navigation database has no transition altitude/level, leave the field empty
(`-----` / `---`), as FBW does.

### 12.8 ACTIVATE APPR [reconstructed]

`FlightPhaseManager.tryGoInApproachPhase()`: refused in PREFLIGHT, TAKEOFF and DONE; otherwise switch to
APPROACH (if not already). The button is only offered in CLB/CRZ/DES/GA (§11).

### 12.9 Other FMC rules relevant to PERF data [FBW]

* When engines start (or ZFW is entered) and the active plan has a destination but no CI: CI = 0, mode LRC,
  message `LRC MODE IN USE` (`FMC:1777-1786`).
* At CLIMB entry: activate a preselected climb speed (`K:A32NX.FMS_PRESET_SPD_ACTIVATE`), arm the preselected
  cruise speed (`L:A32NX_SpeedPreselVal` / `L:A32NX_MachPreselVal`, −1 = none), and if CRZ FL is empty set it
  from the FCU altitude (`FMC:1278-1302`).
* At CRUISE entry from CLIMB: activate the preselected cruise speed (`FMC:1304-1313`).
* At DESCENT entry: clear preselections and **clear the cruise FL** (`FMC:1315-1326`), check dest data
  (`ENTER DEST DATA` if QNH/temp/wind not all entered).
* At DONE: all plans and FMGC data reset, navigate to `fms/data/status` (`FMC:1367-1382`).
* Managed speed target published to the FG/PFD (`L:A32NX_SPEEDS_MANAGED_PFD`, `ACIF:985-1077`):
  PREFLIGHT/TAKEOFF = V2; CLIMB = min(CLB speed, speed limit below its altitude, constraint) (EO: ramps to GD
  at 1 kt/s); CRUISE = min(CRZ speed, limit); DESCENT = VNAV; APPROACH = VAPP; GOAROUND = min(GD, constraint).
  Approach managed speed by flap lever (`ACIF:1083-1098`): 0 → GD, 1 → S, 2 → F, 3 → (CONF 3 landing ? VAPP : F),
  4 → VAPP.

---

## 13. Validation and messages

### 13.1 Data-entry formats used on PERF (`DEF`)

Error texts: `FORMAT ERROR` / `ENTRY OUT OF RANGE`, optionally with a second line `FORMAT: <fmt>[ <unit>]` or
`RNG: <min> TO <max>[ <unit>]` (`DEF:15-31`).

| Format | Placeholder | Max digits | Display | Parse / range | Error 2nd line |
|---|---|---|---|---|---|
| `SpeedKnotsFormat(90, 340)` | `---` + `KT` | 3 | integer | number in [90,340] | `FORMAT: XXX KT` / `RNG: 90 TO 340 KT` |
| `SpeedMachFormat(0.1, 0.89)` | `.--` | 3 | `.NN` | number; if > 0.89 → `0.<input>` (so `84` → .84); range [0.1,0.89] | `FORMAT: XX` / `RNG: 0.1 TO 0.89` |
| `AltitudeOrFlightLevelFormat(trans)` | `-----` + `FT` | 5 | value > trans (≥ if trans is a FL) → `FL` + `NNN` (value/100, 3-digit padded); else `NNNNN` + `FT` | ≤ 3 chars → ×100; range [0,43000] | `FORMAT: FOR ALT XXXXX FOR FL XXX` / range: no 2nd line |
| `AltitudeFormat(min, 43000)` | `-----` + `FT` | 5 | integer | [min,43000] (min 1 for TRANS, 0 for BARO) | `FORMAT: XXXXX FT` / none |
| `FlightLevelFormat(0, 430)` | `FL` + `---` | 3 (+2 overflow) | `FL` + 3-digit padded | number or `FL<1-3 digits>`; [0,430] | `FORMAT: FL XXX` / none |
| `RadioAltitudeFormat(1, 43000)` | `-----` + `FT` | 5 | integer + `FT`; 0 → `NO DH` | `NO DH` / `NODH` / `NONE` / `NO` → 0; [1,43000] | no 2nd line |
| `LengthFormat(1, rwyLen)` | `----` + `M`/`FT` | 4 | metres converted to unit, integer | converted to metres, [1, runway length] | `FORMAT: XXXX M` / none |
| `PercentageFormat(min,max)` | `--.-` + `%` | 4 | 1 decimal | [min,max] | `FORMAT: XX.X %` / `RNG: 0.0 TO 99.9 %` |
| `TemperatureFormat(min,max)` | `---` + `°C` | 3 | `+N` / `-N` | [min,max] | `FORMAT: +/-XX °C` / `RNG: -99 TO 99 °C` |
| `WindDirectionFormat` | `---` + `°` | 3 | 3-digit padded | [0,359] | `RNG: 0 TO 359 °` |
| `WindSpeedFormat` | `---` + `KT` | 3 | 3-digit padded | [0,250] | `RNG: 0 TO 250 KT` |
| `QnhFormat(isHpa)` | `----` / `--.--` | 5 | value < 745 → 2 decimals, else integer | `NN.NN` or `NNNN` in [22.00,32.48] → inHg (`2992` → 29.92); else 1–4 digits in [745,1100] hPa | `FORMAT: XXXX` / `RNG: 22.00 TO 32.48` or `RNG: 745 TO 1100` |
| `CostIndexFormat` | `---` | 3 | integer | [0,999] | `FORMAT: XXX` / `RNG: 0 TO 999` |
| `DescentRateFormat(-999,-100)` | `----` + `FT/MN` | 4 | `value.toString()` | positive negated; [-999,-100] | `FORMAT: -XXXX FT/MN` / `RNG: -999 TO -100 FT/MN` |

### 13.2 Mandatory fields (amber boxes)

| Field | Condition |
|---|---|
| CRZ FL | loaded plan is Active and FMGC phase < DESCENT |
| V1, VR, V2, THS FOR | `AT` (and not `toPageInactive`) |
| OAT, QNH (APPR) | `AT` and distance to destination ≤ 180 NM (unknown = 0) |

### 13.3 CRZ FL handler `trySetCruiseFl(fl, plan)` [FBW] (`FMC:1804-1849`)

```
if plan does not exist: reject
if CLR (null) or 0: reject silently (`v ? ... : false`, PERF:1370-1372)
if fl <= 0 or fl > 430: ENTRY OUT OF RANGE, reject
insert = fl
if plan == Active and CLIMB <= phase < DONE:
    fcuFl = floor(max(0, FCU selected alt)/100)
    if fl < fcuFl: insert = fcuFl ; newCrzAlt = phase in {CRUISE, DESCENT, APPROACH}
store cruiseFlightLevel = insert
if insert > REC MAX FL: message CRZ FL ABOVE MAX FL
if Active: ACIF.onUpdateCruiseLevel(insert)  (=> L:A32NX_AIRLINER_CRUISE_ALTITUDE, step clean-up)
           if newCrzAlt: message NEW CRZ ALT: <insert*100>
```

### 13.4 V-speed and T.O checks [FBW] (`ACIF:569-655`)

Run every FMS cycle on the active plan:

* **Disagree**: `V1 <= VR`, `VR <= V2`, `V1 <= V2` (each pair only if both set). If violated → message
  `V1/VR/V2 DISAGREE` (type II, amber, auto-removed when resolved) and DISCRETE_WORD_3 bit 16.
* **Too low** [FBW-PH]: computation exists (V1 < Vmcg, VR < 1.05·Vmca, V2 < 1.1·Vmca at departure pressure
  altitude, A320 tables) but is forced `false` (`FIXME`). Message `T.O SPEED TOO LOW - CHECK TOW & T.O DATA`
  never raised. Bit 17 = 0.
* **Not inserted**: any of V1/VR/V2 null → DISCRETE_WORD_3 bit 18 (no message).

[PORT] Same three checks. Keep "too low" disabled until A380 Vmcg/Vmca data exist (do not reuse A320 tables).

### 13.5 Message catalogue used by/for this page (`MSG:82-120`)

| Text | Type | Colour | Raised by |
|---|---|---|---|
| `ENTRY OUT OF RANGE` (+ 2nd line) | I | white | format range errors, CRZ FL |
| `FORMAT ERROR` (+ 2nd line) | I | white | format errors |
| `CHECK T.O. DATA` | II | amber | PACKS / ANTI ICE change after engine start; departure runway change (resolved when VR set) |
| `V1/VR/V2 DISAGREE` | II | amber | §13.4 |
| `T.O SPEED TOO LOW - CHECK TOW & T.O DATA` | II | amber | disabled |
| `CRZ FL ABOVE MAX FL` | II | white | §13.3 |
| `NEW CRZ ALT: HHHHH` | II | white | §13.3 |
| `NEW ACCEL ALT: HHHHH` / `NEW THR RED ALT: HHHHH` | II | white | §12.7 |
| `LRC MODE IN USE` | I | white | §12.9 |
| `ENTER DEST DATA` | II | amber | DES/APPR entry without dest data |
| `DEST EFOB BELOW MIN` | II | amber | EFOB < min in CRZ/DES (or CLB ALT > 10 min, or APPR/GA above 800 ft RA) |
| `STEP DELETED` | II | white | EO / auto step deletion |
| `CHECK SPD MODE` | II | white | DES entry with selected speed and no presel |

Messages are shown in the MFD footer message area (other spec). Duplicate cleared messages are replaced
(`FMC:1136-1161`).

---

## 14. Data model (per flight plan: Active, Temporary, SEC1..3)

C core proposal: one `MfdPerfData` per plan, copied with the plan (TMPY/SEC copies). `NULL` = explicit
"not set" flag (do not use 0 as null except where FBW does). FBW keys in `code`. Defaults marked
**[r]** are reconstructed from fmgc (verify).

| Field | FBW key(s) | Type / unit | Default |
|---|---|---|---|
| cruise FL | `cruiseFlightLevel` | FL | NULL |
| cost index / mode | `costIndex` / `costIndexMode` | 0..999 / LRC=0, ECON=1 | NULL / ECON [r] |
| V1, VR, V2 | `v1`,`vr`,`v2` | kt | NULL |
| T.O shift | `takeoffShift` | m | NULL |
| flaps | `takeoffFlaps` | FlapConf 1..3 | NULL |
| THS FOR | `takeoffThsFor` | % | NULL |
| power setting | `takeoffPowerSetting` | TOGA=0, FLEX=1, DERATED=2 | TOGA [r] |
| flex temp | `flexTakeoffTemperature` | °C | NULL |
| derated | `takeoffDeratedSetting` | D01..D05 = 0..4 | NULL |
| packs / anti-ice | `takeoffPacks` / `takeoffAntiIce` | OFF_APU=0, ON=1 / OFF=0, ENG_ONLY=1, ENG_WINGS=2 | ON / OFF [r] |
| THR RED, ACCEL, EO ACCEL | `pilot*` + `default*` → `thrustReductionAltitude`, `accelerationAltitude`, `engineOutAccelerationAltitude` + `…IsPilotEntered` | ft | §12.7 |
| missed THR RED / ACCEL / EO ACCEL | `pilotMissed*` → `missed…Altitude` + `…IsPilotEntered` | ft | §12.7 |
| TRANS ALT | `pilotTransitionAltitude` → `transitionAltitude`, `transitionAltitudeIsFromDatabase` | ft | database |
| TRANS FL | `pilotTransitionLevel` → `transitionLevel`, `transitionLevelIsFromDatabase` | FL | database |
| noise | `noiseEnabled`, `noiseN1`, `noiseSpeed`, `noiseEndAltitude` | bool, %, kt, ft | false, NULL, NULL, NULL (page subject default 800 ft is overwritten) |
| climb derated | `climbDerated` | NONE=0, 01..05 | NONE [r] |
| CLB speed limit | `climbSpeedLimitSpeed`, `climbSpeedLimitAltitude` | kt, ft | 250 / 10000 [r] |
| presel CLB / CRZ speed | `preselectedClimbSpeed`, `preselectedCruiseSpeed` | kt (CRZ: kt if > 1 else Mach) | NULL |
| DES managed spd / Mach | `pilotManagedDescentSpeed`, `pilotManagedDescentMach` | kt / Mach | NULL |
| DES cabin rate | `descentCabinRate` | ft/min (negative) | −350 [r] |
| approach wind dir/mag | `approachWindDirection`, `approachWindMagnitude` | °mag / kt | NULL |
| approach OAT / QNH | `approachTemperature`, `approachQnh` | °C / hPa or inHg (< 100 ⇒ inHg) | NULL |
| BARO / RADIO minimum | `approachBaroMinimum`, `approachRadioMinimum` | ft / ft (0 = NO DH) | NULL |
| landing conf 3 | `approachFlapsThreeSelected` | bool | false (FULL) |
| VAPP pilot | `pilotVapp` | kt | NULL |

Non-plan FMGC data (`FMGC:72-176`): `vSpeedsForRunway`, `v1/vr/v2ToBeConfirmed`, F/S/GD (T.O and approach),
VLS/VREF/VAPP (approach), `climbPredictionsReferencePilotEntry/Automatic`, `engineOut`,
`destEfobBelowMinInActive`. Page-local: DES PRED TO.

---

## 15. Outputs consumed by other displays / systems

FBW names first; the port column proposes X-Plane datarefs (owned by the MFD/FMS core, published by the
adapter) and the existing port consumers found in `native/avionics`.

| Datum | FBW variable (writer) | FBW consumer | Port dataref / consumer |
|---|---|---|---|
| V1 | `L:AIRLINER_V1_SPEED` kt, −1 none (`PERF:1465`, `ACIF:564`) | PFD V1 bug (cyan tick + `1`) and V1 off-scale readout, visible FWC phase ≤ 5 and V1 > 0 (`PFD/SpeedIndicator.tsx:42-96,960-990`) | write `a380x/xp12/systems/pfd/v1_knots` (existing PFD input, 0 = none; `a380x_pfd_inputs.inc:36`, render `a380x_pfd_render.inc:746`) |
| VR | `L:AIRLINER_VR_SPEED` kt, −1 none | PFD VR cyan circle | `a380x/xp12/systems/pfd/vr_knots` (existing) |
| V2 | `L:AIRLINER_V2_SPEED` kt, **0 = invalid** | FG (SRS / takeoff managed speed target via `L:A32NX_SPEEDS_MANAGED_PFD`) | new `a380x/xp12/fms/perf/v2_knots` → guidance managed target in PREFLIGHT/TAKEOFF |
| FLEX | `L:A32NX_AIRLINER_TO_FLEX_TEMP` (0 = none, 0.1 = FLEX 0 °C) | EWD thrust-rating `FLEX +NN` when flex ≠ 0, flex ≥ SAT−10 and N1 limit type FLEX (`EWD/elements/ThrustRatingMode.tsx:25-33`); PFD FMA A1A2 `MAN FLX +NN` (`PFD/FMA.tsx:537-551`); FADEC flex limit | write `a380x/xp12/engine/flex_temperature_c` with the same 0 / 0.1 semantics (existing EWD input, `a380x_ewd_data.inc:86,384-389`) |
| T.O flaps | `L:A32NX_FM1/2_DISCRETE_WORD_2` bits 13..16 = flaps 0..3 (`ACIF:661-667`) | FWS T.O CONFIG | `a380x/xp12/fms/perf/to_flaps` (int, −1 none) |
| T.O pitch trim | `L:A32NX_FM1/2_TO_PITCH_TRIM` BNR 12 bit, range 180 | FWS pitch-trim T.O check | `a380x/xp12/fms/perf/ths_for_pct` |
| V-speed status | `L:A32NX_FM1/2_DISCRETE_WORD_3` bit16 disagree, 17 too low, 18 not inserted | FWS | `a380x/xp12/fms/perf/vspd_status` (bitfield) |
| THR RED / ACC / EO ACC | `L:A32NX_FM1/2_THR_RED_ALT`, `…_ACC_ALT`, `…_EO_ACC_ALT` BNR 17 bit range 131072 ft; T.O values in phase ≤ TAKEOFF, missed values in GOAROUND, NCD otherwise (`ACIF:476-521`) | FG (SRS→CLB acceleration, LVR CLB), FADEC | `a380x/xp12/fms/perf/thr_red_ft`, `acc_ft`, `eo_acc_ft` (−1 = NCD) → `a380x_guidance.c` |
| TRANS ALT / FL | `L:A32NX_FM1/2_TRANS_ALT` (BNR 17 bit, ft), `L:A32NX_FM1/2_TRANS_LVL` (BNR 9 bit, FL) (`ACIF:523-549`) | PFD STD/QNH reminder (`PFD/AttitudeIndicatorHorizon.tsx:516`, `PFD/shared/ArincValueProvider.ts:245`) | `a380x/xp12/fms/perf/trans_alt_ft`, `trans_lvl_fl` |
| MDA | `L:AIRLINER_MINIMUM_DESCENT_ALTITUDE` ft, 0 none (direct, `PERF:3157`) **and** `L:A32NX_FM1/2_MINIMUM_DESCENT_ALTITUDE` BNR (valid only "in range": phase > CRUISE, or CRUISE and < 250 NM to dest) (`ACIF:758-783`) | PFD digital altitude readout amber when MDA ≠ 0 (`DigitalAltitudeReadout.tsx:86-94`), altitude-tape MDA marker, FMA `BARO NNNN` (`FMA.tsx:1470-1525`), ISIS | `a380x/xp12/fms/perf/mda_ft` + `mda_valid` |
| DH | `L:AIRLINER_DECISION_HEIGHT` ft, −1 none (direct) and `L:A32NX_FM1/2_DECISION_HEIGHT` BNR (valid only if in range, MDA not valid and DH > 0); NO DH = bit 29 of `L:A32NX_FM1/2_EIS_DISCRETE_WORD_2` | PFD DH logic, FMA `RADIO NNN` / `NO DH` | `a380x/xp12/fms/perf/dh_ft`, `dh_valid`, `no_dh`; **also** write `sim/cockpit2/gauges/actuators/radio_altimeter_bug_ft_pilot/_copilot` because the port PFD reads its DH there (`a380x_avionics.c:170-171`, `a380x_pfd_render.inc:1025-1029`) |
| F / S / GD (FMS, current weight) | `L:A32NX_SPEEDS_F/S/GD` (0 none) | FMS holding speed, debug page. **The FBW A380 PFD F/S/GD bugs come from the PRIM (`prim_v_3`, `prim_v_4`, `prim_v_man`), not from the FMS** (`SpeedIndicator.tsx:212-341`) | `a380x/xp12/fms/perf/f_knots`, `s_knots`, `gd_knots`. The port PFD already reads optional `a380x/xp12/systems/pfd/{f_speed,s_speed,green_dot}_knots` from an "envelope provider"; until a PRIM model exists the provider may forward these FMS values (flag as FMS-sourced) |
| VLS / VS / VMAX / VFE next (FMS) | `L:A32NX_SPEEDS_VLS/VS/VMAX/VFEN` | FMS holding/GS-mini; PFD uses PRIM `prim_v_ls` | not needed by PERF; keep PFD envelope provider separate. Note: the PRIM clean-VLS table already in `a380x_pfd_inputs.inc` differs from `OS` VLS_CONF_0 at 15 000/38 000 ft — use `OS` tables for FMS values |
| VAPP | `L:A32NX_SPEEDS_VAPP` (0 none) + managed target in APPROACH | FG/A/THR, PFD speed target | `a380x/xp12/fms/perf/vapp_knots` |
| Landing conf 3 | `L:A380X_FM_LANDING_CONF3` bool (`ACIF:325-330`) | FWS/TAWS flap mode | `a380x/xp12/fms/perf/ldg_conf3` |
| Approach headwind | `L:A380X_FM_APPROACH_HEADWIND_COMPONENT` (ARINC raw word as string, NCD if null) | PRIM/FG GS-mini | `a380x/xp12/fms/perf/appr_headwind_kt` + valid |
| Destination QNH | `L:A32NX_DESTINATION_QNH` hPa | ECAM/press. | `a380x/xp12/fms/perf/dest_qnh_hpa` |
| Cruise altitude | `L:A32NX_AIRLINER_CRUISE_ALTITUDE` ft | FG / pressurization | `a380x/xp12/fms/perf/crz_alt_ft` |
| Preselected speed | `L:A32NX_SpeedPreselVal` kt / `L:A32NX_MachPreselVal` (−1 none), event `K:A32NX.FMS_PRESET_SPD_ACTIVATE` | FCU / FG | `a380x/xp12/fms/perf/presel_kt`, `presel_mach` → `a380x_guidance.c` |
| Managed speed target | `L:A32NX_SPEEDS_MANAGED_PFD` kt | FG / PFD magenta target | owned by the FMS guidance spec |
| FMA | — | PERF data feed FMA only through FLEX (A1A2 `MAN FLX`), MDA/DH (FMA column 5 line 1) and THR RED/ACC (mode transitions) | as above |

Inputs read by the page/FMC: `L:A32NX_FMGC_FLIGHT_PHASE`, `L:A32NX_FLAPS_HANDLE_INDEX`, ADR pressure altitude
and CAS, FQMS GW/CG/FOB, `L:A32NX_ENGINE_N2:1..4`, `INDICATED ALTITUDE`, `ZULU TIME`, ambient temperature,
FCU EFIS discrete word 2 bit 13 (baro inHg, per side), PRIM selected altitude, PRIM FG discrete word 5 bits 17/18
(speed managed/selected), FCU speed target (kt or Mach), VNAV predictions/linear deviation, nav data (runways,
approaches, transition altitude/level). [PORT] The FMGC flight phase must come from the FMS FlightPhaseManager
port — **not** from the existing `a380x/xp12/avionics/flight_phase`, which is the FWS phase (1..12).

---

## 16. FBW placeholders / not available (summary)

| Item | FBW state |
|---|---|
| DERATED take-off thrust (radio + D01..D05 + dialog) | radio always disabled |
| NOISE (T.O and CLB) | button always disabled; noise fields only reachable via stored data |
| CPNY T.O REQUEST, CMS | always disabled |
| DERATED CLB | always inactive (`NONE`) |
| CLB / DES PRED TO predictions, DES PRED TO storage | not computed / page-local |
| CLB managed-speed reason (ECON / SPD LIM / CSTR in small font) | not displayed (TODO) |
| ECON Mach (CLB .84, CRZ .85, DES .84) and CRZ speed (310) | constants |
| LRC (.84) / MAX TURB (.85) rows | hard-coded strings |
| OPT FL / EO MAX FL | 0.96 / 0.8 × REC MAX |
| DRIFT DOWN block | never shown |
| T.O SPEED TOO LOW check | forced off |
| SEC-plan F/S/GD/VLS/VAPP/VREF | `---` (TODO) |
| Magenta constraint colouring | **not present on PERF** — speed constraints are only reachable through `SPD CSTR` (VERT REV page) |
| Computed V1/VR/V2 | none: A380 V-speeds are crew-entered (from the OIS/flyPad T.O performance), as on the real aircraft |

---

## 17. Recommended port behaviour (deltas to FBW)

1. **Fixed layout**: keep CLB L1 row at 70 px and the CRZ prediction header at 83 px in all states; hide the CLB
   THR RED/ACCEL/NOISE grid whenever `AOC && phase ≥ CLIMB` (§6.2, §6.3, §7.2).
2. **CRZ managed speed**: show the managed cruise speed, not the climb speed (§7.2).
3. **Derated dialog** placed at x 415 so it stays in the frame (§5.2).
4. **THR RED / ACCEL validation** ≥ origin elevation + 400 ft, ACCEL ≥ THR RED (§12.7).
5. **Simvar writes from SEC pages**: write V-speed/flex/minimum outputs only when the loaded plan is Active
   (FBW writes V-speed simvars from any plan and relies on the FMC loop to overwrite).
6. **THS FOR 0.0 / CLR and QNH CLR**: keep FBW behaviour (ignored) for parity; document as known.
7. Keep DERATED, NOISE, CPNY T.O REQUEST, CMS and DERATED CLB disabled until FADEC/datalink support exists;
   render them exactly as FBW does (greyed / inactive) so the page looks complete.
8. **Font atlas**: regenerate from the SlashedZero TTF incl. `?`, `°`, `▯` (§0.3).
9. Publish outputs per §15 on the existing port datarefs first (PFD V1/VR, EWD flex, stock DH bug), then the new
   `a380x/xp12/fms/perf/*` contract for guidance/FWS consumers.

---

## Appendix A — Speed lookup tables (`OS:26-319`), exact

All values kt. Weight breakpoints `W = 250, 300, 350, 400, 450, 500, 550, 600` (×1000 kg).

**VLS_APPR[conf](CG, W)** — two CG rows (29 % and 43 % MAC):

| conf | CG | 250 | 300 | 350 | 400 | 450 | 500 | 550 | 600 |
|---|---|---|---|---|---|---|---|---|---|
| CONF_1 | 29 | 128 | 140 | 151 | 162 | 172 | 182 | 191 | 200 |
| CONF_1 | 43 | 126 | 137 | 148 | 158 | 168 | 177 | 186 | 195 |
| CONF_1F | 29 | 123 | 133 | 144 | 154 | 163 | 172 | 180 | 188 |
| CONF_1F | 43 | 123 | 130 | 140 | 150 | 159 | 167 | 176 | 183 |
| CONF_2 | 29 | 123 | 128 | 138 | 148 | 157 | 165 | 173 | 181 |
| CONF_2 | 43 | 123 | 125 | 135 | 144 | 153 | 161 | 169 | 176 |
| CONF_3 | 29 | 123 | 124 | 134 | 144 | 152 | 161 | 169 | 176 |
| CONF_3 | 43 | 123 | 123 | 131 | 140 | 148 | 156 | 164 | 171 |
| CONF_FULL | 29 | 123 | 123 | 131 | 140 | 148 | 157 | 165 | 173 |
| CONF_FULL | 43 | 123 | 123 | 127 | 136 | 144 | 152 | 160 | 168 |

`ApproachConf` numbering: CONF_1=1, CONF_1F=2, CONF_2=3, CONF_3=4, CONF_FULL=5; flap lever `n` (1..4, CAS ≤ 212 for 1)
maps to `n+1`. `FlapConf` (takeoff/landing data): CLEAN=0, CONF_1=1, CONF_2=2, CONF_3=3, CONF_FULL=4 **[r]**.

**F2_SPEED(CG, W)** / **F3_SPEED(CG, W)**:

| table | CG | 250 | 300 | 350 | 400 | 450 | 500 | 550 | 600 |
|---|---|---|---|---|---|---|---|---|---|
| F2 | 29 | 144 | 150 | 162 | 173 | 184 | 191 | 191 | 191 |
| F2 | 43 | 144 | 146 | 158 | 169 | 179 | 189 | 191 | 191 |
| F3 | 29 | 133 | 133 | 143 | 153 | 163 | 171 | 177 | 177 |
| F3 | 43 | 133 | 133 | 139 | 149 | 158 | 167 | 175 | 177 |

**S_SPEED(W)**: 146, 160, 173, 185, 196, 207, 215, 215.

**GREEN_DOT(ALT, W)**:

| ALT ft | 250 | 300 | 350 | 400 | 450 | 500 | 550 | 600 |
|---|---|---|---|---|---|---|---|---|
| 10 000 | 160 | 175 | 190 | 205 | 216 | 228 | 239 | 252 |
| 20 000 | 161 | 178 | 196 | 219 | 237 | 256 | 267 | 277 |
| 30 000 | 186 | 208 | 224 | 241 | 256 | 272 | 284 | 292 |
| 40 000 | 203 | 220 | 236 | 252 | 252 | 260 | 260 | 260 |

(Below 10 000 ft the 10 000 ft row applies — clamped. Same table as `pfd_green_dot()` in the port.)

**VLS_CONF_0(ALT, W)**:

| ALT ft | 250 | 300 | 350 | 400 | 450 | 500 | 550 | 600 |
|---|---|---|---|---|---|---|---|---|
| 0 | 149 | 163 | 176 | 188 | 199 | 210 | 220 | 231 |
| 10 000 | 149 | 163 | 176 | 189 | 202 | 216 | 231 | 243 |
| 15 000 | 149 | 163 | 178 | 192 | 209 | 222 | 234 | 246 |
| 20 000 | 149 | 165 | 183 | 199 | 212 | 226 | 239 | 252 |
| 25 000 | 151 | 171 | 187 | 202 | 217 | 231 | 246 | 260 |
| 30 000 | 158 | 174 | 191 | 207 | 224 | 239 | 256 | 270 |
| 32 000 | 158 | 176 | 193 | 210 | 227 | 244 | 259 | 270 |
| 34 000 | 159 | 178 | 196 | 213 | 231 | 247 | 259 | 270 |
| 36 000 | 161 | 180 | 198 | 217 | 235 | 247 | 259 | 275 |
| 38 000 | 162 | 182 | 202 | 222 | 235 | 247 | 259 | 275 |
| 40 000 | 164 | 185 | 206 | 222 | 235 | 250 | 265 | 270 |
| 43 000 | 168 | 191 | 208 | 223 | 235 | 250 | 250 | 250 |

Vfe (`PC:4-8`): CONF 1 263, 1+F 222, 2 220, 3 196, FULL 182; Vmo 340, Mmo 0.89.

## Appendix B — Test vectors (computed from the formulas above)

| Case | Inputs | Expected |
|---|---|---|
| GD T.O | GW 450 000 kg, ALT 0 ft (clamped to 10 000 row) | GD = 216 |
| S T.O | GW 450 000, ALT 0, phase PREFLIGHT | S = ceil(1.21 × 199 / 1.23) = ceil(195.76) = 196 |
| F T.O | GW 450 000, CG 35, PREFLIGHT | CONF_1 at CG 35: 172 + (168−172)·6/14 = 170.29; F = ceil(max(1.18·170.29/1.23, 128)) = ceil(163.36) = 164 |
| VLS / VAPP | LW 400 000, CG 35, FULL, HW 0 | vls_tab = 140 + (136−140)·6/14 = 138.29 → VLS 139; VAPP = ceil(138.29 + 5) = 144; VREF = 139 |
| VAPP wind | same, HW 30 kt | VAPP = ceil(138.29 + 10) = 149 |
| VAPP wind cap | same, HW 60 kt | VAPP = ceil(138.29 + 15) = 154 |
| CLB ECON | CI 100 | d = 0.01002; 290·0.98998 + 330·0.01002 = 290.40 → `290` |
| DES ECON | CI 100 | round(288·0.8999 + 300·0.1001) = 289 |
| REC MAX | GW 500 000, DISA 0 | 52442 − 6209.7 − 8770.3 = 37462 → 37500 → FL `375`; OPT = 360; EO MAX = 300 |
| QNH | `2992` | inHg 29.92 (display `29.92`); `1013` → hPa 1013 |
| ALT/FL | trans 5000, entry `120` | 12 000 ft → displayed `FL120` |
