"""Guarded rigid-panel approximation for this FBW A380 wing source revision.

OBJ8 cannot retain blended vertex skinning. Following the corrected palette and
evaluated static-pose bake, consolidate only the sixteen audited spoiler skins.
Both material variants follow the same representative bone. Source comparisons
at every authored key bound the approximation to 17.481 mm over the full 65-degree
source range (13.747 mm through 50 source degrees).

The 46 neutral-only triangles on LH spoiler 2 are detached fixed hardware, not
part of the moving skin. Their original weights must remain untouched.
"""
from __future__ import annotations

import math

# Exported object names after glTF import and per-material face splitting.
# RH source nodes .242/.239/.243 import as .237/.232/.238 in this asset.
# Each tuple: side, spoiler number, representative suffix, primary/secondary tris.
SPOILER_PANELS = {
    "Cylinder.143": ("L", 1, 1, 100, 2740),
    "Cylinder.152": ("L", 2, 1, 130, 2740),
    "Cylinder.140": ("L", 3, 1, 92, 2740),
    "Cylinder.138": ("L", 4, 2, 132, 2740),
    "Cylinder.139": ("L", 5, 2, 118, 2064),
    "Cylinder.137": ("L", 6, 2, 118, 2072),
    "Cylinder.144": ("L", 7, 1, 150, 2072),
    "Cylinder.147": ("L", 8, 1, 148, 2072),
    "Cylinder.237": ("R", 1, 1, 100, 2740),
    "Cylinder.232": ("R", 2, 1, 130, 2740),
    "Cylinder.238": ("R", 3, 1, 92, 2740),
    "Cylinder.245": ("R", 4, 2, 132, 3664),
    "Cylinder.244": ("R", 5, 2, 118, 2988),
    "Cylinder.246": ("R", 6, 2, 118, 2996),
    "Cylinder.241": ("R", 7, 1, 1074, 2072),
    "Cylinder.240": ("R", 8, 1, 148, 2996),
}


def spoiler_variant_specs():
    """Return the exact audited 32-object/material contract."""
    specs = []
    for panel, (side, number, representative, first_count, second_count) in SPOILER_PANELS.items():
        prefix = f"A380_EXTERIOR_WING{1 if side == 'L' else 2}"
        pair = (f"WING_SPOILER{55 if number == 5 else number}_1.{side}",
                f"WING_SPOILER{number}_2.{side}")
        for variant, count in enumerate((first_count, second_count)):
            material = prefix + ("_2" if variant else "")
            name = panel + ("__" + material.lower() if variant else "")
            fixed = 46 if side == "L" and number == 2 and variant == 1 else 0
            specs.append({"object": name, "panel": panel, "side": side,
                          "number": number, "material": material, "polygons": count,
                          "allowed_bones": pair, "representative_bone": pair[representative-1],
                          "fixed_polygons": fixed})
    return specs


def prepare_rigid_spoiler_panels(objects):
    """Validate all targets, then change moving skin weights only.

    Vertex coordinates, normals, topology, UVs, transforms and fixed-hardware
    weights are not modified. The existing rigid partitioner handles attachment.
    """
    by_name = {obj.name: obj for obj in objects}
    specs = spoiler_variant_specs()
    if not {spec["object"] for spec in specs} <= by_name.keys():
        raise ValueError("Rigid spoilers require all sixteen audited panels and both material variants")
    plans = []
    for spec in specs:
        name = spec["object"]
        obj = by_name[name]
        if obj.type != "MESH" or len(obj.data.polygons) != spec["polygons"]:
            raise ValueError(f"Unexpected spoiler topology: {name}")
        if any(len(face.vertices) != 3 for face in obj.data.polygons):
            raise ValueError(f"Spoiler source must remain triangulated: {name}")
        if (len(obj.data.materials) != 1 or obj.data.materials[0] is None
                or obj.data.materials[0].name != spec["material"]
                or any(face.material_index != 0 for face in obj.data.polygons)):
            raise ValueError(f"Unexpected spoiler material: {name}")
        if len(obj.modifiers) != 1 or obj.modifiers[0].type != "ARMATURE":
            raise ValueError(f"Expected one retained spoiler armature modifier: {name}")
        modifier = obj.modifiers[0]
        if modifier.show_viewport or modifier.show_render:
            raise ValueError(f"Spoiler approximation requires prior static-pose bake: {name}")
        rig = modifier.object
        if rig is None or rig.name != f"WING_ARMATURE_{spec['side']}H":
            raise ValueError(f"Unexpected spoiler rig: {name}")
        allowed = set(spec["allowed_bones"])
        required = allowed | ({"NEUTRAL_BONE_1"} if spec["fixed_polygons"] else set())
        if not required <= set(rig.data.bones.keys()):
            raise ValueError(f"Missing spoiler bones: {name}")
        groups = {group.index: group.name for group in obj.vertex_groups}
        labels, observed, moving = [], set(), []
        for vertex in obj.data.vertices:
            if any(not math.isfinite(group.weight) or group.weight < 0 for group in vertex.groups):
                raise ValueError(f"Invalid spoiler weight: {name} vertex {vertex.index}")
            influences = {groups[group.group] for group in vertex.groups if group.weight > 0}
            observed.update(influences)
            if influences and influences <= allowed:
                labels.append("moving")
                moving.append(vertex.index)
            elif spec["fixed_polygons"] and influences == {"NEUTRAL_BONE_1"}:
                labels.append("fixed")
            else:
                raise ValueError(f"Unexpected control influences on {name} vertex {vertex.index}: {influences}")
        if observed != required:
            raise ValueError(f"Incomplete audited spoiler influence set: {name}")
        fixed_faces = 0
        referenced = set()
        for face in obj.data.polygons:
            kinds = {labels[i] for i in face.vertices}
            if len(kinds) != 1:
                raise ValueError(f"Spoiler skin and fixed hardware share a face: {name}")
            fixed_faces += kinds == {"fixed"}
            referenced.update(face.vertices)
        if fixed_faces != spec["fixed_polygons"]:
            raise ValueError(f"Unexpected fixed spoiler hardware count: {name}")
        if len(referenced) != len(obj.data.vertices):
            raise ValueError(f"Unexpected loose spoiler vertices: {name}")
        plans.append((obj, spec, moving, sorted(observed)))

    reports = []
    for obj, spec, moving, observed in plans:
        # Keep all neutral-hardware group memberships/weights exactly as-is.
        # Removing memberships only on moving vertices also preserves group IDs.
        for group in obj.vertex_groups:
            group.remove(moving)
        target = obj.vertex_groups.get(spec["representative_bone"])
        if target is None:
            target = obj.vertex_groups.new(name=spec["representative_bone"])
        target.add(moving, 1., 'REPLACE')
        reports.append({**spec, "source_influences": observed,
                        "vertices": len(obj.data.vertices), "moving_vertices": len(moving),
                        "moving_polygons": spec["polygons"]-spec["fixed_polygons"]})
    return {"enabled": True, "approximation": True, "panels": reports,
            "panel_count": 16, "material_variant_count": len(reports),
            "total_polygons": sum(row["polygons"] for row in reports),
            "fixed_polygons_preserved": sum(row["fixed_polygons"] for row in reports),
            "source_sample_max_error_m": .017481,
            "limitation": "Rigid spoiler skins approximate blended source motion; live wing flex and aerodynamic behavior are not validated."}
