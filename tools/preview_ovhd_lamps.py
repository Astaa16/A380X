#!/usr/bin/env python3
"""Offline view of the INSTALLED overhead panel with its annunciator legends (no X-Plane).

Evaluates the OBJ8 animation tree (blender/obj8_preview.py) of the overhead knobs/caps and the
pushbutton legend decals, flat-shades them from the captain's seat (overhead text upright, aft
at the top) and draws every lit lamp quad of the cockpit-controls object (ELEC, ADIRS and the
overhead lamps of tools/ovhd_cockpit_lamps.py) with the fcu_labels.png cell its UVs address,
the daylight (albedo) face X-Plane shows in addition to the additive _LIT glow.

    python3 tools/preview_ovhd_lamps.py --aircraft output/A380X-XP12 --out docs/control-anim-previews
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from obj8_preview import parse_obj8  # noqa: E402
from ovhd_lamps_contract import PREFIX, lamps  # noqa: E402

VIEW = np.array([0.0, 0.907, 0.42]) / np.linalg.norm([0.0, 0.907, 0.42])
RIGHT = np.array([-1.0, 0.0, 0.0])
UP = np.cross(VIEW, RIGHT)
UP = UP / np.linalg.norm(UP) * (1 if UP[2] < 0 else -1)
FULL = dict(low=(-0.36, 3.30, 31.60), high=(0.36, 3.80, 32.60), centre=(0.0002, 3.5303, 32.1016), size=(1700, 2600), width=0.64)
APU = dict(low=(-0.10, 3.28, 32.30), high=(0.13, 3.47, 32.62), centre=(0.0077, 3.3706, 32.4637), size=(1000, 1000), width=0.17)
ELEC = dict(low=(-0.17, 3.40, 32.18), high=(0.25, 3.52, 32.35), centre=(0.04, 3.46, 32.26), size=(1400, 560), width=0.44)


def draws(path: Path, values, low, high):
    with path.open() as stream:
        obj = parse_obj8(stream, values=values, source=str(path))
    positions = np.frombuffer(obj.positions, dtype=np.float64).reshape(-1, 3)
    indices = np.frombuffer(obj.indices, dtype=np.uint32)
    uvs = np.frombuffer(obj.uv, dtype=np.float64).reshape(-1, 2)
    for draw in obj.draws:
        matrix = np.array(draw.matrix).reshape(4, 4)
        ids = indices[draw.start:draw.start + draw.count]
        world = positions[ids] @ matrix[:3, :3].T + matrix[:3, 3]
        centres = world.reshape(-1, 3, 3).mean(axis=1)
        if np.all((centres > low) & (centres < high), axis=1).any():
            yield draw, world.reshape(-1, 3, 3), uvs[ids].reshape(-1, 3, 2)


def render(aircraft: Path, values, out: Path, frame=FULL, controls: Path | None = None, title=''):
    panels = aircraft / 'objects/interior/cockpit-panels'
    controls = controls or aircraft / 'objects/interior/cockpit-controls/a380x_cockpit_controls.obj'
    low, high, centre = (np.array(frame[k]) for k in ('low', 'high', 'centre'))
    size = frame['size']
    scale = size[0] / frame['width']

    def screen(points):
        offset = points - centre
        return np.stack([size[0] / 2 + offset @ RIGHT * scale, size[1] / 2 - offset @ UP * scale], axis=-1)

    image = Image.new('RGB', size, (30, 31, 34))
    canvas = ImageDraw.Draw(image)
    polygons = []
    for name, colour in (('panel_a380_cockpit_ovhd_knobs_4k.obj', (128, 130, 136)),
                         ('decal_a380_pushbutton_decal.obj', (64, 64, 70))):
        for _draw, triangles, _uv in draws(panels / name, values, low, high):
            normals = np.cross(triangles[:, 1] - triangles[:, 0], triangles[:, 2] - triangles[:, 0])
            lengths = np.linalg.norm(normals, axis=1)
            keep = lengths > 1e-12
            shade = np.abs(normals[keep] @ VIEW) / lengths[keep]
            depth = -(triangles[keep].mean(axis=1) @ VIEW)
            for tri, s, d in zip(screen(triangles[keep]), shade, depth):
                polygons.append((d, [tuple(p) for p in tri], tuple(int(c * (0.35 + 0.65 * s)) for c in colour)))
    for _depth, points, colour in sorted(polygons, key=lambda row: row[0]):
        canvas.polygon(points, fill=colour)
    atlas = Image.open(aircraft / 'cockpit_3d/fcu_labels.png').convert('RGBA')
    lit = 0
    for _draw, triangles, uvs in draws(controls, values, low, high):
        if len(triangles) != 2:
            continue
        corners = {}
        for tri, uv in zip(triangles, uvs):
            for point, (u, v) in zip(tri, uv):
                corners[(round(float(u), 6), round(float(v), 6))] = point
        us, vs = sorted({k[0] for k in corners}), sorted({k[1] for k in corners})
        if len(corners) != 4 or len(us) != 2 or len(vs) != 2:
            continue
        (u0, u1), (v0, v1) = us, vs
        cell = atlas.crop((int(round(u0 * atlas.width)), int(round((1 - v1) * atlas.height)),
                           int(round(u1 * atlas.width)), int(round((1 - v0) * atlas.height))))
        tl, tr, bl = (screen(np.array(corners[k])) for k in ((u0, v1), (u1, v1), (u0, v0)))
        # affine map destination pixel -> cell texel (orthographic view keeps parallelograms)
        a = np.array([[tr[0] - tl[0], bl[0] - tl[0]], [tr[1] - tl[1], bl[1] - tl[1]]])
        if abs(np.linalg.det(a)) < 1e-6:
            continue
        inv = np.linalg.inv(a)
        w, h = cell.size
        m = np.diag([w, h]) @ inv
        offset = -m @ tl
        overlay = cell.transform(size, Image.AFFINE, (m[0, 0], m[0, 1], offset[0], m[1, 0], m[1, 1], offset[1]),
                                 resample=Image.BILINEAR)
        image.paste(overlay.convert('RGB'), (0, 0), overlay)
        lit += 1
    if title:
        ImageDraw.Draw(image).text((12, 10), title, fill=(230, 230, 230))
    image.save(out)
    return lit


SCENARIOS = {
    # FBW logic, see native/avionics/test_ovhd_lamps_adapter.c
    'ann-lt-test-all-powered': 'ANN LT TEST, AC + DC powered: every authored overhead legend lit',
    'apu-avail-bleed-on': 'APU MASTER ON, APU START AVAIL, APU BLEED ON (as the FBW reference)',
    'cold-dark-batteries': 'BAT 1 + ESS + APU BAT, ENG 1/2 BLEED OFF, APU MASTER ON: DC ESS / DC APU legends only',
}


def scenario_values(name):
    values = {}
    if name == 'ann-lt-test-all-powered':
        values.update({row[0]: 1 for row in lamps()})
        from elec_panel_contract import lamps as elec_lamps
        values.update({row[0]: 1 for row in elec_lamps()})
        values.update({f'a380x/xp12/adirs/{c}{n}_{f}': 1 for c in ('ir', 'adr') for n in (1, 2, 3) for f in ('off', 'fault')})
        values[PREFIX + 'ann_lt_switch'] = 0.0
    elif name == 'apu-avail-bleed-on':
        values.update({'sim/cockpit2/electrical/APU_starter_switch': 1, 'sim/cockpit2/bleedair/actuators/apu_bleed': 1,
                       PREFIX + 'lamp/apu_master_lower': 1, PREFIX + 'lamp/apu_start_upper': 1,
                       PREFIX + 'lamp/apu_bleed_lower': 1, PREFIX + 'ann_lt_switch': 0.5})
        for i in range(4):
            values[f'sim/cockpit2/bleedair/actuators/engine_bleed_sov[{i}]'] = 1
            values[f'sim/cockpit2/engine/actuators/fuel_pump_on[{i}]'] = 1
        values.update({'sim/cockpit2/bleedair/actuators/pack_left': 1, 'sim/cockpit2/bleedair/actuators/pack_right': 1})
    elif name == 'cold-dark-batteries':
        values.update({'sim/cockpit2/electrical/APU_starter_switch': 1, PREFIX + 'lamp/apu_master_lower': 1,
                       PREFIX + 'lamp/eng2_bleed_lower': 1, PREFIX + 'ann_lt_switch': 0.5,
                       'a380x/xp12/elec/lamp/gen1_upper': 1, 'a380x/xp12/elec/lamp/gen2_upper': 1})
        for i in (2, 3):
            values[f'sim/cockpit2/bleedair/actuators/engine_bleed_sov[{i}]'] = 1
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aircraft', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--controls', type=Path, help='Render this cockpit-controls OBJ instead of the installed one')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    written = []
    for name, title in SCENARIOS.items():
        values = scenario_values(name)
        for frame_name, frame in (('overhead', FULL), ('apu', APU), ('elec', ELEC)):
            if name != 'ann-lt-test-all-powered' and frame_name == 'elec':
                continue
            out = args.out / f'ovhd-lamps-{name}-{frame_name}.png'
            lit = render(args.aircraft, values, out, frame, args.controls, title)
            written.append(f'{out} ({lit} lit quads)')
    print('\n'.join(written))


if __name__ == '__main__':
    main()
