#!/usr/bin/env python3
"""Reproduce the source cargo conversion without Blender or a simulator.

Use --install for the preservation-aware, backed-up aircraft update; --native
also installs the verified native UI build. Otherwise only staging is written.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--asset-root',type=Path,required=True)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--aircraft',type=Path,default=ROOT/'output/A380X-XP12')
    parser.add_argument('--native',type=Path)
    parser.add_argument('--install',action='store_true')
    args=parser.parse_args()
    output=args.build_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    source=args.source.resolve();normalized=output/'normalized/cargo.gltf';manifest=output/'manifests/cargo.json'
    selected=output/'selected-materials.json'
    selected.write_text(json.dumps(dict(exports=[dict(materials=['Cargo_FWD','Cargo_FWD_DECAL','A380_EXTERIOR_MISC','A380X_FUSE3'])])))
    def run(*argv):subprocess.run([sys.executable,*map(str,argv)],check=True,cwd=ROOT)
    run(ROOT/'blender/prepare_gltf_subset.py',source,normalized,'--manifest',selected,
        '--node-pattern',r'(Cube|Circle)\..*|BULK_CARGO_DOOR|MAIN FUSELAGE\.(117|102)')
    run(ROOT/'blender/prepare_interior_materials.py',source,'--asset-root',args.asset_root.resolve(),
        '--texture-dir',output/'textures/cargo','--manifest',manifest,'--report',output/'material-report.json',
        '--prefix','cargo','--include-material','^Cargo_FWD$','--include-material','^Cargo_FWD_DECAL$',
        '--include-material','^A380_EXTERIOR_MISC$','--allow-transparent-material','^Cargo_FWD_DECAL$',
        '--polygon-offset','1','--max-size','2048','--detail-max-size','1024','--reuse-decoded')
    run(ROOT/'tools/build_cargo_geometry.py','--source',source,'--normalized',normalized,
        '--materials',manifest,'--aircraft',args.aircraft.resolve(),'--output',output/'geometry')
    if args.install:
        command=[ROOT/'tools/install_cargo.py',output/'geometry','--aircraft',args.aircraft.resolve(),
                 '--report',output/'installation.json']
        if args.native:command.extend(['--native',args.native.resolve()])
        run(*command)


if __name__=='__main__':main()
