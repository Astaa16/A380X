# FWS signals (native port of the FBW A380X FWS "signal" side)

Files:

- `native/avionics/a380x_fws_signals.h` / `.c`: XPLM-free C11 module (no allocation, one `FwsSignals` struct).
- `native/avionics/test_fws_signals.c`: mock-input tests (552 checks).
- Shared types: `native/avionics/a380x_fws_types.h` (`FWS_MAX_ITEMS`).

It translates the signal part of the pinned FlyByWire A380X Flight Warning System
(`fbw-a380x/src/systems/systems-host/CpiomC/FlightWarningSystem/`):

| FBW source | Ported as |
| --- | --- |
| `FwsFlightPhases.ts` (phases 1-12, T.O / LDG memo, altitude alert C-chord) | `fwss_update_flight_phase`, `fwss_update_to_memo`, `fwss_update_ldg_memo`, `fwss_update_altitude_warning` |
| `FwsCore.ts` `update()` signal block (lines ~2850-5470) | `fwss_update_core` |
| `FwsMemos.ts` plus the memo assembly at `FwsCore.ts:5934-5985` | `fwss_update_memos` |
| `FwsAbnormalSensed.ts` `ewdAbnormalSensed` / `ewdDeferredProcs` | descriptor table `fwss_alerts[]`, `fwss_eval`, `fws_signals_alert_list` |
| `FwsInopSys.ts`, `FwsLimitations.ts`, `FwsInformation.ts` and the STS assembly at `FwsCore.ts:5784-5806, 5987-6150` | `fws_signals_status_lists` |
| `FwsNormalChecklists.ts` `sensedItems` | `fws_signals_normal_sensed` |
| `FwsSystemDisplayLogic.ts` (page when unselected) | `fwss_update_sd` |

Not in this module (integrator side): the alert life cycle (`FwsCore.ts:5470-6050`: presented / cleared /
recalled lists, master caution / warning latching from faults, CRC / SC scheduling, deferred procedure
activation), ECP handling, texts, the line generator and rendering. `FwsAutoCallouts.ts` is not ported
(ROW / ROP is not simulated, its status word is always 0).

## API

```c
void fws_inputs_defaults(FwsInputs *in);          /* healthy defaults, every *_valid = 0 */
void fws_signals_init(FwsSignals *s);
void fws_signals_update(FwsSignals *s, const FwsInputs *in, double clock_s, float dt);
void fws_signals_outputs(const FwsSignals *s, FwsSignalOutputs *out);

int  fws_signals_alert_count(void);
const FwsAlertDesc *fws_signals_alert_desc(int index);
int  fws_signals_alert_index(unsigned id);        /* -1: not implemented */
int  fws_signals_alert_active(const FwsSignals *s, int index);    /* simVarIsActive */
int  fws_signals_alert_confirmed(const FwsSignals *s, int index); /* itemIsActiveConsideringFaultSuppression(.., 0.6) */
void fws_signals_alert_eval(const FwsSignals *s, int index, FwsAlertEval *out);
int  fws_signals_alert_list(const FwsSignals *s, int index, FwsListKind kind,
                            const unsigned char *checked, unsigned n_checked, unsigned *out, int max);
void fws_signals_status_lists(const FwsSignals *s, const FwsActiveAlertRef *alerts, int n_alerts,
                              FwsStatusLists *out);
int  fws_signals_memos_left(const FwsSignals *s, unsigned *out, int max);   /* T.O / LDG memo lines */
int  fws_signals_memos_right(const FwsSignals *s, unsigned *out, int max);  /* EWD right memos (10) */
int  fws_signals_memos_pfd(const FwsSignals *s, unsigned *out, int max);    /* PFD memos (3) */
int  fws_signals_normal_sensed(const FwsSignals *s, unsigned proc_id, signed char *out, int max);
int  fws_signals_flaps_conf_from_angles(float flap_deg, float slat_deg);
const char *fws_signals_code_str(unsigned id, char buf[16]);               /* "%09u" */
```

Usage per FWS cycle (FBW runs the FWS every 125 ms; any rate works, all timers use `dt`):

1. Fill `FwsInputs` (start from `fws_inputs_defaults`), call `fws_signals_update(s, &in, clock_s, dt)`.
   `clock_s` is a monotonic time (sim elapsed seconds); it timestamps alert activation (monitor confirm
   times) and timed checklist items.
2. Life cycle: iterate `fws_signals_alert_count()` entries in table order (= FBW `Object.entries` order,
   ascending id; the 5 deferred procedures are last and have `deferred = 1`). For each alert:
   `fws_signals_alert_confirmed()` replaces `itemIsActiveConsideringFaultSuppression(value, key, 0.6)`;
   `desc->phase_inhib` is `flightPhaseInhib` (test `FWS_PHASE_BIT(phase)` for NEW warnings only, as FBW);
   `fws_signals_alert_eval()` gives `whichItemsToShow / whichItemsChecked / whichItemsActive /
   whichItemsTimer` and the `auralWarning` override (`desc->has_aural`). `items_checked` is raw: apply
   FBW's `!proc.items[i].sensed ? false : !!v` with the text table.
   Alerts without a `has_aural` override use the FBW default (level 3 CRC, level 2 SC).
3. STS lists: `s->lists` is assembled every update from the confirmed, not phase-inhibited procedures with
   their sensed checked state. For exact FBW behaviour call `fws_signals_status_lists()` with the alerts
   the life cycle keeps (presented + recalled, ascending id) and their presented / cleared `itemsChecked`.
4. Memos: `fws_signals_memos_left/right/pfd` give the FBW codes already ordered.
5. SD: feed the previous cycle's `L:A32NX_ECAM_SFAIL` (your life cycle) as `in.sd_fail_page`;
   `FwsSignalOutputs.sd_page_when_unselected` is `pageWhenUnselected`. User page selection, ALL cycling,
   the STS page auto-call and the 3 s STS timer stay in the ECP / SD module.

IDs are the FBW keys as unsigned numbers with leading zeros dropped (`"000017001"` -> `17001u`,
limitation `"1"` -> `1u`), the same convention as `a380x_fws_tables.h`. In the per-alert lists, `0`
stands for FBW `''` / `null` entries (dropped by `pushKeyUnique`).

`FwsSignals` is about 32 KB: allocate it statically. The module is single-threaded (no globals, no static
state outside the struct).

### Build and test

```sh
cd native/avionics
clang -std=c11 -O2 -Wall -Wextra -Werror test_fws_signals.c a380x_fws_signals.c -lm -o test_fws_signals
./test_fws_signals          # "552 checks, 0 failures"
/opt/homebrew/bin/x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -c a380x_fws_signals.c
```

The tests cover: healthy defaults (nothing active, no memo, STS normal), descriptor checks against the
FBW source (phase masks, cancel flag, confirm times), a full normal flight (cold & dark -> engine start ->
T.O memo -> T.O CONFIG -> take-off -> climb -> cruise -> approach with LDG memo -> landing -> taxi in ->
shutdown, phase sequence `1 2 3 4 5 6 7 8 9 10 11 12 1` with no unexpected confirmed alert), memo
selection and FBW ordering, ENG 1 FIRE (in flight, items, timers, aural, LAND ASAP) and ENG 2 FIRE on
ground, AP OFF voluntary / involuntary, ELEC (GEN FAULT, AC BUS FAULT, EMER CONFIG, DC suppression),
HYD G SYS LO PRESS (items, INOP SYS / INFO / LIMITATIONS lists), FUEL feed tank level and pump faults,
L/G NOT DOWN (cancel / no cancel), T.O CONFIG (flaps, slats, park brake, pitch trim, inhibited by doors),
phase inhibition, ADR faults and lists, EXCESS CAB ALT, engine extensions, IR align / TCAS / strobe memos,
SD page, normal checklist sensed items. A run under `-fsanitize=address,undefined` is clean.

## Input contract (`FwsInputs`)

Every field defaults to a healthy value (`fws_inputs_defaults`). A `*_valid = 0` group is treated as
"not simulated": the module uses the healthy value and never raises an alert from it. Native symbols
refer to the unity build of `a380x_avionics.c` (`eln` = network owner in `a380x_elec_adapter.inc`,
`elec` = ELEC panel, `adirs`, `guidance`, `center_right`, FMS outputs `A380XFmsOutputs` of `a380x_fms.h`).
`[none]` = no producer in the port today (leave the default).

### Electrical (native network)

| Field | Source | Default |
| --- | --- | --- |
| `elec_valid` | `a380x_power.available` (`a380x/xp12/elec/network_active`) | 0 (all buses powered) |
| `ac_powered[4]`, `ac_ess_powered`, `ac_emer_powered`, `dc1/dc2/dc_ess_powered`, `dc_ess_inflt_powered` (108PH), `dc_eha_powered` (247PP) | `a380x_eln_powered(&eln.st, A380X_ELN_AC1..)` (`a380x/xp12/elec/bus/<ac1..dc_eha>_powered`) | 1 |
| `gen_pb_on[4]` | `elec.normal[A380X_ELEC_GEN1+i]` (`a380x/xp12/elec/pb/gen1..4`) | 1 |
| `gen_fault[4]` | failure input `eln` `fail.gen[i]` (`a380x/xp12/elec/fail/gen1..4`, merged with `rel_genera0-3`) - not the FAULT lamp | 0 |
| `gen_potential_normal[4]` | `eln.st.gen_avail[i]` (T.O CONFIG "system status" uses GEN 1 and 2) | 1 |
| `apu_gen_pb_on[2]`, `apu_gen_fault[2]` | `elec.normal[APU_GEN_A/B]`, failure `fail.apu_gen[]` | 1, 0 |
| `bat_pb_auto[4]` (BAT 1, BAT 2, ESS, APU), `bat_fault[4]` | `elec.normal[BAT1..]`, `eln.st.lamps.bat_fault[]` (= failure input) | 1, 0 |
| `ext_connected`, `ext_avail[4]`, `ext_pb_on[4]` | `elec_io.ext_connected`, `eln.st.ext_avail[]`, `elec.ext_on[]` | 0 |
| `ext_contactor_closed[4]` | `a380x_eln_contactor(&eln.st, 990XG1..4)` (`a380x/xp12/elec/contactor/990XG1..`) - FBW `extPwrConnected` | 0 |
| `drive_disc[4]`, `bus_tie_auto`, `ac_ess_feed_normal`, `comm_on[2]`, `galley_auto` | `elec.drive_disc[]`, `elec.normal[BUS_TIE / AC_ESS_FEED / COMM1/2 / GALLEY]` | 0 / 1 |
| `rat_stow_ratio`, `rat_man_on`, `rat_fault` | `eln.st.rat_stow` (`a380x/xp12/elec/rat/stow_ratio`), `elec.rat_requested`, `eln.st.lamps.rat_fault` | 0 |
| `tr_fault[4]` (TR 1, TR 2, TR ESS, TR APU), `stat_inv_fault` | failure inputs `a380x/xp12/elec/fail/tr1, tr2, tr_ess, tr_apu, stat_inv` | 0 |

