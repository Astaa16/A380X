import json
import math
import tempfile
import unittest
from pathlib import Path

import cockpit_lever_geometry as geometry
import flaps_lever_cockpit as flaps
import install_flaps_lever as installer
from avionics_provider import FLAPS_COMMANDS, FLAPS_DATAREFS
from patch_cockpit_materials import DEFAULT_SOURCE

AIRCRAFT = Path(__file__).resolve().parents[1] / 'output/A380X-XP12'


def acf_text(**overrides):
    """A minimal ACF with every changed key once, plus neighbours that must not change."""
    values = {key: '1.000000000' for key in flaps.ACF_CHANGES}
    values.update(overrides)
    lines = ['I', '1200 Version', 'ACF', '', 'PROPERTIES_BEGIN', 'P acf/_flap1_dn/count 12',
             'P acf/_flap1_dn/7 0.000000000', 'P _wing/0/_flap2/4 1']
    lines += [f'P {key} {value}' for key, value in sorted(values.items())]
    return '\n'.join(lines + ['PROPERTIES_END', '']) + '\n'


class AcfTests(unittest.TestCase):
    def test_schedule_is_fbw(self):
        c = flaps.ACF_CHANGES
        self.assertEqual([float(c[f'acf/_flap1_dn/{k}']) for k in range(7)], [0, 0, 8, 17, 17, 26, 33])
        self.assertEqual([c[f'acf/_flap2_dn/{k}'] for k in range(7)], [c[f'acf/_flap1_dn/{k}'] for k in range(7)])
        self.assertEqual([round(float(c[f'acf/_slat1_dn/{k}']) * 23, 6) for k in range(7)], [0, 20, 20, 20, 23, 23, 23])
        self.assertEqual(c['acf/_flap_detents'], '6')
        self.assertEqual((c['acf/_slat1_dn_max_deg'], c['acf/_slat_with_stall_EQ'], c['acf/_flap_extend_protect']),
                         ('23.000000000', '0', '0'))
        self.assertAlmostEqual(float(c['acf/_flap_ext_time']), 338.99 / (0.07 * 180 / math.pi * 314.98 / 140), places=6)
        self.assertAlmostEqual(float(c['acf/_slat_ext1_time']), 31.746646, places=5)
        self.assertEqual((float(c['acf/_Vfe1_kts']), float(c['acf/_Vfem_kts'])), (263.0, 182.0))

    def test_patch_changes_only_the_listed_values_and_is_idempotent(self):
        text = acf_text()
        patched, rows = flaps.patch_acf(text)
        self.assertEqual(len(rows), len(flaps.ACF_CHANGES))
        before, after = text.split('\n'), patched.split('\n')
        self.assertEqual(len(before), len(after))
        changed = [b for b, a in zip(before, after) if a != b]
        self.assertTrue(all(line.split(' ')[1] in flaps.ACF_CHANGES for line in changed))
        for keep in ('P acf/_flap1_dn/count 12', 'P acf/_flap1_dn/7 0.000000000', 'P _wing/0/_flap2/4 1'):
            self.assertIn(keep, after)
        again, rows = flaps.patch_acf(patched)
        self.assertEqual(again, patched)
        self.assertFalse(any(r['changed'] for r in rows))

    def test_refuses_missing_or_duplicate_keys(self):
        text = acf_text()
        with self.assertRaises(ValueError):
            flaps.patch_acf(text.replace('P acf/_slat2_dn_max_deg', 'P acf/_slat2_dn_max_degx'))
        with self.assertRaises(ValueError):
            flaps.patch_acf(text + 'P acf/_flap_detents 4\n')


WING = ('I\n800\nOBJ\nPOINT_COUNTS\t0\t0\t0\t0\n'
        'ANIM_begin\n\t\tANIM_trans_begin\tsim/flightmodel2/controls/slat1_deploy_ratio\n'
        '\t\tANIM_trans_key\t0\t0\t0\t0\n\t\tANIM_trans_end\n'
        '\t\tANIM_rotate_begin\t0\t0\t-1\tsim/flightmodel2/controls/flap1_deploy_ratio\n'
        '\t\tANIM_rotate_key\t0.5\t3\n\t\tANIM_rotate_end\n'
        '\t\tANIM_rotate_begin\t1\t0\t0\tsim/flightmodel2/wing/spoiler1_deg[0]\n'
        'ANIM_end\n')


