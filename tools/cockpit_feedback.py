#!/usr/bin/env python3
"""Motion, click targets and lamps for the FBW cockpit controls the port left static.

Driven by tools/cockpit_feedback_contract.py (SWITCHING knobs, ECAM control panel keys, SURV panel,
KCCU key caps, ENG MASTER lever decals, glareshield side switches). For every control:

* panel OBJs (objects/interior/cockpit-panels, objects/interior/cockpit): the visible FBW meshes of the
  animated node's subtree are wrapped in the authored clip (translation and/or single-axis rotation,
  keyed on the control's dataref). Only TRIS ranges that are not animated yet are wrapped (ANIM depth
  0), and VT / IDX / UV / texture payloads stay byte-identical (checked). Lamp faces that live in the
  emissive decal object get their own ATTR_light_level and are hidden while dark.
* a380x_cockpit_controls.obj (append-only, after MARKER): draw-disabled click boxes that move with the
  control, and lit quads (fcu_labels atlas cell) over lamp faces whose decal has no LIT texture.

Re-running on a patched aircraft is a no-op. X-Plane must be closed while the installed aircraft is
patched; --aircraft may also be the repository copy (output/A380X-XP12).

    python3 tools/cockpit_feedback.py --aircraft "<X-Plane>/Aircraft/A380X-XP12" \\
        --source ~/Developer/fbw-cockpit-source/a380-cockpit/A380_COCKPIT_LOD00.gltf [--dry-run]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path

from build_cockpit_controls import (FCU_LABEL_COLUMNS, FCU_LABEL_ROWS, accessor_values, load_buffer,
                                    local_matrix, matrix_identity, matrix_multiply, transform_point)
from cockpit_feedback_contract import COMMANDS, CONTROLS, DATAREFS, Control, click_manipulator, hooked_commands
from obj8_numbers import fmt as obj8_fmt
from patch_center_right_animations import rotation_keys, transform_vector
from patch_efis_animations import vertex_payload

MARKER = "# A380X FEEDBACK V1"
CONTROLS_OBJ = "objects/interior/cockpit-controls/a380x_cockpit_controls.obj"
PROVIDER_MANIFEST = Path("avionics-providers/fbw-avionics-v1.json")
PANEL_DIRS = ("objects/interior/cockpit-panels", "objects/interior/cockpit")
EYE = (0.0, 3.2, 31.9)          # between the two seats (OBJ coordinates), lamps face this way
LAMP_LIFT = 0.00006             # m in front of the authored face
CLICK_MARGIN = 0.0005
TOLERANCE = 0.000012            # vertex match, as patch_center_right_animations
LEGEND_LIFT = 0.002             # the exported legend decals sit 2 mm out along their face normal
LIFT_TOLERANCE = 0.0003         # curved legend faces: per-vertex normals differ slightly from the face
DEFAULT_SOURCE = Path.home() / "Developer/fbw-cockpit-source/a380-cockpit/A380_COCKPIT_LOD00.gltf"


def _sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def _unit(v):
    length = math.sqrt(_dot(v, v)) or 1.0
    return tuple(x / length for x in v)


def _vec(values):
    return "\t".join(obj8_fmt(v) for v in values)


class Source:
    """The FBW cockpit glTF: world matrices, clips and primitive geometry."""

    def __init__(self, path: Path):
        self.path = path
        self.doc = json.loads(path.read_text())
        self.buffers = [load_buffer(path, row["uri"]) for row in self.doc["buffers"]]
        self.nodes = self.doc["nodes"]
        self.names = {node["name"]: i for i, node in enumerate(self.nodes) if "name" in node}
        self.parents = {child: i for i, node in enumerate(self.nodes) for child in node.get("children", [])}
        self.clips = {a["name"].strip(): a for a in self.doc["animations"]}
        self._world = {}

    def world(self, index):
        if index not in self._world:
            local = local_matrix(self.nodes[index])
            self._world[index] = (matrix_multiply(self.world(self.parents[index]), local)
                                  if index in self.parents else local)
        return self._world[index]

    def parent_world(self, index):
        return self.world(self.parents[index]) if index in self.parents else matrix_identity()

    def descendants(self, index):
        yield index
        for child in self.nodes[index].get("children", []):
            yield from self.descendants(child)

    def primitives(self, index):
        """(material name, world triangles, index count) of a node's own mesh."""
        node = self.nodes[index]
        if "mesh" not in node:
            return []
        out = []
        for primitive in self.doc["meshes"][node["mesh"]]["primitives"]:
            points = accessor_values(self.doc, self.buffers, primitive["attributes"]["POSITION"])
            indices = [int(r[0]) for r in accessor_values(self.doc, self.buffers, primitive["indices"])]
            packed = primitive.get("extras", {}).get("ASOBO_primitive", {})
            if packed:
                start, count = packed.get("StartIndex", 0), packed["PrimitiveCount"] * 3
                base = packed.get("BaseVertexIndex", 0)
                indices = [base + i for i in indices[start:start + count]]
            world = self.world(index)
            triangles = [tuple(transform_point(world, points[i]) for i in indices[k:k + 3])
                         for k in range(0, len(indices) - 2, 3)]
            material = self.doc["materials"][primitive["material"]]["name"].lower()
            out.append((material, triangles, len(indices)))
        return out


