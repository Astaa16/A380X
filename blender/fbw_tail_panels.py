"""Keep the four audited FBW elevators whole after v3 tail-index decoding.

The ten source meshes separate exactly into fixed THS skin/hardware and four
elevator panels once their one-based JOINTS lanes are decoded. No fixed/control
mixture is approximated. Each elevator family's source joint deltas agree to
2.410 micrometres on its actual vertices over all 21 authored elevator keys.

Run before generic rigid partitioning, using the v3 bind-shape-corrected neutral
coordinates. Do not evaluate/bake the complete fuselage armature here. This
helper changes weights only; live horizontal-tail flex is not implemented.
"""
from __future__ import annotations

import math

from repair_fbw_tail_bindshape import MESH_JOINT_FAMILIES

# Name: side, function, vertex count, triangle count, representative bone.
TAIL_PARTS = {
    'Cube.118': ('L', 'inner_elevator', 614, 620, 'E_BONE_INNER1.L'),
    'Cube.119': ('L', 'outer_elevator', 588, 572, 'E_BONE_OUTER3.L'),
    'Cube.369': ('L', 'fixed_stabilizer', 1796, 2670, 'EW_BONE2.L'),
    'Cube.370': ('L', 'fixed_stabilizer', 3005, 3144, 'EW_BONE2.L'),
    'Cube.371': ('L', 'fixed_hardware', 166, 172, 'EW_BONE2.L'),
    'Cube.101': ('R', 'inner_elevator', 627, 620, 'E_BONE_INNER1.R'),
    'Cube.100': ('R', 'outer_elevator', 582, 572, 'E_BONE_OUTER3.R'),
    'Cube.099': ('R', 'fixed_stabilizer', 1793, 2670, 'EW_BONE2.R'),
    'Cube.104': ('R', 'fixed_stabilizer', 3005, 3144, 'EW_BONE2.R'),
    'Cube.116': ('R', 'fixed_hardware', 166, 172, 'EW_BONE2.R'),
}


def prepare_rigid_tail_panels(objects):
    """Validate every source family before mutating any group membership."""
    objects = list(objects)
    by_name = {obj.name: obj for obj in objects}
    if len(by_name) != len(objects) or not TAIL_PARTS.keys() <= by_name.keys():
        raise ValueError('Rigid tail repair requires all ten uniquely named audited meshes')
    plans = []
    for name, (side, function, vertices, triangles, target) in TAIL_PARTS.items():
        obj = by_name[name]
        if (obj.type != 'MESH' or len(obj.data.vertices) != vertices
                or len(obj.data.polygons) != triangles
                or any(len(face.vertices) != 3 for face in obj.data.polygons)):
            raise ValueError(f'Unexpected tail topology: {name}')
        if (len(obj.data.materials) != 1 or obj.data.materials[0] is None
                or obj.data.materials[0].name != 'A380X_FUSE4'
                or any(face.material_index != 0 for face in obj.data.polygons)):
            raise ValueError(f'Unexpected tail material: {name}')
        if len(obj.modifiers) != 1 or obj.modifiers[0].type != 'ARMATURE':
            raise ValueError(f'Expected one retained tail armature modifier: {name}')
        rig = obj.modifiers[0].object
        expected_rig = 'ELEVATOR_WINGFLEX_LH' + ('.001' if side == 'R' else '')
        allowed = set(MESH_JOINT_FAMILIES[name])
        if rig is None or rig.name != expected_rig or not allowed <= set(rig.data.bones.keys()):
            raise ValueError(f'Unexpected tail rig or missing bones: {name}')
        groups = {group.index: group.name for group in obj.vertex_groups}
        observed = set()
        for index, vertex in enumerate(obj.data.vertices):
            if vertex.index != index:
                raise ValueError(f'Unexpected tail vertex index: {name}')
            if any(not math.isfinite(group.weight) or group.weight < 0 for group in vertex.groups):
                raise ValueError(f'Invalid tail skin weight: {name} vertex {index}')
            influences = {groups.get(group.group) for group in vertex.groups if group.weight > 0}
            if not influences or not influences <= allowed:
                raise ValueError(f'Tail needs v3 one-based joint decoding; unexpected family on {name}: {influences}')
            observed.update(influences)
        if observed != allowed:
            raise ValueError(f'Incomplete audited tail influence set: {name}')
        plans.append((obj, function, target, sorted(observed)))

    reports = []
    for obj, function, target, observed in plans:
        obj.vertex_groups.clear()
        obj.vertex_groups.new(name=target).add(list(range(len(obj.data.vertices))), 1., 'REPLACE')
        reports.append({'object': obj.name, 'function': function, 'representative_bone': target,
                        'source_influences': observed, 'vertices': len(obj.data.vertices),
                        'polygons': len(obj.data.polygons)})
    return {'enabled': True, 'panels': reports, 'elevator_panel_count': 4,
            'fixed_mesh_count': 6, 'elevator_polygons': 2384, 'fixed_polygons': 11972,
            'neutral_geometry_changed': False, 'source_sample_max_error_m': 0.000002410,
            'limitation': 'Four rigid elevators and fixed neutral tail flex; live elastic horizontal-tail deformation is not implemented.'}
