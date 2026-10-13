"""Source-scoped rigid approximation for six blended FBW main flap panels.

OBJ8 has rigid transforms, not MSFS vertex skinning. Keeping each panel whole
avoids splitting its moving skin from thousands of frozen blended triangles.
Use only after the corrected palette and evaluated static neutral-pose bake.
Source animation comparisons, not this helper, bound the approximation error.
"""
from __future__ import annotations

FLAP_PANELS = {
    "FLAPS_SURFACE_LH_IN": ("L", (2, 3, 4), 3, 2306),
    "FLAPS_SURFACE_LH_MID": ("L", (5, 6, 7, 8), 7, 2378),
    "FLAPS_SURFACE_LH_OUT": ("L", (9, 10, 11), 10, 2118),
    "Cylinder.235": ("R", (2, 3, 4), 3, 2304),
    "Cylinder.233": ("R", (5, 6, 7, 8), 7, 2332),
    "Cylinder.234": ("R", (9, 10, 11), 10, 2086),
}


def prepare_rigid_flap_panels(objects):
    by_name = {obj.name: obj for obj in objects}
    if not FLAP_PANELS.keys() <= by_name.keys():
        raise ValueError("Rigid flap panels require all six audited FBW meshes")
    plans = []
    for name, (side, numbers, representative, polygons) in FLAP_PANELS.items():
        obj = by_name[name]
        if obj.type != "MESH" or len(obj.data.polygons) != polygons:
            raise ValueError(f"Unexpected flap topology: {name}")
        if len(obj.modifiers) != 1 or obj.modifiers[0].type != "ARMATURE":
            raise ValueError(f"Expected one retained flap armature modifier: {name}")
        modifier = obj.modifiers[0]
        if modifier.show_viewport or modifier.show_render:
            raise ValueError(f"Flap approximation requires prior static-pose bake: {name}")
        rig = modifier.object
        if rig is None or rig.name != f"WING_ARMATURE_{side}H":
            raise ValueError(f"Unexpected flap rig: {name}")
        allowed = {f"WING_FLAP_{number}.{side}" for number in numbers}
        target = f"WING_FLAP_{representative}.{side}"
        if not allowed <= set(rig.data.bones.keys()):
            raise ValueError(f"Missing flap bones: {name}")
        group_names = {group.index: group.name for group in obj.vertex_groups}
        observed = set()
        for vertex in obj.data.vertices:
            influences = {group_names[group.group] for group in vertex.groups if group.weight > 0}
            if not influences or not influences <= allowed:
                raise ValueError(f"Unexpected control influences on {name} vertex {vertex.index}: {influences}")
            observed.update(influences)
        if observed != allowed:
            raise ValueError(f"Incomplete audited flap influence set: {name}")
        plans.append((obj, target, sorted(observed)))

    reports = []
    for obj, target, influences in plans:
        # The baked neutral mesh is unchanged. The generic partitioner then
        # attaches its entire skin to this one source-driven flap joint.
        obj.vertex_groups.clear()
        obj.vertex_groups.new(name=target).add(list(range(len(obj.data.vertices))), 1., 'REPLACE')
        reports.append({"object": obj.name, "representative_bone": target,
                        "source_influences": influences, "vertices": len(obj.data.vertices),
                        "polygons": len(obj.data.polygons)})
    return {"enabled": True, "approximation": True, "panels": reports,
            "limitation": "Rigid panel motion approximates blended skin; live wing flex and aerodynamic behavior are not validated."}
