import json
import tempfile
import unittest
from pathlib import Path

import cockpit_lever_geometry as geometry
import install_thrust_levers as installer
import thrust_lever_cockpit as thrust
from avionics_provider import THRUST_COMMANDS, THRUST_DATAREFS
from patch_cockpit_materials import DEFAULT_SOURCE

AIRCRAFT = Path(__file__).resolve().parents[1] / 'output/A380X-XP12'

LEVER = dict(kind='rotation', pivot=(0.0, 1.0, 2.0), axis=(1.0, 0.0, 0.0), values=[0.0, 50.0, 100.0],
             poses=[0.0, 25.0, 50.0], dataref=thrust.PREFIX + 'lever_1_anim', node='LEVER', animation='lever')
BUTTON = dict(kind='translation', pivot=(0.0, 0.0, 0.0), values=[0.0, 1.0], poses=[(0, 0, 0), (-0.0025, 0, 0)],
              dataref=thrust.PREFIX + 'disc_left_anim', node='BUTTON', animation='button')


def panel_obj(nested=False):
    """Lever, button (child of the lever) and a static neighbour, one triangle each."""
    vertices, indices, parts = [], [], []
    for n, (name, chain) in enumerate((('LEVER', [LEVER]), ('BUTTON', [LEVER, BUTTON]), ('static', None))):
        x = 10.0 * n
        points = [(x, 0.0, 0.0), (x + 1, 0.0, 0.0), (x, 0.0, 1.0)]
        base = len(vertices)
        vertices += points
        indices += [base, base + 1, base + 2]
        if chain:
            parts.append(dict(name=name, points=points, count=3, chain=chain))
    lines = ['I', '800', 'OBJ', 'TEXTURE\ttest.png', f'POINT_COUNTS\t{len(vertices)}\t0\t0\t{len(indices)}']
    lines += [f'VT\t{x:g}\t{y:g}\t{z:g}\t0\t1\t0\t0.5\t0.5' for x, y, z in vertices]
    lines += [f'IDX\t{i}' for i in indices]
    lines += ['ATTR_shiny_rat\t0.5']
    if nested:
        lines += ['ANIM_begin', 'ANIM_hide\t0\t1\tsim/foo', f'TRIS\t0\t{len(indices)}', 'ANIM_end']
    else:
        lines += [f'TRIS\t0\t{len(indices)}']
    return '\n'.join(lines) + '\n', parts


