# 05 — MFD FMS flight-plan page family (F-PLN and lateral/vertical revisions)

Status: implementation spec for the native C MFD module. It is derived from FlyByWire `aircraft` pinned at
`2baa2b35` (GPL-3.0). The port must not need the TypeScript source to implement it.

Scope: `ACTIVE/F-PLN` (and `SECn/F-PLN`), the lateral revision context menu, the INSERT NEXT WPT and NEW DEST
windows, F-PLN/DEPARTURE, F-PLN/ARRIVAL, F-PLN/AIRWAYS, F-PLN/DIRECT-TO, F-PLN/HOLD, F-PLN/VERT REV, F-PLN/FIX INFO,
the DUPLICATE NAMES dialog, `FlightPlanFooter`, and the minimum flight-plan data model and prediction feed these
pages need.

The header, title bar, MSG LIST footer, KCCU cursor, and generic widgets (Button, IconButton, DropdownMenu, InputField,
RadioButtonGroup, TopTabNavigator, ContextMenu) belong to the MFD shell and widget specs in this directory. This
document uses them and states only the properties that matter here.

Source abbreviations (all local paths are under
`upstream-aircraft/fbw-a380x/src/systems/instruments/src/`):

| Abbrev. | File |
|---|---|
| `FPLN` | `MFD/pages/FMS/F-PLN/MfdFmsFpln.tsx` |
| `FPLN.scss` | `MFD/pages/FMS/F-PLN/MfdFmsFpln.scss` |
| `REV` | `MFD/pages/FMS/F-PLN/FplnRevisionsMenu.tsx` |
| `NEXT` | `MFD/pages/FMS/F-PLN/InsertNextWptFrom.tsx` |
| `DEST` | `MFD/pages/FMS/F-PLN/DestinationWindow.tsx` |
| `DEP` / `ARR` | `MFD/pages/FMS/F-PLN/MfdFmsFplnDep.tsx` / `MfdFmsFplnArr.tsx` |
| `AWY` | `MFD/pages/FMS/F-PLN/MfdFmsFplnAirways.tsx` (+ `.scss`) |
| `DIR` | `MFD/pages/FMS/F-PLN/MfdFmsFplnDirectTo.tsx` (+ `.scss`) |
| `HOLD` | `MFD/pages/FMS/F-PLN/MfdFmsFplnHold.tsx` (+ `.scss`) |
| `VREV` | `MFD/pages/FMS/F-PLN/MfdFmsFplnVertRev.tsx` (+ `.scss`) |
| `FIX` | `MFD/pages/FMS/F-PLN/MfdFmsFplnFixInfo.tsx` (+ `.scss`) |
| `DUP` | `MFD/pages/FMS/F-PLN/MfdFmsFplnDuplicateNames.tsx` |
| `FPF` | `MFD/pages/common/FlightPlanFooter.tsx` |
| `PAGE` | `MFD/pages/common/FmsPage.tsx` |
| `W.scss` | `MFD/pages/common/widget-style.scss` |
| `DEF` | `MFD/pages/common/DataEntryFormats.tsx` |
| `UTIL` | `MFD/shared/utils.ts` |
| `FMC` | `MFD/FMC/FlightManagementComputer.ts` |

The flight-plan service lives in `fbw-a32nx/src/systems/fmgc/src/` (`@fmgc` alias). That tree is **not** in the
local checkout. It was read at the pinned commit from
`raw.githubusercontent.com/flybywiresim/aircraft/2baa2b35/...`. Those citations are prefixed `@fmgc/`, for example
`@fmgc/flightplanning/legs/FlightPlanLeg.ts`.

Each section separates three things:

- **[FBW]**: what FlyByWire implements.
- **[FBW-NA]**: what FlyByWire leaves as a placeholder, not available, or defective.
- **[PORT]**: the recommended behaviour for the X-Plane port.

---

## 0. Conventions

### 0.1 Coordinates

- Canvas: 768 × 1024 px per MFD. Origin is top-left, x to the right, y **down**, the same as CSS. All geometry is in
  canvas pixels unless a value is marked `r` (relative).
- `CONTENT_TOP` is the y of the first pixel below the page title bar (`ACTIVE/F-PLN`). `FOOTER_TOP` is the y of the
  first pixel of the shell MSG LIST footer. Both come from the shell spec. The values derived from FBW CSS and the
  EIS font metrics below are **`CONTENT_TOP = 128`** (header 96 + title bar 32) and **`FOOTER_TOP = 955`** (footer
  69). The vertical budget is `BUDGET = FOOTER_TOP − CONTENT_TOP = 827`.
- `r` = y relative to `CONTENT_TOP`. `f` = y relative to `FOOTER_TOP` (negative goes upward). Blocks that are
  top-anchored in FBW (lists, boxes) are given in `r`. Blocks that are pushed to the bottom by `flex-grow` spacers
  (button rows, the destination line) are given in `f`. If the shell constants change, keep the anchors and the
  layout still holds.

### 0.2 Box model

FBW pixel widths only add up if `box-sizing: border-box` is in effect. The F-PLN line columns sum to
25 + 11.5 + 15 + 21 + 9 + 6 + 8 % = 733.44 px. Adding the 30 px connector and its 5 px margin gives 768.44 px, which
matches the canvas width only under border-box. The DEPARTURE and ARRIVAL button rows fit only under border-box as
well. **All derived boxes in this spec assume border-box**: a CSS `width` or `height` includes padding and border.

### 0.3 Text

- Font: FBW-Display-EIS-A380 **slashed-zero** variant (`FBW-Display-EIS-A380-SlashedZero.ttf`). It is
  monospace: advance = 2568/4096 = **0.627 em** per glyph. Cap height is 0.80 em.
  `hhea` ascent/descent/gap = 3287/3/369 per 4096 em.
- Text box convention **T1**: a single text line of font size `fs` occupies a box of height `0.893·fs` (CSS
  `line-height: normal` from hhea). Its **baseline is 0.847·fs below the box top**. "Vertically centred in a box of
  height H" means `box_top + (H − 0.893·fs)/2`.
- Text width = `n_chars × 0.627 × fs`. Common sizes: 20 px → 12.54 px/char, 22 px → 13.79, 24 px → 15.05,
  25 px → 15.68, 27 px → 16.93, 30 px → 18.81.
- The port's existing atlas (`native/avionics/a380x_ewd_font.h`) is the non-slashed variant, ASCII plus `°`. The MFD
  needs a slashed-zero atlas with at least: ASCII 32–126 and `°` (U+00B0). NBSP (U+00A0) renders as a space.
- `@` in the EIS font is **not** an at-sign. It is a hollow triangle, the overfly symbol: three contours, verified
  from the TTF outlines. `"` is used as the "ditto" mark. FBW draws ditto marks in the `HoneywellMCDU` font; the port
  uses the EIS `"` glyph.
- Strings are given verbatim between backticks. `␣` is a visible space. `\xa0` is a non-breaking space.

### 0.4 Colours (from `MsfsAvionicsCommon/definitions.scss`)

| Token | Hex | Use here |
|---|---|---|
| `GREEN` | `#00ff00` | active-plan data, values |
| `YELLOW` | `#ffff00` | temporary (TMPY) plan |
| `WHITE` | `#ffffff` | labels, active leg, SEC plan data, special lines |
| `CYAN` | `#00ffff` | alternate and missed-approach legs, hover borders, pilot entry text |
| `MAGENTA` | `#ff94ff` | constraints, LAST EXIT values |
| `AMBER` | `#e68000` | missed constraint `*`, `*`-action button text, warnings |
| `DGREY` | `#868a9c` | disabled text, selected-ident background, title bar |
| `LGREY` | `#c4c6cf` | separators, box borders, button border |
| `BTNBG` | `#272525` | button and menu background |
| `UNIT` | `#0c66f7` | unit labels (`NM`, `T`, `KLB`, `FT`, `FL`, `°`, `KT`) |
| `GRID` | `#777777` | STEP ALTs grid lines |
| `CSSGREY` | `#808080` | disabled icon fill and dropdown arrow (`grey`/`gray`); DIR TO options box border |
| `BLACK` | `#000000` | background; FBW "hides" connector segments by stroking them black |

### 0.5 Widget metrics used here (summary; the widget spec is normative)

- **Button**: bg `BTNBG`, 2 px outset `LGREY` border, text 22 px `WHITE`, padding 9/12/5/12 (t/r/b/l). The right
  padding is 5 when the button has a menu that is non-empty at render time. Height = 22 px text box (19.6) + 14 + 4
  ≈ **38**; a two-line label is ≈ **57**. Disabled: text `DGREY`. A menu button shows a 15×15 down triangle at the
  right, or an up triangle if the menu opens upward. The button's dropdown menu is absolutely positioned below the
  button, `min-width` = button width, `max-height` 275 with a 20 px scrollbar. Items are 20 px, padding 5/16,
  3 px border, ≈ 34–36 px each; FBW assumes 36 (`Button.tsx:92`). Disabled items are `DGREY`. The menu opens
  **upward** if it would extend below y=1024.
- **IconButton** `double-up` / `double-down`: box 60×60 here, bg `BTNBG`, 2 px outset border. Glyph: two stacked
  filled chevrons in a 35×35 cell, polygons (0,17.5)(17.5,0)(35,17.5) and (0,35)(17.5,17.5)(35,35) for up,
  rotated 180° for down. Fill `WHITE`, or `CSSGREY` when disabled.
- **InputField**: box height 37, black bg, 2 px inset `LGREY` border, value 27 px (cyan when pilot-entered, yellow
  when the TMPY flag is set). Hover border `CYAN`. Placeholder dashes per the format.
- **DropdownMenu**: 44 px tall (40 inner + 2 px border), bg `BTNBG`, input inside, 25 px arrow cell. With free text
  allowed, typed text is submitted as `(index = −1, text)`. Typed text filters the list by prefix.
- **RadioButtonGroup**: a 30×30 circle (2 px inset `LGREY`, black fill, 4 px inner ring), 15 px gap, label 26 px.
  Rows are ≈34 px with 5 px between them. When checked, the circle fill and label take the group colour
  (cyan/yellow/amber/…). Disabled: `DGREY`. The **amber** group draws its label text amber even when unchecked
  (`W.scss:463`).
- **ContextMenu**: absolutely positioned, bg `BTNBG`, 2 px outset `LGREY`. Items are 22 px `WHITE` (`DGREY` when
  disabled), padding 7/2, 3 px transparent border (cyan on hover), one item per line, ≈ **40 px** per item. A click
  anywhere ≥ 100 ms after opening closes it. Clicking a disabled item therefore closes the menu without acting.

---

## 1. Page family, navigation and TMPY visibility

### 1.1 URIs and titles

The title bar shows `${category.toUpperCase()}/${pageTitle}`, for example `ACTIVE/F-PLN` or `SEC2/F-PLN/HOLD`
(`PAGE:122`).

| Page | URI(s) (category ∈ `active`,`sec1..3`) | pageTitle | Plan shown |
|---|---|---|---|
| F-PLN | `fms/<cat>/f-pln[/top\|/dest]` | `F-PLN` | active-or-TMPY, or SECn |
| Departure | `fms/<cat>/f-pln-departure` | `F-PLN/DEPARTURE` | same |
| Arrival | `fms/<cat>/f-pln-arrival` | `F-PLN/ARRIVAL` | same |
| Airways | `fms/<cat>/f-pln-airways` | `F-PLN/AIRWAYS` | same |
| Hold | `fms/<cat>/f-pln-hold` | `F-PLN/HOLD` | same |
| Vert rev | `fms/<cat>/f-pln-vert-rev/{rta\|spd\|cms\|alt\|step-alts}` | `F-PLN/VERT REV` | same |
| Direct to | `fms/active/f-pln-direct-to` only | `F-PLN/DIRECT-TO` | ACTIVE list, TMPY result |
| Fix info | `fms/active/f-pln-fix-info` only | `F-PLN/FIX INFO` | ACTIVE |
| Duplicate names | overlay, not a URI (`MFD.tsx:407`) | `DUPLICATE NAMES` | — |

Source: `MfdPageDirectory.tsx:104–200`, `UTIL:58–75`.

### 1.2 Loaded plan (`PAGE:188–275`)

- Category `active`: if a TMPY plan exists, load **TMPY** (index 1) with `tmpyActive = true`; otherwise load ACTIVE
  (index 0). `secActive = false`.
- Category `secN`: load plan index `2+N` (SEC1 = 3). `tmpyActive = false`, `secActive = true`. If the SEC plan is
  deleted while the page is shown, navigate to the same URI with `secN` replaced by `active`.
- Every plan also has an **alternate** sub-plan (`loadedAlternateFlightPlan`).
- The title-bar `TMPY` flag (yellow background) shows on all pages in this family when TMPY exists (`PAGE:60–76`).

### 1.3 Revised-waypoint context (FMC state, `FMC:237–241, 499–523`)

`revisedLegIndex`, `revisedLegPlanIndex`, and `revisedLegIsAltn` are set when a row's revision menu opens. The
revision pages read them. RETURN / TMPY F-PLN / CANCEL reset them to null. `revisedWaypoint()` returns the
termination fix of that leg, or none if the element is a discontinuity.

### 1.4 Entry points

- F-PLN row ident → revision menu → DEPARTURE / ARRIVAL / HOLD / AIRWAYS / CONSTRAINTS (→ VERT REV ALT) / STEP ALTs
  (→ VERT REV STEP ALTs) / FROM P.POS DIR TO (→ DIRECT-TO), or the INSERT NEXT WPT and NEW DEST windows.
- F-PLN row TIME / SPD / ALT cell → VERT REV RTA / SPD / ALT.
- F-PLN destination ident button → ARRIVAL. F-PLN `DIR TO` button → DIRECT-TO. F-PLN INFO menu → FIX INFO.

