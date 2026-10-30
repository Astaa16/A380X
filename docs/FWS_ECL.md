# FWS electronic checklist (ECL) port

Native C11 (no XPLM dependency) port of the FlyByWire A380X electronic checklist logic. It covers the
normal checklists, sensed and non-sensed abnormal procedures, deferred procedures, and the lines they
produce on the EWD warning display. All references point to the pinned FBW revision
`2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2`. The manifest `native/avionics/a380x_fws_tables.json` records
it together with the SHA-256 of every parsed source file. The upstream checkout's `.git` points to an
unmounted volume, so the generator takes the revision from `docs/ATTRIBUTION.md`.

Abbreviations used below: `EM/` = `fbw-a380x/src/systems/instruments/src/MsfsAvionicsCommon/EcamMessages/`,
`FWS/` = `fbw-a380x/src/systems/systems-host/CpiomC/FlightWarningSystem/`,
`EWD/` = `fbw-a380x/src/systems/instruments/src/EWD/`.

## What was ported

| Native file | FBW source |
|---|---|
| `tools/build_fws_tables.py` → `native/avionics/a380x_fws_tables.h` + `.json` | All dictionaries in `EM/index.ts`: `EcamMemos`, `pfdMemoDisplay` (+`AThrOffMemoKey`), `EcamInfos`, `EcamLimitations`, `EcamInopSys`, `ChecklistLineStyle`, `DeferredProcedureType` (+`DEFERRED_PROCEDURE_TYPE_TO_STRING`), the spread-merged `EcamAbnormalSensedProcedures`/`EcamAbnormalProcedures` and `EcamDeferredProcedures` (every `EM/AbnormalSensed/*.ts`, secondary failures included), `EcamAbNormalSensedSubMenuVector`, `FMS_PRED_UNRELIABLE_CHECKLIST_ITEM`, `WD_*`. Also `EM/NormalProcedures.ts` (`EcamNormalProcedures`, `deferredProcedureIds`), `EM/AbnormalNonSensedProcedures.ts`, and `AbnormalNonSensedCategory` from `providers/FwsPublisher.ts`. `index.ts` has no ALERTS IMPACTING LDG PERF, REDUND LOSS or CANCELLED CAUTION dictionaries: those FwsEvents carry INOP SYS / INFO keys. `NormalProceduresBefore2020.ts` is not imported by FBW and is not parsed. |
| `a380x_fws_lines.{h,c}` | `EM/ProcedureLinesGenerator.ts` (whole file) and the item predicates of `EM/index.ts:685-717` |
| `a380x_fws_ecl.{h,c}` | `FWS/FwsNormalChecklists.ts:44-531`; `FWS/FwsAbnormalSensed.ts:92-339` (class only, not the `ewdAbnormalSensed`/`ewdDeferredProcs` dictionaries); `FWS/FwsAbnormalNonSensed.ts:15-146` (class only). From `FWS/FwsCore.ts`: ECP buttons (2868-2908, 2939-2947, 6210-6218), CLEAR AND RECALL (5511-5536), ECL orchestration and publishing (6155-6208), startup and 50-minute resets (2414-2452). EWD side: `EWD/elements/WdAbstractChecklistComponent.tsx` (windowing, selection, overflow arrows), `WdNormalChecklists.tsx`, `WdAbnormalSensedProcedures.tsx` (incl. FWS 1+2 FAULT fallback), `WdAbnormalNonSensed.tsx`, `WdCpiomFailedFallbackChecklistComponent.tsx`, and the visibility logic of `EWD/EWD.tsx:71-130`. |

## Tables (`a380x_fws_tables.h`)

The generator is `python3 tools/build_fws_tables.py`; `--check` exits with 1 if the header or manifest
is stale. It tokenizes and evaluates the TS object/array-literal subset FBW uses: comments, BOM,
trailing commas, `...X` spreads with JS last-wins semantics, quoted/numeric keys, all string escapes,
template literals with `'\xa0'.repeat(n)`, `+` concatenation, enum members, and identifiers across
imports. Anything else raises an error. It also checks that:
- every `AbnormalSensed` export is merged by `index.ts`;
- the `FwsLineStyle` order matches `ChecklistLineStyle` (the generated header also `_Static_assert`s it);
- no two keys of a table map to the same number;
- `FWS_MAX_ITEMS` holds the longest procedure (340900003 with 71 items);
- `FWS_TEXT_MAX` holds the longest line built without truncation.

- IDs are the unsigned value of the FBW key (`"000001001"` → `1001u`, limitation `"1"` → `1u`), with the
  original string in `key`.
