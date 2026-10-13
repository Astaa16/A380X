import copy
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from repair_fbw_wing_joints import EXPECTED_SKINS, MARKER, repair_fbw_wing_joints
from normalize_msfs_gltf import normalize_asobo_primitive_ranges, normalize_optimized_vertex_attributes


def fixture(root, joints=(1, 3, 63, 65535), weights=(32768, 16000, 16767, 0)):
    binary = b'prefix00' + struct.pack('<4H4H', *joints, *weights) + b'padding!'
    (root / 'source.bin').write_bytes(binary)
    document = {'asset': {'extensions': {'ASOBO_asset_optimized': {'MajorVersion': 4, 'MinorVersion': 6}}},
                'buffers': [{'uri': 'source.bin', 'byteLength': len(binary)}],
                'bufferViews': [{'buffer': 0, 'byteOffset': 8, 'byteLength': 24, 'byteStride': 24}],
                'accessors': [{'bufferView': 0, 'byteOffset': 0, 'componentType': 5123, 'type': 'VEC4', 'count': 1},
                              {'bufferView': 0, 'byteOffset': 8, 'componentType': 5123, 'type': 'VEC4', 'count': 1, 'normalized': True}],
                'nodes': [], 'skins': [], 'meshes': [], 'animations': [{'name': 'preserved_clip'}]}
    for name, (first, last) in EXPECTED_SKINS.items():
        start = len(document['nodes'])
        document['nodes'].extend({'name': first if i == 0 else last if i == 62 else f'{name}_joint_{i}'} for i in range(63))
        document['skins'].append({'name': name, 'joints': list(range(start, start + 63))})
        document['nodes'].append({'mesh': len(document['meshes']), 'skin': len(document['skins']) - 1})
        document['meshes'].append({'primitives': [{'attributes': {'JOINTS_0': 0, 'WEIGHTS_0': 1},
                                                 'extras': {'ASOBO_primitive': {'VertexType': 'BLEND4', 'VertexVersion': 2}}}]})
    return document, binary


