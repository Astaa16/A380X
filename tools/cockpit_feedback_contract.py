"""Contract of the cockpit-feedback pass: FBW-authored controls that had no motion, click target or lamp.

One table drives both sides:
  * tools/cockpit_feedback.py wraps the visible FBW meshes in the panel OBJs with the authored clip,
    appends click targets and lamp quads to the cockpit controls OBJ, and lights the lamp faces that
    live in the emissive decal object;
  * native/avionics/a380x_cockpit_feedback.inc publishes the datarefs and commands
    (a380x_cockpit_feedback_table.h is generated from here: python3 tools/cockpit_feedback_contract.py --emit-c).

Sources (FlyByWire A380X, GPL-3.0): node and clip names from A380_COCKPIT_LOD00.gltf; LVAR meanings from
fbw-common/src/systems/shared/src/publishers/SwitchingPanelPublisher.ts (A32NX_ATT_HDG / AIR_DATA / FMS
_SWITCHING_KNOB, 0 CAPT / NORM / F/O), the MFD SURV page contract (a380x_mfd_page_surv.h) and the ECAM
control panel commands (a380x_sd_adapter.inc).

Kinds:
  press     momentary key: <dataref> travels in on command Begin and back on End (A380XPress). The
            commands already exist (KCCU, ECAM) or are created by the native module (SURV).
  selector  rotary selector with N detents: int <dataref> 0..N-1, commands <dataref>_left / _right.
  switch    toggle with N positions, 0 = bottom: int <dataref>, commands <dataref>_up / _down.
  follow    animation only, driven by an existing dataref (no click, no command).
"""
from __future__ import annotations

from dataclasses import dataclass, field

PREFIX = "a380x/xp12/"
GREEN_CELL = 16          # solid green FCU annunciator cell of cockpit_3d/fcu_labels*.png
OFF_WHITE_CELL = 48      # "OFF" white (ADIRS / ELEC cell, tools/ovhd_lamps_contract.py)
ON_WHITE_CELL = 0        # "ON" white


@dataclass(frozen=True)
class Lamp:
    face: str            # FBW node of the lamp face (a *_SEQ1 / *_SEQ2 child)
    dataref: str         # 0..1 light level, 0 = hidden
    cell: int | None     # fcu_labels atlas cell for a lit quad in the controls OBJ; None: the face is in
                         # the emissive decal object and is lit in place with its own LIT art


@dataclass(frozen=True)
class Control:
    id: str              # unique, used in OBJ markers
    kind: str            # press | selector | switch | follow
    node: str            # animated FBW node (its subtree moves)
    clip: str            # FBW animation name
    dataref: str         # animation driver (full name)
    commands: tuple[str, ...] = ()   # press: commands whose phases drive the key
    label: str = ""      # hover label
    positions: int = 2   # selector / switch detents
    default: int = 0
    lamps: tuple[Lamp, ...] = ()
    click: bool = True   # add a click target
    minimum: float = 0.0  # follow: dataref value at the first clip key
    maximum: float = 1.0  # follow: dataref value at the last clip key
    files: tuple[str, ...] = ()      # restrict the panel patch to these OBJ names (default: all)
    group: str = ""


