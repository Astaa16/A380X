# A380X electrical network, cold & dark and power-up: implementation spec

Status: research/specification only (no code in this change). Target readers: native C developers
of `native/avionics` (`A380XAvionics` plugin). Goal: a ToLiss-grade simulation of the A380
electrical network that decides, **per bus**, what is powered. Every display, panel lamp and
system in the port then follows that decision instead of `sim/cockpit2/switches/avionics_power_on`.

The reference behaviour is the FlyByWire A380X Rust electrical model (GPL-3.0, same licence as
this port). Public Airbus/FBW documentation fills the gaps FBW leaves. Where neither source is
conclusive, the gap is marked as an assumption and collected in section 12.

---

## 0. Conventions

### 0.1 Source keys

| Key | Path (relative to `/Volumes/Untitled/A380X-XPlane12/`) |
| --- | --- |
| `AC` | `upstream-aircraft/fbw-a380x/src/wasm/systems/a380_systems/src/electrical/alternating_current.rs` |
| `DC` | `upstream-aircraft/fbw-a380x/src/wasm/systems/a380_systems/src/electrical/direct_current.rs` |
| `EM` | `upstream-aircraft/fbw-a380x/src/wasm/systems/a380_systems/src/electrical/mod.rs` |
| `GAL` | `upstream-aircraft/fbw-a380x/src/wasm/systems/a380_systems/src/electrical/galley.rs` |
| `LIB` | `upstream-aircraft/fbw-a380x/src/wasm/systems/a380_systems/src/lib.rs` |
| `NAV` | `upstream-aircraft/fbw-a380x/src/wasm/systems/a380_systems/src/navigation.rs` |
| `CDSY` | `upstream-aircraft/fbw-a380x/src/wasm/systems/a380_systems/src/control_display_system.rs` |
| `ADCN` | `upstream-aircraft/fbw-a380x/src/wasm/systems/a380_systems/src/avionics_data_communication_network.rs` |
| `PWR` | `upstream-aircraft/fbw-a380x/src/wasm/systems/a380_systems/src/power_consumption.rs` |
| `WASM` | `upstream-aircraft/fbw-a380x/src/wasm/systems/a380_systems_wasm/src/lib.rs` |
| `FBWI` | `upstream-aircraft/fbw-a380x/src/wasm/fbw_a380/src/FlyByWireInterface.cpp` |
| `BAT` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/electrical/battery.rs` |
| `BCRU` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/electrical/battery_charge_rectifier_unit.rs` |
| `TR` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/electrical/transformer_rectifier.rs` |
| `SINV` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/electrical/static_inverter.rs` |
| `EXTP` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/electrical/external_power_source.rs` |
| `EGEN` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/electrical/engine_generator.rs` |
| `EMGEN` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/electrical/emergency_generator.rs` |
| `RAT` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/electrical/ram_air_turbine.rs` |
| `ECOM` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/electrical/mod.rs` |
| `SHR` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/shared/mod.rs` |
| `ECB` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/apu/electronic_control_box.rs` |
| `PW980` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/apu/pw980.rs` |
| `FLAP` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/apu/air_intake_flap.rs` |
| `ADIRS` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/navigation/adirs.rs` |
| `OVHD` | `upstream-aircraft/fbw-common/src/wasm/systems/systems/src/overhead/mod.rs` |
| `CDS` | `upstream-aircraft/fbw-a380x/src/systems/instruments/src/MsfsAvionicsCommon/CdsDisplayUnit.tsx` |
| `ISISDU` | `upstream-aircraft/fbw-a380x/src/systems/instruments/src/ISISlegacy/ISISDisplayUnit.tsx` |
| `OIT` | `upstream-aircraft/fbw-a380x/src/systems/instruments/src/OIT/OitDisplayUnit.tsx` |
| `RMP` | `upstream-aircraft/fbw-a380x/src/systems/instruments/src/RMP/Systems/RmpStateController.ts` |
| `FWS` | `upstream-aircraft/fbw-a380x/src/systems/systems-host/CpiomC/FlightWarningSystem/FwsCore.ts` |
| `AMU` | `upstream-aircraft/fbw-a380x/src/systems/systems-host/Misc/Communications/AudioManagementUnit.ts` |
| `RTPI` | `upstream-aircraft/fbw-a380x/src/systems/instruments/src/RTPI/index.tsx` |
| `BATD` | `upstream-aircraft/fbw-common/src/systems/instruments/src/BAT/BatDisplay.tsx` and `fbw-a380x/src/systems/instruments/src/BAT/index.tsx` |
| `SDE` | `upstream-aircraft/fbw-a380x/src/systems/instruments/src/SD/Pages/ElecAc/elements/*.tsx`, `.../ElecDc/elements/*.tsx` |
| `BEH` | `upstream-aircraft/fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire/Part_Interior_Cockpit/model/A380_Cockpit_Behavior.xml` |
| `BEHX` | same directory, `behaviour/legacy/generated/A32NX_Interior_Elec.xml` |
| `PRESET` | `upstream-aircraft/fbw-a380x/src/base/flybywire-aircraft-a380-842/config/a380x/a380-842/aircraft_preset_procedures.xml` |
| `GPUM` | `upstream-aircraft/fbw-common/src/systems/shared/src/GPUManagement.ts` |
| `PORT:<file>` | `xplane-port/<file>` |
| `XPDR` | X-Plane 12 `Resources/plugins/DataRefs.txt` / `Commands.txt` of the local install (`~/Library/Application Support/Steam/steamapps/common/X-Plane 12/`) |
| `DOC-ELEC` | https://docs.flybywiresim.com/pilots-corner/a380x/a380x-briefing/flight-deck/ovhd/elec/ |
| `DOC-PREP` | https://docs.flybywiresim.com/pilots-corner/a380x/a380x-beginner-guide/02_cockpit-preparation/ |
| `DOC-DOWN` | https://docs.flybywiresim.com/pilots-corner/a380x/a380x-beginner-guide/09_powering-down/ |
| `TUT` | https://simtuts.com/guides/flybywire-a380x-cold-dark-startup (third-party, low weight) |
| `IEEE` | IEEE Electrification Magazine, June 2014, p.54 (A380 "more electric" overview) |

`file:N` or `file:N-M` gives line numbers in the upstream checkout as of this research. Line numbers in
`AC`/`DC` refer to the original files, not to blank-stripped excerpts.

### 0.2 Confidence

* **H**: read directly from FBW code or the local X-Plane dataref catalogue. Reproduce exactly.
* **M**: FBW simplification, public documentation paraphrase, or a straightforward inference.
  Reproduce unless better data appears.
* **L**: an assumption needed to fill a gap. Implement it behind a named constant and list it in section 12.

### 0.3 Naming pitfall (read first)

FBW's A380 reuses A320 bus *types* with the wrong names (`AC:43-51`, H):

| Real A380 bus | FBW element | FBW L:var written | Use in the port |
| --- | --- | --- | --- |
| AC ESS (400XP) | `ac_ess_bus` = `AlternatingCurrentEssentialShed` | `L:A32NX_ELEC_AC_ESS_SHED_BUS_IS_POWERED` | `ac_ess` |
| AC EMER (491XP) | `ac_emer_bus` = `AlternatingCurrentEssential` | `L:A32NX_ELEC_AC_ESS_BUS_IS_POWERED` | `ac_emer` |

So every FBW consumer that tests `AC_ESS` (EGPWC `LIB:151`, RA3 `NAV:92`, FWS `FWS:3027`, F/O OIT `OIT:41`)
is really on **AC EMER 491XP**. Every consumer that tests `AC_ESS_SHED` (APU START lamp, EMER GEN 1 LINE
lamp, autobrake/gear panel lamps, `BEH:1191-1207`, `BEH:1776+`) is on **AC ESS 400XP**. Translate when
you port a mapping. The port must use the real names.

---

## 1. Summary of decisions

1. Add a new XPLM-free core module, `a380x_elec.{h,c}`, plus an adapter `a380x_elec_adapter.inc`.
   The core reproduces the FBW contactor logic (sections 3-4) at flight-loop rate, with no dependency on
   X-Plane's electrical model.
2. Every port consumer asks the core "is bus X powered" (section 6). `avionics_power_on`,
   `bus_volts[0] >= 18`, and `battery_voltage_actual_volts >= 18` stop being power criteria.
3. X-Plane's own electrical system is kept alive as a slave. The adapter drives `battery_on`,
   `generator_on`, `GPU_generator_on`, `APU_generator_on`, `avionics_power_on` and the stock battery charge
   so stock-dependent parts never contradict the native network (section 9). X-Plane has no electrical
   override dataref (XPDR, H), so the slave approach is mandatory.
4. Spawn state is chosen once per aircraft load. It is **Cold & Dark** on the ground with engines stopped
   and **Ready** when engines are running or airborne. A flyPad preference can make the default spawn
   **Turnaround** instead. flyPad presets apply the FBW procedure lists (section 8).
5. Display units implement the FBW CDS state machine: Off, Thales boot (0.25-0.45 s), self-test
   (15 s default), On, Standby (10 s hold). The ISIS powers up through a 90 s INIT (section 6.3).

---

## 2. Network topology

### 2.1 Power sources

| Source | Rating / output (FBW) | Feeds | Availability rule | Src / conf |
| --- | --- | --- | --- | --- |
| GEN 1-4 (variable-frequency gen, "IDG/DRIVE" on panel) | 150 kW max true power, 115 V, normal 360-800 Hz, PF 0.8 for load % | own AC bus via 990XU*n* | `pb ON && !fire pb released && drive connected && !failed && f in 360..800 Hz for >= 0.5 s && V 110..120` | `AC:389-396`, `EGEN:75-137,160-163,205-222` H. Real A380: 4x150 kVA VFG, `IEEE` M |
| VFG frequency | `f = gearbox_rpm * 1.95 * 4 / 120` Hz (direct drive) | | gearbox speed from engine; N2 drives temperature | `EGEN:89-95,343,400-405` H |
| DRIVE disconnect | latched, cannot be reconnected in flight | | `IDG pb released` ⇒ `drive.disconnect()` permanently | `EGEN:82-85`, `DOC-ELEC` (ground maintenance only) H |
| APU GEN A (1), B (2) | 115 V, 400 Hz (390-410 normal), 120 kW-class (tests load 120 kW/0.8) | AC buses via 990XS1/2, SIC 900XU | output when `N >= 77 %` && !fault && !emergency shutdown; used when `APU GEN pb ON` && within params | `PW980:819,870-895`, `PW980:1192` H; real 2x120 kVA `IEEE` M |
| EXT 1-4 | 115 V, 400 Hz | own AC bus via 990XG*n*; EXT 1 also feeds ground-service | `connected` (EFB/ground crew) and `EXT pb ON` | `EXTP:43-49,89-99`, `AC:561-567` H |
| Emergency generator (RAT) | 115 V/400 Hz; GCU max power 0 kW <= 6900 rpm, 50 kW @7500, 70 kW @8000-10000, 0 kW >= 11000 rpm; supplies when rpm > 2000 or P > 100 W | AC ESS 400XP via 5XE | only in emergency config (section 3.9) | `RAT:144-197`, `EMGEN:24-67`, `EM:61,79-84` H |
| Static inverter | 115 V / 400 Hz output when input >= 16 V DC | AC EMER 491XP via 3XB.2 | 7XB closed (section 3.6) | `SINV:36-58,84-101`, `DC:289-301` H |
| BAT 1, BAT 2, ESS BAT, APU BAT | FBW: 23 Ah each, 0-28.96 V curve (section 3.10) | hot buses always; DC buses via contactors | — | `BAT:27-35,98-120` H. Real A380 capacity not verified (often quoted 50 Ah) L |
| BCRU 1, BCRU 2, BCRU ESS ("TR 1/2/ESS") | 28 V; "normal" 27-32 V; overcurrent 300 A > 5 s or > 450 A | DC 1, DC 2, DC ESS | input AC powered; see state machine 3.7 | `BCRU:39-216` H |
| TR APU ("TR 4") | 30.2 V no-load, 0.0135 Ω internal R; normal 25-31 V | DC APU bus 309PP via 7PU | input = AC 4 | `TR:27-66`, `AC:113-114`, `DC:203-206` H |

