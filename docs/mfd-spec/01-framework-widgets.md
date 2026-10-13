# MFD spec 01: framework and widget system

Status: implementation spec for the native C MFD module (simulator-independent core, OpenGL renderer, X-Plane adapter).
Upstream reference: FlyByWire A380X, pinned at `2baa2b35` (GPL-3.0). Paths below are relative to
`upstream-aircraft/fbw-a380x/src/systems/instruments/src/` unless they start with `fbw-a380x/` or `fbw-common/`.

This file covers everything that is not page content: the canvas, fonts, palette, header, title bar, footer, message
area, overlays, navigation and routing, the input model (cursor, hover, click, focus, KCCU, PC keyboard) and every
reusable widget. Page specs (INIT, F-PLN, PERF and so on) build on the primitives defined here and reference them by
section number.

## 0. Conventions used in this spec

### 0.1 Tags

| Tag | Meaning |
|---|---|
| **[FBW]** | What FlyByWire implements at the pinned commit. This is the reference behaviour. |
| **[FBW-NA]** | FBW leaves it as a placeholder, a TODO, inop, or "not found". |
| **[QUIRK]** | FBW behaviour that is an artefact or bug of the TypeScript/Coherent implementation. Do **not** replicate it; the port behaviour is given next to it. |
| **[PORT]** | Recommended behaviour for the X-Plane port. Where no [PORT] note is given, implement [FBW] exactly. |

### 0.2 Coordinates

* One MFD canvas is **768 x 1024 px** (`style.scss:20-28`, `.mfd-main`). The background is opaque `#000000`.
* **Origin is top-left, x grows right, y grows down** (CSS convention), in canvas pixels. Every rectangle is given as
  `x0..x1, y0..y1` (half-open), or as `(x, y, w, h)` with (x, y) as the top-left corner.
* Text positions are given as the **baseline-left point** (x of the first glyph's advance box, y of the baseline), plus
  a font size. Centred text is given as a centre x plus a baseline y.
* The GL renderer works y-up. Convert with `y_gl = 1024 - y_canvas`. The existing `ewd_font_draw(x, y, ...)`
  (`xplane-port/native/avionics/a380x_ewd_font.inc:24`) takes a y-up **baseline**, so pass `1024 - baseline_y`.
* Fractional values come from CSS flex distribution and are kept where they matter. The renderer should snap filled
  rectangles to integer pixels (round half up) and may keep text at sub-pixel x.

### 0.3 Box model

FBW uses the CSS default `box-sizing: content-box` (no global reset exists in the A380X instruments). So a CSS
`width/height` is the content size, and padding and border are added outside it. All boxes in this spec are already
converted to **outer (border-box) rectangles**, unless a box is explicitly called "content".

### 0.4 Text metrics model (used for every derived position)

Both MFD fonts are monospaced, with `unitsPerEm = 4096`, advance `2568` for every glyph, OS/2 typo
ascender `3276`, typo descender `-820`, `USE_TYPO_METRICS` set (fontTools dump of
`fbw-a380x/src/base/flybywire-aircraft-a380-842/html_ui/Fonts/fbw-a380x/*.ttf`). The layout engine (Coherent GT)
resolves `line-height: normal` to **exactly 1.0 x font-size** (typo ascender plus descender is 4096/4096 em, and the
line gap is ignored). Three independent upstream constants confirm this:

1. The dropdown row height of 36 px assumed in `Button.tsx:92` equals 20 px text + 2x5 padding + 2x3 border, which
   requires a line box of 20 px for 20 px text.
2. The full-screen overlays are 852 px tall (`MfdFmsFpln.scss:262-273`), which is exactly
   1024 - 98 (header) - 74 (footer). That is only true with 1.0 x font-size line boxes.
3. The footer button pads 9 px above and 5 px below its text (`widget-style.scss:89`). This compensates for the
   0.2 em descender space below the baseline.

For font size `fs` (px):

| Metric | Formula | 20 | 21 | 22 | 24 | 25 | 26 | 27 | 28 | 30 |
|---|---|---|---|---|---|---|---|---|---|---|
| Line box height | `fs` | 20 | 21 | 22 | 24 | 25 | 26 | 27 | 28 | 30 |
| Baseline below line top | `0.7998*fs` | 16.0 | 16.8 | 17.6 | 19.2 | 20.0 | 20.8 | 21.6 | 22.4 | 24.0 |
| Cap / digit height (`0`, `O` top = 3112 u) | `0.760*fs` | 15.2 | 16.0 | 16.7 | 18.2 | 19.0 | 19.8 | 20.5 | 21.3 | 22.8 |
| Advance per character | `0.62695*fs` | 12.54 | 13.17 | 13.79 | 15.05 | 15.67 | 16.30 | 16.93 | 17.55 | 18.81 |

Text width is `n_chars x advance` (no kerning, no tracking). The glyphs have no descenders worth mentioning (the
descender in the font is about 0).

When an element has an explicit `line-height` (only the InputField text, 27 px), the half-leading is
`(line-height - fs)/2` above the 1.0-em box.

---

## 1. Instrument topology

### 1.1 Two MFDs, one instrument [FBW]

* A single MSFS instrument `A380X/MFD/mfd.html?duID=2` renders **both** MFDs into one texture of
  **1646 x 1024** (`fbw-a380x/src/base/.../Part_Interior_Cockpit/panel/panel.cfg:17-26`). `config.json` still says
  1536 x 1024 but the panel.cfg value is the one used.
* The CAPT MFD occupies texture x `0..768`. A 110 px gap follows (`instrument.tsx:110`, `margin-right: 110px`), and the
  FO MFD occupies x `878..1646`. The comment in panel.cfg says the gap exists "for no mouse cursor overlap".
* Both `MfdComponent`s share **one EventBus, one FmcService and one AtcDatalinkSystem** (`instrument.tsx:32-60`).
  Consequences: flight plans, FMS messages (the footer message queue) and ATC state are shared between both sides.
  Navigation state, open menus, field focus and the cursor are per side.
* At start both sides navigate to `fms/data/status` (`instrument.tsx:144-145`).
* `fmcService.master.mfdReference` is set to the **CAPT** MFD only (`instrument.tsx:139-141`). Requests that the FMC
  makes to "the MFD" (the duplicate-names dialog, `openMessageList()` from the FMC, and the return to DATA/STATUS after
  flight-phase DONE at `FMC/FlightManagementComputer.ts:1377`) always go to the CAPT side. **[QUIRK]**
  **[PORT]:** route a duplicate-names request to the side that triggered the entry. Route the DONE-phase reset to
  both sides.

**[PORT]:** use two independent 768x1024 MFD instances (`MfdSide` 0 = CAPT, 1 = FO), each with its own UI state. Both
reference the same FMS core and message queue. The texture placement is an adapter concern.

### 1.2 Side differences

| Item | CAPT | FO | Source |
|---|---|---|---|
| `captOrFo` / EFIS side | `CAPT` / `L` | `FO` / `R` | `MFD.tsx:88` |
| Widget id prefix | `CAPT_MFD_...` | `FO_MFD_...` | headers |
| KCCU H-events | `A32NX_KCCU_L_<KEY>` | `A32NX_KCCU_R_<KEY>` | `MFD.tsx:236` |
| KBD ON/OFF switch (interaction mode) | `L:A32NX_KCCU_L_KBD_ON_OFF` | `L:A32NX_KCCU_R_KBD_ON_OFF` | `MFDSimvarPublisher.tsx:62-63` |
| Display unit | `CaptMfd`: potentiometer 98, powered by DC ESS **or** DC 1 | `FoMfd`: potentiometer 99, powered by DC 1 **or** DC 2 | `CdsDisplayUnit.tsx:41-61` |
| FMS failure flag (FMS PAGE NOT AVAIL) | `L:A32NX_FMS_L_FAILED` | `L:A32NX_FMS_R_FAILED` | `MFDSimvarPublisher.tsx:78-79` |
| Own FMS label in system selector, knob NORM | `FMS 1` (or `FMS 1-C`) | `FMS 2` (or `FMS 2-C`) | `MFD.tsx:106-125` |
| Active page output | `L:A380X_MFD_L_ACTIVE_PAGE` (string = full URI) | `L:A380X_MFD_R_ACTIVE_PAGE` | `MFD.tsx:359` |
| Mouse cursor | not rotated | rotated 90 deg; x offset -878 inside the shared texture | `MouseCursor.tsx:40,49` |

**FMS source label** (`MFD.tsx:106-125`), from `L:A32NX_FMS_SWITCHING_KNOB` and FMC health
(`L:A32NX_FMC_A_IS_HEALTHY`, `L:A32NX_FMC_B_IS_HEALTHY`):

| Knob | CAPT shows | FO shows |
|---|---|---|
| 0 | FO source: `FMS 2` if FMC B healthy, else `FMS 2-C` | same |
| 1 (NORM) | `FMS 1`, or `FMS 1-C` if FMC A unhealthy | `FMS 2`, or `FMS 2-C` if FMC B unhealthy |
| 2 | CAPT source: `FMS 1` or `FMS 1-C` | same |
| other | `FMS 1` | `FMS 1` |

### 1.3 Display unit wrapper [FBW]

`CdsDisplayUnit` (`MsfsAvionicsCommon/CdsDisplayUnit.tsx:80-330`) wraps the MFD exactly as it wraps the PFD and ND.
States: Off, ThalesBootup (0.25-0.45 s, white 600x70 bar at (84,880) with blue `#4d4dff` 35x60 block at (89,885)),
Selftest (`SAFETY TEST IN PROGRESS` / `(MAX 30 SECONDS)` green 24 px Ecam, centred at y = 50 % and 54 %; duration is the
EFB setting `CONFIG_SELF_TEST_TIME`, default 15 s), On, and Standby (10 s after power loss or brightness 0, then Off).
MFD UI state persists while the unit is off.

**[PORT]:** reuse the port's existing DU state machine (`a380x_du.inc` already exposes `captain_mfd` /
`first_officer_mfd` brightness channels). The MFD core receives `du_on` per side. While the unit is not On, draw
nothing and ignore pointer and key input for that side. **[FBW]** keeps processing KCCU navigation keys while the unit is
off; that is a quirk.

---

## 2. Fonts

| CSS family | File | Used by |
|---|---|---|
| `FBW-Display-EIS-A380-SlashedZero` | `html_ui/Fonts/fbw-a380x/FBW-Display-EIS-A380-SlashedZero.ttf` (source: `fbw-a380x/src/fonts/*.sfd`) | the default for `.mfd-main` and everything else (labels, values, buttons, input fields, menus, footer message, context menu) |
| `Ecam` | `html_ui/Fonts/fbw-a380x/FBW-Display-EIS-A380.ttf` | header callsign (24), page-selector labels (22), `.mfd-surv-heading` (28), `.mfd-adsc-label-off` (20), DU self-test text |

`@font-face` declarations: `pages/common/style.scss:4-18`.

The two fonts differ only in the glyphs **`0`** (slashed vs plain) and **`W`** within ASCII. The SlashedZero file
also lacks U+25A0. Glyphs the MFD needs beyond printable ASCII: **U+25AF `▯`** (mandatory-field placeholder), `°`, and
(for pages) `□` U+25A1 and `◇` U+25C7. The font has **no** lowercase letters and no `! # $ \ ^ { } ~ ``. It has no arrow
glyphs (arrows are drawn as SVG polygons).

**[PORT]:** the existing EWD atlas (`a380x_ewd_font.h`) is built from the plain-zero font. It covers ASCII 32-126 plus
`°`, and has empty boxes for `?`, which the font does contain. Generate a new **MFD atlas** from
`FBW-Display-EIS-A380-SlashedZero.ttf` covering ASCII 32-126, `°`, `▯`, `□`, `◇`, plus two extra slots holding the
plain `0` and plain `W` from `FBW-Display-EIS-A380.ttf` for the `Ecam` family. Store the advance as a float
(`0.626953 x size`, which is 40.125 at a 64 px base). The EWD atlas rounds the advance to 40.0, which accumulates 1.2 px
of error over 24 characters at 27 px.

---

## 3. Colour palette

Source: `MsfsAvionicsCommon/definitions.scss:9-22`, plus literals in widget code.

| Token | Hex | Used for |
|---|---|---|
| `display-background` | `#000000` | screen, input fields, dropdown inner field, footer message text |
| `display-white` | `#ffffff` | labels, button text, menu items, callsign, PENALTY background, white message background |
| `display-cyan` | `#00ffff` | modifiable values (input text), hover frames, caret, selected-text highlight, cyan message background |
| `display-green` | `#00ff00` | computed or non-modifiable values (`.mfd-value`, inactive fields), active SURV states, highlighted tab text |
| `display-amber` | `#e68000` | mandatory placeholders, amber messages, EO label background, errors |
| `display-yellow` | `#ffff00` | temporary flight plan (TMPY) values and title label, mouse cursor |
| `display-magenta` | `#ff94ff` | `.mfd-value.magenta`, `.mfd-label.magenta`, magenta cursor variant |
| `display-light-grey` | `#c4c6cf` | widget borders, validating text, scrollbar thumb, SURV indicators |
| `display-dark-grey` | `#868a9c` | title bar background, disabled text, opened/selected button background, overflow-field border |
| `display-mfd-darker-grey` | `#272525` | button, menu and selector background; disabled field background; footer top rule |
| `display-dark-blue` | `#0c66f7` (rgb 12,102,247) | units (`.mfd-label-unit`) |
| `display-red` | `#ff0000` | not used by the framework |
| `display-grey` | `#4f5467` | not used by the framework |
| title text (literal) | `#222222` | text on the grey title bar |
| SURV status button (literal) | `#3c3c3c` | `.mfd-surv-status-button` background |
| EMER label (literal) | `#ff9601` | ATCCOM EMER selector label background (`AtccomHeader.tsx:91`) |
| page-selector shadow edge | `rgba(211,211,211,0.432)` over `#272525`, which composites to **`#717070`** | bottom and right borders of page selectors |
| `lightgrey` / `lightgray` (CSS named) | `#d3d3d3` | TopTab slanted edge lines |
| `grey` / `gray` (CSS named) | `#808080` | disabled icon and arrow fills |
| bevel dark of `#c4c6cf` | **`#74757b`** | darker side of outset/inset borders (section 13.1) |
| bevel dark of `#00ffff` | **`#00abab`** | darker side of cyan hover bevels |

