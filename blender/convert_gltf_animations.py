#!/usr/bin/env python3
"""Import FBW glTF files and attach XPlane2Blender dataref keyframes.

Run inside Blender, for example:

    blender --background --python convert_gltf_animations.py -- \
      --input /path/to/fbw-a380x \
      --mapping /path/to/animation_datarefs.json \
      --output /path/to/A380X-exterior.blend \
      --report /path/to/A380X-exterior-report.json

The glTF format records animation time but not the simulator variable which
originally drove the animation. This script therefore maps names through the
JSON rules and maps the first/last animation frames to each rule's value range.
Review the generated report before exporting OBJ files.
"""

from __future__ import annotations

import argparse
import importlib
import json
import math
import os
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable

import bpy
import bmesh

sys.path.insert(0, str(Path(__file__).resolve().parent))
from normalize_obj_animation_keys import normalize_obj_animation_keys
from normalize_engine_fan_rotation import normalize_engine_fan_rotation
from prune_engine_animation_branches import prune_engine_animation_branches
from fbw_flap_panels import prepare_rigid_flap_panels
from fbw_spoiler_panels import prepare_rigid_spoiler_panels
from fbw_tail_panels import prepare_rigid_tail_panels
from animation_sampling import sampling_plan
from fbw_spoiler_rest import verify_fbw_spoiler_rest
from synthetic_rigid_animations import (
    apply_synthetic_rigid_animations,
    validate_specs as validate_synthetic_specs,
)
from static_rigid_clones import (
    apply_static_rigid_clones,
    validate_specs as validate_static_clone_specs,
)
from doorway_liners import (
    apply_static_doorway_liners,
    validate_specs as validate_doorway_liner_specs,
)


BONE_PATH_PATTERN = re.compile(r'pose\.bones\["([^"]+)"\]')


def blender_arguments() -> list[str]:
    """Return arguments following Blender's conventional ``--`` separator."""
    return sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path, help="A glTF file or a directory searched recursively")
    parser.add_argument("--mapping", required=True, type=Path, help="JSON dataref mapping rules")
    parser.add_argument("--output", required=True, type=Path, help="Destination .blend file")
    parser.add_argument("--report", required=True, type=Path, help="Destination JSON conversion report")
    parser.add_argument("--limit", type=int, default=0, help="Import only the first N glTF files (0 means all)")
    parser.add_argument("--keep-scene", action="store_true", help="Do not clear the current Blender scene")
    parser.add_argument(
        "--bake-static-skin-pose",
        action="store_true",
        help="Experimental: bake mapped static bone poses into all skin faces before rigid partitioning",
    )
    parser.add_argument("--rigid-fbw-flap-panels", action="store_true",
                        help="Approximate six audited blended main flap skins as complete rigid panels")
    parser.add_argument("--rigid-fbw-spoiler-panels", action="store_true",
                        help="Approximate sixteen audited spoiler skins and restore verified MFTF spoiler motion")
    parser.add_argument("--rigid-fbw-tail-panels", action="store_true",
                        help="Attach ten audited tail skins to their corrected stabilizer/elevator hinges")
    parser.add_argument("--export-name", help="X-Plane OBJ filename stem")
    parser.add_argument("--export-dir", type=Path, help="Also export OBJ files into this directory")
    parser.add_argument(
        "--export-manifest",
        type=Path,
        help="JSON list of material/texture export variants; imports the glTF only once",
    )
    parser.add_argument(
        "--include-material",
        action="append",
        default=[],
        help="Only draw this material (repeatable); hierarchy and animation remain intact",
    )
    parser.add_argument("--texture", type=Path, help="X-Plane albedo texture")
    parser.add_argument("--texture-map-normal", type=Path, help="X-Plane 12 RG normal map")
    parser.add_argument(
        "--texture-map-material-gloss",
        type=Path,
        help="X-Plane 12 material/gloss map",
    )
    return parser.parse_args(blender_arguments())


def find_gltf_files(source: Path, limit: int) -> list[Path]:
    if source.is_file():
        files = [source]
    else:
        files = sorted(path for path in source.rglob("*.gltf") if not path.name.startswith("._"))
        files.extend(sorted(path for path in source.rglob("*.glb") if not path.name.startswith("._")))
    if limit > 0:
        files = files[:limit]
    if not files:
        raise FileNotFoundError(f"No .gltf or .glb files found under {source}")
    return files


def load_mapping(path: Path) -> tuple[
    list[dict[str, Any]], str, list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]]
]:
    document = json.loads(path.read_text(encoding="utf-8"))
    rules = document.get("mappings", [])
    for rule in rules:
        rule["compiled_pattern"] = re.compile(rule["pattern"])
        if bool(rule.get("dataref")) == (rule.get("static_fraction") is not None):
            raise ValueError(f"Mapping must choose exactly one dataref or static_fraction: {rule['pattern']}")
        if not 0 <= float(rule.get("source_start", 0)) < float(rule.get("source_end", 1)) <= 1:
            raise ValueError(f"Invalid source interval: {rule['pattern']}")
        if rule.get("sample_points") is not None:
            if "source_start" in rule or "source_end" in rule or rule.get("clamp", False):
                raise ValueError(
                    f"Explicit sample points cannot be cropped or clamped: {rule['pattern']}"
                )
            # Keep mapping errors deterministic and independent from Blender.
            sampling_plan([0.0, 1.0], rule)
        if rule.get("static_fraction") is not None and not 0 <= rule["static_fraction"] <= 1:
            raise ValueError(f"Invalid static pose fraction: {rule['pattern']}")
    synthetic = validate_synthetic_specs(document.get("synthetic_rigid_animations", []))
    static_clones = validate_static_clone_specs(document.get("static_rigid_clones", []))
    doorway_liners = validate_doorway_liner_specs(document.get("static_doorway_liners", []))
    synthetic_names = {spec["name"] for spec in synthetic}
    clone_animation_names = {
        instance["animation"]
        for spec in static_clones for instance in spec["instances"]
        if "animation" in instance
    }
    if clone_animation_names - synthetic_names:
        raise ValueError(
            f"Unknown cloned animation names: {sorted(clone_animation_names - synthetic_names)}"
        )
    return (
        rules,
        document.get("fallback_prefix", "a380x/xp12/anim").rstrip("/"),
        synthetic,
        static_clones,
        doorway_liners,
    )


def safe_component(name: str) -> str:
    value = re.sub(r"[^a-z0-9_]+", "_", name.lower()).strip("_")
    return value or "unnamed"


