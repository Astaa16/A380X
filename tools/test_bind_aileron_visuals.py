from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bind_aileron_visuals import (CUSTOM_REFS, EXPECTED_FILES, OLD_REF, bind_directory,
                                  rebind_object)


def fixture(signatures=((604, 5472), (532,), (524,)), newline=b'\n'):
    count = sum(sum(signature) for signature in signatures) * 3
    lines = [b'I', b'800', b'OBJ', b'# untouched UTF-8: \xc3\xa9', b'TEXTURE wing.png',
             f'POINT_COUNTS 3 0 0 {count}'.encode(),
             b'VT 0 0 0 0 1 0 0 0', b'VT 1 0 0 0 1 0 1 0', b'VT 0 0 1 0 1 0 0 1']
    lines += [b'IDX10 0 1 2 0 1 2 0 1 2 0'] * (count // 10)
    lines += [b'IDX 0'] * (count % 10)
    offset = 0
    for signature in signatures:
        lines.append(b'ANIM_begin')
        lines.append(b'  ANIM_trans 1 2 3 1 2 3')
        for axis in (b'0 0 -1', b'0 1 -0', b'1 0 -0'):
            lines.append(b'\t ANIM_rotate_begin\t' + axis + b'\t' + OLD_REF + b'  # preserve comment')
            lines += [f' ANIM_rotate_key {(-1 + index / 100):.2f} {index / 10:.2f}'.encode() for index in range(201)]
            lines.append(b' ANIM_rotate_end')
        # Descendant scopes must be included in the owning-bone signature.
        lines.append(b' ANIM_begin')
        for triangles in signature:
            lines.append(f'  TRIS {offset} {triangles * 3}'.encode())
            offset += triangles * 3
        lines += [b' ANIM_end', b'ANIM_end']
    return newline.join(lines) + newline


class RebindAileronTests(unittest.TestCase):
    def test_each_side_changes_only_nine_reference_tokens(self):
        for filename, side in [('a380x_wing1.obj', 'l'), ('a380x_wing2.obj', 'r')]:
            with self.subTest(filename=filename):
                data = fixture()
                output, report = rebind_object(data, filename)
                self.assertEqual(report['header_token_changes'], 9)
                self.assertEqual(len(report['scopes']), 3)
                self.assertEqual(report['triangles'], 7132)
                restored = output
                for panel in ('inboard', 'midboard', 'outboard'):
                    ref = f'a380x/xp12/visual/aileron_{side}_{panel}_ratio'.encode()
                    self.assertEqual(output.count(ref), 3)
                    restored = restored.replace(ref, OLD_REF)
                self.assertEqual(restored, data)
                self.assertEqual({s['bone'] for s in report['scopes']},
                                 {f'WING_AILERON_{number}.{side.upper()}' for number in (12, 13, 14)})

    def test_crlf_whitespace_comments_and_unrelated_bytes_preserved(self):
        data = fixture(newline=b'\r\n')
        output, _ = rebind_object(data, 'a380x_wing1.obj')
        restored = output
        for ref in CUSTOM_REFS:
            restored = restored.replace(ref, OLD_REF)
        self.assertEqual(restored, data)
        self.assertEqual(data.count(b'\r\n'), output.count(b'\r\n'))

    def test_unused_roll_scope_preserved(self):
        original = fixture()
        start = original.index(b'ANIM_begin')
        empty_scope = original[start:].split(b' ANIM_begin', 1)[0] + b'ANIM_end\n'
        data = original + empty_scope
        output, report = rebind_object(data, 'a380x_wing1.obj')
        self.assertTrue(output.endswith(empty_scope))
        self.assertEqual(report['header_token_changes'], 9)
        self.assertEqual(output.count(OLD_REF), 3)

    def test_nine_unrelated_objects_are_identical(self):
        data = fixture(())
        for name in EXPECTED_FILES - {'a380x_wing1.obj', 'a380x_wing2.obj'}:
            output, report = rebind_object(data, name)
            self.assertEqual(output, data)
            self.assertEqual(report['source_sha256'], report['output_sha256'])

    def test_unexpected_filename_fails(self):
        with self.assertRaisesRegex(ValueError, 'name'):
            rebind_object(fixture(), 'another-wing.obj')

    def test_missing_duplicate_and_extra_draw_signatures_fail(self):
        for signatures in (((524,), (532,)), ((524,), (532,), (532,)),
                           ((524,), (532,), (604,)), ((524,), (532,), (604, 5472), (1,))):
            with self.subTest(signatures=signatures), self.assertRaises(ValueError):
                rebind_object(fixture(signatures), 'a380x_wing1.obj')

    def test_no_draw_bearing_roll_scope_allowed_in_other_objects(self):
        with self.assertRaisesRegex(ValueError, 'draw-bearing'):
            rebind_object(fixture(), 'a380x_wing1_2.obj')

    def test_partial_or_complete_existing_binding_fails(self):
        data = fixture()
        for old, new in [(OLD_REF, b'a380x/xp12/visual/aileron_l_inboard_ratio'),
                         (OLD_REF, b'a380x/xp12/visual/aileron_wrong_ratio')]:
            with self.assertRaisesRegex(ValueError, 'already or partially'):
                rebind_object(data.replace(old, new, 1), 'a380x_wing1.obj')
        output, _ = rebind_object(data, 'a380x_wing1.obj')
        with self.assertRaisesRegex(ValueError, 'already or partially'):
            rebind_object(output, 'a380x_wing1.obj')

    def test_key_count_order_range_and_finiteness_are_checked(self):
        data = fixture()
        mutations = [data.replace(b' ANIM_rotate_key -0.99 0.10\n', b'', 1),
                     data.replace(b'ANIM_rotate_key -0.99', b'ANIM_rotate_key -1.00', 1),
                     data.replace(b'ANIM_rotate_key -1.00', b'ANIM_rotate_key -1.01', 1),
                     data.replace(b'ANIM_rotate_key 1.00', b'ANIM_rotate_key 1.01', 1),
                     data.replace(b'ANIM_rotate_key -0.99 0.10', b'ANIM_rotate_key -0.99 nan', 1),
                     data.replace(b'ANIM_rotate_key -0.99 0.10', b'ANIM_rotate_key inf 0.10', 1)]
        for changed in mutations:
            with self.subTest(), self.assertRaises(ValueError):
                rebind_object(changed, 'a380x_wing1.obj')

    def test_wrong_axis_and_unexpected_inline_roll_forms_fail(self):
        data = fixture()
        with self.assertRaisesRegex(ValueError, 'Euler-axis'):
            rebind_object(data.replace(b'0 0 -1\t', b'0 0 1\t', 1), 'a380x_wing1.obj')
        inline = b'ANIM_begin\nANIM_rotate 1 0 0 0 1 -1 1 ' + OLD_REF + b'\nANIM_end\n'
        with self.assertRaisesRegex(ValueError, 'animation form'):
            rebind_object(data + inline, 'a380x_wing1.obj')

    def test_looping_roll_tables_fail_closed(self):
        data = fixture().replace(b'ANIM_rotate_end\n', b'ANIM_rotate_end\nANIM_keyframe_loop 2\n', 1)
        with self.assertRaisesRegex(ValueError, 'looping'):
            rebind_object(data, 'a380x_wing1.obj')

    def test_malformed_scopes_tables_geometry_and_indices_fail(self):
        data = fixture()
        for altered in (data + b'ANIM_begin\n', b'ANIM_end\n' + data,
                        data.replace(b'ANIM_rotate_end', b'ANIM_trans_end', 1),
                        data.replace(b'ANIM_rotate_key -0.99 0.10', b'ANIM_begin', 1),
                        data.replace(b'POINT_COUNTS 3', b'POINT_COUNTS 4', 1),
                        data.replace(b'IDX10 0', b'IDX10 -1', 1),
                        data.replace(b'IDX10 0', b'IDX10 3', 1),
                        data.replace(b'TRIS 0 1812', b'TRIS 900000 1812', 1)):
            with self.subTest(), self.assertRaises(ValueError):
                rebind_object(altered, 'a380x_wing1.obj')

    def make_directory(self, root):
        source = root / 'source'
        source.mkdir()
        for name in EXPECTED_FILES:
            (source / name).write_bytes(fixture() if name in ('a380x_wing1.obj', 'a380x_wing2.obj') else fixture(()))
        return source

    def test_directory_contract_and_source_recovery_copies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = self.make_directory(root)
            original = {p.name: p.read_bytes() for p in source.iterdir()}
            output, report = root / 'output', root / 'audit.json'
            result = bind_directory(source, output, report)
            self.assertEqual(result['header_token_changes'], 18)
            self.assertEqual({p.name for p in output.iterdir()}, EXPECTED_FILES)
            self.assertEqual(original, {p.name: p.read_bytes() for p in source.iterdir()})
            self.assertTrue(report.is_file())
            with self.assertRaisesRegex(ValueError, 'new directory'):
                bind_directory(source, output, root / 'second.json')

    def test_wrong_bundle_name_missing_file_and_invalid_source_do_not_stage(self):
        for mode in ('missing', 'extra', 'bad_scope'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                source = self.make_directory(root)
                if mode == 'missing':
                    (source / 'a380x_fans.obj').unlink()
                elif mode == 'extra':
                    (source / 'unexpected.OBJ').write_bytes(fixture(()))
                else:
                    (source / 'a380x_wing1.obj').write_bytes(fixture(((524,),)))
                with self.assertRaises(ValueError):
                    bind_directory(source, root / 'output', root / 'audit.json')
                self.assertFalse((root / 'output').exists())
                self.assertFalse((root / 'audit.json').exists())

    def test_in_place_nested_and_report_overwrite_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = self.make_directory(root)
            for output, report in ((source, root / 'r.json'), (source / 'nested', root / 'r.json'),
                                   (root / 'output', source / 'r.json'),
                                   (root / 'output', root / 'output/a380x_wing1.obj')):
                with self.subTest(output=output, report=report), self.assertRaises(ValueError):
                    bind_directory(source, output, report)
            report = root / 'exists.json'
            report.write_text('keep me')
            with self.assertRaisesRegex(ValueError, 'new file'):
                bind_directory(source, root / 'output', report)
            self.assertEqual(report.read_text(), 'keep me')


if __name__ == '__main__':
    unittest.main()