### 2.2 Buses

| Port id (`A380XElecBus`) | Real id | FBW type / L:var name (`L:A32NX_ELEC_<name>_BUS_IS_POWERED`) | Normal supply | Src / conf |
| --- | --- | --- | --- | --- |
| `AC1`..`AC4` | 1XP..4XP | `AC_1`..`AC_4` | own GEN / EXT / APU GEN / tie | `AC:41-42` H |
| `AC_ESS` | 400XP | `AC_ESS_SHED` (see 0.3) | AC1 (3XC1) or AC4 (3XC2), or EMER GEN (5XE) | `AC:43-47,103-123` H |
| `AC_EMER` | 491XP | `AC_ESS` | AC ESS via 3XB.1, or static inverter via 3XB.2 | `AC:48-51,124,164-165,178-190` H |
| `AC_EHA` | 247XP | `247XP` | AC3 via 911XN, or AC ESS via 911XH | `AC:52-55,126-127,168-175` H |
| `AC_GND_SVC` | (AC GND/FLT SERVICE) | `AC_GND_FLT_SVC` | AC3 via 991PU2, or EXT 1 via 991PU6 | `AC:56-59,133-153` H |
| `DC1` | 100PP | `DC_1` | BCRU 1 (990PU1); tie 980PC/6PC2 | `DC:66-67` H |
| `DC2` | 200PP | `DC_2` | BCRU 2 (990PU2); tie 980PC | `DC:68-69` H |
| `DC_ESS` | 400PP | `DC_ESS` | BCRU ESS (6PE); BAT1 via ELC 6PC1; ESS BAT 6PB3; DC1 via 6PC2 | `DC:70-71` H |
| `DC_ESS_INFLT` | 108PH (DC ESS in-flight sub-bus) | `108PH` | DC ESS via 10PH | `DC:127-132,341-355` H |
| `DC_EHA` | 247PP | `247PP` | DC2 via 970PN2, or DC ESS via 14PH (TEFO) | `DC:72-73,303-309` H |
| `DC_APU` | 309PP | `309PP` | TR APU (7PU) or APU BAT (5PB) | `DC:74-77,203-206,272-280` H |
| `APU_START` | 49-42-00 sub-bus | `SUB_49-42-00` (not exported) | DC APU via 10KA+5KA | `DC:16,282-287` H |
| `HOT1`,`HOT2` | — | `DC_HOT_1`, `DC_HOT_2` | BAT1, BAT2 permanently | `DC:89-90,238,247` H |
| `HOT_ESS` | 703PP | `DC_HOT_3` | ESS BAT permanently | `DC:91-92,265` H |
| `HOT_APU` | 709PP | `DC_HOT_4` | APU BAT permanently | `DC:93-94,279` H |
| `DC_GND_SVC` | 260PP | `DC_GND_FLT_SVC` | BCRU 2 via 990PX (ground servicing) or DC2 via 970PN | `DC:119-125,311-330` H |
| `ESS_FUEL_A/B` | 501PP/503PP | sub | DC ESS (20PR.2) normally; HOT ESS when refuel-on-battery | `DC:134-138,357-376` H (refuel-on-bat TODO in FBW) |
| `REFUEL_BAT` | 502PP | `502PP` | DC2 (1000PR3.2) normally; HOT2 in refuel-on-bat | same H |

### 2.3 Contactors (FBW ids → L:var `L:A32NX_ELEC_CONTACTOR_<id>_IS_CLOSED`, `ECOM:67-91`)

| Id | Function | Rule section |
| --- | --- | --- |
| 990XU1-4 | GEN n line contactor | 3.2 |
| 990XG1-4 | EXT n contactor | 3.2 |
| 990XS1/2 | APU GEN A/B contactor | 3.2 |
| 980XU1-4 | bus-tie contactor of AC n to its side tie-bus | 3.2 |
| 980XU5/6 | left/right cross tie (AC2↔AC3 path) | 3.2 |
| 900XU | system isolation contactor (BTC7) between left and right tie-buses | 3.2 |
| 3XC1 / 3XC2 | AC ESS feed from AC1 / AC4 | 3.3 |
| 5XE | emergency generator → AC ESS | 3.4 |
| 3XB.1 / 3XB.2 | AC EMER from AC ESS / from static inverter (one 2-way switch) | 3.4 |
| 911XN / 911XH | AC EHA from AC3 / from AC ESS | 3.4 |
| 991PU6 / 991PU2 | EXT 1 → ground service + TR2 / AC3 → TR2 + ground service | 3.5 |
| 990PU1, 990PU2, 6PE | BCRU 1/2/ESS line contactors | 3.7 |
| 7PU | TR APU line contactor | 3.7 |
| 990PB1, 990PB2 | BAT 1 / BAT 2 battery contactors | 3.7 |
| 6PC1 | BAT 1 emergency line contactor (ELC) to DC ESS | 3.7 |
| 6PB3 | ESS BAT contactor | 3.7 |
| 5PB | APU BAT contactor | 3.7 |
| 6PC2 / 980PC | DC1↔DC ESS / DC1↔DC2 tie | 3.7 |
| 7XB | static inverter input | 3.6 |
| 970PN2 / 14PH | DC EHA from DC2 / from DC ESS | 3.7 |
| 990PX / 970PN | DC GND SVC from BCRU2 / from DC2 | 3.5 |
| 10PH | DC ESS → 108PH | 3.8 |
| 10KA_AND_5KA | APU start motor | 3.7 |

### 2.4 Topology sketch (FBW wiring, H)

```
 GEN1  EXT1   GEN2  EXT2       APU GEN A   APU GEN B      GEN3  EXT3   GEN4  EXT4
  |990XU1|990XG1|990XU2|990XG2   |990XS1      |990XS2       |990XU3|990XG3|990XU4|990XG4
 [ AC 1 ]      [ AC 2 ]          |            |            [ AC 3 ]      [ AC 4 ]
   |980XU1       |980XU2 --980XU5--+--900XU(SIC)--+--980XU6-- |980XU3       |980XU4
   +-------------+--- left tie ----+              +--- right tie ----+--------+
   |                   AC2 also reaches the right tie via 980XU5-980XU6       |
 3XC1 (NORM)                                                            3XC2 (AC1 lost / ALTN)
   +-----------------------------> [ AC ESS 400XP ] <---------------------+
                                       ^ 5XE EMER GEN (RAT)
                                       | 3XB.1                          static inverter --3XB.2--+
                                  [ AC EMER 491XP ] <-------------------------------------------+
 AC2 -> BCRU1 -990PU1-> [DC1] -980PC- [DC2] <-990PU2- BCRU2 <- (AC3 via 991PU2 | EXT1 via 991PU6)
                          |6PC2                       BCRU ESS <- AC ESS ; -6PE-> [DC ESS 400PP]
 BAT1 -990PB1-> DC1 ; BAT1 -6PC1(ELC)-> DC ESS ; ESS BAT -6PB3-> DC ESS ; BAT2 -990PB2-> DC2
 AC4 -> TR APU -7PU-> [DC APU 309PP] <-5PB- APU BAT ; DC APU -10KA/5KA-> APU START MOTOR
 DC ESS -10PH-> [108PH]   DC2 -970PN2-> [DC EHA 247PP] <-14PH- DC ESS
 HOT1=BAT1  HOT2=BAT2  HOT ESS(703PP)=ESS BAT  HOT APU(709PP)=APU BAT   (always)
```

Exact flow statements: `AC:126-153,164-175,522-547`, `DC:160-330`.

---

## 3. Network logic (state machines and truth tables)

Evaluate in the FBW order once per update. The order matters because later steps read contactors that
earlier steps closed (`EM:98-232`):

1. Main sources and AC 1-4 (`AC:70-94`)
2. Emergency elec state, RAT controller, GCU, RAT physics (33 ms substeps), emergency generator (`EM:122-144`)
3. AC ESS feed, AC ESS, TR APU, 5XE, 3XB.1, 911XN, ground service (`AC:96-156`)
4. DC network (`DC:149-331`)
5. Static inverter to AC EMER, 911XH (`AC:158-176`)
6. Galleys (`EM:170-173`)
7. Relay logic: emer evac, flight condition, TEFO, emer config (`EM:176-208`)
8. Sub-buses 108PH, fuel and refuel buses (`DC:333-377`)
9. Panel lamps (`EM:370-388,471-480`)

A bus is "powered" when any closed path leads to a live source. Implement this as a fixed-order
assignment of `{powered, volts, hz, origin}` per node, in the same order as FBW's `electricity.flow()`
calls. The FBW graph is a union-find of conductive elements. The order-dependent formulation below gives
the same powered/unpowered answer for every configuration in the FBW tests (`EM:600-3110`).

### 3.1 Source availability (`AC:549-583`, H)

```
gen_avail[i]     = vfg[i].output_within_normal_parameters()                 // 2.1
ext_avail[i]     = ext_pb_on[i] && ext_connected[i]                          // 115 V / 400 Hz
apu_gen_avail[j] = apu_gen_pb_on[j] && apu_gen[j].N >= 77 % && freq/volt normal
in_flight        = adiru1.low_speed_warning_1 || adiru3.low_speed_warning_1   // set CAS > 54 kt, reset < 50 kt (NAV:24-27, ADIRS:785-801)
if (in_flight) {                         // only ONE APU generator may supply in flight
   L = gen_avail[0]+gen_avail[1]; R = gen_avail[2]+gen_avail[3];
   apu_gen_avail[0] &= (L <= R || !apu_gen_avail[1]);
   apu_gen_avail[1] &= (L >  R || !apu_gen_avail[0]);
}
```
Port note: the discrete exists only while the ADR is on and initialised (`ADIRS:785-811`). Use the port
ADIRS ADR CAS with the same hysteresis: set above 54 kt, reset below 50 kt. If ADRs 1 and 3 are off,
the discrete is false, so the aircraft behaves as on ground (H). `lsw_3` (used in 3.9) is the same
rule with 100/104 kt.

### 3.2 AC 1-4 source selection, priority tables (`AC:297-385,585-634`, H)

The algorithm is a row scan over a priority table. `powered_by[bus]` starts as `NONE`.
`can_power_other[bus]` becomes true when the bus is fed by its own GEN or EXT. It becomes false again when
another bus takes power from it, so one GEN or EXT feeds at most two buses. Each APU generator feeds at
most two buses per row scan (`apu_count < 2`). The scan stops when all four buses have a source.

**Normal table** (row r, column = AC bus 1..4):

| Row | AC1 | AC2 | AC3 | AC4 |
| --- | --- | --- | --- | --- |
| 1 | GEN | GEN | GEN | GEN |
| 2 | EXT | EXT | EXT | EXT |
| 3 | APU A | APU A | APU B | APU B |
| 4 | APU B | APU B | APU A | APU A |
| 5 | AC2 | AC1 | AC4 | AC3 |
| 6 | AC2 | AC1 | AC4 | AC3 |
| 7 | AC4 | AC3 | AC2 | AC1 |
| 8 | AC4 | AC3 | AC2 | AC1 |

The same-side gen/ext comes before the APU. The cross-side supply comes last. FBW comment `AC:297-305`:
own GEN > own EXT > same-side APU gen > other APU gen > other gen same side > other EXT same side >
opposite gen > opposite EXT.

**Special table** applies when no EXT is available, exactly one GEN, exactly one APU gen, and that GEN is
not the *outer* engine on the same side as that APU gen (`!(gen1 && apuA || gen4 && apuB)`):

| Row | AC1 | AC2 | AC3 | AC4 |
| --- | --- | --- | --- | --- |
| 1 | GEN | GEN | GEN | GEN |
| 2 | EXT | EXT | EXT | EXT |
| 3 | APU A | AC1 | AC4 | APU B |
| 4 | AC2 | AC1 | AC4 | AC3 |
| 5 | AC2 | APU A | APU B | AC3 |
| 6 | APU B | AC3 | AC2 | APU A |
| 7 | — | AC3 | AC2 | — |
| 8 | — | APU B | APU A | — |

