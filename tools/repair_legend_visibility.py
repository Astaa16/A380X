#!/usr/bin/env python3
"""Restore depth writes on the lifted legend mesh and correct attachment order.

Does not touch textures, vertices, UVs, indices, animation, or attachment poses.
Dry-run by default; --apply installs with patch_cockpit_materials rollback backups.
"""
import argparse
import json
import re
from pathlib import Path

from cockpit_draw_order import (cockpit_decal_order_errors, order_cockpit_attachments,
                                reorder_acf_attachments)
from patch_cockpit_materials import (PROJECT, OBJ_DIR, atomic_write, inspect_obj,
                                     install, texture_references)


def build_plan(root):
    relative = OBJ_DIR / 'decal_a380x_cockpit_decal_emis.obj'
    original = (root / relative).read_text()
    before = inspect_obj(original)
    offsets = re.findall(r'^\s*ATTR_poly_os\s+([^\s]+)', original, re.M)
    if len(offsets) != 1 or offsets[0] not in ('0', '1'):
        raise ValueError('Expected the known lifted legend with one polygon-offset state')
    patched = re.sub(r'^(\s*ATTR_poly_os)[ \t]+[^\r\n]+', r'\g<1> 0', original, flags=re.M)
    after = inspect_obj(patched)
    if before['geometry_animation_sha256'] != after['geometry_animation_sha256']:
        raise AssertionError('Legend geometry or animations changed')
    if list(texture_references(original)) != list(texture_references(patched)):
        raise AssertionError('Legend texture references changed')
    acf_path = Path('A380X-XP12.acf')
    acf = (root / acf_path).read_bytes()
    reordered = reorder_acf_attachments(acf)
    def attachments(data):
        return re.findall(r'^P _obja/(\d+)/_v10_att_file_stl (.+)$', data.decode('latin1'), re.M)
    old_refs, new_refs = attachments(acf), attachments(reordered)
    errors = cockpit_decal_order_errors([path for _, path in new_refs])
    if errors:
        raise ValueError('; '.join(errors))
    manifest_path = Path('build-manifest.json')
    manifest = json.loads((root / manifest_path).read_text())
    manifest['visible_objects'] = order_cockpit_attachments(manifest['visible_objects'])
    plan = {relative: patched.encode(), acf_path: reordered,
            manifest_path: (json.dumps(manifest, indent=2) + '\n').encode()}
    old_indices = {path: int(i) for i, path in old_refs}
    report = {
        'cause': 'Cockpit decals were attached before their base panels while using polygon-offset mode.',
        'polygon_offset_before': float(offsets[0]), 'polygon_offset_after': 0,
        'uv_and_geometry_preserved': True, 'texture_references_preserved': True,
        'geometry_animation_sha256': before['geometry_animation_sha256'],
        'attachment_changes': [{'object': path, 'before': old_indices[path], 'after': int(i)}
                               for i, path in new_refs if old_indices[path] != int(i)],
        'draw_order_errors_after': errors, 'runtime_verified': False,
        'documentation': 'https://developer.x-plane.com/article/using-polygon-offset/',
        'reference_renderer': 'https://github.com/X-Plane/xptools/blob/master/src/Obj/ObjDraw.cpp',
        'changed_files': [str(p) for p, data in plan.items() if (root / p).read_bytes() != data],
    }
    return plan, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aircraft', type=Path, default=PROJECT / 'output/A380X-XP12')
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    root = args.aircraft.resolve()
    plan, report = build_plan(root)
    report['mode'] = 'apply' if args.apply else 'dry-run'
    if args.apply:
        backup = install(root, plan)
        report['backup_manifest'] = str(backup) if backup else None
    if args.report:
        atomic_write(args.report, (json.dumps(report, indent=2) + '\n').encode())
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
