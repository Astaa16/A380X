"""Experimental, source-specific FBW A380 horizontal-tail bind-shape correction.

Source-space audit is consistent with the two named tail skins storing vertices
in the animated trim Bone's local basis while joint transforms/inverse binds
use aircraft space. MSFS documents that it ignores inverse-bind matrices:
https://docs.flightsimulator.com/html/Asset_Creation/3D_Models/General_Principles.htm
The exact replacement formula is not documented there; this source-specific
bind-shape remains inferred from the audited geometry. Standard glTF skinning
leaves these vertices at the origin, rotated upright. This opt-in adapter
applies that Bone's REST world matrix to geometry and decodes the audited
one-based BLEND4 joint lanes. Original joint palettes and inverse-bind matrices
remain unchanged. It does not alter animation channels or skin weights.

This is deliberately not an automatic repair for arbitrary MSFS assets. Exact
skin membership, ancestry, source-space bounds and rest matrix are checked.
The October 4 export passes neutral/trim/elevator pose checks and an offline
render. Native simulator acceptance is separate from those geometry checks.
"""
from __future__ import annotations

import math
import mmap
import struct
from pathlib import Path

MARKER = 'a380x_xp12_tail_bindshape'
MARKER_VERSION = 3
SKIN_MESHES = {
    'ELEVATOR_WINGFLEX_LH': {'Cube.118', 'Cube.119', 'Cube.369', 'Cube.370', 'Cube.371'},
    'ELEVATOR_WINGFLEX_LH.001': {'Cube.099', 'Cube.100', 'Cube.101', 'Cube.104', 'Cube.116'},
}
SOURCE_JOINT_NAMES = {
    'ELEVATOR_WINGFLEX_LH': (
        'EW_BONE2.L', 'EW_BONE3.L', 'EW_BONE4.L', 'EW_BONE5.L',
        'EW_BONE6.L', 'EW_BONE7.L', 'E_BONE_OUTER3.L', 'E_BONE_OUTER2.L',
        'E_BONE_OUTER1.L', 'E_BONE_INNER2.L', 'E_BONE_INNER1.L', 'neutral_bone',
    ),
    'ELEVATOR_WINGFLEX_LH.001': (
        'EW_BONE2.R', 'E_BONE_INNER1.R', 'EW_BONE3.R', 'E_BONE_INNER2.R',
        'EW_BONE4.R', 'EW_BONE5.R', 'EW_BONE6.R', 'EW_BONE7.R',
        'E_BONE_OUTER3.R', 'E_BONE_OUTER2.R', 'E_BONE_OUTER1.R', 'NEUTRAL_BONE_1',
    ),
}
RIGHT_SKIN = 'ELEVATOR_WINGFLEX_LH.001'
# Positive source lanes are one-based, just like the independently audited
# wing BLEND4 data. The old v2 palette permutation was a misdiagnosis of that
# indexing convention and must not be applied to a v3 build.
MESH_JOINT_FAMILIES = {
    'Cube.369': tuple(f'EW_BONE{i}.L' for i in range(2, 8)),
    'Cube.370': tuple(f'EW_BONE{i}.L' for i in range(2, 7)),
    'Cube.371': tuple(f'EW_BONE{i}.L' for i in range(2, 8)),
    'Cube.118': ('E_BONE_INNER1.L', 'E_BONE_INNER2.L'),
    'Cube.119': ('E_BONE_OUTER1.L', 'E_BONE_OUTER2.L', 'E_BONE_OUTER3.L'),
    'Cube.099': tuple(f'EW_BONE{i}.R' for i in range(2, 8)),
    'Cube.104': tuple(f'EW_BONE{i}.R' for i in range(2, 7)),
    'Cube.116': tuple(f'EW_BONE{i}.R' for i in range(2, 8)),
    'Cube.101': ('E_BONE_INNER1.R', 'E_BONE_INNER2.R'),
    'Cube.100': ('E_BONE_OUTER1.R', 'E_BONE_OUTER2.R', 'E_BONE_OUTER3.R'),
}
IDENTITY = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.)
EXPECTED_BONE_WORLD = (0., 0., -1., 0., 1., 0., 0., 3.5336494,
                       0., -1., 0., -26.9592676, 0., 0., 0., 1.)