- Arrays are sorted by id. Lookups use `static inline` binary search: `fws_memo_text`, `fws_info_text`,
  `fws_limitation_text`, `fws_inop_sys_text` (`*_find` returns the entry), `fws_abnormal_procedure`,
  `fws_deferred_procedure`, `fws_normal_procedure`, `fws_normal_procedure_index`,
  `fws_deferred_procedure_id_index`, `fws_is_pfd_memo`.
- Lists where FBW order matters keep that order: `fws_pfd_memo_display`, `fws_abn_non_sensed_overview`,
  `fws_abn_sub_menu_vector`, `fws_deferred_procedure_ids`.
- Strings are UTF-8 (NBSP → `"\xc2\xa0"`, `°` → `"\xc2\xb0"`) with the FWC escape codes kept. Hex escapes
  are split (`"\x1b" "4m"`) and `?` is escaped to avoid trigraphs.
- Item fields: `name`, `label_not_completed`/`label_completed` (`NULL` = undefined), `time` + `has_time`,
  `sensed`, `level`, `style` (`FwsLineStyle` or -1), and the tri-states `colon_if_completed`, `condition`,
  `append_time_if_elapsed` (-1 = undefined).
- Procedure fields: `title`, `sensed`, `recommendation`, `only_activated_by_request`, `deferred_type`,
  `items`, `from_abnormal_procs`.
- Census at the pinned revision: 132 memos, 51 infos, 34 limitations, 337 INOP SYS, 6 PFD memos,
  996 abnormal procedures (1668 items, 31 non-sensed), 8 deferred (28 items), 14 normal (42 items),
  31 non-sensed overview entries, 6 sub-menus.

The header is header-only, so every translation unit that references a table gets its own copy. Only
`a380x_fws_lines.c` touches the big abnormal and deferred tables; `a380x_fws_ecl.c` goes through
`fws_lines_find_procedure()`.

## Line generator (`a380x_fws_lines.h`)

`FwsProcLines` is one `new ProcedureLinesGenerator(...)`. It holds its own copy of the `ChecklistState`
(FBW only replaces it in `checkSelected()` or from the owner), `selected_item_index`, the recommendation
captured at construction, `is_last_procedure`, `procedure_is_active`, and the three FBW callbacks.
`selected_changed` is called on every real index change and implements `selectedItemIndex.pipe(...)`.
`timestamps_defined` distinguishes `itemsTimeStamp === undefined` (a timed condition then reads
`.AS <name> :`) from a null entry (`.IF <name>  AFTER 30 S :`).

```c
void fws_lines_init(FwsProcLines *, unsigned id, FwsProcedureType, const FwsChecklistState *,
                    int timestamps_defined, FwsRecommendation, int is_last_procedure);
int  fws_lines_to_line_data(const FwsProcLines *, FwsWdLine *out, int max);   /* FWS_LINES_MAX lines max */
void fws_lines_move_up(FwsProcLines *);  void fws_lines_move_down(FwsProcLines *, int skip_completed_sensed);
unsigned fws_lines_check_selected(FwsProcLines *);   /* FWS_LINES_EVENT_* = callbacks fired */
void fws_lines_select_first(FwsProcLines *);
int  fws_lines_first_line_is_selected / fws_lines_last_line_is_selected / fws_lines_num_lines_until_selected(const FwsProcLines *);
void fws_lines_conditional_active_items(proc, checked, active, timestamps|NULL, valid|NULL, to_show|NULL, now);
void fws_lines_checklist_action_text / fws_lines_checklist_condition_text(item, idx, complete, state, ts_defined, now, out, size);
```

## ECL controller (`a380x_fws_ecl.h`)

The core owns `FwsEclCoreLists`, which mirrors the FwsCore members that the ECL classes read and mutate:
- `presented_abnormal`, `cleared_abnormal`, `active_deferred`: MapSubjects stored as bounded arrays of
  `FwsEclProcEntry`, 32 entries each;
- `presented_failures`, `all_current_failures`, `recall_failures`, `active_abnormal_non_sensed`: key lists
  of up to 64 entries.

The core passes the struct by pointer to every call. Maps must be changed only through
`fws_ecl_map_set/delete/clear`, because their insertion `serial` and per-`setValue` `revision` let the
ECL replay FBW's map subscriptions. The ECL mutates the lists where FBW does:
- an item check writes `setValue`;
- CLEAR splices `presentedFailures` and rebuilds `recallFailures`;
- deactivating a non-sensed procedure deletes it from the active keys and from `allCurrentFailures`;
- RECALL moves an entry from the cleared map back to the presented map;
- ABN PROC CHECK toggles `activeAbnormalNonSensedKeys`.

Call order per FWS cycle (as in `FwsCore.update`):

