#!/usr/bin/env python3
"""Restore real FBW display surfaces as opaque powered-off screens.

MSFS supplies the screen pixels at runtime. Until X-Plane avionics are connected,
these authored meshes must remain visible as dark screens rather than holes.
This deliberately excludes windshield, display glass and backlight overlays.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image


SCREEN_MATERIALS = (
    "SCREEN_EFB", "RTPI", "SCREEN_ISIS_1", "CLOCK", "BAT", "SCREEN_ISIS_2",
    "SCREEN_OIT_LEFT", "SCREEN_DU_EWD", "SCREEN_DU_MFD", "SCREEN_DU_NDL",
    "SCREEN_DU_NDR", "SCREEN_DU_PFDL", "SCREEN_DU_PFDR", "SCREEN_DU_SD",
    "SCREEN_OIT_RIGHT", "SCREEN_DU_RMP_1", "SCREEN_DU_RMP_2", "FCU",
    "SCREEN_DU_RMP_3",
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    document = json.loads(args.source.read_text())
    found = {material["name"] for material in document.get("materials", [])}
    missing = set(SCREEN_MATERIALS) - found
    if missing:
        raise ValueError(f"Screen material names absent from source: {sorted(missing)}")
    texture_dir = args.build_dir / "textures" / "cockpit-panels"
    texture_dir.mkdir(parents=True, exist_ok=True)
    dark = texture_dir / "powered_off_display_faces.png"
    gloss = texture_dir / "powered_off_display_material_gloss.png"
    Image.new("RGBA", (4, 4), (5, 6, 8, 255)).save(dark)
    Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(gloss)
    manifest = args.build_dir / "manifests" / "cockpit-screen-exports.json"
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_text(json.dumps({"exports": [{
        "name": "screen_powered_off_display_faces",
        "materials": list(SCREEN_MATERIALS),
        "alpha_mode": "OPAQUE",
        "double_sided": False,
        "texture": str(dark),
        "texture_map_material_gloss": str(gloss),
        "notes": "Static powered-off display surfaces only; no simulated avionics pixels.",
    }]}, indent=2) + "\n")
    normalized = args.build_dir / "normalized" / "cockpit-screens" / "A380_COCKPIT_LOD00.screens.gltf"
    subprocess.run([
        sys.executable, str(Path(__file__).with_name("prepare_gltf_subset.py")),
        str(args.source), str(normalized), "--manifest", str(manifest),
    ], check=True)
    print(json.dumps({"manifest": str(manifest), "normalized_gltf": str(normalized), "screen_materials": len(SCREEN_MATERIALS), "texture": str(dark)}, indent=2))


if __name__ == "__main__":
    main()