---

## 4. Typography classes

Family `SZ` = SlashedZero, `E` = Ecam. "inherit" means the class sets no colour; the effective colour is white.

| Class | Family | Size px | Colour | Source (`widget-style.scss` unless noted) |
|---|---|---|---|---|
| `.mfd-label` | SZ | 20 | white | 3-7 |
| `.mfd-label.green / .amber / .magenta` | SZ | 20 | `#00ff00` / `#e68000` / `#ff94ff` | 9-19 |
| `.mfd-label.bigger` / `.biggest` | SZ | 24 / 28 | white | 21-27 |
| `.mfd-label-unit` / `.bigger` | inherit (SZ) | 20 / 24 | `#0c66f7` | 29-36 |
| `.mfd-value` | SZ | 25 | green | 38-42 |
| `.mfd-value.white / .amber / .cyan / .magenta` | SZ | 25 | resp. colour | 44-59 |
| `.mfd-value.tmpy` and `.tmpy .mfd-value` | SZ | 25 | yellow | 61-64 |
| `.mfd-value.sec` | SZ | 25 | white | 66-69 |
| `.mfd-value.smaller` / `.bigger` | SZ | 20 / 30 | (colour as above) | 71-77 |
| `.mfd-button` | SZ | 22 | white | 79-90 |
| `.mfd-dropdown-menu-element` | inherit (SZ) | 20 | white | 205-215 |
| `.mfd-page-selector-label` | E | 22 | white | 225-235 |
| `.mfd-context-menu-element` | SZ | 22 | white | 467-484 |
| `.mfd-input-field-text-input` | SZ | 27, line-height 27 | cyan | 546-554 |
| `... .computedByFms` | SZ | 22 (line-height stays 27) | (unchanged) | 596-598 |
| `.mfd-radio-button` | inherit (SZ) | 26 | inherit / group colour when checked | 364-374 |
| `.mfd-surv-label` | SZ | 20 | white | 699-705 |
| `.mfd-surv-status-button-label` | SZ | 22 | white | 723-729 |
| `.mfd-surv-status-item` | inherit (SZ) | 24 | inherit / green active / amber failed | 741-751 |
| `.mfd-adsc-label-off` | E | 20 | `#868a9c` | 777-781 |
| `.mfd-title-bar-text` (with `.mfd-label`) | SZ | **30** | `#222222` | `style.scss:40-43` (overrides `.mfd-label` 20 because it is declared later) |
| `.mfd-header-callsign` | E | 24 | white | `style.scss:84-91` |
| `.mfd-footer-message-area` | SZ | 26 | `#000000` on coloured background | `style.scss:105-114` |
| `.mfd-surv-heading` / `.failed` | E | 28 | white / amber | `style.scss:142-150` |
| `.mfd-amber-error-message` | inherit (SZ) | 24 | amber | `style.scss:160-167` |
| `.mfd-label.msg-list-element` | SZ | 26 | white | `pages/FMS/MfdMsgList.scss:8-13` |
| TopTab label | inherit (SZ) | `round(h*0.66)` for 2 or fewer tabs, `round(h*0.58)` for more | see section 13.7 | `TopTabNavigator.tsx:84` |

---

## 5. Screen skeleton and stacking

### 5.1 DOM order and regions [FBW]

`MFD.tsx:392-417` renders, inside `.mfd-main` (flex column, 768x1024, black):

1. the header container (system header, rebuilt on every navigation),
2. `MfdFmsPageNotAvail` overlay anchor (zero height),
3. `MfdMsgList` overlay anchor (zero height),
4. `MfdFmsFplnDuplicateNames` overlay anchor (zero height),
5. `.mfd-navigator-container` (flex 1), which holds the active page: its title bar, page body and footer,
6. `MouseCursor` (absolute positioning).

Canonical regions for a standard page (all systems):

```
y    0 ┌───────────────────────────────────────────────┐
       │ Header row 1: system selector     callsign     │  0..44
y   44 ├───────────────────────────────────────────────┤
       │ Header row 2: page selectors                   │ 44..98
y   98 ├───────────────────────────────────────────────┤
       │ Title bar (grey)                    [EO][TMPY] │ 98..138
y  138 ├───────────────────────────────────────────────┤
       │                                               │
       │ Page body (812 px tall)                        │ 138..950
       │                                               │
y  950 ├───────────────────────────────────────────────┤ (2 px rule #272525)
       │ [MSG LIST]  message area                      │ 950..1024
y 1024 └───────────────────────────────────────────────┘
```

Derivation: header row 1 = dropdown 40 + 2x2 border = 44. Header row 2 = page selector 2 + (10 + 1 + 3 + 22 + 3 + 1 + 10)
+ 2 = 54. Title bar = 3 + max(30, 30+2x2) + 3 = 40; the hidden PENALTY label still occupies space. Footer = 2 rule + 5 +
(2 + 9 + 2x22 + 5 + 2) + 5 = 74.

### 5.2 Stacking order (z), lowest first [FBW]

| z | Layer | Source |
|---|---|---|
| auto | page body widgets in DOM order | |
| 0 / 1 | TopTab unselected / selected tab element | `widget-style.scss:315-325` |
| 5 | InputField overflow box | `widget-style.scss:539-544` |
| 10 | ContextMenu, ConfirmationDialog | `widget-style.scss:467-475, 639-650` |
| 20 | full-screen overlays (MSG LIST, DUPLICATE NAMES, FMS PAGE NOT AVAIL) | `MfdFmsFpln.scss:262-273` |
| 60 | every dropdown list (Button menu, DropdownMenu list, page-selector menu) | `widget-style.scss:194-203` |
| 999 | mouse cursor | `widget-style.scss:631-637` |

Header menus therefore draw on top of an open MSG LIST overlay, and the header remains usable while the overlay is
open.

**[PORT]:** render in this order. Hit-test in reverse order: the topmost visible, hit-testable element wins. Elements
with `display:none` or `visibility:hidden` are neither drawn nor hit-tested.

---

## 6. Header

The header is rebuilt from scratch on **every** URI change (`MFD.tsx:307-335, 356`), so any open header menu closes on
navigation. `headerForSystem(sys)` (`MfdPageDirectory.tsx:350-369`) selects `FmsHeader` for `fms`, `AtccomHeader` for
`atccom`, `SurvHeader` for `surv`, `FcuBkupHeader` for `fcubkup`, and `FmsHeader` for anything else.

### 6.1 Row 1: system selector and callsign (all systems) [FBW]

`AbstractHeader.tsx:97-114`. The row is a flex row with `justify-content: space-between`, 768x44, at y 0..44.

**System selector**: a `DropdownMenu` (section 13.5) with `containerStyle="width: 25%"`, `alignLabels="flex-start"`, and
`freeTextAllowed=false`.

| Part | Rect / position |
|---|---|
| Outer box | `0..192, 0..44`; fill `#272525`; 2 px raised bevel `#c4c6cf` (hover: cyan) |
| Inner value field (black) | `2..165, 2..42` |
| Arrow column (grey `#272525`) | `165..190, 2..42` |
| Arrow | down-pointing triangle (170,14.5) (185,14.5) (177.5,29.5), fill white (`gray` if disabled) |
| Value text | baseline-left **(12, 30.1)**, SZ 27 px, **cyan** (InputField default colour) |

Value text derivation: InputField root 49 px tall, centred in the 40 px inner box, so its top is at y -2.5. Content top
= -2.5 + 2 + 4 = 3.5. The 27 px text box is centred in the 37 px content, so its top is 8.5 and the baseline is 30.1.
x = 2 (outer border) + 2 (transparent field border) + 2 (padding) + 6 (empty leading-unit margin) = 12.

**Items** (`AbstractHeader.tsx:29`), in order:

| Index | Label | Action (`changeSystem`, lines 33-54) |
|---|---|---|
| 0 | active FMS source: `FMS 1` / `FMS 2` / `FMS 1-C` / `FMS 2-C` (live, section 1.2) | `navigateTo('fms/active/init')` |
| 1 | `ATCCOM` | `navigateTo('atccom/connect')` |
| 2 | `SURV` | `navigateTo('surv/controls')` |
| 3 | `FCU BKUP` | `navigateTo('fcubkup/afs')` (renders NOT FOUND, section 10.5) |

The selected index follows `activeUri.sys`: `fms` 0, `atccom` 1, `surv` 2, `fcubkup` 3, anything else 0
(`AbstractHeader.tsx:66-87`). The field shows `values[selectedIndex]`. Choosing the item that is already selected still
navigates; `navigateTo` ignores the call only if the URI is identical.

List geometry: rows at x `0..196` (min-width 100 % of the 192 px container, plus 2x2 border), first row at
y 46, 36 px per row, text baseline-left (21, 70 + 36*k), SZ 20 white, left aligned. The invalid CSS
`text-align: flex-start` falls back to left.

**Callsign**: `span.mfd-header-callsign`, `width: 25%` plus padding `4 10 0 10`, so the box is `556..768, 0..28`.
Text baseline-left **(566, 23.2)**, Ecam 24 px, white. The value is `fmgc.data.atcCallsign` (the ATC flight number from
INIT). When it is null the text is **`----------`** (10 dashes) (`AbstractHeader.tsx:111`). There are no other
right-side elements.

### 6.2 Row 2: page selectors

All selectors are `PageSelectorDropdownMenu` (section 13.6) at y **44..98**. A selector is "active" (1 px white frame
around its label) when the URI category matches.

#### 6.2.1 FMS header (`FmsHeader.tsx:40-103`) [FBW]

Four selectors, `flex: 1`, each **192 px**: `x0 = 192*i`. Each has the down triangle because it has more than one item.

| i | Label | Active when category is | Menu items, in order (**disabled** items marked) |
|---|---|---|---|
| 0 | `ACTIVE` | `active` | `F-PLN` -> `fms/active/f-pln` · `PERF` -> `fms/active/perf` · `FUEL&LOAD` -> `fms/active/fuel-load` · **`WIND`** (disabled) -> `fms/active/wind` · `INIT` -> `fms/active/init` |
| 1 | `POSITION` | `position` | `MONITOR` -> `fms/position/monitor` · **`REPORT`** (disabled) · `NAVAIDS` -> `fms/position/navaids` · `IRS` -> `fms/position/irs` · **`GNSS`** (disabled) · **`TIME`** (disabled) |
| 2 | `SEC INDEX` | `sec`, `sec1`, `sec2`, `sec3` | `SEC 1` -> `fms/sec/index/1` · `SEC 2` -> `fms/sec/index/2` · `SEC 3` -> `fms/sec/index/3` |
| 3 | `DATA` | `data` | `STATUS` -> `fms/data/status` · **`WAYPOINT`** (disabled) · **`NAVAID`** (disabled) · **`ROUTE`** (disabled) · `AIRPORT` -> `fms/data/airport` · **`PRINTER`** (disabled) |

Disabled targets: `fms/active/wind`, `fms/position/report`, `fms/position/gnss`, `fms/position/time`,
`fms/data/waypoint`, `fms/data/navaid`, `fms/data/route`, `fms/data/printer`. None of these is routed (they would render
NOT FOUND).

