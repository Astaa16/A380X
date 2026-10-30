"""The SD census (tools/sd_census.json) is fully classified, matches what a380x_sd_data.inc
actually publishes for a full mocked aircraft state, and docs/SD_PAGES.md is up to date."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import sd_pages_census as census

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / 'native/avionics'
HEADERS = ROOT / 'build/sdk/SDK/CHeaders/XPLM'


class SdCensus(unittest.TestCase):
    def test_every_variable_is_classified(self):
        rows = census.load_census()
        self.assertGreater(len(rows), 700)
        for row in rows:
            census.classify(row)
        self.assertEqual(set(row['page'] for row in rows), set(census.PAGE_ORDER))

    def test_census_matches_native_publisher(self):
        clang = shutil.which('clang')
        if not clang or not (HEADERS / 'XPLMDataAccess.h').exists():
            self.skipTest('clang or the X-Plane SDK headers are not available')
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'test_sd_data'
            subprocess.run([clang, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations',
                            '-DAPL=1', '-DXPLM200=1', '-DXPLM210=1', '-DXPLM300=1', '-DXPLM301=1', '-DXPLM400=1',
                            '-DXPLM410=1', '-I', str(HEADERS), str(NATIVE / 'test_sd_data.c'), '-lm', '-o', str(binary)],
                           check=True)
            frame = json.loads(subprocess.run([str(binary), '--dump'], check=True, capture_output=True,
                                              text=True).stdout)
        for row in census.load_census():
            kind, _, check = census.classify(row)
            key = census.frame_key(row)
            if kind in ('live', 'derived', 'ncd') and check:
                self.assertIn(key, frame, '%s %s is %s but not published' % (row['page'], key, kind))
            elif kind in ('missing', 'bundle', 'n/a'):
                self.assertNotIn(key, frame, '%s %s is %s but published' % (row['page'], key, kind))

    def test_doc_is_current(self):
        self.assertTrue(census.doc_is_current(), 'run: python3 tools/sd_pages_census.py --write')


if __name__ == '__main__':
    unittest.main()
