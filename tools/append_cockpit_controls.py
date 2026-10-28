#!/usr/bin/env python3
"""Append click-only manipulators for extra cockpit controls to an existing controls OBJ.

The installed a380x_cockpit_controls.obj carries post-build patches that build_cockpit_controls.py
cannot regenerate, so extra controls are appended instead. Each control gets one draw-disabled
hotspot quad covering the whole control face: the FBW node mesh plus all of its child meshes
(push-button lenses, legends) are projected onto the face plane (same transforms as
build_cockpit_controls.py), and the quad sits FRONT_OFFSET_M in front of the frontmost point,
facing the pilot. A hotspot built from the button cap alone sits behind the lens and is only
reachable at an exposed corner. The switch itself does not animate.

--include-mapped also adds centred hotspots for controls already configured for
build_cockpit_controls.py (command, toggle and command-knob types) whose node matches
--mapped-filter, reusing their command or dataref.

The output is always rebuilt from --base, so re-running never stacks duplicates.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from build_cockpit_controls import (accessor_values, load_buffer, local_matrix, matrix_multiply,
                                    transform_normal, transform_point)

FRONT_OFFSET_M = 0.002
# Approximate captain/first-officer eye region (OBJ metres): only used to orient each face normal.
EYE = (0.0, 2.8, 33.6)
MARKER = '# A380X APPENDED CONTROLS V1 (tools/append_cockpit_controls.py)'
KINDS = {'command', 'toggle', 'axis_knob', 'command_knob', 'push'}
CURSORS = {'hand', 'button', 'arrow', 'rotate_small', 'rotate_medium', 'rotate_large', 'up_down'}


def manipulator_line(control: dict) -> str:
    kind, label = control['kind'], control['label']
    if kind == 'command':
        return f"ATTR_manip_command\t{control.get('cursor', 'hand')}\t{control['command']}\t{label}"
    if kind == 'push':   # momentary: value 1 while held, 0 on release
        return f"ATTR_manip_push\t{control.get('cursor', 'button')}\t1\t0\t{control['dataref']}\t{label}"
    if kind == 'toggle':
        on, off = control['values']
        return f"ATTR_manip_toggle\t{control.get('cursor', 'button')}\t{on:g}\t{off:g}\t{control['dataref']}\t{label}"
    if kind == 'axis_knob':
        return (f"ATTR_manip_axis_knob\t{control['cursor']}\t{control['min']:g}\t{control['max']:g}\t"
                f"{control['step']:g}\t{control['step']:g}\t{control['dataref']}\t{label}")
    return f"ATTR_manip_command_knob\t{control['cursor']}\t{control['command_up']}\t{control['command_down']}\t{label}"


def mapped_controls(path: Path, pattern: str, skip: set[str]) -> list[dict]:
    import re
    out = []
    for c in json.loads(path.read_text(encoding='utf-8')):
        node = c['node']
        if node in skip or not re.match(pattern, node) or c.get('command_push'):
            continue
        label = c['label'] + ' (centre)'
        if c.get('command'):
            out.append({'node': node, 'kind': 'command', 'command': c['command'],
                        'cursor': c.get('command_cursor') or 'hand', 'label': label, 'mapped': True})
        elif c.get('command_up'):
            out.append({'node': node, 'kind': 'command_knob', 'command_up': c['command_up'],
                        'command_down': c['command_down'], 'cursor': c.get('cursor') or 'rotate_small',
                        'label': label, 'mapped': True})
        elif c.get('dataref') and c.get('toggle_values'):
            out.append({'node': node, 'kind': 'toggle', 'dataref': c['dataref'], 'values': c['toggle_values'],
                        'cursor': c.get('cursor') or 'button', 'label': label, 'mapped': True})
    return out


def plugin_datarefs(sources: list[Path]) -> set[str]:
    """Plugin datarefs usable by hotspots: a380x/xp12/ovhd/* (a380x_ovhd_state.inc) and the
    a380x/xp12/pb/* push-button arrays (a380x_pb.inc, sized by a380x_pb_table.inc)."""
    import re
    names = set()
    for source in sources:
        text = source.read_text(encoding='utf-8')
        count = re.search(r'#define PB_COUNT (\d+)', text)
        if count:
            names |= {f'a380x/xp12/pb/state[{i}]' for i in range(int(count.group(1)))}
    for source in sources:
        text = source.read_text(encoding='utf-8')
        if 'ovhd_state[]' in text:
            block = text[text.index('ovhd_state[]'):]
            block = block[:block.index('};')]
            names |= {'a380x/xp12/ovhd/' + n for n in re.findall(r'\{"([a-z0-9_]+)",', block)}
    return names


def validate(controls: list[dict], known: set[str] | None) -> None:
    nodes = [c['node'] for c in controls]
    if len(nodes) != len(set(nodes)):
        raise ValueError('Duplicate control nodes')
    for c in controls:
        if c.get('kind') not in KINDS:
            raise ValueError(f"Unknown kind for {c.get('node')}")
        if c.get('cursor', 'hand') not in CURSORS:
            raise ValueError(f"Unknown cursor for {c['node']}")
        if any(ch in c['label'] for ch in '\t\r\n'):
            raise ValueError(f"Label must fit on one OBJ line: {c['node']}")
        targets = [c.get(k) for k in ('command', 'command_up', 'command_down', 'dataref') if c.get(k)]
        if not targets:
            raise ValueError(f"No command or dataref for {c['node']}")
        if known is not None and not c.get('mapped'):
            missing = [t for t in targets if t not in known and t.split('[')[0] not in known]
            if missing:
                raise ValueError(f"{c['node']}: not an X-Plane command/dataref: {missing}")


def control_meshes(gltf: Path, controls: list[dict]):
    document = json.loads(gltf.read_text(encoding='utf-8'))
    buffers = [load_buffer(gltf, buffer['uri']) for buffer in document['buffers']]
    nodes = document['nodes']
    parents = {child: index for index, node in enumerate(nodes) for child in node.get('children', [])}
    names = {node.get('name'): index for index, node in enumerate(nodes)}
    cache: dict[int, list[list[float]]] = {}

    def world(index: int) -> list[list[float]]:
        if index not in cache:
            local = local_matrix(nodes[index])
            cache[index] = matrix_multiply(world(parents[index]), local) if index in parents else local
        return cache[index]

    def mesh_data(index: int):
        points, normals = [], []
        for primitive in document['meshes'][nodes[index]['mesh']]['primitives']:
            points += [transform_point(world(index), p)
                       for p in accessor_values(document, buffers, primitive['attributes']['POSITION'])]
            normals += [transform_normal(world(index), n)
                        for n in accessor_values(document, buffers, primitive['attributes']['NORMAL'])]
        return points, normals

    def descendants(index: int):
        for child in nodes[index].get('children', []):
            yield child
            yield from descendants(child)

    def unit(v):
        length = math.sqrt(sum(x * x for x in v))
        return tuple(x / length for x in v)

    for control in controls:
        if control['node'] not in names or 'mesh' not in nodes[names[control['node']]]:
            raise ValueError(f"Control node has no mesh in {gltf.name}: {control['node']}")
        index = names[control['node']]
        points, normals = mesh_data(index)
        lens_normals = []
        for child in descendants(index):
            if 'mesh' in nodes[child]:
                child_points, child_normals = mesh_data(child)
                points += child_points
                lens_normals += child_normals
        centre = tuple(sum(p[k] for p in points) / len(points) for k in range(3))
        # Face normal: the lens surfaces when present (they face the pilot), otherwise the dominant
        # direction of the control's own normals that points towards the eye region.
        source = lens_normals or normals
        to_eye = unit(tuple(EYE[k] - centre[k] for k in range(3)))
        facing = [n for n in source if sum(n[k] * to_eye[k] for k in range(3)) > 0.3] or source
        n = unit(tuple(sum(v[k] for v in facing) for k in range(3)))
        if sum(n[k] * to_eye[k] for k in range(3)) < 0:
            n = tuple(-x for x in n)
        helper = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
        u = unit((n[1] * helper[2] - n[2] * helper[1], n[2] * helper[0] - n[0] * helper[2], n[0] * helper[1] - n[1] * helper[0]))
        v = (n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0])
        dots = lambda axis: [sum(p[k] * axis[k] for k in range(3)) for p in points]
        u0, u1 = min(dots(u)), max(dots(u))
        v0, v1 = min(dots(v)), max(dots(v))
        front = max(dots(n)) + FRONT_OFFSET_M
        corners = [tuple(u[k] * a + v[k] * b + n[k] * front for k in range(3)) for a, b in ((u0, v0), (u1, v0), (u1, v1), (u0, v1))]
        vertices = [c + n for c in corners]
        triangles = [0, 1, 2, 0, 2, 3]
        for t in range(0, 6, 3):   # same winding rule as the builder: cross(ab, ac) . normal <= 0
            a, b, c = (corners[i] for i in triangles[t:t + 3])
            ab = [b[k] - a[k] for k in range(3)]
            ac = [c[k] - a[k] for k in range(3)]
            cross = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
            if sum(cross[k] * n[k] for k in range(3)) > 0:
                triangles[t + 1], triangles[t + 2] = triangles[t + 2], triangles[t + 1]
        yield control, vertices, triangles


def append(base_obj: str, meshes) -> str:
    lines = base_obj.split('\n')
    if any(line.startswith(MARKER) for line in lines):
        raise ValueError('Base OBJ already contains appended controls; pass the clean base')
    counts_at = next(i for i, l in enumerate(lines) if l.startswith('POINT_COUNTS'))
    vt_count, lines_count, lights_count, idx_count = map(int, lines[counts_at].split()[1:5])
    last_vt = max(i for i, l in enumerate(lines) if l.startswith('VT'))
    last_idx = max(i for i, l in enumerate(lines) if l.split()[:1] in (['IDX'], ['IDX10']))
    new_vt, new_idx, commands = [], [], [MARKER, 'ATTR_no_cockpit', 'ATTR_draw_disable']
    for control, vertices, triangles in meshes:
        commands.append(manipulator_line(control))
        commands.append(f'TRIS\t{idx_count + len(new_idx)}\t{len(triangles)}')
        commands.append('ATTR_manip_none')
        offset = vt_count + len(new_vt)
        new_idx.extend(offset + i for i in triangles)
        new_vt.extend('VT\t' + '\t'.join(f'{v:.9g}' for v in vertex) + '\t0\t0' for vertex in vertices)
    commands.append('ATTR_draw_enable')
    idx_lines = ['IDX10\t' + '\t'.join(map(str, new_idx[i:i + 10])) for i in range(0, len(new_idx) - len(new_idx) % 10, 10)]
    idx_lines += [f'IDX\t{i}' for i in new_idx[len(new_idx) - len(new_idx) % 10:]]
    lines = lines[:last_idx + 1] + idx_lines + lines[last_idx + 1:]
    lines = lines[:last_vt + 1] + new_vt + lines[last_vt + 1:]
    counts_at = next(i for i, l in enumerate(lines) if l.startswith('POINT_COUNTS'))
    lines[counts_at] = f'POINT_COUNTS\t{vt_count + len(new_vt)}\t{lines_count}\t{lights_count}\t{idx_count + len(new_idx)}'
    while lines and not lines[-1].strip():
        lines.pop()
    return '\n'.join(lines + [''] + commands + [''])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('gltf', type=Path, help='FBW A380_COCKPIT_LOD00.gltf')
    parser.add_argument('--base', type=Path, required=True, help='clean controls OBJ to start from')
    parser.add_argument('--config', type=Path, required=True, action='append',
                        help='control list; repeatable, the first entry for a node wins')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--commands', type=Path, help='X-Plane Commands.txt for validation')
    parser.add_argument('--datarefs', type=Path, help='X-Plane DataRefs.txt for validation')
    parser.add_argument('--plugin-source', type=Path, action='append', default=[],
                        help='plugin source declaring a380x/xp12/ovhd/* state datarefs')
    parser.add_argument('--include-mapped', type=Path, help='config/cockpit_controls.json')
    parser.add_argument('--mapped-filter', default=r'^(PUSH|SWITCH|KNOB)_OVHD_')
    args = parser.parse_args()
    controls, seen, skipped = [], set(), 0
    for config in args.config:
        for control in json.loads(config.read_text(encoding='utf-8')):
            if control['node'] in seen:
                skipped += 1
                continue
            seen.add(control['node'])
            controls.append(control)
    if skipped:
        print(f'{skipped} later duplicate node entries skipped (first config wins)')
    if args.include_mapped:
        controls += mapped_controls(args.include_mapped, args.mapped_filter, {c['node'] for c in controls})
    known = None
    if args.commands and args.datarefs:
        known = {l.split()[0] for path in (args.commands, args.datarefs)
                 for l in path.read_text(errors='replace').splitlines() if l.strip()}
        known |= plugin_datarefs(args.plugin_source)
    validate(controls, known)
    text = append(args.base.read_text(encoding='utf-8'), control_meshes(args.gltf, controls))
    args.output.write_text(text, encoding='utf-8')
    print(f'appended {len(controls)} hotspots ({sum(1 for c in controls if c.get("mapped"))} for already-mapped '
          f'controls) to {args.output}')


if __name__ == '__main__':
    main()
