import re
import tempfile
import unittest
from pathlib import Path

from cockpit_draw_order import (
    CURRENT_COCKPIT_DECALS,
    cockpit_decal_order_errors,
    is_cockpit_panel_decal,
    order_cockpit_attachments,
    reorder_acf_attachments,
)
from build_development_aircraft import (
    MFD_SCREENS_ATTACHMENT, attach_mfd_screens, build_replacements, existing_attachment_paths,
    merge_refreshed_paths, transform_acf,
)


PANEL = "interior/cockpit-panels/panel_a380_cockpit_ovhd01_4k.obj"
DECAL = "interior/cockpit-panels/decal_a380x_cockpit_decal_emis.obj"
CONTROLS = "interior/cockpit-controls/a380x_cockpit_controls.obj"
DARK_FACES = "interior/cockpit-panels/screen_powered_off_display_faces.obj"
MFD = "interior/cockpit-panels/a380x_mfd_screens.obj"


def block(index, path, newline=b"\r\n"):
    fields = [
        f"_v10_att_file_stl {path}",
        f"_obj_flags {2061 if 'controls' in path else 13}",
        f"_v10_att_x_acf_prt_ref {index}.123456789",
        f"_v10_att_y_acf_prt_ref -{index}.765432100",
        "_v10_att_z_acf_prt_ref 115.108000000",
        "_v10_att_psi_ref 180.000000000",
        "_v10_is_internal 1",
    ]
    if index == 1:
        fields.append("_future_field/0 retain every unknown field")
    return b"".join(f"P _obja/{index}/{field}".encode("latin1") + newline for field in fields)


