#!/usr/bin/env python3
"""Validate/install only this aircraft's Windows plugins and required Lua drivers."""
import argparse
import base64
import ctypes
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path,PurePosixPath
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import urlencode
from urllib.request import urlopen

def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def local(root,relative):
    rel=PurePosixPath(relative)
    if rel.is_absolute() or '..' in rel.parts or ':' in relative or '\\' in relative:raise ValueError('Unsafe package path: '+relative)
    root=Path(root).resolve();path=(root/str(rel)).resolve()
    if not path.is_relative_to(root):raise ValueError('Path escapes selected directory: '+relative)
    return path

def verify(package,aircraft):
    package,aircraft=Path(package),Path(aircraft)
    manifest=json.loads((package/'windows-package.json').read_text(encoding='utf-8'))
    if manifest.get('platform')!='Windows x86-64' or not manifest.get('files'):raise ValueError('Not a complete Windows avionics package')
    if aircraft.name!='A380X-XP12' or not (aircraft/'A380X-XP12.acf').is_file():
        raise ValueError('Select the full aircraft folder named A380X-XP12, containing A380X-XP12.acf')
    for relative,expected in manifest['files'].items():
        if sha(local(package,relative))!=expected:raise ValueError('Package file hash mismatch: '+relative)
    for relative,expected in manifest['required_aircraft_files'].items():
        path=local(aircraft,relative)
        if not path.is_file() or sha(path)!=expected:
            raise ValueError('Aircraft geometry/version mismatch: '+relative+'. Copy the current complete A380X-XP12 aircraft first.')
    return manifest

def copy_atomic(source,target):
    target=Path(target);target.parent.mkdir(parents=True,exist_ok=True)
    descriptor,temporary=tempfile.mkstemp(prefix='.a380x-windows-',dir=target.parent)
    try:
        with open(source,'rb') as src,os.fdopen(descriptor,'wb') as dst:
            shutil.copyfileobj(src,dst,1024*1024);dst.flush();os.fsync(dst.fileno())
        os.replace(temporary,target)
    finally:
        if Path(temporary).exists():Path(temporary).unlink()

def installation_plan(package,aircraft,xplane,manifest):
    plan=[]
    for relative in manifest['files']:
        if relative.startswith('plugins/'):
            plan.append((local(package,relative),local(aircraft,relative),'aircraft/'+relative))
        elif relative.startswith('FlyWithLua/Modules/') or relative.startswith('FlyWithLua/Scripts/'):
            if xplane:plan.append((local(package,relative),local(xplane,'Resources/plugins/'+relative),'xplane/Resources/plugins/'+relative))
    if not plan:raise ValueError('No plugin payload')
    if len({str(row[1]) for row in plan})!=len(plan):raise ValueError('Duplicate installation target')
    return plan

def install_files(plan,backup):
    backup=Path(backup);backup.mkdir(parents=True,exist_ok=False);completed=[]
    try:
        for source,target,label in plan:
            saved=local(backup,label) if target.exists() else None
            if saved:copy_atomic(target,saved)
            completed.append((target,saved))
            copy_atomic(source,target)
            if sha(source)!=sha(target):raise RuntimeError('Installed hash mismatch: '+str(target))
    except BaseException:
        for target,saved in reversed(completed):
            if saved:copy_atomic(saved,target)
            elif target.exists():target.unlink()
        raise
    return backup

def metadata_plan(package,aircraft,manifest,scratch):
    """Keep Mac records while updating the Windows payload hashes/contracts."""
    package,aircraft,scratch=map(Path,(package,aircraft,scratch));updates=[]
    report_path='plugins/A380XAvionics/build-report.json'
    platform_path='plugins/A380XAvionics/win_x64/windows-build-report.json'
    if not (package/platform_path).is_file():return updates
    windows=json.loads((package/platform_path).read_text(encoding='utf-8'))
    if (aircraft/report_path).is_file():
        report=json.loads((aircraft/report_path).read_text(encoding='utf-8'))
        winrow=next(row for row in windows['binaries'] if row['file']=='win.xpl')
        report['binaries']=[winrow if row['file']=='win.xpl' else row for row in report['binaries']]
        report['xp_kit_native_windows']=windows['xp_kit_native_windows']
        report['windows_build_report']=platform_path
        staged=scratch/'build-report.json';staged.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        updates.append((staged,local(aircraft,report_path),'aircraft/'+report_path))
    provider_path='avionics-providers/fbw-avionics-v1.json'
    if (aircraft/provider_path).is_file():
        provider=json.loads((aircraft/provider_path).read_text(encoding='utf-8'))
        for row in provider.get('files',[]):
            if row['path'] in manifest['files']:row['sha256']=manifest['files'][row['path']]
            elif row['path']==report_path and updates:row['sha256']=sha(updates[0][0])
        provider['windows_runtime']={'build_report':platform_path,'renderer':'Microsoft Edge WebView2',
            'minimum_xplane':'12.4.0','runtime_tested':False}
        staged=scratch/'provider.json';staged.write_text(json.dumps(provider,indent=2)+'\n',encoding='utf-8')
        updates.append((staged,local(aircraft,provider_path),'aircraft/'+provider_path))
    return updates

