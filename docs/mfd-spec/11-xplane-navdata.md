# 11 — X-Plane 12 navigation data for the native FMS

Status: implementation spec for `native/avionics/a380x_navdata.h/.c` (navigation database service) and the data
contract behind `a380x_fms_fpln.h/.c` (WP-D, see `12-architecture.md` §3/§7). The formats below were checked
against the official Laminar Research specification PDFs and against the files installed with X-Plane 12.4.3 on the
development machine (`Resources/default data/` cycle 2406, `Custom Data/` Navigraph cycle 2609).

Tags: **[XP]** what X-Plane defines; **[OBS]** observed in the installed files; **[PORT]** what the port does.

## 0. Sources

| Topic | Source |
|---|---|
| Layering, file list, precedence | https://developer.x-plane.com/article/navdata-in-x-plane-11/ (updated 5 Jun 2026) |
| `earth_fix.dat` | XP-FIX1200 spec: https://developer.x-plane.com/wp-content/uploads/2021/09/XP-FIX1200-Spec.pdf |
| `earth_nav.dat` | XP-NAV1200 spec (rev. 5 Jun 26): https://developer.x-plane.com/wp-content/uploads/2016/10/XP-NAV1200-Spec-2.pdf |
| `earth_awy.dat` | XP-AWY1101 spec: https://developer.x-plane.com/wp-content/uploads/2019/01/XP-AWY1101-Spec.pdf |
| `earth_hold.dat` | XP-HOLD1140 spec: https://developer.x-plane.com/wp-content/uploads/2018/12/XP-HOLD1140-Spec.pdf |
| `CIFP/<ICAO>.dat` | XP-CIFP1101/1102: https://developer.x-plane.com/wp-content/uploads/2019/01/XP-CIFP1101-Spec.pdf; XP 12 edition XP-CIFP1250: https://developer.x-plane.com/wp-content/uploads/2016/10/XP-CIFP1250-Spec.pdf |
| ARINC 424 enumerations (route types, altitude descriptions, waypoint types) | https://developers.navigraph.com/docs/navigation-data/dfd-data-format-v2 |
| Reference parsers (field indices cross-checked) | LittleNavMap atools `src/fs/xp/xpcifpreader.cpp`, `xpfixreader.cpp`, `xpnavreader.cpp`, `xpairwayreader.cpp`, `src/fs/util/fsutil.cpp` (github.com/albar965/atools); AviTab `src/libxdata/parsers/CIFPParser.cpp` |
| AIRAC dates | FAA 28-day subscription README for 2026-09-03 (cycle 2609); epoch cycle 2001 = 2020-01-02 |
| XPLM | `XPLMNavigation.h`, `XPLMScenery.h` (XPSDK430): `XPLMFindNavAid`, `XPLMGetNavAidInfo`, `XPLMGetMagneticVariation` |

No `earth_aptmeta.dat` specification is published; §5 is inferred from the files and real-world values and is
marked unverified where it matters.

## 1. Files, layering, precedence

**[XP]** X-Plane loads its global navigation database from one of these layers:

1. `Custom Data/earth_424.dat` (raw ARINC 424). If present, it is the only source. **[PORT]** Not supported; a
   warning in the log would be the adapter's job. No such file exists on the development machine.
2. `Resources/default data/` — the base layer shipped and updated by Laminar: `earth_fix.dat`, `earth_nav.dat`,
   `earth_awy.dat`, `earth_hold.dat`, `earth_mora.dat`, `earth_msa.dat`, `earth_aptmeta.dat`, `CIFP/<ICAO>.dat`,
   airspaces, `DOF.DAT`.
3. `Custom Data/` — the same files from a navdata provider (Navigraph here). The navdata article says these files
   *replace* the base layer; mixing cycles is not supported.
