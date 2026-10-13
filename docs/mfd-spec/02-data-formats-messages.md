# MFD Spec 02: Data Entry Formats and FMS Messages

Scope: every MFD data-entry format (parse, validate, render), the error-to-message routing, every FMS/MFD/ATCCOM message,
the message queue semantics (message area, CLEAR INFO, MSG LIST), the unit conversions involved, and a C API that a
developer can implement without reading the TypeScript.

Upstream: FlyByWire `2baa2b35` (GPL-3.0). Port target: X-Plane 12 native C module (simulator-independent core).

---

## 0. Conventions

### 0.1 Source references

All paths below are relative to `upstream-aircraft/fbw-a380x/src/systems/instruments/src/` unless they start with
`fbw-common/` or `fbw-a380x/`. Short prefixes used in citations:

| Prefix | File |
|---|---|
| `DEF:` | `MFD/pages/common/DataEntryFormats.tsx` (1978 lines) |
| `IF:` | `MsfsAvionicsCommon/UiWidgets/InputField.tsx` |
| `MSG:` | `MFD/shared/NXSystemMessages.ts` |
| `ERR:` | `MFD/shared/A380FmsError.ts` |
| `FMC:` | `MFD/FMC/FlightManagementComputer.ts` |
| `ACI:` | `MFD/FMC/FmcAircraftInterface.ts` |
| `ATC:` | `MFD/ATCCOM/AtcDatalinkSystem.ts` |
| `FTR:` | `MFD/pages/common/Footer.tsx` |
| `PERF:` | `fbw-a380x/src/systems/shared/src/PerformanceConstants.ts` |

### 0.2 Geometry and colour

- Canvas: FBW MFD canvas 768 x 1024 px. Origin top-left, +x right, +y down (CSS convention, kept as is).
- Colours are hex sRGB. Font: "FBW-Display-EIS-A380-SlashedZero" (monospace; advance = 2568/4096 em = 0.627 em).

### 0.3 Markers

| Marker | Meaning |
|---|---|
| **[FBW]** | Behaviour implemented by FlyByWire at the pinned commit. Port must reproduce unless a [PORT] note says otherwise. |
| **[N/A]** | Defined upstream but unused, placeholder, or dead code in the A380X. |
| **[PORT]** | Recommended port behaviour (deviation or decision). |
| **[R]** | Reconstructed: the upstream source is not in the pinned checkout (see 0.4). Verify before relying on edge cases. |

### 0.4 Sources absent from the checkout

- `@fmgc/*` resolves to `../../../fbw-a32nx/src/systems/fmgc/src/*` (`fbw-a380x/src/systems/tsconfig.json:41`). The
  `fbw-a32nx/` tree is **not present** in `upstream-aircraft/`. Affected: `FmsError.ts` (`FmsError`, `FmsErrorType`) and
  `flightplanning/WaypointEntryUtils.ts` (`isPlaceFormat`, `isRunwayFormat`, `getOrCreateWaypoint`). Their behaviour is
  reconstructed from usage in the A380X code and from the upstream FBW repository (marked **[R]**).
- `@microsoft/msfs-sdk` is not in `node_modules/`. Used only for `UnitType` conversions (section 8) and `Subject`.

### 0.5 JS terminology used below

`null` = "no value" (field empty). In C this is `has_value = 0`. "Plain error" = an error **without** details text
(single-line message).

---

## 1. Entry pipeline (what a format has to support)

The MFD has no scratchpad. Every editable value is an in-place **input field** (`IF:84`) that owns one *data entry
format* object (`DataEntryFormat<T,U>`, `DEF:33-47`). The format supplies:

| Member | Meaning |
|---|---|
| `placeholder` | Text shown when the value is null (dashes; may be turned into amber boxes, see 2.5). |
| `maxDigits` | Max characters the edit buffer accepts, and field min width (`18 px * maxDigits`, `IF:659-662`). |
| `maxOverflowDigits` | Optional extra characters; field widens while editing ("overflow"). Only `FlightLevelFormat` (2). |
| `unit` | Unit string used in error details and overflow width. Not necessarily the displayed unit. |
| `format(value)` | Returns tuple `[valueText, leadingUnit, trailingUnit]` (`FieldFormatTuple`, `DEF:10`). |
| `parse(input)` | Returns the parsed value, `null` (clear), or throws `FmsError`/`A380FmsError`. |
| `reFormatTrigger` | Notifies the field to re-render (QNH placeholder switch, ALT/FL transition change). |
| `unitFamily` | Weight/length display unit; a change re-renders the field. |

### 1.1 Characters that can reach `parse()`

| Source | Accepted keys -> buffer characters | Ref |
|---|---|---|
| KCCU (cockpit keyboard, `hEvent A32NX_KCCU_L_<KEY>` / `_R_`) | single `[A-Za-z0-9]` key name -> that char; `SP` -> space; `SLASH` -> `/`; `DOT` -> `.`; `PLUSMINUS` -> toggles leading sign (see below); `BACKSPACE` -> delete last; `ENT` -> commit; `ESC`/`ESC2` -> revert (1.5); arrow keys ignored | `IF:593-645` |
| Touchscreen / PC keyboard (Coherent) | `[A-Z0-9/.+- ]` (upper-cased); `,` -> `.`; Enter -> commit; Backspace | `IF:249-271` |

`PLUSMINUS`: if buffer starts with `+` -> replace by `-`; if starts with `-` -> replace by `+`; else prefix `-`
(also when buffer is null -> `"-"`). It bypasses the `maxDigits` limit (`IF:621-630`).

No `:` key exists, so `:` only reaches `parse()` through re-validation of formatted text (1.3).

### 1.2 Buffer length limit

A character is appended only if `len < maxDigits`, or, for overflow-capable fields, `len < maxDigits + maxOverflowDigits`;
free-text fields (`freeText` prop, used by dropdown entries for waypoints) accept up to **24** characters
(`IF:89`, `IF:282-292`). Consequently many syntactically "valid" inputs can never be typed (e.g. `0.84` in a 3-digit Mach
field). Test vectors in section 4 respect these limits.

### 1.3 Commit (ENT or focus loss)

`IF:336-368`: on commit with validation:

- If the buffer was **never modified** and the field has a value, the **formatted value text** (`format(value)[0]`, no
  units) is re-parsed and re-submitted. Every format must therefore round-trip; see section 7 for the formats that do not.
- Otherwise the buffer content is parsed (the empty string when the buffer is empty).

### 1.4 Clear

`parse('')` returns `null` for every format (= delete/clear the value). Text formats also map their own placeholder text
to `null`. `FixFormat` maps whitespace-only input to `null` (`DEF:1079`).
[FBW] `BACKSPACE` on an untouched field yields an empty buffer even when `canBeCleared` is false
(`IF:237-247`: `null?.slice()` falls through to `''`), so `canBeCleared=false` does not actually prevent clearing.
[PORT] Honour `canBeCleared`: when false, an empty buffer on commit restores the old value without calling `parse`.

### 1.5 ESC

[FBW] `ESC` sets the buffer to `format(currentValue)[0]` and commits (`IF:636-640`). If the value is null, the buffer
becomes the **placeholder** (e.g. `---`) and numeric formats then raise FORMAT ERROR.
[PORT] ESC = abandon edit, restore display, no parse, no message.

### 1.6 Validation sequence

`IF:383-441`:

1. `parse(input)`. On throw of an `FmsError`: call the field's `errorHandler(err)`; value unchanged; stop.
2. On success: field enters "validating" state (text colour `#c4c6cf`) for **max(500 ms, page handler time)**
   (`IF:399-414`). Then the page's `dataHandlerDuringValidation(new, old)` runs (may veto with `false` or by throwing;
   exceptions here are swallowed **without** a message, `IF:408-411`), then `onModified(new)` (an `FmsError` thrown here
   is passed to `errorHandler`, `IF:417-425`) or `value.set(new)`.
3. Buffer reset to null, display re-rendered from the (possibly new) value.

### 1.7 Error routing

`ERR:5-15`: `A380FmsError extends FmsError` adds an optional `details` string (second message line). Plain `FmsError`
(used by `RadioAltitudeFormat`, `TropoFormat` out-of-range) has no details.

Standard handler on 93 fields: `errorHandler={(e) => fmcService.master.showFmsErrorMessage(e.type, e.details)}`.
`showFmsErrorMessage` (`FMC:1094-1111`) maps:

| `FmsErrorType` | Message (section 10.5) | Details passed |
|---|---|---|
| `EntryOutOfRange` | `ENTRY OUT OF RANGE` | yes |
| `FormatError` | `FORMAT ERROR` | yes |
| `NotInDatabase` | `NOT IN DATABASE` | no |
| `NotYetImplemented` | `NOT YET IMPLEMENTED` (fictional) | no |
| any other | nothing | - |

Fields whose handler **drops** details (single-line message only) [FBW]: DATA/STATUS fuel-penalty field
(`MfdFmsDataStatus.tsx:154`), F-PLN destination window (`DestinationWindow.tsx:95`), AIRWAYS VIA/TO
(`MfdFmsFplnAirways.tsx:252,325`), FIX INFO ident/radials/radius (`MfdFmsFplnFixInfo.tsx:95,147,185,225`).
Fields with a no-op handler (no message at all): dropdown free-text fields (`DropdownMenu.tsx:316`), OANS fields on the
ND, OIT fields. SURV squawk: FORMAT ERROR is replaced by `SQWK CODE NOT VALID` (`MfdSurvControls.tsx:288-298`).
ATCCOM D-ATIS airport field routes to the ATCCOM queue (`DAtisBlock.tsx:225`, section 10.8).
[PORT] Keep the per-field "details on/off" flag (`MfdFieldDesc.err_details`) so the port matches FBW; optionally enable
details everywhere later.

`FmsErrorType` [R]: enum in `fbw-a32nx/.../fmgc/src/FmsError.ts`; the A380X uses only the four members above. The port
defines its own enum (section 11); upstream numeric values are irrelevant.

---

## 2. Common primitives

### 2.1 String to number

FBW uses JS `Number(s)` in almost all formats, `Number.parseFloat` in QNH only.

**JS `Number(s)` (faithful)** - result is NaN unless the whole string, after trimming leading/trailing white space,
is one of:
- empty -> **0** (never reached: `''` is handled before);
- `[+-]?` followed by `Infinity`, or digits with optional `.` and optional fraction (`1.`, `.5`, `007` are valid; a lone
  `.` is not), optionally followed by an exponent `[eE][+-]?digits`;
- `0x`/`0X` hex, `0o`/`0O` octal, `0b`/`0B` binary integer **without** sign.

So `" 20"` = 20, `"1E2"` = 100, `"0X1F"` = 31, `"+5"` = 5, `"2 0"` = NaN, `"-"` = NaN.

**JS `parseFloat(s)`**: skips leading white space, parses the longest valid decimal prefix (`"29.92X"` = 29.92),
no hex; NaN when no prefix.

[PORT] Implement both: `mfd_js_number()` (faithful) and `mfd_strict_number()` (default). Strict grammar:
optional surrounding spaces, `^[+-]?([0-9]+(\.[0-9]*)?|\.[0-9]+)$`. Exponent/hex/`Infinity` inputs then give FORMAT
ERROR instead of FBW's accidental acceptance (e.g. `1E2` in a speed field). A build flag
`MFD_FMT_FAITHFUL_JS_NUMBER` selects the faithful parser for A/B testing.

### 2.2 Number to string

| JS | Semantics | C implementation |
|---|---|---|
| `v.toFixed(d)` | Exact binary value rounded to `d` decimals; exact ties round **away from zero** (`0.5`->`1`, `2.5`->`3`, `300.25`->`300.3`, `-2.5`->`-3`). Sign `-` is emitted iff `v < 0` (so `-0` -> `"0.0"`, but `-0.04` -> `"-0.0"`). `NaN`->`"NaN"`, `Infinity`->`"Infinity"`. | `mfd_js_to_fixed()`: print `fabs(v)` with `"%.*f"` at `d+25` digits; if the digits after position `d` are exactly `5` followed only by zeros (an exact tie), round the magnitude up manually; otherwise print with `"%.*f"` at `d`. Prefix `-` iff `v < 0`. (C `printf` alone rounds exact ties half-to-even and differs from JS.) |
| `v.toString()` | Shortest round-trip decimal (`250`, `0.84`, `-999`, `Infinity`) | `mfd_js_to_string()`: smallest `p` in 1..17 such that `"%.*g"` round-trips via `strtod`; if `%g` produced an exponent, re-print with `"%.*f"`. Integers: `"%.0f"`. |
| `s.padStart(n,'0')` | Left-pad to length n; never truncates; pads before a `-` sign (`"-5"`->`"0-5"`) | `mfd_pad_start()` (no sign awareness, same as JS). |
| `Math.round(v)` | `floor(v + 0.5)` (half toward +infinity) | `floor(v + 0.5)`; do **not** use C `round()`. |
| `String.replace(str, t)` | Replaces the **first** occurrence only | `mfd_str_replace_first()` |

### 2.3 Error detail strings (exact)

`DEF:11-32`:

```
FORMAT detail : "FORMAT: " + requiredFormat + (unit ? " " + unit : "")
RANGE  detail : "RNG: " + minText + " TO " + maxText + (unit ? " " + unit : "")
```

Message text shown = `MESSAGE + "\n" + detail` (`FMC:1149`). Example: `"ENTRY OUT OF RANGE\nRNG: 90 TO 340 KT"`.
Note the space before units including the degree sign: `"RNG: 0 TO 359 °"`.

### 2.4 Check-order patterns

| Pattern | Logic (after `''` -> null) | Formats |
|---|---|---|
| **P1** | `n = Number(in)`; if `!isNaN(n) && min<=n<=max` -> ok; else if `n>max or n<min` -> RANGE; else -> FORMAT. (NaN fails both comparisons, so NaN -> FORMAT.) | most numeric formats |
| **P2** | `n = Number(in)`; if NaN -> FORMAT; if in range -> ok; else RANGE | SpeedKnots, Weight, RNP |
| **P3** | as P1 but RANGE is *plain* (no details) | AltitudeOrFL, Altitude, FL, Length, VerticalSpeed, TimeHHMM(SS), Latitude |

Range comparisons are inclusive on both ends everywhere.

### 2.5 Rendering contract for `format()` output

(For the renderer; widget geometry is owned by the widget/layout spec.)