---

## 2. Minimum flight-plan data model (C)

### 2.1 What FBW uses

The leg model is `@fmgc/flightplanning/legs/FlightPlanLeg.ts:96–608` and
`@fmgc/flightplanning/legs/FlightPlanLegDefinition.ts`. Leg naming is in `FlightPlanLegNaming.ts`. Plan indices,
segments, and operations are in `FlightPlanManager.ts:32–37`, `BaseFlightPlan.ts`, `FlightPlan.ts`,
`FlightPlanService.ts`, and `FpmConfig.ts`.

- A plan is an ordered list of **elements**. An element is a **leg** or a **discontinuity** (`{isDiscontinuity:true}`).
- `allLegs` is the concatenation of segments in this order: origin, departure runway transition, SID, SID enroute
  transition, **enroute**, STAR enroute transition, STAR, STAR runway transition, approach via, approach,
  destination (only if the plan ends at a runway), missed approach (`BaseFlightPlan.ts:737–755`).
  Segment classes: origin…SID transition = `Departure`; enroute = `Enroute`; everything after = `Arrival`.
- Plan indices: `firstMissedApproachLegIndex` = count − missed legs; `lastEnrouteLegIndex`; `destinationLegIndex`
  (the last leg before missed approach whose fix is the MAP, the destination airport, or the destination runway);
  `activeLegIndex` (the TO leg, initially 1); `fromLegIndex` = last non-discontinuity before `activeLegIndex`.
- A380 FPM config (`@fmgc/flightplanning/FpmConfig.ts`, `FpmConfigs.A380`): every lateral revision and every
  constraint, overfly, insert, or delete creates a TMPY (`TMPY_ON_* = true`). Revisions on an existing TMPY are
  allowed. `MAX_NUM_LEGS = 200`. Three SEC plans. VIA compatibility is checked.
- `prepareDestructiveModification(plan)`: if `plan == ACTIVE`, ensure TMPY exists (a copy of ACTIVE) and apply the
  edit to TMPY instead. SEC plans are edited in place (`FlightPlanService.ts:322–331`).

### 2.2 C structures

These are the contract between the future native FMS core and the MFD. Names are suggestions; field semantics are
normative.

```c
/* mfd_fpln_model.h */
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define MFD_FPLN_MAX_ELEMENTS   200   /* FpmConfigs.A380.MAX_NUM_LEGS */
#define MFD_FPLN_IDENT_LEN       12   /* "EGLL27R", "(SPDLIM)", "INTCPT", "ABC/12" + NUL */
#define MFD_FPLN_ANNOT_LEN       16   /* "C090°\xa0\xa0", "10 ABC", "HOLD R", "UMBO1A" */

typedef enum {                       /* FlightPlanIndex */
    MFD_PLAN_ACTIVE = 0, MFD_PLAN_TEMPORARY = 1, MFD_PLAN_UPLINK = 2,
    MFD_PLAN_SEC1 = 3, MFD_PLAN_SEC2 = 4, MFD_PLAN_SEC3 = 5
} mfd_plan_index_t;

typedef enum {                       /* ARINC 424 path terminators */
    MFD_LEG_IF, MFD_LEG_TF, MFD_LEG_CF, MFD_LEG_DF, MFD_LEG_AF, MFD_LEG_RF, MFD_LEG_HF,   /* "XF" set incl. HF */
    MFD_LEG_CA, MFD_LEG_CD, MFD_LEG_CI, MFD_LEG_CR, MFD_LEG_FA, MFD_LEG_FC, MFD_LEG_FD,
    MFD_LEG_FM, MFD_LEG_HA, MFD_LEG_HM, MFD_LEG_PI, MFD_LEG_VA, MFD_LEG_VD, MFD_LEG_VI,
    MFD_LEG_VM, MFD_LEG_VR
} mfd_leg_type_t;
/* isXF  = IF TF CF DF AF RF HF        (FlightPlanLeg.ts:229)
   isHX  = HA HF HM ;  isXI = PI CI VI ;  isVectors = FM VM ;  isFX = FA FC FD FM */

typedef enum {                       /* segment the element lives in (order = allLegs order) */
    MFD_SEG_ORIGIN, MFD_SEG_DEP_RWY_TRANS, MFD_SEG_SID, MFD_SEG_SID_TRANS,
    MFD_SEG_ENROUTE,
    MFD_SEG_STAR_TRANS, MFD_SEG_STAR, MFD_SEG_STAR_RWY_TRANS, MFD_SEG_APPR_VIA,
    MFD_SEG_APPROACH, MFD_SEG_DESTINATION, MFD_SEG_MISSED
} mfd_segment_t;
/* class: <= SID_TRANS -> DEPARTURE, ENROUTE -> ENROUTE, >= STAR_TRANS -> ARRIVAL */

typedef enum { MFD_WPDESC_NONE, MFD_WPDESC_AIRPORT, MFD_WPDESC_RUNWAY, MFD_WPDESC_OTHER } mfd_wp_desc_t;
typedef enum { MFD_CSTR_UNKNOWN = 0, MFD_CSTR_CLB, MFD_CSTR_DES } mfd_cstr_type_t;   /* WaypointConstraintType */
typedef enum { MFD_TURN_EITHER = 0, MFD_TURN_LEFT, MFD_TURN_RIGHT } mfd_turn_t;
typedef enum { MFD_HOLD_COMPUTED = 0, MFD_HOLD_DATABASE = 1, MFD_HOLD_PILOT = 2 } mfd_hold_type_t;

typedef struct {                     /* AltitudeConstraint */
    char  desc;      /* 0 none, '@' at, '+' at/above, '-' at/below, 'B' between (alt1 high, alt2 low),
                        'C','G','H','I','J','V','X','Y' per ProcedureLeg.ts:123-175 */
    float alt1_ft, alt2_ft;          /* NAN if absent */
} mfd_alt_cstr_t;

typedef struct {                     /* SpeedConstraint */
    char  desc;      /* 0 none, '@','+','-' ; pilot entries are always '-' (BaseFlightPlan.ts:1774) */
    float speed;     /* kt, or Mach if < 2.0 */
} mfd_spd_cstr_t;

typedef struct { bool valid; float inbound_crs_mag, dist_nm, time_min; mfd_turn_t turn; mfd_hold_type_t type; }
    mfd_hold_t;                       /* HoldData; dist and time mutually exclusive (one NAN) */

typedef struct { bool valid; float to_alt_ft, dist_before_term_nm; bool ignored; } mfd_cruise_step_t;

enum {                               /* mfd_fpln_leg_t.flags */
    MFD_LEGF_OVERFLY            = 1u << 0,  /* definition.overfly */
    MFD_LEGF_ORIGIN             = 1u << 1,  /* FlightPlanLegFlags.Origin */
    MFD_LEGF_DIRTO_TP           = 1u << 2,  /* DirectToTurningPoint (the "T-P" leg) */
    MFD_LEGF_PENDING_DIRTO_TP   = 1u << 3,
    MFD_LEGF_HOLD_IMM_EXIT      = 1u << 4,  /* holdImmExit */
    MFD_LEGF_CALC_VALID         = 1u << 5,  /* 'calculated' present */
    MFD_LEGF_TOO_STEEP_AFTER    = 1u << 6,  /* calculated.endsInTooSteepPath */
    MFD_LEGF_IS_DISCONTINUITY   = 1u << 7,  /* element is a discontinuity; other fields ignored */
};

typedef struct {
    uint16_t       flags;
    mfd_leg_type_t type;                      /* effective type (an edited hold becomes HM) */
    mfd_segment_t  segment;
    mfd_wp_desc_t  wp_desc;
    char   ident[MFD_FPLN_IDENT_LEN];          /* display ident (see 2.4) */
    char   annotation[MFD_FPLN_ANNOT_LEN];    /* upper-left text (see 2.4) */
    char   procedure_ident[8];                /* definition.procedureIdent */
    double fix_lat, fix_lon;                  /* termination fix; NAN if none (CA/VA/VI/VM...) */
    float  course_deg;   bool course_true;    /* definition.course; true if magVar==null */
    mfd_turn_t turn;                          /* definition.turnDirection */
    float  length_nm, length_min;             /* definition.length / lengthTime */
    float  vertical_angle_deg;                /* definition.verticalAngle (FPA), NAN if none */
    mfd_alt_cstr_t alt_db, alt_pilot;         /* effective = pilot if set, else db unless desc in {0,'G','H'} */
    mfd_spd_cstr_t spd_db, spd_pilot;         /* effective = pilot if set, else db if desc != 0 */
    mfd_cstr_type_t cstr_type;                /* leg.constraintType */
    mfd_hold_t     hold_default, hold_modified;  /* HX legs */
    mfd_cruise_step_t cruise_step;
    float  cum_dist_with_trans_nm;            /* calculated.cumulativeDistanceWithTransitions */
    float  cum_dist_nm;                       /* calculated.cumulativeDistance (DIR TO page) */
} mfd_fpln_leg_t;

typedef struct mfd_fpln_plan {
    mfd_plan_index_t index;
    uint32_t version;                         /* incremented on every structural/constraint change */
    int  count;                               /* elements incl. discontinuities */
    mfd_fpln_leg_t el[MFD_FPLN_MAX_ELEMENTS];
    int  active_leg_index;                    /* TO leg */
    int  first_missed_appr_index;             /* == count if no missed approach */
    int  last_enroute_index;
    int  destination_leg_index;               /* -1 if none */
    char origin_icao[5], dest_icao[5], altn_icao[5];       /* "" if none */
    char origin_rwy[8], dest_rwy[8];          /* runway ident incl. ICAO, e.g. "EGLL27R"; "" if none */
    char sid[8], sid_trans[8], star[8], star_trans[8], appr_via[8];
    char approach_name[12];                   /* getApproachName(appr,false) e.g. "ILS27R" */
    float trans_alt_ft;                       /* NAN if unset */
    float trans_level_fl;                     /* FL number, NAN if unset */
    float crz_fl;                             /* performanceData.cruiseFlightLevel, NAN if unset */
    struct mfd_fpln_plan *altn;               /* alternate sub-plan (never NULL; may have count==0) */
    bool  pending_airways;                    /* airway entry in progress */
} mfd_fpln_plan_t;
```

### 2.3 Predictions (VNAV) feed

FBW sources (active plan only; TMPY and SEC show dashes):

- Per leg: `guidanceController.vnavDriver.mcduProfile.waypointPredictions.get(jointIndex)`. This is a
  `VerticalWaypointPrediction` (`@fmgc/guidance/vnav/profile/NavGeometryProfile.ts:26–40`).
- Pseudo-waypoints: `guidanceController.pseudoWaypoints.pseudoWaypoints[]` (`@fmgc/guidance/PseudoWaypoint.ts`).
- Destination: `vnavDriver.getDestinationPrediction()` and
  `guidanceController.getAlongTrackDistanceToDestination(planIndex)`.
- Hold speed: `acInterface.getLegHoldingSpeed(i, planIdx)` (only for the hold being flown, ACTIVE or TMPY) and
  `getHoldDecelReached()`.
- EFOB alarm: `fmgc.data.destEfobBelowMinInActive`. Hysteresis: set when `destEfob < minDestFob`; cleared when
  `destEfob − minDestFob > 0.3` t (`FmcAircraftInterface.ts:1692–1706`).

```c
typedef struct {
    bool  valid;
    float seconds_from_present;      /* s */
    float altitude_ft;               /* 0 or NAN = none */
    float speed;                     /* kt, or Mach if < 2 */
    bool  alt_cstr_met, spd_cstr_met;
    float efob_lb;                   /* pounds (FBW unit), NAN if none */
    float dist_from_aircraft_nm;
    uint8_t wind_kind;               /* 0 none, 1 vector, 2 tail/head component */
    float wind_dir_true_deg, wind_speed_kt;   /* kind 1 */
    float tailwind_kt;               /* kind 2: >0 tail, <=0 head */
} mfd_wpt_pred_t;

typedef struct {
    char  ident[10];                 /* A380: "(T/C)","(SPDLIM)","(S/C)","(S/D)","(T/D)","(DECEL)","(FLAP1)","(FLAP2)" */
    int   along_leg_index;           /* joint index of the leg it lies on */
    float dist_from_start_nm;        /* same reference as cum_dist_with_trans_nm */
    mfd_wpt_pred_t pred;             /* flightPlanInfo */
} mfd_pwp_t;

typedef struct {
    bool  pred_valid; float seconds_from_present; float efob_lb;
    bool  dist_valid; float dist_to_dest_nm;
    bool  efob_below_min;            /* -> amber */
    bool  hold_decel_reached; int hold_leg_index; float hold_speed_kt;   /* -1 / NAN if none */
    float estimated_takeoff_time_s;  /* ETT, seconds of day, NAN if none */
    int   flight_phase;              /* FmgcFlightPhase: 0 Preflight,1 Takeoff,2 Climb,3 Cruise,4 Descent,
                                        5 Approach,6 GoAround,7 Done */
    double zulu_seconds;             /* sim UTC seconds of day */
} mfd_fpln_global_pred_t;
```

Pseudo-waypoint idents shown on the MFD come from `@fmgc/guidance/lnav/PseudoWaypoints.ts:25–31, 385–660` and
`A380AircraftConfig.LIM_PSEUDO_WPT_LABEL = '(SPDLIM)'`. They are `(T/C)`, `(SPDLIM)` (climb and descent speed
limit), `(S/C)`, `(S/D)`, `(T/D)`, `(DECEL)`, `(FLAP1)`, and `(FLAP2)`. On the A380 no `mcduHeader` is shown above
them (it is used only when the label is `(LIM)`).

**[PORT]** The port has no VNAV yet (`AVIONICS_DEVELOPMENT.md`: "FMS MAP PARTIAL").

