#!/usr/bin/env python3
"""Render installed engine OBJ8s without loading the complete aircraft.

Run with Blender --background --threads 2 --python this.py -- --output image.png.
--engines 1 2 shows the two left engines; --engines 2 isolates the inboard one.
Animation overrides use --dataref NAME=VALUE and the shared OBJ8 evaluator.
This is an offline material/geometry inspection, not a native X-Plane screenshot.
PBR approximates X-Plane material/F0 and gloss in Blender's Principled shader.
"""
from __future__ import annotations

import argparse
import itertools
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

PORT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PORT / 'blender'))
from obj8_preview import load_obj8


def obj_material(path):
    result = {'normal_metalness': False, 'gloss_scale': .5}
    for line in path.read_text().splitlines():
        words = line.split()
        if not words:
            continue
        if words[0] == 'TEXTURE':
            result['albedo'] = (path.parent / words[1]).resolve()
        elif words[0] == 'TEXTURE_MAP' and words[1] in ('normal', 'material_gloss'):
            result[words[1]] = (path.parent / words[2]).resolve()
        elif words[0] == 'NORMAL_METALNESS':
            result['normal_metalness'] = True
        elif words[0] in ('ATTR_shiny_rat', 'GLOBAL_specular'):
            result['gloss_scale'] = float(words[1])
    return result


def material(name, spec, cull, cutoff, texture_size, normal_strength=1.):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    out = nodes.new('ShaderNodeOutputMaterial')
    shader = nodes.new('ShaderNodeBsdfPrincipled')
    shader.inputs['Base Color'].default_value = (.55, .55, .55, 1)
    shader.inputs['Roughness'].default_value = .65
    shader.inputs['Specular IOR Level'].default_value = .5

    def texture(key):
        path = spec.get(key)
        if not path:
            return None
        if not path.is_file():
            raise FileNotFoundError(path)
        node = nodes.new('ShaderNodeTexImage')
        node.image = bpy.data.images.load(str(path), check_existing=True)
        node.image.colorspace_settings.name = 'sRGB' if key == 'albedo' else 'Non-Color'
        if texture_size and max(node.image.size) > texture_size:
            factor = texture_size / max(node.image.size)
            node.image.scale(*(round(v * factor) for v in node.image.size))
        return node

    def math_node(operation, a, b=None):
        node = nodes.new('ShaderNodeMath')
        node.operation = operation
        for i, value in enumerate((a, b)):
            if value is not None:
                if isinstance(value, (float, int)):
                    node.inputs[i].default_value = value
                else:
                    links.new(value, node.inputs[i])
        return node.outputs[0]

    albedo, normal, gloss = (texture(k) for k in ('albedo', 'normal', 'material_gloss'))
    if albedo:
        links.new(albedo.outputs['Color'], shader.inputs['Base Color'])
    if gloss:
        channels = nodes.new('ShaderNodeSeparateColor')
        links.new(gloss.outputs['Color'], channels.inputs[0])
        roughness = math_node('SUBTRACT', 1., math_node('MULTIPLY', channels.outputs['Green'], spec['gloss_scale']))
        links.new(roughness, shader.inputs['Roughness'])
        if spec['normal_metalness']:
            # F0=.04 is dielectric. Interpolate toward a conductor for higher F0.
            metal = math_node('DIVIDE', math_node('SUBTRACT', channels.outputs['Red'], .04), .96)
            metal = math_node('MAXIMUM', metal, 0.)
            links.new(metal, shader.inputs['Metallic'])
    elif normal:
        roughness = math_node('SUBTRACT', 1., math_node('MULTIPLY', normal.outputs['Alpha'], spec['gloss_scale']))
        links.new(roughness, shader.inputs['Roughness'])
    if normal:
        # X-Plane takes tangent normal XY from RG; blue is reconstructed here.
        channels = nodes.new('ShaderNodeSeparateColor')
        links.new(normal.outputs['Color'], channels.inputs[0])
        x = math_node('SUBTRACT', math_node('MULTIPLY', channels.outputs['Red'], 2.), 1.)
        y = math_node('SUBTRACT', math_node('MULTIPLY', channels.outputs['Green'], 2.), 1.)
        xy = math_node('ADD', math_node('MULTIPLY', x, x), math_node('MULTIPLY', y, y))
        z = math_node('SQRT', math_node('MAXIMUM', math_node('SUBTRACT', 1., xy), 0.))
        rgb = nodes.new('ShaderNodeCombineColor')
        links.new(channels.outputs['Red'], rgb.inputs['Red'])
        links.new(channels.outputs['Green'], rgb.inputs['Green'])
        links.new(math_node('ADD', math_node('MULTIPLY', z, .5), .5), rgb.inputs['Blue'])
        bump = nodes.new('ShaderNodeNormalMap')
        bump.inputs['Strength'].default_value = normal_strength
        links.new(rgb.outputs[0], bump.inputs['Color'])
        links.new(bump.outputs[0], shader.inputs['Normal'])
    surface = shader.outputs[0]
    transparent = nodes.new('ShaderNodeBsdfTransparent')
    if cutoff is not None and albedo:
        mix = nodes.new('ShaderNodeMixShader')
        links.new(math_node('GREATER_THAN', albedo.outputs['Alpha'], cutoff), mix.inputs[0])
        links.new(transparent.outputs[0], mix.inputs[1])
        links.new(surface, mix.inputs[2])
        surface = mix.outputs[0]
    if cull:
        geometry = nodes.new('ShaderNodeNewGeometry')
        mix = nodes.new('ShaderNodeMixShader')
        links.new(geometry.outputs['Backfacing'], mix.inputs[0])
        links.new(surface, mix.inputs[1])
        links.new(transparent.outputs[0], mix.inputs[2])
        surface = mix.outputs[0]
    links.new(surface, out.inputs['Surface'])
    return mat


