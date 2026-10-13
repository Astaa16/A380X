#!/usr/bin/env python3
"""Prepare the detailed FBW cabin for the X-Plane interior export.

The LOD00 model contains the authored staircases, door assemblies, monuments,
galleys, structural floors, carpet layers and cabin signs omitted by the old
LOD01-only export. Transparent MSFS-only shaders and helper geometry remain
excluded until they have a separately reviewed X-Plane treatment.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


CABIN_MATERIAL_ROLES = {
    "A380X_PAX_DOORS": "ten detailed main-deck passenger-door assemblies",
    "A380X_AFT_STAIRS": "aft inter-deck staircase",
    "A380X_FC_DECK_SIDWEALLS": "forward and upper-deck cabin monuments",
    "A380X_MAIN_DECK_YC_SIDEWALLS": "main-deck economy sidewalls and monuments",
    "A380X_MAIN_DECK_ASSETS": "main-deck fixtures and lavatory/monument assets",
    "A380X_MAIN_DECK_PEC_SIDEWALLS": "main-deck premium sidewalls and monuments",
    "A380X_CABIN_STAIRS_FWD": "forward inter-deck staircase and bar",
    "A380_COCKPIT_BLACK_FLOOR_PANEL_4k": "lower forward floor panel",
    "A380X_MAIN_DECK_GALLEY": "main-deck galley fixtures",
    "A380X_UPPER_DECK_GALLEY": "upper-deck galley fixtures",
    "A380X_CABIN_FLOOR": "main-deck structural floor",
    "A380X_UPPER_DECK_FLOOR": "upper-deck structural floor",
    "A380X_CABIN_CARPET_SEWING": "carpet seams and edge detail",
    "A380X_MAIN_DECK_CARPET": "main-deck carpet surface",
    "A380X_CABIN_NTF": "cabin notices and signs",
}


def material_coverage(document: dict) -> dict[str, dict]:
    """Return guarded source coverage for every selected cabin material."""
    materials = document.get("materials", [])
    by_name: dict[str, tuple[int, dict]] = {}
    for index, material in enumerate(materials):
        name = material.get("name")
        if name in CABIN_MATERIAL_ROLES:
            if name in by_name:
                raise ValueError(f"Duplicate cabin material in source: {name}")
            by_name[name] = (index, material)
    missing = set(CABIN_MATERIAL_ROLES) - set(by_name)
    if missing:
        raise ValueError(f"Detailed cabin materials missing from source: {sorted(missing)}")

    result = {
        name: {
            "material_index": index,
            "role": CABIN_MATERIAL_ROLES[name],
            "triangles": 0,
            "primitive_instances": 0,
            "nodes": [],
        }
        for name, (index, _material) in by_name.items()
    }
    indices_to_names = {index: name for name, (index, _material) in by_name.items()}
    for name, (_index, material) in by_name.items():
        extensions = material.get("extensions", {})
        if material.get("alphaMode", "OPAQUE") != "OPAQUE" or any(
            key in extensions
            for key in ("ASOBO_material_invisible", "ASOBO_material_environment_occluder")
        ):
            raise ValueError(f"Selected cabin material is not visibly opaque: {name}")

    for node_index, node in enumerate(document.get("nodes", [])):
        if "mesh" not in node:
            continue
        for primitive in document["meshes"][node["mesh"]].get("primitives", []):
            name = indices_to_names.get(primitive.get("material"))
            if name is None:
                continue
            if primitive.get("mode", 4) != 4 or "indices" not in primitive:
                raise ValueError(f"Cabin material uses unsupported primitive geometry: {name}")
            draw = primitive.get("extras", {}).get("ASOBO_primitive", {})
            triangles = draw.get("PrimitiveCount")
            if triangles is None:
                index_count = document["accessors"][primitive["indices"]]["count"]
                if index_count % 3:
                    raise ValueError(f"Cabin material has incomplete triangle indices: {name}")
                triangles = index_count // 3
            if not isinstance(triangles, int) or isinstance(triangles, bool) or triangles <= 0:
                raise ValueError(f"Cabin material has an invalid triangle count: {name}")
            item = result[name]
            item["triangles"] += triangles
            item["primitive_instances"] += 1
            item["nodes"].append({"index": node_index, "name": node.get("name", "")})

    empty = [name for name, item in result.items() if not item["triangles"]]
    if empty:
        raise ValueError(f"Selected cabin materials contain no geometry: {empty}")
    return dict(sorted(result.items()))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--asset-root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--max-size", type=int, default=2048)
    parser.add_argument("--detail-max-size", type=int, default=1024)
    parser.add_argument("--reuse-decoded", action="store_true")
    args = parser.parse_args()

    document = json.loads(args.source.read_text(encoding="utf-8"))
    coverage = material_coverage(document)
    project = Path(__file__).resolve().parents[1]
    texture_dir = args.build_dir / "textures" / "cabin-lod00"
    manifest = args.build_dir / "manifests" / "cabin-exports.json"
    report_path = args.build_dir / "reports" / "cabin-materials.json"
    command = [
        sys.executable,
        str(project / "blender" / "prepare_interior_materials.py"),
        str(args.source),
        "--asset-root", str(args.asset_root),
        "--texture-dir", str(texture_dir),
        "--manifest", str(manifest),
        "--report", str(report_path),
        "--prefix", "cabin",
        "--max-size", str(args.max_size),
        "--detail-max-size", str(args.detail_max_size),
    ]
    for name in CABIN_MATERIAL_ROLES:
        command.extend(("--include-material", "^" + re.escape(name) + "$"))
    if args.reuse_decoded:
        command.append("--reuse-decoded")
    subprocess.run(command, check=True)

    manifest_document = json.loads(manifest.read_text(encoding="utf-8"))
    exported = {
        material
        for variant in manifest_document["exports"]
        for material in variant.get("materials", [])
    }
    if exported != set(CABIN_MATERIAL_ROLES):
        raise RuntimeError(
            "Prepared cabin material set differs from the guarded selection: "
            f"missing={sorted(set(CABIN_MATERIAL_ROLES) - exported)}, "
            f"unexpected={sorted(exported - set(CABIN_MATERIAL_ROLES))}"
        )

    report = json.loads(report_path.read_text(encoding="utf-8"))
    report.update(
        {
            "source_lod": "A380_CABIN_LOD00",
            "material_roles": coverage,
            "source_triangles_selected": sum(item["triangles"] for item in coverage.values()),
            "export_variants": len(manifest_document["exports"]),
            "omitted_shader_classes": [
                "transparent passenger/cabin glass",
                "rain overlays",
                "transparent ceiling shades",
                "invisible cabin bounds",
                "unclassified validator default material",
            ],
        }
    )
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "manifest": str(manifest),
                "texture_directory": str(texture_dir),
                "materials": len(exported),
                "source_triangles": report["source_triangles_selected"],
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
