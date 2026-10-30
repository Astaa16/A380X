#!/usr/bin/env python3
"""Generate the FBW -> X-Plane sound port map (stage-2 contract) from the staged inventory.

Inputs (all produced read-only from FBW sources):
  build/fbw-sounds-stage/manifest.json   tools/sound_fbw_stage.py
  build/fbw-sounds-stage/emitters.json   tools/sound_emitter_positions.py

Outputs:
  fmod/a380x-sound-port-map.json         one row per FBW event the A380X posts, with its X-Plane
                                         trigger, RTPC sources, emitter, bus/view gating, layers and
                                         curves, playback style, priority and port status
  docs/SOUND_PORT_PLAN.md                the generated appendix between the GENERATED markers

    python3 tools/sound_port_map.py --stage build/fbw-sounds-stage \
        --output fmod/a380x-sound-port-map.json --doc docs/SOUND_PORT_PLAN.md
"""
from __future__ import annotations

import argparse
import json
import math
from collections import defaultdict
from pathlib import Path

# --------------------------------------------------------------------------- X-Plane sources
# id -> (kind, exact reference, conversion to the unit FBW feeds to Wwise)
SOURCES: dict[str, tuple[str, str, str]] = {
    "N1[e]": ("stock", "sim/flightmodel2/engines/N1_percent[e]", "% (FBW A32NX_ENGINE_N1:n / TURB ENG N1, percent)"),
    "N2[e]": ("stock", "sim/flightmodel2/engines/N2_percent[e]", "% (FBW A32NX_ENGINE_N2:n)"),
    "FF_KGH[e]": ("stock", "sim/cockpit2/engine/indicators/fuel_flow_kg_sec[e]", "x3600 -> kg/h (FBW A32NX_ENGINE_FF:n)"),
    "REV[e]": ("stock", "sim/flightmodel2/engines/thrust_reverser_deploy_ratio[e]", "0..1 ratio, fed unscaled like FBW A32NX_REVERSER_n_POSITION"),
    "TAS_KT": ("stock", "sim/flightmodel/position/true_airspeed", "m/s x1.943844 -> kt"),
    "IAS_KT": ("stock", "sim/flightmodel/position/indicated_airspeed", "kt"),
    "AGL_FT": ("stock", "sim/flightmodel/position/y_agl", "m x3.28084 -> ft"),
    "GS_KT": ("stock", "sim/flightmodel/position/groundspeed", "m/s x1.943844 -> kt"),
    "GS_MS": ("stock", "sim/flightmodel/position/groundspeed", "m/s"),
    "ON_GROUND": ("stock", "sim/flightmodel/failures/onground_any", "bool"),
    "VS_RADIO_FPM": ("derived", "d/dt sim/cockpit2/gauges/indicators/radio_altimeter_height_ft_pilot x60",
                     "ft/min, latched on the ON_GROUND rising edge (FBW A32NX_AUTOPILOT_H_DOT_RADIO)"),
    "RHO": ("stock", "sim/weather/aircraft/air_density_kg_m3 (fallback sim/weather/rho)", "kg/m3"),
    "OAT_C": ("stock", "sim/cockpit2/temperature/outside_air_temp_degc", "degC"),
    "APU_N": ("stock", "sim/cockpit2/electrical/APU_N1_percent", "% (FBW A32NX_APU_N_RAW)"),
    "APU_MASTER": ("port", "a380x/xp12/apu/master_on", "bool"),
    "APU_FLAP_PCT": ("port", "a380x/xp12/apu/flap_open_ratio", "x100 -> % (FBW A32NX_APU_FLAP_OPEN_PERCENTAGE)"),
    "APU_START_CONTACTOR": ("port", "a380x/xp12/elec/contactor/10KA_5KA", "bool"),
    "AC_BUS[n]": ("port", "a380x/xp12/elec/bus/ac{n}_powered", "bool, n=1..4"),
    "EXT_LINE_CONTACTOR[n]": ("port", "a380x/xp12/elec/contactor/990XG{n}", "bool, n=1..4 (same ids as FBW)"),
    "BAT_ESS_CONTACTOR": ("port", "a380x/xp12/elec/contactor/6PB3", "bool"),
    "MAIN_BUS_V": ("port", "a380x/xp12/elec/bus/ac1_volts", "V (FBW binds ELECTRICAL MAIN BUS VOLTAGE)"),
    "PACK_ON[1]": ("stock", "sim/cockpit2/bleedair/actuators/pack_left", "bool; FBW uses pack flow rate > 0.005"),
    "PACK_ON[2]": ("stock", "sim/cockpit2/bleedair/actuators/pack_right", "bool"),
    "EPUMP_RPM[p]": ("new", "a380x/xp12/hydraulics/epump_rpm[4] (YA,YB,GA,GB)",
                     "rpm; interim: sim/cockpit2/hydraulics/actuators/electric_hydraulic_pump_on x8000"),
    "FLAP_RATIO": ("stock", "sim/flightmodel2/controls/flap1_deploy_ratio", "0..1"),
    "SLAT_RATIO": ("stock", "sim/flightmodel2/controls/slat1_deploy_ratio", "0..1 (x100 for LEADING EDGE FLAPS LEFT PERCENT)"),
    "FLAP_HANDLE": ("stock", "sim/cockpit2/controls/flap_handle_request_ratio", "0..1 detents (FBW A32NX_FLAPS_HANDLE_PERCENT)"),
    "SPD_BRK_HANDLE": ("stock", "sim/cockpit2/controls/speedbrake_ratio", "-0.5 armed, 0..1 (FBW A32NX_SPOILERS_HANDLE_POSITION)"),
    "SPOILERS_ARMED": ("port", "a380x/xp12/pfd/spoilers_armed", "bool"),
    "GEAR_DEPLOY[g]": ("stock", "sim/flightmodel2/gear/deploy_ratio[g]", "0..1, g 0 nose, 1/2 wing, 3/4 body"),
    "GEAR_TOTAL_PCT": ("derived", "mean(sim/flightmodel2/gear/deploy_ratio[0..4]) x100", "% (GEAR TOTAL PCT EXTENDED)"),
    "GEAR_HANDLE": ("port", "a380x/xp12/center_right/gear_lever", "0/1 (FBW A32NX_GEAR_HANDLE_POSITION)"),
    "THR_LEVER[e]": ("stock", "sim/cockpit2/engine/actuators/throttle_ratio[e]",
                     "0..1 -> detent % via fcu_thrust.idle/cl/mct/toga (FBW A32NX_3D_THROTTLE_LEVER_POSITION_n)"),
    "STICK_LOCK": ("port", "a380x/xp12/glareshield/priority", "!=0 -> lock (FBW A32NX_STICK_LOCK_ACTIVE)"),
    "DOOR_LOCKED": ("port", "a380x/xp12/cockpit_door/locked", "bool"),
    "SEATBELT_SW": ("stock", "sim/cockpit2/switches/fasten_seat_belts", "edge"),
    "NO_SMOKING": ("stock", "sim/cockpit2/switches/no_smoking", "edge (FBW A32NX_NO_SMOKING_MEMO)"),
    "WIPER_SPEED[s]": ("stock", "sim/cockpit2/switches/wiper_speed_switch[s]", "0 off, 1 slow, 2 fast"),
    "VIEW_EXTERNAL": ("stock", "sim/graphics/view/view_is_external", "bool -> VIEWPOINT state INSIDE/OUTSIDE"),
    "PRECIP": ("stock", "sim/weather/aircraft/precipitation_on_aircraft_ratio", "0..1"),
    "FWS_VOLUME": ("new", "C state of the native FWS sound manager", "0 full, 1 attenuated -6 dB, 2 silent"),
    "SND_INT_ENGINE": ("new", "a380x/xp12/sound/interior_engine_volume", "-50..50, default 0 (FBW EFB A32NX_SOUND_INTERIOR_ENGINE)"),
    "SND_EXT_MASTER": ("new", "a380x/xp12/sound/exterior_master_volume", "-50..50, default 0 (FBW A32NX_SOUND_EXTERIOR_MASTER)"),
    "SND_INT_WIND": ("new", "a380x/xp12/sound/interior_wind_volume", "-50..50, default 0 (FBW A32NX_SOUND_INTERIOR_WIND)"),
    "ANNOUNCEMENTS": ("new", "a380x/xp12/sound/announcements_enabled", "bool (FBW A32NX_SOUND_ANNOUNCEMENTS_ENABLED)"),
    "CABIN_READY": ("new", "a380x/xp12/cabin/ready", "bool (FBW A32NX_CABIN_READY)"),
    "FLIGHT_PHASE": ("port", "a380x/xp12/avionics/flight_phase", "FBW 1..12"),
    "RA_FT": ("stock", "sim/cockpit2/gauges/indicators/radio_altimeter_height_ft_pilot", "ft"),
    "CONTROL_COMMAND": ("port", "config/cockpit_controls.json command/dataref of the control node", "edge"),
}