**Contactor derivation** from `powered_by[]` (`AC:434-507`):

```
990XU[i] = powered_by[i]==GEN;  990XG[i] = powered_by[i]==EXT
self[i]  = powered_by[i] in {GEN, EXT, NONE}
for outer buses i in {1,4}:
    980XU[i] = !self[i] || (some other bus is powered_by AC(i))
for inner buses i in {2,3}, opp = (i==2 ? 3 : 2):
    from_opp = powered_by[i]==AC(opp)
    980XU[i]          = (!self[i] && !from_opp) || (AC1 or AC4 is powered_by AC(i))
    980XU[i==2?5:6]   = BUS_TIE_AUTO && ((!self[i] && from_opp) || powered_by[opp]==AC(i))
990XS[j] = any bus powered_by APU(j)
900XU    = BUS_TIE_AUTO && ( AC1|AC2 powered_by APU B || AC3|AC4 powered_by APU A
                            || AC1 powered_by AC4 || AC4 powered_by AC1 )
```
**BUS TIE OFF**: 980XU5/6 and 900XU cannot close, so the left (AC1/AC2) and right (AC3/AC4) halves
cannot feed each other (`DOC-ELEC`: "separates AC 1+2 from AC 3+4 and DC1 from DC2"). The priority
scan itself does not look at BUS TIE. When the pb is OFF, a bus whose chosen source sits on the other
side ends up unpowered because the path is open. Reproduce this exactly by doing the path
propagation, not just copying `powered_by`.

**Resulting ground configurations** (FBW unit tests, `EM:775-1290`, H):

| Configuration (all BAT AUTO, ground, 0 kt) | AC1 | AC2 | AC3 | AC4 | AC ESS | AC EMER | DC1 | DC2 | DC ESS | DC APU |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Batteries only | – | – | – | – | – | STAT INV | – | – | BAT1+ESS | APU BAT |
| Batteries, EXT connected, no EXT pb | – | – | – | – | – | STAT INV | – | – | BAT1+ESS | APU BAT (+ GND SVC from EXT1, DC GND SVC from TR2) |
| EXT 2 ON only | EXT2 | EXT2 | – | – | EXT2 | EXT2 | TR1 | TR1 (980PC) | TR ESS | APU BAT |
| EXT 2+3 ON | EXT2 | EXT2 | EXT3 | EXT3 | EXT | EXT | TR1 | TR2 | TR ESS | TR APU |
| EXT 2 + APU | APU A | EXT2 | APU B | APU B | APU A | APU A | TR1 | TR2 | TR ESS | TR APU |
| EXT 2+3 + APU | APU A | EXT2 | EXT3 | APU B | APU A | APU A | TR1 | TR2 | TR ESS | TR APU |
| APU only | APU A | APU A | APU B | APU B | APU A | APU A | TR1 | TR2 | TR ESS | TR APU |
| 4 GEN | GEN1 | GEN2 | GEN3 | GEN4 | GEN1 | GEN1 | TR1 | TR2 | TR ESS | TR APU |

One EXT supplies only two buses. The FBW doc says at least two external power units are needed for the
full network (`DOC-PREP`, M). SOP order is EXT 2, 3, 1, 4 (`PRESET:68-83`).

### 3.3 AC ESS feed (`AC:684-697`, H; `DOC-ELEC` M)

```
3XC1 = AC1_powered && AC_ESS_FEED == NORMAL
3XC2 = AC4_powered && (!AC1_powered || AC_ESS_FEED == ALTN)
AC_ESS(400XP) <= 3XC1 | 3XC2 | 5XE
```
So the feed transfers to AC4 automatically when AC1 is lost. ALTN forces AC4.

### 3.4 Emergency, AC EMER and EHA (`AC:116-127,158-190`, H)

```
5XE    = !any(AC1..4) && emer_gen.output_within_normal_parameters()
src    = any(AC1..4) || emer_gen_supplying
3XB.1  = src ; 3XB.2 = !src            // AC EMER from AC ESS or static inverter
911XN  = AC3_powered                    // AC EHA from AC3
911XH  = DC_ESS_powered && (TEFO || !AC3_powered)   // AC EHA from AC ESS
```

### 3.5 Ground servicing (`AC:129-153`, `DC:311-330`, `BCRU:79-99`, H)

```
991PU6 = !any(AC1..4) && EXT1_connected      // NOTE: EXT 1 connected, pb not required
991PU2 = AC3_powered
AC_GND_SVC <= 991PU6 | 991PU2 ;  BCRU2 input <= 991PU6 | 991PU2
ground_servicing = 991PU6
BCRU2 in ground servicing: 990PU2 (to DC2) open; 990PX (to DC GND SVC) closed when input powered
970PN = DC2_powered && 990PX open
```
Consequence (`EM:677-718`, H): with all BAT pbs OFF and only a GPU plugged in (EXT pbs OFF), the AC and DC
ground/flight service buses are live (cabin/cargo/service lighting) while the cockpit stays dark. FBW
assumes the galley "ground service" button is ON (`AC:129-132`).

### 3.6 Static inverter (`DC:289-301`, `SINV:51-58`, H)

```
7XB = !AC_ESS(400XP)_powered && 6PC1(ELC) closed && 6PB3 closed
SI input = DC ESS / BAT1-ELC / ESS BAT ; output 115 V 400 Hz if input >= 16 V
AC_EMER <= SI via 3XB.2 (when no AC main and no EMER GEN)
```
On batteries only, AC EMER is therefore live from the static inverter (`EM:775-824`).

### 3.7 DC network and batteries

**BCRU (TR1, TR2, TR ESS) line contactor** (`BCRU:68-99`, H):

```
loss_of_ac += dt unless (input powered || leaving ground-servicing)
line_contactor = !ground_servicing && (input_powered || (line_contactor && loss_of_ac < 3 s))
battery_soc_low = battery_voltage < 24.5 V          // "SOC < 20 %"
```
BCRU inputs: TR1 ← AC2 (`AC:242-244`), TR2 ← 991PU2/991PU6 (`AC:246-252`), TR ESS ← AC ESS 400XP
(`AC:254-256`). Outputs: TR1 → 990PU1 → DC1, TR2 → 990PU2 → DC2, TR ESS → 6PE → DC ESS.

**BCRU battery-contactor state machine** (`BCRU:218-275`, H). `emergency_config` is input from 3.9.

| State | Condition (evaluated in order) | Next |
| --- | --- | --- |
| BatOff | `bat_pb_auto || emergency_config` | Open |
| Open | `emergency_config || ground_servicing` | Open |
| Open | `!bat_pb_auto` | BatOff |
| Open | `loss_of_ac == 0` (AC input present now) | Closed |
| Closed | `emergency_config || ground_servicing || loss_of_ac >= 5 s` | Open |
| Closed | `!bat_pb_auto` | BatOff |

Battery contactor closed ⇔ state == Closed. Initial state Open. BAT 1 → 990PB1 → DC1 and
BAT 2 → 990PB2 → DC2 follow this, so batteries connect only for charging when AC is present. On AC loss
they reconnect through the emergency paths below.

**BAT 1 ELC 6PC1 and ESS BAT 6PB3** (`DC:224-270`, H):

```
elc  = (BAT1_AUTO && ESS_AUTO && !bcru1.soc_low && !any(AC1..4)) || rat_should_deploy
6PC1 = elc                                    // BAT1 -> DC ESS, BCRU1 state also told elc as emergency_config
ess_lc = (BAT1_AUTO && ESS_AUTO && !bcru_ess.soc_low && !any(AC1..4)) || rat_should_deploy
tr_ess_emer = (!DC1_powered && emer_elec_active) || rat_should_deploy
6PB3 = bcru_ess.state==Closed || ess_lc
```
Both BAT 1 **and** ESS BAT must be AUTO before DC ESS comes alive on batteries. This is why the SOP
order is BAT 1, ESS, BAT 2, APU (`PRESET:52-66`). BAT 2 alone powers nothing but HOT 2 until AC exists.
FBW TODOs: ELC should also open on BAT 1 fault (`DC:225,250-251`).

**APU battery** (`DC:203-206,272-287`, H):

```
7PU  = TR_APU_powered (AC4)
5PB  = APU_BAT_AUTO && (7PU open || apu_bat.charge <= 20 Ah)
10KA_5KA (start motor) = DC_APU_powered && ECB.start_contactor_signal == CLOSE
```

**DC ties** (`DC:208-222`, H):

```
6PC2 (DC1->DC ESS) = 980PC open && 6PE open && DC1_powered
980PC (DC1<->DC2)  = BUS_TIE_AUTO && 6PC2 open && (exactly one of 990PU1 / 990PU2 closed)
```
6PC2 and 980PC read last frame's states (mutual interlock). Keep the previous-frame value.

**DC EHA** (`DC:303-309`): `970PN2 = DC2_powered`; `14PH = TEFO`.

### 3.8 DC ESS in-flight sub-bus 108PH (`DC:333-355`, H)

```
bus_ctrl = 7XB closed && !flt_condition
sply2    = delayed_false(any(AC1..4), 10 s)
10PH     = DC_ESS_powered && ( (10PH && !bus_ctrl) || sply2 || emer_config || AC_ESS(400XP)_powered )
```
**Important consequence**: on the ground on batteries only, 108PH is **not** powered. That kills the
CAPT PFD (section 6). After losing AC on the ground, 108PH stays powered for 10 s and then sheds.

### 3.9 Emergency electrical configuration and RAT (`ECOM:290-312`, `EM:176-208,510-543`, `RAT`, H)

```
emer_elec_active = !any(AC1..4) && IAS > 100 kt     (duration accumulates)       ECOM:297-303
rat_should_deploy = sim_ready && ( HOT1_powered && emer_elec_active              // solenoid 1
                                || HOT_ESS_powered && RAT_MAN_ON_pressed )      // solenoid 2   EM:523-537
RAT stow position: += 1.0/s once commanded, latched forever (until reload)          RAT:77-104
GCU active = emer_elec_active || (man_on_latched && !both main gear compressed)    RAT:160-172
EMER GEN supplies when rpm > 2000 (gen rpm = turbine rpm * 2.4286)                 EM:61, RAT:39,129-132
flt_condition = !on_ground && DC_ESS_powered && (adiru1.lsw_3 || adiru3.lsw_3)   // set > 104 kt, reset < 100 kt   EM:184-185
TEFO latch    = set when flt_condition && (no engine N2 > 50 % || (!DC_ESS && !DC2))  EM:193-200
emer_config latch = set when HOT_ESS_powered && rat_should_deploy                   EM:206-207
emer_evac = !BAT1_AUTO && !ESS_AUTO && DC_ESS_powered  -> resets TEFO and emer_config latches  EM:180,187-190
```
RAT physics (wind turbine, governor 4150 rpm) is in `RAT:41-50`. A simple first-order spool (0 to governed
speed in about 4-6 s after full deploy) is acceptable for the port (M). Hydraulic RAT functions are out of
scope here.

### 3.10 Battery model (`BAT:27-160`, H)

* Capacity 23 Ah. Full at spawn (`DC:78-86`).
* Open-circuit voltage vs charge `q` (Ah): `q<=0 → 0 V`; `q<=3.488 → 13.953q - 2q²`;
  `q<22.449 → 23.85 + 0.14q`; above that, a 6th-order polynomial up to 28.96 V at 23 Ah (`BAT:98-120`). Port
  it verbatim. Fully charged gives about 27.0-28.9 V, a "normal" range of 25-31 V (`BAT:151-153`).
* Charging when bus potential > battery potential: `I = (Vin - Vbat)/0.15 Ω`, clamped 0..10 A
  (`BAT:123-135`).
* Discharging: `I = -P_consumed / Vbat`; `q -= I*dt` (`BAT:...process_power_consumption_report`).
* `needs_charging`: `q <= 20 Ah` (`BAT:61-63`). BCRU "SOC < 20 %": `V < 24.5 V` (`BCRU:101-102`).
* Hot buses carry the battery potential permanently (or BCRU potential when charging) (`EM:640-675`).

