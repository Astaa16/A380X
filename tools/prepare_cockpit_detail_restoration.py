#!/usr/bin/env python3
"""Restore 20 verified LOD00 cockpit detail materials to the panel manifest.

Preparation only: this never starts Blender or modifies upstream assets. The
existing interior rebuild command then picks up the expanded panel manifest.
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


DETAIL_MATERIALS = (
    "A380_COCKPIT_SEATS01_4k", "A380_COCKPIT_SEATS02_4k",
    "A380_COCKPIT_SEATS03_4k", "A380_COCKPIT_SEATBELT_TEXTILE_4k",
    "A380_COCKPIT_SEAT_TEXTILE_4k", "A380_COCKPIT_AFT_MISC03_4k",
    "A380_COCKPIT_LEATHER03_4k", "A380_COCKPIT_AFT_MISC02_4k",
    "A380_COCKPIT_AFT_MISC01_4k", "A380_COCKPIT_AFT_MISC05_4k",
    "A380_COCKPIT_MISC03_4k", "A380_COCKPIT_MISC01_4k",
    "A380_COCKPIT_MISC02", "A380_mikes_cables_4k",
    "A380_COCKPIT_LEATHER01_4k", "A380_COCKPIT_LEATHER02_4k",
    "A380_COCKPIT_KEYBOARD", "A380_COCKPIT_KEYBOARD2",
    "A380_COCKPIT_MISC_STUFF", "Rudder_Cables_4k",
)


def coverage(document: dict) -> dict:
    """Fail closed if a selected material is missing, invisible, or unused."""
    selected = set(DETAIL_MATERIALS)
    materials = document.get("materials", [])
    found = {m["name"]: i for i, m in enumerate(materials) if m.get("name") in selected}
    if set(found) != selected:
        raise ValueError(f"Missing detail materials: {sorted(selected - set(found))}")
    if sum(m.get("name") in selected for m in materials) != len(selected):
        raise ValueError("Duplicate detail material names in source")
    result = {}
    for name, index in found.items():
        material = materials[index]
        extensions = material.get("extensions", {})
        if material.get("alphaMode", "OPAQUE") != "OPAQUE" or any(
            key in extensions for key in ("ASOBO_material_invisible", "ASOBO_material_environment_occluder")
        ):
            raise ValueError(f"Detail material is not a visible opaque surface: {name}")
        result[name] = {"material_index": index, "triangles": 0, "primitive_instances": 0, "nodes": []}
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
                raise ValueError(f"Non-triangle detail primitive: {name}")
            draw = primitive.get("extras", {}).get("ASOBO_primitive", {})
            count = draw.get("PrimitiveCount")
            if count is None:
                index_count = document["accessors"][primitive["indices"]]["count"]
                if index_count % 3:
                    raise ValueError(f"Incomplete triangle indices: {name}")
                count = index_count // 3
            if not isinstance(count, int) or isinstance(count, bool) or count <= 0:
                raise ValueError(f"Invalid detail triangle count: {name}")
            result[name]["triangles"] += count
            result[name]["primitive_instances"] += 1
            result[name]["nodes"].append({"index": node_index, "name": node.get("name", ""), "triangles": count})
    if any(not item["triangles"] for item in result.values()):
        raise ValueError("Selected detail material has no geometry")
    return result


def merge_exports(existing: dict, additions: dict) -> dict:
    """Preserve every old variant; do not duplicate materials or object names."""
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
    if sorted(material for entry in added for material in entry.get("materials", [])) != sorted(DETAIL_MATERIALS):
        raise ValueError("Prepared manifest does not contain exactly the 20 detail materials")
    for entry in added:
        if len(entry["materials"]) != 1 or entry.get("alpha_mode") != "OPAQUE":
            raise ValueError("Each detail variant must contain one opaque material")
        material = entry["materials"][0]
        if material in materials:
            if materials[material] != entry:
                raise ValueError(f"Existing material differs from prepared detail variant: {material}")
            continue
        if entry["name"] in names:
            raise ValueError(f"Detail object name collides with another variant: {entry['name']}")
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
    target = manifests / "cockpit-panel-exports.json"
    existing = json.loads(target.read_text())
    for other in ("cockpit-exports.json", "cabin-exports.json", "cockpit-decal-exports.json", "cockpit-screen-exports.json"):
        entries = json.loads((manifests / other).read_text())["exports"]
        overlap = set(DETAIL_MATERIALS) & {material for entry in entries for material in entry.get("materials", [])}
        if overlap:
            raise ValueError(f"Detail materials already exported by {other}: {sorted(overlap)}")
    reports = args.build_dir / "reports"
    reports.mkdir(parents=True, exist_ok=True)
    prepared_path = reports / "cockpit-detail-exports.json"
    material_report = reports / "cockpit-detail-materials.json"
    texture_dir = args.build_dir / "textures" / "cockpit-panels"
    command = [sys.executable, str(project / "blender/prepare_interior_materials.py"), str(args.source),
               "--asset-root", str(args.asset_root), "--texture-dir", str(texture_dir),
               "--manifest", str(prepared_path), "--report", str(material_report),
               "--prefix", "panel", "--max-size", "2048", "--detail-max-size", "1024", "--reuse-decoded"]
    for name in DETAIL_MATERIALS:
        command.extend(["--include-material", "^" + re.escape(name) + "$"])
    subprocess.run(command, check=True)
    additions = json.loads(prepared_path.read_text())
    merged = merge_exports(existing, additions)
    from PIL import Image
    referenced = []
    for entry in additions["exports"]:
        for key in ("texture", "texture_map_normal", "texture_map_material_gloss"):
            path = Path(entry[key])
            if not path.resolve().is_relative_to(texture_dir.resolve()):
                raise ValueError(f"Detail texture escapes panel texture directory: {path}")
            with Image.open(path) as image:
                limit = 2048 if key == "texture" else 1024
                if max(image.size) > limit:
                    raise ValueError(f"Detail texture exceeds resolution limit {limit}: {path}")
                referenced.append({"path": str(path), "size": list(image.size), "bytes": path.stat().st_size})
    # The reversible pre-restoration manifest is saved once, never overwritten.
    backup = reports / "cockpit-panel-exports.before-details.json"
    if merged != existing:
        if not backup.exists():
            backup.write_text(json.dumps(existing, indent=2) + "\n")
        target.write_text(json.dumps(merged, indent=2) + "\n")
    report = {"source": str(args.source), "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
              "materials": evidence, "source_triangles_added": sum(item["triangles"] for item in evidence.values()),
              "panel_variants_before": len(existing["exports"]), "panel_variants_after": len(merged["exports"]),
              "other_group_material_overlap": [], "referenced_textures": referenced,
              "manifest": str(target), "backup_manifest": str(backup),
              "status": "prepared; Blender export and package refresh still required",
              "limitations": ["No glass, helpers, lights, or avionics added.",
                              "Source normal-map convention is unchanged."]}
    report_path = reports / "cockpit-detail-restoration.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    print(f"Prepared 20 real detail materials: {report['source_triangles_added']} source triangles; "
          f"panel manifest now contains {len(merged['exports'])} variants. No Blender process launched.", flush=True)
    print(report_path)


if __name__ == "__main__":
    main()