def multiply(a, b):
    return tuple(sum(a[row * 4 + k] * b[k * 4 + col] for k in range(4))
                 for row in range(4) for col in range(4))


def node_matrix(node):
    if 'matrix' in node:
        return tuple(node['matrix'][col * 4 + row] for row in range(4) for col in range(4))
    x, y, z, w = node.get('rotation', (0., 0., 0., 1.))
    scale = node.get('scale', (1., 1., 1.))
    trans = node.get('translation', (0., 0., 0.))
    rot = ((1-2*y*y-2*z*z, 2*x*y-2*z*w, 2*x*z+2*y*w),
           (2*x*y+2*z*w, 1-2*x*x-2*z*z, 2*y*z-2*x*w),
           (2*x*z-2*y*w, 2*y*z+2*x*w, 1-2*x*x-2*y*y))
    return tuple(rot[row][col] * scale[col] if row < 3 and col < 3 else
                 trans[row] if row < 3 and col == 3 else float(row == col)
                 for row in range(4) for col in range(4))


def transform(matrix, value, direction=False):
    result = tuple(sum(matrix[row * 4 + k] * value[k] for k in range(3)) +
                   (0. if direction else matrix[row * 4 + 3]) for row in range(3))
    if direction:
        magnitude = math.sqrt(sum(component * component for component in result))
        if magnitude > 1e-12:
            result = tuple(component / magnitude for component in result)
    return result + tuple(value[3:])


def bounds(values):
    return [[min(value[axis] for value in values) for axis in range(3)],
            [max(value[axis] for value in values) for axis in range(3)]]


def protected_tail_joint_accessors(document):
    """Preserve raw lanes until this source-scoped decoder validates them.

    Generic packed-attribute normalization clamps invalid skin indices. That
    fallback must not conceal malformed positive lanes from the strict repair.
    Full mesh/skin/family validation still occurs in repair_fbw_tail_bindshape.
    """
    return {primitive['attributes']['JOINTS_0']
            for node in document.get('nodes', []) if node.get('name') in MESH_JOINT_FAMILIES
            for primitive in document['meshes'][node['mesh']]['primitives']
            if 'JOINTS_0' in primitive.get('attributes', {})}


