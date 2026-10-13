"""Author guarded rigid animations for FBW meshes that ship without clips.

The upper-deck exterior door meshes are separate, accurately placed objects,
but the pinned FBW fuselage glTF has no upper-deck door animation clips.  This
module creates a small rigid door hierarchy in Blender without changing
the upstream source asset.  XPlane2Blender then exports the generated actions
through the same reviewed dataref mapping path as authored clips.
"""
from __future__ import annotations

import math
from typing import Any


REQUIRED_FIELDS = {
    "name",
    "objects",
    "primary_object",
    "side",
    "hinge_longitudinal",
    "open_angle_degrees",
    "plug_distance",
    "plug_fraction",
}


def validate_specs(specs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Validate configuration without importing Blender (used by tests too)."""
    if not isinstance(specs, list):
        raise ValueError("Synthetic rigid animations must be a list")
    names: set[str] = set()
    targets: set[str] = set()
    for spec in specs:
        missing = REQUIRED_FIELDS - set(spec)
        if missing:
            raise ValueError(f"Synthetic animation is missing fields: {sorted(missing)}")
        name = spec["name"]
        objects = spec["objects"]
        if not isinstance(name, str) or not name or name in names:
            raise ValueError(f"Synthetic animation names must be non-empty and unique: {name!r}")
        if not isinstance(objects, list) or not objects or any(
            not isinstance(value, str) or not value for value in objects
        ):
            raise ValueError(f"Synthetic animation {name} needs named target objects")
        if len(objects) != len(set(objects)):
            raise ValueError(f"Synthetic animation {name} repeats an object")
        overlap = targets & set(objects)
        if overlap:
            raise ValueError(f"Synthetic objects may belong to only one animation: {sorted(overlap)}")
        if spec["primary_object"] not in objects:
            raise ValueError(f"Synthetic animation {name} primary object is not a target")
        if spec["side"] not in {"left", "right"}:
            raise ValueError(f"Synthetic animation {name} has an invalid side")
        if spec["hinge_longitudinal"] not in {"min", "max"}:
            raise ValueError(f"Synthetic animation {name} has an invalid hinge edge")
        numbers = (
            float(spec["open_angle_degrees"]),
            float(spec["plug_distance"]),
            float(spec["plug_fraction"]),
            float(spec.get("slide_distance", 0.0)),
        )
        if not all(math.isfinite(value) for value in numbers):
            raise ValueError(f"Synthetic animation {name} contains a non-finite number")
        if not 0 <= abs(numbers[0]) <= 120:
            raise ValueError(f"Synthetic animation {name} has an implausible open angle")
        if not 0 <= numbers[1] <= 0.75 or not 0 < numbers[2] < 1:
            raise ValueError(f"Synthetic animation {name} has an invalid plug motion")
        if not 0 <= numbers[3] <= 3:
            raise ValueError(f"Synthetic animation {name} has an invalid slide distance")
        if numbers[0] == 0 and numbers[3] == 0:
            raise ValueError(f"Synthetic animation {name} has no opening motion")
        if "double_sided" in spec and not isinstance(spec["double_sided"], bool):
            raise ValueError(f"Synthetic animation {name} double_sided must be boolean")
        names.add(name)
        targets.update(objects)
    return specs


def motion_samples(spec: dict[str, Any]) -> list[dict[str, float]]:
    """Return a three-key plug-then-open schedule for one door."""
    fraction = float(spec["plug_fraction"])
    return [
        {
            "frame": 1.0,
            "ratio": 0.0,
            "outward": 0.0,
            "longitudinal": 0.0,
            "angle": 0.0,
        },
        {
            "frame": 1.0 + 100.0 * fraction,
            "ratio": fraction,
            "outward": float(spec["plug_distance"]),
            "longitudinal": 0.0,
            "angle": 0.0,
        },
        {
            "frame": 101.0,
            "ratio": 1.0,
            "outward": float(spec["plug_distance"]),
            "longitudinal": float(spec.get("slide_distance", 0.0)),
            "angle": math.radians(float(spec["open_angle_degrees"])),
        },
    ]


def resolve_animation_targets(
    specs: list[dict[str, Any]], object_names: set[str],
    cloned_groups: dict[str, list[str]] | None = None,
) -> list[dict[str, Any]]:
    """Resolve complete original groups and explicitly assigned interior copies.

    An interior-only subset can contain no original exterior group. A partial
    original group is still an error, as is any missing or multiply owned clone.
    Keep this guard independent of Blender so subset exports are testable.
    """
    validate_specs(specs)
    cloned_groups = {} if cloned_groups is None else cloned_groups
    if not isinstance(cloned_groups, dict):
        raise ValueError("Cloned animation groups must be a mapping")
    known = {spec["name"] for spec in specs}
    unknown = set(cloned_groups) - known
    if unknown:
        raise ValueError(f"Unknown cloned animation names: {sorted(unknown)}")
    original_owners = {
        name: spec["name"] for spec in specs for name in spec["objects"]
    }
    assigned: set[str] = set()
    for animation, names in cloned_groups.items():
        if not isinstance(names, list) or not names or any(
            not isinstance(name, str) or not name for name in names
        ):
            raise ValueError(f"Cloned animation {animation} needs named objects")
        if len(names) != len(set(names)) or assigned.intersection(names):
            raise ValueError(f"Cloned animation {animation} repeats an assigned object")
        missing = set(names) - object_names
        if missing:
            raise ValueError(f"Cloned animation {animation} has missing objects: {sorted(missing)}")
        overlap = set(names).intersection(original_owners)
        if overlap:
            raise ValueError(f"Cloned animation {animation} reuses original objects: {sorted(overlap)}")
        assigned.update(names)
    resolved: list[dict[str, Any]] = []
    for spec in specs:
        originals = [name for name in spec["objects"] if name in object_names]
        clones = cloned_groups.get(spec["name"], [])
        if originals and len(originals) != len(spec["objects"]):
            missing = sorted(set(spec["objects"]) - set(originals))
            raise ValueError(
                f"Synthetic animation {spec['name']} has an incomplete source group: {missing}"
            )
        if not originals and not clones:
            continue
        resolved.append({
            "spec": spec,
            "objects": originals + clones,
            "primary_object": spec["primary_object"] if originals else clones[0],
        })
    return resolved


def apply_synthetic_rigid_animations(
    objects: set[Any], root: Any, specs: list[dict[str, Any]],
    cloned_groups: dict[str, list[str]] | None = None,
) -> list[dict[str, Any]]:
    """Create animated Blender empties and parent configured rigid meshes."""
    import bpy
    from mathutils import Matrix, Vector

    by_name = {obj.name: obj for obj in objects}
    resolved_groups = resolve_animation_targets(specs, set(by_name), cloned_groups)
    reports: list[dict[str, Any]] = []
    original_frame = bpy.context.scene.frame_current
    try:
        for group in resolved_groups:
            spec = group["spec"]
            targets = [by_name[name] for name in group["objects"]]
            primary = by_name[group["primary_object"]]
            if primary.type != "MESH" or not primary.data.polygons:
                raise ValueError(f"Synthetic animation {spec['name']} primary object has no mesh")
            if any(target.type != "MESH" for target in targets):
                raise ValueError(f"Synthetic animation {spec['name']} targets must be meshes")

            corners = [primary.matrix_world @ Vector(corner) for corner in primary.bound_box]
            minimum = Vector(tuple(min(corner[axis] for corner in corners) for axis in range(3)))
            maximum = Vector(tuple(max(corner[axis] for corner in corners) for axis in range(3)))
            hinge_y = minimum.y if spec["hinge_longitudinal"] == "min" else maximum.y
            hinge = Vector(((minimum.x + maximum.x) / 2, hinge_y, (minimum.z + maximum.z) / 2))

            pivot = bpy.data.objects.new(f"{spec['name']}_XP12_PIVOT", None)
            pivot.empty_display_type = "PLAIN_AXES"
            pivot.empty_display_size = 0.25
            root.objects.link(pivot)
            objects.add(pivot)
            pivot.matrix_world = Matrix.Translation(hinge)
            for target in targets:
                world = target.matrix_world.copy()
                target.parent = pivot
                target.matrix_world = world
                if spec.get("double_sided", False) and not any(
                    attribute.name == "ATTR_no_cull" for attribute in target.xplane.customAttributes
                ):
                    attribute = target.xplane.customAttributes.add()
                    attribute.name = "ATTR_no_cull"
                    attribute.value = ""
                    attribute.reset = "ATTR_cull"

            animation_data = pivot.animation_data_create()
            action = bpy.data.actions.new(spec["name"])
            animation_data.action = action
            outward_sign = 1.0 if spec["side"] == "left" else -1.0
            longitudinal_sign = -1.0 if spec["hinge_longitudinal"] == "min" else 1.0
            samples = motion_samples(spec)
            for sample in samples:
                bpy.context.scene.frame_set(int(round(sample["frame"])))
                pivot.location = hinge + Vector((
                    outward_sign * sample["outward"],
                    longitudinal_sign * sample["longitudinal"],
                    0.0,
                ))
                pivot.rotation_mode = "XYZ"
                pivot.rotation_euler = (0.0, 0.0, sample["angle"])
                pivot.keyframe_insert(data_path="location", frame=sample["frame"])
                pivot.keyframe_insert(data_path="rotation_euler", frame=sample["frame"])
            for curve in action.fcurves:
                for point in curve.keyframe_points:
                    point.interpolation = "LINEAR"

            reports.append(
                {
                    "name": spec["name"],
                    "status": "authored",
                    "objects": list(group["objects"]),
                    "primary_object": group["primary_object"],
                    "hinge_world": list(hinge),
                    "primary_bounds_world": {"min": list(minimum), "max": list(maximum)},
                    "double_sided": bool(spec.get("double_sided", False)),
                    "samples": samples,
                }
            )
    finally:
        bpy.context.scene.frame_set(original_frame)
    bpy.context.view_layer.update()
    return reports