# RTPC name -> (source id, per-engine index?)
RTPC_SOURCES: dict[str, tuple[str, str]] = {
    "SIMVAR_TURB_ENG_N1": ("N1[e]", "per engine"),
    "SIMVAR_TURB_ENG_N2": ("N2[e]", "per engine"),
    "SIMVAR_TURB_ENG_N1_DERIVED": ("d/dt N1[e] (Engine_n_Start binds d/dt N2[e])", "per engine, %/s"),
    "SIMVAR_TURB_ENG_REVERSE_NOZZLE_PERCENT": ("REV[e]", "engines 2/3 only"),
    "SIMVAR_AIRSPEED_TRUE": ("TAS_KT", ""),
    "SIMVAR_PLANE_ALT_ABOVE_GROUND": ("AGL_FT", ""),
    "SIMVAR_AIRSPEED_INDICATED": ("IAS_KT", ""),
    "SIMVAR_GROUND_VELOCITY": ("GS_KT", "units per XML binding (KNOTS)"),
    "SIMVAR_LEADING_EDGE_FLAPS_LEFT_PERCENT": ("SLAT_RATIO x100", ""),
    "SIMVAR_APU_PCT_RPM": ("APU_N", ""),
    "SIMVAR_APU_PCT_RPM_DERIVED": ("d/dt APU_N", "%/s"),
    "SIMVAR_GEAR_ANIMATION_POSITION": ("GEAR_DEPLOY[0] x100", ""),
    "SIMVAR_GEAR_TOTAL_PCT_EXTENDED_DERIVED": ("d/dt GEAR_TOTAL_PCT", "%/s"),
    "SIMVAR_AMBIENT_DENSITY": ("RHO", "not bound by FBW XML: MSFS value source unknown, feed kg/m3"),
    "LOCALVAR_A32NX_INTERIOR_ENG_VOLUME": ("SND_INT_ENGINE", "user setting"),
    "LOCALVAR_A32NX_EXTERIOR_VOLUME": ("SND_EXT_MASTER", "user setting"),
    "LOCALVAR_A32NX_INTERIOR_WIND_VOLUME": ("SND_INT_WIND", "user setting"),
    "LOCALVAR_FWC_VOLUME": ("FWS_VOLUME", ""),
    "LOCALVAR_A32NX_ANNOUNCEMENT_TOGGLE": ("ANNOUNCEMENTS", "not bound by FBW XML"),
    "3779073172": ("doppler (unresolved MSFS game parameter, x=-4800..4800 cents)",
                   "not mapped: FMOD applies doppler from the velocity passed to XPLMSetAudioPosition"),
    "1948948252": ("camera state (unresolved, x=0..10) -> Flanger FX bypass", "not mapped: no DSP through XPLMSound"),
    "4194702645": ("unresolved, x=-24..0 (TR380 pitch)", "not bound by FBW XML: hold at default 0"),
}