| Element | Font | Colour |
|---|---|---|
| value text, editable field | 27 px | cyan `#00ffff` |
| value text, value computed by FMS (`enteredByPilot=false`) | **22 px** | as state |
| value text, inactive field | 27 px | green `#00ff00` |
| value text, temporary flight plan field (`tmpyActive`) | 27 px | yellow `#ffff00` |
| value text, validating | 27 px | light grey `#c4c6cf` |
| value text, disabled | 27 px | `#868a9c` on `#272525` field background |
| mandatory empty value | 27 px | amber `#e68000`; every `-` of the placeholder replaced by `U+25AF` (white vertical rectangle) (`IF:375-377`) only when `mandatory && !inactive && !disabled` |
| leading unit (`format()[1]`) | 20 px (24 px if `bigUnit`) | dark blue `#0c66f7`, 6 px right margin |
| trailing unit (`format()[2]`) | 20 px (24 px if `bigUnit`) | dark blue `#0c66f7`, 6 px left margin |

`U+25AF` exists in the SlashedZero TTF (`uni25AF`) but **not** in the port's current EWD atlas (`a380x_ewd_font.h`
covers ASCII 32-126, 176, 255). [PORT] Draw it as an outlined box in the 0.627 em cell, or regenerate the atlas with
`U+25AF`. Use UTF-8 in C strings; `°` = `"\xC2\xB0"`.

---

## 3. Format catalogue (summary)

`Used` = instantiated in A380X MFD pages. Ranges are FBW defaults; `[dyn]` = the caller passes min/max (section 6).

| # | Class (`DEF:` line) | Placeholder | maxDigits (+ovf) | Lead / trail unit | Default range | Stored value | Used |
|---|---|---|---|---|---|---|---|
| 1 | SpeedKnotsFormat (67) | `---` | 3 | - / `KT` | 0..+inf [dyn] | kt | yes |
| 2 | SpeedMachFormat (118) | `.--` | 3 | - / - | 0..+inf [dyn] | Mach | yes |
| 3 | AltitudeOrFlightLevelFormat (169) | `-----` | 5 | `FL`/- or -/`FT` | 0..43000 [dyn] | ft | yes |
| 4 | AltitudeFormat (247) | `-----` | 5 | - / `FT` | 0..43000 [dyn] | ft | yes |
| 5 | FlightLevelFormat (300) | `---` | 3 (+2) | `FL` / - | 0..430 [dyn] | FL (ft/100) | yes |
| 6 | RadioAltitudeFormat (361) | `-----` | 5 | - / `FT` (none for NO DH) | 1..43000 [dyn] | ft; 0 = NO DH | yes |
| 7 | TropoFormat (415) | `-----` | 5 | - / `FT` | 1000..60000 | ft | yes |
| 8 | LengthFormat (452) | `----` (n dashes) | 4 (ctor arg) | - / `M` or `FT` | 0..+inf [dyn] | m | yes |
| 9 | WeightFormat (505) | `---.-` | 5 | - / `T` or `KLB` | 0..+inf [dyn] | kg | yes |
| 10 | PercentageFormat (566) | `--.-` | 4 | - / `%` | 0..+inf [dyn] | % | yes |
| 11 | TemperatureFormat (618) | `---` | 3 | - / `°C` | 0..+inf [dyn] | °C | yes |
| 12 | CrzTempFormat (671) | `---` | 3 | - / `°C` | -99..99 | °C | yes |
| 13 | WindDirectionFormat (716) | `---` | 3 | - / `°` | 0..359 | deg | yes |
| 14 | WindSpeedFormat (753) | `---` | 3 | - / `KT` | 0..250 | kt | yes |
| 15 | TripWindFormat (790) | `-----` | 5 | `TL`/`HD` / - | -250..250 | kt, + = tail | yes |
| 16 | QnhFormat (852) | `----` or `--.--` | 5 | - / - | 745..1100 hPa or 22.00..32.48 inHg | hPa or inHg (ambiguous) | yes |
| 17 | CostIndexFormat (931) | `---` | 3 | - / - | 0..999 | CI | yes |
| 18 | VerticalSpeedFormat (966) | `---` | 4 | - / `FT/MN` | 0..+inf [dyn] | ft/min | N/A |
| 19 | DescentRateFormat (1016) | `----` | 4 | - / `FT/MN` | [dyn] | ft/min (negative) | yes |
| 20 | FixFormat (1071) | `-------` | 7 | - / - | - | string in, Fix out | yes |
| 21 | AirportFormat (1099) | `----` | 4 | - / - | length 4 | string | yes |
| 22 | NavaidIdentFormat (1126) | `----` (ctor) | 4 | - / - | - | string | yes |
| 23 | AirwayFormat (1147) | `---` | 5 | - / - | - | string | yes |
| 24 | DropdownFieldFormat (1168) | n dashes | n (ctor) | - / - | - | string | yes |
| 25 | WaypointFormat (1194) | `-------` | 7 | - / - | - | string | yes |
| 26 | LongAlphanumericFormat (1215) | `----------` | 10 | - / - | - | string | yes |
| 27 | PaxNbrFormat (1236) | `---` | 3 | - / - | 0..999 | count | yes |
| 28 | TimeHHMMFormat (1272) | `--:--` | 4 | - / - | 0..90 min | minutes | yes |
| 29 | TimeHHMMSSFormat (1328) | `--:--:--` | 6 | - / - | 0..86400 s | seconds | yes |
| 30 | LatitudeFormat (1389) | `----.--` | 7 | - / - | -90..90 | deg | N/A |
| 31 | HeadingFormat (1422) | `---.-` | 5 | - / `°T` | 0..+inf [dyn] | deg true | yes |
| 32 | InboundCourseFormat (1477) | `---` | 4 | - / `°` | 0..+inf [dyn] | deg | yes |
| 33 | HoldDistFormat (1527) | `--.-` | 4 | - / `NM` | 0..+inf [dyn] | NM | yes |
| 34 | HoldTimeFormat (1577) | `-.-` | 3 | - / `MN` | 0..+inf [dyn] | min | yes |
| 35 | FrequencyILSFormat (1627) | `---.--` | 6 | - / - | 108.00..111.95 | MHz | yes |
| 36 | FrequencyVORDMEFormat (1662) | `---.--` | 6 | - / - | 108.00..117.95 | MHz | yes |
| 37 | FrequencyADFFormat (1697) | `----.-` | 6 | - / - | 190.0..1750.0 | kHz | N/A |
| 38 | LsCourseFormat (1734) | `----` | 4 | - / `°` | -360..360 | deg; negative = back course | yes |
| 39 | SquawkFormat (1793) | `----` | 4 | - / - | octal digits | decimal-digit integer | yes |
| 40 | RadialFormat (1821) | `---` | 4 | - / `°` (none when null) | 0..360 | deg | yes |
| 41 | RadiusFormat (1863) | `----` | 4 | - / `NM` (none when null) | 1..9999 | NM | yes |
| 42 | RnpFormat (1902) | `--.-` | 4 | - / `NM` (none when null) | 0.01..20.0 | NM | yes |
| 43 | FuelPenaltyPercentFormat (1939) | `+000.0` | 6 | - / `%` | 0..999.9 | % | yes |

Constants (`PERF:`): `Vmo = 340` kt, `Mmo = 0.89`, `maxCertifiedAlt = 43000` ft, `minZfw = 270000` kg,
`maxZfw = 373000`, `minZfwCg = 23` %, `maxZfwCg = 48` %, `maxBlockFuel = 289982`, `maxTaxiFuel = 9889`,
`maxRteRsvFuelPerc = 15`, `maxJtsnGw = maxGw = 512000`, `maxAltnFuel = 289982`, `maxFinalFuel = 100018`,
`maxMinDestFuel = 250000`. AMI (`fbw-a380x/src/systems/shared/src/AirlineModifiableInformation.tsx:8-9`):
`rsvMin = 0`, `rsvMax = 50000` kg.

Constructor-default trap [FBW]: classes declare a field default (e.g. `HeadingFormat.maxValue = 360.0`) but the
constructor's default subscribable (`Number.POSITIVE_INFINITY`) overwrites it immediately. The effective default is the
constructor default; the table above lists effective values.

---

## 4. Format specifications

### 4.1 Simple numeric formats (pattern P1/P2)

All of these: `''` -> null; parse with `Number()`; checks per pattern; value stored unchanged. "Fmt" = required-format
string for the FORMAT detail. "Rng text" = how min/max are printed in the RANGE detail. "Out" = `format()` output when
the value is non-null (null -> `[placeholder, lead, trail]` as in section 3).

| Format | Pat | Fmt / unit in detail | Rng text | Out (value text) | Out units when null |
|---|---|---|---|---|---|
| SpeedKnots | P2 | `XXX` / `KT` | `toString` | `toString(v)` | trail `KT` |
| Altitude | P3 | `XXXXX` / `FT` | (plain) | `toFixed(v,0)` | trail `FT` |
| Percentage | P1 | `XX.X` / `%` | `toFixed(1)` | `toFixed(v,1)` | trail `%` |
| Temperature | P1 | `+/-XX` / `°C` | `toString` | `v>=0 ? "+"+toFixed(v,0) : toFixed(v,0)` | trail `°C` |
| WindDirection | P1 | `XXX` / `°` | `toString` | `padStart(toFixed(v,0),3,'0')` | trail `°` |
| WindSpeed | P1 | `XXX` / `KT` | `toString` | `padStart(toFixed(v,0),3,'0')` | trail `KT` |
| CostIndex | P1 | `XXX` / - | `toString` | `toString(v)` | none |
| VerticalSpeed [N/A] | P3 | `+/-XXXX` / `FT/MN` | (plain) | `toString(v)` | trail `FT/MN` |
| PaxNbr | P1 | `XXX` / - | `toString` | `toFixed(v,0)` | none |
| Latitude [N/A] | P3 | (plain FORMAT, no details) | (plain) | `toFixed(v,0)` | none |
| Heading | P1 | `XXX.X` / `°` | min `padStart(toFixed(1),4,'0')`, max `toFixed(1)` | `toFixed(v,1)`, trail **`°T`** | trail `°T` |
| InboundCourse | P1 | `XXX` / `°` | `toFixed(0)` | `toFixed(v,0)` (no padding) | trail `°` |
| HoldDist | P1 | `XX.X` / `NM` | `toFixed(1)` | `toFixed(v,1)` | trail `NM` |
| HoldTime | P1 | `X.X` / `MN` | `toFixed(1)` | `toFixed(v,1)` | trail `MN` |
| FrequencyILS | P1 | `XXX.XX` / - | `toFixed(2)` | `toFixed(v,2)` | none |
| FrequencyVORDME | P1 | `XXX.XX` / - | `toFixed(2)` | `toFixed(v,2)` | none |
| FrequencyADF [N/A] | P1 | `XXXX.X` / - | `toFixed(1)` | `toFixed(v,1)` | none |
| Radial | P1 | `XXX` / `°` | min `toString`, max `toFixed(0)` | `padStart(toFixed(v,0),3,'0')` + trail `°` | **none** |
| Radius | P1 | `XXXX` / `NM` | `toString` | `toFixed(v,0)` + trail `NM` | **none** |
| Rnp | P2 | `X.XX` / `NM` | min `toFixed(2)`, max `toFixed(1)` | `v > 10.0 ? toFixed(v,1) : toFixed(v,2)` + trail `NM` | **none** |

Notes:
- **SpeedKnots** `DEF:67-116`: `maxDigits` is a public mutable field (3).
- **Heading** `DEF:1422-1474`: the only caller (POSITION/IRS SET HDG, `MfdFmsPositionIrs.tsx:347`) uses the defaults
  0..+inf, so a negative entry yields `RNG: 00.0 TO Infinity °` and 400 is accepted. [PORT] range 0.0..359.9
  (`RNG: 00.0 TO 359.9 °`).
- **InboundCourse** `DEF:1477-1525`: used with defaults (HOLD inbound course, VOR1/2 course): `RNG: 0 TO Infinity °`.
  [PORT] range 0..360, render padded to 3 digits like `RadialFormat` (`090°`), keep FBW un-padded only if a reference
  screenshot shows otherwise.
- **HoldDist / HoldTime** `DEF:1527-1625`: used with defaults; FBW range texts become `RNG: 0.0 TO Infinity NM/MN`.
  [PORT] HoldDist 0.0..99.9 NM, HoldTime 0.0..9.9 MN (the class-field values FBW evidently intended).
- **Frequencies**: no channel-spacing check (any decimal accepted, e.g. 109.33). [PORT] keep (real FMS tolerates; the
  radio rounds).
- **Rnp** `DEF:1902-1937`: exactly 10.0 renders `10.00` (5 chars, `>` not `>=`).
- **Radial/Radius/Rnp** render no unit while null.

Test vectors (KCCU-typable inputs; `->` result; `E:` message text with `\n`):

| Format (range) | Input | Result |
|---|---|---|
| SpeedKnots (90..340) | `140` | 140 -> `140` `KT` |
| | `80` | E: `ENTRY OUT OF RANGE\nRNG: 90 TO 340 KT` |
| | `AB` | E: `FORMAT ERROR\nFORMAT: XXX KT` |
| | `1E2` | [FBW] 100 accepted; [PORT strict] FORMAT ERROR |
| Altitude (1..43000) | `5000` | 5000 -> `5000` `FT` |
| | `50` | 50 (no x100 in this format) |
| | `45000` | E: `ENTRY OUT OF RANGE` |
| | `AB` | E: `FORMAT ERROR\nFORMAT: XXXXX FT` |
| Percentage ZFWCG (23..48) | `30` | `30.0` `%` |
| | `50` | E: `ENTRY OUT OF RANGE\nRNG: 23.0 TO 48.0 %` |
| Temperature FLEX (0..99) | `50` | `+50` `°C` |
| | `-5` | E: `ENTRY OUT OF RANGE\nRNG: 0 TO 99 °C` |
| | `A` | E: `FORMAT ERROR\nFORMAT: +/-XX °C` |
| Temperature APPR OAT (-99..99) | `-15` | `-15` `°C` |
| WindDirection | `45` | `045` `°` |
| | `360` | E: `ENTRY OUT OF RANGE\nRNG: 0 TO 359 °` |
| WindSpeed | `5` | `005` `KT` |
| | `300` | E: `ENTRY OUT OF RANGE\nRNG: 0 TO 250 KT` |
| CostIndex | `50` | `50` |
| | `-1` | E: `ENTRY OUT OF RANGE\nRNG: 0 TO 999` |
| | `A` | E: `FORMAT ERROR\nFORMAT: XXX` |
| Heading (defaults) | `123.4` | `123.4` `°T` |
| | `-5` | [FBW] E: `ENTRY OUT OF RANGE\nRNG: 00.0 TO Infinity °` |
| HoldTime | `1.5` | `1.5` `MN` |
| FrequencyILS | `109.5` | `109.50` |
| | `112` | E: `ENTRY OUT OF RANGE\nRNG: 108.00 TO 111.95` |
| | `AB` | E: `FORMAT ERROR\nFORMAT: XXX.XX` |
| FrequencyVORDME | `118` | E: `ENTRY OUT OF RANGE\nRNG: 108.00 TO 117.95` |
| Radial | `45` | `045` `°` |
| | `361` | E: `ENTRY OUT OF RANGE\nRNG: 0 TO 360 °` |
| Radius | `0` | E: `ENTRY OUT OF RANGE\nRNG: 1 TO 9999 NM` |
| Rnp | `0.3` | `0.30` `NM` |
| | `11` | `11.0` `NM` |
| | `25` | E: `ENTRY OUT OF RANGE\nRNG: 0.01 TO 20.0 NM` |
| | `A` | E: `FORMAT ERROR\nFORMAT: X.XX NM` |