def _ecam():
    # FBW node suffix -> (SD command, page index for the selection light or None, hover label)
    keys = (
        ("APU", "apu", 1, "APU"), ("BLEED", "bleed", 2, "BLEED"), ("COND", "cond", 3, "COND"),
        ("PRESS", "press", 4, "CAB PRESS"), ("DOOR", "door", 5, "DOOR / OXY"), ("ELAC", "elec_ac", 6, "ELEC AC"),
        ("ELDC", "elec_dc", 7, "ELEC DC"), ("FUEL", "fuel", 8, "FUEL"), ("WHEEL", "wheel", 9, "WHEEL"),
        ("HYD", "hyd", 10, "HYD"), ("FCTL", "fctl", 11, "F/CTL"), ("CB", "cb", 12, "C/B"),
        ("STS", "status", 14, "STATUS"), ("VIDEO", "video", 15, "VIDEO"),
        ("ALL", "all", None, "ALL - cycle the system pages while held"), ("MORE", "more", None, "MORE"),
        ("TOCONF", "to_config", None, "T.O CONFIG - test"), ("CLR_LH", "clear_left", None, "CLEAR"),
        ("CLR_RH", "clear_right", None, "CLEAR"), ("RCL", "recall", None, "RCL"),
        ("RCLLAST", "recall_last", None, "RCL LAST"), ("CL", "checklist", None, "C/L - normal checklist"),
        ("ABNPROC", "abnormal_procedures", None, "ABN PROC - abnormal procedures"),
        ("UP", "up", None, "C/L cursor up"), ("DOWN", "down", None, "C/L cursor down"),
        ("CHECK_LH", "check_left", None, "C/L check"), ("CHECK_RH", "check_right", None, "C/L check"),
        ("EMERCANC", "emergency_cancel", None, "EMER CANC"),
    )
    rows = []
    for suffix, command, page, label in keys:
        key = suffix.lower()
        lamps = ()
        if page is not None or suffix.startswith("CLR"):
            lamps = (Lamp(f"PUSH_ECAM_{suffix}_SEQ2", f"{PREFIX}ecam/buttons/{key}/selected", GREEN_CELL),)
        rows.append(Control(f"ecam_{key}", "press", f"PUSH_ECAM_{suffix}", f"PUSH_ECAM_{suffix}",
                            f"{PREFIX}ecam/buttons/{key}/press_ratio", (f"{PREFIX}ecam/sd/{command}",),
                            f"ECAM {label}", lamps=lamps, group="ecam"))
    return rows


ECAM_PAGE_LAMPS = {  # lamp dataref -> SD page index (a380x_sd.h) or -1 for the CLR keys
    f"{PREFIX}ecam/buttons/{key}/selected": page for key, page in (
        ("apu", 1), ("bleed", 2), ("cond", 3), ("press", 4), ("door", 5), ("elac", 6), ("eldc", 7),
        ("fuel", 8), ("wheel", 9), ("hyd", 10), ("fctl", 11), ("cb", 12), ("sts", 14), ("video", 15),
        ("clr_lh", -1), ("clr_rh", -1))
}


def _surv():
    p = PREFIX + "surv/"
    rows = (
        ("xpdr_tcas_sys1", "PUSH_SURV_XPDR_TCAS_SYS1", "XPDR / TCAS SYS 1", "PUSH_SURV_XPDR_TCAS_SYS1_SEQ1", None),
        ("xpdr_tcas_sys2", "PUSH_SURV_XPDR_TCAS_SYS2", "XPDR / TCAS SYS 2", "PUSH_SURV_XPDR_TCAS_SYS2_SEQ1", None),
        ("wxr_taws_sys1", "PUSH_SURV_WXR_TAWS_SYS1", "WXR / TAWS SYS 1 - select / off", "PUSH_SURV_WXR_TAWS_SYS1_SEQ1", None),
        ("wxr_taws_sys2", "PUSH_SURV_WXR_TAWS_SYS2", "WXR / TAWS SYS 2 - select / off", "PUSH_SURV_WXR_TAWS_SYS2_SEQ1", None),
        ("tcas_abv", "PUSH_SURV_TCAS_ABV", "TCAS ABV - above / norm", "PUSH_SURV_TCAS_ABV_SEQ1", None),
        ("tcas_blw", "PUSH_SURV_TCAS_BLW", "TCAS BLW - below / norm", "PUSH_SURV_TCAS_BLW_SEQ1", None),
        ("tcas_ta_only", "PUSH_SURV_TCAS_TAONLY", "TCAS TA ONLY - TA / TA-RA", "PUSH_SURV_TCAS_TAONLY_SEQ1", ON_WHITE_CELL),
        ("gs_mode", "PUSH_SURV_GS_MODE", "G/S MODE - glideslope alert off / on", "PUSH_SURV_GS_MODE_SEQ1", OFF_WHITE_CELL),
    )
    return [Control(f"surv_{key}", "press", node, node, f"{p}{key}_pressed", (f"{p}{key}",), label,
                    lamps=(Lamp(face, f"{p}lamp/{key}", cell),), group="surv")
            for key, node, label, face, cell in rows]


SURV_BUTTONS = ("xpdr_tcas_sys1", "xpdr_tcas_sys2", "wxr_taws_sys1", "wxr_taws_sys2",
                "tcas_abv", "tcas_blw", "tcas_ta_only", "gs_mode")