class WingJointTests(unittest.TestCase):
    def test_decode_preserves_source_and_only_repoints_joints(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document, binary = fixture(root)
            before = copy.deepcopy(document)
            report = repair_fbw_wing_joints(document, root / 'wing.gltf')
            self.assertEqual((root / 'source.bin').read_bytes(), binary)
            self.assertEqual((root / 'a380x-wing-zero-based-joints.bin').read_bytes(), bytes((0, 2, 62, 0)))
            for key in ('nodes', 'skins', 'animations'):
                self.assertEqual(document[key], before[key])
            self.assertEqual(document['accessors'][:2], before['accessors'])
            self.assertEqual(report['new_accessors'], 1)
            self.assertTrue(all(p['attributes']['WEIGHTS_0'] == 1 for m in document['meshes'] for p in m['primitives']))
            self.assertEqual(document['meshes'][0]['primitives'][0]['attributes']['JOINTS_0'], 2)
            with self.assertRaisesRegex(ValueError, 'already'):
                repair_fbw_wing_joints(document, root / 'wing.gltf')

    def test_weighted_zero_or_overflow_is_rejected_without_mutation(self):
        for value in (0, 64):
            with self.subTest(value=value), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                document, binary = fixture(root, joints=(value, 3, 63, 0))
                before = copy.deepcopy(document)
                with self.assertRaisesRegex(ValueError, 'one-based'):
                    repair_fbw_wing_joints(document, root / 'wing.gltf')
                self.assertEqual(document, before)
                self.assertEqual((root / 'source.bin').read_bytes(), binary)
                self.assertFalse((root / 'a380x-wing-zero-based-joints.bin').exists())

    def test_missing_top_index_is_ambiguous(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document, _ = fixture(root, joints=(1, 3, 62, 0))
            with self.assertRaisesRegex(ValueError, 'ambiguous'):
                repair_fbw_wing_joints(document, root / 'wing.gltf')

    def test_source_and_layout_guards(self):
        for mutate in (lambda d: d['asset']['extensions']['ASOBO_asset_optimized'].update(MinorVersion=7),
                       lambda d: d['skins'][0].update(name='other_skin'),
                       lambda d: d['skins'][0]['joints'].pop(),
                       lambda d: d['meshes'][0]['primitives'][0]['extras']['ASOBO_primitive'].update(VertexVersion=1),
                       lambda d: d['accessors'][0].update(normalized=True),
                       lambda d: d['accessors'][1].update(count=2),
                       lambda d: d['bufferViews'][0].update(byteLength=9)):
            with tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                document, _ = fixture(root)
                mutate(document)
                with self.assertRaises(ValueError):
                    repair_fbw_wing_joints(document, root / 'wing.gltf')

    def test_zero_weight_padding_is_not_interpreted_as_a_joint(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document, _ = fixture(root, joints=(63, 0, 65535, 64), weights=(65535, 0, 0, 0))
            repair_fbw_wing_joints(document, root / 'wing.gltf')
            self.assertEqual((root / 'a380x-wing-zero-based-joints.bin').read_bytes(), bytes((62, 0, 0, 0)))

    def test_blend1_scalar_weight_and_shared_skin_accessors(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document, _ = fixture(root, joints=(63, 0, 65535, 64))
            data = bytearray((root / 'source.bin').read_bytes())
            struct.pack_into('<f', data, 16, 1.0)
            (root / 'source.bin').write_bytes(data)
            document['accessors'][1].update(type='SCALAR', componentType=5126, normalized=False)
            for mesh in document['meshes']:
                mesh['primitives'][0]['extras']['ASOBO_primitive']['VertexType'] = 'BLEND1'
            repair_fbw_wing_joints(document, root / 'wing.gltf')
            self.assertEqual((root / 'a380x-wing-zero-based-joints.bin').read_bytes(), bytes((62, 0, 0, 0)))

    def test_separate_skin_accessors_require_top_index_evidence_on_each_side(self):
        for right_top in (63, 62):
            with self.subTest(right_top=right_top), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                document, left_binary = fixture(root)
                right_binary = bytearray(left_binary)
                struct.pack_into('<4H', right_binary, 8, 2, 4, right_top, 65535)
                (root / 'right.bin').write_bytes(right_binary)
                document['buffers'].append({'uri': 'right.bin', 'byteLength': len(right_binary)})
                document['bufferViews'].append({**document['bufferViews'][0], 'buffer': 1})
                document['accessors'].extend({**accessor, 'bufferView': 1}
                                             for accessor in document['accessors'][:2])
                right_attributes = document['meshes'][1]['primitives'][0]['attributes']
                right_attributes.update(JOINTS_0=2, WEIGHTS_0=3)
                before = copy.deepcopy(document)
                if right_top == 62:
                    with self.assertRaisesRegex(ValueError, 'ambiguous'):
                        repair_fbw_wing_joints(document, root / 'wing.gltf')
                    self.assertEqual(document, before)
                    self.assertFalse((root / 'a380x-wing-zero-based-joints.bin').exists())
                else:
                    report = repair_fbw_wing_joints(document, root / 'wing.gltf')
                    self.assertEqual(report['new_accessors'], 2)
                    self.assertEqual([skin['top_index_lanes'] for skin in report['skins']], [1, 1])
                    self.assertEqual((root / 'a380x-wing-zero-based-joints.bin').read_bytes(),
                                     bytes((0, 2, 62, 0, 1, 3, 62, 0)))
                    self.assertNotEqual(document['meshes'][0]['primitives'][0]['attributes']['JOINTS_0'],
                                        right_attributes['JOINTS_0'])
                self.assertEqual((root / 'source.bin').read_bytes(), left_binary)
                self.assertEqual((root / 'right.bin').read_bytes(), right_binary)

    def test_mesh_shared_by_different_skins_is_rejected_without_mutation(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document, binary = fixture(root)
            document['nodes'].append({'mesh': 0, 'skin': 1})
            before = copy.deepcopy(document)
            with self.assertRaisesRegex(ValueError, 'shared across different skins'):
                repair_fbw_wing_joints(document, root / 'wing.gltf')
            self.assertEqual(document, before)
            self.assertEqual((root / 'source.bin').read_bytes(), binary)
            self.assertFalse((root / 'a380x-wing-zero-based-joints.bin').exists())

    def test_invalid_scalar_weights_are_rejected_without_mutation(self):
        for weight in (float('nan'), float('inf'), -1.0, 0.0):
            with self.subTest(weight=weight), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                document, binary = fixture(root)
                data = bytearray(binary)
                struct.pack_into('<f', data, 16, weight)
                (root / 'source.bin').write_bytes(data)
                document['accessors'][1].update(type='SCALAR', componentType=5126, normalized=False)
                for mesh in document['meshes']:
                    mesh['primitives'][0]['extras']['ASOBO_primitive']['VertexType'] = 'BLEND1'
                before = copy.deepcopy(document)
                with self.assertRaisesRegex(ValueError, 'Invalid wing weights'):
                    repair_fbw_wing_joints(document, root / 'wing.gltf')
                self.assertEqual(document, before)
                self.assertEqual((root / 'source.bin').read_bytes(), data)
                self.assertFalse((root / 'a380x-wing-zero-based-joints.bin').exists())

    def test_zero_total_blend4_weights_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document, binary = fixture(root, weights=(0, 0, 0, 0))
            before = copy.deepcopy(document)
            with self.assertRaisesRegex(ValueError, 'Invalid wing weights'):
                repair_fbw_wing_joints(document, root / 'wing.gltf')
            self.assertEqual(document, before)
            self.assertEqual((root / 'source.bin').read_bytes(), binary)
            self.assertFalse((root / 'a380x-wing-zero-based-joints.bin').exists())

    def test_external_buffer_paths_are_rejected_without_mutation(self):
        for path_kind in ('relative', 'absolute', 'symlink'):
            with self.subTest(path_kind=path_kind), tempfile.TemporaryDirectory() as folder:
                parent = Path(folder)
                root = parent / 'build'
                root.mkdir()
                document, binary = fixture(root)
                outside = parent / 'outside.bin'
                outside.write_bytes(binary)
                if path_kind == 'symlink':
                    (root / 'linked.bin').symlink_to(outside)
                    uri = 'linked.bin'
                else:
                    uri = '../outside.bin' if path_kind == 'relative' else str(outside)
                document['buffers'][0]['uri'] = uri
                before = copy.deepcopy(document)
                with self.assertRaisesRegex(ValueError, 'outside the build directory'):
                    repair_fbw_wing_joints(document, root / 'wing.gltf')
                self.assertEqual(document, before)
                self.assertEqual(outside.read_bytes(), binary)
                self.assertEqual((root / 'source.bin').read_bytes(), binary)
                self.assertFalse((root / 'a380x-wing-zero-based-joints.bin').exists())

    def test_unselected_skin_retains_shared_original_accessors(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document, binary = fixture(root)
            extra_joint = len(document['nodes'])
            document['nodes'].append({'name': 'other_skin_extra_joint'})
            document['skins'].append({'name': 'UNSELECTED_ARMATURE',
                                      'joints': document['skins'][0]['joints'] + [extra_joint]})
            document['meshes'].append(copy.deepcopy(document['meshes'][0]))
            document['nodes'].append({'mesh': 2, 'skin': 2})
            before = copy.deepcopy(document)
            report = repair_fbw_wing_joints(document, root / 'wing.gltf')
            self.assertEqual(report['primitives'], 2)
            self.assertEqual(document['meshes'][2], before['meshes'][2])
            self.assertEqual(document['meshes'][2]['primitives'][0]['attributes'],
                             {'JOINTS_0': 0, 'WEIGHTS_0': 1})
            for key in ('nodes', 'skins', 'animations'):
                self.assertEqual(document[key], before[key])
            self.assertEqual(document['accessors'][:2], before['accessors'])
            self.assertEqual(document['bufferViews'][:1], before['bufferViews'])
            self.assertEqual(document['buffers'][:1], before['buffers'])
            self.assertEqual((root / 'source.bin').read_bytes(), binary)

    def test_joint_repair_survives_draw_range_and_vertex_normalization(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document, _ = fixture(root)
            joints = b''.join(struct.pack('<4H', joint, 0, 65535, 64) for joint in (1, 2, 3, 63))
            weights = struct.pack('<4f', 1., 1., 1., 1.)
            positions = struct.pack('<12f', 9., 9., 9., 0., 0., 0., 1., 0., 0., 0., 1., 0.)
            indices = struct.pack('<6H', 0, 0, 0, 0, 1, 2)
            binary = joints + weights + positions + indices
            (root / 'source.bin').write_bytes(binary)
            document['buffers'][0]['byteLength'] = len(binary)
            document['bufferViews'] = [
                {'buffer': 0, 'byteOffset': 0, 'byteLength': len(joints)},
                {'buffer': 0, 'byteOffset': 32, 'byteLength': len(weights)},
                {'buffer': 0, 'byteOffset': 48, 'byteLength': len(positions)},
                {'buffer': 0, 'byteOffset': 96, 'byteLength': len(indices)},
            ]
            document['accessors'] = [
                {'bufferView': 0, 'componentType': 5123, 'type': 'VEC4', 'count': 4},
                {'bufferView': 1, 'componentType': 5126, 'type': 'SCALAR', 'count': 4},
                {'bufferView': 2, 'componentType': 5126, 'type': 'VEC3', 'count': 4},
                {'bufferView': 3, 'componentType': 5123, 'type': 'SCALAR', 'count': 6},
            ]
            for mesh in document['meshes']:
                primitive = mesh['primitives'][0]
                primitive['attributes']['POSITION'] = 2
                primitive['indices'] = 3
                primitive['extras']['ASOBO_primitive'].update(
                    VertexType='BLEND1', StartIndex=3, PrimitiveCount=1,
                    BaseVertexIndex=1, VertexCount=3)
            output = root / 'wing.gltf'
            before = copy.deepcopy(document)
            repair_fbw_wing_joints(document, output)
            corrected_file = root / 'a380x-wing-zero-based-joints.bin'
            corrected = bytes((0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 62, 0, 0, 0))
            self.assertEqual(corrected_file.read_bytes(), corrected)
            self.assertEqual((root / 'source.bin').read_bytes(), binary)
            repaired_accessor = copy.deepcopy(document['accessors'][4])
            draw_report = normalize_asobo_primitive_ranges(document, output)
            vertex_report = normalize_optimized_vertex_attributes(document, output)
            self.assertEqual(draw_report['asobo_range_accessors_created'], 2)
            self.assertEqual(vertex_report['scalar_weight_accessors'], 1)
            self.assertEqual(vertex_report['sanitized_joint_indices'], 0)
            self.assertEqual(document['accessors'][4], repaired_accessor)
            self.assertEqual(corrected_file.read_bytes(), corrected)
            normalized_binary = (root / 'source.bin').read_bytes()
            self.assertEqual(normalized_binary[:len(binary)], binary)
            for mesh in document['meshes']:
                primitive = mesh['primitives'][0]
                self.assertNotIn('ASOBO_primitive', primitive.get('extras', {}))
                self.assertEqual(primitive['attributes']['JOINTS_0'], 4)
                index_accessor = document['accessors'][primitive['indices']]
                view = document['bufferViews'][index_accessor['bufferView']]
                self.assertEqual(struct.unpack_from('<3I', normalized_binary, view['byteOffset']), (1, 2, 3))
            weight_accessor = document['accessors'][1]
            self.assertEqual((weight_accessor['type'], weight_accessor['count']), ('VEC4', 4))
            weight_view = document['bufferViews'][weight_accessor['bufferView']]
            self.assertEqual(struct.unpack_from('<16f', normalized_binary, weight_view['byteOffset']),
                             (1., 0., 0., 0.) * 4)
            for key in ('nodes', 'skins', 'animations'):
                self.assertEqual(document[key], before[key])
            self.assertIn(MARKER, document['extras'])


if __name__ == '__main__':
    unittest.main()