def compile_motion(source: Source, control: Control) -> dict:
    """The clip as OBJ stages keyed on the control's dataref values (outermost first)."""
    index = source.names[control.node]
    clip = source.clips.get(control.clip)
    if clip is None:
        raise ValueError(f"{control.id}: no FBW clip {control.clip}")
    channels = [c for c in clip["channels"] if c["target"]["node"] == index]
    if not channels:
        raise ValueError(f"{control.id}: clip {control.clip} does not animate {control.node}")
    samples = {}
    for channel in channels:
        sampler = clip["samplers"][channel["sampler"]]
        times = [row[0] for row in accessor_values(source.doc, source.buffers, sampler["input"])]
        values = accessor_values(source.doc, source.buffers, sampler["output"])
        samples[channel["target"]["path"]] = (times, values)
    times = sorted({t for ts, _ in samples.values() for t in ts})
    node = source.nodes[index]
    parent = source.parent_world(index)
    pivot = transform_point(source.world(index), (0, 0, 0))
    stages = []
    if "translation" in samples:
        ts, poses = samples["translation"]
        rest = node.get("translation", (0, 0, 0))
        deltas = [transform_vector(parent, _sub(p, rest)) for p in poses]
        stages.append(dict(kind="translation", times=ts, poses=deltas))
    if "rotation" in samples:
        ts, quats = samples["rotation"]
        axis, angles = rotation_keys(quats, node.get("rotation", (0, 0, 0, 1)), parent)
        stages.append(dict(kind="rotation", times=ts, axis=axis, poses=angles, pivot=pivot))
    if not stages:
        raise ValueError(f"{control.id}: clip {control.clip} has no translation or rotation")
    t0, t1 = times[0], times[-1]
    if t1 <= t0:
        raise ValueError(f"{control.id}: clip {control.clip} has no duration")
    key_value = {}
    if control.kind in ("press", "follow"):
        lo, hi = (0.0, 1.0) if control.kind == "press" else (control.minimum, control.maximum)
        key_value = {t: lo + (hi - lo) * (t - t0) / (t1 - t0) for t in times}
    elif control.kind == "selector":
        if len(times) != control.positions:
            raise ValueError(f"{control.id}: {len(times)} keys for {control.positions} positions")
        key_value = {t: float(i) for i, t in enumerate(times)}
    elif control.kind == "switch":
        # value 0 = the key whose lever tip is lowest (crew view), upward from there
        tip = max((p for _m, tris, _n in source.primitives(index) for tri in tris for p in tri),
                  key=lambda p: _dot(_sub(p, pivot), _sub(p, pivot)))
        heights = []
        for t in times:
            point = tip
            for stage in reversed(stages):
                point = apply_stage(stage, t, point)
            heights.append((point[1], t))
        order = [t for _h, t in sorted(heights)]
        if control.positions == len(times):
            key_value = {t: float(rank) for rank, t in enumerate(order)}
        elif control.positions == 2 and len(times) == 3:
            key_value = {t: rank / 2.0 for rank, t in enumerate(order)}
        else:
            raise ValueError(f"{control.id}: {len(times)} keys for {control.positions} positions")
    for stage in stages:
        pairs = sorted(zip((key_value[t] for t in stage["times"]), stage["poses"]))
        stage["values"] = [v for v, _p in pairs]
        stage["poses"] = [p for _v, p in pairs]
    return dict(control=control, index=index, pivot=pivot, stages=stages)


