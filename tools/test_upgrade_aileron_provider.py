from pathlib import Path
import json
import unittest
from unittest import mock

import test_aileron_visual_provider as fixtures
from aileron_visual_provider import BOOTSTRAP_FILES, DATAREFS, FILES, MANIFEST, digest, validate_provider
from install_aileron_visuals import install
from upgrade_aileron_provider import upgrade


class UpgradeTests(unittest.TestCase):
    setUp = fixtures.AileronProviderTests.setUp
    package_files = fixtures.AileronProviderTests.package_files
    mutate_manifest = fixtures.AileronProviderTests.mutate_manifest

    def old_install(self):
        self.package_files()
        install(self.package, self.xplane)
        # Reconstruct v1 pre-bootstrap installed state.
        for _, packaged in BOOTSTRAP_FILES:
            (self.package / packaged).unlink()
        self.mutate_manifest(lambda doc: doc.pop("bootstrap"))
        self.mutate_manifest(lambda doc: doc.update(datarefs=doc["datarefs"][:len(DATAREFS)],
                                                    files=doc["files"][:2]))
        for _, packaged, installed in FILES[2:]:
            (self.package / packaged).unlink()
            (self.plugin / installed).unlink()
        (self.package / "A380X-XP12.acf").write_text("acf preserved")
        (self.package / "build-manifest.json").write_text('{"user_field": 42}')
        (self.project / FILES[0][0]).write_text("-- new timing and early binding")
        self.backup = self.base / "backup"

    def do_upgrade(self):
        return upgrade(self.project, self.package, self.xplane, self.backup)

    def test_upgrade_preserves_acf_and_manifest_fields(self):
        self.old_install()
        result = self.do_upgrade()
        self.assertTrue(result["files_match_package"])
        self.assertTrue(result["acf_unchanged"])
        self.assertFalse(validate_provider(self.package)[1])
        self.assertEqual(json.loads((self.package / "build-manifest.json").read_text())["user_field"], 42)
        self.assertTrue((self.backup / "aircraft" / FILES[0][1]).is_file())

    def test_independent_script_change_is_not_overwritten(self):
        self.old_install()
        (self.plugin / FILES[0][2]).write_text("user edit")
        with self.assertRaisesRegex(ValueError, "independently changed"):
            self.do_upgrade()
        self.assertFalse(self.backup.exists())

    def test_new_hydraulic_file_conflict_is_preserved(self):
        self.old_install()
        target = self.plugin / FILES[2][2]
        target.write_text("-- independently installed hydraulic module")
        with self.assertRaisesRegex(ValueError, "independently changed"):
            self.do_upgrade()
        self.assertFalse(self.backup.exists())
        self.assertEqual(target.read_text(), "-- independently installed hydraulic module")

    def test_copy_failure_restores_old_runtime(self):
        self.old_install()
        original = (self.plugin / FILES[0][2]).read_bytes()
        import upgrade_aileron_provider as module
        actual = module.atomic_copy
        count = 0
        def fail_once(source, target):
            nonlocal count
            count += 1
            if count == 2:
                raise OSError("injected failure")
            actual(source, target)
        with mock.patch.object(module, "atomic_copy", side_effect=fail_once):
            with self.assertRaisesRegex(OSError, "injected"):
                self.do_upgrade()
        self.assertEqual((self.plugin / FILES[0][2]).read_bytes(), original)
        self.assertNotIn("bootstrap", json.loads((self.package / MANIFEST).read_text()))
        self.assertFalse((self.package / BOOTSTRAP_FILES[0][1]).exists())
        for _, packaged, installed in FILES[2:]:
            self.assertFalse((self.package / packaged).exists())
            self.assertFalse((self.plugin / installed).exists())

    def test_known_prior_global_version_can_migrate_without_loosening_guard(self):
        self.old_install()
        rows = [{"path": str(self.plugin / installed), "sha256": digest(self.plugin / installed)}
                for _, _, installed in FILES[:2]]
        report = self.base / "previous-runtime.json"
        report.write_text(json.dumps({"provider": "a380x-aileron-dance-v1",
                                      "files_match_package": True, "files": rows}))
        packaged = self.package / FILES[0][1]
        packaged.write_text("-- newer packaged copy, older active global copy")
        self.mutate_manifest(lambda doc: doc["files"][0].update(sha256=digest(packaged)))
        result = upgrade(self.project, self.package, self.xplane, self.backup, report)
        self.assertTrue(result["files_match_package"])

    def test_previous_report_cannot_authorize_unrecorded_edit(self):
        self.old_install()
        rows = [{"path": str(self.plugin / installed), "sha256": digest(self.plugin / installed)}
                for _, _, installed in FILES[:2]]
        report = self.base / "previous-runtime.json"
        report.write_text(json.dumps({"provider": "a380x-aileron-dance-v1",
                                      "files_match_package": True, "files": rows}))
        (self.plugin / FILES[0][2]).write_text("-- independent edit after recorded install")
        with self.assertRaisesRegex(ValueError, "independently changed"):
            upgrade(self.project, self.package, self.xplane, self.backup, report)
        self.assertFalse(self.backup.exists())