class GeometryTests(unittest.TestCase):
    def test_nested_wrap_preserves_payload_and_is_idempotent(self):
        text, parts = panel_obj()
        patched, rows = thrust.patch_panel(text, 'x.obj', parts)
        self.assertEqual([(r['name'], r['indices']) for r in rows], [('LEVER', 3), ('BUTTON', 3)])
        self.assertEqual(patched.count(thrust.MOTION_MARKER), 2)
        # The button is drawn inside the lever's rotation, then its own translation.
        button = patched.split(thrust.MOTION_MARKER + 'BUTTON')[1].splitlines()
        self.assertEqual([l for l in button if l.startswith('ANIM_begin')], ['ANIM_begin', 'ANIM_begin'])
        self.assertLess(next(i for i, l in enumerate(button) if l.startswith('ANIM_rotate_begin')),
                        next(i for i, l in enumerate(button) if l.startswith('ANIM_trans_begin')))
        self.assertIn('ANIM_trans_key\t1\t-0.0025\t0\t0', patched)
        self.assertIn('TRIS\t6\t3', patched)                      # static neighbour stays unanimated
        self.assertEqual(geometry.vertex_payload(text), geometry.vertex_payload(patched))
        self.assertEqual(geometry.tris_coverage(text), geometry.tris_coverage(patched))
        self.assertEqual(sum(l.startswith('ANIM_begin') for l in patched.splitlines()),
                         sum(l.startswith('ANIM_end') for l in patched.splitlines()))
        again, rows = thrust.patch_panel(patched, 'x.obj', parts)
        self.assertEqual((again, rows), (patched, []))

    def test_refuses_nesting_missing_parts_counts_and_ambiguity(self):
        nested, parts = panel_obj(nested=True)
        with self.assertRaises(ValueError):
            thrust.patch_panel(nested, 'x.obj', parts)
        text, parts = panel_obj()
        with self.assertRaises(ValueError):
            thrust.patch_panel(text, 'x.obj', [dict(parts[0], count=6), parts[1]])
        with self.assertRaises(ValueError):
            thrust.patch_panel(text, 'x.obj', [parts[0], dict(parts[1], points=parts[0]['points'])])
        partial = text.replace('TRIS\t0\t9', '# A380X THRUST LEVERS V1 MOTION LEVER\nTRIS\t0\t9')
        with self.assertRaises(ValueError):
            thrust.patch_panel(partial, 'x.obj', parts)

    def test_drag_rotate_follows_the_authored_arc(self):
        rows = geometry.drag_rotate_lines(LEVER, thrust.PREFIX + 'lever_1_input',
                                          [(0, 0), (25, 55), (35, 78), (45, 100)], 'tip')
        fields = rows[0].split('\t')
        self.assertEqual(fields[0], 'ATTR_manip_drag_rotate')
        self.assertEqual(fields[2:8], ['0', '1', '2', '1', '0', '0'])      # pivot and axis of the motion
        self.assertEqual(fields[8:15], ['0', '50', '0', '0', '45', '0', '0'])
        self.assertEqual(fields[15:], [thrust.PREFIX + 'lever_1_input', 'none', 'tip'])
        self.assertEqual(rows[1:], ['ATTR_manip_keyframe\t25\t27.5', 'ATTR_manip_keyframe\t35\t39'])
        with self.assertRaises(ValueError):
            geometry.drag_rotate_lines(BUTTON, 'x', [(0, 0), (1, 1)], 'tip')
        detent = geometry.drag_rotate_lines(LEVER, 'x', [(0, 0), (1, 100)], 'tip', detent_ranges=((0, 0, 0),))
        self.assertEqual(detent[-1], 'ATTR_axis_detent_range\t0\t0\t0')

    def test_retire_exactly_the_generated_throttle_manipulators(self):
        old = ['ATTR_manip_drag_axis\thand\t0\t-0.002\t-0.195\t0\t1\tsim/cockpit2/engine/actuators/throttle_ratio[%d]\tT%d' % (i, i)
               for i in range(4)]
        old += ['ATTR_manip_drag_axis\thand\t0\t0\t0.12\t0\t-1\tsim/cockpit2/engine/actuators/throttle_jet_rev_ratio[%d]\tR' % i
                for i in (1, 2)]
        old += ['ATTR_manip_command\tbutton\ta380x/xp12/fcu/athr_disconnect\tA/THR DISCONNECT - left thrust lever',
                'ATTR_manip_command\tbutton\ta380x/xp12/fcu/athr_disconnect\tA/THR DISCONNECT - right thrust lever']
        other = 'ATTR_manip_command\tbutton\ta380x/xp12/fcu/athr_toggle\tA/THR'
        text = '\n'.join(sum(([line, 'TRIS\t0\t36', 'ATTR_manip_none'] for line in [other, *old]), [])) + '\n'
        retired, rows = thrust.retire_manipulators(text)
        self.assertEqual(rows, old)
        self.assertEqual(retired.count(thrust.RETIRED_MARKER), 8)
        self.assertIn(other, retired)
        for line in old:
            self.assertIn(thrust.RETIRED_MARKER + line + '\nATTR_manip_none\nTRIS', retired)
        with self.assertRaises(ValueError):
            thrust.retire_manipulators(text.replace(old[3], 'ATTR_manip_none'))


