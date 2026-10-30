#!/usr/bin/env python3
"""Create a verified Windows plugin update kit, without touching the Mac installation."""
import argparse
import json
from pathlib import Path
import shutil
import zipfile
from install_windows_avionics import sha

ROOT=Path(__file__).resolve().parents[1]
PYTHON_SHA='a93abe456ab01bd96d7a085b3cdb6566b3063f4241360d114142fbdb07f0a310'
REQUIRED=('A380X-XP12.acf','objects/interior/cockpit-controls/a380x_cockpit_controls.obj',
    'objects/interior/cockpit-panels/a380x_mfd_screens.obj','cockpit_3d/-PANELS-/Panel.png',
    'objects/interior/cockpit-panels/panel_a380_cockpit_fcu_4k.obj')

def copy(source,target):
    target=Path(target);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)

def package(native,aileron,python_zip,output,aircraft=None):
    native,aileron,python_zip,output=map(Path,(native,aileron,python_zip,output))
    aircraft=Path(aircraft) if aircraft else ROOT/'output/A380X-XP12'
    if output.exists():raise ValueError('Choose a fresh package directory')
    report=json.loads((native/'build-report.json').read_text());win=report.get('xp_kit_native_windows')
    if not win or not win['ipc_c_cpp_abi_equal']:raise ValueError('Build --xpkit-windows first')
    expected=next(r['sha256'] for r in report['binaries'] if r['file']=='win.xpl')
    if sha(native/'win.xpl')!=expected:raise ValueError('Native plugin hash mismatch')
    host=native/'windows-host'
    if sha(host/'A380XDisplayHost.exe')!=win['helper_sha256'] or sha(host/'WebView2Loader.dll')!=win['loader_sha256']:
        raise ValueError('Display helper/loader hash mismatch')
    if sha(python_zip)!=PYTHON_SHA:raise ValueError('Expected official Python 3.14.8 x64 embeddable archive')
    for name,expected in report['xp_kit_native_pfd']['asset_hashes'].items():
        p=Path(name)
        if p.is_absolute() or '..' in p.parts or sha(native/'xp-kit'/p)!=expected:raise ValueError('Display asset hash mismatch: '+name)
    output.mkdir(parents=True)
    target=output/'plugins/A380XAvionics/win_x64'
    copy(native/'win.xpl',target/'A380XAvionics.xpl')
    for name in ('A380XDisplayHost.exe','WebView2Loader.dll','WebView2-LICENSE.txt','windows-display-build.json'):
        copy(host/name,target/name)
    shutil.copytree(native/'xp-kit',target/'xp-kit',ignore=shutil.ignore_patterns('._*'))
    assets=json.loads((target/'xp-kit/manifest.json').read_text());assets['renderer']='Windows Microsoft Edge WebView2'
    assets.pop('reused_assets_from',None);assets['windows_helper_sha256']=win['helper_sha256']
    (target/'xp-kit/manifest.json').write_text(json.dumps(assets,indent=2)+'\n')
    copy(native/'build-report.json',target/'windows-build-report.json')
    aileron_report=json.loads((aileron/'build-report.json').read_text())
    if sha(aileron/'win.xpl')!=next(r['sha256'] for r in aileron_report['binaries'] if r['file']=='win.xpl'):
        raise ValueError('Aileron/hydraulic dataref plugin hash mismatch')
    copy(aileron/'win.xpl',output/'plugins/A380XAileronDatarefs/win_x64/A380XAileronDatarefs.xpl')
    copy(aileron/'build-report.json',output/'plugins/A380XAileronDatarefs/win_x64/windows-build-report.json')
    for name in ('priority_left.pcm','priority_right.pcm'):
        copy(ROOT/'native/avionics/sounds'/name,output/'plugins/A380XAvionics/sounds'/name)
    with zipfile.ZipFile(python_zip) as archive:
        for name in archive.namelist():
            rel=Path(name)
            if rel.is_absolute() or '..' in rel.parts or ':' in name or '\\' in name:raise ValueError('Unsafe embedded-runtime path')
        archive.extractall(target/'python')
    # Isolated embedded Python sees only its stdlib and the bundled scenery reader.
    (target/'python/python314._pth').write_text('python314.zip\n.\n../xp-kit\n')
    for name in ('A380X_XP12_Ailerons.lua','A380X_XP12_Hydraulics.lua'):
        copy(aircraft/'plugins/FlyWithLua/Scripts (required)'/name,output/'FlyWithLua/Scripts'/name)
    for name in ('a380x_aileron_dance.lua','a380x_hydraulic_surfaces.lua'):
        copy(aircraft/'plugins/FlyWithLua/Modules'/name,output/'FlyWithLua/Modules'/name)
    for name in ('Install-WindowsAvionics.ps1','Check-WindowsAvionics.ps1','install_windows_avionics.py'):
        copy(ROOT/'tools'/name,output/name)
    copy(ROOT/'docs/WINDOWS_AVIONICS.txt',output/'WINDOWS-README.txt');copy(ROOT/'LICENSE',output/'LICENSE')
    # Corresponding project sources; never include other aircrafts, user scripts,
    # build caches, credentials or system installations in a redistributable kit.
    for directory in ('native/avionics','native/aileron_datarefs','FlyWithLua','tools','config','tests'):
        source=ROOT/directory
        for p in source.rglob('*'):
            if not p.is_file() or p.name.startswith('._') or p.name=='.DS_Store' or any(part in ('dist','dist-initial-build','node_modules','__pycache__') for part in p.relative_to(source).parts):continue
            if p.suffix not in ('.o','.xpl','.exe','.dll','.pyc','.zip'):
                copy(p,output/'source'/directory/p.relative_to(source))
    for name in ('build_windows_display_host.py','package_windows_avionics.py'):
        copy(ROOT/'tools'/name,output/'source/tools'/name)
    for name in ('README.md','install_windows.ps1','LICENSE'):
        copy(ROOT/name,output/'source'/name)
    manifest={'format':1,'platform':'Windows x86-64','minimum_xplane':'12.4.0','windows_runtime_tested':False,
        'included':['native avionics and systems plugin','seven-view WebView2 display host','WebView2 loader','isolated Python airport reader',
            'aileron/hydraulic dataref plugin','required Lua drivers','GPL project source'],
        'external_requirements':['Microsoft Edge WebView2 Evergreen Runtime','FlyWithLua NG for X-Plane 12'],
        'python':{'version':'3.14.8','source':'https://www.python.org/ftp/python/3.14.8/python-3.14.8-embed-amd64.zip','archive_sha256':PYTHON_SHA},
        'required_aircraft_files':{name:sha(aircraft/name) for name in REQUIRED},
        'native_plugin_sha256':sha(target/'A380XAvionics.xpl'),
        'files':{str(p.relative_to(output)).replace('\\','/'):sha(p) for p in sorted(output.rglob('*')) if p.is_file() and not p.name.startswith('._')}}
    (output/'windows-package.json').write_text(json.dumps(manifest,indent=2)+'\n')
    zip_path=output.with_suffix('.zip')
    with zipfile.ZipFile(zip_path,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for p in sorted(output.rglob('*')):
            if p.is_file():archive.write(p,output.name+'/'+str(p.relative_to(output)))
    return {'package':str(output),'archive':str(zip_path),'files':len(manifest['files']),'sha256':sha(zip_path),'windows_runtime_tested':False}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--native',required=True,type=Path);p.add_argument('--aileron',required=True,type=Path)
    p.add_argument('--python-zip',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();print(json.dumps(package(a.native,a.aileron,a.python_zip,a.output),indent=2))
