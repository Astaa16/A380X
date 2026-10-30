"""ISIS 1 cockpit-object augment: append-only, idempotent, contract-complete (no aircraft files)."""
import re
import unittest

from isis_cockpit_device import (ISIS_COMMANDS, ISIS_DEVICE, ISIS2_DEVICE, MARKER, MARKER2, augment_controls,
                                 augment_controls_all, augment_screen, augment_screens_all, screen_quad)

BASE = '\n'.join(['I', '800', 'OBJ', '', 'POINT_COUNTS\t3\t0\t0\t3',
                  'VT\t0\t0\t0\t0\t0\t-1\t0\t0', 'VT\t1\t0\t0\t0\t0\t-1\t1\t0', 'VT\t0\t1\t0\t0\t0\t-1\t0\t1',
                  'IDX\t0', 'IDX\t1', 'IDX\t2', 'TRIS\t0\t3', '']) + '\n'


def counts(text):
    vt = sum(line.startswith('VT\t') for line in text.splitlines())
    idx = sum(len(line.split()) - 1 for line in text.splitlines() if re.match(r'^IDX(?:10)?\s', line))
    declared = list(map(int, next(l for l in text.splitlines() if l.startswith('POINT_COUNTS')).split()[1:]))
    return vt, idx, declared


class IsisAugment(unittest.TestCase):
    def test_screen_append_only_and_idempotent(self):
        text = augment_screen(BASE)
        self.assertEqual(augment_screen(text), text)
        self.assertIn(MARKER, text)
        vt, idx, declared = counts(text)
        self.assertEqual((vt, idx), (declared[0], declared[3]))
        self.assertIn(f'ATTR_cockpit_device\t{ISIS_DEVICE}\t1\t21\t1', text)
        self.assertNotIn('ATTR_manip_', text)
        for line in BASE.splitlines():
            if not line.startswith('POINT_COUNTS'):
                self.assertIn(line, text.splitlines())
        self.assertIn('TRIS\t3\t6', text)

    def test_controls_have_touch_twin_and_every_command(self):
        text = augment_controls_all(BASE)
        self.assertEqual(augment_controls_all(text), text)
        self.assertEqual(augment_controls(augment_controls(BASE, 1), 2), text)
        vt, idx, declared = counts(text)
        self.assertEqual((vt, idx), (declared[0], declared[3]))
        self.assertIn(f'ATTR_manip_device\thand\t{ISIS_DEVICE}\t', text)
        self.assertIn(f'ATTR_manip_device\thand\t{ISIS2_DEVICE}\t', text)
        self.assertIn(f'ATTR_cockpit_device\t{ISIS2_DEVICE}\t1\t22\t1', text)
        for command in ISIS_COMMANDS:
            if not command.endswith('/popup'):
                self.assertIn(command, text)
        tail = text.split(MARKER, 1)[1]
        # Hit boxes are never drawn and never tagged as device surfaces.
        self.assertLess(tail.index('ATTR_draw_disable'), tail.index('ATTR_manip_command\t'))
        self.assertLess(tail.index('ATTR_no_cockpit', tail.index('ATTR_manip_device')), tail.index('ATTR_manip_command\t'))

    def test_isis2_screen_and_stale_isis1_tooltip(self):
        text = augment_screens_all(BASE)
        self.assertIn(MARKER2, text)
        self.assertIn(f'ATTR_cockpit_device\t{ISIS2_DEVICE}\t1\t22\t1', text)
        self.assertIn('ANIM_hide\t-0.5\t0.5\ta380x/xp12/isis2/device_active', text)
        old = augment_controls(BASE).replace('ISIS 1 - MODE (swap SFD / SND)', 'ISIS - MODE (bugs page)')
        upgraded = augment_controls(old)
        self.assertNotIn('bugs page', upgraded)
        self.assertEqual(len(upgraded), len(old) + len('ISIS 1 - MODE (swap SFD / SND)') - len('ISIS - MODE (bugs page)'))

    def test_screen_quad_faces_crew_with_upright_uvs(self):
        rows = [list(map(float, row.split()[1:])) for row in screen_quad(0.002)]
        self.assertEqual([tuple(r[6:]) for r in rows], [(0, 0), (0, 1), (1, 1), (1, 0)])
        self.assertGreater(rows[0][0], rows[3][0])   # seated-view left is +X before the attachment
        self.assertLess(rows[0][5], 0)               # normal toward the crew (-Z)
        self.assertGreater(rows[1][1], rows[0][1])   # top above bottom


if __name__ == '__main__':
    unittest.main()