4. `Custom Data/FAACIFP18` (raw FAA CIFP) overrides terminal records per airport. **[PORT]** Not supported.
5. `Custom Data/user_fix.dat` / `user_nav.dat` add or override points. **[PORT]** Not read (pilot-defined data lives
   in the FMS).

**[PORT]** Precedence, file by file (the same policy as LittleNavMap):

| Data | Rule |
|---|---|
| `earth_fix.dat`, `earth_nav.dat`, `earth_awy.dat`, `earth_aptmeta.dat`, `earth_hold.dat` | `Custom Data/<file>` if it exists, else `Resources/default data/<file>` |
| `CIFP/<ICAO>.dat` | `Custom Data/CIFP/<ICAO>.dat` if it exists, else the default-data file for that airport |
| AIRAC cycle | `Custom Data/cycle.json` when Custom Data provides `earth_nav.dat`, else the `data cycle NNNN` of the `earth_nav.dat` actually used |

Roots are passed in `A380XNavdataConfig` (`xplane_root` from `XPLMGetSystemPath()`, or explicit directories for
tests). `/` separators are used on all platforms; the Windows CRT accepts them.

**Do not read `Custom Data/cycle_info.txt`.** Its Navigraph licence text forbids third-party processing
(`03-fms-init-fuel-data-sec-msg.md` §5.4).

Installed sizes (cycle 2609, Custom Data): `earth_fix.dat` 15.0 MB / 250 989 lines, `earth_nav.dat` 3.7 MB /
39 424 lines, `earth_awy.dat` 4.7 MB / 111 071 lines, `earth_hold.dat` 2.1 MB / 28 947 lines,
`earth_aptmeta.dat` 1.0 MB / 15 624 lines, `CIFP/` 14 943 airport files (default data: 15 911).

## 2. Common `.dat` conventions

- Line 1: `I` (or `A`); the Custom Data files are **CRLF**, the default data **LF**; a UTF-8 BOM can appear in
  hand-edited files.
- Line 2: version line, e.g. `1200 Version - data cycle 2609, build 20260826, metadata NavXP1200. Copyright (c)
  2025 Navigraph, Datasource Jeppesen`. The cycle is the 4 digits after `data cycle `.
- Line 3: blank. Data rows follow, fields separated by runs of spaces/tabs. The file ends with a line `99`.
- **[PORT]** Parsers: own tokenizer and locale-independent number parser (no `strtod`/`sscanf`, so a host locale with
  `,` decimals cannot break parsing); lines longer than 1200 bytes are skipped; every row with a missing/invalid
  numeric field or an out-of-range latitude/longitude is skipped and counted (`A380XNavStats.malformed_lines`).
  On the installed cycle 2609 the count is 0.

## 3. `earth_fix.dat` (XP-FIX1200)

```
 51.672138889   -0.694255556  UMLAT ENRT EG 4464727 UMLAT
 45.362830556    5.372077778  D088B LFLS LF 4595785 SG088002
```

