"""Package and verify the aircraft-local FBW-derived avionics runtime."""
from __future__ import annotations

import hashlib
import json
import os
import tempfile
from pathlib import Path
import shutil


PROVIDER_ID = "a380x-fbw-avionics-v1"
SOURCE_REVISION = "2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2"
MANIFEST = Path("avionics-providers/fbw-avionics-v1.json")
FILES = (
    ("native/avionics/dist/mac.xpl", "plugins/A380XAvionics/mac_x64/A380XAvionics.xpl"),
    ("native/avionics/dist/win.xpl", "plugins/A380XAvionics/win_x64/A380XAvionics.xpl"),
    ("native/avionics/sounds/priority_left.pcm", "plugins/A380XAvionics/sounds/priority_left.pcm"),
    ("native/avionics/sounds/priority_right.pcm", "plugins/A380XAvionics/sounds/priority_right.pcm"),
    ("native/avionics/dist/build-report.json", "plugins/A380XAvionics/build-report.json"),
)
DISPLAY_TARGETS = (
    "captain_pfd", "first_officer_pfd", "captain_nd", "first_officer_nd",
    "ewd", "captain_efb", "first_officer_efb", "fcu", "captain_efis", "first_officer_efis",
    "captain_mfd", "first_officer_mfd",
)
EFB_COMMANDS = (
    *(f"a380x/xp12/efb/page/{page}" for page in (
        "dashboard", "dispatch", "ground", "performance", "navigation",
        "atc", "failures", "checklists", "presets", "settings",
    )),
    *(f"a380x/xp12/efb/action_{index}" for index in range(1, 7)),
    "a380x/xp12/efb/power", "a380x/xp12/efb/toggle", "a380x/xp12/efb/next_page",
    "a380x/xp12/efb/window_toggle", "a380x/xp12/efb/window_open",
    "a380x/xp12/efb/window_close",
)
FCU_COMMANDS = tuple("a380x/xp12/fcu/" + name for name in (
    *(f"{selector}_{action}" for selector in ("speed", "heading", "altitude", "vertical_speed")
      for action in ("up", "down", "push", "pull")),
    "altitude_step_toggle", "ap1_toggle", "ap2_toggle", "fd_toggle", "athr_toggle",
    "speed_mach_toggle", "trk_fpa_toggle", "altitude_hold", "loc_toggle", "approach_toggle",
    "ap_disconnect", "athr_disconnect", "true_mag_toggle", "metric_toggle",
    "ls_captain_toggle", "ls_first_officer_toggle",
))
FCU_DATAREFS = tuple("a380x/xp12/fcu/" + name for name in (
    "selected_speed", "selected_mach", "selected_heading", "selected_altitude",
    "selected_vertical_speed", "selected_fpa", "speed_managed", "heading_managed",
    "altitude_managed", "mach_mode", "trk_fpa", "true_heading", "metric_altitude",
    "ap1_active", "ap2_active", "fd_active", "athr_active", "loc_active", "approach_active",
    "altitude_step_ft", "servo_active", "ls_captain", "ls_first_officer",
    "athr_armed", "altitude_hold_active",
))
PFD_DATAREFS = tuple("a380x/xp12/pfd/" + name for name in (
    "on_ground", "flap_handle_index", "flap_config", "flap_valid", "slat_valid",
    "speedbrake_valid", "spoilers_armed", "gear_down", "flap_fault", "slat_fault",
    "stall_warning", "envelope_valid", "clean_performance_valid", "flap_ratio", "slat_ratio", "spoiler_ratio",
    "flap_target_ratio", "slat_target_ratio", "vmax_knots", "vfe_next_knots",
    "vls_knots", "alpha_prot_knots", "alpha_limit_knots", "stall_warning_knots",
    "f_speed_knots", "s_speed_knots", "green_dot_knots", "v1_knots", "vr_knots", "v2_knots",
))
EFIS_COMMANDS = tuple(
    f"a380x/xp12/efis/{side}/{action}"
    for side in ("captain", "first_officer") for action in (
        "cstr_toggle", "wpt_toggle", "vord_toggle", "ndb_toggle", "arpt_toggle",
        "vor1_toggle", "vor2_toggle", "terr_toggle", "traf_toggle", "wx_toggle",
        "taxi_toggle", "ls_toggle", "vv_toggle", "mode_up", "mode_down",
        "range_up", "range_down", "baro_up", "baro_down", "baro_std", "baro_qnh",
        "baro_unit_toggle", "plan_next", "plan_previous",
    )
)
EFIS_DATAREFS = tuple(
    f"a380x/xp12/efis/{side}/{field}"
    for side in ("captain", "first_officer") for field in (
        "cstr", "wpt", "vord", "ndb", "arpt", "vor1", "vor2", "terr", "traf",
        "ls", "vv", "wx_requested", "taxi_requested", "mode", "range_index",
        "zoom", "baro_std", "baro_inhg", "powered", "range_nm", "qnh_hpa",
        "baro_preset_hpa",
    )
)
LIGHTING_CHANNELS = ("captain_flood", "integral", "lcd", "first_officer_flood")
LIGHTING_COMMANDS = tuple(
    f"a380x/xp12/lighting/{channel}_{direction}"
    for channel in LIGHTING_CHANNELS for direction in ("up", "down")
)
LIGHTING_DATAREFS = tuple(f"a380x/xp12/lighting/{channel}" for channel in LIGHTING_CHANNELS)
DU_CHANNELS = tuple(f"{side}_{unit}" for side in ("captain", "first_officer")
                    for unit in ("pfd", "nd", "wx_terr", "mfd"))
