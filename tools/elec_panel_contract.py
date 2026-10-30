"""Single source of the ELEC + EMER ELEC PWR overhead panel contract.

Shared by the native registry check (native/avionics/build.py), the avionics provider
manifest and the cockpit OBJ patcher (tools/elec_cockpit_panel.py).  The native plugin
(native/avionics/a380x_elec_panel.h / .inc) registers exactly these names; build.py
compares its mock-registered contract against this module.

Node mapping is positional (source glTF +X is the crew's left).  The FBW model names the
third top-row button PUSH_OVHD_ELEC_GALYCAB and the fourth PUSH_OVHD_ELEC_PAX, but on the
panel (and in the A380 FCOM layout) the third position is PAX SYS and the fourth GALLEY.
"""
from __future__ import annotations

PREFIX = "a380x/xp12/elec/"

# id, panel caption, glTF node, kind, SEQ1 legend, SEQ2 legend, cover id
#   kind: latch     - latched pb-sw (pushed in = normal / AUTO / ON)
#         ext       - momentary EXT PWR pb (toggles ON when AVAIL)
#         guarded_latch      - latched pb-sw under a guard (AC ESS FEED)
#         guarded_momentary  - momentary pb under a guard (DRIVE, RAT MAN ON)
#         indicator - lamp only, not pressable (RAT & EMER GEN FAULT)
#   legend None: the authored segment has no legend for this panel (dots / blank).
CONTROLS = (
    ("bat1", "BAT 1", "PUSH_OVHD_ELEC_BAT1", "latch", "fault", "off", None),
    ("bat_ess", "ESS BAT", "PUSH_OVHD_ELEC_BATESS", "latch", "fault", "off", None),
    ("bat2", "BAT 2", "PUSH_OVHD_ELEC_BAT2", "latch", "fault", "off", None),
    ("bat_apu", "APU BAT", "PUSH_OVHD_ELEC_BATAPU", "latch", "fault", "off", None),
    ("elmu", "ELMU", "PUSH_OVHD_ELEC_ELMU", "latch", None, "off", None),
    ("ac_ess_feed", "AC ESS FEED", "PUSH_OVHD_ELEC_ACESSFEED", "guarded_latch", "fault", "altn", "ac_ess_feed"),
    ("pax_sys", "PAX SYS", "PUSH_OVHD_ELEC_GALYCAB", "latch", None, "off", None),
    ("galley", "GALLEY", "PUSH_OVHD_ELEC_PAX", "latch", None, "off", None),
    ("comm1", "COMMERCIAL 1", "PUSH_OVHD_ELEC_COMMERCIAL", "latch", None, "off", None),
    ("comm2", "COMMERCIAL 2", "PUSH_OVHD_ELEC_COMMERCIAL2", "latch", None, "off", None),
    ("bus_tie", "BUS TIE", "PUSH_OVHD_ELEC_BUSTIE", "latch", None, "off", None),
    ("ext1", "EXT 1", "PUSH_OVHD_ELEC_EXTPWR", "ext", "avail", "on", None),
    ("ext2", "EXT 2", "PUSH_OVHD_ELEC_EXTPWR2", "ext", "avail", "on", None),
    ("ext3", "EXT 3", "PUSH_OVHD_ELEC_EXTPWR3", "ext", "avail", "on", None),
    ("ext4", "EXT 4", "PUSH_OVHD_ELEC_EXTPWR4", "ext", "avail", "on", None),
    ("apu_gen_a", "APU GEN A", "PUSH_OVHD_ELEC_APUGEN", "latch", "fault", "off", None),
    ("apu_gen_b", "APU GEN B", "PUSH_OVHD_ELEC_APUGEN2", "latch", "fault", "off", None),
    ("gen1", "GEN 1", "PUSH_OVHD_ELEC_GEN1", "latch", "fault", "off", None),
    ("gen2", "GEN 2", "PUSH_OVHD_ELEC_GEN2", "latch", "fault", "off", None),
    ("gen3", "GEN 3", "PUSH_OVHD_ELEC_GEN3", "latch", "fault", "off", None),
    ("gen4", "GEN 4", "PUSH_OVHD_ELEC_GEN4", "latch", "fault", "off", None),
    ("drive1", "DRIVE 1", "PUSH_OVHD_ELEC_IDG1", "guarded_momentary", "fault", "disc", "drive1"),
    ("drive2", "DRIVE 2", "PUSH_OVHD_ELEC_IDG2", "guarded_momentary", "fault", "disc", "drive2"),
    ("drive3", "DRIVE 3", "PUSH_OVHD_ELEC_IDG3", "guarded_momentary", "fault", "disc", "drive3"),
    ("drive4", "DRIVE 4", "PUSH_OVHD_ELEC_IDG4", "guarded_momentary", "fault", "disc", "drive4"),
    ("rat_man_on", "RAT MAN ON", "PUSH_OVHD_EMERELECPWR_MANON", "guarded_momentary", None, None, "rat_man_on"),
    ("rat_fault", "RAT & EMER GEN", "PUSH_OVHD_EMERELECPWR_RAT", "indicator", "fault", None, None),
)
# cover id, glTF guard node (hinge from the authored FBW clip), caption
COVERS = (
    ("ac_ess_feed", "LOCK_OVHD_ELEC_ACESSFEED", "AC ESS FEED guard"),
    ("drive1", "LOCK_OVHD_ELEC_IDG1", "DRIVE 1 guard"),
    ("drive2", "LOCK_OVHD_ELEC_IDG2", "DRIVE 2 guard"),
    ("drive3", "LOCK_OVHD_ELEC_IDG3", "DRIVE 3 guard"),
    ("drive4", "LOCK_OVHD_ELEC_IDG4", "DRIVE 4 guard"),
    ("rat_man_on", "LOCK_OVHD_EMERELECPWR_MANON", "RAT MAN ON guard"),
)
# Authored SEQ legend nodes that are not called <node>_SEQ1 / _SEQ2.
SEQ_NODE_OVERRIDES = {("PUSH_OVHD_ELEC_COMMERCIAL", 1): "PUSH_OVHD_ELEC_COMMERCIAL.001",
                      ("PUSH_OVHD_ELEC_COMMERCIAL2", 1): "PUSH_OVHD_ELEC_COMMERCIAL.002"}