Per selector (x0 = 192 i), using section 13.6 geometry:

| Item | Value |
|---|---|
| Outer | `x0..x0+192, 44..98` |
| Label container | `x0+2..x0+159` (157 px); label centre x = `x0+80.5` |
| Label text, 22 px Ecam | baseline y **77.6**; ACTIVE starts at x 39.1 · POSITION 217.3 · SEC INDEX 402.4 · DATA 628.9 |
| Active frame (1 px white) | y `56..86`; x = text x - 4 to text end + 4 (ACTIVE `35.1..125.9`, POSITION `213.3..331.7`, SEC INDEX `398.4..530.6`, DATA `624.9..688.1`) |
| Triangle | `(x0+167,62) (x0+182,62) (x0+174.5,77)`, white |
| Menu | `x0..x0+196`, from y 98; rows 36 px; height = 4 + 36 n (ACTIVE 184, POSITION 220, SEC INDEX 112, DATA 220). The DATA menu extends 4 px past x 768 and is clipped. |

#### 6.2.2 ATCCOM header (`AtccomHeader.tsx:43-96`) [FBW]

Six single-item selectors (no triangle). Clicking one navigates immediately. The flex weights are 1, 1, 1.2, 1, 1, 1.
Resolving them with flex min-content clamping (CONNECT and REQUEST need 128.55 px) gives:

| Label | x range | Text (Ecam 22) | Target | Active when category is |
|---|---|---|---|---|
| `CONNECT` | 0..128.5 | baseline-left (16.0, 77.6) | `atccom/connect` | `connect` |
| `REQUEST` | 128.5..257.1 | (144.5, 77.6) | `atccom/request` **[FBW-NA]** | `request` |
| `REPORT` / `& NOTIFY` (2 lines) | 257.1..403.1 | line 1 (288.7, 66.6), line 2 (274.9, 88.6) | `atccom/report-modify/position` **[FBW-NA]** | `report-modify` |
| `MSG` / `RECORD` (wraps to 2 lines) | 403.1..524.7 | (443.2, 66.6), (422.5, 88.6) | `atccom/msg-record` | `msg-record` |
| `D-ATIS` | 524.7..646.4 | (544.2, 77.6) | `atccom/d-atis/list` | `d-atis` |
| `EMER` | 646.4..768 | (679.6, 77.6), text **black**, amber `#ff9601` background behind the text only: `679.6..734.8, 60..82` | `atccom/emer` **[FBW-NA]** | `emer` |

The two-line labels use `labelStyle` margin-top/bottom 0 and padding-top/bottom 2 (lines 68 and 76), so the label box
is 1 + 2 + 44 + 2 + 1 = 50 px, the same height as a one-line label.

**[QUIRK]:** FBW passes the string literal `"REPORT<br /> & NOTIFY"`. FSComponent inserts strings as text nodes, so
the `<br />` markup is not interpreted.

**[PORT]:** render two lines. The wording `& NOTIFY` is most likely a typo: the URIs are `report-modify/*` and the
aircraft page is REPORT & MODIFY. Use **`REPORT` / `& MODIFY`** (see Open questions).

#### 6.2.3 SURV header (`SurvHeader.tsx:31-53`) [FBW]

| Label | x range | Text | Target |
|---|---|---|---|
| `CONTROLS` | 0..192 (`width:25%`) | (40.8, 77.6) | `surv/controls` |
| `STATUS & SWITCHING` | 192..499.2 (`width:40%`) | (221.5, 77.6) | `surv/status-switching` |

The rest of row 2 (x 499.2..768) is black.

#### 6.2.4 FCU BKUP header (`FcuBkupHeader.tsx:31-53`) [FBW]

The row has an inline `width: 50%` (384 px). It holds `AFS CP` (0..192, text (54.6, 77.6)) -> `fcubkup/afs`, and
`EFIS CP` (192..384, text (239.7, 77.6)) -> `fcubkup/efis`. Both targets are **[FBW-NA]** and show NOT FOUND.

---

## 7. Active page title bar

`ActivePageTitleBar.tsx:58-82`, `style.scss:30-75`. Located at y **98..138** (40 px) at the top of every page.

| Element | FMS-subsystem page (`isFmsSubsystemPage=true`) | Other pages |
|---|---|---|
| Title box (`flex:10`, fill `#868a9c`, padding 3/10/3/7) | `0..594` | `0..598` |
| EO section (fill `#868a9c`, 70 wide) | `596..666` (2 px black gap before it) | `598..668` (no gap) |
| TMPY section (fill `#868a9c`, 100 wide) | `668..768` (2 px black gap) | `668..768` |

On non-FMS pages the margins are 0, so the three grey boxes merge into one continuous bar (`ActivePageTitleBar.tsx:44`).

* **Title text**: baseline-left **(7, 125)**, SZ 30 px, `#222222`. The text is `activePage + offsetText`.
  * For FmsPage pages the text is `${category.toUpperCase()}/${pageTitle}` (`FmsPage.tsx:120-124`). Examples:
    `ACTIVE/F-PLN`, `SEC2/PERF`, `POSITION/IRS`, `DATA/STATUS`, `SEC/SEC INDEX`.
  * Non-FMS pages pass a literal: `CONTROLS`, `STATUS & SWITCHING`, `CONNECT`, `MSG RECORD`, `MSG RECORD/ALL MSG`,
    `MSG RECORD/MONITORED MSG`, `MSG RECORD/ALL MSG/EXPAND`, `D-ATIS/LIST`, `D-ATIS/RECEIVED`, `MESSAGE LIST`,
    `DUPLICATE NAMES`, `ERROR 404 NOT FOUND`.
  * `offsetText` is `"     OFFSET"+offset` when the offset is non-empty. HTML collapses the five spaces to one.
    FmsPage always passes `''`, so the offset never shows. **[FBW-NA]**
* **PENALTY**: right-aligned in the title box. Box `448.3..584, 101..135` (white fill, padding 2), text `PENALTY`
  baseline-left (450.3, 127), 30 px `#222222`. Visible only when `isFmsSubsystemPage && penaltyActive`.
* **EO**: box `610.2..651.8, 101..135` (amber `#e68000`), text `EO` (612.2, 127) `#222222`. Visible when
  `isFmsSubsystemPage && fmgc.data.engineOut`.
* **TMPY**: box `678.4..757.6, 101..135` (yellow `#ffff00`), text `TMPY` (680.4, 127) `#222222`. Visible when
  `isFmsSubsystemPage && displayTmpy`.
* Hidden labels keep their space (`visibility:hidden`). The grey section boxes are always drawn.

FmsPage visibility rules (`FmsPage.tsx:59-104`):

* **TMPY** shows when `sys == fms` and a temporary flight plan is loaded for this page (category `active` and
  `flightPlanInterface.hasTemporary`), **and** one of these holds:
  * the page is one of `perf`, `fuel-load`, `init`, `f-pln`, `f-pln-airways`, `f-pln-departure`, `f-pln-arrival`,
    `f-pln-lat-rev`, `f-pln-hold`, `f-pln-vert-rev`;
  * the full URI equals `fms/active/f-pln-fix-info`, `fms/sec/index` or `fms/active/f-pln-direct-to`.
* **PENALTY** shows when `sys == fms` and `fmgc.data.fuelPenaltyActive`, **and** either the full URI equals
  `fms/data/status`, or the page is `fuel-load`, `f-pln` or `f-pln-hold`.
* **[QUIRK]:** both rules compare full URIs exactly, so the deep-linked forms `fms/sec/index/2` and
  `fms/data/status/acft-status` never show the label. **[PORT]:** compare `sys/category/page` and ignore `extra`.

---

## 8. Footers and the message area

### 8.1 Standard footer (`Footer.tsx`) [FBW]

The footer is used on every FMS page, on SURV pages, on ATCCOM CONNECT and MSG RECORD pages, and on NOT FOUND.

| Part | Geometry |
|---|---|
| Footer box | `0..768, 950..1024`; top rule `0..768, 950..952` in `#272525`; padding 5 |
| Button | `Button` with `width:100px`: outer **`5..133, 957..1019`** (100 content + 2x12 padding + 2x2 border; 2x22 lines + 9 + 5 + 4). Label in two centred lines, `white-space: pre` |
| Button lines | centre x 69; line 1 baseline 985.6, line 2 baseline 1007.6, SZ 22 white |
| Message area | starts at x **148** (133 + `margin-left:15`); vertically centred in `957..1019` |

**Button label**: `MSG` / `LIST` when no uncleared message exists, `CLEAR` / `INFO` otherwise.

**Click**: if a message is displayed, call `clearLatestFmsErrorMessage()`; otherwise open the MSG LIST overlay
(section 9.1) on **this** side (`Footer.tsx:62-68`).

**Message display** (`Footer.tsx:20-45`): show the first entry in the shared `fmsErrors` array (newest first) whose
`cleared == false`.

* Text: SZ 26, **black**, background = `#ffffff` (white message), `#e68000` (amber), or `#00ffff` (cyan, never
  produced by the A380 FMC).
* The background rectangle is the text box only: `148 .. 148 + maxLineWidth`, height 26 per line, vertically centred on
  y 988.
  * One line: background `975..1001`, baseline 995.8.
  * Two lines: background `962..1014`, baselines 982.8 and 1008.8.
* `\n` in `messageText` forces a line break (`white-space: pre-wrap`). Lines longer than 615 px (37 characters at
  16.30 px) wrap at spaces.
* With no message: empty text, nothing drawn. **[QUIRK]** FBW sets `backgroundColor='none'`, which is invalid CSS and
  harmless.

### 8.2 FMS message queue semantics (shared by both sides) [FBW]

From `FMC/FlightManagementComputer.ts`:

* `FmsErrorMessage = { message, messageText, backgroundColor: 'white'|'amber'|'cyan', cleared, isResolvedOverride,
  onClearOverride }` (lines 74-81).
* `addMessageToQueue(msg, isResolved?, onClear?, details?)` (lines 1136-1161):
  * `messageText = msg.text + ('\n' + details if details)`.
  * Colour is amber if `msg.isAmber`, else white.
  * An existing **cleared** entry with identical text is removed first. The new entry is inserted at index 0.
  * Uncleared duplicates are not merged, so they stack.
* `clearLatestFmsErrorMessage()` (lines 800-817) acts on the first uncleared entry:
  * Type II: set `cleared = true` (it stays in MSG LIST) and call `onClear`.
  * Type I: remove it.
* `removeMessageFromQueue(text)` removes the first entry with that text (lines 1167-1172).
* `updateMessageQueue()` runs every FMC update and removes Type II entries whose `isResolved()` is true
  (lines 1174-1181, 1515).
* Data-entry errors use `showFmsErrorMessage(type, details)` (lines 1094-1111):

| `FmsErrorType` | Line 1 | Line 2 (details) | Message type, colour |
|---|---|---|---|
| `EntryOutOfRange` | `ENTRY OUT OF RANGE` | `RNG: {min} TO {max}` + (` {UNIT}` if any), when the format provides it (`DataEntryFormats.tsx:16`) | Type I, white |
| `FormatError` | `FORMAT ERROR` | `FORMAT: {fmt}` + (` {UNIT}`) (`DataEntryFormats.tsx:15`) | Type I, white |
| `NotInDatabase` | `NOT IN DATABASE` | (none) | Type I, white |
| `NotYetImplemented` | `NOT YET IMPLEMENTED` | (none) | Type I, white |
| any other | (no message) | | |

Message catalogue: `MFD/shared/NXSystemMessages.ts:80-122` (real messages, with `isAmber` and Type I/II), and
`NXFictionalMessages` at lines 124 onward. The full catalogue is specified in `02-data-formats-messages.md`. The
framework only needs `text`, `isAmber`, `isTypeTwo`, `isResolved()` and `onClear()`.

**[PORT]:** implement the queue in the FMS core as a fixed array of about 32 entries: `{msg_id, text[2][40], amber,
type2, cleared}`. Both MFD footers read it.

### 8.3 ATCCOM footer (`pages/ATCCOM/MfdAtccomFooter.tsx`) [FBW]

The ATCCOM footer is used on the D-ATIS pages. Its geometry is identical to the standard footer. The button always
reads `CLEAR` / `INFO`, and its click calls `atcService.clearLatestAtcErrorMessage()` only if a message is shown. The
message comes from `atcService.atcErrors` with the same colour rules. CONNECT and MSG RECORD use the **standard** footer.

### 8.4 FlightPlanFooter (`FlightPlanFooter.tsx`) [FBW]

FlightPlanFooter is a row placed by revision pages above the standard footer. It is
`display:flex; justify-content:space-between` and holds two wrappers, each `display:flex; justify-content:flex-end;
padding:2px` (`MfdFmsFplnHold.scss:26-30`), so the row is 44 px tall. Both wrappers always take space.

