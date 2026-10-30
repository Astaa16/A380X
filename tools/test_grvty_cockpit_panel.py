import json
import unittest
from pathlib import Path

import grvty_cockpit_panel as panel
from avionics_provider import GRVTY_COMMANDS, GRVTY_DATAREFS
from patch_cockpit_materials import DEFAULT_SOURCE
from patch_center_right_animations import OLD_GEAR_REF

OBJ = ('I\n800\nOBJ\nTEXTURE test.png\nPOINT_COUNTS\t3\t0\t0\t3\n'
       'VT\t0\t0\t0\t0\t1\t0\t0\t0\nVT\t1\t0\t0\t0\t1\t0\t1\t0\n'
       'VT\t0\t0\t1\t0\t1\t0\t0\t1\nIDX\t0\nIDX\t1\nIDX\t2\nTRIS\t0\t3\n')


def knobs_obj(extra_static=True, nested=False):
    """One triangle per FBW mesh of the MIP_KNOBS_02 export plus a static neighbour."""
    names = sorted(panel.EXPECTED[panel.KNOBS])
    vertices, indices, targets = [], [], []
    for n, name in enumerate(names + (['static'] if extra_static else [])):
        x = 10.0 * n
        points = [(x, 0.0, 0.0), (x + 1, 0.0, 0.0), (x, 0.0, 1.0)]
        base = len(vertices)
        vertices += points
        indices += [base, base + 1, base + 2]
        if name != 'static':
            targets.append(dict(name=name, material='a380_cockpit_mip_knobs_02_4k', count=3, points=points,
                                low=(x, 0.0, 0.0), high=(x + 1, 0.0, 1.0), kind='rotation', pivot=(x, 0.0, 0.0),
                                axis=(-1.0, 0.0, 0.0), values=[0, 1, 2], poses=[0.0, 16.07, 32.15],
                                dataref=panel.PREFIX + 'grvty_switch_anim', animation='GRVTY_LDG'))
    lines = ['I', '800', 'OBJ', 'TEXTURE\ttest.png', f'POINT_COUNTS\t{len(vertices)}\t0\t0\t{len(indices)}']
    lines += [f'VT\t{x:g}\t{y:g}\t{z:g}\t0\t1\t0\t0.5\t0.5' for x, y, z in vertices]
    lines += [f'IDX\t{i}' for i in indices]
    if nested:
        lines += ['ANIM_begin', 'ANIM_hide\t0\t1\tsim/foo', f'TRIS\t0\t{len(indices)}', 'ANIM_end']
    else:
        lines += [f'TRIS\t0\t{len(indices)}']
    return '\n'.join(lines) + '\n', targets


class GrvtyPanelMotionTests(unittest.TestCase):
    def test_exact_meshes_wrapped_payload_preserved_and_idempotent(self):
        text, targets = knobs_obj()
        patched, matched = panel.patch_panel(text, panel.KNOBS, targets)
        self.assertEqual({row['name'] for row in matched}, panel.EXPECTED[panel.KNOBS])
        self.assertTrue(all(row['indices'] == 3 for row in matched))
        self.assertEqual(patched.count(panel.MOTION_MARKER), 7)
        self.assertEqual(patched.count('ANIM_rotate_begin\t-1\t0\t0\ta380x/xp12/gear/grvty_switch_anim'), 7)
        self.assertIn('ANIM_rotate_key\t1\t16.07', patched)
        self.assertEqual(panel.vertex_payload(text), panel.vertex_payload(patched))
        self.assertEqual(panel.non_draw_lines(text), panel.non_draw_lines(patched))
        self.assertEqual(panel.tris_coverage(text), panel.tris_coverage(patched))
        self.assertIn('TRIS\t21\t3', patched)  # the static neighbour stays unanimated
        again, rows = panel.patch_panel(patched, panel.KNOBS, targets)
        self.assertEqual(again, patched)
        self.assertEqual(rows, [])

    def test_refuses_side_effects_nesting_and_missing_meshes(self):
        text, targets = knobs_obj()
        with self.assertRaises(ValueError):
            panel.patch_panel(text + f'ANIM_begin\nANIM_rotate_begin\t1\t0\t0\t{OLD_GEAR_REF}\nANIM_end\n',
                              panel.KNOBS, targets)
        nested, targets = knobs_obj(nested=True)
        with self.assertRaises(ValueError):
            panel.patch_panel(nested, panel.KNOBS, targets)
        with self.assertRaises(ValueError):
            panel.patch_panel(text, panel.KNOBS, targets[:-1])
        bad = [dict(t, count=6) if t['name'] == 'GRVTY_LDG' else t for t in targets]
        with self.assertRaises(ValueError):
            panel.patch_panel(text, panel.KNOBS, bad)