INTERIOR, EXTERIOR = "xplm_AudioInterior", "xplm_AudioExteriorAircraft"
PRIORITY = {"aural_warnings": 100, "engines": 90, "apu": 80, "systems": 60, "environment": 50,
            "cockpit_controls": 40}


def eng(name: str) -> int | None:
    for token in name.replace("_", " ").split():
        if token.isdigit() and 1 <= int(token) <= 4 and name.lower().startswith("engine"):
            return int(token) - 1
    return None


# --------------------------------------------------------------------------- trigger table
def trigger_for(name: str) -> dict:
    """X-Plane trigger for one FBW event (FBW behaviour first, deviations listed)."""
    n = name
    e = eng(n)
    if e is not None:
        if n.endswith("_Start"):
            return {"type": "continuous_while", "expr": f"N2[{e}] >= 1",
                    "fbw": "LocalVar A32NX_ENGINE_N2:n >= 1",
                    "deviation": "FBW XML binds Engine_2_Start to A32NX_ENGINE_N2:1 and places Engine_4_Start on "
                                 "SOUND_ENGINE_3; the port uses the event's own engine (upstream typo)"
                    if n in ("Engine_2_Start", "Engine_4_Start") else None}
        if n.endswith("_FF"):
            return {"type": "continuous_while", "expr": f"N2[{e}] >= 1 && FF_KGH[{e}] >= 1",
                    "fbw": "A32NX_ENGINE_N2:n >= 1, requires A32NX_ENGINE_FF:n >= 1"}
        return {"type": "continuous_while", "expr": f"N1[{e}] >= 0 (always; started when N1[{e}] > 0.05 || N2[{e}] > 0.05)",
                "fbw": "Range LowerBound=0 on N1: posted for the whole session; the RTPC volume curves give silence at rest",
                "deviation": "voice stopped below 0.05 % N1/N2 after the curve gain is < -60 dB (saves voices; inaudible)"}
    table: dict[str, dict] = {
        "ac1": {"type": "enter_range_oneshot", "expr": "N2[0] in [51,52]", "fbw": "A32NX_ENGINE_N2:1 51..52"},
        "ac2": {"type": "enter_range_oneshot", "expr": "N2[0] in [52,53]", "fbw": "A32NX_ENGINE_N2:1 52..53"},
        # APU
        "apuwhine": {"type": "continuous_while", "expr": "APU_N >= 1"},
        "apuwhine2": {"type": "continuous_while", "expr": "APU_N >= 1"},
        "apubass": {"type": "continuous_while", "expr": "APU_N >= 2"},
        "apubass2": {"type": "continuous_while", "expr": "APU_N >= 1"},
        "apurun": {"type": "continuous_while", "expr": "APU_N >= 1"},
        "apushut": {"type": "enter_range_oneshot", "expr": "APU_N in [75,95]",
                    "note": "enters on spool-up too; the d/dt APU_N volume curve silences it while accelerating"},
        "apustartinit": {"type": "enter_range_oneshot", "expr": "APU_N >= 1"},
        "apuflapopen": {"type": "continuous_while", "expr": "APU_MASTER && APU_FLAP_PCT < 99",
                        "fbw": "A32NX_OVHD_APU_MASTER_SW_PB_IS_ON, requires L:APU_FLAP_OPEN <= 99 (LVar never written -> 0)",
                        "deviation": "requirement evaluated on the real flap position (port); FBW's is always true"},
        "apuflapopenfull": {"type": "enter_range_oneshot", "expr": "APU_FLAP_PCT >= 100",
                            "fbw": "L:APU_FLAP_OPEN >= 100: that LVar is never written in the A380X, so FBW never plays it",
                            "deviation": "port plays it on the real flap (FBW intent); set disabled for strict parity"},
        "apubassout": {"type": "continuous_while", "expr": "APU_N >= 2"},
        "apuwhineext": {"type": "continuous_while", "expr": "APU_N >= 1"},
        "apuwhineext2": {"type": "continuous_while", "expr": "APU_N >= 1"},
        "apurun380ext": {"type": "continuous_while", "expr": "APU_N >= 1"},
        "apustartinitext_01": {"type": "continuous_while", "expr": "APU_N in [1,55]"},
        "apuflapopenext": {"type": "continuous_while", "expr": "APU_FLAP_PCT in [1,99]"},
        "apuflapopenfullext": {"type": "enter_range_oneshot", "expr": "APU_FLAP_PCT >= 100"},
        "apushutdownext": {"type": "enter_range_oneshot", "expr": "APU_N in [75,97]"},
        "startcontactors": {"type": "enter_range_oneshot", "expr": "APU_START_CONTACTOR == 1"},
        "startcontactorsdc": {"type": "enter_range_oneshot", "expr": "APU_START_CONTACTOR == 0"},
        # electrical / avionics cooling
        "TR380": {"type": "continuous_while", "expr": "AC_BUS[1]"},
        "AEVC380": {"type": "continuous_while", "expr": "AC_BUS[1]"},
        "contactor": {"type": "edge_any", "expr": "change of EXT_LINE_CONTACTOR[1..4]",
                      "fbw": "four SimVarSounds without Range: posted on every value change"},
        "acbus1": {"type": "enter_range_oneshot", "expr": "AC_BUS[1] == 1"},
        "acbus1_01": {"type": "enter_range_oneshot", "expr": "AC_BUS[1] == 1"},
        "acbus2": {"type": "enter_range_oneshot", "expr": "AC_BUS[2] == 1"},
        "acbus3": {"type": "enter_range_oneshot", "expr": "AC_BUS[3] == 1"},
        "acbus3_01": {"type": "enter_range_oneshot", "expr": "AC_BUS[3] == 1"},
        "acbus4": {"type": "enter_range_oneshot", "expr": "AC_BUS[4] == 1"},
        "bat1": {"type": "continuous_while", "expr": "BAT_ESS_CONTACTOR == 1"},
        "frontcabfan": {"type": "continuous_while", "expr": "AC_BUS[1]",
                        "fbw": "RTPCs bound (TURB ENG N1, COCKPIT_DOOR_LOCKED) have no curve in the bank"},
        # air conditioning
        "cockpitpacks380": {"type": "continuous_while", "expr": "PACK_ON[1] && pack flow > 0.001",
                            "deviation": "port has two stock packs; FBW keys on pack 1 flow-valve flow rate"},
        "cabinpacks380": {"type": "continuous_while", "expr": "PACK_ON[1] && pack flow > 0.005"},
        "a380pack1ext": {"type": "continuous_while", "expr": "PACK_ON[1]"},
        "a380pack2ext": {"type": "continuous_while", "expr": "PACK_ON[1]"},
        "a380pack3ext": {"type": "continuous_while", "expr": "PACK_ON[2]"},
        "a380pack4ext": {"type": "continuous_while", "expr": "PACK_ON[2]"},
        "pack1extshut": {"type": "enter_range_oneshot", "expr": "PACK_ON[1] falls to 0",
                         "fbw": "flow rate <= 30 (posted when the range is entered; FBW flow is kg/s, so this fires at "
                                "pack start as well - kept as FBW does)"},
        "pack2extshut": {"type": "enter_range_oneshot", "expr": "PACK_ON[2] falls to 0"},
        # hydraulics
        "epump1": {"type": "continuous_while", "expr": "EPUMP_RPM[0] >= 3500"},
        "epump2": {"type": "continuous_while", "expr": "EPUMP_RPM[1] >= 3500"},
        "epump3": {"type": "continuous_while", "expr": "EPUMP_RPM[2] >= 3500"},
        "epump4": {"type": "continuous_while", "expr": "EPUMP_RPM[3] >= 3500"},
        # high lift, gear, controls
        "flaps380": {"type": "continuous_while", "expr": "|d/dt FLAP_RATIO| > 1e-4 (A32NX_IS_FLAPS_MOVING)"},
        "flaps380stop": {"type": "enter_range_oneshot", "expr": "flaps moving -> stopped"},
        "slats380": {"type": "continuous_while", "expr": "|d/dt SLAT_RATIO| > 1e-4 (A32NX_IS_SLATS_MOVING)"},
        "slats380stop": {"type": "enter_range_oneshot", "expr": "slats moving -> stopped"},
        "380gup": {"type": "enter_range_oneshot", "expr": "GEAR_TOTAL_PCT in [0.1,0.9]",
                   "note": "loop media: stopped (1 s fade) when the range is left"},
        "380gmain": {"type": "enter_range_oneshot", "expr": "GEAR_TOTAL_PCT <= 0.95"},
        "noselock380": {"type": "enter_range_oneshot", "expr": "GEAR_TOTAL_PCT <= 0 (uplock)"},
        "nosedownlock380": {"type": "enter_range_oneshot", "expr": "GEAR_TOTAL_PCT >= 1",
                            "note": "FBW units PERCENT: fires at 1 % extension, not at downlock; kept"},
        "gearleverclick": {"type": "enter_range_oneshot", "expr": "GEAR_HANDLE >= 0.4 || GEAR_HANDLE <= 0"},
        "flapnotch": {"type": "enter_range_oneshot", "expr": "FLAP_HANDLE == 0, .25, .5, .75, 1 (each detent)",
                      "deviation": "the port lever has six native detents mapped to four (pfd/flap_handle_index); "
                                   "trigger on every detent index change"},
        "Spoiler0": {"type": "enter_range_oneshot", "expr": "SPD_BRK_HANDLE == 0 && !SPOILERS_ARMED"},
        "Spoiler50": {"type": "enter_range_oneshot", "expr": "SPD_BRK_HANDLE in [0.49,0.51] && !SPOILERS_ARMED"},
        "Spoiler100": {"type": "enter_range_oneshot", "expr": "SPD_BRK_HANDLE >= 0.99 && !SPOILERS_ARMED"},
        "SpoilerARM": {"type": "enter_range_oneshot", "expr": "SPOILERS_ARMED rises"},
        "SpoilerDISARM": {"type": "enter_range_oneshot", "expr": "SPOILERS_ARMED falls"},
        "detent": {"type": "enter_range_oneshot", "expr": "lever % enters 96..100, 76..80, 53..57 (TOGA, FLX/MCT, CL)",
                   "note": "lever % = FBW 3D throttle position; derive from THR_LEVER[e] and fcu_thrust detents"},
        "IdleDetent": {"type": "enter_range_oneshot", "expr": "lever % <= 0 (idle)"},
        "APLock": {"type": "enter_range_oneshot", "expr": "STICK_LOCK rises (priority taken)"},
        "APUnlock": {"type": "enter_range_oneshot", "expr": "STICK_LOCK falls"},
        "tableout": {"type": "enter_range_oneshot", "expr": "table ratio >= 0.01", "port_state": "new: a380x/xp12/cockpit/table_ratio[2]"},
        "tablein": {"type": "enter_range_oneshot", "expr": "table ratio <= 0.99"},
        "windowopen380": {"type": "enter_range_oneshot", "expr": "sliding window ratio >= 0.01",
                          "port_state": "new: a380x/xp12/cockpit/sliding_window_ratio[2]"},
        "windowclose380": {"type": "enter_range_oneshot", "expr": "sliding window ratio <= 0.99"},
        "seatmotorfwd": {"type": "continuous_while", "expr": "seat fwd/aft motor running forward",
                         "port_state": "new: a380x/xp12/cockpit/seat_motion[2][2]"},
        "seatmotorback": {"type": "continuous_while", "expr": "seat fwd/aft motor running aft"},
        "seatmotorup": {"type": "continuous_while", "expr": "seat up/down motor running up"},
        "seatmotordown": {"type": "continuous_while", "expr": "seat up/down motor running down"},
        "seatmotorstop": {"type": "enter_range_oneshot", "expr": "seat motor stops"},
        # environment
        "cockpitwind380": {"type": "continuous_while", "expr": "IAS_KT >= 1"},
        "380cabinwind": {"type": "continuous_while", "expr": "IAS_KT >= 1"},
        "380Geardownwind": {"type": "continuous_while", "expr": "GEAR_TOTAL_PCT >= 0.01"},
        "noseroll380": {"type": "continuous_while", "expr": "GEAR_DEPLOY[0]*100 >= 51 && GEAR_TOTAL_PCT >= 1 (on ground roll)",
                        "note": "FBW keys on GEAR ANIMATION POSITION >= 51 (gear down); the GS_KT volume curve makes it silent at rest"},
        "Centerthump380": {"type": "continuous_while", "expr": "GEAR_DEPLOY[0]*100 >= 51 && GEAR_TOTAL_PCT >= 1"},
        "cabingroll380": {"type": "continuous_while", "expr": "weight on wheels ratio >= 0.001 && GEAR_TOTAL_PCT >= 1",
                          "port_state": "use sim/flightmodel2/gear/on_ground[1..4] any (FBW A32NX_GROUND_WEIGHT_ON_WHEELS_RATIO)"},
        "NWheelSpinAboveGround380": {"type": "enter_range_oneshot", "expr": "GEAR_DEPLOY[0]*100 <= 60"},
        "medtouch380": {"type": "enter_range_oneshot", "expr": "ON_GROUND rises && VS_RADIO_FPM in [-350,-151]"},
        "a380cabtouchhard": {"type": "enter_range_oneshot", "expr": "ON_GROUND rises && VS_RADIO_FPM <= -351"},
        # cabin
        "light_switch_seatbelt": {"type": "edge_any", "expr": "SEATBELT_SW or NO_SMOKING changes (cabin chime)"},
        "cockpit_cabin_call_fwd": {"type": "enter_range_oneshot", "expr": "CALLS FWD or ALL pressed && AC_BUS[1]",
                                   "port_state": "new: a380x/xp12/calls/{fwd,aft,all,mech,emer}"},
        "cockpit_cabin_call_aft": {"type": "enter_range_oneshot", "expr": "CALLS AFT pressed && AC_BUS[1]"},
        "emercabincall": {"type": "enter_range_oneshot", "expr": "EMER CALL on && AC_BUS[1]"},
        "mechhorn": {"type": "continuous_while", "expr": "MECH call pb held"},
        "evachorncockpit": {"type": "continuous_while", "expr": "EVAC command active && horn not silenced",
                            "port_state": "new: a380x/xp12/evac/command, horn_shutoff"},
        "cabin_crew_seats_landing": {"type": "enter_range_oneshot", "expr": "CABIN_READY rises && FLIGHT_PHASE == 6",
                                     "note": "FBW AIRLINER_FLIGHT_PHASE 6 (approach) - map to the port's approach phase"},
        "fwdgalley": {"type": "fws_or_cids", "expr": "CIDS forward galley chime (posted via AvionicSounds)",
                      "note": "no A380X caller found in FBW sources; keep for CIDS calls"},
        "cockpitdooropen": {"type": "enter_range_oneshot", "expr": "DOOR_LOCKED falls"},
        "cockpitdoorclosed": {"type": "enter_range_oneshot", "expr": "DOOR_LOCKED rises"},
        "cvr_test": {"type": "continuous_while", "expr": "RCDR TEST held && GND CTL on",
                     "port_state": "new: a380x/xp12/rcdr/test, ground_control"},
        "printerloop": {"type": "continuous_while", "expr": "printer printing (AnimationSounds)",
                        "port_state": "new: printer state (not modelled)"},
        "paperflip": {"type": "control_event", "expr": "printer paper animation"},
        "papertear": {"type": "control_event", "expr": "printer paper tear"},
        "papercrumble": {"type": "control_event", "expr": "printer paper crumble"},
        "wiper_slowL": {"type": "continuous_while", "expr": "WIPER_SPEED[0] == 1"},
        "wiper_fastL": {"type": "continuous_while", "expr": "WIPER_SPEED[0] == 2"},
        "wiper_parkL": {"type": "enter_range_oneshot", "expr": "WIPER_SPEED[0] -> 0"},
        "wiper_slowR": {"type": "continuous_while", "expr": "WIPER_SPEED[1] == 1"},
        "wiper_fastR": {"type": "continuous_while", "expr": "WIPER_SPEED[1] == 2"},
        "wiper_parkR": {"type": "enter_range_oneshot", "expr": "WIPER_SPEED[1] -> 0"},
        "revlever": {"type": "control_event", "expr": "reverse lever lift (THR_LEVER reverse latch)"},
        "cpdlc_ring": {"type": "fws_or_cids", "expr": "CPDLC message received (datalink)", "port_state": "datalink not modelled"},
    }
    if n in table:
        return table[n]
    # FWS / GPWS / TCAS
    fws = {
        "CRC_380": ("FwsSoundManager continuousRepetitiveChime", "red warning active (auralCrcActive) -> glare.warning source"),
        "mastercaution": ("FwsSoundManager singleChime (0.4 s)", "new amber caution (glare.caution rising edge)"),
        "cavcharge": ("FwsSoundManager cavalryChargeCont", "AP disconnect (guidance.disconnect_warning rising edge)"),
        "cchordloop": ("FwsSoundManager cChordCont / cChordOnce (1.0 s)", "altitude alert"),
        "3click": ("FwsSoundManager tripleClick (0.62 s)", "FMA mode reversion / capability downgrade"),
        "aural_stall_new": ("FwsSoundManager stall (3.0 s, continuous)", "stall warning (pfd/stall_warning)"),
        "V1": ("FwsSoundManager v1 (1.3 s)", "CAS crosses V1 in flight phase 4 (fms/v1_knots)"),
        "brake_max_braking": ("FwsSoundManager brakeMaxBraking", "ROW/ROP (not modelled)"),
        "set_max_reverse": ("FwsSoundManager setMaxReverse", "ROW/ROP (not modelled)"),
        "keep_max_reverse": ("FwsSoundManager keepMaxReverse", "ROW/ROP (not modelled)"),
        "runway_too_short": ("FwsSoundManager runwayTooShort", "ROW/ROP (not modelled)"),
        "autobrake_off": ("FwsSoundManager autoBrakeOff", "center_right autobrake disarm after active"),
        "aural_minimumnew": ("FwsSoundManager minimums (0.67 s)", "LegacyGpws.gpwsMinimums"),
        "aural_100above": ("FwsSoundManager hundred_above (0.72 s)", "LegacyGpws.gpwsMinimums"),
        "new_retard": ("FwsSoundManager retard (0.9 s, repeat 0.2 s)", "RA < 20 ft (A/P off) or < 10 ft, until TLA idle"),
        "new_2500": ("FwsSoundManager alt_2500", "RA <= 2530 ft, pin TwoThousandFiveHundred (default on)"),
        "new_2_500": ("FwsSoundManager alt_2500b", "pin TwentyFiveHundred"),
        "new_2000": ("FwsSoundManager alt_2000", "pin TwoThousand"),
        "new_1000": ("FwsSoundManager alt_1000", "RA <= 1020 ft (default on)"),
        "new_500": ("FwsSoundManager alt_500", "RA <= 513 ft"),
        "new_400": ("FwsSoundManager alt_400", "RA <= 410 ft (default on)"),
        "new_300": ("FwsSoundManager alt_300", "RA <= 310 ft"),
        "new_200": ("FwsSoundManager alt_200", "RA <= 210 ft"),
        "new_100": ("FwsSoundManager alt_100", "RA <= 110 ft"),
        "aural_pullup_new": ("LegacySoundManager pull_up (periodic 1.1 s)", "GPWS mode 1/2"),
        "aural_dontsink_new": ("LegacySoundManager dont_sink (periodic 1.1 s)", "GPWS mode 3"),
        "aural_too_low_gear": ("LegacySoundManager too_low_gear", "GPWS mode 4"),
        "aural_too_low_flaps": ("LegacySoundManager too_low_flaps", "GPWS mode 4"),
        "aural_too_low_terrain": ("LegacySoundManager too_low_terrain", "GPWS mode 4"),
        "GPWS_test_short": ("AvionicSounds", "GPWS test"),
        "GPWS_test_long": ("AvionicSounds", "GPWS test (long)"),
        "TCAS_sys_test_OK": ("AvionicSounds", "TCAS test"),
    }
    if n in fws:
        caller, cond = fws[n]
        return {"type": "fws_queue", "expr": cond, "fbw": caller}
    if n.startswith("new_") and n[4:].isdigit() and int(n[4:]) <= 50:
        return {"type": "not_posted", "expr": "listed in TcasConstants.ts only; the A380X FWS posts <n>_380 instead",
                "fbw": "TcasConstants SOUNDS"}
    if n.endswith("_380") and n.split("_")[0].isdigit():
        ft = int(n.split("_")[0])
        thresholds = {5: 6, 10: 12, 20: 22, 30: 32, 40: 42, 50: 53, 60: 63, 70: 73, 80: 83, 90: 93}
        return {"type": "fws_queue", "expr": f"RA <= {thresholds.get(ft, ft)} ft (LegacyGpws AltCallState)",
                "fbw": f"FwsSoundManager alt_{ft}"}
    if n in ("clear_of_conflict", "climb_climb", "climb_climb_now", "climb_crossing_climb", "descend_crossing_descend",
             "descend_descend", "descend_descend_now", "increase_climb", "increase_descent", "level_off_level_off",
             "maint_vs_crossing_maint", "maint_vs_maint", "monitor_vs", "traffic_traffic"):
        return {"type": "fws_queue", "expr": "TCAS advisory (LegacyTcasComputer -> LegacySoundManager)",
                "fbw": "LegacyTcasComputer.tryPlaySound", "port_state": "TCAS RA logic not modelled; stock tcas datarefs as interim"}
    # cockpit control foley posted by behaviours (AnimationSounds)
    return {"type": "control_event", "expr": "cockpit control interaction (see controls list)",
            "fbw": "AnimationSounds WWISE_EVENT_1 (press/on) / WWISE_EVENT_2 (release/off)"}


