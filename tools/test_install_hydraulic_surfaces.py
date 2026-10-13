"""Exercise the hydraulic migration using only disposable mock aircraft trees."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import install_hydraulic_surfaces as installer
from aileron_visual_provider import (ALL_DATAREFS, BOOTSTRAP_FILES, DATAREF_CONTRACT,
                                     FILES, MANIFEST, PROVIDER_ID, digest,
                                     validate_provider)
from bind_hydraulic_surfaces import EXPECTED_FILES, bind_directory
from test_bind_hydraulic_surfaces import fixture


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode())


def write_json(path, value):
    write(path, json.dumps(value, indent=2) + '\n')


def snapshot(*roots):
    return {path: path.read_bytes() for root in roots for path in root.rglob('*')
            if path.is_file()}


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.project, self.aircraft, self.xplane, self.candidate, self.native = (
            self.root / name for name in ('project', 'aircraft', 'X-Plane', 'candidate', 'native'))
        self.backup = self.root / 'backup'
        self.plugin = self.xplane / 'Resources/plugins/FlyWithLua'
        for directory in (self.project, self.aircraft, self.native,
                          self.plugin / 'Scripts', self.plugin / 'Modules'):
            directory.mkdir(parents=True)
        self.closed = self.enterContext(patch.object(installer, 'assert_xplane_closed'))
        legacy = dict(format=1, provider=PROVIDER_ID, visual_only=True,
                      requires_global_installation=True,
                      datarefs=list(DATAREF_CONTRACT[:6]), files=[],
                      bootstrap=dict(registration='XPluginStart', files=[]))
        for index, (source, packaged, installed) in enumerate(FILES):
            write(self.project / source, f'new Lua {index}\n')
            if index < 2:
                write(self.aircraft / packaged, f'legacy Lua {index}\n')
                write(self.plugin / installed, f'legacy Lua {index}\n')
                legacy['files'].append(dict(path=packaged, install_path=installed,
                                            sha256=digest(self.aircraft / packaged)))
        old_binaries, new_binaries = [], []
        for (source, packaged), arches in zip(BOOTSTRAP_FILES, (['arm64', 'x86_64'], ['x86_64'])):
            name = Path(source).name
            write(self.project / source, f'legacy {name}\n')
            write(self.aircraft / packaged, f'legacy {name}\n')
            write(self.native / name, f'new {name}\n')
            old_hash = digest(self.project / source)
            legacy['bootstrap']['files'].append(dict(path=packaged, sha256=old_hash))
            old_binaries.append(dict(file=name, sha256=old_hash))
            new_binaries.append(dict(file=name, sha256=digest(self.native / name), architectures=arches))
        sources = []
        for name in ('aileron_datarefs.c', 'test_aileron_datarefs.c', 'build.py'):
            path = self.project / 'native/aileron_datarefs' / name
            write(path, f'mock source: {name}\n')
            sources.append(dict(name=name, sha256=digest(path)))
        write_json(self.project / installer.NATIVE_DIST / 'build-report.json',
                   dict(dataref_count=6, binaries=old_binaries))
        write_json(self.native / 'build-report.json',
                   dict(format=1, registration_phase='XPluginStart', dataref_count=22,
                        type='Float', writable=True, visual_only=True,
                        ranges=installer.EXPECTED_RANGES, binaries=new_binaries, sources=sources))
        write_json(self.aircraft / MANIFEST, legacy)
        write_json(self.aircraft / 'build-manifest.json',
                   dict(cockpit={'independent': [1, 2]}, aileron_visual_provider=legacy))
        write(self.aircraft / 'INSTALL_AILERON_VISUALS.txt', 'legacy install notes\n')
        write(self.aircraft / 'A380X-XP12.acf', 'do not alter aircraft physics\n')
        write(self.aircraft / 'textures/keep.png', b'untouched texture')
        write(self.aircraft / 'objects/cockpit/keep.obj', b'untouched cockpit')
        write(self.plugin / 'Scripts/unrelated.lua', b'untouched script')
        for group, names in EXPECTED_FILES.items():
            for name in names:
                write(self.aircraft / installer.EXTERIOR / group / name, fixture(group, name))
        bind_directory(self.aircraft / installer.EXTERIOR, self.candidate,
                       self.candidate / 'binding-report.json')
        self.roots = (self.project, self.aircraft, self.xplane, self.candidate, self.native)

    def install(self, apply=False):
        return installer.install(self.project, self.aircraft, self.xplane, self.candidate,
                                 self.native, self.backup, apply=apply)

    def test_dry_run_is_complete_and_does_not_mutate_files(self):
        before = snapshot(*self.roots)
        result = self.install()
        self.assertEqual(result['mode'], 'dry-run')
        self.assertEqual(result['dataref_count'], 22)
        self.assertEqual(result['header_token_changes'], 510)
        self.assertEqual(len(result['files']), 31)
        self.assertEqual(snapshot(*self.roots), before)
        self.assertFalse(self.backup.exists())
        self.assertFalse(list(self.root.glob('.hydraulic-install-*')))
        self.closed.assert_called_once_with()

    def test_migration_installs_every_artifact_and_merges_latest_manifest(self):
        real_package = installer.package_provider
        before = snapshot(*self.roots)

        def package_and_update_cockpit(*args):
            result = real_package(*args)
            path = self.aircraft / 'build-manifest.json'
            build = json.loads(path.read_text())
            build['avionics'] = {'concurrent_change': True}
            write_json(path, build)
            return result

        with patch.object(installer, 'package_provider', side_effect=package_and_update_cockpit):
            result = self.install(apply=True)
        self.assertEqual(self.closed.call_count, 2)
        self.assertEqual(result['mode'], 'apply')
        self.assertFalse(result['runtime_execution_verified'])
        self.assertTrue(result['runtime']['files_match_package'])
        self.assertEqual(validate_provider(self.aircraft), (set(ALL_DATAREFS), []))
        for name in installer.NATIVE_FILES:
            self.assertEqual((self.project / installer.NATIVE_DIST / name).read_bytes(),
                             (self.native / name).read_bytes())
        for source, packaged, installed in FILES:
            self.assertEqual((self.aircraft / packaged).read_bytes(), (self.project / source).read_bytes())
            self.assertEqual((self.plugin / installed).read_bytes(), (self.project / source).read_bytes())
        for source, packaged in BOOTSTRAP_FILES:
            self.assertEqual((self.aircraft / packaged).read_bytes(), (self.native / Path(source).name).read_bytes())
        for group, names in EXPECTED_FILES.items():
            for name in names:
                self.assertEqual((self.aircraft / installer.EXTERIOR / group / name).read_bytes(),
                                 (self.candidate / group / name).read_bytes())
        build = json.loads((self.aircraft / 'build-manifest.json').read_text())
        self.assertEqual(build['cockpit'], {'independent': [1, 2]})
        self.assertEqual(build['avionics'], {'concurrent_change': True})
        self.assertEqual(build['hydraulic_surface_provider']['header_token_changes'], 510)
        written = {Path(row['path']) for row in result['files']}
        for path, content in before.items():
            if path not in written:
                self.assertEqual(path.read_bytes(), content, str(path))
        for row in result['files']:
            self.assertEqual(digest(row['path']), row['after_sha256'])
            if row['backup']:
                self.assertEqual(digest(row['backup']), row['before_sha256'])
        self.assertTrue((self.backup / 'installation-plan.json').is_file())
        self.assertTrue((self.backup / 'installation-report.json').is_file())

    def test_stale_original_geometry_refuses_all_writes(self):
        group, names = next(iter(EXPECTED_FILES.items()))
        path = self.aircraft / installer.EXTERIOR / group / next(iter(names))
        write(path, path.read_bytes() + b'# independently edited\n')
        before = snapshot(*self.roots)
        with self.assertRaisesRegex(ValueError, 'geometry changed'):
            self.install(apply=True)
        self.assertEqual(snapshot(*self.roots), before)
        self.assertFalse(self.backup.exists())

    def test_conflicting_legacy_or_new_runtime_refuses_all_writes(self):
        for index, message in ((0, 'independently changed legacy runtime'),
                               (2, 'pre-existing hydraulic runtime')):
            with self.subTest(index=index):
                path = self.plugin / FILES[index][2]
                previous = path.read_bytes() if path.exists() else None
                write(path, 'user runtime\n')
                before = snapshot(*self.roots)
                with self.assertRaisesRegex(ValueError, message):
                    self.install(apply=True)
                self.assertEqual(snapshot(*self.roots), before)
                self.assertFalse(self.backup.exists())
                if previous is None:
                    path.unlink()
                else:
                    write(path, previous)

    def test_stale_native_source_evidence_refuses_all_writes(self):
        write(self.project / 'native/aileron_datarefs/aileron_datarefs.c', 'changed source\n')
        before = snapshot(*self.roots)
        with self.assertRaisesRegex(ValueError, 'source has changed'):
            self.install(apply=True)
        self.assertEqual(snapshot(*self.roots), before)
        self.assertFalse(self.backup.exists())

    def test_target_edit_during_staging_is_preserved(self):
        real_package = installer.package_provider
        path = self.plugin / FILES[0][2]
        before = snapshot(*self.roots)

        def package_and_edit_target(*args):
            result = real_package(*args)
            write(path, 'concurrent user edit\n')
            return result

        with patch.object(installer, 'package_provider', side_effect=package_and_edit_target):
            with self.assertRaisesRegex(ValueError, 'target changed during preparation'):
                self.install(apply=True)
        before[path] = b'concurrent user edit\n'
        self.assertEqual(snapshot(*self.roots), before)
        self.assertFalse(self.backup.exists())

    def test_failure_after_final_replacement_rolls_back_old_and_new_files(self):
        before = snapshot(*self.roots)
        real_copy = installer.atomic_copy
        failed = False

        def copy_then_fail(source, destination):
            nonlocal failed
            real_copy(source, destination)
            if destination == self.aircraft / 'build-manifest.json' and not failed:
                failed = True
                raise OSError('injected error after replacement')

        with patch.object(installer, 'atomic_copy', side_effect=copy_then_fail):
            with self.assertRaisesRegex(OSError, 'injected error after replacement'):
                self.install(apply=True)
        self.assertTrue(failed)
        self.assertEqual(snapshot(*self.roots), before)
        for _, packaged, installed in FILES[2:]:
            self.assertFalse((self.aircraft / packaged).exists())
            self.assertFalse((self.plugin / installed).exists())
        self.assertTrue((self.backup / 'installation-plan.json').is_file())
        self.assertFalse((self.backup / 'installation-report.json').exists())

    def test_xplane_starting_before_commit_prevents_installation(self):
        before = snapshot(*self.roots)
        self.closed.side_effect = [None, ValueError('Close X-Plane')]
        with self.assertRaisesRegex(ValueError, 'Close X-Plane'):
            self.install(apply=True)
        self.assertEqual(snapshot(*self.roots), before)
        self.assertTrue((self.backup / 'installation-plan.json').is_file())


class ProcessTests(unittest.TestCase):
    def test_detects_mac_linux_and_windows_process_names(self):
        for platform, output in (
            ('posix', '/Applications/X-Plane 12/X-Plane.app/Contents/MacOS/X-Plane\n'),
            ('posix', '/opt/X-Plane 12/X-Plane-x86_64\n'),
            ('nt', '"X-Plane.exe","123","Console","1","123 K"\n'),
        ):
            with self.subTest(platform=platform, output=output), patch.object(installer.os, 'name', platform):
                with patch.object(installer.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, output)):
                    with self.assertRaisesRegex(ValueError, 'Close X-Plane completely'):
                        installer.assert_xplane_closed()

    def test_unrelated_processes_allowed_and_failed_listing_propagates(self):
        with patch.object(installer.os, 'name', 'posix'):
            with patch.object(installer.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, '/usr/bin/python3\n')):
                installer.assert_xplane_closed()
            with patch.object(installer.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'ps')):
                with self.assertRaises(subprocess.CalledProcessError):
                    installer.assert_xplane_closed()


if __name__ == '__main__':
    unittest.main()
