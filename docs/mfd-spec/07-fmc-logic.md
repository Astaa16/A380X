# 07 — FMC logic and data flows behind the MFD

Implementation spec for the native C FMS core behind both MFDs. It covers FMS state, the side effects of every
pilot entry, weight/fuel predictions, speed computation, altitude defaults, messages, FMS1/FMS2 handling, every
output FBW writes for other systems, and a mapping onto the existing X-Plane port interfaces.

A developer should be able to implement the FMS core from this file without reading any TypeScript.

---

## 0. Conventions, sources and tags

### 0.1 Source pin and path abbreviations

All upstream facts come from FlyByWire `aircraft` at commit `2baa2b35` (GPL-3.0).

| Abbrev. | Path |
|---|---|
| `FMC/` | `upstream-aircraft/fbw-a380x/src/systems/instruments/src/MFD/FMC/` (`FlightManagementComputer.ts` = **FMC.ts**, `FmcAircraftInterface.ts` = **FAI.ts**, `fmgc.ts`, `FmcService.ts`, `FmcInterface.ts`, `DummyFlightManagementComputer.ts`) |
| `MFD/` | `upstream-aircraft/fbw-a380x/src/systems/instruments/src/MFD/` |
| `SH/` | `upstream-aircraft/fbw-a380x/src/systems/shared/src/` (`OperatingSpeeds.tsx`, `PerformanceConstants.ts`, `OperatingAltitudes.tsx`, `flightphase.ts`) |
| `FMGC/` | `fbw-a32nx/src/systems/fmgc/src/` at `2baa2b35` |
| `PORT/` | `xplane-port/native/avionics/` |

**Important: the `@fmgc/*` alias is not in the local checkout.** `fbw-a380x/src/systems/tsconfig.json:41` maps
`@fmgc/*` to `../../../fbw-a32nx/src/systems/fmgc/src/*`, and `fbw-a32nx/` does not exist under
`upstream-aircraft/`. `fbw-common/src/systems/fmgc` contains only `constraint.ts`.

For this spec the following files were read at the same pin from `raw.githubusercontent.com` (read-only; nothing was
copied into the port):

- `FMGC/flightphase/FlightPhaseManager.ts`, `FMGC/flightphase/Phase.ts`
- `FMGC/flightplanning/plans/performance/A380FlightPlanPerformanceData.ts`
- `FMGC/flightplanning/plans/performance/FlightPlanPerformanceData.ts`
- `FMGC/flightplanning/plans/FlightPlan.ts`, `FMGC/flightplanning/plans/BaseFlightPlan.ts`
- `FMGC/flightplanning/FlightPlanService.ts`, `FMGC/flightplanning/FlightPlanManager.ts`
- `FMGC/flightplanning/FpmConfig.ts`, `FMGC/flightplanning/A380AircraftConfig.ts`
- `FMGC/flightplanning/segments/OriginSegment.ts`, `FMGC/FmsError.ts`, `FMGC/guidance/vnav/common.ts`

LNAV/VNAV internals (GuidanceController, VnavDriver, predictions) are out of scope. This file only defines their
interfaces to the FMC.

### 0.2 Tags

- **[FBW]**: implemented upstream and reproduced as stated.
- **[FBW-PH]**: placeholder, simplified, disabled or not available upstream.
- **[PORT]**: recommended behaviour for the X-Plane port. Where it deviates from FBW, the reason is given.

### 0.3 Units and nulls

- FBW stores ZFW, BLOCK, TAXI, RTE RSV, ALTN, FINAL and MIN DEST FOB in **tonnes** in the plan performance store.
  It computes TOW, LW, TRIP and EXTRA in **kg**. `getFOB()` returns **tonnes**.
- **[PORT]** The core stores every mass in **kg** (`double`). Conversion to t/klb happens only in the MFD formatter
  (`CONFIG_USING_METRIC_UNIT` → `T`, else `KLB`; `MfdFmsFuelLoad.tsx:59-61`).
- **[PORT]** FBW `null` becomes `NAN` for `double` (test with `isnan`), `-1` for enums and `0` for booleans. Every
  published output carries an explicit `valid` flag. Dashes are never published as zero.
- Speeds are in kt CAS, altitudes in ft, flight levels in hundreds of ft, temperatures in °C and CG in %MAC.

### 0.4 Geometry

This file has no drawing geometry. The only visual facts here are the message footer background colours (§7.1).
All MFD specs in this folder use the 768 × 1024 px canvas with the origin at the top left and y pointing down.

---

## 1. Architecture

### 1.1 FBW object graph [FBW]

- `FmcService` (`FmcService.ts:22-119`) creates **one real FMC (FMC-A, Master)** plus two
  `DummyFlightManagementComputer`s for FMC-B and FMC-C. The dummies only publish their health L:var
  (`DummyFlightManagementComputer.ts:15-23`). `slave` and `standby` are `null`.
- **One `FmcService` instance is shared by both MFDs** (`MFD/instrument.tsx:88-132`; both `<MfdComponent>` get the
  same `fmcService`).
- `fmcService.master.mfdReference = this.mfdCaptRef.instance` (`instrument.tsx:139-140`). As a result, FMC-initiated
  page navigation (DONE → DATA/STATUS, engine-out → PERF) only affects the **captain's** MFD.
- `FlightManagementComputer` owns these components:
  - `FlightPlanService` (plans + per-plan performance data)
  - `FlightPhaseManager`
  - `FmgcDataService` (`fmgc`, non-plan data in `fmgc.data`)
  - `FmcAircraftInterface` (`acInterface`, all outputs)
  - `GuidanceController` (LNAV/VNAV/predictions)
  - `Navigation` (position, navaid tuner)
  - `EfisInterface` L/R, `EfisSymbols` L/R
  - `DataManager` (pilot waypoints)
  - the message queue `fmsErrors`

### 1.2 Cadence [FBW]

| Task | Period | Source |
|---|---|---|
| `onUpdate(dt)` | 100 ms `setInterval` | FMC.ts:441-449 |
| Flight phase evaluation (`shouldActivateNextPhase`) | every `onUpdate` | FMC.ts:1465 |
| Heavy block (perf/speeds/weights/checks) | throttled to `FMS_CYCLE_TIME` = 250 ms | FMC.ts:83, 1467-1522 |
| Guidance update, EFIS symbols, ARINC writes (dirty only) | every `onUpdate` | FMC.ts:1542-1547 |
| ENTER DEST DATA check in cruise | 15 s | FMC.ts:452-460 |
| MFD page data poll (`checkIfNewData`, runs `onNewDataChecks` + `onNewData`) | 500 ms | `pages/common/FmsPage.tsx:176-186` |
| FUEL&LOAD computations | 1 Hz, **only while that page is displayed** | `MfdFmsFuelLoad.tsx:218-298` |
| PERF `drawPage` (REC MAX/OPT FL, speed tables) | 1 Hz, only while displayed | `MfdFmsPerf.tsx:932-940` |

### 1.3 `onUpdate` pipeline, exact order [FBW] (FMC.ts:1443-1548)

1. **Health.** `allFmcResetsPulled = RESET_PANEL_FMC_A && RESET_PANEL_FMC_B && RESET_PANEL_FMC_B`. This is an FBW
   typo; C is never checked. `allFmcInop = !FMC_A_IS_HEALTHY && !B && !C`. Then set
   `legacyFmsIsHealthy = !allFmcInop`.
   - If `allFmcResetsPulled || allFmcInop`, call `reset()` once (latched by `wasReset`) and return.
2. Clear `wasReset`. Run `flightPhaseManager.shouldActivateNextPhase(dt)`.
3. If the 250 ms throttle elapsed:
   1. `navigation.update`, then `loadActiveFlightPlanFuelAndApproachData()` (mirrors active-plan perf values into
      FMC subjects; §2.6).
   2. If an active plan exists, run steps a–u in this order:
      - a. `enginesWereStarted = phase ≥ TAKEOFF || (phase == PREFLIGHT && any of N2[1..4] > 20 %)`
      - b. `sfccAquisition()` (flap lever 0..4)
      - c. `updateThrustReductionAcceleration()`
      - d. `updateTransitionAltitudeLevel()`
      - e. `updatePerformanceData()` (V1/VR/V2 vars)
      - f. `updatePerfSpeeds()`
      - g. `updateWeights()`
      - h. `toSpeedsChecks()`
      - i. `checkForCruiseStep()`
      - j. `checkTooSteepPath()`
      - k. `checkDestEfobBelowMin()`
      - l. `checkDestEfobBelowMinScratchPadMessage(dt)`
      - m. `checkEngineOut()`
      - n. `checkLateralDiscontinuityAhead()`
      - o. `setTakeoffFlaps(takeoffFlaps)`
      - p. `setTakeoffTrim(takeoffThsFor)` if set
      - q. `updateDestinationPredictions(destPred)`
      - r. `updateIlsCourse()`
      - s. `updatePerfSpeeds()` again
      - t. `checkCruiseLevelChangeDueToFcu(dt)`
      - u. `updateManagedSpeed()`, then `updateConstraints()`
   3. Otherwise (no active plan), run `resetDestinationPredictions()`.
   4. Always run `checkZfwParams()`, `updateMessageQueue()`, `updateVerticalPath()`, `checkSpeedLimit()` and
      `thrustReductionAccelerationChecks()`.
4. If `activeOrTemporary.version` changed, run `updateManagedProfile()` and `updateDestinationData()`.
   - If the captain MFD is not on F-PLN, also re-centre the EFIS plan on the active leg.
5. Run `guidanceController.update(dt)`, update the EFIS symbols on both sides, and write the dirty ARINC words.

### 1.4 [PORT] Module structure

- `a380x_fms_core.h/.c`: simulator-independent, no XPLM includes. Same rule as `a380x_avionics_core.c`.
  - Holds `A380XFmsState`: plans, perf stores, FmgcData, phase, message queue.
  - Exposes `a380x_fms_update(state, const A380XFmsInputs*, double dt)` and one entry function per pilot action
    (§3). Each entry function returns `A380X_FMS_OK` or an error code that maps to a message (§7).
- `a380x_fms_speeds.c`: tables and the `A380OperatingSpeeds` algorithm (§5). Pure functions with unit tests.
- `a380x_fms_adapter.inc`: XPLM boundary. Included in `a380x_avionics.c` **after** `a380x_fcu_adapter.inc`
  because it needs `guidance`, `guidance_inputs` and `pfd_aircraft`. It reads raw datarefs into
  `A380XFmsInputs`, registers `a380x/xp12/fms/*` outputs and copies outputs into existing port structs (§10).
- **Call order in `flight_loop`** (`a380x_avionics.c:252-296`):
  1. Run `fms_update(dt)` before `update_guidance(dt)`, using the previous frame's guidance state (FCU selected
     altitude, active modes).
  2. Set `phase_inputs.configured_v1` from the FMS (replacing the hard-coded 145.0 at line 281) **before**
     `a380x_flight_phase_update`.
  3. `read_guidance_inputs()` (`a380x_fcu_adapter.inc:152-209`) must copy the FMS managed targets into
     `guidance_inputs.managed_*` **after** its `memset` at line 158.
- Run the core at 10 Hz by accumulating `dt` (100 ms steps). Run the heavy block every 250 ms, as in FBW.
  - **[PORT] deviation:** also run every page-hosted FBW calculation in the core. This covers takeoff weight,
    ALTN/FINAL fuel, V-speed runway-change logic and CRZ TEMP ISA default. FBW only runs these while a page is
    open, which leaves TOW/LW stale (see §11).

---

## 2. FMS state

### 2.1 FMGC flight phase

`FmgcFlightPhase` (`SH/flightphase.ts:4-13`):

| Value | Name |
|---|---|
| 0 | PREFLIGHT |
| 1 | TAKEOFF |
| 2 | CLIMB |
| 3 | CRUISE |
| 4 | DESCENT |
| 5 | APPROACH |
| 6 | GOAROUND |
| 7 | DONE |

Each change writes these vars (`FlightPhaseManager.ts:202-221`):

- `L:A32NX_FMGC_FLIGHT_PHASE` = phase
- `L:AIRLINER_FLIGHT_PHASE` = `phase < TAKEOFF ? 0 : phase + 1` (legacy)

The manager then runs the new phase's `init()`, calls the listeners (FMC side effects, §2.2), and immediately
re-evaluates the next transition.

This FMGC phase is **not** the FWS phase 1..12 already computed by the port (`a380x_avionics_core.c:183-221`,
published as `a380x/xp12/avionics/flight_phase`). Keep both.

**Transitions [FBW]** (`FlightPhaseManager.ts`, `Phase.ts`). Transitions are evaluated only when
`L:A32NX_IS_READY == 1` and slew is not active.

| From → To | Condition | Confirm | Source |
|---|---|---|---|
| any except PREFLIGHT, DONE → DONE | On ground (nose gear compressed, LGCIU 1 or 2) for 30 s **and** neither engine 2 nor 3 has N2 > 20 % | 30 s | FPM.ts:239-248 |
| PREFLIGHT → TAKEOFF | `(FMA vertical == SRS && N1(eng1) ≥ 70 % && N1(eng2) ≥ 70 %) \|\| \|GS\| > 90 kt` | 0.2 s | Phase.ts:34-46; `SH/flightphase.ts:50-55` |
| TAKEOFF → CLIMB | **No engine-out, baro alt valid:** `baroAlt > accAltMsl` | – | Phase.ts:79-108 |
| | **Engine-out, baro alt valid:** `CAS ≥ L:A32NX_SPEEDS_GD && baroAlt > eoAccAltMsl` | | |
| | **Fallback** (EO without both conditions, or baro alt invalid): FMA vertical ∈ {VS, FPA, ALT, ALT*, ALT CST, ALT CST*, OP CLB, CLB} | | |
| TAKEOFF → CLIMB | FCU ALT knob pushed or pulled while in TAKEOFF | – | FPM.ts:116-120 |
| CLIMB → CRUISE | `crzFL > 0 && round(pressAlt/100) ≥ crzFL`, where `crzFL = L:A32NX_AIRLINER_CRUISE_ALTITUDE/100` | – | Phase.ts:116-127 |
| CLIMB, CRUISE → DESCENT | FCU ALT push/pull **and** `canInitiateDes(d)` | – | FPM.ts:121-126 |
| CRUISE → DESCENT | FCU ALT knob turned, FMA vertical ∈ {OP DES, DES, VS with V/S < 0, FPA with FPA < 0}, **and** `canInitiateDes(d)` | – | FPM.ts:131-146 |
| CLIMB, CRUISE → DESCENT | FCU V/S knob: 100 ms later, FMA vertical is VS (V/S < 0) or FPA (FPA < 0), **and** `canInitiateDes(d)`. If `canInitiateDes` is false, run the step-climb/descent handler instead (§6.4). | 100 ms | FPM.ts:148-167 |
| DESCENT → CRUISE | `FL == crzFL && fcuSelFL == FL` (FL = `round(pressAlt/100)`) | – | Phase.ts:146-155 |
| DESCENT → APPROACH | `L:A32NX_FM_ENABLE_APPROACH_PHASE` (VNAV DECEL reached), or pilot ACTIVATE APPR | – | Phase.ts:157-158; FPM.ts:223-237 |
| CLIMB, CRUISE, DESCENT, GOAROUND → APPROACH | `tryGoInApproachPhase()` from the PERF "ACTIVATE APPR" confirmation. Refused in PREFLIGHT, TAKEOFF and DONE. | – | FPM.ts:223-237; `MfdFmsPerf.tsx:3445` |
| APPROACH → GOAROUND | FMA vertical == SRS GA | – | Phase.ts:171-174 |
| APPROACH → DONE | On ground for 30 s, **or** neither engine 2 nor 3 has N2 > 20 % | 30 s | Phase.ts:176-178 |
| GOAROUND → CLIMB | New destination entered (secondary activated with a different DEST) **and** indicated alt > `FM1_ACC_ALT` (or `FM1_EO_ACC_ALT` if engine-out) | – | FPM.ts:190-200; FMC.ts:1077-1084 |
| New CRZ FL entered | APPROACH → CLIMB; DESCENT with FL < new → CLIMB; CLIMB or DESCENT with FL > new → CRUISE | – | FPM.ts:169-188 |
| DONE → PREFLIGHT | `phase == DONE && isReset && active MFD page ∈ {fms/active/init, fms/active/fuel-load, fms/active/perf}` | – | FMC.ts:211-222, 366-371 |

Definitions:

- `canInitiateDes(d)` (FPM.ts:29-40), with `fl = round(indicatedAlt/100)`, `fcuSelFl = FCU alt/100` and
  `cruiseFl = L:A32NX_AIRLINER_CRUISE_ALTITUDE/100`:
  - Returns true when `cruiseFl > 0` and either of these holds:
    - `(d < 200 || fl < 200) && fcuSelFl < cruiseFl && fcuSelFl < fl`
    - `d ≥ 200 && fl > 200 && fcuSelFl ≤ 200`
  - `d` is the along-track distance to destination in NM, or −1 if unknown.
- `accAltMsl` is latched at TAKEOFF `init()` from `L:A32NX_FM1_ACC_ALT`. If that var is invalid it falls back to
  `indicated alt + CONFIG_ACCEL_ALT` (default 1500 ft). `eoAccAltMsl` is latched the same way from
  `FM1_EO_ACC_ALT` (Phase.ts:65-77).
- Spawn initialisation, once in-game (FPM.ts:79-94):
  - `GEAR IS ON GROUND` → PREFLIGHT
  - else `PLANE ALTITUDE > 18000` → CRUISE
  - else `FLAPS HANDLE INDEX > 0` → APPROACH
  - else CLIMB
- Slew (FPM.ts:250-261): PREFLIGHT, TAKEOFF or DONE with AGL ≥ 1500 ft → CLIMB. **[PORT]** X-Plane has no slew;
  omit.
- Phase `init()` side effects:
  - TAKEOFF sets `L:A32NX_COLD_AND_DARK_SPAWN = 0`.
  - APPROACH, GOAROUND and DONE set `L:A32NX_AIRLINER_TO_FLEX_TEMP = 0` (Phase.ts:67, 166, 184, 196).

