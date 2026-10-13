import copy
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from fbw_spoiler_rest import CLIP, PALETTE_MARKER, TARGETS, verify_fbw_spoiler_rest


def fixture(root):
    binary = bytearray(b'prefix00')
    document = {'extras': {PALETTE_MARKER: {'status': 'decoded'}}, 'buffers': [],
                'bufferViews': [], 'accessors': [], 'nodes': [], 'animations': []}

    def accessor(rows, kind):
        width = len(rows[0])
        offset = len(binary)
        binary.extend(b'head')
        for row in rows:
            binary.extend(struct.pack('<' + 'f' * width, *row))
            binary.extend(b'pad!')
        view = len(document['bufferViews'])
        document['bufferViews'].append({'buffer': 0, 'byteOffset': offset,
            'byteLength': len(binary) - offset, 'byteStride': width * 4 + 4})
        index = len(document['accessors'])
        document['accessors'].append({'bufferView': view, 'byteOffset': 4,
            'componentType': 5126, 'count': len(rows), 'type': kind})
        return index

    times = accessor([(0.,), (1.,)], 'SCALAR')
    clip = {'name': CLIP, 'channels': [], 'samplers': []}
    for index, name in enumerate(sorted(TARGETS)):
        node = {'name': name, 'translation': [1., 2., 3.], 'rotation': [0., 0., 0., 1.]}
        document['nodes'].append(node)
        for path, kind in [('translation', 'VEC3'), ('rotation', 'VEC4')]:
            output = accessor([node[path], node[path]], kind)
            clip['channels'].append({'target': {'node': index, 'path': path},
                                     'sampler': len(clip['samplers'])})
            clip['samplers'].append({'input': times, 'output': output, 'interpolation': 'STEP'})
    document['animations'].append(clip)
    document['buffers'].append({'uri': 'source.bin', 'byteLength': len(binary)})
    (root / 'source.bin').write_bytes(binary)
    return document


def set_component(root, document, accessor_index, row, component, value):
    accessor = document['accessors'][accessor_index]
    view = document['bufferViews'][accessor['bufferView']]
    binary = bytearray((root / 'source.bin').read_bytes())
    offset = view['byteOffset'] + accessor['byteOffset'] + row * view['byteStride'] + component * 4
    struct.pack_into('<f', binary, offset, value)
    (root / 'source.bin').write_bytes(binary)


class SpoilerRestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.doc = fixture(self.root)

    def verify(self):
        return verify_fbw_spoiler_rest(self.doc, self.root / 'wings.gltf')

    def test_all_five_strided_external_tracks_pass_without_mutation(self):
        before = copy.deepcopy(self.doc)
        binary = (self.root / 'source.bin').read_bytes()
        self.assertEqual(self.verify(), set(TARGETS))
        self.assertEqual(self.doc, before)
        self.assertEqual((self.root / 'source.bin').read_bytes(), binary)
        self.assertEqual(sorted(p.name for p in self.root.iterdir()), ['source.bin'])

    def test_linear_and_default_linear_constant_tracks_are_valid(self):
        for sampler in self.doc['animations'][0]['samplers']:
            sampler.pop('interpolation')
        self.assertEqual(self.verify(), set(TARGETS))

    def test_exact_constant_values_may_differ_from_rest_within_tolerance(self):
        self.doc['nodes'][0]['translation'][0] += 1.9e-6
        self.doc['nodes'][0]['rotation'][0] += 1.9e-7
        self.assertEqual(self.verify(), set(TARGETS))

    def test_different_constant_translation_or_rotation_is_not_rest(self):
        for path, delta in [('translation', 2.1e-6), ('rotation', 2.1e-7)]:
            with self.subTest(path=path):
                self.doc = fixture(self.root)
                self.doc['nodes'][0][path][0] += delta
                with self.assertRaisesRegex(ValueError, 'not node rest'):
                    self.verify()

    def test_last_sample_motion_rejected_even_within_rest_tolerance(self):
        for accessor, value in [(1, 1.0000001192092896), (2, 1e-8)]:
            with self.subTest(accessor=accessor):
                self.doc = fixture(self.root)
                set_component(self.root, self.doc, accessor, 1, 0, value)
                with self.assertRaisesRegex(ValueError, 'not exactly constant'):
                    self.verify()

    def test_late_target_failure_does_not_return_partial_success_or_mutate(self):
        self.doc['nodes'][-1]['translation'][0] += .01
        before = copy.deepcopy(self.doc)
        with self.assertRaises(ValueError):
            self.verify()
        self.assertEqual(self.doc, before)

    def test_missing_or_duplicate_target_rejected(self):
        for duplicate in (False, True):
            self.doc = fixture(self.root)
            if duplicate:
                self.doc['nodes'].append(copy.deepcopy(self.doc['nodes'][0]))
            else:
                self.doc['nodes'][0]['name'] = 'not_the_target'
            with self.assertRaisesRegex(ValueError, 'unique target'):
                self.verify()

    def test_missing_or_duplicate_clip_rejected(self):
        for clips in ([], [self.doc['animations'][0]] * 2):
            self.doc['animations'] = clips
            with self.assertRaisesRegex(ValueError, 'unique .* clip'):
                self.verify()

    def test_missing_duplicate_and_scale_channels_rejected(self):
        for mode in ('missing', 'duplicate', 'scale', 'extra_scale'):
            with self.subTest(mode=mode):
                self.doc = fixture(self.root)
                channels = self.doc['animations'][0]['channels']
                if mode == 'missing':
                    channels.pop(0)
                elif mode == 'duplicate':
                    channels[1] = copy.deepcopy(channels[0])
                elif mode == 'scale':
                    channels[1]['target']['path'] = 'scale'
                else:
                    channels.append({'target': {'node': 0, 'path': 'scale'}, 'sampler': 0})
                with self.assertRaisesRegex(ValueError, 'translation and one rotation'):
                    self.verify()

    def test_other_clip_and_unrelated_targets_are_not_suppressed(self):
        self.doc['animations'].append({'name': 'l_spoiler1_key', 'channels': [
            {'target': {'node': 0, 'path': 'translation'}, 'sampler': 123}]})
        self.doc['nodes'].append({'name': 'Unrelated'})
        self.doc['animations'][0]['channels'].append(
            {'target': {'node': 5, 'path': 'scale'}, 'sampler': 123})
        self.assertEqual(self.verify(), set(TARGETS))

    def test_marker_required(self):
        for marker in ({}, {'status': 'unknown'}, None):
            self.doc['extras'][PALETTE_MARKER] = marker
            with self.assertRaisesRegex(ValueError, 'corrected wing joint palette'):
                self.verify()

    def test_matrix_nodes_and_invalid_quaternions_fail(self):
        self.doc['nodes'][0]['matrix'] = [0.] * 16
        with self.assertRaisesRegex(ValueError, 'matrix-node'):
            self.verify()
        self.doc = fixture(self.root)
        self.doc['nodes'][0]['rotation'] = [0., 0., 0., 0.]
        with self.assertRaisesRegex(ValueError, 'unit length'):
            self.verify()

    def test_quaternion_sign_is_not_silently_changed(self):
        self.doc['nodes'][0]['rotation'] = [0., 0., 0., -1.]
        with self.assertRaisesRegex(ValueError, 'not node rest'):
            self.verify()

    def test_unsupported_accessor_and_interpolation_fail_closed(self):
        for update in ({'componentType': 5123}, {'type': 'SCALAR'}, {'normalized': True},
                       {'sparse': {}}, {'bufferView': -1}, {'count': 1}, {'count': True}):
            with self.subTest(update=update):
                self.doc = fixture(self.root)
                self.doc['accessors'][1].update(update)
                with self.assertRaises(ValueError):
                    self.verify()
        self.doc = fixture(self.root)
        self.doc['animations'][0]['samplers'][0]['interpolation'] = 'CUBICSPLINE'
        with self.assertRaisesRegex(ValueError, 'unsupported interpolation'):
            self.verify()

    def test_nonfinite_rest_samples_and_times_rejected(self):
        for accessor in (0, 1, 2):
            self.doc = fixture(self.root)
            set_component(self.root, self.doc, accessor, 0, 0, float('nan'))
            with self.assertRaisesRegex(ValueError, 'nonfinite'):
                self.verify()
        self.doc = fixture(self.root)
        self.doc['nodes'][0]['translation'][0] = float('inf')
        with self.assertRaisesRegex(ValueError, 'finite vector'):
            self.verify()

    def test_nonincreasing_negative_and_mismatched_times_rejected(self):
        for row, value in [(1, 0.), (0, -1.)]:
            self.doc = fixture(self.root)
            set_component(self.root, self.doc, 0, row, 0, value)
            with self.assertRaisesRegex(ValueError, 'non-increasing'):
                self.verify()
        self.doc = fixture(self.root)
        self.doc['accessors'][1]['count'] = 3
        with self.assertRaises(ValueError):
            self.verify()

    def test_accessor_view_declared_and_actual_bounds_are_checked(self):
        mutations = [lambda: self.doc['accessors'][1].update(byteOffset=8),
                     lambda: self.doc['bufferViews'][1].update(byteLength=16),
                     lambda: self.doc['bufferViews'][1].update(byteStride=8),
                     lambda: self.doc['bufferViews'][1].update(byteOffset=-1),
                     lambda: self.doc['bufferViews'][1].update(byteStride=17),
                     lambda: self.doc['buffers'][0].update(byteLength=16),
                     lambda: (self.root / 'source.bin').write_bytes(b'1234')]
        for mutate in mutations:
            self.doc = fixture(self.root)
            mutate()
            with self.assertRaises(ValueError):
                self.verify()

    def test_external_buffer_uri_and_symlink_escape_rejected(self):
        for uri in ('../source.bin', '%2e%2e/source.bin', '/tmp/source.bin',
                    'https://example.com/source.bin', 'data:application/octet-stream;base64,AA==',
                    'source.bin?query=1', '//example.com/source.bin'):
            with self.subTest(uri=uri):
                self.doc = fixture(self.root)
                self.doc['buffers'][0]['uri'] = uri
                with self.assertRaises(ValueError):
                    self.verify()
        with tempfile.TemporaryDirectory() as other:
            outside = Path(other) / 'outside.bin'
            outside.write_bytes((self.root / 'source.bin').read_bytes())
            (self.root / 'link.bin').symlink_to(outside)
            self.doc = fixture(self.root)
            self.doc['buffers'][0]['uri'] = 'link.bin'
            with self.assertRaisesRegex(ValueError, 'escapes'):
                self.verify()

    def test_percent_encoded_local_buffer_is_supported(self):
        (self.root / 'source.bin').rename(self.root / 'source copy.bin')
        self.doc['buffers'][0]['uri'] = 'source%20copy.bin'
        self.assertEqual(self.verify(), set(TARGETS))


if __name__ == '__main__':
    unittest.main()
