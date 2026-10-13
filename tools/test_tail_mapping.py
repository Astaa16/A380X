"""Regression coverage for the horizontal-tail visual animations."""
import json
from pathlib import Path
import re
import sys
import unittest

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / 'blender'))
from animation_sampling import sampling_plan
from obj8_preview import interpolate


class TailMappingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.document = json.loads((PROJECT / 'config' / 'animation_datarefs.json').read_text())
        cls.rules = cls.document['mappings']
        cls.reference = json.loads((PROJECT / 'config' / 'aircraft_reference.json').read_text())

    def rule_for(self, name):
        matches = [rule for rule in self.rules if re.fullmatch(rule['pattern'], name)]
        self.assertEqual(len(matches), 1, f'{name}: expected one disjoint rule, got {len(matches)}')
        return matches[0]

    def test_each_elevator_panel_follows_hydraulic_output(self):
        clips = (
            'l_outer_elevator_deflection',
            'l_inner_elevator_deflection',
            'r_outer_elevator_deflection',
            'r_inner_elevator_deflection',
        )
        for clip in clips:
            with self.subTest(clip=clip):
                rule = self.rule_for(clip)
                self.assertEqual(rule['dataref'], 'a380x/xp12/hydraulics/elevator_ratio')
                self.assertEqual((rule['value_min'], rule['value_max']), (-1, 1))

    def test_stabilizer_uses_source_calibrated_leading_edge_positive_degrees(self):
        rule = self.rule_for('trimtab_elevator_key')
        self.assertEqual(rule['dataref'], 'sim/flightmodel2/controls/stabilizer_deflection_degrees')
        self.assertNotIn('value_min', rule)
        self.assertNotIn('value_max', rule)

        limits = self.reference['control_limits_deg']
        self.assertEqual(limits['stabilizer_nose_up'], 10)
        self.assertEqual(limits['stabilizer_nose_down'], 2)

        # Measured pinned-source world-X rotations at the two ends and neutral.
        # Native incidence has the opposite sign: positive raises the leading
        # edge. Aircraft nose-up trim limits are NOT the incidence sign convention.
        source_frames, native_degrees = sampling_plan(range(21), rule)
        self.assertEqual(len(source_frames), 21)
        self.assertTrue(all(a < b for a, b in zip(native_degrees, native_degrees[1:])))
        self.assertAlmostEqual(native_degrees[0], -10.2182739077)
        self.assertAlmostEqual(native_degrees[10], 0)
        self.assertAlmostEqual(native_degrees[-1], 7.5138181433)
        keys = sorted((value, (frame / 20.,)) for frame, value in zip(source_frames, native_degrees))
        self.assertAlmostEqual(interpolate(keys, 0)[0], .5)
        self.assertLess(interpolate(keys, -10)[0], .02)
        self.assertGreater(interpolate(keys, 2)[0], .6)
        # At every authored pose native incidence must equal actual angular
        # displacement, rather than a guessed linear +/-10-degree scale.
        measured_world_x = (10.2182739077, 5.1091335709, 0, -3.7569041008, -7.5138181433)
        for frame, rotation in zip((0, 5, 10, 15, 20), measured_world_x):
            self.assertAlmostEqual(native_degrees[frame], -rotation, places=7)
            self.assertAlmostEqual(interpolate(keys, -rotation)[0], frame / 20.)

    def test_tail_patterns_are_case_insensitive_and_do_not_match_other_clips(self):
        names = (
            'trimtab_elevator_key',
            'l_outer_elevator_deflection',
            'l_inner_elevator_deflection',
            'r_outer_elevator_deflection',
            'r_inner_elevator_deflection',
        )
        for name in names:
            self.assertEqual(self.rule_for(name.upper())['dataref'], self.rule_for(name)['dataref'])
        for name in ('left_elevator_flex', 'l_outer_elevator_deflection_extra',
                     'prefix_trimtab_elevator_key', 'elevator'):
            with self.subTest(unrelated=name):
                self.assertFalse(any(re.fullmatch(self.rule_for(candidate)['pattern'], name) for candidate in names))


if __name__ == '__main__':
    unittest.main()
