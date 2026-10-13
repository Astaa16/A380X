#!/usr/bin/env python3
"""Pure-Python fixtures; no Blender process or renderer is launched."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from obj8_preview import interpolate, parse_obj8


TABLE = '''I
800
OBJ
TEXTURE fixture.png
POINT_COUNTS 3 0 0 3
VT 0 0 0 0 0 1 0 0
VT 1 0 0 0 0 1 1 0
VT 0 1 0 0 0 1 0 1
IDX 0
IDX 2
IDX 1
'''


def evaluate(commands, values=None):
    return parse_obj8((TABLE + commands).splitlines(), values=values)


class Obj8PreviewTests(unittest.TestCase):
    def assertPoint(self, actual, expected):
        for a, b in zip(actual, expected):
            self.assertAlmostEqual(a, b, places=7)

    def test_native_clockwise_becomes_blender_counterclockwise(self):
        result = evaluate('TRIS 0 3')
        vertices, uv, faces, styles = result.bake()
        self.assertEqual(faces, [(0, 1, 2)])
        self.assertEqual(vertices, [(0., 0., 0.), (1., 0., 0.), (0., 0., 1.)])
        self.assertEqual(uv, [(0., 0.), (1., 0.), (0., 1.)])
        self.assertEqual(styles, [(True, None)])

    def test_nested_transform_order_and_stack_restore(self):
        result = evaluate('''ANIM_begin
ANIM_trans 10 0 0 10 0 0
ANIM_begin
ANIM_rotate 0 0 1 90 90
TRIS 0 3
ANIM_end
TRIS 0 3
ANIM_end
TRIS 0 3''')
        vertices, _, faces, _ = result.bake()
        self.assertPoint(vertices[faces[0][1]], (10, 0, 1))
        self.assertPoint(vertices[faces[1][1]], (11, 0, 0))
        self.assertPoint(vertices[faces[2][1]], (1, 0, 0))

    def test_ground_pose_gear_down_and_override(self):
        commands = '''ANIM_begin
ANIM_trans 0 0 0 0 10 0 0 1 sim/flightmodel2/gear/deploy_ratio[3]
TRIS 0 3
ANIM_end'''
        result = evaluate(commands)
        self.assertPoint(result.bake()[0][0], (0, 0, 10))
        result = evaluate(commands, {'sim/flightmodel2/gear/deploy_ratio[3]': .25})
        self.assertPoint(result.bake()[0][0], (0, 0, 2.5))

    def test_keyframes_interpolate_extrapolate_and_loop(self):
        commands = '''ANIM_begin
ANIM_trans_begin test/value
ANIM_trans_key 0 0 0 0
ANIM_trans_key 1 0 10 0
ANIM_trans_key 2 0 40 0
ANIM_trans_end
ANIM_keyframe_loop 3
TRIS 0 3
ANIM_end'''
        self.assertPoint(evaluate(commands, {'test/value': 4.5}).bake()[0][0], (0, 0, 25))
        self.assertPoint(evaluate(commands, {'test/value': 2.5}).bake()[0][0], (0, 0, 55))
        self.assertEqual(interpolate([(0, (0,)), (1, (10,))], -1), (-10,))

    def test_rotation_keyframes_and_unknown_dataref_zero(self):
        commands = '''ANIM_begin
ANIM_rotate_begin 0 0 1 test/value
ANIM_rotate_key -1 -90
ANIM_rotate_key 1 90
ANIM_rotate_end
TRIS 0 3
ANIM_end'''
        result = evaluate(commands)
        self.assertEqual(result.datarefs, {'test/value': 0.})
        self.assertPoint(result.bake()[0][1], (1, 0, 0))
        self.assertPoint(evaluate(commands, {'test/value': 1}).bake()[0][1], (0, 0, 1))

    def test_hide_show_and_draw_state(self):
        result = evaluate('''ANIM_begin
ANIM_hide 0 0 test/unknown
TRIS 0 3
ANIM_begin
ANIM_show 0 0 test/unknown
TRIS 0 3
ANIM_end
TRIS 0 3
ATTR_no_draw
ANIM_end
TRIS 0 3
ATTR_draw
TRIS 0 3''')
        self.assertEqual(len(result.draws), 2)

    def test_culling_alpha_and_lod(self):
        result = evaluate('''ATTR_LOD 0 100
ATTR_no_cull
ATTR_no_blend 0.7
TRIS 0 3
ATTR_cull
ATTR_blend
TRIS 0 3
ATTR_LOD 100 200
TRIS 0 3''')
        self.assertEqual(result.bake()[3], [(False, .7), (True, None)])
        far = parse_obj8((TABLE + 'ATTR_LOD 0 100\nTRIS 0 3\nATTR_LOD 100 200\nTRIS 0 3').splitlines(), lod_distance=150)
        self.assertEqual(len(far.draws), 1)

    def test_rejects_unsupported_and_malformed_animation(self):
        for commands in ('ANIM_begin\nANIM_scale 1 1 1\nANIM_end',
                         'ANIM_rotate 0 0 1 90 90', 'ANIM_begin\nTRIS 0 3',
                         'ANIM_end', 'ANIM_begin\nANIM_keyframe_loop 2\nANIM_end',
                         'ANIM_begin\nANIM_trans_begin test/x\nTRIS 0 3\nANIM_end',
                         'ANIM_begin\nANIM_rotate_begin 0 0 1 test/x\nANIM_rotate_key 1 2\nANIM_rotate_key 1 3\nANIM_rotate_end\nANIM_end'):
            with self.subTest(commands=commands), self.assertRaises(ValueError):
                evaluate(commands)

    def test_rejects_invalid_geometry_and_conditionals(self):
        for commands in ('TRIS 0 4', 'TRIS 3 3', 'IF GLOBAL_LIGHTING\nTRIS 0 3\nENDIF'):
            with self.subTest(commands=commands), self.assertRaises(ValueError):
                evaluate(commands)


if __name__ == '__main__':
    unittest.main()