```c
fws_ecl_buttons(&ecl, &buttons);                 /* L:A32NX_BTN_* levels -> rising-edge pulses */
ev  = fws_ecl_clear_and_recall(&ecl, &lists, &in);   /* FwsCore 5511-5536 */
/* core abnormal + deferred loop: fws_ecl_map_set/delete, entry->failure, entry->updated_items, ... */
ev |= fws_ecl_update(&ecl, &lists, &in);         /* normal/abnormal/non-sensed update + orchestration */
if (ev & FWS_ECL_EVENT_SD_STATUS_PAGE) /* show the STATUS page on the SD */;
fws_ecl_compose_wd(&ecl, &lists, &ewd_in, &wd);  /* EWD side, whenever it draws */
```

Also available: `fws_ecl_init`, `fws_ecl_startup_reset`, `fws_ecl_shutdown_reset` (FwsCore 2414-2452,
including clearing the core lists) and `fws_ecl_sorted_abnormal()` (EWD priority sort).

Outputs:
- `ecl.pub` (`FwsEclPublished`) holds the FwsEvents with bus semantics: a value stays until it is
  published again. It covers `fws_active_item`, `fws_active_procedure`, `fws_show_from_line`,
  `fws_show_normal_checklists`, `fws_normal_checklists_id`, `fws_show_abn_sensed`,
  `fws_show_abn_non_sensed`, `fws_abn_non_sensed_id`, `fws_abn_non_sensed_current_active` and
  `fws_show_failure_pending`.
- The published lists map as follows: `fws_normal_checklists` = `ecl.normal_state[]` (ascending ids),
  `fws_abn_sensed_procedures` = `fws_ecl_sorted_abnormal()`, `fws_deferred_procedures` =
  `lists.active_deferred`.
- `FwsEclWd` gives the active view (NONE, NORMAL, ABN_SENSED, ABN_NON_SENSED, CPIOM_FALLBACK), the
  18-line window `lines[]`, `line_selected[]` and `selected_line` (-1 if none), `show_from_line`,
  `total_lines`, `overflow_up`/`overflow_down`, `memos_limitations_visible`, `sts_area_visible`, and the
  21 CPIOM fallback lines with their visibility.
- `ecl.manual_checklist_reset` is `FwsCore.manualCheckListReset`, which the core's 50-minute reset reads.

### What the core must supply

- **Per presented abnormal entry**:
  - `state`: `procedureActivated: true` on insertion (FwsCore 5688-5696). `itemsChecked` is masked to
    sensed items, and `conditionalActiveItems` has already been applied.
  - `timestamps_defined` and `failure` (`ewdAbnormal[id].failure`).
  - Every cycle, `has_updated_items` and `updated_items[]` (`abnormalUpdatedItems`, cleared at the start
    of the core loop).
- **Per active deferred entry**: the state (`procedureActivated: false` and all `itemsActive` false on
  insertion, FwsCore 5698-5725), `has_which_items_active`, `which_items_active[]` and
  `which_items_checked_length`. These come from `allEwdDeferredProcs[id]` and are read when the pilot
  activates the deferred procedure.
- **`FwsEclInput`** (`fws_ecl_input_init` sets the defaults):
  - `now` in seconds, on the same clock as `items_timestamp`;
  - `flight_phase` and `aircraft_on_ground`;
  - `ecam_status_normal`: whatever the core currently holds, so the previous cycle's value at
    `clear_and_recall` and this cycle's at `update`, as in FBW;
  - `adr_pressure_altitude` (`?? 0`);
  - `approach_auto_display_pulse` (QNH-set pulse || slats-extended pulse);
  - `takeoff_power`;
  - `normal_sensed[checklist][item]` (-1 = null) — `FwsNormalChecklists.sensedItems` (FNC 537-612):

| Checklist (index) | sensed items (item index: FBW expression) |
|---|---|
| 1000001 COCKPIT PREPARATION (0) | 2: `!!seatBelt` |
| 1000002 BEFORE START (1) | 2: `A:LIGHT BEACON` |
| 1000003 AFTER START (2) | 2: `rudderTrimPosition < 0.35` |
| 1000004 TAXI (3) | 3: `false` (T.O sub-headline); 4: `A:CABIN SEATBELTS ALERT SWITCH == 1`; 5: `spoilersArmed`; 6: `slatFlapSelectionS18F10 \|\| S22F15 \|\| S22F20`; 7: `autoBrake == 6`; 8: `toConfigNormal` |
| 1000010 APPROACH (9) | 1: `A:CABIN SEATBELTS ALERT SWITCH` |
| 1000012 LANDING (11) | 0: `false`; 1: seat belts switch; 2: `isAllGearDownlocked`; 3: `spoilersArmed`; 4: `(!L:A380X_FM_LANDING_CONF3 && flaps handle == 4) \|\| (CONF3 && handle == 3)` |
| 1000013 PARKING (12) | 1: all four `engineNMaster` off; 3: `allFuelPumpsOff` |
| 1000014 SECURING THE AIRCRAFT (13) | 0: `L:PUSH_OVHD_OXYGEN_CREW`; 1: `L:XMLVAR_SWITCH_OVHD_INTLT_EMEREXIT_Position == 2` |
| all others / other items | null (-1) |

