#!/usr/bin/env python3
"""Verify and stage the authored upper-deck doorway and inner-panel export.

The fixed U1L meshes are intentionally selected by node name because the
upstream glTF assigned them to its generic fallback material together with
unrelated bar and curve objects.  The Blender conversion pass clones this
reviewed jamb/header group to the other five upper-deck openings.
The final door skins and lower slide-pack face follow the exterior
leaves. The obsolete .004_BACKUP skin is deliberately excluded.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path


MATERIAL = "glTFValidator_Added_Default_Material"
SURROUND_NODES = (
    "PAX_DOOR_U1L.001",
    "PAX_DOOR_U1L.003",
)
INNER_PANEL_NODES = (
    "PAX_DOOR_U1L.004",
    "PAX_DOOR_U1L.005",
    "PAX_DOOR_U1L.006",
    "PAX_DOOR_U1L.007",
    "PAX_DOOR_U1L.008",
)
SELECTED_NODES = SURROUND_NODES + INNER_PANEL_NODES
EXPECTED_TRIANGLES = 2216 + 6377
EXPORT_NAME = "cabin_a380x_upper_deck_door_surrounds"


def source_coverage(document: dict) -> dict:
    materials = document.get("materials", [])
    material_indices = {
        index for index, material in enumerate(materials) if material.get("name") == MATERIAL
    }
    if len(material_indices) != 1:
        raise ValueError(f"Expected exactly one {MATERIAL!r} material")
    nodes_by_name = {node.get("name"): node for node in document.get("nodes", [])}
    if any(name not in nodes_by_name for name in SELECTED_NODES):
        missing = [name for name in SELECTED_NODES if name not in nodes_by_name]
        raise ValueError(f"Upper-deck doorway surround nodes are missing: {missing}")

    result = {"nodes": [], "triangles": 0}
    for name in SELECTED_NODES:
        node = nodes_by_name[name]
        if "mesh" not in node:
            raise ValueError(f"Upper-deck doorway surround node has no mesh: {name}")
        primitives = document["meshes"][node["mesh"]].get("primitives", [])
        if not primitives:
            raise ValueError(f"Upper-deck doorway surround node is empty: {name}")
        triangles = 0
        for primitive in primitives:
            if primitive.get("material") not in material_indices:
                raise ValueError(f"Upper-deck doorway surround uses an unexpected material: {name}")
            index_count = document["accessors"][primitive["indices"]]["count"]
            if primitive.get("mode", 4) != 4 or index_count % 3:
                raise ValueError(f"Upper-deck doorway surround is not indexed triangles: {name}")
            triangles += index_count // 3
        result["nodes"].append({"name": name, "triangles": triangles})
        result["triangles"] += triangles
    if result["triangles"] != EXPECTED_TRIANGLES:
        raise ValueError(
            "Upper-deck doorway surround triangle coverage changed: "
            f"{result['triangles']} != {EXPECTED_TRIANGLES}"
        )
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()

    coverage = source_coverage(json.loads(args.source.read_text(encoding="utf-8")))
    manifest = {
        "exports": [
            {
                "name": EXPORT_NAME,
                "materials": [MATERIAL],
                "alpha_mode": "OPAQUE",
            }
        ]
    }
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    report = {
        "source": str(args.source),
        "selection": "fixed jamb/header and animated inner panels including lower slide-pack face; no backup skin",
        "material": MATERIAL,
        "coverage": coverage,
        "instances": ["U1L", "U1R", "U2L", "U2R", "U3L", "U3R"],
        "export_name": EXPORT_NAME,
        "manifest": str(args.manifest),
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