def draw_center_x(preview, draw):
    used = set(preview.indices[draw.start:draw.start + draw.count])
    center = [sum(preview.positions[i * 3 + axis] for i in used) / len(used) for axis in range(3)]
    return sum(draw.matrix[axis] * center[axis] for axis in range(3)) + draw.matrix[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, default=PORT / 'output/A380X-XP12')
    parser.add_argument('--engines', type=int, nargs='+', choices=range(1, 5), default=[1, 2])
    parser.add_argument('--view', choices=('quarter', 'front', 'side'), default='quarter')
    parser.add_argument('--dataref', action='append', default=[], metavar='NAME=VALUE')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--camera-from', type=Path, help='Reuse camera from a previous preview JSON for pose comparisons')
    parser.add_argument('--width', type=int, default=1400)
    parser.add_argument('--height', type=int, default=900)
    parser.add_argument('--samples', type=int, default=32)
    parser.add_argument('--texture-size', type=int, default=2048, help='In-memory texture size limit; 0 keeps full resolution')
    parser.add_argument('--flip-v', action='store_true', help='Diagnostic texture-coordinate inversion')
    parser.add_argument('--no-normal-map', action='store_true', help='Diagnostic render without tangent normal maps')
    parser.add_argument('--nacelle-normal-strength', type=float, default=1., help='Diagnostic tangent normal strength for nacelle shells only')
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    values = {}
    for item in args.dataref:
        name, separator, number = item.partition('=')
        if not separator or not name or not math.isfinite(float(number)):
            parser.error(f'Invalid dataref override: {item!r}')
        values[name] = float(number)
    wing_dir = args.package / 'objects/exterior/wings'
    fans = load_obj8(wing_dir / 'a380x_fans.obj')
    # Fan draw groups are discrete by engine; group X values at sub-meter range.
    centers = []
    for draw in fans.draws:
        x = draw_center_x(fans, draw)
        if not any(abs(x - center) < 1 for center in centers):
            centers.append(x)
    centers.sort(reverse=True)
    if len(centers) != 4:
        raise ValueError(f'Expected four fan centers, found {centers}')
    del fans
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    paths = [wing_dir / 'a380x_fans.obj']
    for side, numbers in (('lh', (1, 2)), ('rh', (3, 4))):
        if set(args.engines).intersection(numbers):
            paths.extend(wing_dir / f'{name}_{side}.obj' for name in ('a380x_eng', 'a380x_eng_cores'))
    lo, hi, report = None, None, []
    for path in paths:
        preview = load_obj8(path, values=values)
        preview.draws = [d for d in preview.draws if min(range(4), key=lambda i: abs(draw_center_x(preview, d) - centers[i])) + 1 in args.engines]
        vertices, uv, faces, styles = preview.bake(flip_v=args.flip_v)
        if not faces:
            continue
        spec = obj_material(path)
        if args.no_normal_map:
            spec.pop('normal', None)
        mesh = bpy.data.meshes.new(path.stem)
        mesh.from_pydata(vertices, [], faces)
        mesh.uv_layers.new(name='UVMap')
        lookup = {}
        for style in dict.fromkeys(styles):
            lookup[style] = len(mesh.materials)
            normal_strength = args.nacelle_normal_strength if path.stem in ('a380x_eng_lh', 'a380x_eng_rh') else 1.
            mesh.materials.append(material(f'{path.stem}_{len(mesh.materials)}', spec, *style, args.texture_size, normal_strength))
        for poly in mesh.polygons:
            poly.material_index = lookup[styles[poly.index]]
            poly.use_smooth = True
            for index in poly.loop_indices:
                mesh.uv_layers[0].data[index].uv = uv[mesh.loops[index].vertex_index]
        obj = bpy.data.objects.new(path.stem, mesh)
        bpy.context.collection.objects.link(obj)
        a = Vector(tuple(min(v[i] for v in vertices) for i in range(3)))
        b = Vector(tuple(max(v[i] for v in vertices) for i in range(3)))
        lo = a if lo is None else Vector(tuple(min(lo[i], a[i]) for i in range(3)))
        hi = b if hi is None else Vector(tuple(max(hi[i], b[i]) for i in range(3)))
        item = {'object': str(path), 'triangles': len(faces), 'draws': len(preview.draws), 'datarefs': preview.datarefs,
                'material': {k: str(v) if isinstance(v, Path) else v for k, v in spec.items()}}
        report.append(item)
        print(json.dumps(item), flush=True)
        del vertices, uv, faces, styles, preview
    if lo is None:
        raise ValueError('No engine geometry selected')
    center = (lo + hi) / 2
    direction = Vector({'quarter': (1., -1.65, .5), 'front': (0., -1., .02), 'side': (1., -.16, .12)}[args.view]).normalized()
    bpy.ops.object.camera_add(location=center + direction * max(hi - lo) * 3)
    camera = bpy.context.object
    camera.rotation_euler = (-direction).to_track_quat('-Z', 'Y').to_euler()
    camera.data.type = 'ORTHO'
    camera.data.clip_end = 300
    basis = camera.rotation_euler.to_matrix().transposed()
    projected = [basis @ (Vector(corner) - center) for corner in itertools.product(*zip(lo, hi))]
    width = max(p.x for p in projected) - min(p.x for p in projected)
    height = max(p.y for p in projected) - min(p.y for p in projected)
    camera.data.ortho_scale = max(width, height * args.width / args.height) * 1.12
    camera_target = center
    if args.camera_from:
        previous = json.loads(args.camera_from.read_text())
        old_center = Vector(previous['camera_target']) if 'camera_target' in previous else (Vector(previous['bounds'][0]) + Vector(previous['bounds'][1])) / 2
        camera.location = Vector(previous['camera'])
        camera.rotation_euler = (old_center - camera.location).to_track_quat('-Z', 'Y').to_euler()
        camera.data.ortho_scale = previous['ortho_scale']
        camera_target = old_center
    scene = bpy.context.scene
    scene.camera = camera
    scene.world.use_nodes = True
    scene.world.node_tree.nodes['Background'].inputs[0].default_value = (.27, .32, .40, 1)
    scene.world.node_tree.nodes['Background'].inputs[1].default_value = .5
    for name, offset, power, size in [('Key', (6, -8, 12), 4500, 8), ('Fill', (-8, -4, 5), 2400, 10), ('Rim', (2, 8, 8), 5500, 7)]:
        bpy.ops.object.light_add(type='AREA', location=camera_target + Vector(offset))
        light = bpy.context.object
        light.name, light.data.energy, light.data.shape, light.data.size = name, power, 'DISK', size
        light.rotation_euler = (camera_target - light.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = args.samples
    scene.cycles.use_denoising = True
    scene.cycles.transparent_max_bounces = 16
    scene.render.threads_mode = 'FIXED'
    scene.render.threads = 2
    scene.render.resolution_x, scene.render.resolution_y = args.width, args.height
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.view_settings.view_transform = 'AgX'
    scene.render.filepath = str(args.output.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.render.render(write_still=True)
    metadata = {'engines': args.engines, 'view': args.view, 'datarefs': values, 'flip_v': args.flip_v, 'normal_maps': not args.no_normal_map, 'nacelle_normal_strength': args.nacelle_normal_strength, 'bounds': [list(lo), list(hi)],
                'camera': list(camera.location), 'camera_target': list(camera_target), 'ortho_scale': camera.data.ortho_scale, 'objects': report,
                'note': 'Offline Cycles approximation; not a native simulator screenshot.'}
    args.output.with_suffix('.json').write_text(json.dumps(metadata, indent=2) + '\n')


if __name__ == '__main__':
    main()
