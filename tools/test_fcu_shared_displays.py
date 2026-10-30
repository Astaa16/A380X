import re
import unittest

from build_cockpit_controls import FCU_LEGACY_SCREEN_QUADS
from stage_fcu_shared_displays import MARKER, patch_shared_displays, quad_vertices


class SharedFcuDisplayTests(unittest.TestCase):
    def fixture(self):
        rows = ['I\n800\nOBJ\nPOINT_COUNTS\t16\t0\t0\t24\n']
        for bounds in FCU_LEGACY_SCREEN_QUADS:
            rows += ['VT\t'+'\t'.join(f'{v:.10g}' for v in point)+'\n'
                     for point in quad_vertices(bounds)]
        for base in range(0, 16, 4):
            rows += [f'IDX\t{v}\n' for v in (base, base+1, base+2, base, base+2, base+3)]
        rows += ['ATTR_cockpit_region\t0\nATTR_poly_os\t2\n']
        rows += [f'TRIS\t{start}\t6\n' for start in range(0, 24, 6)]
        rows += ['ATTR_no_cockpit\nATTR_poly_os\t0\n',
                 'ATTR_manip_command\tbutton\ta380x/xp12/fcu/ap1_toggle\tAP1\n',
                 'ANIM_begin\nANIM_trans\t0\t0\t0\t0\t.001\t0\t0\t1\ta380x/xp12/fcu/ap1_pressed\n',
                 'TRIS\t24\t3\nANIM_end\nATTR_manip_none\n',
                 '# A380X NATIVE ND DEVICE QUADS V1\nATTR_cockpit_device\t3\nTRIS\t27\t6\n']
        return ''.join(rows)

    def test_one_surface_per_pair_preserves_inputs_and_all_other_payload(self):
        original = self.fixture()
        changed, report = patch_shared_displays(original)
        self.assertEqual(report['shared_screens'], 2)
        self.assertEqual(changed.count('VT\t'), 16)  # preserve all IDs
        self.assertEqual(re.findall(r'^IDX.*$', changed, re.M), re.findall(r'^IDX.*$', original, re.M))
        self.assertIn('TRIS\t0\t6\n', changed)
        self.assertIn('TRIS\t12\t6\n', changed)
        self.assertNotIn('TRIS\t6\t6\n', changed)
        self.assertNotIn('TRIS\t18\t6\n', changed)
        self.assertEqual(original.split('ATTR_no_cockpit')[1], changed.split('ATTR_no_cockpit')[1])
        vertices = [list(map(float, row.split()[1:])) for row in changed.splitlines() if row.startswith('VT\t')]
        self.assertAlmostEqual(vertices[0][0], .014283556)
        self.assertAlmostEqual(vertices[1][0], .115242831)
        self.assertEqual((vertices[0][6], vertices[1][6]), (.75, .5))
        self.assertAlmostEqual(vertices[8][0], -.115033157)
        self.assertAlmostEqual(vertices[9][0], -.016629022)
        self.assertEqual((vertices[8][6], vertices[9][6]), (1, .75))

    def test_migration_is_idempotent_and_marker_is_validated(self):
        changed, _ = patch_shared_displays(self.fixture())
        repeated, report = patch_shared_displays(changed)
        self.assertEqual(changed, repeated)
        self.assertTrue(report['already_applied'])
        with self.assertRaises(ValueError):
            patch_shared_displays(MARKER+'\n'+self.fixture())

    def test_refuse_unknown_uv_mapping_and_duplicate_draws(self):
        original = self.fixture()
        with self.assertRaises(ValueError):
            patch_shared_displays(original.replace('\t0.625\t0.53125', '\t0.7\t0.53125', 1))
        with self.assertRaises(ValueError):
            patch_shared_displays(original+'TRIS\t0\t6\n')


if __name__ == '__main__':
    unittest.main()
