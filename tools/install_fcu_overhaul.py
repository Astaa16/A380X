#!/usr/bin/env python3
"""Install the verified FCU native build and reviewed FCU-only assets offline.

Never launches X-Plane. Preflights hashes and native signatures, backs up the
affected files, and restores them if installation fails. Existing user detent
calibration, the ACF and all display HTML assets are preserved.
"""
from __future__ import annotations
import argparse
from datetime import datetime
import json
from pathlib import Path
import shutil
import subprocess
import sys

from avionics_provider import MANIFEST, FILES
from install_xp_kit_cockpit import ROOT, copy_file, sha, write_text


def local(root: Path, relative: str) -> Path:
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()):
        raise ValueError('Asset path escapes root: ' + relative)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('native', type=Path)
    parser.add_argument('geometry', type=Path)
    parser.add_argument('materials', type=Path)
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    aircraft = ROOT / 'output/A380X-XP12'
    native, geometry, materials = (p.resolve() for p in (args.native, args.geometry, args.materials))
    native_report = json.loads((native/'build-report.json').read_text())
    geometry_report = json.loads((geometry/'staging-report.json').read_text())
    material_report = json.loads((materials/'material-report.json').read_text())
    rows = [(row['relative'], Path(row['staged']), row['before_sha256'], row['after_sha256'])
            for row in geometry_report['files']]
    rows.extend((row['path'], local(materials,row['path']), row['base_sha256'], row['sha256'])
                for row in material_report['files'])
    if len(rows)!=8 or len({row[0] for row in rows})!=8:
        raise ValueError('Expected exactly six reviewed FCU geometry/atlas files and two material files')
    for relative, source, before, after in rows:
        if not (source.is_relative_to(geometry) or source.is_relative_to(materials)):
            raise ValueError('Staged asset is outside its reviewed tree')
        if sha(source)!=after or sha(local(aircraft,relative)) not in (before,after):
            raise ValueError('Changed staged/installed asset: ' + relative)
    for row in material_report['preserved_assets']:
        if sha(local(aircraft,row['path']))!=row['sha256']:
            raise ValueError('Authored FCU albedo/normal changed')
    windows = next(row for row in native_report['binaries'] if row['file']=='win.xpl')
    if sha(native/'win.xpl')!=windows['sha256']:
        raise ValueError('Windows plugin hash mismatch')
    installer=[sys.executable,str(ROOT/'tools/install_xp_kit_cockpit.py'),str(native)]
    preflight=subprocess.run([*installer,'--check-only'],check=True,capture_output=True,text=True)
    preserved_paths=['A380X-XP12.acf',*[
        'plugins/A380XAvionics/mac_x64/xp-kit/'+name for name in ('pfd.html','nd.html','mfd.html','sd.html')]]
    calibration=aircraft/'fcu-calibration.ini'
    if calibration.exists(): preserved_paths.append('fcu-calibration.ini')
    preserved={name:sha(local(aircraft,name)) for name in preserved_paths}
    report=dict(scope='Offline FCU installation; simulator was not launched or tested',
                check_only=args.check_only,passed=False,native=str(native),
                native_preflight=json.loads(preflight.stdout),
                geometry_report=str(geometry/'staging-report.json'),
                material_report=str(materials/'material-report.json'),
                preserved_before=preserved,files=[dict(path=r[0],sha256=r[3]) for r in rows])
    if args.check_only:
        report['passed']=True
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2));return
    if subprocess.run(['pgrep','-x','X-Plane'],capture_output=True).returncode==0:
        parser.error('Quit X-Plane before installing; no files were changed')
    backup=ROOT/'build/backups'/('fcu-overhaul-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
    backup.mkdir(parents=True)
    mac=aircraft/'plugins/A380XAvionics/mac_x64'
    shutil.copytree(mac,backup/'mac_x64',copy_function=copy_file,ignore=shutil.ignore_patterns('._*'))
    affected={r[0] for r in rows}|{str(MANIFEST),FILES[-1][1],
        'plugins/A380XAvionics/win_x64/A380XAvionics.xpl','fcu-calibration.ini',
        'objects/interior/cockpit-panels/a380x_mfd_screens.obj'}
    existed={name:local(aircraft,name).exists() for name in affected}
    for name,exists in existed.items():
        if exists:
            target=backup/'original'/name;target.parent.mkdir(parents=True,exist_ok=True)
            copy_file(local(aircraft,name),target)
    report['backup']=str(backup)
    (backup/'rollback-manifest.json').write_text(json.dumps(dict(aircraft=str(aircraft),files=existed),indent=2)+'\n')
    try:
        for relative,source,_before,_after in rows:
            copy_file(source,local(aircraft,relative))
        installed=subprocess.run([*installer,'--no-backup'],check=True,capture_output=True,text=True)
        report['native_install_output']=installed.stdout
        copy_file(native/'win.xpl',aircraft/'plugins/A380XAvionics/win_x64/A380XAvionics.xpl')
        provider_path=aircraft/MANIFEST
        provider=json.loads(provider_path.read_text())
        for row in provider['files']: row['sha256']=sha(local(aircraft,row['path']))
        provider['fcu_overhaul']=dict(display_region=[1024,1088,2048,1216],
            motion_datarefs=native_report['fcu_motion_datarefs'],light_datarefs=native_report['fcu_light_datarefs'],
            manual='User-provided ATA22 training extracts, 18 April 2006; partial reference',runtime_tested=False)
        write_text(provider_path,json.dumps(provider,indent=2)+'\n')
        for relative,_source,_before,after in rows:
            if sha(local(aircraft,relative))!=after: raise ValueError('Installed FCU asset mismatch: '+relative)
        report['preserved_after']={name:sha(local(aircraft,name)) for name in preserved}
        if report['preserved_after']!=preserved: raise ValueError('Protected ACF/calibration/display assets changed')
        report['mac_sha256']=sha(mac/'A380XAvionics.xpl')
        report['windows_sha256']=sha(aircraft/'plugins/A380XAvionics/win_x64/A380XAvionics.xpl')
        report['passed']=True
    except BaseException:
        for name,exists in existed.items():
            target=local(aircraft,name)
            if exists: copy_file(backup/'original'/name,target)
            else: target.unlink(missing_ok=True)
        shutil.copytree(backup/'mac_x64',mac,dirs_exist_ok=True,copy_function=copy_file)
        raise
    finally:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
