import unittest

from check_animation_bounds import animation_bounds, bounds_errors
from test_obj8_preview import evaluate


class AnimationBoundsTests(unittest.TestCase):
    def test_nested_ref_filter_and_scope_restoration(self):
        result = evaluate('''ANIM_begin
ANIM_trans 0 0 -25 0 0 -25 0 1 test/trim
ANIM_begin
ANIM_trans 0 1 0 0 1 0 0 1 test/flap
TRIS 0 3
ANIM_end
TRIS 0 3
ANIM_end
TRIS 0 3''')
        self.assertEqual(result.draws[0].transform_datarefs, ('test/trim', 'test/flap'))
        self.assertEqual(result.draws[1].transform_datarefs, ('test/trim',))
        self.assertEqual(result.draws[2].transform_datarefs, ())
        bounds = animation_bounds(result, 'test/trim')
        self.assertEqual(bounds, {'minimum': [0, 0, -25], 'maximum': [1, 2, -25], 'triangles': 2, 'draw_calls': 2})
        self.assertEqual(bounds_errors(bounds, [-1, -1, -30], [2, 3, -20]), [])

    def test_detects_displaced_control_surface(self):
        result = evaluate('''ANIM_begin
ANIM_trans 0 0 0 0 0 0 0 1 test/trim
TRIS 0 3
ANIM_end''')
        errors = bounds_errors(animation_bounds(result, 'test/trim'), [-20, -5, -40], [20, 10, -15])
        self.assertEqual(errors, ['z maximum 0.000000 above -15.000000'])

    def test_absent_control_is_not_a_pass(self):
        with self.assertRaisesRegex(ValueError, 'No visible triangles'):
            animation_bounds(evaluate('TRIS 0 3'), 'test/trim')

    def test_nonfinite_and_reversed_limits_rejected(self):
        bounds = {'minimum': [0, 0, 0], 'maximum': [1, 1, 1]}
        for minimum, maximum in (([float('nan'), 0, 0], [1, 1, 1]), ([2, 0, 0], [1, 1, 1])):
            with self.assertRaises(ValueError):
                bounds_errors(bounds, minimum, maximum)


if __name__ == '__main__':
    unittest.main()