Load on batteries needs a consumption model. FBW uses flight-phase aggregates per bus (`PWR:1-347`,
e.g. AC1 26.8 kW before start). For battery-only endurance, the port needs only DC ESS + AC EMER (static
inverter) + DC APU loads. Recommend a constant table (M): DC ESS about 1.0 kW, AC EMER about 0.9 kW
(`PWR` AC_ESS figures 455-875 W), and DC APU about 0.1 kW plus about 9 kW while the starter is engaged.
This gives a realistic 30-60 min battery endurance with the FBW 23 Ah capacity (to be tuned, L).

### 3.11 Load shedding (`GAL:4-51`, `EM:350-366`, `DOC-ELEC`)

| Signal | FBW rule (H) | Panel meaning (DOC-ELEC, M) |
| --- | --- | --- |
| Main galley shed | `!any(AC1..4) || main AC buses powered by <= 2 generators (≥2 buses fed by another bus's single source) || COMMERCIAL off || GALY&CAB off` (`GAL:16-26`, `AC:192-206`) | GALLEY AUTO: galleys supplied, may be auto-shed; OFF: all galleys shed |
| Secondary galley shed | `!any(AC1..4) || COMMERCIAL off || GALY&CAB off` | — |
| `ELEC_GALLEY_IS_SHED` | main || secondary (`EM:298-300`) | ECAM/cabin input |

FBW collapses the real panel's COMMERCIAL 1, COMMERCIAL 2 and ELMU pbs onto one var
`OVHD_ELEC_COMMERCIAL_PB_IS_ON`. It collapses GALLEY and PAX SYS onto `OVHD_ELEC_GALY_AND_CAB_PB_IS_AUTO`
(`BEH:1605-1675`). The port should keep **five separate pb states**: COMMERCIAL 1, COMMERCIAL 2, GALLEY,
PAX SYS, ELMU. Derive the FBW-equivalent signals from them as follows (M):
`commercial_off = COMM1_off || COMM2_off`, `galy_and_cab_off = GALLEY_off || PAX_SYS_off`.
The ELMU FAULT lamp is driven only by a failure.

### 3.12 Failures and FAULT inputs

| Failure (port, section 9.4) | Effect |
| --- | --- |
| GEN n fail | `vfg[n].failed` → no output → 990XU open → GEN FAULT |
| DRIVE n (IDG) oil overheat / low pressure | DRIVE FAULT lamp only (crew must DISC). Optional auto: none |
| APU GEN A/B fail | no output → 990XS open → APU GEN FAULT (when APU AVAIL) |
| BCRU 1/2/ESS fail, TR APU fail | `transform()` returns none (`BCRU:162-173`, `TR:80-91`) → DC re-configures via ties |
| Static inverter fail | no SI output (`SINV:84-101`) |
| BAT n fail | contactors forced open, FAULT lamp |
| EXT n fail | `ext_connected[n] = 0` |
| Bus n fail (AC1-4, AC ESS, DC1/2/ESS) | bus forced unpowered regardless of sources (not in FBW; simple override) |

---

## 4. Overhead panel: controls, defaults and lamps

### 4.1 Controls and FBW default positions

| Control | Positions | FBW construction default | FBW L:var (writes) | Src |
| --- | --- | --- | --- | --- |
| BAT 1, BAT 2, ESS (BAT 3), APU (BAT 4) | AUTO / OFF (+FAULT) | AUTO | `OVHD_ELEC_BAT_{1,2,ESS,APU}_PB_IS_AUTO`, `_HAS_FAULT` | `EM:349-354`, `PRESET:52-66` |
| BAT voltage selector | ESS / APU / OFF / 1 / 2 (5 detents, values 0..4) | OFF (2) | `L:A380X_OVHD_ELEC_BAT_SELECTOR_KNOB` | `BATD`, `BEH:1417-1418` |
| DRIVE 1-4 (FBW "IDG") | guarded momentary; latched DISC (+FAULT) | connected | `OVHD_ELEC_IDG_n_PB_IS_RELEASED`, `_IS_DISC`, `_HAS_FAULT` | `EM:355-357`, `BEH:1677-1730` |
| GEN 1-4 | ON(AUTO) / OFF (+FAULT) | ON | `OVHD_ELEC_ENG_GEN_n_PB_IS_ON` (+ MSFS `GENERAL ENG MASTER ALTERNATOR:n`) | `EM:358-359`, `WASM:589-596` |
| APU GEN A, B | ON / OFF (+FAULT) | ON | `OVHD_ELEC_APU_GEN_n_PB_IS_ON` (+ `APU GENERATOR SWITCH:n`) | `EM:360-361` |
| EXT 1-4 | momentary; ON / (AVAIL) | OFF | `OVHD_ELEC_EXT_PWR_n_PB_IS_ON`, `_IS_AVAILABLE` | `EM:364-365`, `BEHX:38-56` |
| BUS TIE | AUTO / OFF | AUTO | `OVHD_ELEC_BUS_TIE_PB_IS_AUTO` | `EM:362` |
| AC ESS FEED | guarded NORM(AUTO) / ALTN (+FAULT) | NORMAL | `OVHD_ELEC_AC_ESS_FEED_PB_IS_NORMAL`, `_HAS_FAULT` | `EM:362` |
| GALLEY / PAX SYS | AUTO / OFF (+FAULT on GALLEY in FBW) | AUTO | `OVHD_ELEC_GALY_AND_CAB_PB_IS_AUTO` (shared) | `EM:363`, `BEH:1647-1675` |
| COMMERCIAL 1, COMMERCIAL 2, ELMU | AUTO(ON) / OFF (+FAULT on ELMU) | ON | `OVHD_ELEC_COMMERCIAL_PB_IS_ON` (shared) | `EM:366`, `BEH:1605-1645` |
| EMER ELEC PWR: RAT & EMER GEN MAN ON | guarded momentary | released | `OVHD_EMER_ELEC_RAT_AND_EMER_GEN_IS_PRESSED` | `EM:453-499`, `BEH:2941-2955` |
| EMER ELEC PWR: RAT & EMER GEN (FAULT lamp only) | — | — | `OVHD_EMER_ELEC_RAT_AND_EMER_GEN_HAS_FAULT` | `EM:476-480` |
| EMER ELEC PWR: GEN 1 LINE | ON / OFF (+FAULT) | ON | `OVHD_EMER_ELEC_GEN_1_LINE_PB_IS_ON` | `BEH:2913-2924` (no Rust logic, M) |
| EMER ELEC PWR: EMER GEN TEST | guarded momentary | released | `A32NX_EMERELECPWR_GEN_TEST` (inop in FBW) | `BEH:2902-2911` |
| ANN LT | TEST / BRT / DIM | BRT | `OVHD_INTLT_ANN` (0 = TEST) | `BEH:1975-1982`, `BATD:13,39-41` |

The FBW DRIVE legend is labelled DRIVE on the real panel (`DOC-ELEC`). FBW panel defaults are its
"running aircraft" defaults. Cold & dark only changes the batteries (section 7).

### 4.2 Lamp logic (lamp lit = `condition && lamp_bus_powered`, or `ANN LT TEST && lamp_bus_powered`)

Legend positions follow the FBW template: SEQ1 = upper legend, SEQ2 = lower legend (`BEH`, `BEHX`).

| pb | Upper legend (colour): condition | Lower legend (colour): condition | Lamp power (FBW `SEQ_POWERED`) | Src / conf |
| --- | --- | --- | --- | --- |
| BAT 1 | FAULT (amber): BAT 1 failed / abnormal current; contactors forced open | OFF (white): pb OFF | DC ESS | `BEH:1422-1428` H; FAULT rule M (`DOC-ELEC`), not in FBW |
| BAT 2 | as BAT 1 | OFF: pb OFF | DC 2 | `BEH:1432` H |
| ESS | as BAT 1 | OFF | DC ESS | `BEH:1442` H |
| APU (BAT) | as BAT 1 | OFF | DC 2 | `BEH:1452` H |
| GEN 1, 2 | FAULT: `990XU open && pb ON` | OFF: pb OFF | DC ESS | `EM:379-383`, `BEH:1462-1478` H |
| GEN 3, 4 | same | same | DC 2 | `BEH:1480-1496` H |
| APU GEN A | FAULT: `APU AVAIL && pb ON && gen failed/no output` (FBW never sets it) | OFF: pb OFF | DC ESS | `BEH:1498-1504` H (lamp power); FAULT rule M (`DOC-ELEC`: disconnected by GGPCU) |
| APU GEN B | same | same | DC 2 | `BEH:1507` H |
| EXT 1, 2 | AVAIL (green): `ext_connected && !pb_on` | ON (blue): `ext_connected && pb_on` | AVAIL: `ext_connected || DC1`; ON: DC ESS | `BEHX:52-53`, `BEH:1517-1538` H |
| EXT 3, 4 | same | same | AVAIL: `ext_connected || DC2`; ON: DC ESS | `BEH:1541-1562` H |
| DRIVE 1-4 | FAULT: oil overheat or low oil pressure (failure-driven) | DISC (white): drive disconnected | DC ESS | `EM:385-387`, `BEH:1677-1730` H; FAULT rule M (`DOC-ELEC`) |
| BUS TIE | — | OFF: pb OFF | DC ESS | `BEH:1733-1739` H |
| AC ESS FEED | FAULT: `!AC_EMER(491XP)_powered` | ALTN (white): pb ALTN | DC ESS | `EM:374-376`, `BEH:1745-1752` H |
| GALLEY | FAULT: galley failure (FBW reads `GALY_AND_CAB_PB_HAS_FAULT`, never set) | OFF | DC 2 | `BEH:1647-1659` H |
| PAX SYS | (FBW reuses GALY fault) | OFF | DC ESS | `BEH:1661-1673` H |
| COMMERCIAL 1 | — | OFF | `DC1 && DC ESS` | `BEH:1606-1613` H |
| COMMERCIAL 2 | — | OFF | DC 2 | `BEH:1615-1621` H |
| ELMU | FAULT: ELMU failed | OFF | DC 2 | `BEH:1633-1640` H; FAULT `DOC-ELEC` M |
| RAT & EMER GEN | FAULT: `emer_elec_active && 5XE open && !on_ground` | — | DC ESS | `EM:476-480`, `BEH:2927-2935` H |
| GEN 1 LINE | FAULT (no logic) | OFF: pb OFF | AC ESS 400XP | `BEH:2913-2924` H |
| APU MASTER SW | FAULT: APU auto/emergency shutdown fault | ON (blue): pb ON | DC APU 309PP (FBW typo `AC_ES` makes it 309PP only) | `BEH:1758-1765` H |
| APU START | AVAIL (green): ECB `is_available` | ON (blue): starting | AC ESS 400XP or DC APU 309PP | `BEH:1776-1782`, `ECB:328-337` H |
| ADIRS ON BAT | lit 10.5 s → 16 s after a selector leaves OFF; port spec: also while an IR runs on battery | — | (HOT ESS, L) | `ADIRS:126-170` H; `PORT:docs/ADIRS.md` |

Notes:

* With batteries on and engines stopped, GEN 1-4 FAULT are lit amber. FBW: contactor open and pb ON,
  which matches "amber caution lights appear" in `TUT` (M). Do not invent a real-aircraft
  inhibition without evidence (open question 12.4).
* The BAT voltage display (`BATD`) shows the selected battery's potential in the format `NN.NV`. It
  refreshes one digit at a time (FBW constants `BASE_DELAY_MS`/`DIGIT_REFRESH_INTERVAL_MS`). It shows
  `88.8V` with ANN LT TEST when DC 2 is powered. It is blank when the selector is OFF, and it is
  self-powered/hot-bus (no bus check besides the test). Map ESS→BAT 3, APU→BAT 4, 1→BAT 1, 2→BAT 2
  (`BAT/index.tsx:18-20`, H).
* All lamps use ANN LT BRT/DIM intensity. DIM ≈ 0.4-0.5 of BRT (L).

---

## 5. SD ELEC AC/DC page data (what the core must publish)