**[PORT] input mapping for the phase logic:**

| FBW input | Port source |
|---|---|
| `L:A32NX_IS_READY` | Always true once the plugin's first flight loop has run with power |
| Nose gear compressed (LGCIU) | `sim/flightmodel2/gear/on_ground[0]`. FBW uses the nose gear. The port's existing `onground_any` is an acceptable fallback. |
| N2 > 20 % (engines 2/3, and 1..4 where noted) | `sim/cockpit2/engine/indicators/N2_percent[i]`. FBW engine index `n` maps to array index `n-1`. |
| N1 ≥ 70 % (engines 1, 2) | `n1_values[0]`, `n1_values[1]` (already read in `flight_loop`) |
| \|GS\| | `ground_speed_knots` (`a380x_avionics.c:267`) |
| FMA vertical mode (`L:A32NX_FMA_VERTICAL_MODE`) | `guidance.vertical_mode` (`A380XB1Message`): VS→`A380X_B1_VS`, FPA→`_FPA`, ALT→`_ALT`, ALT*→`_ALT_STAR`, ALT CST→`_ALT_CST`, ALT CST*→`_ALT_CST_STAR`, OP CLB→`_OP_CLB`, CLB→`_CLB`, OP DES→`_OP_DES`, DES→`_DES` |
| SRS | **Not ported** (`a380x_guidance.h:58`). Use `engines_at_takeoff_power \|\| GS > 90 kt` (`engines_at_takeoff_power` = at least 2 throttles ≥ 0.85, `a380x_avionics.c:271,278`). Combine with N1(1..2) ≥ 70 % to match FBW. |
| SRS GA | **Not ported.** In APPROACH, enter GOAROUND when airborne and the same TOGA-lever predicate holds for 0.2 s. Port behaviour, flagged. |
| Baro corrected altitude | `guidance_inputs.altitude_ft` |
| Pressure altitude | `guidance_inputs.pressure_altitude_ft` |
| CAS | `guidance_inputs.ias_knots` |
| FCU selected altitude | `guidance.selected_altitude_ft` |
| FCU knob events | Hook the existing `a380x/xp12/fcu/altitude_push/pull/up/down` and `vertical_speed_*` commands (`a380x_fcu_adapter.inc:27-49`). Forward them to `fms_on_fcu_alt_push_pull()`, `fms_on_fcu_alt_turn()` and `fms_on_fcu_vs_knob()` after the guidance command is applied. |
| Along-track distance to destination | From the port route/LNAV. Until that exists, use great-circle PPOS→DEST (§4.8). |

### 2.2 Side effects on phase change [FBW] (FMC.ts:1224-1387)

Every change first sets `L:A32NX_CABIN_READY = 0` and `isReset = false`. The per-phase effects are:

| Entering | Effects (in order) |
|---|---|
| **TAKEOFF** | 1. `destDataCheckedInCruise = false`. |
| | 2. If ACC ALT is not set: `pilotAccelerationAltitude = INDICATED ALTITUDE + CONFIG_ACCEL_ALT` (1500). Same for EO ACC ALT with `pilotEngineOutAccelerationAltitude`. |
| | 3. `tripFuelAtPreflight = tripFuel/1000` (t). |
| | 4. `takeoffWeight = GW` (kg). This freezes TOW. |
| | 5. `pilotTaxiFuel = null`, `pilotRouteReserveFuel = null`, `pilotRouteReserveFuelPercentage = 0`. In-flight route reserve is therefore 0 %. |
| | 6. `climbPredictionsReferenceAutomatic = FCU altitude`. |
| | 7. If a preselected climb speed is set: `updatePreSelSpeedMach(climbPresel)` (arms it). |
| | 8. If step 2 changed anything: `updateThrustReductionAcceleration()`. |
| **CLIMB** | 1. `destDataCheckedInCruise = false`. |
| | 2. If coming from TAKEOFF and a climb presel is set: send `K:A32NX.FMS_PRESET_SPD_ACTIVATE`. |
| | 3. Arm the cruise presel: `updatePreSelSpeedMach(preselectedCruiseSpeed)` (null clears). |
| | 4. If the FCU altitude is valid and CRZ FL is null: `cruiseFlightLevel = FCU alt/100` and `L:A32NX_AIRLINER_CRUISE_ALTITUDE = FCU alt`. |
| **CRUISE** | 1. `destDataCheckedInCruise = false`. |
| | 2. If coming from CLIMB and a cruise presel is set: send `FMS_PRESET_SPD_ACTIVATE`. |
| **DESCENT** | 1. Remove message "NEW CRZ ALT". The text never matches; see §11. |
| | 2. `checkDestData()` (ENTER DEST DATA). |
| | 3. `triggerCheckSpeedModeMessage(null)` (CHECK SPD MODE if speed is selected). |
| | 4. Clear the presel (`SpeedPreselVal = MachPreselVal = -1`). |
| | 5. `cruiseFlightLevel = null`. |
| **APPROACH** | 1. Remove "NEW CRZ ALT". 2. `checkDestData()`. |
| **GOAROUND** | 1. `L:A32NX_GOAROUND_INIT_SPEED = IAS`. |
| | 2. `stringMissedApproach`: each constraint deleted adds "CONSTRAINTS BEFORE <ident> : DELETED". |
| | 3. If missed ACC ALT is not set: `pilotMissedAccelerationAltitude = INDICATED ALT + CONFIG_ENG_OUT_ACCEL_ALT` (1500). Same for missed EO ACC ALT. Each runs `updateThrustReductionAcceleration()`. |
| **DONE** | 1. `flightPlanService.reset()` (all plans deleted, empty active recreated). |
| | 2. `fmgc.data.reset(true)`. |
| | 3. `initSimVars()` (§9.1). |
| | 4. Delete pilot-stored waypoints. |
| | 5. Clear the newest message. |
| | 6. Reset destination predictions. |
| | 7. Set `L:A32NX_COLD_AND_DARK_SPAWN = 1`, navigate the captain MFD to `fms/data/status`, then `isReset = true`. |

**[PORT]**

- Reproduce the table.
- For DONE, navigate **both** MFDs that are on an FMS page to DATA/STATUS. Upstream only does the captain side.
- The DONE → PREFLIGHT condition must test the active page of **either** MFD.

### 2.3 Flight plans [FBW]

**Indices** (`FlightPlanManager.ts:32-37`):

| Index | Plan |
|---|---|
| 0 | Active |
| 1 | Temporary |
| 2 | Uplink |
| 3, 4, 5 | SEC 1, SEC 2, SEC 3 |

`NUM_SECONDARY_FLIGHT_PLANS = 3` and `MAX_NUM_LEGS = 200` (`FpmConfig.ts:59-74`). Every modification increments
the plan's `version`.

- **TMPY.** A380 config: `TMPY_ON_DELETE_WAYPOINT`, `TMPY_ON_OVERFLY`, `TMPY_ON_CONSTRAINT_EDIT`,
  `TMPY_ON_INSERT_WAYPOINT` and `ALLOW_REVISIONS_ON_TMPY` are all `true`.
  - A destructive edit on Active copies Active → TMPY (including FIX INFOs) and edits TMPY
    (`FlightPlanService.ts:322-332`, `899-905`).
  - **INSERT TMPY:** finalise any pending airway, then update the T-P leg of a pending DIR TO with PPOS and present
    track. Copy TMPY → Active (with FIX INFOs) and delete TMPY (`FlightPlanService.ts:224-277`).
  - **ERASE TMPY:** delete TMPY (`:279-285`).
- **New city pair** (INIT FROM/TO; `FlightPlanService.ts:334-368`):
  1. Delete TMPY (when Active) and the target plan.
  2. Recreate it with flag `ManualCreation`, keeping the flight number.
  3. `setOriginAirport` → origin defaults (§6.1).
  4. `setDestinationAirport` → destination defaults (§6.1), and the alternate plan's origin = DEST.
  5. Set the alternate if one was given.
- **SEC copy from Active** (`:110-127`): copy the plan (predictions are not copied), set
  `flags = CopiedFromActive`, and call `active.performanceData.pipeTo(sec.performanceData, isBeforeEngineStart)`.
  - The piped keys are listed in §2.4. "Piped" means a one-way live link from Active to SEC.
  - Implementation detail: `pipe()` stores the subscription **in the source** (Active) under the key. It destroys
    an existing pipe for that key first, so only the most recently copied SEC stays linked.
  - When the **Active** plan sets a piped key, the value propagates once, then **all** pipes of the Active perf
    store are destroyed (`FlightPlan.ts:631-648`).
- **SEC activate / swap** (`:161-222`) uses `persistActivePerfDataAcrossModification`:

  | Value | Rule after the copy/swap |
  |---|---|
  | ZFW, ZFWCG, BLOCK | Old Active value is re-applied if `!isBeforeEngineStart` **or** the new Active has null |
  | Taxi fuel (A380 only, `PERSIST_TAXI_FUEL_ON_SEC_SWAP`) | Old value kept if the new Active `pilotTaxiFuel` is null |
  | CI, flight number | Old values kept if null in the new Active |
  | FIX INFOs | Always preserved |

  `isBeforeEngineStart = !enginesWereStarted`.
- `canActivateOrSwapSecondary(i)` (FMC.ts:891-915) requires SEC i to exist, no TMPY, and the SEC to have both
  origin and destination. In addition, one of these must hold:
  - HDG or TRK mode engaged
  - Neither plan has an active leg
  - Both active legs are equal elements at the same index
  - Phase == PREFLIGHT
- **After SEC activation** (FMC.ts:937-961):
  - If phase is PREFLIGHT or DONE: add "CHECK T.O. DATA", and write `L:A32NX_AIRLINER_TO_FLEX_TEMP = flex`
    (0 → 0.1; null → 0).
  - If `|ΔZFW| > 5` (t) **and** `|ΔZFWCG| > 0.5` (%): add "CHECK ZFW". It is removed on the next Active ZFW change.
  - If the destination changed: `handleNewDestinationAirportEntered()` (GOAROUND → CLIMB rule).
- **When the active plan changes** (FMC.ts:966-983):
  - `hasActiveFlightPlan = active && origin && dest`.
  - Invalidate the VNAV profile.
  - Re-send the flight number words.
  - Reload the mirrors (§2.6).
- **Uplink (CPNY F-PLN)** (FMC.ts:819-889): SimBrief download into the Uplink plan, then
  "COMPANY F-PLN RECEIVED\nWAITING FOR INSERTION".
  - Insert copies Uplink → target, deletes TMPY, runs `updateFmsData`, `trySetCruiseFl(ofp.cruiseAltitude/100)`
    and `paxNumber = ofp.passengerCount`.
  - **[PORT]** Out of scope for the first port; keep the button disabled.

**[PORT]**

- Fixed array `plans[6]` with an `exists` flag each, plus `version` counters.
- Implement the SEC link as a per-key `linked` bit set on the SEC by "copy from active". A pilot edit of that key on
  the SEC clears its bit. Each FMS cycle copies linked keys Active → SEC.
  - This deliberately simplifies the FBW "destroy all pipes" quirk. It gives the same visible result for the
    normal workflow.

### 2.4 Performance data store per plan [FBW]

Source: `A380FlightPlanPerformanceData.ts`; default speed limits from `FlightPlanPerformanceData.ts:790-798`.

- "Eff." gives the effective value that consumers read.
- `round10(x)` = `round(x/10)*10`.
- **P** marks a piped key (§2.3). **P\*** keys are piped only before engine start.

| Key | Unit | Default | Eff. / notes |
|---|---|---|---|
| `cruiseFlightLevel` P | FL | null | |
| `cruiseTemperatureIsaTemp` P | °C | null | Set to ISA(FL) by INIT (§3.3) |
| `cruiseTemperaturePilotEntry` P | °C | null | `cruiseTemperature = pilot ?? isa` |
| `costIndex` P | – | null | |
| `costIndexMode` P | enum LRC=0, ECON=1 | ECON | |
| `pilotTropopause` P / `defaultTropopause` | ft | null / 36090 | `tropopause = round10(pilot ?? default)` |
| `v1`, `vr`, `v2` | kt | null | |
| `pilotThrustReductionAltitude` / `defaultThrustReductionAltitude` | ft | null / §6.1 | `thrustReductionAltitude = round10(pilot ?? default)` |
| `pilotAccelerationAltitude` / `default…` | ft | null / §6.1 | `accelerationAltitude = round10(…)` |
| `pilotEngineOutAccelerationAltitude` / `default…` | ft | null / §6.1 | `engineOutAccelerationAltitude = round10(…)` |
| `pilotMissedThrustReductionAltitude`, `…MissedAcceleration…`, `…MissedEngineOutAcceleration…` (pilot/default each) | ft | null / §6.1 | `missed… = round10(pilot ?? default)` |
| `databaseTransitionAltitude` / `pilotTransitionAltitude` | ft | null | `transitionAltitude = round10(pilot ?? db)`; `…IsFromDatabase = pilot == null` |
| `databaseTransitionLevel` / `pilotTransitionLevel` | FL | null | `transitionLevel = round(pilot ?? db)` |
| `climbSpeedLimitSpeed` / `…Altitude` / `isClimbSpeedLimitPilotEntered` | kt / ft / bool | 250 / 10000 / false | Same three fields for descent and for the alternate climb/descent |
| `zeroFuelWeight` P\* | **t** | null | |
| `zeroFuelWeightCenterOfGravity` P\* | %MAC | null | |
| `blockFuel` P\* | **t** | null | |
| `takeoffWeight` | **kg** | null | §4.3 |
| `pilotTaxiFuel` / `defaultTaxiFuel` | t | null / **1.5** | Constructor arg `defaultTaxiFuel = 1.5` overrides the field initialiser 0.2 (`:26-28, 613`). `taxiFuel = pilot ?? default`. |
| `pilotRouteReserveFuel` P | t | null | `isRouteReserveFuelPilotEntered = pilot != null` |
| `pilotRouteReserveFuelPercentage` P / `default…` | % | null / 5 | `routeReserveFuelPercentage = pilot ?? default`; `isRouteReserveFuelPercentagePilotEntered = !isRouteReserveFuelPilotEntered` |
| `pilotAlternateFuel` / `calculatedAlternateFuel` | t | null / 0 | `alternateFuel = pilot ?? calculated` |
| `pilotFinalHoldingFuel` P / `pilotFinalHoldingTime` P / `defaultFinalHoldingTime` / `calculatedFinalHoldingTime` | t / min | null / null / 30 / null | `finalHoldingTime = pilotTime ?? calcTime ?? 30` |
| `calculatedFinalHoldingFuel` | t | null | `finalHoldingFuel = pilotFuel ?? calculatedFuel` |
| `pilotMinimumDestinationFuelOnBoard` P\* | t | null | `calculatedMin… = altn + final` (both non-null); `minimumDestinationFuelOnBoard = pilot ?? calculated` |
| `pilotTripWind` | kt (+ tail, − head) | null | |
| `takeoffShift` | m | null | |
| `takeoffFlaps` | `FlapConf` 0..3 | null | The MFD stores 1, 2 or 3 (`CONF 1+F`, 2, 3) |
| `trimmableHorizontalStabilizer` | ° | null | Not used by the A380 MFD |
| `flexTakeoffTemperature` | °C | null | |
| `preselectedClimbSpeed` | kt | null | |
| `preselectedCruiseSpeed` | kt, or Mach if ≤ 1 | null | |
| `pilotManagedDescentSpeed` / `pilotManagedDescentMach` | kt / M | null | |
| `approachQnh` | hPa, or inHg if < 100 | null | `qnhToMillibar` (`MFD/shared/QnhUtils.ts:6-12`) |
| `approachTemperature` | °C | null | |
| `approachWindDirection` | ° magnetic | null | |
| `approachWindMagnitude` | kt | null | |
| `isApproachWindPilotEntered` | bool | false | |
| `pilotVapp` | kt | null | |
| `approachBaroMinimum` | ft | null | MDA |
| `approachRadioMinimum` | ft; **0 = NO DH** | null | `RADIO_ALTITUDE_NODH_VALUE = 0` (`DataEntryFormats.tsx:360`) |
| `approachFlapsThreeSelected` | bool | false | |
| `estimatedTakeoffTime` P\* / `…Expired` P\* | s | null / false | |
| `paxNumber` P | count | null | |
| `takeoffPowerSetting` | TOGA=0, FLEX=1, DERATED=2 | TOGA | |
| `takeoffDeratedSetting` | D01..D05 = 0..4 | D01 | |
| `takeoffThsFor` | %MAC | null | "THS FOR" is the CG used for the trim setting |
| `takeoffPacks` | OFF/APU=0, ON=1 | ON | |
| `takeoffAntiIce` | OFF=0, ENG ONLY=1, ENG+WING=2 | OFF | |
| `noiseEnabled` / `noiseN1` / `noiseSpeed` / `noiseEndAltitude` | bool / % / kt / ft | false / null / null / null | |
| `climbDerated` | NONE=0, D01..D05 = 1..5 | NONE | |
| `descentCabinRate` | ft/min | −350 | |
| Climb/descent wind entries, alternate wind | – | empty | Not covered here |

**[PORT]** One `A380XFmsPerf` struct per plan with these fields, stored in kg, plus
`double eff_*(const A380XFmsPerf*)` accessors that implement the "Eff." column exactly, including `round10`.

### 2.5 FmgcData (not stored per plan) [FBW] (`fmgc.ts:72-176`)

`reset()` restores every field to its default. It runs on `reset()` and on DONE.