* Left: **`RETURN`** button (40x110.8), visible when **no** temporary plan exists. On click it calls
  `fmc.resetRevisedWaypoint()` and then `navigateTo('back')`.
* Right: **`TMPY F-PLN`** button (165.9 wide, text **yellow**), visible when a temporary plan exists. On click it calls
  `resetRevisedWaypoint()` and then `navigateTo('fms/<current category>/f-pln')`.

### 8.5 KCCU CLRINFO

The KCCU `CLRINFO` key calls `clearLatestFmsErrorMessage()` (`MFD.tsx:275-277`). It does not open MSG LIST when the
queue is empty.

---

## 9. Overlays

Every overlay anchor is `position:relative` with zero height, placed directly under the header. The overlay body is
`position:absolute; left 0; top 0; 768 x 852; z 20; black`, which covers y **98..950**: the title bar and page body,
but not the header or footer (`MfdFmsFpln.scss:258-273`).

### 9.1 MSG LIST (`pages/FMS/MfdMsgList.tsx`) [FBW]

* Opened by the footer button (per side). Closed by its own `CLOSE` button.
* **[QUIRK]:** it stays open across navigation (`messageListOpened` is never reset in `activeUriChanged`).
  **[PORT]:** close MSG LIST on any navigation.
* Content:
  * Title bar `MESSAGE LIST` (FMS style, shows EO) at y 98..138.
  * Message container: margin 75, so x `75..693`, with a 2 px white top rule at y `213..215`.
  * Up to **4** entries (`idx < 4`), each row 92 px: an 80 px content box, padding 5, and a 2 px white bottom rule.
    Row k spans y `215+92k .. 307+92k`. Text is SZ 26 white, baseline-left (80, 240.8 + 92k), wrapping inside 608 px.
    `\n` is not honoured (it collapses to a space).
  * The list shows **all** entries (cleared or not), newest first. It refreshes at 1 Hz.
* `CLOSE` button: bottom-left, x `0..97`, y `910..950`.

### 9.2 FMS PAGE NOT AVAIL (`pages/FMS/MfdFmsPageNotAvail.tsx`) [FBW]

* Shown when this side's FMS failed flag is set (section 1.2) **and** `activeUri.sys == fms`.
* FBW geometry: body `top:34px; height:818px`, so y `132..950`. It covers the bottom 6 px of the title bar.
* Content: `FMS PAGE NOT AVAIL`, amber 24 px, centred at (384, ~541). The text is 270.8 px wide.
* **[PORT]:** use y `138..950`, which leaves the title bar intact. Centre the text at x 384 with baseline y 551.2
  (the 24 px line box is flex-centred on 544, the centre of 138..950). Swallow pointer input inside the overlay.

### 9.3 DUPLICATE NAMES (`pages/FMS/F-PLN/MfdFmsFplnDuplicateNames.tsx`) [FBW]

This uses the same overlay frame and an FMS title bar `DUPLICATE NAMES`. The content belongs to the F-PLN spec. The
framework only provides `deduplicateFacilities(items)`, which returns a promise of the chosen item or `undefined`.
**[PORT]:** this becomes an async request object in the core: `pending_request`, `items[]`, `result`.

### 9.4 NOT FOUND page (`pages/FMS/MfdNotFound.tsx`) [FBW]

This is the routing fallback (section 10.3). It renders a title bar `ERROR 404 NOT FOUND` (non-FMS style), an empty
body and a standard footer, then calls `navigateTo('back')` **500 ms** after render.

---

## 10. Navigation

### 10.1 URI model (`MfdUiService.tsx`) [FBW]

* URI grammar: `sys/category[/page[/extra...]]`, lower case. `parseUri` splits on `/` (lines 46-55):
  * `sys` = part 0 (`fms` | `atccom` | `surv` | `fcubkup`)
  * `category` = part 1
  * `page` = part 2, or undefined
  * `extra` = parts 3 onward joined with `/`
* `navigateTo(uri)` (lines 62-89):
  1. If `uri == activeUri.uri` and `uri` is not in `{ 'fms/active/f-pln/top', 'fms/active/perf' }`, ignore the call.
  2. Trigger `UNFOCUS_INPUT_FIELD`, which drops PC keyboard capture.
  3. If `uri == 'back'`: when the stack depth is below 2, return. Otherwise pop the top and navigate to the new top
     **without pushing**.
  4. Otherwise push `uri`. The stack is unbounded and duplicates are allowed: forced reloads push again.
  5. Set `activeUri`, publish `mfd_active_uri` on the bus, and (in `MFD.tsx:302-360`) destroy and rebuild the header
     **and** the page, then write `L:A380X_MFD_{L|R}_ACTIVE_PAGE` = the full URI.
* `canGoBack()` returns `depth > 1`.
* If the master FMC does not exist yet, `activeUriChanged` returns without rendering (`MFD.tsx:303-305`).

**Rebuild semantics:** every navigation re-creates the page, including a navigation to the same page with a different
`extra`. Page-local UI state is reset: tab selection, scroll position, open menus and any unsubmitted field entry.
Deep links in `extra` initialise that state:

| URI example | Meaning |
|---|---|
| `fms/data/status/acft-status`, `.../fms-pn` | select tab 0 or 1 |
| `fms/sec/index/1` .. `3` | select SEC tab |
| `fms/active/f-pln/top`, `fms/active/f-pln/dest` | scroll position on the F-PLN page |
| `fms/position/navaids/withReturn`, `fms/position/monitor/withReturn` | show a RETURN button (`showReturnButtonUriExtra`, `shared/utils.ts:56`) |
| `atccom/d-atis/received/<n>` | which ATIS |

**[PORT]:**

* Keep a per-side stack of fixed-size URI strings (64 bytes each) with a cap of 32 entries; drop the oldest on overflow.
* Each page module exposes `enter(side, const MfdUri*)`, `leave()`, `update(dt)` and `build/draw`. On every
  `navigateTo`, call `leave` on the old page and `enter` on the new one, resetting page-local state.
* An unsubmitted field entry is **discarded** (not validated) on navigation (section 12.4).

### 10.2 Page routing table (`MfdPageDirectory.tsx:57-348`) [FBW]

The lookup key is `sys/category/page` (or `sys/category` when there is no page). `extra` is never part of the key.
`*` means each of `active`, `sec1`, `sec2`, `sec3`.

| Key | Page component | `pageTitle` | Title-bar text |
|---|---|---|---|
| `fms/*/perf` | MfdFmsPerf | `PERF` | `ACTIVE/PERF`, `SEC1/PERF`, ... |
| `fms/*/init` | MfdFmsInit | `INIT` | `<CAT>/INIT` |
| `fms/*/fuel-load` | MfdFmsFuelLoad | `FUEL&LOAD` | `<CAT>/FUEL&LOAD` |
| `fms/*/f-pln` | MfdFmsFpln | `F-PLN` | `<CAT>/F-PLN` |
| `fms/*/f-pln-airways` | MfdFmsFplnAirways | `F-PLN/AIRWAYS` | |
| `fms/*/f-pln-departure` | MfdFmsFplnDep | `F-PLN/DEPARTURE` | |
| `fms/*/f-pln-arrival` | MfdFmsFplnArr | `F-PLN/ARRIVAL` | |
| `fms/active/f-pln-direct-to` (active only) | MfdFmsFplnDirectTo | `F-PLN/DIRECT-TO` | |
| `fms/*/f-pln-vert-rev` | MfdFmsFplnVertRev | `F-PLN/VERT REV` | |
| `fms/*/f-pln-hold` | MfdFmsFplnHold | `F-PLN/HOLD` | |
| `fms/active/f-pln-fix-info` (active only) | MfdFmsFplnFixInfo | `F-PLN/FIX INFO` | |
| `fms/sec/index` | MfdFmsSecIndex | `SEC INDEX` | `SEC/SEC INDEX` |
| `fms/position/monitor` | MfdFmsPositionMonitor | `MONITOR` | `POSITION/MONITOR` |
| `fms/position/irs` | MfdFmsPositionIrs | `IRS` | |
| `fms/position/navaids` | MfdFmsPositionNavaids | `NAVAIDS` | |
| `fms/data/status` | MfdFmsDataStatus | `STATUS` | `DATA/STATUS` |
| `fms/data/airport` | MfdFmsDataAirport | `AIRPORT` | |
| `surv/controls` | MfdSurvControls | (own title) | `CONTROLS` |
| `surv/status-switching` | MfdSurvStatusSwitching | (own) | `STATUS & SWITCHING` |
| `atccom/connect` | MfdAtccomConnect | `""` (own title) | `CONNECT` |
| `atccom/msg-record` | MfdAtccomMsgRecord | | `MSG RECORD` |
| `atccom/msg-record/all-msg` | MfdAtccomMsgRecordAll | | `MSG RECORD/ALL MSG` |
| `atccom/msg-record/monitored-msg` | MfdAtccomMsgRecordMonitored | | `MSG RECORD/MONITORED MSG` |
| `atccom/msg-record/all-msg-expand` | MfdAtccomMsgRecordExpand | | `MSG RECORD/ALL MSG/EXPAND` |
| `atccom/d-atis/list` | MfdAtccomDAtis | | `D-ATIS/LIST` |
| `atccom/d-atis/received` | MfdAtccomDAtisReceived | | `D-ATIS/RECEIVED` |
| anything else | MfdNotFound | `NOT FOUND` | `ERROR 404 NOT FOUND`, then back after 500 ms |

URI constants: `shared/utils.ts:58-75`.

**Reachable but unrouted [FBW-NA]:** `fcubkup/afs`, `fcubkup/efis`, `atccom/request`, `atccom/report-modify/*`,
`atccom/emer`, `fms/sec{1,2,3}/wind`, and `fms/sec{n}/what-if` (SEC INDEX buttons, `MfdFmsSecIndex.tsx:490,515`). In
FBW, selecting FCU BKUP in the system selector shows the FCU BKUP header with `ERROR 404 NOT FOUND` for 0.5 s and then
returns to the previous page.

**[PORT]:** keep the generic fallback for truly unknown URIs, but do not show `404`. For the known-unimplemented URIs
above, render a stub page: normal header with the correct selector active, normal title bar (`AFS CP`, `EFIS CP`,
`REQUEST`, ...), an empty body, and the standard footer. Do not auto-return. FCU BKUP AFS/EFIS content is out of scope
here (see Open questions).

### 10.3 FmsPage base behaviour (`FmsPage.tsx`) [FBW]

FmsPage is the base for every FMS page:

* Loads the flight plan for the category (lines 188-275):
  * `active`: the active plan, or the temporary plan if one exists.
  * `secN`: secondary plan N. If secondary N is deleted while the page is open (and the page is not INIT),
    **navigate to the same URI with `secN` replaced by `active`** (lines 217-222, 236-240, 254-258).
* Polls every **500 ms** for a flight-plan version change and calls `onNewData()`.
* Renders the title bar (section 7) with EO, TMPY and PENALTY.

### 10.4 KCCU page keys (`MFD.tsx:243-279`) [FBW]

| Key | Action |
|---|---|
| `DIR` | `navigateTo('fms/active/f-pln-direct-to')` |
| `PERF` | `navigateTo('fms/active/perf')` (forced reload even when already there) |
| `INIT` | `navigateTo('fms/active/init')` |
| `NAVAID` | `navigateTo('fms/position/navaids')` |
| `FPLN` | `navigateTo('fms/active/f-pln/top')` (forced reload) |
| `DEST` | `navigateTo('fms/active/f-pln/dest')` |
| `SECINDEX` | `navigateTo('fms/sec/index')` |
| `SURV` | `navigateTo('surv/controls')` |
| `ATCCOM` | `navigateTo('atccom/connect')` |
| `MAILBOX` | none **[FBW-NA]** (intended: move cursor to the SD mailbox) |
| `ND` | none **[FBW-NA]** (intended: move cursor to the ND) |
| `CLRINFO` | `clearLatestFmsErrorMessage()` |

---

## 11. Pointer model: cursor, hover, click, outside-click

### 11.1 Mouse cursor (`MouseCursor.tsx`) [FBW]

* An 80x80 box centred on the pointer: `left = x - 40`, `top = y - 40`, z 999, never hit-testable. The FO side
  subtracts 878 from x because of the shared texture.
* Shape, in the box's local coordinates (0..80), stroke colour yellow `#ffff00` (magenta `#ff94ff` variant exists;
  the MFD always uses yellow):
  * polyline `(0,0) (40,35) (80,0)`, 3 px stroke, no fill (upper chevron, apex pointing to the centre);
  * polyline `(0,80) (40,45) (80,80)`, 3 px stroke (lower chevron);
  * centre mark: line `(40,39)-(40,41)` and line `(39,40)-(41,40)`, 2 px stroke.
  * **FO:** the group is rotated **90 deg about (40,40)**, so the chevrons open left and right.
