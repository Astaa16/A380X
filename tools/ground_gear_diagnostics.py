#!/usr/bin/env python3
"""Audit ACF wheel geometry and a recorded stationary X-Plane ground probe.

This deliberately distinguishes tire contact geometry from rendered OBJ8 tire
geometry.  The rigid-body calculation is diagnostic, not a substitute for the
simulator's suspension, truck, or terrain-contact solver.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


FT_TO_M = 0.3048
GEAR_NAMES = ("nose", "left_wing", "right_wing", "left_body", "right_body")


def acf_values(path: Path) -> dict[str, float]:
    wanted = {"acf/_cgY", "acf/_cgZ", "acf/_truck_lat_sep", "acf/_truck_lon_sep"}
    for index in range(5):
        wanted.update(f"_gear/{index}/{suffix}" for suffix in
                      ("_gear_x", "_gear_y", "_gear_z", "_leg_len",
                       "_tire_radius", "_gear_type", "_latE", "_lonE",
                       "_eagle_claw_deg"))
    found: dict[str, float] = {}
    with path.open(encoding="utf-8", errors="replace") as source:
        for line in source:
            if not line.startswith("P "):
                continue
            fields = line.split()
            if len(fields) >= 3 and fields[1] in wanted:
                found[fields[1]] = float(fields[2])
    missing = wanted - found.keys()
    if missing:
        raise ValueError(f"ACF is missing fields: {', '.join(sorted(missing))}")
    return found


def extended_gear_contact(values: dict, index: int) -> tuple[float, float, float]:
    """Neutral strut-end/tire envelope, excluding suspension and truck pitch."""
    g = lambda name: float(values[f'_gear/{index}/_{name}'])
    lat, lon = math.radians(g('latE')), math.radians(g('lonE'))
    length = g('leg_len')
    return (g('gear_x') + length * math.sin(lat),
            g('gear_y') - length * math.cos(lat) * math.cos(lon) - g('tire_radius'),
            g('gear_z') + length * math.sin(lon))


def geometry(values: dict[str, float], pitch_deg: float = 0.0) -> dict:
    theta = math.radians(pitch_deg)
    cg_y = values["acf/_cgY"]
    cg_z = values["acf/_cgZ"]
    wheels = []
    for index, name in enumerate(GEAR_NAMES):
        field = lambda suffix: values[f"_gear/{index}/{suffix}"]
        contact_x_ft, contact_y_ft, contact_z_ft = extended_gear_contact(values, index)
        # X-Plane's Z coordinate increases aft; positive pitch lifts forward
        # contacts, hence the minus sign on the Z rotation term.
        contact_y_from_cg_m = (contact_y_ft - cg_y) * FT_TO_M
        contact_z_from_cg_m = (contact_z_ft - cg_z) * FT_TO_M
        wheels.append({
            "name": name,
            "type": int(field("_gear_type")),
            "lateral_from_datum_m": contact_x_ft * FT_TO_M,
            "contact_y_from_datum_m": contact_y_ft * FT_TO_M,
            "contact_z_from_cg_m": contact_z_from_cg_m,
            "contact_y_from_cg_m": contact_y_from_cg_m,
            "rigid_contact_height_at_pitch_m":
                contact_y_from_cg_m * math.cos(theta)
                - contact_z_from_cg_m * math.sin(theta),
            "extended_angles_deg": {"lateral": field("_latE"),
                                    "longitudinal": field("_lonE")},
            "eagle_claw_deg": field("_eagle_claw_deg"),
        })
    body_mean = sum(w["rigid_contact_height_at_pitch_m"] for w in wheels[3:5]) / 2
    nose = wheels[0]
    return {
        "pitch_deg": pitch_deg,
        "truck_separation_ratios": {
            "lateral_half_tire_widths": values["acf/_truck_lat_sep"],
            "longitudinal_tire_radii": values["acf/_truck_lon_sep"],
        },
        "wheels": wheels,
        "nose_above_body_main_contact_plane_m":
            nose["rigid_contact_height_at_pitch_m"] - body_mean,
        "note": "Rigid wheel bottoms only; suspension compression, truck pitch, and terrain are excluded.",
    }


def probe_summary(path: Path) -> dict:
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    fresh = [r for r in rows if r.get("packet_age") is not None
             and r["packet_age"] < 1.0 and r.get("sim/time/paused") == 0
             and r.get("sim/operation/failures/enable_random_failures") == 0
             and r.get("sim/flightmodel/position/theta") is not None]
    if not fresh:
        return {"source": str(path), "status": "no fresh, unpaused samples with random failures disabled"}
    last = fresh[-1]
    # Use the final few seconds to exclude the touchdown/transient settlement.
    settled = [r for r in fresh if r["t"] >= last["t"] - 5.0]
    key = "sim/flightmodel2/gear/"
    def observed(suffix: str, index: int):
        return last.get(f"{key}{suffix}[{index}]")
    contacts = [observed("on_ground", i) for i in range(5)]
    forces = [observed("tire_vertical_force_n_mtr", i) for i in range(5)]
    deflection = [observed("tire_vertical_deflection_mtr", i) for i in range(5)]
    failures = sorted({name for row in settled for name, value in row.items()
                       if name.startswith("sim/operation/failures/rel_")
                       and value not in (None, 0)})
    pitch = last["sim/flightmodel/position/theta"]
    speed = last.get("sim/flightmodel/position/groundspeed")
    roll = last.get("sim/flightmodel/position/phi")
    stationary = speed is not None and abs(speed) < 0.1
    nose_loaded = all(r.get(f"{key}on_ground[0]") == 1
                      and (r.get(f"{key}tire_vertical_force_n_mtr[0]") or 0) > 1000
                      for r in settled)
    all_mains_loaded = all(all(r.get(f"{key}on_ground[{i}]") == 1 for i in range(1, 5))
                           for r in settled)
    return {
        "source": str(path),
        "last_sample_t_sec": last["t"],
        "settled_samples": len(settled),
        "observed_settled_span_sec": settled[-1]["t"] - settled[0]["t"],
        "pitch_deg": pitch,
        "roll_deg": roll,
        "on_ground": contacts,
        "vertical_force_n": forces,
        "vertical_deflection_m": deflection,
        "groundspeed_m_s": speed,
        "crashed": last.get("sim/flightmodel2/misc/has_crashed"),
        "recorded_failures": failures,
        "nose_loaded_through_settled_window": nose_loaded,
        "all_mains_loaded_through_settled_window": all_mains_loaded,
        "stationary_ground_stance_pass":
            len(settled) >= 20 and settled[-1]["t"] - settled[0]["t"] >= 4.0
            and max((b["t"] - a["t"] for a, b in zip(settled, settled[1:])), default=0) < 0.5
            and stationary and abs(pitch) <= 1.0
            and roll is not None and abs(roll) <= 1.0
            and nose_loaded and all_mains_loaded
            and last.get("sim/flightmodel2/misc/has_crashed") == 0
            and not failures,
        "note": "A ground-stance pass does not validate taxi, rotation, takeoff, landing, or visual wheel placement.",
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("acf", type=Path)
    parser.add_argument("--probe", type=Path, help="JSONL from xplane_runtime_probe.py")
    parser.add_argument("--nose-visible-bottom-m", type=float,
                        help="Lowest deployed OBJ8 nose-tire vertex Y in aircraft datum")
    args = parser.parse_args()
    values = acf_values(args.acf)
    result = {"acf": str(args.acf), "neutral": geometry(values)}
    if args.nose_visible_bottom_m is not None:
        physics = result["neutral"]["wheels"][0]["contact_y_from_datum_m"]
        result["nose_visible_above_physics_contact_m"] = args.nose_visible_bottom_m - physics
    if args.probe:
        result["probe"] = probe_summary(args.probe)
        if "pitch_deg" in result["probe"]:
            result["observed_pitch_geometry"] = geometry(values, result["probe"]["pitch_deg"])
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