class GrvtyPanelClickTests(unittest.TestCase):
    def test_oriented_box_has_clockwise_outward_faces(self):
        identity = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
        rows, indices, _ = panel.oriented_box([(0, 0, 0), (1, 2, 3)], identity, margin=0.0)
        self.assertEqual((len(rows), len(indices)), (24, 36))
        vertices = [tuple(map(float, row.split('\t')[1:7])) for row in rows]
        for k in range(0, 36, 3):
            a, b, c = (vertices[i] for i in indices[k:k + 3])
            u = [b[i] - a[i] for i in range(3)]
            v = [c[i] - a[i] for i in range(3)]
            cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            self.assertLess(sum(cross[i] * a[i + 3] for i in range(3)), 0)   # OBJ8 front = clockwise
            centre = [sum(p[i] for p in (a, b, c)) / 3 for i in range(3)]
            self.assertGreater(sum((centre[i] - (0.5, 1.0, 1.5)[i]) * a[i + 3] for i in range(3)), 0)

    def test_controls_append_only_and_idempotent(self):
        identity = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
        rows, box, _ = panel.oriented_box([(0, 0, 0), (1, 1, 1)], identity)
        commands = ['ATTR_draw_disable', 'ATTR_manip_command\thand\ta380x/xp12/gear/grvty_master_guard_toggle\tx',
                    'TRIS\t0\t36', 'ATTR_manip_none', 'ATTR_draw_enable']
        result = panel.augment_controls(OBJ, rows, box, commands)
        lines = result.splitlines()
        self.assertIn('POINT_COUNTS\t27\t0\t0\t39', lines)
        self.assertIn('TRIS\t3\t36', lines)       # shifted after the existing indices
        self.assertIn('TRIS\t0\t3', lines)        # existing draw untouched
        for line in OBJ.splitlines():
            if line.startswith(('VT\t', 'IDX\t', 'TEXTURE')):
                self.assertIn(line, lines)
        # Removing the appended rows restores the original bytes exactly.
        original = [line for line in lines[:lines.index(panel.MARKER) - 1]
                    if line not in rows and not (line.startswith('IDX\t') and int(line.split()[1]) >= 3)]
        original = [('POINT_COUNTS\t3\t0\t0\t3' if line.startswith('POINT_COUNTS') else line) for line in original]
        self.assertEqual('\n'.join(original) + '\n', OBJ)
        self.assertEqual(panel.augment_controls(result, rows, box, commands), result)


@unittest.skipUnless(DEFAULT_SOURCE.is_file(), 'pinned FBW cockpit glTF not available')
class GrvtyPanelSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.targets = panel.motion_targets(DEFAULT_SOURCE)

    def test_authored_clips(self):
        by_name = {t['name']: t for t in self.targets}
        switch = by_name['SWITCH_GRVTY']
        # Key 0 is the source rest pose RESET; OFF and DOWN rotate the lever down 16 and 32 degrees.
        self.assertEqual(switch['values'], [0.0, 1.0, 2.0])
        self.assertAlmostEqual(switch['poses'][0], 0.0, places=3)
        self.assertAlmostEqual(switch['poses'][1], 16.07, places=1)
        self.assertAlmostEqual(switch['poses'][2], 32.15, places=1)
        self.assertEqual(switch['dataref'], panel.PREFIX + 'grvty_switch_anim')
        self.assertAlmostEqual(by_name['GRVTY_LDG_LOCK_COVER']['poses'][1], 76.7, places=0)
        self.assertEqual(by_name['GRVTY_LDG_LOCK1']['material'], 'invisble_clickspots')
        self.assertAlmostEqual(by_name['GRVTY_LDG_LOCK2']['poses'][1], 89.4, places=0)
        self.assertAlmostEqual(by_name['GRVTY_LDG_LOCK3']['poses'][1], 86.9, places=0)
        # +X is the crew's left: guard 1 (LOCK2) left of guard 2 (LOCK3).
        self.assertGreater(by_name['GRVTY_LDG_LOCK2']['pivot'][0], by_name['GRVTY_LDG_LOCK3']['pivot'][0])
        for relative, names in panel.EXPECTED.items():
            self.assertEqual({t['name'] for t in self.targets if relative.endswith(t['material'] + '.obj')}, names)
        self.assertTrue({t['dataref'] for t in self.targets} <= set(GRVTY_DATAREFS))

    def test_click_targets(self):
        vertices, indices, commands, rows = panel.click_geometry(DEFAULT_SOURCE, self.targets)
        self.assertEqual([row['name'] for row in rows], ['cover', 'guard_1', 'guard_2', 'switch'])
        self.assertEqual((len(vertices), len(indices)), (96, 144))
        self.assertEqual({c for row in rows for c in row['commands']}, set(GRVTY_COMMANDS))
        hide = commands.index(f'ANIM_hide\t-1\t0.5\t{panel.MASTER_STATE}')
        cover = next(i for i, c in enumerate(commands) if 'grvty_master_guard_toggle' in c)
        self.assertLess(cover, hide)              # the cover is always clickable
        self.assertEqual(sum(c.startswith('ANIM_begin') for c in commands),
                         sum(c.startswith('ANIM_end') for c in commands))
        self.assertEqual(sum('ATTR_manip_command_switch_up_down\tup_down' in c for c in commands), 1)
        self.assertEqual(commands[-1], 'ATTR_draw_enable')
        switch = rows[-1]
        self.assertTrue(switch['hidden_while_cover_closed'] and not rows[0]['hidden_while_cover_closed'])
        json.dumps(rows)


if __name__ == '__main__':
    unittest.main()