| Field | Default | Use |
|---|---|---|
| `cpnyFplnAvailable`, `cpnyFplnRequestedForPlan`, `cpnyFplnUplinkInProgress` | false / null / false | Uplink state |
| `atcCallsign` | null | Mirror of the active flight number |
| `tripFuelAtPreflight` | null (t) | Latched at TAKEOFF |
| `destEfobBelowMinInActive` | false | §4.7 |
| `vSpeedsForRunway` | null | Departure runway ident the V-speeds belong to |
| `v1ToBeConfirmed`, `vrToBeConfirmed`, `v2ToBeConfirmed` | null | §3.6 |
| `flapRetractionSpeed`, `slatRetractionSpeed`, `greenDotSpeed` | null | Current F/S/GD (§5.4) |
| `approachGreenDotSpeed`, `approachSlatRetractionSpeed`, `approachFlapRetractionSpeed` | null | Approach F/S/GD |
| `climbPredictionsReferencePilotEntry` / `…Automatic` | null | `climbPredictionsReference = pilot ?? auto` |
| `approachVapp`, `approachVref`, `approachVls` | null | §5.5 |
| `positionMonitorFix` | null | POSITION MONITOR page |
| `estimatedTakeoffTime` | null | |
| `engineOut` | false | §5.9 |
| `fuelPenaltyPercentage` | null | `fuelPenaltyActive = v != null && v > 0` |

### 2.6 FMC-level derived state [FBW]

| Name | Definition | Source |
|---|---|---|
| `enginesWereStarted` | §1.3 step 3.2a. Drives FOB source, TOW/LW formulae and SEC perf copy. | FMC.ts:1474-1481 |
| `hasActiveFlightPlan` | Active plan exists with origin and destination | FMC.ts:967-971 |
| Mirrors, refreshed every 250 ms and on active-plan change | `zeroFuelWeight`, `zeroFuelWeightCenterOfGravity`, `approachQnh`, `approachTemperature`, `approachWindDirection`, `approachWindMagnitude`, `minimumDestinationFuel`, `alternateFuel`, `finalFuelWeight`, `destinationRunwayBearing` (magnetic), `approachFlapsThreeSelected` | FMC.ts:1788-1802 |
| `ZfwOrZfwCgUndefined` | `zfw == null \|\| zfwCg == null`. When it becomes false, remove "INITIALIZE ZFW / ZFWCG". | FMC.ts:254-258, 383-388 |
| `destDataEntered` | QNH, temperature, wind direction and wind magnitude all non-null. When true, remove "ENTER DEST DATA". | FMC.ts:270-277, 397-402 |
| `pilotEntryMinFuelBelowAltnPlusFinal` | All three non-null **and** `minFuel < altn + final` → "CHECK MIN FUEL AT DEST" | FMC.ts:282-288; FAI.ts:382-388 |
| `approachHeadWindComponent` | `getHeadwind(windMag, windDir, destRwy.magneticBearing)` when all are set, else null. Recomputed when any input changes. Output in §9.1. | FMC.ts:421-438, 474-489 |
| LRC default | When engines start, or ZFW becomes non-null, and the active plan has a DEST and `costIndex == null`: set `costIndex = 0` and `costIndexMode = LRC`, add "LRC MODE IN USE". | FMC.ts:372-376, 404-408, 1777-1786 |

---

## 3. Pilot entries and their side effects

The page layouts are in the page specs of this folder. This section only defines the FMC-side behaviour.

### 3.1 Entry pipeline [FBW] (`MsfsAvionicsCommon/UiWidgets/InputField.tsx:383-440`)

1. `parse(text)` from the field's format (§3.2).
   - On an `FmsError`, call `showFmsErrorMessage(type, details)`. It queues the message text plus `"\n" + details`
     when details exist (FMC.ts:1094-1111). The field keeps its old value.
2. On parse success, the field shows a "validating" state for **500 ms** while the data handler runs in parallel.
   - If the handler throws or returns `false`, the entry is rejected **silently**, with no message.
3. On success, if the field has a `value` subject and no `onModified`, the subject is updated.
4. **CLR** turns the field text into `''`. `parse('')` returns null, so the handler is called with `null`, which
   deletes the pilot entry and reverts to the default/computed value.
   - This only applies when `canBeCleared` is true (the default). FROM, TO, FLT NBR and CRZ FL set it false.

`FmsErrorType → text` (FMC.ts:1094-1111; `NXSystemMessages.ts`):

| Error type | Message |
|---|---|
| `EntryOutOfRange` | `ENTRY OUT OF RANGE` + details |
| `FormatError` | `FORMAT ERROR` + details |
| `NotInDatabase` | `NOT IN DATABASE` |
| `NotYetImplemented` | `NOT YET IMPLEMENTED` |
| `ListOf99InUse`, `AwyWptMismatch` | Ignored (no message) |

Detail line formats (`DataEntryFormats.tsx:15-32`):

- `"RNG: {min} TO {max}"`, plus `" {unit}"` when the format has a unit
- `"FORMAT: {fmt}"`, plus `" {unit}"` when the format has a unit

### 3.2 Entry formats used by the FMC pages [FBW] (`DataEntryFormats.tsx`)

| Format | Accepted input → value | Range | Errors (details) |
|---|---|---|---|
| `SpeedKnotsFormat(90, 340)` | Number | [90, 340] kt | FORMAT ERROR `FORMAT: XXX KT`; ENTRY OUT OF RANGE `RNG: 90 TO 340 KT` |
| `SpeedMachFormat(0.1, 0.89)` | `.85` → 0.85. A bare integer ≥ 0.89 becomes `0.<digits>` (`85` → 0.85). | [0.1, 0.89] | `RNG: 0.1 TO 0.89`; `FORMAT: XX` |
| `AltitudeOrFlightLevelFormat` | ≤ 3 characters → ×100 (FL); else ft | [0, 43000] | Out of range: **no detail line**; `FORMAT: FOR ALT XXXXX FOR FL XXX` |
| `AltitudeFormat(1, 43000)` (TRANS ALT) / `(0, 43000)` (BARO) | ft | as given | ENTRY OUT OF RANGE (no detail) |
| `FlightLevelFormat(0, 430)` | `nnn` or `FLnnn` | [0, 430] | ENTRY OUT OF RANGE (no detail); `FORMAT: FL XXX` |
| `RadioAltitudeFormat` | `NO DH`, `NODH`, `NONE`, `NO` → 0; else ft | [1, 43000] | ENTRY OUT OF RANGE / FORMAT ERROR (no detail) |
| `TropoFormat` | ≤ 3 characters → ×100 | [1000, 60000] ft | ENTRY OUT OF RANGE (no detail); `FORMAT: FOR ALT XXXXX FOR FL XXX` |
| `WeightFormat(min, max)` | Number in **t or klb** (display unit) → kg | min/max in kg; the message shows them in display units with 1 decimal | `RNG: a.b TO c.d T` (or `KLB`); `FORMAT: XXX.X T` |
| `PercentageFormat(min, max)` | Number | as given | `RNG: a.b TO c.d %`; `FORMAT: XX.X %` |
| `TemperatureFormat(min, max)` | Number | as given | `RNG: … °C`; `FORMAT: +/-XX °C` |
| `CrzTempFormat` | Number; **positive without `+` is negated** (`45` → −45) | [−99, 99] | `RNG: -99 TO 99 C` |
| `CostIndexFormat` | Integer | [0, 999] | `RNG: 0 TO 999`; `FORMAT: XXX` |
| `TripWindFormat` | `HDnnn`, `-nnn`, `Hnnn` → negative; `TLnnn`, `+nnn`, `Tnnn` or bare → positive | [−250, 250] kt | `RNG: -250 TO 250 KT`; `FORMAT: +/-XXX KT` |
| `WindDirectionFormat` | ° | [0, 359] | `RNG: 0 TO 359 °` |
| `WindSpeedFormat` | kt | [0, 250] | `RNG: 0 TO 250 KT` |
| `QnhFormat` | `NN.NN` or `NNNN` within [22.00, 32.48] → inHg (NNNN/100). Otherwise 1–4 digits within [745, 1100] → hPa. | – | `RNG: 22.00 TO 32.48`, `RNG: 745 TO 1100`, `FORMAT: XXXX` |
| `TimeHHMMFormat` | `HHMM` or `HH:MM` (minutes ≤ 59, hours ≤ 23) → minutes | [0, 90] min | ENTRY OUT OF RANGE (no detail) |
| `PaxNbrFormat` | Integer | [0, 999] | `RNG: 0 TO 999` |
| `LengthFormat(1, rwyLength)` | m or ft per the length unit | [1, runway length] | ENTRY OUT OF RANGE |
| `DescentRateFormat(-999, -100)` | ft/min | [−999, −100] | `RNG: -999 TO -100 FT/MN` |
| `AirportFormat` | Exactly 4 characters | – | `FORMAT: AAAA` |
| `LongAlphanumericFormat` | 1..10 characters | – | – |

**[PORT]** Implement these as pure C parsers returning `{status, value, detail[32]}`. The MFD UI owns the
500 ms validating state.

### 3.3 FMS INIT page [FBW] (`MfdFmsInit.tsx`)

| Field | Store | Side effects | Disabled / mandatory |
|---|---|---|---|
| FLT NBR | Plan flight number (`:391`) | If the Active plan is loaded: `updateFlightNumber` → ARINC flight number words, sim `ATC FLIGHT NUMBER`, ATSU network connect. Clearing it disconnects. Then `updateFmsData()` (`:173-185`; FMC.ts:985-1030). | Disabled without a plan. Mandatory for Active/TMPY. Not clearable. |
| FROM / TO | – | When both are set and either differs from the plan: `newCityPair(FROM, TO, ALTN)` (§2.3). For Active also `updateFmsData()` and `calculateFinalAndAlternateFuel()` (`:312-334`). | Disabled when (phase > PREFLIGHT and the plan is Active or a copy of Active) or TMPY exists. Not clearable. |
| ALTN | Alternate airport; `NONE` → none | `setAlternate(...)`, `updateFmsData()` (`:456-467`) | Disabled if TMPY exists or FROM/TO are empty |
| CPNY RTE, ALTN RTE, RTE SEL, WIND, CPNY WIND REQUEST | – | **[FBW-PH]** Disabled (`:476-512`, `:647-659`) | – |
| CRZ FL | `trySetCruiseFl(v, plan)` (§6.4) | §6.4 | Mandatory if phase < DESCENT and the plan is Active/TMPY. Disabled like ALTN. |
| CRZ TEMP | `cruiseTemperaturePilotEntry` | – | Disabled when CRZ FL is null |
| MODE (LRC/ECON; EO-LRC/EO-ECON when EO) | `costIndexMode` | – | Disabled without FROM/TO, or when phase ≥ DESCENT on Active/copy |
| TROPO | `pilotTropopause` | – | Disabled without a plan |
| CI | `costIndex` | – | Disabled when MODE is disabled or MODE == LRC. Mandatory on Active/TMPY. |
| TRIP WIND | `pilotTripWind` | – | Disabled without FROM/TO |

Implicit INIT logic (`:271-285`, runs at each `onNewData`):

- If CRZ FL is set **and** (CRZ TEMP is null **or** `crzTemp − ISA(FL·100) > 0.5`), set
  `cruiseTemperatureIsaTemp = ISA(FL·100)`.
- `ISA(alt) = min(alt, 36089)·(−0.0019812) + 15` (`OperatingAltitudes.tsx:29-31`).

**[PORT]**

- Run this ISA rule in the core whenever CRZ FL changes.
- An unknown airport must give **NOT IN DATABASE** and leave the plan unchanged. In FBW the failing
  `loadAirport` throws a plain `Error` from an un-awaited call, so no message appears (`OriginSegment.ts:45-49`;
  §11).

### 3.4 FUEL & LOAD page [FBW] (`MfdFmsFuelLoad.tsx`)

| Field | Format (kg limits) | Store | Side effects | Disabled / mandatory |
|---|---|---|---|---|
| ZFW | `WeightFormat(270000, 373000)` | `zeroFuelWeight = v/1000` | ZFW mirror → removes INITIALIZE ZFW message, LRC default (§2.6), ARINC ZFW | Mandatory on Active/TMPY |
| ZFWCG | `PercentageFormat(23, 48)` | `zeroFuelWeightCenterOfGravity` | ARINC ZFW CG | Mandatory on Active/TMPY |
| BLOCK | `WeightFormat(0, 289982)` | `blockFuel = v/1000` | – | Mandatory on Active/TMPY. Hidden after engine start (`:300-306`). |
| FUEL PLANNING | – | **[FBW-PH]** Disabled (`:121, 523`) | – | – |
| TAXI | `WeightFormat(0, 9889)` | `pilotTaxiFuel = v/1000` | – | Disabled when phase ≥ TAKEOFF on Active/copy (`:166-172`) |
| RTE RSV (fuel) | `WeightFormat(0, 50000)` (AMI `rsvMin`/`rsvMax`) | `pilotRouteReserveFuel = v/1000`, `pilotRouteReserveFuelPercentage = null` | – | Same as TAXI |
| RTE RSV % | `PercentageFormat(0, 15)` | `pilotRouteReserveFuel = null`, `pilotRouteReserveFuelPercentage = v` | – | Same as TAXI |
| ALTN fuel | `WeightFormat(0, 289982)` | `pilotAlternateFuel = v/1000` | – | Disabled without an alternate airport |
| FINAL fuel | `WeightFormat(0, 100018)` | `pilotFinalHoldingFuel = v/1000`, `pilotFinalHoldingTime = null` | – | – |
| FINAL time | `TimeHHMMFormat` (0..90 min) | `pilotFinalHoldingFuel = null`, `pilotFinalHoldingTime = v` | – | – |
| PAX NBR | `PaxNbrFormat` | `paxNumber` (only if not null) | `L:A32NX_FMS_PAX_NUMBER = v` | Mandatory on Active/TMPY |
| MODE / CI | as INIT | as INIT | – | MODE disabled without DEST, or phase ≥ DESCENT on Active/copy |
| JTSN GW | `WeightFormat(0, 512000)` | – | **[FBW-PH]** Disabled | Visible only on Active/TMPY |
| MIN DEST FOB | `WeightFormat(—, 250000)` | `pilotMinimumDestinationFuelOnBoard = v/1000` | CHECK MIN FUEL AT DEST evaluation | – |

The 1 Hz page loop (`:218-298`) computes the displayed values: ALTN/FINAL (§4.6), TOW (§4.3), LW (§4.4), RTE RSV
(§4.5), GW/CG/FOB (only after engine start, else dashes), TRIP (§4.4), EXTRA (§4.5) and DEST/ALTN predictions.

DEST EFOB is drawn amber when `destEfobBelowMinInActive` is true and the Active plan is loaded (`:73-77`).

### 3.5 PERF page [FBW] (`MfdFmsPerf.tsx`)

Tab activity (the plan "is Active or a copy of Active"; `:219-245`):

| Tab | Inactive when phase … |
|---|---|
| T.O | ≥ TAKEOFF |
| CLB | > CLIMB |
| CRZ | > CRUISE |
| DES | > DESCENT |

CI is disabled for LRC, or for phase ≥ DESCENT (`:440-452`).

**T.O tab**

| Field | Format | Store | Side effects |
|---|---|---|---|
| T.O SHIFT | `LengthFormat(1, rwy length)` | `takeoffShift` (m) | Disabled without an origin runway |
| V1 | Speed [90, 340] | `v1` | Immediate `L:AIRLINER_V1_SPEED = v` (`:1461-1466`) |
| VR | same | `vr` | Immediate `L:AIRLINER_VR_SPEED = v` |
| V2 | same | `v2` | Immediate `L:AIRLINER_V2_SPEED = v` |
| TOGA / FLEX / DERATED radio | – | `takeoffPowerSetting` | On Active, write `L:A32NX_AIRLINER_TO_FLEX_TEMP`: FLEX → `flex == 0 ? 0.1 : flex ?? 0`; TOGA/DERATED → 0 (`:369-386`). TOGA and FLEX are disabled at phase ≥ TAKEOFF; DERATED is **always disabled** (`:434-437`). |
| FLEX temperature | `TemperatureFormat(0, 99)` | `flexTakeoffTemperature` | On Active, `L:A32NX_AIRLINER_TO_FLEX_TEMP = v == 0 ? 0.1 : v ?? 0` (`:1643-1657`). This is written even when TOGA is selected. |
| DERATED D01..D05 | Dropdown | `takeoffDeratedSetting` after the confirm dialog "DERATED Dxx" | – |
| FLAPS 1/2/3 | Dropdown | `takeoffFlaps = index + 1` | On Active, `setTakeoffFlaps` (DISCRETE_WORD_2) |
| THS FOR | `PercentageFormat(0, 99.9)` (ignored if 0) | `takeoffThsFor` | `setTakeoffTrim(v)` → `TO_PITCH_TRIM` word carries this %MAC value |
| PACKS OFF/APU, ON | – | `takeoffPacks` | If engines were started: add "CHECK T.O. DATA" |
| ANTI ICE OFF, ENG ONLY, ENG + WING | – | `takeoffAntiIce` | Same as PACKS |
| THR RED | `AltitudeOrFlightLevelFormat` | `pilotThrustReductionAltitude` | Outputs refresh within 250 ms |
| ACC | same | `pilotAccelerationAltitude` | same |
| EO ACC | same | `pilotEngineOutAccelerationAltitude` | same |
| TRANS ALT | `AltitudeFormat(1, 43000)` | `pilotTransitionAltitude` | `updateTransitionAltitudeLevel()` |
| NOISE, N1, SPD, END, CANCEL NOISE | % (40..110), kt, ft | `noise*` | **[FBW-PH]** NOISE button disabled |
| CONFIRM T.O SPDs | – | `v1/vr/v2 = *ToBeConfirmed`; write the vars; clear the TBC values | Never visible: `vSpeedSet` is always true (`:303-315`; §11) |
| CPNY T.O REQUEST | – | **[FBW-PH]** Disabled | – |

The F, S and GD shown on the T.O tab are the current `flapRetractionSpeed`, `slatRetractionSpeed` and
`greenDotSpeed` (§5.4).

**CLB tab**

- MODE and CI as INIT. DERATED CLB is always inactive (**[FBW-PH]**).
- PRED TO: `climbPredictionsReferencePilotEntry`. When phase == CLIMB and the Active plan is loaded, the automatic
  reference = FCU altitude, else null (`:1065-1075`).
- PRESEL speed (before CLIMB only): `preselectedClimbSpeed`, speed [90, 340].
- THR RED, ACC and TRANS ALT as on the T.O tab.
- SPD CSTR navigates to the vertical revision page.

