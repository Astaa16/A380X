"""Small source-guard and whole-panel parenting regressions; no asset loads."""
import sys
from pathlib import Path
import unittest

import bpy
from mathutils import Matrix

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from fbw_flap_panels import FLAP_PANELS, prepare_rigid_flap_panels
from convert_gltf_animations import clear_scene_content, rigidify_skinned_meshes


class FlapPanelTests(unittest.TestCase):
    def setUp(self):
        clear_scene_content()
        self.root = bpy.data.collections.new('fixture')
        bpy.context.scene.collection.children.link(self.root)
        rigs = {}
        self.objects = set()
        for side in ('L', 'R'):
            rig = bpy.data.objects.new(f'WING_ARMATURE_{side}H', bpy.data.armatures.new('fixture_rig'))
            self.root.objects.link(rig)
            self.objects.add(rig)
            bpy.context.view_layer.objects.active = rig
            rig.select_set(True)
            bpy.ops.object.mode_set(mode='EDIT')
            for number in range(2, 12):
                bone = rig.data.edit_bones.new(f'WING_FLAP_{number}.{side}')
                bone.head, bone.tail = (number, 0, 0), (number, 1, 0)
            bpy.ops.object.mode_set(mode='OBJECT')
            rig.select_set(False)
            rigs[side] = rig
        for name, (side, numbers, representative, count) in FLAP_PANELS.items():
            mesh = bpy.data.meshes.new(name)
            mesh.from_pydata([(0, 0, 0), (1, 0, 0), (0, 1, 0)], [], [(0, 1, 2)] * count)
            mesh.uv_layers.new(name='UVMap')
            for item in mesh.uv_layers.active.data:
                item.uv = (.2, .7)
            obj = bpy.data.objects.new(name, mesh)
            self.root.objects.link(obj)
            obj.matrix_world = Matrix.Translation((2, 3, 4)) @ Matrix.Rotation(.3, 4, 'Z')
            for number in numbers:
                obj.vertex_groups.new(name=f'WING_FLAP_{number}.{side}').add([0, 1, 2], 1 / len(numbers), 'REPLACE')
            modifier = obj.modifiers.new('baked_armature', 'ARMATURE')
            modifier.object = rigs[side]
            modifier.show_viewport = modifier.show_render = False
            self.objects.add(obj)
        bpy.context.view_layer.update()

    def test_neutral_mesh_uv_world_and_all_faces_preserved(self):
        before = {o.name: (o.matrix_world.copy(), [v.co.copy() for v in o.data.vertices],
                          [tuple(v.uv) for v in o.data.uv_layers.active.data])
                  for o in self.objects if o.type == 'MESH'}
        report = prepare_rigid_flap_panels(self.objects)
        self.assertEqual(len(report['panels']), 6)
        for obj in self.objects:
            if obj.type != 'MESH':
                continue
            matrix, positions, uv = before[obj.name]
            self.assertEqual(obj.matrix_world, matrix)
            self.assertEqual([v.co for v in obj.data.vertices], positions)
            self.assertEqual([tuple(v.uv) for v in obj.data.uv_layers.active.data], uv)
            self.assertEqual(len(obj.vertex_groups), 1)
        partitions = rigidify_skinned_meshes(self.objects, self.root)
        self.assertEqual(len(partitions), 6)
        for part in partitions:
            self.assertEqual(len(part['parts']), 1)
            self.assertIsNotNone(part['parts'][0]['bone'])
            self.assertEqual(part['source_polygons'], part['parts'][0]['polygons'])
            obj = bpy.data.objects[part['parts'][0]['object']]
            matrix = before[part['object']][0]
            self.assertLess(max(abs(obj.matrix_world[i][j] - matrix[i][j]) for i in range(4) for j in range(4)), 1e-5)

    def test_enabled_modifier_rejected_before_mutation(self):
        bpy.data.objects['Cylinder.234'].modifiers[0].show_viewport = True
        with self.assertRaisesRegex(ValueError, 'prior static'):
            prepare_rigid_flap_panels(self.objects)
        self.assertEqual(len(bpy.data.objects['FLAPS_SURFACE_LH_IN'].vertex_groups), 3)

    def test_wrong_control_weights_rejected(self):
        obj = bpy.data.objects['Cylinder.235']
        obj.vertex_groups.new(name='WING_SLAT_2.R').add([0], .25, 'REPLACE')
        with self.assertRaisesRegex(ValueError, 'Unexpected control'):
            prepare_rigid_flap_panels(self.objects)

    def test_missing_panel_rejected(self):
        self.objects.remove(bpy.data.objects['Cylinder.233'])
        with self.assertRaisesRegex(ValueError, 'all six'):
            prepare_rigid_flap_panels(self.objects)

    def test_complete_panel_moves_and_returns_without_internal_tearing(self):
        prepare_rigid_flap_panels(self.objects)
        partitions = rigidify_skinned_meshes(self.objects, self.root)
        for part in partitions:
            obj = bpy.data.objects[part['parts'][0]['object']]
            bone = obj.parent.pose.bones[obj.parent_bone]
            neutral = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
            bone.rotation_mode = 'XYZ'
            bone.rotation_euler.x = .5
            bpy.context.view_layer.update()
            moved = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
            self.assertGreater(max((a - b).length for a, b in zip(neutral, moved)), .1)
            for i in range(3):
                self.assertAlmostEqual((neutral[i] - neutral[(i+1)%3]).length,
                                       (moved[i] - moved[(i+1)%3]).length, places=5)
            bone.rotation_euler.x = 0
            bpy.context.view_layer.update()
            self.assertLess(max((obj.matrix_world @ v.co - p).length for v, p in zip(obj.data.vertices, neutral)), 1e-5)


if __name__ == '__main__':
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(FlapPanelTests))
    if not result.wasSuccessful():
        raise RuntimeError('FLAP_PANEL_REGRESSION failed')
    print('FLAP_PANEL_REGRESSION: PASS (5 tests)')
