"""Build fixed curved cabin lining around an unobstructed doorway portal.

Configuration uses glTF source axes: X is lateral, Y is up, Z is longitudinal.
Geometry is emitted in Blender axes (X, -Z, Y), before OBJ8 conversion.
"""
from __future__ import annotations

import math
from typing import Any


REQUIRED_FIELDS = {
    "name", "anchor_object", "profile", "longitudinal_min", "longitudinal_max",
    "portal_min", "portal_max", "portal_bottom", "portal_top", "mirror_x",
}


def _profile_points(value: Any, name: str, label: str) -> list[tuple[float, float]]:
    if not isinstance(value, list) or len(value) < 2 or any(
        not isinstance(point, list) or len(point) != 2 for point in value
    ):
        raise ValueError(f"Doorway liner {name} needs at least two height/X {label} points")
    try:
        points = [(float(height), float(x)) for height, x in value]
    except (ValueError, TypeError, OverflowError) as error:
        raise ValueError(f"Doorway liner {name} requires finite numeric coordinates") from error
    if not all(math.isfinite(value) for point in points for value in point):
        raise ValueError(f"Doorway liner {name} requires finite numeric coordinates")
    if any(x <= 0 for _height, x in points):
        raise ValueError(f"Doorway liner {name} {label} X must be positive")
    if any(a[0] >= b[0] for a, b in zip(points, points[1:])):
        raise ValueError(f"Doorway liner {name} {label} heights must increase strictly")
    return points