**CRZ tab**

- MODE and CI.
- PRESEL Mach or kt (before CRUISE only): `preselectedCruiseSpeed`. Mach [0.1, 0.89], kt [90, 340].
- CMS and STEP ALTs are navigation buttons.

**DES tab**

- MODE and CI.
- CABIN RATE `descentCabinRate` [−999, −100].
- Managed Mach / speed (from DESCENT onwards): `pilotManagedDescentMach` and `pilotManagedDescentSpeed`.
- PRED TO: local field only (**[FBW-PH]**).

**APPR tab** (wind, temperature, QNH and minimum fields are disabled without a destination airport)

| Field | Store | Side effects |
|---|---|---|
| WIND direction / magnitude | `approachWindDirection`, `approachWindMagnitude` | Headwind/crosswind and VAPP recompute (§5.5) |
| TEMP [−99, 99] | `approachTemperature` | – |
| QNH | `approachQnh` (null is ignored) | `L:A32NX_DESTINATION_QNH = qnhToMillibar(v)` (hPa) |
| BARO (MDA) [0, 43000] | `approachBaroMinimum` | `L:AIRLINER_MINIMUM_DESCENT_ALTITUDE = v` |
| RADIO (DH); only shown for an ILS/GLS approach | `approachRadioMinimum` (0 = NO DH) | `L:AIRLINER_DECISION_HEIGHT = v ?? -1` |
| CONF 3 / FULL radio | `approachFlapsThreeSelected = (index == 0)` | `L:A380X_FM_LANDING_CONF3`; approach speeds use CONF 3 |
| VAPP | `pilotVapp` | `approachVapp = pilotVapp ?? computed` |
| TRANS FL (`FlightLevelFormat`) | `pilotTransitionLevel` | `updateTransitionAltitudeLevel()` |
| ACTIVATE APPR (dialog "ACTIVATE APPR ?") | – | `tryGoInApproachPhase()`. Visible on Active/TMPY in CLB, CRZ, DES and GA. |
| CLEAR EO (dialog "CLEAR EO ?") | `engineOut = false` | Visible while EO |

**GA tab**

- `pilotMissedThrustReductionAltitude`, `pilotMissedAccelerationAltitude` and
  `pilotMissedEngineOutAccelerationAltitude`.
- The format uses the transition level (FL → ft) as its FL/ALT boundary.

### 3.6 Page-hosted FMS logic that the port must move into the core

**Runway change invalidates V-speeds** [FBW] (`pages/common/FmsPage.tsx:280-302`; runs every 500 ms while any FMS
page is open):

- If the Active plan has an origin runway and `vSpeedsForRunway` is null, store the runway ident.
- Otherwise, if the ident changed:
  1. Store the new ident.
  2. Copy V1/VR/V2 into `*ToBeConfirmed`, then set `v1 = vr = v2 = null`.
  3. Add "CHECK T.O. DATA", resolved when VR is non-null.
  4. Run `updateFmsData()`.

**CRZ TEMP ISA default** (§3.3).

**ALTN/FINAL calculation and TOW** (§4.3, §4.6).

**[PORT]**

- Run all of the above every 250 ms in the core.
- Show the CONFIRM T.O SPDs button whenever any `*ToBeConfirmed` is non-null and all of V1/VR/V2 are null. This
  matches the evident intent of `shouldShowConfirmVSpeeds`; FBW never shows it (§11).

---

## 4. Weight, CG and fuel predictions

### 4.1 Sources [FBW] (`fmgc.ts:204-260`)

| Quantity | Primary | Fallback |
|---|---|---|
| GW (t) | FQMS `fqms_gross_weight` (kg/1000) when NormalOperation | `ZFW + FOB` (both required), else null |
| GW CG (%MAC) | FQMS `fqms_center_of_gravity_mac` | null. WBBC is TODO. `updatePerfSpeeds` substitutes **35 %**. |
| FOB (t) | If any of engines 1..4 has N2 > 20 %: FQMS FOB/1000 | Otherwise **BLOCK** fuel from the plan |
| ZFW used by `getZeroFuelWeight` | Plan ZFW | `minGw` = 270 t |

**ZFW auto-initialisation** (FAI.ts:1252-1270): when in-game, both ZFW and ZFWCG are null, and
`L:A32NX_COLD_AND_DARK_SPAWN` is false:

- `zeroFuelWeight = L:A32NX_AIRFRAME_ZFW/1000`
- `zeroFuelWeightCenterOfGravity = L:A32NX_AIRFRAME_ZFW_CG_PERCENT_MAC`

Every 250 ms, `L:A32NX_FM_GROSS_WEIGHT = GW (kg) or 0`.

**[PORT]**

| Quantity | X-Plane source |
|---|---|
| GW | `sim/flightmodel/weight/m_total` (kg). Already read as `guidance_inputs.weight_kg`, `a380x_fcu_adapter.inc:179`. |
| FOB | `sim/flightmodel/weight/m_fuel_total` (kg) |
| GW CG | `sim/flightmodel2/misc/cg_offset_z_mac` (%MAC; present in X-Plane 12 `DataRefs.txt`) |

- Treat these sources as FQMS-valid when avionics power is on and the values are finite.
- ZFW auto-init (non cold-and-dark spawn, i.e. engines running when the flight loads): `ZFW = m_total − m_fuel_total`.
  ZFWCG stays null (pilot entry) until a fuel-moment model exists; open question.

### 4.2 Rec max / OPT / EO max FL [FBW] (FMC.ts:665-696; `OperatingAltitudes.tsx:12-40`)

```
recMaxAlt(gw_kg, disa) = round500( 52442 − 1.24194943e-2·gw − 3.50813174e-8·gw²
                                   − 53.7792269·disa − 1.87989779e-4·gw·disa − 4.46181779·disa² )
recMaxFL  = min(recMaxAlt, 43000)/100      (null if GW unknown; Active/TMPY only)
OPT FL    = floor(0.96·recMaxFL/5)·5       (null if EO or phase > CRUISE)
EO MAX FL = floor(0.80·recMaxFL/5)·5
```

- `round500(x) = round(x/500)·500`.
- `disa = SAT − ISA(indicated altitude)`. Port inputs: `sim/weather/aircraft/temperature_ambient_deg_c` and
  `guidance_inputs.altitude_ft`.

### 4.3 Takeoff weight [FBW] (FMC.ts:574-604)

`calculateTakeoffWeight(plan)` returns without writing if the plan is Active or a copy of Active **and**
phase ≥ TAKEOFF (frozen). Otherwise:

| Case | TOW (kg) |
|---|---|
| Not Active/copy, **or** engines not started | `(ZFW + BLOCK − TAXI)·1000` (null if any term is null) |
| Engines started | `GW − TAXI·1000` (null if GW is null) |

At TAKEOFF phase entry, `takeoffWeight = GW` (§2.2). **[FBW-PH]** It is only called from the FUEL&LOAD page loop.
**[PORT]** Call it every 250 ms.

### 4.4 Trip fuel and landing weight [FBW] (FMC.ts:551-616)

- `TRIP (kg) = FOB·1000 − kg(destPred.estimatedFuelOnBoard)`. Active plan only, and only when VNAV has a
  destination prediction. SEC is **[FBW-PH]** (null).
- Landing weight (kg):

  | Case | LW |
  |---|---|
  | Engines not started | `TOW − TRIP` (null if either is null **or 0**) |
  | Engines started and phase ≥ TAKEOFF | `GW − TRIP` |
  | Preflight with engines started | `GW − (TRIP ?? 0) − TAXI·1000` |
  | Otherwise | null |

### 4.5 Route reserve and extra fuel [FBW] (FMC.ts:618-662)

- `RTE RSV (kg) = pilotRouteReserveFuel·1000` if set, else `TRIP·pct/100` (pct default 5).
- Displayed RTE RSV % (`MfdFmsFuelLoad.tsx:246-262`):
  - If RTE RSV fuel is not pilot-entered: show `routeReserveFuelPercentage`.
  - Otherwise: show `pilotRsv·1000/TRIP·100`.
- EXTRA (kg), Active plan with a destination prediction:
  - PREFLIGHT:
    `max( (enginesStarted ? FOB : BLOCK)·1000 − TAXI·1000 − TRIP − MINDEST·1000 − RTE RSV, LOWEST )`
  - Other phases: `max( kg(destEFOB) − MINDEST·1000, LOWEST )`
- `LOWEST = LOWEST_FUEL_ESTIMATE = −77161.8 lb = −35000 kg` (`A380AircraftConfig.ts:30`; `fmgc.ts:67`).

### 4.6 ALTN, FINAL and MIN DEST FOB [FBW-PH] (FAI.ts:1767-1790)

`calculateFinalAndAlternateFuel(plan)` contains placeholder constants:

- `calculatedAlternateFuel = hasAlternate ? 6.5 : 0` (t). Without an alternate, `pilotAlternateFuel` is cleared.
- If FINAL fuel is pilot-entered: `calculatedFinalHoldingTime = pilotFuel/0.2` (min) and
  `calculatedFinalHoldingFuel = null`.
  - Otherwise: `calculatedFinalHoldingTime = null` and `calculatedFinalHoldingFuel = finalHoldingTime·0.2` (t).
  - The rate is 0.2 t/min = 12 t/h. The default 30 min gives 6.0 t.
- MIN DEST FOB = pilot entry, or ALTN + FINAL.
- Alternate cruise level (FMC.ts:1052-1075), from the great-circle DEST → ALTN distance:

  | Distance | Level |
  |---|---|
  | > 300 NM | FL310 |
  | > 150 NM | FL220 |
  | otherwise | FL100 |

**[PORT]** Reproduce these constants and label them "FBW placeholder" in code. Call the function every 250 ms.

### 4.7 Destination / alternate EFOB [FBW]

- `destEFOB (t) = kg(VNAV destination prediction estimatedFuelOnBoard)/1000`, or null (`fmgc.ts:458-467`).
- `altnEFOB (t) = max(destEFOB − alternateFuel, −35)` (`fmgc.ts:470-480`). FUEL&LOAD displays it with the wrong
  unit (§11).
- **DEST EFOB BELOW MIN with hysteresis** (FAI.ts:1692-1707), using MINDEST in t:

  | Current state | New state |
  |---|---|
  | Below | `destEFOB − MINDEST ≤ 0.3` |
  | Not below | `destEFOB < MINDEST` |
  | `destEFOB` or MINDEST is null | false |

  Output: `L:A380X_FMS_DEST_EFOB_BELOW_MIN`.
- **The "DEST EFOB BELOW MIN" message** (FAI.ts:1709-1724) is shown when below is true **and** one of these holds:
  - phase ∈ {CRUISE, DESCENT}
  - CLIMB with the FMA ALT mode (PRIM FG word 3 bit 20) active for > 600 s
  - phase ∈ {APPROACH, GOAROUND} with RA > 800 ft

### 4.8 [PORT] Predictions

FBW's TRIP, EFOB, ETA, EXTRA and LW all depend on the VNAV `getDestinationPrediction()` (lb fuel, seconds,
distance).

- **Phase 1 (with this FMC port):** no predictor. Leave `destPred` null. TRIP, EFOB, EXTRA and LW then show
  dashes, which is exactly FBW's behaviour without a prediction. Approach speeds fall back to GW (§5.5).
- **Phase 2:** port the FBW VNAV predictor and its engine model (`A380AircraftConfig.ts:36-170`) in a separate
  spec. Do not invent fuel-flow coefficients.
- Expose `fms_set_destination_prediction(dist_nm, seconds, efob_kg)` so a later predictor can plug in.

---

## 5. Speeds

### 5.1 Constants [FBW] (`SH/PerformanceConstants.ts:1-42`)

| Name | Value |
|---|---|
| VMO / MMO | 340 kt / 0.89 |
| VFE CONF 1 / 1+F / 2 / 3 / FULL | 263 / 222 / 220 / 196 / 182 kt |
| VMCL / VMCL2 | 123 / 147 kt |
| VLE / MLE, VLO / MLO | 250 kt / 0.55 |
| VLO gravity extension / MLO gravity extension | 220 kt / 0.48 |
| Max certified altitude | 43000 ft |
| Default tropopause | 36090 ft |
| ZFWCG range | 23..48 %MAC |
| ZFW range | 270000..373000 kg |
| GW range | 270000..512000 kg |
| Max BLOCK | 289982 kg |
| Max TAXI | 9889 kg |
| Max RTE RSV | 49986 kg |
| Max RTE RSV % | 15 % |
| Max JTSN GW | 512000 kg |
| Max ALTN fuel | 289982 kg |
| Max FINAL fuel | 100018 kg |
| Max MIN DEST FOB | 250000 kg |

### 5.2 Lookup tables [FBW] (`SH/OperatingSpeeds.tsx:26-319`)

**Interpolation.** Every table is an MSFS-SDK `LerpLookupTable`: a full grid with linear interpolation in each
dimension, evaluated in key order (outer dimension first). Keys outside the grid are **clamped** to the nearest edge
(no extrapolation).

**[PORT]** Implement `lerp1(keys, values, n, x)` with clamping, and `lerp2(k1[], k2[], v[][], a, b)` that
interpolates `b` along each row and then `a` across rows. The test vectors in §12.1 validate the semantics.

Weight keys `W` (kg) for every table: 250000, 300000, 350000, 400000, 450000, 500000, 550000, 600000.

**VLS approach tables** `VLS_APPR_CONF[conf](cg, W)`, with CG keys 29 and 43 %MAC:

| conf | CG | 250t | 300t | 350t | 400t | 450t | 500t | 550t | 600t |
|---|---|---|---|---|---|---|---|---|---|
| 1 (CONF 1) | 29 | 128 | 140 | 151 | 162 | 172 | 182 | 191 | 200 |
| 1 | 43 | 126 | 137 | 148 | 158 | 168 | 177 | 186 | 195 |
| 2 (CONF 1+F) | 29 | 123 | 133 | 144 | 154 | 163 | 172 | 180 | 188 |
| 2 | 43 | 123 | 130 | 140 | 150 | 159 | 167 | 176 | 183 |
| 3 (CONF 2) | 29 | 123 | 128 | 138 | 148 | 157 | 165 | 173 | 181 |
| 3 | 43 | 123 | 125 | 135 | 144 | 153 | 161 | 169 | 176 |
| 4 (CONF 3) | 29 | 123 | 124 | 134 | 144 | 152 | 161 | 169 | 176 |
| 4 | 43 | 123 | 123 | 131 | 140 | 148 | 156 | 164 | 171 |
| 5 (CONF FULL) | 29 | 123 | 123 | 131 | 140 | 148 | 157 | 165 | 173 |
| 5 | 43 | 123 | 123 | 127 | 136 | 144 | 152 | 160 | 168 |

`getApproachVls(conf, cg, m)`: `conf == 0` → `VLS_CONF_0(0 ft, m)`; `conf > 5` is an error.

**VLS_CONF_0(alt, W)** (clean):

| alt ft | 250t | 300t | 350t | 400t | 450t | 500t | 550t | 600t |
|---|---|---|---|---|---|---|---|---|
| 0 | 149 | 163 | 176 | 188 | 199 | 210 | 220 | 231 |
| 10000 | 149 | 163 | 176 | 189 | 202 | 216 | 231 | 243 |
| 15000 | 149 | 163 | 178 | 192 | 209 | 222 | 234 | 246 |
| 20000 | 149 | 165 | 183 | 199 | 212 | 226 | 239 | 252 |
| 25000 | 151 | 171 | 187 | 202 | 217 | 231 | 246 | 260 |
| 30000 | 158 | 174 | 191 | 207 | 224 | 239 | 256 | 270 |
| 32000 | 158 | 176 | 193 | 210 | 227 | 244 | 259 | 270 |
| 34000 | 159 | 178 | 196 | 213 | 231 | 247 | 259 | 270 |
| 36000 | 161 | 180 | 198 | 217 | 235 | 247 | 259 | 275 |
| 38000 | 162 | 182 | 202 | 222 | 235 | 247 | 259 | 275 |
| 40000 | 164 | 185 | 206 | 222 | 235 | 250 | 265 | 270 |
| 43000 | 168 | 191 | 208 | 223 | 235 | 250 | 250 | 250 |

This FMS table differs from the PRIM-FE clean table already in the port (`pfd_clean_vls`,
`a380x_pfd_inputs.inc:111-125`):

| Row | Column | FMS | PRIM |
|---|---|---|---|
| 15000 ft | 400 t | 192 | 193 |
| 38000 ft | 550 t | 259 | 262 |
| 38000 ft | 600 t | 275 | 280 |

Keep **both**: FMS speeds for FMS logic, PRIM speeds for the PFD.

**GREEN_DOT(alt, W):**

| alt ft | 250t | 300t | 350t | 400t | 450t | 500t | 550t | 600t |
|---|---|---|---|---|---|---|---|---|
| 10000 | 160 | 175 | 190 | 205 | 216 | 228 | 239 | 252 |
| 20000 | 161 | 178 | 196 | 219 | 237 | 256 | 267 | 277 |
| 30000 | 186 | 208 | 224 | 241 | 256 | 272 | 284 | 292 |
| 40000 | 203 | 220 | 236 | 252 | 252 | 260 | 260 | 260 |

**F2_SPEED(cg, W)** and **F3_SPEED(cg, W):**

| Table | CG | 250t | 300t | 350t | 400t | 450t | 500t | 550t | 600t |
|---|---|---|---|---|---|---|---|---|---|
| F2 | 29 | 144 | 150 | 162 | 173 | 184 | 191 | 191 | 191 |
| F2 | 43 | 144 | 146 | 158 | 169 | 179 | 189 | 191 | 191 |
| F3 | 29 | 133 | 133 | 143 | 153 | 163 | 171 | 177 | 177 |
| F3 | 43 | 133 | 133 | 139 | 149 | 158 | 167 | 175 | 177 |

**S_SPEED(W)** is one-dimensional, weight only:

| 250t | 300t | 350t | 400t | 450t | 500t | 550t | 600t |
|---|---|---|---|---|---|---|---|
| 146 | 160 | 173 | 185 | 196 | 207 | 215 | 215 |

