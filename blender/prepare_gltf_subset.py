#!/usr/bin/env python3
"""Prepare an animation-preserving material subset of an optimized FBW glTF.

This supports the upstream split .bin.partNN files without modifying source
assets, preserves ancestors and skin joints, and prunes unused geometry before
Blender allocates it. The material names come from an export manifest.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
from pathlib import Path

from normalize_msfs_gltf import normalize_asobo_primitive_ranges, normalize_optimized_vertex_attributes, promote_msfs_texture_sources, strip_texture_references
from normalize_msfs_winding import normalize_msfs_winding
from repair_fbw_tail_bindshape import protected_tail_joint_accessors, repair_fbw_tail_bindshape
from repair_fbw_wing_joints import repair_fbw_wing_joints


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument(
        "--node-pattern",
        action="append",
        default=[],
        help=(
            "Only retain mesh-bearing nodes whose complete source name matches "
            "one of these regular expressions. Ancestors and skin joints are "
            "still preserved. May be supplied more than once."
        ),
    )
    parser.add_argument(
        "--repair-fbw-tail-bindshape", action="store_true",
        help="Opt in to the source-specific experimental horizontal-tail bind-shape correction",
    )
    parser.add_argument("--repair-fbw-wing-joints", action="store_true",
                        help="Decode the two audited optimized wing skins' one-based joint palette")
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        raise ValueError("Refusing to overwrite upstream source")
    document = json.loads(args.source.read_text())
    manifest = json.loads(args.manifest.read_text())
    variants = manifest.get("exports", []) if isinstance(manifest, dict) else manifest
    selected_names = {name for entry in variants for name in entry.get("materials", [])}
    selected_materials = {i for i, material in enumerate(document.get("materials", [])) if material["name"] in selected_names}
    node_patterns = [re.compile(pattern) for pattern in args.node_pattern]
    source_nodes = document.get("nodes", [])
    selected_node_indices = {
        i
        for i, node in enumerate(source_nodes)
        if not node_patterns or any(pattern.fullmatch(node.get("name", "")) for pattern in node_patterns)
    }
    selected_mesh_indices = (
        {
            source_nodes[i]["mesh"]
            for i in selected_node_indices
            if "mesh" in source_nodes[i]
        }
        if node_patterns
        else None
    )
    meshes, mesh_remap = [], {}
    for i, mesh in enumerate(document.get("meshes", [])):
        if selected_mesh_indices is not None and i not in selected_mesh_indices:
            continue
        primitives = [p for p in mesh.get("primitives", []) if p.get("material") in selected_materials]
        if primitives:
            mesh_remap[i] = len(meshes)
            meshes.append({**mesh, "primitives": primitives})
    if not meshes:
        raise ValueError("Selected material subset contains no meshes")
    parents = {child: i for i, node in enumerate(source_nodes) for child in node.get("children", [])}
    keep = {
        i
        for i, node in enumerate(source_nodes)
        if i in selected_node_indices and node.get("mesh") in mesh_remap
    }
    used_skins = {source_nodes[i]["skin"] for i in keep if "skin" in source_nodes[i]}
    for i in used_skins:
        skin = document["skins"][i]
        keep.update(skin["joints"])
        if "skeleton" in skin:
            keep.add(skin["skeleton"])
    for i in list(keep):
        while i in parents:
            i = parents[i]
            keep.add(i)
    node_remap = {old: new for new, old in enumerate(sorted(keep))}
    skin_remap = {old: new for new, old in enumerate(sorted(used_skins))}
    nodes = []
    for old in sorted(keep):
        node = source_nodes[old]
        if node.get("mesh") in mesh_remap:
            node["mesh"] = mesh_remap[node["mesh"]]
        else:
            node.pop("mesh", None)
            node.pop("skin", None)
        if "skin" in node:
            node["skin"] = skin_remap[node["skin"]]
        if "children" in node:
            node["children"] = [node_remap[i] for i in node["children"] if i in keep]
        node.pop("camera", None)
        nodes.append(node)
    skins = []
    for old in sorted(used_skins):
        skin = document["skins"][old]
        skin["joints"] = [node_remap[i] for i in skin["joints"]]
        if "skeleton" in skin:
            skin["skeleton"] = node_remap[skin["skeleton"]]
        skins.append(skin)
    animations = []
    for animation in document.get("animations", []):
        channels = []
        for channel in animation.get("channels", []):
            old = channel.get("target", {}).get("node")
            if old in keep:
                channel["target"]["node"] = node_remap[old]
                channels.append(channel)
        if channels:
            animations.append({**animation, "channels": channels})
    for scene in document.get("scenes", []):
        scene["nodes"] = [node_remap[i] for i in scene.get("nodes", []) if i in keep]
    original_counts = {"meshes": len(document.get("meshes", [])), "nodes": len(source_nodes), "animations": len(document.get("animations", []))}
    document.update(meshes=meshes, nodes=nodes, skins=skins, animations=animations)
    document.pop("cameras", None)
    promote_msfs_texture_sources(document)
    strip_texture_references(document)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    for i, buffer in enumerate(document["buffers"]):
        source_buffer = args.source.parent / buffer["uri"].replace("\\", "/")
        destination = args.output.parent / "buffers" / f"buffer-{i:03d}.bin"
        destination.parent.mkdir(parents=True, exist_ok=True)
        if source_buffer.is_file():
            shutil.copy2(source_buffer, destination)
        else:
            parts = sorted(source_buffer.parent.glob(source_buffer.name + ".part[0-9][0-9]"))
            if not parts or sum(p.stat().st_size for p in parts) != buffer["byteLength"]:
                raise ValueError(f"Missing/incomplete split buffer {source_buffer}")
            with destination.open("wb") as output:
                for part in parts:
                    with part.open("rb") as source:
                        shutil.copyfileobj(source, output)
        buffer["uri"] = destination.relative_to(args.output.parent).as_posix()
    wing_joints = repair_fbw_wing_joints(document, args.output) if args.repair_fbw_wing_joints else {"status": "disabled"}
    draw_ranges = normalize_asobo_primitive_ranges(document, args.output)
    normalized = normalize_optimized_vertex_attributes(
        document, args.output, protected_joint_accessors=protected_tail_joint_accessors(document)
        if args.repair_fbw_tail_bindshape else None)
    tail_bindshape = repair_fbw_tail_bindshape(document, args.output) if args.repair_fbw_tail_bindshape else {"status": "disabled"}
    winding = normalize_msfs_winding(document, args.output)
    winding_summary = {key: value for key, value in winding.items() if key != "primitives"}
    report = {"source": str(args.source), "manifest": str(args.manifest), "node_patterns": args.node_pattern, "original_counts": original_counts, "subset_counts": {"meshes": len(meshes), "nodes": len(nodes), "animations": len(animations)}, "selected_materials": sorted(selected_names), "draw_ranges": draw_ranges, "normalization": normalized, "winding": winding_summary, "experimental_tail_bindshape": tail_bindshape}
    report["wing_joint_palette"] = wing_joints
    document.setdefault("extras", {})["a380x_xp12_subset"] = report
    args.output.write_text(json.dumps(document, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