def _switching():
    p = PREFIX + "switching/"
    return [
        Control("switching_att_hdg", "selector", "KNOB_SWITCHING_1", "KNOB_SWITCHING_1", p + "att_hdg",
                label="SWITCHING ATT HDG - CAPT ON 3 / NORM / F/O ON 3", positions=3, default=1, group="switching"),
        Control("switching_air_data", "selector", "KNOB_SWITCHING_2", "KNOB_SWITCHING_2", p + "air_data",
                label="SWITCHING AIR DATA - CAPT ON 3 / NORM / F/O ON 3", positions=3, default=1, group="switching"),
        Control("switching_fms", "selector", "KNOB_SWITCHING_3", "KNOB_SWITCHING_3", p + "fms",
                label="SWITCHING FMS - BOTH ON 2 / NORM / BOTH ON 1", positions=3, default=1, group="switching"),
    ]


KCCU_NODE_KEYS = (*"ABCDEFGHIJKLMNOPQRSTUVWXYZ", *"0123456789", "DOT", "PLUSMINUS", "ENT", "BACKSPACE", "SP",
                  "SLASH", "ESC", "ESC2", "UP", "DOWN", "LEFT", "RIGHT", "DIR", "PERF", "INIT", "NAVAID",
                  "MAILBOX", "FPLN", "DEST", "SECINDEX", "SURV", "ATCCOM", "ND", "KBD", "CLRINFO", "REWIND",
                  "FORWARD")


def _kccu():
    rows = []
    for letter, side in (("L", "captain"), ("R", "first_officer")):
        for key in KCCU_NODE_KEYS:
            name = key.lower()
            rows.append(Control(f"kccu_{side}_{name}", "press", f"PUSH_KCCU{letter}_{key}",
                                f"PUSH_KCCU{letter}_{key}_ANIM", f"{PREFIX}kccu/{side}/{name}_pressed",
                                (f"{PREFIX}kccu/{side}/{name}",), click=False, group="kccu"))
    return rows


def _eng_master():
    # The lever bodies already follow mixture_ratio (generic export); their FBW child decals (engine
    # number, FIRE / FAULT faces) were baked at the OFF rest pose and floated there while the lever is ON.
    return [Control(f"eng{n}_master_decals", "follow", f"SWITCH_ENGINES_ENG{n}", f"SWITCH_ENGINES_ENG{n}",
                    f"sim/cockpit2/engine/actuators/mixture_ratio[{n - 1}]", click=False,
                    files=("decal_a380x_cockpit_decal_lovis.obj", "decal_new_decal_noemis.obj"), group="eng_master")
            for n in (1, 2, 3, 4)]


def _glareshield_side():
    rows = []
    for short, side, caption in (("CS", "captain", "CAPT"), ("FO", "first_officer", "F/O")):
        rows.append(Control(f"{side}_console_floor_lt", "switch", f"SWITCH_GLARESHIELD_{short}_CONSOLE_FLOOR_LT",
                            f"SWITCH_GLARESHIELD_{short}_CONSOLE_FLOOR_LT", f"{PREFIX}lighting/{side}_console_floor",
                            label=f"{caption} CONSOLE & FLOOR LT - OFF / DIM / BRT", positions=3, default=0,
                            group="glareshield_side"))
        rows.append(Control(f"{side}_oit_side", "switch", f"SWITCH_GLARESHIELD_{short}_OIT_SIDE",
                            f"SWITCH_GLARESHIELD_{short}_OIT_SIDE", f"{PREFIX}oit/{side}_side",
                            label=f"{caption} OIT SIDE - FLT OPS / NSS AVNCS", positions=2, default=1,
                            group="glareshield_side"))
    return rows


def _overhead():
    # Overhead pushbuttons with a latched position in the lamp module (tools/ovhd_lamps_contract.py
    # pb_state_ids): the same-named command toggles it; here the cap travels and gets a click target.
    from ovhd_lamps_contract import pb_state_ids, row
    rows = []
    for pid in pb_state_ids():
        _pid, caption, node, *_rest = row(pid)
        if node in OVERHEAD_WITHOUT_CLIP:
            continue
        rows.append(Control(f"ovhd_{pid}", "press", node, node, f"{PREFIX}ovhd/pb/{pid}_pressed",
                            (f"{PREFIX}ovhd/pb/{pid}",), f"{caption} - pushbutton", group="overhead"))
    return rows