def validate_specs(specs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Check curved-wall and aperture bounds without requiring Blender."""
    if not isinstance(specs, list):
        raise ValueError("Static doorway liners must be a list")
    names: set[str] = set()
    for spec in specs:
        if not isinstance(spec, dict) or REQUIRED_FIELDS - set(spec):
            raise ValueError("Static doorway liner is missing required fields")
        name = spec["name"]
        if not isinstance(name, str) or not name or name in names:
            raise ValueError("Static doorway liner names must be non-empty and unique")
        if not isinstance(spec["anchor_object"], str) or not spec["anchor_object"]:
            raise ValueError(f"Doorway liner {name} needs a named anchor object")
        points = _profile_points(spec["profile"], name, "profile")
        end_points = _profile_points(spec.get("end_profile", spec["profile"]), name, "end profile")
        if end_points[0][0] > points[0][0] or end_points[-1][0] < points[-1][0]:
            raise ValueError(f"Doorway liner {name} end profile must cover the wall profile height range")
        portal_points = _profile_points(spec.get("portal_profile", spec["profile"]), name, "portal profile")
        if portal_points[0][0] > points[0][0] or portal_points[-1][0] < points[-1][0]:
            raise ValueError(f"Doorway liner {name} portal profile must cover the wall profile height range")
        try:
            low, high, pmin, pmax, bottom, top = [float(spec[key]) for key in (
                "longitudinal_min", "longitudinal_max", "portal_min", "portal_max",
                "portal_bottom", "portal_top",
            )]
        except (ValueError, TypeError, OverflowError) as error:
            raise ValueError(f"Doorway liner {name} requires finite numeric coordinates") from error
        if not all(
            math.isfinite(value) for value in (low, high, pmin, pmax, bottom, top)
        ):
            raise ValueError(f"Doorway liner {name} requires finite numeric coordinates")
        if not low < pmin < pmax < high:
            raise ValueError(f"Doorway liner {name} portal must be inside longitudinal bounds")
        if not points[0][0] <= bottom < top <= points[-1][0]:
            raise ValueError(f"Doorway liner {name} portal heights must lie within the profile")
        if not isinstance(spec["mirror_x"], bool):
            raise ValueError(f"Doorway liner {name} mirror_x must be boolean")
        names.add(name)
    return specs


def build_liner_geometry(spec: dict[str, Any]) -> dict[str, Any]:
    """Return a curved quad grid with every cell inside the portal removed."""
    validate_specs([spec])
    profile = [(float(height), float(x)) for height, x in spec["profile"]]
    end_profile = [(float(height), float(x)) for height, x in spec.get("end_profile", spec["profile"])]
    portal_profile = [(float(height), float(x)) for height, x in spec.get("portal_profile", spec["profile"])]
    bottom, top = float(spec["portal_bottom"]), float(spec["portal_top"])
    heights = sorted(
        {height for height, _x in profile}
        | {height for height, _x in end_profile if profile[0][0] <= height <= profile[-1][0]}
        | {height for height, _x in portal_profile if profile[0][0] <= height <= profile[-1][0]}
        | {bottom, top}
    )
    longitudinal = [float(spec[key]) for key in (
        "longitudinal_min", "portal_min", "portal_max", "longitudinal_max",
    )]

    def interpolate(points: list[tuple[float, float]], height: float) -> float:
        for (h0, x0), (h1, x1) in zip(points, points[1:]):
            if h0 <= height <= h1:
                return x0 + (x1 - x0) * (height - h0) / (h1 - h0)
        raise ValueError(f"Height {height} lies outside the doorway liner profile")

    def lateral(height: float, position: float) -> float:
        wall_x = interpolate(profile, height)
        end_x = interpolate(end_profile, height)
        wall_x += (end_x - wall_x) * (position - longitudinal[0]) / (longitudinal[-1] - longitudinal[0])
        portal_x = interpolate(portal_profile, height)
        if height > top:
            portal_x += (wall_x - portal_x) * (height - top) / (heights[-1] - top)
        if position <= longitudinal[1]:
            ratio = (position - longitudinal[0]) / (longitudinal[1] - longitudinal[0])
        elif position >= longitudinal[2]:
            ratio = (longitudinal[-1] - position) / (longitudinal[-1] - longitudinal[2])
        else:
            ratio = 1.0
        return wall_x + (portal_x - wall_x) * ratio

    sign = -1 if spec["mirror_x"] else 1
    vertices = [(sign * lateral(height, position), -position, height)
                for height in heights for position in longitudinal]
    uvs = [((position - longitudinal[0]) / (longitudinal[-1] - longitudinal[0]),
            (height - heights[0]) / (heights[-1] - heights[0]))
           for height in heights for position in longitudinal]
    faces = []
    for row, (h0, h1) in enumerate(zip(heights, heights[1:])):
        for column, (z0, z1) in enumerate(zip(longitudinal, longitudinal[1:])):
            if bottom < (h0 + h1) / 2 < top and longitudinal[1] < (z0 + z1) / 2 < longitudinal[2]:
                continue
            first = row * len(longitudinal) + column
            face = (first, first + 1, first + 1 + len(longitudinal), first + len(longitudinal))
            faces.append(tuple(reversed(face)) if spec["mirror_x"] else face)
    return {"vertices": vertices, "faces": faces, "uvs": uvs}


def apply_static_doorway_liners(
    objects: set[Any], root: Any, specs: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    """Add fixed double-sided collars when their material anchors are present."""
    import bpy

    validate_specs(specs)
    by_name = {obj.name: obj for obj in objects}
    reports: list[dict[str, Any]] = []
    for spec in specs:
        anchor = by_name.get(spec["anchor_object"])
        if anchor is None:
            continue
        if anchor.type != "MESH" or not anchor.data.polygons:
            raise ValueError(f"Doorway liner {spec['name']} anchor must contain a mesh")
        material_index = anchor.data.polygons[0].material_index
        if material_index >= len(anchor.data.materials) or anchor.data.materials[material_index] is None:
            raise ValueError(f"Doorway liner {spec['name']} anchor needs a material")
        if spec["name"] in by_name:
            raise ValueError(f"Doorway liner object already exists: {spec['name']}")
        geometry = build_liner_geometry(spec)
        mesh = bpy.data.meshes.new(spec["name"])
        mesh.from_pydata(geometry["vertices"], [], geometry["faces"])
        mesh.materials.append(anchor.data.materials[material_index])
        mesh.update()
        uv_layer = mesh.uv_layers.new(name="UVMap")
        for polygon in mesh.polygons:
            for loop_index in polygon.loop_indices:
                uv_layer.data[loop_index].uv = geometry["uvs"][mesh.loops[loop_index].vertex_index]
        liner = bpy.data.objects.new(spec["name"], mesh)
        root.objects.link(liner)
        objects.add(liner)
        by_name[liner.name] = liner
        attribute = liner.xplane.customAttributes.add()
        attribute.name = "ATTR_no_cull"
        attribute.value = ""
        attribute.reset = "ATTR_cull"
        reports.append({
            "name": liner.name, "status": "generated", "anchor_object": anchor.name,
            "vertices": len(mesh.vertices), "quads": len(mesh.polygons),
            "triangles": 2 * len(mesh.polygons), "double_sided": True,
            "portal": {key: spec[key] for key in (
                "portal_min", "portal_max", "portal_bottom", "portal_top",
            )},
        })
    return reports
