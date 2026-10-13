#!/usr/bin/env python3
"""Re-normalize and export the FBW interior strictly one Blender at a time.

Texture manifests must already exist. All intermediates stay under build-dir.
Blender's Python exception exit code and output reports are checked explicitly.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path


TASKS = (
    # LOD00 retains the authored cockpit-door hierarchy and clip. LOD01 folds
    # that door into static shell geometry, so it cannot support door 17.
    ("cockpit-textured", "Part_Interior_Cockpit", "A380_COCKPIT_LOD00", "cockpit-opaque", "opaque", "cockpit-exports.json", "cockpit-textured", None),
    ("cabin-textured", "Part_Interior_Cabin", "A380_CABIN_LOD00", "cabin-opaque", "opaque", "cabin-exports.json", "cabin-textured", None),
    (
        "cabin-upper-door-surrounds",
        "Part_Interior_Cabin",
        "A380_CABIN_LOD00",
        "upper-door-surround",
        "surrounds",
        "upper_deck_door_surround_exports.json",
        "cabin-textured",
        r"PAX_DOOR_U1L\.(001|003|004|005|006|007|008)",
    ),
    ("cabin-seats-textured", "Part_Interior_Cabin_Seats", "A380_CABIN_SEATS_LOD00", "cabin-seats-opaque", "opaque", "cabin-seat-exports.json", "cabin-seats-textured", None),
    ("cockpit-panels-textured", "Part_Interior_Cockpit", "A380_COCKPIT_LOD00", "cockpit-panels", "panels", "cockpit-panel-exports.json", "cockpit-panels-textured", None),
    ("cockpit-decals-textured", "Part_Interior_Cockpit", "A380_COCKPIT_LOD00", "cockpit-decals", "decals", "cockpit-decal-exports.json", "cockpit-panels-textured", None),
    ("cockpit-screens-textured", "Part_Interior_Cockpit", "A380_COCKPIT_LOD00", "cockpit-screens", "screens", "cockpit-screen-exports.json", "cockpit-panels-textured", None),
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--aircraft-root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--blender", required=True, type=Path)
    parser.add_argument("--blender-user-dir", required=True, type=Path)
    parser.add_argument("--only", choices=[task[0] for task in TASKS], action="append", default=[])
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    aircraft_root = args.aircraft_root.resolve()
    build_dir = args.build_dir.resolve()
    blender = args.blender.resolve()
    blender_user_dir = args.blender_user_dir.resolve()
    reports = build_dir / "reports"
    reports.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.update(BLENDER_USER_RESOURCES=str(blender_user_dir),
               BLENDER_USER_CONFIG=str(blender_user_dir / "config"),
               BLENDER_USER_SCRIPTS=str(blender_user_dir / "scripts"))
    summary = []
    for name, part, model, folder, suffix, manifest_name, export_folder, node_pattern in TASKS:
        if args.only and name not in args.only:
            continue
        started = time.time()
        source = aircraft_root / "attachments" / "flybywire" / part / "model" / f"{model}.gltf"
        normalized = build_dir / "normalized" / folder / f"{model}.{suffix}.gltf"
        manifest = build_dir / "manifests" / manifest_name
        print(f"START {name}: normalization", flush=True)
        normalize_command = [
            sys.executable,
            str(project / "blender/prepare_gltf_subset.py"),
            str(source),
            str(normalized),
            "--manifest",
            str(manifest),
        ]
        if node_pattern is not None:
            normalize_command.extend(("--node-pattern", node_pattern))
        with (reports / f"{name}-normalize.log").open("w") as log:
            subprocess.run(
                normalize_command,
                cwd=project,
                stdout=log,
                stderr=subprocess.STDOUT,
                check=True,
            )
        print(f"START {name}: Blender export", flush=True)
        report = reports / f"{name}.json"
        export_dir = build_dir / "xplane-obj" / export_folder
        with (reports / f"{name}.log").open("w") as log:
            subprocess.run([str(blender), "--background", "--python-exit-code", "1", "--python", str(project / "blender/convert_gltf_animations.py"), "--", "--input", str(normalized), "--mapping", str(project / "config/animation_datarefs.json"), "--output", str(build_dir / "blend" / f"{name}.blend"), "--report", str(report), "--export-dir", str(export_dir), "--export-manifest", str(manifest)], cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        if not report.is_file() or report.stat().st_mtime < started:
            raise RuntimeError(f"No fresh export report for {name}")
        result = json.loads(report.read_text())
        variants = json.loads(manifest.read_text())["exports"]
        actual = {Path(path).name for path in result["exported_obj_files"]}
        expected = {variant["name"] + ".obj" for variant in variants}
        if actual != expected:
            raise RuntimeError(f"Export report mismatch for {name}: {actual ^ expected}")
        for filename in expected:
            if not (export_dir / filename).is_file() or (export_dir / filename).stat().st_mtime < started:
                raise RuntimeError(f"Missing or stale OBJ: {export_dir / filename}")
        normalized_doc = json.loads(normalized.read_text())
        entry = {"group": name, "objects": len(actual), "elapsed_seconds": round(time.time() - started, 2), "normalization": normalized_doc["extras"]["a380x_xp12_subset"], "summary": result["summary"], "report": str(report)}
        summary.append(entry)
        print(f"DONE {name}: {len(actual)} objects in {entry['elapsed_seconds']} seconds", flush=True)
        (reports / "interior-rebuild-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print("INTERIOR BATCH COMPLETE", flush=True)


if __name__ == "__main__":
    main()
