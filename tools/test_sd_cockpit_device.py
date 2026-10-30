"""Focused, non-simulator ECAM geometry preservation checks."""
import unittest
from sd_cockpit_device import augment_controls, augment_screen, compact_targets, SD_DEVICE, MARKER
from build_cockpit_controls import build_mfd_screens_obj
from validate_aircraft import cockpit_device_quads, cockpit_device_attributes

PATCH='''I
800
OBJ
POINT_COUNTS\t4\t0\t0\t6
VT\t0\t0\t0\t0\t0\t1\t0\t0
VT\t0\t1\t0\t0\t0\t1\t0\t1
VT\t1\t1\t0\t0\t0\t1\t1\t1
VT\t1\t0\t0\t0\t0\t1\t1\t0
IDX\t0
IDX\t1
IDX\t2
IDX\t0
IDX\t2
IDX\t3
ATTR_draw_disable
ATTR_manip_command\thand\ta380x/xp12/ecam/sd/engine\tENGINE
TRIS\t0\t6
ATTR_manip_none
ATTR_draw_enable
'''


class SdGeometry(unittest.TestCase):
    def test_compact_click_target_preserves_bounds_and_command(self):
        result=compact_targets(PATCH)
        self.assertIn('POINT_COUNTS\t24\t0\t0\t36',result)
        self.assertIn('a380x/xp12/ecam/sd/engine',result)
        points=[list(map(float,line.split()[1:4])) for line in result.splitlines() if line.startswith('VT\t')]
        self.assertEqual([min(p[k] for p in points) for k in range(3)],[0,0,0])
        self.assertEqual([max(p[k] for p in points) for k in range(3)],[1,1,0])
    def test_visible_full_uv_screen(self):
        original=build_mfd_screens_obj()
        result=augment_screen(original)
        self.assertEqual(cockpit_device_quads(result)[SD_DEVICE],[(0.,0.,1.,1.)])
        self.assertEqual(cockpit_device_attributes(result)[SD_DEVICE],{('1','18','1')})
        self.assertNotIn('ATTR_manip_',result)
        self.assertEqual(result.count(MARKER),1)

    def test_screen_idempotent(self):
        result=augment_screen(build_mfd_screens_obj())
        self.assertEqual(augment_screen(result),result)

    def test_existing_vertices_and_commands_preserved(self):
        original=build_mfd_screens_obj()
        result=augment_screen(original)
        for line in original.splitlines():
            if line.startswith(('VT\t','IDX\t','IDX10\t','TRIS\t','ATTR_')):
                self.assertIn(line,result.splitlines())

    def test_physical_control_indices_rebased(self):
        original=build_mfd_screens_obj()
        counts=next(line for line in original.splitlines() if line.startswith('POINT_COUNTS')).split()
        result=augment_controls(original,PATCH)
        self.assertIn(f'TRIS\t{counts[4]}\t6',result)
        self.assertIn('IDX\t'+counts[1],result)
        self.assertIn('a380x/xp12/ecam/sd/engine',result)
        self.assertEqual(augment_controls(result,PATCH),result)

    def test_refuses_inconsistent_base(self):
        original=build_mfd_screens_obj().replace('POINT_COUNTS\t8','POINT_COUNTS\t7')
        with self.assertRaises(ValueError):augment_screen(original)


if __name__=='__main__':unittest.main()
