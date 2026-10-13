# MFD spec 06: POSITION, SURV, ATCCOM and FCU BKUP pages

Status: implementation spec for work package **WP-H** (`a380x_mfd_page_position.c`, `a380x_mfd_page_surv.c`,
`a380x_mfd_page_atccom.c`, `a380x_mfd_page_fcubkup.c`, spec 12 §3) plus the adapter items it needs from **WP-J**.
Upstream: FlyByWire A380X pinned at `2baa2b35` (GPL-3.0). Paths without a prefix are relative to
`upstream-aircraft/fbw-a380x/src/systems/instruments/src/`. `DEF:` = `MFD/pages/common/DataEntryFormats.tsx`,
`WS:` = `MFD/pages/common/widget-style.scss`, `ST:` = `MFD/pages/common/style.scss`.

| § | Content |
|---|---|
| 0 | Conventions, tags, the layout model and the **overflow rule** |
| 1 | POSITION/MONITOR |
| 2 | POSITION/IRS |
| 3 | POSITION/NAVAIDS |
| 4 | SURV/CONTROLS |
| 5 | SURV/STATUS & SWITCHING |
| 6 | ATCCOM: system logic, offline behaviour, footer, stubs |
| 7 | ATCCOM/CONNECT |
| 8 | ATCCOM/MSG RECORD (+ ALL MSG, MONITORED MSG, EXPAND) |
| 9 | ATCCOM/D-ATIS LIST and RECEIVED, incl. D-ATIS synthesis from X-Plane weather |
| 10 | FCU BKUP/AFS CP (design) |
| 11 | FCU BKUP/EFIS CP (design) |
| 12 | X-Plane data/command map (verified against XP 12.4.3 `DataRefs.txt` / `Commands.txt`) |
| 13 | Systems-host interface (`A380XMfdSystemsHost`) additions |
| 14 | Quirks table |
| 15 | Open questions |

---

## 0. Conventions

### 0.1 Tags

| Tag | Meaning |
|---|---|
| **[FBW]** | Implemented upstream; reference behaviour. |
| **[FBW-NA]** | Placeholder upstream: hard-coded sample data, `disabled={true}`, empty `onClick`, `TODO`, a `NOT YET IMPLEMENTED` overlay, or an unrouted URI (404). |
| **[QUIRK]** | FBW artefact; do not replicate, the port behaviour is given next to it. |
| **[PORT]** | Recommendation for the X-Plane port. Where no [PORT] note exists, implement [FBW]. |

### 0.2 Geometry model (normative: spec 01 §0, spec 12 §2)

* Canvas 768 x 1024, origin top-left, y down. Rects are `x0..x1 × y0..y1` (half-open). Text positions are
  **baseline-left** `(x, y)` or **centre-x / baseline** `cx, y`.
* Line box = 1.0 x font size; baseline = top + 0.7998·fs; advance = 0.626953·fs per character.

| fs | 20 | 21 | 22 | 24 | 25 | 26 | 27 | 28 | 30 | 35 |
|---|---|---|---|---|---|---|---|---|---|---|
| baseline below top | 16.0 | 16.8 | 17.6 | 19.2 | 20.0 | 20.8 | 21.6 | 22.4 | 24.0 | 28.0 |
| advance | 12.54 | 13.17 | 13.79 | 15.05 | 15.67 | 16.30 | 16.93 | 17.55 | 18.81 | 21.94 |

* Box model per spec 01 §0.3 (content-box, all rects below are outer). Widget sizes come from spec 01 §13:
  Button `width:W` → outer `W+28`, one line 40 tall, n lines `18+22n`; explicit `height:H` → outer `H+18`;
  InputField outer 49 tall, `containerStyle width:W` → outer `W+8`; value baseline `y+32.6` (27 px) or
  `y+31.1` (22 px computed), 20 px units baseline `y+30.5`, 24 px units (`bigUnit`) baseline `y+31.7`;
  DropdownMenu 44 tall, value baseline `y+30.1`; SurvButton/AdscButton 118 x 83 (`width/height` overrides add 18);
  SurvStatusButton 94 x 64; RadioButton row 38 tall, circle centre `(x+19, y+19)`, text `(x+51, y+26.8)`.
* Page frame: title bar 98..138, body **138..950**, footer 950..1024 (standard footer spec 01 §8.1; D-ATIS pages use
  the ATCCOM footer, spec 01 §8.3).
* Colours: spec 01 §3 tokens. Extra literals used here: `lightgrey #d3d3d3` (its outset dark side `#7f7f7f`),
  `darkgrey #a9a9a9`, `#3c3c3c` (SURV status button / MSG RECORD scrollbar), `rgba(0,0,0,0.7)` (ATCCOM inop veil).
* Fonts: SZ = SlashedZero (default), E = Ecam (only `.mfd-surv-heading`, `.mfd-adsc-label-off`).

### 0.3 Overflow rule (important)

Computed with the normative metric model, several FBW pages need **more than the 812 px body** (or more than
768 px width): MONITOR +64.5 px, IRS +5, NAVAIDS (SELECTED FOR FMS NAV tab) +18, SURV CONTROLS +75 (and +44 px
horizontally in the TAWS row), SURV STATUS +100 px horizontally, CONNECT +9 (and +20 px horizontally), D-ATIS LIST
+21 px. How Coherent GT resolves this (flex shrink below content, or clipping) cannot be verified offline
(see Open question 1: the FBW constants fit almost exactly under `box-sizing: border-box`).

**[PORT] normative:** every page in this file is given as **one fitted layout** (the "PORT layout"). Where it differs
from the natural FBW computation, the difference is listed as a numbered *fit adjustment* and only margins/gaps are
changed; widget sizes, texts, fonts, colours, order and horizontal positions stay FBW. Implement the PORT layout.

### 0.4 Common page facts

* POSITION pages are `FmsPage`s: title text `POSITION/MONITOR`, `POSITION/IRS`, `POSITION/NAVAIDS` (FMS style title
  bar with EO/TMPY/PENALTY sections, spec 01 §7). SURV and ATCCOM pages use `ActivePageTitleBar` with literal titles
  (non-FMS style).
* Page-local state resets on every navigation (spec 01 §10.1). Exceptions explicitly listed (shared FMS data).
* Update rates [FBW]: MONITOR 2 Hz, IRS 1 Hz (+ on maintenance-word change), NAVAIDS 1 Hz. **[PORT]** evaluate
  bindings every frame (cheap) but recompute geodesy at the FBW rate.
* Coordinates text: `coordinateToString(lat, lon, false)` (`fbw-common/src/systems/instruments/src/Convert.ts:63-110`)
  = `DD°MM.dN/DDD°MM.dE`. **[QUIRK]** the tenths digit is `ceil(sec/100)` of a DMS split (values 0..6, not tenths).
  **[PORT]** use true tenths of a minute, `floor(frac(minutes)·10)` (same as spec 03). Placeholder
  `noPositionAvailableText` = `--°--.--/---°--.--` (18 chars, `MFD/shared/utils.ts:54`), which has two decimal
  dashes while values have one; keep both as given (18 vs 17 chars).

---

## 1. POSITION/MONITOR (`MFD/pages/FMS/POSITION/MfdFmsPositionMonitor.tsx`, `.scss`)

URI `fms/position/monitor[/withReturn]`. Footer is inside `.mfd-page-container` (body + footer = 138..1024).

### 1.1 Data and logic [FBW] → port source