* Shown on any mouse move over the MFD. Hidden **5 s** after the last move.
* **[PORT]:** draw it last, with GL lines 3 px and 2 px wide (or quads for portability). In X-Plane, hide the OS cursor
  over the MFD screen (`screenCursorCallback` returns `xplm_CursorHidden`) and drive the position from the screen
  cursor callback.

### 11.2 Hover [FBW]

CSS `:hover` applies to the element under the pointer **and its ancestors**. Only these elements have hover styles:

| Element | Hover visual |
|---|---|
| `.mfd-button` (enabled) | border colour cyan, same bevel style (raised; inset when selected or opened) |
| `.mfd-icon-button` | 2 px raised cyan bevel (also when disabled: **[QUIRK]**; **[PORT]** no hover when disabled) |
| `.mfd-dropdown-outer` (enabled, not inactive) | raised cyan bevel |
| `.mfd-dropdown-menu-element` | 3 px **solid** cyan frame inside the 36 px row (also on disabled rows: **[QUIRK]**; **[PORT]** none on disabled rows) |
| `.mfd-page-selector-outer` | all four 2 px borders cyan |
| `.mfd-top-tab-navigator-bar-element-label` | 2 px raised cyan bevel around the label rectangle |
| `.mfd-radio-button` (label row) | 2 px raised cyan bevel around the full row |
| `.mfd-context-menu-element` (enabled) | 3 px solid cyan frame |
| `.mfd-input-field-container` (enabled, not inactive) | 2 px **sunken** cyan bevel |
| `.mfd-surv-status-button` | 2 px solid cyan border |

`SurvButton`, `AdscButton`, labels and values have no hover style.

**[PORT]:** each frame, hit-test the pointer against the hit-testable widgets in reverse z order. The topmost hit is
"hovered", and its hover visual is drawn. Ancestors matter only for composite widgets: hovering a field's unit text
still highlights the field. When the pointer leaves the screen, or the cursor is hidden by the 5 s timer, clear the
hover.

### 11.3 Click and outside-click [FBW]

* A browser `click` is a press and release on the same element.
* Every open menu (Button menu, DropdownMenu list, page-selector menu) listens for clicks anywhere in `MFD_CONTENT`.
  A click whose target is outside the widget's container closes it. The click is **also** delivered to its target:
  with the ACTIVE menu open, clicking POSITION closes ACTIVE and opens POSITION in the same click.
* **[QUIRK]:** `MFD_CONTENT` spans both MFDs, so a click on the FO MFD also closes menus open on the CAPT MFD.
  **[PORT]:** limit outside-clicks to the same side.
* An InputField is blurred (and validated) by **mouse-down** anywhere outside its text span, before the click target
  acts. Clicking another field first commits the old one, then focuses the new one.
* **[PORT] event algorithm**, per side:
  1. On press: record the hit widget `pressed_id`. If a field is focused and the hit is not that field's
     text/units/area, commit the field (section 12.3) first.
  2. On release: let `target` be the hit widget at the release position. If `target == pressed_id`, it is a click.
  3. Close every open popup (menu or context menu) that does not contain `target`. ContextMenu closes on **any**
     click more than 100 ms after it opened, even one on its own disabled item.
  4. Dispatch the click to `target`.

  Optionally draw `pressed_id` as inset while the button is held (FBW has no `:active` style; the aircraft shows a
  pressed state).

### 11.4 Mouse wheel

FBW relies on native scrolling for `overflow:auto` dropdown lists (section 13.2).
**[PORT]:** a wheel event over an open list scrolls it 36 px (one row) per notch, clamped. Wheel events elsewhere go to
the page; page specs define scrolling for F-PLN and similar pages.

---

## 12. Keyboard and KCCU input

### 12.1 KCCU hardware events (`fbw-a380x/.../model/behaviour/kccu.xml`) [FBW]

Each KCCU key fires the H-event **`A32NX_KCCU_<SIDE>_<KEY>`**, with `<SIDE>` = `L` (CAPT) or `R` (FO)
(lines 24, 447-450). The keys are:

| Group | Keys |
|---|---|
| Digits | `0` .. `9`, `DOT`, `PLUSMINUS` |
| Letters | `A` .. `Z` |
| Editing | `ENT`, `BACKSPACE`, `SP` (space), `SLASH`, `ESC` (keyboard), `ESC2` (CCD escape) |
| Arrows | `UP`, `DOWN`, `LEFT`, `RIGHT` |
| Page keys | `DIR`, `PERF`, `INIT`, `NAVAID`, `MAILBOX`, `FPLN`, `DEST`, `SECINDEX`, `SURV`, `ATCCOM`, `ND` |
| Misc | `KBD`, `CLRINFO`, `REWIND`, `FORWARD` (tooltips say INOP: move the cursor to the left or right display) |

Two switches are L-vars: `A32NX_KCCU_<SIDE>_KBD_ON_OFF` and `A32NX_KCCU_<SIDE>_CCD_ON_OFF` (lines 42-56).

### 12.2 MFD-level dispatch (`MFD.tsx:231-283`) [FBW]

For an H-event whose name starts with `A32NX_KCCU_L` (CAPT) or `A32NX_KCCU_R` (FO):

1. `key = name.substring(13)`, the text after `A32NX_KCCU_L_`.
2. Publish `kccuKeyEvent = [side, key]` on the shared bus. Every InputField consumes it (section 12.3).
3. Handle the page keys and `CLRINFO` (section 10.4). `KBD`, `REWIND`, `FORWARD`, arrows, `MAILBOX` and `ND` do nothing
   at MFD level.

**Interaction mode** (`MFD.tsx:223-229`): KBD switch ON sets `Kccu` mode, OFF sets `Touchscreen`.

* In Touchscreen mode a focused field asks the sim to route the **PC keyboard** to it (`FOCUS_INPUT_FIELD`).
* In Kccu mode it does not.
* KCCU key events are processed in **both** modes.

**[QUIRK]:** InputFields do not check `side`. Both MFDs share the bus, so a field focused on the FO MFD also receives
CAPT KCCU keys. **[PORT]:** route KCCU keys only to the same side.

### 12.3 InputField key handling [FBW]

This applies only while the field is focused (`InputField.tsx:593-645` for KCCU, `225-271` for the PC keyboard). Every
key first removes the "value selected" highlight.

| Input | Effect |
|---|---|
| KCCU single character `[A-Za-z0-9]` (`A`..`Z`, `0`..`9`) | `handleKeyInput(ch)` |
| KCCU `SP` / `SLASH` / `DOT` | `handleKeyInput(' ' / '/' / '.')` |
| KCCU `PLUSMINUS` | Toggle the sign of the edit buffer: a leading `+` becomes `-`, a leading `-` becomes `+`, otherwise prepend `-` (a null buffer becomes `-`). It does **not** check the length limit and does not call `onInput`. |
| KCCU `BACKSPACE`; PC Backspace (keydown, keyCode 8) | `handleBackspace()` |
| KCCU `ENT`; PC Enter (keyCode 13) | `handleEnter()`: blur with validation |
| KCCU `ESC` or `ESC2` | **[FBW]** buffer = `format(currentValue).text`, then `handleEnter()`. **[QUIRK]** this re-submits the current value: an empty field re-parses its placeholder (`---` gives FORMAT ERROR in most numeric formats), and a computed value becomes pilot-entered. **[PORT]** ESC cancels: leave edit mode with no validation, no `onModified`, and restore the display. |
| KCCU arrows | ignored ("unsupported atm") |
| PC keypress | `key = String.fromCharCode(keyCode).toUpperCase()`. Accept `/^[A-Z0-9/.+\- ]$/`; `,` maps to `.`. No ESC handling on the PC keyboard (**[PORT]** map PC Escape to ESC). |

`handleKeyInput(ch)` (lines 275-295):

1. If the buffer is null (not yet editing): buffer = `''`, and left-align the text area.
2. Let `L = maxDigits`. If the field can overflow, `L = maxDigits + maxOverflowDigits`. If `freeText`, `L = 24`
   (`MAX_CHARACTERS_FREE_TEXT`, line 89). Append `ch` only if `len < L`. Show the caret.
3. Run `onInput`: if the field can overflow and `len == maxDigits`, enter overflow mode (section 13.4.6). Call
   `props.onInput(buffer)` (DropdownMenu uses it for filtering).

`handleBackspace()` (lines 237-247):

* buffer null and `canBeCleared` gives buffer `''` (the field shows empty and ENT will clear it);
* buffer `''` does nothing;
* otherwise drop the last character.
* **[QUIRK]:** with `canBeCleared == false` and a null buffer, the expression `null?.slice() ?? ''` also gives `''`, so
  the field can be cleared anyway. **[PORT]:** with `canBeCleared == false`, ignore backspace while the buffer is null.

### 12.4 Focus lifecycle across navigation

`navigateTo` triggers `UNFOCUS_INPUT_FIELD` and destroys the page. **[PORT]:** the focused field is dropped **without**
validation, keyboard capture is released, and the caret stops. This matches the aircraft: pressing a page key abandons
an unfinished entry. KCCU page keys are processed even while a field is focused.

### 12.5 X-Plane mapping [PORT]

* Create one command per key and side: `a380x/xp12/kccu/captain/<key>` and `a380x/xp12/kccu/first_officer/<key>`. The
  key is the KCCU name in lower case: `a`..`z`, `0`..`9`, `dot`, `plusminus`, `ent`, `backspace`, `sp`, `slash`, `esc`,
  `esc2`, `up`, `down`, `left`, `right`, `dir`, `perf`, `init`, `navaid`, `mailbox`, `fpln`, `dest`, `secindex`,
  `surv`, `atccom`, `nd`, `kbd`, `clrinfo`, `rewind`, `forward`. Act on `xplm_CommandBegin`. Cockpit manipulators bind
  to these commands.
* Datarefs: `a380x/xp12/kccu/<side>/kbd_on` and `.../ccd_on` (int, writable). Also `a380x/xp12/mfd/<side>/active_page`
  (byte[64], the full URI), which replaces `L:A380X_MFD_*_ACTIVE_PAGE`.
* PC keyboard: while a field (or a free-text dropdown) is focused on a side and the user preference "capture keyboard
  for MFD" is on, capture keystrokes. Use the avionics `keyboardCallback` for a popped-out device, or a key sniffer for
  the 3D cockpit. Map:
  * `A`-`Z` (uppercased) and `0`-`9` to characters;
  * space, `/`, `.`, `,` (as `.`), `+`, `-` to characters;
  * Backspace, Enter (and keypad Enter) and Escape to the matching keys.

  Release capture on blur.
* The core API takes abstract events, so the core does not depend on XPLM:

```c
typedef enum { MFD_CAPT = 0, MFD_FO = 1 } MfdSide;
typedef enum {
  MFD_KEY_CHAR,          /* ch in 'A'..'Z', '0'..'9', ' ', '/', '.', '+', '-' */
  MFD_KEY_PLUSMINUS, MFD_KEY_ENT, MFD_KEY_BACKSPACE, MFD_KEY_ESC, MFD_KEY_ESC2,
  MFD_KEY_UP, MFD_KEY_DOWN, MFD_KEY_LEFT, MFD_KEY_RIGHT,
  MFD_KEY_DIR, MFD_KEY_PERF, MFD_KEY_INIT, MFD_KEY_NAVAID, MFD_KEY_MAILBOX, MFD_KEY_FPLN,
  MFD_KEY_DEST, MFD_KEY_SECINDEX, MFD_KEY_SURV, MFD_KEY_ATCCOM, MFD_KEY_ND,
  MFD_KEY_KBD, MFD_KEY_CLRINFO, MFD_KEY_REWIND, MFD_KEY_FORWARD
} MfdKey;
void mfd_key(MfdCore *c, MfdSide s, MfdKey k, char ch);
void mfd_pointer_move(MfdCore *c, MfdSide s, float x, float y);   /* canvas px, y down */
void mfd_pointer_button(MfdCore *c, MfdSide s, int down);          /* primary button */
void mfd_pointer_wheel(MfdCore *c, MfdSide s, int clicks);         /* +up */
void mfd_pointer_leave(MfdCore *c, MfdSide s);
```

The X-Plane avionics screen callbacks use bottom-left, y-up device pixels (`XPLMDisplay.h:300-301`). Convert with
`y_canvas = 1023 - y`.

---

## 13. Widgets

All geometry is in canvas px. "Raised" and "sunken" refer to the bevel primitive in section 13.1.

