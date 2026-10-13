#!/usr/bin/env python3
"""Inventory glTF nodes and animation channels without requiring Blender."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


def summarize(path: Path) -> dict[str, Any]:
    document = json.loads(path.read_text(encoding="utf-8"))
    nodes = document.get("nodes", [])
    accessors = document.get("accessors", [])
    animations = []
    for animation_index, animation in enumerate(document.get("animations", [])):
        channels = []
        for channel in animation.get("channels", []):
            target = channel.get("target", {})
            sampler_index = channel.get("sampler", 0)
            sampler = animation.get("samplers", [])[sampler_index]
            input_index = sampler.get("input")
            output_index = sampler.get("output")
            input_accessor = accessors[input_index] if input_index is not None else {}
            output_accessor = accessors[output_index] if output_index is not None else {}
            node_index = target.get("node")
            channels.append(
                {
                    "node_index": node_index,
                    "node_name": nodes[node_index].get("name", f"node_{node_index}") if node_index is not None else None,
                    "path": target.get("path"),
                    "interpolation": sampler.get("interpolation", "LINEAR"),
                    "keyframes": input_accessor.get("count", 0),
                    "time_min": input_accessor.get("min"),
                    "time_max": input_accessor.get("max"),
                    "output_type": output_accessor.get("type"),
                }
            )
        animations.append(
            {
                "index": animation_index,
                "name": animation.get("name", f"animation_{animation_index}"),
                "channels": channels,
            }
        )
    return {
        "path": str(path),
        "nodes": len(nodes),
        "meshes": len(document.get("meshes", [])),
        "materials": len(document.get("materials", [])),
        "animations": animations,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    if arguments.input.is_file():
        files = [arguments.input]
    else:
        files = sorted(path for path in arguments.input.rglob("*.gltf") if not path.name.startswith("._"))
    file_reports = [summarize(path) for path in files]
    animations = [animation for file_report in file_reports for animation in file_report["animations"]]
    channels = [channel for animation in animations for channel in animation["channels"]]
    report = {
        "format": 1,
        "files": file_reports,
        "summary": {
            "gltf_files": len(file_reports),
            "nodes": sum(file_report["nodes"] for file_report in file_reports),
            "meshes": sum(file_report["meshes"] for file_report in file_reports),
            "materials": sum(file_report["materials"] for file_report in file_reports),
            "animations": len(animations),
            "channels": len(channels),
            "keyframes": sum(channel["keyframes"] for channel in channels),
        },
    }
    payload = json.dumps(report, indent=2)
    if arguments.output:
        arguments.output.parent.mkdir(parents=True, exist_ok=True)
        arguments.output.write_text(payload, encoding="utf-8")
    else:
        print(payload)


if __name__ == "__main__":
    main()