### 4.2 SpeedMachFormat (`DEF:118-167`)

- Purpose: Mach targets (PERF CRZ preselected Mach, DES managed Mach). Callers: min 0.1, max `Mmo` 0.89.
- Placeholder `.--`; maxDigits 3; no units.
- Parse [FBW]:
  1. `''` -> null.
  2. `n = Number(in)`.
  3. `if (n > Mmo && !in.search('.'))` -> `n = Number("0." + in)`. `String.search('.')` treats `.` as a regex
     "any character", returns 0 for any non-empty input, so the condition is simply **`n > 0.89`** (constant `Mmo`, not
     the caller's max).
  4. In range -> ok. Else if out of range -> RANGE `RNG: <min toString> TO <max toString>` (no unit). Else FORMAT
     `FORMAT: XX`.
- Format: `!v` (null or 0) -> placeholder. Else `"." + (toFixed(v,2) after the ".")`, e.g. 0.84 -> `.84`.

| Input | n | Result |
|---|---|---|
| `84` | 84 -> 0.84 | 0.84 -> `.84` |
| `.84` | 0.84 | 0.84 |
| `8` | 8 -> 0.8 | 0.8 -> `.80` |
| `1` | 1 -> 0.1 | 0.1 -> `.10` |
| `100` | 100 -> 0.100 | 0.1 |
| `9` | 9 -> 0.9 | E: `ENTRY OUT OF RANGE\nRNG: 0.1 TO 0.89` |
| `05` | 5 -> 0.05 | E: RANGE (as above) |
| `089` | 89 -> 0.089 | E: RANGE |
| `0` | 0 | E: RANGE |
| `.95` | 0.95 -> `Number("0..95")` NaN | [FBW] E: `FORMAT ERROR\nFORMAT: XX` |
| `1.2` | 1.2 -> NaN | [FBW] E: FORMAT |

[PORT] If the input has no `.` and `n > 0.89`, reinterpret as `"0." + digits`; if it contains `.`, do not reinterpret
(so `.95` gives RANGE, not FORMAT). Mark as deliberate deviation.

### 4.3 AltitudeOrFlightLevelFormat (`DEF:169-245`)

- Purpose: altitudes that may be entered/displayed as FL: THR RED, ACCEL, EO ACCEL, NOISE END, PRED TO, speed-limit
  altitude, altitude constraints, step altitudes, missed-approach altitudes.
- Constructor: `(transAlt: number|null source, isTransAltFlightLevel = false, min = 0, max = 43000)`.
  Effective transition altitude in ft = `isFl ? transAlt*100 : transAlt` (used with `transFl` on DES/GA pages).
- Placeholder `-----`; maxDigits 5.
- Parse: `''` -> null; `n = Number(in)`; **if `len(in) <= 3`: `n = Number(in) * 100`** (FL shorthand); NaN -> FORMAT
  `FORMAT: FOR ALT XXXXX FOR FL XXX` (no unit); out of `[min,max]` -> **plain** RANGE; else n (ft).
- Format (value ft):
  - null -> `[placeholder, -, "FT"]`.
  - If transition known and (`!isFl && v > T` or `isFl && v >= T`): `["FL", padStart(toFixed(v/100,0),3,'0')]` as
    **leading** unit, no trailing unit -> e.g. lead `FL`, value `350`.
  - Else `[toFixed(v,0), -, "FT"]`.
- [FBW] quirk: the transition subscription is attached without initial notification (`DEF:195-204`); the field shows
  FT until the transition value changes once after construction. [PORT] Apply the transition immediately.

| Input (T = 5000 ft, alt) | Result |
|---|---|
| `350` | 35000 -> lead `FL`, `350` |
| `35` | 3500 -> `3500` `FT` |
| `5000` | 5000 -> `5000` `FT` (5000 is not > 5000) |
| `5100` | 5100 -> `FL` `051` |
| `440` | 44000 -> E: `ENTRY OUT OF RANGE` |
| `-50` | -5000 -> E: `ENTRY OUT OF RANGE` |
| `FL350` | NaN -> E: `FORMAT ERROR\nFORMAT: FOR ALT XXXXX FOR FL XXX` |

With `isFl` true and transFl = 50: value 5000 -> `FL050`.

### 4.4 FlightLevelFormat (`DEF:300-358`)

- Purpose: CRZ FL (INIT, PERF CRZ), TRANS FL (PERF APPR).
- Placeholder `---`; maxDigits 3 + overflow 2 (field widens to accept 5 chars, e.g. `FL350`); leading unit `FL`.
- Value unit is **FL** (feet/100). The JSDoc "Unit of value: Feet" at `DEF:297-299` is wrong.
- Range: 0..430 (ctor default `maxCertifiedAlt/100`).
- Parse: `''` -> null; `n = Number(in)`; if NaN and `in` matches `^FL([0-9]{1,3})$` (case-insensitive) -> n = digits;
  out of range -> **plain** RANGE; NaN -> FORMAT `FORMAT: FL XXX`; else n (decimals kept).
- Format: null -> `[placeholder, "FL", -]`; else `[padStart(toFixed(Math.round(v),0),3,'0'), "FL", -]`.
- Downstream (INIT CRZ FL) `trySetCruiseFl` (`FMC:1804-1852`) additionally rejects `fl <= 0` or `fl > 430` with
  `ENTRY OUT OF RANGE` and non-finite with `FORMAT ERROR`.

| Input | Result |
|---|---|
| `350` / `FL350` | 350 -> `FL` `350` |
| `45` | 45 -> `FL` `045` |
| `450` | E: `ENTRY OUT OF RANGE` |
| `35000` | E: `ENTRY OUT OF RANGE` (no feet conversion) |
| `FLX` | E: `FORMAT ERROR\nFORMAT: FL XXX` |
| `350.6` | [FBW] 350.6 stored, shows `351`; [PORT] FORMAT ERROR (integer FL only) |

### 4.5 RadioAltitudeFormat (`DEF:360-413`)

- Purpose: PERF APPR RADIO minimum (DH). Placeholder `-----`; maxDigits 5; range 1..43000 (ctor default min 1).
- `RADIO_ALTITUDE_NODH_VALUE = 0` (`DEF:360`).
- Parse: `''` -> null; exact `NO DH`, `NODH`, `NONE`, `NO` -> 0; else P1 with **plain** errors (`FmsError`, no details;
  FIXME at `DEF:402`).
- Format: null -> `[placeholder, -, "FT"]`; 0 -> `["NO DH", -, -]`; else `[toFixed(v,0), -, "FT"]`.

| Input | Result |
|---|---|
| `200` | `200` `FT` |
| `NO`, `NODH`, `NONE`, `NO DH` | 0 -> `NO DH` |
| `0` | E: `ENTRY OUT OF RANGE` |
| `ABC` | E: `FORMAT ERROR` |

### 4.6 TropoFormat (`DEF:415-450`)

- Purpose: INIT TROPO. Placeholder `-----`; maxDigits 5; trailing `FT`; range 1000..60000 (fixed).
- Parse: `''` -> null; `n = len(in) <= 3 ? Number(in)*100 : Number(in)`; P1 with **plain** RANGE (`FmsError`) and FORMAT
  `FORMAT: FOR ALT XXXXX FOR FL XXX` (no unit).
- Format: `toFixed(v,0)` + `FT`.

| Input | Result |
|---|---|
| `360` | 36000 |
| `36090` | 36090 |
| `5` | 500 -> E: `ENTRY OUT OF RANGE` |
| `ABC` | E: `FORMAT ERROR\nFORMAT: FOR ALT XXXXX FOR FL XXX` |

### 4.7 LengthFormat (`DEF:452-503`)

- Purpose: PERF T.O SHIFT (MFD); OANS threshold/end shift and required stopping distance (ND OANS panel).
- Constructor `(min_m, max_m, unitFamily = METER, maxDigits = 4)`. Placeholder = `maxDigits` dashes (TS legacy class-field
  order, target ES2017: parameter properties are assigned before field initialisers, so `'-'.repeat(4)` = `----`).
- Display unit: `M` if unit is metre else `FT` (`DEF:49-51`). Unit selected by setting `CONFIG_USING_METRIC_UNIT`
  (`MfdFmsPerf.tsx:744-746`).
- Stored value: **metres**.
- Parse: `''` -> null; `n_m = Number(in)` converted from display unit to metres (`FT`: `* 0.3048`); P3: plain RANGE;
  FORMAT `FORMAT: XXXX M` or `FORMAT: XXXX FT`.
- Format: null -> `[placeholder, -, unit]`; else `[toFixed(convert_m_to_display(v),0), -, unit]`.
- T.O SHIFT range: min 1 m, max = origin runway length in m, or +inf if none (`MfdFmsPerf.tsx:347,776,1439`). OANS:
  0..4000 m; required stopping distance has maxDigits 5.

| Input | Unit | Result |
|---|---|---|
| `500` | M | 500 m -> `500` `M` |
| `1000` | FT | 304.8 m -> `1000` `FT` |
| `0` | M | E: `ENTRY OUT OF RANGE` |
| `AB` | FT | E: `FORMAT ERROR\nFORMAT: XXXX FT` |

### 4.8 WeightFormat (`DEF:505-564`)

- Purpose: all FUEL&LOAD weights (ZFW, BLOCK, TAXI, RTE RSV, ALTN, FINAL, JTSN GW, MIN FUEL AT DEST).
- Constructor `(min_kg = 0, max_kg = +inf, unitFamily = KILOGRAM)`. Unit selected by `CONFIG_USING_METRIC_UNIT`
  (`MfdFmsFuelLoad.tsx:59-61`). Display label: `T` (tonnes) for kg, `KLB` (thousand pounds) for lb (`DEF:53-55`).
- Placeholder `---.-`; maxDigits 5. Stored value **kg**.
- Parse (P2): `''` -> null; `kg = convert_display_to_kg(Number(in)) * 1000`; NaN -> `FORMAT: XXX.X T` /
  `FORMAT: XXX.X KLB`; out of range -> `RNG: <lo> TO <hi> T|KLB` where lo/hi = `toFixed(convert_kg_to_display(min)/1000, 1)`.
- Format: null -> `[placeholder, -, unit]`; else `[toFixed(convert_kg_to_display(v)/1000, 1), -, unit]`.

Range texts (computed, `T` / `KLB` with lb = 0.453592 kg):

| Field | min..max kg | Metric detail | Imperial detail |
|---|---|---|---|
| ZFW | 270000..373000 | `RNG: 270.0 TO 373.0 T` | `RNG: 595.2 TO 822.3 KLB` |
| BLOCK | 0..289982 | `RNG: 0.0 TO 290.0 T` | `RNG: 0.0 TO 639.3 KLB` |
| TAXI | 0..9889 | `RNG: 0.0 TO 9.9 T` | `RNG: 0.0 TO 21.8 KLB` |
| RTE RSV | 0..50000 | `RNG: 0.0 TO 50.0 T` | `RNG: 0.0 TO 110.2 KLB` |
| ALTN | 0..289982 | `RNG: 0.0 TO 290.0 T` | `RNG: 0.0 TO 639.3 KLB` |
| FINAL | 0..100018 | `RNG: 0.0 TO 100.0 T` | `RNG: 0.0 TO 220.5 KLB` |
| JTSN GW | 0..512000 | `RNG: 0.0 TO 512.0 T` | `RNG: 0.0 TO 1128.8 KLB` |
| MIN FUEL AT DEST | 0..250000 | `RNG: 0.0 TO 250.0 T` | `RNG: 0.0 TO 551.2 KLB` |

| Input | Unit | Result |
|---|---|---|
| `300` | T | 300000 kg -> `300.0` `T` |
| `250` | T | E: `ENTRY OUT OF RANGE\nRNG: 270.0 TO 373.0 T` (ZFW) |
| `661` | KLB | 299824.3 kg -> `661.0` `KLB` |
| `A` | T | E: `FORMAT ERROR\nFORMAT: XXX.X T` |

### 4.9 CrzTempFormat (`DEF:671-714`)

- Purpose: INIT CRZ TEMP. Placeholder `---`; maxDigits 3; trailing `°C`; range -99..99.
- Parse: `''` -> null; `n = Number(in)`; **if `n > 0` and `in[0] != '+'` -> `n = -n`** (unsigned entries are ISA-cold
  negative temperatures); P1; RANGE `RNG: -99 TO 99 C`; FORMAT `FORMAT: +/-XXX C` (unit is `C`, **not** `°C`).
- Format: `v >= 0 ? "+" + toFixed(v,0) : toFixed(v,0)` + `°C`.

| Input | Result |
|---|---|
| `50` | -50 -> `-50` |
| `+5` | 5 -> `+5` |
| `-50` | -50 |
| `0` | 0 -> `+0` |
| `100` | -100 -> E: `ENTRY OUT OF RANGE\nRNG: -99 TO 99 C` |
| `A` | E: `FORMAT ERROR\nFORMAT: +/-XXX C` |

### 4.10 TripWindFormat (`DEF:790-850`)

- Purpose: INIT TRIP WIND. Placeholder `-----`; maxDigits 5; range -250..250 kt; stored positive = tailwind.
- Parse: `''` -> null; prefix rules checked in this order:
  1. starts `HD` -> sign -1, number = rest;
  2. starts `-` or `H` -> sign -1, number = rest after 1 char;
  3. starts `TL` -> +1, rest;
  4. starts `+` or `T` -> +1, rest after 1 char;
  5. else +1, whole input.
  `Number(rest)` NaN -> FORMAT `FORMAT: +/-XXX` (no unit). `v = sign * number`; out of range -> `RNG: -250 TO 250 KT`.
  (Empty rest, e.g. `H`, is `Number('')` = 0 -> -0.)
- Format: null -> `[placeholder, -, -]`; `v >= 0` -> lead `TL`, else lead `HD`; value `padStart(toFixed(|v|,0),3,'0')`;
  no trailing unit.

| Input | Result |
|---|---|
| `HD20`, `H20`, `-20` | -20 -> lead `HD`, `020` |
| `TL20`, `T20`, `+20`, `20` | +20 -> lead `TL`, `020` |
| `HD 20` | -20 (space trimmed by `Number`) |
| `HD300` | E: `ENTRY OUT OF RANGE\nRNG: -250 TO 250 KT` |
| `X20` | E: `FORMAT ERROR\nFORMAT: +/-XXX` |

[FBW] round-trip bug: re-validation (ENT without edit) parses the value text only (`020`) and turns a headwind into
tailwind (section 7). [PORT] re-validate using `lead + value` (`HD020`).

### 4.11 QnhFormat (`DEF:852-929`) and QnhUtils

- Purpose: PERF APPR QNH. maxDigits 5; no units.
- Placeholder: `----` if hPa selected, `--.--` if inHg; switches with the constructor flag `isHpa`, which the PERF page
  sets on page load from the side's FCU baro unit (`MfdFmsPerf.tsx:749,1355`). Parse does **not** depend on it.
- Regexes: `QNH_REGEX_HPA = ^\d{1,4}$`; `QNH_REGEX_INCHES = ^\d{2}\.?\d{2}$` (4 digits with optional dot after 2).
- Limits: hPa 745..1100; inHg 22.00..32.48.
- Parse:
  1. `''` -> null.
  2. `parseFloat(in)` NaN -> FORMAT `FORMAT: XXXX`.
  3. If INCHES regex matches: inches = contains `.` ? value : value/100. If 22.00 <= inches <= 32.48 -> return inches.
     Else if the input contained `.` -> RANGE `RNG: 22.00 TO 32.48` (no unit). (No dot: fall through, `NNNN` may be hPa.)
  4. If HPA regex matches: out of 745..1100 -> RANGE `RNG: 745 TO 1100`; else return value (hPa).
  5. Else FORMAT `FORMAT: XXXX`.
- Stored value is **either** hPa (>= 745) or inHg (< 100). Consumers convert with `qnhToMillibar` (section 9.2).
- Format: null -> placeholder; `v < 745` -> `toFixed(v,2)` (inHg) else `toFixed(v,0)`.

| Input | Result |
|---|---|
| `1013` | 1013 hPa -> `1013` |
| `999` | 999 hPa |
| `2992` | 29.92 inHg -> `29.92` |
| `29.92` | 29.92 inHg |
| `2200` | 22.00 inHg |
| `2199` | inches 21.99 invalid, no dot -> hPa 2199 -> E: `ENTRY OUT OF RANGE\nRNG: 745 TO 1100` |
| `33.00`, `10.13` | E: `ENTRY OUT OF RANGE\nRNG: 22.00 TO 32.48` |
| `3300` | E: `ENTRY OUT OF RANGE\nRNG: 745 TO 1100` |
| `700` | E: `ENTRY OUT OF RANGE\nRNG: 745 TO 1100` |
| `29.9`, `101.3` | E: `FORMAT ERROR\nFORMAT: XXXX` |
| `ABC` | E: `FORMAT ERROR\nFORMAT: XXXX` |

Side effect on commit (`MfdFmsPerf.tsx:3132`): `L:A32NX_DESTINATION_QNH` (millibar) = `qnhToMillibar(v)`.
[PORT] Store QNH as a struct `{value, is_inhg}` internally but keep the "<100 means inHg" rule for any value exported to
other modules.

### 4.12 DescentRateFormat (`DEF:1016-1069`)

- Purpose: PERF DES CABIN RATE. Placeholder `----`; maxDigits 4; trailing `FT/MN`. Caller range -999..-100.
- Parse: `''` -> null; `n = Number(in)`; **if `n > 0` -> `n = -n`**; P1; RANGE `RNG: -999 TO -100 FT/MN`;
  FORMAT `FORMAT: -XXXX FT/MN`.
- Format: `toString(v)` + `FT/MN`.
- [FBW] Field defaults are min -inf/max 0 but the constructor defaults (0..+inf) win; irrelevant because the only
  caller passes -999/-100.

| Input | Result |
|---|---|
| `350` | -350 -> `-350` |
| `50` | -50 -> E: `ENTRY OUT OF RANGE\nRNG: -999 TO -100 FT/MN` |
| `1500` | -1500 -> E: RANGE |
| `0` | E: RANGE |
| `A` | E: `FORMAT ERROR\nFORMAT: -XXXX FT/MN` |

### 4.13 Text formats

| Format | Placeholder | maxDigits | Parse | Format | Ref |
|---|---|---|---|---|---|
| FixFormat | `-------` | 7 | `trim(in)` empty -> null; if `isPlaceFormat(in)` or `isRunwayFormat(in)` -> the string; else FORMAT `FORMAT: XXXXX`. Database lookup happens in the page handler (`NOT IN DATABASE` there). | value is a Fix: `fix.ident` | `DEF:1071-1097` |
| AirportFormat | `----` | 4 | `''` or `----` -> null; `len != 4` -> FORMAT `FORMAT: AAAA`; else the string (no letter check) | the string | `DEF:1099-1124` |
| NavaidIdentFormat | ctor, default `----`; `-` for the deselected-navaid list | 4 | `''` or placeholder -> null; else the string | the string | `DEF:1126-1145` |
| AirwayFormat | `---` | 5 | `''` or `---` -> null; else string | string | `DEF:1147-1166` |
| DropdownFieldFormat(n) | n dashes | n | `''` or placeholder -> null; else string | string | `DEF:1168-1192` |
| WaypointFormat | `-------` | 7 | `''` or placeholder -> null; else string | string | `DEF:1194-1213` |
| LongAlphanumericFormat | `----------` | 10 | `''` or placeholder -> null; else string | string | `DEF:1215-1234` |

`isPlaceFormat` / `isRunwayFormat` [R]: place = `^[A-Z0-9]{2,7}$`; runway = `^[A-Z]{4}[0-9]{2}[LCR]?$` (e.g. `EDDF25C`).

| Input (FixFormat) | Result |
|---|---|
| `ABCDE`, `EDDF25C` | the string |
| `A` | E: `FORMAT ERROR\nFORMAT: XXXXX` (FIX INFO shows `FORMAT ERROR` only; details dropped) |
| `   ` | null |

| Input (AirportFormat) | Result |
|---|---|
| `EDDF` | `EDDF` |
| `EDD` | E: `FORMAT ERROR\nFORMAT: AAAA` |
| `1234` | `1234` (lookup later fails with `NOT IN DATABASE`) |

### 4.14 TimeHHMMFormat (`DEF:1271-1323`)

- Purpose: FUEL&LOAD FINAL time. Stored **minutes**; range 0..90. Placeholder `--:--`; maxDigits 4.
- Parse:
  1. `''` -> null.
  2. `s = in` with the first `:` removed.
  3. `minutes = Number(last 2 chars of s)`; `hours = len(s) > 2 ? Number(s without last 2 chars) : 0`.
  4. `minutes < 0 or > 59 or hours < 0 or > 23` -> plain RANGE.
  5. `t = minutes + 60*hours`; P3: out of 0..90 -> plain RANGE; NaN -> FORMAT `FORMAT: HHMM`.
- Format: `!v` (null **or 0**) -> placeholder; else `HH:MM` with `HH = padStart(|floor(v/60)|,2,'0')`,
  `MM = padStart(toFixed(|v mod 60|,0),2,'0')`.

| Input | Result |
|---|---|
| `130` | 90 -> `01:30` |
| `45` | 45 -> `00:45` |
| `1:30` | 90 |
| `0` | 0 accepted, displayed `--:--` [FBW quirk] |
| `200` | 120 -> E: `ENTRY OUT OF RANGE` |
| `60` | E: `ENTRY OUT OF RANGE` |
| `AB` | E: `FORMAT ERROR\nFORMAT: HHMM` |

[PORT] Render 0 as `00:00` (use `has_value`, not truthiness).

### 4.15 TimeHHMMSSFormat (`DEF:1325-1387`)

- Purpose: F-PLN VERT REV RTA tab, estimated takeoff time (`MfdFmsFplnVertRev.tsx:1068`). Stored **seconds**;
  range 0..86400. Placeholder `--:--:--`; maxDigits 6.
- Parse [FBW]:
  1. `''` -> null.
  2. `s` = first `:` removed. **`len(s) < 4` -> null** (silently clears the field).
  3. `hours = Number(s[0..2])`, `minutes = Number(s[2..4])`, `seconds = len(s) > 4 ? Number(last 2) : 0`.
  4. Any of h > 23, m > 59, s > 59 or negative -> **null** (clears).
  5. `t = s + 60m + 3600h`; P3 (NaN -> FORMAT `FORMAT: HHMMSS`; RANGE plain).
- Format: `!v` -> placeholder; else `HH:MM:SS` (all `padStart(...,2,'0')`).

| Input | Result |
|---|---|
| `1230` | 45000 -> `12:30:00` |
| `123045` | 45045 -> `12:30:45` |
| `123` | [FBW] null (field cleared) |
| `2500` | [FBW] null (field cleared) |
| `ABCD` | E: `FORMAT ERROR\nFORMAT: HHMMSS` |

[PORT] Steps 2 and 4 raise errors instead: `len < 4` -> FORMAT `FORMAT: HHMMSS`; invalid h/m/s -> plain RANGE.
Render 0 as `00:00:00`.

### 4.16 LsCourseFormat (`DEF:1732-1791`)

- Purpose: POSITION/NAVAIDS LS course. Placeholder `----`; maxDigits 4; trailing `°`. Internal range -360..360,
  displayed min 0. Negative = back course.
- Parse [FBW]:
  1. `''` -> null.
  2. If `len(in) == 4`: if `in[0]` is `F` or `B` -> sign = (B ? -1 : +1), number = `in[1..4]`; else number =
     `in[0..3]` (**4th digit dropped**).
  3. If `in[0] == 'B'` -> FORMAT `FORMAT: FXXX °` (back course not implemented, FIXME `DEF:1776-1777`).
  4. `Number(number)` NaN -> FORMAT `FORMAT: FXXX °`.
  5. In -360..360 -> `sign * n`; else RANGE `RNG: 0 TO 360 °`.
- Format: `(v < 0 ? "B" : "F") + padStart(toFixed(|v|,0),3,'0')` + `°`.

| Input | Result |
|---|---|
| `109`, `F109` | 109 -> `F109` `°` |
| `B109` | E: `FORMAT ERROR\nFORMAT: FXXX °` |
| `1090` | [FBW] 109 (4th digit dropped) |
| `-5` | [FBW] -5 -> `B005` (sign key bypasses the B check) |
| `400` | E: `ENTRY OUT OF RANGE\nRNG: 0 TO 360 °` |
| `F09` | E: FORMAT |

[PORT] Accept `^F?[0-9]{1,3}$` only, range 0..360; `B...` and `-` -> FORMAT. `1090` -> FORMAT.

### 4.17 SquawkFormat (`DEF:1793-1816`)

- Purpose: SURV CONTROLS SQWK. Placeholder `----`; maxDigits 4; no units.
- Parse: `''` -> null; valid iff `Number(in)` not NaN **and** `in` matches `^[0-7]{4}$`; result = `Number(in)` (decimal
  integer whose digits are the code, e.g. `0123` -> 123). Else plain FORMAT (`A380FmsError`, no details) which the SURV
  page converts to message `SQWK CODE NOT VALID`.
- Format: `padStart(toFixed(v,0),4,'0')`.
- Write [FBW]: `K:XPNDR_SET = parseInt(String(v), 16)` (BCD). `v == 0` (code 0000) is falsy, so the handler returns
  `false` and the entry is silently rejected.
- [PORT] X-Plane: write the decimal-digit integer directly to `sim/cockpit2/radios/actuators/transponder_code`
  (code 7000 -> 7000). Accept 0000.

| Input | Result |
|---|---|
| `7000` | 7000 -> `7000` |
| `0123` | 123 -> `0123` |
| `7800`, `123` | message `SQWK CODE NOT VALID` |
| `0000` | [FBW] silently rejected; [PORT] accepted |

### 4.18 FuelPenaltyPercentFormat (`DEF:1939-1978`)

- Purpose: DATA/STATUS performance factor. Placeholder `+000.0` ("always exists even if cleared"); maxDigits 6;
  trailing `%`; range 0..999.9.
- Parse: `''` -> null; must match `^\+?[0-9]{1,3}(\.[0-9])?$` else FORMAT `FORMAT: +NNN.N %`; out of range ->
  `RNG: 0.0 TO 999.9 %` (unreachable after the regex).
- Format: `"+" + padStart(toFixed(v,1),5,'0')` + `%`.
- The field's handler drops details (`MfdFmsDataStatus.tsx:154`): message is `FORMAT ERROR` only.

| Input | Result |
|---|---|
| `5` | `+005.0` `%` |
| `+12.5` | `+012.5` |
| `12.55`, `-5` | E: `FORMAT ERROR` |

---

## 5. Waypoint / place entry syntax [R]

Used by DIR TO and INSERT NEXT WPT (dropdown free text, up to 24 chars), FIX INFO REF IDENT and POSITION/MONITOR
BRG/DIST TO (FixFormat, 7 chars: only place/runway fit). `WaypointEntryUtils.getOrCreateWaypoint(fms, text, stored)`
tries, in order:

| Order | Syntax | Rule | Result / errors |
|---|---|---|---|
| 1 | LAT/LON | `^([NS])?([0-9]{2,4}\.[0-9])([NS])?/([EW])?([0-9]{2,5}\.[0-9])([EW])?$`; lat = 2 deg digits + minutes, lon = 3 deg digits + minutes; hemisphere letter before or after | lat deg > 90, lon deg > 180, minutes > 60 -> ENTRY OUT OF RANGE; creates a pilot lat/lon waypoint (`createLatLonWaypoint`) |
| 2 | PBX place-bearing/place-bearing | `^(P1)-(B1)/(P2)-(B2)$`, P = place format, B = `[0-9]{1,3}(\.[0-9])?` deg magnetic | B > 360 -> RANGE; P not found -> NOT IN DATABASE |
| 3 | PBD place/bearing/distance | `^(P)/(B)/(D)$`, B as above (magnetic), D = `[0-9]{1,3}(\.[0-9])?` NM | B > 360 or D > 999.9 -> RANGE |
| 4 | Place | `^[A-Z0-9]{2,7}$`; runway form `^[A-Z]{4}[0-9]{2}[LCR]?$` resolves runway threshold | not found -> `createNewWaypoint` (A380X: returns undefined, `MFD.tsx:205`; callers then show `NOT IN DATABASE` or nothing) ; duplicates -> DUPLICATE NAMES window |
| - | anything else | | FORMAT ERROR |

A380X call-site behaviour: FIX INFO and MONITOR throw `NotInDatabase` when the result is undefined
(`MfdFmsFplnFixInfo.tsx:83-86`, `MfdFmsPositionMonitor.tsx:637-639`); INSERT NEXT WPT routes caught `FmsError` types to
`showFmsErrorMessage` (`InsertNextWptFrom.tsx:86-99`); DIR TO ignores failures (`MfdFmsFplnDirectTo.tsx:117-128`).
[PORT] Implement the four syntaxes in the navdata layer (not in the format), returning `MfdFmsError`.

---

## 6. Where each format is used (A380X MFD)

| Page / field | Format | Range passed (storage units) | Ref |
|---|---|---|---|
| INIT FLT NBR, CPNY RTE, ALTN RTE | LongAlphanumeric | - | `MfdFmsInit.tsx:388,480,501` |
| INIT FROM / TO / ALTN | Airport | - | `MfdFmsInit.tsx:425,441,457` |
| INIT CRZ FL | FlightLevel | 0..430 | `MfdFmsInit.tsx:521` |
| INIT CRZ TEMP | CrzTemp | -99..99 | `:538` |
| INIT TROPO | Tropo | 1000..60000 | `:582` |
| INIT CI | CostIndex | 0..999 | `:609` |
| INIT TRIP WIND | TripWind | -250..250 | `:627` |
| FUEL&LOAD ZFW | Weight | 270000..373000 | `MfdFmsFuelLoad.tsx:465` |
| FUEL&LOAD ZFWCG | Percentage | 23..48 | `:484` |
| FUEL&LOAD BLOCK | Weight | 0..289982 | `:505` |
| FUEL&LOAD TAXI | Weight | 0..9889 | `:544` |
| FUEL&LOAD RTE RSV (wt / %) | Weight / Percentage | 0..50000 / 0..15 | `:578,609` |
| FUEL&LOAD ALTN / FINAL | Weight | 0..289982 / 0..100018 | `:635,660` |
| FUEL&LOAD FINAL time | TimeHHMM | 0..90 min | `:684` |
| FUEL&LOAD PAX NBR | PaxNbr | 0..999 | `:711` |
| FUEL&LOAD CI | CostIndex | 0..999 | `:749` |
| FUEL&LOAD JTSN GW | Weight | 0..512000 | `:775` |
| FUEL&LOAD MIN FUEL AT DEST | Weight | 0..250000 | `:868` |
| PERF CRZ FL | FlightLevel | 0..430 | `MfdFmsPerf.tsx:1369` |
| PERF T.O SHIFT | Length | 1..rwy length m | `:1439` |
| PERF V1 / VR / V2 | SpeedKnots | 90..340 | `:1462,1538,1570` |
| PERF FLEX | Temperature | 0..99 | `:1641` |
| PERF T.O THS FOR | Percentage | 0..99.9 | `:1732` |
| PERF THR RED / ACCEL / NOISE END / EO ACCEL (T.O, and duplicate block) | AltitudeOrFL(transAlt) | 0..43000 | `:1815,1896,1979,2027,2256,2344,2425` |
| PERF NOISE N1 | Percentage | 40..110 | `:1850,2291` |
| PERF NOISE SPD, CLB preselected SPD, CRZ preselected SPD, DES managed SPD, APPR VAPP | SpeedKnots | 90..340 | `:1931,2174,2379,2644,2935,3260` |
| PERF TRANS ALT | Altitude | 1..43000 | `:2006,2474` |
| PERF CI (CLB, CRZ, DES) | CostIndex | 0..999 | `:2088,2528,2825` |
| PERF CLB PRED TO | AltitudeOrFL(transAlt) | 0..43000 | `:2142` |
| PERF CRZ preselected Mach, DES managed Mach | SpeedMach | 0.1..0.89 | `:2617,2908` |
| PERF DES CABIN RATE | DescentRate | -999..-100 | `:2849` |
| PERF DES PRED TO | AltitudeOrFL(transFl, isFl) | 0..43000 | `:2881` |
| PERF APPR MAG WIND dir / spd | WindDirection / WindSpeed | 0..359 / 0..250 | `:3051,3067` |
| PERF APPR OAT | Temperature | -99..99 | `:3100` |
| PERF APPR QNH | Qnh | fixed | `:3121` |
| PERF APPR BARO | Altitude | 0..43000 | `:3150` |
| PERF APPR RADIO | RadioAltitude | 1..43000 | `:3173` |
| PERF APPR TRANS FL | FlightLevel | 0..430 | `:3290` |
| PERF GA THR RED / ACCEL / EO ACCEL | AltitudeOrFL(transFl*100) | 0..43000 | `:3353,3377,3399` |
| DATA AIRPORT ARPT IDENT | Airport | - | `MfdFmsDataAirport.tsx:242` |
| DATA STATUS perf factor | FuelPenaltyPercent | 0..999.9 | `MfdFmsDataStatus.tsx:148` |
| F-PLN destination window | Airport | - | `DestinationWindow.tsx:88` |
| AIRWAYS VIA / TO | Airway / Waypoint | - | `MfdFmsFplnAirways.tsx:216,262` |
| FIX INFO REF IDENT / RADIAL x2 / RADIUS | Fix / Radial / Radius | - / 0..360 / 1..9999 | `MfdFmsFplnFixInfo.tsx:96,148,186,226` |
| HOLD INBOUND CRS / LEG TIME / LEG DIST | InboundCourse / HoldTime / HoldDist | defaults (0..+inf) | `MfdFmsFplnHold.tsx:202,233,243` |
| VERT REV RTA est. takeoff time | TimeHHMMSS | 0..86400 | `MfdFmsFplnVertRev.tsx:1068` |
| VERT REV SPD CSTR, CLB SPD LIM speed | SpeedKnots | 90..340 | `:1106,1155` |
| VERT REV SPD LIM altitude | AltitudeOrFL(limit transition) | 0..43000 | `:1169` |
| VERT REV ALT CSTR | AltitudeOrFL(cstr transition) | 0..43000 | `:1252` |
| VERT REV CRZ step altitudes | AltitudeOrFL(transAlt) | 0..43000 | `:1383` |
| POSITION IRS SET HDG | Heading | defaults | `MfdFmsPositionIrs.tsx:347` |
| POSITION MONITOR RNP | Rnp | 0.01..20.0 | `MfdFmsPositionMonitor.tsx:420` |
| POSITION MONITOR BRG/DIST TO | Fix | - | `:632` |
| NAVAIDS VOR1/2 ident, freq, crs | NavaidIdent / FrequencyVORDME / InboundCourse | - / fixed / defaults | `MfdFmsPositionNavaids.tsx:375-459` |
| NAVAIDS deselected list (6) | NavaidIdent(`-`) | - | `:552-613` |
| NAVAIDS LS ident, freq, crs | NavaidIdent / FrequencyILS / LsCourse | - / fixed / fixed | `:639,653,667` |
| SURV SQWK | Squawk | - | `MfdSurvControls.tsx:282` |
| ATCCOM D-ATIS airport | Airport | - | `DAtisBlock.tsx:220` |
| Every dropdown with text entry | DropdownField(n): n = 1,3,4,7,8,10 | - | `DropdownMenu.tsx:303` |

Dynamic inputs a C page must supply at parse/render time: `min`, `max` (e.g. runway length), metric flag
(`CONFIG_USING_METRIC_UNIT`), transition altitude/level + is-FL flag, QNH placeholder flag.

---

## 7. Round-trip (ENT without edit) and ESC behaviour

Because ENT on an unchanged field re-parses `format(v)[0]` (1.3), the value may change:

| Format | Formatted text | Re-parse result | Effect [FBW] | [PORT] |
|---|---|---|---|---|
| TripWind | `020` (HD in lead unit) | +20 | **headwind becomes tailwind** | re-validate `lead+value` |
| Weight | `300.3` | 300300 kg | quantised to 100 kg | skip re-parse when unchanged |
| Length (FT) | `328` | 99.97 m | quantised to 1 ft | skip |
| AltitudeOrFL | `052` (FL) | 5200 ft | quantised to 100 ft | skip |
| LsCourse | `B109` | FORMAT ERROR | message shown | skip |
| TimeHHMM / HHMMSS | `01:30` / `12:30:45` | same value | ok (':' removed) | - |
| RadioAltitude | `NO DH` | 0 | ok | - |
| Squawk, QNH, FL, CrzTemp, Temperature, Mach | | same | ok | - |

[PORT] Recommended rule: ENT on an unmodified field re-runs only the page handler with the current value (no
`parse`), which preserves FBW's side effects (e.g. re-confirming V-speeds) without corrupting values.

ESC: see 1.5.

---

## 8. Unit conversions

| Quantity | From -> to | Factor / rule | Where | Source |
|---|---|---|---|---|
| Weight entry | T -> kg | x 1000 | WeightFormat | `DEF:544-546` |
| Weight entry | KLB -> kg | x 1000 x 0.453592 (msfs-sdk `UnitType.POUND`) [R] | WeightFormat | `DEF:544` |
| Weight display | kg -> T / KLB | inverse of the above, `toFixed(1)` | WeightFormat | `DEF:532-534` |
| Length entry | FT -> m | x 0.3048 (msfs-sdk `UnitType.FOOT`) [R] | LengthFormat | `DEF:489` |
| Length display | m -> FT | / 0.3048, `toFixed(0)` | LengthFormat | `DEF:478` |
| Altitude shorthand | 1..3 chars -> ft | x 100 | AltitudeOrFL, Tropo | `DEF:229-231,440` |
| FL | FL -> ft | x 100 | AltOrFL transition, cruise FL handling | `DEF:197`, `FMC:1843` |
| Mach shorthand | digits -> Mach | `"0." + digits` when value > 0.89 | SpeedMach | `DEF:151-153` |
| QNH | 4 digits -> inHg | / 100 when 22.00..32.48 | Qnh | `DEF:910` |
| QNH | inHg -> hPa | x 33.863886666667 | `qnhToMillibar` | `fbw-common/src/systems/shared/src/units.ts:91-93` |
| Time | HHMM -> min | `60h + m` | TimeHHMM | `DEF:1313` |
| Time | HHMMSS -> s | `3600h + 60m + s` | TimeHHMMSS | `DEF:1377` |
| Squawk | digits -> BCD | `parseInt(decimalString, 16)` (MSFS only) | SURV | `MfdSurvControls.tsx:284` |
| Wind sign | HD/TL -> signed | HD negative | TripWind | `DEF:822-837` |
| Cruise temp sign | unsigned -> negative | `n>0` w/o `+` -> `-n` | CrzTemp | `DEF:701-703` |
| Descent rate sign | positive -> negative | `n>0` -> `-n` | DescentRate | `DEF:1052-1054` |
| Distance/speed in messages | ft | `toFixed(0)` | NEW CRZ/ACCEL/THR RED ALT | `ACI:460,469,1336` |

Other FBW constants (not used by the formats, listed to avoid mixing): `Units.poundToKilogram` = x 0.4535934,
`Units.footToMetre` = / 3.28084 (`units.ts:26-56`). [PORT] Use 0.45359237 kg/lb and 0.3048 m/ft (exact definitions)
in one shared header (`mfd_units.h`); differences vs 0.453592 are below display resolution (0.1 KLB) except at exact
rounding boundaries.

Unit selection: FBW `NXDataStore` setting `CONFIG_USING_METRIC_UNIT` (true = kg / m). [PORT] Plugin setting exposed as
dataref `a380x/mfd/units_metric` (int 0/1); weight and length fields re-render on change.

---

## 9. Display-only helpers (`MFD/shared/utils.ts`, `QnhUtils.ts`, `Adirs.ts`)

### 9.1 utils.ts

| Helper | Behaviour |
|---|---|
| `getEtaFromUtcOrPresent(seconds, fromPresent)` (`:9-20`) | null/undefined/NaN -> `--:--`. Else `t = fromPresent ? seconds : seconds + ZULU_TIME_seconds`; output `HH:MM` of `t` as UTC time of day (`floor(t/3600) mod 24`, `floor(t/60) mod 60`, zero-padded 2). Note: when `fromPresent` is **true** the value is used as-is (the naming is inverted relative to the arithmetic). |
| `hhmmFormatter` (`:77`) | `{HH}:{mm}` of a UTC timestamp in ms; NaN -> `--:--` |
| `noPositionAvailableText` (`:54`) | `--°--.--/---°--.--` |
| `getApproachName(approach, withRnpSuffix=true)` (`:45-52`) | `TYPE + runwayIdent.substring(4) + ("-" + multipleIndicator if any) + (" (RNP)" if withRnpSuffix and (authorisationRequired or missedApproachAuthorisationRequired))`. TYPE: ILS `ILS`, GLS `GLS`, IGS `IGS`, LOC `LOC`, LOC back course `BAC`, LDA `LDA`, SDF `SDF`, GPS `GPS`, RNAV `RNV`, VOR / VOR-DME / VORTAC `VOR`, NDB / NDB-DME `NDB`, FMS `RNAV`, MLS (3 kinds) `MLS`, TACAN `TAC`, unknown empty. Runway idents are `<ICAO4><NN><L/C/R>`, so `substring(4)` = `25C`. Example: ILS EDDF25C multiple Y -> `ILS25C-Y`. |
| `isConstraintRevisionAllowed(leg)` (`:79-86`) | true iff leg is a FlightPlanLeg, not a runway leg, is an XF leg, and not a direct-to turning point |
| URI constants (`:56-75`) | `f-pln`, `f-pln-hold`, `fms/data/status`, `fuel-load`, `perf`, `init`, `f-pln-vert-rev`, `f-pln-lat-rev`, `f-pln-departure`, `f-pln-arrival`, `f-pln-airways`, `fms/sec/index`, `fms/active/f-pln`, `fms/active/fuel-load`, `fms/active/f-pln-hold`, `fms/active/f-pln-fix-info`, `fms/active/f-pln-direct-to`; extra `withReturn` |

### 9.2 QnhUtils.ts

`isQnhInHg(q) = q < 100`; `qnhToMillibar(q) = isQnhInHg(q) ? q * 33.863886666667 : q` (`QnhUtils.ts:6-12`).
Spec test (`QnhUtils.spec.ts`): `isQnhInHg(29.92)` true, `(100)` false, `(1013)` false;
`qnhToMillibar(29.92)` ~ 1013.207489 (6 decimals); `qnhToMillibar(1013)` = 1013. Consumers: `fmgc.ts:449`
(approach QNH, default 1013.25), PERF APPR commit (4.11).

### 9.3 Adirs.ts (inputs to message logic)

| Function | Behaviour |
|---|---|
| `getNdSupplier(displayIndex, knob)` | Captain side = `displayIndex == 1`: knob 0 ("CAPT 3") -> ADIRU 3 else 1. F/O: knob 2 ("F/O 3") -> 3 else 2. |
| `getNdInertialReferenceSource` / `...AirDataReferenceSource` | knob from `L:A32NX_ATT_HDG_SWITCHING_KNOB` / `L:A32NX_AIR_DATA_SWITCHING_KNOB` |
| `mapNotAvailable(displayIndex)` | IR latitude or longitude word of the selected ADIRU not NormalOperation |
| `getFromAnyAdiru(type, param)` | iterate ADIRU 1, 2, 3: return the first word with SSM NormalOperation; if none, return ADIRU 3's word (may be invalid) |
| getters | `getLatitude/Longitude` (deg), `getTrueTrack` (deg), `getTrueAirspeed`, `getCalibratedAirspeed`, `getGroundSpeed` (kt), `getBaroCorrectedAltitude` (ft), `getStaticAirTemperature` (°C), `getCorrectedAverageStaticPressure` (hPa) |

Used here by SPD LIMIT EXCEEDED (CAS, baro alt) and DIR TO (true track). [PORT] Provide
`int mfd_adirs_get(MfdAdirsParam p, double *v)` returning validity, backed by the port's ADIRS model (X-Plane
`sim/cockpit2/gauges/indicators/*` while no ADIRS simulation exists).