DU_COMMANDS = tuple(f"a380x/xp12/du/{channel}_{direction}"
                    for channel in DU_CHANNELS for direction in ("up", "down"))
DU_DATAREFS = tuple(f"a380x/xp12/du/{channel}" for channel in DU_CHANNELS)
GLARESHIELD_COMMANDS = tuple("a380x/xp12/glareshield/" + name for name in (
    "chrono_left", "chrono_right", "takeover_left", "takeover_right",
    "master_warning_left", "master_warning_right", "master_caution_left", "master_caution_right",
    "atc_ack_left", "atc_ack_right", "atc_receive_normal", "atc_receive_urgent",
    "volume_left_up", "volume_left_down", "volume_right_up", "volume_right_down",
    "ptt_left", "ptt_right",
))
GLARESHIELD_DATAREFS = tuple("a380x/xp12/glareshield/" + name for name in (
    "chrono_left_mode", "chrono_right_mode", "chrono_left_seconds", "chrono_right_seconds",
    "stick_left_moving", "stick_right_moving", "priority", "master_warning",
    "master_caution", "autoland_warning", "land_mode", "atc_pending",
    "volume_left", "volume_right", "ptt_left", "ptt_right",
    "master_warning_lit", "master_caution_lit", "autoland_lit", "atc_lit",
    "priority_arrow_left", "priority_arrow_right", "priority_green_left", "priority_green_right",
))
CLOCK_COMMANDS = tuple("a380x/xp12/clock/" + name for name in (
    "mode_up", "mode_down", "et_up", "et_down", "chr_toggle", "chr_reset",
    "date", "set_field_next", "set_up", "set_down",
))
CLOCK_DATAREFS = tuple("a380x/xp12/clock/" + name for name in (
    "mode", "et_selector", "date_held", "set_field", "year", "month", "day",
    "hour", "minute", "second", "et_seconds", "chr_seconds", "chr_running",
    "powered", "chr_held", "rst_held", "et_display", "chr_display",
))
CENTER_RIGHT_COMMANDS = tuple("a380x/xp12/center_right/" + name for name in (
    "gear_toggle", "autobrake_up", "autobrake_down", "rto_toggle", "antiskid_toggle", "accus_reinflate",
))
CENTER_RIGHT_DATAREFS = tuple("a380x/xp12/center_right/" + name for name in (
    "gear_lever", "gear_nose", "gear_wing", "gear_body", "gear_arrow",
    "gear_not_down_warning", "gear_not_locked_down_warning",
    "gear_not_locked_up_warning",
    "gear_nose_green", "gear_nose_red", "gear_wing_green", "gear_wing_red",
    "gear_body_green", "gear_body_red",
    "gear_wing_left_green", "gear_wing_left_red",
    "gear_wing_right_green", "gear_wing_right_red",
    "gear_body_left_green", "gear_body_left_red",
    "gear_body_right_green", "gear_body_right_red",
    "body_accu_psi", "body_accu_zone", "autobrake_selected",
    "autobrake_armed", "rto_arm", "antiskid_on",
    "rto_pressed", "btv_reverted", "normal_brakes_available", "autobrake_available",
    "antiskid_available", "emergency_braking", "autobrake_active",
    "brake_left_ratio_milli", "brake_right_ratio_milli",
    "reinflate_pressed", "reinflate_active",
))
ADIRS_COMMANDS = (
    *(f"a380x/xp12/adirs/{name}" for name in (
        "window_toggle", "align_now", "captain_ir_toggle", "captain_adr_toggle",
        "first_officer_ir_toggle", "first_officer_adr_toggle",
    )),
) + tuple(
    f"a380x/xp12/adirs/{action}"
    for unit in range(1, 4) for action in (
        f"ir{unit}_mode_up", f"ir{unit}_mode_down", f"ir{unit}_toggle", f"adr{unit}_toggle",
    )
)
ADIRS_DATAREFS = (
    *(f"a380x/xp12/adirs/ir{unit}_{field}" for unit in range(1, 4)
      for field in ("mode", "knob", "pressed", "off", "fault", "failure",
        "nav_valid", "attitude_valid", "heading_valid", "nav_locked", "aligning",
        "alignment_remaining", "heading_required", "heading_reminder", "heading_age",
        "heading_deg", "pitch_deg", "roll_deg", "vertical_speed_fpm", "track_deg",
        "ground_speed_knots", "latitude_deg", "longitude_deg", "flight_path_deg")),
    *(f"a380x/xp12/adirs/adr{unit}_{field}" for unit in range(1, 4)
      for field in ("pressed", "off", "fault", "failure", "valid", "ias_knots",
        "altitude_ft", "mach", "aoa_deg", "sat_c", "tat_c", "overspeed")),
    "a380x/xp12/adirs/on_batt",
    *(f"a380x/xp12/adirs/{side}_{kind}_source" for side in ("captain", "first_officer")
      for kind in ("ir", "adr")),
)
COCKPIT_DOOR_COMMANDS = (
    "a380x/xp12/cockpit_door/unlock", "a380x/xp12/cockpit_door/lock",
)
COCKPIT_DOOR_DATAREFS = (
    "a380x/xp12/cockpit_door/position", "a380x/xp12/cockpit_door/locked",
)
# MFD + KCCU (docs/mfd-spec/09-port-integration.md section 1.4, spec 01 section 12.5,
# spec 08 sections 10.1 and 10.5, spec 07 section 10.3).
KCCU_SIDES = ("captain", "first_officer")
KCCU_KEYS = (*"abcdefghijklmnopqrstuvwxyz", *"0123456789", "dot", "plusminus", "ent", "backspace",
             "sp", "slash", "esc", "esc2", "up", "down", "left", "right", "dir", "perf", "init",
             "navaid", "mailbox", "fpln", "dest", "secindex", "surv", "atccom", "nd", "kbd",
             "clrinfo", "rewind", "forward")
