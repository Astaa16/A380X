#!/usr/bin/env python3
"""Bind retained FCU meshes to their authored mechanical motion.

Only OBJ draw commands change. Vertex, index, normal, UV and texture payloads
remain unchanged. Knob translation wraps rotation, preserving the authored
parent/child hierarchy; command click regions keep their screen-facing layout.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
from patch_center_right_animations import source_targets, wrap_geometry
from patch_efis_animations import vertex_payload

BUTTONS = (
    ('AP1', 'ap1'), ('AP2', 'ap2'), ('FD', 'fd'), ('ATHR', 'athr'),
    ('LOC', 'loc'), ('ALT', 'alt_button'), ('APPR', 'approach'),
    ('SPDMACH', 'speed_mach'), ('TRUEMAG', 'true_mag'),
    ('HDGTRK', 'trk_fpa'), ('METER', 'metric'),
)
KNOBS = (('SPEED', 'speed'), ('HDG', 'heading'), ('ALT', 'altitude'), ('UPDN', 'vertical_speed'))
BUTTON_MOTIONS = tuple((f'PUSH_FCU_{node}', f'PUSH_FCU_{node}', f'fcu/{ref}_pressed', 1)
                       for node, ref in BUTTONS)
AXIAL_MOTIONS = tuple((f'PUSH_KNOB_FCU_{node}', f'KNOB_FCU_{node}', f'fcu/{ref}_knob_axial', 1)
                      for node, ref in KNOBS)
ROTARY_MOTIONS = tuple((f'KNOB_FCU_{node}', f'KNOB_FCU_{node}', f'fcu/{ref}_knob_rotation', 360)
                       for node, ref in KNOBS)
SELECTOR_MOTIONS = (('KNOB_FCU_SELECTOR_ALT', 'KNOB_FCU_SELECTOR_ALT', 'fcu/altitude_selector', 1),)


def fcu_target_groups(source):
    groups = []
    for name, motions in (('BUTTON', BUTTON_MOTIONS), ('AXIAL', AXIAL_MOTIONS),
                          ('ROTARY', ROTARY_MOTIONS), ('SELECTOR', SELECTOR_MOTIONS)):
        targets = source_targets(source, motions=motions)
        if name == 'AXIAL':
            for target in targets:
                if target['kind'] != 'translation' or target['values'] != [0, 1]:
                    raise ValueError('FCU axial source must contain its authored rest and push poses')
                # Source supplies push/rest. Pull uses the same physical shaft
                # axis and travel in the opposite direction, with an explicit
                # negative key so X-Plane does not clamp it to the rest pose.
                delta = target['poses'][-1]
                target['values'] = [-1, *target['values']]
                target['poses'] = [tuple(-v for v in delta), *target['poses']]
        groups.append((f'# FCU {name} MOTION ', targets))
    return groups


def fcu_touch_targets(source):
    """Keep rotary/push/pull click zones fixed around the shaft's front face."""
    return [target for marker, targets in fcu_target_groups(source)
            if 'ROTARY' not in marker for target in targets]


def patch_object(original, filename, groups):
    changed = original
    meshes = []
    for marker, targets in groups:
        changed, applied, _ = wrap_geometry(changed, filename, targets, marker)
        meshes.extend(dict(row, group=marker.strip('# ')) for row in applied)
    if vertex_payload(original) != vertex_payload(changed):
        raise ValueError(f'{filename}: FCU patch changed geometry, UV or texture payload')
    return changed, meshes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--directory', required=True, action='append', type=Path)
    parser.add_argument('--backup', type=Path)
    parser.add_argument('--report', required=True, type=Path)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    if not args.dry_run and args.backup is None:
        parser.error('--backup is required unless --dry-run is used')
    groups = fcu_target_groups(args.source)
    staged = []
    materials = {target['material'] for _, targets in groups for target in targets}
    for directory in args.directory:
        relevant = [path for path in sorted(directory.glob('*.obj'))
                    if not path.name.startswith('._') and any(path.name.endswith(material + '.obj') for material in materials)]
        missing = [material for material in materials if not any(path.name.endswith(material + '.obj') for path in relevant)]
        if missing:
            raise ValueError(f'{directory}: missing FCU material objects: {sorted(missing)}')
        for path in relevant:
            original = path.read_text()
            changed, meshes = patch_object(original, path.name, groups)
            staged.append((path, original, changed, meshes))
    rows = []
    for path, original, changed, meshes in staged:
        digest = hashlib.sha256(original.encode()).hexdigest()
        backup = args.backup / (digest[:16] + '-' + path.name) if args.backup else None
        if changed != original and not args.dry_run:
            backup.parent.mkdir(parents=True, exist_ok=True)
            if not backup.exists():
                backup.write_text(original)
            path.write_text(changed)
        rows.append(dict(path=str(path), meshes=meshes, before_sha256=digest,
                         backup=str(backup) if backup and changed != original else None))
    summary = dict(dry_run=args.dry_run, files=rows,
                   source_groups=[dict(marker=marker, targets=[{key: value for key, value in target.items() if key != 'points'}
                                                              for target in targets]) for marker, targets in groups])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(dict(files=len(rows), meshes=sum(len(row['meshes']) for row in rows), report=str(args.report))))


if __name__ == '__main__':
    main()