- Phase 1: all `pred.valid = false`. Rows then show `--:--`, `---`, and magenta constraint values (§3.5), exactly
  like FBW before predictions exist.
- Phase 2: simple predictions. Time uses ground speed along the path, EFOB uses current fuel flow, and altitude and
  speed come from the VNAV profile once it exists.

### 2.4 Ident and annotation text (`@fmgc/flightplanning/legs/FlightPlanLegNaming.ts`)

`magSuffix` = `°` if the course is magnetic, `T` if true. `CCC` = round(course) padded to 3 digits.

| Leg | ident | annotation |
|---|---|---|
| IF / DF / TF | fix ident | procedure ident (SID/STAR/approach name), or airway ident for airway legs (`fromEnrouteFix(..., airwayIdent)`); empty for DCT |
| CF | fix ident | `C` + CCC + magSuffix |
| AF | fix ident | `rho` padded to 2 (space) + `␣` + first 3 chars of the navaid |
| RF | fix ident | `length` padded to 2 + `␣ARC` |
| CA / FA / VA | round(alt1) as text | `C`/`H` + CCC + magSuffix (`H` for VA) |
| CD / FC / FD / VD | first 3 chars of the navaid (FC: fix) + `/` + length padded to 2 with `0` | `C`/`H` + CCC + magSuffix |
| CI / VI | `INTCPT` | `C`/`H` + CCC + magSuffix |
| CR / VR | navaid[0..3] + round(theta) padded to 3 | `C`/`H` + CCC + magSuffix |
| HA | round(alt1) | `HOLD␣L` / `HOLD␣R` |
| HF | fix ident | `HOLD␣L` / `HOLD␣R` |
| HM | fix ident | **F-PLN overrides**: `C` + inbound crs (modified ?? default) padded 3 + `°\xa0\xa0` (`FPLN:496–499`) |
| PI | `INTCPT` | `PROC␣L` / `PROC␣R` |
| FM / VM | `MANUAL` | `C`/`H` + CCC + magSuffix |
| runway (origin/dest) | runway ident, e.g. `EGLL27R` | procedure ident passed by the segment |
| DIR TO turning point | `T-P` | empty |
| unknown | `(UNKN <type>)` | `UNKNOWN` |

### 2.5 FMS operations the pages call (C API sketch)

Every call on `MFD_PLAN_ACTIVE` operates on TMPY, creating it if needed (A380 config). Each call increments the
plan `version`.

```c
int  fms_tmpy_insert(void);                    /* finalise pending airways, fix up pending DIR TO T-P (PPOS, track),
                                                  copy TMPY->ACTIVE incl. FIX INFOs, delete TMPY */
int  fms_tmpy_erase(void);
int  fms_delete_element(int plan, bool altn, int idx);      /* removeElementAt(idx,false); deleting a leg before an
                                                  HX leg also deletes the hold; deleting a disco between duplicates
                                                  merges them; converts the next XF leg to TF */
int  fms_next_waypoint(int plan, bool altn, int after_idx, const fix_t *f);  /* duplicate downstream => legs in
                                                  between removed; else insert TF (IF after disco) + disco */
int  fms_new_dest(int plan, bool altn, int at_idx, const char icao[5]); /* clear arrival procs, set dest, cut
                                                  enroute after idx, append disco */
int  fms_direct_to_leg(int plan, double lat, double lon, float trk_true, int target_idx, bool abeam);
int  fms_direct_to_fix(int plan, double lat, double lon, float trk_true, const fix_t *f, bool abeam);
int  fms_toggle_overfly(int plan, bool altn, int idx);
int  fms_enable_altn(int plan, int at_idx, int crz_fl);     /* crz_fl = 310 if dest-altn > 300 NM, 220 if > 150,
                                                  else 100 (FMC:1052-1072) */
int  fms_add_or_edit_hold(int plan, bool altn, int at_idx, const mfd_hold_t *desired,
                          const mfd_hold_t *modified, const mfd_hold_t *dflt);   /* returns HM leg index */
int  fms_revert_hold_to_computed(int plan, bool altn, int hm_idx);
int  fms_set_hold_imm_exit(int hm_idx, bool exit);
int  fms_airway_start(int plan, bool altn, int at_idx);
int  fms_airway_via(int plan, bool altn, const char *awy);  /* 0 ok, -1 not in DB, -2 not allowed */
int  fms_airway_to(int plan, bool altn, const fix_t *f, bool dct);   /* 0 ok, -2 no intersection */
int  fms_airway_finalise(int plan, bool altn);
int  fms_set_origin_runway / fms_set_sid / fms_set_sid_trans / fms_set_dest_runway / fms_set_approach /
     fms_set_appr_via / fms_set_star / fms_set_star_trans (int plan, bool altn, const char *id_or_NULL);
int  fms_set_pilot_alt_cstr(int plan, bool altn, int idx, bool is_des, const mfd_alt_cstr_t *c_or_NULL);
int  fms_set_pilot_spd_cstr(int plan, bool altn, int idx, bool is_des, float kt_or_0);
int  fms_set_speed_limit(int plan, bool altn, bool des, float kt, float alt_ft);  /* NAN to keep */
int  fms_delete_speed_limit(int plan, bool altn, bool des);
int  fms_add_cruise_step(int plan, int idx, float alt_ft);
int  fms_remove_cruise_step(int plan, int idx);
int  fms_set_fix_info(int n /*1..4*/, const fix_info_t *fi_or_NULL);
```

Database queries the pages need: airport/runway lists, SID/STAR/approach/VIA/transition lists with idents and
RNP-AR flags, airway search from a fix, and fix search by ident (possibly multiple results → DUPLICATE NAMES).

**[PORT]** These come from the port's navigation database (CIFP). Optionally bootstrap from X-Plane's FMS via
`XPLMNavigation.h` (`XPLMCountFMSFlightPlanEntries`, `xplm_Fpl_Pilot_Primary` / `_Temporary`). That API carries
only fixes, not procedures or leg types, so it is a fallback only.

---

## 3. F-PLN page (`FPLN`)

### 3.1 Vertical layout

FBW stacks these vertically (`FPLN:998–1289`):

1. Header row (margin-top 5, border-bottom 1).
2. Lines container: 9 rows, or 8 with TMPY.
3. `flex-grow` spacer.
4. TMPY row (72 px, shown only with TMPY).
5. `flex-grow` spacer.
6. Destination line.
7. Button row.
8. Shell footer.

FBW's DEST button style `padding: auto 15px auto 15px` is invalid CSS and is ignored, so that button is 60 px tall.

| Band | y (port, normative) | Height | Notes |
|---|---|---|---|
| Header band | r5 … r50 | 45 | EFOB/WIND button 43 tall at r5; 2 px padding below |
| Header separator | r50 | 1 | `LGREY` full width |
| Row 0 (FROM / top line) | r67 … r107 | 40 | 16 px top margin (r51…r67) |
| Row k = 1…8 | r107+72(k−1) … +72 | 72 | row 8 ends r683; with TMPY only rows 0…7 (ends r611) |
| TMPY row | centred in the gap between the last row and the destination separator | 72 | buttons bottom-aligned |
| Destination separator | f−125 | 1 | `LGREY` |
| Destination band | f−124 … f−54 | 70 | buttons 60 tall at f−119…f−59; centre line f−89 |
| Button-row separator | f−54 | 1 | `LGREY` |
| Button row | f−43 … f−5 | 38 | 5 px gap to the footer |

With the default shell constants (128 / 955), in absolute y:

- Header band 133–178, separator at 178.
- Row 0: 195–235. Rows 1–8: 235, 307, 379, 451, 523, 595, 667, 739 → 811.
- Destination separator 830; destination buttons 836–896.
- Button-row separator 901; buttons 912–950.
- TMPY row (when shown): 748–820.

Row count: `untilLegIndex = min(lineData.length, from + (tmpy ? 8 : 9))` (`FPLN:578`). With fewer lines than
slots, nothing is drawn in the remaining slots.

### 3.2 Column geometry (rows and header)

Each row is a horizontal flex box 768 wide (`FPLN:2005–2070`). Column boxes (border-box, rounded):

| Column | x0 – x1 | Content rule |
|---|---|---|
| IDENT | 0 – 192 | upper row r0–32: **annotation**, 22 px, right-aligned to x=192. Lower row r32–72: **ident box**, 2 px border (transparent, `CYAN` on hover), 30 px text starting at x=2, vertically centred (text box top = row+38.6). |
| TIME | 192 – 280 | 2 px border (cyan on hover if clickable). Upper: time annotation (22 px, white), centred at x=236, top-aligned at row+0. Lower: time text 22 px, centred at x=236, centred on y=row+52. |
| SPD | 280 – 396 | 2 px border; padding-left 40. Lower row: constraint **star cell** x 322–342 (centre 332), **value** left-aligned at x=342 (EFOB mode: value left at x=332, no star). |
| ALT | 396 – 557 | 2 px border; padding-left 20. Lower row: star cell x 417.5–437.5 (centre 428), value left at x=437.5. WIND mode: wind block right-aligned, ending at x=535 (§3.5.6). |
| CONNECTOR | 557 – 587 (+5 gap → 592) | 30-px-wide SVG drawn at x0=557, y0=row top (72 tall; 40 for row 0) — §3.7 |
| TRK | 592 – 661 | upper row only: text **left-aligned at x=594**, top-aligned at row+0 |
| DIST | 661 – 707 | upper row only: text **right-aligned at x=705** |
| FPA | 707 – 768 | upper row only: text **right-aligned at x=766** |

- Text size: TIME, SPD, ALT, TRK, DIST, and FPA are all 22 px. The constraint `*` is 30 px. A constraint value shown
  without a prediction is 25 px. The ident is 30 px; the overfly `@` triangle is 24 px, placed directly after the
  ident text, baseline-aligned.
- Row 0 (40 px tall, 16 px above it) has **no upper row**: no annotation, TRK, DIST, or FPA (`FPLN:2018, 2024,
  2035…2066`). Its lower row occupies the full 40 px (centre y = row+20).
- Text centres for a 72-px row: upper row texts top-aligned with their text box at row+0 … row+19.6. Lower-row text
  is centred on y = row+52.

Header row (`FPLN:1024–1055`, `FPLN.scss:138–189`). All labels are 20 px `WHITE`, vertically centred in r5–r50
(centre r27.5).

| Item | Geometry | Text |
|---|---|---|
| FROM | left at x=3 | `FROM` |
| TIME | centred at x=236 | `TIME` if flight phase is PREFLIGHT or DONE, else `UTC` (`FPLN:198–200`) |
| SPD/ALT button | box x 315 – 575, r5 – r48 (260×43); menu button | label: `\xa0SPD\xa0\xa0\xa0\xa0ALT\xa0\xa0\xa0` (WIND off) or `EFOB\xa0\xa0\xa0\xa0T.WIND` (on), centred in the label area (button width − 15 px arrow − 15 px label padding); 22 px; down triangle at the right edge |
| TRK | left at x=592 | `TRK` |
| DIST | right edge x=707 | `DIST` |
| FPA | right edge x=768 | `FPA` |

The SPD/ALT button menu has one item. Its label is the **other** mode: `SPD\xa0\xa0\xa0\xa0\xa0ALT` when EFOB mode is
on, `EFOB\xa0\xa0\xa0\xa0T.WIND` when it is off. Selecting it toggles `displayEfobAndWind` (`FPLN:91–96`). The
mode is per page instance; reopening the page resets it to SPD/ALT. `TRK` stays magnetic-labelled: `trueTrackEnabled`
is always false (`FPLN:89`).

### 3.3 Building the line list (`FPLN:306–564`)

The page builds `lineData[]`, a flat list of display lines. Line kinds:

- `WAYPOINT`: a real leg or a pseudo-waypoint.
- `HOLD_HM`: the `HOLD L` / `HOLD R` line.
- `SPECIAL`: `DISCONTINUITY`, `END OF F-PLN`, `NO ALTN F-PLN`, `END OF ALTN F-PLN`, or `TOO STEEP PATH`.

Each line has an `originalLegIndex`: the index into its own plan (primary, or the alternate for ALTN lines), or null.

**Empty plan** (`legCount == 0`), `FPLN:309–339`: four lines.

1. WAYPOINT `P.POS` (no leg index, no data).
2. SPECIAL `DISCONTINUITY` (leg index null → not clickable).
3. SPECIAL `END OF F-PLN`.
4. SPECIAL `NO ALTN F-PLN`.

**Non-empty plan.** The joint list `J` is the primary `allLegs` followed by the alternate `allLegs`. Index `i` runs
over `J`; `isAltn = i ≥ primary.count`.