def apply_stage(stage, t, point):
    i = stage["times"].index(t) if t in stage["times"] else min(range(len(stage["times"])),
                                                                 key=lambda k: abs(stage["times"][k] - t))
    if stage["kind"] == "translation":
        return tuple(a + b for a, b in zip(point, stage["poses"][i]))
    angle = math.radians(stage["poses"][i])
    x, y, z = stage["axis"]
    c, s = math.cos(angle), math.sin(angle)
    r = _sub(point, stage["pivot"])
    rotated = (r[0] * (c + x * x * (1 - c)) + r[1] * (x * y * (1 - c) - z * s) + r[2] * (x * z * (1 - c) + y * s),
               r[0] * (y * x * (1 - c) + z * s) + r[1] * (c + y * y * (1 - c)) + r[2] * (y * z * (1 - c) - x * s),
               r[0] * (z * x * (1 - c) - y * s) + r[1] * (z * y * (1 - c) + x * s) + r[2] * (c + z * z * (1 - c)))
    return tuple(a + b for a, b in zip(stage["pivot"], rotated))


def stage_lines(motion: dict) -> list[str]:
    ref = motion["control"].dataref
    lines = []
    for stage in motion["stages"]:
        if stage["kind"] == "translation":
            lines.append(f"ANIM_trans_begin\t{ref}")
            lines += [f"ANIM_trans_key\t{obj8_fmt(v)}\t{_vec(p)}" for v, p in zip(stage["values"], stage["poses"])]
            lines.append("ANIM_trans_end")
        else:
            pivot, inverse = _vec(stage["pivot"]), _vec(-v for v in stage["pivot"])
            lines += [f"ANIM_trans\t{pivot}\t{pivot}", f"ANIM_rotate_begin\t{_vec(stage['axis'])}\t{ref}"]
            lines += [f"ANIM_rotate_key\t{obj8_fmt(v)}\t{obj8_fmt(p)}" for v, p in zip(stage["values"], stage["poses"])]
            lines += ["ANIM_rotate_end", f"ANIM_trans\t{inverse}\t{inverse}"]
    return lines


def mesh_targets(source: Source, motion: dict) -> list[dict]:
    """Every visible primitive of the animated subtree. Children (legends, lamp faces) match either at
    their FBW position or lifted LEGEND_LIFT along their face normal, as the legend export placed them."""
    control = motion["control"]
    emis_lamps = {lamp.face: lamp for lamp in control.lamps if lamp.cell is None}
    out = []
    for child in source.descendants(motion["index"]):
        name = source.nodes[child].get("name", "")
        for material, triangles, count in source.primitives(child):
            points = sorted({p for tri in triangles for p in tri})
            lifted = []
            if child != motion["index"]:
                normal = (0.0, 0.0, 0.0)
                for tri in triangles:
                    normal = tuple(a + b for a, b in zip(normal, _cross(_sub(tri[1], tri[0]), _sub(tri[2], tri[0]))))
                normal = _unit(normal)
                centre = tuple(sum(p[a] for p in points) / len(points) for a in range(3))
                if _dot(normal, _sub(EYE, centre)) < 0:
                    normal = tuple(-n for n in normal)
                lifted = [tuple(p[a] + LEGEND_LIFT * normal[a] for a in range(3)) for p in points]
            every = points + lifted
            margin = LIFT_TOLERANCE if lifted else TOLERANCE
            low = [min(p[a] for p in every) - margin for a in range(3)]
            high = [max(p[a] for p in every) + margin for a in range(3)]
            out.append(dict(motion=motion, name=name, material=material, count=count, low=low, high=high,
                            points=points, lifted=lifted, lamp=emis_lamps.get(name)))
    return out


