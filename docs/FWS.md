# Native Flight Warning System (FWS)

The A380X port runs a C translation of the pinned FlyByWire A380X FWS
(`systems-host/CpiomC/FlightWarningSystem/`, revision in `native/avionics/a380x_fws_tables.json`).
It drives the lower EWD (memos, limitations, ECAM procedures, normal checklists, STS / FAILURE
PENDING), the MASTER WARN / CAUT lights and their aurals, and the engine attention getters.

| Part | Files | Ports | Doc |
| --- | --- | --- | --- |
| ECAM texts | `tools/build_fws_tables.py` -> `a380x_fws_tables.h` (+ `.json` manifest) | `EcamMessages/*` (996 abnormal procedures, 14 checklists, 132 memos, 34 limitations, 337 INOP SYS, 51 INFO) | `docs/FWS_ECL.md` |
| Line generator | `a380x_fws_lines.c/.h` | `ProcedureLinesGenerator.ts` | `docs/FWS_ECL.md` |
| ECL | `a380x_fws_ecl.c/.h` | `FwsNormalChecklists`, `FwsAbnormalSensed` (class), `FwsAbnormalNonSensed`, ECP keys, EWD `Wd*` composition | `docs/FWS_ECL.md` |
| Signals | `a380x_fws_signals.c/.h` | `FwsCore` signals, `FwsFlightPhases`, `FwsMemos`, `ewdAbnormalSensed` (126 FBW alerts + 74 port extensions + 5 deferred), `FwsLimitations`, `FwsInopSys`, `FwsInformation`, `FwsSystemDisplayLogic` | `docs/FWS_SIGNALS.md` |
| Core | `a380x_fws.c/.h` | `FwsCore.update()` alert life cycle (5468-6155) | this file |
| X-Plane boundary | `a380x_fws_adapter.inc` | inputs, ECP / master pb taps, EWD + glareshield outputs | this file |
| WD renderer | `a380x_ewd_wd.inc` | `FormattedFwcText`, `WdMemos`, `WdLimitations`, `EclLine` | `docs/EWD_DEVELOPMENT.md` |

## Cycle (each flight loop, while the EWD display unit is powered)

1. ECP keys are sampled (`fws_ecl_buttons`).
2. Signals update from `FwsInputs` (flight phase, memos, alert conditions, sensed items).
3. MASTER CAUT / WARN pushbutton effects.
4. `fws_ecl_clear_and_recall` (CLR / RCL), then the alert life cycle: new alerts (phase
   inhibition, 0.6 s monitor confirmation, `notActiveWhenItemActive`), presented / cleared /
   recalled procedure maps with sensed item fusion, deferred procedures, CRC and single-chime
   requests, master warning / caution latches, STS lists, ECAM SFAIL.
5. `fws_ecl_update` (normal / abnormal / non-sensed checklists, EWD orchestration).
6. `fws_ewd_view` fills `EwdData.wd`; the glareshield lights read the master outputs.

## Cockpit boundary

* Inputs: native A380 electrical network and ELEC panel, APU ECB, ADIRS, center-right brakes and
  gear, FCU guidance (AP / FD / A/THR, FMA), native FMS, and stock X-Plane datarefs (engines, fuel,
  hydraulics, bleed, pressurization, doors, lights, transponder, failures) - field by field in
  `docs/FWS_SIGNALS.md`. TLA uses the calibrated lever detents (`ewd_lever_tla`).
* ECP keys: pass-through taps on `a380x/xp12/ecam/sd/{clear_left, clear_right, recall, checklist,
  check_left, check_right, up, down, abnormal_procedures, to_config}`; the SD module's own placeholder
  checklist / alert list is disabled while the FWS runs.
* MASTER WARN / CAUT: taps on `a380x/xp12/glareshield/master_{warning,caution}_{left,right}`; the
  glareshield lamps follow the FWS outputs (non-cancellable warnings stay lit).
* AP / A/THR instinctive disconnect: taps on `a380x/xp12/fcu/ap_disconnect`, `athr_disconnect`.

## Port deviations (core)

* Activated abnormal NON-sensed procedures use FBW's default item vectors (all shown, none checked,
  all active), level 1, no SD page, no inhibition and no monitor delay.
* No CPIOM / FWS self-test delay: the FWS starts when the EWD display unit is powered.
* ECL deferred auto display: take-off power = any TLA >= 35 degrees.
* EMER CANC is not routed (FBW does not route it to the ECL either); aurals are the existing
  glareshield tones on light onset, not FBW's continuous CRC / single chime samples.
* Not yet connected: SD STATUS page contents and the SD automatic failure page (SFAIL is computed).

## Tests

`native/avionics/build.py` runs `tools/build_fws_tables.py --check`, compiles every FWS source with
MinGW warnings, and runs `test_fws_lines`, `test_fws_ecl`, `test_fws_signals` and `test_fws_core`
(life cycle over the real modules: memos, ENG 1 FIRE presented with MASTER WARN / CRC / SFAIL,
resolution, C/L checklists, unpowered reset).