**VMCA / VMCG(pressure alt)** (`:322-348`) are **A320 values** (FIXME upstream). They are used only by the disabled
T.O SPEED TOO LOW check:

| alt | −2000 | 0 | 2000 | 4000 | 6000 | 8000 | 10000 | 12000 | 14100 | 15100 |
|---|---|---|---|---|---|---|---|---|---|---|
| VMCA | 115 | 114 | 114 | 113 | 112 | 109 | 106 | 103 | 99 | 97 |
| VMCG | 117 | 116 | 116 | 115 | 114 | 112 | 109 | 106 | 102 | 101 |

### 5.3 `A380OperatingSpeeds(m, cg, cas, fPos, phase, v2, alt, wind = 0, ignoreSpoilers = false)` [FBW] (`:437-504`)

`fPos` is the flap lever 0..4 (0, 1, 2, 3, FULL). The vfe array is `[340, 263, 222, 220, 196, 182]`.

```c
/* 1. configuration VLS / VMAX / VFE-next */
if (fPos == 0)                     { vls = VLS0(alt, m);        vmax = vmo_mmo(); vfeN = 263; }
else if (fPos == 1 && cas > 212)   { vls = VLSAPP(1, cg, m);    vmax = 263;       vfeN = 222; }   /* CONF 1 */
else                               { vls = VLSAPP(fPos+1, cg, m); vmax = vfe[fPos+1];
                                     vfeN = (fPos == 4) ? 0 : vfe[fPos+2]; }
/* 2. values derived from the raw table VLS */
vapp = vls + clamp(round(wind/3), 5, 15);      /* wind = headwind kt; uses raw VLS */
vref = VLSAPP(5 /*FULL*/, cg, m);
gd   = GREEN_DOT(alt, m);
vs1g = vls / 1.23;
vls  = max(vls, 123 /*VMCL*/);
if (phase <= TAKEOFF)            vls = max(1.15*vs1g, 1.05*min(v2, 123));
else if (fPos == 1 && cas > 212) vls = max(1.18*vs1g, 123);
/* 3. speed-brake increment (skipped if ignoreSpoilers) */
if (spoilers > 0 && !ignoreSpoilers) {
    conf = (fPos == 0) ? 0 : (fPos == 1) ? (cas > 212 ? 1 : 2) : fPos + 1;
    vls += INC[conf] * (spoilers / MAXEXT[conf]);
}       /* MAXEXT = {20,20,12,9,8,6} deg, INC = {25,25,7,10,10,8} kt */
/* 4. retraction speeds */
vs1g0  = VLS0(alt, m) / 1.23;
vs1g1F = VLSAPP(1, cg, m) / 1.23;      /* named "1F" upstream but uses the CONF 1 table */
f2 = (phase <= TAKEOFF) ? max(1.18*vs1g1F, 128) : F2(cg, m);
f3 = (phase <= TAKEOFF) ? max(1.18*vs1g1F, 128) : F3(cg, m);
s  = (phase <= TAKEOFF) ? 1.21*vs1g0 : S(m);
```

- `vmo_mmo() = min(340, machToCas(0.89, p))`, where `p` is the ADR corrected average static pressure in hPa
  (1013.25 if unavailable).
- `machToCas(M, p) = 1479.1·sqrt( ((p/1013.25)·((0.2M²+1)^3.5 − 1) + 1)^(1/3.5) − 1 )` (`fbw-common` `MathUtils.ts:321-326`).
- `spoilers` is `L:A32NX_LEFT_SPOILER_1_COMMANDED_POSITION` in degrees.
  - **[PORT]** Use `spoilers/MAXEXT[conf] = clamp(pfd_aircraft.spoiler_ratio, 0, 1)` (X-Plane
    `sim/flightmodel2/controls/speedbrake_ratio`).

### 5.4 Current speeds, every 250 ms [FBW] (FAI.ts:1154-1249)

- Inputs:

  | Input | Source |
  |---|---|
  | `GW` | `getGrossWeightKg() ?? (373000 + FOB·1000)` |
  | `CG` | `getGrossWeightCg() ?? 35` |
  | `alt` | ADR pressure altitude. The block below only runs if it is valid. |
  | `cas` | `navigation.getComputedAirspeed() ?? 0` |
  | `fPos` | `L:A32NX_FLAPS_HANDLE_INDEX` |
  | `v2` | `getV2Speed()` = plan V2 or **150** |
  | `towerHeadwind` | `getHeadwind(appWindMag, appWindDir, destRwy.magneticBearing)` if all are set, else 0 |

- `speeds = A380OperatingSpeeds(GW, CG, cas, fPos, phase, v2, alt, towerHeadwind)` (spoilers included).
- Results:

  | Output | Value |
  |---|---|
  | VS (`L:A32NX_SPEEDS_VS`) | `round(vs1g)` |
  | VLS (`L:A32NX_SPEEDS_VLS`) | `round(vls)` |
  | VMAX | `round(vmax)` |
  | VFEN | `round(vfeN)` |
  | **F** in PREFLIGHT | `ceil(max(f2, 128))` |
  | **F** in other phases | lever 2: `ceil(max(f2, 138))`; lever 3: `ceil(max(f3, 133))`; other levers keep the previous F |
  | **S** | `ceil(s)` |
  | **GD** | `ceil(gd)` |

  → `fmgc.data.flapRetractionSpeed`, `slatRetractionSpeed`, `greenDotSpeed` and the L:vars in §9.1.

### 5.5 Approach speeds and VAPP [FBW] (FAI.ts:1156-1208)

1. `LW = getLandingWeight()`. If phase ≥ APPROACH **or** LW is null, use `LW = GW`. This follows FCOM bulletin
   46/2 per the source comment.
2. `ldgCG = CG` (GW CG or 35).
3. `appr = A380OperatingSpeeds(LW, ldgCG, 0, conf3 ? 3 : 4, APPROACH, v2, destElevation, towerHeadwind,
   ignoreSpoilers = true)`.
   - `destElevation` = destination runway threshold altitude, else airport altitude, else 0.
4. Outputs:

   | Field | Value |
   |---|---|
   | `approachVls` | `ceil(appr.vls)` |
   | `approachVref` | `ceil(appr.vref)` (always the FULL table) |
   | `approachGreenDotSpeed` | `ceil(appr.gd)` (GD at destination elevation) |
   | `approachSlatRetractionSpeed` | `ceil(appr.s)` = `ceil(S(LW))` |
   | `approachFlapRetractionSpeed` | `ceil(appr.f2)` = `ceil(F2(cg, LW))` |
   | `approachVapp` | `pilotVapp ?? ceil(appr.vapp)` |

5. If LW is null, all six are null.

**VAPP:**

- `VAPP = VLS_table(conf, cg, LW) + clamp(round(towerHeadwind/3), 5, 15)`. The +5 kt minimum applies with no wind.
- `getHeadwind(v, a, b) = v·cos(Δ)`, with `Δ = 180 − ||a − b| − 180|` (degrees). Crosswind uses `b + 90`
  (`OperatingSpeeds.tsx:389-391, 524-526`; FMC.ts:474-489).

**Ground-speed mini** [FBW utility, used by the PRIM side] (`:534-556`):

- `windDiff = round((currentHeadwind − towerHeadwind)/3)`
- `Vtarget = max(VAPP, min(round(VAPP + windDiff), round(lever == FULL ? VMAX − 5 : VFEN)))`

**[PORT]** The port has no PRIM approach-speed logic. When phase == APPROACH and speed is managed:

- The managed target is `getAppManagedSpeed()` (§5.7), i.e. GD, S or F by lever, and VAPP in the landing
  configuration.
- The GS-mini formula needs a current-headwind source (open question O-4, §13). Until one exists, use plain VAPP.
  This deviates from FBW, where FBW's PRIM applies GS mini from VAPP and `A380X_FM_APPROACH_HEADWIND_COMPONENT`.

### 5.6 ECON / managed speed laws [FBW] (`fmgc.ts:275-420`)

| Phase | Speed | Mach |
|---|---|---|
| Climb | CI null → 250; else `290·(1−d) + 330·d`, `d = (CI/999)²` | 0.84 (fixed) |
| Cruise | **310** (fixed; CI law commented out) | **0.85** (fixed) |
| Descent | `pilotManagedDescentSpeed` ?? (CI null → 300; else `round(288·(1−CI/999) + 300·CI/999)`) | `pilotManagedDescentMach` ?? 0.84 |

**Speed limit:** `climbSpeedLimit` / `descentSpeedLimit` = {speed, underAltitude}, or null if either is 0 or null.
Defaults are 250 kt below 10000 ft.

### 5.7 Managed speed target, every 250 ms [FBW] (FAI.ts:985-1098)

1. `updateHoldingSpeed()` (§5.8) and `clearCheckSpeedModeMessage()`.
2. If `holdDecelReached`, `vPfd = holdSpeedTarget`.
3. Otherwise, clear SET HOLD SPD if active and select by phase:

   | Phase | `vPfd` |
   |---|---|
   | PREFLIGHT, TAKEOFF | V2 (null if not entered) |
   | CLIMB | `s = min(ECON_CLB, limit if indicated alt < limitAlt, speedConstraint)`. **EO:** if OP CLB or CLB is active and GD is known, `s = cas − (cas − GD)·0.25` (ramps toward GD; 0.25 = 250 ms/1 s). Then `[vPfd, isMach] = managedTargets(s, 0.84)`. |
   | CRUISE | `s = min(310, climb limit if below limitAlt)`; `managedTargets(s, 0.85)` |
   | DESCENT | `vPfd = L:A32NX_SPEEDS_MANAGED_PFD` (written by VNAV); `isMach` from `managedTargets(ECON_DES, MachDES)` |
   | APPROACH | `approachVapp` |
   | GOAROUND | `min(GD ?? ∞, speedConstraint)` |

4. `managedTargets(v, M)` (FAI.ts:1441-1456): if ADR SAT and static pressure are valid, `vM = machToCas(M, p)`.
   Return `[vM, true]` if `v > vM`, else `[v, false]`. If invalid, return `[v, false]`.
5. Write `L:A32NX_SPEEDS_MANAGED_PFD = vPfd ?? 0`.
6. When `isMach` changes, send `K:AP_MANAGED_SPEED_IN_MACH_ON` / `_OFF`.

`speedConstraint` (FAI.ts:1459-1502):

- ∞ unless the lateral mode is managed (NAV, LOC CPT, LOC TRACK, LAND, FLARE or ROLLOUT).
- Climb or GA: `managedProfile[activeLeg].climbSpeed`.
- Descent:
  - `descentSpeed` if the leg distance-to-go < `decelDist(min(prevDescentSpeed, ECON_DES), descentSpeed)`
  - otherwise `previousDescentSpeed`
- `decelDist(a, b) = clamp((a − b)·0.15, 3, 20)` NM.

**`managedProfile`** (FAI.ts:1506-1603, rebuilt on every plan version change), over the legs before the missed
approach:

- **Forward pass (descent):** the running min of DES-leg speed constraints → `descentSpeed`. The previous value
  → `previousDescentSpeed`.
- **Backward pass:** the running min of CLB-leg speed constraints → `climbSpeed`. The CLB altitude constraint
  `@`, `-` or `B` (alt1) → running-min `climbAltitude`. The DES constraint `@` or `+` (alt1), or `B` (alt2) →
  running-max `descentAltitude`, floored at destination elevation.

**Approach managed speed by lever** (`getAppManagedSpeed`, FAI.ts:1083-1098; used by the holding logic):

| Lever | Speed |
|---|---|
| 0 | GD |
| 1 | S |
| 2 | F |
| 3 | `conf3 ? VAPP : F` |
| 4 | VAPP |

### 5.8 Holding speed [FBW] (FAI.ts:837-956, flagged "big hack until VNAV can do this")

- `getHoldingSpeed(cstrSpd, cstrAlt)`:
  1. `alt = max(FCU alt, cstrAlt)`.
  2. `k = GD (+25 if not all engines on)`. In APPROACH, `k = getAppManagedSpeed()`.
  3. If `cstrSpd > 100`, `k = min(k, cstrSpd)`.
  4. ICAO cap:

     | alt | cap |
     |---|---|
     | < 14000 | min(230, k) |
     | < 20000 | min(240, k) |
     | < 34000 | min(265, k) |
     | otherwise | `k = machToCas(alt, 0.83)` |

  5. Apply the climb limit (phase ≤ CRUISE) or the descent limit (phase < GOAROUND) if `alt ≤ limitAlt`.
  6. `k = max(k, VLS)` and return `ceil(k)`.
- `updateHoldingSpeed`:
  - **Active leg is HM:** target = holding speed, `holdDecelReached = true`, warning when speed is selected.
  - **Next leg is HM:** compute the target. Set `holdDecelReached` when DTG < `decelDist(CAS, target)`. Warn when
    speed is selected and DTG ≤ `decelDist + GS/120`.
  - Write `L:A32NX_FM_HOLD_DECEL` and `L:A32NX_FM_HOLD_SPEED`.
  - "SET HOLD SPD" is shown when the warning is enabled and `CAS − target > 5`. It drives
    `L:A32NX_PFD_MSG_SET_HOLD_SPEED`.

### 5.9 Engine-out [FBW]

**Detection** (FAI.ts:1726-1735):

- Pulse on the rising edge of `isFlying && !isAllEngineOn` (all 4 engines N2 > 20). If `engineOut` is false, set it
  true.
- Clear it when all engines are on, or with PERF CLEAR EO.

**EO entry** (FMC.ts:1685-1719):

1. Clear the climb and cruise presels.
2. Delete the cruise steps at or after the active leg. If any were deleted, add "STEP DELETED".
3. Navigate the captain MFD to PERF.

**[PORT]** Navigate both MFDs.

### 5.10 Takeoff speed checks [FBW] (FAI.ts:569-655)

| Check | Logic | Output |
|---|---|---|
| `toSpeedsNotInserted` | Any of V1/VR/V2 null or 0 | DISCRETE_WORD_3 bit 18 |
| `vSpeedDisagree` | `!(V1 ≤ VR && VR ≤ V2 && V1 ≤ V2)`; each pair is only checked when both are set | Bit 16. On the rising edge: "V1/VR/V2 DISAGREE" (resolves when valid). |
| `toSpeedsTooLow` | **[FBW-PH] hard-coded false** (`:635`) | Bit 17 |

`getToSpeedsTooLow()` exists but is unused:

- `zp` = pressure altitude at the departure elevation (or the ADR pressure altitude).
- Too low when `V1 < trunc(VMCG(zp))`, `VR < trunc(1.05·VMCA(zp))` or `V2 < trunc(1.1·VMCA(zp))`.
- The tables are the A320 ones in §5.2.

**[PORT]** Keep it disabled. Expose the `discrete_word_3` bits as booleans for a future FWS.

---

## 6. Altitudes and levels

### 6.1 THR RED / ACC / EO ACC defaults [FBW] (`FlightPlan.ts:467-589`)

**On `setOriginAirport`**, with `ref` = origin airport elevation:

- `defaultThrustReductionAltitude = ref + CONFIG_THR_RED_ALT` (default 1500)
- `defaultAccelerationAltitude = ref + CONFIG_ACCEL_ALT` (1500)
- `defaultEngineOutAccelerationAltitude = ref + CONFIG_ENG_OUT_ACCEL_ALT` (1500)
- With no airport, all three are null. The three pilot values are cleared.
- `databaseTransitionAltitude = airport.transitionAltitude ?? null`

**On `setDestinationAirport`**, the same rule with the destination elevation sets the three **missed** defaults
and clears the three missed pilot values.

- `databaseTransitionLevel = destAirport.transitionLevel ?? null` (FL)
- Descent wind entries are deleted.

**Takeoff/GA fallbacks:** §2.2 (indicated altitude + 1500 at phase entry when the value is still null).

**Constraint reconciliation** (`FlightPlan.ts:662-712`; FAI.ts:454-474; runs every 100 ms):

- `lowestClimbConstraint = round10(min over legs before the missed approach of maxAlt(CLB constraint))`.
- If THR RED > that value: `default = min(default, lc)` and `pilot = min(pilot, lc)`. Add
  `"NEW THR RED ALT: <alt>"`, with `<alt>` = the reduced value printed as an integer.
- The same rule applies to ACC with `"NEW ACCEL ALT: <alt>"`.

**Outputs** (FAI.ts:476-521):

| Phase | THR_RED_ALT / ACC_ALT / EO_ACC_ALT words carry |
|---|---|
| ≤ TAKEOFF | thrustReduction / acceleration / engineOutAcceleration |
| GOAROUND | missed thrustReduction / missed acceleration / missed EO acceleration |
| otherwise | NCD |

These are 17-bit BNR words, range 131072 ft.

**[PORT]**

- Use the same defaults with config keys `thr_red_alt_agl`, `accel_alt_agl` and `eo_accel_alt_agl` (all 1500)
  in the port config file.
- Airport elevation: from the airport navaid (`XPLMFindNavAid(..., xplm_Nav_Airport)` + `XPLMGetNavAidInfo`
  `outHeight`; verify the unit) or from the apt.dat header.

### 6.2 Transition altitude / level source [FBW → PORT]

- **[FBW]** The source is the MSFS navigation database (`Airport.transitionAltitude` for the origin,
  `Airport.transitionLevel` for the destination). If the database has none, the value stays null (dashes) until
  the pilot enters it.
- **[PORT]** Parse X-Plane `apt.dat` 1302 metadata rows:
  - `1302 transition_alt <ft>`
  - `1302 transition_level <ft>`. The value is in **feet** (e.g. `18000`); divide by 100 for FL.
  - Verified present in the installed X-Plane 12 `Global Scenery/Global Airports/Earth nav data/apt.dat`, with
    12905 airports carrying `transition_alt`.
  - Index on a worker thread once at plugin start (the file is about 380 MB). Read Custom Scenery `apt.dat` first
    so overrides win.
  - Do **not** use `sim/cockpit2/radios/indicators/fms_transition_alt/_level`; they belong to the stock FMS
    plan.