class InstallerTests(unittest.TestCase):
    def test_refuses_drift_and_skips_applied(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            aircraft, stage = root / 'aircraft', root / 'stage'
            rows = []
            for relative in (thrust.CONTROLS, thrust.DECAL, thrust.PEDESTAL):
                for base, text in ((aircraft, 'before ' + relative), (stage, 'after ' + relative)):
                    (base / relative).parent.mkdir(parents=True, exist_ok=True)
                    (base / relative).write_text(text)
                rows.append(dict(path=relative, before_sha256=installer.sha(aircraft / relative),
                                 after_sha256=installer.sha(stage / relative)))
            (stage / 'staging-report.json').write_text(json.dumps(
                dict(format=1, marker=thrust.MARKER, aircraft=str(aircraft.resolve()), files=rows)))
            actions = installer.plan(aircraft.resolve(), stage.resolve())
            self.assertEqual([a['action'] for a in actions], ['replace'] * 3)
            (aircraft / thrust.DECAL).write_text('after ' + thrust.DECAL)
            self.assertEqual(installer.plan(aircraft.resolve(), stage.resolve())[1]['action'], 'already-applied')
            (aircraft / thrust.PEDESTAL).write_text('changed by someone else')
            with self.assertRaises(ValueError):
                installer.plan(aircraft.resolve(), stage.resolve())


@unittest.skipUnless(DEFAULT_SOURCE.is_file(), 'pinned FBW cockpit glTF not available')
class SourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = geometry.Source(DEFAULT_SOURCE)
        cls.motion = thrust.motions(cls.source)

    def test_authored_clips(self):
        for n, top in ((1, 55.6), (2, 55.6), (3, 55.3), (4, 55.5)):
            lever = self.motion[f'lever_{n}']
            self.assertEqual(lever['kind'], 'rotation')
            self.assertAlmostEqual(lever['poses'][-1], top, places=2)
            self.assertAlmostEqual(lever['values'][-1], 100.0)
            self.assertAlmostEqual(lever['axis'][0], 1.0, places=6)    # lateral pivot, positive = forward
        for n in (2, 3):
            self.assertAlmostEqual(self.motion[f'reverse_{n}']['poses'][-1], 94.0, places=3)
            self.assertAlmostEqual(self.motion[f'reverse_{n}']['axis'][0], -1.0, places=6)
        self.assertAlmostEqual(self.motion['disc_left']['poses'][-1][0], -0.00253, places=5)
        self.assertAlmostEqual(self.motion['disc_right']['poses'][-1][0], 0.00235, places=5)
        used = {m['dataref'] for m in self.motion.values()}
        self.assertTrue(used <= set(THRUST_DATAREFS))

    def test_click_targets(self):
        vertices, indices, commands, rows = thrust.click_geometry(self.source, self.motion)
        self.assertEqual([r['name'] for r in rows],
                         ['lever_1', 'lever_2', 'lever_3', 'lever_4', 'reverse_2', 'reverse_3', 'disc_left', 'disc_right'])
        self.assertEqual((len(vertices), len(indices)), (8 * 24, 8 * 36))
        self.assertEqual(sum(c.startswith('ATTR_manip_drag_rotate\thand') for c in commands), 6)
        self.assertEqual(sum(c.startswith('ATTR_manip_keyframe\t25\t') for c in commands), 4)
        self.assertEqual({c.split('\t')[2] for c in commands if c.startswith('ATTR_manip_command')}, set(THRUST_COMMANDS))
        self.assertEqual(sum(c == 'ANIM_begin' for c in commands), sum(c == 'ANIM_end' for c in commands))
        self.assertEqual(commands[-1], 'ATTR_draw_enable')
        self.assertFalse(any('drag_axis' in c or 'manip_wheel' in c for c in commands))
        json.dumps(rows)

    @unittest.skipUnless((AIRCRAFT / thrust.PEDESTAL).is_file(), 'aircraft not available')
    def test_real_objects_wrap_exact_source_ranges(self):
        for relative in (thrust.PEDESTAL, thrust.DECAL):
            parts = thrust.panel_parts(self.source, relative, self.motion)
            text = (AIRCRAFT / relative).read_text()
            if thrust.MOTION_MARKER in text:
                self.assertEqual(thrust.patch_panel(text, relative, parts), (text, []))
                continue
            patched, rows = thrust.patch_panel(text, relative, parts)
            self.assertEqual([r['indices'] for r in rows], [p['count'] for p in parts])
            self.assertEqual(geometry.vertex_payload(text), geometry.vertex_payload(patched))


if __name__ == '__main__':
    unittest.main()
