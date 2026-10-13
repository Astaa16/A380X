# MFD spec 03: FMS INIT, FUEL&LOAD, DATA, SEC INDEX, MSG LIST and fallback pages

Scope: the per-page specification for these MFD pages:

| § | Page | FBW URI(s) | FBW component |
|---|---|---|---|
| 3 | ACTIVE/INIT, SECn/INIT | `fms/active/init`, `fms/sec{1,2,3}/init` | `pages/FMS/MfdFmsInit.tsx` |
| 4 | ACTIVE/FUEL&LOAD, SECn/FUEL&LOAD | `fms/active/fuel-load`, `fms/sec{1,2,3}/fuel-load` | `pages/FMS/MfdFmsFuelLoad.tsx` |
| 5 | DATA/STATUS | `fms/data/status[/acft-status\|/fms-pn]` | `pages/FMS/DATA/MfdFmsDataStatus.tsx` |
| 6 | DATA/AIRPORT | `fms/data/airport[/database\|/pilot-stored]` | `pages/FMS/DATA/MfdFmsDataAirport.tsx` |
| 7 | DATA/DEBUG (unrouted) | none | `pages/FMS/DATA/MfdFmsDataDebug.tsx` |
| 8 | SEC INDEX | `fms/sec/index[/1\|/2\|/3]` | `pages/FMS/SEC/MfdFmsSecIndex.tsx` |
| 9 | MESSAGE LIST overlay | none (overlay) | `pages/FMS/MfdMsgList.tsx` |
| 10 | FMS PAGE NOT AVAIL overlay | none (overlay) | `pages/FMS/MfdFmsPageNotAvail.tsx` |
| 11 | ERROR 404 NOT FOUND | any unmapped URI | `pages/FMS/MfdNotFound.tsx` |

Unless stated otherwise, paths are relative to
`upstream-aircraft/fbw-a380x/src/systems/instruments/src/` and the source is pinned at FBW `2baa2b35` (GPL-3.0).
Citations use `file:line`.

Every behaviour is labelled with one of these tags:

- **[FBW]**: implemented by FlyByWire at the pinned commit. Port it as written.
- **[FBW-PH]**: present in FBW only as a placeholder or not available: disabled, a constant, a TODO, or a known bug.
- **[PORT]**: recommended behaviour for the X-Plane 12 native-C port. This applies where FBW is a placeholder or buggy, or where the simulator data source differs.

The shell is owned by the shell and widget spec in this folder: header, title bar, footer, KCCU routing, widget state machines and the font atlas. This document restates only the constants and widget facts its pages need. If the two disagree, the shell and widget spec takes precedence for shared widgets, and this document takes precedence for page content.

The FMGC flight-plan library (`@fmgc/*`, mapped by `fbw-a380x/src/systems/tsconfig.json:41` to `fbw-a32nx/src/systems/fmgc/src`) is **not present** in this sparse checkout. Behaviour inside `FlightPlanService` and `FlightPlanPerformanceData` (`newCityPair`, `secondaryCopyFromActive`, perf-data defaults) is therefore described at the interface level, from the call sites. Items that could not be verified are marked *(unverified: fmgc not in checkout)*.

---

## 0. Conventions used by every page in this document

### 0.1 Coordinate system and layout model

- The canvas is **768 × 1024 px**, matching the FBW `.mfd-main` element (`MFD/pages/common/style.scss:20-28`). The origin is the **top-left corner, with x increasing to the right and y increasing downwards**, the same as CSS. All boxes are absolute canvas coordinates written `[x0, y0 → x1, y1]`, with x1 and y1 exclusive. Fractional values are kept where CSS math produces them; the renderer may round to the nearest integer.
- The C renderer has no layout engine. The positions below were computed from the FBW CSS under two rules, which are the only rules consistent with FBW's own hard-coded sizes:
  1. **`box-sizing: border-box` everywhere.** Any CSS `width` or `height` includes padding and border.
  2. **`line-height: normal` = 1.00 × font-size.** The FBW EIS font has typo ascender 3276 and typo descender −820 at 4096 upem (`USE_TYPO_METRICS` is set), so ascender plus descender is exactly 1 em.

  Evidence: with these rules, the DATA/STATUS ACFT STATUS tab content sums to exactly **735 px**, which is the height FBW hard-codes at `MfdFmsDataStatus.tsx:125`. Header plus footer sums to exactly **172 px**, which is 1024 minus the **852 px** overlay height FBW hard-codes in `F-PLN/MfdFmsFpln.scss:268`.
- **Text placement.** A text box of font size `fs` is `fs` px tall. The **baseline is at `y_top + 0.80·fs`**. Capitals and digits rise about `0.766·fs` above the baseline, which matches the port atlas `a380x_ewd_font.h` (glyph height 49 at size 64).
- **Character advance.** The font is monospaced at **0.625·fs** per character, matching atlas advance 40 at size 64. For example, 20 px gives 12.5 px per character, 24 px gives 15, 25 px gives 15.625 and 30 px gives 18.75. A string's width is its character count × 0.625·fs. Non-breaking spaces count as characters.
- **Font.** MFD text uses `FBW-Display-EIS-A380-SlashedZero` (`style.scss:25`). The port atlas `a380x_ewd_font.*` is built from the non-slashed `FBW-Display-EIS-A380.ttf`. **[PORT]** Generate an MFD atlas from `upstream-aircraft/fbw-a380x/src/base/flybywire-aircraft-a380-842/html_ui/Fonts/fbw-a380x/FBW-Display-EIS-A380-SlashedZero.ttf`, which has the same metrics. The header callsign and the page-selector labels use font "Ecam" (non-slashed); everything on these pages is slashed-zero.

### 0.2 Frame constants (from the shell; restated here)

| Region | Box | Derivation |
|---|---|---|
| Header row 1 (system selector + callsign) | `[0,0 → 768,44]` | dropdown inner 40 + 2×2 border |
| Header row 2 (ACTIVE / POSITION / SEC INDEX / DATA) | `[0,44 → 768,98]` | label 22 + pad 6 + border 2 + margin 20 + outer border 4 |
| Title bar | `[0,98 → 768,134]` | 30 px text + 3+3 padding |
| **Page content** (`CT`=134 … `CB`=950) | `[0,134 → 768,950]` | 816 px tall |
| Footer (MSG LIST / CLEAR INFO + message area) | `[0,950 → 768,1024]` | border 2 + pad 5 + 2-line button 62 + pad 5 |

If the shell spec changes `CT`, shift every y in this document by the same delta.

### 0.3 Colours (`MsfsAvionicsCommon/definitions.scss:9-22` plus inline values)

| Token | Hex | Used for |
|---|---|---|
| BG | `#000000` | page background, input fields, active tab |
| WHITE | `#ffffff` | labels (`.mfd-label`), button text, tab text |
| GREEN | `#00ff00` | values (`.mfd-value`), INIT-type idents in ACTIVE |
| CYAN | `#00ffff` | input-field text, hover/KCCU-cursor border |
| AMBER | `#e68000` | mandatory placeholders, EO tag, DEST EFOB below min, SWAP ACTIVE text, NOT AVAIL text, amber message background |
| YELLOW | `#ffff00` | TMPY tag, TMPY input text |
| LIGHT_GREY | `#c4c6cf` | borders, separators, button outset border |
| DARK_GREY | `#868a9c` | disabled text, title-bar background, opened/selected button background |
| DARKER_GREY | `#272525` | button face, disabled input face, inactive tab face |
| UNIT_BLUE | `#0c66f7` (`rgb(12,102,247)`) | units (`.mfd-label-unit`) |
| TITLE_TEXT | `#222222` | title-bar text |
| SVG_LIGHTGREY | `#d3d3d3` | CSS keyword `lightgrey`, used by tab slant lines and the DATA/STATUS ACTIVE box border |

### 0.4 Typography classes (`MFD/pages/common/widget-style.scss`)

| Class | Size | Colour |
|---|---|---|
| `.mfd-label` | 20 | WHITE (`.green` → GREEN, `.amber` → AMBER) |
| `.mfd-label.bigger` | 24 | as above |
| `.mfd-label.biggest` | 28 | as above |
| `.mfd-label-unit` | 20 | UNIT_BLUE; leading unit has 6 px right margin, trailing unit has 6 px left margin |
| `.mfd-value` | 25 | GREEN (`.sec` → WHITE 25, `.amber` → AMBER, `.cyan` → CYAN) |
| `.mfd-value.bigger` | 30 | GREEN |
| Input text (pilot-entered or editing) | 27 | CYAN |
| Input text (computed, `enteredByPilot=false`) | **22** | CYAN |
| Input text (empty and mandatory) | 27 | AMBER, each placeholder `-` replaced by `▯` (U+25AF) |
| Input text (disabled) | 27 or 22 | DARK_GREY on DARKER_GREY face |
| Input text (validating, 500 ms) | same | LIGHT_GREY |
| `.mfd-button` text | 22 | WHITE (disabled → DARK_GREY) |
| Dropdown or button-menu item | 20 | WHITE (disabled item → DARK_GREY) |
| Title bar text | 30 | TITLE_TEXT |
| Top tab label | 24 (2 tabs) / 21 (3 tabs) | WHITE |

### 0.5 Widget geometry cheat-sheet (border-box)

| Widget | Size rule | Visual |
|---|---|---|
| **InputField** (`UiWidgets/InputField.tsx`) | height **37**. Width = CSS width if given, otherwise 8 (2 px padding + 2 px border, each side) + 6 (leading-unit margin) + leading-unit text + `maxDigits×18` + 6 + trailing-unit text | face BG, 2 px inset LIGHT_GREY border (disabled: face DARKER_GREY). Text is aligned by `alignText`; the default is **center** (`InputField.tsx:122-125`) |
| **Button** (`UiWidgets/Button.tsx`) | height = 22·lines + 14 (9 px top + 5 px bottom padding) + 4 (border), so **40** for 1 line and **62** for 2 lines, unless CSS height is set. Width = CSS width, or text width + 28 | face DARKER_GREY, 2 px outset LIGHT_GREY. Text box top = button top + 11. Opened or selected: face DARK_GREY, inset border |
| **Button with menu** | as Button. If `showArrow`, a 15×15 triangle sits right of the label and the right padding is 5 | menu opens below (or above if it would pass y=1024); items 36 px tall, 20 px text |
| **DropdownMenu** (`UiWidgets/DropdownMenu.tsx`) | 44 tall (2 px border + 40 inner). Arrow cell 25 wide on the right with a 15×15 down-triangle (white, or grey when disabled) | face DARKER_GREY around a BG inner; the selected value is drawn as InputField text (27 px CYAN) |
| **TopTabNavigator** (`UiWidgets/TopTabNavigator.tsx`) | sits at `x 2…766` (2 px margin). Tab bar is 36 tall. Each tab has a slant triangle of width 36·tan25° = **16.79 px** on both sides, and tabs overlap by 15 px (`margin-right:-15`). Tab width `w = (764 + 15(n−1))/n`, so n=2 gives 389.5 and n=3 gives 264.67. Tab *i* starts at `x = 2 + i·(w−15)` | active tab face BG with no bottom border. Inactive face DARKER_GREY with a 2 px LIGHT_GREY bottom line. 2 px top border LIGHT_GREY. Slant lines 2 px SVG_LIGHTGREY |
| Tab content | below the bar, with 2 px LIGHT_GREY border on left, right and bottom (none on top) and 10 px padding, so **inner x 14 … 754** and inner y = bar bottom + 10 | `overflow:hidden` |
| **IconButton** (`UiWidgets/IconButton.tsx`) | box from the container style (50×50 or 60×60). Double-arrow glyph is white, grey when disabled | face DARKER_GREY, 2 px outset LIGHT_GREY |
| **ConfirmationDialog** (`UiWidgets/ConfirmationDialog.tsx`) | box from the content style. 5 px margin and padding, 2 px outset border. Title 20 px white, 20 px from the top. Bottom row: `CANCEL` left, `CONFIRM *` right | face BG, z above the page |

Input-field interaction (summary; the full state machine is in the widget spec):

- Click or KCCU focus selects the field: CYAN background, BG text.
- Typing replaces the value. `ENT` validates. `ESC` restores. `BACKSPACE` on an unmodified field clears it, only if `canBeCleared`.
- Validation runs `format.parse()`, then the page's `dataHandlerDuringValidation`, with an artificial **500 ms** wait (`InputField.tsx:399`). The text is LIGHT_GREY while it waits.
- A parse error produces an FMS message (§0.7) and the field reverts.
- A handler that returns `false` or throws reverts the field silently.

### 0.6 Number formats shared by these pages