- **Outputs** (FAI.ts:523-549):
  - `TRANS_ALT` word: ft, 17 bits, range 131072.
  - `TRANS_LVL` word: FL, 9 bits, range 512.
  - Each is NCD when null. Consumer: PFD altimeter STD/QNH reminder.

### 6.3 FG altitude constraint [FBW] (FAI.ts:1281-1313)

Computed only if the PRIM selected altitude is valid:

| Condition | Constraint |
|---|---|
| phase < CRUISE or GOAROUND, and `climbAltitude(active leg)` is finite and < FCU alt | `climbAltitude` |
| CRUISE < phase < GOAROUND, and `descentAltitude` is finite and > FCU alt | `descentAltitude` |
| otherwise | 0 |

Write `L:A32NX_FG_ALTITUDE_CONSTRAINT` on change.

### 6.4 Cruise FL logic [FBW]

**`trySetCruiseFl(fl, plan)`** (FMC.ts:1804-1849):

1. Non-finite → FORMAT ERROR. `fl ≤ 0` or `fl > 430` → ENTRY OUT OF RANGE. Either returns false.
2. If the plan is Active and CLIMB ≤ phase < DONE, let `fcuFl = floor(max(0, FCU alt)/100)`. If `fl < fcuFl`,
   insert `fcuFl` instead. In CRUISE, DESCENT or APPROACH this also flags "NEW CRZ ALT".
3. Set `cruiseFlightLevel`. If the inserted FL > rec max FL, add "CRZ FL ABOVE MAX FL".
4. For Active, run `onUpdateCruiseLevel(fl)`:
   - `L:A32NX_AIRLINER_CRUISE_ALTITUDE = fl·100`
   - `updateConstraints()`
   - phase rule "new CRZ FL" (§2.1)
5. If flagged, add `"NEW CRZ ALT: <fl·100>"`.

**FCU-driven change** (FAI.ts:1372-1413):

1. Any valid change of the PRIM selected altitude arms the check and calls `handleFcuAltKnobTurn` (§2.1).
2. On the next 250 ms cycle, test: CLIMB with `fcuFL > crzFL`, or CRUISE with `fcuFL ≠ crzFL`.
3. The FG vertical mode must be CLB, DES, OP CLB, OP DES, VS or FPA.
4. When that holds for 3 s (confirm node): `crzFL = fcuFL`, `CRUISE_ALTITUDE = fcuAlt`, and add
   `"NEW CRZ ALT: <alt>"`.

**Step climb/descent** (`onStepClimbDescent`, FAI.ts:1315-1343). Called on OP/managed vertical mode activation,
and on VS/FPA when descent cannot be initiated.

- In CLIMB with FCU FL > CRZ FL, or CRUISE with FCU FL ≠ CRZ FL:
  1. Delete the cruise steps between the old and new level. Each deletion removes "STEP AHEAD".
  2. Add "NEW CRZ ALT".
  3. Set CRZ FL and `CRUISE_ALTITUDE`.

**Step ahead** (FAI.ts:1607-1640):

- When the next non-ignored cruise step is < 20 NM ahead, add "STEP AHEAD" once per step altitude.
- If `AUTO_STEP_CLIMB == ENABLED`, NAV is engaged, and the vertical mode is not selected: send
  `K:A32NX.FCU_ALT_SET = step alt`, then `K:A32NX.FCU_ALT_PUSH` 500 ms later.
- **[PORT]** Implement this as a call to a new guidance command `A380X_FCU_ALT_SET(value)` followed by
  `A380X_FCU_ALT_PUSH`. Default the setting to DISABLED.

---

## 7. Messages

### 7.1 Queue semantics [FBW] (FMC.ts:1136-1181; `pages/common/Footer.tsx:22-43`; `pages/FMS/MfdMsgList.tsx:36-60`)

**Message entry:** `{message, messageText, backgroundColor, cleared, isResolved(), onClear()}`.

- `messageText` = template text, plus `"\n" + details` when details exist.
- `backgroundColor` = amber if `isAmber`, else white. Cyan is never used.

**Add:**

1. If an entry with the same text exists **and is cleared**, remove it.
2. Insert the new entry at index 0 (newest first).
   - An identical uncleared entry is **not** deduplicated upstream (§11).

**Remove by text:** removes the first entry whose `messageText` equals the given text exactly.

**Cyclic resolution, every 250 ms:** every Type II entry whose `isResolved()` returns true is removed.

**CLEAR INFO** (KCCU CLRINFO key, or the footer button when it reads "CLEAR\nINFO"):

- Take the first entry with `!cleared`.
- **Type I:** remove it.
- **Type II:** set `cleared = true` and call `onClear()`. It remains in the MSG LIST until resolved or removed.

**Footer:** shows the first `!cleared` entry.

| Entry colour | Background |
|---|---|
| white | `#ffffff` |
| amber | `#e68000` |
| cyan | `#00ffff` |

The footer button reads "CLEAR\nINFO" while a message is shown, else "MSG\nLIST".

**MSG LIST:** shows the first **4** entries of the array (cleared or not), newest first.

**Shared queue:** one queue serves **both** MFDs (`master.fmsErrors`).

### 7.2 Catalogue [FBW] (`MFD/shared/NXSystemMessages.ts:80-151` + triggers)

Type: **I** = cleared by CLR; **II** = persistent/resolvable. Colour: A = amber, W = white.

| Text (verbatim) | T | C | Trigger | Removal / resolution |
|---|---|---|---|---|
| `FORMAT ERROR` (+ `\n` + detail) | I | W | Entry parse; `trySetCruiseFl` non-finite | CLR |
| `ENTRY OUT OF RANGE` (+ detail) | I | W | Entry parse; CRZ FL ≤ 0 or > 430 | CLR |
| `NOT IN DATABASE` | I | W | Airways/FIX INFO/POSITION MONITOR lookups | CLR |
| `NOT ALLOWED` | I | W | Airways entry; POSITION/NAVAIDS deselect logic | CLR |
| `NO INTERSECTION FOUND` | I | W | Airway intersection | CLR |
| `XXXX IS DESELECTED` (XXXX = ident) | I | W | Navaid deselect | CLR |
| `SQWK CODE NOT VALID` | I | W | SURV XPDR entry | CLR |
| `LRC MODE IN USE` | I | W | §2.6 LRC default | CLR |
| `NOT YET IMPLEMENTED` | I | W | FmsErrorType NotYetImplemented | CLR |
| `NO NAVIGRAPH USER` | I | W | CPNY F-PLN request without a user | CLR |
| `CHECK T.O. DATA` | II | A | (a) SEC activated in PREFLIGHT/DONE; (b) PACKS or ANTI ICE changed after engine start; (c) departure runway changed with V-speeds set (resolves when VR is set) | CLR (Type II) / resolution (c) |
| `CHECK ZFW` | II | A | SEC activation with ΔZFW > 5 t and ΔCG > 0.5 % | Next Active ZFW change |
| `INITIALIZE ZFW / ZFWCG` | II | A | Engine 2 or 3 starting (`L:A32NX_ENGINE_STATE:n == 2`) in phase < TAKEOFF, then any engine N2 > 20 with ZFW or ZFWCG null. Shown once per power-up (`zfwInitDisplayed < 1`). | ZFW and ZFWCG both set |
| `ENTER DEST DATA` | II | A | Entering DESCENT or APPROACH, or in CRUISE when DEST < 180 NM (checked every 15 s, once per cruise), with any of QNH/TEMP/WIND missing | All four entered |
| `CHECK MIN FUEL AT DEST` | II | W | Pilot MIN DEST FOB < ALTN + FINAL | Condition false |
| `DEST EFOB BELOW MIN` | II | A | §4.7 | Condition false |
| `NEW CRZ ALT: HHHHH` (HHHHH = ft) | II | W | §6.4 | CLR |
| `CRZ FL ABOVE MAX FL` | II | W | §6.4 | CLR |
| `STEP ABOVE MAX FL` | II | W | VERT REV step entry | CLR |
| `STEP AHEAD` | II | W | §6.4 | CLR; step deletion |
| `STEP DELETED` | II | W | Engine-out step deletion; plan auto-delete event | CLR |
| `NEW ACCEL ALT: HHHHH` / `NEW THR RED ALT: HHHHH` | II | W | §6.1 reconciliation | CLR |
| `CONSTRAINTS BEFORE WWWWW : DELETED` | II | W | Entering GO AROUND | CLR |
| `CHECK SPD MODE` | II | W | Entering DESCENT with speed selected and no presel. Sets `L:A32NX_PFD_MSG_CHECK_SPEED_MODE = 1`. | Removed when speed becomes managed (var → 0). `onClear` sets the var to 0. |
| `SET HOLD SPD` | II | W | §5.8; `L:A32NX_PFD_MSG_SET_HOLD_SPEED` | Warning ends |
| `T/D REACHED` | II | W | VNAV sets `L:A32NX_PFD_MSG_TD_REACHED` | Var false; `onClear` sets the var false |
| `SPD LIMIT EXCEEDED` | II | A | CLB/CRZ (climb limit) or DES (descent limit): ADR alt < limitAlt − 150 **and** CAS > limit + 10 | Alt > limitAlt, or CAS ≤ limit + 5, or ADR invalid |
| `V1/VR/V2 DISAGREE` | II | A | §5.10 | Valid again |
| `T.O SPEED TOO LOW - CHECK TOW & T.O DATA` | II | A | **Disabled** | – |
| `TOO STEEP PATH AHEAD` | II | W | VNAV | VNAV condition false |
| `LATERAL DISCONTINUITY AHEAD` | II | A | VNAV `shouldShowLatDiscontinuityAhead` | Condition false |
| `NAV PRIMARY` / `NAV PRIMARY LOST` | II | W / A | GPS primary edge (`fms_nav_gps_primary`); also ND flags (§7.3) | Opposite edge; NAV PRIMARY `onClear` removes the ND flag |
| `COMPANY F-PLN RECEIVED`↵`WAITING FOR INSERTION` | II | W | Uplink available | Inserted/deleted |
| `RECEIVED COMPANY F-PLN NOT VALID` | II | W | Uplink failure | CLR |

Defined but never triggered by the A380 MFD/FMC at this pin:

- `AIRWAY / WPT DISAGREE`
- `CANCEL AUTO UPDATE FIRST`
- `COM DATALINK NOT AVAIL`
- `DATABASE CODING ERROR`
- `F-PLN ELEMENT RETAINED`
- `NOT ALLOWED IN NAV`
- `RUNWAY / LS DISAGREE` (A)

**[PORT]**

- Use a fixed ring of 16 entries `{id, text[64], amber, type2, cleared, resolve_fn}`, newest first.
- Deduplicate identical uncleared text as well (this fixes §11-4).
- Resolution functions are enum-switched in the core; no closures.
- Pages call `fms_msg_push(id, detail)`.

### 7.3 ND FM message flags [FBW] (FMC.ts:1753-1775; `SH/FmMessages.ts:4-16`)

Per side, write `L:A32NX_EFIS_{L|R}_ND_FM_MESSAGE_FLAGS` (bitmask) on change:

| Bit | Message |
|---|---|
| 0 | SELECT TRUE NORTH REF |
| 1 | CHECK NORTH REF |
| 2 | NAV ACCUR DOWNGRAD |
| 3 | NAV ACCUR UPGRADED |
| 4 | SPECIF VOR-D NOT AVAIL |
| 5 | NavAccuracyUpgradeGps |
| 6 | NAV PRIMARY |
| 7 | MAP PARTLY DISPLAYED |
| 8 | OFFSIDE FM CONTROL |
| 9 | NAV PRIMARY LOST |

The A380 FMC only sets bits 6 and 9, on both sides.

**[PORT]** Publish `a380x/xp12/fms/nd_fm_message_flags_l/_r`. The port ND currently draws no FM messages.

---

## 8. FMS1 / FMS2, FMC-A/B/C and the two MFDs

### 8.1 [FBW] (`FmcService.ts:27-79`; `MFD.tsx:105-124`)

FMC power and inop logic:

| FMC | Power bus | Inop when |
|---|---|---|
| FMC-A | DC ESS | `!powered \|\| L:A32NX_RESET_PANEL_FMC_A \|\| failure FmcA` |
| FMC-B | DC 1 | same pattern |
| FMC-C | DC 2 | same pattern |

Outputs: `L:A32NX_FMC_{A,B,C}_IS_HEALTHY = !inop`.

**FMS switching knob** `L:A32NX_FMS_SWITCHING_KNOB`: 0 = BOTH ON 2, 1 = NORM, 2 = BOTH ON 1.

- `FMS_L_FAILED = knob == 0 ? (B && C inop) : (A && C inop)`
- `FMS_R_FAILED = knob == 2 ? (A && C inop) : (B && C inop)`

MFD header source label:

| Knob | CAPT MFD | FO MFD |
|---|---|---|
| 0 | FO source | FO source |
| 1 | captain source | FO source |
| 2 | captain source | captain source |

- Captain source text = `FMS 1` if FMC-A is healthy, else `FMS 1-C`.
- FO source text = `FMS 2` if FMC-B is healthy, else `FMS 2-C`.

- **Only FMC-A computes.** Both MFDs read and write the same flight plans, perf data, message queue and FmgcData.
  There is no FMS1/FMS2 data synchronisation (FIXME, FMC.ts:310).
- Every ARINC word is written to **both** `L:A32NX_FM1_<name>` and `L:A32NX_FM2_<name>` with identical content
  (`fbw-common .../shared/src/arinc429.ts:392-404`).
- `L:A32NX_FM1/FM2_HEALTHY_DISCRETE = !allFmcInop` (FMC.ts:377-381).
- Per-side items:

  | Item | Per side |
  |---|---|
  | UI page | own URI, KCCU focus |
  | `EfisInterface` plan centre | L / R |
  | ND FM flags | L / R |
  | QNH display format | inHg / hPa from FCU EFIS discrete word 2 bit 13 |
  | `MfdUiService` | own instance |

- `updateEfisPlanCentre` only follows the captain MFD (`mfdReference`).
- FWS consistency (`FwsCore.ts:4035-4043`) uses different buses: B ↔ DC2 and C ↔ DC1, the opposite of
  `FmcService`. See §11.

### 8.2 [PORT]

- One `A380XFmsState` (FMC-A) shared by both MFD UI contexts `mfd[2]`, each with its own page, scroll and KCCU
  focus.
- Publish `fmc_a/b/c_healthy`, `fms_l_failed` and `fms_r_failed`.
- Inputs:
  - Power = `sim/cockpit2/switches/avionics_power_on` until the port has DC bus modelling.
  - Failures and reset buttons = new writable datarefs `a380x/xp12/fms/fmc_{a,b,c}_failed` and
    `a380x/xp12/fms/fmc_{a,b,c}_reset` (default 0).
  - Switching knob = `a380x/xp12/fms/switching_knob` (default 1).
- When all three FMCs are inop or reset, run the `reset()` of §9.1 once and freeze the outputs (all `valid = 0`).
- FMC-initiated navigation (DONE, EO) applies to both MFDs.

---

## 9. Complete FBW output and input catalogue

### 9.1 L:vars, events and sim vars written by the FMC [FBW]

`initSimVars()` (FMC.ts:773-798) runs at construction, at DONE and on reset. It writes:

| Var | Init value |
|---|---|
| `SPEEDS_MANAGED_PFD` | −1 |
| `MachPreselVal`, `SpeedPreselVal` | −1 |
| `AIRLINER_DECISION_HEIGHT` | −1 |
| `AIRLINER_MINIMUM_DESCENT_ALTITUDE` | 0 |
| `FG_ALTITUDE_CONSTRAINT` | 0 |
| `CABIN_READY` | 0 |
| `FM_GROSS_WEIGHT` | 0 |
| `PFD_MSG_SET_HOLD_SPEED` | 0 |
| `AIRLINER_V1_SPEED` | −1 |
| `AIRLINER_V2_SPEED` | 0 |
| `AIRLINER_VR_SPEED` | −1 |
| `A32NX_AIRLINER_CRUISE_ALTITUDE` | −1 |

It also sends `K:A32NX.ATHR_RESET_DISABLE` if A/THR is disabled.