OVERHEAD_WITHOUT_CLIP = ("PUSH_OVHD_SIGNS",)   # EMER EXIT LT OFF: the FBW legend has no pushbutton clip

CONTROLS: tuple[Control, ...] = (*_switching(), *_ecam(), *_surv(), *_kccu(), *_eng_master(), *_glareshield_side(),
                                 *_overhead())


def owned_commands() -> tuple[str, ...]:
    """Commands the native feedback module creates (press commands of other modules are only hooked)."""
    out = [PREFIX + "surv/" + key for key in SURV_BUTTONS]
    for c in CONTROLS:
        if c.kind == "selector":
            out += [c.dataref + "_left", c.dataref + "_right"]
        elif c.kind == "switch":
            out += [c.dataref + "_up", c.dataref + "_down"]
    return tuple(out)


def hooked_commands() -> tuple[str, ...]:
    owned = set(owned_commands())
    return tuple(cmd for c in CONTROLS if c.kind == "press" for cmd in c.commands if cmd not in owned)


def published_datarefs() -> tuple[str, ...]:
    out = []
    for c in CONTROLS:
        if c.kind != "follow":
            out.append(c.dataref)
        out += [lamp.dataref for lamp in c.lamps]
    return tuple(dict.fromkeys(out))


COMMANDS = owned_commands()
DATAREFS = published_datarefs()


def click_manipulator(c: Control) -> str | None:
    if not c.click or c.kind == "follow":
        return None
    if c.kind == "press":
        return f"ATTR_manip_command\tbutton\t{c.commands[0]}\t{c.label}"
    if c.kind == "selector":
        return f"ATTR_manip_command_knob\trotate_medium\t{c.dataref}_right\t{c.dataref}_left\t{c.label}"
    return f"ATTR_manip_command_switch_up_down\tup_down\t{c.dataref}_up\t{c.dataref}_down\t{c.label}"


def emit_c_table() -> str:
    """native/avionics/a380x_cockpit_feedback_table.h (generated, checked by native/avionics/build.py)."""
    out = ["/* SPDX-License-Identifier: GPL-3.0-or-later",
           " * GENERATED by tools/cockpit_feedback_contract.py emit_c_table(); do not edit by hand. */",
           "#ifndef A380X_COCKPIT_FEEDBACK_TABLE_H", "#define A380X_COCKPIT_FEEDBACK_TABLE_H", "",
           "typedef struct { const char *dataref; const char *command; } CfbPressRow;",
           "typedef struct { const char *dataref; int positions, initial, switch_kind; } CfbPositionRow;",
           "typedef struct { const char *dataref; int page; } CfbEcamLampRow;", "",
           "static const CfbPressRow cfb_press_rows[] = {"]
    for c in CONTROLS:
        if c.kind == "press":
            for cmd in c.commands:
                out.append(f'    {{"{c.dataref}", "{cmd}"}},')
    out += ["};", "", "static const CfbPositionRow cfb_position_rows[] = {"]
    for c in CONTROLS:
        if c.kind in ("selector", "switch"):
            out.append(f'    {{"{c.dataref}", {c.positions}, {c.default}, {1 if c.kind == "switch" else 0}}},')
    out += ["};", "", "static const CfbEcamLampRow cfb_ecam_lamps[] = {"]
    out += [f'    {{"{ref}", {page}}},' for ref, page in ECAM_PAGE_LAMPS.items()]
    out += ["};", "", "static const char *const cfb_surv_buttons[] = {"]
    out += [f'    "{key}",' for key in SURV_BUTTONS]
    out += ["};", "", "#endif", ""]
    return "\n".join(out)


if __name__ == "__main__":
    import sys
    if sys.argv[1:] == ["--emit-c"]:
        sys.stdout.write(emit_c_table())
    else:
        print(f"{len(CONTROLS)} controls, {len(COMMANDS)} owned commands, {len(hooked_commands())} hooked commands, "
              f"{len(DATAREFS)} datarefs")