class ExteriorTests(unittest.TestCase):
    def test_rebinds_only_the_flap_and_slat_tokens(self):
        patched, report = flaps.rebind_exterior(WING)
        self.assertEqual(report['replaced'], {'sim/flightmodel2/controls/flap1_deploy_ratio': 1,
                                              'sim/flightmodel2/controls/slat1_deploy_ratio': 1})
        self.assertIn('\t\tANIM_trans_begin\ta380x/xp12/flaps/slat_anim_ratio\n', patched)
        self.assertIn('\t\tANIM_rotate_begin\t0\t0\t-1\ta380x/xp12/flaps/flap_anim_ratio\n', patched)
        self.assertIn('sim/flightmodel2/wing/spoiler1_deg[0]', patched)
        self.assertEqual(len(patched.splitlines()), len(WING.splitlines()))
        again, report = flaps.rebind_exterior(patched)
        self.assertEqual(again, patched)
        self.assertEqual(report['already_bound'], {'a380x/xp12/flaps/flap_anim_ratio': 1,
                                                   'a380x/xp12/flaps/slat_anim_ratio': 1})

    def test_refuses_other_uses(self):
        with self.assertRaises(ValueError):
            flaps.rebind_exterior(WING + 'ANIM_hide\t0\t0.1\tsim/flightmodel2/controls/flap1_deploy_ratio\n')
        with self.assertRaises(ValueError):
            flaps.rebind_exterior('I\n800\nOBJ\nTRIS\t0\t0\n')

    def test_contracts(self):
        self.assertEqual(FLAPS_COMMANDS, ('a380x/xp12/flaps/lever_down', 'a380x/xp12/flaps/lever_up'))
        for name in ('lever_input', 'lift_input', 'lever_anim', 'lock_anim', 'flap_anim_ratio', 'slat_anim_ratio'):
            self.assertIn(flaps.PREFIX + name, FLAPS_DATAREFS)
        self.assertTrue(set(flaps.EXTERIOR_REBIND.values()) <= set(FLAPS_DATAREFS))
        ranges = list(flaps.DETENT_RANGES)
        self.assertEqual(ranges[0], (0.0, 0.0, 0.0))
        self.assertEqual(ranges[-1], (4.0, 4.0, 0.0))
        self.assertEqual([r for r in ranges if r[0] != r[1]], [(k, k + 1.0, 1.0) for k in (0.0, 1.0, 2.0, 3.0)])


