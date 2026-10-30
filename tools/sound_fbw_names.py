#!/usr/bin/env python3
"""Name candidates and classification rules for the FBW A380X Wwise sound banks.

Wwise banks only store hashes.  ``sound_fbw_stage.py`` hashes every candidate
below (and every identifier found in the FBW repository) and keeps only exact
matches, recording where each recovered name came from.

``A32NX_SOUND_XML_EVENTS`` and ``A32NX_RTPC_NAMES`` are the ``WwiseEvent`` /
``RTPCName`` values of the FlyByWire A32NX ``sound.xml`` (GPL-3.0), read from
https://github.com/flybywiresim/aircraft/blob/master/fbw-a32nx/src/base/flybywire-aircraft-a320-neo/SimObjects/AirPlanes/FlyByWire_A320_NEO/sound/sound.xml
on 2026-10-21.  The A380X shares the ``FBW_A320_NEO_{1,2,3}`` packages with the
A32NX, so these names label the A32NX-only events found in the same banks.
"""
from __future__ import annotations

import re

A32NX_SOUND_XML_URL = (
    "https://github.com/flybywiresim/aircraft/blob/master/fbw-a32nx/src/base/"
    "flybywire-aircraft-a320-neo/SimObjects/AirPlanes/FlyByWire_A320_NEO/sound/sound.xml"
)

