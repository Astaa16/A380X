#!/usr/bin/env python3
"""Stage the FCU's authored roughness/metal response for X-Plane 12.

This writes only an aircraft-relative staging tree, never installed files.
The existing 4K albedo, 2K normals, geometry, UVs, animations and manipulators
are preserved. Technical material channels are converted without color/gamma
adjustment: F0/metalness in R, gloss (1 - roughness) in G.

References:
https://developer.x-plane.com/article/obj8-file-format-specification/
https://developer.x-plane.com/article/the-x-plane-12-material-model/
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile
from urllib.parse import unquote

import PIL
from PIL import Image, ImageOps, ImageStat

PROJECT = Path(__file__).resolve().parents[1]
OBJECT = Path('objects/interior/cockpit-panels/panel_a380_cockpit_fcu_4k.obj')
MATERIAL = 'A380_COCKPIT_FCU_4k'
DEFAULT_SOURCE = (PROJECT.parent / 'upstream-aircraft-large-files/fbw-a380x/src/base/'
                  'flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/'
                  'attachments/flybywire/Part_Interior_Cockpit/model/A380_COCKPIT_LOD00.gltf')
F0_FLOOR = round(.04 * 255)
SOURCES = ['https://developer.x-plane.com/article/obj8-file-format-specification/',
           'https://developer.x-plane.com/article/the-x-plane-12-material-model/']


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def convert_comp(image: Image.Image, metallic_factor: float = 1., roughness_factor: float = 1.) -> Image.Image:
    """Retain authored wear/metal mask; no normal, albedo or RGB repainting."""
    if any(not math.isfinite(x) or not 0 <= x <= 1 for x in (metallic_factor, roughness_factor)):
        raise ValueError('Material factors must be finite ratios in [0,1]')
    image = image.convert('RGBA')
    f0 = image.getchannel('B').point([max(F0_FLOOR, round(v * metallic_factor)) for v in range(256)])
    roughness = image.getchannel('G').point([round(v * roughness_factor) for v in range(256)])
    return Image.merge('RGBA', (f0, ImageOps.invert(roughness),
                                Image.new('L', image.size, 0), Image.new('L', image.size, 255)))


def records(text: str):
    for line in text.splitlines(keepends=True):
        fields = line.split('#', 1)[0].split()
        if fields:
            yield line, fields


def protected_object_bytes(text: str) -> bytes:
    """Every record outside the two allowed specular directives must survive."""
    return ''.join(line for line in text.splitlines(keepends=True)
                   if not re.match(r'^\s*(?:ATTR_shiny_rat|GLOBAL_specular)(?:\s|$)', line)).encode()


def patch_master_gloss(text: str) -> str:
    rows = list(records(text))
    if [fields[0] for _, fields in rows[:3]] not in (['I', '800', 'OBJ'], ['A', '800', 'OBJ']):
        raise ValueError('Expected an OBJ8 FCU object')
    if sum(fields[0] == 'NORMAL_METALNESS' for _, fields in rows) != 1:
        raise ValueError('FCU material must use NORMAL_METALNESS')
    if not any(fields[0] == 'VT' for _, fields in rows) or not any(fields[0] == 'TRIS' for _, fields in rows):
        raise ValueError('FCU object has no mesh')
    if any(fields[0] == 'ATTR_reset' for _, fields in rows):
        raise ValueError('Unexpected material reset: inspect the FCU material scopes before patching')
    if sum(fields[0] == 'ATTR_shiny_rat' for _, fields in rows) != 1:
        raise ValueError('Expected exactly one FCU ATTR_shiny_rat material scope')
    if sum(fields[0] == 'GLOBAL_specular' for _, fields in rows) > 1:
        raise ValueError('Duplicate GLOBAL_specular directive')
    pattern = re.compile(r'^(\s*(?:ATTR_shiny_rat|GLOBAL_specular)\s+)([^\s#]+)', re.M)
    for _, fields in rows:
        if fields[0] in ('ATTR_shiny_rat', 'GLOBAL_specular'):
            if len(fields) != 2 or not math.isfinite(float(fields[1])):
                raise ValueError('Invalid FCU master gloss directive')
    patched = pattern.sub(lambda match: match[1] + '1.0', text)
    if protected_object_bytes(text) != protected_object_bytes(patched):
        raise AssertionError('A protected FCU object record was changed')
    return patched


def material_references(text: str) -> dict[str, str]:
    refs: dict[str, str] = {}
    for _, fields in records(text):
        key = fields[0]
        if key == 'TEXTURE':
            value = ' '.join(fields[1:])
        elif key == 'TEXTURE_MAP' and len(fields) >= 3 and fields[1] in ('normal', 'material_gloss'):
            key += ' ' + fields[1]
            value = ' '.join(fields[2:])
        elif key == 'TEXTURE_NORMAL':
            raise ValueError('Legacy TEXTURE_NORMAL cannot accompany X-Plane 12 TEXTURE_MAP')
        else:
            continue
        if not value or key in refs:
            raise ValueError('Missing or duplicate FCU texture reference: ' + key)
        refs[key] = value
    if set(refs) != {'TEXTURE', 'TEXTURE_MAP normal', 'TEXTURE_MAP material_gloss'}:
        raise ValueError('Expected albedo, normal and material_gloss references')
    return refs


def aircraft_asset(aircraft: Path, reference: str) -> tuple[Path, Path]:
    path = (aircraft / OBJECT.parent / reference).resolve()
    if not path.is_relative_to(aircraft):
        raise ValueError('FCU texture reference escapes the aircraft: ' + reference)
    if not path.is_file():
        raise FileNotFoundError(path)
    return path, path.relative_to(aircraft)


def source_comp(source: Path, explicit_comp: Path | None) -> tuple[Path, dict]:
    document = json.loads(source.read_text())
    materials = [m for m in document['materials'] if m.get('name') == MATERIAL]
    if len(materials) != 1:
        raise ValueError('Expected one authored FCU material')
    pbr = materials[0]['pbrMetallicRoughness']
    spec = pbr['metallicRoughnessTexture']
    if spec.get('texCoord', 0) != 0 or spec.get('extensions', {}).get('KHR_texture_transform'):
        raise ValueError('COMP uses a different UV set/transform; automatic conversion is unsafe')
    texture = document['textures'][spec['index']]
    index = texture.get('extensions', {}).get('MSFT_texture_dds', {}).get('source', texture.get('source'))
    if index is None:
        raise ValueError('FCU material has no COMP image source')
    filename = unquote(document['images'][index]['uri']).replace('\\', '/').split('/')[-1]
    comp = explicit_comp.resolve() if explicit_comp else (source.parent.parent / 'texture' / filename).resolve()
    if comp.name != filename or not comp.is_file():
        raise ValueError('COMP file does not match the authored FCU material: ' + str(comp))
    return comp, pbr


def load_comp(path: Path) -> Image.Image:
    if path.suffix.lower() == '.ktx2':
        sys.path.insert(0, str(PROJECT / 'blender'))
        from convert_ktx2_textures import ktx2_to_dds
        payload, _ = ktx2_to_dds(path)
        with Image.open(io.BytesIO(payload)) as image:
            return image.convert('RGBA')
    with Image.open(path) as image:
        return image.convert('RGBA')


def image_report(path: Path) -> dict:
    with Image.open(path) as image:
        return dict(path=str(path), sha256=sha(path.read_bytes()), dimensions=list(image.size),
                    mode=image.mode, channel_ranges=image.getextrema())


def atomic_write(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix='.fcu-material-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'wb') as file:
            file.write(payload)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def prepare(aircraft: Path, source: Path, output: Path, object_source: Path | None = None,
            comp_override: Path | None = None) -> dict:
    aircraft, source, output = aircraft.resolve(), source.resolve(), output.resolve()
    if output == aircraft or output.is_relative_to(aircraft):
        raise ValueError('Staging output must be outside the installed aircraft')
    object_source = object_source.resolve() if object_source else aircraft / OBJECT
    original_bytes = object_source.read_bytes()
    # read_bytes/decode preserves CRLF exactly, unlike universal-newline reads.
    text = original_bytes.decode('utf-8')
    refs = material_references(text)
    albedo, albedo_relative = aircraft_asset(aircraft, refs['TEXTURE'])
    normal, normal_relative = aircraft_asset(aircraft, refs['TEXTURE_MAP normal'])
    original_gloss, gloss_relative = aircraft_asset(aircraft, refs['TEXTURE_MAP material_gloss'])
    albedo_info, normal_info = image_report(albedo), image_report(normal)
    if albedo_info['dimensions'] != [4096, 4096] or normal_info['dimensions'] != [2048, 2048]:
        raise ValueError('Expected the authored 4K FCU albedo and 2K normal map')
    comp, pbr = source_comp(source, comp_override)
    source_image = load_comp(comp)
    if source_image.size != (2048, 2048):
        raise ValueError('Expected the authored 2K FCU COMP; no source upscaling is performed')
    converted = convert_comp(source_image, float(pbr.get('metallicFactor', 1)), float(pbr.get('roughnessFactor', 1)))
    encoded = io.BytesIO()
    converted.save(encoded, format='PNG', compress_level=9)
    texture_bytes = encoded.getvalue()
    object_bytes = patch_master_gloss(text).encode('utf-8')
    geometry_hash = sha(protected_object_bytes(text))
    report = dict(scope='FCU-only technical PBR staging; no installed files changed and no simulator visual/FPS claim',
                  aircraft=str(aircraft), output=str(output), tool=str(Path(__file__).resolve()), pillow_version=PIL.__version__,
                  sources=SOURCES, material=MATERIAL,
                  authored_comp=dict(path=str(comp), sha256=sha(comp.read_bytes()), gltf=str(source),
                                     gltf_sha256=sha(source.read_bytes()), dimensions=list(source_image.size),
                                     channel_ranges=source_image.getextrema(), channel_means=ImageStat.Stat(source_image).mean,
                                     metallic_factor=pbr.get('metallicFactor', 1), roughness_factor=pbr.get('roughnessFactor', 1)),
                  original_gloss=image_report(original_gloss),
                  converted=dict(path=str(gloss_relative), sha256=sha(texture_bytes), dimensions=list(converted.size),
                                 channel_ranges=converted.getextrema(),
                                 R='max(10, round(COMP.B * metallicFactor)): dielectric F0 floor about .04 and authored metal mask',
                                 G='255 - round(COMP.G * roughnessFactor): authored gloss', B=0, A=255,
                                 encoding='Linear technical channel values; no color transform, gamma correction, tint or sharpening'),
                  object=dict(path=str(OBJECT), source=str(object_source), source_sha256=sha(original_bytes),
                              sha256=sha(object_bytes), protected_before_sha256=geometry_hash,
                              protected_after_sha256=sha(protected_object_bytes(object_bytes.decode('utf-8'))),
                              master_gloss=1., geometry_uv_animation_manipulators_preserved=True),
                  preserved_assets=[dict(albedo_info, path=str(albedo_relative)), dict(normal_info, path=str(normal_relative))],
                  files=[dict(path=str(gloss_relative), sha256=sha(texture_bytes), base_sha256=sha(original_gloss.read_bytes())),
                         dict(path=str(OBJECT), sha256=sha(object_bytes), base_sha256=sha(original_bytes))])
    # Everything is validated before staging either file. Albedo/normal are
    # deliberately not copied or re-encoded; their installed bytes stay intact.
    atomic_write(output / gloss_relative, texture_bytes)
    atomic_write(output / OBJECT, object_bytes)
    atomic_write(output / 'material-report.json', (json.dumps(report, indent=2) + '\n').encode())
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aircraft', type=Path, default=PROJECT / 'output/A380X-XP12')
    parser.add_argument('--source-gltf', type=Path, default=DEFAULT_SOURCE)
    parser.add_argument('--comp', type=Path, help='Explicit relocated COMP with its original authored filename')
    parser.add_argument('--object-source', type=Path, help='Final motion-patched FCU OBJ to compose with material staging')
    parser.add_argument('--output', type=Path, default=PROJECT / 'build/fcu-overhaul-20261020/materials')
    args = parser.parse_args()
    report = prepare(args.aircraft, args.source_gltf, args.output, args.object_source, args.comp)
    print(json.dumps(dict(report=str(args.output.resolve() / 'material-report.json'),
                          files=report['files'], preserved_assets=report['preserved_assets']), indent=2))


if __name__ == '__main__':
    main()