---

## 10. FMS messages

### 10.1 Data model (`MSG:5-75`)

| Field | Meaning |
|---|---|
| `text` | Verbatim message text (may contain `\n` for a second line). |
| `isAmber` | Background colour of the message area: amber `#e68000` if true, else white `#ffffff`. |
| `replace` | Token replaced by a run-time value (`getModifiedMessage(t)`: first occurrence; if `t` is empty, no replacement). |
| `isTypeTwo` | Type II if true. |
| `isResolved()` (Type II) | Polled every FMS cycle; when true the entry is removed. Default `() => false`. |
| `onClear()` (Type II) | Called when the crew clears it with CLEAR INFO. Default no-op. |

Semantics:
- **Type I**: response to a crew action (entry errors, NOT ALLOWED...). CLEAR INFO **deletes** it.
- **Type II**: FMS condition/advisory. CLEAR INFO only marks it `cleared` (leaves the message area, stays in MSG
  LIST); it is removed when `isResolved()` becomes true or the producer calls remove.

### 10.2 Queue semantics (`FMC:228-232, 800-817, 1136-1181`)

The queue (`fmsErrors`) belongs to the **master FMC**; both MFD footers and the MSG LIST read the same queue
[FBW] (a message caused on the CAPT MFD also appears on the F/O MFD; clearing on either clears it for both).

