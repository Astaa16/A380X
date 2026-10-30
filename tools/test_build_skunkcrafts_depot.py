#!/usr/bin/env python3
from __future__ import annotations

import tempfile
from pathlib import Path
import unittest
import zlib

import build_skunkcrafts_depot as depot


VERSION = "0.1.0-test.1"
MODULE = "https://example.invalid/a380x/"


class DepotTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.package = Path(self.temporary.name) / "A380X-XP12"
        (self.package / "licenses").mkdir(parents=True)
        for relative in (
            "README.txt",
            "licenses/ATTRIBUTION.md",
            "licenses/GPL-3.0.txt",
            "licenses/mSparks-747-400-CC-BY-NC-SA-4.0.txt",
        ):
            (self.package / relative).write_text(f"fixture {relative}\n", encoding="utf-8")
        (self.package / "changelog.txt").write_text(f"{VERSION}\n- Test build\n", encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def generate(self, check: bool = False) -> list[depot.ManifestRow]:
        return depot.generate(self.package, "A380X Test", MODULE, VERSION, check)

    def test_manifest_crc_size_once_and_exclusions(self) -> None:
        (self.package / "objects").mkdir()
        payload = b"actual OBJ bytes\n"
        (self.package / "objects/aircraft.obj").write_bytes(payload)
        (self.package / "A380X-XP12_prefs.txt").write_text("preferences\n", encoding="utf-8")
        (self.package / ".hidden").write_text("ignore\n", encoding="utf-8")
        (self.package / "build").mkdir()
        (self.package / "build/report.json").write_text("{}\n", encoding="utf-8")
        rows = self.generate()
        by_path = {row.path: row for row in rows}
        self.assertEqual(by_path["objects/aircraft.obj"].crc, zlib.crc32(payload) & 0xFFFFFFFF)
        self.assertEqual(by_path["objects/aircraft.obj"].size, len(payload))
        self.assertEqual(by_path["A380X-XP12_prefs.txt"].crc, -1)
        self.assertNotIn(".hidden", by_path)
        self.assertNotIn("build/report.json", by_path)
        self.assertNotIn("skunkcrafts_updater.cfg", by_path)
        self.assertIn("liveries|false\n", (self.package / "skunkcrafts_updater.cfg").read_text())
        self.generate(check=True)

    def test_check_detects_stale_metadata(self) -> None:
        self.generate()
        (self.package / "skunkcrafts_updater_sizeslist.txt").write_text("stale\n", encoding="utf-8")
        with self.assertRaisesRegex(depot.DepotError, "metadata is stale"):
            self.generate(check=True)

    def test_lfs_pointer_is_rejected(self) -> None:
        (self.package / "texture.png").write_bytes(
            depot.LFS_HEADER + b"oid sha256:" + b"0" * 64 + b"\nsize 123\n"
        )
        with self.assertRaisesRegex(depot.DepotError, "LFS pointer"):
            self.generate()

    def test_symlink_is_rejected(self) -> None:
        target = self.package / "README.txt"
        link = self.package / "linked.txt"
        try:
            link.symlink_to(target)
        except OSError as error:
            self.skipTest(f"symlinks unavailable: {error}")
        with self.assertRaisesRegex(depot.DepotError, "symlinks"):
            self.generate()

    def test_path_validation_and_case_collisions(self) -> None:
        for value in ("../escape", "/absolute", "has|pipe", "back\\slash", " trailing"):
            with self.subTest(value=value), self.assertRaises(depot.DepotError):
                depot.validate_relative(value)
        with self.assertRaisesRegex(depot.DepotError, "collision"):
            depot.validate_unique_paths(["Objects/Wing.obj", "objects/wing.obj"])

    def test_blacklist_is_preserved_and_validated(self) -> None:
        blacklist = self.package / "skunkcrafts_updater_blacklist.txt"
        blacklist.write_text("objects/obsolete.obj\n", encoding="utf-8")
        self.generate()
        self.assertEqual(blacklist.read_text(), "objects/obsolete.obj\n")
        blacklist.write_text("skunkcrafts_updater.cfg\n", encoding="utf-8")
        with self.assertRaisesRegex(depot.DepotError, "control files"):
            self.generate()


if __name__ == "__main__":
    unittest.main()
