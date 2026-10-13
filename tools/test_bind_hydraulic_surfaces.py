"""Guard staging and whole-aircraft provider detection for hydraulic surfaces."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from bind_hydraulic_surfaces import (AILERONS, EXPECTED_FILES, PREFIX, SPOILERS, TAIL,
                                     bind_directory, expected_tables, rebind_object)


def fixture(group, name, newline=b'\n'):
    lines = [b'I', b'800', b'OBJ', b'# keep UTF-8: \xc3\xa9', b'POINT_COUNTS 3 0 0 3',
             b'VT 0 0 0 0 1 0 0 0', b'VT 1 0 0 0 1 0 1 0', b'VT 0 0 1 0 1 0 0 1',
             b'IDX 0', b'IDX 1', b'IDX 2', b'ANIM_begin']
    for (ref, command, key_count, lower, upper), table_count in expected_tables(group, name).items():
        for unused in range(table_count):
            header = command + (' 1 0 0' if command == 'ANIM_rotate_begin' else '') + '\t' + ref
            lines.append(header.encode() + b' # keep comment')
            for index in range(key_count):
                value = lower + (upper - lower) * index / (key_count - 1)
                pose = f'{index / 10:.8f}' + (' 0 0' if command == 'ANIM_trans_begin' else '')
                lines.append(f"{command.replace('_begin', '_key')} {value:.8f} {pose}".encode())
            lines.append(command.replace('_begin', '_end').encode())
    lines += [b'TRIS 0 3', b'ANIM_end']
    return newline.join(lines) + newline


class BindingTests(unittest.TestCase):
    def test_every_object_preserves_all_bytes_except_expected_tokens(self):
        inverse = {target.encode(): ref.encode() for mapping in (AILERONS, TAIL, SPOILERS)
                   for ref, target in mapping.items()}
        total = 0
        for group, names in EXPECTED_FILES.items():
            for name in names:
                with self.subTest(group=group, name=name):
                    source = fixture(group, name, b'\r\n')
                    output, record = rebind_object(source, group, name)
                    restored = output
                    for target, original in inverse.items():
                        restored = restored.replace(target, original)
                    self.assertEqual(restored, source)
                    self.assertTrue(record['all_other_bytes_preserved'])
                    self.assertEqual(output.count(b'\r\n'), source.count(b'\r\n'))
                    total += record['header_token_changes']
        self.assertEqual(total, 510)

    def test_unrelated_trim_and_comment_bytes_remain_untouched(self):
        source = fixture('fuselage', 'a380x_fuse4.obj')
        extra = (b'ANIM_begin\nANIM_rotate_begin 1 0 0 sim/flightmodel2/controls/stabilizer_deflection_degrees\n'
                 b'ANIM_rotate_key -10 10\nANIM_rotate_key 2 -2\nANIM_rotate_end\nANIM_end\n'
                 b'# sim/joystick/yoke_pitch_ratio remains in this comment\n')
        output, _ = rebind_object(source + extra, 'fuselage', 'a380x_fuse4.obj')
        self.assertTrue(output.endswith(extra))

    def test_rejects_cockpit_and_wrong_exterior_file(self):
        for group, name in [('cockpit', 'a380x_fuse4.obj'), ('fuselage', 'cockpit.obj')]:
            with self.assertRaisesRegex(ValueError, 'Unexpected exterior'):
                rebind_object(b'', group, name)

    def test_rejects_duplicate_missing_bad_domain_and_partial_binding(self):
        name = 'a380x_fuse4.obj'
        source = fixture('fuselage', name)
        ref = next(iter(TAIL)).encode()
        for changed in (source.replace(ref, b'unexpected/ref', 1),
                        source.replace(ref, TAIL[next(iter(TAIL))].encode(), 1),
                        source.replace(b'ANIM_rotate_key -1.00000000', b'ANIM_rotate_key -2.00000000', 1),
                        source.replace(b'ANIM_rotate_key -0.90000000', b'ANIM_rotate_key -1.00000000', 1),
                        source.replace(b'ANIM_rotate_key -0.90000000', b'ANIM_rotate_key nan', 1),
                        source + b'ANIM_begin\n'):
            with self.assertRaises(ValueError):
                rebind_object(changed, 'fuselage', name)
        rebound, _ = rebind_object(source, 'fuselage', name)
        with self.assertRaisesRegex(ValueError, 'already or partially'):
            rebind_object(rebound, 'fuselage', name)

    def test_rejects_geometry_index_corruption(self):
        source = fixture('fuselage', 'a380x_fuse4.obj')
        for bad in (source.replace(b'IDX 2', b'IDX 3'), source.replace(b'TRIS 0 3', b'TRIS 2 3')):
            with self.assertRaisesRegex(ValueError, 'geometry/index'):
                rebind_object(bad, 'fuselage', 'a380x_fuse4.obj')

    def test_directory_staging_is_new_and_preserves_originals(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output, report = root / 'source', root / 'candidate', root / 'candidate/report.json'
            for group, names in EXPECTED_FILES.items():
                (source / group).mkdir(parents=True)
                for name in names:
                    (source / group / name).write_bytes(fixture(group, name))
            result = bind_directory(source, output, report)
            self.assertEqual(result['header_token_changes'], 510)
            self.assertEqual(len(result['objects']), 20)
            for group, names in EXPECTED_FILES.items():
                for name in names:
                    self.assertEqual((source / group / name).read_bytes(), fixture(group, name))
            with self.assertRaisesRegex(ValueError, 'new directory'):
                bind_directory(source, output, root / 'another-report.json')


class CachedExportTests(unittest.TestCase):
    def test_packaging_rebinds_exterior_only_and_retains_keyframes(self):
        from build_development_aircraft import copy_exterior_obj
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "a380x_fuse4.obj"
            text = ("ANIM_begin\nANIM_rotate_begin 1 0 0 sim/joystick/yoke_pitch_ratio # keep\n"
                    "ANIM_rotate_key -1 -20\nANIM_rotate_key 1 30\nANIM_rotate_end\nANIM_end\n")
            source.write_text(text)
            target = root / "packed.obj"
            copy_exterior_obj(source, target, root, "fuselage", hydraulic_surfaces=True)
            expected = text.replace("sim/joystick/yoke_pitch_ratio", PREFIX + "elevator_ratio")
            self.assertEqual(target.read_text(), expected)
            source.write_text(expected)
            copy_exterior_obj(source, target, root, "fuselage", hydraulic_surfaces=True)
            self.assertEqual(target.read_text(), expected)
            source.write_text(text)
            copy_exterior_obj(source, target, root, "fuselage", "interior", hydraulic_surfaces=True)
            self.assertEqual(target.read_text(), text)


class ProviderDetectionTests(unittest.TestCase):
    def test_full_sets_from_both_groups_and_comments(self):
        from build_development_aircraft import exterior_requires_surface_provider
        with tempfile.TemporaryDirectory() as directory:
            aircraft = Path(directory)
            for group in ('fuselage', 'wings'):
                (aircraft / 'objects/exterior' / group).mkdir(parents=True)
            wing = aircraft / 'objects/exterior/wings/a380x_wing1.obj'
            tail = aircraft / 'objects/exterior/fuselage/a380x_fuse4.obj'
            wing.write_text('\n'.join(f'ANIM_rotate_begin 1 0 0 {ref} # trailing comment'
                                     for ref in (*AILERONS.values(), *SPOILERS.values())))
            tail.write_text('\n'.join(f'ANIM_rotate_begin 1 0 0 {ref}' for ref in TAIL.values()))
            self.assertTrue(exterior_requires_surface_provider(aircraft))
            tail.write_text('')
            with self.assertRaisesRegex(ValueError, 'Incomplete'):
                exterior_requires_surface_provider(aircraft)
            wing.write_text('\n'.join(f'ANIM_rotate_begin 1 0 0 {ref}' for ref in AILERONS))
            self.assertTrue(exterior_requires_surface_provider(aircraft))
            wing.write_text('ANIM_rotate_begin 1 0 0 ' + PREFIX + 'unknown_ratio')
            with self.assertRaisesRegex(ValueError, 'unexpected'):
                exterior_requires_surface_provider(aircraft)
            wing.write_text('# ' + PREFIX + 'unknown_ratio\n')
            self.assertFalse(exterior_requires_surface_provider(aircraft))


if __name__ == '__main__':
    unittest.main()