From `SDE` (H): per generator, potential (V), load (%), and line contactor state. Amber thresholds:
V outside 110-120, load ≥ 108 %. GEN legend amber when `pb_off || engine not running || failed`.
APU gens: load, V, Hz (390-410), contactor; hidden unless APU MASTER ON. EXT: V/Hz shown when connected,
legend shown on ground or when connected (`ExternalPower.tsx:28-60`). EMER GEN: load/V, 5XE, RAT stow
position. STAT INV: V/Hz, 7XB && 3XB.2. Battery: V (amber < 25 unloaded / < 20 loaded), A (amber
< -5 A), pb AUTO. TR: V 27-32, A > 2 normal, input AC bus powered. Every contactor in 2.3 is drawn.
Publish all of these (section 10.3) even if the port's SD ELEC pages are written later.
`a380x_sd_adapter.inc:37-42` already looks for `a380x/xp12/apu/generator_{load_percent,volts,hz}`.

---

## 6. Consumers: which bus powers what

### 6.1 Display units (CDS)

Two FBW sources disagree. The CDS logic (`CDS:41-50`) decides the display state. The 3D emissive in `BEH`
uses simpler buses. **Use `CDS`** (H for FBW; real-world M):

| DU | Powered if any of | `BEH` emissive (cosmetic) |
| --- | --- | --- |
| CAPT PFD | 108PH (DC ESS in flight) | DC ESS |
| CAPT ND | 108PH, DC1 | DC ESS or DC1 |
| CAPT MFD | DC ESS, DC1 | DC ESS or DC1 |
| F/O PFD | DC2 | DC2 |
| F/O ND | DC1, DC2 | DC1 or DC2 |
| F/O MFD | DC1, DC2 | DC1 or DC2 |
| EWD | DC ESS | DC ESS |
| SD | DC2 | DC2 |
| CAPT OIT | AC2, DC2 | AC2 |
| F/O OIT | AC EMER(491XP, FBW `AC_ESS`), 108PH, DC1 | (`OIT:39-42`) |

### 6.2 DU state machine (`CDS:66-245`, H)

```
initial: Off if cold&dark spawn, else Standby (resolves to On immediately if powered)
On       -- !powered || brightness==0 -->  Standby (start 10 s timer)
Standby  -- powered && brightness>0   -->  On (no self test)
Standby  -- 10 s elapsed              -->  Off
Off      -- powered && brightness>0 && !failed --> ThalesBoot (0.25 + rand*0.2 s)
ThalesBoot -- timer, still powered    -->  SelfTest (CONFIG_SELF_TEST_TIME, default 15 s)
SelfTest -- timer                     -->  On
ThalesBoot/SelfTest -- !powered || brightness==0 --> Off
any -- failed --> Off
```
* Self-test time is a flyPad setting in FBW (default 15 s). The port should expose it (0-30 s).
* Brightness: the port's existing DU potentiometers (`PORT:native/avionics/a380x_du.inc:13-22`) map to
  FBW potentiometers 88-99 (`CDS:52-61`). Brightness 0 counts as unpowered for the state machine.
* The 10 s standby makes source transfers (EXT→APU→GEN, BCRU 3 s hold) invisible, as on the aircraft.

### 6.3 ISIS (`ISISDU:19-60`, H)

`has_power = DC_ESS || (IAS > 50 kt && HOT1)`. States Off → SelfTest (**90 s**, "INIT" flags) → On;
On → Standby (10 s) → Off. Cold & dark spawn starts Off. The port already implements this
(`PORT:native/avionics/a380x_isis.inc:39,54-66`, `PORT:docs/ISIS.md:103`) using `bus_volts[0]` and
battery volts. Re-point it to `DC_ESS`/`HOT1` from the core.

### 6.4 Other consumers

| Consumer | Bus(es) | Src / conf |
| --- | --- | --- |
| FCU channel 1 / channel 2 (FCU alive if either) | 108PH / DC2 | `FBWI:2372` H |
| PRIM 1 / 2 / 3, SEC 1 / 2 / 3 | 108PH / DC EHA 247PP / DC1 | `FBWI:1699-1706,2188-2195` H |
| FCDC inputs | DC ESS(108PH), DC2, AC2 failure discretes | `FBWI:2255-2257` H |
| FWS (FWS 1 / 2) | DC ESS / DC2 | `FWS:2855-2856` H |
| RMP 1 / RMP 2 / RMP 3 | DC ESS / DC ESS / DC1 | `RMP:56-72` H |
| AMU (audio) | DC ESS | `AMU:41` H |
| RTPI (rudder trim) | DC ESS | `RTPI:35-37` H |
| KCCU CAPT / F/O | DC1, DC ESS, DC ESS / DC2, DC1, DC2 | `CDSY:79-96` H |
| ADCN switches / end systems | DC ESS, DC1, DC2, 108PH, 502PP (per switch) | `ADCN:111-220` H |
| Radio altimeter 1 / 2 / 3 | AC1 / AC2 / AC EMER(FBW `AC_ESS`) | `NAV:59,77,92` H |
| EGPWC | AC EMER (FBW `AC_ESS`) | `LIB:149-151` H |
| LGCIU 1 / 2 | DC ESS / DC GND SVC | `LIB:137-141` H |
| APU ECB + air-intake flap | DC ESS | `LIB:113-118` H |
| APU start motor | APU START sub-bus (DC APU 309PP) | `DC:16,282-287` H |
| RAT solenoids | HOT1 (auto), HOT ESS (MAN ON) | `EM:63-67` H |
| BAT voltage display | self/hot (test needs DC2) | `BATD:13-41` H |
| MCDU (legacy) L / R | AC ESS 400XP / AC2 | `BEH:385-393` H |
| DCDU / MPL keys | AC1 | `BEH:1278-1320` H |
| Autobrake / gear panel lamps | AC ESS 400XP | `BEH:1191-1207` H |
| Sidestick priority lamps L / R | AC1 / AC2 | `BEH:1278-1282` H |
| ELEC panel lamps | see 4.2 | H |
| ADIRU 1 / 2 / 3 | **not modelled in FBW**. Proposal: ADIRU 1 AC EMER 491XP + HOT ESS backup; ADIRU 2 AC2 + HOT2 backup; ADIRU 3 AC1/AC ESS + HOT1 backup; on-battery operation lights ON BAT | L, see 12.6 |
| Integral/flood cockpit lighting, clock | DC1/DC ESS (lighting), HOT bus (clock) | L |
| Fuel pumps (feed main/stby, transfer) | AC buses (FBW uses MSFS circuits) | L |
| Exterior lights | AC buses (nav AC1/AC2 etc.) | L |

### 6.5 Expected display picture per configuration (derived from 3.2-3.8 and 6.1, H for FBW logic)

| Configuration | CAPT PFD | CAPT ND | CAPT MFD | EWD | SD | F/O PFD/ND/MFD | ISIS | FCU | RMP 1/2 / 3 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Cold & dark | off | off | off | off | off | off | off | off | off/off |
| BAT 1 only | off | off | off | off | off | off | off | off | off/off |
| BAT 1 + ESS (+2 +APU) | off (108PH shed) | off | **boot** | **boot** | off | off | **INIT 90 s** | off | on / off |
| + EXT 2 ON | boot | boot | on | on | boot (DC2 via 980PC) | boot | INIT continues, then on | on | on / on |
| + EXT 3 ON or APU GEN | on | on | on | on | on | on | on | on | on / on |
| Battery only in flight (RAT not yet, IAS > 100 kt) | on (10PH stays closed: flt_condition) | on | on | on | off | off | on | ch1 on | on / off |

Verify the battery-only display picture on the ground in X-Plane against FBW MSFS behaviour (12.3).

---

## 7. Cold & dark state

### 7.1 Initial state table (cold & dark spawn)

Rule: the previous crew secured the aircraft per `DOC-DOWN` (M). Panel switches that the securing flow
does not touch stay in their normal position.

| System | Cold & dark state | Src / conf |
| --- | --- | --- |
| BAT 1, BAT 2, ESS, APU BAT | **OFF** | `DOC-DOWN` step 11, `PRESET:463-478` H |
| Battery charge | full (23 Ah each). Optional flyPad "battery state" for realism | `DC:78-86` H |
| BAT selector | OFF | `BATD` H |
| GEN 1-4, APU GEN A/B | ON (AUTO) | `EM:358-361` H |
| DRIVE 1-4 | connected (reset on every aircraft load) | `EGEN:252-254` H |
| BUS TIE, AC ESS FEED, GALLEY, PAX SYS, COMMERCIAL 1/2, ELMU | AUTO / NORM / AUTO / ON / AUTO / AUTO | `EM:362-366` H |
| EXT 1-4 pb | OFF; GPU **not connected** (user requests it via the flyPad) | `EM:364-365`, `GPUM` H |
| APU MASTER / START / APU BLEED | OFF / OFF / OFF; APU N = 0, flap closed | `DOC-DOWN` H |
| Engine masters | OFF; ENG MODE NORM | `DOC-PREP` H |
| ADIRS 1-3 | OFF, not aligned | `DOC-DOWN`, `PORT:docs/ADIRS.md:20-22` H |
| Crew oxygen | OFF | `DOC-DOWN`, `PRESET:105-108` H |
| RCDR GND CTL | OFF | `PRESET:109-112` H |
| Ext lights (strobe, beacon, nav/logo, land, taxi, RWY TURN OFF, wing, logo) | OFF | `DOC-DOWN` H |
| Seat belts / no smoking / EMER EXIT LT | OFF / OFF / OFF (disarmed) | `DOC-DOWN`, `PRESET:400-412` H |
| Fuel pumps (feed main/stby, transfer, trim) | OFF | `PRESET:316-396`, `DOC-DOWN` "fuel pumps off" M |
| Anti-ice | OFF | `DOC-DOWN` H |
| Parking brake | ON | `DOC-DOWN` H |
| EFIS/FCU | last values persisted by the plugin. FCU and displays are dark (unpowered). FCU power-up defaults per `FCU_FUNCTIONAL_SPEC.md` | M |
| DUs | Off state (full boot on first power) | `CDS:81-83` H |
| ISIS | Off (90 s INIT on power) | `ISISDU:19-22` H |
| FMS | cold-and-dark FMS state (already implemented, `PORT:native/avionics/a380x_fms_adapter.inc:63,832,874`) | H |
| Doors / ground services | L1 door + jetway as user chooses (flyPad). Chocks on. GPU disconnected. No ground air | M |
| RMPs | off (unpowered); volume/TX presets per `PRESET:233-283` once powered | H |

### 7.2 Spawn selection (recommendation)

At `XPLM_MSG_PLANE_LOADED` (param 0) and on the first flight-loop tick after it (X-Plane sets engines a
frame late), compute:

```
running = any engine N2 > 50 % (or N1 > 15 %, as the port already does: a380x_adirs_adapter.inc:279-283)
airborne = !on_ground
startup_running_pref = sim/operation/prefs/startup_running  (XPDR, writable int)
if (airborne || running)                      mode = READY_ENGINES_RUNNING
else if (efb_pref_default_spawn == TURNAROUND) mode = TURNAROUND
else                                          mode = COLD_DARK
```
* Reuse the same decision everywhere. Today FMS, ADIRS and ISIS each compute their own
  (`a380x_fms_adapter.inc:832,874`, `a380x_adirs_adapter.inc:279-283`, `a380x_isis_adapter.inc:460`).
  Publish it as `a380x/xp12/elec/spawn_mode` (0 C&D, 1 turnaround, 2 ready) and make those modules read
  it. This also replaces FBW `L:A32NX_COLD_AND_DARK_SPAWN`.
* `startup_running` is only a hint: X-Plane has already started the engines when it is set. The live
  engine state is the authority.
* **READY** snaps the native state to a running aircraft: all BAT AUTO, GEN ON with engines providing
  power, APU off, ADIRS NAV aligned, DUs On, ISIS On, lights per phase, EXT disconnected.
* **TURNAROUND** ("Powered", FBW preset 2) snaps to the end state of section 8.2 steps 1-6 without the
  timers: BAT AUTO, GPU connected + EXT 1-4 ON, ADIRS NAV aligned (or aligning), DUs On, APU off.

### 7.3 flyPad presets (port the FBW list, `PRESET`, H)