def mapping_for(name: str, rules: list[dict[str, Any]], fallback_prefix: str) -> dict[str, Any]:
    # glTF clip names are authoritative. Mesh/bone names are not: for example
    # BLG_LH contains both deployment and bogie-tilt animations.
    name = re.sub(r"\.\d{3}$", "", name)
    for rule in rules:
        if rule["compiled_pattern"].fullmatch(name):
            return {
                "dataref": rule.get("dataref"),
                "value_min": float(rule.get("value_min", 0.0)),
                "value_max": float(rule.get("value_max", 1.0)),
                "source_start": float(rule.get("source_start", 0.0)),
                "source_end": float(rule.get("source_end", 1.0)),
                "clamp": bool(rule.get("clamp", False)),
                "sample_points": rule.get("sample_points"),
                "static_fraction": rule.get("static_fraction"),
                "priority": int(rule.get("priority", 0)),
                "note": rule.get("note", ""),
                "matched_pattern": rule["pattern"],
            }
    return {
        "dataref": None,
        "proposed_dataref": f"{fallback_prefix}/{safe_component(name)}",
        "note": "Unmapped clip retained at source rest pose; no unregistered dataref emitted.",
        "matched_pattern": None,
    }


def unique_frames(fcurves: Iterable[Any]) -> list[float]:
    frames = {
        float(point.co[0])
        for curve in fcurves
        if ".xplane.datarefs[" not in curve.data_path and "xplane.datarefs[" not in curve.data_path
        for point in curve.keyframe_points
    }
    return sorted(frames)


def value_at_frame(frame: float, frames: list[float], value_min: float, value_max: float) -> float:
    if len(frames) <= 1 or frames[-1] == frames[0]:
        return value_min
    ratio = (frame - frames[0]) / (frames[-1] - frames[0])
    return value_min + (value_max - value_min) * ratio


def ensure_linear(action: Any) -> None:
    for curve in action.fcurves:
        for point in curve.keyframe_points:
            point.interpolation = "LINEAR"


def find_or_add_dataref(collection: Any, path: str) -> tuple[Any, int]:
    for index, existing in enumerate(collection):
        if existing.path == path:
            return existing, index
    item = collection.add()
    item.path = path
    item.anim_type = "transform"
    return item, len(collection) - 1


def attach_object_dataref(
    obj: Any,
    frames: list[float],
    mapping: dict[str, Any],
) -> None:
    item, _index = find_or_add_dataref(obj.xplane.datarefs, mapping["dataref"])
    for frame in frames:
        bpy.context.scene.frame_set(int(round(frame)))
        item.value = value_at_frame(frame, frames, mapping["value_min"], mapping["value_max"])
        item.keyframe_insert(data_path="value", frame=frame, group="XPlane Datarefs")


def attach_bone_dataref(
    armature_object: Any,
    bone_name: str,
    frames: list[float],
    mapping: dict[str, Any],
) -> bool:
    data_bone = armature_object.data.bones.get(bone_name)
    if data_bone is None:
        return False
    animation_data = armature_object.data.animation_data_create()
    if animation_data.action is None or not animation_data.action.name.endswith(
        "_XPlaneDatarefs"
    ):
        animation_data.action = bpy.data.actions.new(
            f"{armature_object.name}_XPlaneDatarefs"
        )
    item, index = find_or_add_dataref(data_bone.xplane.datarefs, mapping["dataref"])
    data_path = f'bones["{bone_name}"].xplane.datarefs[{index}].value'
    for frame in frames:
        bpy.context.scene.frame_set(int(round(frame)))
        item.value = value_at_frame(frame, frames, mapping["value_min"], mapping["value_max"])
        armature_object.data.keyframe_insert(
            data_path=data_path,
            frame=frame,
            group=f"XPlane Datarefs {bone_name}",
        )
    return True


def actions_for_object(obj: Any) -> list[tuple[Any, list[Any]]]:
    """Return only the action slot belonging to this object (Blender 4.4+)."""
    animation_data = obj.animation_data
    if animation_data is None:
        return []
    actions: list[tuple[Any, list[Any]]] = []
    seen: set[tuple[int, int]] = set()

    def add(action: Any, slot: Any) -> None:
        if action is None:
            return
        identity = (action.as_pointer(), slot.handle if slot else 0)
        if identity in seen:
            return
        seen.add(identity)
        if getattr(action, "is_action_layered", False):
            curves = []
            for layer in action.layers:
                for strip in layer.strips:
                    bag = strip.channelbag(slot, ensure=False) if slot else None
                    if bag:
                        curves.extend(bag.fcurves)
        else:
            curves = list(action.fcurves)
        actions.append((action, curves))

    add(animation_data.action, getattr(animation_data, "action_slot", None))
    for track in animation_data.nla_tracks:
        for strip in track.strips:
            add(strip.action, getattr(strip, "action_slot", None))
    return actions