A32NX_SOUND_XML_EVENTS = """
rattle_kinematic YOKE_MOVEMENT wind_sound LEFT_WING_SCRAPE RIGHT_WING_SCRAPE FUSELAGE_SCRAPE
CENTER_TOUCHDOWN LEFT_TOUCHDOWN RIGHT_TOUCHDOWN AP_PREFLIGHT_CHECK_OVER flapsmovement slatsmovement
apuwhine apuwhine2 apubass apubass2 apurun apushut apustartinit apuflapopen apuflapopenfull
startcontactors startcontactorsdc apubassout apuwhineext apuwhineext2 apurunext apurearint
apustartinitext apuflapopenext apuflapopenfullext apuexhaust apushutdownext TRUnit transformers
svcbuswhine svcbusoff AVvent AVvent_Blower_OVRD AVvent_Extract_OVRD AVvent_Blower_OVRD_high
AVvent_Blower_extract_OVRD AVvent_OVRD_off AVvent_extract AVvent_extract_OVRD_extract
AVvent_extract_OVRD_off fanshut cabfanfront OSelfTest OExtractOVRDStart OBlowerExtractOVRDStart
OExtractOVRD OOVRDOff ISelfTest IExtractOVRD IOVRDOff IBlowerExtractOVRD cabfans rearcabfans brakefan
brakefan2 brakefanshut intbrakefan intbrakefanshut packscockpit cabinpack1 cabinpack2 cabinpackshigh
packscabinshut pack1ext pack1extshut pack2ext pack2extshut APUnlock APLock CIDSchimes cockpitdooropen
cockpitdoorclosed gearleverclick batteryrelay batteryrelay2 batteryrelayoff batteryrelayoff2 transfer
transfer_01 doorlocks BuzzLEXT BuzzREXT EngL_Front_int EngR_Front_Int EngL_Front_ext EngR_Front_ext
EngL_Rear_int EngR_Rear_Int EngL_Rear_ext EngR_Rear_ext LWhineG RWhineG CombLX CombRX ReverseL ReverseR
StartL StartR StartInitLEXT StartInitREXT SwooshLEXT SwooshREXT ShutAirLEXT ShutAirREXT ShutScreechLEXT
ShutScreechREXT Start2L Start2R StartWhineL StartWhineR ShutL ShutR LShutEXT RShutEXT BoomL BoomR
BoomLHot BoomRHot StartBassLB StartBassRB BoomLEXT BoomREXT FFlowL FFlowR StartLEXT StartREXT Start2LEXT
Start2REXT FFlowLEXT FFlowREXT idlehighLEXT idlehighREXT ReverseLEXT ReverseREXT Fuelpump Fuelpump2
Fuelpump3 Fuelpump4 Fuelpumpext Fuelpumpextcav1 Fuelpumpext2 Fuelpumpextcav2 Fuelpumpext3
Fuelpumpextcav3 Fuelpumpext4 Fuelpumpextcav4 yellowepump yellowepumpext blueepump blueepumpext
yellowshut blueshut epumpshutext GROLL whinerollint whinerollext noseroll groundrollnose
NWheelSpinAboveGround taxirattle panelrattle Centerthump1 Centerthump2 bumpslow taxirumble ptu1 ptu2
ptu3 ptu4 ptu_cont_mid ptu_cont_end ptu_cont_max ptustartcockpit ptu_sound smoothtouch medtouch
firmtouch cabtouchsmooth cabtouchmed cabtouchhard turbulence1 turbulence2 Betterwind cabinwind
Geardownwind Geardoorwind splrleftwind splrrightwind splrwindext splrwindext_retract LandLightsDragL
LandLightsDragR aileron_left aileron_right rudder elevator gdown gup gearcab gmain gearlock flapnotch
Spoiler0 Spoiler50 Spoiler100 SpoilerARM SpoilerDISARM lever_throttle_1 lever_throttle_2 detent
IdleDetent lever_trim_elevator pedal_movement rattle_ground rattle_overspeed wiper_slowL wiper_fastL
wiper_parkL wiper_slowR wiper_fastR wiper_parkR light_switch_seatbelt cockpit_cabin_call_fwd
cockpit_cabin_call_aft emercabincall mechhorn evachorncockpit cvr_test printerloop papertear
papercrumble paperflip LightRainL LightRainR ModerateRainL ModerateRainR HeavyRainL HeavyRainR
selcalbeep identbeep squelchloop squelchloop2 squelchloop3 squelchloop4 squelchloop5 CRC paxchatter
boarding deboarding boardingcompleted welcomeonboard armdoors safetydemo disarmdoors shutdownclicks
cruise descent cabin_crew_seats_takeoff cabin_crew_seats_landing cavcharge cchordloop aural_stall_new
brake_max_braking set_max_reverse keep_max_reverse runway_too_short autobrake_off improved_tone_caution
new_5 new_10 new_20 new_30 new_40 new_50 new_100 new_200 new_300 new_400 new_500 new_1000 new_2000
new_2500 new_2_500 new_retard aural_100above aural_minimumnew aural_speed aural_pitch aural_crickets
cchordshort 3click fwdgalley cpdlc_ring aural_bank_bank aural_stall_stall clear_of_conflict
climb_climb climb_climb_now climb_crossing_climb descend_crossing_descend descend_descend
descend_descend_now increase_climb increase_descent level_off_level_off maint_vs_crossing_maint
maint_vs_maint monitor_vs traffic traffic_traffic TCAS_sys_test_OK aural_terrain aural_pull_up
aural_pullup_new aural_overspeed aural_sink_rate aural_sink_rate_new aural_dont_sink aural_dontsink_new
GPWS_test_short GPWS_test_long aural_too_low_gear aural_too_low_flaps aural_too_low_terrain
aural_glideslope_quiet aural_glideslope_loud aural_terrain_ahead aural_obstacle_ahead aural_obstacle
aural_altimeter_setting button_switch_generic fmc_push_button_on fmc_push_button_off
parking_brake_switch_on parking_brake_switch_off safety_push_button_on safety_push_button_off
autobrake_push_button_on autobrake_push_button_off antiskid_switch_on antiskid_switch_off
yoke_push_button_on yoke_push_button_off rudder_trim_rest_push_button_on rudder_trim_rest_push_button_off
rudder_trim_knob wipers_switch flaps_lever_button_on flaps_lever_button_off engine_master_switch_on
engine_master_switch_off engine_mode_switch throttle_lever_lock_on throttle_lever_lock_off
fuel_pump_push_button_on fuel_pump_push_button_off alternator_switch_on alternator_switch_off
battery_switch_on battery_switch_off engine_bleed_switch_on engine_bleed_switch_off apu_bleed_switch_on
apu_bleed_switch_off master_apu_switch_on master_apu_switch_off starter_apu_switch_on
starter_apu_switch_off apu_generator_switch_on apu_generator_switch_off externalpower_push_button_on
externalpower_push_button_off light_switch_on light_switch_off strobe_light_switch_on
strobe_light_switch_off beacon_light_switch_on beacon_light_switch_off wing_light_switch_on
wing_light_switch_off landing_light_switch light_switch taxi_light_switch_on taxi_light_switch_off
nav_light_switch_on nav_light_switch_off double_light_switch cabin_light_switch lighting_knob_generic
pb1on pb1off mpb1on mpb1off lswitch engmaster parkbrk QNHknob flapcover pulloutknob rudtrim turnknob
clds RMPknob firepbguard firepb revlever mcdubuttons fcubutton CHRon CHRoff SETon SEToff SETknob RSTon
RSToff smallknob roundbutton firetestbutton autopilot_knob_push_button_on autopilot_knob_push_button_off
autopilot_push_button_on autopilot_push_button_off autopilot_push_button2_on autopilot_push_button2_off
autopilot_knob autopilot_selector_knob autopilot_speed_knob autopilot_heading_knob
autopilot_altitude_knob autopilot_verticalspeed_knob atc_transponder_push_button_on
atc_transponder_push_button_off atc_transponder_knob autopilot_baro_selector_button_on
autopilot_baro_selector_button_off audioreceiver_push_button_on audioreceiver_push_button_off
audioreceiver_knob_push_button_on audioreceiver_knob_push_button_off audioreceiver_knob
radio_push_button_on radio_push_button_off radio_switch_on radio_switch_off radio_lock_on radio_lock_off
atc_push_button_on atc_push_button_off elac_push_button_on elac_push_button_off transfer_push_button_on
transfer_push_button_off nav_aid_switch radar_sys_switch_on radar_sys_switch_off radar_mode_knob
radio_small_knob radio_big_knob deice_engine_switch_on deice_engine_switch_off deice_airframe_switch_on
deice_airframe_switch_off deice_windshield_switch_on deice_windshield_switch_off wipers_forward
wipers_backward
""".split()