def runtime_preflight(package,xplane):
    if sys.platform!='win32':raise RuntimeError('Run installation on 64-bit Windows; --check-only is available on macOS')
    if ctypes.sizeof(ctypes.c_void_p)!=8:raise RuntimeError('A 64-bit Windows Python runtime is required')
    result=subprocess.run(['tasklist','/FI','IMAGENAME eq X-Plane.exe','/FO','CSV','/NH'],capture_output=True,text=True,timeout=10,creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode or 'x-plane.exe' in result.stdout.lower():raise RuntimeError('Quit X-Plane before updating its plugins')
    if not (xplane/'X-Plane.exe').is_file():raise RuntimeError('X-Plane.exe was not found in the selected simulator directory')
    fwl=xplane/'Resources/plugins/FlyWithLua/win_x64/FlyWithLua.xpl'
    if not fwl.is_file():raise RuntimeError('Install FlyWithLua NG for X-Plane 12 first: https://github.com/X-Friese/FlyWithLua/releases')
    for name in ('A380X_XP12_Avionics.lua','A380X_XP12_Autopilot.lua','A380X_XP12_EFB.lua'):
        if (xplane/'Resources/plugins/FlyWithLua/Scripts'/name).is_file():
            raise RuntimeError('Legacy duplicate controller must be moved out of FlyWithLua/Scripts first: '+name)
    loader=ctypes.WinDLL(str(local(package,'plugins/A380XAvionics/win_x64/WebView2Loader.dll')))
    available=loader.GetAvailableCoreWebView2BrowserVersionString
    available.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_void_p)];available.restype=ctypes.c_long
    version=ctypes.c_void_p();status=available(None,ctypes.byref(version))
    if status<0 or not version.value:raise RuntimeError('Install Microsoft Edge WebView2 Evergreen Runtime: https://developer.microsoft.com/microsoft-edge/webview2/')
    try:return ctypes.wstring_at(version)
    finally:
        release=ctypes.windll.ole32.CoTaskMemFree;release.argtypes=[ctypes.c_void_p];release(version)

def live_check():
    def request(path):
        with urlopen('http://127.0.0.1:8086/api/v3/'+path,timeout=10) as response:return json.load(response)
    def read(name):
        rows=request('datarefs?'+urlencode({'filter[name]':name}))['data']
        exact=[r for r in rows if r['name']==name]
        if len(exact)!=1:raise RuntimeError('Missing aircraft dataref: '+name)
        return request('datarefs/'+str(exact[0]['id'])+'/value')['data']
    path=base64.b64decode(read('sim/aircraft/view/acf_relative_path')).decode().rstrip('\0').replace('\\','/')
    if not path.endswith('/A380X-XP12/A380X-XP12.acf'):raise RuntimeError('The project aircraft is not loaded: '+path)
    names=['helper_running','captain_active','first_officer_active','captain_nd_active','first_officer_nd_active','captain_mfd_active','first_officer_mfd_active','sd_active','errors']
    health={name:read('a380x/xp12/xpkit/'+name) for name in names}
    passed=all(health[n]==1 for n in names if n!='errors') and health['errors']==0
    report={'aircraft':path,'health':health,'passed':passed,
        'scope':'Read-only live display-host health only. Does not prove flight controls, clickable areas, AP laws, hydraulics or complete flight functionality.'}
    print(json.dumps(report,indent=2))
    if not passed:raise RuntimeError('Display health failed. Power the aircraft and inspect X-Plane Log.txt and win_x64/display-host.log.')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package',type=Path,default=Path(__file__).resolve().parent)
    parser.add_argument('--aircraft',type=Path);parser.add_argument('--xplane',type=Path)
    parser.add_argument('--check-only',action='store_true');parser.add_argument('--live-check',action='store_true')
    args=parser.parse_args()
    if args.live_check:live_check();return
    if not args.aircraft:parser.error('--aircraft is required')
    package,aircraft=args.package.resolve(),args.aircraft.resolve()
    manifest=verify(package,aircraft)
    plan=installation_plan(package,aircraft,args.xplane.resolve() if args.xplane else None,manifest)
    if args.check_only:
        print(json.dumps({'check_only':True,'passed':True,'files':len(plan),'windows_runtime_checked':False}));return
    if not args.xplane:parser.error('--xplane is required to install the required Lua drivers')
    runtime=runtime_preflight(package,args.xplane.resolve())
    stamp=datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f')
    with tempfile.TemporaryDirectory(prefix='a380x-windows-metadata-') as temporary:
        plan+=metadata_plan(package,aircraft,manifest,Path(temporary))
        backup=install_files(plan,aircraft/'.windows-avionics-backups'/stamp)
    print(json.dumps({'installed':str(aircraft),'backup':str(backup),'webview2_runtime':runtime,
        'next_step':'Load the aircraft in Windows X-Plane 12.4+, then run Check-WindowsAvionics.ps1 and the manual checklist.'},indent=2))

if __name__=='__main__':
    try:main()
    except Exception as error:print(str(error),file=sys.stderr);sys.exit(1)
