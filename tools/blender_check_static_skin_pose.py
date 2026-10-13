#!/usr/bin/env python3
"""Synthetic, small Blender regression for the opt-in neutral skin bake.

Run only when memory-intensive simulator/export jobs have stopped:
  blender --background --factory-startup --python-exit-code 1 \
    --python tools/blender_check_static_skin_pose.py

No assets are imported and no files are written. XPlane2Blender is not needed.
The checks exercise real armature evaluation, mixed/rigid boundary vertices,
all-blended meshes, nonidentity world transforms, and subsequent rigid motion.
"""
from __future__ import annotations

import math
import re
import sys
import unittest
from pathlib import Path

import bpy
from mathutils import Matrix

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "blender"))
import convert_gltf_animations as converter


def world_vertices(obj, evaluated=False):
    target = obj.evaluated_get(bpy.context.evaluated_depsgraph_get()) if evaluated else obj
    return [target.matrix_world @ vertex.co for vertex in target.data.vertices]


class StaticSkinPoseTests(unittest.TestCase):
    def setUp(self):
        converter.clear_scene_content()
        self.root = bpy.data.collections.new("synthetic_static_skin")
        bpy.context.scene.collection.children.link(self.root)
        armature = bpy.data.armatures.new("synthetic_rig")
        self.rig = bpy.data.objects.new("synthetic_rig", armature)
        self.root.objects.link(self.rig)
        bpy.context.view_layer.objects.active = self.rig
        self.rig.select_set(True)
        bpy.ops.object.mode_set(mode="EDIT")
        flex = armature.edit_bones.new("WING_FLEX_1.L")
        flex.head, flex.tail = (0, 0, 0), (0, 2, 0)
        control = armature.edit_bones.new("WING_SLAT_2.L")
        control.head, control.tail = (0, 2, 0), (0, 3, 0)
        control.parent = flex
        bpy.ops.object.mode_set(mode="OBJECT")
        self.rig.matrix_world = (Matrix.Translation((4, -3, 2))
                                 @ Matrix.Rotation(.37, 4, "Y")
                                 @ Matrix.Diagonal((1.2, 1.2, 1.2, 1)))
        for bone in self.rig.pose.bones:
            bone.rotation_mode = "XYZ"
        self.flex = self.rig.pose.bones["WING_FLEX_1.L"]
        self.control = self.rig.pose.bones["WING_SLAT_2.L"]
        self.flex.rotation_euler.z = 0
        self.flex.keyframe_insert(data_path="rotation_euler", index=2, frame=0)
        self.flex.rotation_euler.z = math.radians(60)
        self.flex.keyframe_insert(data_path="rotation_euler", index=2, frame=10)
        action = self.rig.animation_data.action
        action.name = "synthetic_static_flex"
        for _action, curves in converter.actions_for_object(self.rig):
            for curve in curves:
                for point in curve.keyframe_points:
                    point.interpolation = "LINEAR"
        track = self.rig.animation_data.nla_tracks.new()
        track.name = "source_static_clip"
        track.strips.new(action.name, 0, action)
        track.mute = True
        self.rig.animation_data.action = None
        self.flex.rotation_euler.z = 0
        self.rules = [{"pattern": "synthetic_static_flex",
                       "compiled_pattern": re.compile("synthetic_static_flex"),
                       "static_fraction": .5}]
        self.mesh = self.make_mesh("mixed_and_rigid", False)
        self.blended = self.make_mesh("fully_blended", True)
        # An untouched skinned reference can be evaluated at later poses to
        # check that rigid fragments retain their original supported motion.
        self.reference = self.mesh.copy()
        self.reference.data = self.mesh.data.copy()
        self.reference.name = "reference_not_converted"
        bpy.context.scene.collection.objects.link(self.reference)
        self.objects = {self.rig, self.mesh, self.blended}
        bpy.context.view_layer.update()

    def make_mesh(self, name, all_blended):
        mesh = bpy.data.meshes.new(name)
        positions = [(0, 1, 0), (1, 1, 0), (0, 2, 0),
                     (1, 2, 0), (0, 3, 0), (1, 3, 0), (0, 4, 0)]
        faces = [(0, 1, 2), (2, 1, 3), (2, 3, 4), (4, 5, 6)]
        mesh.from_pydata(positions, [], faces)
        mesh.materials.append(bpy.data.materials.new(name + "_material"))
        uv = mesh.uv_layers.new(name="UVMap")
        for index, item in enumerate(uv.data):
            item.uv = (index / 12, 1 - index / 12)
        obj = bpy.data.objects.new(name, mesh)
        self.root.objects.link(obj)
        obj.parent = self.rig
        obj.matrix_world = (self.rig.matrix_world @ Matrix.Translation((.2, -.1, .3))
                            @ Matrix.Rotation(.12, 4, "X"))
        root_group = obj.vertex_groups.new(name="WING_FLEX_1.L")
        control_group = obj.vertex_groups.new(name="WING_SLAT_2.L")
        if all_blended:
            root_group.add(list(range(7)), .5, "REPLACE")
            control_group.add(list(range(7)), .5, "REPLACE")
        else:
            root_group.add([0, 1, 2], 1, "REPLACE")
            root_group.add([3], .5, "REPLACE")
            control_group.add([3], .5, "REPLACE")
            control_group.add([4, 5, 6], 1, "REPLACE")
        modifier = obj.modifiers.new("skin", "ARMATURE")
        modifier.object = self.rig
        return obj

    def expected_neutral(self):
        self.flex.rotation_euler.z = math.radians(30)
        self.rig.update_tag(refresh={"OBJECT", "DATA", "TIME"})
        bpy.context.view_layer.update()
        expected = world_vertices(self.mesh, True)
        blended = world_vertices(self.blended, True)
        self.flex.rotation_euler.z = 0
        self.rig.update_tag(refresh={"OBJECT", "DATA", "TIME"})
        bpy.context.view_layer.update()
        return expected, blended

    def repair(self):
        report = converter.bake_static_skin_pose(self.objects, self.rules, "unused")
        self.assertEqual(len(report["baked_meshes"]), 2)
        partitions = converter.rigidify_skinned_meshes(self.objects, self.root)
        parts = next(row["parts"] for row in partitions if row["object"] == self.mesh.name)
        return report, partitions, parts

    def assert_points_equal(self, actual, expected):
        self.assertEqual(len(actual), len(expected))
        self.assertLess(max((a - b).length for a, b in zip(actual, expected)), 2e-5)

    def test_shared_boundaries_and_world_transforms(self):
        expected, _ = self.expected_neutral()
        original_world = self.mesh.matrix_world.copy()
        report, _partitions, parts = self.repair()
        self.assertEqual({row["bone"] for row in parts}, {None, "WING_FLEX_1.L", "WING_SLAT_2.L"})
        self.assertLess(max(abs(a - b) for ra, rb in zip(original_world, self.mesh.matrix_world)
                            for a, b in zip(ra, rb)), 1e-6)
        for row in report["baked_meshes"]:
            self.assertLess(row["world_matrix_error"], 1e-6)
        points = [point for part in parts for point in world_vertices(bpy.data.objects[part["object"]])]
        self.assertTrue(all(min((point - source).length for source in expected) < 2e-5 for point in points))
        for index in (1, 2, 4):
            copies = [point for point in points if (point - expected[index]).length < 2e-5]
            self.assertGreaterEqual(len(copies), 2, f"Boundary vertex {index} lost its coincident copies")

    def test_rigid_motion_still_matches_original_skin(self):
        self.expected_neutral()
        _report, _partitions, parts = self.repair()
        control_part = bpy.data.objects[next(row["object"] for row in parts if row["bone"] == "WING_SLAT_2.L")]
        neutral = world_vertices(control_part)
        self.control.rotation_euler.x = .41
        self.rig.update_tag(refresh={"OBJECT", "DATA", "TIME"})
        bpy.context.view_layer.update()
        expected = world_vertices(self.reference, True)
        actual = world_vertices(control_part)
        self.assertTrue(all(min((point - expected[index]).length for index in (4, 5, 6)) < 2e-5 for point in actual))
        self.assertGreater(max((a - b).length for a, b in zip(actual, neutral)), .05)

    def test_all_blended_mesh_uses_same_neutral_pose_and_keeps_uvs(self):
        _expected, blended = self.expected_neutral()
        original_uv = [tuple(value.uv) for value in self.blended.data.uv_layers[0].data]
        _report, partitions, _parts = self.repair()
        self.assertEqual(next(row["status"] for row in partitions if row["object"] == self.blended.name), "blended_skin_static")
        self.assert_points_equal(world_vertices(self.blended), blended)
        self.assertEqual([tuple(value.uv) for value in self.blended.data.uv_layers[0].data], original_uv)
        self.assertFalse(self.blended.modifiers)

    def test_ambiguous_modifier_stack_is_rejected_before_mutation(self):
        self.mesh.modifiers.new("unexpected_geometry", "SOLIDIFY")
        old_mesh = self.mesh.data
        with self.assertRaisesRegex(ValueError, "one armature"):
            converter.bake_static_skin_pose(self.objects, self.rules, "unused")
        self.assertIs(self.mesh.data, old_mesh)
        self.assertAlmostEqual(self.flex.rotation_euler.z, 0)

    def test_no_static_mapping_does_not_change_skin(self):
        old_mesh = self.mesh.data
        report = converter.bake_static_skin_pose(self.objects, [], "unused")
        self.assertEqual(report["baked_meshes"], [])
        self.assertIs(self.mesh.data, old_mesh)
        self.assertTrue(self.mesh.modifiers[0].show_viewport)
        self.assertAlmostEqual(self.flex.rotation_euler.z, 0)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(StaticSkinPoseTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():
        raise RuntimeError("Static skin pose regression failed")
    print(f"STATIC_SKIN_POSE_REGRESSION: PASS ({result.testsRun} tests)")
