import json
import math
import tempfile
import unittest
from pathlib import Path

import cockpit_lever_geometry as geometry
import install_speedbrake_lever as installer
import speedbrake_lever_cockpit as speedbrake
from avionics_provider import AVIONICS_COMMANDS, AVIONICS_DATAREFS, SPEEDBRAKE_COMMANDS, SPEEDBRAKE_DATAREFS
from patch_cockpit_materials import DEFAULT_SOURCE

AIRCRAFT = Path(__file__).resolve().parents[1] / 'output/A380X-XP12'
ANIMATIONS = Path(__file__).resolve().parents[1] / 'config/animation_datarefs.json'


class ContractTests(unittest.TestCase):
    def test_contracts(self):
        self.assertEqual(SPEEDBRAKE_COMMANDS, tuple('a380x/xp12/speedbrake/' + n for n in (
            'extend_one', 'retract_one', 'arm_toggle', 'arm', 'retract')))
        self.assertEqual(SPEEDBRAKE_DATAREFS[0], speedbrake.PREFIX + 'lever_input')
        for name in ('lever_input', 'lever_anim', 'lift_anim', 'ground_spoilers_armed', 'handle_position',
                     'spoiler_1_deg', 'spoiler_8_deg', 'xp_handle_ratio'):
            self.assertIn(speedbrake.PREFIX + name, SPEEDBRAKE_DATAREFS)
        self.assertTrue(set(SPEEDBRAKE_DATAREFS) <= set(AVIONICS_DATAREFS))
        self.assertTrue(set(SPEEDBRAKE_COMMANDS) <= set(AVIONICS_COMMANDS))
        self.assertEqual(len(set(SPEEDBRAKE_DATAREFS)), len(SPEEDBRAKE_DATAREFS))

    def test_animation_mapping_follows_the_plugin(self):
        rules = [r for r in json.loads(ANIMATIONS.read_text())['mappings'] if 'lever_speed_brake' in r.get('pattern', '')]
        self.assertEqual(len(rules), 1)
        rule = rules[0]
        self.assertEqual(rule['dataref'], speedbrake.PREFIX + 'lever_anim')
        self.assertAlmostEqual(rule['source_start'], speedbrake.RET_KEY / speedbrake.FULL_KEY)
        self.assertEqual((rule['value_min'], rule['value_max'], rule['source_end']), (0, 1, 1))


class LeverMathTests(unittest.TestCase):
    MOTION = dict(handle=[k / 10 for k in range(11)], angles=[13.0 - 2.5 * k for k in range(11)], rest_angle=13.0,
                  normal=(0.1, 2.0, 32.0))

    def test_angle_interpolation_and_arm_extrapolation(self):
        m = self.MOTION
        self.assertAlmostEqual(speedbrake._lever_angle(m, 0.0), 13.0)
        self.assertAlmostEqual(speedbrake._lever_angle(m, 0.55), 13.0 - 2.5 * 5.5)
        self.assertAlmostEqual(speedbrake._lever_angle(m, 1.0), -12.0)
        self.assertAlmostEqual(speedbrake._lever_angle(m, -0.25), 13.0 + 2.5 * 2.5)

    def test_manipulator(self):
        rows = speedbrake.manipulator(self.MOTION)
        fields = rows[0].split('\t')
        self.assertEqual(fields[0], 'ATTR_manip_drag_rotate')
        self.assertEqual(fields[5:8], ['-1', '0', '0'])
        self.assertAlmostEqual(float(fields[8]), -6.25)        # ARM zone, beyond RET
        self.assertAlmostEqual(float(fields[9]), 25.0)         # FULL
        self.assertEqual(fields[10:15], ['0', '-0.25', '1', '0', '0'])
        self.assertEqual(fields[15:17], [speedbrake.PREFIX + 'lever_input', 'none'])
        frames = [r.split('\t') for r in rows[1:]]
        self.assertEqual([float(f[1]) for f in frames], [k / 10 for k in range(10)])
        self.assertEqual([round(float(f[2]), 9) for f in frames], [2.5 * k for k in range(10)])

    def test_rotate_x(self):
        v = speedbrake._rotate_x((0.0, 1.0, 0.0), 90.0)
        self.assertAlmostEqual(v[1], 0.0)
        self.assertAlmostEqual(v[2], 1.0)