def annotate_animations(
    imported_objects: set[Any],
    rules: list[dict[str, Any]],
    fallback_prefix: str,
    redundant_flap_targets: set[str] | None = None,
) -> list[dict[str, Any]]:
    converted: list[dict[str, Any]] = []
    for obj in sorted(imported_objects, key=lambda candidate: candidate.name):
        actions = sorted(actions_for_object(obj), key=lambda item: -mapping_for(item[0].name, rules, fallback_prefix).get("priority", 0))
        if not actions:
            continue
        animation_data = obj.animation_data_create()
        # The importer stashes independent clips in muted NLA tracks. Adding
        # dataref keys alone does not activate their transforms. Assemble an
        # export action from the correct slots, while leaving the originals
        # stashed for subsequent authoring.
        animation_data.action = bpy.data.actions.new(f"{obj.name}_XPlaneExport")
        for track in animation_data.nla_tracks:
            track.mute = True
        merged = animation_data.action
        claimed: dict[tuple[str, int], str] = {}
        for action, action_curves in actions:
            bone_curves: dict[str, list[Any]] = defaultdict(list)
            object_curves: list[Any] = []
            for curve in action_curves:
                if curve.data_path.startswith("xplane."):
                    continue
                match = BONE_PATH_PATTERN.search(curve.data_path)
                if match:
                    bone_curves[match.group(1)].append(curve)
                else:
                    object_curves.append(curve)

            targets = [(None, object_curves)] + sorted(bone_curves.items())
            for bone_name, curves in targets:
                frames = unique_frames(curves)
                if not frames:
                    continue
                chosen = mapping_for(action.name, rules, fallback_prefix)
                record = {
                    "kind": "bone" if bone_name else "object",
                    "target": bone_name or obj.name,
                    "armature": obj.name if bone_name else None,
                    "action": action.name,
                    "frames": frames,
                    **chosen,
                }
                converted.append(record)
                if (bone_name in (redundant_flap_targets or set())
                        and obj.name == "WING_ARMATURE_LH"
                        and re.sub(r"\.\d{3}$", "", action.name).lower() == "l_flap_percent_key"):
                    record["status"] = "redundant_rest_pose_omitted"
                    record["note"] = "Source-verified constant rest transform; retain rest rotation and allow the dynamic spoiler clip to own this target."
                    continue
                if chosen.get("static_fraction") is not None:
                    frame = frames[0] + (frames[-1] - frames[0]) * chosen["static_fraction"]
                    for curve in curves:
                        path, _, property_name = curve.data_path.rpartition(".")
                        owner = obj.path_resolve(path) if path else obj
                        value = getattr(owner, property_name)
                        value[curve.array_index] = curve.evaluate(frame)
                    record["status"] = "static_pose"
                    continue
                if not chosen["dataref"]:
                    record["status"] = "source_rest_pose"
                    continue
                scale_curves = [curve for curve in curves if curve.data_path.endswith("scale")]
                if any(max(point.co[1] for point in curve.keyframe_points) - min(point.co[1] for point in curve.keyframe_points) > 0.00001 for curve in scale_curves):
                    record["status"] = "unsupported_scale_animation"
                    continue
                # Exporters frequently write redundant, constant scale keys.
                # Keep the imported bind scale; OBJ only needs rotation/location.
                curves = [curve for curve in curves if not curve.data_path.endswith("scale")]
                if not curves:
                    record["status"] = "constant_scale_only"
                    continue
                conflicts = {claimed[(c.data_path, c.array_index)] for c in curves
                             if (c.data_path, c.array_index) in claimed}
                if conflicts:
                    record["status"] = "secondary_motion_omitted"
                    record["primary_actions"] = sorted(conflicts)
                    record["note"] = "Shared FBW multifunction surface: primary clip retained; secondary absolute transform needs a verified composition before export."
                    continue
                # XPlane2Blender samples integral frame numbers. Re-time source
                # fractions onto separate integer frames without rounding them
                # together; evaluate the original curve to retain interpolation.
                source_frames, input_values = sampling_plan(frames, chosen)
                output_frames = [float(i + 1) for i in range(len(source_frames))]
                for curve in curves:
                    key = (curve.data_path, curve.array_index)
                    claimed[key] = action.name
                    copied = merged.fcurves.new(curve.data_path, index=curve.array_index)
                    for output_frame, source_frame in zip(output_frames, source_frames):
                        point = copied.keyframe_points.insert(output_frame, curve.evaluate(source_frame))
                        point.interpolation = "LINEAR"
                    copied.update()
                if hasattr(merged, "slots") and merged.slots:
                    # A new action has no slot when assigned above. Creating
                    # FCurves later does not reliably bind that new slot to the
                    # object; this is especially visible on bone-only rigs.
                    animation_data.action_slot = merged.slots[0]
                # Nonuniform source sample spacing must also be reflected in
                # the dataref keys, not mistaken for uniformly spaced values.
                if bone_name:
                    attach_bone_dataref(obj, bone_name, output_frames, chosen)
                    data_action = obj.data.animation_data.action
                    prefix = f'bones["{bone_name}"].xplane.datarefs'
                else:
                    attach_object_dataref(obj, output_frames, chosen)
                    data_action = merged
                    prefix = "xplane.datarefs"
                for curve in data_action.fcurves:
                    if curve.data_path.startswith(prefix):
                        for point, input_value in zip(curve.keyframe_points, input_values):
                            point.co[1] = input_value
                            point.interpolation = "LINEAR"
                record["export_frames"] = output_frames
                record["source_sample_frames"] = source_frames
                record["dataref_sample_values"] = input_values
                record["status"] = "animated"
        # Low-level FCurve edits do not invalidate the dependency graph in all
        # Blender versions. Explicitly tag the new active action before the
        # exporter samples it, otherwise the first export can freeze rotations
        # even though reopening the saved .blend plays them correctly.
        merged.update_tag()
        obj.update_tag(refresh={"OBJECT", "DATA", "TIME"})
        if obj.type == "ARMATURE":
            obj.data.update_tag()
    bpy.context.view_layer.update()
    return converted


def import_at_rest(path: Path) -> None:
    """Use glTF's extension hook to retain source rest pose on import.

    Otherwise Blender activates the first clip and unknown animations can be
    baked at a random endpoint. The hook is temporary and uses the bundled
    importer's supported extension interface, without changing its files.
    """
    module = importlib.import_module("io_scene_gltf2")
    previous = getattr(module, "glTF2ImportUserExtension", None)
    called = []

    class RestPoseExtension:
        def gather_import_animations(self, animations: Any, options: Any, gltf: Any) -> None:
            options.restore_first_anim = False
            called.append(True)

    module.glTF2ImportUserExtension = RestPoseExtension
    try:
        bpy.ops.import_scene.gltf(filepath=str(path))
    finally:
        if previous is None:
            del module.glTF2ImportUserExtension
        else:
            module.glTF2ImportUserExtension = previous
    if not called:
        raise RuntimeError("glTF rest-pose extension hook did not run; refusing an ambiguous animation export")


def checked_texture(path: Path | None) -> str:
    if path is None:
        return ""
    resolved = path.resolve()
    if not resolved.is_file():
        raise FileNotFoundError(f"Texture does not exist: {resolved}")
    return str(resolved)


def add_lit_texture_to_obj(output: Path, texture: Path, light_level_dataref: str | None = None) -> None:
    """Preserve a source material's emission map in its OBJ8 export."""
    checked_texture(texture)
    lines = output.read_text().splitlines(keepends=True)
    if any(re.match(r"^TEXTURE_LIT\s", line) for line in lines):
        raise ValueError(f"OBJ already has a lit texture: {output}")
    texture_index = next((i for i, line in enumerate(lines) if re.match(r"^TEXTURE\s", line)), None)
    if texture_index is None:
        raise ValueError(f"OBJ has no base texture: {output}")
    relative = Path(os.path.relpath(texture, output.parent)).as_posix()
    lines.insert(texture_index + 1, f"TEXTURE_LIT {relative}\n")
    if light_level_dataref:
        first_draw = next((i for i, line in enumerate(lines) if re.match(r"^\s*TRIS\s", line)), None)
        if first_draw is None:
            raise ValueError(f"OBJ has no triangles: {output}")
        lines.insert(first_draw, f"\tATTR_light_level\t0\t1\t{light_level_dataref}\n")
    output.write_text("".join(lines))