# --------------------------------------------------------------------------- curve maths for doc checks
def _shape(t: float, interp: str) -> float:
    t = min(1.0, max(0.0, t))
    return {
        "Linear": t, "Log3": 1 - (1 - t) ** 3, "Exp3": t ** 3, "Log1": 1 - (1 - t) ** 1.41, "Exp1": t ** 1.41,
        "Sine": math.sin(t * math.pi / 2), "SineRecip": 1 - math.cos(t * math.pi / 2),
        "SCurve": 0.5 - 0.5 * math.cos(t * math.pi), "InvSCurve": math.acos(1 - 2 * t) / math.pi,
        "Constant": 0.0,
    }.get(interp, t)


def evaluate(points: list[dict], x: float) -> float:
    if x <= points[0]["x"]:
        return points[0]["y"]
    for a, b in zip(points, points[1:]):
        if x <= b["x"]:
            span = b["x"] - a["x"]
            return a["y"] + (b["y"] - a["y"]) * _shape((x - a["x"]) / span if span else 1.0, a["interp"])
    return points[-1]["y"]


# --------------------------------------------------------------------------- build
def view_gate(leaf: dict) -> tuple[bool, bool, dict]:
    inside = outside = True
    offsets = {}
    for group in leaf.get("states", []):
        if group.get("group") != "VIEWPOINT":
            continue
        for state in group["states"]:
            volume = state["props"].get("Volume", 0.0)
            if state["state"] == "INSIDE" and volume <= -96:
                inside = False
            elif state["state"] == "OUTSIDE" and volume <= -96:
                outside = False
            elif state["props"]:
                offsets[state["state"].lower()] = state["props"]
    return inside, outside, offsets