### 13.1 Bevel border primitive (CSS `outset` / `inset`)

* **Raised (outset)**, width w, colour c: top and left edges in `c`; bottom and right edges in `dark(c)`.
* **Sunken (inset)**: top and left in `dark(c)`; bottom and right in `c`.
* `dark(c)` follows Chromium's `Color::Dark`: `v = max(r,g,b)/255`, `m = max(0, (v-0.33)/v)`, each channel times `m`.
  * `dark(#c4c6cf) = #74757b`
  * `dark(#00ffff) = #00abab`
  * `transparent` stays transparent.
* Corners are mitred diagonally where the two colours meet: top-right and bottom-left. With w = 2, filling the top and
  bottom edges as full-width rectangles and the side edges between them is acceptable.
* A "solid" border is the colour `c` on all sides.

The aircraft MFD shows raised buttons and sunken entry fields, so implement the two-tone bevel even though Coherent's
own rendering of `outset` cannot be checked here.

### 13.2 Button (`UiWidgets/Button.tsx`) [FBW]

**Props:** `label` (string, or rich content including multi-line); `menuItems?` (list of
`{label, disabled?, action}`); `showArrow` (default true); `disabled`; `visible`; `selected`; `highlighted`;
`buttonStyle`; `containerStyle`; `onClick`; `scrollToMenuItem?`; `dropdownMenuRightAligned`.

**Geometry** (`widget-style.scss:79-90`): `.mfd-button` is display flex, centred, padding `9 12 5 12`, 2 px raised
`#c4c6cf`, fill `#272525`, SZ 22 white, text centred.

| Case | Outer size |
|---|---|
| One line, auto width | `(textW + 28) x 40` |
| One line, `width: W` in buttonStyle | `(W + 28) x 40` |
| n lines | height `18 + 22 n` |

* Text: line top = y + 11; baseline = **y + 28.6** (+22 per extra line); each line centred on the content box.
* Inside a block (non-flex) parent, the button stretches to the parent width. Page specs give explicit widths.
* Common sizes: `RETURN` 110.8 x 40; `CLOSE` 97 x 40; `CANCEL` 110.8 x 40; `CONFIRM *` 146.1 x 40 (padding-right 6);
  footer button 128 x 62.

**States** (class precedence as in CSS):

| State | Fill | Border | Text |
|---|---|---|---|
| normal | `#272525` | raised `#c4c6cf` | white |
| hover | `#272525` | raised **cyan** | white |
| disabled | `#272525` | raised `#c4c6cf` (no hover change) | `#868a9c`; arrow `#808080` |
| selected (segmented control) | `#868a9c` | **sunken** `#c4c6cf`, hover sunken cyan | white |
| opened (its menu is open) | `#868a9c` | sunken `#c4c6cf`, hover cyan | white |
| highlighted | **white** | raised `#c4c6cf` (cyan on hover) | `#272525` |
| `visible = false` | not drawn, not hit-testable (layout space kept) | | |

`buttonStyle` may override the text colour: `color: yellow` for TMPY F-PLN.

**Click:** if not disabled, call `onClick()`. If it has menu items and is not disabled, also toggle the menu
(`Button.tsx:70-74, 254-258`).

**Menu button layout** (`MfdFmsFpln.scss:214-233`): the label slot is flex-grow, centred, padding-left 5 and
padding-right 10. After it comes a 15x15 arrow, vertically centred: `TriangleDown` `(0,0)(15,0)(7.5,15)`, or
`TriangleUp` `(7.5,0)(15,15)(0,15)` when the menu opens upward. The arrow is white, or `grey #808080` when disabled.
Button padding-right is 5 instead of 12, so auto width = `textW + 51`.

**[QUIRK]:** the `padding-right: 5px` is appended after `buttonStyle` in the style string. When `buttonStyle` is
undefined the string starts with `undefined`, which invalidates the declaration, so FBW keeps 12 px.
**[PORT]:** always use 5 px for menu buttons.

**Menu list** (`.mfd-dropdown-menu`, `widget-style.scss:182-223`):

* Absolute, directly under the button: `top = button bottom`, `left = container left`. With `dropdownMenuRightAligned`
  it is right-aligned to the container instead.
* Width: `max(container width, longest item)`, plus 2x2 border. The minimum content width is 100 % of the container.
  The longest item counts as `textW(20 px) + 2x16 padding + 2x3 border`.
* Fill `#272525`; 2 px raised `#c4c6cf`; z 60.
* **Rows:** 36 px each (3 px transparent border, padding 5/16, 20 px line). Text SZ 20 white, left-aligned,
  baseline-left (row.x + 19, row.y + 24) relative to the row's outer box (19 = 3 border + 16 padding). No wrapping;
  long text is clipped. Disabled rows: text `#868a9c`, not clickable (the menu stays open).
* **Max height** 275 px content (279 outer), which shows 7 full rows plus 23 px of the 8th. Above that the list
  scrolls with a **20 px** scrollbar on the right inside the border: track `#272525`, thumb `#c4c6cf`, no arrow buttons.
  The thumb length is `275 * 275 / contentH`.
* **Opening upward:** when the items change, FBW measures the list. If `top + height > 1024`, it sets
  `top = -height` (the list sits directly above the button) and shows the up arrow (`Button.tsx:191-207`).
* `scrollToMenuItem`: when the menu opens, or the index changes while open, set `scrollTop = index * 36`.
* **Item click:** if enabled, call `action()` and close the menu. Clicking outside closes it (section 11.3).

### 13.3 IconButton and shapes (`IconButton.tsx`, `shapes.tsx`) [FBW]

**Container** `.mfd-icon-button`: flex, centred, fill `#272525`, 2 px raised `#c4c6cf` (hover raised cyan). The size
comes from `containerStyle` per page (for example `width:42px; height:42px; padding:6px`, or `50x50`).

**Icons**: 35x35 px (local units scaled to 35 px). Fill white; `grey #808080` when disabled. A disabled icon button
ignores clicks but keeps the hover border (**[QUIRK]**; **[PORT]** no hover).

| Icon | Local viewBox -> polygons |
|---|---|
| `single-up` | 15x15: (7.5,1) (15,14) (0,14) |
| `single-down` | 15x15: (0,1) (15,1) (7.5,14) |
| `single-left` / `single-right` | `single-down` rotated +90 deg / +270 deg about (7.5, 7.5) |
| `double-up` | 35x35: (0,17.5) (17.5,0) (35,17.5) and (0,35) (17.5,17.5) (35,35) |
| `double-down` / `double-left` / `double-right` | `double-up` rotated 180 / 270 / 90 deg about (17.5, 17.5) |
| `flat-single-up` | 35x35: (0,25) (17.5,7.5) (35,25) |
| `flat-single-down` / `-left` / `-right` | rotated 180 / 270 / 90 deg |
| `ecl-single-up` | 35x35 outline only, 3 px stroke: (3,32) (17.5,3) (32,32) |
| `ecl-single-down` | `ecl-single-up` rotated 180 deg |
| `ecl-check` | check-mark path (ECL only, not used by the MFD) |

`TriangleDown` and `TriangleUp` (shapes.tsx) are 15x15: `(0,0)(15,0)(7.5,15)` and `(7.5,0)(15,15)(0,15)`, default
fill white.

### 13.4 InputField (`UiWidgets/InputField.tsx`) [FBW]

The central data-entry widget. Value formatting and parsing are delegated to a **DataEntryFormat**
(`pages/common/DataEntryFormats.tsx:34-47`; the per-format catalogue is in `02-data-formats-messages.md`).

#### 13.4.1 DataEntryFormat contract

```
placeholder        : string            e.g. "---", "--:--", ".--"
maxDigits          : int               nominal characters; sets the minimum text width
maxOverflowDigits? : int               extra characters allowed (enables overflow mode)
unit?              : string            used for the overflow width
format(v|null)     -> (text|null, leadingUnit|null, trailingUnit|null)
parse(string)      -> v|null           or raises FmsError{type, details?}
reFormatTrigger?   : signal            re-run format() (dependency changed)
unitFamily?        : signal            unit system changed (kg/lb, m/ft)
```

`parse('')` normally returns null (clear). Example: `DropdownFieldFormat(n)` has placeholder `'-' * n`, `maxDigits =
n`, `format(v) = v || placeholder`, and `parse(s) = (s == '' or s == placeholder) ? null : s`
(`DataEntryFormats.tsx:1168-1191`).

#### 13.4.2 Props

| Prop | Default | Meaning |
|---|---|---|
| `value` (mutable) or `readonlyValue` | (required) | the displayed value |
| `mandatory` | false | an empty value shows amber `▯` placeholders |
| `inactive` | false | static green text, no box, not focusable |
| `disabled` | false | grey box, not focusable |
| `canBeCleared` | true | whether backspace may clear the field |
| `enteredByPilot` | true | false = FMS-computed, shown in the small font |
| `freeText` | false | allow up to 24 characters, with overflow |
| `onModified(v)` | (none) | called on a successful commit instead of `value.set` |
| `onInput(s)` | (none) | called on every keystroke |
| `dataHandlerDuringValidation(v, old)` | (none) | async validator; returning `false` (or throwing) rejects the entry |
| `errorHandler(err)` | (required) | normally `fmc.showFmsErrorMessage(err.type, err.details)` |
| `handleFocusBlurExternally` | false | the parent drives focus and blur (DropdownMenu) |
| `containerStyle` | (none) | page-specific width and justification |
| `alignText` | **`center`** | value alignment in the text area. The code sets `flex-end` after the subscribable is already created, so the effective default is centre (lines 122-125 vs 468-470). |
| `tmpyActive` | false | yellow text |
| `bigUnit` | false | units at 24 px instead of 20 |
| `overrideEmptyMandatoryPlaceholder` | `▯` | replacement character for `-` |
| `class` | (none) | extra class on the root (`yellow-when-disabled`: disabled text shows yellow) |

#### 13.4.3 Geometry

Root and container are flex rows. The container (`widget-style.scss:510-520`) has 2 px sunken `#c4c6cf` border,
padding `4 2`, content height 37, and is black.

| Item | Value |
|---|---|
| Outer height | **49** |
| Content box | `x+4 .. x+w-4`, `y+6 .. y+43` |
| Horizontal layout | `[leading unit][6 px][text area][6 px][trailing unit]`. The 6 px gaps (`.mfd-unit-leading` margin-right, `.mfd-unit-trailing` margin-left) exist even when the unit is empty. |
| Text area | `min-width = round(maxDigits * 27 / 1.5) = 18 * maxDigits` px; it grows if `containerStyle` fixes a wider container |
| Auto outer width | `8 + Wlead + 6 + max(18*maxDigits, textW) + 6 + Wtrail`. Example: SpeedKnots (3 digits, trailing `KT` at 20 px): `8 + 0 + 6 + 54 + 6 + 25.1 = 99.1` |
| Value text | 27 px box (line-height 27) centred in the content, at `y+11 .. y+38`; baseline **y + 32.6** (computed 22 px font: baseline y + 31.1) |
| Units | SZ 20 (or 24) `#0c66f7`, vertically centred: 20 px at `y+14.5 .. y+34.5`, baseline **y + 30.5** |
| Alignment, idle and focused-unmodified | per `alignText` within the text area (default centre) |
| Alignment while typing | left (`flex-start`) |

#### 13.4.4 Display states and colour precedence

Text colour precedence follows the CSS declaration order (`widget-style.scss:546-598`). `inactive` uses `!important`
and always wins.

| State | Container | Text colour | Text background | Font |
|---|---|---|---|---|
| idle, value present | sunken `#c4c6cf`, black | cyan | none | 27 (22 if `enteredByPilot=false`) |
| idle, empty, not mandatory | same | cyan placeholder (`---`, units still shown) | | 27 |
| idle, empty, **mandatory** | same | **amber**; every `-` in the placeholder becomes `▯` (`---` shows `▯▯▯`, `--:--` shows `▯▯:▯▯`) | | 27 |
| `tmpyActive` | same | yellow | | |
| hover (enabled, not inactive) | **sunken cyan** | (unchanged) | | |
| **inactive** | no border, black | **green** (`!important`) | | 27/22 |
| **disabled** | sunken `#c4c6cf` (no hover change), fill **`#272525`** | `#868a9c` (yellow with `yellow-when-disabled`); the mandatory look is removed | `#272525` | |
| **focused, unmodified** ("value selected") | as hover or idle | **black** | **cyan** (yellow if tmpy) behind the text box: `textW x 27` | 27 |
| **editing** | as hover or idle | cyan (yellow if tmpy) | none | 27 + caret |
| **validating** | as idle | light grey `#c4c6cf` | none | 27 |
| **overflow** (section 13.4.6) | **1 px solid `#868a9c`**, black | (as editing) | | |