class PedestalSyntheticTests(unittest.TestCase):
    def test_refuses_partial_previous_patch(self):
        with self.assertRaises(ValueError):
            speedbrake.rebind_pedestal('I\n800\nOBJ\n' + speedbrake.MOTION_MARKER + 'LEVER_SPEEDBRAKE\n', None, None)
        with self.assertRaises(ValueError):
            speedbrake.rebind_pedestal('I\n800\nOBJ\nANIM_trans_begin\ta380x/xp12/speedbrake/lever_anim\n', None, None)

    def test_idempotent_on_complete_patch(self):
        text = ('I\n800\nOBJ\n' + speedbrake.MOTION_MARKER + 'LEVER_SPEEDBRAKE\n' + speedbrake.BRUSH_MARKER + 'x 0\n'
                'ANIM_trans_begin\ta380x/xp12/speedbrake/lift_anim\n')
        self.assertEqual(speedbrake.rebind_pedestal(text, None, None), (text, {}))

    def test_controls_refuse_partial_patch(self):
        with self.assertRaises(ValueError):
            speedbrake.augment_controls('I\n800\nOBJ\n' + speedbrake.MARKER + '\n', None, None)
        text = 'I\n800\nOBJ\n' + speedbrake.MARKER + '\n' + speedbrake.CLICK_MARKER + 'lever\n'
        self.assertEqual(speedbrake.augment_controls(text, None, None), (text, []))


