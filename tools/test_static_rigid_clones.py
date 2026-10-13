"""Regression coverage for configured static doorway surround clones."""
from __future__ import annotations

import json
import copy
import sys
import unittest
from pathlib import Path


PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "blender"))
from static_rigid_clones import validate_specs


class StaticRigidCloneTests(unittest.TestCase):
    def test_shipped_upper_door_surrounds_cover_all_six_openings(self):
        document = json.loads(
            (PROJECT / "config" / "animation_datarefs.json").read_text(encoding="utf-8")
        )
        specs = validate_specs(document["static_rigid_clones"])
        surround = next(spec for spec in specs if spec["name"] == "UPPER_DOOR_SURROUND")
        self.assertEqual(
            surround["objects"],
            [
                "PAX_DOOR_U1L.001",
                "PAX_DOOR_U1L.003",
            ],
        )
        self.assertEqual(
            [instance["name"] for instance in surround["instances"]],
            ["U1L", "U1R", "U2L", "U2R", "U3L", "U3R"],
        )
        self.assertTrue(surround["double_sided"])

    def test_static_frame_and_moving_panels_can_reuse_station_names(self):
        frame = {
            "name": "FRAME",
            "objects": ["SOURCE_FRAME"],
            "instances": [{"name": "U1L", "translation": [0, 0, 0], "mirror_x": False}],
        }
        panel = copy.deepcopy(frame)
        panel.update(name="PANEL", objects=["SOURCE_PANEL"])
        panel["instances"][0]["animation"] = "ANIM_DOOR_U1L"
        self.assertEqual(validate_specs([frame, panel]), [frame, panel])
        panel["instances"].append(copy.deepcopy(panel["instances"][0]))
        with self.assertRaisesRegex(ValueError, "instance names must be non-empty and unique"):
            validate_specs([frame, panel])

    def test_invalid_animation_assignment_is_rejected(self):
        for animation in ("", None, 12):
            with self.subTest(animation=animation):
                with self.assertRaisesRegex(ValueError, "animation must be a non-empty name"):
                    validate_specs([{
                        "name": "PANEL", "objects": ["SOURCE_PANEL"],
                        "instances": [{
                            "name": "U1L", "translation": [0, 0, 0], "mirror_x": False,
                            "animation": animation,
                        }],
                    }])

    def test_invalid_translation_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "finite XYZ translation"):
            validate_specs(
                [
                    {
                        "name": "SURROUND",
                        "objects": ["FRAME"],
                        "instances": [
                            {"name": "U1L", "translation": [0, 0], "mirror_x": False}
                        ],
                    }
                ]
            )


if __name__ == "__main__":
    unittest.main()