KCCU_ACTIONS = ("wheel_up", "wheel_down", "click", "kbd_switch_toggle", "ccd_switch_toggle")
KCCU_COMMANDS = tuple(f"a380x/xp12/kccu/{side}/{key}"
                      for side in KCCU_SIDES for key in (*KCCU_KEYS, *KCCU_ACTIONS))
KCCU_DATAREFS = tuple(f"a380x/xp12/kccu/{side}/{name}"
                      for side in KCCU_SIDES for name in ("kbd_on", "ccd_on"))
MFD_COMMANDS = tuple(f"a380x/xp12/mfd/{side}/{name}"
                     for side in KCCU_SIDES for name in ("popup", "popout"))
MFD_DATAREFS = (*(f"a380x/xp12/mfd/{side}/{name}" for side in KCCU_SIDES
                  for name in ("active_page", "keyboard_captured")),
                "a380x/xp12/mfd/pc_keyboard_capture")
FMS_DATAREFS = tuple("a380x/xp12/fms/" + name for name in (
    "flight_phase", "v1_knots", "vr_knots", "v2_knots", "to_flex_temp_c", "to_power_setting",
    "to_flaps", "to_ths_for_pct", "to_speeds_not_inserted", "vspeeds_disagree", "to_speeds_too_low",
    "thr_red_alt_ft", "acc_alt_ft", "eo_acc_alt_ft",
    "thr_red_alt_valid", "acc_alt_valid", "eo_acc_alt_valid",
    "trans_alt_ft", "trans_level_fl", "trans_alt_valid", "trans_level_valid",
    "cruise_altitude_ft", "fg_altitude_constraint_ft",
    "managed_speed_knots", "managed_speed_is_mach", "managed_mach", "managed_speed_valid",
    "presel_speed_knots", "presel_mach",
    "green_dot_knots", "f_speed_knots", "s_speed_knots", "vls_knots",
    "approach_vapp_knots", "approach_vls_knots", "approach_vref_knots",
    "approach_f_knots", "approach_s_knots", "approach_gd_knots",
    "landing_conf3", "approach_headwind_knots", "approach_headwind_valid",
    "dh_ft", "mda_ft", "minimums_in_range",
    "landing_elevation_ft", "dest_lat_deg", "dest_lon_deg", "landing_elevation_valid", "dest_pos_valid",
    "gross_weight_kg", "zfw_kg", "zfw_cg_pct",
    "dest_efob_below_min", "msg_check_speed_mode", "msg_set_hold_speed", "msg_td_reached",
    "nd_fm_message_flags_l", "nd_fm_message_flags_r",
    "fmc_a_healthy", "fmc_b_healthy", "fmc_c_healthy", "fms_l_failed", "fms_r_failed",
    "switching_knob", "fmc_a_failed", "fmc_b_failed", "fmc_c_failed",
    "fmc_a_reset", "fmc_b_reset", "fmc_c_reset",
))
SURV_DATAREFS = tuple("a380x/xp12/surv/" + name for name in (
    "xpdr_auto", "alt_rptg_request", "tcas_alert_level", "tcas_alt_select", "terr_sys_off",
    "gpws_sys_off", "gs_mode_off", "flap_mode_off", "wxr_taws_sys", "xpdr_tcas_sys",
))  # spec 06 section 4.2, shared by both MFDs, writable
MFD_DEVICES = (("a380x.xp12.mfd.captain", "A380X MFD CAPT"),
               ("a380x.xp12.mfd.first_officer", "A380X MFD F/O"))
