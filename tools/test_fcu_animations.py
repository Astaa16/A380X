import unittest
from patch_fcu_animations import BUTTONS, KNOBS, patch_object
from patch_efis_animations import vertex_payload


class FcuAnimationTests(unittest.TestCase):
    def test_manual_control_inventory_contains_no_added_exped_button(self):
        self.assertEqual({node for node, _ in BUTTONS},
                         {'AP1', 'AP2', 'FD', 'ATHR', 'LOC', 'ALT', 'APPR',
                          'SPDMACH', 'TRUEMAG', 'HDGTRK', 'METER'})
        self.assertEqual({node for node, _ in KNOBS}, {'SPEED', 'HDG', 'ALT', 'UPDN'})

    def test_axial_parent_wraps_rotating_mesh_without_changing_uv_or_geometry(self):
        original = ('I\n800\nOBJ\nTEXTURE original.png\nTEXTURE_LIT original_lit.png\n'
                    'POINT_COUNTS\t3\t0\t0\t3\n'
                    'VT\t0\t0\t0\t0\t0\t1\t0.1\t0.2\n'
                    'VT\t1\t0\t0\t0\t0\t1\t0.3\t0.4\n'
                    'VT\t0\t1\t0\t0\t0\t1\t0.5\t0.6\n'
                    'IDX\t0\nIDX\t1\nIDX\t2\nTRIS\t0\t3\n')
        common = dict(name='KNOB_FCU_ALT', material='test', low=(0, 0, 0),
                      high=(1, 1, 0), count=3, points=[(0, 0, 0), (1, 0, 0), (0, 1, 0)])
        axial = dict(common, kind='translation', dataref='a380x/xp12/fcu/altitude_knob_axial',
                     values=[-1, 0, 1], poses=[(0, .001, -.002), (0, 0, 0), (0, -.001, .002)])
        rotary = dict(common, kind='rotation', dataref='a380x/xp12/fcu/altitude_knob_rotation',
                      values=[0, 360], poses=[0, 360], axis=(0, 0, 1), pivot=(.5, .5, 0))
        groups = [('# FCU AXIAL MOTION ', [axial]), ('# FCU ROTARY MOTION ', [rotary])]
        changed, rows = patch_object(original, 'panel_test.obj', groups)
        self.assertEqual(vertex_payload(original), vertex_payload(changed))
        self.assertEqual(len(rows), 2)
        self.assertEqual(changed.count('TRIS\t0\t3'), 1)
        self.assertLess(changed.index('# FCU AXIAL'), changed.index('# FCU ROTARY'))
        self.assertIn('ANIM_trans_key\t-1\t0\t0.001\t-0.002', changed)
        self.assertIn('ANIM_rotate_begin\t0\t0\t1\ta380x/xp12/fcu/altitude_knob_rotation', changed)
        repeated, rows = patch_object(changed, 'panel_test.obj', groups)
        self.assertEqual(repeated, changed)
        self.assertEqual(rows, [])


if __name__ == '__main__':
    unittest.main()
