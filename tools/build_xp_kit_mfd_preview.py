"""Build an isolated interactive MFD web-review engine; never install the aircraft."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
NATIVE=ROOT/'native/avionics'
OUTPUT=ROOT/'build/xp-kit-mfd-preview'
sys.path.insert(0,str(NATIVE))
from build import MFD_CORE_SOURCES, MFD_PAGE_MODULES
from package_xp_kit_cockpit import package_mfd


def main():
    OUTPUT.mkdir(parents=True,exist_ok=True)
    adapter=(NATIVE/'a380x_fms_adapter.inc').read_text()
    start='/* ---- FMS nav hook '
    end='/* ---- Published FMS datarefs '
    assert adapter.count(start)==adapter.count(end)==1
    hooks=adapter[adapter.index(start):adapter.index(end)]
    assert 'XPLM' not in hooks
    (OUTPUT/'preview_fms_hooks.inc').write_text(hooks)
    pages=[NATIVE/f'a380x_mfd_page_{name}.c' for name in MFD_PAGE_MODULES]
    sources=[NATIVE/'xp-kit-mfd-preview/preview.c',*MFD_CORE_SOURCES,*pages]
    flags=[f'-DA380X_MFD_HAVE_PAGE_{name.upper()}=1' for name in MFD_PAGE_MODULES if name!='data']
    subprocess.run(['clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-dynamiclib',
                    '-I',str(NATIVE),'-I',str(OUTPUT),*flags,*map(str,sources),'-o',str(OUTPUT/'libMfdPreview.dylib')],check=True)
    (OUTPUT/'view').mkdir(exist_ok=True)
    package_mfd(OUTPUT/'view')
    for name in ('FBW-Display-EIS-A380.ttf','FBW-Display-EIS-A380-SlashedZero.ttf'):
        destination=OUTPUT/'view/Fonts/fbw-a380x'/name
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(ROOT/'build/xp-kit-nd-native/Fonts/fbw-a380x'/name,destination)
    (OUTPUT/'build-report.json').write_text(json.dumps({'scope':'isolated web preview only; no simulator connection or installation',
        'native_sources':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
        'plan_hooks_sha256':hashlib.sha256(hooks.encode()).hexdigest()},indent=2))
    print('MFD preview built:',OUTPUT)


if __name__=='__main__':main()