| Name | Rule |
|---|---|
| `WEIGHT` | Internal value is kg. Display `(unit==KG ? kg : kg×2.20462262)/1000` with 1 decimal. NaN or null shows `---.-`. The unit label is `T` when metric, `KLB` otherwise (`MfdFmsFuelLoad.tsx:59-66,212`) |
| `PCT1` | `value.toFixed(1)`; null shows `--.-` |
| `HHMM_COLON` | `HH:MM` (zero-padded); null or NaN shows `--:--` (`shared/utils.ts:77`) |
| `ETA` (`getEtaFromUtcOrPresent(s, fromPresent)`, `shared/utils.ts:9-20`) | if `fromPresent`: time span `s` as `HH:MM`, otherwise UTC clock of `zulu_time + s` as `HH:MM`; null or NaN shows `--:--` |
| `HHMM` (`DateTimeFormatters.milisecondsTohhmm`) | `HHMM` without a colon, from a UTC epoch in ms |

Metric versus imperial: FBW reads EFB setting `CONFIG_USING_METRIC_UNIT` (`MfdFmsFuelLoad.tsx:59`, `MfdFmsDataAirport.tsx:41`). **[PORT]** Use a port setting `a380x/xp12/config/metric_units` (int, default **1** = kg and metres), shared with the EFB.

### 0.7 FMS message plumbing used by input errors

`errorHandler(e)` calls `fmc.showFmsErrorMessage(e.type, e.details)` (`FMC/FlightManagementComputer.ts:1094-1111`). It queues a Type I, white message:

| `FmsErrorType` | First line | Second line (`details`) |
|---|---|---|
| EntryOutOfRange | `ENTRY OUT OF RANGE` | e.g. `RNG: 270.0 TO 373.0 T` |
| FormatError | `FORMAT ERROR` | e.g. `FORMAT: XXX.X T` |
| NotInDatabase | `NOT IN DATABASE` | n/a |
| NotYetImplemented | `NOT YET IMPLEMENTED` | n/a |

The message text is `first + "\n" + details` (`FlightManagementComputer.ts:1147`). The detail strings are built at `MFD/pages/common/DataEntryFormats.tsx:15-31`: `FORMAT: {fmt}{ unit}` and `RNG: {from} TO {to}{ unit}`. The queue and footer model are in §9.2.

### 0.8 Data entry formats used on these pages (`MFD/pages/common/DataEntryFormats.tsx`)

| Format | maxDigits | Placeholder | Lead / trail unit | Parse rules | Range | Errors (details) |
|---|---|---|---|---|---|---|
| `LongAlphanumericFormat` (1215) | 10 | `----------` | none | any text; `""` or placeholder gives null | n/a | none |
| `AirportFormat` (1099) | 4 | `----` | none | length must be 4 | n/a | FORMAT `FORMAT: AAAA` |
| `FlightLevelFormat(min,max)` (300) | 3 (+2 overflow) | `---` | `FL` / none | `350` or `FL350` (regex `^FL(\d{1,3})$`); display 3-digit zero-padded | 0…430 on INIT | OUT OF RANGE (no details); FORMAT `FORMAT: FL XXX` |
| `CrzTempFormat` (671) | 3 | `---` | none / `°C` | **a positive number entered without `+` is negated** (`45` gives −45); display `+5` or `-45` | −99…+99 | `RNG: -99 TO 99 C`, `FORMAT: +/-XXX C` |
| `TropoFormat` (415) | 5 | `-----` | none / `FT` | if 3 or fewer characters, value×100 (`360` gives 36000) | 1000…60000 ft | OUT OF RANGE (no details); `FORMAT: FOR ALT XXXXX FOR FL XXX` |
| `CostIndexFormat` (931) | 3 | `---` | none | integer | 0…999 | `RNG: 0 TO 999`, `FORMAT: XXX` |
| `TripWindFormat` (790) | 5 | `-----` | `TL` or `HD` / none | prefix `HD`, `H` or `-` means headwind (negative); `TL`, `T` or `+` or none means tailwind; display `abs` padded to 3 digits | −250…+250 kt | `RNG: -250 TO 250 KT`, `FORMAT: +/-XXX` |
| `WeightFormat(min,max,unit)` (505) | 5 | `---.-` | none / `T` or `KLB` | input in t or klb is converted to kg | per field (kg) | `RNG: a TO b T\|KLB` (shown in the display unit), `FORMAT: XXX.X T\|KLB` |
| `PercentageFormat(min,max)` (566) | 4 | `--.-` | none / `%` | number | per field | `RNG: a TO b %`, `FORMAT: XX.X %` |
| `TimeHHMMFormat` (1272) | 4 | `--:--` | none | `HHMM`, `HH:MM` or `MM`; the value is minutes. Display `HH:MM`, but a value of **0 shows the placeholder** | 0…90 min (HH≤23, MM≤59) | OUT OF RANGE (no details); `FORMAT: HHMM` |
| `PaxNbrFormat` (1236) | 3 | `---` | none | integer | 0…999 | `RNG: 0 TO 999`, `FORMAT: XXX` |
| `FuelPenaltyPercentFormat` (1939) | 6 | `+000.0` | none / `%` | regex `^[+]?\d{1,3}(?:\.\d)?$`; display `'+' + v.toFixed(1).padStart(5,'0')` | 0.0…999.9 | `FORMAT: +NNN.N %`, `RNG: 0.0 TO 999.9 %` (DATA/STATUS drops the details, see §5) |
| `DropdownFieldFormat(n)` (1168) | n | `-`×n | none | free text | n/a | none |

`WeightFormat` limits in kg come from `fbw-a380x/src/systems/shared/src/PerformanceConstants.ts:26-42`:

| Constant | Value | Unit |
|---|---|---|
| `minZfw` | 270 000 | kg |
| `maxZfw` | 373 000 | kg |
| `minZfwCg` | 23 | % |
| `maxZfwCg` | 48 | % |
| `maxBlockFuel` | 289 982 | kg |
| `maxTaxiFuel` | 9 889 | kg |
| `maxRteRsvFuelPerc` | 15 | % |
| `maxAltnFuel` | 289 982 | kg |
| `maxFinalFuel` | 100 018 | kg |
| `maxMinDestFuel` | 250 000 | kg |
| `maxJtsnGw` | 512 000 | kg |
| `maxCertifiedAlt` | 43 000 | ft |
| `defaultTropopauseAlt` | 36 090 | ft |

The RTE RSV weight limits come from airline data `AirlineModifiableInformation.EK.rsvMin` = 0 kg and `rsvMax` = 50 000 kg (`fbw-a380x/src/systems/shared/src/AirlineModifiableInformation.tsx:7-9`).

---

## 1. Common FMS page base (`pages/common/FmsPage.tsx`)

All FMS pages in this document except MSG LIST, NotAvail and NotFound derive from `FmsPage`.

### 1.1 Title bar text [FBW]

The title is `category.toUpperCase() + "/" + pageTitle` (`FmsPage.tsx:120-124`):

| Page | Title |
|---|---|
| ACTIVE/INIT | `ACTIVE/INIT` |
| SEC1 INIT | `SEC1/INIT` |
| FUEL&LOAD | `ACTIVE/FUEL&LOAD` |
| DATA/STATUS | `DATA/STATUS` |
| DATA/AIRPORT | `DATA/AIRPORT` |
| SEC INDEX | `SEC/SEC INDEX` |

The page titles come from `MfdPageDirectory.tsx`: INIT is `"INIT"`, FUEL&LOAD is `"FUEL&LOAD"`, STATUS is `"STATUS"`, AIRPORT is `"AIRPORT"` and SEC INDEX is `"SEC INDEX"`. The title is drawn at x = 7 (left padding), text box y 101…131, 30 px, TITLE_TEXT on DARK_GREY.

**[PORT]** Render SEC INDEX as `SEC INDEX`, as on the aircraft, rather than FBW's `SEC/SEC INDEX`.

### 1.2 Title-bar tags [FBW]

These are 70 px (EO) and 100 px (TMPY) DARK_GREY cells on the right of the title bar, separated by 2 px black gaps on FMS pages (`ActivePageTitleBar.tsx:58-80`, `style.scss:45-75`).

