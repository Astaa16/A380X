#!/usr/bin/env python3
"""Derive FBW A380X sound-emitter positions in X-Plane aircraft coordinates.

Read-only inputs:
  * FBW ``sound.xml`` files (``NodeName`` emitters) and cockpit behaviour XML
    (``WWISE_EVENT``/``WWISE_EVENT_1``/``WWISE_EVENT_2`` template parameters).
  * FBW glTF JSON (nodes, scenes, accessor min/max only; ``.bin`` never read).
  * MSFS attachment configuration (presets, ``attached_objects.cfg``, model XML).
  * The port's X-Plane ACF, cockpit-control map and exported OBJs (verification).

Coordinate convention (identical to the port's OBJ pipeline):
  * MSFS glTF: right-handed, +Y up, +Z forward, +X left, metres, origin at the
    MSFS reference datum (``flight_model.cfg`` reference_datum_position = 0).
  * OBJ = glTF world coordinates unchanged.  ``build_cockpit_controls.py`` writes
    glTF world positions directly; the Blender route (glTF importer Y-up -> Z-up,
    XPlane2Blender Z-up -> Y-up) cancels.  This is re-verified numerically.
  * ACF ``_obja`` attaches every OBJ at (0, 0, 115.108 ft) with psi = 180 deg,
    i.e. the reference point is the nose tip, X-Plane +x right, +y up, +z aft.
  * Therefore  x_xp = -x_gltf,  y_xp = y_gltf,  z_xp = 35.0849 m - z_gltf
    (metres relative to the ACF reference point).

Writes ``build/fbw-sounds-stage/emitters.json`` and prints a short summary.
"""

from __future__ import annotations

import argparse
import collections
import configparser
import json
import math
import re
import statistics
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np

PORT_ROOT = Path(__file__).resolve().parents[1]
AIRCRAFT_REL = Path("fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X")
MODEL_BEHAVIOR_DEFS_REL = Path("fbw-a380x/src/base/flybywire-aircraft-a380-842/ModelBehaviorDefs")
PRESET = "presets/flybywire/FlyByWire_A380_842"
FT = 0.3048
SCHEMA_VERSION = 1

COCKPIT_SOUND_XML = "attachments/flybywire/Part_Interior_Cockpit/sound/sound.xml"
COMMON_SOUND_XML = "common/sound/sound.xml"
COCKPIT_BEHAVIOUR = "attachments/flybywire/Part_Interior_Cockpit/model/A380_Cockpit_Behavior.xml"
COCKPIT_LOD00 = "attachments/flybywire/Part_Interior_Cockpit/model/A380_COCKPIT_LOD00.gltf"
CONTROLS_OBJ = "objects/interior/cockpit-controls/a380x_cockpit_controls.obj"

# Sound.xml nodes that do not exist in any A380 glTF (legacy A32NX names).
# These substitutes are heuristics for the port, not FBW data.
SUBSTITUTES = {
    "WHEELS_L": ("gear:wing_left", "legacy A32NX left main wheels; epump/nose-lock sounds"),
    "WHEELS_C": ("gear:nose", "legacy A32NX nose wheels; mech call horn"),
    "TAIL_RUDDER": ("node:TAIL_RUDDER_center", "A380 rudder hinge node in fuselage glTF"),
    "TAIL_ELEVATOR_LEFT": ("node:THS_TRIM", "no separate A380 elevator node; THS pivot"),
    "TAIL_ELEVATOR_TRIM_RIGHT": ("node:THS_TRIM", "THS pivot"),
    "WING_AILERON_1_LEFT": ("node:WING_AILERON_12.L", "inboard aileron hinge, left wing"),
    "WING_AILERON_1_RIGHT": ("node:WING_AILERON_12.R", "inboard aileron hinge, right wing"),
    "AILERON_RIGHT001": ("node:VFX_Vortex_Wingtip_Left", "LEFT_WING_SCRAPE event: left wing tip"),
    "AILERON_RIGHT": ("node:VFX_Vortex_Wingtip_Right", "RIGHT_WING_SCRAPE event: right wing tip"),
    "LEVER_ELEVATORTRIM_1": ("node:SWITCH_TRIM_PITCH", "A380 has no trim wheel; pedestal pitch-trim switch"),
    "HublotIn002": ("mesh:A380X_WINDOW_GLASS", "legacy cabin window node; main-deck window band centre"),
}

# Port control nodes whose FBW sound comes from a sound.xml LocalVar trigger rather
# than a behaviour WWISE_EVENT ({n} = engine number).  Same-name LocalVars link too.
CONTROL_LOCALVAR_LINKS = {
    "LEVER_LANDINGGEAR": ["A32NX_GEAR_HANDLE_POSITION"],
    "LEVER_FLAPS": ["A32NX_FLAPS_HANDLE_PERCENT"],
    "LEVER_SPEEDBRAKE": ["A32NX_SPOILERS_HANDLE_POSITION", "A32NX_SPOILERS_ARMED"],
    "LEVER_THROTTLE_{n}_CLICK": ["XMLVAR_Throttle{n}Position", "A32NX_3D_THROTTLE_LEVER_POSITION_{n}"],
    "LEVER_THROTTLE_{n}_REVERSE": ["A32NX_3D_THROTTLE_LEVER_POSITION_{n}"],
}
# Port control nodes whose FBW behaviour binds a node absent from the A380 glTF.
CONTROL_BEHAVIOUR_ALIASES = {
    "ATHR_DISCONNECT_ENG1": "PUSH_THROTTLE_1",
    "ATHR_DISCONNECT_ENG2": "PUSH_THROTTLE_2",
}

ASOBO_INTERACTION = re.compile(
    r"(Push|Switch|Knob|Lever|Interaction_(?!Tooltip)|AnimTriggers|Trim|Dragging|Wheel)", re.I)
ASOBO_NON_SOUND = re.compile(r"(Emissive|Visibility|Tooltip|Highlight|MouseRect|Material|Update|Dial|EICAS)", re.I)
TEMPLATE_RANK = (  # preferred terminal template when several fire on one node
    ("AnimTriggers", 0), ("Push_Button", 1), ("Switch", 1), ("Knob", 1), ("Lever", 1),
    ("LIGHTING_Switch", 1), ("AUTOPILOT", 1), ("Interaction_Base", 2), ("Interaction", 3))


# --------------------------------------------------------------------------- helpers

def r4(values) -> list[float]:
    return [round(float(v), 4) for v in values]


def gltf_local_matrix(node: dict) -> np.ndarray:
    if "matrix" in node:
        return np.array(node["matrix"], dtype=float).reshape(4, 4).T  # column-major
    tx, ty, tz = node.get("translation", (0.0, 0.0, 0.0))
    x, y, z, w = node.get("rotation", (0.0, 0.0, 0.0, 1.0))
    sx, sy, sz = node.get("scale", (1.0, 1.0, 1.0))
    rotation = np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])
    matrix = np.eye(4)
    matrix[:3, :3] = rotation * np.array([sx, sy, sz])
    matrix[:3, 3] = (tx, ty, tz)
    return matrix


class Gltf:
    """glTF JSON only: node hierarchy, world matrices, accessor bounds."""

    def __init__(self, path: Path, attach: np.ndarray, attachment: str, lod_rank: int):
        self.path = path
        self.attachment = attachment
        self.lod_rank = lod_rank
        self.doc = json.loads(path.read_text(encoding="utf-8"))
        nodes = self.doc.get("nodes", [])
        self.nodes = nodes
        parents = {child: index for index, node in enumerate(nodes) for child in node.get("children", [])}
        self.parents = parents
        world: dict[int, np.ndarray] = {}

        def resolve(index: int) -> np.ndarray:
            stack = []
            while index not in world:
                stack.append(index)
                if index not in parents:
                    break
                index = parents[index]
            for item in reversed(stack):
                parent = world.get(parents[item], attach) if item in parents else attach
                world[item] = parent @ gltf_local_matrix(nodes[item])
            return world[stack[0]] if stack else world[index]

        for index in range(len(nodes)):
            resolve(index)
        self.world = world
        self.attach = attach
        self.by_name: dict[str, int] = {}
        for index, node in enumerate(nodes):
            self.by_name.setdefault(node.get("name", ""), index)
        scene_roots = set()
        for scene in self.doc.get("scenes", []):
            scene_roots.update(scene.get("nodes", []))
        self.scene_roots = scene_roots

    def origin(self, index: int) -> np.ndarray:
        return self.world[index][:3, 3].copy()

    def mesh_bounds(self, index: int) -> tuple[np.ndarray, np.ndarray] | None:
        node = self.nodes[index]
        if "mesh" not in node:
            return None
        low, high = np.full(3, np.inf), np.full(3, -np.inf)
        # A skinned mesh is placed by its joints; at bind pose its vertices are
        # already in model space, so only the attachment transform applies.
        matrix = self.attach if "skin" in node else self.world[index]
        for primitive in self.doc["meshes"][node["mesh"]].get("primitives", []):
            accessor = self.doc["accessors"][primitive["attributes"]["POSITION"]]
            if "min" not in accessor:
                continue
            mn, mx = accessor["min"], accessor["max"]
            corners = np.array([[x, y, z, 1.0] for x in (mn[0], mx[0]) for y in (mn[1], mx[1])
                                for z in (mn[2], mx[2])])
            points = (matrix @ corners.T).T[:, :3]
            low, high = np.minimum(low, points.min(0)), np.maximum(high, points.max(0))
        if not np.all(np.isfinite(low)):
            return None
        return low, high

    def mesh_center(self, index: int) -> np.ndarray | None:
        """Exact transform of the local accessor-box centre (not the AABB of corners)."""
        bounds = self.mesh_bounds(index)
        return None if bounds is None else (bounds[0] + bounds[1]) / 2


