#!/usr/bin/env python3
"""Bind the authored ADIRS selector and pushbutton meshes to native state.

Only draw commands change. The nine source animations supply their own pivots,
axes, detents and travel; the six pushbuttons' twelve legends travel with them.
All geometry, indices, UVs and texture references are retained byte for byte.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from patch_center_right_animations import source_targets, wrap_geometry
from patch_efis_animations import vertex_payload


ADIRS_MOTIONS = tuple(
    motion
    for unit in range(1, 4)
    for motion in (
        (f"KNOB_OVHD_ADIRS_{unit}", f"KNOB_OVHD_ADIRS_{unit}", f"adirs/ir{unit}_knob", 2),
        (f"PUSH_OVHD_ADIRS_IR{unit}", f"PUSH_OVHD_ADIRS_IR{unit}", f"adirs/ir{unit}_pressed", 1),
        (f"PUSH_OVHD_ADIRS_{unit}", f"PUSH_OVHD_ADIRS_{unit}", f"adirs/adr{unit}_pressed", 1),
    )
)
MARKER = "# ADIRS MOTION "


def adirs_targets(source):
    return source_targets(source, motions=ADIRS_MOTIONS)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--directory", required=True, action="append", type=Path)
    parser.add_argument("--backup", type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if not args.dry_run and args.backup is None:
        parser.error("--backup is required unless --dry-run is used")
    targets = adirs_targets(args.source)
    staged = []
    for directory in args.directory:
        relevant = [path for path in sorted(directory.glob("*.obj"))
                    if not path.name.startswith("._") and any(
                        path.name.endswith(t["material"] + ".obj") for t in targets)]
        found_materials = {t["material"] for t in targets for path in relevant
                           if path.name.endswith(t["material"] + ".obj")}
        missing = {t["material"] for t in targets} - found_materials
        if missing:
            raise ValueError(f"{directory}: missing ADIRS material objects: {sorted(missing)}")
        for path in relevant:
            original = path.read_text()
            changed, meshes, gear_count = wrap_geometry(original, path.name, targets, MARKER)
            if gear_count:
                raise ValueError(f"{path}: unexpected legacy gear dataref")
            if vertex_payload(original) != vertex_payload(changed):
                raise ValueError(f"{path}: geometry, UV or texture payload changed")
            staged.append((path, original, changed, meshes))
    report = []
    # All material objects are validated before any object is replaced.
    for path, original, changed, meshes in staged:
        digest = hashlib.sha256(original.encode()).hexdigest()
        backup = args.backup / (digest[:16] + "-" + path.name) if args.backup else None
        if changed != original and not args.dry_run:
            backup.parent.mkdir(parents=True, exist_ok=True)
            if not backup.exists():
                backup.write_text(original)
            path.write_text(changed)
        report.append(dict(path=str(path), meshes=meshes, before_sha256=digest,
                           backup=str(backup) if backup and changed != original else None))
    summary = dict(dry_run=args.dry_run, files=report,
                   source_targets=[{k: v for k, v in t.items() if k != "points"} for t in targets])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(dict(files=len(report), meshes=sum(len(r["meshes"]) for r in report),
                          report=str(args.report))))


if __name__ == "__main__":
    main()