# Lamp colours (RGBA) of the lit legend cells.
LEGEND_COLORS = {
    "fault": (255, 180, 45, 255), "off": (240, 245, 255, 255), "altn": (240, 245, 255, 255),
    "disc": (240, 245, 255, 255), "avail": (65, 255, 95, 255), "on": (80, 175, 255, 255),
}
LEGEND_TEXT = {"fault": "FAULT", "off": "OFF", "altn": "ALTN", "disc": "DISC", "avail": "AVAIL", "on": "ON"}
FAULT_CONTROLS = tuple(c[0] for c in CONTROLS if c[4] == "fault") + ("elmu",)  # ELMU FAULT has no authored face
SEQ_POSITION = {1: "upper", 2: "lower"}   # FBW template: SEQ1 = upper legend, SEQ2 = lower legend


def control(cid: str) -> tuple:
    return next(row for row in CONTROLS if row[0] == cid)


def latch_ids() -> tuple[str, ...]:
    return tuple(c[0] for c in CONTROLS if c[3] in ("latch", "guarded_latch"))


def momentary_ids() -> tuple[str, ...]:
    return tuple(c[0] for c in CONTROLS if c[3] in ("ext", "guarded_momentary"))


def pressable_ids() -> tuple[str, ...]:
    return tuple(c[0] for c in CONTROLS if c[3] != "indicator")


def seq_node(node: str, sequence: int) -> str:
    return SEQ_NODE_OVERRIDES.get((node, sequence), f"{node}_SEQ{sequence}")


def press_command(cid: str) -> str:
    """Command bound to the 3D pushbutton (held while the mouse button is down)."""
    return PREFIX + (f"{cid}_toggle" if cid in latch_ids() else f"{cid}_press")


def lamps() -> tuple[tuple[str, str, int, str, str], ...]:
    """(lamp dataref, control id, SEQ number, legend, authored legend node) with an authored face."""
    rows = []
    for cid, _caption, node, _kind, seq1, seq2, _cover in CONTROLS:
        for sequence, legend in ((1, seq1), (2, seq2)):
            if legend:
                rows.append((f"{PREFIX}lamp/{cid}_{SEQ_POSITION[sequence]}", cid, sequence, legend,
                             seq_node(node, sequence)))
    return tuple(rows)


# Names follow docs/ELEC_SYSTEM_SPEC.md section 10.3 (phase 2 takes them over unchanged).
COMMANDS = (
    *(press_command(cid) for cid in pressable_ids()),
    *(PREFIX + f"{cid}_{state}" for cid in latch_ids() for state in ("on", "off")),
    *(PREFIX + f"drive{n}_reset" for n in range(1, 5)), PREFIX + "drive_reset_all",
    PREFIX + "gpu_connect_toggle",
    *(PREFIX + f"{cover}_cover_{suffix}" for cover, _node, _caption in COVERS for suffix in ("toggle", "open", "close")),
)
DATAREFS = (
    *(PREFIX + f"pb/{cid}" for cid in pressable_ids()),
    *(PREFIX + f"pb/{cid}_press_ratio" for cid in pressable_ids()),
    *(row[0] for row in lamps()), PREFIX + "lamp/elmu_upper",
    *(PREFIX + f"ext/avail{n}" for n in range(1, 5)), PREFIX + "ext/connected",
    *(PREFIX + f"fail/{cid}" for cid in FAULT_CONTROLS),
    *(PREFIX + f"cover/{cover}" for cover, _node, _caption in COVERS),
    PREFIX + "ann_lt", PREFIX + "lamp_power", PREFIX + "external_logic", PREFIX + "stock_battery_slots",
)
SOUND_FILES = ("elec_pb_press_a.pcm", "elec_pb_press_b.pcm", "elec_pb_release_a.pcm",
               "elec_pb_release_b.pcm", "elec_guard_open.pcm", "elec_guard_close.pcm")
