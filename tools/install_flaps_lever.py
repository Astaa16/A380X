#!/usr/bin/env python3
"""Apply a staged FLAPS-lever patch (tools/flaps_lever_cockpit.py) to the aircraft and the project ACF snapshot.

Exactly the files of the staging report are replaced: the pedestal-misc cockpit OBJ (lever re-bound to the
plugin, pull-out sleeve), the cockpit controls OBJ (lift-detent manipulator), the six exterior wing OBJs
(flap/slat clips on the FBW FPPU ratios), the aircraft ACF and acf/A380X-XP12-current.acf (seven-detent
FBW slat/flap schedule). Each file is replaced atomically and only when it still has the exact bytes it
was staged from (a file that already has the staged bytes is skipped). All previous files are backed up
first and every replaced file is restored if any replacement fails. Dry run with --check-only. Never
touches plugins, the provider manifest or the simulator; install the rebuilt A380XAvionics plugin with
tools/install_xp_kit_cockpit.py afterwards (it writes the a380x/xp12/flaps commands and datarefs into
the provider manifest, which the new animation datarefs need).
"""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from flaps_lever_cockpit import ACF, CONTROLS, MARKER, PEDESTAL, PROJECT_ACF, WINGS

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {('aircraft', p) for p in (PEDESTAL, CONTROLS, *WINGS, ACF)} | {('project', PROJECT_ACF)}


def sha(path: Path) -> str:
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def atomic_copy(source: Path, destination: Path) -> None:
    fd, name = tempfile.mkstemp(prefix='.a380x-flaps-', dir=destination.parent)
    temporary = Path(name)
    try:
        with source.open('rb') as incoming, os.fdopen(fd, 'wb') as outgoing:
            shutil.copyfileobj(incoming, outgoing, length=1024 * 1024)
            outgoing.flush()
            os.fsync(outgoing.fileno())
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def plan(aircraft: Path, project_acf: Path, stage: Path) -> list[dict]:
    report = json.loads((stage / 'staging-report.json').read_text())
    if report.get('format') != 1 or report.get('marker') != MARKER:
        raise ValueError('Not a FLAPS-lever staging report')
    if Path(report['aircraft']).resolve() != aircraft:
        raise ValueError(f'Geometry was staged for {report["aircraft"]}, not {aircraft}')
    if Path(report['project_acf']).resolve() != project_acf:
        raise ValueError(f'The ACF snapshot was staged from {report["project_acf"]}, not {project_acf}')
    rows = report['files']
    if {(row['root'], row['path']) for row in rows} != EXPECTED or len(rows) != len(EXPECTED):
        raise ValueError('Staging report must list exactly the FLAPS-lever objects and the two ACFs')
    actions = []
    for row in rows:
        base = aircraft if row['root'] == 'aircraft' else None
        target = base / row['path'] if base else project_acf
        staged = stage / row['root'] / row['path']
        if not staged.resolve().is_relative_to(stage) or (base and not target.resolve().is_relative_to(aircraft)):
            raise ValueError(f'Unsafe path {row["path"]}')
        if sha(staged) != row['after_sha256']:
            raise ValueError(f'Staged file changed after staging: {row["path"]}')
        current = sha(target)
        if current == row['after_sha256']:
            actions.append(dict(row, target=str(target), staged=str(staged), action='already-applied'))
        elif current == row['before_sha256']:
            actions.append(dict(row, target=str(target), staged=str(staged), action='replace'))
        else:
            raise ValueError(f'File changed since staging (re-stage): {row["root"]}/{row["path"]} {current}')
    return actions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aircraft', type=Path, required=True)
    parser.add_argument('--project-acf', type=Path, default=ROOT / PROJECT_ACF)
    parser.add_argument('--geometry-stage', type=Path, required=True)
    parser.add_argument('--backup', type=Path, help='default: build/backups/flaps-lever-<timestamp>')
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    aircraft, stage, project_acf = args.aircraft.resolve(), args.geometry_stage.resolve(), args.project_acf.resolve()
    if not (aircraft / ACF).is_file():
        parser.error('Expected the A380X-XP12 aircraft folder')
    actions = plan(aircraft, project_acf, stage)
    if args.check_only:
        print(json.dumps({'check_only': True, 'passed': True, 'aircraft': str(aircraft), 'project_acf': str(project_acf),
                          'files': [{k: r[k] for k in ('root', 'path', 'action', 'before_sha256', 'after_sha256')}
                                    for r in actions]}, indent=2))
        return
    if subprocess.run(['pgrep', '-x', 'X-Plane'], capture_output=True).returncode == 0:
        parser.error('Quit X-Plane before replacing aircraft files')
    backup = (args.backup or ROOT / 'build/backups' / ('flaps-lever-' + datetime.now().strftime('%Y%m%d-%H%M%S'))).resolve()
    for row in actions:
        if row['action'] != 'replace':
            continue
        saved = backup / row['root'] / row['path']
        saved.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(row['target'], saved)
        if sha(saved) != row['before_sha256']:
            raise RuntimeError(f'Backup verification failed: {saved}')
    replaced = []
    try:
        for row in actions:
            if row['action'] == 'replace':
                atomic_copy(Path(row['staged']), Path(row['target']))
                replaced.append(row)
                if sha(Path(row['target'])) != row['after_sha256']:
                    raise RuntimeError(f'Installed file differs from the staged file: {row["path"]}')
    except BaseException:
        for row in replaced:
            atomic_copy(backup / row['root'] / row['path'], Path(row['target']))
        raise
    print(json.dumps({'installed': [f'{r["root"]}/{r["path"]}' for r in replaced],
                      'backup': str(backup) if replaced else None, 'aircraft': str(aircraft),
                      'next': 'python3 tools/install_xp_kit_cockpit.py <native build dir>'}, indent=2))


if __name__ == '__main__':
    main()
