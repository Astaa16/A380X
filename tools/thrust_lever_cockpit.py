#!/usr/bin/env python3
"""Stage the animated thrust levers, reverse levers and A/THR instinctive-disconnect buttons.

Pinned FBW clips and meshes (A380_COCKPIT_LOD00.gltf; behaviour pedestal.xml + A32NX_Interior_Engine.xml):

* throttle_lever_1..4 rotate LEVER_THROTTLE_1..4 (and their children) about the lateral pivot, keys
  0..55.x deg = 0..100 % of FBW's A32NX_3D_THROTTLE_LEVER_POSITION (TLA -20/0/25/35/45 -> 0/0/55/78/100 %),
  driven by a380x/xp12/thrust/lever_N_anim (percent).
* LEVER_THROTTLE_2/3_REVERSE rotate the piggy-back reverse levers 0..94 deg (FBW anim code
  (1 - min(position, 1)) * 100), driven by reverse_N_anim (0 stowed .. 1 MAX REV), inside lever 2/3.
* ATHR_DISCONNECT_ENG1/2 translate the red buttons (left of lever 1, right of lever 4), driven by
  disc_left_anim / disc_right_anim (0..1), inside lever 1/4.

Staged changes (the input aircraft is never written):

* cockpit_a380_cockpit_pedestal_misc01_4k.obj and decal_new_decal_noemis.obj: the exact source triangle
  ranges are wrapped in (nested) ANIM blocks; VT/IDX/UV/texture payload, every other line and the drawn
  index ranges stay identical (checked); every wrapped range was static.
* a380x_cockpit_controls.obj: the eight old throttle manipulators (straight ATTR_manip_drag_axis on
  X-Plane's throttle_ratio / throttle_jet_rev_ratio, static, pointing aft, and the two
  athr_disconnect buttons) are retired in place (commented, ATTR_manip_none), and after MARKER
  draw-disabled boxes are appended that move with their part: a click-hold-drag
  ATTR_manip_drag_rotate on lever_N_input (TLA 0..45, keyframes at CL 25 / FLX 35 matching the 3D
  table) about each lever's own pivot, one on reverse_N_input (0..1) about each reverse lever's pivot,
  and the two buttons (athr_disconnect_left/right). Stickiness, interlocks and clicks are native
  (a380x_thrust.inc); no X-Plane lift detents are used because Airbus thrust levers have no lift.

    python3 tools/thrust_lever_cockpit.py --aircraft output/A380X-XP12 \\
        --source <A380_COCKPIT_LOD00.gltf> --stage build/thrust-geometry-<timestamp>
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

from avionics_provider import THRUST_COMMANDS, THRUST_DATAREFS
from cockpit_lever_geometry import (Source, clip_motion, describe, drag_rotate_lines, nested_block, oriented_box,
                                    part_points, wrap_parts)
from isis_cockpit_device import _append

PREFIX = 'a380x/xp12/thrust/'
MARKER = '# A380X THRUST LEVERS V1'
MOTION_MARKER = MARKER + ' MOTION '
CLICK_MARKER = MARKER + ' CLICK '
RETIRED_MARKER = MARKER + ' RETIRED '
CONTROLS = 'objects/interior/cockpit-controls/a380x_cockpit_controls.obj'
PEDESTAL = 'objects/interior/cockpit/cockpit_a380_cockpit_pedestal_misc01_4k.obj'
DECAL = 'objects/interior/cockpit-panels/decal_new_decal_noemis.obj'
PEDESTAL_MATERIAL = 'A380_COCKPIT_PEDESTAL_MISC01_4k'
DECAL_MATERIAL = 'NEW_DECAL_NOEMIS'
CLICK_MARGIN = 0.001  # m around the authored meshes
# FlyByWireInterface mappingTable3d: manipulator TLA -> lever_N_anim percent (the clip's 0..100 %).
TLA_TO_ANIM = ((0.0, 0.0), (25.0, 55.0), (35.0, 78.0), (45.0, 100.0))
REVERSE_TO_ANIM = ((0.0, 0.0), (1.0, 1.0))


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def motions(source: Source) -> dict[str, dict]:
    result = {f'lever_{n}': clip_motion(source, f'throttle_lever_{n}', f'LEVER_THROTTLE_{n}',
                                         f'{PREFIX}lever_{n}_anim', 100.0) for n in range(1, 5)}
    for n in (2, 3):
        result[f'reverse_{n}'] = clip_motion(source, f'LEVER_THROTTLE_{n}_REVERSE', f'LEVER_THROTTLE_{n}_REVERSE',
                                             f'{PREFIX}reverse_{n}_anim', 1.0)
    result['disc_left'] = clip_motion(source, 'ATHR_DISCONNECT_ENG1', 'ATHR_DISCONNECT_ENG1', f'{PREFIX}disc_left_anim', 1.0)
    result['disc_right'] = clip_motion(source, 'ATHR_DISCONNECT_ENG2', 'ATHR_DISCONNECT_ENG2', f'{PREFIX}disc_right_anim', 1.0)
    for name, motion in result.items():
        if name.startswith(('lever', 'reverse')) and motion['kind'] != 'rotation':
            raise ValueError(f'{name}: expected a rotation clip')
        if name.startswith('disc') and motion['kind'] != 'translation':
            raise ValueError(f'{name}: expected a translation clip')
    return result


# (part name / glTF node, material file, chain of motion names)
PARTS = {
    PEDESTAL: (
        ('LEVER_THROTTLE_1', PEDESTAL_MATERIAL, ('lever_1',)),
        ('ATHR_DISCONNECT_ENG1', PEDESTAL_MATERIAL, ('lever_1', 'disc_left')),
        ('LEVER_THROTTLE_2', PEDESTAL_MATERIAL, ('lever_2',)),
        ('ReverseMechanism.001', PEDESTAL_MATERIAL, ('lever_2',)),
        ('ThrottlePivot.002', PEDESTAL_MATERIAL, ('lever_2',)),
        ('LEVER_THROTTLE_2_REVERSE', PEDESTAL_MATERIAL, ('lever_2', 'reverse_2')),
        ('LEVER_THROTTLE_3', PEDESTAL_MATERIAL, ('lever_3',)),
        ('ReverseMechanism.003', PEDESTAL_MATERIAL, ('lever_3',)),
        ('ThrottlePivot.001', PEDESTAL_MATERIAL, ('lever_3',)),
        ('LEVER_THROTTLE_3_REVERSE', PEDESTAL_MATERIAL, ('lever_3', 'reverse_3')),
        ('LEVER_THROTTLE_4', PEDESTAL_MATERIAL, ('lever_4',)),
        ('ATHR_DISCONNECT_ENG2', PEDESTAL_MATERIAL, ('lever_4', 'disc_right')),
    ),
    DECAL: (
        ('LEVER_THROTTLE_1', DECAL_MATERIAL, ('lever_1',)),
        ('LEVER_THROTTLE_4', DECAL_MATERIAL, ('lever_4',)),
    ),
}

# (name, box node, chain, manipulator kind, dataref / command, tooltip)
CLICKS = (
    *((f'lever_{n}', f'LEVER_THROTTLE_{n}_CLICK', (f'lever_{n}',), 'lever', f'{PREFIX}lever_{n}_input',
       f'THRUST LEVER {n} - click and drag: IDLE / CL / FLX-MCT / TOGA') for n in range(1, 5)),
    *((f'reverse_{n}', f'LEVER_THROTTLE_{n}_REVERSE', (f'lever_{n}', f'reverse_{n}'), 'reverse',
       f'{PREFIX}reverse_{n}_input', f'REVERSE LEVER {n} - lift with thrust lever {n} at IDLE: IDLE REV / MAX REV')
      for n in (2, 3)),
    ('disc_left', 'ATHR_DISCONNECT_ENG1', ('lever_1', 'disc_left'), 'button', f'{PREFIX}athr_disconnect_left',
     'A/THR INSTINCTIVE DISCONNECT (left) - hold more than 15 s: A/THR lost until FMGC power-up'),
    ('disc_right', 'ATHR_DISCONNECT_ENG2', ('lever_4', 'disc_right'), 'button', f'{PREFIX}athr_disconnect_right',
     'A/THR INSTINCTIVE DISCONNECT (right) - hold more than 15 s: A/THR lost until FMGC power-up'),
)

# The generated throttle manipulators this patch replaces (tools/build_cockpit_controls.py output).
RETIRE_PATTERNS = (
    *(re.compile(r'^ATTR_manip_drag_axis\t\S+(\t\S+){5}\tsim/cockpit2/engine/actuators/throttle_ratio\[%d\]\t' % i)
      for i in range(4)),
    *(re.compile(r'^ATTR_manip_drag_axis\t\S+(\t\S+){5}\tsim/cockpit2/engine/actuators/throttle_jet_rev_ratio\[%d\]\t' % i)
      for i in (1, 2)),
    re.compile(r'^ATTR_manip_command\t\S+\ta380x/xp12/fcu/athr_disconnect\tA/THR DISCONNECT - left thrust lever$'),
    re.compile(r'^ATTR_manip_command\t\S+\ta380x/xp12/fcu/athr_disconnect\tA/THR DISCONNECT - right thrust lever$'),
)


def panel_parts(source: Source, relative: str, motion: dict[str, dict]) -> list[dict]:
    parts = []
    for node, material, chain in PARTS[relative]:
        points, count = part_points(source, node, material)
        parts.append(dict(name=node, points=points, count=count, chain=[motion[m] for m in chain]))
    return parts


def patch_panel(text: str, relative: str, parts: list[dict]) -> tuple[str, list[dict]]:
    if MOTION_MARKER in text:
        missing = [p['name'] for p in parts if MOTION_MARKER + p['name'] not in text]
        if missing:
            raise ValueError(f'{relative}: incomplete previous thrust-lever patch: {missing}')
        return text, []
    if any(motion['dataref'] in text for part in parts for motion in part['chain']):
        raise ValueError(f'{relative}: thrust-lever datarefs already present without the marker')
    return wrap_parts(text, parts, MOTION_MARKER)


def retire_manipulators(text: str) -> tuple[str, list[str]]:
    lines = text.split('\n')
    retired = []
    for k, line in enumerate(lines):
        if any(pattern.match(line) for pattern in RETIRE_PATTERNS):
            retired.append(line)
            lines[k] = f'{RETIRED_MARKER}{line}\nATTR_manip_none'
    if len(retired) != len(RETIRE_PATTERNS) or len(set(retired)) != len(retired):
        raise ValueError(f'Expected the {len(RETIRE_PATTERNS)} generated throttle manipulators, found {len(retired)}')
    return '\n'.join(lines), retired


def click_geometry(source: Source, motion: dict[str, dict]):
    """(vertices, indices, commands, report rows) for the controls object, relative to 0."""
    vertices, indices = [], []
    commands = ['ATTR_manip_none', 'ATTR_no_cockpit', 'ATTR_draw_disable']
    rows = []
    for name, node, chain, kind, target, tooltip in CLICKS:
        box_vertices, box_indices, bounds = oriented_box(source.points_in_frame((node,)),
                                                         source.world(source.names[node]), margin=CLICK_MARGIN)
        start, base = len(indices), len(vertices)
        vertices.extend(box_vertices)
        indices.extend(base + i for i in box_indices)
        chain_motions = [motion[m] for m in chain]
        if kind == 'lever':
            manip = drag_rotate_lines(chain_motions[0], target, list(TLA_TO_ANIM), tooltip)
        elif kind == 'reverse':
            manip = drag_rotate_lines(chain_motions[1], target, list(REVERSE_TO_ANIM), tooltip)
        else:
            manip = [f'ATTR_manip_command\tbutton\t{target}\t{tooltip}']
        commands += [CLICK_MARKER + name, *nested_block(chain_motions, [*manip, f'TRIS\t{start}\t{len(box_indices)}',
                                                                        'ATTR_manip_none'])]
        rows.append(dict(name=name, node=node, kind=kind, target=target, chain=[m['dataref'] for m in chain_motions],
                         manipulator=manip[0].split('\t')[0], keyframes=len(manip) - 1,
                         triangles=len(box_indices) // 3, **bounds))
    commands.append('ATTR_draw_enable')
    return vertices, indices, commands, rows


def augment_controls(text: str, source: Source, motion: dict[str, dict]) -> tuple[str, list[str], list[dict]]:
    if MARKER in text:
        if text.count(RETIRED_MARKER) != len(RETIRE_PATTERNS) or any(CLICK_MARKER + c[0] not in text for c in CLICKS):
            raise ValueError('Incomplete previous thrust-lever controls patch')
        return text, [], []
    retired_text, retired = retire_manipulators(text)
    vertices, indices, commands, rows = click_geometry(source, motion)
    return _append(retired_text, vertices, indices, commands, MARKER), retired, rows


def stage(aircraft: Path, source_path: Path, destination: Path) -> dict:
    if destination.exists():
        raise ValueError('Stage directory must not exist')
    source = Source(source_path)
    motion = motions(source)
    used_refs = {m['dataref'] for m in motion.values()} | {c[4] for c in CLICKS if c[3] != 'button'}
    if not used_refs <= set(THRUST_DATAREFS):
        raise ValueError(f'Datarefs outside the native contract: {sorted(used_refs - set(THRUST_DATAREFS))}')
    if {c[4] for c in CLICKS if c[3] == 'button'} != set(THRUST_COMMANDS):
        raise ValueError('Click targets do not cover the native thrust command set')
    staged, wrapped = {}, {}
    for relative in (PEDESTAL, DECAL):
        staged[relative], wrapped[relative] = patch_panel((aircraft / relative).read_text(), relative,
                                                          panel_parts(source, relative, motion))
    staged[CONTROLS], retired, clicks = augment_controls((aircraft / CONTROLS).read_text(), source, motion)
    destination.mkdir(parents=True)
    files = []
    for relative, text in staged.items():
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)
        files.append(dict(path=relative, before_sha256=sha(aircraft / relative), after_sha256=sha(target),
                          changed=sha(aircraft / relative) != sha(target)))
    report = dict(format=1, aircraft=str(aircraft), source=str(source_path), source_sha256=sha(source_path),
                  marker=MARKER, files=files, motions={k: describe(v) for k, v in motion.items()},
                  wrapped=wrapped, retired_manipulators=retired, click_targets=clicks,
                  tla_to_anim_percent=[list(row) for row in TLA_TO_ANIM], datarefs_used=sorted(used_refs),
                  commands=list(THRUST_COMMANDS), visible_vertex_index_uv_payload_preserved=True,
                  controls_retire_and_append_only=True, simulator_tested=False)
    (destination / 'staging-report.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--aircraft', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--stage', type=Path, required=True)
    args = parser.parse_args()
    report = stage(args.aircraft.resolve(), args.source.resolve(), args.stage.resolve())
    print(json.dumps({'stage': str(args.stage.resolve()), 'files': report['files'],
                      'wrapped': {k: [(m['name'], m['indices']) for m in v] for k, v in report['wrapped'].items()},
                      'retired': len(report['retired_manipulators']),
                      'click_targets': [c['name'] for c in report['click_targets']]}, indent=2))


if __name__ == '__main__':
    main()