### Engines, thrust levers, fire

| Field | Source | Default |
| --- | --- | --- |
| `eng_valid` | 1 once the engine datarefs are bound | 0 (engines OFF) |
| `n1_pct[4]` | `sim/cockpit2/engine/indicators/N1_percent` | 0 |
| `n3_pct[4]` (HP spool, FBW `L:A32NX_ENGINE_N3`) | `sim/cockpit2/engine/indicators/N2_percent` | 0 |
| `egt_c[4]` | `sim/flightmodel2/engines/EGT_deg_cel` | 0 |
| `n1_idle_pct` | FADEC idle N1 (`a380x_ewd_fadec_limits()`, `[C] a380x/xp12/engine/thrust_limit_idle_n1`) | 19 |
| `eng_state[4]` (FBW `L:A32NX_ENGINE_STATE`: 0 OFF, 1 ON, 2 STARTING, 3 SHUTTING) | derive: ON = `engine_is_burning_fuel && !starter_is_running` (`EwdEngine.running`); STARTING = starter running, or master ON and not yet running; SHUTTING = master OFF and N2 > 5 %; else OFF | 0 |
| `eng_master_on[4]` | `sim/cockpit2/engine/actuators/mixture_ratio[i] > 0.5` (ENG MASTER switches) | 0 |
| `eng_limits_valid`, `egt_red_c`, `n1_red_pct`, `n3_red_pct` | `sim/aircraft/limits/red_hi_EGT`, `red_hi_N1`, `red_hi_N2` | 0 |
| `oil_press_low[4]` | `sim/cockpit2/annunciators/oil_pressure_low` | 0 |
| `eng_start_fault[4]` | adapter: state STARTING and any of `rel_hunsta<i>`, `rel_hotsta<i>`, `rel_startr<i>`, `rel_clonoz<i>` failed | 0 |
| `eng_anti_ice_on[4]`, `wing_anti_ice_on` | `sim/cockpit2/ice/ice_inlet_heat_on_per_engine`, `ice_surface_hot_bleed_air_on` | 0 |
| `eng_bleed_pb_auto[4]` | `sim/cockpit2/bleedair/actuators/engine_bleed_sov` | 1 |
| `eng_mode_selector` (0 CRANK, 1 NORM, 2 IGN/START) | `sim/cockpit2/engine/actuators/eng_mode_selector` (-1 -> 0, 0 -> 1, 1 -> 2) | 1 |
| `tla_valid`, `tla_deg[4]` (FBW TLA: IDLE 0, CL 25, MCT/FLX 35, TOGA 45, reverse < 0) | `guidance_inputs.throttle_lever_ratio[]` with the detent ratios of `config/fcu-calibration.ini`, piecewise linear between detents; reverse when `reverse_selected[]` | 0 (idle) |
| `flex_temp_set` | FMS `to_flex_temp_c != 0` | 0 |
| `thrust_limit_flex` | FMS `to_power_setting == FLEX` while on ground before T.O (`L:A32NX_AUTOTHRUST_THRUST_LIMIT_TYPE == 3`) | 0 |
| `eng_fire_detected[4]` | `sim/cockpit2/annunciators/engine_fires` (failures `rel_engfir0-3`) | 0 |
| `apu_fire_detected` | `sim/operation/failures/rel_apu_fire == 6` | 0 |
| `eng_fire_pb_released[4]`, `apu_fire_pb_released` | `[C] a380x/xp12/engine/fire_pushbutton` (no producer yet) | 0 |
| `eng_agent_discharged[4][2]`, `apu_agent_discharged`, `fire_test_pb`, `evac_command` | [none] (`sim/cockpit2/engine/actuators/fire_extinguisher_on[i]` could map agent 1) | 0 |

### APU, bleed, air conditioning, pressurization

| Field | Source | Default |
| --- | --- | --- |
| `apu_master_on` | `eln.st.apu_master` (`sim/cockpit2/electrical/APU_starter_switch >= 1`) | 0 |
| `apu_avail` | `eln.st.apu_avail` (`a380x/xp12/apu/avail`) | 0 |
| `apu_bleed_pb_on`, `apu_bleed_valve_open` | `sim/cockpit2/bleedair/actuators/apu_bleed` (both) | 0 |
| `apu_n_valid`, `apu_n_pct` | `sim/cockpit2/electrical/APU_N1_percent` | 0 |
| `pack_pb_on[2]` | `sim/cockpit2/bleedair/actuators/pack_left / pack_right` | 1 |
| `ram_air_on`, `cabin_air_extract_on`, `pack_flow_lo`, `man_cab_alt_mode` | [none] | 0 |
| `cabin_alt_valid`, `cabin_alt_ft`, `cabin_diff_psi` | `sim/cockpit2/pressurization/indicators/cabin_altitude_ft`, `pressure_diffential_psi` | 0 |
| `pax_oxy_masks_deployed` | [none] | 0 |

### Air data, ADIRS, radio altimeters

| Field | Source | Default |
| --- | --- | --- |
| `adr_cas_valid[3]`, `adr_cas_kt[3]` | `a380x/xp12/adirs/adr{n}_valid`, `adr{n}_ias_knots` | 0 |
| `adr_mach_valid/_mach[3]`, `adr_alt_valid/_alt_ft[3]` | `adr{n}_mach`, `adr{n}_altitude_ft` (pressure altitude) | 0 |
| `adr_fault[3]` | `adirs.unit[n].adr_fault_lit`, or ADR invalid while its IR mode is not OFF and the ADR pb is ON | 0 |
| `adr_overspeed[3]` | `a380x/xp12/adirs/adr{n}_overspeed` (ADR discrete word 1 bit 9) | 0 |
| `adr_pb_on[3]`, `ir_pb_on[3]` | `!adirs.unit[n].adr_off`, `!adirs.unit[n].ir_off` | 1 |
| `ir_fault[3]` | `adirs.unit[n].ir_fault_lit`, or attitude invalid while not aligning and mode not OFF | 0 |
| `ir_in_align[3]`, `ir_mode[3]` | `adirs.unit[n].aligning`, `adirs.unit[n].mode` (0 OFF, 1 NAV, 2 ATT) | 0, 1 |
| `ir_align_remaining_s` | max `adirs.unit[n].alignment_remaining` of aligning units | 0 |
| `air_data_knob`, `att_hdg_knob` (0 CAPT ON 3, 1 NORM, 2 F/O ON 3) | [none] (switching knobs not simulated) | 1 |
| `ir_gs_valid[3]`, `ir_gs_kt[3]` | `sim/flightmodel/position/groundspeed` x 1.94384 when the IR nav is valid | 0 |
| `ra_ft[3]`, `ra_ssm[3]` | RA 1 / RA 3: `sim/cockpit2/gauges/indicators/radio_altimeter_height_ft_pilot`, RA 2: `_copilot`; FW when `rel_pil_radalt` / `rel_cop_radalt` failed or unpowered; NCD above 8192 ft (ALA-52B range) | 0 / normal |
| `indicated_alt_ft` | `sim/cockpit2/gauges/indicators/altitude_ft_pilot` | 0 |
| `true_north_ref` | [none] | 0 |
| `stall_warning_raw` | `sim/cockpit2/annunciators/stall_warning` | 0 |
| `normal_law` | 1 (no law output in the native FBW laws) | 1 |

### Ground, gear, brakes, flight controls

