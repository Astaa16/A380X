import math
import unittest

from build_cockpit_controls import local_matrix, matrix_identity
from patch_center_right_animations import (
    GEAR_REF, OLD_GEAR_REF, animation_block, rotation_keys, wrap_geometry,
)
from patch_efis_animations import vertex_payload


def quaternion_z(degrees):
    angle = math.radians(degrees) / 2
    return (0, 0, math.sin(angle), math.cos(angle))


class CenterRightAnimationPatchTests(unittest.TestCase):
    def test_rotation_uses_source_rest_parent_axis_and_unwraps_quaternion_signs(self):
        rest = quaternion_z(35)
        poses = [quaternion_z(35), quaternion_z(125), quaternion_z(215),
                 tuple(-v for v in quaternion_z(260))]
        # The authored knob's local axis points along a tilted panel normal.
        parent = local_matrix(dict(rotation=(math.sin(math.pi/4), 0, 0, math.cos(math.pi/4))))
        axis, angles = rotation_keys(poses, rest, parent)
        for actual, expected in zip(axis, (0, -1, 0)):
            self.assertAlmostEqual(actual, expected)
        for actual, expected in zip(angles, (0, 90, 180, 225)):
            self.assertAlmostEqual(actual, expected)

    def test_source_vertex_matching_excludes_base_inside_lever_bounds(self):
        source = ('TEXTURE panel.png\nPOINT_COUNTS 6 0 0 6\n'
                  'VT\t0\t0\t0\t0\t0\t1\t0\t0\n'
                  'VT\t1\t0\t0\t0\t0\t1\t1\t0\n'
                  'VT\t0\t1\t0\t0\t0\t1\t0\t1\n'
                  'VT\t.1\t.1\t0\t0\t0\t1\t0\t0\n'
                  'VT\t.5\t.1\t0\t0\t0\t1\t1\t0\n'
                  'VT\t.1\t.5\t0\t0\t0\t1\t0\t1\n'
                  'IDX10\t0\t1\t2\t3\t4\t5\nTRIS\t0\t6\n')
        target = dict(name='ASKID', material='test', low=(0, 0, 0), high=(1, 1, 0), count=3,
                      points=[(0, 0, 0), (1, 0, 0), (0, 1, 0)], kind='rotation',
                      pivot=(.5, .5, 0), axis=(0, 0, 1), dataref='test/askid',
                      values=[0, 1], poses=[0, 44])
        patched, meshes, gears = wrap_geometry(source, 'panel_test.obj', [target])
        self.assertEqual(vertex_payload(source), vertex_payload(patched))
        self.assertEqual(meshes, [dict(name='ASKID', dataref='test/askid', indices=3)])
        self.assertEqual(gears, 0)
        self.assertIn('ANIM_rotate_key\t1\t44', patched)
        self.assertTrue(patched.endswith('ANIM_end\nTRIS\t3\t3\n'))
        self.assertEqual(wrap_geometry(patched, 'panel_test.obj', [target]), (patched, [], 0))
        with self.assertRaisesRegex(ValueError, 'expected 6 indices, found 3'):
            wrap_geometry(source, 'panel_test.obj', [dict(target, count=6)])

    def test_existing_gear_motion_is_retargeted_without_second_transform(self):
        source = f'ANIM_begin\nANIM_rotate_begin\t1\t0\t0\t{OLD_GEAR_REF}\n'
        source += 'ANIM_rotate_key\t0\t53.3\nANIM_rotate_key\t1\t-2.1\nANIM_rotate_end\nTRIS\t0\t3\nANIM_end\n'
        patched, meshes, gears = wrap_geometry(source, 'unrelated.obj', [])
        self.assertEqual(patched, source.replace(OLD_GEAR_REF, GEAR_REF))
        self.assertEqual(gears, 1)
        self.assertFalse(meshes)
        self.assertEqual(patched.count('ANIM_begin'), 1)

    def test_pushbutton_translation_is_an_offset_not_absolute_world_position(self):
        target = dict(name='DATE', dataref='test/date', kind='translation', values=[0, 1],
                      poses=[(0, 0, 0), (0, -.00046, .001297)])
        block = '\n'.join(animation_block(target, 18, 6))
        self.assertIn('ANIM_trans_key\t0\t0\t0\t0', block)
        self.assertIn('ANIM_trans_key\t1\t0\t-0.00046\t0.001297', block)
        self.assertIn('TRIS\t18\t6', block)

    def test_future_native_export_is_not_wrapped_twice(self):
        target = dict(name='CHR', material='test', dataref='test/chr')
        source = 'ANIM_trans_begin\ttest/chr\nANIM_trans_key\t0\t0\t0\t0\nANIM_trans_end\n'
        self.assertEqual(wrap_geometry(source, 'panel_test.obj', [target]), (source, [], 0))


if __name__ == '__main__':
    unittest.main()