| Item | FBW (file:line) | Display rule | [PORT] source |
|---|---|---|---|
| NAV PRIMARY | `!navigation.getGpsPrimary()` → lost (`:245`) | `NAV PRIMARY` green 30, or `NAV PRIMARY LOST` **amber** 30 (`:120-124`) | FMS core `gps_primary` = GPS valid (§12.2) **and** FM position valid (spec 07 §9.4) |
| ACCURACY | `navigation.isAccuracyHigh()` (`:246`) | label+value visible **only when NAV PRIMARY LOST** (`:130`); `HIGH` green / `LOW` amber (`:126-128`) | `epu_nm <= rnp_nm` |
| EPU | `navigation.getEpe()` (`:247`) | `v===Infinity ? '-.--' : v.toFixed(2)`, padded to 5 chars with NBSP (`:32`); unit `NM` hidden when Infinity | FMS core `epu_nm` (model §1.4) |
| RNP | `getActiveRnp()`, `isPilotRnp()` (`:243,248-249`) | InputField RnpFormat (spec 02 #42, `--.-`, 0.01..20.0 NM), pilot entry cyan 27, computed cyan 22 | FMS core `rnp_nm` + `rnp_pilot`; entry `setPilotRnp(v)`; clear → null → default |
| POS1 / POS2 | `navigation.getPpos()` (`:250-255`); POS2 **= POS1** (`:112`, TODO 2nd FMS) | coordinate text green 30 | FM position (§1.4) for both [FBW] |
| POS1/POS2 mode | always `''` (`:153-155`) **[FBW-NA]** | empty | keep empty (FBW mode strings such as `GPIRS` not modelled) |
| GNSS1 | `GPS POSITION LAT/LON` simvars (`:290-292`), only refreshed while sensors shown and not frozen | coordinates | `sim/flightmodel/position/latitude/longitude` if GPS1 valid else placeholder |
| GNSS2 | **= GNSS1 text** (`:146`) **[FBW-NA]** | | **[PORT]** same rule with GPS2 (`rel_gps2`, `gps2_power`) |
| IRS1..3 | `L:A32NX_ADIRS_IR_n_LATITUDE/LONGITUDE` ARINC; placeholder if SSM invalid (`:338-379`) | coordinates | `a380x/xp12/adirs/ir{n}_latitude_deg/_longitude_deg` valid iff `ir{n}_nav_valid` |
| RADIO | always placeholder (`:114`) **[FBW-NA]** | | **[PORT]** same value as NAVAIDS `RADIO POSITION` (§3.1) when available, else placeholder |
| MIXIRS | mean of valid IR lat/lon (`:294-319`) | coordinates | same, over the port ADIRS units |
| DEVIATION FROM POSn | `n` = 1 on CAPT, 2 on FO (`:148`) | header text | per side |
| IRSn deviation | great-circle NM IR→FM, `toFixed(1)`; `--.-` if IR or FM invalid; unit `NM` hidden when `--.-` (`:374-378`) | value green 30 | same. In X-Plane the IR position equals the sim position, so the value is `0.0` (never fabricate drift) |
| GPS DESELECTED | constant false (`:191`) **[FBW-NA]** | amber 30, hidden | keep hidden (no GPS deselect function) |
| Sensors visible | page-local, button DISPLAY/HIDE POS SENSORS (`:381-387`) | left table `visibility:hidden` when false (space kept); IRS1..3 deviation identifiers visible **only when the table is hidden** (`:185-187`) | |
| FREEZE | page-local (`:389-404`): freezing forces the table visible (and remembers whether the pilot had shown it); unfreeze hides it again unless the pilot had shown it; hiding the table unfreezes | while frozen, sensor rows (GNSS/IRS/MIXIRS) stop updating; `POS DATA FROZEN` / ` AT HH:MM` (UTC at the moment of freezing, `getEtaFromUtcOrPresent(0,false)`) | **[QUIRK]** FBW recomputes the time string only when the subscription fires, which is correct; IR **deviations** keep updating while frozen (FBW: only positions freeze). Keep. |
| BRG / DIST TO | `fmgc.data.positionMonitorFix` (**shared FMS data, survives navigation**, spec 07 §2 table `positionMonitorFix`) | FixFormat (spec 02 #20); `getOrCreateWaypoint(fms, text, true)`; not found → `NOT IN DATABASE` | FMS core `position_monitor_fix` |
| Bearing | magnetic initial great-circle course FM→fix (`MagVar.get(ppos)`) (`:322-333`); `v ? toFixed(0).padStart(3,'0') : '---'`; unit `°` or NBSP | **[QUIRK]** bearing 0 shows `---`; **[PORT]** test for null, show `000` | magvar via `XPLMGetMagneticVariation(lat,lon)` (XPLMScenery.h:195) |
| Distance | NM, `toFixed(v>9999?0:1).padEnd(6,NBSP)`, else `----.-`; unit `NM` | **[QUIRK]** unit visibility keyed on **bearing** (`:171`) and `0` distance shows dashes; **[PORT]** key both on the fix/FM validity | |
| POSITION UPDATE | disabled, no action **[FBW-NA]** | | keep disabled |
| GNSS button | disabled → `fms/position/gnss` (unrouted) **[FBW-NA]** | | keep disabled |
| NAVAIDS button | → `fms/position/navaids/nav` (opens tab 2 and shows RETURN) | | |
| IRS button | → `fms/position/irs` | | |
| RETURN | visible iff `extra == 'withReturn'` (`:195`); → back | | |

### 1.2 PORT layout (fit adjustments M1–M6 listed in §1.3)

Rows are given top→bottom. "L24" = `.mfd-label.bigger` (SZ 24 white), "V30" = `.mfd-value.bigger` (SZ 30 green),
"V25" = `.mfd-value` (SZ 25 green).

| Element | Geometry / text |
|---|---|
| NAV PRIMARY | V30 (amber when LOST) at (93, 188.5) |
| `RNP` | L24 at (445.9, 183.7) |
| RNP field | InputField RnpFormat, `width:155px`, `bigUnit` → outer **506.0..669.0 × 152..201**; text centred in 516.0..628.9 (cx 572.4), baseline 184.6 (27) / 183.1 (22 computed); unit `NM` 24 px at x 634.9, baseline 183.7, visible only when value non-null |
| `ACCURACY` | L24 at (93, 239.2); value V30 (`HIGH` green / `LOW` amber) at (228.4, 241.0); both hidden unless NAV PRIMARY LOST |
| `EPU` | L24 at (453.8, 239.2); value V30 at (535.9, 241.0) (5-char padded); `NM` L24-unit `#0c66f7` 24 at (635.9, 239.2), hidden when EPU invalid |
| ` POS1` | L24 at (16, 285.2) (leading NBSP, glyphs start 31.0); value V30 at (106.2, 287.0) |
| ` POS2` | L24 at (16, 335.2); value V30 at (106.2, 337.0) |
| rule A | 1 px outset `#c4c6cf` box `5..751.96 × 353..356` (top/left `#c4c6cf`, bottom/right `#74757b`) |
| sensor table (hidden unless sensors visible) | labels L24 at x 17 (` IRS1/2/3` with NBSP), values V30 at x 107.2 (MIXIRS: label x 3, value x 108.3). Rows (label baseline / value baseline): GNSS1 389.2 / **396.0** (FBW `top:5px` shift), GNSS2 432.2 / 434.0, rule `5..452 × 445..448`, IRS1 485.2 / 487.0, IRS2 528.2 / 530.0, IRS3 571.2 / 573.0, rule `5..452 × 589..592`, RADIO 629.2 / 631.0, MIXIRS 676.2 / 678.0 |
| `GPS DESELECTED` | V30 amber at (472.9, 411.0), hidden |
| `DEVIATION FROM POS1/2` | L24 at (472.9, 453.2) |
| deviation rows | identifier L24 `IRS1/2/3` at x 472.9; value V30 at x 597.1; unit `NM` 24 at value_end+6. Baselines label/value: IRS1 490.2/492.0, IRS2 527.2/529.0, IRS3 564.2/566.0 |
| frozen time | L24, centred on cx 608.85: `POS DATA FROZEN` baseline 634.7, ` AT HH:MM` baseline 658.7 (FBW: `position:fixed` 496,632 — **[QUIRK]** fixed is viewport-relative, so on the shared 1646-px texture the FO text would land on the CAPT screen; port draws it on its own side) |
| DISPLAY/HIDE POS SENSORS | Button `width:219;height:58` → **95..342 × 691..767**; two lines `DISPLAY`|`HIDE` / `POS SENSORS` centred cx **206.0** (FBW span `margin-right:25`), baselines 726.6, 748.6; `selected` = sensors visible |
| FREEZE/UNFREEZE POS DATA \* | Button `width:212;height:58` → **468..708 × 691..767**; lines centred cx 576.1 baselines 726.6/748.6; `*` at (641.25, 737.6); `selected` = frozen |
| rule B | `5..751.96 × 773..776` |
| POSITION / UPDATE | Button `width:138;height:59`, disabled → **0..166 × 793..870**; lines cx 83, baselines 829.1/851.1 (**[QUIRK]** literal `<br />` string; PORT two lines, spec 01 Q7) |
| `BRG / DIST TO` | SZ 22 white at (409.7, 819.1) |
| fix field | InputField FixFormat → **599..745 × 788..837** (**[QUIRK]** FBW `containerStyle='"width:130px;'` has a stray quote, the declaration is invalid → auto width 146; port keeps 146); text baseline 820.6 (27) / 819.1 (22 when not pilot-entered incl. empty) |
| bearing / distance | V25 baseline 865.0: bearing at x 351, unit (20 px blue) `NBSP°` at x 404; `/` at x 449.1; distance at x 478.8; unit `NBSP NM` at x 578.8 |
| rule C | `5..751.96 × 878..881` |
| RETURN | Button `width:150` → **0..178 × 889..929**, text cx 89 baseline 917.6; visible per §1.1 |
| NAVAIDS / GNSS / IRS | Buttons `height:43` → 61 tall, **y 889..950**: NAVAIDS 251..428 (cx 339.5), GNSS 433..594 (cx 513.5, disabled), IRS 599..763 (cx 681.0); text baseline 928.1 |
| footer | standard, 950..1024 |

### 1.3 Fit adjustments (natural FBW = +64.5 px)

| # | FBW | PORT |
|---|---|---|
| M1 | top rows `margin-bottom:4` (`.scss:9`) | 2 |
| M2 | POS1 row `margin-bottom:14.5` (`.tsx:451`) | 6 |
| M3 | rule `margin-bottom:8` after POS2 | 4 |
| M4 | button row `margin-bottom:19` (`.tsx:580`) | 6 |
| M5 | POSITION UPDATE `margin-top:18`; rule C `margin-top:34` | 13; 8 |
| M6 | rule B/C `margin-bottom:8` | B: 4, C: 8 |

FBW natural positions for reference: sensor table starts at 376.5 (port 360), buttons at 707.5 (port 691), bottom
row would end at 1014.5 and the footer at 1088.5.

### 1.4 FM position, EPU and RNP model [PORT]

The FBW `Navigation`/`RequiredPerformance` classes live in `@fmgc` (fbw-a32nx), **not in this checkout**; behaviour
below is a port model, marked *(unverified vs FBW)*.

* **FM position valid** = at least one ADIRU `ir{n}_nav_valid` **or** a valid GPS. Value = sim lat/lon
  (`sim/flightmodel/position/latitude/longitude`, double).
* **GPS valid (per receiver)** = `sim/cockpit2/radios/actuators/gps_power` (`gps2_power`) == 1 **and**
  `sim/operation/failures/rel_gps` (`rel_gps2`) != 6 **and** avionics powered. `gps_primary` = GPS1 or GPS2 valid.
* **EPU** *(unverified)*: GPS primary → 0.05 NM; else radio position (VOR/DME or DME/DME, §3.1) → 0.30 NM; else
  IRS only → `epu_at_loss + 2.0 NM/h · t` (capped 20.0); no FM position → Infinity (`-.--`).
* **RNP default** *(unverified)*: by flight area — approach (active leg in the approach segment) 0.30, terminal
  (within 30 NM of origin/destination below FL150 equivalent) 1.00, en route 2.00; database leg RNP wins over area
  default; pilot entry wins over both until cleared or the flight plan is cleared/done.
* `isAccuracyHigh = epu <= rnp`. Messages NAV ACCUR DOWNGRAD/UPGRADED and NAV PRIMARY (LOST) are spec 07 §7.

---

## 2. POSITION/IRS (`MfdFmsPositionIrs.tsx`, `.scss`)

URI `fms/position/irs`. Footer outside the page container (body 138..950).

### 2.1 IR status logic [FBW] and port mapping

FBW per IR (`:237-277`) uses `L:A32NX_OVHD_ADIRS_IR_n_MODE_SELECTOR_KNOB` (1 NAV, 2 ATT) and the ARINC maintenance
word (bit 1 ALIGN, 2 ATT, 3 NAV, 4 ENTER HDG, 8 SWITCH ADR, 9/14 IR FAULT, 13 EXCESS MOTION, 16-18 minutes to NAV).

| Column | FBW rule | [PORT] rule from `a380x/xp12/adirs/ir{n}_*` (port ADIRS, `native/avionics/a380x_adirs.h/.c`) |
|---|---|---|
| col1 | `IRS 1/2/3` label 20 white | same |
| status (V30 green) | knob OFF → `OFF`; else ALIGN / ATT / NAV / `INVALID` | `mode==0` → `OFF`; `fault` → `INVALID`; `aligning` → `ALIGN`; `mode==2` (ATT) && !aligning → `ATT`; `mode==1` && `nav_valid` → `NAV`; else `INVALID` |
| col3 (V25) | only when ALIGN: `AVAIL IN n MIN` from bits 16-18, `AVAIL IN > 7 MIN` | `aligning`: `m = ceil(alignment_remaining/60)`; `m>7` → `AVAIL IN > 7 MIN`, `m>=1` → `AVAIL IN m MIN`; else empty |
| col4 (V25) | priority IR FAULT > ENTER HDG > EXCESS MOTION > SWITCH ADR | `fault` → `IR FAULT`; `heading_required` → `ENTER HDG`; NAV mode, on ground, not aligned, not `nav_locked`, ground speed > 0.5 kt → `EXCESS MOTION`; SWITCH ADR not modelled |
| **[QUIRK]** | col3/col4 are only written in the branch that sets them, so stale text remains (e.g. `ENTER HDG` stays after OFF) | recompute every update; OFF clears col3/col4 |

Alignment banner (`:124-146`): all three IR in NAV or ATT → `IRS ALIGNED ON GPS POS:` else `IRS ALIGNING ON GPS POS:`
(`ON REF POS` variants unreachable, `irsAreAlignedOnRefPos` constant false). Position = FM position or `0,0` if null.
**[QUIRK]** both texts are set only when the aligned state toggles (snapshot) and show `00°00.0N/000°00.0E` when no
FM position. **[PORT]** refresh at 1 Hz, placeholder when invalid.

`ALIGN ON OTHER REF` and `FREEZE ALL IRS` disabled **[FBW-NA]**.

`SET HDG` row: always `visibility:hidden` (`:178`) **[FBW-NA]**. **[PORT]** visible when any IR has
`ir{n}_heading_required==1`; field HeadingFormat, **mandatory** (amber `▯▯▯.▯`), right aligned. On commit call
host `adirs_enter_heading(unit, deg)` for every unit with `heading_required` (crew enters one magnetic heading; see
Open question 6 about the `°T` unit of HeadingFormat). Rejected (returns 0) → `NOT ALLOWED`.

IRS data block (selected IR, toggled by IRS1/2/3 buttons, NONE hides the block via `visibility`, initial IRS1):

| Label | FBW source (`:181-210`) | [PORT] |
|---|---|---|
| POSITION | IR lat/lon (**no SSM check [QUIRK]**) | `ir{n}_latitude_deg/longitude_deg` if `nav_valid` else placeholder |
| T.TRK `°T` | IR true track `toFixed(1)` | `sim/flightmodel/position/hpath` (true), `---.-` if !nav_valid |
| GND SPD `KT` | IR GS `toFixed(1)` | `ir{n}_ground_speed_knots` |
| T.WIND `°` `/spd` `KT` | IR wind dir / speed | `sim/weather/aircraft/wind_now_direction_degt`, `wind_now_speed_msc`·1.943844; dashes when !nav_valid or IAS < 100 kt? keep FBW (always) |
| T.HDG `°T` | IR true heading | `sim/flightmodel/position/true_psi` (valid iff `heading_valid`) |
| MAG HDG `° ` | IR heading | `ir{n}_heading_deg` (valid iff `heading_valid`) |
| MAG VAR `°E/°W` | `HEADING − TRUE_HEADING`, `<0 → W` **[QUIRK]** sign inverted (east variation shows W) | `v = sim/flightmodel/position/magnetic_variation` (true = mag + v); `|v|` with `°E` if v>0 else `°W` |
| GPIRS POSITION | FM position | FM position |
| ACCURACY `FT` | `getEpe().toFixed(0)` = **NM value with FT unit [QUIRK]** | `round(epu_nm·6076.12)` FT, placeholder `----` |

### 2.2 PORT layout (fit adjustment I1: IRS table `margin-top:40` → 35; natural FBW +5 px)

| Element | Geometry / text |
|---|---|
| banner label | SZ 20 **amber `#e68000`**, centred cx 214.7, baseline 174.0 |
| banner position | V30 at (414.4, 177.0) (**[QUIRK]** the 2nd div has `style="flex: 1 display: flex; ..."`, an invalid declaration, so it is not flex: text is left-aligned; keep) |
| ALIGN ON / OTHER REF | Button disabled, **0..152.1 × 198..260**, lines cx 76.05 baselines 226.6/248.6 |
| separator | `0..768 × 280..282` `#d3d3d3` |
| status table | 3 rows: **317..363, 363..409, 409..453**; columns `0..100.72`, `100.72..257.92`, `257.92..542.2`, `542.2..768` (FBW 12/20/38/30 % plus padding overflow 62 px, flex-shrunk); 2 px `#868a9c` right borders at x 98.72, 255.92, 540.2 (full 317..453) and bottom borders under rows 1-2 (y 361..363, 407..409) |
| table text | centred: col1 cx 49.36 (20 white), col2 cx 178.32 (30 green), col3 cx 399.06 (25 green), col4 cx 655.1 (25 green). Baselines row1 345.0 / 348.0 / 346.5; row2 391.0 / 394.0 / 392.5; row3 437.0 / 440.0 / 438.5 (label / status / cols 3-4) |
| SET HDG | label SZ 20 at (512.2, 493.5); field `width:150px; margin-left:10px` → **610..768 × 463..512**, unit `°T` 20 px at x 738.9 |
| IRS1/IRS2/IRS3 | Buttons `width:150` → **114..292, 295..473, 476..654 × 532..572**, cx 203/384/565 baseline 560.6; `selected` = shown IR |
| data box | `575..910`, full width, 2 px outset `#d3d3d3`/`#7f7f7f`, content `17..751 × 579..893` |
| FREEZE / ALL IRS | Button disabled **17..141.6 × 579..641**, cx 79.3, baselines 607.6/629.6 |
| `POSITION` | SZ 20 at (292.1, 635.0); value V30 at (412.4, 635.0) |
| data rows | 656..700, 700..744, 744..788 (+ `#d3d3d3` 2 px rule `17..751 × 788..790`). Cells: label cells right-aligned to x 174.67 and 579.33 (SZ 20); value cells right-aligned to x 414.67 and 744.0 (V30 + units 20 px blue, unit gap 6). Baselines label/value: 684.0/687.0, 728.0/731.0, 772.0/775.0. Row3 col2 (`dir° /spd KT`, ≥ 237.6 px) overflows its 226-px cell to the left; **[PORT]** keep it right-aligned to 414.67 (FBW min-content widens that row's cell and shifts columns 3-4 by ~12 px **[QUIRK]**) |
| `GPIRS POSITION` | SZ 20 at (213.5, 833.0); value V30 at (404.0, 836.0) (span `width:325px`, the 18-char text overflows 13.6 px right) |
| `ACCURACY` | SZ 20 at (288.7, 877.0); value V30 right-aligned to 704.0, baseline 880.0; `FT` 20 px blue at (704.0, 877.0) |
| RETURN | Button **0..110.8 × 910..950**, cx 55.4 baseline 938.6 (always visible) |

---

## 3. POSITION/NAVAIDS (`MfdFmsPositionNavaids.tsx`, `.scss`)

URI `fms/position/navaids[/withReturn|/nav]`; `nav` selects tab 2 (`:53-55`); RETURN visible for both extras.

### 3.1 Logic [FBW] (navaid tuner in `@fmgc`, not in checkout) and port model

| Field | FBW | [PORT] |
|---|---|---|
| VOR1/VOR2 IDENT | NavaidIdentFormat (spec 02 #22), `handleVorIdent` (`:234-262`): empty → clear manual (or `NOT ALLOWED` if not manual); else VOR search (`searchVor`) + DUPLICATE NAMES; deselected → `XXXX IS DESELECTED`; else `setManualVor(i, navaid)` | port NavaidTuner (FMS core). Lookup `XPLMFindNavAid(NULL, ident, lat, lon, NULL, xplm_Nav_VOR|xplm_Nav_DME)` candidates → duplicate-names dialog (spec 05) |
| VOR FREQ | FrequencyVORDMEFormat 108.00..117.95 (spec 02 #36); `setManualVor(i, freq)` | write `nav_frequency_hz[i]` (10 kHz units) |
| VOR CRS | InboundCourseFormat (spec 02 #32); `setVorCourse(i, v || null)` **[QUIRK]** course 0 clears | `nav_obs_deg_mag_pilot/_copilot[i]` (both sides); `0` accepted |
| VOR CLASS | `dmeOnly ? 'DME' : 'VOR/DME'` when ident | from XPLMNavType of the tuned facility: VOR (+DME co-located → `VOR/DME`), DME only → `DME`, TACAN → `VOR/TAC` |
| entered-by-pilot | ident manual with facility → ident cyan 27; freq-only manual → freq cyan 27; auto → cyan 22 | same |
| LS IDENT/FREQ | `handleIlsIdent/Freq`; MMR tuning locked → `NOT ALLOWED` (**[QUIRK]** continues anyway) ; FrequencyILSFormat 108.00..111.95 | port MMR (both MMRs tuned together). Locked when LOC/GS captured (`guidance.lateral_mode` LOC*/LAND) → `NOT ALLOWED` and **return** |
| LS CRS | LsCourseFormat (spec 02 #38), `setIlsCourse(|v|, v<0)`; back course `B` → FORMAT | port: front course only (spec 02 PORT rule) |
| LS SLOPE | `mmr.slope.toFixed(1)` or `---`, unit `°` | GS angle from navdata: `XPLMGetNavAidInfo(gs).outHeading` = `angle·100000 + course` for glideslope records |
| LS CLASS | `ILS/DME` when ident **[QUIRK]** even without DME | `ILS/DME` if a DME is paired, else `ILS` |
| DESELECT GLIDE \* | disabled, `deselectGlide()` TODO **[FBW-NA]** | keep disabled |
| SELECTED FOR FMS NAV | `navigation.getSelectedNavaids()[1..3]`; ident Button (disabled, menu `DATA NAVAID`), freq `toFixed(2)`, class `NAVAID_TYPE_STRINGS` | port: up to 3 navaids the FMS core uses for radio position (Phase 1: none → rows invisible) |
| RADIO NAV MODE / POSITION | "fake it": `DME/DME` if slots 1&2 have facilities, `VOR/DME` if slot 1, position = plane lat/lon (`:189-210`) | same rule on the port slots |
| deselected list | 6 fields NavaidIdentFormat(`-`) (placeholder `-`); field k+1 disabled while field k empty; entry → `deselectNavaid(databaseId)`, clearing → reselect; ident shown = `databaseId.substring(7)` **[QUIRK]** | store idents (max 6) in FMS core; tuner never auto-selects a deselected navaid |

Receiver allocation **[PORT]** (needs WP-J, see Open question 3): VOR1 = X-Plane NAV1 (index 0), VOR2 = NAV2
(index 1) — the port ND already reads NAV1/NAV2 for VOR pointers (`a380x_efis_nd.inc:163-176`). MMR (LS) = **NAV3
(index 2, CAPT PFD) and NAV4 (index 3, FO PFD)** tuned to the same frequency/course; the PFD/guidance ILS inputs in
`a380x_fcu_adapter.inc:365-378` must move from `nav1/nav2_*` to `nav_*[2]/[3]` arrays and the GS angle from navdata
(no `nav3_slope_degt` exists). Until then LS tuning writes NAV1+NAV2 (and VOR tuning is suspended).

### 3.2 PORT layout (fit adjustment N1: one fixed tab frame for both tabs)

FBW natural: the tab container and the bottom spacer share the free space, so the LS block moves between tabs
(TUNED tab: LS block at y 543.5; SELECTED tab: content 18 px too tall, tab clipped by `overflow:hidden`, LS block at 610).
**[PORT]** fixed frame for both tabs; the LS block shares its vertical band with RETURN (no x overlap).

| Element | Geometry / text |
|---|---|
| TopTabNavigator | container `2..766`, bar `140..176`, angle 25° (tw 16.79), 2 tabs → font 24: `TUNED FOR DISPLAY` tab `2..391.5` (text cx 196.75, x 68.9), `SELECTED FOR FMS NAV` tab `376.5..766` (cx 571.25, x 420.8), baseline 167.2; selected text white |
| tab content frame | **2..766 × 176..640** (2 px raised, no top border), content `14..754 × 186..628` |
| **TUNED FOR DISPLAY** | three columns: VOR1 cx **246.15** (x 179.65..312.65), labels cx **384.0**, VOR2 cx **521.85** (455.35..588.35); rows 50 tall at y 186, 236, 286, 336, 386 |
| — headers | `VOR1`, `VOR2` SZ 20 centred, baseline 217.0 |
| — labels | `IDENT` 267.0, `FREQ` 317.0, `CRS` 367.0, `CLASS` 417.0 (SZ 20, cx 384) |
| — fields | InputFields `width:125px` → 133 wide, y 236.5..285.5 (IDENT), 286.5..335.5 (FREQ), 336.5..385.5 (CRS); text centred, baseline field_y+32.6 |
| — CLASS values | V25 centred, baseline 418.5 |
| — rule | `14..754 × 456..463`, bottom 2 px `#a9a9a9` at 461..463 |
| **SELECTED FOR FMS NAV** | grid cols `14..199`, `199..439.5`, `439.5..680` |
| — header row | 186..227: `IDENT` cx 106, `FREQ/CHAN` cx 318.75, `CLASS` cx 559.75, SZ 20 baseline 212.0; 1 px `#c4c6cf` bottom at 226..227 under all three; 1 px vertical lines at x 198..199 and 438.5..439.5 for y 186..347 |
| — rows k=0..2 | y `227+40k .. 267+40k`: ident menu Button (disabled, arrow, min-width 107 → 128 wide, `42..170`), invisible if ident empty; freq V25 cx 318.75; class V25 cx 559.75; baseline row+27.5 |
| — `RADIO NAV MODE` | SZ 20 right-aligned to x 284 (**[QUIRK]** FBW `justify-content:right` on a block div has no effect, text sits at x 94), baseline 373.0 (N2: grid `margin-bottom` 20 → 10); value V25 at (304, 377.0) |
| — `RADIO POSITION` | baseline 418.0; value V25 at (304, 422.0) |
| — rule | `14..754 × 447..451` (1 px `#d3d3d3` at 450..451) |
| — `LIST OF DESELECTED NAVAIDS` | SZ 20 at (29, 482.0) |
| — fields | 6 InputFields (92 wide): row 1 y 496..545 at x 14, 134.5, 255; row 2 y 555..604, same x |
| LS block | x **117.05..650.95**, rows 50 tall at y 642, 692, 742, 792, 842, 892 |
| — labels | right-aligned to x 229.95, SZ 20: `IDENT` 723.0, `FREQ/CHAN` 773.0, `CRS` 823.0, `SLOPE` 873.0, `CLASS` 923.0 |
| — column | cx 316.45: `LS` SZ 20 baseline 673.0; fields 249.95..382.95 at y 692.5, 742.5, 792.5; SLOPE value V25 at (283.7, 874.5) + `°` 20 px blue at (336.7, 873.0); CLASS V25 centred baseline 924.5 |
| — DESELECT / GLIDE \* | Button `width:225` disabled → **397.95..650.95 × 761..823**, lines cx 512.6 baselines 789.6/811.6, `*` at (577.7, 800.6) |
| RETURN | **0..110.8 × 910..950**, visible per §3 header |

---
## 4. SURV/CONTROLS (`MFD/pages/SURV/MfdSurvControls.tsx`, `.scss`)

URI `surv/controls`; title `CONTROLS`; standard footer outside the page container.

### 4.1 Widgets used (spec 01 §13.12, sources)

* **SurvButton** (`MsfsAvionicsCommon/UiWidgets/SurvButton.tsx`): box 118 x 83 (4 px sunken `#c4c6cf`, padding 5,
  content 100 x 65). Upper label = `labelTrue`, lower = `labelFalse`. The active label is `.mfd-value.bigger`
  (green 30), the other `.mfd-label` (white 20). Label stack centred in the content (top `y+9`):
  state true: upper baseline `y+40.5`, lower `y+62.5`; state false: upper `y+32.5`, lower `y+60.5`.
  Disabled (`:36-58`): both labels white 20, the label of the *inactive* state hidden; fill `#868a9c`, no border
  (**[PORT]** keep the 118 x 83 footprint, spec 01): visible label baseline `y+37.5` (upper) or `y+57.5` (lower).
  Labels centred on `x+59`. Click (enabled): `onChanged(!state)`. No hover style.
* **SurvStatusButton** (`SurvStatusButton.tsx`): 94 x 64, fill `#3c3c3c`, 2 px solid `#c4c6cf` (cyan hover); black
  block `x+2..x+92 × y+2..y+32`; three bars `x+9..x+85`, 4.29 tall, tops `y+6.28, y+14.85, y+23.43`, `#c4c6cf` or
  green when `active`; label SZ 22 white centred, baseline `y+52.6`. Click → `onClick` (optional).
* **SurvStatusItem** (`SurvStatusItem.tsx`): text `${label} ${active ? sys : 'OFF'}`, SZ 24; amber if `failed`, else
  green if `active`, else white.
* **AdscButton**: §7.
* **Headings** `.mfd-surv-heading` (ST:142-150): E 28 white, **amber** when failed; positioned `relative` (offsets
  below are already applied).

### 4.2 Logic and bindings

| Control | FBW behaviour (file:line) | [PORT] binding |
|---|---|---|
| XPDR heading | amber when `xpdrFailed` — constant false (`:45`) **[FBW-NA]** | amber when `sim/operation/failures/rel_xpndr == 6` |
| SQWK field | SquawkFormat (spec 02 §4.17). Commit → `K:XPNDR_SET` (BCD); value 0 → silently rejected; FORMAT → message `SQWK CODE NOT VALID` (`:281-302`) | write `sim/cockpit2/radios/actuators/transponder_code` (decimal digits, 0000-7777); accept 0000. Display = that dataref, `%04d`; `whenChanged` refresh |
| IDENT | `K:XPNDR_IDENT_ON` (`:305`) | `XPLMCommandOnce(sim/transponder/transponder_ident)`; no lit state on the MFD (`sim/cockpit2/radios/indicators/transponder_id` available if wanted) |
| ALT RPTG (SurvButton ON/OFF) | publishes `mfd_xpdr_set_alt_reporting` (`:317`). Shown state: on ground = the request; airborne = XPDR state is Mode C or S (`:214-231`). Disabled when XPDR failed or not in A/C/S | port SURV state `alt_rptg_request` (default 1); displayed as FBW |
| AUTO / STBY radio | publishes `mfd_xpdr_set_auto` (`:322-331`), `additionalVerticalSpacing=50`; index 0 (AUTO) when XPDR in Mode A/C/S, else 1; colour **green** when AUTO, **white** when STBY | port SURV state `xpdr_auto` |
| XPDR mode law | `systems-host/Misc/Communications/Transponder.ts:90-106`: unpowered/failed → OFF; !auto → STBY; airborne & alt rptg → ALT; airborne → ON (mode A); ground → GND (mode S) | write `sim/cockpit2/radios/actuators/transponder_mode`: OFF 0; STBY 1; ground 5; airborne & !altRptg 2; airborne & altRptg: TCAS STBY → 3, TA ONLY → 6, TA/RA → 7. Ground = `sim/flightmodel/failures/onground_any`. Re-evaluated every frame. Initial: AUTO when not cold & dark (`Transponder.ts:71-73`), STBY cold & dark; initial code 2000 (`:70` sets 0x2000) |
| TCAS heading | amber when `L:A32NX_TCAS_FAULT` (initial **true**) | `tcas_fault` = !avionics powered or `rel_xpndr == 6` |
| TA/RA · TA ONLY · STBY radio | index = `2 − mfd_tcas_alert_level` (2 TA/RA, 1 TA ONLY, 0 STBY); colour **green** if TA/RA else **white**; disabled when TCAS fault or XPDR OFF/STBY (`:69-79,153`). Initial selected index 2 until the bus value arrives; TCAS computer publishes TA/RA at spawn unless cold & dark (`LegacyTcasComputer.ts:302-306`) | port SURV state `tcas_alert_level` (default 2, 0 cold & dark); drives the XPDR mode law above |
| NORM · ABV · BLW radio | `mfd_tcas_alt_select` 0/1/2; colour **green** if NORM else **cyan** | write `sim/cockpit2/radios/actuators/tcas_filter`: NORM → 0 (normal), ABV → 2, BLW → 3 |
| WXR (all) | `wxrFailed` constant **true** (`:86`) → heading amber, all six SurvButtons and ELEVN/TILT radios disabled; `onChanged` no-ops **[FBW-NA]** | **keep FBW** (port has no weather-radar image, `a380x_efis.inc:4,104`). Initial states: WXR false, PRED W/S false, TURB false, GAIN AUTO true, MODE WX true, WX ON VD false, ELEVN/TILT index 0 |
| TAWS failures | TERR/GPWS failed flags of the selected `L:A32NX_WXR_TAWS_SYS_SELECTED` group (1/2; 0 → both failed) | `surv.wxr_taws_sys` (default 1); failed flags 0 (no TAWS failure model); 0 → failed |
| TERR SYS ON/OFF | `L:A32NX_GPWS_TERR_OFF = !v`; disabled when TERR failed | port SURV state `terr_sys_off`; **[PORT]** while OFF the ND terrain layer is not drawn (gate `efis[side].terr` in `a380x_efis_nd.inc:113`) |
| GPWS, G/S MODE, FLAP MODE | `L:A32NX_GPWS_SYS_OFF`, `_GS_OFF`, `_FLAPS_OFF` = `!v`; disabled when GPWS failed | port SURV state `gpws_sys_off`, `gs_mode_off`, `flap_mode_off` (X-Plane has no GPWS inhibit datarefs; only `sim/cockpit2/annunciators/GPWS` read-only). Consumed by any future GPWS aural model |
| SURV DEFAULT SETTINGS | `:233-264`: if XPDR ok → AUTO + ALT RPTG ON; if TCAS ok → TA/RA + NORM; if WXR ok → WXR defaults (never, WXR failed); if TERR ok → TERR ON; if GPWS ok → GPWS, G/S, FLAP ON | same on the port states |

All SURV states (`xpdr_auto`, `alt_rptg_request`, `tcas_alert_level`, `tcas_alt_select`, `terr_sys_off`,
`gpws_sys_off`, `gs_mode_off`, `flap_mode_off`, `wxr_taws_sys`) are **shared by both MFDs** (one aircraft system)
and published read/write as `a380x/xp12/surv/<name>` (new, WP-J) so cockpit or Lua can read them.

### 4.3 PORT layout (natural FBW +75 px vertical, +44 px horizontal in the TAWS row)

Fit adjustments: S1 SQWK `margin-top` 30→20; S2 ALT RPTG `margin-top` 20→14; S3 XPDR column `margin-bottom` 10→8;
S4 WXR grid cell vertical margin 20→12, WXR-right `padding-top` 30→10, section 2 `padding-bottom` 15→6, section 3
`margin-top` 15→6, ELEVN/TILT `margin-top` 80→60; S5 TAWS elements margin 10→4, TERR SYS `padding-right` 50→12,
DEFAULT SETTINGS container margin 10→4.

**Section 1 — XPDR / TCAS (138..438, rule `0..768 × 438..440` `#c4c6cf`)**

| Element | Geometry / text |
|---|---|
| XPDR sub-section | `5..308.6`, right border 2 px `#c4c6cf` at x 306.6..308.6, y 138..438 |
| `XPDR` | E 28 at (140.3, 163.4) |
| `SQWK` | L24 centred cx 75.4, baseline 205.2 |
| SQWK field | `width:100px` → **21.4..129.4 × 210..259**, text centred, baseline 242.6 |
| IDENT | Button `width:100` → **11.4..139.4 × 264..304**, cx 75.4 baseline 292.6 |
| `ALT RPTG` | L24 cx 75.4 baseline 335.2 |
| ALT RPTG SurvButton | **16.4..134.4 × 347..430** (`ON` / `OFF`) |
| AUTO/STBY radios | rows `157.1..275.3 × 250..288` and `× 338..376`; circles (176.1, 269), (176.1, 357); text x 208.1 baselines 276.8, 364.8 |
| TCAS sub-section | `313.6..768`, right border x 766..768, y 138..357 |
| `TCAS` | E 28 at (463.6, 165.4) |
| TA/RA group | rows y 223..261, 271..309, 319..357; circles x 402.8 (y 242, 290, 338); text x 434.8 baselines 249.8, 297.8, 345.8; separator 2 px `#c4c6cf` x 590.9..592.9, y 213..357 |
| NORM/ABV/BLW group | same rows; circles x 616.9; text x 648.9 |

**Section 2 — WXR (448..722)**

| Element | Geometry / text |
|---|---|
| `WXR` | E 28 **amber** at (188.9, 470.4) |
| `ELEVN/TILT` | L24 at (40.0, 555.2) |
| AUTO/ELEVN/TILT radios (disabled, green, index 0) | rows `47.95..182.45 × 570..608, 618..656, 666..704`; circles x 66.95 (y 589, 637, 685); text x 98.95 baselines 596.8, 644.8, 692.8 |
| grid labels (SZ 20 white, `.mfd-surv-label`) | row 0 baseline 486.0: `WXR` cx 341.2, `PRED W/S` cx 499.2, `TURB` cx 657.2; row 1 baseline 615.0: `GAIN`, `MODE`, `WX ON VD` |
| SurvButtons | x 282.2, 440.2, 598.2 (118 wide); row 0 y **492..575**, row 1 y **621..704**. Labels (true/false): WXR AUTO/OFF, PRED W/S AUTO/OFF, TURB AUTO/OFF, GAIN AUTO/MAN, MODE WX/MAP, WX ON VD AUTO/OFF; all disabled |

**Section 3 — TAWS (rule `0..768 × 728..730`, content 730..935)**

| Element | Geometry / text |
|---|---|
| `TAWS` | E 28 at (100, 757.4), amber when TERR **and** GPWS failed |
| TAWS block | `70.2..588.2 × 730..935`, right border 2 px at x 586.2..588.2 |
| elements | label SZ 20 baseline 806.0, SurvButton y **812..895**: TERR SYS x 74.2 (cx 133.2), GPWS x 212.2 (cx 271.2), G/S MODE x 338.2 (cx 397.2), FLAP MODE x 464.2 (cx 523.2); labels `ON` / `OFF` |
| `SURV` | SZ 20 centred cx 676.2, baseline 824.5 |
| DEFAULT / SETTINGS | Button `width:140` (text wraps: two lines) → **592.2..760.2 × 830.5..892.5**, cx 676.2 baselines 859.1/881.1 |

---

## 5. SURV/STATUS & SWITCHING (`MfdSurvStatusSwitching.tsx`, `.scss`)

URI `surv/status-switching`; title `STATUS & SWITCHING`; standard footer.

### 5.1 Logic

| Element | FBW | [PORT] |
|---|---|---|
| upper SYS 1 / SYS 2 buttons | toggle `L:A32NX_WXR_TAWS_SYS_SELECTED`: SYS1 → `active ? 0 : 1`, SYS2 → `active ? 0 : 2` (`:107-113,163-169`) | `surv.wxr_taws_sys` |
| upper groups | WXR DISPLAY / TURB / PRED W/S and TERR SYS / GPWS items, `active` = group selected; failed: WXR/TURB/PRED constants false (**[QUIRK]** contradicts CONTROLS where WXR is failed), TERR/GPWS from `L:A32NX_TERR_n_FAILED`, `L:A32NX_GPWS_n_FAILED` | WXR/TURB/PRED W/S items **failed (amber)** in the port (no radar, consistent with CONTROLS); TERR/GPWS failed = 0 |
| lower XPDR/TCAS | `activeSystemGroupXpdrTcas` constant 1; SYS1 button has no `onClick`, SYS2 logs to console **[FBW-NA]** | **[PORT]** SYS 1 / SYS 2 select `surv.xpdr_tcas_sys` (1/2, default 1); both systems map to the single X-Plane transponder; XPDR n failed = `rel_xpndr==6`; TCAS1 failed = `tcas_fault`, TCAS2 failed = `tcas_fault` |
| group frame colour | `.sys-group` 1 px `#c4c6cf`, green when active (`.failed` class defined, never applied) | same |
| `.sys-box` | 1 px `#c4c6cf` (`.active` defined, never applied) | same |

### 5.2 PORT layout

Fit adjustment T1: the row divs are `width:100%; padding:50px` → 868 px wide under the content-box model (right box
clipped). **[PORT]** treat the row as 768 wide including its 50-px padding (content `50..718`).

| Element | Geometry / text |
|---|---|
| upper boxes | left **50..342 × 188..434**, right **426..718 × 188..434** (1 px `#c4c6cf`) |
| upper SurvStatusButtons | `SYS 1` **149..243 × 154..218**, `SYS 2` **525..619 × 154..218** (straddle the box top edge; z above the box) |
| WXR group | left `61..331 × 239..343` (right `437..707`), 1 px frame; items SZ 24 at x 67 (right 443): `WXR DISPLAY 1|OFF` baseline 264.2, `TURB 1|OFF` 298.2, `PRED W/S 1|OFF` 332.2 |
| TAWS group | `61..331 × 353..423`: `TERR SYS 1|OFF` 378.2, `GPWS 1|OFF` 412.2 |
| centre labels | L24 centred cx 384: `WXR` baseline 262.2, `TAWS` baseline 376.2 |
| lower boxes | left **50..342 × 534..673**, right **426..718 × 534..673** |
| lower SurvStatusButtons | **149..243 × 500..564**, **525..619 × 500..564** |
| XPDR group | `61..331 × 585..621`: `XPDR 1|OFF` baseline 610.2 |
| TCAS group | `61..331 × 626..662`: `TCAS 1|OFF` baseline 651.2 |
| centre labels | `XPDR` baseline 608.2, `TCAS` baseline 652.2 |

---
## 6. ATCCOM: system logic, offline behaviour, footer, stubs

### 6.1 FBW architecture

* One `AtcDatalinkSystem` shared by both MFDs (`MFD/ATCCOM/AtcDatalinkSystem.ts`, spec 01 §1.1). It owns:
  * `atcErrors` — the **ATCCOM message queue** (separate from the FMS queue): `addMessageToQueue(msg, details)`
    (`:236-249`: text + `\n` + details, amber if `isAmber`, removes an identical **cleared** entry, inserts at 0);
    `clearLatestAtcErrorMessage()` (`:215-229`: Type II → cleared, else removed); `showAtcErrorMessage(type, d)`
    maps EntryOutOfRange/FormatError/NotInDatabase/NotYetImplemented to the NX messages (`:193-210`). Catalogue:
    spec 02 §10.8.
  * `atisAirports[3]` = `{icao, type, requested, autoupdate, lastReadAtis, status}`; block 0 DEP, blocks 1-2 ARR
    (`:73-77`). Re-initialised (`initAtis`, `:257-274`) on `fmsOrigin` → block 0 (DEP), `fmsDestination` → block 1
    (ARR), `fmsAlternate` → block 2 (ARR) whenever they change.
  * `requestAtis(i)` (`:328-360`): ignored if icao null or already requested; `status = 'SENDING'`; publish;
    await the ATC router; `ComFailed` → `SEND\nFAILED`; `NoAtisReceived` → `NO REPLY` + ATCCOM message
    `D-ATIS NO REPLY`; `NewAtisReceived` → status `''` (no message, `D-ATIS RECEIVED` commented out). Publish.
    `updateAllAtis()` requests all three.
  * auto update: `activate/deactivateAtisAutoUpdate(i)` → the ATC component polls every **60 s**
    (`fbw-common/src/systems/datalink/atc/src/ATC.ts:1023-1048`); `atcActiveAtisAutoUpdates` sets `autoupdate` for
    non-DEP blocks.
  * ATIS reports per ICAO, newest first (`MessageStorage.ts:45-56`); a report replaces/prepends when its
    `Information` letter changes (`ATC.ts:938-986`); `Information` = first character of the word after
    `INFORMATION`/`INFO`/`<icao>` (last occurrence wins, `AtisMessage.ts:26-51`); empty → `NoAtisReceived`.
    Timestamp `HHMMZ` = sim UTC at reception (`AtsuTimestamp.mailboxTimestamp`).
  * The ATIS text itself comes from the internet (FAA/VATSIM/IVAO/PilotEdge/BeyondATC/SayIntentions) via
    `router/src/webinterfaces/NXApiConnector.ts:60-108`; failure → literal `D-ATIS NOT AVAILABLE` (→ no letter →
    NO REPLY).
* CPDLC (CONNECT, MSG RECORD, REQUEST, REPORT, EMER) is **not wired**: CONNECT and the three MSG RECORD sub-pages
  are mock-ups with hard-coded data under a `NOT YET IMPLEMENTED` veil; REQUEST, REPORT & MODIFY, EMER are unrouted
  (404).

### 6.2 [PORT] Offline datalink model (X-Plane has no CPDLC/ACARS)

`A380XAtcDatalink` (FMS-core side, XPLM-free) with a constant **datalink status = NOT AVAILABLE** for CPDLC/ADS-C
(no VHF/SATCOM/HF datalink is simulated) and an **ATIS provider** callback (`atis_request(icao, type)` implemented by
the adapter, §9.4).

| Function | Faithful offline behaviour |
|---|---|
| CONNECT | page drawn without the veil; no ATC centre known (dropdown list empty → `----`); ACTIVE/NEXT ATC value **empty** (FBW mock shows `XXXX`); NOTIFY, DISCONNECT ALL, MODIFY MAX UPLINK DELAY disabled; ADS-C / ADS-C EMERGENCY ARMED/OFF are local toggles (FBW), no downlink; CONNECTED GROUND STATIONS `NONE`, empty list |
| MSG RECORD | ALL MSG / MONITORED MSG lists empty (no fake messages); ERASE ALL, PRINT, CANCEL MONITORING disabled; scroll arrows disabled |
| REQUEST, REPORT & MODIFY, EMER | stub pages (spec 01 §10.2 PORT): header with the selector active, title `REQUEST`, `REPORT & MODIFY`, `EMER`, empty body, **standard** footer. No auto-return. Do not invent request forms |
| D-ATIS | functional through the ATIS provider: either **synthesised from X-Plane weather** (§9.4, recommended default) or **OFF** (every request → status `SEND FAILED`, no message — FBW `ComFailed`) |
| ATCCOM message queue | implemented exactly as FBW (only D-ATIS and entry errors produce messages) |
| ATC MSG glareshield (`a380x/xp12/glareshield/atc_*`) | never triggered by the offline datalink |

### 6.3 ATCCOM footer

D-ATIS pages use the ATCCOM footer (spec 01 §8.3: button always `CLEAR`/`INFO`, acts only when an uncleared ATCCOM
message exists; message area shows the newest uncleared `atcErrors` entry). CONNECT and MSG RECORD pages use the
standard (FMS) footer [FBW].

### 6.4 `NOT YET IMPLEMENTED` veil [FBW-NA]

CONNECT, ALL MSG, MONITORED MSG, EXPAND: absolute `768 x 818` at y 132 (covers 6 px of the title bar),
`rgba(0,0,0,0.7)`, text `NOT YET IMPLEMENTED` SZ 35 amber centred cx 384 baseline 551.5. **[PORT]** not drawn
(offline model §6.2 replaces it).

---

## 7. ATCCOM/CONNECT (`pages/ATCCOM/MfdAtccomConnect.tsx`, `.scss`)

`.mfd-atccom-connect.mfd-label` = SZ **22** white; values `.mfd-value` 25 green.

AdscButton (`AdscButton.tsx`, overridden by `.mfd-atccom-connect-adscbutton .mfd-adsc-button {width:200;height:70;
margin-top:10}` → **218 x 88**; EMER variant width 110 → **128 x 88**): active upper label green 30, active lower
white 24, inactive label E 20 `#868a9c`. State false (OFF): `ARMED` E20 grey baseline `y+38.0`, `OFF` white 24
baseline `y+61.2`; state true: `ARMED` green 30 baseline `y+43.0`, `OFF` E20 grey baseline `y+65.0`; centred on the
box. Click toggles local state (`:122-124,135-137`).

PORT layout (fit adjustments C1: row-2 `NOTIFY` aligned with the shrunk column 2; C2: list box height 265→252; C3:
right-column buttons clamped to x ≤ 768):

| Element | Geometry / text |
|---|---|
| `NOTIFY TO ATC :` | SZ 22 right-aligned to x 297.06 (x 90.16), baseline 196.6 |
| ATC dropdown | DropdownMenu `width:150`, 4 digits, centred labels, no free text → **317.06..467.06 × 168..212**; value `----` cyan 27 cx 379.56 baseline 198.1 |
| NOTIFY | Button `width:120`, disabled → **544.86..692.86 × 170..210**, cx 618.86 baseline 198.6 |
| `NOTIFY` (status label) | SZ 22 at (317.06, 239.6) |
| `ACTIVE ATC :` | SZ 22 at (133.4, 323.1); value V25 at (318.93, 324.0) — empty in PORT |
| `NEXT ATC :` | SZ 22 at (161.0, 378.1); value at (318.93, 379.0) — empty |
| DISCONNECT ALL | disabled, **480..768 × 324..364**, cx 624 baseline 352.6 |
| MODIFY / MAX UPLINK DELAY | disabled, two lines, **480..768 × 394..456**, cx 624 baselines 422.6/444.6 |
| section rule | `0..768 × 476..478` `#c4c6cf` |
| `ADS-C` | SZ 22 centred cx 234, baseline 525.6; AdscButton (`ARMED`/`OFF`) **125..343 × 540..628** |
| `ADS-C EMERGENCY` | SZ 22 centred cx 534, baseline 525.6; AdscButton **470..598 × 540..628** |
| `ADS-C CONNECTED GROUND STATIONS :` | SZ 22 at (110, 675.6); `NONE` SZ 20 at (588.2, 675.0) |
| stations list box | 2 px solid `#c4c6cf`, **215..579 × 690..946** (empty) |

---

## 8. ATCCOM/MSG RECORD

### 8.1 MSG RECORD (`MfdAtccomMsgRecord.tsx`) [FBW], functional navigation

| Element | Geometry |
|---|---|
| frame | white 1 px rules `0..768 × 228..229` and `× 504..505` |
| ALL MSG | Button `width:205;height:64` → **65..298 × 279..361**, cx 181.5 baseline 328.6 → `atccom/msg-record/all-msg` |
| MONITORED MSG | Button `width:205;height:64;padding:9 5 5 5` → **65..284 × 394..476**, cx 174.5 baseline 443.6 → `atccom/msg-record/monitored-msg` |

### 8.2 MSG RECORD/ALL MSG and MONITORED MSG (`MfdAtccomMsgRecordAll.tsx`, `...Monitored.tsx`)

[FBW-NA]: three (two) hard-coded sample messages (`1323Z LFBO UNABLE MAINTAIN M.77`, ...), veil. Element click →
`all-msg-expand`. **[PORT]** empty list.

| Element | PORT geometry |
|---|---|
| list area | `0..684 × 138..862` (FBW `#msg-record-list` width 710 flex-shrunk), `padding-top:18` |
| scrollbar | `684..707 × 153..847`, fill `#3c3c3c`, 2 px outset `#c4c6cf` (thumb not drawn when empty) |
| nav arrows | IconButtons at x **710..768**, 58 x 58 each: double-up y 161, single-up 223, single-down 285, double-down 347 (**[QUIRK]** FBW single arrows use `padding:10` → 66 x 66 and overflow 8 px; port 58 x 58). Disabled when the list fits |
| ERASE ALL (ALL MSG only) | Button `width:190;height:64` → **0..218 × 865..947**, cx 109 baseline 914.6; PORT disabled when empty |
| PRINT (ALL MSG only) | **550..768 × 865..947** (FBW top 862, **[PORT]** aligned with ERASE ALL); disabled (no printer) |

Message element (for a future datalink; [FBW] `.msg-record-msg-element`): SZ 26 green, `line-height:30`, padding 16,
`min-height:124` → ≥157 tall incl. 1 px white bottom rule; time SZ 22 at (16, top+22.8); `FROM XXXX` SZ 22 at x 146
same baseline; status box `155 x 25` right-aligned 6 px from the element's right edge at `top+19`, black text on green
(on **cyan** for MONITORED, placed 190 px from the right), SZ 26 centred; body width 470, baselines `top+54.0+30k`;
expand mark `...... >>>` (MONITORED `>>>`) white, right 115, bottom 15; MONITORED adds a `CANCEL / MONITORING`
Button `160x55` at top 19, right 10. **[QUIRK]** `width:100%` + padding overflows the list by 32 px; port: element
width = list width.

### 8.3 MSG RECORD/ALL MSG/EXPAND (`MfdAtccomMsgRecordExpand.tsx`) [FBW-NA]

Hard-coded SIGMET sample (`msg-body-expand` SZ 27, line-height 40). Footer row: RETURN / TO LIST
**0..218 × 865..947** (cx 109, baselines 903.6/925.6) → `atccom/msg-record/all-msg`; PRINT **550..768 × 865..947**
disabled. **[PORT]** unreachable with an empty record; keep the route with an empty body.

---

## 9. ATCCOM/D-ATIS

### 9.1 D-ATIS/LIST block logic (`pages/ATCCOM/DAtisBlock.tsx`) [FBW]

| Element | Behaviour |
|---|---|
| ICAO field | AirportFormat (spec 02 #21, `----`, 4 chars, errors to the **ATCCOM** queue `:225`). Change → clears letter, time, text, status and stores the airport (`:164-173`) |
| DEP/ARR dropdown | values `DEP`,`ARR` (index = AtisType 0/1), no free text; change stores the type |
| letter / time | `Information` (SZ 22 white) and `HHMMZ` (SZ 22 white) of the latest report |
| new-ATIS icon | envelope, visible when the latest letter ≠ `lastReadAtis` |
| auto-update icon | green circular arrow, visible when `autoupdate` |
| auto-print icon | never visible |
| SEND REQUEST | visible when status null **and** no report text (`dropdownMenuVisible`); disabled when ICAO empty/`----`; → `requestAtis(i)` |
| AUTO UPDATE | same visibility; disabled for DEP or empty ICAO; toggles auto update |
| status button | `highlighted` (white fill, `#272525` text), visible when status non-empty: `SENDING`, `SEND`/`FAILED`, `NO REPLY`, `USE`/`VOICE`, `NO AUTO`/`UPDATE`, … (`AtcDatalinkSystem.ts:29-39`); no action |
| UPDATE OR PRINT ▾ | visible when the two buttons above are hidden; right-aligned menu: `UPDATE` (disabled if ICAO empty) → request; `AUTO UPDATE` (disabled DEP/empty) → toggle; `PRINT`, `AUTO PRINT` disabled |
| text | `truncateAtis`: greedy word fill of 5 lines with max 43/43/43/43/28 characters (`lineLengths−1`), joined by spaces, `  ......` appended if truncated, upper case |
| `>>>` | visible when text exists → sets `lastReadAtis = letter`, navigates `atccom/d-atis/received/<i>` |
| page bottom | PRINT ALL (disabled), UPDATE ALL → `updateAllAtis()` |

**[QUIRK]** `DAtisBlock.destroy()` does not call `super.destroy()` and the `atisIcao/atisType/atisCode` `sub()`s are not
collected (leak). Port: n/a.

### 9.2 D-ATIS/LIST PORT layout (fit D1: block pitch 256→248, width 778→768; D2: status/right buttons repacked inside
768; D3: ICAO field 106 x 44 aligned with the dropdown; D4: text line-height 38→36; D5: `>>>` raised; D6: bottom
buttons moved into the body and de-overlapped)

Block `i` top `T = 138 + 248·i` (138, 386, 634); block `0..768 × T..T+248`, 1 px `#c4c6cf` bottom rule at
`T+247`. Positions relative to T:

| Element | Geometry / text |
|---|---|
| ICAO field | **10..116 × T+10..T+54** (InputField look, sunken 2 px); value cyan 27 centred cx 63, baseline T+40.1; mandatory=false |
| DEP/ARR dropdown | `width:106` → **120..226 × T+10..T+54**; value cyan 27 at (132, T+40.1) |
| letter | SZ 22 white at (232, T+37.6) |
| time | SZ 22 white at (264, T+37.6) |
| new-ATIS icon | `340..380 × T+17..T+47`: white 40x28 card, black 1.5 px inner frame (2,2,36,25) and envelope flap lines (`Elements/NewAtisIcon.tsx:26-32`) |
| auto-update icon | `386..424 × T+13..T+51`: green `#00ff00` 3 px arc (≈315°, gap at upper right) + filled arrow head (`AutoUpdateIcon.tsx:26-29`, viewBox 40x37 scaled 0.95) |
| status button | Button `width:131` highlighted → **431..590 × T+7..T+71** (lines centred, SZ 22) |
| SEND / REQUEST | Button `width:159;padding-left:5` → **583..763 × T+7..T+61** (lines cx 678.5, baselines T+35.6/T+57.6) |
| AUTO / UPDATE | **583..763 × T+65..T+119** (FBW top 61; 4-px gap) |
| UPDATE / OR PRINT ▾ | menu button → **590..763 × T+7..T+61**, arrow 15x15; menu right-aligned to 763, opens downward |
| text | SZ 27 white at x 12, baselines T+86.1, T+122.1, T+158.1, T+194.1, T+230.1 |
| `>>>` | Button → **650..743 × T+200..T+243**, text cx 704.5 baseline T+230.1 |
| PRINT / ALL | disabled, **328..546 × 886..948** |
| UPDATE / ALL | **550..768 × 886..948** |
| footer | ATCCOM footer 950..1024 |

(FBW natural for reference: blocks 256 apart at 138/394/650, buttons at y 909..971 overlapping the footer, PRINT ALL
`360..578` overlapping UPDATE ALL `550..768`, `>>>` at T+213..T+264 overlapping the next block, SEND REQUEST
`600..780` and the status button `469..628` overlapping UPDATE OR PRINT `600..773`.)

### 9.3 D-ATIS/RECEIVED (`MfdAtccomDAtisReceived.tsx`) [FBW]

* `atisIndex = Number(extra)`; reports = `atisReports(icao)`: LAST = `[0]`, PREVIOUS = `[1]` (empty if absent).
  **[QUIRK]** `[0]` is dereferenced without a check (crash if no report); port: empty text.
* Text processing: greedy words into rows of ≤ **39** characters, 18 rows per page, pages = `ceil(rows/18)`;
  the row slice is joined with spaces and re-wrapped by CSS at the 660-px width (≈42 chars) **[QUIRK]**; **[PORT]**
  draw the 39-char rows one per line.
* Tabs `LAST` | `PREVIOUS` (angle 27°, tw 18.34, font 24), tab click switches (no callback; internal index).
* Page arrows double-up/double-down, visible when pages > 1, disabled at first/last page; `PGE` / `n/m` label.
* RETURN TO LIST → `atccom/d-atis/list`; PRINT disabled.

PORT layout (fit R1: line height 38 → 36 so 18 rows fit):

| Element | Geometry / text |
|---|---|
| tab bar | `2..766 × 140..176`; `LAST` tab `2..391.5` (text x 166.65), `PREVIOUS` `376.5..766` (x 511.05), baseline 167.2 |
| tab frame | `2..766 × 176..862`, content `14..754 × 186..850` |
| text | SZ **25** white (`.atis-message-full` overrides `.mfd-label` 20), x 14, rows k = 0..17 baselines `219.5 + 36k` |
| arrows | IconButtons 52 x 52 (`width:40;height:40;padding:4`): double-up **696..748 × 213..265**, double-down **696..748 × 265..317** |
| `PGE` / `n/m` | SZ 21 white, block right edge x 747: baselines 822.8 and 843.8 |
| RETURN / TO LIST | Button `width:190;height:62` → **0..218 × 867..947**, cx 109 baselines 904.6/926.6 |
| PRINT | disabled, **550..768 × 866..946** |
| footer | ATCCOM footer |

### 9.4 D-ATIS synthesis from X-Plane weather [PORT] — feasible, specified here

**Feasibility.** XP 12 SDK (`XPLMWeather.h`, XPLM400+): `XPLMGetMETARForAirport(icao, XPLMFixedString150_t*)`
returns the last downloaded METAR **only in real-weather mode** (empty otherwise); `XPLMGetWeatherAtLocation(lat,
lon, alt_m, XPLMWeatherInfo_t*)` returns the sim's weather at a point (wind/temperature/dewpoint/visibility/
precipitation/cloud layers/sea-level pressure; returns 1 if an airport METAR backs it). Both must be called from a
flight-loop callback, not per frame, never from a draw callback. Airport position from
`XPLMFindNavAid(NULL, icao, NULL, NULL, NULL, xplm_Nav_Airport)` + `XPLMGetNavAidInfo`. Weather source:
`sim/weather/region/weather_source` (0 preset, **1 real**, 2 control pad, 3 plugin). Runways and ILS from the port
navdata (WP-D, spec 11). Therefore a D-ATIS consistent with the simulated world can be produced. It replaces FBW's
internet ATIS sources; the provenance is synthetic, which must be stated in the port documentation.

**Configuration:** `a380x_mfd.cfg: datis_source = sim | off` (default `sim`).

**Request flow** (adapter, flight loop): MFD `requestAtis(i)` → status `SENDING` → after a fixed 3.0 s link delay
the adapter answers:

1. Unknown ICAO (no airport navaid) → `NoAtisReceived` (status `NO REPLY` + `D-ATIS NO REPLY`).
2. `weather_source == 1`: METAR = `XPLMGetMETARForAirport`; empty → `NoAtisReceived`; else parse METAR (station,
   `DDHHMMZ`, wind `dddff(Ggg)KT|MPS`/`VRB`, visibility (`9999`, `nnnn`, `nSM`, `CAVOK`), weather groups, cloud
   groups `FEW|SCT|BKN|OVCnnn(CB|TCU)`, `VV`, `NSC/SKC/CLR`, `TT/DD` (`M` prefix), `Qnnnn`/`Annnn`).
3. Otherwise: `XPLMGetWeatherAtLocation(apt_lat, apt_lon, apt_elev_m + 10, &info)` (`structSize =
   sizeof(XPLMWeatherInfo_t)`): wind = `wind_dir_alt`, `wind_spd_alt`·1.943844 kt (gust from the lowest
   `wind_layers[].gust_speed` if > speed+10 kt); visibility `info.visibility` m; clouds = `cloud_layers[0..2]`
   with coverage → FEW (<0.25) / SCT (<0.5) / BKN (<0.875) / OVC, base = (`alt_base` − elevation) in ft rounded
   down to 100 ft; precipitation `precip_rate` > 0.1 → `RA` (temperature ≤ 0 → `SN`), `+` if > 0.6;
   temperature/dewpoint `temperature_alt`/`dewpoint_alt` rounded; QNH = `pressure_sl`/100 hPa rounded down.
4. Runway in use: navdata runways of the airport; choose the runway end with the largest headwind component
   (true wind vs runway true heading); wind < 5 kt → keep the previous choice, initially the FMS departure/arrival
   runway of that airport if one is in the active plan, else the longest runway. ARR approach: `ILS` if an ILS/LOC
   for that runway exists in navdata, else `RNAV`.
5. Letter: per ICAO, a persisted index `n`; initial `n = (zulu_hour + Σ icao chars) mod 26`; incremented when the
   runway changes, wind direction changes ≥ 30°, wind speed ≥ 10 kt, QNH ≥ 1 hPa, visibility/ceiling category
   changes, or at every HH:50 boundary (hourly observation). Letter = `'A' + n`.
6. Text (upper case, single spaces, no punctuation except `.`, `/` — all glyphs exist in the MFD font):
   `<ICAO> <ARR|DEP> ATIS INFO <L> <HHMM>Z. [EXPECT <ILS|RNAV> APPROACH RWY <rr>. ]<ARR|DEP> RWY <rr>.
   [TRANSITION LEVEL <FLnnn>. ]WIND <ddd> DEG <ff> KT[ GUSTS <gg> KT]|WIND VRB <ff> KT|WIND CALM.
   VIS <10 KM OR MORE|nnnn M|n SM>.[ <WX>.] <CAVOK|CLOUD <cov> <hhhh> FT ...|NSC>. TEMP <t> DEWPOINT <d>.
   QNH <qqqq> HPA|ALTIMETER <aaaa>. ACKNOWLEDGE INFO <L> ON INITIAL CONTACT.`
   `ALTIMETER` (inHg·100) for ICAO prefixes K, P, C, T, M; else `QNH`. Transition level from navdata/FMS if
   known. `HHMM` = METAR observation time, or the last HH:50 for sim weather. The report's `Information` is set
   directly to `<L>` (no parsing).
7. Result: `NewAtisReceived` when the letter differs from the stored latest report (prepend; keep 2 per ICAO) or
   the request was explicit (FBW `overwrite=true`: replace `[0]`); timestamp `HHMMZ` = sim UTC now
   (`sim/time/zulu_time_sec`). Auto update: every 60 s for ARR blocks with auto update, `overwrite=false`.
8. `datis_source = off`: every request → `ComFailed` (`SEND`/`FAILED`), auto update not available.

---
## 10. FCU BKUP / AFS CP — design [PORT]

### 10.1 Upstream and aircraft facts

* **[FBW-NA]** `fcubkup/afs` and `fcubkup/efis` are unrouted (404 → back after 0.5 s; spec 01 §10.2). Only the
  header exists (`pages/common/FcuBkupHeader.tsx:31-53`: row `width:50%`, selectors `AFS CP` 0..192 and `EFIS CP`
  192..384).
* Real aircraft (public sources: FBW A380X pilot docs "AFS control panel" and "MFD" pages; A350 FCTM extract on
  backup use; x-plained FlightFactor A350 review — summarised, not copied): the MFD **FCU BKUP** pages contain *all*
  AFS CP controls and indicators and the onside EFIS CP controls; they are used when the AFS CP or an EFIS CP fails
  (alerts `AUTO FLT AFS CTL PNL FAULT`, `CDS CAPT/F/O EFIS CTL PNL FAULT` call them up automatically) or when the crew
  selects them. On the backup EFIS page the **ZOOM function and the TAXI pb are not available**. Knob semantics:
  turn = preselect, pull = selected guidance, push = managed guidance (SPD, HDG, ALT); the V/S-FPA knob push has
  no effect on the A380 (port: `a380x_guidance.c:157`).
* **[PORT] decisions:** one aircraft state; the MFD page and the physical/3D FCU drive the same controller
  (`guidance` in `a380x_fcu_adapter.inc`). Both MFDs show the same AFS CP state. No failure-triggered automatic
  page call (no FCU failure model); the page is always usable. Window strings are produced by the **same
  formatting as the port FCU display** (`a380x_fcu_render.inc:64-81`) so both always agree.

### 10.2 Control → port command/dataref map (all existing; `a380x_fcu_adapter.inc:28-61`, `tools/avionics_provider.py:36-51`)

| MFD control | Action | Existing command (`a380x/xp12/fcu/…`) | State shown from |
|---|---|---|---|
| SPD/MACH −/+ arrows | press = `XPLMCommandBegin`, release = `XPLMCommandEnd` (handler repeats 0.4 s then every 0.08 s, `:91-92`) | `speed_down` / `speed_up` (±1 kt or ±0.01 M) | `selected_speed`, `selected_mach`, `mach_mode`; dashes `guidance.speed_dashes` |
| SPD/MACH value entry | host `fcu_rotate(A380X_FCU_SPD_ROTATE, n)` (§13), n = target − current (kt or 0.01 M units) | same code path as `speed_up/down` with amount n | |
| SPD PUSH / PULL | CommandOnce | `speed_push` / `speed_pull` | `speed_managed` (managed dot) |
| SPD/MACH pb | CommandOnce | `speed_mach_toggle` | `mach_mode` |
| HDG/TRK −/+ ; entry ; PUSH/PULL | as above | `heading_down/up` (±1°), `heading_push/pull`; entry `A380X_FCU_HDG_ROTATE` with n = wrap(target − current, ±180) | `selected_heading` (+ `true_heading`), `heading_managed`; dashes `guidance.heading_dashes` |
| TRUE/MAG pb | CommandOnce | `true_mag_toggle` | `true_heading` (`TRUE` label) |
| HDG-V/S / TRK-FPA pb | CommandOnce | `trk_fpa_toggle` | `trk_fpa` |
| ALT −/+ ; entry ; PUSH/PULL | as above | `altitude_down/up` (±100 or ±1000 ft), `altitude_push/pull`; entry `A380X_FCU_ALT_ROTATE` with n = (target − current)/`altitude_step_ft` (fractional amounts are exact, `a380x_guidance.c:129-130`) | `selected_altitude`, `altitude_managed`, `altitude_step_ft` |
| ALT 100 / 1000 | click on the non-selected segment → CommandOnce | `altitude_step_toggle` | `altitude_step_ft` |
| V/S-FPA −/+ ; entry ; PULL | as above | `vertical_speed_down/up` (±50 ft/min or ±0.1°), `vertical_speed_pull`; entry `A380X_FCU_VS_ROTATE`, n = Δ/50 or Δ/0.1 | `selected_vertical_speed`, `selected_fpa`, `trk_fpa`; dashes `guidance.vs_dashes` |
| V/S PUSH | drawn **disabled** (A380: no effect) | (`vertical_speed_push` exists, no-op) | — |
| AP1 / AP2 | CommandOnce (handler does AP engagement checks, `:93-111`) | `ap1_toggle` / `ap2_toggle` | `ap1_active` / `ap2_active` |
| A/THR | CommandOnce | `athr_toggle` | `athr_armed` (lights when armed or active) |
| FD | CommandOnce (FD2 mirrored, `:115-116`) | `fd_toggle` | `fd_active` |
| LOC | CommandOnce | `loc_toggle` | `loc_active` |
| APPR | CommandOnce | `approach_toggle` | `approach_active` |
| ALT (level off) | CommandOnce | `altitude_hold` | lights with `altitude_hold_active` (same as the port's 3D pb) |
| METRIC ALT | CommandOnce | `metric_toggle` | `metric_altitude` |
| EXPED | drawn **disabled** — no port command, no A380X FBW function (Open question 8) | — | — |

Entry formats (new, WP-B, `a380x_mfd_formats`): placeholder shown when dashes; errors → FMS queue (standard footer).

| Format | Placeholder / digits / unit | Parse | Range (port controller clamps) |
|---|---|---|---|
| `FCU_SPD` | `---` / 3 / `KT` | `^[0-9]{2,3}$` | 100..399 → `RNG: 100 TO 399 KT` |
| `FCU_MACH` | `---` / 4 / – ; display `%.2f` (`0.84`) | `^0?\.[0-9]{1,2}$` | .10..0.99 |
| `FCU_HDG` | `---` / 3 / `°` | `^[0-9]{1,3}$`, 360 → 0 | 0..360 |
| `FCU_ALT` | – / 5 / `FT`; display `%05d` | `^[0-9]{3,5}$` | 100..49000 |
| `FCU_VS` | `-----` / 5 / – ; display `%+05d` | `^[+-]?[0-9]{1,4}$`, rounded to 50 | −6000..+6000 |
| `FCU_FPA` | `-----` / 4 / `°`; display `%+.1f` | `^[+-]?[0-9](\.[0-9])?$` | −9.9..+9.9 |

### 10.3 Layout (title `AFS CP`, standard footer)

Four columns `x0 = 0, 192, 384, 576` (SPD, HDG/TRK, ALT, V/S-FPA).

| Element | Geometry / text |
|---|---|
| window labels | SZ 22, active **white**, inactive `#868a9c`, baseline 172: SPD col `SPD` at x 40 and `MACH` at x 100 (active per `mach_mode`); HDG col `TRUE` at 200 (white, shown only if `true_heading`), `HDG` at 266, `TRK` at 322; ALT col `ALT` centred cx 480; V/S col `V/S` at 616, `FPA` at 676 |
| value fields | InputField `width:144px` → **x0+12..x0+164 × 182..231**; value cyan 27 (entered style), dashes as placeholder when the window shows dashes; units per §10.2 at 20 px blue |
| managed dot | filled circle r 7 at (x0+178, 206.5), **green** when `speed_managed` / `heading_managed` / `altitude_managed`; none for V/S |
| −/+ arrows | IconButtons 50 x 50 `single-down` **x0+16..x0+66** and `single-up` **x0+110..x0+160**, y **243..293** |
| PUSH / PULL | Buttons **x0+10..x0+93.2** and **x0+98.8..x0+182**, y **305..345**, baseline 333.6 (V/S PUSH disabled) |
| SPD/MACH | Button **26.85..165.15 × 357..397**, baseline 385.6 |
| TRUE/MAG | Button **218.85..357.15 × 357..397** |
| 100 / 1000 | segmented Buttons **401.7..471.1** and **475.1..558.3**, y 357..397; the current increment `selected` |
| HDG V/S / TRK FPA | Button two lines **609.7..734.3 × 357..419**, baselines 385.6/407.6 |
| rule | `0..768 × 435..437` `#c4c6cf` |
| pushbuttons row 1 | SurvStatusButtons 94 x 64, y **455..519**: LOC 101, AP1 219, AP2 337, A/THR 455, APPR 573 (x0); label baseline 507.6 |
| pushbuttons row 2 | y **543..607**: FD 160, ALT 278, EXPED 396 (disabled: label `#868a9c`, bars `#c4c6cf`, no hover), METRIC 514; baseline 595.6 |

All controls disabled (placeholders shown) while `guidance_inputs.powered == 0` (the controller ignores commands then,
`a380x_guidance.c:101`).

---

## 11. FCU BKUP / EFIS CP — design [PORT]

The page controls the **onside** EFIS CP: CAPT MFD → `a380x/xp12/efis/captain/*`, FO MFD →
`a380x/xp12/efis/first_officer/*` (`a380x_efis.inc:290-308`). Title `EFIS CP`. Controls disabled while the
side's `powered` dataref is 0.

### 11.1 Control map (existing commands/datarefs only)

| Control | Action | Command `a380x/xp12/efis/<side>/…` | State dataref `a380x/xp12/efis/<side>/…` |
|---|---|---|---|
| BARO value entry | write `sim/cockpit2/gauges/actuators/barometer_setting_in_hg_{pilot|copilot}` = hPa / 33.8638867 → adopted as preset (and QNH when not STD) by `efis_update` (`a380x_efis.inc:201-210`) | – | `baro_preset_hpa`, `qnh_hpa` |
| BARO −/+ | press/hold (repeat 0.4 s / 0.10 s) | `baro_down` / `baro_up` (1 hPa or 0.01 inHg) | |
| STD | CommandOnce | `baro_std` | `baro_std` |
| QNH | CommandOnce | `baro_qnh` | `baro_std == 0` |
| HPA / IN HG | click on the non-selected segment | `baro_unit_toggle` | `baro_inhg` |
| LS | CommandOnce | `ls_toggle` (routes to `A380X_FCU_LS1/LS2`) | `ls` |
| VV | CommandOnce | `vv_toggle` | `vv` |
| TAXI | disabled (not available on the backup page) | (`taxi_toggle` exists) | `taxi_requested` |
| ND MODE radio (ROSE LS, ROSE VOR, ROSE NAV, ARC, PLAN = 0..4) | select k → issue `mode_up`/`mode_down` |k−mode| times (keeps the ARC range shift of `efis_set_mode`) | `mode_up` / `mode_down` | `mode` |
| ND RANGE radio (ZOOM, 10, 20, 40, 80, 160, 320, 640 = index 0..7) | select k (1..7) → `range_up`/`range_down` |k−index| times; ZOOM row disabled | `range_up` / `range_down` | `range_index`, `range_nm`, `zoom` |
| CSTR, WPT, VOR D, NDB, ARPT | CommandOnce | `cstr_toggle`, `wpt_toggle`, `vord_toggle`, `ndb_toggle`, `arpt_toggle` | `cstr`, `wpt`, `vord`, `ndb`, `arpt` |
| WX, TERR, TRAF | CommandOnce | `wx_toggle`, `terr_toggle`, `traf_toggle` | `wx_requested`, `terr`, `traf` (WX has no image in the port: ND shows its INOP annunciation) |
| VOR 1, VOR 2 (ND bearing pointers) | CommandOnce | `vor1_toggle`, `vor2_toggle` | `vor1`, `vor2` |

Formats: `BARO_HPA` (`----`, 4, unit `HPA`, `^[0-9]{3,4}$`, 745..1100) and `BARO_INHG` (`--.--`, 5, unit `IN`,
`^[0-9]{2}\.?[0-9]{2}$`, 22.00..32.48); the field uses the format of the current unit. Display: hPa `%04.0f`, inHg
`%.2f`.

### 11.2 Layout (standard footer)

| Element | Geometry / text |
|---|---|
| `BARO` | E 28 white at (16, 172) |
| BARO field | InputField `width:152px` → **16..176 × 186..235**, value cyan 27 = preset in the current unit |
| −/+ | IconButtons 50 x 50 **186..236** and **242..292**, y 186..236 |
| STD / QNH | segmented Buttons **16..85.4** and **91.4..160.8**, y **247..287**; `selected` = current reference |
| HPA / IN HG | segmented Buttons **200..269.4** and **275.4..372.4**, y 247..287 |
| `PFD` | E 28 at (420, 172) |
| LS / VV / TAXI | SurvStatusButtons y **186..250** at x 420, 538, 656 (TAXI disabled); baseline 238.6 |
| rule | `0..768 × 303..305` |
| `ND MODE` | L24 at (16, 340.2); RadioButtonGroup (cyan) rows x 16..230, y `352+43k` (k 0..4); circle x 35, text x 67 |
| `BRG PTR` | L24 at (16, 600.2); SurvStatusButtons `VOR 1` **16..110**, `VOR 2` **134..228**, y 612..676 |
| `ND RANGE` | L24 at (250, 340.2); RadioButtonGroup (cyan) rows x 250..440, y `352+43k` (k 0..7); circle x 269, text x 301; `ZOOM` disabled |
| `ND DATA` | L24 at (466, 340.2); SurvStatusButtons y 352..416: CSTR 466, WPT 568, VOR D 670; y 424..488: NDB 466, ARPT 568 |
| `ND OVERLAY` | L24 at (466, 528.2); y 540..604: WX 466, TERR 568, TRAF 670 |

---

## 12. X-Plane data / command map (all verified in XP 12.4.3 `Resources/plugins/DataRefs.txt` / `Commands.txt`)

`w` = writable. "Port" = dataref owned by this plugin (`tools/avionics_provider.py`).

| Need | Source | Notes / fallback |
|---|---|---|
| IR mode/status/alignment | Port `a380x/xp12/adirs/ir{1..3}_{mode, aligning, alignment_remaining, nav_valid, attitude_valid, heading_valid, fault, off, heading_required, heading_reminder, nav_locked}` | X-Plane has **no** IRS alignment datarefs (only AHRS failures `rel_g_arthorz[_2]`); the port ADIRS is the authority |
| IR position, GS, heading, track | Port `ir{n}_latitude_deg, _longitude_deg, _ground_speed_knots, _heading_deg (mag), _track_deg (mag)` | invalid → dashes |
| ATT heading entry | port `a380x_adirs_enter_heading()` via host (§13) | no dataref; returns 0 when not accepted → `NOT ALLOWED` |
| true heading / true track / magvar | `sim/flightmodel/position/true_psi`, `hpath`, `magnetic_variation` (float, n) | |
| wind | `sim/weather/aircraft/wind_now_direction_degt`, `wind_now_speed_msc` (n) | |
| aircraft position | `sim/flightmodel/position/latitude`, `longitude` (double, n) | FM / GNSS / IR all derive from it |
| GPS validity | `sim/cockpit2/radios/actuators/gps_power`, `gps2_power` (int, w); `sim/operation/failures/rel_gps`, `rel_gps2` (6 = failed) | X-Plane provides no GNSS accuracy/HIL/EPU → port model §1.4 |
| FMS position / EPU / RNP | FMS core (port model) | **not provided by X-Plane** |
| VOR tuning | `sim/cockpit2/radios/actuators/nav_frequency_hz[0..1]` (int[12], w, 10 kHz), `nav_power[..]`, `nav_obs_deg_mag_pilot/_copilot[..]` (w), `sim/cockpit2/radios/indicators/nav1_nav_id`, `nav2_nav_id` (byte[150]), `nav_type[..]`, `nav_has_dme[..]`, `nav_dme_distance_nm[..]`, `nav_bearing_deg_mag[..]`; `nav1_tuning_mode` (n: 0 manual, 1 FMS auto) | failures `rel_nav1`, `rel_nav2`, `rel_dme` |
| LS (MMR) tuning | recommended `nav_frequency_hz[2..3]`, `nav_course_deg_mag_pilot[2]`/`_copilot[3]`, `nav_hdef_dots_pilot/_copilot[..]`, `nav_vdef_dots_*[..]`, `nav3_nav_id`, `nav4_nav_id` | GS angle only exists as `sim/cockpit/radios/nav1_slope_degt`/`nav2_slope_degt` → for NAV3/4 use navdata; `rel_gls` = GS failure |
| ADF | `sim/cockpit2/radios/actuators/adf1_frequency_hz`, `adf2_…`, `adf1_power`; indicators `adf1_nav_id`, `adf1_bearing_deg_mag`; `rel_adf1/2` | FBW MFD has **no ADF** fields; not used on these pages |
| XPDR code | `sim/cockpit2/radios/actuators/transponder_code` (int, w, 0000-7777 decimal digits) | |
| XPDR mode | `sim/cockpit2/radios/actuators/transponder_mode` (w: 0 off, 1 stby, 2 A, 3 C, 4 test, 5 GND S, 6 TA only, 7 TA/RA) | port law §4.2 is the only writer (EFB actions `a380x_efb.c:409-413` also write it — must route through SURV state) |
| IDENT | command `sim/transponder/transponder_ident`; status `sim/cockpit2/radios/indicators/transponder_id` (n) | |
| XPDR failure | `sim/operation/failures/rel_xpndr` | |
| Mode-S flight ID | `sim/cockpit2/radios/actuators/flight_id` (byte[8], w) | optional: from INIT ATC callsign |
| TCAS mode | via `transponder_mode` 6/7 (above) | no separate TCAS mode dataref |
| TCAS ABV/BLW | `sim/cockpit2/radios/actuators/tcas_filter` (w: 0 normal, 2 above, 3 below) | |
| TCAS status | `sim/cockpit2/tcas/indicators/tcas_alert`, `tcas_active_advisory` (read) | no TCAS failure dataref → `tcas_fault` port-derived |
| WXR | `sim/cockpit2/EFIS/EFIS_weather_mode[_copilot]` (0 off, 1 test, 2 WX, 3 WX+T, 4 MAP, 5 TURB), `EFIS_weather_gain`, `EFIS_weather_tilt`, commands `sim/instruments/EFIS_wxr_radar_*`; failure `rel_wxr_radar` | port has no radar image → WXR section stays failed/disabled (FBW) |
| TAWS/GPWS inhibits | none in X-Plane (only `sim/cockpit2/annunciators/GPWS`, n) | port SURV states `a380x/xp12/surv/*` (new) |
| baro | `sim/cockpit2/gauges/actuators/barometer_setting_in_hg_pilot/_copilot` (w), `barometer_setting_is_std_pilot/_copilot` (w) | consumed by `efis_update` |
| ND mode/range | `sim/cockpit2/EFIS/map_mode[_copilot]` (0..4), `map_range[_copilot]` (w) | consumed by `efis_update` |
| weather / METAR | `XPLMGetMETARForAirport`, `XPLMGetWeatherAtLocation` (XPLMWeather.h); `sim/weather/region/weather_source` (n) | §9.4 |
| UTC | `sim/time/zulu_time_sec` (float), `sim/cockpit2/clock_timer/zulu_day/_month` | |
| on ground | `sim/flightmodel/failures/onground_any` | (already used by the port) |

---

## 13. Systems-host interface additions (`A380XMfdSystemsHost`, spec 12 §7)

Pages stay XPLM-free; the adapter (WP-J) implements:

```c
typedef struct A380XMfdSystemsHost {
  /* FCU (§10): button-like actions run the existing command handler so engagement
     checks stay identical; begin/end let the handler's own repeat timer work. */
  void (*fcu_command_once)(void *ctx, int fcu_cmd);           /* a380x/xp12/fcu/<name> */
  void (*fcu_command_hold)(void *ctx, int fcu_cmd, int down); /* XPLMCommandBegin/End */
  void (*fcu_rotate)(void *ctx, int a380x_fcu_rotate_cmd, double detents); /* typed entry */
  void (*efis_command_once)(void *ctx, int side, int efis_action);
  void (*efis_command_hold)(void *ctx, int side, int efis_action, int down);
  void (*efis_set_baro_hpa)(void *ctx, int side, double hpa);  /* writes stock in-Hg dataref */
  /* SURV (§4) */
  void (*surv_set)(void *ctx, int surv_field, int value);     /* xpdr_auto, alt_rptg, tcas_level, ... */
  void (*xpdr_set_code)(void *ctx, int code_digits);
  void (*xpdr_ident)(void *ctx);
  /* ADIRS (§2) */
  int  (*adirs_enter_heading)(void *ctx, int unit, double mag_heading); /* 1 accepted */
  /* NAV radios (§3) */
  void (*nav_tune)(void *ctx, int receiver, int freq_10khz, double course_deg_or_nan);
  /* ATC datalink (§9.4): asynchronous; the answer arrives through A380XFmsInputs */
  void (*atis_request)(void *ctx, const char icao[5], int type, int overwrite, uint32_t request_id);
} A380XMfdSystemsHost;
```

`fcu_rotate` = the body of `fcu_command_handler` (`a380x_fcu_adapter.inc:84-120`) factored into
`fcu_apply(command, amount)` and called with an arbitrary amount (WP-J refactor, no new XPLM command). For typed SPD
and V/S entry the host first applies amount 0 (collapses dashes to the current value exactly like a first detent),
then the computed difference.

`A380XFmsInputs` additions (spec 07 §9.4): `adirs[3]` (fields of §12 row 1-2), GPS validity ×2, SURV readbacks
(transponder code/mode, ident, tcas filter, failures), FCU/guidance window state (`selected_*`, `*_managed`,
`*_dashes`, `mach_mode`, `trk_fpa_mode`, `true_heading`, `metric_alt`, `altitude_increment_1000`, AP/FD/A/THR/LOC/APPR
flags, `powered`), EFIS state per side (the `EfisSide` struct, `a380x_efis.h`), ATIS answers
`{request_id, status, icao, letter, hhmm, text[512]}`.

---

## 14. Quirks (do not replicate)

| # | Quirk | Port | Source |
|---|---|---|---|
| Q1 | Tenths digit of coordinates from a DMS split | true tenths | `Convert.ts:73-75` |
| Q2 | POS2 = POS1, GNSS2 = GNSS1 | POS2 = POS1 kept (one FMS core); GNSS2 from GPS2 | `MONITOR:112,146` |
| Q3 | Bearing/distance `0` show dashes; NM unit keyed on bearing | null tests | `MONITOR:161-171` |
| Q4 | BRG/DIST field `containerStyle` stray quote | keep effective width 146 | `MONITOR:655` |
| Q5 | Frozen-time label `position:fixed` lands on the CAPT half for FO | own side | `.scss:29-36` |
| Q6 | IRS banner snapshot only on aligned toggle; `0,0` fallback | 1 Hz, placeholder | `IRS:124-146` |
| Q7 | IRS col3/col4 stale text | recompute | `IRS:258-276` |
| Q8 | IRS data block ignores SSM; MAG VAR sign inverted; ACCURACY NM shown as FT | validity, E positive, feet | `IRS:185-208` |
| Q9 | `flex: 1 display: flex` invalid declaration | keep left alignment | `IRS:291` |
| Q10 | VOR course 0 clears the course | accept 0 | `NAVAIDS:405,461` |
| Q11 | LS entry continues after `NOT ALLOWED` (MMR locked) | stop | `NAVAIDS:279-281` |
| Q12 | LS class `ILS/DME` without DME; deselected ident from `databaseId.substring(7)` | real class/ident | `NAVAIDS:160,169` |
| Q13 | RADIO NAV MODE label right-justify ignored | right aligned | `NAVAIDS:532` |
| Q14 | WXR failed on CONTROLS but WXR items not failed on STATUS | failed on both | `SURV CONTROLS:86`, `STATUS:37-53` |
| Q15 | SQWK 0000 rejected | accepted | `SURV CONTROLS:284` |
| Q16 | `NOT YET IMPLEMENTED` veil and sample messages on CONNECT/MSG RECORD | offline model §6.2 | ATCCOM pages |
| Q17 | D-ATIS overlaps: status vs UPDATE OR PRINT, PRINT ALL vs UPDATE ALL, `>>>` into next block, buttons into the footer | repacked §9.2 | `DAtisBlock.tsx:253-326`, `MfdAtccomDAtis.tsx:52-66` |
| Q18 | D-ATIS RECEIVED dereferences `[0]` unchecked; CSS re-wraps the 39-char rows | guard; draw rows | `MfdAtccomDAtisReceived.tsx:129-130` |
| Q19 | MSG RECORD single arrows 66 px wide, elements 32 px too wide | 58 px; list width | `MessageRecordNav.tsx:11-12`, `.scss:10-17` |
| Q20 | Literal `<br />` in string labels (POSITION UPDATE, ALIGN ON OTHER REF, FREEZE ALL IRS, MODIFY MAX UPLINK DELAY, SEND REQUEST, AUTO UPDATE, UPDATE OR PRINT, PRINT ALL, UPDATE ALL, RETURN TO LIST) | two lines | spec 01 Q7 |
| Q21 | FCU BKUP → 404 + auto-back | designed pages §10-11 | `MfdPageDirectory.tsx` |

---

## 15. Open questions

1. **Box sizing.** The FBW constants of these pages fit the 768 x 812 body almost exactly under
   `box-sizing: border-box` (CONNECT row 1: 305+167+296 = 768; D-ATIS status 469+131 = 600 = next button x;
   SURV STATUS `width:100%; padding:50px`; MONITOR total 882.5 of 886 px; IRS 943 of 950; NAVAIDS 806 of 812;
   CONNECT 799 of 812), but overflow under the content-box model that spec 01 makes normative. This strongly
   suggests the MSFS/Coherent base stylesheet applies border-box. If spec 01 is revised to border-box, every widget
   size in spec 01 §13 and the fitted layouts here must be recomputed (the fit adjustments then largely disappear).
2. **EPU and RNP defaults** (§1.4) are a port model; FBW `Navigation`/`RequiredPerformance` (fbw-a32nx `@fmgc`) are
   not in the sparse checkout. Obtain the pinned files to replace the model.
3. **Receiver allocation**: VOR1/2 on NAV1/2 and the MMRs on NAV3/4 require moving the PFD/guidance ILS inputs
   (`a380x_fcu_adapter.inc:365-378`) to the `nav_*[2..3]` arrays and the GS angle to navdata (WP-J decision).
4. **Navaid tuner scope**: Phase 1 = manual VOR/LS tuning + LS auto-tune from the active approach; FBW
   auto-tuning/DME-pair selection (`NavaidTuner`, `NavaidSelectionManager`) needs the fmgc sources.
5. **D-ATIS synthesis default** (`sim` vs `off`) and wording; the template is synthetic and must be documented as
   such in the user docs.
6. **SET HDG unit**: HeadingFormat shows `°T`, but ATT-mode heading entry is magnetic on the aircraft and in the
   port ADIRS. Use a magnetic variant of the format (`°`)?
7. **ACTIVE/NEXT ATC when not connected**: empty (recommended) vs dashes; confirm against aircraft imagery.
8. **EXPED** on the A380 AFS CP: FBW A380X has none and the port has no command; the backup page draws it disabled.
   Confirm whether the real A380 AFS CP has an EXPED pb or remove it.
9. **FCU BKUP visual design** (§10-11) follows public descriptions and FBW MFD widgets; no aircraft image of the
   pages was available. Validate the arrangement (in particular PUSH/PULL buttons and the managed dot) against
   FCOM imagery when available. Should an explicit "EFIS CP BACKUP" activation button (A350 style) exist?
10. **ALT pb light**: mirrored from `altitude_hold_active` like the 3D cockpit; confirm the A380 ALT pb has a light.
11. **TERR SYS OFF** gating the ND terrain layer is a port decision (FBW only writes the L:var).