def repair_fbw_tail_bindshape(document, output_document: Path, *, source_buffer_root: Path | None = None):
    """Append repaired attributes in a build-only buffer, returning an audit.

    ``source_buffer_root`` is useful for an isolated regression candidate; the
    production normalizer reads and writes only within its output directory.
    No existing binary buffer, geometry index or animation accessor is modified.
    """
    output_document = Path(output_document)
    output_root = output_document.parent.resolve()
    input_root = Path(source_buffer_root or output_root).resolve()
    nodes = document.get('nodes', [])
    parents = {}
    for index, node in enumerate(nodes):
        for child in node.get('children', []):
            if child in parents:
                raise ValueError('Tail repair requires a tree, not multiply-parented nodes')
            parents[child] = index
    cache, visiting = {}, set()

    def world(index):
        if index in visiting:
            raise ValueError('Cycle in source node hierarchy')
        if index not in cache:
            visiting.add(index)
            cache[index] = multiply(world(parents[index]) if index in parents else IDENTITY, node_matrix(nodes[index]))
            visiting.remove(index)
        return cache[index]

    def ancestors(index):
        chain, seen = [], set()
        while True:
            if index in seen:
                raise ValueError('Cycle in source node hierarchy')
            seen.add(index)
            chain.append(index)
            if index not in parents:
                return chain
            index = parents[index]

    plans, skin_indices, trim_node = [], [], None
    palette_names = {}
    for name, expected_meshes in SKIN_MESHES.items():
        matches = [index for index, skin in enumerate(document.get('skins', [])) if skin.get('name') == name]
        if len(matches) != 1:
            raise ValueError(f'Tail repair requires exactly one skin named {name}')
        skin_index = matches[0]
        skin = document['skins'][skin_index]
        if len(skin.get('joints', [])) != 12 or 'inverseBindMatrices' not in skin:
            raise ValueError(f'{name}: unexpected joint/bind structure')
        mesh_nodes = [(i, node) for i, node in enumerate(nodes) if node.get('skin') == skin_index]
        if len(mesh_nodes) != len(expected_meshes) or {node.get('name') for _, node in mesh_nodes} != expected_meshes:
            raise ValueError(f'{name}: unexpected mesh membership; refusing broad repair')
        armatures = [i for i, node in enumerate(nodes) if node.get('name') == name]
        if len(armatures) != 1:
            raise ValueError(f'{name}: missing/ambiguous armature node')
        armature = armatures[0]
        if any(armature not in ancestors(joint) for joint in skin['joints']):
            raise ValueError(f'{name}: skin joints are outside expected armature')
        palette_names[name] = tuple(nodes[joint].get('name') for joint in skin['joints'])
        bone = parents.get(armature)
        ths = parents.get(bone)
        if bone is None or nodes[bone].get('name') != 'Bone' or ths is None or nodes[ths].get('name') != 'THS_TRIM':
            raise ValueError(f'{name}: expected armature -> Bone -> THS_TRIM relationship')
        if trim_node is not None and trim_node != bone:
            raise ValueError('Tail skins do not share the same trim Bone')
        trim_node = bone
        if any(armature not in ancestors(i) for i, _ in mesh_nodes):
            raise ValueError(f'{name}: tail mesh is outside expected armature')
        skin_indices.append(skin_index)
        for node_index, node in mesh_nodes:
            if 'mesh' not in node:
                raise ValueError(f'{node["name"]}: expected mesh node')
            for primitive in document['meshes'][node['mesh']]['primitives']:
                if primitive.get('mode', 4) != 4 or 'targets' in primitive:
                    raise ValueError('Tail repair supports triangle primitives without morph targets only')
                if not {'POSITION', 'NORMAL', 'JOINTS_0', 'WEIGHTS_0'} <= primitive.get('attributes', {}).keys():
                    raise ValueError('Tail repair requires complete skin attributes')
                plans.append((node_index, name, primitive))

    trim_actions = [action for action in document.get('animations', []) if action.get('name') == 'trimtab_elevator_key']
    if len(trim_actions) != 1 or not any(channel['target'].get('node') == trim_node and channel['target'].get('path') == 'rotation'
                                       for channel in trim_actions[0].get('channels', [])):
        raise ValueError('Expected trimtab_elevator_key rotation on the shared Bone')
    matrix = world(trim_node)
    if any(not math.isfinite(value) or abs(value - expected) > .002 for value, expected in zip(matrix, EXPECTED_BONE_WORLD)):
        raise ValueError('Source trim Bone rest matrix differs from audited A380 geometry')

    processed = [primitive.get('extras', {}).get(MARKER) for _, _, primitive in plans]
    document_marker = document.get('extras', {}).get(MARKER)
    if document_marker and not any(processed):
        raise ValueError('Tail document marker is inconsistent with unprocessed primitives')
    if any(processed):
        if (not document_marker or document_marker.get('version') != MARKER_VERSION
                or document_marker.get('matrix') != list(matrix)
                or document_marker.get('positive_joint_lane_offset') != -1):
            raise ValueError('Tail document repair marker is missing or stale; normalize the original source again')
        if not all(marker and marker.get('version') == MARKER_VERSION and marker.get('matrix') == list(matrix)
                   and marker.get('positive_joint_lane_offset') == -1
                   for marker in processed):
            raise ValueError('Tail bind-shape repair markers are partial or inconsistent')
        if any(palette_names.get(name) != expected for name, expected in SOURCE_JOINT_NAMES.items()):
            raise ValueError('Tail repair marker does not match the original joint palettes')
        return {'status': 'already_repaired', 'primitives': len(plans), 'repaired_primitives': 0,
                'rest_matrix': list(matrix), 'new_accessors': 0}

    for name, expected in SOURCE_JOINT_NAMES.items():
        if palette_names.get(name) != expected:
            raise ValueError(f'{name}: joint palette differs from the audited FBW source')

    # A mesh referenced outside these exact two skins would make attribute
    # rewrites affect an unrelated object. Reject it rather than guessing.
    selected_nodes = {index for index, _, _ in plans}
    selected_meshes = {nodes[index]['mesh'] for index in selected_nodes}
    if any(index not in selected_nodes and node.get('mesh') in selected_meshes for index, node in enumerate(nodes)):
        raise ValueError('Tail mesh is also instanced outside the targeted skins')

    mapped, streams = {}, []
    prepared, records = {}, []
    formats = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5126: 'f'}

    def read(index, allowed_types=('VEC3', 'VEC4')):
        accessor = document['accessors'][index]
        if 'sparse' in accessor or accessor.get('type') not in allowed_types:
            raise ValueError('Unpack sparse/packed attributes before tail repair')
        view = document['bufferViews'][accessor['bufferView']]
        buffer_index = view['buffer']
        if buffer_index not in mapped:
            uri = document['buffers'][buffer_index]['uri']
            if uri.startswith('data:'):
                raise ValueError('Tail repair requires external build-copy buffers')
            stream = (input_root / uri.replace('\\', '/')).open('rb')
            streams.append(stream)
            mapped[buffer_index] = mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ)
        component = accessor['componentType']
        if component not in formats:
            raise ValueError(f'Unsupported tail vector component type: {component}')
        width = {'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[accessor['type']]
        unpack = struct.Struct('<' + formats[component] * width)
        stride = view.get('byteStride', unpack.size)
        offset = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
        end = accessor.get('byteOffset', 0) + ((accessor['count'] - 1) * stride + unpack.size if accessor['count'] else 0)
        if stride < unpack.size or end > view['byteLength'] or view.get('byteOffset', 0) + view['byteLength'] > len(mapped[buffer_index]):
            raise ValueError('Tail attribute extends outside its buffer view')
        data = [unpack.unpack_from(mapped[buffer_index], offset + row * stride) for row in range(accessor['count'])]
        if accessor.get('normalized') and component != 5126:
            divisor = {5120: 127, 5121: 255, 5122: 32767, 5123: 65535}[component]
            data = [tuple(max(-1., value / divisor) for value in row) for row in data]
        if not data or not all(math.isfinite(value) for row in data for value in row):
            raise ValueError('Tail attribute is empty or nonfinite')
        return data

    try:
        for node_index, name, primitive in plans:
            attributes = primitive['attributes']
            for semantic, expected_type in (('POSITION', 'VEC3'), ('NORMAL', 'VEC3'), ('TANGENT', 'VEC4')):
                if semantic in attributes and document['accessors'][attributes[semantic]].get('type') != expected_type:
                    raise ValueError(f'Tail {semantic} must be unpacked as {expected_type}')
            original = read(attributes['POSITION'])
            joint_index, weight_index = attributes['JOINTS_0'], attributes['WEIGHTS_0']
            joint_accessor = document['accessors'][joint_index]
            if (joint_accessor.get('type') != 'VEC4' or joint_accessor.get('normalized')
                    or joint_accessor.get('componentType') not in (5121, 5123)):
                raise ValueError('Tail JOINTS_0 must be unnormalized unsigned-byte/short VEC4')
            if document['accessors'][weight_index].get('type') != 'VEC4':
                raise ValueError('Tail WEIGHTS_0 must be VEC4')
            joints, weights = read(joint_index), read(weight_index)
            if len(joints) != len(weights) or len(joints) != len(original):
                raise ValueError('Tail skin/position accessor counts differ')
            corrected_joints, observed, raw_indices = [], set(), set()
            for row, weight_row in zip(joints, weights):
                if any(weight < 0 for weight in weight_row) or sum(weight_row) <= 0:
                    raise ValueError('Tail weights must be nonnegative with a positive total')
                shifted = []
                for joint, weight in zip(row, weight_row):
                    if weight > 0:
                        if not 1 <= joint <= 11:
                            raise ValueError('Tail positive-weight source lanes must be one-based 1..11')
                        observed.add(SOURCE_JOINT_NAMES[name][joint - 1])
                        raw_indices.add(joint)
                        shifted.append(joint - 1)
                    else:
                        shifted.append(0)  # An unused padding lane has no palette semantics.
                corrected_joints.append(tuple(shifted))
            expected = set(MESH_JOINT_FAMILIES[nodes[node_index]['name']])
            if observed != expected:
                raise ValueError(f'{nodes[node_index]["name"]}: decoded joint family differs from audited source')
            # The same joint accessor can in principle be used with different
            # weight streams. Reject disagreement rather than overwrite a plan.
            key = ('JOINTS_0', joint_index)
            if key in prepared and prepared[key] != corrected_joints:
                raise ValueError('Shared tail joint accessor has conflicting weighted lane usage')
            prepared[key] = corrected_joints
            before = bounds(original)
            if any(before[0][i] < lo or before[1][i] > hi for i, (lo, hi) in enumerate(((-2., 3.), (-5., 12.), (-17., 17.)))):
                raise ValueError(f'{nodes[node_index]["name"]}: positions are not in the audited trim-local range')
            corrected = [transform(matrix, row) for row in original]
            after = bounds(corrected)
            if any(after[0][i] < lo or after[1][i] > hi for i, (lo, hi) in enumerate(((-17., 17.), (1., 7.), (-40., -20.)))):
                raise ValueError('Repaired tail coordinates failed the aft-aircraft regression envelope')
            prepared[('POSITION', attributes['POSITION'])] = corrected
            for semantic in ('NORMAL', 'TANGENT'):
                if semantic in attributes:
                    index = attributes[semantic]
                    if (semantic, index) not in prepared:
                        prepared[(semantic, index)] = [transform(matrix, row, direction=True) for row in read(index)]
            records.append({'node': nodes[node_index]['name'], 'skin': name,
                            'vertices': len(original), 'before_bounds': before, 'after_bounds': after,
                            'source_positive_joint_indices': sorted(raw_indices),
                            'decoded_joint_family': sorted(observed)})
    finally:
        for value in mapped.values():
            value.close()
        for value in streams:
            value.close()

    buffer_path = output_root / 'buffers' / f'{output_document.stem}.tail-bindshape-{len(document.get("buffers", [])):03d}.bin'
    referenced = {(input_root / buffer['uri'].replace('\\', '/')).resolve() for buffer in document['buffers'] if 'uri' in buffer}
    if buffer_path.resolve() in referenced:
        raise ValueError('Refusing to overwrite an existing input buffer')
    buffer_path.parent.mkdir(parents=True, exist_ok=True)
    binary = bytearray()
    accessor_lookup, new_views, new_accessors = {}, [], []
    for key, rows in prepared.items():
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        width = len(rows[0])
        component = 5123 if key[0] == 'JOINTS_0' else 5126
        pack = struct.Struct('<' + ('H' if component == 5123 else 'f') * width)
        for row in rows:
            binary.extend(pack.pack(*row))
        view = {'buffer': len(document['buffers']), 'byteOffset': offset,
                'byteLength': len(binary) - offset}
        view['target'] = 34962
        new_views.append(view)
        accessor = {'bufferView': len(document['bufferViews']) + len(new_views) - 1,
                    'componentType': component, 'count': len(rows), 'type': f'VEC{width}'}
        if key[0] == 'POSITION':
            accessor['min'], accessor['max'] = bounds(rows)
        accessor_lookup[key] = len(document['accessors']) + len(new_accessors)
        new_accessors.append(accessor)
    buffer_path.write_bytes(binary)
    document['buffers'].append({'uri': buffer_path.relative_to(output_root).as_posix(), 'byteLength': len(binary)})
    document['bufferViews'].extend(new_views)
    document['accessors'].extend(new_accessors)
    for node_index, name, primitive in plans:
        for semantic in ('POSITION', 'NORMAL', 'TANGENT', 'JOINTS_0'):
            if semantic in primitive['attributes']:
                primitive['attributes'][semantic] = accessor_lookup[(semantic, primitive['attributes'][semantic])]
        primitive.setdefault('extras', {})[MARKER] = {'version': MARKER_VERSION, 'matrix': list(matrix),
                                                     'source_node': nodes[node_index]['name'], 'skin': name,
                                                     'positive_joint_lane_offset': -1}
    document.setdefault('extras', {})[MARKER] = {'version': MARKER_VERSION, 'matrix': list(matrix),
                                                'positive_joint_lane_offset': -1,
                                                'original_joint_palettes_preserved': True}
    return {'status': 'candidate_repaired', 'repaired_primitives': len(plans),
            'repaired_skins': list(SKIN_MESHES), 'source_rest_node': trim_node,
            'rest_matrix': list(matrix), 'new_accessors': len(new_accessors),
            'new_buffer_bytes': len(binary), 'meshes': records,
            'joint_index_correction': {'positive_weight_offset': -1, 'source_range': [1, 11],
                                       'original_palettes_preserved': True, 'inverse_binds_preserved': True},
            'verification': 'Source-space regression passed; Blender/native rebuild verification still required.'}
