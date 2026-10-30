"""Regression coverage for passenger and cockpit door mappings."""

import json
from pathlib import Path
import re
import unittest


PROJECT = Path(__file__).resolve().parents[1]


class PassengerDoorMappingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        document = json.loads(
            (PROJECT / "config" / "animation_datarefs.json").read_text(encoding="utf-8")
        )
        cls.rules = document["mappings"]

    def matching_rules(self, name):
        return [rule for rule in self.rules if re.fullmatch(rule["pattern"], name)]

    def test_main_deck_clips_have_one_native_door_channel_each(self):
        for station in range(1, 6):
            for side, index in (("L", 2 * station - 2), ("R", 2 * station - 1)):
                clip = f"ANIM_DOOR_M{station}{side}"
                with self.subTest(clip=clip):
                    matches = self.matching_rules(clip)
                    self.assertEqual(len(matches), 1)
                    self.assertEqual(
                        matches[0]["dataref"],
                        f"sim/flightmodel2/misc/door_open_ratio[{index}]",
                    )
                    self.assertEqual(matches[0]["value_min"], 0)
                    self.assertEqual(matches[0]["value_max"], 1)

    def test_upper_deck_clips_have_independent_native_channels(self):
        for station in range(1, 4):
            for side, index in (("L", 2 * station + 8), ("R", 2 * station + 9)):
                clip = f"ANIM_DOOR_U{station}{side}"
                with self.subTest(clip=clip):
                    matches = self.matching_rules(clip)
                    self.assertEqual(len(matches), 1)
                    self.assertEqual(
                        matches[0]["dataref"],
                        f"sim/flightmodel2/misc/door_open_ratio[{index}]",
                    )
                    self.assertEqual(matches[0]["value_min"], 0)
                    self.assertEqual(matches[0]["value_max"], 1)

    def test_cockpit_door_uses_native_channel_seventeen(self):
        matches = self.matching_rules("COCKPITDOOR_OPEN_ANIM")
        self.assertEqual(len(matches), 1)
        self.assertEqual(
            matches[0]["dataref"],
            "sim/flightmodel2/misc/door_open_ratio[16]",
        )

    def test_legacy_meshless_cargo_animation_remains_at_rest(self):
        for clip in ("door_cargo",):
            with self.subTest(clip=clip):
                matches = self.matching_rules(clip)
                self.assertEqual(len(matches), 1)
                self.assertEqual(matches[0]["static_fraction"], 0)

    def test_armed_handle_clips_do_not_drive_door_travel(self):
        for station in range(1, 6):
            for side in "LR":
                self.assertEqual(
                    self.matching_rules(f"ANIM_DOOR_M{station}{side}_ARMED"),
                    [],
                )


if __name__ == "__main__":
    unittest.main()
