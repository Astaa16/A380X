#!/usr/bin/env python3
import copy
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from repair_fbw_tail_bindshape import (EXPECTED_BONE_WORLD, MARKER, MESH_JOINT_FAMILIES,
                                      MARKER_VERSION, RIGHT_SKIN, SKIN_MESHES,
                                      SOURCE_JOINT_NAMES, protected_tail_joint_accessors, repair_fbw_tail_bindshape)
from normalize_msfs_gltf import normalize_optimized_vertex_attributes


def fixture(root):
    binary = bytearray()
    document = {'buffers': [], 'bufferViews': [], 'accessors': [], 'meshes': [], 'skins': [],
                'nodes': [{'name': 'THS_TRIM', 'children': [1]},
                          {'name': 'Bone', 'matrix': [EXPECTED_BONE_WORLD[r * 4 + c] for c in range(4) for r in range(4)], 'children': []}],
                'animations': [{'name': 'trimtab_elevator_key', 'channels': [{'target': {'node': 1, 'path': 'rotation'}, 'sampler': 0}], 'samplers': []}]}
    def accessor(rows, kind, component=5126):
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        fmt = {5126: 'f', 5121: 'B', 5123: 'H'}[component]
        for row in rows:
            binary.extend(struct.pack('<' + fmt * len(row), *row))
        vi = len(document['bufferViews'])
        document['bufferViews'].append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(binary) - offset})
        ai = len(document['accessors'])
        document['accessors'].append({'bufferView': vi, 'componentType': component, 'count': len(rows), 'type': kind})
        return ai
    normal = accessor([(0., 1., 0.)] * 6, 'VEC3')
    tangent = accessor([(1., 0., 0., -1.)] * 6, 'VEC4')
    uv = accessor([(0., 0.), (1., 0.), (0., 1.)] * 2, 'VEC2')
    weights = accessor([(1., 0., 0., 0.)] * 6, 'VEC4')
    indices = accessor([(0,), (1,), (2,)], 'SCALAR', 5123)
    ibm_rows = [tuple(float(j * 16 + value) for value in range(16)) for j in range(12)]
    ibm = accessor(ibm_rows, 'MAT4')
    for si, (name, members) in enumerate(SKIN_MESHES.items()):
        armature = len(document['nodes'])
        document['nodes'].append({'name': name, 'children': []})
        document['nodes'][1]['children'].append(armature)
        bone_ids = []
        for joint_name in SOURCE_JOINT_NAMES[name]:
            bone_ids.append(len(document['nodes']))
            document['nodes'].append({'name': joint_name})
        document['nodes'][armature]['children'].extend(bone_ids)
        document['skins'].append({'name': name, 'joints': bone_ids, 'inverseBindMatrices': ibm})
        z = -5. if si == 0 else 5.
        position = accessor([(.5, 3., z), (1., 3., z), (.5, 4., z)] * 2, 'VEC3')
        for member in sorted(members):
            family = MESH_JOINT_FAMILIES[member]
            indices_one_based = [SOURCE_JOINT_NAMES[name].index(bone) + 1 for bone in family]
            joints = accessor([(indices_one_based[i % len(family)], 255, 0, 0)
                               for i in range(6)], 'VEC4', 5121)
            document['nodes'][armature]['children'].append(len(document['nodes']))
            document['nodes'].append({'name': member, 'skin': si, 'mesh': len(document['meshes'])})
            document['meshes'].append({'primitives': [{'attributes': {'POSITION': position, 'NORMAL': normal, 'TANGENT': tangent,
                                          'TEXCOORD_0': uv, 'JOINTS_0': joints, 'WEIGHTS_0': weights}, 'indices': indices}]})
    document['buffers'].append({'uri': 'fixture.bin', 'byteLength': len(binary)})
    (root / 'fixture.bin').write_bytes(binary)
    return document


