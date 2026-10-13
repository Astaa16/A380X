#!/usr/bin/env python3
"""Lightweight mutation-contract tests; no Blender or installed assets needed."""
import copy
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace as NS

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from fbw_tail_panels import MESH_JOINT_FAMILIES, TAIL_PARTS, prepare_rigid_tail_panels


class Groups(list):
    def __init__(self, obj, names):
        super().__init__()
        self.obj = obj
        for name in names:
            self.new(name)

    def clear(self):
        for vertex in self.obj.data.vertices:
            vertex.groups = []
        super().clear()

    def new(self, name):
        group = NS(name=name, index=len(self))
        def add(indices, weight, mode):
            assert mode == 'REPLACE'
            for index in indices:
                self.obj.data.vertices[index].groups.append(NS(group=group.index, weight=weight))
        group.add = add
        self.append(group)
        return group


def fixture():
    objects = []
    for name, (side, function, vertices, triangles, target) in TAIL_PARTS.items():
        allowed = MESH_JOINT_FAMILIES[name]
        data = NS(vertices=[NS(index=i, co=(i / 100., 3., -30.), normal=(0., 1., 0.),
                               groups=[NS(group=i % len(allowed), weight=.4),
                                       NS(group=(i + 1) % len(allowed), weight=.6)]) for i in range(vertices)],
                  polygons=[NS(vertices=(i % vertices, (i+1) % vertices, (i+2) % vertices), material_index=0)
                            for i in range(triangles)],
                  materials=[NS(name='A380X_FUSE4')], uv_layers=[(0., 0.), (.5, .5), (1., 1.)])
        rig = NS(name='ELEVATOR_WINGFLEX_LH' + ('.001' if side == 'R' else ''),
                 data=NS(bones={bone: object() for bone in allowed}))
        obj = NS(name=name, type='MESH', data=data, matrix_world=tuple(range(16)),
                 modifiers=[NS(type='ARMATURE', object=rig, show_viewport=True, show_render=True)])
        obj.vertex_groups = Groups(obj, allowed)
        objects.append(obj)
    return objects


def geometry_snapshot(objects):
    return [(obj.name, [(v.co, v.normal) for v in obj.data.vertices],
             [(p.vertices, p.material_index) for p in obj.data.polygons],
             list(obj.data.uv_layers), obj.matrix_world, [m.name for m in obj.data.materials],
             [(m.type, m.object.name, m.show_viewport, m.show_render) for m in obj.modifiers]) for obj in objects]


def weight_snapshot(objects):
    return [[[(g.group, g.weight) for g in v.groups] for v in obj.data.vertices] for obj in objects]


class TailPanelTests(unittest.TestCase):
    def test_whole_panels_and_fixed_skin_preserve_all_geometry(self):
        objects = fixture()
        before = geometry_snapshot(objects)
        report = prepare_rigid_tail_panels(objects)
        self.assertEqual(geometry_snapshot(objects), before)
        self.assertEqual(report['elevator_panel_count'], 4)
        self.assertEqual(sum(p['polygons'] for p in report['panels']), 14356)
        for obj in objects:
            self.assertEqual([group.name for group in obj.vertex_groups], [TAIL_PARTS[obj.name][-1]])
            self.assertTrue(all([(g.group, g.weight) for g in v.groups] == [(0, 1.)] for v in obj.data.vertices))
        for p in report['panels']:
            self.assertEqual(p['representative_bone'].startswith('EW_'), p['function'].startswith('fixed'))

    def test_atomic_validation_of_last_target(self):
        objects = fixture()
        objects[-1].data.vertices[-1].groups[0].group = 999
        before = weight_snapshot(objects)
        with self.assertRaisesRegex(ValueError, 'v3'):
            prepare_rigid_tail_panels(objects)
        self.assertEqual(weight_snapshot(objects), before)

    def test_rejects_old_zero_based_family_mix(self):
        objects = fixture()
        # The old decoding incorrectly put the left inner root on neutral_bone.
        obj = objects[0]
        obj.vertex_groups[0].name = 'neutral_bone'
        with self.assertRaisesRegex(ValueError, 'v3'):
            prepare_rigid_tail_panels(objects)

    def test_guards_structure_material_rig_and_weight(self):
        for kind in ('missing', 'topology', 'quad', 'material', 'rig', 'bone', 'modifier', 'nan', 'negative', 'zero', 'incomplete'):
            with self.subTest(kind=kind):
                objects = fixture()
                obj = objects[-1]
                if kind == 'missing': objects.pop()
                elif kind == 'topology': obj.data.polygons.pop()
                elif kind == 'quad': obj.data.polygons[0].vertices = (0, 1, 2, 3)
                elif kind == 'material': obj.data.materials[0].name = 'Wrong'
                elif kind == 'rig': obj.modifiers[0].object.name = 'Wrong'
                elif kind == 'bone': obj.modifiers[0].object.data.bones.clear()
                elif kind == 'modifier': obj.modifiers.append(NS(type='ARMATURE'))
                elif kind == 'nan': obj.data.vertices[0].groups[0].weight = float('nan')
                elif kind == 'negative': obj.data.vertices[0].groups[0].weight = -1.
                elif kind == 'zero': obj.data.vertices[0].groups = []
                elif kind == 'incomplete':
                    for vertex in obj.data.vertices: vertex.groups = [NS(group=0, weight=1.)]
                with self.assertRaises(ValueError):
                    prepare_rigid_tail_panels(objects)


if __name__ == '__main__':
    unittest.main()
