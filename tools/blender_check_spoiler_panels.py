"""Small source-guard, fixed-hardware and rigid-motion regressions; no asset loads."""
import sys
from pathlib import Path
import unittest

import bpy
from mathutils import Matrix

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from fbw_spoiler_panels import prepare_rigid_spoiler_panels, spoiler_variant_specs
from convert_gltf_animations import clear_scene_content, rigidify_skinned_meshes


def memberships(obj, vertex):
    return {obj.vertex_groups[group.group].name: group.weight for group in obj.data.vertices[vertex].groups}


class SpoilerPanelTests(unittest.TestCase):
    def setUp(self):
        clear_scene_content()
        self.root = bpy.data.collections.new('spoiler_fixture')
        bpy.context.scene.collection.children.link(self.root)
        self.objects = set()
        rigs = {}
        for side in ('L', 'R'):
            rig = bpy.data.objects.new(f'WING_ARMATURE_{side}H', bpy.data.armatures.new('spoiler_rig'))
            self.root.objects.link(rig)
            self.objects.add(rig)
            bpy.context.view_layer.objects.active = rig
            rig.select_set(True)
            bpy.ops.object.mode_set(mode='EDIT')
            names = {name for spec in spoiler_variant_specs() if spec['side'] == side for name in spec['allowed_bones']}
            names.add('NEUTRAL_BONE_1')
            for index, name in enumerate(sorted(names)):
                bone = rig.data.edit_bones.new(name)
                bone.head, bone.tail = (index, 0, 0), (index, 1, 0)
            bpy.ops.object.mode_set(mode='OBJECT')
            rig.select_set(False)
            rigs[side] = rig
        for spec in spoiler_variant_specs():
            name = spec['object']
            fixed = spec['fixed_polygons']
            vertices = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
            faces = [(0, 1, 2)] * (spec['polygons']-fixed)
            if fixed:
                vertices += [(3, 0, 0), (3.03, 0, 0), (3, .03, 0)]
                faces += [(3, 4, 5)] * fixed
            mesh = bpy.data.meshes.new(name)
            mesh.from_pydata(vertices, [], faces)
            mesh.uv_layers.new(name='UVMap')
            for i, item in enumerate(mesh.uv_layers.active.data):
                item.uv = ((i % 3)*.2, (i % 5)*.1)
            material = bpy.data.materials.get(spec['material']) or bpy.data.materials.new(spec['material'])
            mesh.materials.append(material)
            obj = bpy.data.objects.new(name, mesh)
            self.root.objects.link(obj)
            obj.matrix_world = Matrix.Translation((2, 3, 4)) @ Matrix.Rotation(.3, 4, 'Z')
            for bone in spec['allowed_bones']:
                obj.vertex_groups.new(name=bone).add([0, 1, 2], .5, 'REPLACE')
            if fixed:
                obj.vertex_groups.new(name='NEUTRAL_BONE_1').add([3, 4, 5], 1., 'REPLACE')
            modifier = obj.modifiers.new('baked_armature', 'ARMATURE')
            modifier.object = rigs[spec['side']]
            modifier.show_viewport = modifier.show_render = False
            self.objects.add(obj)
        bpy.context.view_layer.update()

    def assert_unmodified_weights(self):
        obj = bpy.data.objects['Cylinder.143']
        self.assertEqual(memberships(obj, 0), {'WING_SPOILER1_1.L': .5, 'WING_SPOILER1_2.L': .5})

    def test_neutral_geometry_uv_normals_transforms_and_hardware_preserved(self):
        before = {obj.name: (obj.matrix_world.copy(), [tuple(v.co) for v in obj.data.vertices],
                            [tuple(v.normal) for v in obj.data.vertices], [tuple(v.uv) for v in obj.data.uv_layers.active.data],
                            [tuple(p.vertices) for p in obj.data.polygons])
                  for obj in self.objects if obj.type == 'MESH'}
        hardware = bpy.data.objects['Cylinder.152__a380_exterior_wing1_2']
        hardware_before = [memberships(hardware, i) for i in (3, 4, 5)]
        report = prepare_rigid_spoiler_panels(self.objects)
        self.assertEqual(report['material_variant_count'], 32)
        self.assertEqual(report['total_polygons'], 45076)
        self.assertEqual(report['fixed_polygons_preserved'], 46)
        for obj in self.objects:
            if obj.type != 'MESH':
                continue
            matrix, positions, normals, uv, faces = before[obj.name]
            self.assertEqual(obj.matrix_world, matrix)
            self.assertEqual([tuple(v.co) for v in obj.data.vertices], positions)
            self.assertEqual([tuple(v.normal) for v in obj.data.vertices], normals)
            self.assertEqual([tuple(v.uv) for v in obj.data.uv_layers.active.data], uv)
            self.assertEqual([tuple(p.vertices) for p in obj.data.polygons], faces)
        self.assertEqual([memberships(hardware, i) for i in (3, 4, 5)], hardware_before)
        partitions = rigidify_skinned_meshes(self.objects, self.root)
        self.assertEqual(len(partitions), 32)
        self.assertEqual(sum(p['source_polygons'] for p in partitions), 45076)
        self.assertTrue(all(part['bone'] is not None for p in partitions for part in p['parts']))
        self.assertEqual(sum(part['polygons'] for p in partitions for part in p['parts'] if part['bone'] == 'NEUTRAL_BONE_1'), 46)
        for p in partitions:
            for part in p['parts']:
                obj = bpy.data.objects[part['object']]
                matrix = before[p['object']][0]
                self.assertLess(max(abs(obj.matrix_world[i][j]-matrix[i][j]) for i in range(4) for j in range(4)), 1e-5)

    def test_complete_skins_move_rigidly_and_return_hardware_stays_fixed(self):
        prepare_rigid_spoiler_panels(self.objects)
        partitions = rigidify_skinned_meshes(self.objects, self.root)
        parts = [bpy.data.objects[part['object']] for p in partitions for part in p['parts']]
        neutral = {o.name: [o.matrix_world @ v.co for v in o.data.vertices] for o in parts}
        for rig in [o for o in self.objects if o.type == 'ARMATURE']:
            for bone in rig.pose.bones:
                if 'SPOILER' in bone.name:
                    bone.rotation_mode = 'XYZ'
                    bone.rotation_euler.x = .6
        bpy.context.view_layer.update()
        for obj in parts:
            moved = [obj.matrix_world @ v.co for v in obj.data.vertices]
            distance = max((a-b).length for a, b in zip(neutral[obj.name], moved))
            if obj.parent_bone == 'NEUTRAL_BONE_1':
                self.assertLess(distance, 1e-5)
            else:
                self.assertGreater(distance, .1)
                self.assertAlmostEqual((moved[0]-moved[1]).length, (neutral[obj.name][0]-neutral[obj.name][1]).length, places=5)
        for rig in [o for o in self.objects if o.type == 'ARMATURE']:
            for bone in rig.pose.bones:
                bone.rotation_euler = (0, 0, 0)
        bpy.context.view_layer.update()
        for obj in parts:
            self.assertLess(max((obj.matrix_world @ v.co-p).length for v, p in zip(obj.data.vertices, neutral[obj.name])), 1e-5)

    def test_missing_variant_rejected_before_any_mutation(self):
        self.objects.remove(bpy.data.objects['Cylinder.240__a380_exterior_wing2_2'])
        with self.assertRaisesRegex(ValueError, 'all sixteen'):
            prepare_rigid_spoiler_panels(self.objects)
        self.assert_unmodified_weights()

    def test_wrong_material_rejected_before_any_mutation(self):
        bpy.data.objects['Cylinder.240'].data.materials[0] = bpy.data.materials.new('incorrect')
        with self.assertRaisesRegex(ValueError, 'material'):
            prepare_rigid_spoiler_panels(self.objects)
        self.assert_unmodified_weights()

    def test_active_skin_modifier_rejected(self):
        bpy.data.objects['Cylinder.240'].modifiers[0].show_render = True
        with self.assertRaisesRegex(ValueError, 'prior static'):
            prepare_rigid_spoiler_panels(self.objects)
        self.assert_unmodified_weights()

    def test_unknown_controls_rejected_before_any_mutation(self):
        bpy.data.objects['Cylinder.240'].vertex_groups.new(name='WING_FLAP_11.R').add([0], .1, 'REPLACE')
        with self.assertRaisesRegex(ValueError, 'Unexpected control'):
            prepare_rigid_spoiler_panels(self.objects)
        self.assert_unmodified_weights()

    def test_hardware_must_be_isolated_not_blended_with_skin(self):
        obj = bpy.data.objects['Cylinder.152__a380_exterior_wing1_2']
        obj.vertex_groups['NEUTRAL_BONE_1'].remove([3])
        obj.vertex_groups['WING_SPOILER2_1.L'].add([3], 1., 'REPLACE')
        with self.assertRaisesRegex(ValueError, 'share a face'):
            prepare_rigid_spoiler_panels(self.objects)
        self.assert_unmodified_weights()

    def test_material_index_guarded(self):
        obj = bpy.data.objects['Cylinder.240']
        obj.data.polygons[0].material_index = 1
        with self.assertRaisesRegex(ValueError, 'material'):
            prepare_rigid_spoiler_panels(self.objects)
        self.assert_unmodified_weights()

    def test_exact_fixed_hardware_face_count_guarded(self):
        obj = bpy.data.objects['Cylinder.152__a380_exterior_wing1_2']
        face = obj.data.polygons[-1]
        for offset in range(3):
            obj.data.loops[face.loop_start+offset].vertex_index = offset
        with self.assertRaisesRegex(ValueError, 'hardware count'):
            prepare_rigid_spoiler_panels(self.objects)
        self.assert_unmodified_weights()

    def test_exact_material_variant_triangle_count_guarded(self):
        bpy.data.objects['Cylinder.240'].data.clear_geometry()
        with self.assertRaisesRegex(ValueError, 'topology'):
            prepare_rigid_spoiler_panels(self.objects)
        self.assert_unmodified_weights()


if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(SpoilerPanelTests))
    if not result.wasSuccessful():
        raise RuntimeError('SPOILER_PANEL_REGRESSION failed')
    print(f'SPOILER_PANEL_REGRESSION: PASS ({result.testsRun} tests)')