def emitter_for(node: str | None, emitters: dict, engine: int | None) -> dict | None:
    if engine is not None:
        row = emitters["engines"][engine]["recommended_emitter"]
        return {"node": f"SOUND_ENGINE_{engine + 1}", "xyz_ref_m": [row["x"], row["y"], row["z"]],
                "basis": row.get("basis")}
    if not node:
        return None
    item = emitters["sound_xml_nodes"].get(node)
    if not item:
        return {"node": node, "xyz_ref_m": None, "basis": "not found"}
    if item.get("found"):
        return {"node": node, "xyz_ref_m": [item["x"], item["y"], item["z"]], "basis": "FBW glTF node"}
    sub = item.get("substitute") or {}
    return {"node": node, "xyz_ref_m": [sub.get("x"), sub.get("y"), sub.get("z")],
            "basis": f"heuristic substitute ({sub.get('target')}): node absent from A380 glTF"}


def build(stage: Path) -> dict:
    manifest = json.loads((stage / "manifest.json").read_text())
    emitters = json.loads((stage / "emitters.json").read_text())
    cg = emitters["conversion"]["acf_default_cg_m"]
    controls_by_event: dict[str, list[dict]] = defaultdict(list)
    for control in emitters["controls"]:
        for key in ("events_on", "events_off"):
            event = control.get(key)
            if event and not str(event).startswith("asobo_default"):
                controls_by_event[str(event).lower()].append({
                    "node": control["node"], "edge": "on/press" if key == "events_on" else "off/release",
                    "command": control.get("command") or control.get("command_push") or control.get("command_up"),
                    "dataref": control.get("dataref"), "xyz_ref_m": [control["x"], control["y"], control["z"]]})
    media = {m["media_id"]: m for m in manifest["media"]}
    rows, seen = [], set()
    for event in manifest["events"]:
        if not event["used_by_a380x"] or event["event_id"] in seen:
            continue
        seen.add(event["event_id"])
        name = event["name"]
        engine = eng(name)
        refs = event["sound_xml"]
        node = next((r["attributes"].get("NodeName") for r in refs if r["attributes"].get("NodeName")), None)
        viewpoints = sorted({r["attributes"].get("ViewPoint", "Both") for r in refs}) or ["Both"]
        continuous = sorted({r["attributes"].get("Continuous", "default") for r in refs})
        fade = next((r["attributes"].get("FadeOutTime") for r in refs if r["attributes"].get("FadeOutTime")), None)
        bindings = sorted({f"{(b.get('LocalVar') or b.get('SimVar'))}{':' + b['Index'] if b.get('Index') else ''}"
                           f"{' [' + b['Units'] + ']' if b.get('Units') else ''}{' derived' if b.get('Derived') == 'true' else ''}"
                           f" -> {b.get('RTPCName')}" for r in refs for b in r["rtpcs"]})
        bound = {b.get("RTPCName") for r in refs for b in r["rtpcs"]}
        layers = []
        for leaf in event["leaves"]:
            if "sound_id" not in leaf:
                continue
            inside, outside, offsets = view_gate(leaf)
            m = media.get(leaf["media_id"], {})
            curves = []
            for curve in leaf["rtpcs"]:
                rtpc = curve["rtpc"] or str(curve["rtpc_id"])
                src = RTPC_SOURCES.get(rtpc, ("unmapped", ""))
                curves.append({"rtpc": rtpc, "param": curve["param"], "scaling": curve["scaling"],
                               "bound_in_fbw_xml": rtpc in bound or rtpc.startswith(("LOCALVAR_FWC",)),
                               "xplane_source": src[0], "note": src[1],
                               "points": [[p["x"], p["y"], p["interp"]] for p in curve["points"]]})
            layers.append({
                "media_id": leaf["media_id"], "wav": m.get("wav"), "duration_s": m.get("duration_s"),
                "sample_rate": m.get("sample_rate"), "channels": m.get("channels"),
                "loop": bool(leaf["looping"]), "loop_frames": [m["loop"]["start_frame"], m["loop"]["end_frame"]]
                if m.get("loop") else None,
                "container_path": [{k: v for k, v in step.items() if k in ("type", "mode", "continuous", "weight",
                                                                          "crossfade_rtpc", "crossfade_curve")}
                                   for step in leaf["path"]],
                "static_props": leaf["props"], "random_ranges": leaf["ranged_props"],
                "audible_inside": inside, "audible_outside": outside, "viewpoint_offsets": offsets,
                "bus": INTERIOR if not outside else EXTERIOR,
                "attenuation_max_m": (leaf.get("attenuation") or {}).get("max_distance_m"),
                "attenuation_volume_dry": [[p["x"], p["y"], p["interp"]] for p in
                                           ((leaf.get("attenuation") or {}).get("volume_dry") or {"points": []})["points"]],
                "effects_not_ported": sorted({fx["plugin"] or "?" for fx in leaf.get("effects", [])}),
                "rtpc_curves": curves,
            })
        emitter = emitter_for(node, emitters, engine)
        if emitter and emitter.get("xyz_ref_m") and None not in emitter["xyz_ref_m"]:
            x, y, z = emitter["xyz_ref_m"]
            emitter["xyz_cg_m"] = [round(x - cg[0], 4), round(y - cg[1], 4), round(z - cg[2], 4)]
        trig = trigger_for(name)
        controls = controls_by_event.get(name.lower(), [])
        sources = sorted({s for s in SOURCES if s.split("[")[0] in trig["expr"]})
        status = "ready"
        if trig.get("port_state") or any(SOURCES[s][0] == "new" for s in sources) or trig["type"] in ("fws_queue", "fws_or_cids"):
            status = "needs_port_state"
        if trig["type"] == "control_event" and controls:
            status = "ready"
        if trig["type"] == "control_event" and not controls:
            status = "needs_port_state"
        if trig["type"] == "not_posted":
            status = "not_posted_by_a380x"
        rows.append({
            "event": name, "event_id": event["event_id"], "bank": event["bank"], "category": event["category"],
            "subcategory": event["subcategory"], "a380_specific": event["a380_specific"],
            "shared_with_a32nx": event["shared_with_a32nx"], "priority": PRIORITY.get(event["category"], 40),
            "fbw_xml": {"groups": sorted({r["group"] for r in refs}), "viewpoint": viewpoints, "continuous": continuous,
                        "fade_out_s": fade, "node": node, "rtpc_bindings": bindings,
                        "triggers": sorted({f"{r['attributes'].get('LocalVar') or r['attributes'].get('SimVar') or '(posted)'}"
                                            f" {json.dumps(r.get('range', []))}" for r in refs})},
            "xplane_trigger": trig, "xplane_sources": {s: SOURCES[s] for s in sources},
            "emitter": emitter, "controls": controls, "layers": layers, "status": status,
            "playback": ("loop" if any(layer["loop"] for layer in layers) else "one_shot")
            + ("+rtpc" if any(layer["rtpc_curves"] for layer in layers) else ""),
        })
    rows.sort(key=lambda r: (r["category"], r["subcategory"], r["event"].lower()))
    budget = defaultdict(lambda: [0, set()])
    for row in rows:
        for layer in row["layers"]:
            m = media.get(layer["media_id"], {})
            if layer["media_id"] not in budget[row["category"]][1]:
                budget[row["category"]][1].add(layer["media_id"])
                budget[row["category"]][0] += (m.get("wav_bytes") or 0) - 44
    unavailable = manifest["sound_xml_events_not_in_fbw_banks"]
    return {
        "schema_version": 1, "generator": "tools/sound_port_map.py",
        "coordinates": "xyz_ref_m: X-Plane aircraft metres from the ACF reference point (+x right, +y up, +z aft); "
                       "xyz_cg_m: minus the ACF default CG " + json.dumps(cg),
        "sources": {k: {"kind": v[0], "ref": v[1], "units": v[2]} for k, v in SOURCES.items()},
        "rtpc_sources": {k: {"source": v[0], "note": v[1]} for k, v in RTPC_SOURCES.items()},
        "curve_semantics": {
            "Volume dB-scaled": "points store linear_gain - 1 (-1 = silence, 0 = unity); evaluate, then gain = 1 + y",
            "Pitch": "cents", "LPF/HPF": "Wwise 0..100 filter units (not portable through XPLMSound)",
            "interpolation": "segment shape of the left point: Linear, Log3=1-(1-t)^3, Exp3=t^3, Log1=1-(1-t)^1.41, "
                             "Exp1=t^1.41, Sine=sin(pi t/2), SineRecip=1-cos(pi t/2), SCurve, InvSCurve, Constant=hold",
        },
        "events": rows,
        "memory_budget_bytes_by_category": {k: v[0] for k, v in sorted(budget.items())},
        "fbw_xml_events_without_fbw_audio": unavailable,
    }


