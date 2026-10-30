#!/usr/bin/env python3
"""Install staged cargo geometry and an optional verified native UI build.

Back up and atomically replace only reviewed paths. Flight-model properties,
all old attachments, cockpit controls, avionics HTML and calibration survive.
Never starts X-Plane; installation refuses while the simulator is running.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import subprocess

from avionics_provider import MANIFEST
from install_xp_kit_cockpit import copy_file, sha

ROOT = Path(__file__).resolve().parents[1]
CARGO_ATTACHMENTS = tuple('interior/cargo/'+n for n in (
    'a380x_cargo_holds.obj','a380x_cargo_holds_decals.obj','a380x_cargo_hardware.obj'))


def append_attachments(text):
    matches = list(re.finditer(r'^P _obja/(\d+)/_v10_att_file_stl (.+)$',text,re.M))
    paths = {m[2]:int(m[1]) for m in matches}
    present = set(CARGO_ATTACHMENTS).intersection(paths)
    if present == set(CARGO_ATTACHMENTS):
        return text
    if present:
        raise ValueError('Partial existing cargo installation; inspect before replacing')
    base = paths.get('exterior/fuselage/a380x_fuse1.obj')
    if base is None:
        raise ValueError('Expected authored fuselage attachment not found')
    template = re.findall(r'^P _obja/'+str(base)+r'/[^\n]*\n?',text,re.M)
    if not template:
        raise ValueError('Fuselage attachment lacks its transformation properties')
    count = max(paths.values())+1
    added = []
    for offset,path in enumerate(CARGO_ATTACHMENTS):
        for line in template:
            line = re.sub(r'^(P _obja/)\d+/',lambda m:m[1]+str(count+offset)+'/',line)
            line = re.sub(r'(/_v10_att_file_stl )[^\n]*',lambda m:m[1]+path,line)
            added.append(line.rstrip('\n')+'\n')
    property_rows = list(re.finditer(r'^P _obja/[^\n]*\n?',text,re.M))
    at = property_rows[-1].end()
    changed = text[:at]+''.join(added)+text[at:]
    changed,n = re.subn(r'^P _obja/count \d+$','P _obja/count '+str(count+3),changed,flags=re.M)
    if n != 1:
        raise ValueError('Expected one object count in saved ACF')
    # Exclude only added attachments/count; every pre-existing byte survives.
    restored = re.sub(r'^P _obja/(?:'+ '|'.join(str(count+i) for i in range(3))+r')/[^\n]*\n?','',changed,flags=re.M)
    restored = re.sub(r'^P _obja/count \d+$','P _obja/count '+str(count),restored,flags=re.M)
    if restored != text:
        raise ValueError('Cargo integration altered existing ACF properties')
    return changed


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('staging',type=Path)
    parser.add_argument('--aircraft',type=Path,default=ROOT/'output/A380X-XP12')
    parser.add_argument('--native',type=Path)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--check-only',action='store_true')
    args=parser.parse_args()
    staging,aircraft=args.staging.resolve(),args.aircraft.resolve()
    cargo=json.loads((staging/'cargo-report.json').read_text())
    sources={}
    for row in cargo['files']:
        relative=row['path'];source=Path(row['staged']).resolve();target=(aircraft/relative).resolve()
        if not source.is_relative_to(staging) or not target.is_relative_to(aircraft):
            raise ValueError('Cargo asset escapes staging/aircraft')
        if sha(source)!=row['sha256']:
            raise ValueError('Staged cargo asset changed: '+relative)
        if target.exists() and sha(target) not in (row['before_sha256'],row['sha256']):
            raise ValueError('Installed asset changed during staging: '+relative)
        sources[target]=source
    for path in [aircraft/'A380X-XP12.acf']:
        encoded=append_attachments(path.read_text(encoding='latin1')).encode('latin1')
        temporary=staging/'saved-acf'/path.name
        temporary.parent.mkdir(exist_ok=True);temporary.write_bytes(encoded);sources[path]=temporary
    if aircraft == (ROOT/'output/A380X-XP12').resolve():
        canonical=ROOT/'acf/A380X-XP12-current.acf'
        if canonical.exists():
            temp=staging/'saved-acf/canonical.acf'
            temp.write_text(append_attachments(canonical.read_text(encoding='latin1')),encoding='latin1')
            sources[canonical]=temp
    native=None
    if args.native:
        native=args.native.resolve()
        report=json.loads((native/'build-report.json').read_text())
        for row in report['binaries']:
            if sha(native/row['file'])!=row['sha256']:
                raise ValueError('Native binary hash mismatch')
        subprocess.run(['codesign','--verify','--strict',str(native/'mac.xpl')],check=True,capture_output=True)
        if sha(native/'A380XDisplayHost')!=report['xp_kit_native_pfd']['helper_sha256']:
            raise ValueError('Mac display helper hash mismatch')
        plugin=aircraft/'plugins/A380XAvionics'
        sources.update({plugin/'mac_x64/A380XAvionics.xpl':native/'mac.xpl',
            plugin/'win_x64/A380XAvionics.xpl':native/'win.xpl',
            plugin/'mac_x64/A380XDisplayHost':native/'A380XDisplayHost',
            plugin/'mac_x64/xp-kit-build-report.json':native/'build-report.json',
            plugin/'build-report.json':native/'build-report.json'})
        win=report.get('xp_kit_native_windows')
        if win:
            for name,key in (('A380XDisplayHost.exe','helper_sha256'),('WebView2Loader.dll','loader_sha256')):
                source=native/'windows-host'/name
                if sha(source)!=win[key]: raise ValueError('Windows helper hash mismatch')
                sources[plugin/'win_x64'/name]=source
    protected=[aircraft/'fcu-calibration.ini',aircraft/'objects/interior/cockpit-controls/a380x_cockpit_controls.obj']
    protected += list((aircraft/'plugins/A380XAvionics/mac_x64/xp-kit').glob('*.html'))
    preserved={str(p):sha(p) for p in protected if p.exists()}
    result=dict(passed=False,check_only=args.check_only,scope='Offline cargo installation; X-Plane not launched',
                aircraft=str(aircraft),files=[str(p) for p in sources],preserved_before=preserved)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    if args.check_only:
        result['passed']=True;args.report.write_text(json.dumps(result,indent=2)+'\n');return
    if subprocess.run(['pgrep','-x','X-Plane'],capture_output=True).returncode==0:
        parser.error('Quit X-Plane before installing; no aircraft files changed')
    backup=ROOT/'build/backups'/('cargo-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
    backup.mkdir(parents=True)
    manifest_path=aircraft/MANIFEST
    all_paths=[*sources,manifest_path]
    existed={p:p.exists() for p in all_paths}
    saved={p:backup/str(i) for i,p in enumerate(all_paths)}
    for p in all_paths:
        if existed[p]:copy_file(p,saved[p])
    result['backup']=str(backup)
    (backup/'rollback.json').write_text(json.dumps({str(p):str(saved[p]) if existed[p] else None for p in all_paths},indent=2)+'\n')
    try:
        for target,source in sources.items():
            target.parent.mkdir(parents=True,exist_ok=True);copy_file(source,target)
            if sha(source)!=sha(target):raise ValueError('Installed hash mismatch: '+str(target))
        provider=json.loads(manifest_path.read_text())
        for row in provider['files']:row['sha256']=sha(aircraft/row['path'])
        provider['cargo']=dict(doors={'FWD':17,'AFT':18,'BULK':19},objects=list(CARGO_ATTACHMENTS),runtime_tested=False)
        manifest_path.write_text(json.dumps(provider,indent=2)+'\n')
        result['preserved_after']={str(p):sha(p) for p in protected if p.exists()}
        if result['preserved_after']!=preserved:raise ValueError('Protected cockpit/avionics files changed')
        result['passed']=True
    except BaseException:
        for p in reversed(all_paths):
            if existed[p]:copy_file(saved[p],p)
            else:p.unlink(missing_ok=True)
        raise
    finally:
        args.report.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(passed=result['passed'],backup=str(backup),files=len(sources))))


if __name__=='__main__':main()
