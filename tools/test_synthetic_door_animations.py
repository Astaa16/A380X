"""Pure-Python guards for the generated upper-deck door motions."""
import json
from pathlib import Path
import sys
import unittest


PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "blender"))
from synthetic_rigid_animations import motion_samples, resolve_animation_targets, validate_specs


class SyntheticDoorAnimationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        document = json.loads((PROJECT / "config" / "animation_datarefs.json").read_text())
        cls.specs = validate_specs(document["synthetic_rigid_animations"])

    def test_six_unique_upper_deck_door_specs(self):
        self.assertEqual(
            {spec["name"] for spec in self.specs},
            {f"ANIM_DOOR_U{station}{side}" for station in range(1, 4) for side in "LR"},
        )
        targets = [target for spec in self.specs for target in spec["objects"]]
        self.assertEqual(len(targets), len(set(targets)))

    def test_each_motion_plugs_out_then_slides_forward(self):
        for spec in self.specs:
            with self.subTest(name=spec["name"]):
                samples = motion_samples(spec)
                self.assertEqual([sample["ratio"] for sample in samples], [0, 0.2, 1])
                self.assertEqual(samples[0]["outward"], 0)
                self.assertEqual(samples[1]["outward"], 0.55)
                self.assertEqual(samples[1]["longitudinal"], 0)
                self.assertEqual(samples[2]["longitudinal"], 1.3)
                self.assertEqual(samples[1]["angle"], 0)
                self.assertEqual(samples[2]["angle"], 0)
                self.assertTrue(spec["double_sided"])

    def test_left_and_right_doors_keep_matching_parallel_paths(self):
        by_name = {spec["name"]: spec for spec in self.specs}
        for station in range(1, 4):
            left = motion_samples(by_name[f"ANIM_DOOR_U{station}L"])[-1]
            right = motion_samples(by_name[f"ANIM_DOOR_U{station}R"])[-1]
            self.assertEqual(left, right)

    def test_interior_only_subset_uses_cloned_meshes_with_existing_motion(self):
        spec = self.specs[0]
        clones = ["INNER_U1L_PANEL", "INNER_U1L_HANDLE"]
        resolved = resolve_animation_targets(
            self.specs, set(clones), {spec["name"]: clones},
        )
        self.assertEqual(len(resolved), 1)
        self.assertIs(resolved[0]["spec"], spec)
        self.assertEqual(resolved[0]["objects"], clones)
        self.assertEqual(resolved[0]["primary_object"], clones[0])

    def test_combined_subset_keeps_original_primary_and_adds_clones(self):
        spec = self.specs[0]
        targets = spec["objects"] + ["INNER_U1L_PANEL"]
        resolved = resolve_animation_targets(
            self.specs, set(targets), {spec["name"]: [targets[-1]]},
        )
        self.assertEqual(resolved[0]["objects"], targets)
        self.assertEqual(resolved[0]["primary_object"], spec["primary_object"])

    def test_clones_do_not_mask_a_partial_original_group(self):
        spec = self.specs[0]
        with self.assertRaisesRegex(ValueError, "incomplete source group"):
            resolve_animation_targets(
                self.specs, {spec["primary_object"], "INNER_U1L_PANEL"},
                {spec["name"]: ["INNER_U1L_PANEL"]},
            )

    def test_bad_cloned_assignments_fail_before_export(self):
        name = self.specs[0]["name"]
        other_name = self.specs[1]["name"]
        cases = [
            ({"UNKNOWN_DOOR": ["PANEL"]}, "Unknown cloned animation"),
            ({name: ["MISSING"]}, "missing objects"),
            ({name: [""]}, "needs named objects"),
            ({name: ["PANEL", "PANEL"]}, "repeats an assigned object"),
            ({name: ["PANEL"], other_name: ["PANEL"]}, "repeats an assigned object"),
            ({name: [self.specs[0]["primary_object"]]}, "reuses original objects"),
        ]
        for groups, message in cases:
            with self.subTest(groups=groups):
                with self.assertRaisesRegex(ValueError, message):
                    resolve_animation_targets(
                        self.specs, {"PANEL", self.specs[0]["primary_object"]}, groups,
                    )

    def test_legacy_subset_without_clones_still_skips_absent_groups(self):
        spec = self.specs[0]
        resolved = resolve_animation_targets(self.specs, set(spec["objects"]))
        self.assertEqual([group["objects"] for group in resolved], [spec["objects"]])
        self.assertEqual(resolve_animation_targets(self.specs, set()), [])


if __name__ == "__main__":
    unittest.main()
