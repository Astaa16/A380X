#!/usr/bin/env python3
"""Prepare/export all five FBW gear assemblies; never write upstream assets."""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--asset-root', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--blender', type=Path)
    parser.add_argument('--export-only', action='store_true')
    parser.add_argument('--reuse-materials', action='store_true',
                        help='Reuse existing texture manifests, but rebuild normalized geometry')
    args = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    scripts = project / 'blender'
    attachments = args.asset_root / 'fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire'
    parts = [('body-lh', 'Body_LH', 'A380_GEAR_BODY_LH_LOD00'),
             ('body-rh', 'Body_RH', 'A380_GEAR_BODY_RH_LOD00'),
             ('wing-lh', 'Wing_LH', 'A380_GEAR_WING_LH'),
             ('wing-rh', 'Wing_RH', 'A380_GEAR_WING_RH'),
             ('nose', 'Nose', 'A380_GEAR_NOSE_LOD01')]
    for group, folder, filename in parts:
        group = 'gear-' + group
        source = attachments / ('Part_Exterior_Gear_' + folder) / 'model' / (filename + '.gltf')
        manifest = args.build_dir / 'manifests' / (group + '-exports.json')
        normalized = args.build_dir / 'normalized' / (group + '-complete') / (filename + '.geometry.gltf')
        if not args.export_only:
            if not args.reuse_materials or not manifest.is_file():
                subprocess.run([sys.executable, str(scripts / 'prepare_interior_materials.py'), str(source),
                                '--asset-root', str(args.asset_root), '--texture-dir', str(args.build_dir / 'textures' / (group+'-complete')),
                                '--manifest', str(manifest), '--report', str(args.build_dir / 'reports' / (group+'-materials.json')),
                                '--prefix', group.replace('-', '_'), '--max-size', '2048', '--detail-max-size', '1024',
                                '--include-material', '^A380'], check=True)
            subprocess.run([sys.executable, str(scripts / 'prepare_gltf_subset.py'), str(source), str(normalized),
                            '--manifest', str(manifest)], check=True)
        if args.blender:
            subprocess.run([str(args.blender), '--background', '--python-exit-code', '1', '--python', str(scripts / 'convert_gltf_animations.py'), '--',
                            '--input', str(normalized), '--mapping', str(project / 'config/animation_datarefs.json'),
                            '--output', str(args.build_dir / 'blend' / (group+'-complete.blend')),
                            '--report', str(args.build_dir / 'reports' / (group+'-complete.json')),
                            '--export-manifest', str(manifest), '--export-dir', str(args.build_dir / 'xplane-obj' / (group+'-complete'))], check=True)


if __name__ == '__main__':
    main()