A32NX_RTPC_NAMES = """
SIMVAR_ROTATION_VELOCITY_BODY_X SIMVAR_ROTATION_VELOCITY_BODY_Y SIMVAR_ROTATION_VELOCITY_BODY_Z
SIMVAR_YOKE_X_POSITION_DERIVED SIMVAR_YOKE_Y_POSITION_DERIVED SIMVAR_WHEEL_RPM_DERIVED
SIMVAR_VERTICAL_SPEED_DERIVED SIMVAR_LEADING_EDGE_FLAPS_LEFT_PERCENT SIMVAR_AIRSPEED_TRUE SIMVAR_TURB_ENG_N1
SIMVAR_APU_PCT_RPM SIMVAR_APU_PCT_RPM_DERIVED SIMVAR_EXIT_OPEN LOCALVAR_A32NX_EXTERIOR_VOLUME
LOCALVAR_A32NX_EXTRACT_OVRD LOCALVAR_A32NX_BLOWER_OVRD LOCALVAR_A32NX_EXTRACT_OVRD_01
SIMVAR_AMBIENT_TEMPERATURE LOCALVAR_A32NX_COCKPIT_DOOR_LOCKED SIMVAR_AIRSPEED_INDICATED
SIMVAR_ELECTRICAL_MAIN_BUS_VOLTAGE LOCALVAR_A32NX_DC_ESS_BUS SIMVAR_TURB_ENG_N2
LOCALVAR_A32NX_INTERIOR_ENG_VOLUME SIMVAR_PLANE_ALT_ABOVE_GROUND SIMVAR_AMBIENT_DENSITY
SIMVAR_TURB_ENG_N1_DERIVED LOCALVAR_A32NX_COLD_START LOCALVAR_A32NX_EPUMP_RPM LOCALVAR_A32NX_FLUID_LEVEL
SIMVAR_GROUND_VELOCITY SIMVAR_GEAR_ANIMATION_POSITION SIMVAR_GEAR_TOTAL_PCT_EXTENDED_DERIVED
SIMVAR_GEAR_TOTAL_PCT_EXTENDED LOCALVAR_A32NX_INTERIOR_WIND_VOLUME SIMVAR_SPOILERS_LEFT_POSITION
SIMVAR_SPOILERS_RIGHT_POSITION SIMVAR_AILERON_POSITION SIMVAR_RUDDER_POSITION SIMVAR_ELEVATOR_POSITION
SIMVAR_ELEVATOR_TRIM_POSITION_DERIVED SIMVAR_RUDDER_PEDAL_POSITION_DERIVED
LOCALVAR_GENERAL_ENG_THROTTLE1_LEVER_POSITION_DERIVED LOCALVAR_GENERAL_ENG_THROTTLE1_LEVER_POSITION
LOCALVAR_GENERAL_ENG_THROTTLE2_LEVER_POSITION_DERIVED LOCALVAR_GENERAL_ENG_THROTTLE2_LEVER_POSITION
SIMVAR_SURFACE_TYPE SIMVAR_GENERAL_ENG_COMBUSTION_SOUND_PERCENT LOCALVAR_A32NX_PASSENGER_AMB_TOGGLE
LOCALVAR_A32NX_BOARDING LOCALVAR_A32NX_BOARDING_MUSIC_TOGGLE LOCALVAR_A32NX_ANNOUNCEMENT_TOGGLE
LOCALVAR_ACP_VOLUME SIMVAR_AMBIENT_PRECIP_RATE LOCALVAR_FWC_VOLUME SIMVAR_TURB_ENG_REVERSE_NOZZLE_PERCENT
""".split()