On focus the `mandatory` class is removed, but an empty mandatory field still shows `▯▯▯`. In the focused state that
appears as black `▯` on cyan.

#### 13.4.5 Caret

The caret (`widget-style.scss:600-617`) is an 18x27 block in cyan, placed immediately after the typed text, at the text
box top (`y+11`).

* It is visible only in the **editing** state, after the first typed character. Pure focus shows no caret.
* Blink: 1 s period, `step-start`. Phase 0-0.5 s is black (invisible). Phase 0.5-1 s is the cyan block. Run the timer
  from the edit start.
* When the buffer is at its length limit, the text shows the first `L-1` characters and the **last character is drawn
  inside the caret block**: black on cyan, and cyan on black in the opposite blink phase (`InputField.tsx:171-183`).

#### 13.4.6 Overflow mode

Overflow mode applies to fields with `freeText` or `maxOverflowDigits` (`InputField.tsx:198-223`).

* **Enter:** when the typed length reaches `maxDigits`, or when an idle value is longer than `maxDigits`.
* **Box:** the container becomes `position:absolute; top:-18px; z:5` with a 1 px `#868a9c` border.
  * Content width = `18 * (maxOverflowDigits + maxDigits + len(unit) + 1)`. Example: FlightLevel
    `18*(2+3+2+1) = 144`, outer 150.
  * For `freeText`: `18*24 = 432`, outer 438.
  * The box keeps its left edge and extends right, over the following widgets.
* **Exit:** after a commit (blur with validation), or when a new value fits.
* **[PORT]:** draw the expanded box in an overlay pass (z 5), keeping the field's top-left corner (FBW's `-18px` is an
  artefact of the collapsed flex root). Hit-testing uses the expanded box while it is shown.

#### 13.4.7 Edit state machine

```
IDLE --click on area/units (enabled && !inactive && !validating)--> FOCUSED
      Touchscreen mode: request PC keyboard capture. Start a 20 s watchdog.
FOCUSED (buffer = null, value highlighted)
   char         -> EDITING (buffer = char, left-align, caret on)
   BACKSPACE    -> EDITING (buffer = "") if canBeCleared        [PORT: else ignore]
   PLUSMINUS    -> EDITING (buffer = "-")
   ENT / mouse-down outside -> VALIDATING(input = value? format(value).text : "")
   ESC / ESC2   -> [FBW] same as ENT with input = format(value).text
                   [PORT] -> IDLE, no commit
   navigation   -> [PORT] IDLE, no commit
EDITING
   char         -> append if len < L (section 12.3)
   BACKSPACE    -> drop last char (no-op on "")
   PLUSMINUS    -> toggle leading sign
   ENT / mouse-down outside -> VALIDATING(input = buffer)
   ESC          -> [FBW] buffer = format(value).text, then ENT   [PORT] -> IDLE, no commit
VALIDATING(input)   (text drawn light grey; focus is blocked)
   v = format.parse(input)
     raises FmsError -> errorHandler(err); display old value; -> IDLE (immediately)
   ok -> wait for max(500 ms, dataHandlerDuringValidation(v, old))
         handler false or throws -> IDLE (old value shown; the handler emits its own message)
         else onModified(v) (it may raise FmsError -> errorHandler) or value.set(v)
   -> IDLE: buffer = null; leave overflow mode; restore the mandatory look if the value is still null;
      restore alignment
```

* Re-entering the unchanged value (click, then click outside or ENT) **does** re-submit
  `parse(format(value).text)`. That is the FBW design ("Enter is pressed after no modification", lines 347-350).
* Clearing works by BACKSPACE to empty, then ENT: `parse('')` returns null, the value becomes null, and the format
  decides what that means (usually "revert to computed or default").
* **20 s watchdog [FBW]:** if the field is still focused after 20 s, only the sim keyboard capture is released; the
  field stays focused. **[PORT]:** release the PC keyboard capture after 20 s without a keystroke and keep the field in
  edit mode, so KCCU input still works.
* While FOCUSED, EDITING or VALIDATING, external value changes do not update the display (`onNewValue`, lines
  131-156). After validation the current value is shown.
* Character limits: see section 12.3 (L = `maxDigits`, `+ maxOverflowDigits`, or 24 for free text). Accepted
  characters: `A-Z 0-9 space / . + -`.

### 13.5 DropdownMenu (`UiWidgets/DropdownMenu.tsx`) [FBW]

A selector field with an embedded InputField, optional free-text entry, and a list.

**Props:** `values` (string list, live); `selectedIndex`; `freeTextAllowed`; `idPrefix`;
`onModified(index, freeText)` (index -1 = free text); `inactive`; `disabled`; `containerStyle`; `alignLabels`
(default `center`); `numberOfDigitsForInputField` (default 6); `tmpyActive`.

**Geometry** (`widget-style.scss:137-176`):

* Outer box `.mfd-dropdown-outer`: container width x **44**, fill `#272525`, 2 px raised `#c4c6cf` (hover raised cyan).
* Inner black field: `x+2 .. x+W-27`, `y+2 .. y+42`.
* Arrow column: 25 px wide at the right, inside the border. Arrow: 15x15 down triangle, centred, white, or `gray` when
  disabled.
* The embedded InputField uses `DropdownFieldFormat(numberOfDigits)` with a transparent 2 px border (so no inner hover
  frame). Its value text is cyan 27 px, baseline y + 30.1, and its x is set by `alignLabels`: flex-start gives
  `x + 12`; centre is centred within the inner field.
* `inactive`: outer fill black, border transparent, arrow hidden, text green. This is the static look.
* `disabled`: grey arrow, no hover; the field shows the disabled state.

**List:** `.mfd-dropdown-menu` (section 13.2 metrics), directly below the outer box (top = y + 44), width
`max(W, items) + 4`.

* Item text alignment: `alignLabels` written as CSS `text-align`. Only `center` is valid; `flex-start`/`flex-end` fall
  back to **left**. The default (centre) centres the items.
* There is no disabled-item support.
* It never opens upward.

**Behaviour:**

| Event | Effect |
|---|---|
| Click on outer (not inactive or disabled) | Toggle open. **Opening** focuses the InputField (current value highlighted, PC keyboard captured in touchscreen mode). **Closing by toggle** blurs it **without** validation. |
| Typing while open | Edits the field. `onInput` filters the list to values that **start with** the typed text; the filtered list maps back to the original indices. |
| Click on a list item (not inactive or disabled) | `onModified(originalIndex, '')`, or set `selectedIndex` directly; close; reset the filter. |
| ENT while open | InputField commit: `onFieldSubmit(text)`. If `freeTextAllowed`, call `onModified(-1, text)`. Close the list. |
| Click outside while open | If the text is `''`, the displayed value becomes null (placeholder dashes). Commit with validation (as ENT). Close. |

The displayed value is `values[selectedIndex]`, or null (placeholder `------`) when the index is null, and follows
`selectedIndex` and `values` changes. `scrollToValue(i)` scrolls the list so item i is at the top when it next opens.

**[QUIRK]:** with `freeTextAllowed=false`, typing then clicking outside leaves the typed text displayed, because
`inputFieldValue` is set to the typed text even though `onModified` is not called. **[PORT]:** for non-free-text
dropdowns, a commit without a list selection reverts the display to `values[selectedIndex]`.

KCCU: keys reach the embedded field through the normal focused-field routing ("TODO add KCCU events" at line 242 means
no list navigation by arrows). **[PORT]:** optional: UP/DOWN move a highlighted row and ENT selects it.

### 13.6 PageSelectorDropdownMenu (`UiWidgets/PageSelectorDropdownMenu.tsx`) [FBW]

**Props:** `label`; `menuItems` (list of `{label, action, disabled?, separatorBelow?}`; `separatorBelow` has no MFD
style); `isActive`; `idPrefix`; `containerStyle`; `labelStyle`; `dropdownMenuStyle`.

**Geometry** (`widget-style.scss:225-268`), for a selector of width W at (x0, 44):

* Outer box `x0..x0+W, 44..98`, fill `#272525`.
* Border 2 px: **top and left `#c4c6cf`, bottom and right `#717070`**. These are four solid edges (not a bevel
  algorithm). On hover all four are cyan.
* Label container: `flex: 8`, fills the inner width minus the triangle slot. The triangle slot is 31 px (8 px padding,
  15 px triangle, 8 px padding) and exists only when there is more than one item.
* Label: Ecam 22 white, centred in the container. Margin 10, padding 3, and a 1 px border: transparent, or **white
  when `isActive`**. Label border box height 30 at y `56..86`; text baseline **77.6**.
  * Two-line variant (`labelStyle` margin-top/bottom 0, padding-top/bottom 2): box y `46..96`, baselines 66.6 and 88.6,
    lines centred.
  * The text wraps at spaces when wider than `container - 28`.
* Triangle (more than one item): `(x0+W-25, 62) (x0+W-10, 62) (x0+W-17.5, 77)`, white.

**Click on outer:**

* More than one item: toggle the menu.
* Exactly one item: run `menuItems[0].action()` immediately.

**Menu:** `.mfd-dropdown-menu` below the selector (y from 98), x from x0, width `max(W, items) + 4`, rows of 36 px,
left-aligned text.

* Enabled item: `action()`, then close.
* Disabled item: nothing happens and the menu stays open.
* Outside click closes it.

The class `opened` toggles with the menu but has no style.

### 13.7 TopTabNavigator (`UiWidgets/TopTabNavigator.tsx`) [FBW]

**Props:** `pageTitles`; `selectedPageIndex`; `selectedTabTextColor` (default `white`); `tabBarHeight` h (default
**36**); `tabBarSlantedEdgeAngle` a (default **30 deg**); `additionalRightSpace` S (default 0); `highlightedTab?`;
`pageChangeCallback?`. Children are TopTabNavigatorPage elements, one per title. Only the selected one is displayed
(`display: flex` or `none`).

**Container:** `.mfd-top-tab-navigator-container` with a 2 px margin all round, so inside a page region of width Wp it
spans `x = px+2 .. px+Wp-2` (width `Wc = Wp - 4`) and starts 2 px below the region top.

**Tab bar** (height h):

* N tabs, each `flex: 1`. Every tab except the last has `margin-right: -15px`, so tabs overlap by 15 px.
* `tw = h * tan(a)` (h=36: a=25 deg gives **16.79**; a=30 deg gives **20.78**).
* Tab outer width: `w = (Wc - S + 15*(N-1)) / N`.
* Tab i spans `x_i = xc + i*(w-15) .. x_i + w`.
* If S > 0, a trailing S-px strip ends the bar; it has a 2 px `#d3d3d3` bottom border.

**One tab element** (drawn as three parts):

1. **Left slant** (`x_i .. x_i+tw`, height h): filled triangle `(0,h) (tw,0) (tw,h)` in `#000000` if selected, else
   `#272525`. Slant line `(0,h)-(tw,0)` in `#d3d3d3`, 2 px. If **not** selected, also a bottom line
   `(0,h-1)-(tw,h-1)` in `#d3d3d3`, 2 px.
2. **Label** (`x_i+tw .. x_i+w-tw`): fill `#272525`, or **black when selected**. 2 px `#c4c6cf` top border.
   * Padding-top 6, bottom 2. The label is 34 px tall: the bar row h=36 minus the outer's 2 px bottom border.
   * Text centred. Size `round(h*0.66)` if N <= 2 (h=36: **24**), else `round(h*0.58)` (h=36: **21**).
   * Text colour: `#00ff00` if `highlightedTab == i`, else `selectedTabTextColor` if selected, else white.
     (**[QUIRK]** FBW writes the invalid value `'white;'`, which inherits white.)
   * Text baseline = bar top + 2 + 6 + 0.8*fs (h=36, fs 24: **bar top + 27.2**).
3. **Right slant** (`x_i+w-tw .. x_i+w`): filled triangle `(0,0) (tw,h) (0,h)`, slant line `(0,0)-(tw,h)`, and the
   same bottom line rule.

* **Outer bottom border:** 2 px `#c4c6cf` across the whole tab width at `bar top + 34 .. + 36`, **only if not
  selected**. The selected tab is open towards the content.
* **Draw order:** unselected tabs in index order (z 0), then the selected tab (z 1), so it overlaps both neighbours.
* **Hover:** a 2 px raised cyan bevel around the label rectangle (`x_i+tw .. x_i+w-tw`, bar top .. bar top+34).
* **Click** anywhere on a tab: `pageChangeCallback(i)`, or set `selectedPageIndex = i`. Re-render.

**Content frame** (`.mfd-top-tab-navigator-tab-content`): 2 px raised `#c4c6cf` on left, right and bottom (left light;
right and bottom dark `#74757b`), **no top border**, padding 10, contents clipped. Its top edge is formed by the tabs'
bottom borders.

