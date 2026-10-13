#!/usr/bin/env python3
"""Prepare the omitted FBW MIP03 material for a dedicated cockpit OBJ export.

This only writes a build directory. It neither exports Blender geometry nor
modifies the installed aircraft or the shared cockpit material manifest.
The output manifest retains source UVs, alpha masking, and both face sides.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops

from patch_cockpit_materials import (
    DEFAULT_SOURCE, SourceTextures, atomic_write, digest, make_materials, png_bytes,
)


MATERIAL = "A380_COCKPIT_MIP03_4k"
EXPORT_NAME = "panel_a380_cockpit_mip03_4k"
LIGHT_DATAREF = "a380x/xp12/lighting/integral"


def source_coverage(document: dict) -> dict:
    """Inventory every node instance using the exact source material."""
    matches = [(index, material) for index, material in enumerate(document.get("materials", []))
               if material.get("name") == MATERIAL]
    if len(matches) != 1:
        raise ValueError(f"Expected one {MATERIAL} material, found {len(matches)}")
    material_index, material = matches[0]
    if material.get("alphaMode") != "MASK" or not material.get("doubleSided"):
        raise ValueError("MIP03 source material no longer has the expected MASK/double-sided configuration")
    if float(material.get("alphaCutoff", 0.5)) != 0.5:
        raise ValueError("MIP03 alpha cutoff changed; review export settings before preparation")

    primitives = []
    for node_index, node in enumerate(document.get("nodes", [])):
        if "mesh" not in node:
            continue
        mesh_index = node["mesh"]
        for primitive_index, primitive in enumerate(document["meshes"][mesh_index].get("primitives", [])):
            if primitive.get("material") != material_index:
                continue
            if primitive.get("mode", 4) != 4:
                raise ValueError(f"MIP03 node {node_index} contains a non-triangle primitive")
            attributes = primitive.get("attributes", {})
            required = {"POSITION", "NORMAL", "TEXCOORD_0"}
            if not required.issubset(attributes):
                raise ValueError(f"MIP03 node {node_index} is missing {sorted(required - attributes.keys())}")
            position = document["accessors"][attributes["POSITION"]]
            vertex_count = position["count"]
            if position.get("type") != "VEC3" or vertex_count <= 0:
                raise ValueError(f"Invalid MIP03 position accessor on node {node_index}")
            for key, expected_type in (("NORMAL", "VEC3"), ("TEXCOORD_0", "VEC2")):
                accessor = document["accessors"][attributes[key]]
                packed_asobo_normal = (key == "NORMAL" and accessor.get("type") == "VEC4"
                                       and accessor.get("componentType") == 5120
                                       and "ASOBO_primitive" in primitive.get("extras", {}))
                if (accessor.get("type") != expected_type and not packed_asobo_normal) or accessor["count"] != vertex_count:
                    raise ValueError(f"MIP03 {key} accessor does not cover all vertices on node {node_index}")
            if "indices" in primitive:
                indices = document["accessors"][primitive["indices"]]
                if indices.get("type") != "SCALAR":
                    raise ValueError(f"Invalid MIP03 index accessor on node {node_index}")
                index_count = indices["count"]
            else:
                index_count = vertex_count
            if index_count <= 0 or index_count % 3:
                raise ValueError(f"MIP03 triangle indices are incomplete on node {node_index}")
            triangle_count = index_count // 3
            authored = primitive.get("extras", {}).get("ASOBO_primitive", {})
            if authored.get("PrimitiveCount", triangle_count) != triangle_count:
                raise ValueError(f"ASOBO triangle count mismatch on MIP03 node {node_index}")
            if authored.get("VertexCount", vertex_count) != vertex_count:
                raise ValueError(f"ASOBO vertex count mismatch on MIP03 node {node_index}")
            primitives.append({
                "node_index": node_index, "node_name": node.get("name", ""),
                "mesh_index": mesh_index, "primitive_index": primitive_index,
                "vertices": vertex_count, "triangles": triangle_count,
                "position_accessor": attributes["POSITION"],
                "uv_accessor": attributes["TEXCOORD_0"],
                "indices_accessor": primitive.get("indices"),
            })
    if not primitives or not sum(item["triangles"] for item in primitives):
        raise ValueError("MIP03 material has no triangle geometry on any source node")
    return {
        "material_index": material_index,
        "node_count": len({item["node_index"] for item in primitives}),
        "mesh_count": len({item["mesh_index"] for item in primitives}),
        "primitive_instances": len(primitives),
        "vertices": sum(item["vertices"] for item in primitives),
        "triangles": sum(item["triangles"] for item in primitives),
        "primitives": primitives,
    }


def prepare(source_path: Path, build_dir: Path, max_size: int) -> dict:
    source_path, build_dir = source_path.resolve(), build_dir.resolve()
    if max_size < 1 or max_size & (max_size - 1):
        raise ValueError("--max-size must be a positive power of two")
    source = SourceTextures(source_path, source_path.parent.parent / "texture", max_size)
    coverage = source_coverage(source.doc)
    material = source.materials[MATERIAL]
    images = make_materials(source, material, "mip")
    if material.get("emissiveTexture"):
        emission = source.load(material["emissiveTexture"])
        # OBJ8 adds LIT alpha to albedo alpha, which would fill masked holes.
        # Preserve source alpha in the albedo only; emission is RGB on black.
        # The MSFS emissive factor of 1000 is deliberately not baked into LIT.
        alpha = emission.getchannel("A")
        images["lit"] = Image.merge("RGB", tuple(
            ImageChops.multiply(channel, alpha) for channel in emission.split()[:3]
        ))

    texture_dir = build_dir / "textures/cockpit-panels"
    manifest_path = build_dir / "manifests/cockpit-mip03-exports.json"
    report_path = build_dir / "reports/cockpit-mip03-preparation.json"
    roles = {
        "albedo": "texture", "normal": "texture_map_normal",
        "material_gloss": "texture_map_material_gloss", "lit": "texture_lit",
    }
    entry = {
        "name": EXPORT_NAME, "materials": [MATERIAL],
        "alpha_mode": "MASK", "alpha_cutoff": 0.5,
        "double_sided": True, "polygon_offset": 0,
        "normal_metalness": True, "specular": 1.0,
    }
    if "lit" in images:
        entry["lit_level_dataref"] = LIGHT_DATAREF
    prepared, outputs = [], {}
    for role, image in images.items():
        destination = texture_dir / f"mip03_restoration_{role}.png"
        content = png_bytes(image)
        entry[roles[role]] = str(destination)
        prepared.append((destination, content))
        outputs[role] = {"path": str(destination), "size": list(image.size),
                         "mode": image.mode, "sha256": digest(content), "bytes": len(content)}

    report = {
        "source": str(source_path), "source_sha256": digest(source_path.read_bytes()),
        "material": MATERIAL, "source_geometry": coverage,
        "max_texture_size": max_size, "source_assets": source.provenance,
        "textures": outputs, "manifest": str(manifest_path), "report": str(report_path),
        "emission": {
            "included": "lit" in images, "light_dataref": LIGHT_DATAREF if "lit" in images else None,
            "source_factor": material.get("emissiveFactor", [0, 0, 0]),
            "source_factor_applied": False,
            "encoding": "RGB with source alpha multiplied into RGB; no LIT alpha",
        },
        "scope": "Dedicated preparation only; no installed aircraft or global manifest changes.",
        "geometry_exported": False,
        "verification_needed": [
            "Export all listed node primitives through the existing FBW Blender conversion.",
            "Compare exported triangle count and authored UV sets before installation.",
            "Inspect the restored MIP geometry in X-Plane after aircraft reload.",
        ],
    }
    # Complete all decoding and coverage checks before publishing any output.
    for destination, content in prepared:
        atomic_write(destination, content)
    atomic_write(manifest_path, (json.dumps({"exports": [entry]}, indent=2) + "\n").encode())
    atomic_write(report_path, (json.dumps(report, indent=2) + "\n").encode())
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--max-size", type=int, default=4096)
    args = parser.parse_args()
    report = prepare(args.source, args.build_dir, args.max_size)
    print(json.dumps({"manifest": report["manifest"], "report": report["report"],
                      "source_nodes": report["source_geometry"]["node_count"],
                      "source_triangles": report["source_geometry"]["triangles"],
                      "textures": len(report["textures"])}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