def expose_cockpit_decal(output: Path, offset_m: float, cutout_ratio: float) -> None:
    """Keep translucent source lettering above the opaque cockpit shell."""
    if not 0 < offset_m <= 0.005 or not 0 < cutout_ratio < 1:
        raise ValueError("Invalid cockpit decal offset or alpha cutoff")
    lines = output.read_text().splitlines(keepends=True)
    for i, line in enumerate(lines):
        if not line.startswith("VT\t"):
            continue
        parts = line.rstrip("\n").split("\t")
        if len(parts) != 9:
            raise ValueError(f"Unexpected decal vertex in {output}: {line!r}")
        for axis in range(3):
            parts[axis + 1] = f"{float(parts[axis + 1]) + offset_m * float(parts[axis + 4]):.8f}"
        lines[i] = "\t".join(parts) + "\n"
    first_draw = next((i for i, line in enumerate(lines) if line.lstrip().startswith("TRIS\t")), None)
    if first_draw is None:
        raise ValueError(f"OBJ has no triangles: {output}")
    lines.insert(first_draw, f"\tATTR_no_blend\t{cutout_ratio:g}\n")
    output.write_text("".join(lines))


def manifest_path(value: str | None, manifest: Path) -> Path | None:
    if not value:
        return None
    path = Path(value)
    return path if path.is_absolute() else manifest.parent / path


def configure_export_root(collection: Any, name: str, arguments: argparse.Namespace) -> None:
    # Blender's glTF importer deliberately hides its generated collection and
    # names it ``glTF_not_exported``. XPlane2Blender rejects hidden roots.
    collection.hide_viewport = False
    collection.hide_render = False
    collection.xplane.is_exportable_collection = True
    collection.xplane.layer.name = safe_component(name)
    collection.xplane.layer.export_type = "aircraft"
    collection.xplane.layer.texture = checked_texture(arguments.texture)
    collection.xplane.layer.texture_map_normal = checked_texture(arguments.texture_map_normal)
    collection.xplane.layer.texture_map_material_gloss = checked_texture(
        arguments.texture_map_material_gloss
    )
    if hasattr(bpy.context.scene, "xplane"):
        # XPlane2Blender uses string constants such as "1220" for X-Plane 12.2.
        bpy.context.scene.xplane.version = "1220"


def clear_scene_content() -> None:
    """Clear imported content without reloading Blender factory settings.

    ``read_factory_settings`` also tears down/rebuilds add-on RNA types. That
    invalidates XPlane2Blender PropertyGroup pointers during a headless run in
    Blender 4.5. Removing data blocks preserves the registered exporter API.
    """
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for collection in list(bpy.data.collections):
        bpy.data.collections.remove(collection)
    for action in list(bpy.data.actions):
        bpy.data.actions.remove(action)


def apply_material_filter(objects: set[Any], included_names: list[str]) -> dict[str, bool]:
    included = set(included_names)
    for obj in objects:
        if obj.type != "MESH":
            continue
        if not obj.data.polygons:
            obj.hide_set(True)
            continue
        object_materials = {
            slot.material.name
            for slot in obj.material_slots
            if slot.material is not None
        }
        # Hidden mesh objects are exported by XPlane2Blender as animation-only
        # nodes, preserving their transforms while omitting their vertex table.
        # Most FBW glTF meshes contain one material, so this avoids duplicating
        # the complete airframe geometry in every texture-specific OBJ file.
        obj.hide_set(bool(included) and not bool(object_materials & included))
    materials = {
        slot.material
        for obj in objects
        if obj.type == "MESH"
        for slot in obj.material_slots
        if slot.material is not None
    }
    result: dict[str, bool] = {}
    for material in materials:
        draw = not included or material.name in included
        material.xplane.draw = draw
        result[material.name] = draw
    missing = included - set(result)
    if missing:
        raise KeyError(f"Requested materials were not imported: {sorted(missing)}")
    return dict(sorted(result.items()))


def split_mesh_materials(objects: set[Any], root: Any) -> list[dict[str, Any]]:
    """Give each exported mesh exactly one material and its own faces.

    XPlane2Blender selects one material per object. Hiding a material does not
    discard that material's polygons from a multi-material glTF mesh. Split
    using BMesh so UVs, normals, vertex weights and existing skin modifiers
    survive. Copies remain siblings with the same local transform/animation;
    the original object stays in place so its children keep their parent.
    """
    reports = []
    for obj in sorted(list(objects), key=lambda value: value.name):
        if obj.type != "MESH" or not obj.data.polygons:
            continue
        source = obj.data
        vertex_group_names = [group.name for group in obj.vertex_groups]
        indices = sorted({polygon.material_index for polygon in source.polygons})
        if len(indices) == 1 and indices[0] == 0 and len(source.materials) <= 1:
            continue
        names = []
        for index in indices:
            material = source.materials[index] if index < len(source.materials) else None
            if material is None:
                raise ValueError(f"Mesh {obj.name} uses an empty material slot {index}")
            mesh = bpy.data.meshes.new(f"{source.name}__{safe_component(material.name)}")
            bm = bmesh.new()
            try:
                bm.from_mesh(source)
                bmesh.ops.delete(bm, geom=[face for face in bm.faces if face.material_index != index], context="FACES")
                # FACES keeps boundary geometry; omit loose vertices/edges that
                # belonged exclusively to discarded material subsets.
                bmesh.ops.delete(bm, geom=[vertex for vertex in bm.verts if not vertex.link_faces], context="VERTS")
                for face in bm.faces:
                    face.material_index = 0
                bm.to_mesh(mesh)
            finally:
                bm.free()
            mesh.materials.append(material)
            mesh.update()
            if index == indices[0]:
                target = obj
            else:
                target = obj.copy()
                target.name = f"{obj.name}__{safe_component(material.name)}"
                root.objects.link(target)
                objects.add(target)
            target.data = mesh
            # Blender 4.5 stores group names with the mesh. BMesh preserves
            # numeric deform weights but assigning a fresh mesh clears names;
            # restore their original index order before resolving skin bones.
            for group_name in vertex_group_names:
                target.vertex_groups.new(name=group_name)
            names.append({"object": target.name, "material": material.name, "polygons": len(mesh.polygons)})
        reports.append({"source_object": obj.name, "source_polygons": len(source.polygons), "parts": names})
    return reports


