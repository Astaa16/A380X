from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "blender"))
from animation_sampling import sampling_plan


class AnimationSamplingTests(unittest.TestCase):
    def test_uncropped_nonuniform_frames(self):
        self.assertEqual(sampling_plan([1, 3, 11], {"value_min": -1, "value_max": 1}),
                         ([1., 3., 11.], [-1., -.6, 1.]))

    def test_clamped_cropped_spoiler(self):
        samples, values = sampling_plan(range(1, 42), {
            "value_min": 0, "value_max": 50, "source_end": 50 / 65, "clamp": True})
        self.assertEqual(samples[:2], [1., 1.])
        self.assertAlmostEqual(samples[-1], 1 + 40 * 50 / 65)
        self.assertEqual(samples[-2], samples[-1])
        self.assertEqual(values[:2], [-50, 0])
        self.assertEqual(values[-2:], [50, 100])
        self.assertTrue(all(a < b for a, b in zip(values, values[1:])))

    def test_reversed_interval_has_flat_endpoint_guards(self):
        self.assertEqual(sampling_plan([0, 1, 2], {"value_min": 1, "value_max": -1, "clamp": True}),
                         ([0., 0., 1., 2., 2.], [3., 1., 0., -1., -3.]))

    def test_crop_interpolates_endpoints_without_rounding(self):
        self.assertEqual(sampling_plan([0, 1, 2], {
            "value_min": 0, "value_max": 1, "source_start": .25, "source_end": .75}),
                         ([.5, 1., 1.5], [0., .5, 1.]))

    def test_single_frame_is_constant(self):
        self.assertEqual(sampling_plan([4], {"value_min": 0, "value_max": 1}),
                         ([4., 4.], [0., 1.]))

    def test_explicit_points_can_revisit_source_poses(self):
        self.assertEqual(sampling_plan([10, 60, 110], {
            "value_min": 0,
            "value_max": 1,
            "sample_points": [
                {"source_fraction": 0, "dataref_value": 0},
                {"source_fraction": 1, "dataref_value": .1},
                {"source_fraction": 1, "dataref_value": .9},
                {"source_fraction": 0, "dataref_value": 1},
            ],
        }), ([10., 110., 110., 10.], [0., .1, .9, 1.]))

    def test_explicit_points_reject_ambiguous_dataref_order(self):
        for values in ((0, 0), (0, 1, .5), (1, 0, .5)):
            with self.subTest(values=values), self.assertRaises(ValueError):
                sampling_plan([0, 1], {
                    "sample_points": [
                        {"source_fraction": index / max(len(values) - 1, 1),
                         "dataref_value": value}
                        for index, value in enumerate(values)
                    ]
                })

    def test_invalid_inputs_fail(self):
        for frames, extra in [([], {}), ([float("nan")], {}), ([0, 1], {"value_max": 0}),
                              ([0, 1], {"source_start": 1}), ([0, 1], {"source_end": 2})]:
            with self.subTest(frames=frames, extra=extra), self.assertRaises(ValueError):
                sampling_plan(frames, {"value_min": 0, "value_max": 1, **extra})


if __name__ == "__main__":
    unittest.main()
