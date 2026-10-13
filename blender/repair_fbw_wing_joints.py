"""Decode the audited FBW optimized wing BLEND1/4-v2 one-based joint palette.

This is deliberately opt-in and wing-specific, not a heuristic for all glTF.
The two 63-joint source skins use weighted indices 1..63, never zero. Standard
glTF expects 0..62. Merely clamping index 63 made Blender import succeed while
attaching virtually every control surface to the wrong joint. Source mesh
membership and corrected flap/slat trajectories independently confirm the shift.
Only new JOINTS_0 accessors are emitted; weights, geometry and clips stay intact.
"""
from __future__ import annotations

import math
import struct
from pathlib import Path

MARKER = "a380x_xp12_wing_joint_palette"
EXPECTED_SKINS = {
    "WING_ARMATURE_LH": ("WING_FLEX_1.L", "NEUTRAL_BONE_1"),
    "WING_ARMATURE_RH": ("WING_FLEX_1.R", "neutral_bone"),
}


def repair_fbw_wing_joints(document: dict, output_document: Path) -> dict:
    if MARKER in document.get("extras", {}):
        raise ValueError("Wing joint palette has already been decoded; rebuild from upstream")
    optimized = document.get("asset", {}).get("extensions", {}).get("ASOBO_asset_optimized", {})
    if (optimized.get("MajorVersion"), optimized.get("MinorVersion")) != (4, 6):
        raise ValueError("Wing palette repair requires audited ASOBO asset version 4.6")
    all_skins = document.get("skins", [])
    skins = {i: skin for i, skin in enumerate(all_skins) if skin.get("name") in EXPECTED_SKINS}
    if len(skins) != 2 or {skin["name"] for skin in skins.values()} != set(EXPECTED_SKINS):
        raise ValueError("Wing palette repair requires the two uniquely named audited wing skins")
    for skin in skins.values():
        joints = skin.get("joints", [])
        expected = EXPECTED_SKINS[skin["name"]]
        if len(joints) != 63 or tuple(document["nodes"][joints[i]]["name"] for i in (0, -1)) != expected:
            raise ValueError(f"Unexpected 63-joint palette: {skin['name']}")
        if len(set(joints)) != len(joints):
            raise ValueError("Duplicate joints in wing palette")
    root = Path(output_document).parent.resolve()
    binaries = {}

    def read(index, semantic):
        accessor = document["accessors"][index]
        formats = {5121: ("B", 255), 5123: ("H", 65535), 5126: ("f", 1)}
        component = accessor.get("componentType")
        count = accessor.get("count")
        scalar_weight = semantic == "WEIGHTS_0" and accessor.get("type") == "SCALAR" and component == 5126
        if "sparse" in accessor or (accessor.get("type") != "VEC4" and not scalar_weight) or component not in formats:
            raise ValueError(f"Unsupported {semantic} accessor")
        if semantic == "JOINTS_0" and (component == 5126 or accessor.get("normalized")):
            raise ValueError("Joint indices must be unsigned, unnormalized integers")
        if semantic == "WEIGHTS_0" and component != 5126 and not accessor.get("normalized"):
            raise ValueError("Integer skin weights must be normalized")
        if not isinstance(count, int) or isinstance(count, bool) or count <= 0:
            raise ValueError("Invalid accessor count")
        view = document["bufferViews"][accessor["bufferView"]]
        buffer_index = view.get("buffer", 0)
        info = document["buffers"][buffer_index]
        uri = info.get("uri", "")
        if not uri or uri.startswith("data:"):
            raise ValueError("Wing repair requires external build-copy buffers")
        path = (root / uri.replace("\\", "/")).resolve()
        if not path.is_relative_to(root):
            raise ValueError("Wing buffer is outside the build directory")
        if buffer_index not in binaries:
            binaries[buffer_index] = path.read_bytes()
        binary = binaries[buffer_index]
        fmt, divisor = formats[component]
        width = 1 if scalar_weight else 4
        size = struct.calcsize(f"<{width}" + fmt)
        stride = view.get("byteStride", size)
        view_offset, offset = view.get("byteOffset", 0), accessor.get("byteOffset", 0)
        if (min(view_offset, offset, stride) < 0 or stride < size
                or offset + (count - 1) * stride + size > view["byteLength"]
                or view_offset + view["byteLength"] > min(len(binary), info["byteLength"])):
            raise ValueError("Wing attribute exceeds buffer bounds")
        rows = [struct.unpack_from(f"<{width}" + fmt, binary, view_offset + offset + i * stride) for i in range(count)]
        if scalar_weight:
            rows = [(row[0], 0., 0., 0.) for row in rows]
        if semantic == "WEIGHTS_0" and component != 5126:
            rows = [tuple(value / divisor for value in row) for row in rows]
        return rows

    mesh_skins = {}
    for node in document["nodes"]:
        if "mesh" in node and "skin" in node:
            mesh_skins.setdefault(node["mesh"], set()).add(node["skin"])
    plans, decoded = [], {}
    skin_stats = {i: {"name": skin["name"], "weighted_lanes": 0, "top_index_lanes": 0} for i, skin in skins.items()}
    for mesh_index, skin_indexes in mesh_skins.items():
        if not (skin_indexes & skins.keys()):
            continue
        if len(skin_indexes) != 1:
            raise ValueError("Wing mesh is shared across different skins")
        skin_index = next(iter(skin_indexes))
        for primitive in document["meshes"][mesh_index]["primitives"]:
            layout = primitive.get("extras", {}).get("ASOBO_primitive", {})
            if layout.get("VertexType") not in {"BLEND1", "BLEND4"} or layout.get("VertexVersion") != 2:
                raise ValueError("Wing joint repair requires audited BLEND1/4 version 2 layout")
            attributes = primitive["attributes"]
            weight_accessor = document["accessors"][attributes["WEIGHTS_0"]]
            expected_weights = ("SCALAR", 5126) if layout["VertexType"] == "BLEND1" else ("VEC4", 5123)
            if (weight_accessor.get("type"), weight_accessor.get("componentType")) != expected_weights:
                raise ValueError("Unexpected optimized wing weight layout")
            if "JOINTS_1" in attributes or "WEIGHTS_1" in attributes:
                raise ValueError("Extra wing skin influence sets are unsupported")
            pair = (attributes["JOINTS_0"], attributes["WEIGHTS_0"])
            if pair not in decoded:
                joint_rows, weight_rows = read(pair[0], "JOINTS_0"), read(pair[1], "WEIGHTS_0")
                if len(joint_rows) != len(weight_rows):
                    raise ValueError("Joint/weight accessor counts differ")
                rows, positive, top = [], 0, 0
                for joints, weights in zip(joint_rows, weight_rows):
                    if any(not math.isfinite(w) or w < 0 for w in weights) or sum(weights) <= 0:
                        raise ValueError("Invalid wing weights")
                    row = []
                    for joint, weight in zip(joints, weights):
                        if weight > 0:
                            if not 1 <= joint <= 63:
                                raise ValueError("Weighted wing joint is outside one-based range 1..63")
                            row.append(joint - 1)
                            positive += 1
                            top += joint == 63
                        else:
                            # Unused lanes may contain padding; standard glTF
                            # importers still index them, so use harmless zero.
                            row.append(0)
                    rows.append(tuple(row))
                decoded[pair] = (rows, positive, top)
            rows, positive, top = decoded[pair]
            skin_stats[skin_index]["weighted_lanes"] += positive
            skin_stats[skin_index]["top_index_lanes"] += top
            plans.append((attributes, pair))
    if any(not stats["top_index_lanes"] for stats in skin_stats.values()):
        raise ValueError("Missing weighted index 63 evidence; refusing ambiguous zero-based data")

    # Nothing is changed until all source/layout/range checks have passed.
    packed, accessors = bytearray(), {}
    new_buffer = len(document["buffers"])
    for pair, (rows, _positive, _top) in sorted(decoded.items()):
        while len(packed) % 4:
            packed.append(0)
        start = len(packed)
        for row in rows:
            packed.extend(struct.pack("<4B", *row))
        view_index = len(document["bufferViews"])
        document["bufferViews"].append({"buffer": new_buffer, "byteOffset": start,
                                      "byteLength": len(packed) - start, "target": 34962})
        accessors[pair] = len(document["accessors"])
        document["accessors"].append({"bufferView": view_index, "componentType": 5121,
                                      "type": "VEC4", "count": len(rows),
                                      "name": f"A380X_wing_zero_based_joints_{pair[0]}_{pair[1]}"})
    filename = "a380x-wing-zero-based-joints.bin"
    (root / filename).write_bytes(packed)
    document["buffers"].append({"uri": filename, "byteLength": len(packed)})
    for attributes, pair in plans:
        attributes["JOINTS_0"] = accessors[pair]
    report = {"status": "decoded", "source_layout": "ASOBO 4.6 BLEND1/4 v2; audited wing one-based palette",
              "primitives": len(plans), "new_accessors": len(accessors),
              "vertices": sum(len(value[0]) for value in decoded.values()),
              "skins": list(skin_stats.values()), "source_geometry_weights_animations_unchanged": True}
    document.setdefault("extras", {})[MARKER] = report
    return report
