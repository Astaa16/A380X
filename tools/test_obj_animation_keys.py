#!/usr/bin/env python3
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from normalize_obj_animation_keys import normalize_animation_stream, normalize_obj_animation_keys


def normalize(text):
    output = io.StringIO()
    stats = normalize_animation_stream(text.splitlines(keepends=True), output)
    return output.getvalue(), stats


class AnimationKeyTests(unittest.TestCase):
    def test_sort_rows_preserves_axis_transforms_comments_and_geometry(self):
        text = '''VT 1 2 3 0 0 1 0.4 0.5
ANIM_begin
 ANIM_rotate_begin 0 1 0 sim/trim
 # high
 ANIM_rotate_key 1 -10 # upper endpoint
 ANIM_rotate_key 0 0
 # low
 ANIM_rotate_key -1 10
 # trailing comment
 ANIM_rotate_end
 ANIM_keyframe_loop 4
 TRIS 0 3
ANIM_end
'''
        result, stats = normalize(text)
        self.assertEqual(stats['reordered_tables'], 1)
        self.assertIn('ANIM_rotate_begin 0 1 0 sim/trim', result)
        self.assertLess(result.index('ANIM_rotate_key -1 10'), result.index('ANIM_rotate_key 0 0'))
        self.assertLess(result.index('ANIM_rotate_key 0 0'), result.index('ANIM_rotate_key 1 -10'))
        self.assertEqual(sorted(text.splitlines()), sorted(result.splitlines()))
        self.assertEqual(normalize(result)[0], result)
        self.assertEqual(normalize(result)[1]['reordered_tables'], 0)

    def test_translation_rows_remain_paired(self):
        result, _ = normalize('ANIM_trans_begin sim/x\nANIM_trans_key 2 9 8 7\nANIM_trans_key -2 1 2 3\nANIM_trans_end\n')
        self.assertEqual(result, 'ANIM_trans_begin sim/x\nANIM_trans_key -2 1 2 3\nANIM_trans_key 2 9 8 7\nANIM_trans_end\n')

    def test_identical_duplicate_deduplicated_without_lost_comments(self):
        result, stats = normalize('ANIM_rotate_begin 0 0 1 sim/x\nANIM_rotate_key 0 5 # first\n# duplicate\nANIM_rotate_key 0.0 5.0 # second\nANIM_rotate_end\n')
        self.assertEqual(stats['equal_duplicate_keys_removed'], 1)
        self.assertEqual(result.count('ANIM_rotate_key'), 1)
        self.assertIn('# first', result)
        self.assertIn('# duplicate', result)
        self.assertIn('# second', result)

    def test_rejects_ambiguous_and_malformed_tables(self):
        for content in ('ANIM_rotate_key 0 5\nANIM_rotate_key 0 6\nANIM_rotate_end\n',
                        'ANIM_rotate_key nan 5\nANIM_rotate_end\n',
                        'ANIM_rotate_key 0 5\n', 'TRIS 0 3\nANIM_rotate_end\n',
                        'ANIM_trans_key 0 1 2 3\nANIM_rotate_end\n', 'ANIM_rotate_end\n'):
            with self.subTest(content=content), self.assertRaises(ValueError):
                normalize('ANIM_rotate_begin 0 0 1 sim/x\n' + content)

    def test_failed_file_rewrite_preserves_source(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.obj'
            raw = b'ANIM_rotate_begin 0 0 1 sim/x\nANIM_rotate_key 0 1\nANIM_rotate_key 0 2\nANIM_rotate_end\n'
            path.write_bytes(raw)
            with self.assertRaises(ValueError):
                normalize_obj_animation_keys(path)
            self.assertEqual(path.read_bytes(), raw)
            self.assertEqual(list(path.parent.iterdir()), [path])

    def test_crlf_and_noop_file_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'fixture.obj'
            raw = b'ANIM_trans_begin sim/x\r\nANIM_trans_key 0 1 2 3\r\nANIM_trans_key 1 4 5 6\r\nANIM_trans_end\r\n'
            path.write_bytes(raw)
            before = path.stat().st_mtime_ns
            self.assertEqual(normalize_obj_animation_keys(path)['reordered_tables'], 0)
            self.assertEqual(path.read_bytes(), raw)
            self.assertEqual(path.stat().st_mtime_ns, before)


if __name__ == '__main__':
    unittest.main()
