#!/usr/bin/env python3
"""Animate the eight FBW DU knobs in existing X-Plane cockpit panel OBJs.

Uses the source glTF poses and exact knob triangles. Existing panel geometry,
texture paths, center-right motion, and other animation blocks are retained.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from patch_center_right_animations import source_targets, wrap_geometry
from patch_efis_animations import vertex_payload


DU_MOTIONS = tuple(
    (f"KNOB_EFIS_{short}_{node}", f"KNOB_EFIS_{short}_{node}",
     f"du/{side}_{channel}", 1)
    for short, side in (("CS", "captain"), ("FO", "first_officer"))
    for node, channel in (("PFD", "pfd"), ("ND_SMALL", "nd"),
                          ("ND", "wx_terr"), ("MFD", "mfd"))
)
MARKER = "# DU KNOB MOTION "


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--object", required=True, action="append", type=Path)
    parser.add_argument("--backup", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()

    raw_targets = source_targets(args.source, motions=DU_MOTIONS, include_child_points=True)
    # The two ND inner knobs share their axis with a small authored light
    # overlay. Their vertices can coincide, so animate each pair as one target.
    grouped = {}
    for target in raw_targets:
        key = (target["dataref"], target["material"])
        if key not in grouped:
            grouped[key] = dict(target)
            continue
        combined = grouped[key]
        combined["low"] = tuple(min(a, b) for a, b in zip(combined["low"], target["low"]))
        combined["high"] = tuple(max(a, b) for a, b in zip(combined["high"], target["high"]))
        combined["count"] += target["count"]
        combined["points"] = (combined["points"] or []) + (target["points"] or [])
    targets = list(grouped.values())
    staged = []
    for path in args.object:
        original = path.read_text()
        changed, meshes, gear_count = wrap_geometry(original, path.name, targets, MARKER)
        if gear_count:
            raise ValueError(f"{path}: unexpected legacy gear dataref")
        if vertex_payload(original) != vertex_payload(changed):
            raise ValueError(f"{path}: vertex, index, UV, or texture payload changed")
        if meshes and len(meshes) != len(targets):
            raise ValueError(f"{path}: expected {len(targets)} animated meshes, found {len(meshes)}")
        staged.append((path, original, changed, meshes))
    if not any(meshes for _, _, _, meshes in staged) and not all(MARKER in old for _, old, _, _ in staged):
        raise ValueError("No DU knob geometry found")

    result = []
    for path, original, changed, meshes in staged:
        digest = hashlib.sha256(original.encode()).hexdigest()
        backup = args.backup / (digest[:16] + "-" + path.name)
        if changed != original:
            backup.parent.mkdir(parents=True, exist_ok=True)
            if not backup.exists():
                backup.write_text(original)
            path.write_text(changed)
        result.append({"path": str(path), "meshes": meshes,
                       "before_sha256": digest, "backup": str(backup) if changed != original else None})
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"files": len(result), "patched": sum(bool(row["meshes"]) for row in result),
                      "meshes": sum(len(row["meshes"]) for row in result)}))


if __name__ == "__main__":
    main()
