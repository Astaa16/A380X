#!/usr/bin/env python3
"""Prepare the FlyByWire passenger seats for static X-Plane OBJ export.

The five authored seat/suite materials retain their albedo, normal and material
maps. MSFS supplies the two seatback displays at runtime, so this development
port renders those exact surfaces as dark powered-off screens instead of white
or missing faces. The invisible helper material is deliberately excluded.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image


SEAT_MATERIALS = (
    "A380X_BC_SEATS",
    "A380X_MAIN_DECK_CAS_SEATS",
    "A380X_UPPER_DECK_SUITES",
    "A380X_MAIN_DECK_PEC_SEATS",
    "A380X_MAIN_DECK_YC_SEAT",
)
SCREEN_MATERIALS = ("$SCREEN_PEC_SEAT", "$SCREEN_YC_SEAT")
EXPECTED_MATERIALS = {*SEAT_MATERIALS, *SCREEN_MATERIALS, "INVISIBLE"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--asset-root", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--max-size", type=int, default=2048)
    parser.add_argument("--detail-max-size", type=int, default=1024)
    parser.add_argument("--reuse-decoded", action="store_true")
    args = parser.parse_args()

    document = json.loads(args.source.read_text())
    actual_materials = {material["name"] for material in document.get("materials", [])}
    if actual_materials != EXPECTED_MATERIALS:
        raise ValueError(
            "Passenger-seat material inventory changed: "
            f"missing={sorted(EXPECTED_MATERIALS - actual_materials)}, "
            f"unexpected={sorted(actual_materials - EXPECTED_MATERIALS)}"
        )

    project = Path(__file__).resolve().parents[1]
    texture_dir = args.build_dir / "textures" / "cabin-seats"
    manifest = args.build_dir / "manifests" / "cabin-seat-exports.json"
    material_report = args.build_dir / "reports" / "cabin-seat-materials.json"
    command = [
        sys.executable,
        str(project / "blender" / "prepare_interior_materials.py"),
        str(args.source),
        "--asset-root",
        str(args.asset_root),
        "--texture-dir",
        str(texture_dir),
        "--manifest",
        str(manifest),
        "--report",
        str(material_report),
        "--prefix",
        "cabin_seats",
        "--max-size",
        str(args.max_size),
        "--detail-max-size",
        str(args.detail_max_size),
    ]
    for material in SEAT_MATERIALS:
        command.extend(("--include-material", f"^{material}$"))
    if args.reuse_decoded:
        command.append("--reuse-decoded")
    subprocess.run(command, check=True)

    dark = texture_dir / "powered_off_seatback_screens.png"
    gloss = texture_dir / "powered_off_seatback_material_gloss.png"
    Image.new("RGBA", (4, 4), (5, 6, 8, 255)).save(dark)
    Image.new("RGBA", (4, 4), (0, 0, 0, 255)).save(gloss)

    manifest_document = json.loads(manifest.read_text())
    exported_materials = {
        material
        for variant in manifest_document["exports"]
        for material in variant["materials"]
    }
    if exported_materials != set(SEAT_MATERIALS):
        raise RuntimeError(f"Unexpected prepared seat materials: {sorted(exported_materials)}")
    manifest_document["exports"].append(
        {
            "name": "cabin_seats_powered_off_screens",
            "materials": list(SCREEN_MATERIALS),
            "alpha_mode": "OPAQUE",
            "double_sided": False,
            "texture": str(dark),
            "texture_map_material_gloss": str(gloss),
            "notes": "Static powered-off seatback displays; no simulated passenger-screen pixels.",
        }
    )
    manifest.write_text(json.dumps(manifest_document, indent=2) + "\n")

    report = json.loads(material_report.read_text())
    report.update(
        {
            "seat_materials": list(SEAT_MATERIALS),
            "screen_materials": list(SCREEN_MATERIALS),
            "invisible_helper_excluded": True,
            "export_variants": len(manifest_document["exports"]),
            "powered_off_screen_texture": str(dark),
        }
    )
    material_report.write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            {
                "manifest": str(manifest),
                "texture_directory": str(texture_dir),
                "seat_materials": len(SEAT_MATERIALS),
                "screen_materials": len(SCREEN_MATERIALS),
                "export_variants": len(manifest_document["exports"]),
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
