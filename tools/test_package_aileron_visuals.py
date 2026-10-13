import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from aileron_visual_provider import BOOTSTRAP_FILES, FILES, MANIFEST, validate_provider
from bind_aileron_visuals import EXPECTED_FILES, rebind_object
from build_development_aircraft import copy_exterior_obj
from package_aileron_visuals import package, PROVIDER_PATHS, WINGS, WING_ROOT
from test_bind_aileron_visuals import fixture


class ScopedPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.candidate, self.aircraft, self.textures, self.project = (
            self.root / name for name in ("candidate", "aircraft", "textures", "project"))
        self.backup, self.report = self.root / "backup", self.root / "report.json"
        for path in (self.candidate, self.aircraft, self.textures, self.project):
            path.mkdir()
        (self.textures / "wing.png").write_bytes(b"source texture")
        target_texture = self.aircraft / "textures/exterior/wings/wing.png"
        target_texture.parent.mkdir(parents=True)
        target_texture.write_bytes(b"USER TEXTURE EDIT MUST SURVIVE")
        for name in EXPECTED_FILES:
            original = fixture() if name in WINGS else fixture(())
            proposed, _ = rebind_object(original, name)
            (self.candidate / name).write_bytes(proposed)
            source = self.root / name
            source.write_bytes(original)
            copy_exterior_obj(source, self.aircraft / WING_ROOT / name, self.textures, "wings")
        for source, _, _ in FILES:
            path = self.project / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("-- provider fixture " + path.name)
        for source, _ in BOOTSTRAP_FILES:
            path = self.project / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"bootstrap fixture")
        (self.aircraft / "A380X-XP12.acf").write_bytes(b"USER ACF EDIT")
        (self.aircraft / "README.txt").write_bytes(b"USER README")
        cockpit = self.aircraft / "objects/interior/cockpit/custom.obj"
        cockpit.parent.mkdir(parents=True)
        cockpit.write_bytes(b"USER COCKPIT")
        self.manifest = {"visible_objects": [str(WING_ROOT / name) for name in WINGS],
                         "user_field": {"nested": [1, "preserve me"]}, "replacement_count": 1234}
        (self.aircraft / "build-manifest.json").write_text(json.dumps(self.manifest))

    def snapshot(self):
        return {str(path.relative_to(self.aircraft)): path.read_bytes()
                for path in self.aircraft.rglob("*") if path.is_file()}

    def run_package(self):
        return package(self.candidate, self.aircraft, self.textures, self.project, self.backup, self.report)

    def assert_preflight_rejects(self, pattern):
        original = self.snapshot()
        with self.assertRaisesRegex((ValueError, FileNotFoundError), pattern):
            self.run_package()
        self.assertEqual(self.snapshot(), original)
        self.assertFalse(self.backup.exists())
        self.assertFalse(self.report.exists())

    def test_only_two_wing_objects_provider_and_manifest_change(self):
        original = self.snapshot()
        report = self.run_package()
        after = self.snapshot()
        changed = {name for name in original if original[name] != after[name]}
        self.assertEqual(changed, {str(WING_ROOT / name) for name in WINGS} | {"build-manifest.json"})
        self.assertEqual(set(after) - set(original), {str(path) for path in PROVIDER_PATHS})
        self.assertEqual(report["header_token_changes"], 18)
        self.assertTrue(report["acf_unchanged"])
        self.assertFalse(report["global_runtime_installed"])
        self.assertFalse(validate_provider(self.aircraft)[1])
        updated = json.loads((self.aircraft / "build-manifest.json").read_text())
        for key, value in self.manifest.items():
            self.assertEqual(updated[key], value)
        for name in changed:
            self.assertEqual((self.backup / name).read_bytes(), original[name])
        self.assertEqual(json.loads(self.report.read_text())["files"], report["files"])
        self.assertEqual(len(report["files"]), 11)

    def test_user_wing_edit_conflict_is_rejected_before_writes(self):
        target = self.aircraft / WING_ROOT / WINGS[1]
        target.write_bytes(target.read_bytes() + b"# independent edit\n")
        self.assert_preflight_rejects("native baseline")

    def test_missing_provider_rejected_before_writes(self):
        (self.project / FILES[1][0]).unlink()
        self.assert_preflight_rejects("driver/core")

    def test_missing_installed_acf_or_texture_rejected(self):
        (self.aircraft / "A380X-XP12.acf").unlink()
        self.assert_preflight_rejects("regular package file")
        (self.aircraft / "A380X-XP12.acf").write_bytes(b"acf")
        (self.aircraft / "textures/exterior/wings/wing.png").unlink()
        self.assert_preflight_rejects("Installed texture")

    def test_candidate_wrong_slot_or_extra_token_rejected(self):
        target = self.candidate / WINGS[0]
        data = target.read_bytes()
        target.write_bytes(data.replace(b"aileron_l_inboard_ratio", b"aileron_l_midboard_ratio", 1))
        self.assert_preflight_rejects("nine audited tokens")

    def test_candidate_inventory_must_be_exact(self):
        (self.candidate / "a380x_fans.obj").unlink()
        self.assert_preflight_rejects("eleven")

    def test_existing_provider_files_are_backed_up(self):
        for relative in PROVIDER_PATHS:
            path = self.aircraft / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"old provider " + str(relative).encode())
        original = self.snapshot()
        self.run_package()
        for relative in PROVIDER_PATHS:
            self.assertEqual((self.backup / relative).read_bytes(), original[str(relative)])

    def test_nonempty_backup_or_existing_report_refused(self):
        self.backup.mkdir()
        (self.backup / "precious.txt").write_text("preserve")
        original = self.snapshot()
        with self.assertRaisesRegex(ValueError, "new or empty"):
            self.run_package()
        self.assertEqual(self.snapshot(), original)
        (self.backup / "precious.txt").unlink()
        self.report.write_text("existing report")
        with self.assertRaisesRegex(ValueError, "new file"):
            self.run_package()
        self.assertEqual(self.report.read_text(), "existing report")
        self.assertEqual(self.snapshot(), original)

    def test_target_symlink_and_linked_parent_refused(self):
        target = self.aircraft / WING_ROOT / WINGS[1]
        original = target.read_bytes()
        outside = self.root / "outside.obj"
        outside.write_bytes(original)
        target.unlink()
        target.symlink_to(outside)
        self.assert_preflight_rejects("symbolic-link")
        self.assertEqual(outside.read_bytes(), original)

    def test_strict_native_baseline_refuses_reapplication(self):
        self.run_package()
        self.backup, self.report = self.root / "backup2", self.root / "report2.json"
        self.assert_preflight_rejects("native baseline")

    def test_deployment_failure_restores_targets_and_retains_backups(self):
        original = self.snapshot()
        import package_aileron_visuals as module
        actual_copy = module.atomic_copy
        calls = 0
        def fail_once(source, destination):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError("injected copy failure")
            actual_copy(source, destination)
        with mock.patch.object(module, "atomic_copy", side_effect=fail_once):
            with self.assertRaisesRegex(OSError, "injected"):
                self.run_package()
        self.assertEqual(self.snapshot(), original)
        self.assertTrue((self.backup / WING_ROOT / WINGS[0]).is_file())
        self.assertFalse(self.report.exists())


if __name__ == "__main__":
    unittest.main()
