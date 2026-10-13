#!/usr/bin/env python3
"""Install a verified MIP03 export without refreshing other cockpit objects.

Dry-run by default. --apply backs up the ACF/manifest and installs only this
object and its textures. Use patch_cockpit_materials.py --restore MANIFEST to undo.
"""
from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path

from audit_cockpit_uvs import audit_cockpit_uvs
from cockpit_draw_order import order_cockpit_attachments, reorder_acf_attachments
from patch_cockpit_materials import (PROJECT, DEFAULT_SOURCE, OBJ_DIR, atomic_write,
                                     digest, inspect_obj, install, texture_references)
from prepare_mip03_restoration import EXPORT_NAME, MATERIAL


def attach(acf: str, object_path: str) -> tuple[str, int]:
    refs = re.findall(r'^P _obja/(\d+)/_v10_att_file_stl (.+)$', acf, re.M)
    existing = [int(i) for i, name in refs if name == object_path]
    if existing:
        if len(existing) != 1:
            raise ValueError("MIP03 has duplicate attachments")
        return acf, existing[0]
    counts = re.findall(r'^P _obja/count (\d+)$', acf, re.M)
    if len(counts) != 1:
        raise ValueError("Expected one attachment count")
    count = int(counts[0])
    if sorted(int(i) for i, _ in refs) != list(range(count)):
        raise ValueError("Attachment indexes are not contiguous")
    matches = [(i, name) for i, name in refs if name.endswith('/panel_a380_cockpit_mip04_4k.obj')]
    if len(matches) != 1:
        raise ValueError("Expected one MIP04 attachment as placement reference")
    index, reference = matches[0]
    block = re.findall(rf'^P _obja/{index}/[^\n]*\n', acf, re.M)
    if len(block) != 13:
        raise ValueError("Unexpected MIP04 attachment schema")
    added = ''.join(block).replace(f'P _obja/{index}/', f'P _obja/{count}/').replace(reference, object_path)
    result = acf.replace(f'P _obja/count {count}\n', added + f'P _obja/count {count + 1}\n', 1)
    return result, count


def plan_install(root: Path, build: Path, source: Path):
    preparation = json.loads((build / 'reports/cockpit-mip03-preparation.json').read_text())
    if preparation['source_sha256'] != digest(source.read_bytes()):
        raise ValueError("Preparation does not match source glTF")
    exported = build / 'xplane-obj/cockpit-panels-textured' / (EXPORT_NAME + '.obj')
    text = exported.read_text()
    info = inspect_obj(text)
    if info['triangles_drawn'] != preparation['source_geometry']['triangles']:
        raise ValueError("Exported triangle count does not match MIP03 source")
    for directive in ('NORMAL_METALNESS', 'ATTR_no_cull', 'ATTR_no_blend', 'TEXTURE_LIT'):
        if not any(line.strip().split()[0] == directive for line in text.splitlines() if line.strip()):
            raise ValueError(f"Missing expected material state: {directive}")
    uv = audit_cockpit_uvs(source, build, {MATERIAL: str(exported.relative_to(build))})
    if not uv['ok']:
        raise ValueError('Exported UVs differ from source: ' + json.dumps(uv))
    relative_obj = OBJ_DIR / exported.name
    plan, changes = {}, {}
    for directive, filename in texture_references(text):
        texture = (exported.parent / filename).resolve()
        if not texture.is_relative_to(build) or not texture.is_file():
            raise ValueError(f"Texture is missing or outside prepared build: {texture}")
        destination = Path('textures/interior/cockpit-panels') / texture.name
        plan[destination] = texture.read_bytes()
        changes[directive] = Path(os.path.relpath(root / destination, (root / relative_obj).parent)).as_posix()
    lines = []
    for line in text.splitlines():
        refs = list(texture_references(line))
        if refs:
            line = refs[0][0] + ' ' + changes[refs[0][0]]
        if line.strip() != 'GLOBAL_cockpit_lit':
            lines.append(line)
    packaged = '\n'.join(lines) + '\n'
    if inspect_obj(packaged)['geometry_animation_sha256'] != info['geometry_animation_sha256']:
        raise ValueError("Packaging changed geometry or animations")
    plan[relative_obj] = packaged.encode()
    acf_path = Path('A380X-XP12.acf')
    acf = (root / acf_path).read_text()
    patched, index = attach(acf, relative_obj.relative_to('objects').as_posix())
    plan[acf_path] = reorder_acf_attachments(patched.encode())
    index = next(int(i) for i, name in re.findall(r'^P _obja/(\d+)/_v10_att_file_stl (.+)$',
                 plan[acf_path].decode(), re.M) if name == relative_obj.relative_to('objects').as_posix())
    manifest_path = Path('build-manifest.json')
    manifest = json.loads((root / manifest_path).read_text())
    if str(relative_obj) not in manifest['visible_objects']:
        manifest['visible_objects'].append(str(relative_obj))
    manifest['visible_objects'] = order_cockpit_attachments(manifest['visible_objects'])
    plan[manifest_path] = (json.dumps(manifest, indent=2) + '\n').encode()
    report = {'source': str(source), 'triangles_added': info['triangles_drawn'],
              'vertices': info['vertices'], 'object': str(relative_obj),
              'attachment_index': index, 'source_uv_audit': uv,
              'geometry_animation_sha256': info['geometry_animation_sha256'],
              'changed_files': [str(p) for p, data in plan.items() if not (root / p).is_file() or (root / p).read_bytes() != data],
              'runtime_verified': False,
              'animation_limit': 'Unmapped GRVTY_LDG_LOCK1 remains at source rest pose.'}
    return plan, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aircraft', type=Path, default=PROJECT / 'output/A380X-XP12')
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=DEFAULT_SOURCE)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    root, build = args.aircraft.resolve(), args.build_dir.resolve()
    plan, report = plan_install(root, build, args.source.resolve())
    report['mode'] = 'apply' if args.apply else 'dry-run'
    if args.apply:
        backup = install(root, plan)
        report['backup_manifest'] = str(backup) if backup else None
    if args.report:
        atomic_write(args.report, (json.dumps(report, indent=2) + '\n').encode())
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