AVIONICS_COMMANDS = (*EFB_COMMANDS, *FCU_COMMANDS, *EFIS_COMMANDS, *LIGHTING_COMMANDS, *DU_COMMANDS,
                     *GLARESHIELD_COMMANDS, *CLOCK_COMMANDS, *CENTER_RIGHT_COMMANDS,
                     *COCKPIT_DOOR_COMMANDS, *ADIRS_COMMANDS, *KCCU_COMMANDS, *MFD_COMMANDS)
AVIONICS_DATAREFS = (*FCU_DATAREFS, *PFD_DATAREFS, *EFIS_DATAREFS, *LIGHTING_DATAREFS, *DU_DATAREFS,
                     *GLARESHIELD_DATAREFS, *CLOCK_DATAREFS, *CENTER_RIGHT_DATAREFS,
                     *COCKPIT_DOOR_DATAREFS, *ADIRS_DATAREFS, *KCCU_DATAREFS, *MFD_DATAREFS,
                     *FMS_DATAREFS, *SURV_DATAREFS)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package_avionics_provider(root: Path, project: Path) -> dict:
    sources = [project / source for source, _ in FILES]
    if not all(path.is_file() for path in sources):
        raise ValueError("Missing built A380X avionics plugin or build report")
    build_report = json.loads(sources[-1].read_text(encoding="utf-8"))
    if (build_report.get("format") != 1
            or build_report.get("source_revision") != SOURCE_REVISION
            or build_report.get("stock_xplane_efis_modes_consumed") is not True
            or tuple(build_report.get("display_targets", ())) != DISPLAY_TARGETS
            or build_report.get("mock_test_result") != "a380x avionics + flyPad core tests passed"):
        raise ValueError("Avionics build report does not prove the reviewed FBW runtime contract")
    rows = []
    for source, (_, packaged) in zip(sources, FILES):
        target = root / packaged
        target.parent.mkdir(parents=True, exist_ok=True)
        # A loaded dylib may still map the old inode. Never overwrite it in place.
        fd, staged = tempfile.mkstemp(prefix=target.name + ".", dir=target.parent)
        os.close(fd)
        try:
            shutil.copy2(source, staged)
            os.replace(staged, target)
        finally:
            if os.path.exists(staged):
                os.unlink(staged)
        rows.append({"path": packaged, "sha256": digest(target)})
    document = {
        "format": 1,
        "provider": PROVIDER_ID,
        "source_revision": SOURCE_REVISION,
        "display_targets": list(DISPLAY_TARGETS),
        "draw_target": "2048x2048 aircraft cockpit panel texture + two 768x1024 plugin cockpit devices",
        "plugin_admin_name": "A380X FBW Avionics + flyPad",
        "physical_touch_targets": 34,
        "kccu_targets": len(KCCU_COMMANDS),
        "cockpit_devices": [device for device, _ in MFD_DEVICES],
        "commands": list(AVIONICS_COMMANDS),
        "datarefs": list(AVIONICS_DATAREFS),
        "stock_xplane_efis_modes_consumed": True,
        "logic_sources": build_report["logic_sources"],
        "files": rows,
        "runtime_tested": False,
    }
    target = root / MANIFEST
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    return document


