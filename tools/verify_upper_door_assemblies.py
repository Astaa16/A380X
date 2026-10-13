#!/usr/bin/env python3
"""Check exported upper-door frames, inner panels, and hardware as one assembly.

The cabin source's upper U1L hardware is misleadingly named PAX_DOOR_M3L.033
through .049. It was exported as static, unparented geometry while the outer
door moved. The final inner face is .004 + .005 + .006 + .007 + .008 (6,377
triangles), including its vertical lower slide-pack panels; the overlapping
.004_BACKUP is deliberately excluded. The fixed 2,216-triangle jamb/header
surround and 5,555-triangle frame fittings must remain stationary while the
12,352-triangle leaf fittings move with the final inner face and exterior.

This evaluates actual OBJ8 draw transforms, including mirrored copies, rather
than trusting the source configuration or a successful Blender export.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "blender"))
from obj8_preview import load_obj8


DOORS = tuple(f"U{station}{side}" for station in range(1, 4) for side in "LR")
DATAREFS = {
    door: f"sim/flightmodel2/misc/door_open_ratio[{10 + index}]"
    for index, door in enumerate(DOORS)
}
LONGITUDINAL_CENTERS = (14.197443, -5.204494, -14.096539)
EXPECTED = {
    "surround": {"moving_triangles": 6377, "fixed_triangles": 2216},
    "hardware": {"moving_triangles": 12352, "fixed_triangles": 5555},
}
RATIOS = (0.0, 0.1, 0.2, 0.6, 1.0)
TOLERANCE = 2e-5


def expected_displacement(door: str, ratio: float) -> tuple[float, float, float]:
    outward = 0.55 * min(ratio / 0.2, 1.0)
    forward = 1.3 * max((ratio - 0.2) / 0.8, 0.0)
    return (outward if door.endswith("L") else -outward, 0.0, forward)


def world_position(preview, draw, index):
    point = preview.positions[index * 3:index * 3 + 3]
    return tuple(
        sum(draw.matrix[row * 4 + axis] * point[axis] for axis in range(3))
        + draw.matrix[row * 4 + 3]
        for row in range(3)
    )


def fixed_door(preview, draw) -> str | None:
    """Locate stationary upper fittings by evaluated bounds, not source names."""
    if draw.transform_datarefs:
        return None
    indices = set(preview.indices[draw.start:draw.start + draw.count])
    points = [world_position(preview, draw, index) for index in indices]
    low = tuple(min(point[axis] for point in points) for axis in range(3))
    high = tuple(max(point[axis] for point in points) for axis in range(3))
    if low[1] < 3.5 or high[1] > 6.2:
        return None
    center = tuple((a + b) / 2 for a, b in zip(low, high))
    if not 1.5 < abs(center[0]) < 3.5:
        return None
    station = min(range(3), key=lambda index: abs(center[2] - LONGITUDINAL_CENTERS[index]))
    if abs(center[2] - LONGITUDINAL_CENTERS[station]) > 1.1:
        return None
    return f"U{station + 1}{'L' if center[0] > 0 else 'R'}"


def matching_draws(preview, door):
    reference = DATAREFS[door]
    return [draw for draw in preview.draws if reference in draw.transform_datarefs]


def coverage(preview, kind, label, errors, liner_triangles):
    result = {door: {"moving_triangles": 0, "fixed_triangles": 0} for door in DOORS}
    reverse_refs = {value: key for key, value in DATAREFS.items()}
    for draw in preview.draws:
        upper_refs = set(draw.transform_datarefs) & reverse_refs.keys()
        if upper_refs:
            if len(upper_refs) != 1 or len(draw.transform_datarefs) != 1:
                errors.append(f"{label}: draw {draw.draw_index} has mixed animation drivers")
            for reference in upper_refs:
                result[reverse_refs[reference]]["moving_triangles"] += draw.count // 3
            if draw.cull:
                errors.append(f"{label}: upper-door draw {draw.draw_index} is single-sided")
        elif kind != "exterior":
            door = fixed_door(preview, draw)
            if door:
                result[door]["fixed_triangles"] += draw.count // 3
                if draw.cull:
                    errors.append(f"{label}: fixed upper frame draw {draw.draw_index} is single-sided")
    if kind in EXPECTED:
        for door in DOORS:
            for key, expected in EXPECTED[kind].items():
                if kind == "surround" and key == "fixed_triangles":
                    expected += liner_triangles.get(door, 0)
                actual = result[door][key]
                if actual != expected:
                    errors.append(f"{label}: {door} {key} is {actual}, expected {expected}")
    return result


def check_pose(closed, posed, door, ratio, label, errors):
    if len(closed.draws) != len(posed.draws):
        errors.append(f"{label}: visible draw count changed for {door} at {ratio}")
        return None
    expected_delta = expected_displacement(door, ratio)
    observed = None
    for before, after in zip(closed.draws, posed.draws):
        if (before.start, before.count, before.transform_datarefs) != (
            after.start, after.count, after.transform_datarefs
        ):
            errors.append(f"{label}: draw topology changed for {door} at {ratio}")
            continue
        active = DATAREFS[door] in before.transform_datarefs
        delta = expected_delta if active else (0.0, 0.0, 0.0)
        expected_matrix = list(before.matrix)
        for axis in range(3):
            expected_matrix[axis * 4 + 3] += delta[axis]
        if any(abs(actual - expected) > TOLERANCE
               for actual, expected in zip(after.matrix, expected_matrix)):
            role = "moving leaf" if active else "stationary/other-door geometry"
            errors.append(
                f"{label}: {role} draw {before.draw_index} has wrong motion for {door} at {ratio}"
            )
        if active:
            observed = tuple(after.matrix[axis * 4 + 3] - before.matrix[axis * 4 + 3]
                             for axis in range(3))
    return observed


def verify(surround_obj: Path, hardware_obj: Path, exterior_objs: list[Path],
           liner_triangles: dict[str, int] | None = None) -> dict:
    liner_triangles = liner_triangles or {}
    assets = [("surround", surround_obj), ("hardware", hardware_obj)]
    assets.extend(("exterior", path) for path in exterior_objs)
    errors = []
    closed_assets = []
    report = {"ratios": list(RATIOS), "liner_triangles": liner_triangles,
              "objects": [], "errors": errors}
    for kind, path in assets:
        closed = load_obj8(path)
        counts = coverage(closed, kind, str(path), errors, liner_triangles)
        report["objects"].append({"kind": kind, "path": str(path), "coverage": counts})
        closed_assets.append((kind, path, closed))
    if exterior_objs:
        for door in DOORS:
            if not any(matching_draws(closed, door) for kind, _, closed in closed_assets
                       if kind == "exterior"):
                errors.append(f"Exterior objects do not contain {door}")
    for door in DOORS:
        for ratio in RATIOS:
            deltas = []
            for kind, path, closed in closed_assets:
                posed = load_obj8(path, values={DATAREFS[door]: ratio})
                delta = check_pose(closed, posed, door, ratio, str(path), errors)
                if delta is not None:
                    deltas.append((kind, path, delta))
            if deltas:
                reference = deltas[0][2]
                for kind, path, delta in deltas[1:]:
                    if any(abs(actual - expected) > TOLERANCE
                           for actual, expected in zip(delta, reference)):
                        errors.append(f"{path}: {door} {kind} separates from inner panels at {ratio}")
    report["status"] = "passed" if not errors else "failed"
    report["exterior_comparison"] = bool(exterior_objs)
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--surround-obj", type=Path, required=True)
    parser.add_argument("--hardware-obj", type=Path, required=True)
    parser.add_argument("--exterior-obj", type=Path, action="append", default=[])
    parser.add_argument("--liner-triangles", action="append", default=[], metavar="DOOR=COUNT",
                        help="Expected added fixed collar triangles for one door, e.g. U1L=144")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    liner_triangles = {}
    for value in args.liner_triangles:
        door, separator, count = value.partition("=")
        if door not in DOORS or not separator or not count.isdecimal() or door in liner_triangles:
            parser.error(f"Expected one DOOR=COUNT per upper door, received {value!r}")
        liner_triangles[door] = int(count)
    report = verify(args.surround_obj, args.hardware_obj, args.exterior_obj, liner_triangles)
    text = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text)
    print(text, end="")
    raise SystemExit(0 if report["status"] == "passed" else 1)


if __name__ == "__main__":
    main()