class CockpitDrawOrderTests(unittest.TestCase):
    def test_classifies_all_six_current_decals_only_in_panel_group(self):
        self.assertEqual(len(CURRENT_COCKPIT_DECALS), 6)
        for filename in CURRENT_COCKPIT_DECALS:
            self.assertTrue(is_cockpit_panel_decal("interior/cockpit-panels/" + filename))
            self.assertTrue(is_cockpit_panel_decal("objects/interior/cockpit-panels/" + filename))
            self.assertFalse(is_cockpit_panel_decal("interior/cabin/" + filename))
        for path in (PANEL, CONTROLS, "exterior/cockpit-panels/decal_other.obj",
                     "interior/cockpit-panels/decal_nested/part.obj"):
            self.assertFalse(is_cockpit_panel_decal(path))

    def test_stable_partition_preserves_duplicates_and_does_not_mutate_input(self):
        other = "interior/cockpit-panels/decal_new_decal_noemis.obj"
        original = [DECAL, PANEL, CONTROLS, other, PANEL, DECAL]
        before = list(original)
        ordered = order_cockpit_attachments(original)
        self.assertEqual(ordered, [PANEL, CONTROLS, PANEL, DECAL, other, DECAL])
        self.assertEqual(original, before)
        self.assertEqual(order_cockpit_attachments(ordered), ordered)

    def test_order_validator_excludes_native_controls_and_unrelated_groups(self):
        self.assertEqual(cockpit_decal_order_errors([PANEL, DECAL, CONTROLS]), [])
        self.assertEqual(cockpit_decal_order_errors([DECAL, "interior/cabin/cabin.obj"]), [])
        errors = cockpit_decal_order_errors([DECAL, CONTROLS, PANEL])
        self.assertEqual(len(errors), 1)
        self.assertIn(DECAL, errors[0])
        self.assertIn(PANEL, errors[0])

    def test_acf_preserves_complete_blocks_and_all_nonattachment_bytes(self):
        prefix = b"I\r\n1200\r\nACF\r\n# latin1 caf\xe9\n"
        separators = [b"# leave separator 0 here\r\n", b"P unrelated/value 99\n"]
        suffix = b"P _obja/count 3\r\nP other/end 7\n# no final newline"
        source = (prefix + block(0, DECAL) + separators[0] + block(1, PANEL)
                  + separators[1] + block(2, CONTROLS) + suffix)
        result = reorder_acf_attachments(source)
        def renumber(data, index):
            return re.sub(rb"(?m)^(P _obja/)\d+/", lambda m: m[1] + str(index).encode() + b"/", data)
        expected = (prefix + renumber(block(1, PANEL), 0) + separators[0]
                    + renumber(block(2, CONTROLS), 1) + separators[1]
                    + renumber(block(0, DECAL), 2) + suffix)
        self.assertEqual(result, expected)
        self.assertEqual(reorder_acf_attachments(result), result)

    def test_noop_retains_exact_bytes_and_zero_count(self):
        source = block(0, PANEL, b"\n") + block(1, DECAL) + b"P _obja/count 2\n"
        self.assertIs(reorder_acf_attachments(source), source)
        empty = b"I\n1200\nACF\nP _obja/count 0\n"
        self.assertEqual(reorder_acf_attachments(empty), empty)

    def test_malformed_attachment_records_are_rejected(self):
        cases = [
            block(0, DECAL) + b"P _obja/count 2\n",
            block(1, DECAL) + b"P _obja/count 1\n",
            block(0, DECAL) + b"P _obja/0/_obj_flags 99\nP _obja/count 1\n",
            block(0, DECAL) + b"# split block\nP _obja/0/_new_field 1\nP _obja/count 1\n",
            b"P _obja/0/_obj_flags 13\nP _obja/count 1\n",
            b"P _obja/0/_v10_att_file_stl \nP _obja/count 1\n",
            block(0, DECAL) + b"P _obja/count 1\nP _obja/count 1\n",
        ]
        for source in cases:
            with self.subTest(source=source):
                with self.assertRaises(ValueError):
                    reorder_acf_attachments(source)

    def test_full_and_focused_builds_attach_surfaces_and_controls_before_decals(self):
        # Exercise the shared final serialization used by both build modes.
        with tempfile.TemporaryDirectory() as directory:
            template = Path(directory) / "template.acf"
            template.write_text("".join(f"P {key} 0\n" for key in build_replacements())
                                + "P _obja/count 0\n", encoding="latin1")
            full = [DECAL, PANEL, "interior/cabin/cabin.obj"]
            focused = merge_refreshed_paths(
                [DECAL, "interior/cockpit-panels/old_panel.obj", "interior/cabin/cabin.obj"],
                {"cockpit-panels": [DECAL, PANEL]}, "interior")
            for paths in (full, focused):
                with self.subTest(paths=paths):
                    lines, report = transform_acf(template, ["exterior/fuselage.obj"], paths,
                                                  CONTROLS, legacy_gear=False)
                    attached = re.findall(r"^P _obja/\d+/_v10_att_file_stl (.+)$",
                                          "".join(lines), re.M)
                    self.assertEqual(attached, ["exterior/fuselage.obj", PANEL,
                                                "interior/cabin/cabin.obj", CONTROLS, DECAL])
                    self.assertEqual(report["visible_objects"], ["objects/" + path for path in attached])
                    self.assertEqual(cockpit_decal_order_errors(attached), [])


    def test_mfd_screens_attach_directly_after_dark_display_faces_before_controls_and_decals(self):
        self.assertEqual(MFD_SCREENS_ATTACHMENT, MFD)
        self.assertFalse(is_cockpit_panel_decal(MFD))
        self.assertEqual(attach_mfd_screens([DECAL, DARK_FACES, PANEL]), [DECAL, DARK_FACES, MFD, PANEL])
        # Idempotent on refresh: an existing attachment moves, never duplicates.
        self.assertEqual(attach_mfd_screens([MFD, DECAL, DARK_FACES, PANEL]), [DECAL, DARK_FACES, MFD, PANEL])
        self.assertEqual(attach_mfd_screens([PANEL]), [PANEL, MFD])
        with tempfile.TemporaryDirectory() as directory:
            template = Path(directory) / "template.acf"
            template.write_text("".join(f"P {key} 0\n" for key in build_replacements())
                                + "P _obja/count 0\n", encoding="latin1")
            full = attach_mfd_screens([DECAL, PANEL, DARK_FACES, "interior/cabin/cabin.obj"])
            # A cockpit-controls refresh keeps the installed interior list,
            # which already contains the MFD screens from an earlier refresh.
            installed = [DECAL, PANEL, DARK_FACES, MFD, "interior/cabin/cabin.obj"]
            focused = attach_mfd_screens(merge_refreshed_paths(installed, {}, "interior"))
            # A cockpit-panels refresh replaces the group, then re-attaches.
            panels = attach_mfd_screens(merge_refreshed_paths(
                installed, {"cockpit-panels": [DECAL, PANEL, DARK_FACES]}, "interior"))
            for paths in (full, focused, panels):
                with self.subTest(paths=paths):
                    lines, report = transform_acf(template, ["exterior/fuselage.obj"], paths,
                                                  CONTROLS, legacy_gear=False)
                    text = "".join(lines)
                    attached = re.findall(r"^P _obja/\d+/_v10_att_file_stl (.+)$", text, re.M)
                    self.assertEqual(attached, ["exterior/fuselage.obj", PANEL, DARK_FACES, MFD,
                                                "interior/cabin/cabin.obj", CONTROLS, DECAL])
                    self.assertEqual(cockpit_decal_order_errors(attached), [])
                    self.assertEqual(report["visible_objects"], ["objects/" + path for path in attached])
                    index = attached.index(MFD)
                    # Same internal flags and transform as the dark faces (spec 08 section 10.3).
                    self.assertIn(f"P _obja/{index}/_obj_flags 13\n", text)
                    self.assertIn(f"P _obja/{index}/_v10_is_internal 1\n", text)
                    self.assertRegex(text, rf"P _obja/{index}/_v10_att_psi_ref 180(\.0+)?\n")
                    self.assertRegex(text, rf"P _obja/{index}/_v10_att_z_acf_prt_ref 115\.108(0+)?\n")
                    acf = Path(directory) / "package.acf"
                    acf.write_text(text, encoding="latin1")
                    _exterior, interior, controls = existing_attachment_paths(acf)
                    self.assertEqual(controls, CONTROLS)
                    self.assertEqual(interior.count(MFD), 1)


if __name__ == "__main__":
    unittest.main()