def markdown(port: dict) -> str:
    lines = ["<!-- BEGIN GENERATED: tools/sound_port_map.py -->", "",
             "### A.1 Event table (generated)", "",
             "| Category | FBW event | Layers (loop) | Trigger in X-Plane | View / bus | Emitter (ref m) | Status |",
             "|---|---|---|---|---|---|---|"]
    for row in port["events"]:
        layers = row["layers"]
        loops = sum(1 for layer in layers if layer["loop"])
        inside = any(layer["audible_inside"] for layer in layers)
        outside = any(layer["audible_outside"] for layer in layers)
        view = "in+out" if inside and outside else ("in" if inside else "out")
        buses = "/".join(sorted({layer["bus"].replace("xplm_Audio", "") for layer in layers}))
        emitter = row["emitter"]
        pos = "-"
        if emitter and emitter.get("xyz_ref_m") and None not in emitter["xyz_ref_m"]:
            pos = f"{emitter['node']} ({emitter['xyz_ref_m'][0]:.2f}, {emitter['xyz_ref_m'][1]:.2f}, {emitter['xyz_ref_m'][2]:.2f})"
        elif row["controls"]:
            pos = f"{len(row['controls'])} control nodes"
        expr = row["xplane_trigger"]["expr"].replace("|", "\\|")
        lines.append(f"| {row['category']}/{row['subcategory']} | `{row['event']}` | {len(layers)} ({loops}) | "
                     f"{row['xplane_trigger']['type']}: {expr} | {view} / {buses} | {pos} | {row['status']} |")
    lines += ["", "### A.2 Memory if every A380X layer were resident (PCM16)", "", "| Category | MiB |", "|---|---:|"]
    total = 0
    for key, value in port["memory_budget_bytes_by_category"].items():
        total += value
        lines.append(f"| {key} | {value / 1048576:.1f} |")
    lines += [f"| **total** | **{total / 1048576:.1f}** |", "",
              "### A.3 Events named by the A380X XML that have no audio in the FBW packages", "",
              ", ".join(f"`{n}`" for n in port["fbw_xml_events_without_fbw_audio"]), "",
              "<!-- END GENERATED -->"]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--stage", type=Path, default=Path("build/fbw-sounds-stage"))
    parser.add_argument("--output", type=Path, default=Path("fmod/a380x-sound-port-map.json"))
    parser.add_argument("--doc", type=Path, help="replace the GENERATED block of this Markdown file")
    args = parser.parse_args()
    port = build(args.stage)
    args.output.write_text(json.dumps(port, indent=1) + "\n", encoding="utf-8")
    if args.doc:
        text = args.doc.read_text(encoding="utf-8")
        begin, end = "<!-- BEGIN GENERATED: tools/sound_port_map.py -->", "<!-- END GENERATED -->"
        if begin not in text or end not in text:
            raise SystemExit(f"{args.doc} has no GENERATED markers")
        head, rest = text.split(begin, 1)
        tail = rest.split(end, 1)[1]
        args.doc.write_text(head + markdown(port) + tail, encoding="utf-8")
    counts = defaultdict(int)
    for row in port["events"]:
        counts[row["status"]] += 1
    print(json.dumps({"events": len(port["events"]), "by_status": counts,
                      "memory_mib": round(sum(port["memory_budget_bytes_by_category"].values()) / 1048576, 1)}))


if __name__ == "__main__":
    main()