Entry: `{message (original def), messageText, backgroundColor ('white'|'amber'|'cyan'), cleared, isResolvedOverride,
onClearOverride}`. `cyan` is supported by the footer but never produced.

| Operation | Behaviour |
|---|---|
| `add(msg, isResolved?, onClear?, details?)` | `text = modified text + (details ? "\n" + details : "")`; colour from `isAmber`; Type II callbacks from the def or overrides. If an entry with the **same text and `cleared == true`** exists, remove it. Insert the new entry at **index 0** (newest first). Non-cleared identical entries are *not* de-duplicated (two `FORMAT ERROR` entries can stack). |
| `remove(text)` | Remove the **first** entry whose `messageText` equals `text` exactly. |
| `clearLatest()` (CLEAR INFO) | Find first entry with `cleared == false`. Type II: set `cleared = true`, call `onClear`. Type I: delete. |
| `update()` | Every FMS cycle (`FMS_CYCLE_TIME = 250 ms`, `FMC:83`, called `FMC:1515`): remove every Type II entry whose `isResolved()` is true. [FBW] iterates with `forEach` + `removeAt`, so an entry directly after a removed one is skipped until the next cycle. |
| Footer source | first entry with `cleared == false` |
| Reset | On DONE phase (`FMC:1374`) and on FMC `reset()` / nav database swap (`FMC:1733-1742`) the FMC calls `clearLatest()` **once**; the queue is never emptied. [PORT] `reset()` empties the queue (`mfd_msg_init`). |

