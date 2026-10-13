#!/usr/bin/env python3
"""Audit exported engine geometry, continuous rotation and reverser isolation.

The comparison matches triangle corners by position and UV and conserves their
oriented triangle multiset, allowing exporter draw/index reordering. The motion
audit evaluates real OBJ8 transforms, independently of the Blender exporter.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import itertools
import json
from pathlib import Path
import sys

import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "blender"))
from obj8_preview import parse_obj8
from prepare_engine_animation_subset import ENGINE_VARIANTS

FAN = "sim/flightmodel2/engines/prop_rotation_angle_deg"
REVERSE = "sim/flightmodel2/engines/thrust_reverser_deploy_ratio"


def evaluated(text, values=None):
    obj = parse_obj8(text.splitlines(), values)
    positions = np.asarray(obj.positions).reshape(-1, 3)
    indices = np.asarray(obj.indices)
    expanded = np.empty((len(indices), 3))
    mask = np.zeros(len(indices), dtype=bool)
    for draw in obj.draws:
        matrix = np.asarray(draw.matrix).reshape(4, 4)
        selection = slice(draw.start, draw.start + draw.count)
        expanded[selection] = positions[indices[selection]] @ matrix[:3, :3].T + matrix[:3, 3]
        mask[selection] = True
    if not np.all(mask):
        raise ValueError("Audit requires all engine triangles visible at every sampled pose")
    return obj, expanded, np.asarray(obj.uv).reshape(-1, 2)[indices]


def oriented_triangles(ids):
    return Counter(min((a,b,c), (b,c,a), (c,a,b)) for a,b,c in zip(ids[::3], ids[1::3], ids[2::3]))


def compare_geometry(before, after):
    _, first, first_uv = evaluated(before)
    _, second, second_uv = evaluated(after)
    if len(first) != len(second):
        raise ValueError("Engine triangle counts changed")
    # Most exports preserve index-stream order, so this is both exact and cheap.
    error = np.linalg.norm(first-second, axis=1)
    if np.max(np.abs(first_uv-second_uv)) < 1e-8 and np.max(error) < 0.0001:
        return {"triangles": len(first)//3, "max_neutral_error_m": float(np.max(error)),
                "oriented_geometry_uv_preserved": True, "index_stream_reordered": False}
    # Reverser animation can alter Blender's mesh enumeration. Match full corner
    # attributes, then require the same oriented faces, not just point clouds.
    cells, identifiers, old_ids = defaultdict(list), {}, []
    for point, uv in zip(first, first_uv):
        key = tuple(np.round(np.concatenate((point,uv)),7))
        if key not in identifiers:
            identifier = len(identifiers)
            identifiers[key] = identifier
            cell = tuple(np.floor(point / .005).astype(int))
            cells[cell].append((point, uv, identifier))
        old_ids.append(identifiers[key])
    mapped, worst = [], 0.
    offsets = list(itertools.product((-1,0,1),repeat=3))
    for point, uv in zip(second, second_uv):
        cell = tuple(np.floor(point/.005).astype(int))
        candidates = cells[cell]
        matches = [(float(np.linalg.norm(point-p)),i) for p,u,i in candidates
                   if np.max(np.abs(uv-u)) < 1e-7]
        if not matches or min(matches)[0] > .0001:
            matches = [(float(np.linalg.norm(point-p)),i)
                       for offset in offsets
                       for p,u,i in cells.get(tuple(a+b for a,b in zip(cell,offset)),())
                       if np.max(np.abs(uv-u)) < 1e-7]
        if not matches or min(matches)[0] > .0001:
            raise ValueError("New engine corner has no matching original position and UV")
        distance, identifier = min(matches)
        worst = max(worst,distance)
        mapped.append(identifier)
    if oriented_triangles(old_ids) != oriented_triangles(mapped):
        raise ValueError("Engine oriented triangle/UV multiset changed")
    return {"triangles":len(first)//3, "max_neutral_error_m":worst,
            "oriented_geometry_uv_preserved":True, "index_stream_reordered":True}


def audit_motion(text, name):
    obj, neutral, _ = evaluated(text)
    reverser_refs = {ref for ref in obj.datarefs if "thrust_reverser" in ref}
    if reverser_refs - {f"{REVERSE}[1]",f"{REVERSE}[2]"}:
        raise ValueError("Outboard reverser animation was exported")
    result = {"fan_engines": [], "reversers": {}}
    for engine in range(4):
        ref = f"{FAN}[{engine}]"
        if name == "a380x_fans":
            _, quarter, _ = evaluated(text,{ref:90})
            _, full, _ = evaluated(text,{ref:360})
            changed = np.linalg.norm(quarter-neutral,axis=1)>1e-5
            if np.count_nonzero(changed) != 74280:
                raise ValueError(f"Engine {engine+1} must move only its fan and spinner")
            expected_x = (25.8,14.9,-14.9,-25.8)[engine]
            if abs(float(np.mean(neutral[changed,0]))-expected_x) > .15:
                raise ValueError(f"Engine {engine+1} fan dataref is attached to the wrong nacelle")
            if np.max(np.linalg.norm(full-neutral,axis=1)) > 1e-8:
                raise ValueError("Fan revolution does not close continuously")
            # At each octant the rotating point keeps a common fixed axis; the
            # opposite-pose midpoint is the centre of its circular trajectory.
            _, opposite, _ = evaluated(text,{ref:180})
            center = (neutral+opposite)/2
            for angle in (45,135,225,315):
                _, pose, _ = evaluated(text,{ref:angle})
                expected = center + np.cos(np.radians(angle))*(neutral-center) + np.sin(np.radians(angle))*(quarter-center)
                if np.max(np.linalg.norm(pose-expected,axis=1)) > 1e-7:
                    raise ValueError("Fan rotation wobbles or reverses between authored keys")
            result["fan_engines"].append(engine+1)
        ref=f"{REVERSE}[{engine}]"
        if engine in (0,3):
            continue
        _, deployed, _ = evaluated(text,{ref:1})
        movement = np.linalg.norm(deployed-neutral,axis=1)
        moving = movement > .0001
        if np.any(moving & (np.abs(neutral[:,0]) > 20)):
            raise ValueError("Reverser moves outboard engine geometry")
        if engine == 1 and np.any(moving & (neutral[:,0]<0)) or engine == 2 and np.any(moving & (neutral[:,0]>0)):
            raise ValueError("Reverser moves the opposite engine")
        if name == 'a380x_fans' and np.any(moving):
            raise ValueError("Reverser moves fan geometry")
        if np.any(moving):
            _, middle, _ = evaluated(text,{ref:.5})
            if np.max(np.linalg.norm(middle-neutral,axis=1)) < .1:
                raise ValueError("Reverser does not pass through a deployed intermediate pose")
            result['reversers'][str(engine+1)]={"moving_triangles":int(np.count_nonzero(np.any(moving.reshape(-1,3),axis=1))), "max_travel_m":float(np.max(movement))}
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before',type=Path,required=True)
    parser.add_argument('--after',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    reports=[]
    for name in sorted(ENGINE_VARIANTS):
        before=(args.before/(name+'.obj')).read_text()
        after=(args.after/(name+'.obj')).read_text()
        report={"name":name,**compare_geometry(before,after),**audit_motion(after,name)}
        reports.append(report)
        print(json.dumps(report),flush=True)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps({"files":reports,"status":"passed"},indent=2)+'\n')


if __name__=='__main__':main()
