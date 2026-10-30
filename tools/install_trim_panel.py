#!/usr/bin/env python3
"""Install the independently staged trim module with a full APFS recovery copy.

Only three cockpit OBJs and a NEW aircraft-local plugin are installed. Every
other aircraft file is verified byte-identical against the recovery copy.
Never changes the ACF, restored avionics, Windows plugins or global simulator.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from trim_cockpit_panel import CONTROLS,PANEL,SCREENS

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def inventory(root):
    return {str(p.relative_to(root)):sha(p) for p in root.rglob('*')
            if p.is_file() and not p.name.startswith('._')}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--aircraft',required=True,type=Path)
    p.add_argument('--native-stage',required=True,type=Path)
    p.add_argument('--geometry-stage',required=True,type=Path)
    p.add_argument('--check-only',action='store_true');a=p.parse_args()
    aircraft=a.aircraft.resolve();native=a.native_stage.resolve();geometry=a.geometry_stage.resolve()
    if aircraft.is_symlink() or not (aircraft/'A380X-XP12.acf').is_file():p.error('Expected installed A380X-XP12 aircraft')
    if (aircraft/'plugins/A380XTrim').exists():p.error('Trim plugin already exists; will not overwrite it')
    if subprocess.run(['pgrep','-x','X-Plane'],capture_output=True).returncode==0:
        p.error('Quit X-Plane before installation')
    report=json.loads((native/'build-report.json').read_text())
    layout=json.loads((geometry/'staging-report.json').read_text())
    if Path(layout['aircraft']).resolve()!=aircraft:p.error('Geometry was staged for a different aircraft')
    binary=native/'A380XTrim.xpl'
    assert sha(binary)==report['sha256'],'Native plugin checksum mismatch'
    subprocess.run(['codesign','--verify','--strict',str(binary)],check=True)
    assert set(r['path'] for r in layout['files'])=={CONTROLS,PANEL,SCREENS}
    for row in layout['files']:
        assert sha(aircraft/row['path'])==row['before_sha256'],'Aircraft changed since staging: '+row['path']
        assert sha(geometry/row['path'])==row['after_sha256'],'Staged OBJ changed: '+row['path']
    if a.check_only:print('PASS: native signature/checksum and all three staged cockpit assets');return
    # Recovery is deliberately OUTSIDE Aircraft/ so discovery/update tools cannot
    # mistake an old recovery aircraft for the active installation.
    sim=aircraft.parent.parent
    recovery_parent=sim/'Output/aircraft-restore-backups';recovery_parent.mkdir(parents=True,exist_ok=True)
    recovery=Path(tempfile.mkdtemp(prefix='A380X-before-trim-20261028-',dir=recovery_parent))
    subprocess.run(['cp','-c','-pR',str(aircraft),str(recovery/'aircraft')],check=True)
    before=inventory(recovery/'aircraft')
    if before!=inventory(aircraft):raise RuntimeError('Recovery copy differs from live aircraft; nothing installed')
    package=recovery/'staged-A380XTrim';(package/'mac_x64').mkdir(parents=True)
    shutil.copy2(binary,package/'mac_x64/A380XTrim.xpl')
    shutil.copy2(native/'build-report.json',package/'build-report.json')
    readme=Path(__file__).resolve().parents[1]/'native/trim/README.txt'
    shutil.copy2(readme,package/'README.txt')
    changes=[]
    try:
        for row in layout['files']:
            shutil.copy2(geometry/row['path'],aircraft/row['path'])
        package.rename(aircraft/'plugins/A380XTrim')
        after=inventory(aircraft)
        for name in sorted(before.keys()|after.keys()):
            if before.get(name)!=after.get(name):
                if name not in (CONTROLS,PANEL,SCREENS) and not name.startswith('plugins/A380XTrim/'):
                    raise RuntimeError('Unexpected file changed: '+name)
                changes.append(dict(path=name,before=before.get(name),after=after.get(name)))
        for row in layout['files']:assert after[row['path']]==row['after_sha256']
        assert after['plugins/A380XTrim/mac_x64/A380XTrim.xpl']==report['sha256']
        subprocess.run(['codesign','--verify','--strict',str(aircraft/'plugins/A380XTrim/mac_x64/A380XTrim.xpl')],check=True)
    except BaseException:
        if (aircraft/'plugins/A380XTrim').exists():
            (aircraft/'plugins/A380XTrim').rename(recovery/'failed-A380XTrim')
        for row in layout['files']:shutil.copy2(recovery/'aircraft'/row['path'],aircraft/row['path'])
        raise
    evidence=dict(format=1,installed_utc=datetime.now(timezone.utc).isoformat(),aircraft=str(aircraft),
        recovery=str(recovery),native_stage=str(native),geometry_stage=str(geometry),
        changes=changes,unchanged_files=sum(before.get(n)==h for n,h in after.items()),
        preserved_acf_sha256=after['A380X-XP12.acf'],
        preserved_avionics_sha256=after['plugins/A380XAvionics/mac_x64/A380XAvionics.xpl'],
        core_test=report['test'],adapter_test=report['adapter_test'],simulator_tested=False)
    (recovery/'trim-install-report.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print(json.dumps(evidence,indent=2))

if __name__=='__main__':main()