def _depths(lines):
    depth = 0
    for line in lines:
        words = line.split()
        word = words[0] if words else ""
        if word == "ANIM_begin":
            depth += 1
        elif word == "ANIM_end":
            depth -= 1
        yield depth


def patch_panel(text: str, filename: str, targets: list[dict]):
    """Wrap the static draws of every applicable target; returns (text, report rows)."""
    applicable = [t for t in targets if filename.endswith(t["material"] + ".obj")
                  and (not t["motion"]["control"].files or filename in t["motion"]["control"].files)]
    if not applicable:
        return text, []
    if MARKER in text:
        missing = sorted({t["motion"]["control"].id for t in applicable
                          if f"{MARKER} {t['motion']['control'].id} " not in text})
        if missing:
            raise ValueError(f"{filename}: incomplete earlier feedback patch, missing {missing[:5]}")
        return text, []
    lines = text.splitlines(keepends=True)
    vertices = [tuple(map(float, line.split()[1:4])) for line in lines if line.startswith("VT")]
    indices = [int(v) for line in lines if line.startswith(("IDX\t", "IDX10\t", "IDX ", "IDX10 "))
               for v in line.split()[1:]]
    chosen = []
    for target in applicable:
        picked = set()
        low, high = target["low"], target["high"]
        for i, p in enumerate(vertices):
            if not all(low[a] <= p[a] <= high[a] for a in range(3)):
                continue
            if any(max(abs(a - b) for a, b in zip(p, q)) <= TOLERANCE for q in target["points"]) or \
                    any(max(abs(a - b) for a, b in zip(p, q)) <= LIFT_TOLERANCE for q in target["lifted"]):
                picked.add(i)
        chosen.append(picked)
    counts = [0] * len(applicable)
    skipped_animated = [0] * len(applicable)
    light_state = "ATTR_light_level_reset"
    output = []
    for line, depth in zip(lines, _depths(lines)):
        words = line.split()
        if words and words[0] in ("ATTR_light_level", "ATTR_light_level_reset"):
            light_state = line.strip()
        if not words or words[0] != "TRIS":
            output.append(line)
            continue
        start, count = int(words[1]), int(words[2])
        runs = []
        for position in range(start, start + count, 3):
            tri = indices[position:position + 3]
            matches = [k for k, picked in enumerate(chosen) if all(v in picked for v in tri)]
            if len(matches) > 1:
                names = sorted({applicable[k]["name"] for k in matches})
                if len(names) > 1:
                    raise ValueError(f"{filename}: ambiguous geometry at index {position}: {names}")
                matches = matches[:1]
            match = matches[0] if matches else -1
            if match >= 0 and depth > 0:
                skipped_animated[match] += 3
                match = -1
            if runs and runs[-1][0] == match:
                runs[-1][2] += 3
            else:
                runs.append([match, position, 3])
        if len(runs) == 1 and runs[0][0] == -1:
            output.append(line)
            continue
        indent = line[:len(line) - len(line.lstrip())]
        for match, run_start, run_count in runs:
            if match < 0:
                output.append(f"{indent}TRIS\t{run_start}\t{run_count}\n")
                continue
            target = applicable[match]
            counts[match] += run_count
            control, lamp = target["motion"]["control"], target["lamp"]
            block = [f"{MARKER} {control.id} {target['name']}"]
            if lamp:
                block.append(f"ATTR_light_level\t0\t1\t{lamp.dataref}")
            block.append("ANIM_begin")
            if lamp:
                block.append(f"ANIM_hide\t-1\t0.001\t{lamp.dataref}")
            block += stage_lines(target["motion"]) + [f"TRIS\t{run_start}\t{run_count}", "ANIM_end"]
            if lamp:
                block.append(light_state)
            output.extend(indent + row + "\n" for row in block)
    patched = "".join(output)
    if vertex_payload(text) != vertex_payload(patched):
        raise ValueError(f"{filename}: VT/IDX/texture payload changed")
    report = []
    for target, count, animated in zip(applicable, counts, skipped_animated):
        report.append(dict(control=target["motion"]["control"].id, mesh=target["name"], wrapped=count,
                           source_indices=target["count"], already_animated=animated,
                           lamp=target["lamp"].dataref if target["lamp"] else None))
    return patched, report