# --------------------------------------------------------------------------- MSFS attachments

def read_cfg(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(strict=False, interpolation=None, inline_comment_prefixes=(";",))
    parser.optionxform = str
    parser.read_string(path.read_text(encoding="utf-8", errors="replace"))
    return parser


def unquote(value: str) -> str:
    return value.strip().strip('"').strip()


def model_lods(xml_path: Path) -> list[Path]:
    root = ET.parse(xml_path).getroot()
    lods = []
    for lod in root.iter("LOD"):
        model_file = lod.get("ModelFile")
        if model_file:
            lods.append((xml_path.parent / model_file.replace("\\", "/")).resolve())
    return lods


def to_large(path: Path, upstream: Path, large: Path) -> Path:
    try:
        return large / path.resolve().relative_to(upstream.resolve())
    except ValueError:
        return path


def aircraft_relpath(root_value: str) -> Path:
    text = unquote(root_value).replace("\\", "/")
    marker = "FlyByWire_A380X/"
    index = text.lower().find(marker.lower())
    return Path(text[index + len(marker):]) if index >= 0 else Path(text)


def build_model_registry(upstream: Path, large: Path) -> tuple[list[Gltf], dict]:
    """Load every glTF reachable from the default preset with its MSFS attach transform."""
    aircraft = upstream / AIRCRAFT_REL
    aircraft_large = large / AIRCRAFT_REL
    notes: dict = {"attachments": [], "root_models": {}}
    gltfs: list[Gltf] = []
    loaded: dict[Path, Gltf] = {}

    def load(path: Path, attach: np.ndarray, label: str, rank: int) -> Gltf | None:
        large_path = to_large(path, upstream, large)
        if not large_path.exists():
            notes.setdefault("missing_gltf", []).append(str(large_path))
            return None
        if large_path in loaded:
            return loaded[large_path]
        gltf = Gltf(large_path, attach, label, rank)
        loaded[large_path] = gltf
        gltfs.append(gltf)
        return gltf

    # Root models (model.cfg normal/interior): both are Helper.gltf at identity.
    model_cfg = read_cfg(aircraft / "common/model/model.cfg")
    root_models = {}
    for kind, key in (("exterior", "normal"), ("interior", "interior")):
        xml = aircraft / "common/model" / model_cfg["models"][key]
        lods = [to_large(p, upstream, large) for p in model_lods(xml)]
        root = Gltf(lods[0], np.eye(4), f"root:{kind}", 0) if lods and lods[0].exists() else None
        root_models[kind] = root
        notes["root_models"][kind] = {"xml": str(xml.relative_to(upstream)),
                                      "lods": [str(p.relative_to(large)) for p in lods]}

    def attach_matrix(parent: Gltf | None, node_name: str | None) -> tuple[np.ndarray, str]:
        if parent is None:
            return np.eye(4), "no parent model (identity)"
        if not node_name:
            return parent.attach.copy(), "model root"
        index = parent.by_name.get(node_name)
        if index is None:
            return parent.attach.copy(), f"attach node {node_name} not found (identity assumed)"
        return parent.world[index].copy(), f"node {node_name} in {parent.path.name}"

    preset_cfg = read_cfg(aircraft / PRESET / "config/attached_objects.cfg")
    for section in preset_cfg.sections():
        if not section.startswith("sim_attachment"):
            continue
        entry = preset_cfg[section]
        function_dir = aircraft / aircraft_relpath(entry["attachment_root"])
        function_xml = function_dir / unquote(entry["attachment_file"]).replace("\\", "/")
        kind = unquote(entry.get("attach_to_model", "exterior"))
        node = unquote(entry.get("attach_to_node", "")) or None
        matrix, how = attach_matrix(root_models.get(kind), node)
        function_lods = model_lods(function_xml)
        function_models = []
        for rank, lod in enumerate(function_lods):
            gltf = load(lod, matrix, function_dir.name, rank)
            if gltf:
                function_models.append(gltf)
        notes["attachments"].append({
            "attachment": function_dir.name, "attach_to_model": kind, "attach_to_node": node,
            "attach_resolved": how, "translation_m": r4(matrix[:3, 3]),
            "is_identity": bool(np.allclose(matrix, np.eye(4), atol=1e-9)),
            "lods": [str(to_large(p, upstream, large).relative_to(large)) for p in function_lods]})
        part_cfg_path = function_dir / "config/attached_objects.cfg"
        if not part_cfg_path.exists():
            continue
        part_cfg = read_cfg(part_cfg_path)
        parent = function_models[0] if function_models else None
        for part_section in part_cfg.sections():
            part = part_cfg[part_section]
            part_dir = aircraft / aircraft_relpath(part["attachment_root"])
            part_xml = part_dir / unquote(part["attachment_file"]).replace("\\", "/")
            if not part_xml.exists():  # case-insensitive fallback (A380_Exterior.xml)
                matches = [p for p in part_xml.parent.glob("*.xml") if p.name.lower() == part_xml.name.lower()]
                part_xml = matches[0] if matches else part_xml
            part_node = unquote(part.get("attach_to_node", "")) or None
            part_matrix, part_how = attach_matrix(parent, part_node)
            part_lods = model_lods(part_xml)
            for rank, lod in enumerate(part_lods):
                load(lod, part_matrix, part_dir.name, rank)
            notes["attachments"].append({
                "attachment": part_dir.name, "parent": function_dir.name, "attach_to_node": part_node,
                "attach_resolved": part_how, "translation_m": r4(part_matrix[:3, 3]),
                "is_identity": bool(np.allclose(part_matrix, np.eye(4), atol=1e-9)),
                "lods": [str(to_large(p, upstream, large).relative_to(large)) for p in part_lods]})
    notes["all_attachments_identity"] = all(item["is_identity"] for item in notes["attachments"])
    # glTFs not reachable from the preset (e.g. LOD01 of nose gear is, others not)
    reachable = {g.path for g in gltfs}
    extra = sorted(str(p.relative_to(large)) for p in aircraft_large.rglob("*.gltf") if p not in reachable)
    notes["unreferenced_gltf"] = extra
    return gltfs, notes


class NodeIndex:
    def __init__(self, gltfs: list[Gltf], large: Path):
        self.large = large
        self.gltfs = gltfs
        self.names: dict[str, list[tuple[Gltf, int]]] = collections.defaultdict(list)
        for gltf in gltfs:
            for index, node in enumerate(gltf.nodes):
                name = node.get("name")
                if name:
                    self.names[name].append((gltf, index))

    def lookup(self, name: str, prefer: str | None = None) -> list[tuple[Gltf, int]]:
        hits = list(self.names.get(name, []))
        if not hits and name:
            folded = name.casefold()
            hits = [hit for key, values in self.names.items() if key.casefold() == folded for hit in values]
        hits.sort(key=lambda item: (0 if prefer and prefer in item[0].path.name else 1,
                                    item[0].lod_rank, str(item[0].path)))
        return hits

    def rel(self, gltf: Gltf) -> str:
        return str(gltf.path.relative_to(self.large))


# --------------------------------------------------------------------------- X-Plane conversion

def read_acf(path: Path) -> dict[str, str]:
    values = {}
    with path.open(encoding="latin-1") as handle:
        for line in handle:
            if line.startswith("P "):
                parts = line.rstrip("\n").split(" ", 2)
                if len(parts) == 3:
                    values[parts[1]] = parts[2]
    return values


class Conversion:
    def __init__(self, acf: dict[str, str]):
        attachments = collections.defaultdict(dict)
        for key, value in acf.items():
            match = re.match(r"_obja/(\d+)/(.+)", key)
            if match:
                attachments[int(match.group(1))][match.group(2)] = value
        self.attachments = attachments
        fields = ("_v10_att_x_acf_prt_ref", "_v10_att_y_acf_prt_ref", "_v10_att_z_acf_prt_ref",
                  "_v10_att_psi_ref", "_v10_att_the_ref", "_v10_att_phi_ref")
        counts = collections.Counter(tuple(round(float(a.get(f, 0)), 6) for f in fields)
                                     for a in attachments.values())
        (x, y, z, psi, the, phi), self.base_count = counts.most_common(1)[0]
        if abs(the) > 1e-6 or abs(phi) > 1e-6:
            raise ValueError("Only heading-rotated OBJ attachments are supported")
        self.offset_ft = (x, y, z)
        self.psi = psi
        self.exceptions = []
        for index, attachment in sorted(attachments.items()):
            key = tuple(round(float(attachment.get(f, 0)), 6) for f in fields)
            if key != (x, y, z, psi, the, phi):
                self.exceptions.append({"obja": index, "obj": attachment.get("_v10_att_file_stl"),
                                        "offset_ft": list(key[:3]), "psi_the_phi_deg": list(key[3:])})
        self.cg_ft = (0.0, float(acf.get("acf/_cgY", 0)), float(acf.get("acf/_cgZ", 0)))
        self.total = len(attachments)

    def obj_offset_ft(self, obj_path: str) -> tuple[float, float, float]:
        for attachment in self.attachments.values():
            if attachment.get("_v10_att_file_stl") == obj_path:
                return tuple(float(attachment[f"_v10_att_{a}_acf_prt_ref"]) for a in "xyz")
        return self.offset_ft

    def to_xp(self, point, offset_ft=None) -> np.ndarray:
        """glTF/OBJ metres -> X-Plane aircraft metres relative to the ACF reference point."""
        x, y, z = (float(v) for v in point)
        ox, oy, oz = offset_ft if offset_ft is not None else self.offset_ft
        c, s = math.cos(math.radians(self.psi)), math.sin(math.radians(self.psi))
        return np.array([x * c - z * s + ox * FT, y + oy * FT, x * s + z * c + oz * FT])

    def from_xp(self, point) -> np.ndarray:
        x, y, z = (float(v) for v in point)
        ox, oy, oz = (v * FT for v in self.offset_ft)
        x, y, z = x - ox, y - oy, z - oz
        c, s = math.cos(math.radians(self.psi)), math.sin(math.radians(self.psi))
        return np.array([x * c + z * s, y, -x * s + z * c])

    def cg_relative(self, xp) -> list[float]:
        return r4(np.asarray(xp) - np.array(self.cg_ft) * FT)

    def describe(self) -> dict:
        ox, oy, oz = self.offset_ft
        return {
            "description": (
                "MSFS glTF (right-handed, +Y up, +Z forward, +X left, metres, origin = MSFS reference "
                "datum) -> X-Plane aircraft coordinates (+x right, +y up, +z aft, metres, relative to "
                "the ACF reference point = nose tip). OBJ vertices equal glTF world coordinates; the ACF "
                f"attaches {self.base_count} of {self.total} OBJs at ({ox}, {oy}, {oz}) ft with "
                f"psi={self.psi} deg."),
            "formula": (f"x_xp = -x_gltf; y_xp = y_gltf + {oy * FT:.4f}; "
                        f"z_xp = {oz * FT:.4f} - z_gltf   (general: R_y(psi) * p_obj + offset_ft*0.3048)"),
            "obj_attachment_offset_ft": [ox, oy, oz],
            "obj_attachment_offset_m": r4(np.array(self.offset_ft) * FT),
            "obj_attachment_psi_deg": self.psi,
            "obj_equals_gltf": True,
            "msfs_attachments_note": "",
            "acf_default_cg_ft": list(self.cg_ft),
            "acf_default_cg_m": r4(np.array(self.cg_ft) * FT),
            "cg_relative_note": ("x,y,z are relative to the ACF reference point. 'cg_relative' fields "
                                 "subtract the ACF default CG (acf/_cgY, acf/_cgZ); X-Plane runtime "
                                 "position datarefs such as sim/aircraft/view/acf_peX/Y/Z use CG-relative "
                                 "metres."),
            "attachment_exceptions": self.exceptions,
        }


def xp_entry(conv: Conversion, gltf_point, offset_ft=None) -> dict:
    xp = conv.to_xp(gltf_point, offset_ft)
    return {"x": round(float(xp[0]), 4), "y": round(float(xp[1]), 4), "z": round(float(xp[2]), 4),
            "cg_relative": conv.cg_relative(xp), "gltf": r4(gltf_point)}


def msfs_cfg_to_gltf(longitudinal_ft: float, lateral_ft: float, vertical_ft: float) -> np.ndarray:
    """MSFS cfg tuple (longitudinal fwd, lateral right, vertical up; ft) -> glTF metres."""
    return np.array([-lateral_ft * FT, vertical_ft * FT, longitudinal_ft * FT])


# --------------------------------------------------------------------------- sound.xml

def parse_sound_xml(path: Path, rel: str, local_vars: dict | None = None) -> tuple[dict, list[str]]:
    nodes: dict[str, dict] = {}
    animation_sounds = []
    root = ET.parse(path).getroot()
    for parent in root.iter():
        for sound in parent.findall("Sound"):
            node = sound.get("NodeName")
            event = sound.get("WwiseEvent") or sound.get("FileName") or ""
            if parent.tag == "AnimationSounds" and event:
                animation_sounds.append(event)
            if local_vars is not None and sound.get("LocalVar") and event:
                local_vars.setdefault(sound.get("LocalVar"), []).append(
                    {"event": event, "emitter_node": node, "local_var": sound.get("LocalVar"), "sound_xml": rel})
            if not node:
                continue
            entry = nodes.setdefault(node, {"events": [], "files": set(), "viewpoints": set()})
            if event and event not in entry["events"]:
                entry["events"].append(event)
            entry["files"].add(rel)
            entry["viewpoints"].add(sound.get("ViewPoint", "Both"))
    return nodes, animation_sounds


# --------------------------------------------------------------------------- behaviour templates

TOKEN = re.compile(r"#([A-Za-z0-9_]+)#")


def subst(text: str | None, scope: dict, passes: int = 8) -> str | None:
    if text is None:
        return None
    for _ in range(passes):
        new = TOKEN.sub(lambda m: scope[m.group(1)] if m.group(1) in scope else m.group(0), text)
        if new == text:
            break
        text = new
    return text


def element_text(element: ET.Element) -> str:
    if len(element):  # parameter containing markup: keep raw XML text
        return "".join(ET.tostring(child, encoding="unicode") for child in element).strip()
    return (element.text or "").strip()


class BehaviourExpander:
    """Small emulator of the MSFS model-behaviour template language.

    Supported: Include, Template, UseTemplate (inherited parameter scope),
    DefaultTemplateParameters / OverrideTemplateParameters / Parameters Type=,
    Condition (Check/Match/Valid/NotEmpty/Test + True/False), Switch/Case/Default,
    Component nesting and the Asobo helpers Suffix_ID_Appender / Recursive_ID.
    Templates not present in the tree (Asobo SDK) are terminals.
    """

    def __init__(self, entry: Path, behavior_defs: Path):
        self.templates: dict[str, tuple[ET.Element, str]] = {}
        self.duplicates: dict[str, list[str]] = collections.defaultdict(list)
        self.missing_includes: list[str] = []
        self.files: list[str] = []
        self.base = entry.parent
        self.behavior_defs = behavior_defs
        self.root = self._load(entry)
        self.records: list[dict] = []
        self.terminals = collections.Counter()
        self.max_depth_hits = 0

    def _rel(self, path: Path) -> str:
        try:
            return str(path.resolve().relative_to(self.base.resolve()))
        except ValueError:
            return str(path)

    def _load(self, path: Path) -> ET.Element:
        root = ET.parse(path).getroot()
        self.files.append(self._rel(path))
        containers = [root] + [child for child in root if child.tag == "Behaviors"]
        for container in containers:
            for child in container:
                if child.tag == "Include":
                    target = None
                    if child.get("RelativeFile"):
                        target = path.parent / child.get("RelativeFile").replace("\\", "/")
                    elif child.get("Path"):
                        target = self.behavior_defs / child.get("Path").replace("\\", "/")
                    if target is not None and target.exists():
                        if self._rel(target) not in self.files:
                            self._load(target)
                    else:
                        self.missing_includes.append(child.get("ModelBehaviorFile") or child.get("Path")
                                                     or child.get("RelativeFile") or "?")
                elif child.tag == "Template":
                    name = child.get("Name")
                    if name in self.templates:
                        self.duplicates[name].append(self._rel(path))
                    else:
                        self.duplicates.setdefault(name, [])
                    # Later definitions win (include order); duplicates are reported.
                    self.templates[name] = (child, self._rel(path))
        return root

    # -- conditions -------------------------------------------------------
    def _test(self, element: ET.Element, scope: dict) -> bool:
        tag = element.tag
        if tag == "Test":
            return all(self._test(child, scope) for child in element)
        if tag == "And":
            return all(self._test(child, scope) for child in element)
        if tag == "Or":
            return any(self._test(child, scope) for child in element)
        if tag == "Not":
            return not all(self._test(child, scope) for child in element)
        if tag in ("Greater", "Lower", "Equal", "GreaterOrEqual", "LowerOrEqual", "NotEqual"):
            values = []
            for child in element:
                raw = (child.text or "").strip()
                if child.tag == "Value":
                    raw = subst(scope.get(raw, raw), scope)
                values.append(raw)
            try:
                a, b = (float(v) for v in values[:2])
            except (TypeError, ValueError):
                a, b = values[0] if values else None, values[1] if len(values) > 1 else None
            ops = {"Greater": lambda: a > b, "Lower": lambda: a < b, "Equal": lambda: a == b,
                   "GreaterOrEqual": lambda: a >= b, "LowerOrEqual": lambda: a <= b,
                   "NotEqual": lambda: a != b}
            try:
                return bool(ops[tag]())
            except TypeError:
                return False
        return False

    def _attr_true(self, element: ET.Element, scope: dict) -> bool | None:
        if element.get("Check") is not None:
            name = element.get("Check")
            if element.get("Match") is not None:
                return name in scope and subst(scope[name], scope).strip() == element.get("Match")
            return name in scope
        if element.get("Valid") is not None:
            name = element.get("Valid")
            return name in scope and subst(scope[name], scope).strip().lower() not in ("", "false")
        if element.get("NotEmpty") is not None:
            name = element.get("NotEmpty")
            return name in scope and subst(scope[name], scope).strip() != ""
        return None

    def _condition(self, element: ET.Element, scope: dict) -> list[ET.Element]:
        result = self._attr_true(element, scope)
        if result is None:
            tests = [child for child in element if child.tag == "Test"]
            result = all(self._test(test, scope) for test in tests) if tests else False
        branches = {child.tag: child for child in element if child.tag in ("True", "False")}
        if branches:
            chosen = branches.get("True" if result else "False")
            return list(chosen) if chosen is not None else []
        return [child for child in element if child.tag != "Test"] if result else []

    def _switch(self, element: ET.Element, scope: dict) -> list[ET.Element]:
        param = element.get("Param")
        default = None
        for case in element:
            if case.tag == "Default":
                default = case
                continue
            if case.tag != "Case":
                continue
            if case.get("Value") is not None and param is not None:
                if param in scope and subst(scope[param], scope).strip() == case.get("Value"):
                    return list(case)
            elif self._attr_true(case, scope):
                return list(case)
        return list(default) if default is not None else []

    # -- parameters -------------------------------------------------------
    def _params(self, children, scope: dict, override: bool, fixed: set) -> None:
        """Default: set unless supplied by the caller/inherited scope (``fixed``);
        later defaults in the same template may replace earlier defaults.
        Override: always set, and then protected from later defaults."""
        for child in children:
            if child.tag == "Condition":
                self._params(self._condition(child, scope), scope, override, fixed)
            elif child.tag == "Switch":
                self._params(self._switch(child, scope), scope, override, fixed)
            elif child.tag in ("True", "False", "Default", "Case"):
                continue
            elif override:
                scope[child.tag] = subst(element_text(child), scope)
                fixed.add(child.tag)
            elif child.tag not in fixed:
                scope[child.tag] = subst(element_text(child), scope)

    # -- body ---------------------------------------------------------------
    def expand(self) -> None:
        containers = [self.root] + [child for child in self.root if child.tag == "Behaviors"]
        for container in containers:
            for child in container:
                if child.tag == "Component":
                    self._component(child, {}, set(), [], [], [])

    def _component(self, element, scope, fixed, chain, comp_ids, comp_nodes) -> None:
        cid = subst(element.get("ID"), scope)
        node = subst(element.get("Node"), scope)
        self._body(list(element), dict(scope), set(fixed), chain, comp_ids + [cid or "?"],
                   comp_nodes + ([node] if node else []))

    def _body(self, children, scope, fixed, chain, comp_ids, comp_nodes) -> None:
        for child in children:
            tag = child.tag
            if tag == "Component":
                self._component(child, scope, fixed, chain, comp_ids, comp_nodes)
            elif tag == "UseTemplate":
                self._use(child, scope, chain, comp_ids, comp_nodes)
            elif tag == "Condition":
                self._body(self._condition(child, scope), scope, fixed, chain, comp_ids, comp_nodes)
            elif tag == "Switch":
                self._body(self._switch(child, scope), scope, fixed, chain, comp_ids, comp_nodes)
            elif tag == "DefaultTemplateParameters":
                self._params(list(child), scope, False, fixed)
            elif tag == "OverrideTemplateParameters":
                self._params(list(child), scope, True, fixed)
            elif tag == "Parameters":
                self._params(list(child), scope, (child.get("Type") or "").lower() == "override", fixed)

    def _use(self, element, scope, chain, comp_ids, comp_nodes) -> None:
        name = subst(element.get("Name"), scope) or "?"
        new = dict(scope)
        self._params(list(element), new, True, set())
        self._call(name, new, chain, comp_ids, comp_nodes)

    def _call(self, name, scope, chain, comp_ids, comp_nodes) -> None:
        if len(chain) > 80:
            self.max_depth_hits += 1
            return
        if name == "ASOBO_GT_Helper_Suffix_ID_Appender" and "TEMPLATE_TO_CALL" in scope:
            ident = subst(scope.get("ID", ""), scope).strip() if "ID" in scope else ""
            scope["SUFFIX_ID"] = f"_{ident}" if ident else ""
            self._call(subst(scope["TEMPLATE_TO_CALL"], scope), scope, chain + [name], comp_ids, comp_nodes)
            return
        if name == "ASOBO_GT_Helper_Recursive_ID" and "EXIT_TEMPLATE" in scope:
            try:
                first = int(subst(scope.get("FIRST_ID", scope.get("ID", "1")), scope))
                last = int(subst(scope.get("MAX_ID", str(first)), scope))
            except (TypeError, ValueError):
                first = last = 1
            for ident in range(first, last + 1):
                child = dict(scope)
                child["ID"] = str(ident)
                for k in range(1, 10):  # PARAMk names a parameter set to PREFIX + ID + SUFFIX
                    target = child.get(f"PARAM{k}")
                    if target:
                        child[subst(target, child)] = (subst(child.get(f"PARAM{k}_PREFIX", ""), child)
                                                       + str(ident)
                                                       + subst(child.get(f"PARAM{k}_SUFFIX", ""), child))
                self._call(subst(scope["EXIT_TEMPLATE"], child), child, chain + [name], comp_ids, comp_nodes)
            return
        if name in self.templates:
            template, _source = self.templates[name]
            self._body(list(template), scope, set(scope), chain + [name], comp_ids, comp_nodes)
            return
        self._terminal(name, scope, chain, comp_ids, comp_nodes)

    def _terminal(self, name, scope, chain, comp_ids, comp_nodes) -> None:
        self.terminals[name] += 1
        if ASOBO_NON_SOUND.search(name) or not ASOBO_INTERACTION.search(name):
            return
        get = lambda key: subst(scope.get(key), scope) if key in scope else None  # noqa: E731
        events = {key: get(key) for key in ("WWISE_EVENT", "WWISE_EVENT_1", "WWISE_EVENT_2")}
        node_id = get("NODE_ID")
        component_node = comp_nodes[-1] if comp_nodes else None
        node = component_node or node_id
        if node and TOKEN.search(node) and node_id and not TOKEN.search(node_id):
            node = node_id
        resolution = "exact"
        if node and TOKEN.search(node):
            folded = {key.casefold(): key for key in scope}
            fixed = TOKEN.sub(lambda m: subst(scope[folded[m.group(1).casefold()]], scope)
                              if m.group(1).casefold() in folded else m.group(0), node)
            if fixed != node and not TOKEN.search(fixed):
                node, resolution = fixed, "case_insensitive_parameter"
            else:
                resolution = "unresolved_parameter"
        self.records.append({
            "node": node, "node_resolution": resolution, "node_id_param": node_id,
            "component_node": component_node,
            "anim_name": get("ANIM_NAME"), "part_id": get("PART_ID"),
            "terminal_template": name, "template_chain": chain, "component_path": "/".join(comp_ids),
            "wwise_event": events["WWISE_EVENT"], "wwise_event_1": events["WWISE_EVENT_1"],
            "wwise_event_2": events["WWISE_EVENT_2"],
            "normalized_time_1": get("NORMALIZED_TIME_1"), "normalized_time_2": get("NORMALIZED_TIME_2"),
        })


def template_rank(name: str) -> int:
    for needle, rank in TEMPLATE_RANK:
        if needle.lower() in name.lower():
            return rank
    return 5


def summarise_events(record: dict, declared: set[str]) -> dict:
    def clean(value):
        if value is None or value == "":
            return None
        return f"unresolved:{value}" if TOKEN.search(value) else value

    on, off, single = clean(record["wwise_event_1"]), clean(record["wwise_event_2"]), clean(record["wwise_event"])
    if on or off:
        source = "explicit_or_fbw_template_default"
        events_on, events_off = on, off
    elif single:
        source = "single_event_all_transitions"
        events_on = events_off = single
    else:
        source = "asobo_default"
        events_on = events_off = f"asobo_default:{record['terminal_template']}"
    names = {e for e in (events_on, events_off) if e and not e.startswith(("asobo_default:", "unresolved:"))}
    return {"events_on": events_on, "events_off": events_off, "events_source": source,
            "events_declared_in_sound_xml": {e: e.lower() in declared for e in sorted(names)}}


# --------------------------------------------------------------------------- OBJ verification

def parse_obj_vertices(path: Path):
    vertices, indices, lines = [], [], []
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if line.startswith("VT"):
                parts = line.split()
                vertices.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif line.startswith("IDX"):
                indices.extend(int(v) for v in line.split()[1:])
            else:
                lines.append(line.rstrip("\n"))
    return np.array(vertices), np.array(indices, dtype=np.int64), lines


def verify_controls_obj(obj_path: Path, controls: list[dict], cockpit: Gltf) -> dict:
    """Compare each control's OBJ triangle bounds with its glTF mesh (both in OBJ space)."""
    if not obj_path.exists():
        return {"available": False, "path": str(obj_path)}
    vertices, indices, lines = parse_obj_vertices(obj_path)
    label_to_control = {c["label"]: c for c in controls
                        if not c.get("command_push") and not c.get("rest_rotation")}
    residuals = {}
    pending = None
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("ATTR_manip_") and stripped != "ATTR_manip_none":
            for label, control in label_to_control.items():
                if stripped.endswith("\t" + label):
                    pending = control
                    break
            else:
                pending = None
        elif stripped.startswith("TRIS") and pending is not None:
            _, offset, count = stripped.split()[:3]
            used = np.unique(indices[int(offset):int(offset) + int(count)])
            node_index = cockpit.by_name.get(pending["node"])
            centre = cockpit.mesh_center(node_index) if node_index is not None else None
            if used.size and centre is not None and pending["node"] not in residuals:
                points = vertices[used]
                obj_centre = (points.min(0) + points.max(0)) / 2
                residuals[pending["node"]] = float(np.linalg.norm(obj_centre - centre))
            pending = None
    values = sorted(residuals.values())
    return {
        "available": True, "path": str(obj_path), "controls_compared": len(values),
        "median_centre_residual_m": round(statistics.median(values), 5) if values else None,
        "p90_centre_residual_m": round(values[int(0.9 * (len(values) - 1))], 5) if values else None,
        "within_5mm": sum(v < 0.005 for v in values),
        "examples_m": {k: round(residuals[k], 5) for k in ("LEVER_LANDINGGEAR", "LEVER_THROTTLE_1_CLICK",
                                                            "KNOB_SWITCHING_BRK") if k in residuals},
        "note": ("OBJ triangle-AABB centre vs transformed glTF accessor-box centre, both in OBJ space; "
                 "small residuals confirm OBJ == glTF world coordinates (no axis change)."),
    }


def verify_blender_obj_pivots(obj_path: Path, gltf: Gltf) -> dict:
    """Static ANIM_trans pivots of a Blender-exported OBJ vs glTF node origins."""
    if not obj_path.exists():
        return {"available": False, "path": str(obj_path)}
    pivots = set()
    with obj_path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            parts = line.split()
            if len(parts) >= 7 and parts[0] == "ANIM_trans" and parts[1:4] == parts[4:7]:
                pivots.add(tuple(float(v) for v in parts[1:4]))
    if not pivots:
        return {"available": True, "path": str(obj_path), "pivots": 0}
    pivots_arr = np.array(sorted(pivots))
    origins = np.array([gltf.origin(i) for i in range(len(gltf.nodes))]) - gltf.attach[:3, 3]
    result = {"available": True, "path": str(obj_path), "gltf": gltf.path.name, "pivots": len(pivots_arr)}
    for label, sign in (("identity", np.array([1, 1, 1])), ("if_rotated_180", np.array([-1, 1, -1]))):
        distances = np.linalg.norm((pivots_arr * sign)[:, None, :] - origins[None, :, :], axis=2).min(1)
        result[label] = {"median_m": round(float(np.median(distances)), 5),
                         "within_1mm": int((distances < 1e-3).sum())}
    return result


def deployed_tyres(package: Path, conv: Conversion) -> dict:
    """Evaluate tyre OBJs at deploy_ratio=1 with the port's OBJ8 evaluator."""
    sys.path.insert(0, str(PORT_ROOT / "blender"))
    try:
        from obj8_preview import parse_obj8  # type: ignore
    except Exception as error:  # pragma: no cover
        return {"error": f"obj8_preview unavailable: {error}"}
    result = {}
    for gear_index, group, label in ((0, "gear-nose", "nose"), (1, "gear-wing-lh", "wing_left"),
                                     (2, "gear-wing-rh", "wing_right"), (3, "gear-body-lh", "body_left"),
                                     (4, "gear-body-rh", "body_right")):
        rel = f"exterior/{group}/{group.replace('-', '_')}_a380_exterior_tyres.obj"
        path = package / "objects" / rel
        if not path.exists():
            continue
        mesh = parse_obj8(path.read_text(encoding="utf-8", errors="replace").splitlines(),
                          values={f"sim/flightmodel2/gear/deploy_ratio[{gear_index}]": 1.0})
        positions = np.frombuffer(mesh.positions, dtype=np.float64).reshape(-1, 3)
        indices = np.frombuffer(mesh.indices, dtype=np.uint32)
        low, high = np.full(3, np.inf), np.full(3, -np.inf)
        for draw in mesh.draws:
            used = np.unique(indices[draw.start:draw.start + draw.count])
            matrix = np.array(draw.matrix, dtype=float).reshape(4, 4)
            points = positions[used] @ matrix[:3, :3].T + matrix[:3, 3]
            low, high = np.minimum(low, points.min(0)), np.maximum(high, points.max(0))
        offset = conv.obj_offset_ft(rel)
        centre_obj = (low + high) / 2
        bottom_obj = np.array([centre_obj[0], low[1], centre_obj[2]])
        centre = conv.to_xp(centre_obj, offset)
        bottom = conv.to_xp(bottom_obj, offset)
        result[label] = {"obj": rel, "attachment_offset_ft": list(offset),
                         "tyre_group_centre": {"x": round(centre[0], 4), "y": round(centre[1], 4),
                                               "z": round(centre[2], 4), "cg_relative": conv.cg_relative(centre)},
                         "tyre_bottom_y": round(float(bottom[1]), 4),
                         "obj_bounds_m": [r4(low), r4(high)]}
    return result


# --------------------------------------------------------------------------- main build

def node_entry(index: NodeIndex, conv: Conversion, name: str, prefer: str | None = None) -> dict:
    hits = index.lookup(name, prefer)
    if not hits:
        return {"found": False, "x": None, "y": None, "z": None}
    gltf, node_index = hits[0]
    entry = {"found": True, **xp_entry(conv, gltf.origin(node_index)), "source_gltf": index.rel(gltf),
             "occurrences": len(hits)}
    if len(hits) > 1:
        spread = max(float(np.linalg.norm(g.origin(i) - gltf.origin(node_index))) for g, i in hits)
        entry["occurrence_spread_m"] = round(spread, 4)
    centre = gltf.mesh_center(node_index)
    if centre is not None:
        mesh_xp = conv.to_xp(centre)
        entry["mesh_center"] = r4(mesh_xp)
    return entry


def build(args) -> dict:
    upstream, large = args.upstream.resolve(), args.large_files.resolve()
    aircraft = upstream / AIRCRAFT_REL
    acf_path = args.acf.resolve()
    package = acf_path.parent
    acf = read_acf(acf_path)
    conv = Conversion(acf)

    gltfs, attach_notes = build_model_registry(upstream, large)
    index = NodeIndex(gltfs, large)
    cockpit = next(g for g in gltfs if g.path.name == Path(COCKPIT_LOD00).name)

    conversion = conv.describe()
    conversion["msfs_attachments"] = attach_notes
    conversion["msfs_attachments_note"] = (
        "All MSFS function/part attachments resolve to identity transforms (ATTACH_CENTER, "
        "ATTACH_COCKPIT and ATTACH_CABIN carry no translation/rotation), so every part shares the "
        "aircraft origin." if attach_notes["all_attachments_identity"] else
        "Some MSFS attachments are not identity; their transforms were composed into world positions.")
    conversion["msfs_reference_datum"] = (
        "flight_model.cfg reference_datum_position = 0,0,0: glTF origin == MSFS datum. "
        "Nose radome contact point (115.108 ft fwd) defines the X-Plane reference point.")

    # ---- Part A: sound.xml nodes
    local_var_sounds: dict[str, list[dict]] = {}
    common_nodes, common_anim = parse_sound_xml(aircraft / COMMON_SOUND_XML, COMMON_SOUND_XML, local_var_sounds)
    cockpit_nodes, cockpit_anim = parse_sound_xml(aircraft / COCKPIT_SOUND_XML, COCKPIT_SOUND_XML,
                                                  local_var_sounds)
    declared = {e.lower() for e in common_anim + cockpit_anim}
    declared |= {e.lower() for n in (common_nodes, cockpit_nodes) for v in n.values() for e in v["events"]}
    all_sound_nodes: dict[str, dict] = {}
    for source in (common_nodes, cockpit_nodes):
        for name, info in source.items():
            entry = all_sound_nodes.setdefault(name, {"events": [], "files": set(), "viewpoints": set()})
            entry["events"] += [e for e in info["events"] if e not in entry["events"]]
            entry["files"] |= info["files"]
            entry["viewpoints"] |= info["viewpoints"]

    # ---- engines
    engines = []
    msfs_engines = []
    engines_cfg = read_cfg(aircraft / "common/config/engines.cfg")
    for section in engines_cfg.sections():
        for key, value in engines_cfg[section].items():
            if re.fullmatch(r"Engine\.\d+", key):
                lon, lat, vert = (float(v) for v in value.split(";")[0].split(",")[:3])
                msfs_engines.append((int(key.split(".")[1]), lon, lat, vert))
    msfs_engines.sort()
    exhaust_nodes = [(name, hits[0]) for name, hits in index.names.items() if name.startswith("FX_EXHAUST_")
                     and name != "FX_EXHAUST_APU" and hits]
    for number in range(1, 5):
        engine = {"engine": number, "acf_index": number - 1}
        sound = node_entry(index, conv, f"SOUND_ENGINE_{number}")
        engine["sound_emitter_SOUND_ENGINE"] = sound
        if sound.get("found"):
            engine["recommended_emitter"] = {"x": sound["x"], "y": sound["y"], "z": sound["z"],
                                             "basis": "FBW sound.xml emitter SOUND_ENGINE_n (intake lip)"}
        fan_hits = index.lookup(f"FAN_BLADE_STILL_{number}")
        spinner_hits = index.lookup(f"SPINNER_STILL_{number}")
        if fan_hits and spinner_hits:
            fan_g, fan_i = fan_hits[0]
            spin_g, spin_i = spinner_hits[0]
            fan_c = fan_g.mesh_center(fan_i)
            spin_low, spin_high = spin_g.mesh_bounds(spin_i)
            axis = (spin_low + spin_high) / 2
            spinner_tip = np.array([axis[0], axis[1], spin_high[2]])
            engine["fan_face_center"] = xp_entry(conv, fan_c)
            engine["spinner_tip"] = xp_entry(conv, spinner_tip)
            # nearest FX exhaust node laterally
            best = min(exhaust_nodes, key=lambda item: abs(item[1][0].origin(item[1][1])[0] - axis[0]))
            ex_g, ex_i = best[1]
            engine["exhaust_FX_node"] = best[0]
            engine["exhaust"] = xp_entry(conv, ex_g.origin(ex_i))
            exhaust_z = ex_g.origin(ex_i)[2]
            nacelle_mid = np.array([axis[0], axis[1], (spinner_tip[2] + exhaust_z) / 2])
            engine["nacelle_axis_mid"] = xp_entry(conv, nacelle_mid)
            contrail = index.lookup(f"VFX_Contrails_Engine_{number}")
            if contrail:
                engine["contrail_FX_node"] = xp_entry(conv, contrail[0][0].origin(contrail[0][1]))
        acf_i = number - 1
        if f"_engn/{acf_i}/_part_x" in acf:
            acf_m = np.array([float(acf[f"_engn/{acf_i}/_part_{a}"]) for a in "xyz"]) * FT
            engine["acf_engine_part_m"] = r4(acf_m)
            if sound.get("found"):
                engine["acf_engine_minus_fbw_sound_emitter_m"] = r4(acf_m - np.array([sound[k] for k in "xyz"]))
            engine["acf_engine_part_ft"] = [float(acf[f"_engn/{acf_i}/_part_{a}"]) for a in "xyz"]
            if "nacelle_axis_mid" in engine:
                fan = np.array([engine["fan_face_center"][k] for k in "xyz"])
                exhaust = np.array([engine["exhaust"][k] for k in "xyz"])
                lo_z, hi_z = sorted((fan[2], exhaust[2]))
                engine["acf_vs_model"] = {
                    "lateral_residual_m": round(float(acf_m[0] - fan[0]), 4),
                    "vertical_residual_m": round(float(acf_m[1] - fan[1]), 4),
                    "acf_z_minus_fan_face_m": round(float(acf_m[2] - fan[2]), 4),
                    "acf_z_within_fan_to_exhaust": bool(lo_z - 0.5 <= acf_m[2] <= hi_z + 0.5),
                }
        if acf_i < len(msfs_engines):
            _, lon, lat, vert = msfs_engines[acf_i]
            engine["msfs_engines_cfg_thrust_point"] = {**xp_entry(conv, msfs_cfg_to_gltf(lon, lat, vert)),
                                                       "msfs_ft": [lon, lat, vert]}
        engines.append(engine)

    # ---- APU
    apu = {"FX_EXHAUST_APU": node_entry(index, conv, "FX_EXHAUST_APU")}
    for name in ("APU_OUTLET1", "APU_NACELLE", "APU EXHAUST CYLINDER.001"):
        hits = index.lookup(name)
        if hits and hits[0][0].mesh_center(hits[0][1]) is not None:
            apu[f"{name}_mesh_center"] = xp_entry(conv, hits[0][0].mesh_center(hits[0][1]))
    apu["acf"] = "X-Plane ACF has no APU part position (only APU timing/fuel fields)."
    apu["recommended_emitter"] = {k: apu["FX_EXHAUST_APU"][k] for k in ("x", "y", "z")} \
        if apu["FX_EXHAUST_APU"].get("found") else None

    # ---- gear
    flight_model = read_cfg(aircraft / "common/config/flight_model.cfg")
    contact = {}
    for key, value in flight_model["CONTACT_POINTS"].items():
        if re.fullmatch(r"point\.\d+", key):
            fields = [v.strip() for v in value.split(";")[0].split(",")]
            if fields[0] == "1":
                contact[int(key.split(".")[1])] = [float(v) for v in fields[1:4]] + [float(fields[6])]
    msfs_gear = {"nose": 0, "body_left": 1, "body_right": 2, "wing_left": 3, "wing_right": 4}
    acf_gear = {"nose": 0, "wing_left": 1, "wing_right": 2, "body_left": 3, "body_right": 4}
    tyres = deployed_tyres(package, conv) if not args.skip_obj_gear else {}
    gear = {}
    for label in ("nose", "wing_left", "wing_right", "body_left", "body_right"):
        item = {}
        point = contact.get(msfs_gear[label])
        if point:
            lon, lat, vert, radius = point
            item["msfs_contact_point"] = {**xp_entry(conv, msfs_cfg_to_gltf(lon, lat, vert)),
                                          "msfs_ft": [lon, lat, vert], "wheel_radius_ft": radius}
            item["msfs_wheel_centre"] = xp_entry(conv, msfs_cfg_to_gltf(lon, lat, vert + radius))
        gi = acf_gear[label]
        if f"_gear/{gi}/_gear_x" in acf:
            g = lambda n: float(acf[f"_gear/{gi}/_{n}"])  # noqa: E731
            lat_e, lon_e = math.radians(g("latE")), math.radians(g("lonE"))
            leg = g("leg_len")
            attach = np.array([g("gear_x"), g("gear_y"), g("gear_z")])
            centre = np.array([attach[0] + leg * math.sin(lat_e),
                               attach[1] - leg * math.cos(lat_e) * math.cos(lon_e),
                               attach[2] + leg * math.sin(lon_e)])
            item["acf_gear"] = {"index": gi, "type": int(g("gear_type")), "attach_m": r4(attach * FT),
                                "extended_tyre_centre_m": r4(centre * FT),
                                "extended_contact_m": r4((centre - [0, g("tire_radius"), 0]) * FT),
                                "leg_len_ft": leg, "tire_radius_ft": g("tire_radius")}
        if label in tyres:
            item["obj_deployed_tyres"] = tyres[label]
            t = tyres[label]["tyre_group_centre"]
            item["recommended_emitter"] = {"x": t["x"], "y": t["y"], "z": t["z"],
                                           "basis": "deployed tyre-group centre from the X-Plane OBJ"}
            if "acf_gear" in item:
                c = item["acf_gear"]["extended_tyre_centre_m"]
                item["acf_vs_obj_residual_m"] = r4(np.array(c) - np.array([t["x"], t["y"], t["z"]]))
        elif "msfs_wheel_centre" in item:
            w = item["msfs_wheel_centre"]
            item["recommended_emitter"] = {"x": w["x"], "y": w["y"], "z": w["z"],
                                           "basis": "MSFS contact point + wheel radius"}
        gear[label] = item
    gear["note"] = ("glTF gear-wheel nodes are in their authored rest pose (not deployed; LH/RH differ), "
                    "so gear positions use the MSFS contact points, the ACF gear and the deployed tyre OBJs.")

    # ---- sound.xml node table
    def resolve_substitute(name: str) -> dict | None:
        if name not in SUBSTITUTES:
            return None
        target, why = SUBSTITUTES[name]
        kind, ref = target.split(":", 1)
        out = {"target": target, "reason": why, "heuristic": True}
        if kind == "gear":
            emitter = gear.get(ref, {}).get("recommended_emitter")
            if emitter:
                out.update({k: emitter[k] for k in ("x", "y", "z")})
        elif kind in ("node", "mesh"):
            hits = index.lookup(ref)
            if hits:
                g, i = hits[0]
                point = g.mesh_center(i) if kind == "mesh" else g.origin(i)
                if point is not None:
                    out.update({k: v for k, v in xp_entry(conv, point).items() if k in ("x", "y", "z")})
                    out["source_gltf"] = index.rel(g)
        return out

    origin_xp = conv.to_xp((0.0, 0.0, 0.0))
    sound_xml_nodes = {}
    for name in sorted(all_sound_nodes):
        info = all_sound_nodes[name]
        entry = node_entry(index, conv, name, prefer="LOD00")
        entry["events"] = info["events"]
        entry["sound_xml"] = sorted(info["files"])
        entry["viewpoints"] = sorted(info["viewpoints"])
        if not entry["found"]:
            entry["source_gltf"] = None
            entry["msfs_fallback_assumption"] = {"x": round(origin_xp[0], 4), "y": round(origin_xp[1], 4),
                                                 "z": round(origin_xp[2], 4),
                                                 "note": "missing NodeName assumed to fall back to the model origin"}
            entry["substitute"] = resolve_substitute(name)
        sound_xml_nodes[name] = entry

    # ---- verification
    verification = {}
    camera_cfg = read_cfg(aircraft / "common/config/cameras.cfg")
    eye_lon, eye_lat, eye_vert = (float(v) for v in camera_cfg["VIEWS"]["eyepoint"].split(",")[:3])
    eye_ref = msfs_cfg_to_gltf(eye_lon, eye_lat, eye_vert)
    cameras = {}
    for section in camera_cfg.sections():
        if section.startswith("CAMERADEFINITION"):
            title = unquote(camera_cfg[section].get("Title", ""))
            if title in ("Pilot", "CoPilot") and "InitialXyz" in camera_cfg[section]:
                ix, iy, iz = (float(v) for v in camera_cfg[section]["InitialXyz"].split(",")[:3])
                # InitialXyz: metres, +x right, +y up, +z forward, relative to the eyepoint
                cameras[title] = eye_ref + np.array([-ix, iy, iz])
    eye_checks = {}
    for title, key in (("Pilot", "acf/_pe_xyz"), ("CoPilot", "acf/_pe_xyz_copilot")):
        if title in cameras and f"{key}/0" in acf:
            acf_eye = np.array([float(acf[f"{key}/{i}"]) for i in range(3)]) * FT
            derived = conv.to_xp(cameras[title])
            eye_checks[title] = {"msfs_camera_gltf": r4(cameras[title]), "derived_xp_m": r4(derived),
                                 "acf_pe_xyz_m": r4(acf_eye), "residual_m": r4(acf_eye - derived),
                                 "residual_norm_m": round(float(np.linalg.norm(acf_eye - derived)), 4)}
    verification["pilot_eye"] = eye_checks
    verification["engines_acf_vs_model"] = [
        {"engine": e["engine"], **e.get("acf_vs_model", {})} for e in engines]
    # nose tip: forward-most radome/fuselage geometry should map to z_xp ~ 0
    fuselage = next((g for g in gltfs if g.path.name == "A380_FUSELAGE.gltf"), None)
    if fuselage is not None:
        best, best_name = None, None
        for i, node in enumerate(fuselage.nodes):
            if "BOUNDS" in node.get("name", "").upper():
                continue  # MSFS culling/bounds helper meshes are not geometry
            bounds = fuselage.mesh_bounds(i)
            if bounds is not None and (best is None or bounds[1][2] > best):
                best, best_name = bounds[1][2], node.get("name")
        radome = msfs_cfg_to_gltf(115.108, 0.0, 2.452346)  # flight_model.cfg point.13 "Nose radome"
        verification["nose_tip"] = {"forward_most_mesh": best_name, "max_gltf_z_m": round(float(best), 4),
                                    "xp_z_m": round(float(conv.to_xp((0, 0, best))[2]), 4),
                                    "msfs_nose_radome_contact_xp_m": r4(conv.to_xp(radome)),
                                    "note": "expect ~0: the ACF reference point is the nose (MSFS 115.108 ft fwd)"}
    controls = json.loads(args.controls.read_text(encoding="utf-8"))
    verification["controls_obj_vs_gltf"] = verify_controls_obj(package / CONTROLS_OBJ, controls, cockpit)
    blender_checks = []
    wings = next((g for g in gltfs if g.path.name == "A380_WINGS.gltf"), None)
    nose = next((g for g in gltfs if g.path.name == "A380_GEAR_NOSE.gltf"), None)
    for rel, gltf in (("objects/interior/cockpit/cockpit_a380_cockpit_floor_4k.obj", cockpit),
                      ("objects/exterior/gear-nose/gear_nose_a380_exterior_nlg.obj", nose),
                      ("objects/exterior/wings/a380x_fans.obj", wings)):
        if gltf is not None:
            blender_checks.append(verify_blender_obj_pivots(package / rel, gltf))
    verification["blender_obj_pivots_vs_gltf"] = blender_checks
    verification["gear_acf_vs_obj"] = {k: v.get("acf_vs_obj_residual_m") for k, v in gear.items()
                                       if isinstance(v, dict)}
    conversion["verification"] = verification

    # ---- Part B: behaviour expansion
    expander = BehaviourExpander(aircraft / COCKPIT_BEHAVIOUR, upstream / MODEL_BEHAVIOR_DEFS_REL)
    expander.expand()
    explicit_by_terminal = collections.defaultdict(collections.Counter)
    for record in expander.records:
        if record["wwise_event_1"] or record["wwise_event_2"] or record["wwise_event"]:
            pair = (record["wwise_event_1"] or record["wwise_event"], record["wwise_event_2"] or record["wwise_event"])
            explicit_by_terminal[record["terminal_template"]][f"{pair[0]}/{pair[1]}"] += 1
    by_node: dict[str, list[dict]] = collections.defaultdict(list)
    for record in expander.records:
        if record["node"]:
            by_node[record["node"]].append(record)

    def best_binding(records: list[dict]) -> dict:
        def key(record):
            has_events = bool(record["wwise_event_1"] or record["wwise_event_2"] or record["wwise_event"])
            return (0 if has_events else 1, template_rank(record["terminal_template"]))
        return sorted(records, key=key)[0]

    def binding(records: list[dict]) -> dict:
        best = best_binding(records)
        events = summarise_events(best, declared)
        if events["events_source"] == "asobo_default":
            inferred = explicit_by_terminal.get(best["terminal_template"])
            events["inferred_default_from_fbw_usage"] = dict(inferred.most_common(3)) if inferred else None
        variants = sorted({(r["terminal_template"], r["wwise_event_1"] or r["wwise_event"],
                            r["wwise_event_2"] or r["wwise_event"]) for r in records}, key=str)
        return {**events, "template": best["terminal_template"], "template_chain": best["template_chain"],
                "component_path": best["component_path"], "node_resolution": best["node_resolution"],
                "anim_name": best["anim_name"], "part_id": best["part_id"],
                "variants": [list(v) for v in variants]}

    behaviour_nodes = {node: binding(records) for node, records in sorted(by_node.items())}

    alias: dict[str, list[dict]] = collections.defaultdict(list)
    for node, records in by_node.items():
        for record in records:
            for key in ("anim_name", "part_id", "node_id_param", "component_node"):
                value = record.get(key)
                if value and not TOKEN.search(value) and value not in by_node:
                    alias[value].append(record)

    def candidates(name: str) -> list[str]:
        out = [name]
        base = re.sub(r"\.\d+$", "", name)
        for suffix in ("_CLICK", "_SEQ1", "_SEQ2", "_SWITCH", "_PUSH", "_PULL", "_REVERSE", "_KNOB",
                       "_HANDLE", "_GUARD", "_COVER", "_LOCK"):
            if base.endswith(suffix):
                out.append(base[: -len(suffix)])
        out.append(base)
        return list(dict.fromkeys(out))

    controls_out = []
    matched_behaviour = set()
    for control in controls:
        node = control["node"]
        row = {"node": node, "label": control.get("label")}
        for key in ("command", "command_up", "command_down", "command_push", "command_pull", "dataref",
                    "toggle_values", "axis", "value_min", "value_max"):
            if control.get(key) is not None:
                row[key] = control[key]
        match_node, match_kind, bound = None, None, None
        for candidate in candidates(node):
            if candidate in behaviour_nodes:
                match_node, match_kind = candidate, "node" if candidate == node else "normalized_node"
                bound = behaviour_nodes[candidate]
                break
            if candidate in alias:
                match_node, match_kind = alias[candidate][0]["node"], "anim_or_part_alias"
                bound = binding(alias[candidate])
                break
        if match_node is None and CONTROL_BEHAVIOUR_ALIASES.get(node) in behaviour_nodes:
            match_node = CONTROL_BEHAVIOUR_ALIASES[node]
            match_kind = "curated_alias (FBW node absent from A380 glTF; binding is inert in MSFS)"
            bound = behaviour_nodes[match_node]
        if match_node:
            binding_row = bound
            matched_behaviour.add(match_node)
            row.update({"behaviour_node": match_node, "match": match_kind,
                        "events_on": binding_row["events_on"], "events_off": binding_row["events_off"],
                        "events_source": binding_row["events_source"], "template": binding_row["template"],
                        "template_chain": binding_row["template_chain"]})
            if "inferred_default_from_fbw_usage" in binding_row:
                row["inferred_default_from_fbw_usage"] = binding_row["inferred_default_from_fbw_usage"]
        else:
            row.update({"behaviour_node": None, "match": None, "events_on": None, "events_off": None,
                        "events_source": None, "template": None})
        sound_xml_hits = [c for c in candidates(node) if c in all_sound_nodes]
        if sound_xml_hits:
            row["sound_xml_node"] = sound_xml_hits[0]
            row["sound_xml_events"] = all_sound_nodes[sound_xml_hits[0]]["events"]
        linked_vars = list(candidates(node))
        number = re.search(r"_(\d)_", node + "_")
        for pattern, variables in CONTROL_LOCALVAR_LINKS.items():
            concrete = pattern.replace("{n}", number.group(1)) if number else pattern
            if concrete == node:
                linked_vars += [v.replace("{n}", number.group(1)) if number else v for v in variables]
        linked = [dict(item) for var in dict.fromkeys(linked_vars) for item in local_var_sounds.get(var, [])]
        if linked:
            seen = set()
            row["sound_xml_linked_events"] = [item for item in linked
                                              if (item["event"], item["emitter_node"], item["local_var"]) not in seen
                                              and not seen.add((item["event"], item["emitter_node"], item["local_var"]))]
        hits = index.lookup(node, prefer="LOD00")
        if hits:
            g, i = hits[0]
            centre = g.mesh_center(i)
            point = centre if centre is not None else g.origin(i)
            xp = conv.to_xp(point)
            row.update({"x": round(float(xp[0]), 4), "y": round(float(xp[1]), 4), "z": round(float(xp[2]), 4),
                        "position_basis": "mesh_center" if centre is not None else "node_origin",
                        "node_origin": r4(conv.to_xp(g.origin(i))), "cg_relative": conv.cg_relative(xp),
                        "source_gltf": index.rel(g), "found": True})
        else:
            row.update({"x": None, "y": None, "z": None, "found": False})
        controls_out.append(row)

    unmapped = []
    for node, binding_info in behaviour_nodes.items():
        if node in matched_behaviour:
            continue
        entry = {"node": node, "events_on": binding_info["events_on"], "events_off": binding_info["events_off"],
                 "events_source": binding_info["events_source"], "template": binding_info["template"],
                 "component_path": binding_info["component_path"],
                 "template_chain": binding_info["template_chain"],
                 "node_resolution": binding_info["node_resolution"],
                 "events_declared_in_sound_xml": binding_info["events_declared_in_sound_xml"]}
        if len(binding_info["variants"]) > 1:
            entry["variants"] = binding_info["variants"]
        hits = index.lookup(node, prefer="LOD00")
        if hits:
            g, i = hits[0]
            centre = g.mesh_center(i)
            xp = conv.to_xp(centre if centre is not None else g.origin(i))
            entry.update({"x": round(float(xp[0]), 4), "y": round(float(xp[1]), 4), "z": round(float(xp[2]), 4),
                          "found": True})
        else:
            entry.update({"x": None, "y": None, "z": None, "found": False})
        unmapped.append(entry)

    behaviour_meta = {
        "entry": COCKPIT_BEHAVIOUR, "files": expander.files,
        "templates_defined": len(expander.templates),
        "duplicate_templates": {k: v for k, v in expander.duplicates.items() if v},
        "missing_includes_asobo_sdk": sorted(set(expander.missing_includes)),
        "terminal_interaction_records": len(expander.records),
        "sound_bound_nodes": len(behaviour_nodes),
        "asobo_terminal_templates": dict(expander.terminals.most_common()),
        "explicit_events_by_asobo_template": {k: dict(v) for k, v in explicit_by_terminal.items()},
        "declared_animation_sounds": sorted(set(cockpit_anim)),
        "note": ("events_on = WWISE_EVENT_1 (or WWISE_EVENT), events_off = WWISE_EVENT_2 (or WWISE_EVENT). "
                 "'asobo_default:<template>' means FBW passes no event and the Asobo SDK template default "
                 "applies (SDK templates are not in this tree)."),
    }

    found = sum(1 for v in sound_xml_nodes.values() if v["found"])
    return {
        "schema_version": SCHEMA_VERSION,
        "generator": "tools/sound_emitter_positions.py",
        "inputs": {"upstream_aircraft": str(upstream), "upstream_large_files": str(large),
                   "acf": str(acf_path), "cockpit_controls": str(args.controls.resolve())},
        "conversion": conversion,
        "sound_xml_nodes": sound_xml_nodes,
        "engines": engines,
        "apu": apu,
        "gear": gear,
        "controls": controls_out,
        "unmapped_behaviour_nodes": unmapped,
        "behaviour": behaviour_meta,
        "counts": {
            "sound_xml_nodes": len(sound_xml_nodes), "sound_xml_nodes_found": found,
            "sound_xml_nodes_missing": len(sound_xml_nodes) - found,
            "controls": len(controls_out),
            "controls_with_behaviour_events": sum(1 for c in controls_out if c["events_on"]),
            "controls_with_explicit_events": sum(1 for c in controls_out if c["events_source"]
                                                 and c["events_source"] != "asobo_default"),
            "controls_with_sound_xml_events": sum(1 for c in controls_out if c.get("sound_xml_events")),
            "controls_without_events": sum(1 for c in controls_out if not c["events_on"]
                                           and not c.get("sound_xml_events") and not c.get("sound_xml_linked_events")),
            "controls_with_position": sum(1 for c in controls_out if c["found"]),
            "controls_without_position": sum(1 for c in controls_out if not c["found"]),
            "controls_with_sound_xml_linked_events": sum(1 for c in controls_out if c.get("sound_xml_linked_events")),
            "controls_with_any_fbw_sound": sum(1 for c in controls_out if c["events_on"] or c.get("sound_xml_events")
                                               or c.get("sound_xml_linked_events")),
            "behaviour_sound_nodes": len(behaviour_nodes),
            "behaviour_sound_nodes_in_gltf": sum(1 for n in behaviour_nodes if index.lookup(n)),
            "unmapped_behaviour_nodes": len(unmapped),
            "unmapped_behaviour_nodes_in_gltf": sum(1 for u in unmapped if u["found"]),
            "unmapped_behaviour_nodes_absent_from_gltf": sum(1 for u in unmapped if not u["found"]),
        },
    }


def print_summary(data: dict, output: Path) -> None:
    c = data["counts"]
    conv = data["conversion"]
    print(f"Conversion: {conv['formula']}")
    for title, check in conv["verification"]["pilot_eye"].items():
        print(f"  eye check {title}: residual {check['residual_m']} m (|r|={check['residual_norm_m']})")
    for row in conv["verification"]["engines_acf_vs_model"]:
        if row.get("lateral_residual_m") is not None:
            print(f"  engine {row['engine']} ACF-vs-model lateral {row['lateral_residual_m']:+.3f} m, "
                  f"vertical {row['vertical_residual_m']:+.3f} m")
    obj = conv["verification"]["controls_obj_vs_gltf"]
    if obj.get("available"):
        print(f"  controls OBJ vs glTF: {obj['controls_compared']} compared, median "
              f"{obj['median_centre_residual_m']} m")
    nose = conv["verification"].get("nose_tip")
    if nose:
        print(f"  nose tip ({nose['forward_most_mesh']}) z_xp = {nose['xp_z_m']} m")
    print(f"sound.xml nodes: {c['sound_xml_nodes_found']} found / {c['sound_xml_nodes_missing']} missing")
    print(f"controls: {c['controls']} total, {c['controls_with_behaviour_events']} with behaviour events "
          f"({c['controls_with_explicit_events']} explicit), {c['controls_with_sound_xml_events']} with sound.xml "
          f"events, {c['controls_with_sound_xml_linked_events']} with sound.xml LocalVar-linked events, "
          f"{c['controls_without_events']} without any; {c['controls_with_position']} positioned, "
          f"{c['controls_without_position']} without position")
    print(f"behaviour sound nodes: {c['behaviour_sound_nodes']} ({c['behaviour_sound_nodes_in_gltf']} exist in the "
          f"A380 glTF); not used by port controls: {c['unmapped_behaviour_nodes_in_gltf']} in glTF + "
          f"{c['unmapped_behaviour_nodes_absent_from_gltf']} absent (inert legacy bindings)")
    for engine in data["engines"]:
        s = engine["sound_emitter_SOUND_ENGINE"]
        if s.get("found"):
            print(f"  engine {engine['engine']}: SOUND_ENGINE ({s['x']:.2f}, {s['y']:.2f}, {s['z']:.2f}) m")
    apu = data["apu"]["FX_EXHAUST_APU"]
    if apu.get("found"):
        print(f"  APU exhaust: ({apu['x']:.2f}, {apu['y']:.2f}, {apu['z']:.2f}) m")
    print(f"wrote {output}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--upstream", type=Path, default=PORT_ROOT.parent / "upstream-aircraft")
    parser.add_argument("--large-files", type=Path, default=PORT_ROOT.parent / "upstream-aircraft-large-files")
    parser.add_argument("--acf", type=Path, default=PORT_ROOT / "output/A380X-XP12/A380X-XP12.acf")
    parser.add_argument("--controls", type=Path, default=PORT_ROOT / "config/cockpit_controls.json")
    parser.add_argument("--output", type=Path, default=PORT_ROOT / "build/fbw-sounds-stage/emitters.json")
    parser.add_argument("--skip-obj-gear", action="store_true",
                        help="do not evaluate the deployed tyre OBJs (faster; gear falls back to MSFS contacts)")
    args = parser.parse_args()
    data = build(args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, indent=1, sort_keys=False) + "\n", encoding="utf-8")
    print_summary(data, args.output)


if __name__ == "__main__":
    main()
