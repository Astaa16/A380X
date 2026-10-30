#!/usr/bin/env python3
"""Build/test an independent staged universal Mac trim module; never installs it."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

HERE=Path(__file__).resolve().parent

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    a=p.parse_args();out=a.output.resolve();sdk=a.sdk.resolve()
    if out.exists():p.error('Choose a fresh staging directory')
    out.mkdir(parents=True)
    evidence=[]
    def run(argv):
        r=subprocess.run(list(map(str,argv)),capture_output=True,text=True)
        evidence.append(dict(argv=list(map(str,argv)),stdout=r.stdout,stderr=r.stderr,code=r.returncode))
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        return r.stdout.strip()
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations']
    core=run(['clang',*flags,HERE/'trim.c',HERE/'test_trim.c','-o',out/'test_trim'])
    del core
    test=run([out/'test_trim'])
    defs=['-DAPL=1',*[f'-DXPLM{v}=1' for v in (200,210,300,301,302,303,400,410)]]
    run(['clang',*flags,*defs,'-I',sdk/'CHeaders/XPLM',HERE/'trim.c',HERE/'test_plugin.c',
         '-o',out/'test_plugin'])
    adapter_test=run([out/'test_plugin'])
    binary=out/'A380XTrim.xpl'
    run(['clang',*flags,*defs,'-fvisibility=hidden','-I',sdk/'CHeaders/XPLM',
         '-arch','arm64','-arch','x86_64','-mmacosx-version-min=11.0','-dynamiclib',
         HERE/'trim.c',HERE/'plugin.c','-F',sdk/'Libraries/Mac','-framework','XPLM',
         '-framework','OpenGL','-Wl,-install_name,A380XTrim.xpl','-o',binary])
    run(['codesign','--force','--sign','-',binary]);run(['codesign','--verify','--strict',binary])
    archs=run(['lipo','-archs',binary]);assert set(archs.split())=={'arm64','x86_64'}
    report=dict(format=1,staged_only=True,simulator_tested=False,minimum_xplane='12.1.0',
        test=test,adapter_test=adapter_test,architectures=archs.split(),sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        source_hashes={n:hashlib.sha256((HERE/n).read_bytes()).hexdigest()
                       for n in ('trim.h','trim.c','plugin.c','test_trim.c','test_plugin.c','build.py')},commands=evidence)
    (out/'build-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(test);print(adapter_test);print('Staged:',out)

if __name__=='__main__':main()
