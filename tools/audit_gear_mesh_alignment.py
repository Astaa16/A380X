#!/usr/bin/env python3
"""Compare deployed tire meshes with the ACF's physical ground contact points.

This is a static geometry check. It cannot establish suspension loading or
ground contact in X-Plane, but it catches a tire that visibly hovers even when
its physics contact point is on the runway.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "blender"))
from obj8_preview import parse_obj8


FEET_TO_METRES = 0.3048
GEARS = (
    (0, "gear-nose"),
    (1, "gear-wing-lh"),
    (2, "gear-wing-rh"),
    (3, "gear-body-lh"),
    (4, "gear-body-rh"),
)


def acf_values(path: Path) -> dict[str, str]:
    values = {}
    for line in path.read_text(encoding="latin1").splitlines():
        if line.startswith("P "):
            _, key, value = line.split(" ", 2)
            values[key] = value
    return values


def audit(package: Path) -> dict[str, object]:
    values = acf_values(package / "A380X-XP12.acf")
    object_attachments = {
        key.removesuffix("/_v10_att_file_stl"): path
        for key, path in values.items()
        if key.endswith("/_v10_att_file_stl")
    }
    rows = []
    for index, group in GEARS:
        suffix = f"exterior/{group}/{group.replace('-', '_')}_a380_exterior_tyres.obj"
        attachments = [key for key, path in object_attachments.items() if path == suffix]
        if len(attachments) != 1:
            raise ValueError(f"Expected one attachment for {suffix}: {attachments}")
        attachment = attachments[0]
        tire_obj = package / "objects" / suffix
        mesh = parse_obj8(
            tire_obj.read_text().splitlines(),
            values={f"sim/flightmodel2/gear/deploy_ratio[{index}]": 1.0},
        )
        vertices, _, _, _ = mesh.bake()
        # obj8_preview.bake() uses Blender's (x, -z, y) axes, so [2] is
        # X-Plane/OBJ Y. The ACF object attachment offsets are in feet.
        visual_bottom_m = min(vertex[2] for vertex in vertices) + (
            float(values[f"{attachment}/_v10_att_y_acf_prt_ref"]) * FEET_TO_METRES
        )
        gear = f"_gear/{index}"
        if float(values[f"{gear}/_latE"]) or float(values[f"{gear}/_lonE"]):
            raise ValueError(f"{gear} is tilted; extend contact geometry calculation")
        contact_m = (
            float(values[f"{gear}/_gear_y"])
            - float(values[f"{gear}/_leg_len"])
            - float(values[f"{gear}/_tire_radius"])
        ) * FEET_TO_METRES
        rows.append({
            "gear": group,
            "visual_bottom_m": round(visual_bottom_m, 5),
            "physical_contact_m": round(contact_m, 5),
            "visual_above_contact_m": round(visual_bottom_m - contact_m, 5),
        })
    return {"package": str(package), "gear": rows}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--strict", action="store_true", help="fail if a tire differs by more than 0.1 m")
    args = parser.parse_args()
    report = audit(args.package)
    print(json.dumps(report, indent=2))
    if args.strict and any(abs(row["visual_above_contact_m"]) > 0.1 for row in report["gear"]):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