| Var / event | Unit, encoding | Written | A380X consumers |
|---|---|---|---|
| `L:A32NX_FMGC_FLIGHT_PHASE` | enum 0–7 | On change (FPM.ts:206) | PFD, MFD, PRIM (`FlyByWireInterface.cpp:347`), GPWS, light sync |
| `L:AIRLINER_FLIGHT_PHASE` | 0 or phase + 1 | On change | legacy |
| `L:AIRLINER_V1_SPEED` | kt; −1 = none | 250 ms (FAI.ts:564) + immediate on entry | PFD, FWS phases (V1 + 3 kt memory; **no V1 → "above V1" stays false**, `FwsFlightPhases.ts:203-209`) |
| `L:AIRLINER_VR_SPEED` | kt; −1 | 250 ms + entry | PFD |
| `L:AIRLINER_V2_SPEED` | kt; **0 = none** | 250 ms + entry | PRIM (SRS / managed takeoff) |
| `L:A32NX_AIRLINER_TO_FLEX_TEMP` | °C; **0 = no FLEX, 0.1 = FLEX 0 °C** | §3.5, §2.3; zeroed on APPR/GA/DONE init | EWD, PFD, FWS (`!= 0` → FLEX takeoff), PRIM |
| `L:A32NX_AIRLINER_CRUISE_ALTITUDE` | ft; −1 init | §2.2, §6.4 | Phase manager, PRIM |
| `L:A32NX_SPEEDS_MANAGED_PFD` | kt; 0 = none (−1 init) | 250 ms | PRIM managed target; VNAV writes it in DES |
| `L:A32NX_SpeedPreselVal` / `L:A32NX_MachPreselVal` | kt / M; −1 = none | 200 ms after arm | PRIM |
| `K:A32NX.FMS_PRESET_SPD_ACTIVATE` | event | CLB/CRZ entry with presel | PRIM |
| `K:AP_MANAGED_SPEED_IN_MACH_ON` / `_OFF` | events | `isMach` change | FCU/PRIM |
| `L:A32NX_SPEEDS_GD` / `_S` / `_F` | kt (ceil); 0 = none | On change | FMS-internal: EO takeoff → climb, holding speed |
| `L:A32NX_SPEEDS_VS` / `_VLS` / `_VMAX` / `_VFEN` | kt (round) | On change | FMS-internal (holding floor, GS mini). **PFD uses PRIM values, not these.** |
| `L:A32NX_SPEEDS_VAPP` | kt; 0 = none | On change | PRIM (`FlyByWireInterface.cpp:349`) |
| `L:A380X_FM_LANDING_CONF3` | bool | On change | PRIM, FWS memos/checklists, GPWS |
| `L:A380X_FM_APPROACH_HEADWIND_COMPONENT` | Raw ARINC word as a string (float32 kt + SSM) | On change | PRIM (GS mini) |
| `L:A32NX_FG_ALTITUDE_CONSTRAINT` | ft; 0 = none | On change | PRIM, FWS |
| `L:A32NX_FM_GROSS_WEIGHT` | kg; 0 = none | 250 ms | (none found in A380X) |
| `L:A32NX_FMS_PAX_NUMBER` | count | PAX entry | (air cond.) |
| `L:A32NX_PRESS_AUTO_LANDING_ELEVATION` | ft (0 if none) | Plan change | Pressurisation (legacy path) |
| `L:A32NX_FM_LS_COURSE` | °; −1 = none | 250 ms | PFD, ND, PRIM |
| `L:A32NX_FM_HOLD_DECEL` / `L:A32NX_FM_HOLD_SPEED` | bool / kt | On change | VNAV, PRIM |
| `L:A32NX_PFD_MSG_CHECK_SPEED_MODE` / `_SET_HOLD_SPEED` / `_TD_REACHED` | bool | §7.2 | PFD messages |
| `L:A380X_FMS_DEST_EFOB_BELOW_MIN` | bool | On change | `FmsMessagePublisher` |
| `L:A32NX_EFIS_{L,R}_ND_FM_MESSAGE_FLAGS` | bitmask | On change | ND |
| `L:A32NX_FMC_{A,B,C}_IS_HEALTHY`, `L:A32NX_FMS_{L,R}_FAILED`, `L:A32NX_FM{1,2}_HEALTHY_DISCRETE` | bool | On change | MFD, FWS, PFD |
| `L:A32NX_COLD_AND_DARK_SPAWN` | bool | DONE (1), TAKEOFF init (0) | FMS |
| `L:A32NX_CABIN_READY` | 0 on every phase change | – | cabin |
| `L:A32NX_GOAROUND_INIT_SPEED` | kt | GA entry | PRIM |
| `L:A32NX_DESTINATION_QNH` | hPa | APPR QNH entry | (none in A380X) |
| `L:AIRLINER_DECISION_HEIGHT` | ft; −1 none; 0 NO DH | APPR RADIO entry | PFD (legacy publisher) |
| `L:AIRLINER_MINIMUM_DESCENT_ALTITUDE` | ft | APPR BARO entry | PFD (legacy publisher) |
| `ATC FLIGHT NUMBER` (sim var) | string | Flight number change | third party |
| `K:A32NX.FCU_ALT_SET`, `K:A32NX.FCU_ALT_PUSH` | ft / bool | Auto step climb | FCU |

### 9.2 ARINC 429 output words [FBW] (FAI.ts:66-116)

Each word is written as `L:A32NX_FM1_<NAME>` and `L:A32NX_FM2_<NAME>`, only when dirty.

**Encoding:**

- The L:var holds a double: low 32 bits = IEEE float32 of the value; `SSM·2³²` added on top.
- SSM values: 0 = FW, 1 = NCD, 2 = FT, 3 = NO.
- BNR quantisation: `quantum = max(|min|, max)/2^bits`; `value = clamp(round(v/quantum)·quantum, min, max)`
  (`arinc429.ts:329-338`).
- **[PORT]** Do not emulate ARINC. Publish `value` + `valid` pairs. The table defines the content and validity.

| Word | Content | Bits / range | Valid (NO) when | Consumers |
|---|---|---|---|---|
| `DISCRETE_WORD_2` | Bits 13/14/15/16 = T.O CONF 0/1/2/3 (the MFD can only produce 1..3) | – | Always NO | FWS T.O config (`FwsCore.ts:4024-4033`), PRIM |
| `DISCRETE_WORD_3` | Bit 16 V-speed disagree, 17 too low (always 0), 18 not inserted | – | Always NO | FWS T.O SPEEDS NOT INSERTED / DISAGREE / TOO LOW (`FwsCore.ts:3990-4022`) |
| `TO_PITCH_TRIM` | THS FOR **%MAC** value | 12 / ±180 | NO when set; the word is not written while null | FWS pitch-trim/MCDU-CG disagree: \|FM − FQMS CG\| ≥ 1 % or trim out of the cyan band (`FwsCore.ts:4819-4834`) |
| `LANDING_ELEVATION` | DEST runway threshold alt, else airport alt (ft) | 14 / [−2048, 16384] | Always NO (bug: null → 0, §11) | PFD, SD PRESS, FWS, CPIOM-B |
| `DEST_LAT` / `DEST_LONG` | Threshold (else airport) position, ° | 18 / ±180 | Always NO | – |
| `MINIMUM_DESCENT_ALTITUDE` | MDA ft | 17 / 131072 | In range **and** MDA set | PFD |
| `DECISION_HEIGHT` | DH ft | 16 / 8192 | In range **and** MDA **not** set **and** DH > 0 | PFD |
| `EIS_DISCRETE_WORD_2` | Bit 29 = NO DH (in range **and** DH == 0) | – | Always NO | PFD |
| `THR_RED_ALT`, `ACC_ALT`, `EO_ACC_ALT` | §6.1 | 17 / 131072 | Value set and phase ≤ TAKEOFF or GOAROUND | PRIM (`FlyByWireInterface.cpp:357-360`), FMGC TAKEOFF phase |
| `TRANS_ALT` / `TRANS_LVL` | ft / FL | 17 / 131072; 9 / 512 | Value set | PFD |
| `ZERO_FUEL_WEIGHT` | kg | 19 / 524288 | ZFW set | CPIOM-F fuel (`a380_systems/src/fuel/cpiom_f`) |
| `ZERO_FUEL_WEIGHT_CG` | %MAC | 12 / 64 | ZFWCG set | Fuel (CG control) |
| `REMAINING_FLIGHT_TIME` | s to DEST | 17 / 131072 | Prediction exists | – |
| `FLIGHT_NUMBER_1..5` | ISO-5 pairs of `flightNumber[0..1]`, `[2..3]`, `[4..5]`, `[6..7]`, `[9]` (sic) | – | NO | ATC/ATSU |

"In range" for minimums (FAI.ts:778-783): `phase > CRUISE`, **or** `phase == CRUISE` and (DEST distance < 250 NM
or unknown).

### 9.3 Event-bus outputs [FBW]

| Topic(s) | Content | Source |
|---|---|---|
| `fmgc_flight_phase` | flight phase | – |
| `fmsOrigin`, `fmsDepartureRunway`, `fmsDestination`, `fmsLandingRunway`, `fmsAlternate`, `fmsFlightNumber` | idents/strings | FAI.ts:365-373, 785-805 |
| `a32nx_fms_vertical_target_profile`, `…_constraints`, `…_actual_profile`, `…_descent_profile`, `a32nx_fms_vd_track_change_distance` | Vertical display data | FMC.ts:1585-1683 |
| EfisSymbols / EfisInterface | ND map symbols and plan centre | – |

### 9.4 FMC inputs [FBW] and port equivalents

| FBW input | Used for | Port equivalent |
|---|---|---|
| `L:A32NX_ENGINE_N2:1..4` | engines started / on | `sim/cockpit2/engine/indicators/N2_percent[0..3]` |
| `L:A32NX_ENGINE_N1:1,2` | T/O condition | `n1_values[0..1]` |
| `L:A32NX_ENGINE_STATE:2,3` == 2 (starting) | INITIALIZE ZFW | `sim/flightmodel2/engines/starter_is_running[1,2]` |
| FQMS GW / FOB / CG | §4.1 | `m_total` / `m_fuel_total` / `cg_offset_z_mac` |
| `L:A32NX_AIRFRAME_ZFW`, `…_ZFW_CG_PERCENT_MAC` | ZFW auto-init | `m_total − m_fuel_total`; CG none |
| `L:A32NX_COLD_AND_DARK_SPAWN` | auto-init gate | Port spawn detection (engines running at load) |
| ADR CAS / baro alt / pressure alt / SAT / static pressure (ADIRU 1 → 3 fallback, `MFD/shared/Adirs.ts:85-98`) | speeds, limits, Mach | `guidance_inputs.ias_knots`, `.altitude_ft`, `.pressure_altitude_ft`; `sim/weather/aircraft/temperature_ambient_deg_c`; `sim/weather/aircraft/barometer_current_pas`/100 |
| IR lat/long, GS | hold logic | `sim/flightmodel/position/latitude/longitude`, `ground_speed_knots` |
| RA 1/2/3 (only in APPR/GA) | DEST EFOB message | `refs.side[0].radio_altitude_value` |
| SFCC flap lever (status word bits 18–21) / `L:A32NX_FLAPS_HANDLE_INDEX` | conf | `pfd_aircraft.flap_handle_index` (0..4, same semantics) |
| `L:A32NX_LEFT_SPOILER_1_COMMANDED_POSITION` | VLS increment | `pfd_aircraft.spoiler_ratio` |
| PRIM selected altitude | CRZ FL logic, constraints | `guidance.selected_altitude_ft` |
| PRIM FG word 3 (b11 CLB, b12 DES, b13 OP CLB, b14 OP DES, b17 VS, b18 FPA, b20 ALT, b24 FLARE) | §2.1, §5.7, §6.4 | `guidance.vertical_mode` (`A380X_B1_*`) |
| PRIM FG word 4 (b12 NAV, b13 LOC CPT, b14 LOC TRK, b16/17 HDG/TRK, b25 LAND, b26 ROLLOUT) | managed lateral, SEC swap | `guidance.lateral_mode` (`A380X_C1_*`) |
| PRIM FG word 5 (b17 speed managed, b18 speed selected) | CHECK SPD MODE, hold | `guidance.speed_managed` |
| FCU EFIS L/R discrete word 2 bit 13 (inHg) | QNH format | Port EFIS CP baro unit state (glareshield) |
| MMR course / frequency, `NAV LOCALIZER:3` | `FM_LS_COURSE` | `guidance_inputs.ils[i].course_deg` when `loc_valid` |
| `fms_nav_gps_primary` | NAV PRIMARY messages | `!sim/operation/failures/rel_gps` (≥ 6 = failed) **and** a position is available |
| `NXDataStore` `CONFIG_THR_RED_ALT`, `CONFIG_ACCEL_ALT`, `CONFIG_ENG_OUT_ACCEL_ALT` (1500), `AUTO_STEP_CLIMB` (DISABLED), `CONFIG_USING_METRIC_UNIT` | defaults | Port config file |

---

## 10. Mapping onto the existing X-Plane port

### 10.1 Facts about the current port (read from source)

- **Phase:** `published.flight_phase` (`a380x_avionics.c:105, 282`; dataref `a380x/xp12/avionics/flight_phase`) is
  the **FWS** phase 1..12 from `a380x_flight_phase_update` (`a380x_avionics_core.c:183-221`).
- **V1:** `phase_inputs.configured_v1 = 145.0` is hard-coded (`a380x_avionics.c:281`). The core uses
  `fmax(v1, 1)`, so a missing V1 would make "above V1" true at about 4 kt. FBW's FWS instead holds "above V1"
  false when V1 ≤ 0.
- **PFD performance speeds:** `A380XPfdGuidance` (`a380x_pfd_data.h:8-34`) carries `vmax_knots`, `vls_knots`,
  `v1_knots`, `vr_knots`, `green_dot_knots`, `vfe_next_knots`, `f_speed_knots`, `s_speed_knots`, …. They are
  filled by `read_pfd_aircraft()` (`a380x_pfd_inputs.inc:172-254`) and copied by `publish_pfd_aircraft()`
  (`:255-269`).
  - **Gated inputs.** V1, VR, F, S, VLS, alpha speeds, stall speed and a provided VMAX come from the input datarefs
    `a380x/xp12/systems/pfd/{vls,alpha_prot,alpha_limit,stall_warning,f_speed,s_speed,green_dot,v1,vr,vmax}_knots`.
    They are accepted only if `a380x/xp12/systems/pfd/envelope_valid == 1` and the value is within [40, 450]
    (`:232-242`). **No producer registers these datarefs** (`docs/PFD_IMPLEMENTATION.md:64-69`), so V1/VR/F/S
    are currently always 0 and not drawn.
  - **Green dot** is computed natively: `pfd_green_dot(guidance_inputs.weight_kg,
    guidance_inputs.pressure_altitude_ft)` (`:86-101, 230`). It uses the same GREEN_DOT table as the FMS
    (§5.2), but it returns 0 outside 250–600 t (FBW clamps) and is not `ceil`ed.
  - **Clean VLS** is the PRIM-FE table (`:102-135, 247-253`). VMAX and VFE-next are native (`:174-229`).
- **Published PFD datarefs:** `a380x/xp12/pfd/{v1_knots, vr_knots, f_speed_knots, s_speed_knots, green_dot_knots,
  vls_knots, vmax_knots, vfe_next_knots, …}` are read-only accessors over `pfd_guidance` (`:47-69`). They are
  outputs, not inputs.
- **PFD drawing:** V1 ("1") and VR (circle) are drawn only when `published.flight_phase <= 5` (FWS phase) and the
  value is > 0 (`a380x_pfd_render.inc:746-758`). Green dot needs `flap_config == 0` and airborne (`:759-762`);
  F/S labels are drawn when > 0 (`:729-735`).
- **Decision height:** `Side.decision_height_value` is read from
  `sim/cockpit2/gauges/actuators/radio_altimeter_bug_ft_pilot|_copilot` (`a380x_avionics.c:169-170, 219`).
  - The PFD treats any finite value ≥ 0 as a valid DH and draws "DH" when RA ≤ DH (`a380x_pfd_render.inc:1025-1030`).
  - There is no MDA, no NO DH and no "in range" gating.
- **Managed guidance:** `A380XGuidanceInputs.managed_speed_valid`, `managed_route_valid`, `managed_vertical_valid`,
  `managed_speed_knots`, `managed_heading_deg`, `managed_altitude_ft`, `managed_vs_fpm`, `route_*`
  (`a380x_guidance.h:28-32`) are **never set**. `read_guidance_inputs` memsets them to 0
  (`a380x_fcu_adapter.inc:158`), so every managed request is rejected (`a380x_guidance.c:112-140, 259-266`).
  - The speed target shown on the PFD is `guidance.speed_target_knots` (`a380x_fcu_adapter.inc:279`). It equals
    `managed_speed_knots` when speed is managed (`a380x_guidance.c:267-269`).
- **No V2 field and no SRS** (`a380x_guidance.h:58`).
- **ND route:** read from X-Plane's stock FMS via the legacy API `XPLMCountFMSEntries` / `XPLMGetFMSEntryInfo`
  every 0.5 s (`a380x_efis_nd.inc:70-85, 216-218`).
- **EWD FLEX:** displayed only if `a380x/xp12/engine/thrust_limit_type == 3` (FLX) and the optional input
  `a380x/xp12/engine/flex_temperature_c` is non-zero and ≥ SAT − 10 (`a380x_ewd_data.inc:86, 384-388`). That
  input is not registered by anyone.
- **Weight:** `guidance_inputs.weight_kg = sim/flightmodel/weight/m_total` (`a380x_fcu_adapter.inc:179, 351`).

### 10.2 Field-by-field mapping

"Feed" = what the native FMS writes. A new **`A380XFmsOutputs fms_out`** global (in `a380x_fms_adapter.inc`) is
the single source; existing structs are filled from it.