**Worked example, DATA/STATUS** (`pages/FMS/DATA/MfdFmsDataStatus.tsx:115-125`): titles `ACFT STATUS | FMS P/N`, a=25 deg,
h=36. The page region is `0..768, 138..950`.

* Container: `x 2..766`, bar `y 140..176`, `tw = 16.79`.
* `w = (764 + 15)/2 = 389.5`:
  * Tab 0 `ACFT STATUS`: `x 2..391.5`, label `18.8..374.7`.
  * Tab 1 `FMS P/N`: `x 376.5..766`, label `393.3..749.2`.
* Font 24. Baselines at y 167.2: `ACFT STATUS` (165.5 wide) centred at x 196.8, so it starts at 114.0. `FMS P/N`
  (105.3 wide) centred at x 571.3, so it starts at 518.6.
* Tab content frame `x 2..766, y 176..940.5`. It grows by half of the 15 px slack; the other half goes to the spacer
  below.

### 13.8 RadioButtonGroup (`UiWidgets/RadioButtonGroup.tsx`) [FBW]

**Props:** `values` (labels); `valuesDisabled`; `selectedIndex`; `idPrefix`; `onModified(i)`;
`additionalVerticalSpacing`; `color` (Yellow / Green / Amber / Cyan / White, default **Cyan**).

**One row** (`label.mfd-radio-button`): a flex row with a 2 px transparent border (raised cyan on hover), font 26,
**38 px tall**, spanning the full width of its parent. Rows after the first get `margin-top: 5`, a 43 px pitch. If
`additionalVerticalSpacing = s`, **every** row (including the first) gets `margin-top: s` instead.

| Part | Geometry |
|---|---|
| Circle | outer diameter 34, centred at (row.x + 19, row.y + 19); ring is a 2 px sunken `#c4c6cf` circle (top-left half `#74757b`, bottom-right half `#c4c6cf`); interior black |
| Checked | a filled disc of **diameter 22** in the group colour, centred; a 4 px black ring separates it from the border ring |
| Disabled | the interior (30 px disc) is filled `#868a9c`; when also checked the 22 px disc is grey; text `#868a9c` |
| Text | SZ 26, baseline-left (row.x + 51, row.y + 26.8) |

Text colour: the group colour when checked (cyan `#00ffff`, yellow, green, amber or white); otherwise inherited white.
With `amber` the whole row text is amber.

**Click** anywhere on an enabled row selects it: `onModified(i)`, or `selectedIndex.set(i)`. The checked state follows
`selectedIndex`.

### 13.9 ContextMenu (`UiWidgets/ContextMenu.tsx`) [FBW]

* `display(x, y)` positions the menu's top-left at (x, y) in the page's coordinate space (callers pass canvas-relative
  values) and opens it. `hideMenu()` closes it.
* Box: fill `#272525`, 2 px raised `#c4c6cf`, z 10. Width = `max(textW at 22 px) + 2*2 + 2*3 + 4`.
* Items: 42 px tall (3 px border, padding 7/2, 22 px line). Text SZ 22 white, baseline-left
  (item.x + 5, item.y + 27.6). Disabled items: text `#868a9c`, no hover.
* **Item click** (enabled): hide the menu, then call `onPressed()`.
* **Close:** any click more than **100 ms** after opening (`ContextMenu.tsx:115-119`).
* **[QUIRK]:** `onItemClick` checks `!val.disabled`. A subscribable `disabled` is a truthy object, so such items can
  never be clicked, and the grey class is applied only for the literal `true`. **[PORT]:** evaluate `disabled` as a
  boolean at draw time and at click time.

### 13.10 ConfirmationDialog (`UiWidgets/ConfirmationDialog.tsx`) [FBW]

* Visible when `visible`. `.mfd-dialog` is absolute, z 10, black, 2 px raised `#c4c6cf`, padding 5, margin 5.
  `contentContainerStyle` per page supplies position and size.
* The dialog is a flex column with `space-between`:
  * **Title area:** padding-top 20, children centred. The text is `.mfd-label` 20 px, white, or amber with
    `amberLabel`.
  * **Button row** (`space-between`): `CANCEL` at the left calls `cancelAction()`. `CONFIRM *` at the right
    (padding-right 6) calls `confirmAction()`. The `*` is a literal asterisk.

### 13.11 ConditionalComponent [FBW]

ConditionalComponent shows `componentIfTrue` or `componentIfFalse` according to `condition`. The wrapper is a flex box
that centres its child, optionally with a fixed `width`/`height`; `noStyle` disables the centring.
**[PORT]:** this is a draw-time `if`; no widget is needed.

### 13.12 SURV widgets [FBW]

**SurvButton** (`SurvButton.tsx`, `.mfd-surv-button`): a two-state toggle.

* Box: 4 px **sunken** `#c4c6cf` border, padding 5, content 100x65, so the outer box is **118x83**.
* Two labels in a centred column: `labelTrue` on top, `labelFalse` below.
  * `state` true: upper label green 30 px (`.mfd-value.bigger`), lower label white 20 px.
  * `state` false: upper white 20, lower green 30.
  * Example layout with state true: the label stack is 30 + 20 = 50 px, centred in 65, so the upper baseline is
    y + 40.5 and the lower baseline is y + 62.5.
* **Disabled:** fill `#868a9c`, **no border** (the FBW box shrinks to 110x75). Both labels white 20. The label for the
  inactive state is hidden.
* **Click** (enabled): `onChanged(!state)`. No hover style.
* **[PORT]:** keep the 118x83 footprint when disabled and fill all of it with `#868a9c`.

**AdscButton** (`AdscButton.tsx`, `.mfd-adsc-button`): same box as SurvButton.

* Active upper label: green 30.
* Active lower label: **white 24** (`.mfd-label.bigger`).
* The inactive label is drawn with `.mfd-adsc-label-off`: Ecam 20, `#868a9c`.
* Disabled and click behaviour are as for SurvButton.

**SurvStatusButton** (`SurvStatusButton.tsx`):

* Outer **94x64** (90x60 content, 2 px solid `#c4c6cf` border, cyan on hover), fill `#3c3c3c`.
* Positioned absolutely: `top: -35px` relative to its positioned parent, centred horizontally.
* Upper half: a black block 30 px tall (inset 7 px left and right) holding three horizontal bars, each 76 x 4.29 px,
  spaced evenly: bar tops at +4.28, +12.85 and +21.43 px below the top of the black block (which starts at y + 2).
  Bars are `#c4c6cf`, or **green** when `active`.
* Below the block: label SZ 22 white, centred, baseline about y + 52 (assuming `<p>` margin 0 in Coherent).
* Click calls `onClick`.

**SurvStatusItem** (`SurvStatusItem.tsx`): text `${label} ${active ? sys : 'OFF'}`, SZ 24. Colour: green when
`active`; **amber when `failed`** (amber wins); otherwise white.

---

## 14. Recommended core structure [PORT]

These are recommendations; the architecture spec may rename things.

* **Files:**
  * `a380x_mfd_core.c/.h`: UI state, navigation, widget logic, input dispatch, message-queue view. No GL and no XPLM.
  * `a380x_mfd_widgets.c`: widget layout, hit-test and state machines.
  * `a380x_mfd_render.inc`: GL drawing of the widget display list, using the MFD font atlas.
  * `a380x_mfd_adapter.inc`: X-Plane commands, datarefs, avionics-device mouse/keyboard, DU power.
  * Page modules: `a380x_mfd_page_<name>.c`.
* **Widget model:** retained mode with stable ids. On `enter()` each page declares its widgets once (kind, rect,
  bindings, enabled/visible predicates). Each frame the core:
  1. evaluates the predicates;
  2. lays out any auto-width widgets with the metrics in section 0.4;
  3. hit-tests the pointer for hover;
  4. emits a display list in z order (section 5.2).

  Per-side persistent widget state:

  ```c
  typedef struct {
    uint16_t hovered, pressed, focused;      /* widget ids, 0 = none */
    uint16_t open_menu;                      /* Button / DropdownMenu / PageSelector id */
    float    menu_scroll_px;
    char     edit_buf[25]; int8_t editing;   /* -1 none, 0 selected, 1 typing */
    double   edit_t0, validate_until;        /* caret phase, 500 ms validation */
    uint8_t  overflow;
    double   cursor_last_move;               /* 5 s auto-hide */
    float    cursor_x, cursor_y;
  } MfdUiState;
  ```

* **Validation timing:** commits are asynchronous only to show the 500 ms grey "validating" state. Run the parse
  immediately, show the result after 500 ms (or when the page's validator returns), then apply it. Page validators that
  hit the nav database may complete later; keep the field grey until they do.
* **Timers:**

| Timer | Duration |
|---|---|
| Caret blink | 1 Hz |
| Validation | at least 500 ms |
| NOT FOUND auto-back | 500 ms (stub pages: none) |
| Cursor hide | 5 s |
| Context-menu click guard | 100 ms |
| MSG LIST refresh | 1 Hz |
| FmsPage flight-plan poll | 500 ms (or event driven) |
| PC keyboard capture watchdog | 20 s |

---

## 15. FBW quirks not to replicate (summary)

| # | Quirk | Port behaviour | Source |
|---|---|---|---|
| 1 | Focused fields receive KCCU keys from both sides | filter by side | `InputField.tsx:593` |
| 2 | Outside-click listener spans both MFDs | per side | `Button.tsx:213` and others |
| 3 | ESC re-submits the formatted current value | cancel edit | `InputField.tsx:636-640` |
| 4 | `canBeCleared=false` not enforced on backspace | ignore backspace while unmodified | `InputField.tsx:237-247` |
| 5 | PLUSMINUS ignores the length limit and `onInput` | apply the length limit, call `onInput` | `InputField.tsx:621-630` |
| 6 | Non-free-text dropdown keeps typed text after an outside click | revert to the selected value | `DropdownMenu.tsx:95-109` |
| 7 | `REPORT<br /> & NOTIFY` literal and wording | two lines, `REPORT` / `& MODIFY` | `AtccomHeader.tsx:64` |
| 8 | Duplicate-names dialog and DONE reset always on CAPT | requesting side / both | `instrument.tsx:139-141` |
| 9 | MSG LIST stays open across navigation | close on navigate | `MFD.tsx:302-360` |
| 10 | NOT AVAIL overlay covers 6 px of the title bar | start at y 138 | `MfdFmsPageNotAvail.tsx:53` |
| 11 | Menu-button padding-right lost without `buttonStyle` | always 5 px | `Button.tsx:289` |
| 12 | ContextMenu items with subscribable `disabled` never clickable | evaluate a boolean | `ContextMenu.tsx:108-113` |
| 13 | Disabled menu rows and disabled IconButtons still highlight on hover | no hover when disabled | `widget-style.scss:217-223, 133-135` |
| 14 | 404 page plus auto-back for FCU BKUP and unimplemented ATCCOM pages | stub pages | `MfdNotFound.tsx:16` |
| 15 | TMPY/PENALTY rules compare full URIs | compare `sys/category/page` | `FmsPage.tsx:60-98` |
| 16 | Navigation stack unbounded | cap at 32 | `MfdUiService.tsx:44` |
| 17 | KCCU page keys act while the DU is off | ignore input unless the DU is On | `MFD.tsx:231` |
| 18 | MSG LIST shows only 4 entries | keep 4 visible; add the scroll arrows of the real page later | `MfdMsgList.tsx:51` |

---

## Open questions

1. **ATCCOM selector wording.** FBW shows `REPORT & NOTIFY`, but the routes are `report-modify/*`. Recommend
   `REPORT & MODIFY` (aircraft wording). This needs confirmation against A380 FCOM imagery.
2. **System-selector value colour.** FBW draws `FMS 1` in cyan 27 px because it reuses the InputField. Confirm against
   aircraft imagery whether the header value should be white.
3. **Bevel rendering.** Coherent's actual rendering of CSS `outset`/`inset` could not be checked. This spec prescribes
   the Chromium two-tone algorithm, which matches the raised and sunken look of the aircraft.
4. **FCU BKUP pages** (AFS CP, EFIS CP) are absent in FBW. Decide whether a later spec designs them from aircraft
   documentation, or whether the stub stays.
5. **KBD, MAILBOX, ND, REWIND and FORWARD keys** have no MFD function in FBW. Multi-display cursor routing (MFD, ND,
   SD mailbox) is out of scope here.
6. **PC keyboard capture policy** in the 3D cockpit: a user preference versus following the KBD switch as FBW does.
   This needs an adapter-level decision.
7. **Real-aircraft cursor timeout and colours.** FBW uses yellow, a 90 deg rotation for FO, and a 5 s hide. These are
   kept as-is pending documentation.
