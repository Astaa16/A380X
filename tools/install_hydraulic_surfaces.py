#!/usr/bin/env python3
"""Install audited hydraulic OBJ tokens and their visual runtime with rollback.

Dry-run by default; --apply requires X-Plane to be closed. Only the twenty
reviewed exterior OBJs, provider files, four global Lua files, source bootstrap
binaries/report and two provider manifest fields are eligible for replacement.
ACF, textures, avionics and other cockpit files are never written.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from aileron_visual_provider import (ALL_DATAREFS, BOOTSTRAP_FILES, DATAREF_CONTRACT,
    DATAREFS, FILES, MANIFEST, PROVIDER_ID, digest, package_provider,
    validate_provider, verify_installed)
from bind_hydraulic_surfaces import EXPECTED_FILES, rebind_object
from package_aileron_visuals import atomic_copy, checked_target

BUILD_MANIFEST = Path('build-manifest.json')
EXTERIOR = Path('objects/exterior')
NATIVE_DIST = Path('native/aileron_datarefs/dist')
NATIVE_FILES = ('mac.xpl', 'win.xpl', 'build-report.json')
EXPECTED_RANGES = [dict(suffix='_ratio', count=14, minimum=-1, maximum=1),
                   dict(suffix='_deg', count=8, minimum=0, maximum=65)]


def assert_xplane_closed():
    if os.name == 'nt':
        result = subprocess.run(['tasklist', '/FO', 'CSV', '/NH'],
                                check=True, capture_output=True, text=True)
        names = [row[0] for row in csv.reader(result.stdout.splitlines()) if row]
    else:
        result = subprocess.run(['ps', '-axo', 'comm='],
                                check=True, capture_output=True, text=True)
        names = [Path(line.strip()).name for line in result.stdout.splitlines()]
    if any(name.lower() in ('x-plane', 'x-plane.exe', 'x-plane-x86_64') for name in names):
        raise ValueError('Close X-Plane completely before installing hydraulic surfaces')


def load_object(path):
    result = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(result, dict):
        raise ValueError(f'Expected JSON object: {path}')
    return result


def current_hash(path):
    return digest(path) if path.is_file() else None


def validate_legacy(aircraft, plugin):
    manifest = checked_target(aircraft, MANIFEST, True)
    old = load_object(manifest)
    if (old.get('format') != 1 or old.get('provider') != PROVIDER_ID
            or old.get('visual_only') is not True
            or old.get('requires_global_installation') is not True
            or old.get('datarefs') != list(DATAREF_CONTRACT[:len(DATAREFS)])):
        raise ValueError('Expected exact legacy six-ref provider contract')
    rows = old.get('files')
    if not isinstance(rows, list) or len(rows) != 2:
        raise ValueError('Expected the two legacy Lua files')
    for row, (_, packaged, installed) in zip(rows, FILES[:2]):
        if row.get('path') != packaged or row.get('install_path') != installed:
            raise ValueError('Unexpected legacy Lua paths')
        for root, relative in ((aircraft, packaged), (plugin, installed)):
            path = checked_target(root, Path(relative), True)
            if digest(path) != row.get('sha256'):
                raise ValueError(f'Preserving independently changed legacy runtime: {path}')
    for _, packaged, installed in FILES[2:]:
        for root, relative in ((aircraft, packaged), (plugin, installed)):
            path = checked_target(root, Path(relative))
            if path.exists():
                raise ValueError(f'Preserving pre-existing hydraulic runtime: {path}')
    bootstrap = old.get('bootstrap', {})
    rows = bootstrap.get('files')
    if bootstrap.get('registration') != 'XPluginStart' or not isinstance(rows, list) or len(rows) != 2:
        raise ValueError('Expected installed legacy early-registration plugin')
    for row, (_, packaged) in zip(rows, BOOTSTRAP_FILES):
        path = checked_target(aircraft, Path(packaged), True)
        if row.get('path') != packaged or digest(path) != row.get('sha256'):
            raise ValueError(f'Preserving independently changed legacy bootstrap: {path}')
    return old


def validate_native(project, native_stage, legacy):
    report = load_object(checked_target(native_stage, Path('build-report.json'), True))
    if (report.get('format') != 1 or report.get('registration_phase') != 'XPluginStart'
            or report.get('dataref_count') != 22 or report.get('type') != 'Float'
            or report.get('writable') is not True or report.get('visual_only') is not True
            or report.get('ranges') != EXPECTED_RANGES):
        raise ValueError('Staged bootstrap is not the verified 22-ref build')
    rows = report.get('binaries', [])
    if [row.get('file') for row in rows] != ['mac.xpl', 'win.xpl']:
        raise ValueError('Expected exact Mac and Windows bootstrap artifacts')
    for row, arches in zip(rows, ({'arm64', 'x86_64'}, {'x86_64'})):
        path = checked_target(native_stage, Path(row['file']), True)
        if digest(path) != row.get('sha256') or set(row.get('architectures', [])) != arches:
            raise ValueError(f'Staged bootstrap binary/hash mismatch: {path}')
    sources = report.get('sources', [])
    if [row.get('name') for row in sources] != ['aileron_datarefs.c', 'test_aileron_datarefs.c', 'build.py']:
        raise ValueError('Staged bootstrap has unexpected source evidence')
    for row in sources:
        path = checked_target(project, Path('native/aileron_datarefs') / row['name'], True)
        if digest(path) != row.get('sha256'):
            raise ValueError(f'Staged bootstrap source has changed: {path}')
    old_report_path = checked_target(project, NATIVE_DIST / 'build-report.json', True)
    old_report = load_object(old_report_path)
    old_rows = old_report.get('binaries', [])
    if old_report.get('dataref_count') != 6 or [r.get('file') for r in old_rows] != ['mac.xpl', 'win.xpl']:
        raise ValueError('Source dist is not the expected legacy bootstrap build')
    for row, old_binary in zip(old_rows, legacy['bootstrap']['files']):
        path = checked_target(project, NATIVE_DIST / row['file'], True)
        if digest(path) != row.get('sha256') or row['sha256'] != old_binary['sha256']:
            raise ValueError(f'Preserving independently changed source bootstrap: {path}')
    return report


def validate_geometry(candidate, aircraft):
    report = load_object(checked_target(candidate, Path('binding-report.json'), True))
    expected = {f'{group}/{name}' for group, names in EXPECTED_FILES.items() for name in names}
    rows = report.get('objects', [])
    if (report.get('format') != 1 or report.get('source_untouched') is not True
            or len(rows) != len(expected) or {row.get('object') for row in rows} != expected):
        raise ValueError('Candidate must identify exactly the twenty reviewed exterior objects')
    records = []
    for row in rows:
        relative = Path(row['object'])
        original = checked_target(aircraft, EXTERIOR / relative, True)
        proposed = checked_target(candidate, relative, True)
        if digest(original) != row.get('source_sha256'):
            raise ValueError(f'Installed geometry changed since candidate staging: {original}')
        output, proof = rebind_object(original.read_bytes(), relative.parts[0], relative.name)
        if proof != row or proposed.read_bytes() != output:
            raise ValueError(f'Candidate must change only audited animation reference tokens: {relative}')
        records.append(proof)
    if report.get('header_token_changes') != sum(row['header_token_changes'] for row in records):
        raise ValueError('Candidate token total disagrees with verified objects')
    return report


def install(project, aircraft, xplane, candidate, native_stage, backup, *, apply=False):
    if backup.exists() or backup.is_symlink():
        raise ValueError('Choose a new backup directory')
    project, aircraft, xplane, candidate, native_stage, backup = (
        Path(path).resolve() for path in (project, aircraft, xplane, candidate, native_stage, backup))
    for root in (project, aircraft, xplane, candidate, native_stage, backup.parent):
        if not root.is_dir():
            raise ValueError(f'Required directory does not exist: {root}')
    if any(backup.is_relative_to(root) for root in (aircraft, xplane, candidate, native_stage)):
        raise ValueError('Backup must be outside aircraft, simulator and candidate inputs')
    if native_stage == project / NATIVE_DIST or native_stage.is_relative_to(project / NATIVE_DIST):
        raise ValueError('Native stage must be separate from source dist')
    assert_xplane_closed()
    plugin = xplane / 'Resources/plugins/FlyWithLua'
    if not (plugin / 'Scripts').is_dir() or not (plugin / 'Modules').is_dir():
        raise ValueError('FlyWithLua Scripts and Modules directories are required')
    # Capture replacement targets before validation/staging. Otherwise a target
    # changed while packaging could become the accepted baseline for overwrite.
    # The build manifest is intentionally read later to merge unrelated fields.
    early_targets = [
        *(checked_target(project, NATIVE_DIST / name) for name in NATIVE_FILES),
        *(checked_target(plugin, Path(installed)) for _, _, installed in FILES),
        *(checked_target(aircraft, Path(packaged)) for _, packaged, _ in FILES),
        *(checked_target(aircraft, Path(packaged)) for _, packaged in BOOTSTRAP_FILES),
        checked_target(aircraft, MANIFEST),
        checked_target(aircraft, Path('INSTALL_AILERON_VISUALS.txt')),
        *(checked_target(aircraft, EXTERIOR / group / name)
          for group, names in EXPECTED_FILES.items() for name in names),
    ]
    initial_hashes = {path: current_hash(path) for path in early_targets}
    legacy = validate_legacy(aircraft, plugin)
    native = validate_native(project, native_stage, legacy)
    geometry = validate_geometry(candidate, aircraft)
    acf = checked_target(aircraft, Path('A380X-XP12.acf'), True)
    acf_hash = digest(acf)
    watched = {path: digest(path) for path in (
        checked_target(candidate, Path('binding-report.json'), True),
        *(checked_target(candidate, Path(row['object']), True) for row in geometry['objects']),
        *(checked_target(native_stage, Path(name), True) for name in NATIVE_FILES),
        *(checked_target(project, Path(source), True) for source, _, _ in FILES),
        *(checked_target(project, Path('native/aileron_datarefs') / row['name'], True)
          for row in native['sources']),
    )}
    with tempfile.TemporaryDirectory(prefix='.hydraulic-install-', dir=backup.parent) as temp:
        stage = Path(temp)
        provider_source = stage / 'project'
        for source, _, _ in FILES:
            atomic_copy(project / source, provider_source / source)
        for source, _ in BOOTSTRAP_FILES:
            atomic_copy(native_stage / Path(source).name, provider_source / source)
        package_stage = stage / 'aircraft'
        provider = package_provider(package_stage, provider_source)
        refs, errors = validate_provider(package_stage)
        if errors or refs != set(ALL_DATAREFS):
            raise ValueError('Staged provider failed verification: ' + '; '.join(errors))
        mappings = []

        def add(source, root, relative, area):
            target = checked_target(root, relative)
            mappings.append(dict(source=source, target=target, area=area, relative=relative,
                                 before=current_hash(target), after=digest(source)))

        for name in NATIVE_FILES:
            add(native_stage / name, project, NATIVE_DIST / name, 'project')
        for _, packaged, installed in FILES:
            add(package_stage / packaged, plugin, Path(installed), 'FlyWithLua')
        for row in geometry['objects']:
            relative = Path(row['object'])
            add(candidate / relative, aircraft, EXTERIOR / relative, 'aircraft')
        for source in sorted(package_stage.rglob('*')):
            if source.is_file():
                add(source, aircraft, source.relative_to(package_stage), 'aircraft')

        # Read the latest whole manifest only after expensive staging so concurrent
        # independent cockpit/avionics fields are preserved, then guard its hash.
        manifest_path = checked_target(aircraft, BUILD_MANIFEST, True)
        build = load_object(manifest_path)
        build['aileron_visual_provider'] = provider
        build['hydraulic_surface_provider'] = {
            'format': 1, 'visual_only': True, 'runtime_execution_verified': False,
            'header_token_changes': geometry['header_token_changes'],
            'objects': geometry['objects'], 'backup': str(backup)}
        manifest_stage = stage / 'build-manifest.json'
        manifest_stage.write_text(json.dumps(build, indent=2) + '\n', encoding='utf-8')
        add(manifest_stage, aircraft, BUILD_MANIFEST, 'aircraft')
        if len({row['target'] for row in mappings}) != len(mappings):
            raise ValueError('Overlapping installation targets')
        changes = [row for row in mappings if row['before'] != row['after']]
        result = {
            'format': 1, 'mode': 'apply' if apply else 'dry-run', 'aircraft': str(aircraft),
            'backup': str(backup), 'dataref_count': len(ALL_DATAREFS),
            'header_token_changes': geometry['header_token_changes'],
            'acf_sha256': acf_hash, 'acf_unchanged': True, 'textures_written': False,
            'runtime_execution_verified': False,
            'files': [dict(path=str(row['target']), before_sha256=row['before'],
                           after_sha256=row['after'],
                           backup=str(backup / row['area'] / row['relative']) if row['before'] else None)
                      for row in changes],
        }

        def unchanged():
            if any(current_hash(path) != sha for path, sha in initial_hashes.items()):
                raise ValueError('Installation target changed during preparation; refusing overwrite')
            if any(current_hash(row['target']) != row['before'] for row in mappings):
                raise ValueError('Installation target changed during preparation; refusing overwrite')
            if any(digest(path) != sha for path, sha in watched.items()) or digest(acf) != acf_hash:
                raise ValueError('Installation input or ACF changed during preparation')

        unchanged()
        if not apply:
            return result
        backup.mkdir()
        for row in changes:
            if row['before'] is not None:
                saved = backup / row['area'] / row['relative']
                saved.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(row['target'], saved)
                if digest(saved) != row['before']:
                    raise ValueError('Backup verification failed; no installation performed')
        (backup / 'installation-plan.json').write_text(json.dumps(result, indent=2) + '\n')
        unchanged()
        assert_xplane_closed()
        written = []
        try:
            for row in changes:
                # Include a target before copying: an injected/OS error after
                # replacement must still restore that target from the backup.
                written.append(row)
                atomic_copy(row['source'], row['target'])
            if any(digest(row['target']) != row['after'] for row in mappings):
                raise ValueError('Installed file verification failed')
            if any(digest(path) != sha for path, sha in watched.items()) or digest(acf) != acf_hash:
                raise ValueError('Installation input or ACF changed during deployment')
            refs, errors = validate_provider(aircraft)
            if errors or refs != set(ALL_DATAREFS):
                raise ValueError('Installed provider failed verification: ' + '; '.join(errors))
            result['runtime'] = verify_installed(aircraft, plugin)
            (backup / 'installation-report.json').write_text(json.dumps(result, indent=2) + '\n')
        except Exception as error:
            failures = []
            for row in reversed(written):
                try:
                    if row['before'] is None:
                        row['target'].unlink(missing_ok=True)
                    else:
                        atomic_copy(backup / row['area'] / row['relative'], row['target'])
                    if current_hash(row['target']) != row['before']:
                        raise ValueError('Restored hash disagrees with backup')
                except Exception as restore_error:
                    failures.append(f"{row['target']}: {restore_error}")
            if failures:
                raise RuntimeError('Installation failed and rollback requires recovery from '
                                   + str(backup) + ': ' + '; '.join(failures)) from error
            raise
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('project', 'aircraft', 'xplane', 'candidate', 'native-stage', 'backup'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    print(json.dumps(install(**vars(parser.parse_args())), indent=2))


if __name__ == '__main__':
    main()