| Port interface | Currently sourced from | Native FMS feed | Change required |
|---|---|---|---|
| `phase_inputs.configured_v1` (`a380x_avionics.c:281`) | Constant 145.0 | `fms_out.v1_knots` (Active plan V1) | Assign from FMS. Change `a380x_flight_phase_update` so that `configured_v1 <= 0` → `above_v1 = 0` (FBW `FwsFlightPhases.ts:203-209`). Update `test_avionics_core.c`. |
| `pfd_guidance.v1_knots` → `a380x/xp12/pfd/v1_knots` | Provider dataref, gated by `envelope_valid` | `fms_out.v1_knots` (0 = none) | Set `pfd_aircraft.v1_knots` from the FMS in `read_pfd_aircraft()` **independently of `envelope_valid`**. V-speeds are FMS data, not envelope data. Keep the provider path as an override only if valid. |
| `pfd_guidance.vr_knots` → `a380x/xp12/pfd/vr_knots` | same | `fms_out.vr_knots` | same |
| *(none)* V2 | – | `fms_out.v2_knots` | Add `v2_knots` to `A380XPfdGuidance` and the published list. V2 is the managed target in PREFLIGHT/TAKEOFF (below). |
| `pfd_guidance.f_speed_knots`, `s_speed_knots` | Provider (none) | Option A (recommended interim): `fms_out.f_speed_knots` / `s_speed_knots` (§5.4) | FBW draws PRIM `v_3`/`v_4` here, not FMS F/S. Until a native PRIM-FE exists, feed the FMS values and add a code comment that the source is the FMS. Never invent values. |
| `pfd_guidance.green_dot_knots` | Native `pfd_green_dot()` | `fms_out.green_dot_knots` = `ceil(GREEN_DOT(alt, GW))` | Either keep the native function (same table) or replace it with the FMS value. If kept, apply FBW clamping and `ceil` for consistency with the MFD T.O tab. |
| `pfd_guidance.vls_knots` | PRIM clean table / provider | **Do not feed** FMS VLS | FBW's PFD uses PRIM `v_ls`. The FMS VLS (§5.3) is for FMS logic only. |
| `pfd_guidance.vmax_knots`, `vfe_next_knots` | Native | **Do not feed** | Same constants; already correct. |
| `Side.decision_height_value` (`radio_altimeter_bug_ft_*`) | X-Plane RA bug | `fms_out.dh_ft` (−1 none, 0 NO DH, >0 ft), `fms_out.mda_ft`, `fms_out.minimums_in_range` | Recommended: the PFD reads the FMS values. "DH" logic: valid only if `minimums_in_range && dh_ft > 0 && mda not set`. NO DH is a separate flag. Fallback: the FMS **writes** both writable RA-bug datarefs with `dh_ft` (or −1 when not valid/NO DH). This needs no PFD change but loses NO DH/MDA. |
| `guidance_inputs.managed_speed_valid` / `managed_speed_knots` | Never set (0) | `fms_out.managed_speed_valid = (phase target non-null && FMS healthy)`, `managed_speed_knots = vPfd` (§5.7). For Mach targets pass CAS = `machToCas(M, p)` as FBW does. | Copy in `read_guidance_inputs()` after the memset. Add `managed_speed_is_mach` and `managed_mach` to `A380XGuidanceInputs` so the FCU/PFD can show Mach (replaces `K:AP_MANAGED_SPEED_IN_MACH_ON/OFF`). |
| `guidance_inputs.managed_altitude_ft` | Never set | `fms_out.fg_altitude_constraint_ft` (0 = none, §6.3) as the constraint part | Full `managed_vertical_valid` needs VNAV (out of scope). Until then keep `managed_vertical_valid = 0`. |
| `guidance_inputs.managed_route_valid`, `managed_heading_deg`, `route_*` | Never set | LNAV (out of scope) | – |
| Preselected speed | No field | `fms_out.presel_speed_knots` / `presel_mach` (−1 none) + pulse `presel_activate` | Add a guidance command `A380X_FCU_SPD_PRESEL_ACTIVATE` for the CLB/CRZ entry (§2.2). |
| THR RED / ACC / EO ACC | No field | `fms_out.thr_red_alt_ft`, `acc_alt_ft`, `eo_acc_alt_ft`, each + `valid` (§6.1 phase rule) | Future SRS/THR RED logic. The FMS uses them for TAKEOFF → CLIMB now. |
| CRZ altitude | No field | `fms_out.cruise_altitude_ft` (−1 none) | Used by the FMS phase logic; publish for the PRIM port. |
| FLEX | `a380x/xp12/engine/flex_temperature_c` consumer contract, unregistered | `fms_out.to_flex_temp_c` with FBW encoding (0 none, 0.1 = 0 °C) + `fms_out.to_power_setting` | FBW's EWD reads the FMS var directly (`EWD/shared/EwdSimvarPublisher.tsx`). Recommended: register `a380x/xp12/fms/to_flex_temp_c` and change `ER_FLEX` in `a380x_ewd_data.inc:86` to read it. The FLX rating mode itself still comes from a FADEC producer. |
| FMGC phase | Not available | `fms_out.flight_phase` 0..7 → `a380x/xp12/fms/flight_phase` | New. **Do not overwrite** `published.flight_phase` (FWS). |
| GW for display/speeds | `guidance_inputs.weight_kg` | FMS uses the same source as FQMS GW | No change |
| ND route (`a380x_efis_nd.inc:70-85`) | Stock X-Plane FMS legacy entries | Active (or TMPY) plan legs | Either (a) the adapter mirrors the active plan into the stock FMS with `XPLMSetFMSFlightPlanEntryInfo` / `…LatLonWithId` on `xplm_Fpl_Pilot_Primary` (XPLM 4.1+, SDK 4.3 present). This keeps the ND untouched but loses leg types. Or (b) replace `efis_nd_read_route()` with a copy from the native plan (recommended long-term). |
| `pfd_aircraft.flap_handle_index`, `spoiler_ratio` | Native | – (FMS **input**) | FMS reads them for §5.3/§5.4 |
| `guidance.selected_altitude_ft`, `vertical_mode`, `lateral_mode`, `speed_managed` | Guidance | – (FMS **input**) | FMS reads them (§9.4) |
| FCU commands `a380x/xp12/fcu/altitude_*`, `vertical_speed_*` | `fcu_command_handler` | – (FMS **input** events) | After `a380x_guidance_command`, call `fms_on_fcu_*` (§2.1) |

### 10.3 Proposed `a380x/xp12/fms/*` datarefs (read-only unless stated)

All floats. `valid` companions are ints (0/1). "None" encodings follow FBW where FBW has one.

| Dataref | Unit / encoding |
|---|---|
| `flight_phase` | int 0..7 |
| `v1_knots`, `vr_knots`, `v2_knots` | kt; 0 = none |
| `to_flex_temp_c` | °C; 0 none; 0.1 = 0 °C |
| `to_power_setting` | int 0 TOGA, 1 FLEX, 2 DERATED |
| `to_flaps` | int 0..3; −1 none |
| `to_ths_for_pct` | %MAC; NaN none |
| `to_speeds_not_inserted`, `vspeeds_disagree`, `to_speeds_too_low` | int |
| `thr_red_alt_ft`, `acc_alt_ft`, `eo_acc_alt_ft` + `_valid` | ft |
| `trans_alt_ft`, `trans_level_fl` + `_valid` | ft / FL |
| `cruise_altitude_ft` | ft; −1 none |
| `fg_altitude_constraint_ft` | ft; 0 none |
| `managed_speed_knots`, `managed_speed_is_mach`, `managed_mach` + `managed_speed_valid` | kt / int / M |
| `presel_speed_knots`, `presel_mach` | −1 none |
| `green_dot_knots`, `f_speed_knots`, `s_speed_knots`, `vls_knots` (FMS) | kt; 0 none |
| `approach_vapp_knots`, `approach_vls_knots`, `approach_vref_knots` | kt; 0 none |
| `approach_f_knots`, `approach_s_knots`, `approach_gd_knots` | kt; 0 none |
| `landing_conf3` | int |
| `approach_headwind_knots` + `_valid` | kt |
| `dh_ft` | −1 none, 0 NO DH |
| `mda_ft` | −1 none |
| `minimums_in_range` | int |
| `landing_elevation_ft`, `dest_lat_deg`, `dest_lon_deg` + `_valid` | ft / ° |
| `gross_weight_kg`, `zfw_kg`, `zfw_cg_pct` | NaN none |
| `dest_efob_below_min`, `msg_check_speed_mode`, `msg_set_hold_speed`, `msg_td_reached` | int |
| `nd_fm_message_flags_l`, `nd_fm_message_flags_r` | int bitmask |
| `fmc_a_healthy`, `fmc_b_healthy`, `fmc_c_healthy`, `fms_l_failed`, `fms_r_failed` | int |
| **Writable:** `switching_knob` (0/1/2, default 1), `fmc_{a,b,c}_failed`, `fmc_{a,b,c}_reset` | int |

### 10.4 Integration order (suggested)

1. `a380x_fms_speeds.c` + tests (§12.1). This needs no X-Plane.
2. Core state, perf store, flight phase manager, message queue, entry functions. Pure C tests.
3. Adapter:
   - Inputs (§9.4) and outputs (§10.3).
   - Feed `configured_v1`, PFD V1/VR (decoupled from `envelope_valid`) and the managed speed target.
4. MFD pages bind to the core (other specs in this folder).
5. EWD FLEX binding, DH/MDA, ND route mirror.
6. Later: VNAV predictions (§4.8), LNAV/VNAV managed guidance.

---

## 11. FBW defects and quirks (decide deliberately; default = fix in the port)

| # | Quirk | Location | Port decision |
|---|---|---|---|
| 1 | `allFmcResetsPulled` checks FMC_B twice, never C | FMC.ts:1446-1448 | Fix (A && B && C) |
| 2 | FWS fault logic pairs FMC-B with DC2 and FMC-C with DC1; `FmcService` uses B = DC1, C = DC2 | `FwsCore.ts:4038-4040` vs `FmcService.ts:36-52` | Single definition (FmcService) |
| 3 | "NEW CRZ ALT" removal at DES/APPR uses the template text `NEW CRZ ALT: HHHHH`, which never matches | FMC.ts:1316, 1329 | Remove by message id |
| 4 | Identical uncleared messages are duplicated | FMC.ts:1156-1160 | Deduplicate |
| 5 | ALTN EFOB: tonnes passed as kg (displays 1000× too small) | `MfdFmsFuelLoad.tsx:431, 435` | Fix (t → kg) |
| 6 | CONFIRM T.O SPDs never shown (`pd.v1 !== undefined` is always true) | `MfdFmsPerf.tsx:311` | Fix (§3.6) |
| 7 | T.O SPEED TOO LOW disabled; VMCA/VMCG are A320 tables; `interpolateTable` returns `vmca[0]` below range for VMCG too and uses `lerpAngle` | FAI.ts:635; `OperatingSpeeds.tsx:322-348, 558-572` | Keep disabled |
| 8 | Flight number word 5 uses `substring(9,10)` and skips character 8 | FMC.ts:1023-1026 | Use `[8..9]` if implemented |
| 9 | `LANDING_ELEVATION`/`DEST_LAT/LONG` SSM test `!== undefined` against `null` → NO with value 0 when there is no destination | FAI.ts:711-737 | `valid = value set` |
| 10 | TOW and ALTN/FINAL are only computed while FUEL&LOAD is open | `MfdFmsFuelLoad.tsx:233-234` | Compute every 250 ms |
| 11 | The PRIM reads `FM1_MISSED_THR_RED_ALT`/`MISSED_ACC_ALT`, which the FMS never writes. The FMS puts GA values into `THR_RED_ALT`/`ACC_ALT` instead. | FAI.ts:491-495; `FlyByWireInterface.cpp:358, 360` | Publish both phase-switched and explicit missed values |
| 12 | Engine indices differ: `conditionTakeOff` uses N1 of engines 1 and 2; `flightphase.isAnEngineOn` uses N2 of 2 and 3; `fmgc.isAnEngineOn` uses 1–4 | `SH/flightphase.ts:30-55`; `fmgc.ts:518-534` | Keep as specified (phase logic) — documented |
| 13 | In-flight route reserve forced to 0 % at TAKEOFF, although AMI `rsvInflt = true` | FMC.ts:1260; `AirlineModifiableInformation.tsx:10` | Keep (FBW) |
| 14 | VREF always uses the FULL table; the takeoff-phase VLS uses `min(V2, VMCL)` | `OperatingSpeeds.tsx:462, 469` | Keep (FBW) |
| 15 | FMC-initiated navigation goes only to the captain MFD | `instrument.tsx:139-140` | Both MFDs |
| 16 | An unknown airport on INIT gives no NOT IN DATABASE (exception swallowed) | `OriginSegment.ts:45-49`; `MfdFmsInit.tsx:426-429` | Show NOT IN DATABASE |
| 17 | MSG LIST renders only 4 entries (the warning text says 5) | `MfdMsgList.tsx:39-56` | Keep 4 per page; scrolling optional |
| 18 | "CHECK ZFW" requires both ΔZFW > 5 t **and** ΔCG > 0.5 % | FMC.ts:950 | Keep (FBW) |
| 19 | The FLEX temperature entry writes `TO_FLEX_TEMP` even while TOGA is selected | `MfdFmsPerf.tsx:1643-1657` | Only write while FLEX is selected |
| 20 | `updatePerfSpeeds` runs twice per cycle | FMC.ts:1487, 1507 | Once is enough |

---

## 12. Test vectors

Vectors computed from §5.2/§5.3 with clamped multilinear interpolation (reproduced independently in Python).

### 12.1 Speeds

| # | Inputs | Expected |
|---|---|---|
| A | PREFLIGHT, GW 500 t, CG 30 %, alt 0 ft, lever 0, V2 null (150), no spoilers | raw vls 196.341, vs1g 170.732 → VLS (T/O rule) `max(1.15·170.732, 1.05·123)` = 196.34 → **196**; f2 = `max(1.18·181.643/1.23, 128)` = 174.259 → **F 175**; s = 1.21·210/1.23 = 206.585 → **S 207**; **GD 228** (alt clamped to the 10000 row) |
| B | Approach speeds: LW 380 t, CG 35 %, CONF FULL, DEST elev 0, wind not entered | VLS 134.686 → **135**; VAPP 134.686 + 5 → **140**; VREF **135**; F = `F2(35, 380t)` = 166.886 → **167**; S = 180.2 → **181**; GD = 199.0 → **199** |
| C | As B, but CONF 3 with tower headwind 30 kt | VLS 138.457 → **139**; VAPP 138.457 + clamp(round(10), 5, 15) = 148.457 → **149** |
| D | CRUISE, GW 450 t, CG 38, FL350, lever 0 | VLS0(35000, 450t) = 233.0 → **233**; GD = 254.0 → **254** |
| E | CLIMB, GW 480 t, CG 33, 3000 ft, lever 2 | VLS 160.657 → **161**; F = `ceil(max(F2 = 187.286, 138))` = **188**; S = S(480t) = 202.6 → **203**; VMAX 220; VFEN 196 |
| F | CLIMB, GW 480 t, CG 33, lever 1, CAS 220 (CONF 1) | raw CONF 1 table VLS(33 %, 480 t) = 176.686; vs1g = 176.686/1.23 = 143.647; CONF 1 rule `max(1.18·vs1g, 123)` = 169.503 → **VLS 170**; VAPP (raw) = 181.686; VMAX 263; VFEN 222 |
| G | TAKEOFF, GW 480 t, CG 33, lever 2, V2 = 160 | raw CONF 2 VLS = 160.657; vs1g = 130.616 → VLS = `max(1.15·130.616 = 150.208, 1.05·min(160, 123) = 129.15)` → **150** |

Row F exercises the `else if (fPos == 1 && cas > 212)` branch (`OperatingSpeeds.tsx:470-471`). With CAS ≤ 212 the
same lever uses the CONF 1+F table instead (§5.3 step 1).

### 12.2 Altitudes

| GW kg | ΔISA | raw | REC MAX | FL | OPT | EO MAX |
|---|---|---|---|---|---|---|
| 500000 | 0 | 37461.92 | 37500 | 375 | 360 | 300 |
| 400000 | 0 | 41861.19 | 42000 | 420 | 400 | 335 |
| 560000 | +10 | 32448.87 | 32500 | 325 | 310 | 260 |
| 300000 | −5 | 45998.17 | 46000 | **430** (capped) | 410 | 340 |

### 12.3 Managed speeds and Mach

Climb and descent speeds by CI:

| CI | 0 | 100 | 300 | 500 | 999 |
|---|---|---|---|---|---|
| ECON CLB | 290.0 | 290.4 | 293.61 | 300.02 | 330.0 |
| ECON DES | 288 | 289 | 292 | 294 | 300 |

`machToCas`:

| Pressure | M 0.89 | M 0.85 | M 0.84 |
|---|---|---|---|
| 1013.25 hPa | 588.71 kt | 562.25 | 555.64 |
| 238.42 hPa (≈ FL350) | 306.37 kt | 290.92 | 287.10 |

At FL350 the climb target therefore switches to Mach 0.84 when `ECON_CLB > 287.1`.

### 12.4 Fuel

**Inputs:** ZFW 300 t, BLOCK 100 t, TAXI 1.5 t (default), engines off. VNAV TRIP = 60 t (supplied), RTE RSV 5 %,
ALTN exists, FINAL 30 min, no pilot MIN DEST FOB.

| Quantity | Result |
|---|---|
| TOW | (300 + 100 − 1.5)·1000 = **398500 kg** |
| LW | 398500 − 60000 = **338500 kg** |
| RTE RSV | 0.05·60000 = **3000 kg** |
| ALTN | **6500 kg** |
| FINAL | 30·0.2 t = **6000 kg** |
| MIN DEST FOB | **12500 kg** |
| EXTRA (PREFLIGHT) | 100000 − 1500 − 60000 − 12500 − 3000 = **23000 kg** |

**DEST EFOB BELOW MIN hysteresis** with MIN 12.5 t, as `destEFOB` moves:

| destEFOB | 12.6 t | 12.4 t | 12.7 t | 12.81 t |
|---|---|---|---|---|
| State | false | **true** | true (Δ = 0.2 ≤ 0.3) | **false** |

### 12.5 Headwind / VAPP

Wind 270°/20 kt, runway QFU 240°:

1. Δ = 180 − ||270 − 240| − 180| = 30°
2. HW = 20·cos30° = 17.32 kt
3. round(17.32/3) = 6, clamped to [5, 15] → **+6 kt** to VLS.

Crosswind uses bearing 330°: Δ = 60°, giving 10.0 kt.

---

## 13. Open questions

- **O-1** ZFWCG auto-initialisation needs a fuel-moment model (X-Plane only gives GW CG via `cg_offset_z_mac`).
  Until then, pilot entry is required.
- **O-2** The airport elevation unit returned by `XPLMGetNavAidInfo(outHeight)` must be verified (ft vs m) before
  the THR RED/ACC defaults are computed. Alternative: the apt.dat header elevation (ft).
- **O-3** VNAV destination predictions (TRIP/EFOB/ETA) are deferred. Phase 1 shows dashes exactly as FBW does
  without a prediction.
- **O-4** The GS-mini current headwind needs a wind source. Candidates, verified in the X-Plane 12 `DataRefs.txt`:
  `sim/weather/aircraft/wind_now_direction_degt` (° true, wind from) and `sim/weather/aircraft/wind_now_speed_msc`
  (m/s; × 1.94384 for kt), both at the aircraft's location. Project them on the runway QFU after converting
  magnetic to true with `sim/flightmodel/position/magnetic_variation`. The `wind_direction_degt` /
  `wind_speed_kts` arrays are per-layer `float[13]` and are not suitable. Implement only together with a PRIM-side
  approach speed law.
- **O-5** F/S on the PFD: an FMS-sourced interim value versus waiting for a native PRIM-FE (`v_3`/`v_4`) port.
- **O-6** DH presentation: the PFD reads the FMS (preferred) versus the FMS writing X-Plane RA-bug datarefs.