# Generic names tried for switch groups, states, buses and MSFS built-in game parameters.
GENERIC_NAMES = """
Master_Audio_Bus Master_Secondary_Bus Master Interior Exterior Cockpit Cabin Inside Outside ViewPoint
VIEWPOINT CAMERA CAMERA_TYPE Camera_Type INSIDE OUTSIDE ON OFF On Off None none Default default
Engine Engines Wind Ground Avionics Aircraft AIRCRAFT Environment Weather Rain Surface SURFACE_TYPE
SURFACE_TYPE_SWITCH Concrete Asphalt Grass Dirt Water Snow Gravel Wet Dry Speed Distance DISTANCE
OCCLUSION Occlusion Obstruction OBSTRUCTION Insulation INSULATION INSULATION_RTPC
LOCALVAR_FWC_VOLUME LOCALVAR_SEAT_POS SIMVAR_APU_PCT_RPM SIMVAR_SIM_ON_GROUND SIMVAR_ON_ANY_RUNWAY
""".split()

# Ordered regex rules: (pattern on the lower-case name, category, subcategory).
CATEGORY_RULES: list[tuple[str, str, str]] = [
    (r"^engine_[1-4]_(front|rear|ff|start|front_ext)$", "engines", "trent972"),
    (r"^(ac1|ac2)$", "engines", "start_n2_threshold_cue"),
    (r"^eng[lr]_|^(lwhineg|rwhineg|comblx|combrx|reverse[lr]|start[lr]|startinit|swoosh|shutair|shutscreech"
     r"|start2|startwhine|shut[lr]$|[lr]shutext|boom|startbass|fflow|idlehigh|buzz[lr])", "engines", "cfm_leap_a32nx"),
    (r"^(aplock|apunlock)$", "cockpit_controls", "levers_and_controls"),
    (r"^apu|^startcontactors", "apu", "apu"),
    (r"^(crc|crc_380|cavcharge|cchord|3click|mastercaution|improved_tone_caution|aural_|gpws_test"
     r"|new_|[0-9]+_380$|v1$|brake_max_braking|set_max_reverse|keep_max_reverse|runway_too_short"
     r"|autobrake_off|clear_of_conflict|climb_|descend_|increase_|level_off|maint_vs|monitor_vs|traffic"
     r"|tcas_|ap_preflight)", "aural_warnings", "fws"),
    (r"^(cpdlc_ring|selcalbeep|identbeep|squelchloop)", "aural_warnings", "radio_comms"),
    (r"^(wind_sound|cockpitwind|380cabinwind|cabinwind|betterwind|380geardownwind|geardownwind|geardoorwind"
     r"|splr.*wind|turbulence|landlightsdrag|rain_sound|.*rain[lr]$)", "environment", "aero_weather"),
    (r"^(cvr_test|printerloop|paper)", "systems", "misc_equipment"),
    (r"(touch|thump|scrape|groll|roll|rattle|rumble|bumpslow|nwheelspin|whineroll)", "environment", "ground"),
    (r"^(epump|yellowepump|blueepump|yellowshut|blueshut|epumpshut|ptu)", "systems", "hydraulics"),
    (r"^fuelpump", "systems", "fuel_pumps"),
    (r"(pack|avvent|fanshut|cabfan|frontcabfan|rearcabfans|selftest|extractovrd|blowerextract|ovrdoff"
     r"|brakefan)", "systems", "air_conditioning_ventilation"),
    (r"^(tr380|trunit|aevc380|contactor|acbus|bat1|batteryrelay|transformers|svcbus|transfer$|transfer_01)",
     "systems", "electrical"),
    (r"^(380gup|380gmain|noselock380|nosedownlock380|gdown|gup|gearcab|gmain|gearlock)$", "systems", "landing_gear"),
    (r"^(flaps380|flaps380stop|slats380|slats380stop|flapsmovement|slatsmovement)$", "systems", "high_lift"),
    (r"^(aileron_left|aileron_right|rudder|elevator)$", "systems", "flight_control_surfaces"),
    (r"^(cockpitdoor|doorlocks|windowopen|windowclose)", "systems", "doors_windows"),
    (r"^(cockpit_cabin_call|emercabincall|mechhorn|evachorn|cidschimes|light_switch_seatbelt|fwdgalley"
     r"|cabin_crew_seats|paxchatter|boarding|deboarding|welcomeonboard|armdoors|safetydemo|disarmdoors"
     r"|cruise|descent|shutdownclicks)", "systems", "cabin"),
    (r"^(wiper_|wipers_)", "systems", "wipers"),
    (r"^(seatmotor|tablein|tableout)", "cockpit_controls", "seats_tables"),
    (r"^(flapnotch|spoiler|detent|idledetent|lever_|pedal_movement|yoke_movement|gearleverclick|aplock"
     r"|apunlock|revlever)", "cockpit_controls", "levers_and_controls"),
    (r".*", "cockpit_controls", "switches_buttons_knobs"),
]


