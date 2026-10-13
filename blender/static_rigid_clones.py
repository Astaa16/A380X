"""Create reviewed static copies of source meshes at configured locations.

The FBW cabin source contains one authored upper-deck doorway surround at U1L.
Its jamb, header, and threshold are reusable at the other five upper-deck
openings, whose exterior door centers are geometrically equivalent. This
module performs those copies after glTF import and before OBJ8 export. Instances
can also assign their generated meshes to an existing synthetic rigid animation.
"""
from __future__ import annotations

import math
from typing import Any


REQUIRED_SPEC_FIELDS = {"name", "objects", "instances"}
REQUIRED_INSTANCE_FIELDS = {"name", "translation", "mirror_x"}


def validate_specs(specs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Validate clone configuration without importing Blender."""
    if not isinstance(specs, list):
        raise ValueError("Static rigid clones must be a list")
    spec_names: set[str] = set()
    source_objects: set[str] = set()
    for spec in specs:
        missing = REQUIRED_SPEC_FIELDS - set(spec)
        if missing:
            raise ValueError(f"Static clone spec is missing fields: {sorted(missing)}")
        name = spec["name"]
        objects = spec["objects"]
        instances = spec["instances"]
        if not isinstance(name, str) or not name or name in spec_names:
            raise ValueError(f"Static clone names must be non-empty and unique: {name!r}")
        if not isinstance(objects, list) or not objects or any(
            not isinstance(value, str) or not value for value in objects
        ):
            raise ValueError(f"Static clone {name} needs named source objects")
        if len(objects) != len(set(objects)):
            raise ValueError(f"Static clone {name} repeats a source object")
        overlap = source_objects & set(objects)
        if overlap:
            raise ValueError(f"Static clone source objects may occur once: {sorted(overlap)}")
        if not isinstance(instances, list) or not instances:
            raise ValueError(f"Static clone {name} needs at least one instance")
        instance_names: set[str] = set()
        for instance in instances:
            missing_instance = REQUIRED_INSTANCE_FIELDS - set(instance)
            if missing_instance:
                raise ValueError(
                    f"Static clone {name} instance is missing fields: {sorted(missing_instance)}"
                )
            instance_name = instance["name"]
            translation = instance["translation"]
            if (
                not isinstance(instance_name, str)
                or not instance_name
                or instance_name in instance_names
            ):
                raise ValueError(
                    "Static clone instance names must be non-empty and unique: "
                    f"{instance_name!r}"
                )
            if (
                not isinstance(translation, list)
                or len(translation) != 3
                or not all(math.isfinite(float(value)) for value in translation)
            ):
                raise ValueError(
                    f"Static clone {name}/{instance_name} needs a finite XYZ translation"
                )
            if not isinstance(instance["mirror_x"], bool):
                raise ValueError(
                    f"Static clone {name}/{instance_name} mirror_x must be boolean"
                )
            if "animation" in instance and (
                not isinstance(instance["animation"], str) or not instance["animation"]
            ):
                raise ValueError(
                    f"Static clone {name}/{instance_name} animation must be a non-empty name"
                )
            instance_names.add(instance_name)
        if "double_sided" in spec and not isinstance(spec["double_sided"], bool):
            raise ValueError(f"Static clone {name} double_sided must be boolean")
        spec_names.add(name)
        source_objects.update(objects)
    return specs


def apply_static_rigid_clones(
    objects: set[Any], root: Any, specs: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    """Replace source groups with copies and report optional animation ownership."""
    import bpy
    from mathutils import Matrix, Vector

    validate_specs(specs)
    by_name = {obj.name: obj for obj in objects}
    reports: list[dict[str, Any]] = []
    mirror_x = Matrix.Diagonal((-1.0, 1.0, 1.0, 1.0))
    for spec in specs:
        present = [name for name in spec["objects"] if name in by_name]
        if not present:
            continue
        missing = sorted(set(spec["objects"]) - set(present))
        if missing:
            raise ValueError(f"Static clone {spec['name']} has an incomplete source group: {missing}")
        sources = [by_name[name] for name in spec["objects"]]
        if any(source.type != "MESH" or not source.data.polygons for source in sources):
            raise ValueError(f"Static clone {spec['name']} source objects must contain meshes")

        generated: list[str] = []
        animation_groups: dict[str, list[str]] = {}
        for instance in spec["instances"]:
            offset = Matrix.Translation(Vector(tuple(map(float, instance["translation"]))))
            transform = offset @ (mirror_x if instance["mirror_x"] else Matrix.Identity(4))
            for source in sources:
                clone = source.copy()
                clone.data = source.data
                clone.name = f"{spec['name']}_{instance['name']}_{source.name}"
                root.objects.link(clone)
                objects.add(clone)
                clone.matrix_world = transform @ source.matrix_world
                if spec.get("double_sided", False) and not any(
                    attribute.name == "ATTR_no_cull" for attribute in clone.xplane.customAttributes
                ):
                    attribute = clone.xplane.customAttributes.add()
                    attribute.name = "ATTR_no_cull"
                    attribute.value = ""
                    attribute.reset = "ATTR_cull"
                generated.append(clone.name)
                if instance.get("animation"):
                    animation_groups.setdefault(instance["animation"], []).append(clone.name)

        for source in sources:
            objects.remove(source)
            by_name.pop(source.name, None)
            bpy.data.objects.remove(source, do_unlink=True)
        reports.append(
            {
                "name": spec["name"],
                "status": "cloned",
                "source_objects": list(spec["objects"]),
                "instances": [instance["name"] for instance in spec["instances"]],
                "generated_objects": generated,
                "animation_groups": animation_groups,
                "double_sided": bool(spec.get("double_sided", False)),
            }
        )
    return reports
