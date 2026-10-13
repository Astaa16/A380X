#!/usr/bin/env python3
"""Add the two verified FBW landing-gear-bay materials to the fuselage export.

Preparation only: this never starts Blender or modifies upstream assets. The
normal exterior export then emits the bay geometry as two additional OBJ8
attachments, keeping its texture state isolated from the fuselage skins.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


BAY_MATERIALS = ("A380X_LG_BAY_MAIN", "A380X_LG_BAY")
BAY_OBJECT_NAMES = {
    "A380X_LG_BAY_MAIN": "a380x_lg_bay_main",
    "A380X_LG_BAY": "a380x_lg_bay",
}


def coverage(document: dict) -> dict:
    """Fail closed if either selected bay material is missing or has no faces."""
    selected = set(BAY_MATERIALS)
    materials = document.get("materials", [])
    found = {material["name"]: index for index, material in enumerate(materials)
             if material.get("name") in selected}
    if set(found) != selected:
        raise ValueError(f"Missing landing-gear-bay materials: {sorted(selected - set(found))}")
    if sum(material.get("name") in selected for material in materials) != len(selected):
        raise ValueError("Duplicate landing-gear-bay material names in source")

    result = {}
    for name, index in found.items():
        material = materials[index]
        extensions = material.get("extensions", {})
        if material.get("alphaMode", "OPAQUE") != "OPAQUE" or any(
            key in extensions
            for key in ("ASOBO_material_invisible", "ASOBO_material_environment_occluder")
        ):
            raise ValueError(f"Landing-gear-bay material is not visibly opaque: {name}")
        result[name] = {
            "material_index": index,
            "triangles": 0,
            "primitive_instances": 0,
            "nodes": [],
            "has_albedo": "baseColorTexture" in material.get("pbrMetallicRoughness", {}),
            "has_normal": "normalTexture" in material,
            "has_comp": "metallicRoughnessTexture" in material.get("pbrMetallicRoughness", {}),
        }

    for node_index, node in enumerate(document.get("nodes", [])):
        if "mesh" not in node:
            continue
        mesh = document["meshes"][node["mesh"]]
        for primitive in mesh.get("primitives", []):
            material_index = primitive.get("material")
            if material_index not in found.values():
                continue
            name = materials[material_index]["name"]
            if primitive.get("mode", 4) != 4:
                raise ValueError(f"Non-triangle landing-gear-bay primitive: {name}")
            draw = primitive.get("extras", {}).get("ASOBO_primitive", {})
            count = draw.get("PrimitiveCount")
            if count is None:
                index_count = document["accessors"][primitive["indices"]]["count"]
                if index_count % 3:
                    raise ValueError(f"Incomplete landing-gear-bay triangle indices: {name}")
                count = index_count // 3
            if not isinstance(count, int) or isinstance(count, bool) or count <= 0:
                raise ValueError(f"Invalid landing-gear-bay triangle count: {name}")
            result[name]["triangles"] += count
            result[name]["primitive_instances"] += 1
            result[name]["nodes"].append({
                "index": node_index,
                "name": node.get("name", ""),
                "triangles": count,
            })

    if any(not item["triangles"] for item in result.values()):
        raise ValueError("Selected landing-gear-bay material has no geometry")
    main = result["A380X_LG_BAY_MAIN"]
    if not (main["has_albedo"] and main["has_normal"] and main["has_comp"]):
        raise ValueError("Main landing-gear-bay material lost its authored PBR texture set")
    return result


def normalize_additions(additions: dict, manifest: Path, texture_dir: Path) -> dict:
    """Give generated variants stable names and relocatable texture paths."""
    normalized = copy.deepcopy(additions)
    build_root = manifest.parent.parent.resolve()
    exports = normalized.get("exports", [])
    prepared_materials = [material for entry in exports for material in entry.get("materials", [])]
    if sorted(prepared_materials) != sorted(BAY_MATERIALS):
        raise ValueError("Prepared manifest does not contain exactly the two bay materials")
    for entry in exports:
        if len(entry.get("materials", [])) != 1 or entry.get("alpha_mode") != "OPAQUE":
            raise ValueError("Each bay variant must contain one opaque material")
        material = entry["materials"][0]
        entry["name"] = BAY_OBJECT_NAMES[material]
        for key in ("texture", "texture_map_normal", "texture_map_material_gloss"):
            if key not in entry:
                continue
            texture = Path(entry[key]).resolve()
            if not texture.is_relative_to(texture_dir.resolve()):
                raise ValueError(f"Landing-gear-bay texture escapes texture directory: {texture}")
            if not texture.is_relative_to(build_root):
                raise ValueError(f"Landing-gear-bay texture escapes build directory: {texture}")
            entry[key] = str(Path("..") / texture.relative_to(build_root))
    return normalized


def merge_exports(existing: dict, additions: dict) -> dict:
    """Preserve existing variants and append each bay material exactly once."""
    merged = copy.deepcopy(existing)
    exports = merged["exports"]
    names, materials = {}, {}
    for entry in exports:
        if entry["name"] in names:
            raise ValueError(f"Duplicate existing object name: {entry['name']}")
        names[entry["name"]] = entry
        for material in entry.get("materials", []):
            if material in materials:
                raise ValueError(f"Duplicate existing material: {material}")
            materials[material] = entry

    added = additions["exports"]
    prepared_materials = [material for entry in added for material in entry.get("materials", [])]
    if sorted(prepared_materials) != sorted(BAY_MATERIALS):
        raise ValueError("Prepared manifest does not contain exactly the two bay materials")
    for entry in added:
        material = entry["materials"][0]
        if material in materials:
            if materials[material] != entry:
                raise ValueError(f"Existing material differs from prepared bay variant: {material}")
            continue
        if entry["name"] in names:
            raise ValueError(f"Bay object name collides with another variant: {entry['name']}")
        exports.append(copy.deepcopy(entry))
        names[entry["name"]] = entry
        materials[material] = entry
    return merged


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--asset-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()

    project = Path(__file__).resolve().parents[1]
    source_document = json.loads(args.source.read_text())
    evidence = coverage(source_document)
    manifests = args.build_dir / "manifests"
    target = manifests / "fuselage-exports.json"
    existing = json.loads(target.read_text())
    reports = args.build_dir / "reports"
    reports.mkdir(parents=True, exist_ok=True)
    prepared_path = reports / "fuselage-gear-bay-exports.json"
    material_report = reports / "fuselage-gear-bay-materials.json"
    texture_dir = args.build_dir / "textures" / "fuselage"
    command = [
        sys.executable,
        str(project / "blender/prepare_interior_materials.py"),
        str(args.source),
        "--asset-root", str(args.asset_root),
        "--texture-dir", str(texture_dir),
        "--manifest", str(prepared_path),
        "--report", str(material_report),
        "--prefix", "bay",
        "--max-size", "2048",
        "--detail-max-size", "1024",
        "--reuse-decoded",
    ]
    for name in BAY_MATERIALS:
        command.extend(["--include-material", "^" + re.escape(name) + "$"])
    subprocess.run(command, check=True)

    additions = normalize_additions(json.loads(prepared_path.read_text()), target, texture_dir)
    merged = merge_exports(existing, additions)
    from PIL import Image
    referenced = []
    for entry in additions["exports"]:
        for key in ("texture", "texture_map_normal", "texture_map_material_gloss"):
            if key not in entry:
                continue
            path = (target.parent / entry[key]).resolve()
            with Image.open(path) as image:
                limit = 2048 if key == "texture" else 1024
                if max(image.size) > limit:
                    raise ValueError(f"Landing-gear-bay texture exceeds {limit}px: {path}")
                referenced.append({
                    "material": entry["materials"][0],
                    "kind": key,
                    "path": str(path),
                    "size": list(image.size),
                    "bytes": path.stat().st_size,
                })

    backup = reports / "fuselage-exports.before-gear-bays.json"
    if merged != existing:
        if not backup.exists():
            backup.write_text(json.dumps(existing, indent=2) + "\n")
        target.write_text(json.dumps(merged, indent=2) + "\n")

    report = {
        "source": str(args.source),
        "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
        "materials": evidence,
        "source_triangles_added": sum(item["triangles"] for item in evidence.values()),
        "fuselage_variants_before": len(existing["exports"]),
        "fuselage_variants_after": len(merged["exports"]),
        "referenced_textures": referenced,
        "manifest": str(target),
        "backup_manifest": str(backup),
        "status": "prepared; Blender fuselage export and package refresh still required",
        "limitations": [
            "Uses the pinned FBW bay geometry, UVs, and authored main-bay PBR textures.",
            "The plain A380X_LG_BAY material intentionally retains its source-authored white finish.",
            "No new geometry, decals, bay lighting, or hydraulic-system behavior is added.",
        ],
    }
    report_path = reports / "landing-gear-bay-texturing.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(
        f"Prepared two landing-gear-bay materials: {report['source_triangles_added']} source triangles; "
        f"fuselage manifest now contains {len(merged['exports'])} variants. No Blender process launched.",
        flush=True,
    )
    print(report_path)


if __name__ == "__main__":
    main()
