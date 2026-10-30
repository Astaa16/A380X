import unittest
from trim_cockpit_panel import (MARKER,PREFIX,augment_controls,augment_screen,control_boxes,group_motion_meshes)

OBJ=('I\n800\nOBJ\nTEXTURE test.png\nPOINT_COUNTS\t3\t0\t0\t3\n'
     'VT\t0\t0\t0\t0\t1\t0\t0\t0\nVT\t1\t0\t0\t0\t1\t0\t1\t0\n'
     'VT\t0\t0\t1\t0\t1\t0\t0\t1\nIDX\t0\nIDX\t1\nIDX\t2\nTRIS\t0\t3\n')

class TrimGeometryTests(unittest.TestCase):
    def targets(self):
        return [dict(name=n,animation=n,low=(-1,0,-1),high=(1,1,1),pivot=(0,0,0))
                for n in ('SWITCH_TRIM_PITCH','KNOB_RUDDERTRIM','PUSH_TRIM_RESET')]
    def test_hit_regions_direction_and_no_overlap(self):
        boxes=control_boxes(self.targets());self.assertEqual(len(boxes),5)
        self.assertLess(boxes[0][1][2],boxes[1][0][2])
        self.assertGreater(boxes[2][0][0],boxes[3][1][0])
        self.assertEqual([b[2] for b in boxes],['pitch_up','pitch_down','rudder_left','rudder_right','rudder_reset'])
    def test_append_only_and_idempotent(self):
        result=augment_controls(OBJ,control_boxes(self.targets()))
        self.assertEqual(next(l for l in result.splitlines() if l.startswith('POINT_COUNTS')),
                         'POINT_COUNTS\t123\t0\t0\t183')
        self.assertEqual(result.count('ATTR_manip_command\t'),5)
        self.assertNotIn('ATTR_manip_drag',result)
        self.assertIn('TRIS\t0\t3',result)
        for line in OBJ.splitlines():
            if line.startswith(('VT\t','IDX\t')):self.assertIn(line,result)
        self.assertEqual(augment_controls(result,control_boxes(self.targets())),result)
    def test_device_does_not_use_existing_panel_atlas(self):
        rows=['VT\t0\t0\t0\t0\t1\t0\t0\t0']*3
        result=augment_screen(OBJ,rows,[0,1,2]);self.assertIn('a380x.xp12.trim.rudder',result)
        self.assertNotIn('ATTR_cockpit_region',result)
        self.assertIn(PREFIX+'device_active',result)
        self.assertEqual(augment_screen(result,rows,[0,1,2]),result)
    def test_authored_pitch_animation_is_inverted_but_rudder_is_not(self):
        targets=[dict(t,material='panel',points=[(0,0,0)],count=3,poses=[0,11,22])
                 for t in self.targets()]
        grouped=group_motion_meshes(targets)
        self.assertEqual(grouped[0]['poses'],[22,11,0])
        self.assertEqual(grouped[1]['poses'],[0,11,22])
        self.assertEqual(targets[0]['poses'],[0,11,22])

if __name__=='__main__':unittest.main()