def bake_static_skin_pose(
    objects: set[Any], rules: list[dict[str, Any]], fallback_prefix: str,
) -> dict[str, Any]:
    """Use one selected static pose for both rigid and blended skin geometry.

    This opt-in preparation must run before ``rigidify_skinned_meshes`` and
    ``annotate_animations``. Previously, static wing-flex curves moved rigid
    fragments away from mixed-weight faces frozen in the imported bind pose.
    Evaluate the existing armature deformation for every affected mesh first;
    partitioning then binds its rigid pieces against that same posed skeleton.

    Only static mapped bone curves are sampled. Source clips, dynamic mappings,
    and the existing rigid/mixed classification remain unchanged. Mixed faces
    still cannot deform dynamically in OBJ8; this fixes their neutral seams,
    not the unresolved compiled-source control pivots or vertex skinning.
    """
    assignments: dict[tuple[Any, str, int], float] = {}
    selected_rigs = set()
    samples = []
    for rig in sorted((obj for obj in objects if obj.type == "ARMATURE"), key=lambda obj: obj.name):
        actions = actions_for_object(rig)
        dynamic_channels = {
            (curve.data_path, curve.array_index)
            for action, curves in actions
            if mapping_for(action.name, rules, fallback_prefix).get("dataref")
            for curve in curves
        }
        for action, curves in actions:
            mapping = mapping_for(action.name, rules, fallback_prefix)
            fraction = mapping.get("static_fraction")
            if fraction is None:
                continue
            grouped: dict[str, list[Any]] = defaultdict(list)
            for curve in curves:
                match = BONE_PATH_PATTERN.fullmatch(curve.data_path.rpartition(".")[0])
                if not match:
                    raise ValueError(f"Static skin bake requires bone-only static clips: {action.name}")
                grouped[match.group(1)].append(curve)
            for bone_name, bone_curves in grouped.items():
                frames = unique_frames(bone_curves)
                if not frames:
                    continue
                frame = frames[0] + (frames[-1] - frames[0]) * fraction
                for curve in bone_curves:
                    channel = (curve.data_path, curve.array_index)
                    if channel in dynamic_channels:
                        raise ValueError(f"Static and dynamic clips share a skin-bake channel: {rig.name}:{channel}")
                    path, _, property_name = curve.data_path.rpartition(".")
                    if property_name not in {"location", "rotation_quaternion", "rotation_euler", "scale"}:
                        raise ValueError(f"Unsupported static skin-bake property: {curve.data_path}")
                    current = getattr(rig.path_resolve(path), property_name)
                    if not 0 <= curve.array_index < len(current):
                        raise ValueError(f"Invalid static skin-bake channel index: {curve.data_path}")
                    value = float(curve.evaluate(frame))
                    if not math.isfinite(value):
                        raise ValueError(f"Non-finite static skin-bake value: {curve.data_path}")
                    key = (rig, curve.data_path, curve.array_index)
                    if key in assignments and abs(assignments[key] - value) > 1e-7:
                        raise ValueError(f"Conflicting static skin-bake clips: {rig.name}:{channel}")
                    assignments[key] = value
                selected_rigs.add(rig)
                samples.append({"armature": rig.name, "bone": bone_name,
                                "action": action.name, "source_frame": frame,
                                "static_fraction": fraction})

    # Reject ambiguous stacks before changing the pose or any mesh. Import at
    # rest normally supplies no active action and muted source NLA tracks.
    for rig in selected_rigs:
        animation_data = rig.animation_data
        if animation_data and (animation_data.action is not None or any(not track.mute for track in animation_data.nla_tracks)):
            raise ValueError(f"Static skin bake requires inactive source actions: {rig.name}")
    meshes = []
    for obj in sorted(objects, key=lambda value: value.name):
        if obj.type != "MESH" or not obj.data.polygons:
            continue
        relevant = [modifier for modifier in obj.modifiers
                    if modifier.type == "ARMATURE" and modifier.object in selected_rigs]
        if not relevant:
            continue
        if len(relevant) != 1 or len(obj.modifiers) != 1 or actions_for_object(obj):
            raise ValueError(f"Static skin bake requires one armature and no mesh actions: {obj.name}")
        modifier = relevant[0]
        if not modifier.show_viewport or not modifier.show_render:
            raise ValueError(f"Static skin bake requires an enabled armature modifier: {obj.name}")
        meshes.append((obj, modifier))

    for (rig, data_path, index), value in assignments.items():
        path, _, property_name = data_path.rpartition(".")
        getattr(rig.path_resolve(path), property_name)[index] = value
    for rig in selected_rigs:
        rig.update_tag(refresh={"OBJECT", "DATA", "TIME"})
        rig.data.update_tag()
    bpy.context.view_layer.update()
    depsgraph = bpy.context.evaluated_depsgraph_get()
    reports = []
    for obj, modifier in meshes:
        source = obj.data
        group_names = [group.name for group in obj.vertex_groups]
        source_world = obj.matrix_world.copy()
        evaluated = obj.evaluated_get(depsgraph)
        baked = bpy.data.meshes.new_from_object(
            evaluated, preserve_all_data_layers=True, depsgraph=depsgraph,
        )
        if (len(baked.vertices) != len(source.vertices)
                or len(baked.polygons) != len(source.polygons)
                or any(tuple(a.vertices) != tuple(b.vertices) for a, b in zip(source.polygons, baked.polygons))):
            bpy.data.meshes.remove(baked)
            raise ValueError(f"Static skin bake changed topology: {obj.name}")
        baked.name = f"{source.name}__static_pose"
        obj.data = baked
        # Preserve deform-group indices for the existing partition classifier.
        # Blender versions differ in whether new_from_object carries names.
        if not obj.vertex_groups:
            for name in group_names:
                obj.vertex_groups.new(name=name)
        if [group.name for group in obj.vertex_groups] != group_names:
            raise ValueError(f"Static skin bake changed vertex-group ordering: {obj.name}")
        # Keep the modifier as a rig reference for partitioning, but prevent a
        # second deformation while dependency graphs/parenting are updated.
        modifier.show_viewport = False
        modifier.show_render = False
        difference = max(abs(obj.matrix_world[row][col] - source_world[row][col])
                         for row in range(4) for col in range(4))
        if difference > 0.00001:
            raise RuntimeError(f"Static skin bake changed mesh world transform: {obj.name}")
        reports.append({"object": obj.name, "armature": modifier.object.name,
                        "vertices": len(baked.vertices), "polygons": len(baked.polygons),
                        "world_matrix_error": difference})
    bpy.context.view_layer.update()
    return {"enabled": True, "experimental": True, "static_samples": samples,
            "baked_meshes": reports,
            "limitation": "Mixed-weight faces remain static at the selected neutral pose; dynamic skin deformation is not implemented."}