- **`FwsEcpButtons`**: `clr`/`clr2`, `rcl`, `cl`, `check_lh`/`check_rh`, `up`, `down`, `abn_proc`, and
  `ecp_failed`. As in FBW, `ecp_failed` gates only RCL.
- **`FwsEclEwdInput`** (EWD side): `fws_avail`, CPIOM A-G failed, `fcdc12_failed`, nose-gear `on_ground`,
  `groundspeed`, and `now`.

## Deviations and substitutions

- **Clock**: `now` in seconds replaces `Date.now()` in milliseconds:
  `floor(now - ts)` ≡ `Math.floor((Date.now() - ts) / 1000)`. The EWD recomputes timed texts on every
  compose instead of the 1 Hz throttled `updateTimedItems()` (WAS 177-214), so the countdown can lead
  FBW by up to 1 s.
- **Subscriptions → explicit replay**: MapSubject/Subject subscriptions are replayed at defined points:
  at entry to `clear_and_recall`/`update`, right after RCL, and synchronously for the ECL's own writes.
  Several map events inside one core loop collapse into a single replay. The only lost effect is an
  intermediate `selectFirst()` on a procedure that is no longer first, and that procedure is
  re-`selectFirst()`ed when it becomes active anyway. The flight-phase and take-off-power subscriptions
  are evaluated at `fws_ecl_update`; the first update seeds them without firing.
- **Shared JS objects**:
  - The abnormal item-check callback updates the generator's state before `moveDown`
    (`item_checked_adopts_state`).
  - `conditionalActiveItems` mutates `clState` in place after `setValue`; the ECL writes the final state
    back into the map.
  - Generators otherwise keep stale states as in FBW: normal and deferred generators are not refreshed
    by later sensed updates.
- **Bus**: the EWD composition reads the current FWS-side state instead of a serialized publication.
  Published scalars keep last-value semantics. `fws_active_procedure` is shared by the normal-deferred
  and abnormal views and is sticky, as in FBW.
- **Array lengths**: JS arrays sized by `whichItems*()` become `FWS_MAX_ITEMS` arrays with
  `num_items` as `itemsChecked.length`. The core must fill every item; FBW's `?? true` for missing
  `itemsToShow` entries is not representable.
- **FBW quirks kept on purpose**:
  - `isChecklistHeadline`'s comma expression: only CenteredSubHeadline counts.
  - `firstLineIsSelected()` returns NaN (falsy) when activated.
  - `splice(indexOf(-1), 1)` removes the last element.
  - The EWD `deferredIsCompleted` uses `every()` across all types.
  - The sensed-item auto-move applies to whichever checklist is active.
  - A DOWN on the deferred page's CLEAR still moves the last generator.
  - An empty deferred page returns before publishing its id.
  - The ABN PROC `showFromLine` is never changed.
  - The recommendation is captured when the procedure is added (none if on ground).
  - The FWS-failed fallback state has ten-entry arrays, so at most 10 items are shown.
- **Paths where FBW throws** (unknown ids, sub-menu 7-10 CHECK) are no-ops.
- **EMER CANC**: FBW's FWS does not route it to the ECL; no FwsCore reference exists, so the API has no
  EMER CANC input.
- **CPIOM fallback page**: its 21 lines are hand-ported from JSX (`WdCpiomFailedFallbackChecklistComponent.tsx:39-114`).
  They are static texts, not table data.
- **View priority**: the EWD can show only one of the overlapping components. The order is CPIOM
  fallback > abnormal sensed > normal > non-sensed. They can only overlap with inconsistent inputs.
- **Text**: lines are UTF-8, and lengths and padding count characters the way JS `.length` does.

## Tests

```sh
cd native/avionics
clang -std=c11 -O2 -Wall -Wextra -Werror a380x_fws_lines.c test_fws_lines.c -lm -o /tmp/test_fws_lines && /tmp/test_fws_lines
clang -std=c11 -O2 -Wall -Wextra -Werror a380x_fws_lines.c a380x_fws_ecl.c test_fws_ecl.c -lm -o /tmp/test_fws_ecl && /tmp/test_fws_ecl
for f in a380x_fws_lines.c a380x_fws_ecl.c test_fws_lines.c test_fws_ecl.c; do
  /opt/homebrew/bin/x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -c $f -o /tmp/${f%.c}.o; done
cd ../.. && python3 tools/test_build_fws_tables.py && python3 tools/build_fws_tables.py --check
```