| Field | Source | Default |
| --- | --- | --- |
| `sim_on_ground` (flight phases only, `Simplane.getIsGrounded`) | `sim/flightmodel/failures/onground_any` | 1 |
| `main_gear_compressed` (LGCIU hardwire, FWS on-ground logic) | any of `sim/flightmodel2/gear/on_ground[1..4]` | 1 |
| `gear_lever_down` (FBW `GEAR_LEVER_POSITION_REQUEST`) | `center_right.gear_lever`: X-Plane's `sim/cockpit2/controls/gear_handle_down`, or the crew lever while L/G GRVTY holds that handle ([LG_GRVTY.md](LG_GRVTY.md)); the raw handle only if center-right is unavailable | 1 |
| `lg_grvty_switch_pos` (`L:A32NX_LG_GRVTY_SWITCH_POS` 0 RESET, 1 OFF, 2 DOWN) | `lgg.panel.switch_pos` (a380x_lg_grvty.inc) | 1 |
| `gear_valid`, `nose_downlocked`, `main_left_downlocked`, `main_right_downlocked`, `all_gear_uplocked` | `sim/flightmodel2/gear/deploy_ratio[0]` (nose), `[1],[3]` left wing/body, `[2],[4]` right; >= 0.99 downlocked, all <= 0.01 uplocked | 0 / healthy |
| `gear_extended_ratio`, `nose_gear_ratio` | mean deploy ratio, `deploy_ratio[0]` | 1 |
| `park_brake_set` | `sim/cockpit2/controls/parking_brake_ratio >= 0.99` | 0 |
| `antiskid_on`, `autobrake_active` | `center_right.antiskid_on`, `center_right.autobrake_active` | 1, 0 |
| `autobrake_mode` (FBW 0 DISARM, 1 BTV, 2 LO, 3 L2, 4 L3, 5 HI, 6 RTO) | `center_right.autobrake_selected` / `rto_arm` | 0 |
| `brakes_valid`, `autobrake_fault`, `norm_brake_fault`, `antiskid_fault` | `!center_right.autobrake_available`, `!normal_brakes_available`, `!antiskid_available` | 0 |
| `brakes_hot`, `nw_steering_disc` | [none] | 0 |
| `flaps_handle_index` (0, 1 = 1+F, 2, 3, 4 = FULL) | FLAPS module CSU handle index ([FLAPS_LEVER.md](FLAPS_LEVER.md)); without it `round(sim/cockpit2/controls/flap_handle_request_ratio * 4)` | 0 |
| `flap_slat_valid`, `flap_angle_deg`, `slat_angle_deg` | FLAPS module FBW surface angles (from its FPPU drives); without it `sim/flightmodel2/controls/flap1_deploy_ratio * 33`, `slat1_deploy_ratio * 23` (converted internally to the A380 FPPU angles of `a380_systems/src/hydraulic/mod.rs:1735`) | 0 |
| `flaps_conf_index` | FLAPS module FBW FlapsConf; without it -1 = derived from the angles (`fws_signals_flaps_conf_from_angles`) | -1 |
| `flap_sys_fault[2]`, `flaps_jammed`, `slat_sys_fault[2]`, `slats_jammed` | `rel_flap_act` (both flap systems + jammed), `rel_fc_slt` (slats) | 0 |
| `gnd_splr_armed` | SPEED BRAKE module lever ARM ([SPEED_BRAKE.md](SPEED_BRAKE.md)); without it `sim/cockpit2/controls/speedbrake_ratio < 0` (ARMED -0.5) | 0 |
| `speedbrake_lever_out` (FBW speed brake command) | SPEED BRAKE module handle > 0.05 (X-Plane's handle carries the surface order there); without it `speedbrake_ratio > 0.05` | 0 |
| `pitch_trim_valid`, `pitch_trim_deg` (THS deg, nose-up positive, FBW green band -0.18..5.8) | `sim/flightmodel2/controls/stabilizer_deflection_degrees` (sign as displayed on the ECAM) | 0 |
| `rudder_trim_valid`, `rudder_trim_deg` | `sim/cockpit2/controls/rudder_trim` x rudder trim authority (deg) | 0 |
| `controls_moved` | sidestick `yoke_pitch_ratio` / `yoke_roll_ratio` > 0.05 or `yoke_heading_ratio` > 0.2 | 0 |

### Auto flight (guidance) and FMS

| Field | Source | Default |
| --- | --- | --- |
| `ap_engaged`, `fd1_on`, `fd2_on` | `guidance.ap1 || guidance.ap2`, `guidance.fd1/fd2` | 0 |
| `ap_instinctive_disc_count` | number of `a380x/xp12/fcu/ap_disconnect` presses since the previous update | 0 |
| `athr_instinctive_disc` | `a380x/xp12/fcu/athr_disconnect` pressed since the previous update | 0 |
| `athr_status` (0 off, 1 armed, 2 active), `athr_mode_active` | `guidance.athr_active ? 2 : guidance.athr_armed ? 1 : 0`, `guidance.thrust_mode != NONE` | 0 |
| `afs_a3` (A/THR LIMITED, ENGINE THRUST LOCKED) | `A380XA3Message` (`a380x/xp12/avionics/fma_a3`, currently always 0) | 0 |
| `afs_a1a2` | `guidance.thrust_mode` (pass-through A.FLOOR / TOGA LK) | 0 |
| `fcu_alt_ft`, `fg_alt_constraint_ft`, `alt_constraint_is_target` | `guidance.selected_altitude_ft`, FMS `fg_altitude_constraint_ft`, managed altitude with an ALT CST target | 0 |
| `gs_captured` | `guidance.vertical_mode` in GS / F-GS / GS* / F-GS* | 0 |
| `tcas_ra_active` | [none] | 0 |
| `fms_valid`, `v1_kt` | FMS `valid`, `v1_knots` | 0 |
| `fms_to_speeds_not_inserted`, `fms_to_speeds_too_low`, `fms_v_disagree` | FMS `to_speeds_not_inserted`, `to_speeds_too_low`, `vspeeds_disagree` (FM discrete word 3) | 0 |
| `fms_to_flaps` | FMS `to_flaps` (1..3, -1 none) | -1 |
| `fms_to_pitch_trim_valid`, `fms_to_pitch_trim_cg` | FMS `to_ths_for_pct` (valid when not NaN) | 0 |
| `gw_cg_valid`, `gw_cg_pct` | [none] (FQMS gross weight CG) | 0 |
| `fms_landing_conf3`, `fms_dest_efob_below_min` | FMS `landing_conf3`, `dest_efob_below_min` | 0 |
| `fms_zfw_zfwcg_set` | FMS `!isnan(zfw_kg) && !isnan(zfw_cg_pct)` | 1 |
| `landing_elev_valid/_ft`, `departure_elev_valid/_ft` | FMS `landing_elevation_valid/_ft`; departure [none] | 0 |

### Fuel, hydraulics, cabin, surveillance, buttons

| Field | Source | Default |
| --- | --- | --- |
| `fuel_valid`, `feed_tank_kg[4]` | `sim/flightmodel/weight/m_fuel[1..4]` (acf tanks "No.1..4 main tank") | 0 |
| `fuel_on_board_kg` | `sim/flightmodel/weight/m_fuel_total` | 0 |
| `feed_pump_main_on[4]` | `sim/cockpit2/engine/actuators/fuel_pump_on[i]` (FEED TK n MAIN pb) | 1 |
| `feed_pump_stby_valid`, `feed_pump_stby_on[4]` | [none] (STBY pumps are TEST-only lamps; invalid = follows the MAIN pump) | 0, 1 |
| `feed_pump_main_fault[4]`, `feed_pump_stby_fault[4]` | `a380x/xp12/ovhd/fail/feed<n>_main`, `feed<n>_stby` (or `rel_fuepmp<i>`) | 0 |
| `crossfeed_open[4]` | `sim/cockpit2/annunciators/crossfeed_on` for all four (single stock crossfeed) | 0 |
| `refuel_panel_open`, `refuel_in_progress`, `defuel_in_progress` | [none] | 0 |
| `hyd_valid`, `hyd_green_psi`, `hyd_yellow_psi` | `sim/cockpit2/hydraulics/indicators/hydraulic_pressure_1 / _2` (psi; pressurised switch 3700 / 2900 psi hysteresis inside) | 0 |
| `eng_pump_pb_auto[4][2]` | [none] (ENG PMP pbs are TEST-only lamps) | 1 |
| `eng_pump_section_low[4][2]` | adapter: `!(engine running && pump pb AUTO && system failure rel_hydpmp/rel_hydpmp2 not active)` | 0 |
| `eng_pump_disc[4]`, `elec_pump_active[4]` | [none] | 0 |
| `green_rsv_level_low`, `yellow_rsv_level_low` | `sim/cockpit2/hydraulics/indicators/hydraulic_fluid_ratio_1 / _2 < 0.3` (port threshold) | 0 |
| `seat_belts_on`, `no_mobile_on`, `strobe_off`, `beacon_on` | `sim/cockpit2/switches/fasten_seat_belts`, `no_smoking`, `!strobe_lights_on`, `beacon_on` | 0 |
| `crew_oxy_off` | `!sim/cockpit2/oxygen/actuators/o2_valve_on` | 0 |
| `emer_exit_light_off` | [none] | 0 |
| `main_door_open[10]` (M1L, M1R, M2L ... M5R = FBW INTERACTIVE POINT 0..9) | `sim/flightmodel2/misc/door_open_ratio[0..9] > 0.01` | 0 |
| `cargo_fwd_locked`, `cargo_aft_locked` | `door_open_ratio[17] / [18] <= 0.01` | 1 |
| `xpdr_stby`, `xpdr_alt_reporting` | `sim/cockpit2/radios/actuators/transponder_mode == 1`; mode in {3, 5, 6, 7} (on ground: the MFD XPDR alt reporting request, default 1) | 0, 1 |
| `tcas_mode` (0 STBY, 1 TA, 2 TA/RA) | MFD SURV TCAS mode, else transponder mode 6 -> 1, 7 -> 2, otherwise 0 | 2 |
| `tcas_fault` | `rel_xpndr` | 0 |
| `vhf3_voice` | [none] (VHF 3 in DATA) | 0 |
| `ice_valid`, `ice_ratio`, `tat_c`, `in_cloud` | `sim/flightmodel/failures/frm_ice`, TAT (`sim/cockpit2/temperature/outside_air_LE_temp_degc`), [none] | 0 |
| `to_config_pb`, `rcl_pb` | ECP T.O CONFIG / RCL held (commands `a380x/xp12/ecam/sd/to_config`, `recall`) | 0 |
| `master_warning_pb`, `master_caution_pb` | glareshield MASTER WARN / CAUT (either side) held | 0 |
| `sd_fail_page` | previous cycle `L:A32NX_ECAM_SFAIL` of the life cycle (-1 none) | -1 |

## Alerts

FBW `ewdAbnormalSensed` has 269 sensed alerts and `ewdDeferredProcs` 5 deferred procedures (274 entries).
Implemented: **126 sensed + 5 deferred = 131 of 274**, with FBW logic, levels, `flightPhaseInhib`,
`notActiveWhenItemActive`, confirm times, cancel flags, SD pages, aural overrides and item vectors
(item counts and LAND ASAP / ANSA recommendations were cross-checked against the EcamMessages texts by a
script). In addition **74 port extensions** use ECAM procedure ids that exist in the FBW texts but have no
FBW logic (ELEC, FUEL pumps, L/G, BRAKES, ENG); they are flagged `port_extension = 1`.

Implemented alerts whose input has no producer in the port today stay inactive until one exists:
A/THR LIMITED and ENGINE THRUST LOCKED (`afs_a3`), PITCH TRIM FMS DISAGREE (`gw_cg_pct`),
BRAKES HOT (`brakes_hot`), fire push-button / agent dependent items.

### Implemented FBW alerts (level, item count)

| Id | Title | Level | Items |
| --- | --- | --- | --- |
| 211800011 | PACK 1 OFF | 2 | 0 |
| 211800012 | PACK 2 OFF | 2 | 0 |
| 211800021 | PACK 1+2 FAULT | 2 | 15 |
| 211800051 | APU BLEED FAULT due to outside envelope | 2 | 6 |
| 211800052 | ENG 1 BLEED OFF | 2 | 0 |
| 211800053 | ENG 2 BLEED OFF | 2 | 0 |
| 211800054 | ENG 3 BLEED OFF | 2 | 0 |
| 211800055 | ENG 4 BLEED OFF | 2 | 0 |
| 213800001 | EXCESS CAB ALT | 3 | 23 |
| 213800002 | EXCESS DIFF PRESS | 3 | 7 |
| 213800004 | EXCESS RESIDUAL DIFF PRESS | 3 | 5 |
| 213800009 | INHIBITED BY DOORS | 2 | 1 |
| 220000002 | A/THR OFF (caution, non-procedure) | 2 | 0 |
| 220800001 | AP OFF involuntary | 3 | 0 |
| 220800003 | A/THR LIMITED | 2 | 2 |
| 220800004 | A/THR OFF involuntary | 2 | 1 |
| 221800007 | TO SPEEDS NOT INSERTED | 2 | 0 |
| 221800008 | TO SPEEDS TOO LOW | 2 | 1 |
| 221800009 | TO V1/VR/V2 DISAGREE | 2 | 0 |
| 260800001 | APU FIRE | 3 | 3 |
| 260800005 | ENG 1 FIRE (IN FLIGHT) | 3 | 9 |
| 260800006 | ENG 2 FIRE (IN FLIGHT) | 3 | 7 |
| 260800007 | ENG 3 FIRE (IN FLIGHT) | 3 | 7 |
| 260800008 | ENG 4 FIRE (IN FLIGHT) | 3 | 7 |
| 260800009 | ENG 1 FIRE (ON GROUND) | 3 | 16 |
| 260800010 | ENG 2 FIRE (ON GROUND) | 3 | 16 |
| 260800011 | ENG 3 FIRE (ON GROUND) | 3 | 16 |
| 260800012 | ENG 4 FIRE (ON GROUND) | 3 | 16 |
| 271800003 | PITCH TRIM NOT IN TO RANGE | 3 | 0 |
| 271800004 | RUDDER TRIM NOT IN TO RANGE | 3 | 0 |
| 271800005 | SPD BRK NOT RETRACTED | 3 | 0 |
| 271800017 | GND SPLRs NOT ARMED | 2 | 0 |
| 271800032 | PITCH TRIM FMS DISAGREE | 2 | 0 |
| 271800065 | SPEED BRAKES STILL EXTENDED | 2 | 0 |
| 271800069 | OVERSPEED | 3 | 7 |
| 271800070 | LOAD ANALYSIS REQUIRED | 1 | 0 |
| 272800001 | SLAT NOT IN TO CONFIG | 3 | 0 |
| 272800002 | FLAPS NOT IN TO CONFIG | 3 | 0 |
| 272800003 | FLAPS LEVER NOT ZERO | 3 | 0 |
| 272800006 | FLAP SYS 1 FAULT | 2 | 1 |
| 272800007 | FLAP SYS 2 FAULT | 2 | 1 |
| 272800008 | FLAP SYS 1+2 FAULT | 2 | 12 |
| 272800021 | SLAT SYS 1 FAULT | 2 | 1 |
| 272800022 | SLAT SYS 2 FAULT | 2 | 1 |
| 272800023 | SLAT SYS 1+2 FAULT | 2 | 11 |
| 272800028 | TO FLAPS FMS DISAGREE | 2 | 0 |
| 281800002 | ALL FEED TKs LEVEL LO | 2 | 7 |
| 281800023 | FEED TK 1 LEVEL LO | 2 | 8 |
| 281800024 | FEED TK 2 LEVEL LO | 2 | 8 |
| 281800025 | FEED TK 3 LEVEL LO | 2 | 8 |
| 281800026 | FEED TK 4 LEVEL LO | 2 | 8 |
| 281800076 | NO ZFW OR ZFWCG DATA | 2 | 1 |
| 281800102 | FEED TKs 1+2 LEVEL LO | 2 | 7 |
| 281800103 | FEED TKs 3+4 LEVEL LO | 2 | 7 |
| 290800005 | G ENG 1 PMP A PRESS LO | 2 | 2 |
| 290800006 | G ENG 1 PMP B PRESS LO | 2 | 2 |
| 290800007 | G ENG 2 PMP A PRESS LO | 2 | 2 |
| 290800008 | G ENG 2 PMP B PRESS LO | 2 | 2 |
| 290800009 | Y ENG 3 PMP A PRESS LO | 2 | 2 |
| 290800010 | Y ENG 3 PMP B PRESS LO | 2 | 2 |
| 290800011 | Y ENG 4 PMP A PRESS LO | 2 | 2 |
| 290800012 | Y ENG 4 PMP B PRESS LO | 2 | 2 |
| 290800021 | G RSVR LEVEL LO | 2 | 5 |
| 290800022 | Y RSVR LEVEL LO | 2 | 5 |
| 290800035 | G SYS LO PRESS | 2 | 8 |
| 290800036 | Y SYS LO PRESS | 2 | 5 |
| 290800039 | G + Y SYS LO PRESS | 2 | 11 |
| 320000001 | AUTO BRK OFF Master Caution light | 2 | 0 |
| 320800008 | BRAKES A_SKID OFF | 2 | 2 |
| 320800018 | BRAKES HOT | 2 | 5 |
| 320800022 | BRAKES PARK BRK ON | 2 | 1 |
| 320800030 | CONFIG PARK BRK ON | 3 | 0 |
| 320800037 | L/G NOT DOWN NO CANCEL | 3 | 0 |
| 320800038 | L/G NOT DOWN CANCEL | 3 | 0 |
| 340800001 | ADR 1 FAULT | 2 | 4 |
| 340800002 | ADR 2 FAULT | 2 | 2 |
| 340800003 | ADR 3 FAULT | 2 | 4 |
| 340800004 | ADR 1+2 FAULT | 2 | 8 |
| 340800005 | ADR 1+3 FAULT | 2 | 8 |
| 340800006 | ADR 2+3 FAULT | 2 | 8 |
| 340800008 | ADR 1+2+3 FAULT | 3 | 3 |
| 340800040 | IR 1 FAULT | 2 | 2 |
| 340800041 | IR 2 FAULT | 2 | 2 |
| 340800042 | IR 1+2 FAULT | 2 | 5 |
| 340800043 | IR 1+3 FAULT | 2 | 6 |
| 340800044 | IR 2+3 FAULT | 2 | 5 |
| 340800053 | RA SYS A FAULT | 1 | 0 |
| 340800054 | RA SYS B FAULT | 1 | 0 |
| 340800055 | RA SYS C FAULT | 1 | 0 |
| 340800059 | RA SYS A+B FAULT | 2 | 0 |
| 340800060 | RA SYS A+C FAULT | 2 | 0 |
| 340800061 | RA SYS B+C FAULT | 2 | 0 |
| 340800062 | RA SYS A+B+C FAULT | 2 | 3 |
| 340800072 | IR 3 FAULT | 2 | 2 |
| 341800016 | TCAS 1 FAULT | 2 | 1 |
| 341800017 | TCAS 2 FAULT | 2 | 1 |
| 341800018 | TCAS 1+2 FAULT | 2 | 0 |
| 341800019 | TCAS STBY | 2 | 0 |
| 341800037 | XPDR STBY | 2 | 0 |
| 520800017 | MAIN 1L NOT CLOSED | 2 | 6 |
| 520800018 | MAIN 1R NOT CLOSED | 2 | 6 |
| 520800019 | MAIN 2L NOT CLOSED | 2 | 6 |
| 520800020 | MAIN 2R NOT CLOSED | 2 | 6 |
| 520800021 | MAIN 3L NOT CLOSED | 2 | 6 |
| 520800022 | MAIN 3R NOT CLOSED | 2 | 6 |
| 520800023 | MAIN 4L NOT CLOSED | 2 | 6 |
| 520800024 | MAIN 4R NOT CLOSED | 2 | 6 |
| 520800025 | MAIN 5L NOT CLOSED | 2 | 6 |
| 520800026 | MAIN 5R NOT CLOSED | 2 | 6 |
| 701800029 | ENG 1 FAIL | 2 | 0 |
| 701800030 | ENG 2 FAIL | 2 | 0 |
| 701800031 | ENG 3 FAIL | 2 | 0 |
| 701800032 | ENG 4 FAIL | 2 | 0 |
| 701800109 | ENG 1 SHUTDOWN | 2 | 8 |
| 701800110 | ENG 2 SHUTDOWN | 2 | 9 |
| 701800111 | ENG 3 SHUTDOWN | 2 | 9 |
| 701800112 | ENG 4 SHUTDOWN | 2 | 8 |
| 701800151 | ALL ENGINES FAILURE | 3 | 0 |
| 701800158 | ENGINE THRUST LOCKED | 2 | 1 |
| 999800001 | *F/CTL | 2 | 0 |
| 999800002 | *FUEL | 2 | 0 |
| 999800003 | *WHEEL | 2 | 0 |
| 999800004 | *ELEC | 2 | 0 |
| 999800005 | *ELEC | 2 | 0 |
| 999800006 | *BLEED | 2 | 0 |
| 999800007 | *HYD | 2 | 0 |
| 210700001 | WHEN PACK OVHT OUT (deferred) | 0 | 2 |
| 210700002 | WHEN DIFF PR<2 PSI & FL<100 (deferred) | 0 | 2 |
| 221700001 | LDG ELEVN (deferred) | 0 | 2 |
| 270700001 | RUDDER TRIM RUNAWAY (deferred) | 0 | 5 |
| 320700001 | L/G GRVTY EXTN (deferred) | 0 | 5 |

### Port extensions [EXT]

Conditions (all require their `*_valid` group; levels follow the title colour of the FBW text, red = 3,
amber = 2, redundancy-loss type cautions = 1; phase masks reuse the FBW masks of comparable alerts:
caution `[3,4,5,6,7,9,10]`, level 1 `[3..11]`, engine `[5,6]`, brakes `[4,5,6,7]`):

- ELEC AC BUS n / AC ESS / AC EMER / DC BUS 1, 2, ESS FAULT: bus unpowered while at least one AC main bus
  is powered (no alert in cold & dark or in EMER CONFIG). DC BUS 1+2 and DC BUS 1+ESS suppress the single
  DC alerts. Sensed items: COMMERCIAL OFF, CROSSFEED OFF / ON, AIR DATA SWTG, AC ESS FEED ALTN.
- ELEC EMER CONFIG (red, LAND ASAP): no AC main bus powered, in flight. Items: RAT MAN ON (RAT deployed),
  BUS TIE OFF, A/THR OFF; items 10-12 shown above FL200, 24 at or below, 21-23 (repeated branch) hidden.
- ELEC GEN n FAULT: generator failure with its engine running and the drive connected; item GEN OFF.
  GEN n OFF: pb OFF with the engine running, no fault, drive connected.
  DRIVE n DISCONNECTED: drive disconnected; item GEN OFF.
- ELEC APU GEN A/B FAULT (failure with APU AVAIL), BAT 1/2 FAULT, APU BAT FAULT (battery failure; item
  BAT OFF), APU TR FAULT, TR 1/2/ESS FAULT (TR failure with AC available), STATIC INV FAULT, RAT FAULT,
  BUS TIE OFF, AC ESS BUS ALTN (AC ESS FEED pb ALTN), EXT PWR n FAULT (on ground, GPU connected, EXT pb ON,
  EXT not available).
- FUEL FEED TK n MAIN PMP FAULT (level 1), STBY PMP FAULT (level 1, needs `feed_pump_stby_valid`),
  MAIN + STBY PMPs FAULT (suppresses the two others).
- L/G GEAR NOT LOCKED DOWN (red) / NOT LOCKED UP: lever down / up and the gear not down-locked /
  up-locked for 30 s, airborne phases 6-9. Items L/G LEVER UP / DOWN sensed; NOT LOCKED DOWN also senses
  its L/G GRVTY .... DOWN item (`lg_grvty_switch_pos == 2`, port extension: FBW has no 320800039 logic).
- BRAKES AUTO BRK FAULT, NORM BRK FAULT, A-SKID FAULT ON ALL L/G: `center_right` availability flags.
- ENG n EGT OVER LIMIT, N1/N2 OVER LIMIT (red-line limits, engine running), OIL PRESS LO (stock
  annunciator, engine running), START FAULT (adapter start-fault flag).

| Id | Title | Level | Items |
| --- | --- | --- | --- |
| 240800002 | ELEC AC BUS 1 FAULT | 2 | 1 |
| 240800004 | ELEC AC BUS 2 FAULT | 2 | 8 |
| 240800007 | ELEC AC BUS 3 FAULT | 2 | 12 |
| 240800009 | ELEC AC BUS 4 FAULT | 2 | 9 |
| 240800010 | ELEC AC EMER BUS FAULT | 2 | 4 |
| 240800011 | ELEC AC ESS BUS ALTN | 1 | 0 |
| 240800012 | ELEC AC ESS BUS FAULT | 2 | 11 |
| 240800013 | ELEC APU BAT FAULT | 2 | 1 |
| 240800014 | ELEC APU GEN A FAULT | 1 | 1 |
| 240800015 | ELEC APU GEN B FAULT | 1 | 1 |
| 240800016 | ELEC APU TR FAULT | 1 | 2 |
| 240800017 | ELEC BAT 1 (ESS) FAULT | 2 | 1 |
| 240800018 | ELEC BAT 2 (ESS) FAULT | 2 | 1 |
| 240800019 | ELEC BUS TIE OFF | 2 | 0 |
| 240800026 | ELEC DC BUS 1 FAULT | 2 | 11 |
| 240800027 | ELEC DC BUS 1+2 FAULT | 2 | 22 |
| 240800028 | ELEC DC BUS 1+ESS FAULT | 2 | 21 |
| 240800029 | ELEC DC BUS 2 FAULT | 2 | 12 |
| 240800030 | ELEC DC ESS BUS FAULT | 2 | 20 |
| 240800036 | ELEC DRIVE 1 DISCONNECTED | 2 | 1 |
| 240800037 | ELEC DRIVE 2 DISCONNECTED | 2 | 1 |
| 240800038 | ELEC DRIVE 3 DISCONNECTED | 2 | 1 |
| 240800039 | ELEC DRIVE 4 DISCONNECTED | 2 | 1 |
| 240800055 | ELEC EMER CONFIG | 3 | 30 |
| 240800056 | ELEC EXT PWR 1 FAULT | 2 | 1 |
| 240800057 | ELEC EXT PWR 2 FAULT | 2 | 1 |
| 240800058 | ELEC EXT PWR 3 FAULT | 2 | 1 |
| 240800059 | ELEC EXT PWR 4 FAULT | 2 | 1 |
| 240800061 | ELEC GEN 1 FAULT | 2 | 1 |
| 240800062 | ELEC GEN 2 FAULT | 2 | 1 |
| 240800063 | ELEC GEN 3 FAULT | 2 | 1 |
| 240800064 | ELEC GEN 4 FAULT | 2 | 1 |
| 240800065 | ELEC GEN 1 OFF | 2 | 0 |
| 240800066 | ELEC GEN 2 OFF | 2 | 0 |
| 240800067 | ELEC GEN 3 OFF | 2 | 0 |
| 240800068 | ELEC GEN 4 OFF | 2 | 0 |
| 240800072 | ELEC RAT FAULT | 1 | 0 |
| 240800080 | ELEC STATIC INV FAULT | 1 | 0 |
| 240800081 | ELEC TR 1 FAULT | 1 | 0 |
| 240800082 | ELEC TR 2 FAULT | 1 | 0 |
| 240800083 | ELEC TR ESS FAULT | 2 | 0 |
| 281800027 | FUEL FEED TK 1 MAIN + STBY PMPs FAULT | 2 | 0 |
| 281800028 | FUEL FEED TK 2 MAIN + STBY PMPs FAULT | 2 | 0 |
| 281800029 | FUEL FEED TK 3 MAIN + STBY PMPs FAULT | 2 | 0 |
| 281800030 | FUEL FEED TK 4 MAIN + STBY PMPs FAULT | 2 | 0 |
| 281800031 | FUEL FEED TK 1 MAIN PMP FAULT | 1 | 0 |
| 281800032 | FUEL FEED TK 2 MAIN PMP FAULT | 1 | 0 |
| 281800033 | FUEL FEED TK 3 MAIN PMP FAULT | 1 | 0 |
| 281800034 | FUEL FEED TK 4 MAIN PMP FAULT | 1 | 0 |
| 281800035 | FUEL FEED TK 1 STBY PMP FAULT | 1 | 0 |
| 281800036 | FUEL FEED TK 2 STBY PMP FAULT | 1 | 0 |
| 281800037 | FUEL FEED TK 3 STBY PMP FAULT | 1 | 0 |
| 281800038 | FUEL FEED TK 4 STBY PMP FAULT | 1 | 0 |
| 320800001 | BRAKES A-SKID FAULT ON ALL L/G | 2 | 2 |
| 320800013 | BRAKES AUTO BRK FAULT | 2 | 0 |
| 320800020 | BRAKES NORM BRK FAULT | 2 | 1 |
| 320800039 | L/G GEAR NOT LOCKED DOWN | 3 | 6 |
| 320800040 | L/G GEAR NOT LOCKED UP | 2 | 7 |
| 701800009 | ENG 1 EGT OVER LIMIT | 2 | 0 |
| 701800010 | ENG 2 EGT OVER LIMIT | 2 | 0 |
| 701800011 | ENG 3 EGT OVER LIMIT | 2 | 0 |
| 701800012 | ENG 4 EGT OVER LIMIT | 2 | 0 |
| 701800073 | ENG 1 N1/N2 OVER LIMIT | 2 | 0 |
| 701800074 | ENG 2 N1/N2 OVER LIMIT | 2 | 0 |
| 701800075 | ENG 3 N1/N2 OVER LIMIT | 2 | 0 |
| 701800076 | ENG 4 N1/N2 OVER LIMIT | 2 | 0 |
| 701800085 | ENG 1 OIL PRESS LO | 2 | 0 |
| 701800086 | ENG 2 OIL PRESS LO | 2 | 0 |
| 701800087 | ENG 3 OIL PRESS LO | 2 | 0 |
| 701800088 | ENG 4 OIL PRESS LO | 2 | 0 |
| 701800117 | ENG 1 START FAULT | 2 | 0 |
| 701800118 | ENG 2 START FAULT | 2 | 0 |
| 701800119 | ENG 3 START FAULT | 2 | 0 |
| 701800120 | ENG 4 START FAULT | 2 | 0 |

### FBW alerts not implemented (missing input)

| Id | Title | Missing input |
| --- | --- | --- |
| 211800001 | PACK 1 CTL 1 FAULT | Pack controller / FDAC channel and CPIOM-B AGS words: stock packs have no controllers |
| 211800002 | PACK 1 CTL 2 FAULT | Pack controller / FDAC channel and CPIOM-B AGS words: stock packs have no controllers |
| 211800003 | PACK 2 CTL 1 FAULT | Pack controller / FDAC channel and CPIOM-B AGS words: stock packs have no controllers |
| 211800004 | PACK 2 CTL 2 FAULT | Pack controller / FDAC channel and CPIOM-B AGS words: stock packs have no controllers |
| 211800005 | PACK 1 CTL DEGRADED | Pack controller / FDAC channel and CPIOM-B AGS words: stock packs have no controllers |
| 211800006 | PACK 2 CTL DEGRADED | Pack controller / FDAC channel and CPIOM-B AGS words: stock packs have no controllers |
| 211800007 | PACK 1 CTL REDUNDANCY LOST | Pack controller / FDAC channel and CPIOM-B AGS words: stock packs have no controllers |
| 211800008 | PACK 2 CTL REDUNDANCY LOST | Pack controller / FDAC channel and CPIOM-B AGS words: stock packs have no controllers |
| 211800009 | PACK 1 FAULT | PACK n FAULT needs FDAC channel failures (not simulated) |
| 211800010 | PACK 2 FAULT | PACK n FAULT needs FDAC channel failures (not simulated) |
| 211800023 | ALL PRIMARY CABIN FANS FAULT | Cabin fans (VCS) not simulated |
| 211800025 | BULK CARGO HEATER FAULT | Cargo heater / isolation / ventilation not simulated |
| 211800026 | BULK CARGO ISOL FAULT | Cargo heater / isolation / ventilation not simulated |
| 211800027 | BULK CARGO VENT FAULT | Cargo heater / isolation / ventilation not simulated |
| 211800029 | FWD CARGO ISOL FAULT | Cargo heater / isolation / ventilation not simulated |
| 211800031 | FWD CARGO VENT FAULT | Cargo heater / isolation / ventilation not simulated |
| 211800032 | HOT AIR 1 FAULT | HOT AIR pb / valve: lamp is TEST-only, no state |
| 211800033 | HOT AIR 2 FAULT | HOT AIR pb / valve: lamp is TEST-only, no state |
| 211800035 | ONE PRIMARY CABIN FAN FAULT | Cabin fans (VCS) not simulated |
| 211800039 | TEMP CTL 1 FAULT | Temperature control (TCS / TADD) not simulated |
| 211800040 | TEMP CTL 2 FAULT | Temperature control (TCS / TADD) not simulated |
| 211800041 | TEMP CTL FAULT | Temperature control (TCS / TADD) not simulated |
| 211800042 | TEMP CTL REDUNDANCY LOST | Temperature control (TCS / TADD) not simulated |
| 211800043 | THREE PRIMARY CABIN FANS FAULT | Cabin fans (VCS) not simulated |
| 211800044 | TWO PRIMARY CABIN FANS FAULT | Cabin fans (VCS) not simulated |
| 211800046 | HOT AIR 1 OFF | HOT AIR pb / valve: lamp is TEST-only, no state |
| 211800047 | HOT AIR 2 OFF | HOT AIR pb / valve: lamp is TEST-only, no state |
| 211800048 | HOT AIR 1 VLV OPEN | HOT AIR pb / valve: lamp is TEST-only, no state |
| 211800049 | HOT AIR 2 VLV OPEN | HOT AIR pb / valve: lamp is TEST-only, no state |
| 211800050 | TEMP CTL DEGRADED | Temperature control (TCS / TADD) not simulated |
| 212800001 | AFT VENT CTL 1 FAULT | Ventilation controllers (VCM / CPIOM-B VCS) not simulated |
| 212800002 | AFT VENT CTL 2 FAULT | Ventilation controllers (VCM / CPIOM-B VCS) not simulated |
| 212800003 | AFT VENT CTL DEGRADED | Ventilation controllers (VCM / CPIOM-B VCS) not simulated |
| 212800004 | AFT VENT CTL FAULT | Ventilation controllers (VCM / CPIOM-B VCS) not simulated |
| 212800005 | AFT VENT CTL REDUNDANCY LOST | Ventilation controllers (VCM / CPIOM-B VCS) not simulated |
| 212800007 | FWD VENT CTL 1 FAULT | Ventilation controllers (VCM / CPIOM-B VCS) not simulated |
| 212800008 | FWD VENT CTL 2 FAULT | Ventilation controllers (VCM / CPIOM-B VCS) not simulated |
| 212800009 | FWD VENT CTL DEGRADED | Ventilation controllers (VCM / CPIOM-B VCS) not simulated |
| 212800010 | FWD VENT CTL FAULT | Ventilation controllers (VCM / CPIOM-B VCS) not simulated |
| 212800011 | FWD VENT CTL REDUNDANCY LOST | Ventilation controllers (VCM / CPIOM-B VCS) not simulated |
| 213800005 | PRESS AUTO CTL FAULT | CPCS / OCSM / outflow valves not simulated |
| 213800006 | PRESS CTL REDUNDANCY LOST | CPCS / OCSM / outflow valves not simulated |
| 213800011 | PRESS OUTFLW VLV CTL 1 FAULT | CPCS / OCSM / outflow valves not simulated |
| 213800012 | PRESS OUTFLW VLV CTL 2 FAULT | CPCS / OCSM / outflow valves not simulated |
| 213800013 | PRESS OUTFLW VLV CTL 3 FAULT | CPCS / OCSM / outflow valves not simulated |
| 213800014 | PRESS OUTFLW VLV CTL 4 FAULT | CPCS / OCSM / outflow valves not simulated |
| 213800017 | CAB PRESS SYS FAULT | CPCS / OCSM / outflow valves not simulated |
| 213800019 | PRESS OUTFLW VLV CTL FAULT OUTFLW VLV 1+2+3 | CPCS / OCSM / outflow valves not simulated |
| 213800020 | PRESS OUTFLW VLV CTL FAULT OUTFLW VLV 1+2+4 | CPCS / OCSM / outflow valves not simulated |
| 213800021 | PRESS OUTFLW VLV CTL FAULT OUTFLW VLV 1+3+4 | CPCS / OCSM / outflow valves not simulated |
| 213800022 | PRESS OUTFLW VLV CTL FAULT OUTFLW VLV 2+3+4 | CPCS / OCSM / outflow valves not simulated |
| 213800023 | PRESS OUTFLW VLV CTL FAULT OUTFLW VLV 1+2 | CPCS / OCSM / outflow valves not simulated |
| 213800024 | PRESS OUTFLW VLV CTL FAULT OUTFLW VLV 1+3 | CPCS / OCSM / outflow valves not simulated |
| 213800025 | PRESS OUTFLW VLV CTL FAULT OUTFLW VLV 1+4 | CPCS / OCSM / outflow valves not simulated |
| 213800026 | PRESS OUTFLW VLV CTL FAULT OUTFLW VLV 2+3 | CPCS / OCSM / outflow valves not simulated |
| 213800027 | PRESS OUTFLW VLV CTL FAULT OUTFLW VLV 2+4 | CPCS / OCSM / outflow valves not simulated |
| 213800028 | PRESS OUTFLW VLV CTL FAULT OUTFLW VLV 3+4 | CPCS / OCSM / outflow valves not simulated |
| 213800029 | PRESS AUTO CTL FAULT SYS 1 | CPCS / OCSM / outflow valves not simulated |
| 213800030 | PRESS AUTO CTL FAULT SYS 2 | CPCS / OCSM / outflow valves not simulated |
| 213800031 | PRESS AUTO CTL FAULT SYS 3 | CPCS / OCSM / outflow valves not simulated |
| 213800032 | PRESS AUTO CTL FAULT SYS 4 | CPCS / OCSM / outflow valves not simulated |
| 213800033 | PRESS AUTO CTL FAULT SYS 1+2 | CPCS / OCSM / outflow valves not simulated |
| 213800034 | PRESS AUTO CTL FAULT SYS 1+3 | CPCS / OCSM / outflow valves not simulated |
| 213800035 | PRESS AUTO CTL FAULT SYS 1+4 | CPCS / OCSM / outflow valves not simulated |
| 213800036 | PRESS AUTO CTL FAULT SYS 2+3 | CPCS / OCSM / outflow valves not simulated |
| 213800037 | PRESS AUTO CTL FAULT SYS 2+4 | CPCS / OCSM / outflow valves not simulated |
| 213800038 | PRESS AUTO CTL FAULT SYS 3+4 | CPCS / OCSM / outflow valves not simulated |
| 213800039 | PRESS AUTO CTL FAULT SYS 1+2+3 | CPCS / OCSM / outflow valves not simulated |
| 213800040 | PRESS AUTO CTL FAULT SYS 1+2+4 | CPCS / OCSM / outflow valves not simulated |
| 213800041 | PRESS AUTO CTL FAULT SYS 1+3+4 | CPCS / OCSM / outflow valves not simulated |
| 213800042 | PRESS AUTO CTL FAULT SYS 2+3+4 | CPCS / OCSM / outflow valves not simulated |
| 220800013 | ROLL OUT FAULT | rollOutFault is constant false in FBW (FCDC) |
| 221800001 | FMC-A FAULT | FMC A/B/C health not published by the native FMS |
| 221800002 | FMC-B FAULT | FMC A/B/C health not published by the native FMS |
| 221800003 | FMC-C FAULT | FMC A/B/C health not published by the native FMS |
| 221800004 | FMS 1 FAULT | FMC A/B/C health not published by the native FMS |
| 221800005 | FMS 2 FAULT | FMC A/B/C health not published by the native FMS |
| 221800006 | FMS 1+2 FAULT | FMC A/B/C health not published by the native FMS |
| 230800012 | RMP 1 FAULT | RMPs not simulated |
| 230800013 | RMP 2 FAULT | RMPs not simulated |
| 230800014 | RMP 3 FAULT | RMPs not simulated |
| 230800015 | RMP 1+2 FAULT | RMPs not simulated |
| 230800016 | RMP 1+3 FAULT | RMPs not simulated |
| 230800017 | RMP 2+3 FAULT | RMPs not simulated |
| 230800018 | RMP 1+2+3 FAULT | RMPs not simulated |
| 260800002 | APU FIRE DET FAULT | Fire detection loops / FDU not simulated |
| 260800003 | APU FIRE LOOP A FAULT | Fire detection loops / FDU not simulated |
| 260800004 | APU FIRE LOOP B FAULT | Fire detection loops / FDU not simulated |
| 260800013 | ENG 1 FIRE DET FAULT | Fire detection loops / FDU not simulated |
| 260800014 | ENG 2 FIRE DET FAULT | Fire detection loops / FDU not simulated |
| 260800015 | ENG 3 FIRE DET FAULT | Fire detection loops / FDU not simulated |
| 260800016 | ENG 4 FIRE DET FAULT | Fire detection loops / FDU not simulated |
| 260800017 | ENG 1 FIRE LOOP A FAULT | Fire detection loops / FDU not simulated |
| 260800018 | ENG 1 FIRE LOOP B FAULT | Fire detection loops / FDU not simulated |
| 260800019 | ENG 2 FIRE LOOP A FAULT | Fire detection loops / FDU not simulated |
| 260800020 | ENG 2 FIRE LOOP B FAULT | Fire detection loops / FDU not simulated |
| 260800021 | ENG 3 FIRE LOOP A FAULT | Fire detection loops / FDU not simulated |
| 260800022 | ENG 3 FIRE LOOP B FAULT | Fire detection loops / FDU not simulated |
| 260800023 | ENG 4 FIRE LOOP A FAULT | Fire detection loops / FDU not simulated |
| 260800024 | ENG 4 FIRE LOOP B FAULT | Fire detection loops / FDU not simulated |
| 260800025 | MLG BAY FIRE | MLG bay fire detection not simulated |
| 260800026 | MLG BAY FIRE DET FAULT | MLG bay fire detection not simulated |
| 260800027 | MLG BAY FIRE LOOP A FAULT | MLG bay fire detection not simulated |
| 260800028 | MLG BAY FIRE LOOP B FAULT | MLG bay fire detection not simulated |
| 271800008 | ALTN LAW | Flight control law (FCDC word 1) not published by the native FBW laws |
| 271800009 | DIRECT LAW | Flight control law (FCDC word 1) not published by the native FBW laws |
| 271800013 | FCDC 1 FAULT | FCDC / PRIM / SEC / rudder actuators not simulated |
| 271800014 | FCDC 2 FAULT | FCDC / PRIM / SEC / rudder actuators not simulated |
| 271800015 | FCDC 1+2 FAULT | FCDC / PRIM / SEC / rudder actuators not simulated |
| 271800036 |  | FCDC / PRIM / SEC / rudder actuators not simulated |
| 271800037 |  | FCDC / PRIM / SEC / rudder actuators not simulated |
| 271800038 |  | FCDC / PRIM / SEC / rudder actuators not simulated |
| 271800058 | SEC 1 FAULT | FCDC / PRIM / SEC / rudder actuators not simulated |
| 271800059 | SEC 2 FAULT | FCDC / PRIM / SEC / rudder actuators not simulated |
| 271800060 | SEC 3 FAULT | FCDC / PRIM / SEC / rudder actuators not simulated |
| 271800062 | SINGLE RUDDER FAULT | FCDC / PRIM / SEC / rudder actuators not simulated |
| 271800064 | SPEED BRAKES POS/LEVER DISAGREE | FCDC / PRIM / SEC / rudder actuators not simulated |
| 281800101 | ZFW OR ZFWCG FMS DISAGREE | FQMS ZFW/ZFWCG cross-check not simulated |
| 290800001 | G ELEC PMP A FAULT | Electric hydraulic pumps (state / low pressure / pb) not simulated |
| 290800002 | G ELEC PMP B FAULT | Electric hydraulic pumps (state / low pressure / pb) not simulated |
| 290800003 | Y ELEC PMP A FAULT | Electric hydraulic pumps (state / low pressure / pb) not simulated |
| 290800004 | Y ELEC PMP B FAULT | Electric hydraulic pumps (state / low pressure / pb) not simulated |
| 290800019 | G RSVR AIR PRESS LO | Reservoir air pressure / overheat not simulated |
| 290800020 | Y RSVR AIR PRESS LO | Reservoir air pressure / overheat not simulated |
| 290800031 | G SYS OVHT | Reservoir air pressure / overheat not simulated |
| 290800032 | Y SYS OVHT | Reservoir air pressure / overheat not simulated |
| 290800040 | Y ELEC PMP A+B OFF | Electric hydraulic pumps (state / low pressure / pb) not simulated |
| 314800006 | AUDIO FUNCTION LOST | FWS / ECP / audio health: owned by the integrator (FWS life cycle) |
| 314800007 | ECP FAULT | FWS / ECP / audio health: owned by the integrator (FWS life cycle) |
| 314800008 | FWS 1 FAULT | FWS / ECP / audio health: owned by the integrator (FWS life cycle) |
| 314800009 | FWS 2 FAULT | FWS / ECP / audio health: owned by the integrator (FWS life cycle) |
| 320800014 | BTV FAULT | FCDC landing function word (BTV / ROW / ROP) not simulated |
| 340800015 | ARPT NAV LOST | OANS / airport navigation not simulated |
| 340800021 | EXTREME LATITUDE | IR maintenance word bit 15 (extreme latitude) not published |
| 340800045 | IR NOT ALIGNED | IR excess motion (maint word bit 13) not published |
| 341800015 |  | FCDC landing function word (BTV / ROW / ROP) not simulated |
| 341800020 | TERR SYS 1 FAULT | TAWS / terrain system not simulated |
| 341800021 | TERR SYS 2 FAULT | TAWS / terrain system not simulated |
| 341800022 | TERR SYS 1+2 FAULT | TAWS / terrain system not simulated |
| 341800023 | TAWS 1 FAULT | TAWS / terrain system not simulated |
| 341800024 | TAWS 2 FAULT | TAWS / terrain system not simulated |
| 341800025 | TAWS 1+2 FAULT | TAWS / terrain system not simulated |
| 520800008 | CKPT SLIDING WINDOW NOT CLOSED | Cockpit sliding windows not simulated |

### Deferred procedures

`ewdDeferredProcs` are always active (`Subject.create(true)`), descriptors with `deferred = 1`; their
items: 210700001 `[PACK 1 ON, PACK 2 ON]`, 210700002 `[RAM AIR, CABIN AIR EXTRACT]`, 221700001
`[MAN CAB ALT, -]`, 270700001 (none sensed), 320700001 `[-, L/G LEVER UP, L/G GRVTY DOWN
(`L:A32NX_LG_GRVTY_SWITCH_POS === 2`, FwsAbnormalSensed.ts:4883), all gear down-locked, L/G LEVER DOWN]`. The parent alerts (`fromAbnormalProcs`) are in the texts:
210700001/2 <- 211800021 (implemented), 221700001 <- 221800006 (not implemented),
270700001 <- 270900002 (non-sensed), 320700001 <- 290800035/36/39 (implemented).

## Flight phases

Faithful port of `FwsFlightPhases._updateFlightPhase` (phases 1..12, confirm / monostable / memory nodes
with FBW time constants, JS short-circuit evaluation order kept so node writes happen exactly when FBW
writes them). The phase starts at 1 (`FwsCore.flightPhase = Subject.create(ElecPwr)`); several valid
phases keep the current one or take the earliest. Notable FBW timings reproduced (and tested): phase 2
starts 30 s after the first engine N1 > 15 %, phase 9 lasts at most 180 s, phase 12 lasts 300 s.

## Memos

Right side (EWD, 10 lines) in FBW order: `ewdMemos` in `Object.entries` order (integer keys ascending,
then `0000170`, `0000180`), reversed by `mapOrder()` with an empty order list (V8 / JavaScriptCore
behaviour of the inconsistent comparator), then stable-sorted by `messagePriority` of the text colour
(magenta, red, amber, green). Left side: T.O / LDG memo lines in `codesToReturn` order. PFD: memos of
`pfdMemoDisplay` plus A/THR OFF on involuntary disconnection, priority sorted, 3 lines.

Covered (code): HI ALT AIRPORT 210000001, AP OFF 220000001, A/THR OFF 220000002, DEST EFOB 221000002,
VHF3 VOICE 230000015, ELEC EXT PWR 241000001/2, RAT OUT 242000001/2, GND SPLRs ARMED 271000001,
CROSSFEED OPEN 280000001/13, DEFUEL / REFUEL / REFUEL PNL 280000003/9/10/11, G/Y ELEC PMP CTL
290000001-4, ENG A-ICE 300000001, WING A-ICE 300000002, ICE NOT DETECTED 300000003, T.O / LDG INHIBIT
314000001/2, AUTO BRK OFF 320000001, PARK BRK ON 320000002, N/W STEER DISC 322000001/2, STROBE LT OFF
333000001, SEAT BELTS 335000001, NO MOBILE 335000003, TRUE NORTH REF 340000001, IR IN ALIGN
340003001-8 / 340003101-6, ADIRS SWTG 340068001, TCAS STBY 343000001, ALT RPTG OFF 343000002, XPDR STBY
343000003, OXY PAX SYS ON 350000002, IGNITION 709000001, APU AVAIL 000017001, APU BLEED 000018001,
T.O memo 000001001-13, LDG memo 000002001-11.

Omitted (no input in the port): SWITCHG PNL 000029001 (ND XFR knob), CAPT/F-O ON RMP 3 and RMP OFF
230000001-3 / 9-12 (RMPs), FMS SWTG 221000001 (knob), GPWS OFF / TAWS FLAP MODE OFF / TAWS G/S MODE OFF
341000001-3 (TAWS panel), COMPANY MSG 460000001 (datalink). Several covered memos have inputs without a
producer today (refuel, elec pumps, N/W steer disc, oxygen masks, true north, VHF 3) and stay off.

## STS lists

`FwsStatusLists` mirrors `fws_limitations_all_phases`, `fws_limitations_appr_ldg`, PFD limitations,
`fws_inop_sys_all_phases`, `fws_inop_sys_appr_ldg`, `fws_inop_sys_redundancy_loss`, `fws_information`,
`fws_alerts_impacting_ldg_perf` (always empty: FBW never fills it) and `ecamStatusNormal`. Assembly:
per-alert deprecated lists of the active procedures (ascending id, `pushKeyUnique` semantics, FBW's
`redundLoss` lists are not merged, like FBW), then the dictionaries with `itemIsActiveConsideringFault
Suppression(item, key, 1.0)`, LAND ANSA removed when LAND ASAP is present, limitations stable-sorted by
colour. Dictionary entries the port can derive:

- INOP SYS: GEN 1-4, GEN 1+2, GEN 3+4, ENG 1-4 BLEED, PART SPLRs, MOST SPLRs, FLAP SYS 1/2, SLAT SYS 1/2,
  FLAPS, SLATS, G / Y HYD SYS, ENG n PMP A+B, ADR 1..1+2+3, TCAS 1 / 2 / 1+2, RA SYS A..A+B+C (redundancy
  loss for the singles), IR 1..1+2+3, ENG 2 / 3 / 2+3 REVERSER. Not derivable (stay off): AUTOLAND,
  CAB PRESS SYS, PACK 1+2, ROLL OUT, FMC / FMS, RMP / VHF / HF, fire detection, SEC / PRIM / FCDC, RUDDER
  TRIM, F/CTL PROT, FWS, BTV / ROW / ROP, GPWS, TERR / ADS-B / TAWS, ARPT NAV.
- LIMITATIONS: LAND ASAP (`landAsap`: fire detected or all engines failed, in flight), APU BLEED DO NOT
  USE (ENG 1 fire pb), SLATS SLOW, FLAPS SLOW. Not derivable: NO COM AVAIL (RMPs), direct / alternate law
  speeds, LDG DIST / PERF AFFECTED (FCDC).
- INFORMATION: every `FwsInformation` entry depends on FCDC / law / roll-out signals the port does not
  have; INFO lines come only from the per-alert lists.

## Normal checklist sensed items (`FwsNormalChecklists.sensedItems`)

`fws_signals_normal_sensed(proc, out)` returns -1 (null, not sensed) / 0 / 1 per item:

| Procedure | Sensed items |
| --- | --- |
| 1000001 COCKPIT PREPARATION | 2 SEAT BELTS |
| 1000002 BEFORE START | 2 BEACON |
| 1000003 AFTER START | 2 RUDDER TRIM (abs < 0.35 deg) |
| 1000004 TAXI | 3 T.O (always false), 4 SEAT BELTS, 5 GND SPLRs, 6 FLAPS (handle 1..3), 7 AUTO BRK RTO, 8 T.O CONFIG |
| 1000010 APPROACH | 1 SEAT BELTS |
| 1000012 LANDING | 0 (false), 1 SEAT BELTS, 2 LDG GEAR, 3 GND SPLRs, 4 FLAPS (FULL, or 3 with CONF 3 landing) |
| 1000013 PARKING | 1 ENGINES (all masters off), 3 FUEL PUMPs (all feed pumps and masters off) |
| 1000014 SECURING THE AIRCRAFT | 0 OXYGEN (crew oxy off), 1 EMER EXIT LIGHT (no input, false) |
| 1000005/6/7/8/9/11 | none sensed |

## Other outputs (`FwsSignalOutputs`)

- `flight_phase`, `to_memo`, `ldg_memo`, `on_ground` (FWS on-ground logic from LGCIU hardwire + RAs),
  `show_takeoff_inhibit`, `show_landing_inhibit`, `to_config_normal`, `to_config_test`.
- `normal_attention_getter_eng[4]` / `abnormal_primary_eng_attention_getter[4]` (`eng<n>StartOrCrank`,
  `eng<n>PrimaryAbnormalParams`).
- `sd_page_when_unselected` (FwsSystemDisplayLogic: phase pages, APU / ENG checks, CRZ and F/CTL timers,
  SFAIL override).
- `master_warning_from_ap_off` (`requestMasterWarningFromApOff`, cleared by MASTER WARN), aural requests
  `aural_cavalry_charge` (AP disconnection, attenuated first charge in FBW), `aural_cchord_once` /
  `aural_cchord_cont` (altitude alert), `aural_v1` (V1 callout pulse in phase 4). Per-alert overrides are
  in `FwsAlertEval.aural` (CRC for fires / T.O config warnings while the condition holds, cavalry charge for
  AP OFF, single chime for A/THR OFF, none for AUTO BRK OFF).
- `stall_warning`, `land_asap`, `lg_lever_red_arrow`, `checklist_reset_50min`
  (`shutDownFor50MinutesCheckListReset`), `autothrust_involuntary_pfd_memo`.
- `afs_alpha_floor`, `afs_toga_lk`, `afs_thr_lk`: pass-through of the guidance FMA (`thrust_mode` A.FLOOR /
  TOGA LK, A3 THR LK); the FBW FWS does not compute them.

## Deviations from FBW

Obvious upstream defects are fixed (tag in the source):

1. [DEV 1] `eng4TLAFullPwr` reads TLA 3 -> TLA 4.
2. [DEV 2] `FwsFlightPhases.toConfigTest` is never assigned in FBW (T.O memo only after the 120 s timer);
   the T.O CONFIG 0.5 s pulse now sets the T.O memo in phase 2.
3. [DEV 3] `flightPhaseEndedPulseNode` is always written false (a RCL press would suppress T.O / LDG
   INHIBIT forever); the override is reset on a flight phase change.
4. [DEV 4] `thrustLever3Idle / 4Idle` read TLA 2 and `thrustLever2/3Reverse` is `TLA 2 > 0`; per-engine
   idle and reverse = TLA < 0 are used.
5. [DEV 5] ENG 4 PMP A fault compares objects (`!this.eng4APumpAuto`, `this.engine4Running`); fixed.
6. [DEV 6] the four feed tank low signals share `feedTank1LowConfirm`; one 30 s confirm node per tank.
7. [DEV 7] `flapConfigWarning` reads `slatConfigSr`; `flapConfigSr` is used.
8. [DEV 8] `eng3/4NotStartingConfNode` use `master && !STARTING` while engines 1/2 use
   `running && !STARTING`; the engine 1/2 form is used for all four.
9. [DEV 9] ENG 2 FIRE (in flight) shows the ENG 1 agent-1 timer; own engine used.
10. [DEV 10] `eng4BleedInop = gen4Inop`; `eng4Out` like engines 1-3.
11. [DEV 11] the T.O CONFIG pulse node is written twice per cycle (buffer, then raw), which cancels the
    pulse in FBW; it is written once with the raw button level.
12. [DEV 12] AFTER START rudder trim compares the signed trim (`< 0.35`); `|trim| < 0.35` is used.

Kept FBW quirks: `GEAR TOTAL PCT EXTENDED` read in percent and compared with 0.95 (LDG memo, SD page);
`speedBrakeCommand5sConfirm` written twice per cycle (2.5 s effective); OXY PAX SYS ON always returns code
350000002 (`codesToReturn` evaluated at construction); the LAND ASAP / ANSA push from
`proc.recommendation` never fires (`pushKeyUnique` called without a state); AP OFF voluntary escalates to
the AP OFF warning when the instinctive pb is not pressed a second time within 1.9 s; Y RSVR LEVEL LO and
G RSVR LEVEL LO check the yellow elec pumps; `alertsImpactingLdgPerfKeys` stays empty.

Port adaptations: flight phases use the inputs of the current cycle (FBW reads subjects set in the previous
125 ms cycle); LGCIU discrete words are assumed valid and in agreement with the gear hardwire; OVERSPEED
uses the FWS CAS / Mach limits plus the ADR overspeed discretes (no ADR 3 VMAX word); GND SPLRs NOT ARMED
uses the main gear down-lock for LGCIU bit 29; HI ALT AIRPORT uses 0 for the manual cabin altitude target;
the SFCC conf index is derived from the surface angles; outflow valves, ram air, cabin air extract,
FCDC / PRIM / SEC, TAWS and RMP signals are absent (healthy / false).