class InstallerTests(unittest.TestCase):
    def test_refuses_drift_and_skips_applied(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            aircraft, stage = root / 'aircraft', root / 'stage'
            rows = []
            for row_root, relative in sorted(installer.EXPECTED):
                target, staged = aircraft / relative, stage / row_root / relative
                for path, text in ((target, 'before ' + relative), (staged, 'after ' + relative)):
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(text)
                rows.append(dict(root=row_root, path=relative, before_sha256=installer.sha(target),
                                 after_sha256=installer.sha(staged)))
            (stage / 'staging-report.json').write_text(json.dumps(
                dict(format=1, marker=speedbrake.MARKER, aircraft=str(aircraft.resolve()), files=rows)))
            actions = installer.plan(aircraft.resolve(), stage.resolve())
            self.assertEqual([a['action'] for a in actions], ['replace', 'replace'])
            (aircraft / speedbrake.CONTROLS).write_text('after ' + speedbrake.CONTROLS)
            actions = installer.plan(aircraft.resolve(), stage.resolve())
            self.assertEqual({a['path']: a['action'] for a in actions}[speedbrake.CONTROLS], 'already-applied')
            (aircraft / speedbrake.PEDESTAL).write_text('changed by someone else')
            with self.assertRaises(ValueError):
                installer.plan(aircraft.resolve(), stage.resolve())
            with self.assertRaises(ValueError):
                installer.plan((root / 'other').resolve(), stage.resolve())
            (stage / 'aircraft' / speedbrake.CONTROLS).write_text('tampered')
            (aircraft / speedbrake.PEDESTAL).write_text('before ' + speedbrake.PEDESTAL)
            with self.assertRaises(ValueError):
                installer.plan(aircraft.resolve(), stage.resolve())


@unittest.skipUnless(DEFAULT_SOURCE.is_file(), 'pinned FBW cockpit glTF not available')
class SourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = geometry.Source(DEFAULT_SOURCE)
        cls.motion = speedbrake.motions(cls.source)

    def test_authored_lever_clip(self):
        lever = self.motion['lever']
        self.assertEqual(len(lever['times']), 20)
        self.assertAlmostEqual(lever['rest_angle'], 13.0598, places=3)
        self.assertAlmostEqual(lever['angles'][0], lever['rest_angle'], places=6)
        self.assertAlmostEqual(lever['angles'][-1], -11.6183, places=3)
        self.assertAlmostEqual(lever['half_angle'], 0.7936, places=3)     # key 14, the angular midpoint
        self.assertAlmostEqual(lever['half_angle'], (lever['angles'][0] + lever['angles'][-1]) / 2, delta=0.1)
        self.assertAlmostEqual(lever['lift_length_m'], 0.013711, places=5)  # ARM pull-up
        self.assertAlmostEqual(lever['lift_local'][1], lever['lift_length_m'], places=6)   # along the lever
        self.assertAlmostEqual(math.sqrt(sum(v * v for v in lever['push_local'])), speedbrake.PUSH_M, places=9)
        self.assertLess(lever['push_local'][1], 0.0)

    def test_brush_rig(self):
        rig = self.motion['brush']
        self.assertEqual([s['triangles'] for s in rig['strips']], [140, 140])
        for k, v in zip(rig['offset'], (0.14666, 2.38886, 32.17190)):
            self.assertAlmostEqual(k, v, places=4)
        self.assertLess(rig['joint_distance_max_m'], 0.003)
        moving = {j for j, keys in rig['deltas'].items() if any(any(c for c in d) for d in keys)}
        self.assertTrue({'SPD_LVR_RH_BONE9', 'SPD_LVR_LH_BONE9', 'SPD_LVR_RH_BONE4'} <= moving)
        self.assertFalse(moving & {'SPD_LVR_RH_SIDE', 'SPD_LVR_LH_SIDE', 'NEUTRAL_BONE_1', 'SPD_LVR_RH_BONE1'})
        for joint, keys in rig['deltas'].items():
            self.assertEqual(keys[speedbrake.RET_KEY], (0.0, 0.0, 0.0))
            self.assertLess(max(math.sqrt(sum(c * c for c in d)) for d in keys), 0.006)

    def test_click_target(self):
        vertices, indices, commands, row = speedbrake.click_geometry(self.source, self.motion['lever'])
        self.assertEqual((len(vertices), len(indices)), (24, 36))
        manip = [c for c in commands if c.startswith('ATTR_manip_drag_rotate')]
        self.assertEqual(len(manip), 1)
        self.assertEqual(sum(c.startswith('ATTR_manip_keyframe') for c in commands), 10)
        self.assertEqual(sum(c == 'ANIM_begin' for c in commands), sum(c == 'ANIM_end' for c in commands))
        self.assertIn('ANIM_trans_begin\t' + speedbrake.PREFIX + 'lift_anim', commands)
        self.assertEqual(row['range'], [-0.25, 1.0])
        json.dumps(row)

    @unittest.skipUnless((AIRCRAFT / speedbrake.PEDESTAL).is_file(), 'aircraft not available')
    def test_real_pedestal_rebind(self):
        text = (AIRCRAFT / speedbrake.PEDESTAL).read_text()
        if speedbrake.MOTION_MARKER in text:
            self.assertEqual(speedbrake.rebind_pedestal(text, self.source, self.motion), (text, {}))
            return
        patched, report = speedbrake.rebind_pedestal(text, self.source, self.motion)
        self.assertEqual(report['lever']['tris'], [38046, 1092])
        self.assertEqual(report['brushes']['triangles'], 280)
        self.assertLess(report['brushes']['max_translation_m'], 0.005)
        self.assertEqual(geometry.vertex_payload(text), geometry.vertex_payload(patched))
        self.assertEqual(geometry.tris_coverage(text), geometry.tris_coverage(patched))
        self.assertEqual(speedbrake.rebind_pedestal(patched, self.source, self.motion), (patched, {}))
        # The thrust and flaps patches are untouched.
        for marker in ('# A380X THRUST LEVERS V1 MOTION', '# A380X FLAPS LEVER V1 MOTION'):
            self.assertEqual(text.count(marker), patched.count(marker))
        lever = patched.split(speedbrake.MOTION_MARKER + 'LEVER_SPEEDBRAKE')[1].splitlines()[1:]
        words = [l.split()[0] for l in lever[:22]]
        self.assertEqual(words[:4], ['ANIM_begin', 'ANIM_trans', 'ANIM_rotate_begin', 'ANIM_rotate_key'])
        self.assertEqual(words[14:22], ['ANIM_rotate_end', 'ANIM_trans_begin', 'ANIM_trans_key', 'ANIM_trans_key',
                                        'ANIM_trans_key', 'ANIM_trans_end', 'TRIS', 'ANIM_end'])
        # Only the 26 empty joint blocks still name X-Plane's handle (they draw nothing).
        self.assertEqual(patched.count(speedbrake.NATIVE_HANDLE), 26)
        # Every brush triangle sits under the panel offset.
        brush = [l for l in patched.splitlines() if l.lstrip().startswith(speedbrake.BRUSH_MARKER)]
        self.assertEqual(len(brush), report['brushes']['blocks'])


if __name__ == '__main__':
    unittest.main()