def oriented_box(points, matrix, margin=CLICK_MARGIN):
    """12-triangle box over node-local points placed by matrix (OBJ8 clockwise fronts)."""
    low = [min(p[a] for p in points) - margin for a in range(3)]
    high = [max(p[a] for p in points) + margin for a in range(3)]
    x, y, z = low
    X, Y, Z = high
    local = ((x, y, z), (X, y, z), (X, Y, z), (x, Y, z), (x, y, Z), (X, y, Z), (X, Y, Z), (x, Y, Z))
    world = [transform_point(matrix, p) for p in local]
    centre = tuple(sum(p[a] for p in world) / 8 for a in range(3))
    faces = ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (3, 7, 6, 2), (0, 4, 7, 3), (1, 2, 6, 5))
    vertices, indices = [], []
    for face in faces:
        a, b, c = (world[k] for k in face[:3])
        normal = _unit(_cross(_sub(b, a), _sub(c, a)))
        face_centre = tuple(sum(world[k][axis] for k in face) / 4 for axis in range(3))
        if _dot(normal, _sub(face_centre, centre)) < 0:
            normal = tuple(-n for n in normal)
            order = (0, 1, 2, 0, 2, 3)
        else:
            order = (0, 2, 1, 0, 3, 2)
        base = len(vertices)
        vertices.extend("VT\t" + _vec((*world[k], *normal, 0.0, 0.0)) for k in face)
        indices.extend(base + k for k in order)
    return vertices, indices


def local_points(source: Source, index: int):
    """Points of a node's subtree in that node's local frame."""
    inverse = _invert(source.world(index))
    out = []
    for child in source.descendants(index):
        for _material, triangles, _count in source.primitives(child):
            out.extend(transform_point(inverse, p) for tri in triangles for p in tri)
    return out


