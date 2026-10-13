"""Regression coverage for the sixteen hydraulic-driven visual spoiler clips."""
import json
from pathlib import Path
import re
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / 'blender'))
from animation_sampling import sampling_plan
from obj8_preview import interpolate


class SpoilerMappingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = json.loads((PROJECT / 'config' / 'animation_datarefs.json').read_text())
        cls.rules = cls.document['mappings']
        cls.names = [f'{side}_spoiler{panel}_key' for side in ('l', 'r') for panel in range(1, 9)]

    def matching_rules(self, name):
        return [rule for rule in self.rules if re.fullmatch(rule['pattern'], name)]

    def rule_for(self, name):
        matches = self.matching_rules(name)
        self.assertEqual(len(matches), 1, f'{name}: expected one disjoint rule, got {len(matches)}')
        return matches[0]

    def test_all_sixteen_clips_are_covered_exactly_once(self):
        self.assertEqual(len(set(self.names)), 16)
        patterns = set()
        for name in self.names:
            with self.subTest(clip=name):
                rule = self.rule_for(name)
                self.assertTrue(rule.get('dataref'))
                self.assertIsNone(rule.get('static_fraction'))
                patterns.add(rule['pattern'])
        self.assertEqual(len(patterns), 8)

    def test_hydraulic_paths_and_left_right_panels_are_exact(self):
        for side, side_offset in (('l', 0), ('r', 1)):
            for panel in range(1, 9):
                name = f'{side}_spoiler{panel}_key'
                if panel <= 2:
                    suffix = f'spoiler_{side}_inboard_deg'
                elif panel == 3:
                    suffix = f'spoiler_{side}_inner_deg'
                elif panel <= 6:
                    suffix = f'spoiler_{side}_middle_deg'
                else:
                    suffix = f'spoiler_{side}_outer_deg'
                with self.subTest(clip=name):
                    self.assertEqual(self.rule_for(name)['dataref'], 'a380x/xp12/hydraulics/' + suffix)

    def test_source_caps_are_35_inboard_and_50_outboard_degrees(self):
        for name in self.names:
            panel = int(re.search(r'spoiler([1-8])', name).group(1))
            cap = 35. if panel <= 2 else 50.
            with self.subTest(clip=name):
                rule = self.rule_for(name)
                self.assertEqual(rule['value_min'], 0)
                self.assertEqual(rule['value_max'], 50)
                self.assertEqual(rule.get('source_start', 0), 0)
                self.assertAlmostEqual(rule['source_end'] * 65., cap, places=12)

    def test_clamping_is_explicit_and_legacy_ratio_is_not_used(self):
        for name in self.names:
            with self.subTest(clip=name):
                rule = self.rule_for(name)
                self.assertIs(rule.get('clamp'), True)
                self.assertNotIn('/controls/', rule['dataref'])
                self.assertNotIn('speedbrake_ratio', rule['dataref'])
                self.assertTrue(rule['dataref'].startswith('a380x/xp12/hydraulics/spoiler_'))
                self.assertNotEqual(rule['value_max'], 1.3)

    def test_complete_clip_names_only_and_case_insensitivity(self):
        spoiler_rules = {self.rule_for(name)['pattern'] for name in self.names}
        for name in self.names:
            self.assertEqual(self.rule_for(name.upper())['dataref'], self.rule_for(name)['dataref'])
        for name in ('l_spoiler0_key', 'r_spoiler9_key', 'l_spoiler10_key',
                     'prefix_l_spoiler1_key', 'l_spoiler1_key_suffix',
                     'WING_SPOILER1_1.L', 'lever_speed_brake'):
            with self.subTest(unrelated=name):
                self.assertFalse(any(re.fullmatch(pattern, name) for pattern in spoiler_rules))

    def test_sampler_and_obj_extrapolation_preserve_caps_at_all_inputs(self):
        # A source clip of 0..200 frames represents 0..65 authored degrees.
        # Evaluate its sampled keys using OBJ8's extrapolating interpolation,
        # including values far outside the guard keys; those tails must be flat.
        for name in self.names:
            rule = self.rule_for(name)
            cap = rule['source_end'] * 65.
            source_frames, native_values = sampling_plan(range(201), rule)
            self.assertEqual(source_frames[:2], [0., 0.])
            self.assertEqual(source_frames[-2], source_frames[-1])
            self.assertEqual(native_values[:2], [-50., 0.])
            self.assertEqual(native_values[-2:], [50., 100.])
            self.assertTrue(all(a < b for a, b in zip(native_values, native_values[1:])))
            keys = [(value, (frame / 200. * 65.,))
                    for frame, value in zip(source_frames, native_values)]
            for value in (-1_000_000., -100., -1., 0., 12.5, 25., 49.9, 50., 75., 1_000_000.):
                with self.subTest(clip=name, native_degrees=value):
                    actual = interpolate(keys, value)[0]
                    expected = min(max(value, 0.), 50.) / 50. * cap
                    self.assertAlmostEqual(actual, expected, places=10)


if __name__ == '__main__':
    unittest.main()