def validate_avionics_provider(root: Path) -> tuple[dict | None, list[str]]:
    manifest = root / MANIFEST
    if not manifest.is_file():
        return None, ["Missing aircraft-local FBW avionics provider manifest"]
    try:
        document = json.loads(manifest.read_text(encoding="utf-8"))
        if document.get("format") != 1 or document.get("provider") != PROVIDER_ID:
            raise ValueError("unsupported provider identity")
        if document.get("source_revision") != SOURCE_REVISION:
            raise ValueError("unexpected FlyByWire source revision")
        if tuple(document.get("display_targets", ())) != DISPLAY_TARGETS:
            raise ValueError("provider does not own the PFD, ND, flyPad and FCU displays")
        if tuple(document.get("commands", ())) != AVIONICS_COMMANDS:
            raise ValueError("provider does not own the physical flyPad and FCU command sets")
        if tuple(document.get("datarefs", ())) != AVIONICS_DATAREFS:
            raise ValueError("provider does not publish the FCU and lighting datarefs")
        if document.get("stock_xplane_efis_modes_consumed") is not True:
            raise ValueError("provider lacks physical EFIS selector inputs")
        if document.get("cockpit_devices") != [device for device, _ in MFD_DEVICES]:
            raise ValueError("provider does not own the two MFD cockpit devices")
        rows = document.get("files")
        if not isinstance(rows, list) or len(rows) != len(FILES):
            raise ValueError("expected Mac, Windows, voice, and build-report files")
        for row, (_, packaged) in zip(rows, FILES):
            path = root / packaged
            if (row.get("path") != packaged
                    or not path.resolve().is_relative_to(root.resolve())
                    or not path.is_file()
                    or digest(path) != row.get("sha256")):
                raise ValueError(f"missing, escaped, or changed provider file: {packaged}")
        build_report = json.loads((root / FILES[-1][1]).read_text(encoding="utf-8"))
        if (build_report.get("source_revision") != SOURCE_REVISION
                or build_report.get("stock_xplane_efis_modes_consumed") is not True
                or build_report.get("mock_test_result") != "a380x avionics + flyPad core tests passed"):
            raise ValueError("packaged native build evidence is inconsistent")
        return document, []
    except (OSError, ValueError, TypeError, AttributeError, json.JSONDecodeError) as error:
        return None, [f"Invalid aircraft-local FBW avionics provider: {error}"]
