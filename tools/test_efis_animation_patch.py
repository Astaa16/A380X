import unittest

from patch_efis_animations import patch_legacy, vertex_payload, wrap_unit_geometry


class EfisAnimationPatchTests(unittest.TestCase):
    def test_existing_range_and_pressure_poses_keep_their_angles_with_new_input_units(self):
        source = ('TEXTURE panel.png\nVT\t1\t2\t3\t0\t0\t1\t.2\t.3\nIDX\t0\n'
                  'ANIM_rotate_begin\t0\t0\t1\tsim/cockpit2/EFIS/map_range\n'
                  'ANIM_rotate_key\t1\t-30\nANIM_rotate_key\t6\t60\nANIM_rotate_end\n'
                  'ANIM_rotate_begin\t0\t1\t0\tsim/cockpit2/gauges/actuators/barometer_setting_in_hg_copilot\n'
                  'ANIM_rotate_key\t28\t-90\nANIM_rotate_key\t31\t90\nANIM_rotate_end\n')
        patched, count = patch_legacy(source)
        self.assertEqual(count, 2)
        self.assertIn('a380x/xp12/efis/captain/range_index', patched)
        self.assertIn('a380x/xp12/efis/first_officer/baro_preset_hpa', patched)
        self.assertIn('ANIM_rotate_key\t0\t-30', patched)
        self.assertIn('ANIM_rotate_key\t7\t60', patched)
        self.assertIn('ANIM_rotate_key\t745\t-90', patched)
        self.assertIn('ANIM_rotate_key\t1100\t90', patched)
        self.assertEqual(vertex_payload(source), vertex_payload(patched))
        self.assertEqual(patch_legacy(patched), (patched, 0))

    def test_baro_unit_patch_splits_only_target_triangles_and_keeps_uv_indices(self):
        source = ('TEXTURE panel.png\n'
                  'VT\t0\t0\t0\t0\t0\t1\t0\t0\n'
                  'VT\t1\t0\t0\t0\t0\t1\t1\t0\n'
                  'VT\t0\t1\t0\t0\t0\t1\t0\t1\n'
                  'VT\t4\t4\t4\t0\t0\t1\t0\t0\n'
                  'IDX10\t0\t1\t2\t3\t3\t3\nTRIS\t0\t6\n')
        target = dict(name='UNIT_POINTER', side='captain', material='test',
                      low=(0,0,0), high=(1,1,0), count=3,
                      pivot=(.5,.5,0), axis=(0,0,1), angle=53)
        patched, count = wrap_unit_geometry(source, 'panel_test.obj', [target])
        self.assertEqual(count, 1)
        self.assertIn('ANIM_rotate_key\t0\t53', patched)
        self.assertIn('ANIM_rotate_key\t1\t0', patched)
        self.assertTrue(patched.endswith('ANIM_end\nTRIS\t3\t3\n'))
        self.assertEqual(vertex_payload(source), vertex_payload(patched))
        self.assertEqual(wrap_unit_geometry(patched, 'panel_test.obj', [target]), (patched, 0))
        with self.assertRaisesRegex(ValueError, 'expected 6 indices, found 3'):
            wrap_unit_geometry(source, 'panel_test.obj', [dict(target, count=6)])

if __name__ == '__main__':
    unittest.main()