def rigidify_skinned_meshes(objects: set[Any], root: Any) -> list[dict[str, Any]]:
    """Convert rigid joint weights to actual bone-parented OBJ geometry.

    OBJ8 cannot express vertex skinning. XPlane2Blender otherwise evaluates an
    armature modifier once and exports its bones without moving any triangles.
    Faces whose vertices all belong wholly to one joint can be converted
    exactly. Blended faces remain a static bind-pose subset and are reported.
    """
    reports = []
    for obj in sorted(list(objects), key=lambda value: value.name):
        if obj.type != "MESH" or not obj.data.polygons:
            continue
        modifiers = [m for m in obj.modifiers if m.type == "ARMATURE" and m.object]
        if not modifiers:
            continue
        if len(modifiers) != 1 or actions_for_object(obj):
            reports.append({"object": obj.name, "status": "unsupported_animated_mesh_or_multiple_rigs"})
            continue
        rig = modifiers[0].object
        source = obj.data
        groups = {g.index: g.name for g in obj.vertex_groups if g.name in rig.data.bones}
        vertex_bones = []
        for vertex in source.vertices:
            weights = [(g.weight, groups[g.group]) for g in vertex.groups if g.group in groups and g.weight > 0.000001]
            total = sum(weight for weight, _name in weights)
            dominant = max(weights, default=(0.0, None))
            vertex_bones.append(dominant[1] if total and dominant[0] / total >= 0.9999 else None)
        face_bones = []
        for polygon in source.polygons:
            bones = {vertex_bones[index] for index in polygon.vertices}
            face_bones.append(next(iter(bones)) if len(bones) == 1 else None)
        labels = sorted(set(face_bones), key=lambda value: value or "")
        if labels == [None]:
            # Freeze at bind pose before mapped bones are evaluated at an
            # animation endpoint by the exporter.
            for modifier in list(obj.modifiers):
                if modifier.type == "ARMATURE":
                    obj.modifiers.remove(modifier)
            reports.append({"object": obj.name, "status": "blended_skin_static", "polygons": len(source.polygons)})
            continue
        world = obj.matrix_world.copy()
        parts = []
        for bone_name in labels:
            mesh = bpy.data.meshes.new(f"{source.name}__{safe_component(bone_name or 'static_skin')}")
            bm = bmesh.new()
            try:
                bm.from_mesh(source)
                bm.faces.ensure_lookup_table()
                bmesh.ops.delete(bm, geom=[face for face in bm.faces if face_bones[face.index] != bone_name], context="FACES")
                bmesh.ops.delete(bm, geom=[vertex for vertex in bm.verts if not vertex.link_faces], context="VERTS")
                bm.to_mesh(mesh)
            finally:
                bm.free()
            for material in source.materials:
                mesh.materials.append(material)
            mesh.update()
            target = obj.copy()
            target.name = f"{obj.name}__{safe_component(bone_name or 'static_skin')}"
            target.data = mesh
            root.objects.link(target)
            objects.add(target)
            for modifier in list(target.modifiers):
                if modifier.type == "ARMATURE":
                    target.modifiers.remove(modifier)
            if bone_name:
                target.parent = rig
                target.parent_type = "BONE"
                target.parent_bone = bone_name
                bpy.context.view_layer.update()
                target.matrix_world = world
                bpy.context.view_layer.update()
                difference = max(abs(target.matrix_world[row][col] - world[row][col]) for row in range(4) for col in range(4))
                if difference > 0.0001:
                    raise RuntimeError(f"Rigid skin attachment changed the bind transform of {target.name}")
            parts.append({"object": target.name, "bone": bone_name, "polygons": len(mesh.polygons)})
        # Retain the original node for any existing children and animation
        # references, but its faces now belong only to the explicit subsets.
        obj.data = bpy.data.meshes.new(f"{source.name}__transform_only")
        # The exporter still validates the material of animation-only mesh
        # nodes. Keep a valid slot even though this node has no polygons.
        for material in source.materials:
            obj.data.materials.append(material)
        for modifier in list(obj.modifiers):
            if modifier.type == "ARMATURE":
                obj.modifiers.remove(modifier)
        reports.append({"object": obj.name, "status": "partitioned", "source_polygons": len(source.polygons), "parts": parts})
    return reports