| Tag | Condition | Pages |
|---|---|---|
| `EO` (AMBER background, 30 px text #222222) | `fmgc.data.engineOut` | all FMS pages |
| `TMPY` (YELLOW background) | a temporary plan is loaded on this page | URI page is `init`, `fuel-load`, `perf`, `f-pln*`, or URI equals `fms/sec/index`, fix-info or dir-to (`FmsPage.tsx:59-76`) |
| `PENALTY` (WHITE background, right end of the title cell) | `fuelPenaltyPercentage > 0` (`FMC/fmgc.ts:155-157`) | URI equals `fms/data/status`, or URI page is `fuel-load`, `f-pln` or `f-pln-hold` (`FmsPage.tsx:90-98`) |

**[FBW-PH]** SEC INDEX never shows TMPY. The URI is normally `fms/sec/index/N`, not `fms/sec/index`, and the default branch of `onFlightPlanChanged` never sets `tmpyActive`.

**[PORT]** Show TMPY on SEC INDEX whenever a temporary plan exists.

### 1.3 Loaded plan selection [FBW] (`FmsPage.tsx:188-275`)

`FlightPlanIndex` values: `Active=0`, `Temporary=1`, `Uplink=2`, `FirstSecondary=3`. SEC n is plan index `n+2`; the arithmetic at `MfdFmsInit.tsx:719` and `MfdFmsSecIndex.tsx:265` relies on this.

| URI category | Loaded plan | `loadedFlightPlanIndex` | Flags |
|---|---|---|---|
| `active` | TMPY if one exists, else ACTIVE. On INIT with no plan: none | 1 if TMPY exists, else 0 | `tmpyActive = hasTmpy`, `secActive = false` |
| `secN` | SECn if it exists. If not: INIT keeps no plan (`loadedFlightPlan = null`). Other pages, when triggered by a flight-plan event, navigate to the same URI with `active` | N+2 | `secActive = true` |
| other (`data`, `sec`, `position`) | ACTIVE or TMPY | unchanged (0) | n/a |

Refresh happens on flight-plan sync events (create, delete, deleteAll, swap, copy) and on a **500 ms** poll of `loadedFlightPlan.version` (`FmsPage.tsx:176-186`). Each refresh calls the page's `onNewData()`.

### 1.4 Side effect present on every FMS page: CHECK T.O. DATA [FBW] (`FmsPage.tsx:279-306`)

Runs on every refresh. If the active plan has an origin runway and `fmgc.data.vSpeedsForRunway` differs from it:

1. Store the old V1, VR and V2 into `v1ToBeConfirmed`, `vrToBeConfirmed` and `v2ToBeConfirmed`.
2. Clear V1, VR and V2.
3. Queue the Type II amber message `CHECK T.O. DATA`, resolved when VR is re-entered.

If `vSpeedsForRunway` was empty, it is simply set. Every refresh also calls `acInterface.updateFmsData()`, which publishes origin, departure runway, destination, landing runway and alternate, and sets `atcCallsign` from the active flight number. That callsign is shown in header row 1. **[PORT]** Keep this logic in the FMS core, not in the page.

### 1.5 Global FMS state referenced below

| Datum | Source in FBW | Meaning |
|---|---|---|
| `activeFlightPhase` | `L:A32NX_FMGC_FLIGHT_PHASE` | enum `Preflight=0, Takeoff, Climb, Cruise, Descent, Approach, GoAround, Done=7` (`shared/src/flightphase.ts:4-13`) |
| `enginesWereStarted` | `FlightManagementComputer.ts:1474-1481` | phase ≥ Takeoff, **or** phase = Preflight and any engine N2 > 20 %. Updated only while an active plan exists |
| `hasActiveFlightPlan` | `FlightManagementComputer.ts:967-971` | active plan has origin **and** destination |
| `isActiveOrCopiedFromActive(plan)` | fmgc *(unverified)* | true for ACTIVE or TMPY, or a SEC created by IMPORT ACTIVE |
| `engineOut` | `FmcAircraftInterface.ts:1726-1734` | airborne and not all engines running |
| Weight unit | §0.6 | kg or lb |

Units in FBW perf data: **weights are tonnes** (`zeroFuelWeight`, `blockFuel`, `taxiFuel` and so on), except `takeoffWeight`, which is **kg** (`FlightManagementComputer.ts:574-604`). Pages multiply or divide by 1000. **[PORT]** Store all weights in kg in the C core, and convert only in the formatters.

---

## 2. Effective-value rule for pilot and computed data

Most fields in this document show one perf datum resolved like this:

```
effective(X) = pilotX ?? calculatedX ?? defaultX
enteredByPilot(X) = (pilotX != null)  // 27 px text if true, 22 px if false
```

Writing a `pilotX` value of null reverts the field to the computed or default value, shown in small font. The defaults below are airline data *(the defaults' application inside fmgc is unverified; values from `AirlineModifiableInformation.tsx:2-17`)*:

| Default | Value |
|---|---|
| taxi fuel | 1 500 kg |
| route reserve | 5 % of trip |
| final holding time (fuel prediction) | 30 min |
| IDLE factor | 0.0 |
| PERF factor | 0.0 |

---

## 3. ACTIVE/INIT and SECn/INIT (`pages/FMS/MfdFmsInit.tsx`, `MfdFmsInit.scss`)

The INIT page is the preflight entry page. The page renders nothing until `fmcService.master` exists (`MfdFmsInit.tsx:380`).

### 3.1 Layout [FBW, computed]

All label cells on the left (`.mfd-label.init-input-field`) are 150 wide with 5 px right padding. Their **text is right-aligned to x = 145**, 20 px WHITE. Rows are flex rows with centred items.

| # | Element | Box `[x0,y0 → x1,y1]` | Text and format | Widget |
|---|---|---|---|---|
| L1 | row 1 (margin-top 5) | `[0,139 → 768,201]` | n/a | n/a |
| | label | text y 160 | `FLT NBR` | label |
| | FLT NBR field | `[150,151.5 → 350,188.5]` | LongAlphanumeric, centred | InputField |
| | ACFT STATUS | `[355,150 → 555,190]` | `ACFT STATUS` | Button 200 |
| | CPNY F-PLN button | `[593,139 → 768,201]` | 2 lines: `CPNY F-PLN`/`REQUEST`, or `REQUEST`/`PENDING...`, or `RECEIVED`/`CPNY F-PLN` | Button 175 with menu, no arrow |
| L2 | row 2 (margin 10/10) | `[0,211 → 768,248]` | n/a | n/a |
| | label | text y 219.5 | `FROM` | label |
| | FROM field | `[150,211 → 242,248]` | Airport, centred | InputField (auto width 92) |
| | label | `[252,219.5 → 277,239.5]` | `TO` | label (10 px margins) |
| | TO field | `[287,211 → 379,248]` | Airport | InputField 92 |
| | label | `[389,219.5 → 439,239.5]` | `ALTN` | label |
| | ALTN field | `[449,211 → 541,248]` | Airport | InputField 92 |
| L3 | row 3 (mt 5) | `[0,263 → 768,303]` | n/a | n/a |
| | label | text y 273 | `CPNY RTE` | label |
| | CPNY RTE field | `[150,264.5 → 350,301.5]` | LongAlphanumeric | InputField 200 |
| | RTE SEL | `[355,263 → 555,303]` | `RTE SEL` | Button 200 |
| L4 | row 4 (mt 5, pb 25) | `[0,308 → 768,374]` | n/a | n/a |
| | label | text y 318 | `ALTN RTE` | label |
| | ALTN RTE field | `[150,309.5 → 350,346.5]` | LongAlphanumeric | InputField 200 |
| | ALTN RTE SEL | `[355,308 → 555,348]` | `ALTN RTE SEL` | Button 200 |
| | separator | 1 px LIGHT_GREY line `[0,373 → 768,374]` | n/a | n/a |
| L5 | row 5 (mt 25+5) | `[0,404 → 768,441]` | n/a | n/a |
| | label | text y 412.5 | `CRZ FL` | label |
| | CRZ FL field | `[150,404 → 249,441]` (root box 150…350) | leading unit `FL` (UNIT_BLUE) at x 154; 3 digits centred | InputField auto 99 |
| | label | `[350,412.5 → 450,432.5]` (right pad 5) | `CRZ TEMP` | label |
| | CRZ TEMP field | `[455,404 → 565,441]` | value centred, trailing `°C` | InputField 110 |
| L6 | row 6 (mt 10) | `[0,451 → 768,495]` | n/a | n/a |
| | label | text y 463 | `MODE` | label |
| | MODE dropdown | `[150,451 → 325,495]` | `LRC`/`ECON` or `EO-LRC`/`EO-ECON`, centred | DropdownMenu 175 |
| | label | `[390,463 → 452.5,483]` | `TROPO` | label |
| | TROPO field | `[457.5,454.5 → 592.5,491.5]` | right-aligned digits, trailing `FT` | InputField auto 135 |
| L7 | row 7 (mt 5, pb 35) | `[0,500 → 768,628]` | n/a | n/a |
| | label | `[0,515 → 145,535]` right-aligned | `CI` | label |
| | label | `[0,562 → 145,582]` right-aligned | `TRIP WIND` | label |
| | CI field | `[150,505 → 220,542]` | CostIndex, centred | InputField 70 |
| | TRIP WIND field | `[150,552 → 275,589]` | leading `TL`/`HD` + 3 digits, centred | InputField 125 |
| | WIND | `[355,552 → 438,592]` | `WIND` | Button (auto 83) |
| | CPNY WIND REQUEST | `[583,515 → 758,577]` | `CPNY WIND`/`REQUEST` | Button 175 (2 lines) |
| | separator | 1 px LIGHT_GREY `[0,627 → 768,628]` | n/a | n/a |
| B1 | IRS | `[150,643 → 310,683]` | `IRS` | Button 160 |
| B2 | DEPARTURE | `[150,693 → 310,733]` | `DEPARTURE` | Button 160 |
| B2 | RTE SUMMARY | `[360,693 → 539.25,733]` | `RTE SUMMARY` | Button auto |
| B3 | NAVAIDS | `[150,743 → 310,783]` | `NAVAIDS` | Button 160 |
| B4 | FUEL&LOAD | `[150,793 → 310,833]` | `FUEL&LOAD` | Button 160 |
| B5 | T.O. PERF | `[150,843 → 310,883]` | `T.O. PERF` | Button 160 |
| B5 | CPNY T.O. REQUEST | `[583,843 → 758,905]` | `CPNY T.O.`/`REQUEST` | Button 175 (2 lines) |
| S1 | RETURN (SEC only) | `[0,905 → 150,945]` | `RETURN` | Button 150 |

Notes:

- The lower button block B1–B5 has `visibility: hidden` when the loaded plan is not ACTIVE or TMPY. It still occupies its space, which is why RETURN sits at y 905 (`MfdFmsInit.tsx:658-723`).
- **[FBW-PH]** The labels `CPNY WIND<br />REQUEST` and `CPNY T.O.<br />REQUEST` are passed as plain strings (`MfdFmsInit.tsx:653,709`), so FSComponent renders the literal `<br />`. **[PORT]** Draw two lines as shown in the table.
- The CPNY F-PLN labels contain `\n`, but the button wraps them at word boundaries in its 175 px width, which gives the same two lines.

### 3.2 Field bindings, formats, and enable/mandatory logic [FBW]

Abbreviations:

- `idx` = `loadedFlightPlanIndex`
- `PD` = the loaded plan's performance data
- `ACT` = (idx ∈ {Active, Temporary}), which is `mandatoryAndActiveFpln`
- `FROM`/`TO` = the page-local ICAO subjects
- `TMPY` = tmpyActive
- `PH` = flight phase

| Field | Shows (readonly source) | On valid entry (handler) | Disabled when | Mandatory when | Clearable |
|---|---|---|---|---|---|
| FLT NBR | `plan(idx).flightNumber` | `plan(idx).flightNumber := v` (`:391`). On ACTIVE only, the page then also connects ATSU (`routerConnect`), calls `fmc.updateFlightNumber(v)` (ARINC words + `ATC FLIGHT NUMBER` simvar) and `updateFmsData()` (header callsign) (`:173-187`) | no plan exists (`noFlightPlan`) | ACT | **no** |
| FROM | `PD`-independent: origin ident, kept in a page subject | `FROM := v`; then if FROM and TO are both set and differ from the plan's: `newCityPair(FROM, TO, ALTN or undefined, idx)`; for ACTIVE also `updateFmsData()` and `calculateFinalAndAlternateFuel()` (`:312-335`) | `(PH > Preflight AND plan isActiveOrCopiedFromActive) OR TMPY` | ACT | no |
| TO | destination ident | same as FROM | same | ACT | no |
| ALTN | `altn dest ident`; `NONE` if FROM and TO exist but no alternate; empty otherwise (`:265-269`) | `NONE` → `setAlternate(undefined, idx)` (delete); otherwise `setAlternate(v, idx)`; then `updateFmsData()` | `TMPY OR !TO OR !FROM` | ACT | yes |
| CPNY RTE | `NONE` once FROM and TO exist | **[FBW-PH]** none | always | `(!TO OR !FROM) AND ACT` (no visual effect while disabled) | no |
| ALTN RTE | `NONE` once FROM and TO exist | **[FBW-PH]** none | always | no | no |
| CRZ FL | `PD.cruiseFlightLevel` | `trySetCruiseFl(v, idx)` (§3.4); a null or 0 entry is rejected | `TMPY OR !TO OR !FROM` | `PH < Descent AND ACT` | no |
| CRZ TEMP | `PD.cruiseTemperature` (pilot entry, else ISA at CRZ FL); `enteredByPilot = PD.isCruiseTemperaturePilotEntered` | `cruiseTemperaturePilotEntry := v` | CRZ FL is null | no | yes |
| MODE | `PD.costIndexMode` (`LRC=0`, `ECON=1`); labels become `EO-LRC`/`EO-ECON` when engine-out | `PD.costIndexMode := index` | `!TO OR !FROM OR (PH ≥ Descent AND plan isActiveOrCopiedFromActive)` | n/a | n/a |
| TROPO | `PD.tropopause` (pilot, else default 36 090 ft); small font unless pilot-entered | `pilotTropopause := v` (null reverts) | no plan | no | yes |
| CI | `PD.costIndex` | `costIndex := v` | MODE disabled **OR** MODE = LRC | ACT | yes |
| TRIP WIND | `PD.pilotTripWind` | `pilotTripWind := v` (kt, + = tail) | `!TO OR !FROM` | no | yes |

Automatic CRZ TEMP default [FBW] (`MfdFmsInit.tsx:276-285`): on each refresh, if CRZ FL is set and (CRZ TEMP is null, or CRZ TEMP − ISA(CRZ FL×100) > 0.5), set `cruiseTemperatureIsaTemp := ISA(CRZ FL×100)`. The ISA formula is `T = 15 − 0.0019812·min(alt_ft, 36089)` °C (`shared/src/OperatingAltitudes.tsx:29-31`). For example, FL350 gives −54.3 °C, displayed `-54°C` in 22 px.

**[FBW-PH]** The "> 0.5" test means a pilot entry warmer than ISA + 0.5 is *overwritten* with ISA on the next refresh. Only colder entries persist. **[PORT]** Apply the ISA default only when no pilot entry exists.

### 3.3 Buttons and navigation [FBW]

| Button | Enabled | Action |
|---|---|---|
| ACFT STATUS | always | `navigateTo('fms/data/status')` |
| CPNY F-PLN (label per §3.5) | see §3.5 | not available: `cpnyFplnRequest(idx)`; available: open menu `INSERT*` → `insertCpnyFpln(Active)`, `CLEAR*` → delete uplink |
| RTE SEL, ALTN RTE SEL, WIND, CPNY WIND REQUEST, RTE SUMMARY, CPNY T.O. REQUEST | **never** [FBW-PH] | none (RTE SUMMARY would go to `fms/data/route`, which does not exist) |
| IRS | always (ACT only visible) | `fms/position/irs` |
| DEPARTURE | FROM and TO set and PH = Preflight | `fms/{category}/f-pln-departure` |
| NAVAIDS | always | `fms/position/navaids/withReturn` (the NAVAIDS page then shows its own RETURN) |
| FUEL&LOAD | always | `fms/{category}/fuel-load` |
| T.O. PERF | always | `fms/{category}/perf/to` (PERF page, T.O. tab) |
| RETURN (SEC only) | always | `fms/sec/index/{idx−2}` (SEC INDEX on tab N) |

### 3.4 `trySetCruiseFl(fl, idx)` [FBW] (`FlightManagementComputer.ts:1804-1849`)

1. If the plan does not exist, return false.
2. If `fl` is non-finite, show `FORMAT ERROR` and return false.
3. If `fl ≤ 0` or `fl > 430`, show `ENTRY OUT OF RANGE` and return false.
4. If `idx` is ACTIVE and Climb ≤ PH < Done: let `fcuFl = floor(FCU selected altitude/100)`. If `fl < fcuFl`, insert `fcuFl` instead, and if PH ∈ {Cruise, Descent, Approach}, queue `NEW CRZ ALT: {fl×100}` (Type II, white).
5. Write `PD.cruiseFlightLevel`. If it exceeds REC MAX FL, queue `CRZ FL ABOVE MAX FL` (Type II).
   REC MAX is `min(A380AltitudeUtils.calculateRecommendedMaxAltitude(GW, ISA dev), 43000)`; it is null when no GW, and only for ACTIVE or TMPY.
6. If ACTIVE, call `acInterface.onUpdateCruiseLevel(fl)`, which publishes the cruise altitude to the FG and PFD.

### 3.5 CPNY F-PLN REQUEST button (`shared/CpnyFplnButtonUtils.tsx`) [FBW]

Label, from `cpnyFplnAvailable` and `cpnyFplnUplinkInProgress`:

| State | Label |
|---|---|
| uplink in progress | `REQUEST\nPENDING...` |
| not available | `CPNY F-PLN\nREQUEST` |
| available | `RECEIVED\nCPNY F-PLN` |

The button is disabled when `uplinkInProgress OR (planIndex == Active AND (enginesWereStarted OR hasActiveFlightPlan OR (requestedForPlan ≥ FirstSecondary)))`.

Menu items: when available, `INSERT*` and `CLEAR*`; otherwise no menu.

- `INSERT*` calls `uplinkInsert(plan)`. For ACTIVE it also calls `updateFmsData()` and `trySetCruiseFl(OFP cruise alt/100)`. It sets `paxNumber` from the OFP, then clears `available` and `requestedForPlan` (`FlightManagementComputer.ts:850-874`).
- `CLEAR*` calls `uplinkDelete()` and sets `available := false` and `requestedForPlan := null`.

Request flow (`FlightManagementComputer.ts:819-848`): with no Navigraph username and no SimBrief override ID, queue `NO NAVIGRAPH USER`. Otherwise, download the SimBrief OFP and uplink it into the uplink plan.

- `onUplinkInProgress` sets `uplinkInProgress`.
- `onUplinkDone(ok)` sets `available := ok`. Availability queues the Type II message `COMPANY F-PLN RECEIVED\nWAITING FOR INSERTION` (`FlightManagementComputer.ts:390-396`).
- On failure, the message is `RECEIVED COMPANY F-PLN NOT VALID`.

**[FBW-PH]** On SEC INIT, FBW still passes `FlightPlanIndex.Active` to the label, disabled and menu helpers (`MfdFmsInit.tsx:44-52`). Only the click uses `idx`, so INSERT on a SEC INIT page inserts into ACTIVE. **[PORT]** Use `idx` for all four.

**[PORT]** Data source: SimBrief.

- Fetch the OFP XML or JSON on a **background thread** (never in a flight-loop or draw callback), from `https://www.simbrief.com/api/xml.fetcher.php?username=…&json=1`. The username comes from a port config value.
- Parse only origin, destination, alternate, route, cruise altitude, flight number, pax count, ZFW and block fuel.
- If no SimBrief user is configured, queue `NO SIMBRIEF PILOT ID PROVIDED`, the existing FBW fictional message (`shared/NXSystemMessages.ts:150`).
- Until the uplink plan type exists in the port FMS core, keep the button disabled.

### 3.6 Cross-page effects

| INIT entry | Effect elsewhere |
|---|---|
| FROM + TO | `newCityPair` **replaces** the plan at `idx` with a new plan holding origin and destination (FBW comment `MfdFmsInit.tsx:152`: newCityPair deletes the flight plan). F-PLN is rebuilt, and the ND, PFD and header data are refreshed by `updateFmsData`. Then: FUEL&LOAD alternate and final fuel are recomputed; the DEPARTURE button is enabled in Preflight; and on ACTIVE the CPNY F-PLN REQUEST becomes disabled (`hasActiveFlightPlan`). *(newCityPair internals unverified: fmgc not in checkout)* |
| ALTN | Creates or deletes the alternate plan. FUEL&LOAD then gets ALTN fuel 6.5 t (calculated), the ALTN ident, and the ALTN EFOB. With ALTN=NONE, `pilotAlternateFuel` is cleared and ALTN fuel becomes 0 (`FmcAircraftInterface.ts:1767-1777`) |
| CRZ FL | VNAV and predictions, PERF CRZ, F-PLN; FG cruise altitude; CRZ TEMP default |
| CI and MODE | Managed speeds: CLB speed `290(1−d)+330d` with `d=(CI/999)²` (`FMC/fmgc.ts:275-283`); DES `round(288(1−d)+300d)` with `d=CI/999` (`fmgc.ts:395-409`). LRC means CI is disabled (LRC uses CI 0 semantics) |
| TROPO, TRIP WIND, CRZ TEMP | VNAV predictions (`getTropoPause`, `getTripWind`) |
| FLT NBR (ACTIVE) | header callsign, ARINC flight-number words, ATSU connect |

**Automatic CI.** When engines start, or ZFW is entered, while the active plan has a destination and CI is null, the FMS sets CI=0 and MODE=LRC and queues `LRC MODE IN USE` (`FlightManagementComputer.ts:1777-1786`).

### 3.7 Port notes

- **[PORT]** Await the city-pair creation and surface errors. FBW calls `cityPairModified()` without awaiting it (`MfdFmsInit.tsx:428,444`), so an unknown ICAO stays displayed with no message.
  1. Validate FROM, TO and ALTN against the port nav database before creating the plan.
  2. On failure, queue `NOT IN DATABASE` and revert the field.
- **[PORT]** CPNY RTE and RTE SEL could later list X-Plane `.fms` files from `Output/FMS plans/` as company routes. Until then, keep FBW's disabled state and `NONE` text.
- **[PORT]** WIND, the CPNY requests and RTE SUMMARY stay disabled, matching FBW.

---

## 4. ACTIVE/FUEL&LOAD and SECn/FUEL&LOAD (`pages/FMS/MfdFmsFuelLoad.tsx`, `MfdFmsFuelLoad.scss`)

### 4.1 Layout [FBW, computed]

Section A, the top line: three label/value groups (`.mfd-label-value-container`, 7 px padding) spread with `space-between` over x 25…743, in the box `[25,144 → 743,183]`.

| Group | Box (metric) | Label (20 px WHITE) | Value (25 px GREEN, WHITE on SEC) | Unit (20 px UNIT_BLUE) |
|---|---|---|---|---|
| GW | `[25,144 → 175.6,183]`, left-anchored | `GW` at x 32 | x 72 (`---.-`) | x 156.1 `T` or `KLB` |
| CG | centred on x 377.75, `[310.25 → 445.25]` | `CG` at x 317.25 | x 357.25 (`--.-`) | x 425.75 `%` |
| FOB | right-anchored at 743, `[579.9 → 743]` | `FOB` at x 586.9 | x 639.4 | x 723.5 |

Text y: values 151…176, labels and units 153.5…173.5.

**[PORT]** Anchor GW left at 25, CG centred at 377.75 and FOB right at 743. This keeps positions stable when the unit text is `KLB` or a value has 6 characters.

Section B, ZFW and BLOCK:

| Element | Box | Text and format |
|---|---|---|
| label `ZFW` | right edge 175, text y 201.5 | 20 px WHITE |
| ZFW field | `[190,193 → 340,230]` | WEIGHT, right-aligned, trailing unit |
| label `ZFWCG` | right edge 515, text y 201.5 | n/a |
| ZFWCG field | `[530,193 → 655,230]` | PCT1, centred, trailing `%` |
| label `BLOCK` | right edge 175, text y 266 | n/a |
| BLOCK field | `[190,257.5 → 340,294.5]` | WEIGHT, right-aligned |
| FUEL PLANNING button | `[478.1,245 → 629.9,307]` | 2-line `FUEL`/`PLANNING` (centred block) + `*` 10 px right of it; right padding 2 |

The BLOCK row (y 245…312) gets `visibility:hidden` once `enginesWereStarted` (`MfdFmsFuelLoad.tsx:300-308`).

Section C, the middle box: a 1 px LIGHT_GREY line at y 327 and another at y 636, each spanning x 0…768. Content runs y 343…636.

Left grid columns: c1 `0…125` (labels, right edge 110), c2 `125…280`, c3 `280…425`.

| Row (y) | c1 label | c2 | c3 |
|---|---|---|---|
| R1 343…400 | `TAXI` | TAXI field `[125,343 → 275,380]` WEIGHT, right-aligned | none |
| R2 400…459 | `TRIP` | value group right-aligned to 280: value x 176.4…254.5 (25 px), unit x 260.5, y 407 | time `HH:MM` 25 px centred on x 352.5, y 407 |
| R3 459…516 | `RTE RSV` | RTE RSV field `[125,459 → 275,496]` | RTE RSV % field `[285,459 → 405,496]` PCT1, centred |
| R4 516…573 | `ALTN` | ALTN field `[125,516 → 275,553]` | `--:--` 25 px centred on 352.5, y 522 [FBW-PH] |
| R5 573…630 | `FINAL` | FINAL field `[125,573 → 275,610]` | FINAL time field `[285,573 → 405,610]` HH:MM, centred |

Label text y = row top + 8.5 (rows of 57) or + 9.5 (row of 59).

Right grid: label column `403.2…553.2` with right-aligned text ending at **538.2**; value column `553.2…768`.

| Row (y) | Label | Element and box |
|---|---|---|
| 343…390 | `PAX NBR` | PAX NBR field `[553.2,343 → 628.2,380]`, centred |
| 390…444 | `MODE` | MODE dropdown `[553.2,390 → 728.2,434]` |
| 444…491 | `CI` | CI field `[553.2,444 → 628.2,481]`, centred |
| 491…538 | `JTSN GW` (hidden unless ACT) | JTSN GW field `[553.2,491 → 703.2,528]`, always disabled |
| 538…587 | `TOW` | value group centred on x 660.6: value 612.3…690.4, unit 696.4, y 545 |
| 587…636 | `LW` | same layout, y 594 |

Section D, the bottom (y 687…858). The left grid is x 0…480 with 4 auto columns: c1 `0…96.1`, c2 `96.1…202.2`, c3 `202.2…323.3`, c4 `323.3…480`.

| Row | c1 | c2 | c3 | c4 |
|---|---|---|---|---|
| header 687…733 (text y 687, 20 px WHITE, centred; 1 px LIGHT_GREY underline across 0…480 at y 717) | none | none | `TIME` (Preflight) or `UTC` | `EFOB` |
| DEST 733…782 | `DEST` 20 px, y 742.5 | dest ident or `NONE`, 24 px, y 740.5 | ETA 24 px, y 740.5 | EFOB group centred on 401.65: value 25 px at 353.4 (y 740), unit at 437.5 |
| ALTN 782…831 | `ALTN` 20 px, y 796.5 | altn ident or `NONE`, 24 px, y 794.5 | `--:--` 24 px [FBW-PH] | EFOB group, value y 789 |

The ident and ETA text (24 px) is GREEN when ACT and WHITE otherwise. EFOB values are GREEN (WHITE on SEC; AMBER for DEST under the condition in §4.3).

The right block is x 480…768, centred on x 624:

| Element | Box and text |
|---|---|
| `MIN FUEL AT DEST` label | 20 px WHITE, `[524,687 → 724,707]` |
| MIN FUEL field | `[549,727 → 699,764]` WEIGHT, right-aligned |
| `EXTRA` label | `[594,794 → 654,814]` |
| EXTRA value group | value 25 px at x 526.65 (y 826), unit at 610.8 |
| EXTRA time | `HH:MM` 25 px at x 650.25 (y 826) |

The RETURN button `[0,910 → 145,950]` runs `navigateTo('back')`.

### 4.2 Field bindings [FBW]

The page refreshes **at 1 Hz** (`MfdFmsFuelLoad.tsx:220-298`). Each tick:

1. reload perf data;
2. `calculateFinalAndAlternateFuel(idx)`;
3. `calculateTakeoffWeight(idx)`;
4. recompute LW, TOW, RTE RSV, GW, CG, FOB, TRIP, EXTRA, DEST and ALTN.

Definitions: `ACT` as in §3.2. `TR_DIS = PH ≥ Takeoff AND plan isActiveOrCopiedFromActive` (`taxiAndRouteRsvDisabled`).

| Field | Format and range | Shows | On entry (perf-data key ← value) | Disabled | Mandatory | Clearable |
|---|---|---|---|---|---|---|
| ZFW | WEIGHT 270.0…373.0 t | `PD.zeroFuelWeight` | `zeroFuelWeight ← v/1000` (t) | never | ACT | no |
| ZFWCG | PCT 23.0…48.0 | `PD.zeroFuelWeightCenterOfGravity` | `zeroFuelWeightCenterOfGravity ← v` | never | ACT | no |
| BLOCK | WEIGHT 0…289.9 t | `PD.blockFuel` | `blockFuel ← v/1000` | never (row hidden after engine start) | ACT | yes |
| TAXI | WEIGHT 0…9.8 t | `PD.taxiFuel` (null when TR_DIS); small font unless `taxiFuelIsPilotEntered` | `pilotTaxiFuel ← v/1000` | TR_DIS | no | yes |
| RTE RSV (weight) | WEIGHT 0…50.0 t | `getRouteReserveFuel(idx)` (§4.3); small font unless pilot | `pilotRouteReserveFuel ← v/1000`; `pilotRouteReserveFuelPercentage ← null` | TR_DIS | no | yes |
| RTE RSV % | PCT 0.0…15.0 | §4.3 | `pilotRouteReserveFuel ← null`; `pilotRouteReserveFuelPercentage ← v` | TR_DIS | no | yes |
| ALTN fuel | WEIGHT 0…289.9 t | `PD.alternateFuel` (pilot, else calculated) | `pilotAlternateFuel ← v/1000` | plan has no alternate | no | yes |
| FINAL fuel | WEIGHT 0…100.0 t | `PD.finalHoldingFuel` | `pilotFinalHoldingFuel ← v/1000`; `pilotFinalHoldingTime ← null` | never | no | yes |
| FINAL time | HHMM 0…90 min | `PD.finalHoldingTime` | `pilotFinalHoldingFuel ← null`; `pilotFinalHoldingTime ← v` | never | no | yes |
| PAX NBR | 0…999 | `PD.paxNumber` | `paxNumber ← v` (ignored if null) **and** `L:A32NX_FMS_PAX_NUMBER ← v` (air-conditioning airflow, `FmcAircraftInterface.ts:1277-1279`) | never | ACT | yes |
| MODE | `LRC`/`ECON` (`EO-…` when engine-out) | `PD.costIndexMode` | **[FBW-PH]** no `onModified`: selecting only changes the local subject, which the 1 Hz reload overwrites (`MfdFmsFuelLoad.tsx:733-744`). **[PORT]** write `costIndexMode` as INIT does | `(isActiveOrCopied AND PH ≥ Descent) OR no destination` | n/a | n/a |
| CI | 0…999 | `PD.costIndex` | `costIndex ← v` | MODE disabled **or** MODE = LRC | ACT | yes |
| JTSN GW | WEIGHT 0…512 t | always null → `---.-` | none | **always** [FBW-PH] | no | n/a |
| MIN FUEL AT DEST | WEIGHT (min 0)…250.0 t | `PD.minimumDestinationFuelOnBoard`; small font unless pilot | `pilotMinimumDestinationFuelOnBoard ← v/1000` | never | no | yes |

ZFW, ZFWCG and BLOCK are never shown in small font. FBW passes no `enteredByPilot` for them, so they default to pilot style.

### 4.3 Computed values and formulas [FBW]

All values are kg unless noted. `destPred` is the VNAV destination prediction. It exists for the ACTIVE plan only; SEC predictions are a TODO in FBW (`MfdFmsFuelLoad.tsx:290,411`).

| Display | Formula | Source |
|---|---|---|
| GW | `enginesWereStarted ? (FQMS GW if valid, else ZFW + FOB) : NaN` | `MfdFmsFuelLoad.tsx:265-284`, `FMC/fmgc.ts:211-233` |
| CG | `enginesWereStarted ? FQMS CG %MAC : null`; shows `--.-` if null **or 0** | `fmgc.ts:241-244` |
| FOB | `enginesWereStarted ? getFOB()×1000 : NaN`. `getFOB` = FQMS FOB if any engine N2 > 20, else BLOCK | `fmgc.ts:250-260` |
| TRIP fuel | ACTIVE only: `FOB_now − destPred.EFOB` (EFOB converted lb→kg), otherwise null | `FlightManagementComputer.ts:606-616` |
| TRIP time | ACTIVE only: `ETA(destPred.secondsFromPresent, fromPresent=true)` (flight time from now) | `MfdFmsFuelLoad.tsx:289-293` |
| RTE RSV fuel | `pilotRouteReserveFuel ?? TRIP×(PD.routeReserveFuelPercentage/100)`; null if no TRIP | `FlightManagementComputer.ts:649-663` |
| RTE RSV % | pilot % if entered. Otherwise, if pilot RTE RSV fuel is entered: `pilotRteRsv_t×1000/TRIP×100` (null without TRIP). Otherwise `PD.routeReserveFuelPercentage` (default 5) | `MfdFmsFuelLoad.tsx:248-263` |
| Calculated ALTN fuel | **6.5 t** if an alternate exists, else 0 (and pilot ALTN cleared) | `FmcAircraftInterface.ts:1774-1778` [FBW-PH: fixed placeholder] |
| Calculated FINAL | if pilot FINAL fuel: time = fuel_t/0.2 (minutes); else fuel_t = time_min × 0.2, which is a fixed **200 kg/min** holding burn | `FmcAircraftInterface.ts:1779-1789` |
| TOW (`PD.takeoffWeight`, kg) | Frozen once PH ≥ Takeoff on an active-type plan. If the plan is not active-type, or engines are not started: `(ZFW + BLOCK − TAXI)×1000` (null if any is null). If engines are started in Preflight: `GW − TAXI×1000`. At the Takeoff transition the FMS sets TOW := GW | `FlightManagementComputer.ts:578-604, 1255-1256` |
| LW | engines not started: `TOW − TRIP` (null if either is null or 0). In flight (PH ≥ Takeoff) with GW and TRIP: `GW − TRIP`. Engines on in Preflight: `GW − TRIP − TAXI` | `FlightManagementComputer.ts:551-572` |
| DEST ident | dest ident, or `NONE` | `:145` |
| DEST ETA | ACT only: `ETA(destPred.secondsFromPresent, fromPresent = PH==Preflight)`, so the column header is `TIME` in Preflight (elapsed) and `UTC` afterwards. Otherwise `--:--` | `:123-125, 412-419` |
| DEST EFOB | ACTIVE only: `destPred.EFOB` (lb→kg); null otherwise | `fmgc.ts:458-467` |
| DEST EFOB colour | AMBER when `destEfobBelowMinInActive` and idx = Active. The flag sets when `EFOB < MIN DEST FOB` and clears only when `EFOB − MIN > 0.3 t` (hysteresis) | `FmcAircraftInterface.ts:1692-1707` |
| ALTN ident | alternate ident, or `NONE` | `:428` |
| ALTN EFOB | `max(DEST EFOB_t − ALTN fuel_t, LOWEST_FUEL_ESTIMATE_t)` in tonnes | `fmgc.ts:470-478` |
| EXTRA fuel | ACTIVE with `destPred` only. Preflight: `max((engines started ? FOB : BLOCK) − TAXI − TRIP − MIN DEST FOB − RTE RSV, LOWEST)`. Later phases: `max(destPred.EFOB − MIN DEST FOB, LOWEST)` | `FlightManagementComputer.ts:618-642` |
| EXTRA time | never set, so always `--:--` [FBW-PH] | `:161-162` |

`LOWEST_FUEL_ESTIMATE` (`FMC/fmgc.ts:67`) is `A380AircraftConfig.vnavConfig.LOWEST_FUEL_ESTIMATE` in lb, a negative floor *(value unverified: fmgc not in checkout)*. **[PORT]** Use a floor of −99 999 kg, so a shortfall can display as a negative EXTRA.

**[FBW-PH] ALTN EFOB unit bug.** `getAltEFOB` returns tonnes, but the page stores it with unit KILOGRAM (`MfdFmsFuelLoad.tsx:431,435`), so the display shows about `0.0`. **[PORT]** Multiply by 1000.

**[PORT] EXTRA time.** Display `EXTRA_kg / 200 kg·min⁻¹` as `HH:MM`. This uses the same holding burn as FBW's final-fuel model. Show `--:--` when EXTRA ≤ 0 or is null.

**[PORT] ALTN ETA.** Keep `--:--` until the port's alternate predictions exist.

### 4.4 Cross-page effects

| Entry | Effect elsewhere |
|---|---|
| ZFW and ZFWCG | GW fallback before the FQMS is valid; TOW and LW; PERF T.O. (TOW, CG for THS); speed computation (VLS etc. via `acInterface.updateWeights`). Entering both removes `INITIALIZE ZFW / ZFWCG` (`FlightManagementComputer.ts:383-388`). Entering ZFW may trigger the automatic CI=0/LRC (§3.6). On a SEC swap, if ΔZFW > 5 t **and** ΔZFWCG > 0.5 %, queue `CHECK ZFW` (`FlightManagementComputer.ts:950-956`) |
| BLOCK | FOB before engine start, TOW, EXTRA, VNAV initial fuel |
| TAXI, RTE RSV, FINAL, ALTN, MIN DEST FOB | EXTRA, DEST EFOB below-min logic, and the message `DEST EFOB BELOW MIN` (Type II, amber). That message shows in CRZ or DES, or after ALT* in CLB for more than 10 min, or in APPR or GA above 800 ft RA (`FmcAircraftInterface.ts:1709-1722`) |
| PAX NBR | air-conditioning airflow LVar |
| CI and MODE | as INIT |

At Takeoff, the FMS clears `pilotTaxiFuel` and `pilotRouteReserveFuel` and sets `pilotRouteReserveFuelPercentage := 0` (`FlightManagementComputer.ts:1258-1260`).

### 4.5 [PORT] X-Plane data sources for FUEL&LOAD

| Datum | X-Plane source | Notes |
|---|---|---|
| FQMS GW (kg) | `sim/flightmodel/weight/m_total` (float, kg) | Treat as valid while the sim runs. Publish into the port FMS core as "FQMS GW" so the formulas above stay unchanged |
| FQMS FOB (kg) | `sim/flightmodel/weight/m_fuel_total` (float, kg) | Used only when an engine runs (N2 > 20 %) |
| FQMS CG (%MAC) | `sim/flightmodel/misc/cgz_ref_to_default` (m, positive aft) converted with the A380 LEMAC position and MAC length used by the port's weight-and-balance module | Do **not** invent MAC constants in the MFD. If the W&B module is missing, return null and the display shows `--.-` |
| Engine N2 > 20 % | `sim/cockpit2/engine/indicators/N2_percent[0..3]` | Gives `enginesWereStarted` and `isAnEngineOn` |
| PAX NBR output | publish `a380x/xp12/fms/pax_number` (int) | replaces `L:A32NX_FMS_PAX_NUMBER` |
| Pilot entries (ZFW, BLOCK, …) | pilot entry only. **Do not auto-fill from sim weights**, matching FBW and the aircraft | The EFB load page (`native/avionics/a380x_efb.c`) sets sim payload and fuel; the pilot then types the loadsheet values |
| Destination predictions | port VNAV/predictions module | Until it exists: TRIP, DEST EFOB, EXTRA and ETA show `---.-`/`--:--`, exactly as FBW does without `destPred` |

---

## 5. DATA/STATUS (`pages/FMS/DATA/MfdFmsDataStatus.tsx`, `.scss`)

This is the start-up page: both MFDs navigate to `fms/data/status` at instrument start (`MFD/instrument.tsx:144-145`) and after an FMS reset (`FlightManagementComputer.ts:1743`).

### 5.1 Frame

The TopTabNavigator uses `selectedTabTextColor = white`, slant 25° and 2 tabs:

| Tab | Box | Deep link |
|---|---|---|
| `ACFT STATUS` | `[2,136 → 391.5,172]` | URI extra `acft-status` |
| `FMS P/N` | `[376.5,136 → 766,172]` | URI extra `fms-pn` |

Tab content box: `[2,172 → 766,927.5]`. Inner content area: x 14…754, y 182…915.5. Switching tabs hides the SWAP dialog.

### 5.2 ACFT STATUS tab layout [FBW, computed]

Boxes are absolute. Text size and colour are given in parentheses.

| Element | Box | Content |
|---|---|---|
| Airframe line | text `[192.8,202 → 567.8,232]`, centred on x 380.3 | `A380-800 / TRENT 972` (30 GREEN; the `/` has a non-breaking space on each side) |
| *(gap: margin-bottom 200)* | n/a | n/a |
| Factor box | `[14,432 → 669,494]` 1 px LIGHT_GREY border | n/a |
| label | `[25,451 → 85,475]` | `IDLE` (24 WHITE) |
| value | `[110,448 → 185,478]` | `+0.0` (30 GREEN): `(f>=0?'+':'-') + f.toFixed(1)` with `f = AMI.idleFactor = 0` |
| label | `[245,451 → 305,475]` | `PERF` (24 WHITE) |
| value | `[330,448 → 405,478]` | `+0.0` (30 GREEN), `AMI.perfFactor = 0` |
| MODIFY button | `[500,443 → 625,483]` | `MODIFY`, **always disabled** [FBW-PH] |
| Fuel-penalty box | `[14,504 → 394,563]` 1 px LIGHT_GREY | n/a |
| label | `[25,521.5 → 220,545.5]` | `FUEL PENALTY` followed by a non-breaking space (24 WHITE) |
| FUEL PENALTY field | `[220,515 → 375,552]` | `+NNN.N` centred, trailing `%` |
| Divider | `[14,573 → 754,575]` | 2 px: top LIGHT_GREY, bottom DARK_GREY (CSS 1 px outset) |
| `NAV DATABASE` label | `[24,593 → 204,617]` | 24 WHITE, on the same baseline as the value |
| NAV DB serial | `[229,590 → 416.5,620]` | e.g. `MS24100001` (30 GREEN) |
| ACTIVE box | `[14,635 → 252.25,752]` 1 px SVG_LIGHTGREY | n/a |
| ACTIVE label | `[88.1,656 → 178.1,680]` | `ACTIVE` (24 WHITE, centred in the box) |
| ACTIVE dates | `[29,705 → 235.25,735]` | e.g. `03OCT-31OCT` (30 GREEN) |
| SWAP button | `[322.25,662.5 → 462.25,724.5]` (140×62) | `SWAP` at x 352.25 and `*` ending at x 442.25; text y 684.5 (22 WHITE) |
| SECOND label | `[573.8,659.5 → 663.8,683.5]` | `SECOND` (24 WHITE, centred on 618.8) |
| SECOND dates | text from x 548.5 (two leading non-breaking spaces from 517.25), y 710.5…735.5 | `03OCT-31OCT` (**25** GREEN, not 30) |
| Section line | `[14,777 → 754,779]` 2 px LIGHT_GREY | n/a |
| `PILOT STORED ELEMENTS` | `[33,794 → 348,818]` | 24 WHITE |
| `WAYPOINTS` label | right edge 179.24, y 838…862 | 24 WHITE |
| waypoints count | `[189.24,835 → 226.74,865]` | `00` (30 GREEN) |
| `ROUTES` label | right edge 388.18, y 838 | n/a |
| routes count | x 398.18, y 835 | `00` |
| `NAVAIDS` label | right edge 179.24, y 890…914 | n/a |
| navaids count | x 189.24, y 887…917 | `00` |
| `RUNWAYS` label | right edge 388.18, y 890 | n/a |
| runways count | x 398.18, y 887 | `00` |
| DELETE ALL button | `[551,847 → 754,905]` | `DELETE ALL` left (x 565) and `*` right (ends 740); text y 867 |

The FMS P/N tab is empty in FBW [FBW-PH].

**SWAP confirmation dialog**, visible after SWAP is clicked:

| Element | Box | Content |
|---|---|---|
| Dialog | `[226.5,595 → 551.5,760]` | BG face, 2 px outset LIGHT_GREY, drawn above the ACTIVE box and SWAP button |
| Title | text `[351.5,622 → 426.5,642]` | `SWAP ?` (20 WHITE; non-breaking space before `?`) |
| `CANCEL` button | `[233.5,713 → 344,753]` | n/a |
| `CONFIRM *` button | `[398.25,713 → 544,753]` | 6 px right padding |

**Reference screenshot discrepancy.** The user's MSFS screenshot shows an older FBW build with `A380-800` on one line, `ENGINE  TRENT 972` on the next, and no FUEL PENALTY row. The pinned code renders the single line `A380-800 / TRENT 972` and adds the FUEL PENALTY box.

**[PORT]** Follow the pinned layout above. It fills the 735 px tab exactly. An optional legacy look would be `A380-800` at y 202 and `ENGINE` (24 WHITE) plus `TRENT 972` (30 GREEN) centred at y 262. That variant must keep every other y unchanged.

### 5.3 Data and actions [FBW]

| Item | Binding |
|---|---|
| Airframe and engine | constant string |
| IDLE and PERF | AMI constants (0.0). MODIFY is disabled |
| FUEL PENALTY | `value = fmgc.data.fuelPenaltyPercentage` (writes directly); `enteredByPilot = fuelPenaltyActive` (>0), so 0 or null shows in 22 px; null shows the placeholder `+000.0`; `canBeCleared = true`. Error messages drop the details line, because the page calls `showFmsErrorMessage(e.type)` only (`:154`). Effects: `getPerformanceFactorPercent()` for fuel predictions (`fmgc.ts:510-512`); the PENALTY tag (§1.2); reset to null at DONE and on FMS reset (`fmgc.ts:165-175`) |
| NAV DATABASE serial | `provider.substring(0,2).toUpperCase() + airacCycle + "0001"`. MSFS provider `Msfs` and cycle `2410` give `MS24100001` (`:56-64`; `fbw-common/src/systems/navdata/client/backends/Msfs/Msfs.ts:57-102`) |
| ACTIVE and SECOND dates | `DD MON - DD MON` built as `effDay + MON + "-" + expDay + MON` from ISO dates (`:100-107`). `effectiveTo` is the first day of the next cycle, i.e. effectiveFrom + 28 days (`navdata/shared/types/DatabaseIdent.ts:8`). **SECOND = ACTIVE** (FBW has one database) [FBW-PH] |
| SWAP * | always enabled; opens the dialog. CONFIRM runs `fmc.swapNavDatabase()`, which equals `fmc.reset()` (`FlightManagementComputer.ts:1729-1746`): reset every flight plan, reset FMGC data (including fuel penalty), invalidate managed speed, re-init simvars, delete stored waypoints, clear the newest message, navigate to DATA/STATUS, reset navigation and destination predictions. CANCEL closes the dialog |
| Stored element counts | `getDataManager().numberOfStoredElements()` gives `{waypoints, routes, navaids, runways, total}`, each 2-digit zero-padded (`:66-73`). Lat/long, PBD and PBX waypoints come from the waypoint-creation helpers (`FlightManagementComputer.ts:1186-1213`); routes, navaids and runways are always 0 in FBW *(DataManager unverified: fmgc not in checkout)* |
| DELETE ALL * | disabled when `total == 0`; runs `deleteAllStoredWaypoints()` immediately (no confirmation) |
| Refresh | 1 Hz (`:89-97`) plus the FmsPage refresh |

### 5.4 [PORT] X-Plane navigation-database identification

1. Resolve the X-Plane root with `XPLMGetSystemPath()`.
2. If `<root>/Custom Data/cycle.json` exists, read `"cycle"` (for example `{"cycle":"2609","revision":"1","name":"X-Plane 12"}` on this machine). X-Plane loads Custom Data in preference to default data.
3. Otherwise, read line 2 of `<root>/Resources/default data/earth_nav.dat` and extract `data cycle (\d{4})`. On this machine that is `1200 Version - data cycle 2406, build 20251002, …`.
4. **Do not read `Custom Data/cycle_info.txt`.** Its Navigraph licence text says it is for Navigraph support use only, and may not be read or processed by third-party add-ons.
5. Compute the AIRAC dates. Cycle 2001 starts on 2020-01-02 and cycles are 28 days apart. For cycle `YYNN`:
   - `k0 = ceil(days(2020-01-02 → YYYY-01-01) / 28)`
   - `eff = 2020-01-02 + (k0 + NN − 1)·28 days`
   - `exp = eff + 28 days`

   Checks: 2410 gives 03OCT-31OCT, matching the screenshot. 2609 gives 03SEP-01OCT, matching Navigraph's validity for that cycle.
6. Serial = `"XP" + cycle + "0001"`, for example `XP26090001`. The `XP` prefix is the X-Plane equivalent of FBW's `MS` for MSFS.
7. SECOND = ACTIVE. Keep SWAP → CONFIRM as an **FMS reset**: it is useful and matches FBW.
8. Run the file reads once at plugin enable (and on aircraft reload), not per frame.

---

## 6. DATA/AIRPORT (`pages/FMS/DATA/MfdFmsDataAirport.tsx`, `.scss`)

### 6.1 Frame

The page has two tabs:

| Tab | Box | Deep link |
|---|---|---|
| `DATABASE ARPTs` | `[2,136 → 391.5,172]` | extra `database` |
| `PILOT STORED RWYs` | `[376.5,136 → 766,172]` | extra `pilot-stored` |

Tab content box: `[2,172 → 766,880]`. Inner area: x 14…754, y 182…868.

A RETURN button at `[0,910 → 145,950]` runs `navigateTo('back')`. It is visible only on tab 0, and still occupies its space on tab 1.

### 6.2 DATABASE ARPTs tab [FBW, computed]

| Element | Box | Content |
|---|---|---|
| label | text `[185.5,210.5 → 310.5,230.5]` | `ARPT IDENT` (20 WHITE) |
| ARPT IDENT field | `[340.5,202 → 432.5,239]` | AirportFormat, centred, **always mandatory** (AMBER `▯▯▯▯` when empty), not clearable |
| Airport name | centred on x 384, y 266.5…296.5 | `airport.name.toUpperCase()` (30 GREEN) |
| Airport coordinates | centred on x 384, y 301.5…331.5 | `DD°MM.dN/DDD°MM.dE` (30 GREEN) |
| List or detail frame | `[14,359 → 754,809]` 1 px LIGHT_GREY, clip inside | n/a |

The FBW `.mfd-data-airport-input` row centres the label and field, then offsets the label 90 px left and the field 60 px left. The boxes above already include these offsets.

**Runway list mode** (`selectedRunwayIndex == null`):

- One Button per runway, each 220×40, laid out in a column-wrapped flow: 8 per column, 3 columns visible.
- Button *k*: column `c = k div 8`, row `r = k mod 8`, box `[25+230c, 365+50r → 245+230c, 405+50r]`.
- FBW shows no more than 24; further columns are clipped [FBW-PH, no scrolling].
- Label: `designation.padEnd(3) + " " + lengthText.padStart(5, NBSP) + ("M"|"FT") + " " + ("LS" or "")`, 22 px WHITE, left-aligned without wrapping. Examples: `09L  3902M LS` (metric) and `27R 12802FT LS` (imperial).
- Click: `selectedRunwayIndex := k`.

**Runway detail mode.** Text labels are right-aligned to x 265 (20 WHITE); values start at x 285.

| Row | y | Label | Value |
|---|---|---|---|
| RWY | 380…410 | `RWY` | designation padded to 3 (30 GREEN) |
| index | 410…430 | none | `n/N` (20 WHITE), centred on x 449 |
| scroll | 440…490 | none | IconButton `double-left` `[396.5,440 → 446.5,490]` (disabled when n=1) and `double-right` `[451.5,440 → 501.5,490]` (disabled when n=N); they step the index by ±1 |
| LAT/LONG | 510…540 | `LAT/LONG` | threshold coordinates (30 GREEN) |
| ELEVATION | 565…595 | `ELEVATION` | threshold altitude rounded to 10 ft, padded to 5 with NBSP (30 GREEN), then `FT` (20 UNIT_BLUE) at +6 |
| LENGTH | 620…650 | `LENGTH` | `" " + round(length)` in M or FT (30 GREEN) + unit. On the same row, right-aligned to x 653: `CRS` (20 WHITE) at x 530.75, course `NNN` (30 GREEN) at x 578.25, `°` (20 UNIT_BLUE) at x 640.5 |
| LS IDENT | 675…705 | `LS IDENT` | LS ident, or blank (30 GREEN) |
| RWY LIST button | `[25,750 → 245,790]` | n/a | `RWY LIST`; sets `selectedRunwayIndex := null` |

The detail panel has a BG background. It uses CSS `box-sizing:border-box` and padding `20 0 20 130` (`MfdFmsDataAirport.scss:65-71`).

**PILOT STORED RWYs tab** [FBW-PH]:

- `NO PILOT STORED NAVAID` (sic; 20 WHITE), centred on x 384, y 227…247.
- `NEW RWY` button, 170×60, always disabled. FBW positions it with `position:absolute; top:630px; right:0`, relative to its own button container, which puts it at `[584,832 → 754,892]`, partly clipped by the tab.

**[PORT]** Show `NO PILOT STORED RWY` and place NEW RWY at `[584,798 → 754,858]`.

### 6.3 Logic [FBW]

| Event | Effect |
|---|---|
| ARPT IDENT entered (4 characters) | `airportIcao := v`. If the length is 4: `loadAirport(icao)`, then `loadAllRunways()`, then build the list; show the info and list. Otherwise hide the info, list and detail (`:174-189`) |
| Airport not found | `loadAirport` returns null, so the previous name and list stay visible. **[PORT]** Queue `NOT IN DATABASE`, clear the name, and hide the list |
| Runway designation | `runway.ident.substring(4)`: FBW runway idents are `ICAO + designation`, e.g. `EGLL27L` gives `27L` |
| LS on the list button | **[FBW-PH] bug**: `lsIdent` defaults to NBSP, which is truthy, so every runway shows `LS` (`:142,124`). **[PORT]** Show `LS` only when an LS exists |
| Course | `runway.bearing` (true), `toFixed(0)` padded to 3. **[PORT]** Same; true course from threshold to opposite threshold |
| Coordinates | `coordinateToString(lat, lon, false)` (`fbw-common/src/systems/instruments/src/Convert.ts:63-91`): `DD°MM.dN/DDD°MM.dE`, where FBW's tenths digit is `ceil(6·frac(minutes×10))` (a DMS-derived quirk). **[PORT]** Use true tenths of a minute, `floor(frac(minutes)×10)`. Draw the degree sign from the atlas |
| Length | the runway length in m, shown in M (metric) or converted to FT; NaN shows `-----` |
| Initial state | no airport: info, list and detail hidden; RETURN visible |

### 6.4 [PORT] X-Plane data sources

The port's navigation-data service (owned by the FMS core spec) is preferred. Minimal fallback:

| Datum | Source |
|---|---|
| Airport existence, name, ARP | `XPLMFindNavAid(NULL, ident, NULL, NULL, NULL, xplm_Nav_Airport)` then `XPLMGetNavAidInfo`. **Verify** `outID` equals `ident`: the SDK matches IDs that *contain* the fragment and returns the last match (`XPLMNavigation.h:137-162`). For an exact index, iterate `XPLMFindFirstNavAidOfType(xplm_Nav_Airport)`…`XPLMGetNextNavAid` once on the main thread and build a hash |
| Runways (both ends, lat/lon) | `apt.dat` row type 100. Reuse the incremental, off-draw-callback loader in `native/avionics/a380x_efis_airport.inc` (it already extracts `EfisAirportRunway {lat1,lon1,lat2,lon2,width,end1,end2}`), with a second parser target for the MFD query. Length = great-circle distance between the ends; course = initial true bearing from each end to the other |
| Threshold elevation and LS ident | `Custom Data/CIFP/<ICAO>.dat`, falling back to `Resources/default data/CIFP/<ICAO>.dat`. Records look like `RWY:RW27L,     ,      ,00077, ,ILL ,3,   ;N51275382,W000260276,0000;`: field 4 is the threshold elevation in ft, field 6 the LS ident (blank if none), then the threshold lat/lon. Prefer the CIFP threshold over the apt.dat end coordinates for LAT/LONG |

Run the loading asynchronously. While loading, show the airport info and an empty list frame. The list appears when parsing completes.

---

## 7. DATA/DEBUG (`pages/FMS/DATA/MfdFmsDataDebug.tsx`)

**[FBW]** This page is **not routed**: no `MfdPageDirectory.tsx` case and no menu item points to it, so it is unreachable in FBW.

Content, if it were routed:

- Title `DATA/{pageTitle}`.
- Tabs `FMGC` and `N/A`.
- The FMGC tab is a vertical list: each line is a label (20 WHITE, 25 px right margin) followed by a value (30 GREEN), with 10 px line spacing.
- Refreshed at 2 Hz.

| Line index | Label | Value |
|---|---|---|
| 1 | `VLS (FMC)` | `L:A32NX_SPEEDS_VLS`, 2 decimals |
| 2 | `V_A_PROT (FMC)` | `L:A32NX_SPEEDS_ALPHA_PROTECTION_CALC` |
| 3 | `V_A_MAX (FMC)` | `L:A32NX_SPEEDS_ALPHA_MAX_CALC` |
| 5 | `VLS (PRIM 1)` | ARINC `L:A32NX_PRIM_1_V_LS`, 1 decimal |
| 6 | `V_A_PROT (PRIM 1)` | ARINC `L:A32NX_PRIM_1_V_ALPHA_PROT` |
| 7 | `V_A_MAX ((PRIM 1))` (sic) | ARINC `L:A32NX_PRIM_1_V_ALPHA_LIM` |
| 8 | `PRIM SSM` | SSM of the VLS word |
| 10 | `GD SPEED (FCOM LOOKUP)` | `L:A32NX_SPEEDS_GD` |
| 11 | `F SPEED (FCOM LOOKUP)` | `L:A32NX_SPEEDS_F` |
| 12 | `S SPEED (FCOM LOOKUP)` | `L:A32NX_SPEEDS_S` |

Empty lines (0, 4, 9, 13–16) still take one 30 px line each.

**[PORT]** Do not expose this page to users. Optionally register `fms/data/debug` behind a developer build flag. If so, use the line layout starting at y 182, 40 px per line (30 + 10 margin), and source the values from the port's FMGC/PRIM speed outputs. It is not reachable from any menu.

---

## 8. SEC INDEX (`pages/FMS/SEC/MfdFmsSecIndex.tsx`, `.scss`)

### 8.1 Frame and entry

**Entry points:**

- Header SEC INDEX menu items `SEC 1`, `SEC 2` and `SEC 3`, which go to `fms/sec/index/1|2|3`.
- The KCCU `SECINDEX` key goes to `fms/sec/index` with no extra, and opens tab 0.
- The INIT RETURN button on a SEC page.

The initial tab comes from the extra, read once at render. Clicking a tab changes the tab only, not the URI.

**Tabs:** `SEC 1`, `SEC 2` and `SEC 3`, in 21 px font. Boxes:

| Tab | Box |
|---|---|
| SEC 1 | `[2,136 → 266.67,172]` |
| SEC 2 | `[251.67,136 → 516.33,172]` |
| SEC 3 | `[501.33,136 → 766,172]` |

Tab content box: `[2,172 → 766,948]`. Inner area: x 14…754, y 182…936. There is no RETURN button.

### 8.2 Per-tab layout [FBW, computed]

| Element | Box | Content |
|---|---|---|
| City pair | centred on x 384, y 186.75…214.75 | `FROM / TO`, e.g. `EGLL / KJFK` (28 WHITE). Empty unless both exist |
| Created line | left x 14, y 228.25…248.25 | `CREATED` + 2 NBSP + `HHMM` + source suffix (20 WHITE). Empty unless the SEC exists and has a creation time |
| Route box | `[14,257 → 569,757]` 1 px LIGHT_GREY | n/a |
| Route grid | inner `[20,278 → 563,736]` | 4 columns of 135.75 px; 11 rows of 41.64 px |
| Route cell (r, col) | `[20+135.75·col, 278+41.64r+6.82 → +135.75, +28]` | 1 px LIGHT_GREY bottom border; text at the cell's top-left. Columns 0 and 2 hold the airway or procedure (22 WHITE), columns 1 and 3 the fix ident (26 WHITE) |
| IMPORT | `[574,257 → 749,297]` | `IMPORT` with a ▼ arrow (▲ if the menu opens upward), 175 px |
| CPNY F-PLN | `[574,302 → 749,364]` | 2-line label per §3.5 |
| F-PLN | `[581.5,492 → 741.5,532]` | 160 px |
| PERF | `[581.5,537 → 741.5,577]` | n/a |
| WIND | `[581.5,582 → 741.5,622]` | n/a |
| FUEL&LOAD | `[581.5,627 → 741.5,667]` | n/a |
| INIT | `[581.5,672 → 741.5,712]` | n/a |
| WHAT IF | `[581.5,717 → 741.5,757]` | n/a |
| DELETE * | `[14,771.5 → 174,831.5]` (160×60) | `DELETE` and `*` |
| scroll down | `[229,771.5 → 289,831.5]` | IconButton `double-down` |
| scroll up | `[294,771.5 → 354,831.5]` | IconButton `double-up` |
| SWAP ACTIVE * | `[409,771.5 → 569,831.5]` | 2-line `SWAP`/`ACTIVE` + `*`, **AMBER text** |
| XFR TO MAILBOX | `[409,861.5 → 569,921.5]` | n/a |
| PRINT * | `[581.5,862 → 741.5,922]` | n/a |

Route slots: slot *i* (0…21) maps to grid row `i div 2`, and columns `(i mod 2)·2` (annotation) and `(i mod 2)·2 + 1` (ident). A slot is hidden, including its cell underline, when its ident is null.

### 8.3 Data [FBW] (`MfdFmsSecIndexDataStore`, `:34-149`)

The store refreshes on every flight-plan change notification, on the FmsPage refresh, and when HDG/TRK mode engagement changes.

| Datum | Rule |
|---|---|
| `secExists` | `hasSecondary(n)` and `legCount > 0` |
| Source suffix (first matching flag) | `CopiedFromActive` gives ` (IMPORT ACTIVE[-MODIFIED])`; `SwappedWithActive` gives ` (SWAP ACTIVE[-MODIFIED])`; `ManualCreation` gives ` (MANUAL)`; `CompanyFlightPlan` gives ` (CPNY F-PLN[-MODIFIED])`; `AtcFlightPlan` gives ` (ATC F-PLN[-MODIFIED])`. `-MODIFIED` is appended when `plan.wasModified` |
| `timeCreated` | `plan.timeCreated` (epoch ms), formatted `HHMM` |
| Route overview legs | See the filter below |
| Annotation text | `''` gives `DIR`. A runway leg gives `RWY` with ident `ident.substring(4)` (e.g. `27L`). Otherwise the annotation (airway or procedure name) |
| Scrolling | `routeStartFromLine` starts at 0. Down (`double-down`) adds 1 and is disabled when `ceil(legs/2) − line ≤ 11`. Up subtracts 1 (minimum 0) and is disabled at 0 |

Route overview filter (`:93-118`):

1. Take `allLegs` before `firstMissedApproachLegIndex`.
2. Drop discontinuities.
3. Keep a leg only if:
   - it is the last leg, **or** the next leg's annotation differs, **or** its annotation is `''`, **or** it is a runway; **and**
   - it is **not**: the origin or destination airport ident, or a procedure leg whose next leg has the same `procedureIdent` but a different annotation.

The result is one entry per airway or procedure exit point, i.e. an airway/fix summary.

### 8.4 Buttons [FBW]

| Button | Enabled | Action |
|---|---|---|
| IMPORT (menu) | always | Items always enabled: `ACTIVE*` runs `secondaryCopyFromActive(n, beforeEngineStart=!enginesWereStarted)`. The other two items are the remaining SECs, in order (SEC1 shows `SEC 2*`, `SEC 3*`; SEC2 shows `SEC 1*`, `SEC 3*`; SEC3 shows `SEC 1*`, `SEC 2*`) and run `secondaryCopyFromSecondary(src, n, …)` |
| CPNY F-PLN | §3.5 with plan index n+2 (only `uplinkInProgress` disables it for SECs) | request into SECn; INSERT/CLEAR menu |
| F-PLN | always | `fms/sec{n}/f-pln` |
| PERF | `secExists` | `fms/sec{n}/perf` |
| WIND | never [FBW-PH] | n/a |
| FUEL&LOAD | `secExists` | `fms/sec{n}/fuel-load` |
| INIT | always | if no SECn: `secondaryInit(n)` (creates an empty SEC, MANUAL); then `fms/sec{n}/init` |
| WHAT IF | never [FBW-PH] | n/a |
| DELETE * | `secExists` | `secondaryDelete(n)`, immediately. Any open SECn page then auto-navigates to its ACTIVE equivalent (§1.3) |
| SWAP ACTIVE * | **visible** only when `secExists AND canActivateOrSwapSecondary(n)` | `swapActiveAndSecondaryPlan(n)` (below) |
| XFR TO MAILBOX, PRINT * | never [FBW-PH] | n/a |

`canActivateOrSwapSecondary(n)` (`FlightManagementComputer.ts:891-915`) requires all of:

- SECn exists;
- **no** TMPY;
- SECn has an origin and a destination;

and **at least one** of:

- HDG/TRK mode is engaged;
- neither plan has an active leg;
- both plans have the same TO leg at the same `activeLegIndex`;
- PH = Preflight.

`swapActiveAndSecondaryPlan(n)` (`:917-958`):

1. Record ΔZFW, ΔZFWCG and the old destination.
2. If an active plan exists, call `activeAndSecondarySwap(n, beforeEngineStart)`; otherwise `secondaryActivate(n, beforeEngineStart)`.
3. Then:
   - if PH ∈ {Preflight, Done}: queue `CHECK T.O. DATA` and publish the flex temperature;
   - if ΔZFW > 5 t and ΔZFWCG > 0.5: queue `CHECK ZFW`;
   - if the destination changed: notify the flight-phase manager (`handleNewDestinationAirportEntered`).

**[PORT]**

- Grey out IMPORT items whose source plan does not exist (FBW leaves them enabled).
- Confirm DELETE* and SWAP ACTIVE* with the standard star-button semantics; FBW has no extra dialog, so keep none.
- Swap and copy semantics belong to the port FMS core. This page only calls them.

---

## 9. MESSAGE LIST (`pages/FMS/MfdMsgList.tsx`, `MfdMsgList.scss`)

### 9.1 Overlay layout [FBW, computed]

This is an overlay, not a page. It opens with the footer `MSG LIST` button (`MFD.tsx:150-152`, `Footer.tsx:62-68`) and closes with its own `CLOSE` button. The overlay is opaque BG covering `[0,98 → 768,950]` (z 20): header and footer stay visible and usable, and the page beneath is hidden.

| Element | Box | Content |
|---|---|---|
| Title bar | `[0,98 → 768,134]` | `MESSAGE LIST`; EO tag possible; no TMPY or PENALTY |
| List top line | `[75,209 → 693,211]` | 2 px WHITE |
| Message row k (k = 0…3) | `[75, 211+80k → 693, 291+80k]` | Text 26 WHITE at x 80, top y 216+80k, wrapped to 608 px (37 characters per line). 2 px WHITE line at the bottom of each row |
| CLOSE button | `[0,910 → 96.75,950]` | `CLOSE` |

The message text is always drawn WHITE in the list, whatever its amber flag. A `\n` in the text is collapsed to a space by CSS (`white-space: normal`); **[PORT]** render the second line as a real line break.

### 9.2 Content and queue model [FBW]

The list shows `fms.fmsErrors[0..3]`. The array is newest-first and includes cleared Type II messages. It is re-rendered at 1 Hz (`MfdMsgList.tsx:71-77`); with more than 5 entries FBW logs a warning and still shows 4.

The queue (`FlightManagementComputer.ts:1136-1180`):

| Field | Meaning |
|---|---|
| `messageText` | `text` plus optional `"\n" + details` |
| `backgroundColor` | `'amber'` if `isAmber`, else `'white'` |
| `cleared` | true once the pilot cleared a Type II message |
| `isResolvedOverride`, `onClearOverride` | resolution test and clear callback |

| Operation | Rule |
|---|---|
| add | If an identical text exists and is cleared, remove it; then insert at index 0 |
| remove (by text) | removes the first match |
| auto-resolve | a Type II message whose `isResolved()` is true is removed during periodic update |
| CLEAR INFO (footer button when a non-cleared message exists, or the KCCU `CLRINFO` key) | First non-cleared message: if Type II, set `cleared=true` (it stays in the list) and call `onClear`; if Type I, remove it |

Footer interplay (`Footer.tsx:17-46`):

- The footer message area shows the first non-cleared message: 26 px **black** text on a WHITE (`#ffffff`) or AMBER (`#e68000`) background, with `pre-wrap`, so `\n` gives two lines.
- The footer button label is `CLEAR`/`INFO` while such a message exists, and `MSG`/`LIST` otherwise.
- The button is a 2-line Button 100 wide (`[5,957 → 105,1019]`). The message area starts 15 px right of it, at x 120.

### 9.3 [PORT]

- Show up to 5 rows with the same 80 px pitch; there is room down to y 611. Draw the text in its message colour: AMBER for amber messages, WHITE otherwise.
- Keep the newest-first order and the CLOSE button.
- The overlay must swallow KCCU and mouse input for the region it covers.

---

## 10. FMS PAGE NOT AVAIL (`pages/FMS/MfdFmsPageNotAvail.tsx`)

**[FBW]** An opaque BG overlay at `[0,132 → 768,950]` (z 20), drawn over the title bar's lower 2 px and the whole page. Content: `FMS PAGE NOT AVAIL` in 24 AMBER, centred on (384, 541), with text box `[249,529 → 519,553]`.

It is visible **iff**:

- the current URI system is `fms`; **and**
- this side's FMS is failed: `L:A32NX_FMS_L_FAILED` for CAPT, `L:A32NX_FMS_R_FAILED` for FO.

The failure flags are computed in `FMC/FmcService.ts:54-79`, from FMS DATA knob position `k` and `inop(X)` = not powered, or reset pulled, or failed:

| Side | Failed when |
|---|---|
| CAPT | `k==0` (FO side selected): FMC B and FMC C both inop; otherwise FMC A and FMC C both inop |
| FO | `k==2` (CAPT side selected): FMC A and FMC C both inop; otherwise FMC B and FMC C both inop |

The header stays usable, so the crew can switch to ATCCOM, SURV or FCU BKUP. The footer region (y ≥ 950) is not covered.

**[PORT]** The port FMS core publishes `a380x/xp12/fms/capt_side_failed` and `a380x/xp12/fms/fo_side_failed` (int) with the same logic. Inputs: `a380x/xp12/fms/data_knob` (0 = FO, 1 = NORM, 2 = CAPT), the FMC A/B/C bus-power states (FBW `FmcService.ts:26-51`: FMC A on DC ESS, FMC B on DC 1, FMC C on DC 2), and the reset-panel and failure states. While the overlay is visible, FMS page content must neither draw nor accept input.

---

## 11. ERROR 404 NOT FOUND (`pages/FMS/MfdNotFound.tsx`)

**[FBW]** This page renders for any URI with no `pageForUrl` case (`MfdPageDirectory.tsx:337-346`).

| Part | Behaviour |
|---|---|
| Header | for `uri.sys` (FMS header by default) |
| Title bar | `ERROR 404 NOT FOUND`; not an FMS-subsystem page, so the EO and TMPY cells are drawn without the 2 px black gaps and stay empty |
| Content | empty BG |
| Footer | normal |

**500 ms after render it calls `navigateTo('back')`.** If the navigation stack has fewer than 2 entries, it stays on the error page.

Reachable only through enabled links to unimplemented URIs. All such menu items and buttons in FBW are disabled, so this is mainly a safety net.

**[PORT]** Same behaviour. Implement it as the default case of the page dispatcher, with the timer driven by the MFD core's clock (not wall-clock threads).

---

## 12. [PORT] Data-interface summary for the MFD core

The MFD module (`mfd_core.c`) should read and write the FMS core only through an interface like the one below. Units are SI or aviation-standard internally; null is NaN for floats and −1 for enumerations and integers.

| Interface item | Type and unit | Read by | Written by |
|---|---|---|---|
| `fms_plan_exists(idx)`, `fms_plan_has_tmpy()`, `fms_sec_exists(n)` | bool | all | n/a |
| `plan.origin`, `plan.dest`, `plan.altn` | ICAO[5] or "" | INIT, F&L, SEC | `fms_new_city_pair(idx, from, to, altn)`, `fms_set_alternate(idx, icao or NULL)` |
| `plan.flight_number` | char[11] | INIT, header | `fms_set_flight_number(idx, s)` |
| `perf.crz_fl` | int FL | INIT | `fms_try_set_cruise_fl(idx, fl)` returns bool (§3.4) |
| `perf.crz_temp_c` and pilot flag | float °C | INIT | `fms_set_perf(idx, PILOT_CRZ_TEMP, v)` |
| `perf.ci`, `perf.ci_mode` (0 LRC, 1 ECON) | int | INIT, F&L | `fms_set_perf(idx, CI or CI_MODE, v)` |
| `perf.tropo_ft` and pilot flag | float | INIT | `PILOT_TROPO` |
| `perf.trip_wind_kt` | float (+ = tail) | INIT | `PILOT_TRIP_WIND` |
| `perf.zfw_kg`, `perf.zfwcg_pct`, `perf.block_kg` | float | F&L | `ZFW`, `ZFWCG`, `BLOCK` |
| `perf.taxi_kg`, `perf.rte_rsv_kg`, `perf.rte_rsv_pct`, `perf.altn_kg`, `perf.final_kg`, `perf.final_min`, `perf.min_dest_fob_kg`, each with a pilot flag | float | F&L | the matching `PILOT_*` keys; writes follow the pairs in §4.2 (setting fuel clears the paired pct or time) |
| `perf.pax` | int | F&L | `PAX` (also publishes `a380x/xp12/fms/pax_number`) |
| `fms_tow_kg(idx)`, `fms_lw_kg(idx)`, `fms_trip_kg(idx)`, `fms_extra_kg(idx)`, `fms_dest_efob_kg(idx)`, `fms_altn_efob_kg(idx)`, `fms_dest_eta_s(idx)` | float | F&L | n/a (computed at 1 Hz by the core with the §4.3 formulas) |
| `fqms_gw_kg`, `fqms_fob_kg`, `fqms_cg_pct` | float | F&L | X-Plane adapter (§4.5) |
| `fms_engines_were_started`, `fms_flight_phase`, `fms_engine_out`, `fms_dest_efob_below_min` | bool or enum | all | core |
| `fms_fuel_penalty_pct` | float (NaN = none) | STATUS, title | `fms_set_fuel_penalty(v)` |
| `fms_navdb_cycle`, `fms_navdb_eff`, `fms_navdb_exp` | char[5], dates | STATUS | X-Plane adapter (§5.4) |
| `fms_stored_counts()` | 4×int | STATUS | `fms_delete_all_stored()` |
| `fms_reset()` | n/a | STATUS SWAP | n/a |
| SEC operations: `copy_from_active(n)`, `copy_from_sec(src,n)`, `init(n)`, `delete(n)`, `can_swap(n)`, `swap(n)`, `sec_overview(n, out[])`, `sec_created(n)`, `sec_flags(n)` | n/a | SEC INDEX | n/a |
| Messages: `fms_msg_add(id, details)`, `fms_msg_clear_latest()`, `fms_msg_get(i)` | n/a | all, MSG LIST, footer | n/a |
| CPNY F-PLN: `available`, `in_progress`, `requested_for`, `fms_cpny_request(idx)`, `insert(idx)`, `clear()` | n/a | INIT, SEC | n/a |
| Airport query: `fms_airport_lookup(icao)` gives name, ARP and a runway array (async status) | n/a | AIRPORT | n/a |

Keep all X-Plane SDK calls in the adapter (`mfd_xplane.c`). The core and renderer must build without XPLM, so the unit tests (like the existing `native/avionics/test_*.c`) can drive the page state machines headless.

---

## 13. [PORT] Acceptance checks

1. **Geometry.** At 768×1024, overlay this document's boxes on screenshots of the port. The DATA/STATUS `DELETE ALL` button must end at y ≈ 905. The INIT `CPNY T.O. REQUEST` button must end at y 905. The INIT SEC `RETURN` button must sit at y 905…945.
2. **INIT.**
   1. Enter FROM `EGLL`, then TO `KJFK`. The plan is created, ALTN shows `NONE` (GREEN, 27 px), and CPNY RTE and ALTN RTE show `NONE`.
   2. Enter CRZ FL `350`: CRZ TEMP shows `-54°C` (22 px).
   3. Enter CRZ TEMP `50`: it stores −50.
   4. Enter `999` in CRZ FL: `ENTRY OUT OF RANGE` appears in the footer.
3. **Mandatory fields.** With an empty ACTIVE plan, FLT NBR, FROM, TO, ALTN, CRZ FL and CI show AMBER `▯` boxes. ALTN, CRZ FL and CI are disabled (grey) until FROM and TO exist.
4. **FUEL&LOAD.**
   1. ZFW `300`, ZFWCG `30`, BLOCK `120`, TAXI default 1.5: TOW = 418.5 t.
   2. Enter FINAL time `0045`: FINAL fuel shows 9.0 t (small font).
   3. Enter RTE RSV `3.0`: the % field recomputes from TRIP, or `--.-` without TRIP.
   4. Switch the unit setting to imperial: all units become `KLB` and values are ×2.2046.
5. **DATA/STATUS.**
   1. With Custom Data cycle 2609 installed, the page shows `XP26090001` and `03SEP-01OCT` in both ACTIVE and SECOND.
   2. SWAP → CONFIRM resets the FMS and lands on DATA/STATUS.
   3. FUEL PENALTY `5` shows the PENALTY tag on DATA/STATUS and FUEL&LOAD.
6. **SEC INDEX.**
   1. IMPORT → ACTIVE* fills the city pair, `CREATED  HHMM (IMPORT ACTIVE)` and the route overview.
   2. In Preflight, SWAP ACTIVE* is visible, and pressing it swaps the plans and queues `CHECK T.O. DATA`.
   3. DELETE* clears the tab.
7. **MSG LIST.** Queue 3 messages. The footer shows the newest; CLEAR INFO walks through them; MSG LIST shows cleared Type II messages too.
8. **NOT AVAIL.** Fail FMC A and FMC C with the knob at NORM. The CAPT MFD on FMS shows the AMBER `FMS PAGE NOT AVAIL`; switching the header system to SURV hides it.