[FBW] bugs to be aware of:
- `remove(NXSystemMessages.newCrzAlt.text)` passes the un-substituted text `NEW CRZ ALT: HHHHH`, so it never matches
  (`FMC:1316,1329`). Similar risk for any message with `replace` or details.
- CHECK SPD MODE is added with `isResolved = () => !checkSpeedModeMessageActive` where the captured flag is false,
  so it is resolved (removed) at the next cycle (`FMC:1389-1404`).
- Unbounded queue; Type II entries without a resolve condition (NEW CRZ ALT, STEP AHEAD, ...) accumulate as cleared
  entries forever.

[PORT]:
- Fixed-capacity queue (16). When full, drop the oldest *cleared* entry, else the oldest entry.
- Store a message **id** and remove by id (`mfd_msg_remove_id`), plus `remove_text` for parity.
- Implement CHECK SPD MODE resolve as "speed managed or preselected speed present".
- Keep the shared queue; carry an `origin_side` field (0 both, 1 CAPT, 2 F/O) for possible later per-side Type I.

### 10.3 Message area (footer)

`FTR:5-76`, styles `MFD/pages/common/style.scss:99-114`, button `widget-style.scss:79-90`.

| Element | Spec |
|---|---|
| Footer box | Last child of the 768 x 1024 column. Border-top 2 px `#272525`, padding 5 px. Derived height: 2 + 5 + button + 5. Button outer height = 2+9+(2 lines x 22 px font)+5+2. With the font's typo metrics (fsSelection USE_TYPO_METRICS set; asc 3276, desc -820, gap 369 per 4096) a 22 px line is 23.98 px -> button 66 px -> footer **78 px, y = 946..1024**. [PORT] use these values unless the layout spec defines the footer differently. |
| Button | x = 5..133 (content width 100 + padding 12+12 + border 2+2), y = 953..1019; bg `#272525`, 2 px outset border `#c4c6cf` (`#00ffff` when hovered by KCCU cursor), text white 22 px, 2 lines centred. Label `MSG` / `LIST` when no uncleared message, `CLEAR` / `INFO` when one is shown. Click: CLEAR INFO -> `clearLatest()`; MSG LIST -> open MSG LIST page. |
| Message area | x from 148 (button right edge 133 + 15 px margin) to 763; vertically centred on y = 986. Font 26 px EIS SlashedZero (16.30 px advance -> **37 characters per line**), text `#000000`, `white-space: pre-wrap` (honours `\n`, wraps at spaces). |
| Message background | Inline span: background colour (`#ffffff` white, `#e68000` amber, `#00ffff` cyan) drawn behind each text line only (width = chars x 16.30 px, height = one line box, 26 px x 1.0901 = 28.3 px). No message -> nothing drawn. |

Rendering algorithm [PORT]: split `messageText` on `\n`; word-wrap each part to 37 chars; draw up to 3 lines with
24 px pitch centred on y = 986 (2 lines: baselines approx. 982 and 1006 for a 26 px font with 0.8 em ascent); for each
line fill a rect behind the glyphs, then draw the glyphs in black.

Pages with this footer: all FMS pages (INIT, FUEL&LOAD, PERF, F-PLN family, DATA, POSITION, SEC INDEX, NOT FOUND),
SURV CONTROLS and STATUS & SWITCHING, ATCCOM CONNECT and MSG RECORD pages. ATCCOM D-ATIS pages use the ATCCOM footer
(10.8).

### 10.4 MSG LIST page (`MFD/pages/FMS/MfdMsgList.tsx`, `MfdMsgList.scss`)

- Overlay dialog (`mfd-fms-fpln-dialog-outer/inner`), title bar text `MESSAGE LIST` (FMS subsystem page style), button
  `CLOSE` bottom-left (closes the overlay).
- Re-rendered at **1 Hz** from the queue: all entries (cleared and uncleared), newest first, **first 4 only**
  (warning logged when more than 5).
- List container: margin 75 px, 2 px white top border. Each row: `messageText`, 26 px, white `#ffffff` text
  (no amber colouring), content height 80 px + padding 5 px each side + 2 px white bottom border (row pitch 92 px).
- [PORT] Same, but colour amber messages' text `#e68000` only if a reference shows it; otherwise keep white. Refresh on
  queue change instead of 1 Hz.

### 10.5 NXSystemMessages catalogue (`MSG:80-122`)

Type I = `TypeIMessage`, II = `TypeIIMessage`. Amber = background colour. `Used` = referenced in the A380X systems.

| Key | Text (verbatim) | Type | Amber | Replace | Used |
|---|---|---|---|---|---|
| awyWptDisagree | `AIRWAY / WPT DISAGREE` | I | no | - | N/A |
| crzFlAboveMaxFL | `CRZ FL ABOVE MAX FL` | II | no | - | yes |
| cancelAtisUpdate | `CANCEL AUTO UPDATE FIRST` | I | no | - | N/A |
| checkMinFuelAtDest | `CHECK MIN FUEL AT DEST` | II | no | - | yes |
| checkSpeedMode | `CHECK SPD MODE` | II | no | - | yes |
| checkToData | `CHECK T.O. DATA` | II | yes | - | yes |
| checkZfw | `CHECK ZFW` | II | yes | - | yes |
| comFplnReceivedPendingInsertion | `COMPANY F-PLN RECEIVED` `\n` `WAITING FOR INSERTION` | II | no | - | yes |
| comDatalinkNotAvail | `COM DATALINK NOT AVAIL` | I | no | - | N/A |
| cstrDelUpToWpt | `CONSTRAINTS BEFORE WWWWW : DELETED` | II | no | `WWWWW` | yes |
| databaseCodingError | `DATABASE CODING ERROR` | II | no | - | N/A |
| destEfobBelowMin | `DEST EFOB BELOW MIN` | II | yes | - | yes |
| enterDestData | `ENTER DEST DATA` | II | yes | - | yes |
| entryOutOfRange | `ENTRY OUT OF RANGE` | I | no | - | yes |
| formatError | `FORMAT ERROR` | I | no | - | yes |
| fplnElementRetained | `F-PLN ELEMENT RETAINED` | I | no | - | N/A |
| initializeZfwOrZfwCg | `INITIALIZE ZFW / ZFWCG` | II | yes | - | yes |
| newAccAlt | `NEW ACCEL ALT: HHHHH` | II | no | `HHHHH` | yes |
| newCrzAlt | `NEW CRZ ALT: HHHHH` | II | no | `HHHHH` | yes |
| newThrRedAlt | `NEW THR RED ALT: HHHHH` | II | no | `HHHHH` | yes |
| noIntersectionFound | `NO INTERSECTION FOUND` | I | no | - | yes |
| notAllowed | `NOT ALLOWED` | I | no | - | yes |
| notAllowedInNav | `NOT ALLOWED IN NAV` | I | no | - | N/A |
| notInDatabase | `NOT IN DATABASE` | I | no | - | yes |
| receivedCpnyFplnNotValid | `RECEIVED COMPANY F-PLN NOT VALID` | II | no | - | yes |
| rwyLsDisagree | `RUNWAY / LS DISAGREE` | II | yes | - | N/A |
| setHoldSpeed | `SET HOLD SPD` | II | no | - | yes |
| tdReached | `T/D REACHED` | II | no | - | yes |
| spdLimExceeded | `SPD LIMIT EXCEEDED` | II | yes | - | yes |
| toSpeedTooLow | `T.O SPEED TOO LOW - CHECK TOW & T.O DATA` | II | yes | - | dead (condition hard-coded false, `ACI:635`) |
| vToDisagree | `V1/VR/V2 DISAGREE` | II | yes | - | yes |
| xxxIsDeselected | `XXXX IS DESELECTED` | I | no | `XXXX` | yes |
| stepAboveMaxFl | `STEP ABOVE MAX FL` | II | no | - | yes |
| stepAhead | `STEP AHEAD` | II | no | - | yes |
| stepDeleted | `STEP DELETED` | II | no | - | yes |
| tooSteepPathAhead | `TOO STEEP PATH AHEAD` | II | no | - | yes |
| navprimary | `NAV PRIMARY` | II | no | - | yes |
| navprimaryLost | `NAV PRIMARY LOST` | II | yes | - | yes |
| sqwkCodeNotValid | `SQWK CODE NOT VALID` | I | no | - | yes |
| lrcInUse | `LRC MODE IN USE` | I | no | - | yes |
| lateralDiscontinuityAhead | `LATERAL DISCONTINUITY AHEAD` | II | yes | - | yes |

Note the inconsistent dots: `CHECK T.O. DATA` vs `T.O SPEED TOO LOW...`; reproduce verbatim.

### 10.6 Triggers, removal and side effects (A380X)