| Col | Field |
|---|---|
| 0, 1 | latitude, longitude (decimal degrees) |
| 2 | ident (≤ 5 chars) |
| 3 | terminal area: airport ICAO for terminal fixes, `ENRT` for enroute fixes |
| 4 | ICAO region (terminal fixes: the airport's region) |
| 5 | ARINC 424 waypoint type (§5.42), the three columns packed little-endian: `c1 = v & 0xFF`, `c2 = v >> 8 & 0xFF`, `c3 = v >> 16 & 0xFF`. 4530263 = 0x452057 = `W`,` `,`E` (RNAV waypoint, STAR); 2105430 = `V`,` `,` ` (VFR) |
| 6.. | printed/spoken name (may contain spaces) |

**[PORT]** Stored: position, ident, area, region, type (3 chars, `A380XNavFix.wpt_type`). The spoken name is not
kept (the FMS never shows it). An unparsable type is tolerated (the point is kept).

## 4. `earth_nav.dat` (XP-NAV1200)

```
 3  51.330875000    0.034811111      589    11510   130      1.000  BIG ENRT EG BIGGIN VOR/DME
12  51.330875000    0.034811111      589    11510   130      0.000  BIG ENRT EG BIGGIN VOR/DME
 2  44.575694444   26.071944444        0      659    15      0.000   LL ENRT LR OTOPENI (BUCHAREST) NDB
 4  51.477466667   -0.493719444       78    11030    18  96749.709  IRR EGLL EG 27R ILS-cat-III
 6  51.478841667   -0.438194444       78    11030    18 300269.709  IRR EGLL EG 27R GS
12  51.478844444   -0.459030556       99    11030    25      1.000  IRR EGLL EG HEATHROW DME-ILS
```

Columns: 0 row code, 1 lat, 2 lon, 3 elevation ft, 4 frequency, 5 range/class, 6 type-specific value, 7 ident,
8 airport ICAO or `ENRT`, 9 region, then (rows 4/5/6/7/8/9/14/15/16) 10 runway and 11.. name, or (rows 2/3/12/13)
10.. name.

| Row | Meaning | Frequency | Col 6 |
|---|---|---|---|
| 2 | NDB | kHz | 0 (1.0 = BFO needed) |
| 3 | VOR, VOR/DME, VORTAC, TACAN (name suffix) | MHz × 100 | slaved variation: station declination, **east positive** (BIG LAKE AK 19.000) |
| 4 | ILS localizer | MHz × 100 | `magCourseInt·360 + trueCourse`: `fmod(v,360)` = true front course, `floor(v/360)` = magnetic course (integer) — 96749.709 → 269.709 °T, 268 °M |
| 5 | LOC / LDA / SDF | MHz × 100 | as row 4 |
| 6 | glide slope | MHz × 100 | `angle·100000 + trueCourse` (angle in hundredths): 300269.709 → 3.00°, 269.709 °T. Decode `angle = floor(v/1000)/100` |
| 7/8/9 | OM/MM/IM | 0 | localizer bearing |
| 12 | DME paired with a VOR or ILS (no runway column; `DME-ILS` suffix) | MHz × 100 | DME bias NM |
| 13 | standalone DME / TACAN / NDB-DME | MHz × 100 | DME bias NM |
| 14 / 15 / 16 | FPAP / GLS station / LTP-FTP | channel | course or `angle·100000 + course` |

**[PORT]** Merging into one record per facility (what an FMS calls a navaid):

- VOR (3) + DME (12) with the same ident/region within 3 NM → one `A380X_NAV_VOR` with `A380X_NAVF_DME`
  (+ `A380X_NAVF_TACAN` for VORTAC names). A TACAN-only row 3 becomes `A380X_NAV_DME` + TACAN.
- LOC (4/5) + GS (6) + DME-ILS (12, same ident + airport) → one `A380X_NAV_LOC` with `GS`/`DME` flags,
  true/magnetic course, glide angle, DME position.
- Unpaired DME (12/13) → `A380X_NAV_DME`.
- Markers, FPAP, GLS, LTP (7/8/9/14/15/16) are not loaded (no FMS use yet).

Cycle 2609 result: 12 759 navaids (from 39 418 rows).

## 5. `earth_aptmeta.dat` (XP-APT1210 metadata; spec not published — inferred, unverified)

```
EGLL EG  51.477500000   -0.461388889    83 C 12700 I  6000    -1
KJFK K6  40.639927778  -73.778691667    13 C 14500 I 18000 FL180
```

| Col | Inferred meaning | Evidence |
|---|---|---|
| 0 | airport ident | |
| 1 | ICAO region | KJFK = K6 |
| 2, 3 | airport reference point lat/lon | matches published ARPs |
| 4 | elevation ft | EGLL 83, KJFK 13 |
| 5 | ARINC 5.177 public/military: `C` civil, `M` military, `P` private, `J` joint | |
| 6 | longest runway, ft in hundreds precision | EGLL 12 799 ft → 12700; KJFK 14 511 → 14500 |
| 7 | `I` / `0` — probably IFR capability (unverified) | |
| 8 | transition altitude ft (0/-1 none) | EGLL 6000 |
| 9 | transition level `FLnnn`, or `-1` (assigned by ATC) | KJFK FL180 |

**[PORT]** This file supplies the airport reference point, elevation and the database **transition altitude/level**
(FBW `Airport.transitionAltitude/Level`, spec 07 §6.2) without the 380 MB `apt.dat`. `A380XNavAirportInfo`
returns TL in feet (FL180 → 18000). The airport **name** is not in any navdata file: it comes from the host
(`XPLMGetNavAidInfo(..., outName)`). Airports missing from the file (15 620 entries in 2609) are resolved through
the host callback (`XPLMFindNavAid(NULL, icao, NULL, NULL, NULL, xplm_Nav_Airport)`; `outHeight` is in metres).

## 6. `CIFP/<ICAO>.dat` (XP-CIFP1101/1102/1250)

Record syntax: `TYPE:` then comma-separated fields; `;` separates optional groups and ends the line. Fields keep
their fixed-width space padding — trim every field. Records are grouped by procedure, route type and transition
and sorted by sequence number. Types: `SID`, `STAR`, `APPCH`, `RWY`, `PRDAT` (approach continuation), and in
X-Plane 12.5+ `HSID`/`HSTAR`/`HAPPCH`/`HPRDAT` (helicopters). There is **no airport/ARP record**.

### 6.1 SID / STAR / APPCH fields (index after the colon)

```
SID:020,2,DET2F,RW27R,D161J,EG,P,C,EY  ,L,   ,CF,Y,LON,EG,D, ,      ,1613,0100,1350,0080, ,     ,     ,     , ,   ,    ,   , , , , , , , , ;
APPCH:020,I,I27R, ,FF27R,EG,P,C,E  F, ,   ,CF, ,IRR,EG,P,I,      ,0887,0098,2680,0025,G,02500,02500,     , ,   ,-300,   ,EGLL,EG,P,A, ,0,D,S;
APPCH:027,R,R13L, ,JEVNI,K6,P,C,E   ,R,302,RF, , , , , ,002100,    ,    ,    ,0033,+,00313,     ,     , ,   ,-300,   ,CFBMG,K6,P,C, ,A,P,S;
APPCH:060,I,I04L, ,DUFFY,K6,P,C,EE H,L,   ,HM, , , , , ,      ,    ,    ,2420,T010, ,     ,     ,     , ,   ,    ,   , , , , , ,0,N,S;
```

| Idx | Field (ARINC 424 ref) | Encoding / port decoding |
|---|---|---|
| 0 | sequence number (5.12) | `010`, `020`… |
| 1 | route type (5.7) | §6.3 |
| 2 | procedure ident | `CPT3F`, `I27R`, `R22LX`, `R19-Y`, `VDMA` |
| 3 | transition ident (5.11) | runway `RW27R`, `RW27B` (all parallel 27s), `ALL`, enroute fix, approach IAF; blank for common/final |
| 4, 5 | fix ident, fix region | |
| 6, 7 | fix section, subsection (5.4/5.5) | `P C` terminal wpt, `P G` runway, `P I` localizer, `P N` terminal NDB, `P A` airport, `E A` enroute wpt, `D ` VHF navaid, `D B` NDB |
| 8 | waypoint description code (5.17), 4 positional chars | col 1 `E` essential, `G` runway, `V` VHF, `N` NDB, `A` airport; col 2 `Y` fly-over, `B` fly-over end of route, `E` end of route; col 3 `M` **first leg of the missed approach**; col 4 `A` IAF, `B` IF, `F` FAF, `I` FACF, `M` MAP, `H` holding fix |
| 9 | turn direction (5.20) | `L`, `R`, `E` |
| 10 | RNP (5.211) | 2-digit mantissa × 10^−(3rd digit): `010` = 1.0, `302` = 0.30 |
| 11 | path terminator (5.21) | IF TF CF DF AF RF HF HA HM CA CD CI CR FA FC FD FM PI VA VD VI VM VR |
| 12 | turn direction valid (5.22) | `Y` |
| 13–16 | recommended navaid ident, region, section, subsection | |
| 17 | arc radius (5.204) | 6 digits, thousandths NM (`002100` = 2.1 NM) |
| 18 | theta (5.24) | tenths of a degree (magnetic bearing from the navaid) |
| 19 | rho (5.25) | tenths NM |
| 20 | course (5.26) | tenths of a degree magnetic; `nnnT` = integer degrees **true** (`099T`, 697 legs in 2609) |
| 21 | route / holding distance or time (5.27) | tenths NM; `Tnnn` = tenths of minutes (`T010` = 1.0 min) |
| 22 | altitude description (5.29) | §6.4 |
| 23, 24 | altitude 1, altitude 2 (5.30) | `05000` ft or `FL350`; blank = none |
| 25 | transition altitude (5.53) | ft |
| 26 | speed limit description (5.261) | `@`/blank at, `+` minimum, `-` maximum |
| 27 | speed limit (5.72) | kt |
| 28 | vertical angle (5.70) | signed hundredths of a degree: `-300` = −3.00° |
| 29 | vertical scale factor (5.293) | unused |
| 30–33 | centre fix (RF; 5.144) or TAA procedure-turn indicator, its region, section, subsection | also seen on FAF/FA legs (airport reference) — the port uses it only for RF |
| 34 | multiple code / TAA sector (5.130) | |
| 35 | GNSS/FMS indicator (5.222) | |
| 36, 37 | route qualifiers 1, 2 (5.7) | approaches: `D` DME required, `J` GNSS required, `P` (see §6.6), `N`, `L` GBAS; `S` straight-in, `C` circling, `A`, `V` |

**[PORT]** decoding rules worth noting:

- A blank altitude description with an altitude present means **AT** (`'@'`); a blank speed description with a speed
  means `'@'`. Example: CPT3F `D100H`, blank + `06000` → at 6000.
- `F`-type legs (FA, FC, FD, FM): the fix is the **start** point. All other fix legs terminate at the fix.
- Fix references are resolved at load time to lat/lon from the global database (section/subsection select the
  kind; terminal records prefer the airport's terminal area; ties resolve to the candidate nearest the airport;
  exact-kind matches up to 1500 NM, any-kind fallback within 400 NM). Runway fixes (`P G`) come from the file's own
  `RWY` records. Unresolved fixes keep their ident with NaN position (`fix_unresolved`, counted per airport).
  World sweep of all 14 943 Custom Data airports: 769 937 legs, **23 unresolved references, all to runways whose
  `RWY` record has no coordinates**.
- Duplicate (route type, transition) groups inside one procedure are dropped (observed in some files).

### 6.2 `RWY` record

```
RWY:RW09L,     ,      ,00079, ,IAA ,3,   ;N51283900,W000290597,1014;
RWY:RW18 ,     ,      ,00615, ,    , ,   ;
```

| Idx | Field | Encoding |
|---|---|---|
| 0 | runway ident (5.46) | `RW09L` (pad-trimmed) |
| 1 | gradient (5.212) | signed; scaling unverified (unused) |
| 2 | ellipsoid height (5.225) | unused |
| 3 | landing threshold elevation (5.68) | ft |
| 4 | TCH value indicator (5.270) | |
| 5 | localizer/GLS ident (5.44) | |
| 6 | ILS/MLS/GLS category (5.80) | `1`..`3` |
| 7 | threshold crossing height (5.67) | ft |
| 8 | threshold latitude (5.36) | `N/S DD MM SS ss` (hundredths of seconds): `N51283900` |
| 9 | threshold longitude (5.37) | `E/W DDD MM SS ss` |
| 10 | displaced threshold distance (5.69) | ft |

There is **no runway length, bearing or width**. About 5 % of airports omit the coordinate group (fields 8–10).

**[PORT]** derived runway data (`A380XNavRunway`):

- Opposite end = number + 18 (mod 36), `L`↔`R`, `C`↔`C`. True bearing = great-circle bearing threshold → opposite
  threshold; else the LOC true course when the LOC serves this runway; else NaN.
- Physical length = threshold-to-threshold distance + both displacements (EGLL 09L/27R: 12 763 ft computed vs
  3 902 m = 12 802 ft published). NaN for single-ended runways or runways without coordinates (3 745 of 37 679 in
  2609).
- Runway start = threshold moved back by the displacement along the reciprocal.
- Magnetic bearing = true − airport variation (§8). LS frequency/course/glide slope from the matching `earth_nav`
  LOC (ident + airport).
- Runways without coordinates are kept (they appear in menus) with NaN positions; a runway leg falls back to the
  airport reference point.

### 6.3 Route types and how the port assembles a procedure

| Kind | Route types → role (`A380XProcTransRole`) |
|---|---|
| SID | `0` engine-out (EOSID; not offered); `1` `4` `F` `T` runway transition; `2` `5` `M` (and others) common route; `3` `6` `S` `V` enroute transition |
| STAR | `1` `4` `7` `F` enroute transition; `2` `5` `8` `M` common route; `3` `6` `9` `S` runway transition |
| APPCH | `A` approach transition (VIA, ident = IAF); any other letter = final approach incl. missed approach: `B` LOC BC, `D`/`S` VOR/DME, `F` FMS, `G` IGS, `I` ILS, `J` GLS, `L` LOC, `M`/`W`/`Y` MLS, `N` NDB, `P` GPS, `Q` NDB/DME, `R` RNAV, `T` TACAN, `U` SDF, `V` VOR, `X` LDA |

Observations: EGLL publishes runway-specific SIDs as *common routes* with transition `RW27R` (route type 2); LFPG
uses runway transitions (type 4) + a blank-ident common route (type 5); most STARs use `ALL`.

Leg list of a selection (FBW segments, spec 05 §2.1):

- Departure: origin runway leg (`EGLL27R`, IF at the threshold) → SID runway transition matching the runway
  (`RW27R`, `RW27B`, `ALL`) → common route (runway-specific one first, else the generic one) → enroute transition.
  Adjacent parts that repeat the junction fix are merged (constraints of the later copy win); a first SID leg that
  is an IF/TF at the departure runway is dropped.
- Arrival: STAR enroute transition → common → runway transition matching the approach runway → approach VIA →
  final approach up to the leg flagged `M` in description col 3 → missed approach (segment MISSED). STAR → approach:
  same fix = merged; an F-type first leg from the STAR's last fix (EGLL I27R VIA BNN starts `FC BNN`) connects
  without a discontinuity; otherwise an automatic discontinuity.
- Without an approach the destination segment holds the destination runway (or airport) leg.

Approach naming (spec 05 §8.2): type name + runway designator + `-` multiple indicator, e.g. `ILS27R`, `RNV22L-X`.
The approach ident encodes runway and multiple indicator: `I27R`, `R22LX` (side L, indicator X), `R28RF`,
`R19-Y`; idents without digits (`VDMA`) are circling approaches (no runway; excluded by the ARR page).

### 6.4 Altitude descriptions (5.29)

`+` at/above alt1; `-` at/below alt1; `@`/blank at alt1; `B` between (alt1 upper, alt2 lower); `C` at/above alt2;
`G` alt1 at (FAF), alt2 = glide slope altitude; `H` alt1 at/above, alt2 = GS altitude; `I` alt1 at (FACF), alt2 =
GS intercept; `J` alt1 at/above, alt2 = GS intercept; `V` alt1 at/above (step-down), alt2 on the vertical angle;
`X` alt1 at, alt2 on the angle; `Y` alt1 at/below, alt2 on the angle. These are FBW's `AltitudeDescriptor` values
1:1, so the plan stores the character.

### 6.5 `PRDAT`

Approach continuation (PF) record: three pairs of level-of-service authorised (`A`/`N`) + name (`LPV`,
`LNAV/VNAV`, `LNAV`, `LP`), four pairs of authorisation + a 3-char field (5.296, unverified), route qualifiers 1/2.
It carries no procedure ident and applies to the preceding APPCH group. **[PORT]** Ignored for now.

### 6.6 RNP AR

The CIFP has no explicit "authorisation required" field. KJFK `R13L`/`R13R` (RNAV (RNP), RF legs, RNP 0.3) carry
route qualifier 1 = `P`; RNAV (GNSS) approaches carry `J`. **[PORT]** `rnp_ar = (qualifier 1 == 'P')` — a
heuristic, documented as unverified; it only adds the ` (RNP)` label suffix.

## 7. `earth_awy.dat` (XP-AWY1100/1101)

```
  BIG EG  3 DIKVU EG 11 N 1  90 245 L9
  BIG EG  3 DIKVU EG 11 F 2 250 460 UL9
```

| Col | Field |
|---|---|
| 0–2 | fix 1 ident, region, type (`11` fix, `2` NDB, `3` VHF navaid) |
| 3–5 | fix 2 ident, region, type |
| 6 | direction restriction: `N` none, `F` forward only (1→2), `B` backward only |
| 7 | `1` low, `2` high (a segment on both is listed twice) |
| 8, 9 | base, top (hundreds of feet) |
| 10 | airway name(s), several joined by `-` (`J13-J14`) |

The file has no coordinates; endpoints are resolved against `earth_fix`/`earth_nav` enroute records by
ident + region + type. **[PORT]** Segments are indexed by airway name (hash), the low/high duplicates merged
(91 048 segments, 9 865 names in 2609). Airway names are reused worldwide (`A1` exists in many FIRs), so a path
search starts from the node matching the from-fix ident, region and position (≤ 5 NM) and walks the name's
segment graph breadth-first in either direction. Direction restrictions are kept but not enforced (like the FMS
airway entry). Intersection of two airways = first node of airway 1 (nearest along the airway from the start)
that is also a node of airway 2.

## 8. `earth_hold.dat` (XP-HOLD1140)

```
  LAM EG ENRT  3    262.0      1.5      0.0 L    14000    17000        0
LOGAN EG ENRT 11    287.0      1.0      0.0 L    10000    14000      220
```

Columns: 0 ident, 1 region, 2 airport or `ENRT`, 3 fix type (11/2/3), 4 inbound course **magnetic**, 5 leg time
min (0 for DME holds), 6 leg distance NM (0 for timed holds), 7 turn `L`/`R`, 8 minimum altitude ft (0 none),
9 maximum altitude ft (0 none), 10 speed limit kt (0 = ICAO default). **[PORT]** Loaded on first use; feeds
`a380x_fpln_default_hold(..., use_database = 1)` (a DATABASE hold for the HOLD page, spec 05 §11 [PORT]).

## 9. Magnetic variation

The files carry magnetic data only indirectly (VOR station declination, LOC magnetic course, CIFP courses). FBW
legs carry a procedure-design `magVar`. **[PORT]** order of preference:

1. Host callback `magvar(lat, lon)` → `XPLMGetMagneticVariation(lat, lon)` (XPLM300; the same model as X-Plane's
   instruments; east positive).
2. Per airport: mean of (true − magnetic) of the airport's localizers (EGLL: 1.71°E from the four ILS).
3. Inverse-distance mean of the four nearest VOR station declinations, cached on a 1° grid. VOR declinations can
   be decades old (several degrees off), hence last.

Procedure legs use the airport's variation; enroute legs the variation at their fix.

## 10. AIRAC cycle and dates

`Custom Data/cycle.json` = `{"cycle":"2609","revision":"1","name":"X-Plane 12"}`; otherwise the `data cycle` of
`earth_nav.dat` (default data here: 2406). Dates (spec 03 §5.4): epoch cycle 2001 effective 2020-01-02, 28-day
cycles; `k0 = ceil(days(2020-01-02 → YYYY-01-01) / 28)`, `eff = epoch + (k0 + NN − 1)·28`, `exp = eff + 28`
(FBW `DatabaseIdent.effectiveTo` = first day of the next cycle). Checks: 2609 → 2026-09-03 … 2026-10-01; 2410 →
2024-10-03; 2406 → 2024-06-13; 2014 → 2020-12-31 (a 14-cycle year); 2614 is rejected (2026 has 13 cycles).
`A380XNavCycle.provider = "XP"` for the DATA/STATUS serial `XP26090001`.

## 11. Service design summary (`a380x_navdata.h`)

- One `A380XNavdata` per plugin, created with roots + host callbacks; global files load lazily on the first lookup
  or explicitly (`a380x_nav_load_global`, e.g. at plugin enable). `a380x_nav_reload` for DATA/STATUS SWAP.
- Points (fixes, merged navaids, aptmeta airports) in one compact array (40 bytes/point, float positions ≈ 1 m),
  navaid details in a side array, FNV-1a hash by ident with chaining (all duplicates of an ident on one chain).
- Lookups return copies (`A380XNavFix`); duplicates are sorted nearest-first for DUPLICATE NAMES
  (`a380x_nav_find_near`).
- Per-airport CIFP parsed on demand into `A380XNavAirport` (runways, procedures → transitions → legs, all
  resolved) and kept in an LRU (default 8 airports). Returned pointers are valid until that many other airports
  have been requested — do not keep them across frames.
- Everything is freed in `a380x_nav_destroy`; heap accounting in `A380XNavStats.bytes`.

Measured on the development machine (Apple silicon, files in the OS cache, clang -O2):

| Operation | Result |
|---|---|
| Global load (fix + nav + awy + aptmeta, cycle 2609) | 65–75 ms CPU (≈0.6 s wall on a cold disk cache); 250 985 waypoints, 12 759 navaids, 15 620 airports, 91 048 airway segments, 0 malformed lines |
| Memory | 17.9 MB heap after load; 26 MB peak RSS during load; +2.1 MB for EGLL, KJFK, LFPG, EDDF CIFP |
| Ident lookup (`a380x_nav_find`, copies included) | ≈ 0.07 µs per call |
| CIFP load | EGLL 0.7 ms (54 procedures, 513 legs), KJFK 0.5 ms, LFPG 2.3 ms (253 procedures, 2 932 legs), EDDF 1.8 ms; whole world (14 943 files) 3.9 s |
| Flight plan EGLL 27R DET2F → LFPG I08L | built and recomputed in 0.5 ms |

## 12. Limitations and open points

- `earth_424.dat`, `FAACIFP18`, `user_fix.dat`/`user_nav.dat` and the Gateway `earth_nav.dat` corrections are not
  read.
- `earth_aptmeta.dat` column meanings 5 and 7 are inferred (no published spec); TA/TL decoding was checked against
  real values only.
- RNP AR detection is heuristic (§6.6); `PRDAT` (LPV/LNAV minima) is ignored.
- Airport names exist only via the host; with no host, `name` is empty.
- Runway width/surface are not in the navdata (not needed by the FMS pages; `apt.dat` would provide them).
- Airway direction restrictions and MEA/MOCA-like limits (`base`/`top`) are loaded but not enforced.
