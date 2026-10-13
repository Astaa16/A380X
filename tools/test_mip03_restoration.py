import unittest

from build_development_aircraft import object_attachment
from install_mip03_restoration import attach


class AttachmentTests(unittest.TestCase):
    def setUp(self):
        self.reference = 'interior/cockpit-panels/panel_a380_cockpit_mip04_4k.obj'
        self.new = 'interior/cockpit-panels/panel_a380_cockpit_mip03_4k.obj'
        self.block = ''.join(object_attachment(0, self.reference, x=0, y=0,
                           z=115.108, psi=180, obj_flags=13, is_internal=True))
        self.acf = 'I\n1200\nACF\nP acf/_name Existing aircraft\n' + self.block + 'P _obja/count 1\n'

    def test_only_adds_exact_clone_and_increments_count(self):
        result, index = attach(self.acf, self.new)
        self.assertEqual(index, 1)
        added = self.block.replace('_obja/0/', '_obja/1/').replace(self.reference, self.new)
        self.assertEqual(result.replace(added, '').replace('_obja/count 2', '_obja/count 1'), self.acf)

    def test_repeated_attachment_is_noop(self):
        once, index = attach(self.acf, self.new)
        self.assertEqual(attach(once, self.new), (once, index))

    def test_refuses_inconsistent_count(self):
        with self.assertRaises(ValueError):
            attach(self.acf.replace('_obja/count 1', '_obja/count 2'), self.new)


if __name__ == '__main__':
    unittest.main()
