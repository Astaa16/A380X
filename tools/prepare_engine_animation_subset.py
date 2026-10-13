#!/usr/bin/env python3
"""Prepare a small engine-only export from an already normalized wing glTF.

Geometry accessors, UVs, nodes, skins and animation channels are reused intact.
Buffers remain read-only references to the normalized input, avoiding copies of
large wing buffers. Full exterior exports need only animation_datarefs.json;
this utility supplies a focused rebuild for the five existing engine variants.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

ENGINE_VARIANTS = frozenset({
    "a380x_eng_lh", "a380x_eng_rh", "a380x_eng_cores_lh",
    "a380x_eng_cores_rh", "a380x_fans",
})


def prepare_subset(document: dict, materials: set[str]) -> dict:
    """Drop unrelated mesh draws, preserving every retained mesh primitive."""
    document = json.loads(json.dumps(document))
    selected = {index for index, material in enumerate(document["materials"])
                if material["name"] in materials}
    found = {document["materials"][index]["name"] for index in selected}
    if found != materials:
        raise ValueError(f"Missing engine materials: {sorted(materials - found)}")
    meshes, remap = [], {}
    for index, mesh in enumerate(document["meshes"]):
        primitives = [primitive for primitive in mesh["primitives"]
                      if primitive.get("material") in selected]
        if primitives:
            remap[index] = len(meshes)
            meshes.append({**mesh, "primitives": primitives})
    if not meshes:
        raise ValueError("Engine material subset has no geometry")
    document["meshes"] = meshes
    for node in document["nodes"]:
        if "mesh" not in node:
            continue
        if node["mesh"] in remap:
            node["mesh"] = remap[node["mesh"]]
        else:
            node.pop("mesh")
            node.pop("skin", None)
    return document


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    document = json.loads(source.read_text())
    if "a380x_xp12_subset" not in document.get("extras", {}):
        raise ValueError("Source must be a normalized glTF from prepare_gltf_subset.py")
    original_manifest = json.loads(args.manifest.read_text())
    variants = [dict(row) for row in original_manifest["exports"] if row["name"] in ENGINE_VARIANTS]
    if {row["name"] for row in variants} != ENGINE_VARIANTS:
        raise ValueError("Wing manifest must contain all five existing engine variants")
    for row in variants:
        for field in ("texture", "texture_map_normal", "texture_map_material_gloss"):
            if row.get(field):
                row[field] = str((args.manifest.parent / row[field]).resolve())
    document = prepare_subset(document, {name for row in variants for name in row["materials"]})
    for buffer in document["buffers"]:
        buffer["uri"] = str((source.parent / buffer["uri"]).resolve())
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output = args.output_dir / "engines.gltf"
    if output.resolve() == source:
        raise ValueError("Refusing to overwrite normalized wing source")
    output.write_text(json.dumps(document, indent=2) + "\n")
    (args.output_dir / "exports.json").write_text(json.dumps({"exports": variants}, indent=2) + "\n")
    print(json.dumps({"source": str(source), "output": str(output), "meshes": len(document["meshes"])}))


if __name__ == "__main__":
    main()
