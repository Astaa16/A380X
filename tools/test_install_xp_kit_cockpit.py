"""Exercise atomic installer writes on isolated temporary files, never the aircraft."""
from pathlib import Path
import tempfile
import unittest

from install_xp_kit_cockpit import copy_file, write_text


class InstallerWrites(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='a380x-installer-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_unchanged_text_preserves_file(self):
        target = self.root / 'cockpit.obj'
        target.write_text('unchanged cockpit\n')
        before = target.stat()
        write_text(target, 'unchanged cockpit\n')
        after = target.stat()
        self.assertEqual((before.st_ino, before.st_mtime_ns), (after.st_ino, after.st_mtime_ns))

    def test_changed_text_is_replaced_and_temporary_removed(self):
        target = self.root / 'provider.json'
        target.write_text('old\n')
        before = target.stat().st_ino
        write_text(target, 'new\n')
        self.assertEqual(target.read_text(), 'new\n')
        self.assertNotEqual(target.stat().st_ino, before)
        self.assertEqual(list(self.root.glob('.a380x-install-*')), [])

    def test_new_text_file(self):
        target = self.root / 'new.json'
        write_text(target, 'new\n')
        self.assertEqual(target.read_text(), 'new\n')

    def test_unchanged_binary_preserves_file(self):
        source, target = self.root / 'source.xpl', self.root / 'installed.xpl'
        source.write_bytes(b'native-plugin')
        target.write_bytes(source.read_bytes())
        before = target.stat()
        copy_file(source, target)
        after = target.stat()
        self.assertEqual((before.st_ino, before.st_mtime_ns), (after.st_ino, after.st_mtime_ns))

    def test_changed_binary_copies_contents_and_mode(self):
        source, target = self.root / 'source.xpl', self.root / 'installed.xpl'
        source.write_bytes(b'verified-native-plugin')
        source.chmod(0o755)
        target.write_bytes(b'old-plugin')
        copy_file(source, target)
        self.assertEqual(source.read_bytes(), target.read_bytes())
        self.assertEqual(target.stat().st_mode & 0o777, 0o755)
        self.assertEqual(list(self.root.glob('.a380x-install-*')), [])


if __name__ == '__main__':
    unittest.main()