class InstallerTests(unittest.TestCase):
    def test_refuses_drift_and_skips_applied(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            aircraft, stage, project = root / 'aircraft', root / 'stage', root / 'project' / 'snapshot.acf'
            rows = []
            for row_root, relative in sorted(installer.EXPECTED):
                target = aircraft / relative if row_root == 'aircraft' else project
                staged = stage / row_root / relative
                for path, text in ((target, 'before ' + relative), (staged, 'after ' + relative)):
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(text)
                rows.append(dict(root=row_root, path=relative, before_sha256=installer.sha(target),
                                 after_sha256=installer.sha(staged)))
            (stage / 'staging-report.json').write_text(json.dumps(
                dict(format=1, marker=flaps.MARKER, aircraft=str(aircraft.resolve()), project_acf=str(project.resolve()),
                     files=rows)))
            actions = installer.plan(aircraft.resolve(), project.resolve(), stage.resolve())
            self.assertEqual([a['action'] for a in actions], ['replace'] * len(installer.EXPECTED))
            (aircraft / flaps.ACF).write_text('after ' + flaps.ACF)
            actions = installer.plan(aircraft.resolve(), project.resolve(), stage.resolve())
            self.assertEqual({a['path']: a['action'] for a in actions}[flaps.ACF], 'already-applied')
            with self.assertRaises(ValueError):
                installer.plan(aircraft.resolve(), (root / 'other.acf').resolve(), stage.resolve())
            (aircraft / flaps.PEDESTAL).write_text('changed by someone else')
            with self.assertRaises(ValueError):
                installer.plan(aircraft.resolve(), project.resolve(), stage.resolve())


@unittest.skipUnless(DEFAULT_SOURCE.is_file(), 'pinned FBW cockpit glTF not available')
class SourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = geometry.Source(DEFAULT_SOURCE)
        cls.motion = flaps.motions(cls.source)

    def test_authored_clips(self):
        lever, lock = self.motion['lever'], self.motion['lock']
        self.assertEqual(lever['values'], [0.0, 1.0, 2.0, 3.0, 4.0])
        for pose, expected in zip(lever['poses'], (0.0, 13.0676, 26.2141, 40.8019, 53.4348)):
            self.assertAlmostEqual(pose, expected, places=3)
        self.assertEqual(lever['axis'], (-1.0, 0.0, 0.0))
        self.assertAlmostEqual(flaps.lift_length(self.motion), 0.0078427, places=6)
        self.assertEqual(lock['values'], [0.0, 1.0])

    def test_click_target(self):
        vertices, indices, commands, row = flaps.click_geometry(self.source, self.motion)
        self.assertEqual((len(vertices), len(indices)), (24, 36))
        manip = [c for c in commands if c.startswith('ATTR_manip_drag_rotate')]
        self.assertEqual(len(manip), 1)
        fields = manip[0].split('\t')
        self.assertEqual(fields[15:17], [flaps.PREFIX + 'lever_input', flaps.PREFIX + 'lift_input'])
        self.assertEqual(fields[12:15], ['4', '0', '1'])                  # v1max, v2 range
        self.assertAlmostEqual(float(fields[10]), 0.0078427, places=6)  # lift
        self.assertEqual([c.split('\t')[1] for c in commands if c.startswith('ATTR_manip_keyframe')], ['1', '2', '3'])
        self.assertEqual(sum(c.startswith('ATTR_axis_detent_range') for c in commands), 9)
        self.assertEqual(sum(c == 'ANIM_begin' for c in commands), sum(c == 'ANIM_end' for c in commands))
        self.assertEqual(row['keyframes'], 3)
        json.dumps(row)

    @unittest.skipUnless((AIRCRAFT / flaps.PEDESTAL).is_file(), 'aircraft not available')
    def test_real_pedestal_rebind(self):
        text = (AIRCRAFT / flaps.PEDESTAL).read_text()
        if flaps.MOTION_MARKER in text:
            self.assertEqual(flaps.rebind_pedestal(text, self.source, self.motion), (text, []))
            return
        patched, rows = flaps.rebind_pedestal(text, self.source, self.motion)
        self.assertAlmostEqual(rows[0]['exported_pose_deg'], 26.174589, places=4)
        self.assertEqual([(p['name'], p['indices']) for p in rows[0]['parts']],
                         [('LEVER_FLAPS', 1026), ('LEVER_FLAPS_LOCK_Cylinder.001', 312), ('LOCK_FLAPS', 3816)])
        self.assertNotIn(flaps.NATIVE_HANDLE, patched)
        self.assertEqual(geometry.vertex_payload(text), geometry.vertex_payload(patched))
        self.assertEqual(geometry.tris_coverage(text), geometry.tris_coverage(patched))
        self.assertEqual(flaps.rebind_pedestal(patched, self.source, self.motion), (patched, []))
        lock = patched.split(flaps.MOTION_MARKER + 'LOCK_FLAPS')[1].splitlines()[:7]
        self.assertEqual([l.split()[0] for l in lock[1:]],
                         ['ANIM_begin', 'ANIM_trans_begin', 'ANIM_trans_key', 'ANIM_trans_key', 'ANIM_trans_end', 'TRIS'])


if __name__ == '__main__':
    unittest.main()
