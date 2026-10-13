#!/usr/bin/env python3
"""Render exported OBJ8 geometry in a neutral gear-down pose.

Run with Blender --background --python this.py -- --obj part.obj --output out.png.
Nested animations are evaluated; unknown datarefs default to zero. Optional
--dataref NAME=VALUE overrides let a specific flight-control pose be inspected.
Backfaces are culled in Cycles, matching native X-Plane's clockwise front faces.
"""
import argparse
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from obj8_preview import load_obj8


def make_material(name, texture, cull, cutoff):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    material.use_backface_culling = cull
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    nodes.clear()
    output = nodes.new('ShaderNodeOutputMaterial')
    shader = nodes.new('ShaderNodeEmission')
    shader.inputs['Color'].default_value = (.8, .8, .8, 1)
    transparent = nodes.new('ShaderNodeBsdfTransparent')
    surface = shader.outputs[0]
    if texture:
        image = nodes.new('ShaderNodeTexImage')
        image.image = bpy.data.images.load(str(texture), check_existing=True)
        links.new(image.outputs['Color'], shader.inputs['Color'])
        alpha = image.outputs['Alpha']
        if cutoff is not None:
            threshold = nodes.new('ShaderNodeMath')
            threshold.operation = 'GREATER_THAN'
            threshold.inputs[1].default_value = cutoff
            links.new(alpha, threshold.inputs[0])
            alpha = threshold.outputs[0]
        mix = nodes.new('ShaderNodeMixShader')
        links.new(alpha, mix.inputs[0])
        links.new(transparent.outputs[0], mix.inputs[1])
        links.new(surface, mix.inputs[2])
        surface = mix.outputs[0]
    if cull:
        # material.use_backface_culling only affects rasterized viewport modes;
        # Cycles must explicitly make geometric backfaces transparent.
        geometry = nodes.new('ShaderNodeNewGeometry')
        front_only = nodes.new('ShaderNodeMixShader')
        links.new(geometry.outputs['Backfacing'], front_only.inputs[0])
        links.new(surface, front_only.inputs[1])
        links.new(transparent.outputs[0], front_only.inputs[2])
        surface = front_only.outputs[0]
    links.new(surface, output.inputs['Surface'])
    return material


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--obj', type=Path, action='append', default=[])
    parser.add_argument('--obj-dir', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--flip-v', action='store_true')
    parser.add_argument('--cockpit', action='store_true', help='Perspective at captain eye in source model coordinates')
    parser.add_argument('--cockpit-heading', type=float, default=0, help='Cockpit look angle in degrees; positive looks right')
    parser.add_argument('--cockpit-pitch', type=float, default=-7.96961039432136, help='Cockpit look angle in degrees; positive looks up')
    parser.add_argument('--texture-root', type=Path, help='Fallback directory for un-packaged export texture filenames')
    parser.add_argument('--export-build-root', type=Path, help='Resolve generated ../textures/group headers from the build root')
    parser.add_argument('--dataref', action='append', default=[], metavar='NAME=VALUE', help='Override a neutral-ground-pose dataref')
    parser.add_argument(
        '--draw-index', type=int, action='append', default=[],
        help='Render only the selected zero-based OBJ8 draw call; may be repeated',
    )
    parser.add_argument('--lod-distance', type=float, default=0, help='Distance used to select OBJ LODs, in meters')
    parser.add_argument('--camera-position', type=float, nargs=3, help='Preview-space camera XYZ (X=OBJ X, Y=-OBJ Z, Z=OBJ Y)')
    parser.add_argument('--camera-target', type=float, nargs=3, help='Preview-space look-at XYZ; requires camera-position')
    parser.add_argument('--ortho-scale', type=float, help='Orthographic image span in meters')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    if not math.isfinite(args.cockpit_heading) or not math.isfinite(args.cockpit_pitch):
        parser.error('Cockpit view angles must be finite')
    if bool(args.camera_position) != bool(args.camera_target) or (args.cockpit and args.camera_position):
        parser.error('Specify camera-position and camera-target together, without cockpit mode')
    if args.camera_position and (not all(math.isfinite(v) for v in (*args.camera_position, *args.camera_target)) or args.camera_position == args.camera_target):
        parser.error('Camera points must be finite and distinct')
    if args.ortho_scale is not None and (not math.isfinite(args.ortho_scale) or args.ortho_scale <= 0):
        parser.error('Ortho scale must be positive and finite')
    values = {}
    for override in args.dataref:
        name, separator, value = override.partition('=')
        if not separator or not name:
            raise ValueError(f'Expected --dataref NAME=VALUE, received {override!r}')
        values[name] = float(value)
    for directory in args.obj_dir:
        args.obj.extend(sorted(p for p in directory.glob('*.obj') if not p.name.startswith('._')))
    if not args.obj:
        raise ValueError('No OBJ files selected')
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    lo, hi = None, None
    for path in args.obj:
        preview = load_obj8(path, values=values, lod_distance=args.lod_distance)
        if args.draw_index:
            invalid = sorted(set(args.draw_index) - set(range(len(preview.draws))))
            if invalid:
                raise ValueError(f'{path}: draw indexes out of range: {invalid}')
            selected = set(args.draw_index)
            preview.draws = [draw for index, draw in enumerate(preview.draws) if index in selected]
        vertices, uv, faces, styles = preview.bake(flip_v=args.flip_v)
        if not faces:
            print(f'Preview skips {path.name}: no visible triangles at requested pose/LOD')
            continue
        texture = None
        if preview.texture:
            texture = (path.parent / preview.texture).resolve()
            if not texture.is_file() and args.export_build_root and preview.texture.startswith('../textures/'):
                texture = args.export_build_root / preview.texture[3:]
        if texture and not texture.is_file() and args.texture_root:
            texture = args.texture_root / texture.name
        if texture and not texture.is_file():
            raise FileNotFoundError(f'{path}: texture not found: {texture}')
        mesh = bpy.data.meshes.new(path.stem)
        mesh.from_pydata(vertices, [], faces)
        mesh.uv_layers.new(name='UVMap')
        material_indices = {}
        for style in dict.fromkeys(styles):
            material_indices[style] = len(mesh.materials)
            mesh.materials.append(make_material(f'{path.stem}_{len(mesh.materials)}', texture, *style))
        for poly in mesh.polygons:
            poly.material_index = material_indices[styles[poly.index]]
            for li in poly.loop_indices:
                mesh.uv_layers[0].data[li].uv = uv[mesh.loops[li].vertex_index]
        obj = bpy.data.objects.new(path.stem, mesh)
        bpy.context.collection.objects.link(obj)
        part_lo = tuple(min(v[i] for v in vertices) for i in range(3))
        part_hi = tuple(max(v[i] for v in vertices) for i in range(3))
        lo = part_lo if lo is None else tuple(min(lo[i], part_lo[i]) for i in range(3))
        hi = part_hi if hi is None else tuple(max(hi[i], part_hi[i]) for i in range(3))
        print(f'Preview {path.name}: {len(faces)} triangles, {len(preview.draws)} draw calls; datarefs={preview.datarefs}')
        # Do not keep a second Python geometry copy after Blender has its mesh.
        del vertices, uv, faces, styles, preview
    if lo is None:
        raise ValueError('No visible triangles in selected OBJ files')
    lo, hi = Vector(lo), Vector(hi)
    center = (lo+hi)/2
    bpy.ops.object.camera_add(location=center + Vector((65, -15, 12)))
    camera = bpy.context.object
    camera.rotation_euler = (center-camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = max(hi-lo)*1.25
    if args.camera_position:
        camera.location = Vector(args.camera_position)
        camera.rotation_euler = (Vector(args.camera_target)-camera.location).to_track_quat('-Z', 'Y').to_euler()
    if args.ortho_scale is not None:
        camera.data.ortho_scale = args.ortho_scale
    if args.cockpit:
        camera.location = Vector((0.530352, -32.00644, 3.2004))
        heading, pitch = map(math.radians, (args.cockpit_heading, args.cockpit_pitch))
        # OBJ8 preview coordinates: forward -Y, right -X, up +Z.
        target = camera.location + Vector((-math.sin(heading) * math.cos(pitch),
                                           -math.cos(heading) * math.cos(pitch), math.sin(pitch)))
        camera.rotation_euler = (target-camera.location).to_track_quat('-Z', 'Y').to_euler()
        camera.data.type = 'PERSP'
        camera.data.lens = 20
        camera.data.clip_start = .015
    scene = bpy.context.scene
    scene.camera = camera
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 8
    scene.cycles.transparent_max_bounces = 32
    scene.render.resolution_x = 1100
    scene.render.resolution_y = 800
    scene.render.resolution_percentage = 100
    scene.world.color = (.12, .12, .12)
    scene.view_settings.view_transform = 'Standard'
    scene.render.filepath = str(args.output)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.render.render(write_still=True)


if __name__ == '__main__':
    main()