def import_one(
    path: Path,
    rules: list[dict[str, Any]],
    fallback_prefix: str,
    synthetic_specs: list[dict[str, Any]],
    static_clone_specs: list[dict[str, Any]],
    doorway_liner_specs: list[dict[str, Any]],
    arguments: argparse.Namespace,
) -> dict[str, Any]:
    redundant_flap_targets = set()
    if getattr(arguments, "rigid_fbw_tail_panels", False):
        source_document = json.loads(path.read_text())
        tail_repair = source_document.get("extras", {}).get("a380x_xp12_tail_bindshape", {})
        if tail_repair.get("version") != 3:
            raise ValueError("Rigid FBW tail panels require version 3 corrected tail joint indices")
    if (getattr(arguments, "rigid_fbw_flap_panels", False)
            or getattr(arguments, "rigid_fbw_spoiler_panels", False)):
        if not getattr(arguments, "bake_static_skin_pose", False):
            raise ValueError("Rigid FBW panels require --bake-static-skin-pose")
        source_document = json.loads(path.read_text())
        if source_document.get("extras", {}).get("a380x_xp12_wing_joint_palette", {}).get("status") != "decoded":
            raise ValueError("Rigid FBW panels require the corrected wing joint palette")
        if getattr(arguments, "rigid_fbw_spoiler_panels", False):
            redundant_flap_targets = verify_fbw_spoiler_rest(source_document, path)
    objects_before = set(bpy.data.objects)
    import_at_rest(path)
    imported_objects = set(bpy.data.objects) - objects_before

    # glTF puts render objects directly in the scene's master collection and
    # creates a separate hidden ``glTF_not_exported`` collection for custom
    # bone-shape helpers. A master collection cannot be an XPlane2Blender
    # export root, so move only the real imported objects into a new root.
    root = bpy.data.collections.new(f"A380X_{safe_component(path.stem)}")
    bpy.context.scene.collection.children.link(root)
    exported_objects: set[Any] = set()
    for obj in imported_objects:
        hidden_helper = any(
            collection.name.startswith("glTF_not_exported")
            for collection in obj.users_collection
        )
        if hidden_helper or "BOUNDS" in obj.name.upper():
            continue
        root.objects.link(obj)
        exported_objects.add(obj)
        for collection in list(obj.users_collection):
            if collection != root:
                collection.objects.unlink(obj)

    export_name = arguments.export_name or path.stem
    configure_export_root(root, export_name, arguments)
    static_clones = apply_static_rigid_clones(
        exported_objects, root, static_clone_specs,
    )
    doorway_liners = apply_static_doorway_liners(
        exported_objects, root, doorway_liner_specs,
    )
    cloned_animation_groups: dict[str, list[str]] = {}
    for clone_report in static_clones:
        for name, generated in clone_report["animation_groups"].items():
            cloned_animation_groups.setdefault(name, []).extend(generated)
    synthetic_animations = apply_synthetic_rigid_animations(
        exported_objects, root, synthetic_specs, cloned_groups=cloned_animation_groups,
    )
    material_splits = split_mesh_materials(exported_objects, root)
    static_skin_pose = {"enabled": False}
    if getattr(arguments, "bake_static_skin_pose", False):
        static_skin_pose = bake_static_skin_pose(exported_objects, rules, fallback_prefix)
    flap_panels = {"enabled": False}
    if getattr(arguments, "rigid_fbw_flap_panels", False):
        flap_panels = prepare_rigid_flap_panels(exported_objects)
    spoiler_panels = {"enabled": False}
    if getattr(arguments, "rigid_fbw_spoiler_panels", False):
        spoiler_panels = prepare_rigid_spoiler_panels(exported_objects)
    tail_panels = {"enabled": False}
    if getattr(arguments, "rigid_fbw_tail_panels", False):
        tail_panels = prepare_rigid_tail_panels(exported_objects)
    skin_partitions = rigidify_skinned_meshes(exported_objects, root)
    material_draw = apply_material_filter(exported_objects, arguments.include_material)
    converted = annotate_animations(exported_objects, rules, fallback_prefix, redundant_flap_targets)
    return {
        "source": str(path),
        "root_collection": root.name,
        "imported_objects": len(exported_objects),
        "excluded_helper_objects": len(imported_objects - exported_objects),
        "material_splits": material_splits,
        "static_rigid_clones": static_clones,
        "static_doorway_liners": doorway_liners,
        "synthetic_rigid_animations": synthetic_animations,
        "skin_partitions": skin_partitions,
        "static_skin_pose": static_skin_pose,
        "rigid_flap_panels": flap_panels,
        "rigid_spoiler_panels": spoiler_panels,
        "rigid_tail_panels": tail_panels,
        "source_verified_redundant_flap_targets": sorted(redundant_flap_targets),
        "material_draw": material_draw,
        "converted_targets": converted,
    }


def export_manifest_variants(
    arguments: argparse.Namespace,
    results: list[dict[str, Any]],
) -> tuple[list[str], list[dict[str, Any]]]:
    if arguments.export_manifest is None:
        return [], []
    if arguments.export_dir is None:
        raise ValueError("--export-manifest requires --export-dir")
    if len(results) != 1:
        raise ValueError("--export-manifest currently requires exactly one input glTF")

    manifest_path_value = arguments.export_manifest.resolve()
    document = json.loads(manifest_path_value.read_text(encoding="utf-8"))
    variants = document["exports"] if isinstance(document, dict) else document
    if not isinstance(variants, list) or not variants:
        raise ValueError("Export manifest must contain a non-empty JSON list")

    root = bpy.data.collections[results[0]["root_collection"]]
    exported_objects = set(root.all_objects)
    arguments.export_dir.mkdir(parents=True, exist_ok=True)
    exported_files: list[str] = []
    export_reports: list[dict[str, Any]] = []
    source_specular = {material.name: material.specular_intensity for material in bpy.data.materials}

    for variant in variants:
        name = safe_component(variant["name"])
        material_draw = apply_material_filter(exported_objects, variant.get("materials", []))
        alpha_mode = variant.get("alpha_mode", "OPAQUE").upper()
        if alpha_mode not in {"OPAQUE", "MASK", "BLEND"}:
            raise ValueError(f"Unsupported alpha_mode {alpha_mode} for {name}")
        for material_name, draw in material_draw.items():
            if not draw:
                continue
            material = bpy.data.materials[material_name]
            material.xplane.blend_v1000 = "on" if alpha_mode == "BLEND" else "off"
            material.xplane.blendRatio = float(variant.get("alpha_cutoff", 0.5)) if alpha_mode == "MASK" else 0.0
            material.xplane.poly_os = int(variant.get("polygon_offset", 0))
            material.specular_intensity = float(variant.get("specular", source_specular[material_name]))
        root.xplane.layer.name = name
        root.xplane.layer.texture = checked_texture(
            manifest_path(variant.get("texture"), manifest_path_value)
        )
        root.xplane.layer.texture_map_normal = checked_texture(
            manifest_path(variant.get("texture_map_normal"), manifest_path_value)
        )
        root.xplane.layer.texture_map_material_gloss = checked_texture(
            manifest_path(variant.get("texture_map_material_gloss"), manifest_path_value)
        )
        root.xplane.layer.normal_metalness = bool(variant.get("normal_metalness", False))
        result = bpy.ops.export.xplane_obj(
            filepath=str(arguments.export_dir / f"{name}.obj")
        )
        if result != {"FINISHED"}:
            raise RuntimeError(f"XPlane2Blender export failed for {name}: {result}")
        output = arguments.export_dir / f"{name}.obj"
        if not output.is_file():
            candidates = sorted(arguments.export_dir.glob(f"{name}*.obj"))
            if len(candidates) != 1:
                raise FileNotFoundError(
                    f"Exporter finished but output for {name} was not found in {arguments.export_dir}"
                )
            output = candidates[0]
        fan_rotation = normalize_engine_fan_rotation(output)
        animation_key_order = normalize_obj_animation_keys(output)
        empty_engine_animation_branches = prune_engine_animation_branches(output, name)
        # The add-on resolves paths relative to the blend file's directory.
        # Package and standalone exports need paths relative to the OBJ itself.
        texture_paths = {
            "TEXTURE": variant.get("texture"),
            "TEXTURE_MAP normal": variant.get("texture_map_normal"),
            "TEXTURE_MAP material_gloss": variant.get("texture_map_material_gloss"),
        }
        lines = output.read_text().splitlines()
        for i, line in enumerate(lines):
            match = re.match(r"^(TEXTURE|TEXTURE_MAP\s+normal|TEXTURE_MAP\s+material_gloss)\s+", line)
            if match:
                key = " ".join(match.group(1).split())
                texture_path = manifest_path(texture_paths[key], manifest_path_value)
                if texture_path:
                    lines[i] = key + " " + Path(os.path.relpath(texture_path, output.parent)).as_posix()
            elif "specular" in variant and line.startswith("GLOBAL_specular"):
                lines[i] = f"GLOBAL_specular {float(variant['specular']):g}"
        output.write_text("\n".join(lines) + "\n")
        if variant.get("double_sided", False):
            # Single-material variants retain the glTF back-face visibility.
            # XPlane2Blender has no native double-sided material switch.
            lines = output.read_text().splitlines()
            if any(line.strip() == "ATTR_cull" for line in lines):
                raise ValueError(f"Cannot apply whole-object double-sided state to {name}: explicit cull sections")
            if not any(line.strip() == "ATTR_no_cull" for line in lines):
                first_draw = next(i for i, line in enumerate(lines) if line.lstrip().startswith("TRIS"))
                lines.insert(first_draw, "ATTR_no_cull")
                output.write_text("\n".join(lines) + "\n")
        lit_texture = manifest_path(variant.get("texture_lit"), manifest_path_value)
        if lit_texture:
            add_lit_texture_to_obj(output, lit_texture, variant.get("lit_level_dataref"))
        if variant.get("decal_offset_m"):
            expose_cockpit_decal(output, float(variant["decal_offset_m"]),
                                  float(variant["decal_cutout_ratio"]))
        exported_files.append(str(output))
        export_reports.append(
            {
                "name": name,
                "output": str(output),
                "materials": variant.get("materials", []),
                "material_draw": material_draw,
                "texture": root.xplane.layer.texture,
                "texture_map_normal": root.xplane.layer.texture_map_normal,
                "texture_map_material_gloss": root.xplane.layer.texture_map_material_gloss,
                "texture_lit": str(lit_texture) if lit_texture else None,
                "alpha_mode": alpha_mode,
                "alpha_cutoff": variant.get("alpha_cutoff", 0.5),
                "double_sided": bool(variant.get("double_sided", False)),
                "normal_metalness": bool(variant.get("normal_metalness", False)),
                "animation_key_order": animation_key_order,
                "fan_rotation": fan_rotation,
                "empty_engine_animation_branches": empty_engine_animation_branches,
            }
        )

    return exported_files, export_reports