class TailBindshapeTests(unittest.TestCase):
    def test_targeted_geometry_and_preserved_inputs(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = fixture(root)
            before = copy.deepcopy(document)
            source = (root / 'fixture.bin').read_bytes()
            report = repair_fbw_tail_bindshape(document, root / 'candidate.gltf')
            self.assertEqual(report['repaired_primitives'], 10)
            self.assertEqual(report['new_accessors'], 14) # two positions, normal/tangent, ten joint streams
            self.assertEqual((root / 'fixture.bin').read_bytes(), source)
            for key in ('nodes', 'animations'):
                self.assertEqual(document[key], before[key])
            self.assertEqual(document['skins'], before['skins'])
            right = next(skin for skin in document['skins'] if skin['name'] == RIGHT_SKIN)
            old_right = next(skin for skin in before['skins'] if skin['name'] == RIGHT_SKIN)
            self.assertEqual(right, old_right)
            for mesh, original in zip(document['meshes'], before['meshes']):
                p, old = mesh['primitives'][0], original['primitives'][0]
                self.assertEqual(p['indices'], old['indices'])
                for semantic in ('TEXCOORD_0', 'WEIGHTS_0'):
                    self.assertEqual(p['attributes'][semantic], old['attributes'][semantic])
                old_a = before['accessors'][old['attributes']['JOINTS_0']]
                old_v = before['bufferViews'][old_a['bufferView']]
                old_rows = [struct.unpack_from('<4B', source, old_v['byteOffset'] + i * 4) for i in range(6)]
                new_a = document['accessors'][p['attributes']['JOINTS_0']]
                new_v = document['bufferViews'][new_a['bufferView']]
                new_bytes = (root / document['buffers'][new_v['buffer']]['uri']).read_bytes()
                new_rows = [struct.unpack_from('<4H', new_bytes, new_v['byteOffset'] + i * 8) for i in range(6)]
                self.assertEqual(new_rows, [(row[0] - 1, 0, 0, 0) for row in old_rows])
            for record in report['meshes']:
                self.assertGreater(record['after_bounds'][0][1], 3.)
                self.assertLess(record['after_bounds'][1][2], -29.)
            p = document['meshes'][0]['primitives'][0]
            data = (root / document['buffers'][-1]['uri']).read_bytes()
            for semantic, expected in [('NORMAL', (0., 0., -1.)), ('TANGENT', (0., 1., 0., -1.))]:
                a = document['accessors'][p['attributes'][semantic]]
                v = document['bufferViews'][a['bufferView']]
                values = struct.unpack_from('<' + 'f' * len(expected), data, v['byteOffset'])
                self.assertEqual(values, expected)
            self.assertTrue(all(mesh['primitives'][0]['extras'][MARKER]['version'] == MARKER_VERSION
                                for mesh in document['meshes']))
            self.assertEqual(document['extras'][MARKER]['version'], 3)
            self.assertEqual(document['extras'][MARKER]['positive_joint_lane_offset'], -1)

    def test_idempotent(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = fixture(root)
            repair_fbw_tail_bindshape(document, root / 'candidate.gltf')
            snapshot = json.dumps(document, sort_keys=True)
            self.assertEqual(repair_fbw_tail_bindshape(document, root / 'candidate.gltf')['status'], 'already_repaired')
            self.assertEqual(json.dumps(document, sort_keys=True), snapshot)

    def test_guard_ancestry_matrix_and_membership(self):
        for change in ('ancestor', 'matrix', 'membership', 'outside_instance', 'action', 'palette'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                document = fixture(root)
                if change == 'ancestor':document['nodes'][0]['name'] = 'Unrelated'
                elif change == 'matrix':document['nodes'][1]['matrix'][12] += 5
                elif change == 'membership':next(n for n in document['nodes'] if n.get('name') == 'Cube.099')['name'] = 'Unknown'
                elif change == 'outside_instance':document['nodes'].append({'name': 'Unrelated', 'mesh': 0})
                elif change == 'action':document['animations'][0]['name'] = 'Other'
                else:
                    right = next(skin for skin in document['skins'] if skin['name'] == RIGHT_SKIN)
                    right['joints'][1], right['joints'][2] = right['joints'][2], right['joints'][1]
                snapshot = copy.deepcopy(document)
                with self.assertRaises(ValueError):
                    repair_fbw_tail_bindshape(document, root / 'candidate.gltf')
                self.assertEqual(document, snapshot)
                self.assertFalse((root / 'buffers').exists())

    def test_rejects_already_world_space_without_marker(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = fixture(root)
            repair_fbw_tail_bindshape(document, root / 'candidate.gltf')
            for mesh in document['meshes']:
                del mesh['primitives'][0]['extras'][MARKER]
            snapshot = copy.deepcopy(document)
            with self.assertRaises(ValueError):
                repair_fbw_tail_bindshape(document, root / 'candidate.gltf')
            self.assertEqual(document, snapshot)

    def test_rejects_partial_markers(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = fixture(root)
            repair_fbw_tail_bindshape(document, root / 'candidate.gltf')
            del document['meshes'][0]['primitives'][0]['extras'][MARKER]
            with self.assertRaises(ValueError):
                repair_fbw_tail_bindshape(document, root / 'candidate.gltf')

    def test_rejects_v2_marker_without_silently_repairing_again(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = fixture(root)
            repair_fbw_tail_bindshape(document, root / 'candidate.gltf')
            for mesh in document['meshes']:
                mesh['primitives'][0]['extras'][MARKER]['version'] = 2
            before = copy.deepcopy(document)
            with self.assertRaises(ValueError):
                repair_fbw_tail_bindshape(document, root / 'candidate.gltf')
            self.assertEqual(document, before)

    def test_rejects_stale_or_missing_document_marker(self):
        for version in (None, 2):
            with self.subTest(version=version), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                document = fixture(root)
                repair_fbw_tail_bindshape(document, root / 'candidate.gltf')
                if version is None:
                    del document['extras'][MARKER]
                else:
                    document['extras'][MARKER]['version'] = version
                with self.assertRaisesRegex(ValueError, 'document repair marker'):
                    repair_fbw_tail_bindshape(document, root / 'candidate.gltf')

    def test_rejects_bad_joint_lanes_and_weights_before_mutation(self):
        for kind in ('zero', 'twelve', 'wrong_family', 'negative_weight', 'nan_weight', 'zero_weights', 'normalized_joints'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                document = fixture(root)
                attrs = document['meshes'][0]['primitives'][0]['attributes']
                ja, wa = (document['accessors'][attrs[semantic]] for semantic in ('JOINTS_0', 'WEIGHTS_0'))
                jv, wv = (document['bufferViews'][a['bufferView']] for a in (ja, wa))
                binary = bytearray((root / 'fixture.bin').read_bytes())
                if kind == 'normalized_joints':
                    ja['normalized'] = True
                elif kind in ('zero', 'twelve', 'wrong_family'):
                    binary[jv['byteOffset']] = {'zero': 0, 'twelve': 12, 'wrong_family': 1}[kind]
                else:
                    struct.pack_into('<f', binary, wv['byteOffset'], {'negative_weight': -1., 'nan_weight': float('nan'), 'zero_weights': 0.}[kind])
                (root / 'fixture.bin').write_bytes(binary)
                before = copy.deepcopy(document)
                with self.assertRaises(ValueError):
                    repair_fbw_tail_bindshape(document, root / 'candidate.gltf')
                self.assertEqual(document, before)
                self.assertFalse((root / 'buffers').exists())

    def test_rejects_unsupported_vector_storage(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = fixture(root)
            position = document['meshes'][0]['primitives'][0]['attributes']['POSITION']
            document['accessors'][position]['componentType'] = 5125
            with self.assertRaises(ValueError):
                repair_fbw_tail_bindshape(document, root / 'candidate.gltf')
            self.assertFalse((root / 'buffers').exists())

    def test_generic_normalizer_does_not_hide_invalid_tail_lane(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = fixture(root)
            joint = document['meshes'][0]['primitives'][0]['attributes']['JOINTS_0']
            view = document['bufferViews'][document['accessors'][joint]['bufferView']]
            binary = bytearray((root / 'fixture.bin').read_bytes())
            binary[view['byteOffset']] = 12
            (root / 'fixture.bin').write_bytes(binary)
            normalized = normalize_optimized_vertex_attributes(
                document, root / 'candidate.gltf',
                protected_joint_accessors=protected_tail_joint_accessors(document))
            self.assertEqual(normalized['sanitized_joint_indices'], 0)
            self.assertEqual((root / 'fixture.bin').read_bytes(), binary)
            with self.assertRaisesRegex(ValueError, 'one-based'):
                repair_fbw_tail_bindshape(document, root / 'candidate.gltf')


if __name__ == '__main__':
    unittest.main()