| FBW preset id | Name | Procedure(s) |
| --- | --- | --- |
| 1 | Cold & Dark | run *_OFF procedures in reverse: TAKEOFF_OFF, TAXI_OFF, PUSHBACK_OFF, POWERUP_OFF |
| 2 | Powered (Turnaround) | POWERUP_CONFIG_ON |
| 3 | Ready for Pushback | + PUSHBACK_CONFIG_ON |
| 4 | Ready for Taxi | + TAXI_CONFIG_ON |
| 5 | Ready for Takeoff | + TAKEOFF_CONFIG_ON |

Each step has `Condition` (skip if already true), `Action`, `Delay` ms, and a type (STEP, PROC = normal
only, NOEX, EXON, COND/NCON/ECON = wait) (`PRESET:8-44`). Port the data table as a static C array of
steps that act on **native** pb states, never on X-Plane commands. Offer "expedited" mode (delays
skipped except NOEX) and show progress on the flyPad. Ground-only, engines off for presets 1-2, as the
port already enforces (`PORT:native/avionics/a380x_efb.c:296-325`). The current port presets call
stock commands (`sim/electrical/battery_1_off`, `sim/ground_ops/toggle_gpu_request`,
`sim/operation/quick_start`) and must be replaced.

POWERUP_CONFIG_ON, condensed (`PRESET:48-296`): BAT1, BAT ESS, BAT2 (1 s each), BAT APU (3 s);
EXT 2, 3, 1 (1 s each), EXT 4 (3 s); fire test (normal mode only); APU MASTER (3 s), APU START,
wait AC1 powered; crew oxy ON; GND CTL ON; ADIRS 1, 2, 3 NAV; strobe AUTO; nav & logo ON; seat belts ON;
no smoking AUTO; EMER EXIT LT ARM; all fuel pumps ON; RMP volumes/TX; wait APU AVAIL; APU BLEED ON.
POWERUP_CONFIG_OFF reverses it and waits until AC1 is unpowered (`PRESET:299-490`).

---

## 8. Power-up and preliminary cockpit preparation flow (expected behaviour per step)

Times are in sim time (paused or replay stops all timers). "Boot" means the DU state machine of 6.2.

### 8.1 Step-by-step

| # | Crew action | Expected network effect | Expected cockpit effect | Src / conf |
| --- | --- | --- | --- | --- |
| 0 | (cold & dark) | Only hot buses live (HOT1/2/ESS/APU = battery V ≈ 27-28.9 V) | Everything dark. BAT display shows voltage only if the selector is not OFF. Clock powered from hot bus (L) | `EM:640-675` H |
| 1 | BAT 1 AUTO | 990PB1 stays open (no AC) and ELC needs ESS too, so **nothing new** | BAT 1 OFF legend extinguishes once its lamp bus is powered (not yet) | `DC:226-240` H |
| 2 | ESS AUTO | ELC 6PC1 + 6PB3 close: **DC ESS** from BAT1+ESS. 7XB closes: static inverter: **AC EMER 491XP**. 108PH stays shed (ground) | EWD and CAPT MFD boot (0.25-0.45 s + 15 s). ISIS INIT 90 s. RMP 1/2 on. DC ESS lamps live: GEN 1/2 FAULT, AC ESS FEED (no FAULT, since AC EMER is live), EXT ON legends powered. ECAM shows battery/elec memos | `DC:252-301`, `EM:775-824` H |
| 3 | BAT 2 AUTO | BCRU2 state Open (no AC), so no DC2 | none | `BCRU:246-256` H |
| 4 | APU BAT AUTO | 5PB closes (7PU open): **DC APU 309PP**; APU MASTER/START lamps powered | APU pb lamps can light | `DC:272-280` H |
| 5 | flyPad GPU connect | `ext_connected[1..4] = 1` (all four together). With BAT on: no change until EXT pb. With BAT off: ground-service buses live (3.5) | EXT 1-4 **AVAIL** green (lamp powered by EXT itself) | `GPUM`, `BEHX:52` H |
| 6 | EXT 2 ON | AC1+AC2 ← EXT2; AC ESS (3XC1); TR ESS → DC ESS (6PE). TR1 (AC2) → DC1; 980PC → DC2. 108PH on. 7XB opens; AC EMER via 3XB.1. 990PB1 closes (BAT 1 charging); ELC opens. BCRU ESS Closed → 6PB3 charging | CAPT PFD/ND, F/O PFD/ND/MFD, SD boot. EXT 2 ON blue. FCU on | `EM:826-892` H |
| 7 | EXT 3 ON | AC3+AC4 ← EXT3, TR2 → DC2 (980PC opens), TR APU → DC APU (5PB opens unless APU BAT needs charging), 990PB2 closes, AC GND SVC via 991PU2, AC EHA via 911XN | — | `EM:975-1040` H |
| 8 | EXT 1, EXT 4 ON | each bus moves to its own EXT (row 2 beats row 5). No break visible (DU 10 s standby) | all EXT ON | `AC:306-345` H |
| 9 | Fire test, RMP, interphone | — | (FWS on DC ESS/DC2) | `DOC-PREP` M |
| 10 | APU MASTER ON | ECB powered (DC ESS), air-intake flap opens in 6-12 s (random per unit). SD switches to APU page | MASTER ON blue | `FLAP:21-35`, `ECB:395-421` H |
| 11 | APU START (after flap fully open) | start contactors close (DC APU powered required), starter until N 55 %. APU GEN outputs from N ≥ 77 % but EXT keeps priority; with EXT ON it fills nothing. **AVAIL** when N > 95 % for 2 s or N > 99.5 % (≈ 60 s total) | START ON blue → AVAIL green. SD APU N/EGT | `ECB:328-337,395-421`, `PW980:819`, `TUT` "about a minute" M. X-Plane ACF spool-up 74 s (`acf/_APU_spoolup_time`) |
| 12 | (optional) EXT 1-4 OFF before pushback | buses transfer to APU GEN A (AC1/2) and B (AC3/4) | EXT AVAIL green again | `EM:1123-1200` H |
| 13 | ADIRS 1, 2, 3 NAV | units power (6.4). ON BAT 10.5 s after leaving OFF, for 5.5 s (FBW). Port: 5 s self-test, then lit while on battery. Alignment 300 s / cos(lat) (≤ 60°), 600 s (60-73°), 1020 s (> 73°). FAST 90 s. INSTANT option | ADIRS IR ALIGN. PFD ATT flags until aligned | `ADIRS:126-170,1708,2350-2366`, `PORT:docs/ADIRS.md:15-52` H |
| 14 | Crew oxy, GND CTL, ext lights, signs, fuel pumps, EFIS/FCU, MFD init | consumers on their buses | — | `DOC-PREP`, `PRESET` M |
| 15 | APU BLEED ON (only when ready to start engines) | bleed valve opens when N > 95 % && master && pb | — | `ECB:465-479`, `TUT` M |
| 16 | Engine start (4 engines) | each VFG freq enters 360-800 Hz, stable 0.5 s, then 990XU closes; bus leaves APU/EXT. GEN FAULT extinguishes | SD ELEC shows GEN load/V green | `EGEN:100-137` H |
| 17 | APU BLEED OFF, APU MASTER OFF | AVAIL lost immediately on master off (PW980). APU N decays; gens open below 77 % N (or immediately if not selected). Cooldown 60 s before stop if bleed used (BLEED cooldown = 0 for PW980) | — | `PW980:31-36`, `ECB:320-337,445-462` H |

### 8.2 Shutdown and secure (`DOC-DOWN` M; network effects H)

1. APU master ON (APU AVAIL) or GPU connected and EXT ON before engine shutdown, so the buses
   transfer to APU/EXT when each GEN drops below the frequency range.
2. Engine masters OFF. GEN contactors open, buses re-source per 3.2, GEN FAULT lit.
3. Beacon off when N1 < 5 %. Fuel pumps OFF.
4. Securing: crew oxy OFF, ADIRS 1-3 OFF (alignment lost), ext lights OFF, APU BLEED OFF, EXT OFF,
   APU MASTER OFF, EMER EXIT LT OFF, no smoking OFF.
5. **Wait for the APU to stop** (N < 8 %, flap closed) before batteries OFF. The ECB is on DC ESS and the
   start/flap logic needs power (`LIB:113-118`, `ECB:425-440`). The 2-minute rule from A320 practice
   (`L`) is not documented for the A380. Implement it as "BAT OFF while APU N > 8 % ⇒ APU emergency
   shutdown, flap stays open" and add an ECAM/flyPad hint (L).
6. All batteries OFF. DC ESS drops after the last battery path opens. DUs go Standby, then Off after
   10 s. If GPU stays connected, ground service buses remain live (3.5).
7. Battery OFF in flight is not forbidden by the model. FBW "emer_evac" resets the TEFO and emer-config
   latches when BAT1 and ESS are both OFF with DC ESS still powered (`EM:180,187-190`).

---

## 9. X-Plane 12 mapping

### 9.1 Facts about the X-Plane side (H unless stated)

* **No electrical override exists**. The full `sim/operation/override/*` list (XPDR) has no
  electrical, bus, battery or generator override. `sim/cockpit2/electrical/bus_volts[6]` is flagged
  writable but is recomputed by the sim (M: not verified in-sim). Treat it as read-only.
* Relevant writable stock datarefs (XPDR): `sim/cockpit2/electrical/battery_on[8]`,
  `generator_on[8]`, `APU_generator_on`, `APU_starter_switch` (0 off, 1 on, 2 start), `GPU_generator_on`,
  `inverter_on[2]`, `cross_tie`, `RAT_generator_on/_extended`, `plugin_bus_load_amps[6]`,
  `sim/cockpit/electrical/battery_charge_watt_hr[8]`, `sim/cockpit2/switches/avionics_power_on`,
  `sim/operation/prefs/startup_running`. Read-only: `APU_N1_percent`, `APU_EGT_c`, `APU_running`,
  `GPU_can_be_called_for`, `generator_amps/volts`, `sim/aircraft/electrical/num_*`.
* Commands (XPDR): `sim/electrical/battery_{1,2}_{on,off,toggle}`, `generator_n_{on,off,toggle,reset}`,
  `APU_{start,on,off,generator_on,generator_off,fire_shutoff}`, `GPU_{on,off,toggle}`,
  `sim/ground_ops/toggle_gpu_request`, `sim/systems/avionics_{on,off,toggle}`, `sim/operation/quick_start`.
* Failure datarefs (XPDR): `sim/operation/failures/rel_genera0..3` (GEN 1-4), `rel_batter0..3`,
  `rel_bat0_lo/hi`, `rel_esys..rel_esys6` (bus 1-6), `rel_bus0..5_other_bus`, `rel_apu`, `rel_apu_fire`.
  There is no TR, static-inverter or IDG failure.
* Current ACF electrical config (`PORT:output/A380X-XP12/A380X-XP12.acf`, M because another developer
  is editing these files): `_num_batteries 1` (l.64521), `_num_generators 4`, `_num_buses 5`,
  `_num_inverters 1`, `_nom_bat_volt 24`, `_nom_gen_volt 28`, `_battery_watt_hr_max 1272` (l.62563),
  `_has_APU_generator 1`, `_has_ground_pwr 1` (l.64255,64284), `_bus_apu_is_on 0`,
  `_bus_essential 0`, `_APU_spoolup_time 74` s / `_spooldn_time 65` s (l.61924-61925).
  `tools/test_adapt_a388_flightmodel.py:10,56` expects 4 batteries from the native A388 base, so the
  current value of 1 should be checked.

### 9.2 How the port decides power today (to be migrated)