def main() -> None:
    arguments = parse_arguments()
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.report.parent.mkdir(parents=True, exist_ok=True)

    if not arguments.keep_scene:
        clear_scene_content()

    if not hasattr(bpy.context.scene, "xplane"):
        raise RuntimeError(
            "XPlane2Blender is not enabled. Install and enable the official add-on before running this script."
        )

    # Reuse identical position/normal/UV records within each mesh. OBJ8 uses
    # indexed triangles; emitting three separate vertices per triangle wastes
    # storage and simulator memory without preserving any extra information.
    bpy.context.scene.xplane.optimize = True

    rules, fallback_prefix, synthetic_specs, static_clone_specs, doorway_liner_specs = load_mapping(
        arguments.mapping
    )
    sources = find_gltf_files(arguments.input, arguments.limit)
    results = [
        import_one(
            source,
            rules,
            fallback_prefix,
            synthetic_specs,
            static_clone_specs,
            doorway_liner_specs,
            arguments,
        )
        for source in sources
    ]
    bpy.context.scene.frame_set(1)
    bpy.ops.wm.save_as_mainfile(filepath=str(arguments.output))

    exported_files, export_reports = export_manifest_variants(arguments, results)
    if arguments.export_dir is not None and arguments.export_manifest is None:
        arguments.export_dir.mkdir(parents=True, exist_ok=True)
        result = bpy.ops.export.xplane_obj(
            filepath=str(arguments.export_dir / "export.obj")
        )
        if result != {"FINISHED"}:
            raise RuntimeError(f"XPlane2Blender export failed: {result}")
        exported_files = [
            str(path)
            for path in sorted(arguments.export_dir.glob("*.obj"))
            if not path.name.startswith("._")
        ]
        for path in exported_files:
            normalize_obj_animation_keys(Path(path))

    report = {
        "format": 2,
        "input": str(arguments.input),
        "output_blend": str(arguments.output),
        "exported_obj_files": exported_files,
        "exports": export_reports,
        "obj_checks": [audit_obj(Path(path)) for path in exported_files],
        "sources": results,
        "summary": {
            "gltf_files": len(results),
            "objects": sum(result["imported_objects"] for result in results),
            "animation_targets": sum(len(result["converted_targets"]) for result in results),
            "animated_targets": sum(target["status"] == "animated" for result in results for target in result["converted_targets"]),
            "static_targets": sum(target["status"] != "animated" for result in results for target in result["converted_targets"]),
            "fallback_mappings": sum(
                target["matched_pattern"] is None
                for result in results
                for target in result["converted_targets"]
            ),
            "synthetic_rigid_animations": sum(
                len(result["synthetic_rigid_animations"]) for result in results
            ),
            "static_rigid_clones": sum(
                len(result["static_rigid_clones"]) for result in results
            ),
            "static_doorway_liners": sum(
                len(result["static_doorway_liners"]) for result in results
            ),
        },
    }
    arguments.report.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report["summary"], indent=2))


def audit_obj(path: Path) -> dict[str, Any]:
    """Record what was actually emitted, accepting exporter indentation."""
    depth = 0
    transform_blocks = 0
    animated_triangles = 0
    indices = 0
    datarefs = set()
    with path.open(encoding="utf-8") as stream:
        for line in stream:
            fields = line.split()
            if not fields:
                continue
            if fields[0] == "ANIM_begin":
                depth += 1
            elif fields[0] == "ANIM_end":
                depth -= 1
                if depth < 0:
                    raise ValueError(f"Unbalanced animation block in {path}")
            elif fields[0] in {"ANIM_trans_begin", "ANIM_rotate_begin"}:
                transform_blocks += 1
                datarefs.add(fields[-1])
            elif fields[0] == "TRIS":
                indices += int(fields[2])
                if depth:
                    animated_triangles += int(fields[2]) // 3
    if depth:
        raise ValueError(f"Unclosed animation block in {path}")
    return {"output": str(path), "triangle_count": indices // 3,
            "triangles_in_animation_blocks": animated_triangles,
            "transform_blocks": transform_blocks, "datarefs": sorted(datarefs)}


if __name__ == "__main__":
    main()