def classify(name: str | None) -> tuple[str, str]:
    if not name:
        return "unnamed", "unnamed"
    lowered = name.lower()
    for pattern, category, sub in CATEGORY_RULES:
        if re.search(pattern, lowered):
            return category, sub
    return "unnamed", "unnamed"


# Meet-in-the-middle naming-convention search for events no declared name explains:
# fnv1(prefix_state + PREFIX + stem) is tabulated forward, the event ID is unwound by SUFFIX.
CONVENTION_PREFIXES = ["", "new_", "aural_", "a380", "380", "a380_", "380_", "alt_", "ra_", "fws_", "eng",
                       "apu", "new"]
CONVENTION_SUFFIXES = [
    "", "380", "_380", "_a380", "a380", "_new", "new", "_01", "_02", "_03", "01", "02", "ext", "int", "_ext",
    "_int", "_l", "_r", "l", "r", "_1", "_2", "_3", "_4", "1", "2", "3", "4", "5", "_loop", "loop", "_short",
    "_long", "short", "long", "_start", "_stop", "stop", "start", "_on", "_off", "on", "off", "_old", "old",
    "c", "ft", "_ft", "ftnew", "_ftnew", "s", "_cont", "cont", "_test", "test", "_sound", "sound", "ext_01",
    "_01ext", "2ext", "int2", "lext", "rext", "_cab", "cab", "cockpit", "_cockpit", "outside", "inside",
    "_out", "out", "_in", "in",
]
CONVENTION_NUMBER_RANGE = range(0, 10001)


def variants(name: str) -> set[str]:
    """Cheap naming-convention variants (FBW suffixes ``380``, ``_01``, ``ext``...)."""
    base = {name}
    out = set(base)
    for suffix in ("380", "_380", "ext", "_ext", "int", "_int", "2", "_01", "_02", "_03", "_1", "_2",
                   "stop", "shut", "380ext", "380int", "a380"):
        out.add(name + suffix)
    for prefix in ("a380", "380", "a380_"):
        out.add(prefix + name)
    return out