```
derived distance/track (FPLN:349-401):
  last = 0; lastPos = (0,0)
  for i in J:
    if i == primary.count or i == primary.first_missed_appr_index: last = 0   // restart at ALTN and at MAP
    if J[i] is a leg (and i < primary.count + altn.count):
      if i == 0 or !calc_valid(J[i]):  dist[i] = trk[i] = null
      else: dist[i] = J[i].cum_dist_with_trans - last
            trk[i]  = J[i] has fix ? great-circle initial TRUE bearing(lastPos -> fix) : null
      if calc_valid(J[i]): last = J[i].cum_dist_with_trans; lastPos = fix ?? lastPos
    else dist[i] = trk[i] = null

lines (FPLN:418-563):  lastD = 0
  for i in J:
    pred = (plan==ACTIVE) ? waypointPredictions[i] : none
    if a PWP has along_leg_index == i and is displayed on the MFD (ACTIVE only):
        reduce = pwp.dist_from_start - lastD
        push WAYPOINT{ ident=pwp.ident, pseudo, annotation="", eta=ts(pwp.pred.secs),
                       alt=pwp.pred.alt, spd=pwp.pred.speed, efob=kg(pred.efob)  /* leg's EFOB, FBW quirk */,
                       wind=pwp.pred.wind, hasSpd = (ident=="(SPDLIM)"), hasAlt=false,
                       trk=trk[i], dist=reduce, fpa=null, transAlt=plan.trans_alt }
        lastD = pwp.dist_from_start
    if J[i] is a leg:
        transAlt = (i >= primary.last_enroute_index) ? trans_level*100 : trans_alt
        missed   = isAltn ? i >= altn.first_missed_appr_index : i >= primary.first_missed_appr_index
        if type == HM: push HOLD_HM{ legIdx=i, ident = turn==LEFT ? "HOLD L" : "HOLD R",
                                     holdSpeed, immExit, decelSequenced = ACTIVE && i==active+1 && decelReached,
                                     isActiveLeg = ACTIVE && i==active }
        push WAYPOINT{ legIdx = isAltn ? i-primary.count : i, ident, overfly, annotation (HM: §2.4),
                       eta=ts(pred.secs or 0), alt/spd/efob/wind from pred, constraints (effective),
                       met flags (default true), trk=trk[i], dist=(dist[i] ?? -reduce) - reduce, fpa=vertical_angle,
                       isActiveHoldFix = (HM and i==active) }
        lastD = J[i].cum_dist_with_trans ?? (lastD + dist[i] - reduce)
        if too_steep_after(J[i]): push SPECIAL "TOO STEEP PATH" (legIdx null)
    else: push SPECIAL "DISCONTINUITY" (legIdx = isAltn ? i-primary.count : i)
    if i == primary.count-1: push SPECIAL "END OF F-PLN"; if altn.count == 0: push SPECIAL "NO ALTN F-PLN"
    if altn.count > 0 and i == J.count-1: push SPECIAL "END OF ALTN F-PLN"
```

- `reduce` is reset to 0 at the start of each `i` iteration.
- `ts(s)` (prediction timestamp, `FPLN:700–715`): if the phase is ≥ TAKEOFF → `(zulu + s)·1000` ms. Otherwise, if ETT
  is set → `(ETT + s)·1000`. Otherwise `s·1000` (elapsed time, header shows `TIME`).
- **[FBW-NA]** When `pred` is missing in flight, `ts(0)` = the current UTC time, which is non-zero, so the row shows
  the **current time** instead of `--:--`. **[PORT]**: show `--:--` when `pred.valid` is false.
- **[FBW-NA]** The `HOLD_HM` line's `originalLegIndex` is not shifted for ALTN legs (`FPLN:474`). **[PORT]**: shift
  it like the waypoint line.

### 3.4 Row flags (`FPLN:594–617`)

For the line drawn in slot `s` (0-based) at list index `d`:

| Flag | Condition |
|---|---|
| `FIRST` | s == 0 |
| `LAST` | s == (tmpy ? 7 : 8) |
| `AFTER_SPECIAL` | `lineData[d−1]` is SPECIAL |
| `BEFORE_SPECIAL` | `lineData[d+1]` is SPECIAL |
| `ACTIVE_LEG` | `d == activeLegIndex`, or (WAYPOINT with `isActiveHoldFix`), or (HOLD_HM with `isActiveLeg`) |
| `BEFORE_ACTIVE` | else if `d == activeLegIndex − 1` |

**[FBW-NA]** FBW compares list indices with leg indices. They match only while no extra lines (HOLD_HM,
TOO STEEP PATH) sit before the active leg; PWPs are only ahead of the aircraft. **[PORT]**: evaluate `ACTIVE_LEG`
and `BEFORE_ACTIVE` against the line's own `originalLegIndex` in the primary plan (`== active`, `== active−1`).

### 3.5 Field formatting (WAYPOINT lines)

`plan colour` = SEC → `WHITE`, TMPY → `YELLOW`, else ACTIVE → `GREEN`.

#### 3.5.1 Time (`FPLN:1629–1640`)

- ACTIVE colour and `eta` non-zero → `HH:MM` (UTC, zero-padded).
- Otherwise `--:--`.
- TMPY and SEC always show `--:--`.

#### 3.5.2 Speed (non-EFOB mode, `FPLN:1874–1927`, ACTIVE colour only; otherwise `---`)

1. Start with `s = "---"`.
2. **Ditto** `"` (with 3 px padding each side) if all of the following hold:
   - The previous line is a WAYPOINT.
   - Both have speed predictions, and `|prev − cur| < 1` when `cur ≥ 2` (kt), or `< 0.01` when `cur < 2` (Mach).
   - This leg has no speed constraint.
   - The row is neither `FIRST` nor `AFTER_SPECIAL`.
3. Otherwise, if the prediction is finite and > 0: `cur > 2` → integer kt (`250`); else `.` + 2 decimals (`.85`).
4. Constraint present and prediction present → star cell `*` 30 px (`MAGENTA` if met, `AMBER` if missed) + `s` at
   the value position.
5. Constraint present and no prediction → only the constraint value in `MAGENTA` 25 px (kt integer or `.xx` Mach),
   at the value position.
6. Otherwise `s` at the value position.

#### 3.5.3 Altitude (non-EFOB mode, `FPLN:1794–1872`, ACTIVE colour only; otherwise `---`)

1. Start with `a = "---"`.
2. If there is an altitude prediction:
   - **Ditto** `"` if the previous line is a WAYPOINT with an altitude prediction, `|Δ| < 100 ft`, this leg has no
     altitude constraint, and the row is neither `FIRST` nor `AFTER_SPECIAL`.
   - Else if `alt ≥ transAlt` (non-null): `FL` + round(alt/100) padded to 3 (`FL350`).
   - Else round(alt, 10 ft) as an integer (`4520`).
