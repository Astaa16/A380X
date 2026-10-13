"""Regression coverage for the fuselage landing-gear door animations."""
import json
from pathlib import Path
import re
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "blender"))
from animation_sampling import sampling_plan
from obj8_preview import interpolate


class GearDoorMappingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        document = json.loads((PROJECT / "config" / "animation_datarefs.json").read_text())
        cls.rules = document["mappings"]

    def matching_rules(self, name):
        return [rule for rule in self.rules if re.fullmatch(rule["pattern"], name)]

    def rule_for(self, name):
        matches = self.matching_rules(name)
        self.assertEqual(len(matches), 1, f"{name}: expected one exact rule, got {len(matches)}")
        return matches[0]

    def test_all_door_clips_use_their_nearest_native_gear(self):
        expected = {
            "c_gear_door1": 0,
            "l_wing_gear_door": 1,
            "r_wing_gear_door": 2,
            "l_body_gear_door": 3,
            "r_body_gear_door": 4,
            "l_blg_inner_door": 3,
            "r_blg_inner_door": 4,
        }
        for clip, index in expected.items():
            with self.subTest(clip=clip):
                self.assertEqual(
                    self.rule_for(clip)["dataref"],
                    f"sim/flightmodel2/gear/deploy_ratio[{index}]",
                )

    def test_primary_doors_close_at_endpoints_and_open_during_transit(self):
        primary = (
            "c_gear_door1",
            "l_wing_gear_door",
            "r_wing_gear_door",
            "l_body_gear_door",
            "r_body_gear_door",
        )
        for clip in primary:
            with self.subTest(clip=clip):
                samples, values = sampling_plan(range(101), self.rule_for(clip))
                self.assertEqual(samples, [0., 100., 100., 0.])
                self.assertEqual(values, [0., .12, .88, 1.])
                keys = list(zip(values, ((sample / 100.,) for sample in samples)))
                for deployment, expected_fraction in (
                    (0, 0), (.06, .5), (.12, 1), (.5, 1),
                    (.88, 1), (.94, .5), (1, 0),
                ):
                    self.assertAlmostEqual(interpolate(keys, deployment)[0], expected_fraction)

    def test_auxiliary_body_doors_stay_open_with_gear_down(self):
        for clip in ("l_blg_inner_door", "r_blg_inner_door"):
            with self.subTest(clip=clip):
                rule = self.rule_for(clip)
                self.assertNotIn("sample_points", rule)
                samples, values = sampling_plan((0, 50, 100), rule)
                self.assertEqual(samples, [0., 50., 100.])
                self.assertEqual(values, [0., .5, 1.])

    def test_gear_and_door_clip_names_do_not_overlap(self):
        self.assertEqual(self.rule_for("c_gear")["dataref"],
                         "sim/flightmodel2/gear/deploy_ratio[0]")
        for unrelated in (
            "c_gear_door2",
            "prefix_c_gear_door1",
            "l_wing_gear_door_extra",
            "B_BLG_DOOR1.L",
            "NLG_DOOR_FWD.L",
        ):
            with self.subTest(unrelated=unrelated):
                self.assertFalse(any(re.fullmatch(rule["pattern"], unrelated)
                                     for rule in self.rules if "gear_door" in rule["pattern"]
                                     or "blg_inner_door" in rule["pattern"]))


if __name__ == "__main__":
    unittest.main()
