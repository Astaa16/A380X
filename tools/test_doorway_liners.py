"""Geometry guards for fixed cabin walls around operable door portals."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "blender"))
from doorway_liners import build_liner_geometry, validate_specs


class DoorwayLinerTests(unittest.TestCase):
    def setUp(self):
        self.spec = {
            "name": "LINER_U1L", "anchor_object": "FRAME_U1L",
            "profile": [[0, 3], [4, 2]],
            "longitudinal_min": 10, "longitudinal_max": 16,
            "portal_min": 12, "portal_max": 14,
            "portal_bottom": 1, "portal_top": 3, "mirror_x": False,
        }

    def test_portal_remains_clear_and_outer_wall_area_is_complete(self):
        geometry = build_liner_geometry(self.spec)
        self.assertEqual(len(geometry["faces"]), 8)
        area = 0
        for face in geometry["faces"]:
            vertices = [geometry["vertices"][index] for index in face]
            ymin, ymax = min(v[1] for v in vertices), max(v[1] for v in vertices)
            hmin, hmax = min(v[2] for v in vertices), max(v[2] for v in vertices)
            self.assertFalse(-14 < (ymin + ymax) / 2 < -12 and 1 < (hmin + hmax) / 2 < 3)
            area += (ymax - ymin) * (hmax - hmin)
        self.assertEqual(area, 6 * 4 - 2 * 2)
        self.assertIn((2.75, -12.0, 1.0), geometry["vertices"])

    def test_mirror_preserves_aperture_and_inward_winding(self):
        left = build_liner_geometry(self.spec)
        right_spec = dict(self.spec, mirror_x=True)
        right = build_liner_geometry(right_spec)
        self.assertEqual(right["vertices"], [(-x, y, z) for x, y, z in left["vertices"]])
        self.assertEqual(right["faces"], [tuple(reversed(face)) for face in left["faces"]])

    def test_portal_profile_bridges_wall_to_jamb_and_blends_into_roof(self):
        self.spec["profile"] = [[0, 2.5], [4, 2.5]]
        self.spec["portal_profile"] = [[0, 3.5], [2, 3.5], [3.5, 3.5], [4, 3.5]]
        geometry = build_liner_geometry(self.spec)
        self.assertIn((2.5, -10.0, 2.0), geometry["vertices"])
        self.assertIn((3.5, -12.0, 2.0), geometry["vertices"])
        self.assertIn((3.0, -12.0, 3.5), geometry["vertices"])
        self.assertIn((2.5, -12.0, 4.0), geometry["vertices"])

    def test_invalid_profiles_and_apertures_fail(self):
        cases = [
            ({"profile": [[0, 3], [0, 2]]}, "increase strictly"),
            ({"profile": [[0, 0], [4, 2]]}, "X must be positive"),
            ({"profile": [[0, 3], [4, float("nan")]]}, "finite numeric"),
            ({"portal_profile": [[1, 3], [4, 2]]}, "cover the wall profile"),
            ({"portal_min": 10}, "inside longitudinal bounds"),
            ({"portal_top": 5}, "heights must lie within"),
            ({"portal_top": 1}, "heights must lie within"),
            ({"mirror_x": 1}, "must be boolean"),
        ]
        for changes, message in cases:
            with self.subTest(changes=changes):
                spec = copy.deepcopy(self.spec)
                spec.update(changes)
                with self.assertRaisesRegex(ValueError, message):
                    validate_specs([spec])

    def test_each_outer_edge_meets_its_own_cabin_wall(self):
        self.spec["profile"] = [[0, 2.5], [4, 2.5]]
        self.spec["end_profile"] = [[0, 2.8], [4, 2.8]]
        self.spec["portal_profile"] = [[0, 3], [4, 3]]
        vertices = build_liner_geometry(self.spec)["vertices"]
        self.assertIn((2.5, -10.0, 1.0), vertices)
        self.assertIn((2.8, -16.0, 1.0), vertices)
        self.assertIn((3.0, -12.0, 1.0), vertices)
        self.assertIn((3.0, -14.0, 1.0), vertices)


if __name__ == "__main__":
    unittest.main()