| Message | Added when | Removed / resolved when | Side effects | Ref |
|---|---|---|---|---|
| ENTRY OUT OF RANGE / FORMAT ERROR (+details) | format parse errors (sections 1.7, 4); `trySetCruiseFl` invalid FL | CLEAR INFO | - | `FMC:1094-1111,1804-1816` |
| NOT IN DATABASE | lookup failures (airway VIA, FIX INFO, MONITOR, INSERT NEXT WPT) | CLEAR INFO | - | `MfdFmsFplnAirways.tsx:230,288` |
| NOT ALLOWED | AIRWAYS VIA continuation refused; NAVAIDS: clearing a VOR/ILS ident or freq that is not manually tuned; ILS ident entry while MMR tuning locked (entry still processed) | CLEAR INFO | - | `MfdFmsFplnAirways.tsx:243`, `MfdFmsPositionNavaids.tsx:240-306` |
| NO INTERSECTION FOUND | AIRWAYS TO fix not on airway | CLEAR INFO | - | `MfdFmsFplnAirways.tsx:316` |
| XXXX IS DESELECTED | manual VOR ident is in the deselected list; XXXX = navaid ident | CLEAR INFO | - | `MfdFmsPositionNavaids.tsx:252` |
| SQWK CODE NOT VALID | squawk FORMAT ERROR | CLEAR INFO | - | `MfdSurvControls.tsx:291` |
| LRC MODE IN USE | engines started (or ZFW entered) with destination set and CI null: CI set 0 and mode LRC | CLEAR INFO | CI = 0 | `FMC:1777-1786` |
| CRZ FL ABOVE MAX FL | inserted CRZ FL > REC MAX FL | never (until cleared, stays in list) | - | `FMC:1835-1837` |
| NEW CRZ ALT: <ft> | (a) CRZ FL entry below FCU alt in CRZ/DES/APPR (FL raised to FCU); (b) step climb/descent from FCU in CLB/CRZ; (c) FCU alt change confirmed 3 s in CLB (above CRZ FL) or CRZ (different) with CLB/DES/OP CLB/OP DES/VS/FPA engaged | never effectively (removal on DES/APPR phase uses unsubstituted text) | `L:A32NX_AIRLINER_CRUISE_ALTITUDE` | `FMC:1840-1846`, `ACI:1315-1342,1372-1408` |
| NEW ACCEL ALT: <ft> / NEW THR RED ALT: <ft> | active plan reconciles accel / thr red altitude with constraints, each FMS cycle that it changes | never | - | `ACI:454-473` |
| CHECK T.O. DATA | secondary F-PLN activated in PREFLIGHT/DONE; PACKS or ANTI ICE dropdown changed after engine start; origin runway changed after V-speeds entered (V1/VR/V2 cleared) | runway case: resolved when VR re-entered; others never | flex temp republished on SEC activation | `FMC:944-947`, `MfdFmsPerf.tsx:1760,1788`, `FmsPage.tsx:287-302` |
| CHECK ZFW | SEC activation with ZFW diff > 5 and ZFWCG diff > 0.5 | removed on next ZFW change | - | `FMC:950-956` |
| INITIALIZE ZFW / ZFWCG | an engine running (once per inhibit cycle, before T.O phase) and ZFW or ZFWCG null | removed when both defined | - | `FMC:1426-1440,386` |
| ENTER DEST DATA | entering DES or APPR phase while any of APPR QNH, TEMP, WIND dir, WIND spd is null | removed when all four entered | - | `FMC:1416-1420,400` |
| COMPANY F-PLN RECEIVED / WAITING FOR INSERTION | company (SimBrief) F-PLN uplink available | removed when no longer available | - | `FMC:390-396` |
| RECEIVED COMPANY F-PLN NOT VALID | uplink failed | never | - | `FMC:845` |
| CONSTRAINTS BEFORE <ident> : DELETED | GO AROUND phase: missed approach strung; per affected leg | never | - | `FMC:1337-1342` |
| STEP DELETED | engine-out entry deletes cruise steps; or flight plan auto-deletes a cruise step | never | - | `FMC:1711`, `ACI:403-411` |
| STEP AHEAD | < 20 NM to next cruise step (once per step altitude); auto step sets FCU alt if enabled | removed when outdated steps deleted | `K:A32NX.FCU_ALT_SET` (auto step) | `ACI:1607-1630,1365` |
| STEP ABOVE MAX FL | VERT REV step altitude > REC MAX ALT at the step's estimated GW | never | - | `MfdFmsFplnVertRev.tsx:874` |
| TOO STEEP PATH AHEAD | VNAV reports too-steep segment ahead (edge) | resolved when no longer | - | `ACI:1676-1689` |
| LATERAL DISCONTINUITY AHEAD | VNAV reports discontinuity ahead | removed when false | - | `ACI:356-362,1763-1765` |
| DEST EFOB BELOW MIN | dest EFOB < min AND (CRZ or DES or ALT engaged in CLB > 600 s or (APPR/GA and RA > 800 ft)) | removed when false | - | `ACI:374-380,1709-1724` |
| CHECK MIN FUEL AT DEST | pilot MIN FUEL AT DEST < ALTN + FINAL fuel | removed when false | - | `ACI:381-387`, `FMC:282-288` |
| V1/VR/V2 DISAGREE | edge into "not V1 <= VR <= V2" (only pairs both entered) | resolved when order valid | PRIM discrete word bit 16 | `ACI:606-651` |
| SPD LIMIT EXCEEDED | CLB/CRZ (climb limit) or DES (descent limit): alt < limit alt - 150 ft and CAS > limit + 10 kt (valid ADR) | alt > limit alt or CAS <= limit + 5 or ADR invalid | - | `ACI:1102-1146` |
| SET HOLD SPD | in/approaching a hold (HM) with selected speed and CAS - hold speed > 5 kt | resolved when condition ends | `L:A32NX_PFD_MSG_SET_HOLD_SPEED`; on clear sets it false | `ACI:883-955` |
| CHECK SPD MODE | entering DES with selected speed and no preselected speed | [FBW] resolved next cycle (bug); removed when speed managed | `L:A32NX_PFD_MSG_CHECK_SPEED_MODE` | `FMC:1389-1414` |
| T/D REACHED | `tdReached` event true | removed when false | on clear: `L:A32NX_PFD_MSG_TD_REACHED` false | `ACI:123-136` |
| NAV PRIMARY | GPS primary becomes true | removed when GPS primary lost | ND message flags (`NavPrimary`) set; on clear: ND flags removed | `ACI:188-212` |
| NAV PRIMARY LOST | GPS primary becomes false | removed when GPS primary regained | ND flag `NavPrimaryLost` | `ACI:188-212` |
| NO NAVIGRAPH USER (fictional) | CPNY F-PLN request without Navigraph/SimBrief user | CLEAR INFO | - | `FMC:822-826` |
| NOT YET IMPLEMENTED (fictional) | `FmsErrorType.NotYetImplemented` | CLEAR INFO | - | `FMC:1105` |

[PORT] Map PFD/ND side-effect L-vars to port datarefs, e.g. `a380x/fms/pfd_msg_set_hold_speed`,
`a380x/fms/pfd_msg_check_speed_mode`, `a380x/fms/pfd_msg_td_reached`, `a380x/efis/{l,r}/nd_fm_message_flags`.

### 10.7 NXFictionalMessages (`MSG:124-151`)

All Type I, white, no replace, except `reloadPlaneApply` (Type II, amber). Used in A380X: `noNavigraphUser`,
`notYetImplemented`; all others **[N/A]**.

| Key | Text | Key | Text |
|---|---|---|---|
| noNavigraphUser | `NO NAVIGRAPH USER` | noOriginApt | `NO ORIGIN AIRPORT` |
| internalError | `INTERNAL ERROR` | noOriginSet | `NO ORIGIN SET` |
| noAirportSpecified | `NO AIRPORT SPECIFIED` | secondIndexNotFound | `2ND INDEX NOT FOUND` |
| fltNbrInUse | `FLT NBR IN USE` | firstIndexNotFound | `1ST INDEX NOT FOUND` |
| fltNbrMissing | `ENTER ATC FLT NBR` | noRefWpt | `NO REF WAYPOINT` |
| notYetImplemented | `NOT YET IMPLEMENTED` | noWptInfos | `NO WAYPOINT INFOS` |
| recipientNotFound | `RECIPIENT NOT FOUND` | emptyMessage | (empty) |
| authErr | `AUTH ERR` | reloadPlaneApply | `RELOAD A/C TO APPLY` (II, amber) |
| invalidMsg | `INVALID MSG` | noAcarsConnection | `NO ACARS CONNECTION` |
| unknownDownlinkErr | `UNKNOWN DOWNLINK ERR` | unknownAtsuMessage | `UNKNOWN ATSU MESSAGE` |
| telexNotEnabled | `TELEX NOT ENABLED` | reverseProxy | `REVERSE PROXY ERROR` |
| freeTextDisabled | `FREE TEXT DISABLED` | simBriefNoUser | `NO SIMBRIEF PILOT ID PROVIDED` |
| freetextEnabled | `FREE TEXT ENABLED` | | |
| enabledFltNbrInUse | `ENABLED. FLT NBR IN USE` | | |

[PORT] Replace `NO NAVIGRAPH USER` by `NO SIMBRIEF PILOT ID PROVIDED` (`simBriefNoUser`) if the port's company-route
source is SimBrief only.

### 10.8 ATCCOM messages and queue (`MSG:157-185`, `ATC:186-249`, `MfdAtccomFooter.tsx`)

`ATCCOMMessage` = Type I-like (no resolve/clear callbacks), all white.

| Key | Text | Used |
|---|---|---|
| cancelAutoUpdateFirst | `CANCEL AUTO UPDATE FIRST` | N/A |
| comDatalinkNotAvail | `COM DATALINK NOT AVAIL` | N/A |
| datisGroundMsg | `D-ATIS GROUND MSG` | N/A |
| datisNoReply | `D-ATIS NO REPLY` | yes: D-ATIS request returned "no ATIS" (`ATC:351`) |
| datisReceived | `D-ATIS RECEIVED` | commented out (`ATC:356`) |
| datisSendFailed | `D-ATIS SEND FAILED` | N/A |
| datisUpdated | `D-ATIS UPDATED - READ AGAIN` | N/A |
| datisUsedOffside | `D-ATIS USED OFFSIDE` | N/A |
| entryOutOfRange | `ENTRY OUT OF RANGE` | N/A (NX version used) |
| formatError | `FORMAT ERROR` | N/A (NX version used) |
| identicalDatisRequest | `IDENTICAL D-ATIS REQUEST` | N/A |
| lastMsgElement | `LAST MSG ELEMENT` | N/A |
| mailboxFull | `MAILBOX FULL - SEND OR CANCEL SOME MSG` | N/A |
| msgAbortedActiveAtcDisconnected | `MSG ABORTED - ACTIVE ATC DISCONNECTED` | N/A |
| msgAbortedNotSupportedByCurrentATC | `MSG ABORTED - NOT SUPPORTED BY CURRENT ATC` | N/A |
| msgRecordLost | `MSG RECORD LOST` | N/A |
| msgRecordUsedOffside | `MSG RECORD USED OFFSIDE` | N/A |
| newDatisGroundMsg | `NEW D-ATIS GROUND MSG - READ AGAIN` | N/A |
| noSysData | `NO SYS DATA` | N/A |
| notifNotAvailAcftPosNotAvail | `NOTIFICATION NOT AVAIL - ACFT POSITION NOT AVAIL` | N/A |
| notifNotAvailChckFltNbr | `NOTIFICATION NOT AVAIL - CHECK FLT NBR IN FMS INIT PAGE` | N/A |
| notifNotAvailChckFromTo | `NOTIFICATION NOT AVAIL - CHECK FROM/TO IN FMS INIT PAGE` | N/A |
| notifNotAvailWithThisAtcCtr | `NOTIFICATION NOT AVAIL - WITH THIS ATC CENTER` | N/A |
| pleaseWaitUpdateInProgress | `PLEASE WAIT: UPDATE IN PROGRESS` | N/A |
| printerNotAvail | `PRINTER NOT AVAIL` | N/A |
| printing | `PRINTING` | N/A |
| sendingMaydayWillSwitchAdscToEmergency | `SENDING MAYDAY WILL SWITCH ADS-C TO EMERGENCY` | N/A |

ATCCOM queue (`atcErrors`, owned by `AtcDatalinkSystem`): same add/dedupe/clearLatest rules as 10.2, no update/resolve,
no onClear. `showAtcErrorMessage(type, details)` maps EntryOutOfRange/FormatError (with details), NotInDatabase,
NotYetImplemented to the **NX** messages (`ATC:191-209`). ATCCOM footer (D-ATIS pages): same geometry as 10.3, button
label always `CLEAR` / `INFO`; click with no message does nothing; no MSG LIST.

### 10.9 ND FM messages (cross-reference, `fbw-a380x/src/systems/shared/src/FmMessages.ts`)

Bit flags written to `L:A32NX_EFIS_{L,R}_ND_FM_MESSAGE_FLAGS` (`FMC:1760-1775`): SelectTrueRef (bit 0, `SELECT TRUE NORTH
REF`, amber), CheckNorthRef (1, `CHECK NORTH REF`, amber), NavAccuracyDowngrade (2, `NAV ACCUR DOWNGRAD`, amber),
NavAccuracyUpgrade (3, `NAV ACCUR UPGRADED`, white), SpecifiedVorDmeUnavailable (4, `SPECIF VOR-D NOT AVAIL`, amber),
NavAccuracyUpgradeGps (5), NavPrimary (6, `NAV PRIMARY`, white), MapPartlyDisplayed (7, `MAP PARTLY DISPLAYED`, amber),
OffsideFmControl (8, `OFFSIDE FM CONTROL`, amber), NavPrimaryLost (9, `NAV PRIMARY LOST`, amber). Only NavPrimary /
NavPrimaryLost are driven from the MFD message logic. Rendering belongs to the ND spec.

---

## 11. C API proposal

Three headers in the simulator-independent core (no XPLM): `mfd_fmt.h`, `mfd_msg.h`, `mfd_units.h`. Style follows
`a380x_avionics_core.h` (C99, no allocation, all buffers caller-owned).

### 11.1 Errors and results

```c
typedef enum {
    MFD_ERR_NONE = 0,
    MFD_ERR_NOT_IN_DATABASE,
    MFD_ERR_NOT_YET_IMPLEMENTED,
    MFD_ERR_FORMAT_ERROR,
    MFD_ERR_ENTRY_OUT_OF_RANGE
} MfdFmsErrorType;

#define MFD_ERR_DETAILS_LEN 64
typedef struct {
    MfdFmsErrorType type;
    char details[MFD_ERR_DETAILS_LEN];  /* "" -> single-line message */
} MfdFmsError;

typedef enum { MFD_PARSE_ERROR = -1, MFD_PARSE_CLEAR = 0, MFD_PARSE_OK = 1 } MfdParseStatus;

typedef struct {
    char value[32];        /* UTF-8 */
    char lead[8];          /* "" if none */
    char trail[8];         /* "" if none */
    unsigned char is_placeholder;  /* renderer may substitute U+25AF for '-' when mandatory */
} MfdFieldText;
```

### 11.2 Helpers

```c
int    mfd_js_number(const char *s, double *out);      /* faithful JS Number(); returns 0 (and *out = NAN) on NaN */
int    mfd_strict_number(const char *s, double *out);  /* PORT default grammar, 2.1 */
double mfd_js_parse_float(const char *s);              /* NAN if no numeric prefix */
void   mfd_js_to_fixed(double v, int digits, char *buf, size_t n);
void   mfd_js_to_string(double v, char *buf, size_t n);
void   mfd_pad_start(char *buf, size_t n, size_t width, char pad);
double mfd_js_round(double v);                          /* floor(v + 0.5) */
void   mfd_err_set_format(MfdFmsError *e, const char *required_format, const char *unit /* NULL ok */);
void   mfd_err_set_range(MfdFmsError *e, const char *lo, const char *hi, const char *unit /* NULL ok */);
void   mfd_err_set_plain(MfdFmsError *e, MfdFmsErrorType t);
```

`mfd_err_set_format(e, "XXX", "KT")` -> type FORMAT_ERROR, details `FORMAT: XXX KT`.

### 11.3 Per-format functions

Signature convention: `MfdParseStatus mfd_fmt_parse_<x>(const char *in, <limits>, double *out, MfdFmsError *err)`
and `void mfd_fmt_render_<x>(int has_value, double v, <context>, MfdFieldText *out)`. On `MFD_PARSE_CLEAR` the caller
clears the value; on `MFD_PARSE_ERROR` `err` is filled and `*out` is untouched.