3. Constraint present and prediction present → star cell `*` (`MAGENTA` met / `AMBER` missed) + `a`.
4. Constraint present, `alt1` set, and no prediction → `MAGENTA` 25 px text, left-aligned at **x=417.5** (the star
   cell's left edge; unlike speed, FBW adds no 20 px margin here):
   - `WINDOW` if `alt2` is set.
   - Otherwise prefix + value:
     - prefix `+` for descriptors `+`, `V`, `J`, `H`; `-` for `-`, `Y`; nothing otherwise.
     - value `alt1` as an integer if `alt1 < transAlt`, else `FL` + round(alt1/100), **not** padded.
5. Otherwise `a` at the value position.

`transAlt` for the line: for legs at index ≥ `last_enroute_index`, transition level × 100; otherwise transition
altitude. PWPs always use the transition altitude.

#### 3.5.4 EFOB (EFOB mode, SPD column, `FPLN:1929–1938`)

- ACTIVE colour and EFOB known → `(efob converted to t or klb) / 1000` with 1 decimal (`123.4`).
- Otherwise `--.-`.
- The unit follows the metric setting (`CONFIG_USING_METRIC_UNIT`: kg → t, else lb → klb), but **no unit text** is
  drawn in rows.

#### 3.5.5 SPD and ALT columns in EFOB mode

The SPD column is not clickable; the ALT (wind) column is (it routes to WIND, which is a no-op).

#### 3.5.6 Wind (EFOB mode, ALT column, `FPLN:1752–1792`)

Three cells right-aligned to x=535:

- Direction cell, 45 px centred: x 431–476.
- `/`: x 476–490.
- Speed cell, 45 px centred: x 490–535.

Rules:

- Direction:
  - `"` if the previous WAYPOINT has a wind with the same formatted direction and the row is neither `FIRST` nor
    `AFTER_SPECIAL`.
  - Else the formatted direction: `TAIL` / `HEAD` for a component (`>0` → TAIL), or `DDD°` true.
  - Else `---`.
- Speed:
  - `"` if the formatted magnitude matches the previous WAYPOINT. FBW does **not** exclude `FIRST` /
    `AFTER_SPECIAL` here; **[PORT]**: apply the same exclusion as for direction.
  - Else magnitude rounded and padded to 3 (`045`).
  - Else `--`.
- Not ACTIVE colour → the text `---°/---` placed in the same block.

#### 3.5.7 TRK / DIST / FPA (upper row, `FPLN:1651–1662`)

All three are blank on `AFTER_SPECIAL` rows and on row 0.

- `TRK`: `DDD°` (padded to 3). Blank if null **or 0** (FBW falsy test).
- `DIST`: integer NM, or blank.
- `FPA`: 1 decimal (`-3.0`), or blank if null or 0.

**[FBW-NA]** FBW's track is the **true** great-circle bearing between successive fixes, labelled `°`.
**[PORT]**: display magnetic = true − magvar at the previous fix. Append `T` when the EFIS true reference is
selected. Show `360` rather than blank for north.

#### 3.5.8 Ident (`FPLN:1616–1622`)

The ident text, followed by `@` (24 px overfly triangle) if `overfly` is set.

### 3.6 Colours of a row

`lineColour` = `CYAN` if (WAYPOINT or HOLD_HM) and (`isAltn` or `isMissedAppch`), else the plan colour
(`FPLN:1424–1433`).

| Element | Normal row | `ACTIVE_LEG` row |
|---|---|---|
| ident text | lineColour | `WHITE` if plan colour is GREEN; stays YELLOW (TMPY) or CYAN (ALTN/missed) |
| annotation | `WHITE`; YELLOW in TMPY, CYAN for ALTN/missed, WHITE for SEC | same |
| TIME / SPD / ALT text (non-constraint) | lineColour | WHITE (GREEN plans only) |
| TRK / DIST / FPA | lineColour | WHITE (GREEN plans only) |
| constraint value or star | MAGENTA / AMBER | same |
| ident **selected** (revision menu open) | text `WHITE` on `DGREY` fill of the ident box | same |

Derived from `FPLN.scss:22–43, 61–123`; the active class is overridden in TMPY and ALTN by more specific rules.

SPECIAL lines are always `WHITE` 30 px. They are centred across x 0–768 and vertically centred in the **whole**
row: centre y = row+36, or row+20 for row 0 (`FPLN.scss:45–48`, `FPLN:2072–2085`). Text: `delimiter`, then
`label` with 15 px left and right margins, then `delimiter`. `delimiter` is `- - - - -` (9 chars) when the label is longer than 13 characters, else `- - - - - -␣`
(12 chars). Resulting strings:

- `- - - - - -␣` `DISCONTINUITY` `- - - - - -␣`
- `- - - - - -␣` `END OF F-PLN` `- - - - - -␣`
- `- - - - - -␣` `NO ALTN F-PLN` `- - - - - -␣`
- `- - - - -` `END OF ALTN F-PLN` `- - - - -`
- `- - - - -` `TOO STEEP PATH` `- - - - -`

The whole line box gets a 2 px `CYAN` border on hover, even when not clickable.

HOLD_HM line:

- Ident `HOLD L` / `HOLD R` in lineColour 30 px.
- TIME column: upper `SPD` (22 px white annotation), lower = hold speed integer kt, or blank.
- SPD and ALT text are blank; TRK, DIST, and FPA are blank. The connector is variant G.

### 3.7 Connector graphics (`FPLN:1949–1982, 2107–2190`)

The SVG origin is (x=557, y=row top). Stroke width is 2, no fill. `C` = lineColour. Segments marked `hideIf(cond)`
are stroked `BLACK` (drawn invisible) when `cond` holds. The port may simply skip them.

| Variant | Selected when (first match) | Size | Primitives |
|---|---|---|---|
| A | `FIRST && BEFORE_ACTIVE` (the FROM row) | 30×40 | nothing |
| B | `FIRST` | 30×40 | line (15,40)–(15,30) `C` hideIf(BEFORE_SPECIAL); tick (8,18)–(0,18) colour = WHITE if ACTIVE_LEG and C==GREEN else C; diamond polyline (15,31)(8,19)(15,5)(22,19)(15,31) colour = WHITE if ACTIVE_LEG and C≠YELLOW else C |
| C | `AFTER_SPECIAL` | 30×72 | line (15,72)–(15,63) hideIf(LAST or BEFORE_SPECIAL); diamond (15,63)(8,51)(15,37)(22,51)(15,63); tick (8,50)–(0,50); all `C` |
| D | `ACTIVE_LEG` | 30×72 | line (15,72)–(15,62) `C` (never hidden); group colour `W = (C==GREEN ? WHITE : C)`: line (15,9)–(15,37), diamond (15,63)(8,51)(15,37)(22,51)(15,63), tick (8,50)–(0,50), stub (15,10)–(30,10) |
| E | pseudo-waypoint | 30×72 | line (15,72)–(15,50) hideIf(LAST or BEFORE_SPECIAL); line (15,0)–(15,50); tick (15,50)–(0,50); stub (15,10)–(30,10); all `C` (no diamond) |
| F | otherwise | 30×72 | line (15,72)–(15,63) hideIf(LAST or BEFORE_SPECIAL); line (15,0)–(15,37); diamond (15,63)(8,51)(15,37)(22,51)(15,63); tick (8,50)–(0,50); stub (15,10)–(30,10); all `C` |
| G | HOLD_HM line | 30×72 | line (15,72)–(15,0) `C` |

Meaning:

- The diamond (centre (15,50)) marks the waypoint.
- The left tick points at the lower-row data.
- The right stub at y=10 points at the leg's TRK / DIST / FPA in the upper row.
- The vertical line is the route.
- The active leg (from FROM to TO) is the white segment of variant D.

### 3.8 Scrolling

State: `from` = list index shown in slot 0 (`displayFplnFromLineIndex`).

| Input | Effect (FBW) |
|---|---|
| KCCU `UP` key (same side) | `from −= 1` (`FPLN:809–817`) |
| KCCU `DOWN` key | `from = min(from + 1, lineData.length)` |
| double-down button | `from = min(from + 8, lineData.length − 9)` (`scrollPage(false)`, `FPLN:974–996`) |
| double-up button | `from = max(from − 8, 0)` |
| DEST button | `from = (line index of destination leg) + 1 − (tmpy ? 8 : 9)` → destination in the last slot; `0` if not found |
| URI extra `top` | `from = max(lineIndexOf(activeLeg) − 1, 0)` |
| URI extra `dest` | as DEST button |

Clamps on every refresh (`FPLN:251–261`): `from ≥ activeLegIndex − 1` (the FROM line), and `from ≤ lineData.length`.

Button enable states (`FPLN:839–847`):

- double-up is disabled when `from ≤ activeLegIndex − 1`.
- double-down is disabled when `from ≥ lineData.length − 9`.
- DEST is disabled when there is no destination.
- Disabled icons are `CSSGREY`.

**[PORT]** Use line indices consistently. Clamp `from` to `[lineOf(active−1), max(lineOf(active−1),
lineData.length − slots)]` with `slots = tmpy ? 8 : 9`. Re-clamp when TMPY appears or disappears.

### 3.9 Click targets

| Target | Region (port) | Enabled | Action |
|---|---|---|---|
| Ident (WAYPOINT, HOLD_HM, or DISCONTINUITY with a leg index) | ident box (lower row x 0..(4+text width)); SPECIAL: the whole line | `originalLegIndex != null` | open the revision menu (§4); mark the ident selected |
| TIME | TIME column box | WAYPOINT, not ALTN, eligible¹ | set revised waypoint → `fms/<cat>/f-pln-vert-rev/rta` |
| SPD | SPD column box | WAYPOINT, eligible¹, not EFOB mode | → `.../f-pln-vert-rev/spd` |
| ALT | ALT column box | WAYPOINT, eligible¹ | EFOB mode: WIND (no-op); else → `.../f-pln-vert-rev/alt` |
| IMMEDIATE EXIT / RESUME HOLD | button (§3.10) | HOLD_HM with `decelSequenced` or `isActiveLeg` | §3.10 |

¹ eligible = `isConstraintRevisionAllowed(leg)` and `legIndex ≥ activeLegIndex`. `isConstraintRevisionAllowed`
requires: a leg (not a discontinuity), not a runway, an XF type, and not the DIR TO turning point (`UTIL:79–86`,
`VREV:281–287`).

FBW attaches the click to the lower-row text only, while the hover border covers the whole column (`FPLN:1539–1542`).
**[PORT]**: the whole column box is the click target. Pseudo-waypoint and TOO STEEP PATH lines are never clickable.

### 3.10 HOLD exit button (`FPLN:1984–2003`, `921–938`)

- Shown inside the SPD column of a HOLD_HM line when `decelSequenced || isActiveLeg`.
- Button: width 200 (x 322–522), two-line label, `AMBER` text, `*` on the right.
  - Label `IMMEDIATE` / `EXIT` when `immExit` is false; `RESUME` / `HOLD` when true.
  - FBW centres the 57-px-tall button on y=row+52, so it overlaps the next row by 9 px. **[PORT]**: keep it within
    row+24…row+81, or shrink it to 40 px.
- Click:
  - If `decelSequenced`, delete the HM leg (`deleteElementAt(i, false, plan)`); this creates a TMPY on ACTIVE.
  - Else if `isActiveLeg`, publish `fms_set_hold_immediate_exit {index: i, exit: !immExit}`, which toggles
    `holdImmExit` on the active plan.

### 3.11 Destination line (`FPLN:1090–1172`, `268–302`)

The row is a horizontal flex with `space-between`. Fixed port positions; values right-aligned so that changing
widths don't move them:

| Item | Geometry (x) | Content |
|---|---|---|
| Dest ident | button box 0–150 (vertically centred on f−89, ≈45 tall, 30 px text) | destination runway ? `ICAO + runway ident minus its first 4 chars` (e.g. `EGLL27R`) : airport ICAO. Click: reset revised waypoint → `fms/<cat>/f-pln-arrival`. With no destination: plain text `--------` (8 dashes, 20 px, GREEN if ACTIVE else WHITE), no button. |
| Time | text left at x=193, 20 px | `HH:MM` UTC of dest ETA, else `--:--` |
| EFOB | value right-aligned at x=364, 20 px; unit at x=370 | weight/1000 with 1 decimal, NaN → `---.-`; unit `T` / `KLB` in `UNIT` blue, hidden when NaN |
| Dist | value right-aligned at x=484; unit `NM` at x=490 | integer NM, else `----`; unit hidden when null |
| Scroll down | 560–620 (60×60) | double-down icon |
| Scroll up | 620–680 | double-up icon |
| DEST | button 680–763, 60 tall | `DEST`; disabled when there is no destination |

Value colours (`FPLN:1114–1150`):

- TMPY → `YELLOW`.
- ACTIVE with no value → `GREEN` (dashes).
- EFOB with `efob_below_min` → `AMBER`.
- **Otherwise `WHITE`.**

Destination predictions exist only for the ACTIVE plan.

**[FBW-NA]** Valid ACTIVE values render white; this is inverted. **[PORT]**: `GREEN` for ACTIVE values, `YELLOW`
for TMPY, `WHITE` for SEC, `AMBER` EFOB when below the minimum.

### 3.12 TMPY row and the INSERT / ERASE flow (`FPLN:1058–1087`)

- Shown only when TMPY is loaded. Row height 72; buttons bottom-aligned.
- **ERASE TMPY**: at the left, x 0–111, ≈57 tall. Label column `ERASE` / `TMPY`, 10 px gap, `*` vertically centred.
  `AMBER` text. Action `fms_tmpy_erase()`.
- **INSERT TMPY**: at the right, x 643–768. `INSERT` / `TMPY` + `*`. `AMBER`. Action `fms_tmpy_insert()`.

TMPY lifecycle on the A380:

1. Any revision of ACTIVE creates TMPY (a copy of ACTIVE) and applies the edit there. Every page in this family then
   loads TMPY. Lines turn `YELLOW`. Only 8 rows are shown. Predictions are not shown. The title bar shows `TMPY`.
2. Further revisions apply to TMPY.
3. Revision sub-pages replace `RETURN` with a yellow-text `TMPY F-PLN` button that returns to the F-PLN page.
4. INSERT does three things:
   - Finalises pending airway entries.
   - If the TMPY FROM leg is a pending DIR TO turning point, moves the T-P to the present position and current
     ground track.
   - Copies TMPY to ACTIVE (with FIX INFOs) and deletes TMPY (`FlightPlanService.ts:218–265`).
5. ERASE deletes TMPY.
6. SEC plans never get a TMPY; edits apply in place.

### 3.13 Button row (`FPLN:1173–1278`)

The row is a horizontal flex with `space-between`. Buttons are 38 tall at f−43 … f−5.

| Slot | x | Shown when | Label → action |
|---|---|---|---|
| left (125 slot) | 0–125 | SEC loaded | `RETURN` → `fms/sec/index/<N>` |
| | | else PREFLIGHT phase and ACTIVE/TMPY loaded | `INIT` → `fms/<cat>/init` |
| centre | 290–479 (menu button) | always | `F-PLN INFO` (menu below) |
| right (125 slot) | button 648–758 | not SEC | `DIR TO` → `fms/active/f-pln-direct-to` |

The F-PLN INFO menu opens **upward** (it would overflow below 1024). Items, in order:

| # | Label | Enabled | Target |
|---|---|---|---|
| 1 | `ALTERNATE` | no | `f-pln-alternate` |
| 2 | `CLOSEST AIRPORTS` | no | `f-pln-closest-airports` |
| 3 | `EQUI-TIME POINT` | no | `f-pln-equi-time-point` |
| 4 | `FIX INFO` | when not SEC | `fms/active/f-pln-fix-info` |
| 5 | `LL CROSSING` | no | `f-pln-ll-xing-time-mkr` |
| 6 | `TIME` | no | `f-pln-ll-xing-time-mkr` |
| 7 | `CPNY F-PLN REPORT` | no | — |

### 3.14 Refresh and side effects

- FBW rebuilds `lineData` and re-renders all rows when the active leg index changes, the plan version increments, or
  the list scrolls. Otherwise it refreshes predictions only. It checks every 500 ms (version) and every 2 s
  (`realTime` at 0.5 Hz) (`FPLN:240–248, 799–807`, `PAGE:176`).
- **ND PLAN centre**: after each render, FBW takes the first non-pseudo WAYPOINT line at or above slot 0 and calls
  `updateEfisPlanCentre(side, planIndex, legIndex, isAltn)`. It also sets the ND's F-PLN leg count to 7 with TMPY,
  else 8 (`FPLN:674–697`, `FMC:1550–1561`). On page exit it resets to (ACTIVE, `activeLegIndex`).
- **[PORT]**: rebuild lines on `(version, active_leg_index, from, tmpy, sec)` change. Refresh prediction text at 1 Hz.
  Publish the plan centre to the ND module.

### 3.15 F-PLN summary of [FBW-NA] items and [PORT] decisions

- Time-constraint (RTA) symbols, RNP display, and the `T` true-track suffix: not implemented in FBW. The port may add
  them later.
- Predictions for TMPY and SEC plans: not computed in FBW (dashes). The port matches this.
- The empty-plan `P.POS` line has no data; keep it.
- The revision menu is anchored at a fixed point (195,183) instead of next to the row (§4). **[PORT]**: keep the
  FBW position for fidelity.

---

## 4. Lateral revision context menu (`REV`, `FPLN:717–738`)

Opening:

- Clicking a row ident with a non-null leg index calls `setRevisedWaypoint(legIndex, loadedPlanIndex, isAltn)`.
- Menu type:
  - Discontinuity element → `DISCONTINUITY`.
  - Otherwise by the leg's segment class: `Departure` → `DEPARTURE`, `Arrival` → `ARRIVAL`, else `WAYPOINT`.
  - The types `RUNWAY`, `PSEUDO_WAYPOINT`, and `TOO_STEEP_PATH` exist in FBW but are never assigned.
- The menu does not open if it is already open.

Geometry:

- Absolute **canvas** position: top-left (195, 183). FBW passes (195,183) relative to `.mfd-main` (`FPLN:732`).
- Width = widest label (`FROM P.POS DIR TO`, 17 × 13.79 = 234.5) + 4 + 6 + 4 ≈ **249** → x 195–444.
- 15 items × 40 px + 4 = **604** → y 183–787.
- While open, the revised row's ident box is drawn selected: `DGREY` fill, `WHITE` text. Closing removes this. The
  revised-waypoint context stays set until a revision page or window resets it.

Let:

- `L` = revised element.
- `prev` = element `idx−1` of the **primary** loaded plan.
- `isFrom` = `idx == fromLegIndex`.
- `dbFix` = (`L` is a leg, `L` is XF, and `L` is not a DIR TO T-P).
- `T` = menu type.
- `P` = plan index.
- `A` = is ALTN.

| # | Label | Disabled when | Action |
|---|---|---|---|
| 1 | `FROM P.POS DIR TO` (SEC: `\xa0`, blank) | A ∨ idx ≥ firstMissedAppr ∨ P==TMPY ∨ P≥SEC1 ∨ T==DISCO ∨ isFrom ∨ ¬dbFix | `fms_direct_to_leg(P, ppos, GPS ground true track, idx, abeam=true)` → TMPY; navigate DIRECT-TO |
| 2 | `INSERT NEXT WPT` | L is a leg with FM/VM type | open the INSERT NEXT WPT window (§5) |
| 3 | `DELETE *` | (L is a disco ∧ prev is an FM/VM leg) ∨ isFrom | `fms_delete_element(P, A, idx)` → TMPY |
| 4 | `DEPARTURE` | T ≠ DEPARTURE | → `fms/<cat>/f-pln-departure` |
| 5 | `ARRIVAL` | T ≠ ARRIVAL | → `fms/<cat>/f-pln-arrival` |
| 6 | `(N/A) OFFSET` | always | — |
| 7 | `HOLD` | T==DISCO ∨ isFrom ∨ ¬dbFix | see below → `fms/<cat>/f-pln-hold` |
| 8 | `AIRWAYS` | T==DISCO ∨ isFrom ∨ ¬dbFix ∨ L is an airport or runway (`wp_desc`) | `fms_airway_start(P, A, idx)` → TMPY; → `fms/<cat>/f-pln-airways` |
| 9 | `OVERFLY *`, or `DELETE OVERFLY *` if ¬A ∧ T≠DISCO ∧ L.overfly | A ∨ T==DISCO ∨ isFrom ∨ ¬dbFix | `fms_toggle_overfly(P, A, idx)` → TMPY |
| 10 | `ENABLE ALTN *` | A ∨ L is a disco ∨ the plan has no alternate airport | `fms_enable_altn(P, idx, altnCrzFl)`, then recompute FMS data and final/alternate fuel |
| 11 | `NEW DEST` | isFrom ∨ ¬dbFix | open the NEW DEST window (§6) |
| 12 | `CONSTRAINTS` | T==DISCO ∨ ¬isConstraintRevisionAllowed(L) | → `fms/<cat>/f-pln-vert-rev/alt` |
| 13 | `(N/A) CMS` | always | (`.../vert-rev/cms`) |
| 14 | `STEP ALTs` | A ∨ ¬dbFix ∨ T==DISCO | → `fms/<cat>/f-pln-vert-rev/step-alts` |
| 15 | `(N/A) WIND` | always | (WIND CLB/CRZ/DES by segment) |

HOLD action (`REV:112–147`). If `L` is not already HX:

- `alt` = `L.alt1` if set, else the indicated altitude.
- Inbound course: if `prev` is an XF leg, the great-circle heading from prev's fix to L's fix; **else 100°** (FBW
  placeholder).
- Default hold = {computed, turn RIGHT, time = 1.0 min if alt ≤ 14000 ft else 1.5 min, no distance}.
- Call `fms_add_or_edit_hold(P, A, idx, dflt, NULL, dflt)`. This inserts an HM leg **after** idx, so
  `revisedLegIndex = idx + 1`.

If `L` is already HX, navigate without changes. The HM leg then appears as a `HOLD R` line followed by a waypoint
line with the same ident and `C<crs>°` annotation.

**[FBW-NA]**:

- `prev` and `L` are taken from the primary plan even for ALTN rows. **[PORT]**: use the plan the row belongs to.
- `FROM P.POS DIR TO` passes `abeam=true`, but the FPM ignores it.
- `RUNWAY` and `TOO STEEP PATH` revision types are never produced.

---

## 5. INSERT NEXT WPT window (`NEXT`, `FPLN.scss:324–343`)

- Dialog box at x 180–680, r55–r680 (500×625, wrapper at `CONTENT_TOP`, `left:175; top:50` + 5 px margin).
- Black background, 2 px outset `LGREY` border, padding 5.
- Content column (padding-left 10):
  1. `INSERT NEXT WPT FROM␣` (20 px white) followed by the revised ident (30 px `GREEN`).
  2. 10 px below, indented 50: fix coordinates (30 px `GREEN`).
  3. 10 px below, indented 50: DropdownMenu, 175 wide, 7 chars, free text allowed. The list holds the idents of all
     XF legs with index ≥ revised+1 in the same plan (primary or ALTN).
- CANCEL button at the bottom-left: resets the revised waypoint and closes.

Behaviour:

- Choosing a list item → `fms_next_waypoint(P, A, rev, thatLeg.fix)`. Legs between the two are removed (the
  duplicate rule).
- Typing text → `getOrCreateWaypoint(text)` → `fms_next_waypoint`. Accepted forms:
  - Place `^[A-Z0-9]{2,7}$`.
  - Runway `^[A-Z]{4}[0-9]{2}[LCR]?$`.
  - Lat/lon `^[NS]?\d{2,4}\.\d[NS]?/[EW]?\d{2,5}\.\d[EW]?$`.
  - PBD `PLACE/BRG/DIST`.
  - PBX `PLACE-BRG/PLACE-BRG`.
  - P/D is NOT YET IMPLEMENTED.
  - Anything else → `FORMAT ERROR`.
  - Not found → `NOT IN DATABASE`. Multiple matches → DUPLICATE NAMES (§14).
- Either way: close, then reset the revised waypoint.

**[FBW-NA]** Coordinates are passed as `{lat,long}` to a `{lat,lon}` formatter, which yields `NaN` for longitude
(`NEXT:137`). **[PORT]**: format `DD°MM.MN/DDD°MM.MW`, e.g. `51°28.6N/000°27.1W` (latitude degrees unpadded,
longitude padded to 3, minutes with 1 decimal).

## 6. NEW DEST window (`DEST`, `FPLN.scss:345–363`)

- Box at x 200–700, r230–r530 (500×300).
- Inner content (padding-top 40, padding-left 15): `NEW DEST FROM␣` (20 px white) + revised ident (30 px `GREEN`).
- Airport InputField 50 px below, centred: `AirportFormat`, placeholder `----`, 4 chars, cyan entry.
- `CANCEL` button at the bottom-left.

Entry handling:

- Empty or `----` → nothing.
- Length ≠ 4 → `FORMAT ERROR` with detail `FORMAT: AAAA`.
- Valid → `fms_new_dest(P, A, rev, icao)` → TMPY. This clears the arrival, approach, and VIA, sets the destination
  with no runway, removes enroute legs after rev, and appends a DISCONTINUITY. Then FMS data is recomputed.
- Any non-empty entry closes the window and resets the revised waypoint.

**[FBW-NA]** `revWpt` is tested for truthiness, so index 0 never works. **[PORT]**: test `!= null`.

---

## 7. DEPARTURE page (`DEP`)

The page edits the plan selected by `revisedLegIsAltn` (the primary or its ALTN).

### 7.1 Layout (r = below the title bar, `FPLN.scss:235–256`)

**Labelled box**: x 0–768, r20–r162, 1 px `LGREY` border, padding 15. The title `SELECTED DEPARTURE` (20 px white)
sits on the top border at x=13 over a black background strip x 11–241, r11–r29.

Row 1 (r36–r86). Label and value columns: label 20 px white at the top; value 25 px 10 px below the label box (value
text box r64–r86).

| Field | x range | Label | Value |
|---|---|---|---|
| FROM | 16–384 | `FROM` (inline, vertically centred), value starts x=81 | origin ICAO, or `----` |
| RWY | 384–507 | `RWY` | runway designator (ident without the first 4 chars, e.g. `27R`), or `---` |
| LENGTH | 507–666 | `LENGTH` | length in m or ft (by `CONFIG_USING_METRIC_UNIT`), 1-unit precision, NaN → `----`; unit `M` / `FT` (`UNIT`, 6 px gap) |
| CRS | 666–752 | `CRS` | runway bearing padded to 3, or `---`; unit `°` |

Row 2 (r96–r146):

| Field | x range | Label | Value |
|---|---|---|---|
| EOSID | 16–200 | `EOSID` | `NONE` if a runway is selected, else `------` |
| FREQ/CHAN | 200–421 | `FREQ/CHAN` | ILS frequency `xxx.xx`, or `---.--` |
| SID | 421–605 | `SID` | SID ident; else `------` if SIDs exist, else `NONE` |
| TRANS | 605–752 | `TRANS` | SID transition ident; `NONE` if the SID has no transitions; else `------` |

Value colour: `GREEN`, `YELLOW` when TMPY, `WHITE` when SEC.

**Menu buttons** at r182–r220 (38 tall):

- `RWY`: x 50–300.
- `SID`: x 400–540, disabled per §7.2.
- `TRANS`: x 590–720, disabled per §7.2.

**Bottom** (f−42 … f−2):

- `RETURN` at x 2–113, visible when not TMPY. Action: reset the revised waypoint, navigate back.
- `TMPY F-PLN` with YELLOW text at x 600–766, visible when TMPY. Action: reset, then `fms/<cat>/f-pln`.

### 7.2 Menus and actions

Every selection → TMPY (on ACTIVE).

**RWY** — all origin runways, sorted by ident. Each item label is built from these parts:

- runway designator padded to 3 with NBSP;
- one NBSP;
- length in the display unit, integer, padded to 5 with NBSP;
- `M` or `FT`;
- one space, then `ILS` if the runway has an LS.

Example: `27R\xa0\xa03902M ILS` renders as `27R  3902M ILS`. Action: set the origin runway, clear the SID, clear the
SID transition.

**SID** — `NONE`, then the available departures sorted by ident. A label gets `␣(RNP)` if RNP-AR. Action: set the SID
(or none), clear the transition.

- Enabled when a runway is selected and SIDs exist.
- Disabled when there is no runway.
- **[FBW-NA]** With a runway but zero SIDs, the previous enable state is kept. **[PORT]**: disable.

**TRANS** — `NONE`, then the SID's enroute transitions in database order.

- Enabled when the SID has transitions; disabled when there is no SID.
- **[FBW-NA]** Same stale state as SID with a SID that has no transitions. **[PORT]**: disable.

**[FBW-NA]**:

- CRS shows the **true** runway bearing (`originRunway.bearing`). **[PORT]**: magnetic.
- EOSID is display-only (`NONE`); EOSID selection is not implemented.
- FREQ/CHAN shows `lsFrequencyChannel` of the runway.

## 8. ARRIVAL page (`ARR`)

### 8.1 Layout

The labelled box is the same as DEPARTURE, titled `SELECTED ARRIVAL`, r20–r162. Five equal columns of 147.2 px start
at x = 16, 163, 310, 458, 605 (end 752).

| Row | Col 1 | Col 2 | Col 3 | Col 4 | Col 5 |
|---|---|---|---|---|---|
| 1 | `TO` + ICAO inline (or `----`) | `LS` / LS ident or `----` | `RWY` / designator or `---` | `LENGTH` / value + unit | `CRS` / bearing + `°` |
| 2 | `APPR` / approach name | `FREQ/CHAN` / `xxx.xx` or `---.--` | `VIA` / via | `STAR` / star | `TRANS` / trans |

Value rules:

- APPR:
  - `getApproachName(appr, false)` if selected.
  - Else `------` if approaches exist, else `NONE`.
- VIA:
  - The VIA ident if selected.
  - Else `------` if there is no approach or the approach has transitions; else `NONE`.
- STAR:
  - The STAR ident if selected.
  - Else `------` if STARs exist, else `NONE`.
- TRANS:
  - The transition ident if selected.
  - `NONE` if the STAR has no transitions.
  - Else `------`.

**Buttons** at r182–r220, spread with `space-between`:

| Button | x |
|---|---|
| `RWY` | 0–190 |
| `APPR` | 201–361 |
| `VIA` | 372–497 |
| `STAR` | 507–632 |
| `TRANS` | 643–768 |

The bottom RETURN / TMPY F-PLN row is as on DEPARTURE.

### 8.2 Menus and actions (all → TMPY on ACTIVE)

**RWY** — destination runways sorted by ident. Label: `<desig padded 3><sp><length padded 5><M|FT>`, without `ILS`.
**[FBW-NA]** FBW uses normal spaces, which HTML collapses. **[PORT]**: use the DEPARTURE NBSP format without `ILS`.
Action: set the destination runway, clear the approach, clear the VIA.

**APPR** — enabled when approaches exist. Items:

1. `NONE`: clear the approach and VIA.
2. Approaches, filtered and sorted:
   - Excluded: TACAN, MLS (all types), LOC back-course, and approaches without a runway (circling).
   - Sorted by runway ident, then by type order: MLS, MLS-A, MLS-BC, ILS, GLS, IGS, LOC, BAC, LDA, SDF, FMS, GPS,
     RNAV, (VORDME = VORTAC = TACAN), VOR, NDBDME, NDB, unknown (`ARR:25–46`).

Approach label (`UTIL:22–52`) = type name + runway designator + (`-` + multiple indicator) + (`␣(RNP)` if the
approach or its missed approach is RNP-AR). Type names: `ILS`, `GLS`, `IGS`, `LOC`, `BAC`, `LDA`, `SDF`, `GPS`,
`RNV` (RNAV), `VOR`, `NDB`, `RNAV` (FMS), `MLS`, `TAC`. Example: `RNV27L-Y␣(RNP)`.

Action: set the destination runway to the approach runway, set the approach, clear the VIA. When the menu opens, it
scrolls to the first approach for the currently selected runway (36 px per item).

**VIA** — enabled when an approach is selected and it has VIAs. Items:

1. `NONE`.
2. VIAs. If the selected STAR has runway transitions, only VIAs whose first fix equals the last fix of one of those
   runway transitions are listed.

A label gets `␣(RNP)` if the approach is RNP-AR.

**STAR** — enabled only when STARs exist **and an approach is selected**. Items:

1. `NONE`: clear the STAR and its transition.
2. STARs:
   - A STAR without runway transitions is always listed.
   - Otherwise it is listed only if a runway transition ident equals the approach runway ident, or if it is a
     "both" transition: 5th char `B`, first 4 chars equal.

Action: set the STAR, clear the STAR transition.

**TRANS** — `NONE`, then the STAR's enroute transitions. Enabled when the STAR has transitions.

**[FBW-NA]** CRS is the true bearing. The LS frequency is shown only for localizer-type approaches (FIXME). The same
stale-enable issue as DEPARTURE. **[PORT]**: fix as in §7.

## 9. AIRWAYS page (`AWY`)

Entering the page via the menu has already called `fms_airway_start` (pending airways on the TMPY or SEC plan).

### 9.1 Layout

- r15–r42: `AIRWAYS FROM` (20 px white) at x=15, then the revised ident (30 px `GREEN`, `YELLOW` if TMPY) at x=186.
- Airway box: x 30–738, r52–r682 (630 tall), 2 px outset `LGREY` border, padding 15.
- Line n (n = 0…10, max **11 lines**) at r84 + 52·n, 37 tall, `space-between` across x 47–721:
  - left group: `VIA` (20 px) + 5 px + field (AirwayFormat: placeholder `---`, 5 chars, centred).
  - right group: `TO` + 5 px + field (WaypointFormat: `-------`, 7 chars).
- Disabled (already accepted) fields: `BTNBG` background, value text `YELLOW` (`yellow-when-disabled`).
- Scroll buttons below the box at r687–r747, centred: down x 314–374, up 394–454. Both are **always disabled**: FBW
  defines `displayFromLine` but nothing uses it (`AWY:28–32`).
- Bottom row (f−42 … f−2):
  - `RETURN` (left, when not TMPY): if SEC, `fms_airway_finalise` first; then reset → `fms/<cat>/f-pln`.
  - `TMPY F-PLN` (right, `#ffff00`, when TMPY): `fms_airway_finalise`, reset → `fms/<cat>/f-pln`.

### 9.2 Line state machine (`AWY:199–332`)

Each line holds `fromFix` (line 0: the revised fix; line k: the TO fix of line k−1). Initially VIA and TO are both
editable.

| Event | Condition | Result |
|---|---|---|
| VIA entered | empty, or VIA disabled | rejected |
| | `DCT` | accepted; VIA locked unless this is line 0; TO enabled |
| | airway ident | search airways named X that contain `fromFix`. None → `NOT IN DATABASE`, rejected. Else `fms_airway_via(first match)`. Fail → `NOT ALLOWED` (the start fix is not on the airway). OK → VIA locked, TO enabled |
| TO entered | empty, or TO disabled | rejected |
| | VIA empty | VIA set to `DCT` |
| | VIA = airway | the fix must lie on the tail airway (ident match) else `NOT IN DATABASE`; then `fms_airway_to(fix, dct=false)` |
| | VIA = DCT | VIA locked; search all fixes. 0 → `NOT IN DATABASE`; >1 → DUPLICATE NAMES; 1 → that fix; `fms_airway_to(fix, dct=true)` |
| | result | OK → TO locked, append line k+1 with `fromFix` = this fix (while < 11 lines). Fail → `NO INTERSECTION FOUND` |

The pending entry builds TF legs for each airway segment (annotation = airway ident) or a DCT leg. They are inserted
after the revised leg at finalise (`@fmgc/flightplanning/plans/LocalPendingAirways.ts`). FBW cannot auto-compute an
airway–airway intersection from the UI because the next line only appears after TO.

**[PORT]**:

- Implement scrolling: 11 visible lines, scroll by one line.
- Also allow entering a VIA airway on the next line when the previous TO is empty, computing the automatic
  intersection (FCOM behaviour).

## 10. DIRECT-TO page (`DIR`)

### 10.1 Layout (two halves, x 0–384 and 384–768)

Left half:

- r10–r54: `DIRECT TO` (20 px) at x=0, then a DropdownMenu at x 128–303: 175 wide, 7 chars, free text,
  `YELLOW` when TMPY.
  - List = idents of the ACTIVE plan's XF legs from `activeLegIndex` up to, but not including,
    `firstMissedApproachLegIndex`.
  - The list is always built from ACTIVE, even when TMPY exists.
- r94 onward: a 2×3 grid centred in x 67–317:

  | Row | Label (right-aligned at x≈150, 20 px) | Value (30 px, right-aligned at x≈254) | Unit |
  |---|---|---|---|
  | 1 | `UTC` | `--:--` | — |
  | 2 | `DIST` | distance or `---` | `NM` (`UNIT`, x≈260) |

  Values are `GREEN`, or `YELLOW` with TMPY. Rows are ≈37 apart.

Right half:

- Box from r10 at x 384–768: 2 px solid `CSSGREY` border, padding 5/10/35/10. `OPTIONS` (20 px) at x=396, r17.
- RadioButtonGroup at x=411, from r80: `DIRECT`, `DIRECT WITH ABEAM`, `CRS IN`, `CRS OUT`, rows ≈39 apart (box
  ends ≈r268).
  - Only `DIRECT` is enabled; the others are disabled.
  - Colour `CYAN`, or `YELLOW` with TMPY. Default selection `DIRECT`.

Bottom row (f−42 … f−2, `space-between`):

- No TMPY: `RETURN` (left) → `fms/<cat>/f-pln`.
- With TMPY: left `ERASE DIR TO*` (`AMBER`), right `INSERT DIR TO*` (`AMBER`).

**[FBW-NA]** These labels are passed as the literal string `ERASE<br />DIR TO*` and render as that text. **[PORT]**:
two lines, `ERASE` / `DIR TO*` and `INSERT` / `DIR TO*`.

### 10.2 Behaviour

**List selection, index k:**

1. If TMPY exists, erase it and reset the revised waypoint.
2. Set the revised waypoint (ACTIVE, leg index).
3. `fms_direct_to_leg(ACTIVE, ppos, ADIRS true track (0 if invalid), legIndex, abeam = (option==DIRECT WITH
   ABEAM))`.

The FPM then:

- Builds a `T-P` (CF leg at PPOS with the current course, flagged DIR TO TP, and *pending* in TMPY).
- Adds a `DF` leg to the target with the same ident, keeping its constraints.
- Removes everything before it.
- Makes the DF leg active (`@fmgc/flightplanning/plans/FlightPlan.ts:170–214`).
- The direct-to target cannot be in the missed approach and must be XF or HX.

**Free text:**

1. `getOrCreateWaypoint(text)` (same formats and errors as §5).
2. `fms_direct_to_fix(...)`. If the fix exists in the plan (before the missed approach), this is equivalent to
   direct-to-leg. Otherwise T-P → DF(fix) → DISCONTINUITY → the rest. The next XF leg after the disco is converted
   to IF and non-ground-referenced legs before it are removed.
3. The dropdown shows the typed ident without a selection.

**With TMPY present:**

- The dropdown preselects the revised waypoint.
- DIST = the TMPY active leg's `cum_dist_nm`, integer.
- UTC stays `--:--` (TODO in FBW).

**INSERT DIR TO\*:**

1. Fire the X-Plane-side equivalent of `K:A32NX.FMGC_DIR_TO_TRIGGER`.
2. `fms_tmpy_insert()`.
3. Invalidate the VNAV profile.
4. Navigate to F-PLN.

**ERASE DIR TO\*:** `fms_tmpy_erase()`, then navigate to F-PLN.

**[FBW-NA]** `DIRECT WITH ABEAM`, `CRS IN`, and `CRS OUT` are disabled, and UTC is never computed.

**[PORT]** (phase 2, when the FMS supports it):

- **WITH ABEAM**: insert abeam points (ident `AB` + first 3 chars of the fix) for the bypassed fixes.
- **CRS IN**: show a course field `CRS ---°` (0–360, `InboundCourseFormat`). Build T-P → intercept → CF(fix, course).
- **CRS OUT**: build DF(fix) → FM(fix, course) → DISCONTINUITY.
- UTC = `now + dist / GS`.

## 11. HOLD page (`HOLD`)

Data: the HM leg at `revisedLegIndex` (after insertion this is the new HM leg).

- `hold = modifiedHold ?? defaultHold`.
- Title: `COMPUTED HOLD AT␣` (type computed), `MODIFIED HOLD AT␣` (pilot), or empty for a database hold.
  **[PORT]**: `DATABASE HOLD AT␣`.

### 11.1 Layout

**Left labelled box**: x 0–604, r20–≈r702.

- Title on the border at x=13: type text (20 px white) + ident (24 px; `GREEN` for ACTIVE/TMPY, else `WHITE`).
- `INBOUND CRS` label, x=16, r86. Field at x=91, r124–r161. Format: `---` + unit `°`.
- `TURN` label, r211. Radios at x=91, r249: `LEFT`, `RIGHT` (≈39 apart). Colour `CYAN`, or `YELLOW` with TMPY.
- `LEG DEFINING PARAMETER` label, r372. Radios at x=91, r410: `TIME`, `DIST`.
  - To their right, at x≈235, two stacked fields: TIME (r410–r447, `-.-` + `MN`) and DIST (r447–r484, `--.-` + `NM`).
  - Only the field of the selected parameter is visible; the other keeps its space.
- 1 px `LGREY` line across the box at r533. Below it, `LAST EXIT (FOR EXTRA FUEL 0 AT ALTN)` (20 px) at r584.
- Grid at r632, columns x 73–188 / 188–359 / 359–531:
  - Row 1 labels: `AT`, `UTC`, `EFOB`.
  - Row 2: (blank), `--:--`, `---.-` + unit `T` / `KLB`.
  - Values 25 px `MAGENTA` (ACTIVE/TMPY) or `GREEN`.

**Right column** (x 609–763):

- `DATABASE` button r45–r109, padding 20; **always disabled**.
- `COMPUTED` button r119–r183; action `fms_revert_hold_to_computed` (drops `modifiedHold`).

**Bottom**: RETURN / TMPY F-PLN as on DEPARTURE.

### 11.2 Edits (`HOLD:106–183`)

Changing the inbound course, turn, parameter, time, or distance calls `addOrEditManualHold(rev, desired, desired,
defaultHold ?? fallback{DB, 1 NM, 1 min, 0°, R}, plan, altn)` with:

- `desired = {type: PILOT, inbound, turn, time or dist}`; only the field of the selected parameter is set.

Ranges intended by the formats:

| Field | Range | Errors |
|---|---|---|
| Course | 0–360 | `FORMAT ERROR` `FORMAT: XXX °`, `ENTRY OUT OF RANGE` `RNG: 0 TO 360 °` |
| Time | 0–9.9 min | `FORMAT: X.X MN` |
| Distance | 0–99.9 NM | `FORMAT: XX.X NM` |

**[FBW-NA]**:

1. The format constructors default `max` to +∞, which overrides the class limits, so there are no upper limits in
   FBW. **[PORT]**: enforce the limits above.
2. `modifyHold` checks `revWptIdx && revPlanIdx`. ACTIVE = 0 is falsy, so edits on a hold created from the ACTIVE
   plan are silently ignored. **[PORT]**: test `!= null`.
3. Subscriptions also fire when data is loaded, which can convert a COMPUTED hold into MODIFIED just by opening the
   page. **[PORT]**: call modify **only on user edits**.
4. LAST EXIT UTC and EFOB are never computed.

## 12. VERT REV page (`VREV`)

### 12.1 Frame

- 15 px spacer, then a TopTabNavigator: margin 2, tab bar r17–r53 (36 tall, slant 30°), content border 2 outset
  `LGREY`, padding 10.
- Tab content inner area: x 14–754, from **TT = r63** down to the bottom row. The bottom row is RETURN / TMPY F-PLN
  at f−42.
- Tabs in order: `RTA`, `SPD`, `CMS`, `ALT`, `STEP ALTs`. Selected tab text `WHITE`.
- URI extra selects the tab: `rta`, `spd`, `cms`, `alt`, `step-alts`.
  - `spd` and `alt` open on `revisedLegIndex` in the revised plan (primary or ALTN).
  - `rta` and `step-alts` work on the primary plan and preselect the first available waypoint.

**Waypoint list** (`VREV:227–279`): idents of the XF legs from `activeLegIndex` to the end of the selected plan.
When the selection is no longer available, select the first entry and reset the unsaved radio and input states.

### 12.2 RTA tab (placeholder)

- `NOT IMPLEMENTED` (20 px), centred.
- 20 px below it, a centred row: `ETT` label + time field (175 wide, `TimeHHMMSSFormat`, placeholder `--:--:--`).
  The field edits the estimated takeoff time.
- Entry formats: `HHMM` or `HHMMSS`, with `:` optional. Invalid input returns no change.

### 12.3 SPD tab

Port-fixed positions relative to TT; everything is centred at x=384 unless stated.

| y | Element |
|---|---|
| TT+15 | message, 28 px `AMBER`: empty or `SPD CSTR NOT ALLOWED AT <IDENT>` |
| TT+65 … TT+109 | row: constraint type (`CLB` / `DES` / empty, 28 px `GREEN`) + `SPD CSTR AT␣` (24 px white) + waypoint DropdownMenu (175 wide, no free text) |
| TT+159 … TT+252 | constraint line (`space-between`, padding-left 10): speed field (SpeedKnotsFormat, `---` + `KT`, range **90–340**); CLB/DES radio (amber, `CLB CSTR` / `DES CSTR`, visible only when the constraint type is unknown); `DELETE` / `SPD CSTR` `*` button |
| TT+302 | 2 px `LGREY` separator |
| TT+324 | `CLB SPD LIMIT` or `DES SPD LIMIT` (24 px) at x=24 |
| TT+375 | speed-limit row (`space-between`): speed field (90–340), `AT OR BELOW` (24 px), altitude field (AltitudeOrFlightLevelFormat with TA/TL), `DELETE` / `SPD LIMIT` `*` button |

Logic (`VREV:289–383, 655–791`):

- **Not eligible** (§3.9 rule): show the message; disable the speed field; clear the radio and input.
- **Constraint type** = `leg.cstr_type`, or, if unknown, from the segment: departure segments → CLB, arrival and
  approach segments → DES, enroute → unknown.
  - Known type → the radio is hidden and preselected, and the field shows the leg's effective speed constraint.
  - Unknown → the pilot must pick CLB CSTR or DES CSTR.
- **Speed entry** (requires a radio selection) → `fms_set_pilot_spd_cstr(P, A, idx, is_des, kt)`. The descriptor is
  always `-` (at or below). Creates TMPY on ACTIVE.
- **DELETE SPD CSTR** (enabled when the leg has a speed constraint) → set with 0, which clears both the database and
  pilot speed constraints.
- **Speed limit type**: DES if the constraint type is DES, or if (the plan is ACTIVE or copied from active, and the
  phase is > CRUISE and < GO AROUND). Otherwise CLB.
  - Values: the plan's (alternate-) climb or descent speed-limit speed and altitude.
  - The pilot-entered flag gives large-font values; computed values are small.
  - Clearing either field, or pressing DELETE, deletes the limit. DELETE is enabled when both values exist.

### 12.4 ALT tab

| y | Element |
|---|---|
| TT+15 | message `AMBER` 28 px: `ALT CSTR NOT ALLOWED AT <IDENT>`, `TOO STEEP PATH AHEAD` (leg or previous leg ends in a too-steep path), or empty |
| TT+65 … TT+109 | type + `ALT CSTR AT␣` + waypoint dropdown |
| TT+159 … ≈TT+300 | constraint line, see below |

Constraint line, three blocks:

1. **Left** (right border 2 px `LGREY`, padding-right 25, padding-bottom 20):
   - Radio `AT`, `AT OR ABOVE`, `AT OR BELOW`. Colour `CYAN`, or `YELLOW` with TMPY. All disabled when not eligible.
   - Below it, 20 px down, `WINDOW` (24 px), visible only for a `B` constraint.
2. **Centre** (bottom-aligned, padding-bottom 20):
   - Altitude field (AltitudeOrFlightLevelFormat), **invisible until an option is selected**.
   - 20 px below it, the window value: `FT` / `FL` lead unit + `GREEN` 24 px `aaa-bbb` + trail unit, visible only
     for `B`.
3. **Right** (margin right 20, bottom 20): `DELETE` / `ALT CSTR` `*` button; 20 px below, the CLB/DES amber radio
   (only when the type is unknown).

2 px `LGREY` separator under the line.

Logic:

- The radio preselects from the effective descriptor: `@`, `I`, `X` → AT; `+`, `J`, `V` → AT OR ABOVE; `-`, `Y` →
  AT OR BELOW; otherwise none. The field shows `alt1`.
- Entry needs a CLB/DES selection and an option. It calls `fms_set_pilot_alt_cstr(P, A, idx, is_des, {alt1, @|+|-})`.
  The plan updates the first DES or last CLB constraint marker.
- DELETE is enabled when an altitude constraint with a descriptor exists. It clears the database and pilot altitude
  constraints.
- **Window text**: `alt2` (lower) then `alt1`, each shown as FL/100 when above the transition (CLB: `> TA`, DES:
  `≥ TL×100`), padded to 3. Lead unit `FT` only when alt1 is FL and alt2 is not; trail unit `FL` if alt1 is FL, else
  `FT`.

**AltitudeOrFlightLevelFormat** (`DEF:169–232`):

- Placeholder `-----` + `FT`.
- Input ≤ 3 characters means a flight level (×100).
- Range 0–43000. Errors: `FORMAT ERROR` with `FORMAT: FOR ALT XXXXX FOR FL XXX`; `ENTRY OUT OF RANGE` (no detail).
- Display: `FL` + 3 digits when above the TA (`>`), or at or above the TL (DES).

### 12.5 CMS tab

`NOT IMPLEMENTED`, centred. The revision menu item is `(N/A) CMS` and disabled.

### 12.6 STEP ALTs tab (`VREV:490–614, 805–933, 1312–1480`)

**Access check, in order.** The first match shows its message, centred in the tab content (24 px `AMBER`); otherwise
the editor is shown.

| Condition | Message |
|---|---|
| Engine out | `STEP ALTs NOT ALLOWED IN EO` |
| Plan is ACTIVE or copied from active, phase DESCENT | `STEP ALTs NOT ALLOWED IN DES` |
| … phase APPROACH | `STEP ALTs NOT ALLOWED IN APPR` |
| … phase GO AROUND | `STEP ALTs NOT ALLOWED IN GA` |
| … phase DONE | `STEP ALTs NOT ALLOWED IN DONE` |
| No cruise FL | `STEP ALTs NOT ALLOWED: NO CRZ FL` |

**Editor layout:**

- Labelled box (x 14–754, from TT+20). Title at x≈40: `STEP ALTs FROM CRZ` + 15 px + `FL` (`UNIT`) + crz FL value
  (25 px `GREEN`, `YELLOW` TMPY, `WHITE` SEC, `---` if none).
- Grid at x 30–738: column widths 248 / 177 / 142 / 142. Rows are 60 px.
- Header row (TT+36 … TT+96): `WPT` (padding-left 60), `ALT` (padding-left 25), `DIST`, and `TIME` or `UTC` (TIME
  in PREFLIGHT). Labels are bottom-aligned with padding-bottom 10.
  - A 1 px `GRID` line runs under the header across all columns.
  - A 1 px `GRID` vertical line is on the right edge of the ALT column, for all rows.
- 5 step rows (TT+96 + 60k):
  - waypoint DropdownMenu (175 wide, centred in col 1);
  - altitude field (150 wide, centred in col 2, AltitudeOrFlightLevelFormat with TA);
  - then either a message spanning cols 3–4 (`IGNORED`, or `ABOVE MAX FL` when above the recommended max at the
    estimated gross weight), 20 px white, centred;
  - or DIST (`GREEN` value + `NM`, right-aligned in col 3) and TIME (`HH:MM`, centred in col 4).
  - DIST and TIME are visible only when a waypoint and altitude are set. DIST = `dist_from_aircraft − 0`
    (step at the fix). TIME uses the ETA rule of §3.3.
- Rows show existing steps from `startIndex`. The first row after the last step is shown empty for a new entry;
  later rows are hidden.
- Below the grid: `STEP NOT ALLOWED AT <IDENT>` (28 px `AMBER`) when the last insertion was rejected.
- Scroll buttons, centred: down (disabled when `steps − start ≤ 4`) and up (disabled when `start == 0`); each moves
  by one.
- Labelled box `OPTIMUM STEP POINT` (margin-top 25): `TO␣` + `FL` + `---`, and `NO OPTIMUM STEP FOUND`. This is
  static (**[FBW-NA]**).

**Insertion rules** (`checkStepInsertionRules`, `VREV:805–852`):

- Walk the existing steps up to the insertion point; the starting altitude is crz FL×100.
- The step size must be ≥ 1000 ft.
- No step climb after a step descent.
- The next step after the insertion must also be ≥ 1000 ft away and must not be a climb after a descent.
- Valid → `fms_add_cruise_step(P, legIdx, alt)`. All steps are un-ignored.
- Invalid → `STEP NOT ALLOWED AT <ident of the selected leg>`. **[FBW-NA]** It should be the step's leg.
- Above the max FL → message `STEP ABOVE MAX FL`, but the step is still inserted if valid.

**Editing:**

- Changing a row's waypoint or altitude deletes the old step and adds the new one.
- Clearing either field removes the step.

## 13. FIX INFO page (`FIX`)

FIX INFO is always for the ACTIVE plan (`ALLOW_NON_ACTIVE_FIX_INFOS=false`). It is disabled from F-PLN INFO when a
SEC plan is shown. 1 rem = 16 px is assumed (MSFS default).

### 13.1 Layout

- r14: TopTabNavigator with tabs `FIX 1` … `FIX 4`. Bar r16–r52; content inner x 14–754 from r62; content max
  height 720.
- `REF IDENT` (20 px) at x=52, r86. Then, 19 px to the right, a field: FixFormat, placeholder `-------`, 7 chars.
  - Accepts place (2–7 alnum) or runway format, else `FORMAT ERROR` `FORMAT: XXXXX`.
  - Not found → `NOT IN DATABASE`.
  - Clearing deletes the entry.
- Table at x 14–739, from r167. Left column 22% (x 14–174) with a 2 px `DGREY` right border. Rows are separated by
  2 px `DGREY` lines.

| Row | Height | Left cell | Right cell |
|---|---|---|---|
| 1 | 88 | — | header block centred: `F-PLN INTERCEPT` (spans 3) over `UTC` `DIST` `ALT` (20 px) |
| 2 | 184 | `RADIAL` (20 px, x≈37) + 2 radial fields (`---` + `°`, 0–360; the 2nd disabled until the 1st is set; width 106) | 2 prediction lines (see below) |
| 3 | 112 | `RADIUS` + radius field (`----` + `NM`, **1–9999**) | (empty) |
| 4 | 184 | `ABEAM` button (disabled, width 123) | — |

Prediction line (each): ETA `--:--` (30 px), DIST `----` (unit `NM` hidden while NaN), ALT `---` (unit `FL` hidden),
and an `INSERT` / `AS WPT*` button that is **invisible and disabled**. Value colour `GREEN`, or `YELLOW` with TMPY.

**Bottom**: `FlightPlanFooter` (§15).

### 13.2 Behaviour

- **[FBW]** Entries are stored per plan: `FixInfoEntry {fix, radials[≤2]{magBrg, trueBrg}, radii[≤1]{radius}}`.
  `trueBrg = mag + magvar(fix)`. The ND draws them.
- **[FBW-NA]** Intercept predictions are never computed. ABEAM and INSERT AS WPT are not implemented. FBW writes to
  `loadedFlightPlanIndex` (TMPY if present) but reads ACTIVE. **[PORT]**: read and write ACTIVE; TMPY INSERT copies
  FIX INFOs anyway.
- **[PORT]** (phase 2): compute the first intercept of the ACTIVE path with each radial or circle: UTC, along-track
  DIST from PPOS, predicted ALT (as FL when above TA).

## 14. DUPLICATE NAMES dialog (`DUP`)

Opened by `deduplicateFacilities(items)`, which returns a promise resolved with the chosen item or `undefined` on
RETURN. The caller treats `undefined` as "not in database / cancelled".

### 14.1 Layout

- Overlay at x 0–768, y `MFD_HDR_H` … `MFD_HDR_H`+852 (default 96–948). Black, above everything except the cursor.
- Its own title bar `DUPLICATE NAMES` (EO flag possible).
- Table header, 100 px below the title bar (≈y +133 … +171 in the overlay). Columns 20/20/30/30 %: `IDENT`,
  `DIST(NM)`, `LAT/LONG`, `FREQ/CHAN` (20 px white, centred).
- Outline box (2 px `DGREY`, height 511) with up to **10 rows**, ≈51 px each. Cells are centred, values 30 px
  `GREEN`:
  - ident;
  - integer distance from PPOS (blank if unknown);
  - `DDN/DDDE`, using **rounded whole degrees**;
  - freq: `>120` → integer, else 2 decimals; blank for fixes.
  - Rows get a cyan 2 px border on hover.
- Scroll buttons centred below the box (down, then up, 20 px apart).
  - Down is enabled when `count > from + 10`; up is enabled when `from > 0`.
  - Each moves by one row.
- `RETURN` at the bottom-left.

### 14.2 Behaviour

- Items are sorted by ascending distance from PPOS. For ILS navaids the localizer position is used.
- Clicking a row resolves with that item and closes. RETURN resolves `undefined`. A new request rejects the pending
  one.

**[PORT]** Lat/long as `DD°MMN/DDD°MME` (whole minutes) to disambiguate nearby duplicates.

## 15. FlightPlanFooter (`FPF`)

Used by FIX INFO. The row is `space-between` at f−42 … f−2, with two padded (2 px) slots that always keep their
space:

- **Left**: `RETURN`, visible when no TMPY exists. Action: reset the revised waypoint → navigate `back`.
- **Right**: `TMPY F-PLN` with `YELLOW` text, visible when TMPY exists. Action: reset → `fms/<cat>/f-pln`.

TMPY existence comes from the flight-plan manager (any TMPY, regardless of the page's loaded plan).

---

## 16. Messages and formats used by this family

FMS messages appear in the shell footer message area; their text comes from `NXSystemMessages.ts`.

| Trigger | Message (+ detail line) |
|---|---|
| Unknown ident (entry) | `NOT IN DATABASE` |
| Bad format | `FORMAT ERROR` + `FORMAT: <fmt>[ <unit>]` |
| Out of range | `ENTRY OUT OF RANGE` + `RNG: <min> TO <max>[ <unit>]` |
| Airway start fix not on airway | `NOT ALLOWED` |
| Airway TO fails | `NO INTERSECTION FOUND` |
| Step above REC MAX | `STEP ABOVE MAX FL` (type II) |
| P/D place-distance entry | `NOT YET IMPLEMENTED` (fictional) |

Formats (`DEF`). Placeholders render in the field's colour.

| Format | Placeholder | Max chars | Range | Unit |
|---|---|---|---|---|
| AirportFormat | `----` | 4 | exactly 4 chars | — |
| AirwayFormat | `---` | 5 | any | — |
| WaypointFormat | `-------` | 7 | any | — |
| FixFormat | `-------` | 7 | place/runway | — |
| SpeedKnotsFormat (VREV) | `---` | 3 | 90–340 | `KT` |
| AltitudeOrFlightLevelFormat | `-----` | 5 | 0–43000, ≤3 chars = FL | `FT` / `FL` |
| InboundCourseFormat | `---` | 4 | 0–360 (port) | `°` |
| HoldTimeFormat | `-.-` | 3 | 0–9.9 (port) | `MN` |
| HoldDistFormat | `--.-` | 4 | 0–99.9 (port) | `NM` |
| RadialFormat | `---` | 4 | 0–360 | `°` |
| RadiusFormat | `----` | 4 | 1–9999 | `NM` |
| TimeHHMMSSFormat | `--:--:--` | 6 | 00:00:00–23:59:59 | — |
| Dropdown field (n chars) | n × `-` | n | free text | — |

---

## 17. Implementation checklist and acceptance tests

1. **Model**: `mfd_fpln_plan_t` for ACTIVE, TMPY, and SEC1–3, each with its ALTN sub-plan. The `version` counter
   drives rebuilds.
2. **Line builder** (§3.3) is pure C. Unit-test it against these cases:
   - Empty plan → `P.POS`, DISCONTINUITY, END OF F-PLN, NO ALTN F-PLN.
   - Plan with SID + DCT + STAR + approach + missed approach. Missed-approach rows are CYAN, and DIST restarts at
     the first missed-approach leg.
   - An HM hold → `HOLD R` line + fix line with `C090°` annotation.
   - Pseudo-waypoints split the DIST of the leg they lie on.
   - `TOO STEEP PATH` follows the flagged leg.
   - ALTN legs come after `END OF F-PLN` and end with `END OF ALTN F-PLN`.
3. **Renderer**:
   - Column boxes (§3.2), connector variants A–G (§3.7), ditto rules, colours for ACTIVE, TMPY, SEC, ALTN, and the
     active leg.
   - Golden image: 9 rows with a white active leg and a FROM row without a connector.
4. **Interaction**:
   - Hit-test list per page (§3.9, §4–§15).
   - The revision menu items' enable matrix (§4) for FROM leg, disco, departure leg, arrival leg, ALTN leg, SEC,
     and TMPY.
5. **TMPY flow**:
   - Any revision on ACTIVE → TMPY. 8 rows, yellow, ERASE / INSERT row, and `TMPY F-PLN` on sub-pages.
   - INSERT copies to ACTIVE; ERASE discards.
6. **Bug fixes vs FBW** (all marked [PORT]):
   - Index-0 / plan-0 falsy checks (§6, §11).
   - Hold format limits.
   - Hold auto-modify on load.
   - True-vs-magnetic track and CRS.
   - Current time shown instead of `--:--`.
   - Destination-line colours.
   - Literal `<br />` labels.
   - Coordinate NaN.
   - ALTN revision context.
   - FIX INFO plan index.