| Location | Current criterion | Replace with |
| --- | --- | --- |
| `a380x_avionics.c:196,284,295-323` (PFD/ND sides, FMS, EWD, MFD, SD) | `avionics_power_on != 0` (one flag for all DUs) | per-DU `a380x_elec_du_powered()` + DU state machine (6.1-6.2) |
| `a380x_fcu_adapter.inc:182,429-430`, `a380x_lighting.inc:26`, `a380x_efis.inc:216` | `avionics_power_on` | FCU = 108PH \|\| DC2; EFIS CP per side (L: CAPT DC ESS, F/O DC2) |
| `a380x_center_right.inc:254` | `avionics_power_on` && inertial | autobrake: AC ESS 400XP + ADIRS |
| `a380x_mfd_adapter.inc:407,1083-1092` | `powered` param | CAPT MFD = DC ESS \|\| DC1; F/O = DC1 \|\| DC2 |
| `a380x_sd_adapter.inc:270-282` | `powered` param | SD = DC2 |
| `a380x_adirs_adapter.inc:20-66` | `bus_volts[0] >= 18`; on-battery = battery ≥ 18 V && no gen/APU/GPU amps | per-ADIRU supply (6.4) and `on_battery` = running from backup hot bus |
| `a380x_isis_adapter.inc:112-126` | DC ESS = `bus_volts[0] >= 18`, DC HOT = any battery ≥ 18 V | `DC_ESS`, `HOT1` |
| `a380x_clock.inc:15-30` | any battery or bus ≥ 18 V | hot bus (L: HOT1) |
| `a380x_xpkit_*.inc`, `a380x_xpkit_adapter.inc:279` | stock `bus_volts` etc. for the external renderer | native bus states |
| `a380x_efb.c:150-230,296-325,1023-1051` | stock battery/GPU/APU/generator for "aircraft power" (tablet charging) and presets | native `any AC main powered` (tablet charging only from aircraft power, M), native presets (7.3) |
| `a380x_iscs.inc:198,383,400` | stock GPU / APU commands | native GPU connect + APU pbs |
| `FlyWithLua/Scripts/A380X_XP12_Avionics.lua:108-122`, `A380X_XP12_EFB.lua:65-90` | read stock battery/GPU/APU/gen datarefs | read `a380x/xp12/elec/*` (or keep, since stock is slaved) |
| `config/cockpit_controls.json` (ELEC BAT1/2, GEN1-4 → `sim/electrical/*_toggle`; APU MASTER → `APU_starter_switch`; APU START → `sim/electrical/APU_start`) | stock commands | `a380x/xp12/elec/*` commands (coordinate with the panel-controls developer; do not edit concurrently) |

### 9.3 Slaving X-Plane's electrical system (adapter writes, every frame after the core step)

| Stock dataref | Value written | Why |
| --- | --- | --- |
| `sim/cockpit2/electrical/battery_on[0..nb-1]` | `1` if native DC ESS **or** any DC main bus is powered, else `0` | keeps X-Plane buses (and anything stock: radios, X-Plane instruments, lighting with `bus_volts`) alive exactly when the native network has DC |
| `sim/cockpit/electrical/battery_charge_watt_hr[i]` | pin to `_battery_watt_hr_max` while native DC is powered | X-Plane would otherwise drain its single battery at its own load and black out stock consumers while the native batteries are fine. The native battery model (3.10) is the only endurance model |
| `sim/cockpit2/electrical/generator_on[0..3]` | native GEN n pb ON && drive connected && !ENG FIRE released | X-Plane generator amps/volts match. Stock gen failure still observable |
| `sim/cockpit2/electrical/APU_generator_on` | APU GEN A \|\| B pb ON | — |
| `sim/cockpit2/electrical/GPU_generator_on` | `any ext_connected && on_ground` | lets X-Plane's ground-power truck and the stock bus see GPU |
| `sim/cockpit2/switches/avionics_power_on` | native DC ESS powered | legacy/third-party readers. **No port module may use it as a power criterion after migration** |
| `sim/cockpit2/electrical/cross_tie` | 1 | single stock bus picture |
| `sim/cockpit2/electrical/inverter_on[0]` | static inverter output | cosmetic |
| `sim/cockpit2/electrical/plugin_bus_load_amps[0]` | optional: native total DC load / 28 V | stock ammeter realism |

APU turbine (two options; recommend A for a first version, M):

* **A: X-Plane turbine, native ECB.** The native core owns MASTER/START/flap/AVAIL logic. It writes
  `APU_starter_switch = 1` when `MASTER && ECB powered` (DC ESS) and `= 2` while the start contactors are
  closed (DC APU powered, flap open, N < 55 %). It reads `APU_N1_percent` as N. AVAIL is computed with
  the FBW rule (`ECB:328-337`). APU GEN output when N ≥ 77 %. Pros: X-Plane fuel burn, sound and EGT stay
  consistent. Cons: X-Plane spool-up profile 74 s (ACF), not the FBW curve.
* **B: native PW980 port** (`PW980` N/EGT polynomials, `ECB`, `FLAP`). It publishes
  `a380x/xp12/apu/n_percent` etc. and keeps the stock APU off (`APU_starter_switch = 0`). Fuel burn is then
  a native write to tank quantities. Higher fidelity, more work.

GPU / external power:

* Native owns `ext_connected[4]` (one flag; all four set together, as `GPUM`). flyPad "Ground power"
  toggles it. If `GPU_can_be_called_for`, also call `sim/ground_ops/toggle_gpu_request` so X-Plane shows the
  GPU truck (M). Edge-detect a user-made X-Plane GPU request (`GPU_generator_on` 0→1 not written by us)
  and adopt it as "connect". Auto-disconnect when ground speed > 0.3 kt (`GPUM` "disable ext power when
  aircraft starts moving", H) or when airborne.

### 9.4 Failure mapping (FAULT lamps)

| Native failure | X-Plane input (read, ≥ 6 = failed, as the port already does: `a380x_isis_adapter.inc:97`) | Native custom dataref (writable int) |
| --- | --- | --- |
| GEN 1-4 | `rel_genera0..3` | `a380x/xp12/elec/fail/gen[4]` |
| BAT 1, 2, ESS, APU | `rel_batter0..3` | `.../fail/bat[4]` |
| AC1-4 bus | `rel_esys..rel_esys4` (bus 1-4) (M mapping) | `.../fail/ac_bus[4]` |
| DC ESS bus / DC1 / DC2 | `rel_esys5`, `rel_esys6` (M) | `.../fail/dc_bus[3]` |
| APU / APU fire | `rel_apu`, `rel_apu_fire` | — |
| TR 1/2/ESS/APU, static inverter, DRIVE oil, APU GEN A/B, EXT n, ELMU, galley | none | `.../fail/tr[4]`, `stat_inv`, `drive_oil[4]`, `apu_gen[2]`, `ext[4]`, `elmu`, `galley` |

The flyPad FAILURES page (`a380x_efb.c:89-99`) should list the native failures.

### 9.5 Pitfalls

1. **Fighting the sim**: writing `bus_volts`, or toggling stock batteries by command, races X-Plane's own
   electrical step. Write switch states once per frame in a single adapter, after the core, and never
   from other modules.
2. **Stock battery drain**: with 1 stock battery of 1272 Wh and generators off on the ground, X-Plane
   drains it and stock `bus_volts` drop while native says powered. Pin the charge (9.3).
3. **Start with engines running**: X-Plane turns batteries, generators and avionics on and starts engines
   before the plugin's first frame. Read the state (7.2), then take ownership. Do not "correct" a running
   aircraft to cold & dark.
4. **Hardware bindings**: users bind `sim/electrical/battery_1_toggle` etc. Intercept these commands
   (`XPLMRegisterCommandHandler(..., before=1)` returning 0) and route them to native pbs:
   battery_1 → BAT 1, battery_2 → BAT 2, generator_n → GEN n, APU_start/on/off → APU pbs,
   GPU_* → EXT connect/EXT pbs, avionics_* → ignore (log once). Without this, X-Plane state and native pbs
   diverge.
5. **Pause / replay / time acceleration**: freeze all elec timers on pause and replay, as the port's
   ADIRS already does (`a380x_adirs_adapter.inc:63`). Scale with sim time, not wall time.
6. **Order of modules**: run `elec_update()` first in `flight_loop` (`a380x_avionics.c:274-327`), before
   `read_side`, `adirs_update`, displays, FCU, EFB.
7. **Plane reload / situation load**: re-initialise the core on `XPLM_MSG_PLANE_LOADED` param 0
   (`a380x_avionics.c:596-605`). RAT latch, DRIVE disconnect and battery charge reset.
8. **Lua and other plugins** that read stock datarefs still work because stock is slaved. Do not
   unregister stock behaviour.
9. **Concurrent ownership**: the ELEC panel OBJ/manipulators and `config/cockpit_controls.json` are being
   changed by another developer. Expose stable `a380x/xp12/elec/...` command and dataref names (10.3)
   that the panel side can bind to.

---

## 10. Recommended native module design

### 10.1 Files

* `native/avionics/a380x_elec.h` / `a380x_elec.c`: XPLM-free, GL-free core (pure C99). Unit-testable
  like `a380x_adirs.c`.
* `native/avionics/a380x_elec_adapter.inc`: XPLM boundary. Reads sim inputs, registers datarefs and
  commands, slaves stock datarefs (9.3), owns failure mapping.
* `native/avionics/a380x_elec_presets.c` (optional): the FBW preset step tables (7.3) as data.
* `native/avionics/test_elec.c`: unit tests (section 11), registered in `build.py` like `ADIRS_TEST`.

### 10.2 Core API (sketch)

```c
typedef enum {
  ELB_AC1, ELB_AC2, ELB_AC3, ELB_AC4, ELB_AC_ESS, ELB_AC_EMER, ELB_AC_EHA, ELB_AC_GND_SVC,
  ELB_DC1, ELB_DC2, ELB_DC_ESS, ELB_DC_ESS_INFLT /*108PH*/, ELB_DC_EHA, ELB_DC_APU /*309PP*/,
  ELB_APU_START, ELB_HOT1, ELB_HOT2, ELB_HOT_ESS, ELB_HOT_APU, ELB_DC_GND_SVC,
  ELB_ESS_FUEL, ELB_REFUEL_BAT, ELB_COUNT } A380XElecBus;

typedef enum { ELS_NONE, ELS_GEN1..ELS_GEN4, ELS_APU_A, ELS_APU_B, ELS_EXT1..ELS_EXT4,
  ELS_EMER_GEN, ELS_STAT_INV, ELS_BAT1, ELS_BAT2, ELS_BAT_ESS, ELS_BAT_APU,
  ELS_TR1, ELS_TR2, ELS_TR_ESS, ELS_TR_APU } A380XElecOrigin;   /* bitmask in node */

typedef struct { float volts, hz; uint32_t origins; } A380XElecNode;

typedef struct {                       /* crew controls: owned by the core, set via commands */
  uint8_t bat_auto[4], gen_on[4], apu_gen_on[2], ext_on[4], drive_released[4];
  uint8_t bus_tie_auto, ac_ess_feed_altn, galley_auto, pax_sys_on, comm_on[2], elmu_auto;
  uint8_t rat_man_on, emer_gen1_line_on, bat_selector /*0 ESS..4 BAT2*/, ann_lt /*0 TEST,1 BRT,2 DIM*/;
} A380XElecPanel;

typedef struct {                       /* sim inputs, filled by the adapter each frame */
  double dt; int paused, on_ground, sim_ready;
  double ias_kt, gear_compressed_both;
  double eng_gearbox_rpm[4], eng_n2_pct[4]; int eng_fire_released[4];
  int ext_connected[4];
  double apu_n_pct; int apu_gen_fault[2], apu_emergency_shutdown;
  int lsw1_adiru1, lsw1_adiru3, lsw3_adiru1, lsw3_adiru3;   /* 50 kt / 100 kt discretes */
  struct { uint8_t gen[4], bat[4], tr[4], stat_inv, ac_bus[4], dc_bus[3], ext[4], drive_oil[4], elmu, galley; } fail;
} A380XElecInputs;

typedef struct {
  A380XElecPanel panel;
  A380XElecNode bus[ELB_COUNT];
  uint64_t contactors;                 /* bit per contactor id, section 2.3 */
  double bat_ah[4], bat_v[4], bat_a[4];
  struct { int state; double loss_of_ac, overcurrent; int line_closed, gnd_svc_closed; } bcru[3];
  double vfg_stable_s[4]; int drive_connected[4];
  double sply2_false_timer, emer_active_s, rat_stow, rat_rpm;
  int tefo, emer_config, ess_inflt_closed, dc_tie_6pc2, dc_tie_980pc;
  int galley_main_shed, galley_sec_shed;
  struct { uint8_t upper[24], lower[24]; } lamp;   /* indexed by A380XElecPb enum */
  int spawn_mode;
} A380XElecState;

void a380x_elec_init(A380XElecState *s, int spawn_mode);      /* 7.1 / 7.2 */
void a380x_elec_step(A380XElecState *s, const A380XElecInputs *in);  /* sections 3-4, FBW order */
int  a380x_elec_powered(const A380XElecState *s, A380XElecBus b);
int  a380x_elec_any(const A380XElecState *s, uint32_t bus_mask);
int  a380x_elec_du_powered(const A380XElecState *s, int du_id);    /* table 6.1 */
void a380x_elec_command(A380XElecState *s, int pb, int action);    /* press/toggle/set */
```

* Update cadence: every flight-loop callback (the port's loop runs every frame, `return -1.0f`). Clamp
  `dt` to [0, 0.25 s] and sub-step at ≤ 50 ms for the BCRU timers, battery integration and RAT (FBW RAT
  uses 33 ms, `EM:69`). The full step is a few hundred boolean operations, so the cost is negligible.
* Contactors are instantaneous except: VFG 0.5 s stability, BCRU line 3 s hold, BCRU battery 5 s,
  108PH 10 s delayed-false, APU flap 6-12 s, APU AVAIL 2 s at > 95 %.
* Mutual-interlock contactors (6PC2/980PC) use the previous-frame values. Keep them in state.
* Node propagation: implement each FBW `flow(a,b)` as `b = a.powered ? merge(b,a) : b` in the FBW
  statement order (`AC:96-176,434-547`, `DC:160-377`). Contactors are nodes whose powered state is
  `closed ? input : none`.

### 10.3 Published datarefs and commands (namespace `a380x/xp12/elec/`)

Read-only unless noted (int unless noted):

| Dataref | Content |
| --- | --- |
| `bus/<name>_powered`, `bus/<name>_volts` (float) | for every bus in 2.2 (`ac1..ac4, ac_ess, ac_emer, ac_eha, ac_gnd_svc, dc1, dc2, dc_ess, dc_ess_inflt, dc_eha, dc_apu, hot1, hot2, hot_ess, hot_apu, dc_gnd_svc`) |
| `contactor/<id>` | 0/1 per contactor id in 2.3 (ids with `.` written as `_`) |
| `gen/potential[4]`, `gen/freq[4]`, `gen/load_pct[4]` (float), `gen/line_closed[4]`, `drive/connected[4]`, `drive/oil_temp_c[4]` (float) | SD ELEC AC |
| `apu_gen/potential[2]`, `apu_gen/freq[2]`, `apu_gen/load_pct[2]` (float) | also aliased to `a380x/xp12/apu/generator_{volts,hz,load_percent}` (SD adapter) |
| `ext/connected[4]` (**writable**, flyPad/ground crew), `ext/potential`, `ext/freq` (float) | |
| `emer_gen/potential`, `emer_gen/load_pct`, `rat/stow_ratio` (float), `stat_inv/potential`, `stat_inv/freq` | |
| `bat/potential[4]`, `bat/current[4]`, `bat/charge_ah[4]` (float, charge writable for flyPad), `bat/display_text` (string or float) | BAT display (`BATD`) |
| `tr/potential[4]`, `tr/current[4]` (float) | TR1, TR2, TR ESS, TR APU |
| `emer/active`, `emer/tefo`, `emer/config`, `galley_shed` | ECAM |
| `pb/<name>` (**writable** 0/1 position) + `lamp/<name>_upper`, `lamp/<name>_lower` (float 0..1 including ANN LT DIM/BRT and lamp-bus power) | names: `bat1, bat2, bat_ess, bat_apu, gen1..4, apu_gen_a, apu_gen_b, ext1..4, drive1..4, bus_tie, ac_ess_feed, galley, pax_sys, comm1, comm2, elmu, rat_man_on, rat_fault, emer_gen1_line, emer_gen_test, bat_selector` |
| `du/<capt_pfd,capt_nd,capt_mfd,fo_pfd,fo_nd,fo_mfd,ewd,sd,isis,capt_oit,fo_oit>_state` | 0 off, 1 boot, 2 self-test, 3 on, 4 standby |
| `spawn_mode` | 0 C&D, 1 turnaround, 2 ready |
| `fail/...` (**writable**) | section 9.4 |
| `settings/du_selftest_s` (**writable** float, default 15) | FBW `CONFIG_SELF_TEST_TIME` |

Commands (`a380x/xp12/elec/<pb>_toggle`, `<pb>_on`, `<pb>_off` for two-position pbs. EXT n and DRIVE n
are `_press`. BAT selector `bat_selector_left/right`. `gpu_connect_toggle`. `preset/<1..5>`). These are
the bind points for the 3D manipulators that the other developer is building. Hold a short
press/animation ratio dataref per pb if the panel needs it (`pb/<name>_press_ratio`), following the
existing FCU/EFIS pattern (`PORT:docs/COCKPIT_CONTROLS.md:229-249`).

---

## 11. Test plan

### 11.1 Pure-C unit tests (`test_elec.c`)

Each FBW test in `EM` becomes a table-driven case asserting powered state **and** origin per bus:

1. `everything_off` (`EM:640-675`): no AC/DC buses; hot buses = their battery.
2. `everything_off_ext_power_connected` (`EM:677-718`): AC GND SVC from EXT, DC GND SVC from TR2, nothing else.
3. `distribution_on_ground_only_batteries` (`EM:775-824`): DC ESS = BAT1+BAT ESS, AC EMER = static inverter, DC APU = APU BAT, DC1/DC2 off, 108PH off.
4. BAT 1 only / ESS only / BAT 2 only: DC ESS off in all three cases (ELC needs both 1 and ESS).
5. Single EXT 2 (`EM:826-892`), EXT 2 + APU (`EM:894-973`), double EXT (`EM:975-1041`), double EXT + APU (`EM:1043-1121`), APU only (`EM:1123-1202`), APU + one engine (`EM:1204`), double EXT + one engine (`EM:1286`), outer engine with same-side or other-side APU gen (special table, `EM:1362,1438`).
6. Flight: normal (`EM:1518`), one/two/three gens off with and without APU (`EM:1590-2141`); in-flight single-APU-gen rule (3.1).
7. Emergency: `distribution_emergency_config_before_gen_available` / `after_gen_available` (`EM:2143,2195`); RAT deploy only above 100 kt with no AC; MAN ON on ground with gear compressed does not activate the GCU.
8. TR ESS fault (`EM:2400`), TR APU fault (`EM:2471`), static inverter input rules (`EM:2682-2724`).
9. Pb-off cases: APU running with APU GEN pbs off, EXT connected with EXT pb off (`EM:2726-2780`); empty ESS/APU battery with pb off not fed back (`EM:2782-2805`).
10. AC ESS FEED: AC1 lost → 3XC2 within the same step; ALTN with AC1 live → AC4 feeds; FAULT lamp only when AC EMER is unpowered (`EM:2870-2885`).
11. BUS TIE OFF: EXT 2 only gives AC3/AC4 unpowered and DC1/DC2 separated (980PC open).
12. Galley shed (`EM:2887-2964`).
13. APU start with BAT APU off: start contactors open (`EM:2966`).
14. Timers: BCRU line contactor holds 3.0 s then opens; battery contactor opens at 5.0 s; 108PH stays 10.0 s after AC loss on ground; VFG closes after 0.5 s in range; DU boot 0.25-0.45 + 15 s; DU standby 10 s; ISIS 90 s.
15. Battery: voltage curve spot values (q = 0, 3.488, 10, 22.449, 23 Ah); charge current clamp 10 A; discharge to < 24.5 V opens ELC/6PB3.
16. Lamps: every row of table 4.2, including "lamp bus unpowered → dark" and ANN LT TEST.
17. Spawn modes: C&D (all BAT OFF, DUs Off), turnaround, ready (engines running → AC1-4 from GEN, DUs On).
18. Presets: step tables execute in order, honour conditions/delays, expedited mode skips PROC steps.

### 11.2 Adapter tests (mock XPLM, like `test_adirs_adapter.c`)

* Slaving: stock `battery_on` follows native DC; `battery_charge_watt_hr` pinned; `GPU_generator_on` follows connect.
* Command interception: `sim/electrical/battery_1_toggle` flips native BAT 1 and returns 0 (consumed).
* Failure mapping: `rel_genera1 = 6` → GEN 2 FAULT lamp after the next step.
* Pause/replay freezes timers.
* Plane reload re-inits the core and the spawn decision.

### 11.3 In-sim manual checklist (X-Plane 12)

1. Load at a gate, engines off: everything dark, ISIS dark, BAT display blank, stock `bus_volts` 0.
2. Section 8.1 steps 1-17, checking each "expected" column, the SD ELEC pages and the lamp states.
3. Load with "start with engines running": READY state, no DU boot, no ISIS INIT, ADIRS aligned.
4. Airborne spawn: READY, RAT stowed.
5. In flight, fail GEN 1-4 above 100 kt: emergency elec, RAT deploys, EMER GEN on AC ESS, displays per 6.5, RAT & EMER GEN FAULT only during the gap.
6. Shutdown/secure per 8.2; BAT OFF with APU running gives the APU emergency shutdown and hint.
7. Time acceleration 4x and pause during APU start and ADIRS alignment.
8. Hardware-bound stock battery/GPU commands act on native pbs.

---

## 12. Open questions and assumptions

1. **Battery capacity**: FBW uses its shared 23 Ah model. The real A380 value (often quoted 50 Ah) was
   not verified. Keep 23 Ah (FBW) unless an authoritative source appears. (L)
2. **Real-aircraft load figures** for battery endurance (3.10) are estimates. Tune against the
   ToLiss-style expectation of 30+ min on batteries. (L)
3. **Battery-only displays on the ground**: FBW CDS has the CAPT PFD on 108PH (shed on ground on
   batteries), so EWD and CAPT MFD come up but not the PFD. The `BEH` emissive uses DC ESS. Confirm
   against FBW in MSFS or Airbus data before finalising. (M)
4. **GEN FAULT with engines stopped**: FBW lights it (contactor open && pb ON). The real A380 may
   inhibit it at low engine speed. FBW behaviour is kept. (M)
5. **APU GEN / BAT / DRIVE / ELMU FAULT** conditions are not modelled in FBW. This spec drives them from
   failures only. (M)
6. **ADIRU power supplies and ON BAT** are not modelled by FBW. The proposal in 6.4 is an assumption.
   Also decide whether IR alignment survives the EXT→APU transfer (it should: the backup battery path). (L)
7. **APU turbine ownership** (9.3 option A vs B) and the 74 s X-Plane spool-up vs ≈60 s FBW/PW980. (M)
8. **ACF electrical config**: confirm `_num_batteries` (1 now vs 4 expected by the adapter test) and the
   meaning of `_bus_bat_is_on` / `_bus_gen_is_on` values (4, 3, 2, ...). The slaving strategy only needs
   one stock bus that a battery and the generators feed. (M)
9. **COMMERCIAL 1/2, PAX SYS, ELMU, GALLEY separation**: FBW merges them into two vars (3.11). The port
   keeps five pbs. The exact real shedding granularity is not needed for cockpit fidelity. (M)
10. **EMER GEN 1 LINE / EMER GEN TEST** have no FBW logic. Implement as lamps/pb only, or with A380 FCOM
    data if available. (L)
11. **Refuel-on-battery** (501/502/503PP switching) is a TODO in FBW (`DC:357-366`). Out of scope. (H)
12. **ELC opening on BAT 1 fault / SOC** is partially implemented in FBW (`DC:225,250-251`). Follow FBW. (H)
13. **Securing**: battery-off-before-APU-stopped consequence (8.2 step 5) is an A320-derived
    assumption. (L)
14. **EFIS control panel and integral lighting buses** are not in FBW logic (only emissive in `BEH`). The
    split proposed in 9.2 is an assumption. (L)