| Format | Parse signature (after `in`) | Render context |
|---|---|---|
| speed_kt | `double min, double max` | - |
| speed_mach | `double min, double max` | - |
| alt_or_fl | `double min_ft, double max_ft` | `int has_trans, double trans_ft_or_fl, int trans_is_fl` |
| altitude | `double min_ft, double max_ft` | - |
| flight_level | `double min_fl, double max_fl` | - |
| radio_alt | `double min_ft, double max_ft` (0 = NO DH result) | - |
| tropo | - | - |
| length | `double min_m, double max_m, int metric` | `int metric, int max_digits` |
| weight | `double min_kg, double max_kg, int metric` | `int metric` |
| percent | `double min, double max` | - |
| temperature | `double min, double max` | - |
| crz_temp | - | - |
| wind_dir, wind_speed, trip_wind | - | - |
| qnh | - (result may be hPa or inHg) | `int is_hpa` (placeholder only) |
| cost_index, pax_nbr | - | - |
| vertical_speed, descent_rate | `double min, double max` | - |
| time_hhmm (minutes), time_hhmmss (seconds) | - | - |
| heading, inbound_course, hold_dist, hold_time | `double min, double max` | - |
| freq_ils, freq_vor, freq_adf | - | - |
| ls_course | - | - |
| squawk | - | - |
| radial, radius, rnp | - | - |
| fuel_penalty | - | - |
| latitude [N/A] | - | - |

Text formats use `char *out, size_t n` instead of `double *out`:

```c
MfdParseStatus mfd_fmt_parse_text(MfdTextKind kind, const char *in, int max_digits,
                                  const char *placeholder, char *out, size_t n, MfdFmsError *err);
/* MfdTextKind: MFD_TXT_FIX, MFD_TXT_AIRPORT, MFD_TXT_NAVAID, MFD_TXT_AIRWAY,
                MFD_TXT_DROPDOWN, MFD_TXT_WAYPOINT, MFD_TXT_LONG_ALNUM */
void mfd_fmt_render_text(const char *value_or_null, const char *placeholder, MfdFieldText *out);
int  mfd_is_place_format(const char *s);   /* ^[A-Z0-9]{2,7}$ */
int  mfd_is_runway_format(const char *s);  /* ^[A-Z]{4}[0-9]{2}[LCR]?$ */
```

### 11.4 Descriptor-driven dispatch (for the generic input-field widget)

```c
typedef enum {
    MFD_FMT_SPEED_KT, MFD_FMT_SPEED_MACH, MFD_FMT_ALT_OR_FL, MFD_FMT_ALTITUDE, MFD_FMT_FLIGHT_LEVEL,
    MFD_FMT_RADIO_ALT, MFD_FMT_TROPO, MFD_FMT_LENGTH, MFD_FMT_WEIGHT, MFD_FMT_PERCENT,
    MFD_FMT_TEMPERATURE, MFD_FMT_CRZ_TEMP, MFD_FMT_WIND_DIR, MFD_FMT_WIND_SPEED, MFD_FMT_TRIP_WIND,
    MFD_FMT_QNH, MFD_FMT_COST_INDEX, MFD_FMT_VERTICAL_SPEED, MFD_FMT_DESCENT_RATE, MFD_FMT_PAX_NBR,
    MFD_FMT_TIME_HHMM, MFD_FMT_TIME_HHMMSS, MFD_FMT_LATITUDE, MFD_FMT_HEADING, MFD_FMT_INBOUND_COURSE,
    MFD_FMT_HOLD_DIST, MFD_FMT_HOLD_TIME, MFD_FMT_FREQ_ILS, MFD_FMT_FREQ_VOR, MFD_FMT_FREQ_ADF,
    MFD_FMT_LS_COURSE, MFD_FMT_SQUAWK, MFD_FMT_RADIAL, MFD_FMT_RADIUS, MFD_FMT_RNP, MFD_FMT_FUEL_PENALTY,
    MFD_FMT_TXT_FIX, MFD_FMT_TXT_AIRPORT, MFD_FMT_TXT_NAVAID, MFD_FMT_TXT_AIRWAY, MFD_FMT_TXT_DROPDOWN,
    MFD_FMT_TXT_WAYPOINT, MFD_FMT_TXT_LONG_ALNUM,
    MFD_FMT_COUNT
} MfdFmtKind;

typedef struct {
    MfdFmtKind kind;
    double min, max;            /* storage units; refreshed by the page before parse */
    int    metric;              /* LENGTH/WEIGHT */
    int    max_digits;          /* 0 = default from table; LENGTH(5) and DROPDOWN(n) override */
    int    has_trans;           /* ALT_OR_FL */
    double trans;               /* ALT_OR_FL: ft, or FL if trans_is_fl */
    int    trans_is_fl;
    int    qnh_is_hpa;          /* QNH placeholder */
    const char *placeholder;    /* NULL = default; NAVAID deselected list uses "-" */
    unsigned char err_details;  /* 1 = pass details to message (FBW per-field, 1.7) */
} MfdFmt;

typedef struct { int has; double num; char str[25]; } MfdValue;

const char    *mfd_fmt_placeholder(const MfdFmt *f);
int            mfd_fmt_max_digits(const MfdFmt *f);
int            mfd_fmt_max_overflow(const MfdFmt *f);       /* FLIGHT_LEVEL: 2, else 0 */
const char    *mfd_fmt_error_unit(const MfdFmt *f);         /* the `unit` member, used for overflow width */
MfdParseStatus mfd_fmt_parse(const MfdFmt *f, const char *in, MfdValue *out, MfdFmsError *err);
void           mfd_fmt_render(const MfdFmt *f, const MfdValue *v, MfdFieldText *out);
```

A static table `mfd_fmt_defaults[MFD_FMT_COUNT]` holds placeholder, maxDigits, units, fixed ranges and
required-format strings from section 3/4 so that `mfd_fmt_parse` is a switch over `kind` calling the functions of 11.3.

### 11.5 Messages

```c
typedef enum {
    /* NXSystemMessages, in the order of section 10.5 */
    MFD_MSG_AWY_WPT_DISAGREE, MFD_MSG_CRZ_FL_ABOVE_MAX_FL, MFD_MSG_CANCEL_ATIS_UPDATE,
    MFD_MSG_CHECK_MIN_FUEL_AT_DEST, MFD_MSG_CHECK_SPD_MODE, MFD_MSG_CHECK_TO_DATA, MFD_MSG_CHECK_ZFW,
    MFD_MSG_COM_FPLN_RECEIVED, MFD_MSG_COM_DATALINK_NOT_AVAIL, MFD_MSG_CSTR_DEL_UP_TO_WPT,
    MFD_MSG_DATABASE_CODING_ERROR, MFD_MSG_DEST_EFOB_BELOW_MIN, MFD_MSG_ENTER_DEST_DATA,
    MFD_MSG_ENTRY_OUT_OF_RANGE, MFD_MSG_FORMAT_ERROR, MFD_MSG_FPLN_ELEMENT_RETAINED,
    MFD_MSG_INITIALIZE_ZFW_ZFWCG, MFD_MSG_NEW_ACC_ALT, MFD_MSG_NEW_CRZ_ALT, MFD_MSG_NEW_THR_RED_ALT,
    MFD_MSG_NO_INTERSECTION_FOUND, MFD_MSG_NOT_ALLOWED, MFD_MSG_NOT_ALLOWED_IN_NAV, MFD_MSG_NOT_IN_DATABASE,
    MFD_MSG_RECEIVED_CPNY_FPLN_NOT_VALID, MFD_MSG_RWY_LS_DISAGREE, MFD_MSG_SET_HOLD_SPD, MFD_MSG_TD_REACHED,
    MFD_MSG_SPD_LIM_EXCEEDED, MFD_MSG_TO_SPEED_TOO_LOW, MFD_MSG_V_TO_DISAGREE, MFD_MSG_XXX_IS_DESELECTED,
    MFD_MSG_STEP_ABOVE_MAX_FL, MFD_MSG_STEP_AHEAD, MFD_MSG_STEP_DELETED, MFD_MSG_TOO_STEEP_PATH_AHEAD,
    MFD_MSG_NAV_PRIMARY, MFD_MSG_NAV_PRIMARY_LOST, MFD_MSG_SQWK_CODE_NOT_VALID, MFD_MSG_LRC_IN_USE,
    MFD_MSG_LAT_DISCONTINUITY_AHEAD,
    /* fictional, section 10.7 (all 26) */
    MFD_MSG_NO_NAVIGRAPH_USER, MFD_MSG_NOT_YET_IMPLEMENTED, /* ... */ MFD_MSG_SIMBRIEF_NO_USER,
    /* ATCCOM, section 10.8 (all 27) */
    MFD_MSG_ATC_DATIS_NO_REPLY, /* ... */
    MFD_MSG_COUNT
} MfdMsgId;

typedef struct {
    const char   *text;     /* verbatim, may contain '\n' */
    unsigned char type2;    /* 1 = Type II */
    unsigned char amber;
    const char   *token;    /* "WWWWW", "HHHHH", "XXXX" or NULL */
} MfdMsgDef;
extern const MfdMsgDef mfd_msg_defs[MFD_MSG_COUNT];

typedef enum { MFD_MSG_BG_WHITE = 0, MFD_MSG_BG_AMBER, MFD_MSG_BG_CYAN } MfdMsgBg;
typedef int  (*MfdMsgResolvedFn)(void *ctx);
typedef void (*MfdMsgClearFn)(void *ctx);

typedef struct {
    MfdMsgId id;
    char     text[128];
    MfdMsgBg bg;
    unsigned char type2, cleared, origin_side;
    MfdMsgResolvedFn is_resolved;  /* NULL = never */
    MfdMsgClearFn    on_clear;     /* NULL = none */
    void    *ctx;
} MfdMsgEntry;

#define MFD_MSG_QUEUE_CAP 16
typedef struct {
    MfdMsgEntry e[MFD_MSG_QUEUE_CAP];  /* e[0] = newest */
    int      count;
    unsigned version;                   /* ++ on every change; renderers cache on it */
} MfdMsgQueue;

void mfd_msg_init(MfdMsgQueue *q);
void mfd_msg_add(MfdMsgQueue *q, MfdMsgId id, const char *token_value, const char *details,
                 MfdMsgResolvedFn res, MfdMsgClearFn clr, void *ctx);
void mfd_msg_show_error(MfdMsgQueue *q, const MfdFmsError *err, int with_details);  /* showFmsErrorMessage */
int  mfd_msg_remove_text(MfdMsgQueue *q, const char *text);   /* first exact match; returns 1 if removed */
int  mfd_msg_remove_id(MfdMsgQueue *q, MfdMsgId id);          /* PORT: all entries with id */
int  mfd_msg_has_id(const MfdMsgQueue *q, MfdMsgId id);
void mfd_msg_clear_latest(MfdMsgQueue *q);                     /* CLEAR INFO */
void mfd_msg_update(MfdMsgQueue *q);                           /* every 250 ms FMS cycle */
const MfdMsgEntry *mfd_msg_footer_entry(const MfdMsgQueue *q); /* first uncleared or NULL */
int  mfd_msg_list(const MfdMsgQueue *q, const MfdMsgEntry **out, int max); /* MSG LIST rows */
```

The ATCCOM queue reuses `MfdMsgQueue` with `mfd_msg_show_error` mapping to NX ids; it simply never calls
`mfd_msg_update`. All calls happen on the X-Plane flight-loop thread; no locking needed.

### 11.6 Required unit tests (`native/avionics/test_mfd_formats.c`, `test_mfd_messages.c`)

- Every test-vector row of section 4 (input, expected status, value, rendered text, message text).
- `mfd_js_to_fixed`: `(0.5,0)="1"`, `(2.5,0)="3"`, `(-2.5,0)="-3"`, `(300.25,1)="300.3"`, `(1.005,2)="1.00"`,
  `(289.982,1)="290.0"`.
- `mfd_js_number`: `" 20"`=20, `"1E2"`=100, `"0X1F"`=31, `"1."`=1, `".5"`=0.5, `"."`/`"-"`/`"2 0"`/`"-0x1"` = NaN.
- Queue: add Type I twice -> 2 entries; clear -> 1 left; Type II cleared then re-added with same text -> old removed,
  new uncleared at index 0; `update` removes resolved; capacity overflow drops oldest cleared.
- `qnhToMillibar(29.92)` within 1e-6 of 1013.207489.

---

## 12. Consolidated FBW quirks and port decisions

| # | FBW behaviour | Port decision |
|---|---|---|
| Q1 | `Number()` accepts `1E2`, `0X1F`, `Infinity` | strict grammar (flag for faithful mode) |
| Q2 | Mach `.95` -> FORMAT ERROR instead of RANGE | reinterpret only digit-only input |
| Q3 | Heading/InboundCourse/HoldDist/HoldTime unbounded ("TO Infinity") | 0..359.9 / 0..360 / 0..99.9 / 0..9.9 |
| Q4 | TripWind re-validation flips HD to TL | re-validate with lead unit, or skip parse on unchanged ENT |
| Q5 | TimeHHMMSS invalid input clears the field silently | FORMAT / RANGE errors |
| Q6 | TimeHHMM(SS) value 0 renders as placeholder | render `00:00` / `00:00:00` |
| Q7 | LsCourse: `-5` gives back course, 4th digit dropped | strict `F?NNN` |
| Q8 | Squawk 0000 rejected silently; BCD write | accept; write decimal-digit int to X-Plane dataref |
| Q9 | FL decimals accepted, displayed rounded | FORMAT ERROR on decimals |
| Q10 | AltOrFL transition not applied until first change | apply at construction |
| Q11 | ESC on empty numeric field -> FORMAT ERROR | ESC aborts edit |
| Q12 | `canBeCleared=false` not enforced | enforce |
| Q13 | remove-by-text fails for substituted texts (NEW CRZ ALT) | remove by id |
| Q14 | CHECK SPD MODE resolved one cycle after insertion | resolve on managed speed / preselected speed |
| Q15 | Unbounded message queue | capacity 16, evict oldest cleared |
| Q16 | `forEach`+`removeAt` skips entries in `update()` | iterate backwards |
| Q17 | Some fields drop error details | keep per-field flag (parity) |
| Q18 | Exceptions in `dataHandlerDuringValidation` swallowed without message | page handlers return `MfdFmsError` explicitly |
| Q19 | `toFixed` vs C `printf` rounding differ on ties | `mfd_js_to_fixed` |

---

## 13. Open questions

1. `FmsError.ts` and `WaypointEntryUtils.ts` are reconstructed **[R]**: fetch `fbw-a32nx/src/systems/fmgc/src/` at the
   pinned commit (it is a sibling tree of the same FBW monorepo) to confirm the place/runway regexes and the PBX/PBD
   limits before implementing section 5.
2. msfs-sdk `UnitType.POUND` factor assumed 0.453592 **[R]**; confirm against the SDK version FBW pins. The port's
   recommendation (0.45359237) changes no displayed range text in section 4.8.
3. Real A380 behaviour for Type I messages on the *other* MFD (shared vs per-side) is not established; FBW shares.
4. Real MSG LIST capacity / scrolling (FBW: 4 rows, warns above 5).
5. Real-aircraft ranges for IRS SET HDG, hold inbound course, hold leg time/distance (FBW: unbounded).
6. Footer height depends on the line-height metric choice (78 px with typo metrics vs ~69 px with hhea); align with
   the layout spec.