def _invert(m):
    r = [[m[j][i] for j in range(3)] for i in range(3)]        # rotation (with scale) inverse via adjugate
    a, b, c = m[0][:3]
    d, e, f = m[1][:3]
    g, h, i = m[2][:3]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    inv = [[(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det],
           [(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det],
           [(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]]
    del r
    t = [m[0][3], m[1][3], m[2][3]]
    it = [-sum(inv[row][k] * t[k] for k in range(3)) for row in range(3)]
    return [inv[0] + [it[0]], inv[1] + [it[1]], inv[2] + [it[2]], [0, 0, 0, 1]]


def lamp_geometry(source: Source, lamp, cell: int):
    """Lit triangles over a lamp face, lifted toward the crew, sampling one atlas cell."""
    index = source.names[lamp.face]
    u = (cell % FCU_LABEL_COLUMNS + .5) / FCU_LABEL_COLUMNS
    v = 1 - (cell // FCU_LABEL_COLUMNS + .5) / FCU_LABEL_ROWS
    vertices, indices = [], []
    for _material, triangles, _count in source.primitives(index):
        for tri in triangles:
            centre = tuple(sum(p[a] for p in tri) / 3 for a in range(3))
            normal = _unit(_cross(_sub(tri[1], tri[0]), _sub(tri[2], tri[0])))
            view = _unit(_sub(EYE, centre))
            if _dot(normal, view) > 0:          # OBJ8 front = clockwise: the cross product points away
                tri = (tri[0], tri[2], tri[1])
                normal = tuple(-n for n in normal)
            facing = tuple(-n for n in normal)  # toward the crew
            base = len(vertices)
            for p in tri:
                lifted = tuple(p[a] + LAMP_LIFT * facing[a] for a in range(3))
                vertices.append("VT\t" + _vec((*lifted, *facing, u, v)))
            indices.extend((base, base + 1, base + 2))
    if not indices:
        raise ValueError(f"lamp face {lamp.face} has no triangles")
    return vertices, indices


def append_controls(text: str, source: Source, motions: list[dict]):
    if MARKER in text:
        return text, []
    clicks, lamps, report = [], [], []
    for motion in motions:
        control = motion["control"]
        manip = click_manipulator(control)
        if manip:
            vertices, indices = oriented_box(local_points(source, motion["index"]), source.world(motion["index"]))
            clicks.append((control, manip, vertices, indices, motion))
        for lamp in control.lamps:
            if lamp.cell is not None:
                vertices, indices = lamp_geometry(source, lamp, lamp.cell)
                lamps.append((control, lamp, vertices, indices, motion))
    lines = text.splitlines()
    row = next(i for i, line in enumerate(lines) if line.startswith("POINT_COUNTS"))
    counts = list(map(int, lines[row].split()[1:]))
    if sum(line.startswith("VT") for line in lines) != counts[0]:
        raise ValueError("controls OBJ vertex count differs from POINT_COUNTS")
    all_vertices, all_indices, commands = [], [], []
    def add(vertices, indices):
        base, start = counts[0] + len(all_vertices), counts[3] + len(all_indices)
        all_vertices.extend(vertices)
        all_indices.extend(base + i for i in indices)
        return start, len(indices)
    commands += ["ATTR_manip_none", "ATTR_no_cockpit", "ATTR_draw_enable", "ATTR_poly_os\t2", "ATTR_no_blend\t0.5"]
    for control, lamp, vertices, indices, motion in lamps:
        start, count = add(vertices, indices)
        commands += [f"{MARKER} LAMP {control.id} {lamp.face}", f"ATTR_light_level\t0\t1\t{lamp.dataref}",
                     "ANIM_begin", f"ANIM_hide\t-1\t0.001\t{lamp.dataref}", *stage_lines(motion),
                     f"TRIS\t{start}\t{count}", "ANIM_end", "ATTR_light_level_reset"]
        report.append(dict(lamp=lamp.dataref, face=lamp.face, cell=lamp.cell, triangles=count // 3))
    commands += ["ATTR_blend", "ATTR_poly_os\t0", "ATTR_draw_disable"]
    for control, manip, vertices, indices, motion in clicks:
        start, count = add(vertices, indices)
        commands += [f"{MARKER} CLICK {control.id}", "ANIM_begin", *stage_lines(motion), manip,
                     f"TRIS\t{start}\t{count}", "ATTR_manip_none", "ANIM_end"]
        report.append(dict(click=control.id, manipulator=manip.split("\t")[0], triangles=count // 3))
    commands += ["ATTR_manip_none", "ATTR_draw_enable"]
    at = max(i for i, line in enumerate(lines) if line.startswith("VT")) + 1
    lines[at:at] = all_vertices
    at = max(i for i, line in enumerate(lines) if line.startswith(("IDX\t", "IDX10\t", "IDX ", "IDX10 "))) + 1
    lines[at:at] = ["IDX\t" + str(i) for i in all_indices]
    lines[row] = "POINT_COUNTS\t" + "\t".join(map(str, (counts[0] + len(all_vertices), counts[1], counts[2],
                                                          counts[3] + len(all_indices))))
    return "\n".join([*lines, "", MARKER + " CONTROLS", *commands, ""]), report


def sha(text: str) -> str:
    return hashlib.sha256(text.encode("latin-1")).hexdigest()


def run(aircraft: Path, source_path: Path, dry_run: bool, controls=CONTROLS) -> dict:
    source = Source(source_path)
    motions = [compile_motion(source, c) for c in controls]
    targets = [t for m in motions for t in mesh_targets(source, m)]
    staged, report = {}, {"panels": [], "controls": []}
    for directory in PANEL_DIRS:
        for path in sorted((aircraft / directory).glob("*.obj")):
            if path.name.startswith("._"):
                continue
            original = path.read_bytes().decode("latin-1")
            patched, rows = patch_panel(original, path.name, targets)
            if patched != original:
                staged[path] = (original, patched)
            report["panels"] += [dict(file=path.name, **r) for r in rows]
    # Every visible source mesh must be wrapped exactly, unless it already moved before this pass.
    # Legend children may differ by a few triangles from the FBW mesh (the export re-triangulated some
    # legend art): at least 90 % wrapped is accepted, and reported as partial.
    for r in report["panels"]:
        r["partial"] = 0 < r["wrapped"] < r["source_indices"]
    problems = [r for r in report["panels"] if (r["partial"] and r["wrapped"] < 0.9 * r["source_indices"])
                or r["wrapped"] > r["source_indices"] or (r["wrapped"] == 0 and r["already_animated"] == 0)]
    controls_path = aircraft / CONTROLS_OBJ
    original = controls_path.read_bytes().decode("latin-1")
    patched, rows = append_controls(original, source, motions)
    if patched != original:
        staged[controls_path] = (original, patched)
    report["controls"] = rows
    report["problems"] = problems
    report["files"] = [dict(path=str(p.relative_to(aircraft)), before=sha(o), after=sha(n)) for p, (o, n) in staged.items()]
    if not dry_run and not problems:
        for path, (_original, text) in staged.items():
            tmp = path.with_name(path.name + ".tmp-feedback")
            tmp.write_bytes(text.encode("latin-1"))
            tmp.replace(path)
    manifest_path = aircraft / PROVIDER_MANIFEST
    if manifest_path.is_file():
        document = json.loads(manifest_path.read_text())
        section = {"commands": list(COMMANDS), "datarefs": list(DATAREFS),
                   "hooked_commands": list(hooked_commands()), "runtime_tested": False}
        if document.get("cockpit_feedback") != section:
            document["cockpit_feedback"] = section
            report["files"].append(dict(path=str(PROVIDER_MANIFEST)))
            if not dry_run and not problems:
                manifest_path.write_text(json.dumps(document, indent=2) + "\n")
    report["written"] = not dry_run and not problems
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--aircraft", type=Path, required=True)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    report = run(args.aircraft.resolve(), args.source.resolve(), args.dry_run)
    if args.report:
        args.report.write_text(json.dumps(report, indent=1) + "\n")
    wrapped = sum(1 for r in report["panels"] if r["wrapped"])
    print(f"{len(report['files'])} OBJ files {'would change' if args.dry_run else 'changed' if report['written'] else 'NOT written'}, "
          f"{wrapped} meshes wrapped, {sum(1 for r in report['controls'] if 'click' in r)} click targets, "
          f"{sum(1 for r in report['controls'] if 'lamp' in r)} lamp quads, {len(report['problems'])} problems")
    for row in report["problems"][:40]:
        print("  problem:", row)
    return 1 if report["problems"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
