"""Check the real C/Python shared-memory ABI and independent display buffers."""
import ctypes as C
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from xp_kit_transport import Shared, VERSION, view_buffer, view_dimensions

ROOT = Path(__file__).resolve().parents[1]


class TransportTests(unittest.TestCase):
    def test_all_view_buffers_have_independent_native_resolution(self):
        previous_end = 0
        for view in range(7):
            width, height = view_dimensions(view)
            self.assertEqual((width, height), (384, 512) if view < 2 else (768, 1024))
            start, length = view_buffer(view)
            self.assertEqual(length, width * height * 4)
            self.assertGreaterEqual(start, previous_end)
            previous_end = start + length
        self.assertLessEqual(previous_end, C.sizeof(Shared))

    def test_python_matches_compiled_c_abi(self):
        compiler = shutil.which('clang') or shutil.which('cc')
        if not compiler:
            self.skipTest('C compiler unavailable')
        fields = ['pixels', 'nd_pixels', 'mfd_pixels', 'mfd', 'sd', 'nd_profile']
        prints = ','.join('\\"%s\\":%%zu' % field for field in fields)
        offsets = ','.join('offsetof(A380XXpKitShared,%s)' % field for field in fields)
        source = ('#include <stdio.h>\n#include <stddef.h>\n#include "a380x_xpkit_shared.h"\n'
                  'int main(void){printf("{\\"version\\":%u,\\"size\\":%zu,' + prints + '}\\n",'
                  'XPKIT_VERSION,sizeof(A380XXpKitShared),' + offsets + ');return 0;}\n')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'layout.c').write_text(source)
            subprocess.run([compiler, '-std=c11', '-I', str(ROOT / 'native/avionics'),
                            str(path / 'layout.c'), '-o', str(path / 'layout')], check=True, capture_output=True)
            actual = json.loads(subprocess.check_output([str(path / 'layout')]))
        self.assertEqual(actual.pop('version'), VERSION)
        self.assertEqual(actual.pop('size'), C.sizeof(Shared))
        self.assertEqual(actual, {field: getattr(Shared, field).offset for field in fields})


if __name__ == '__main__':
    unittest.main()
