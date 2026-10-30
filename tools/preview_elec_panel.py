#!/usr/bin/env python3
"""Offline view of the INSTALLED ELEC overhead panel from the captain's seat (no X-Plane).

Evaluates the OBJ8 animation tree (blender/obj8_preview.py) of the overhead knobs, the
pushbutton legend decals and the cockpit-controls lamp overlays, flat-shades them in a
seat-like view (overhead text upright: aft at the top) and textures every lit lamp quad
with its fcu_labels_LIT.png cell exactly as its UVs address it.

    python3 tools/preview_elec_panel.py --aircraft output/A380X-XP12 --out docs/control-anim-previews
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from obj8_preview import parse_obj8  # noqa: E402
from elec_panel_contract import COVERS, PREFIX, lamps, pressable_ids  # noqa: E402

VIEW = np.array([0.0, 0.907, 0.42]) / np.linalg.norm([0.0, 0.907, 0.42])  # from the crew into the panel
RIGHT = np.array([-1.0, 0.0, 0.0])
UP = np.cross(VIEW, RIGHT)
UP = UP / np.linalg.norm(UP) * (1 if UP[2] < 0 else -1)                      # aft is up
LOW, HIGH = np.array([-0.17, 3.40, 32.18]), np.array([0.25, 3.52, 32.35])
CENTRE = np.array([0.04, 3.46, 32.26])
SIZE, SCALE = (1400, 560), 1400 / 0.44


def draws(path: Path, values):
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
        if np.all((centres > LOW) & (centres < HIGH), axis=1).any():
            yield draw, world.reshape(-1, 3, 3), uvs[ids]


def screen(point):
    offset = point - CENTRE
    return SIZE[0] / 2 + offset @ RIGHT * SCALE, SIZE[1] / 2 - offset @ UP * SCALE


def render(aircraft: Path, values, lit, out: Path):
    panels = aircraft / 'objects/interior/cockpit-panels'
    image = Image.new('RGB', SIZE, (24, 24, 27))
    canvas = ImageDraw.Draw(image)
    polygons = []
    for name, colour in (('panel_a380_cockpit_ovhd_knobs_4k.obj', (150, 150, 156)),
                         ('decal_a380_pushbutton_decal.obj', (88, 88, 94))):
        for _draw, triangles, _ids in draws(panels / name, values):
            for tri in triangles:
                normal = np.cross(tri[1] - tri[0], tri[2] - tri[0])
                length = np.linalg.norm(normal)
                if length < 1e-12:
                    continue
                shade = abs(normal @ VIEW) / length
                polygons.append((-(tri.mean(axis=0) @ VIEW), [screen(p) for p in tri],
                                 tuple(int(c * (0.35 + 0.65 * shade)) for c in colour)))
    for _depth, points, colour in sorted(polygons, key=lambda row: row[0]):
        canvas.polygon(points, fill=colour)
    if lit:
        atlas = Image.open(aircraft / 'cockpit_3d/fcu_labels_LIT.png').convert('RGBA')
        controls = aircraft / 'objects/interior/cockpit-controls/a380x_cockpit_controls.obj'
        for _draw, triangles, uv_rows in draws(controls, values):
            if len(triangles) != 2:
                continue
            points = triangles.reshape(-1, 3)
            corners = {}
            for point, uv in zip(points, uv_rows):
                corners[(round(float(uv[0]), 6), round(float(uv[1]), 6))] = point
            if len(corners) != 4:
                continue
            us, vs = sorted({k[0] for k in corners}), sorted({k[1] for k in corners})
            if len(us) != 2 or len(vs) != 2:
                continue
            (u0, u1), (v0, v1) = us, vs
            cell = atlas.crop((int(round(u0 * atlas.width)), int(round((1 - v1) * atlas.height)),
                               int(round(u1 * atlas.width)), int(round((1 - v0) * atlas.height))))
            # The quad corner that carries UV (u0, v1) shows the cell's upper-left texel, etc.
            tl, bl, br, tr = (corners[k] for k in ((u0, v1), (u0, v0), (u1, v0), (u1, v1)))
            pts = [screen(p) for p in (tl, bl, br, tr)]
            xs, ys = [p[0] for p in pts], [p[1] for p in pts]
            box = (int(min(xs)), int(min(ys)), int(max(xs)) + 1, int(max(ys)) + 1)
            w, h = box[2] - box[0], box[3] - box[1]
            if w < 2 or h < 2:
                continue
            local = [(x - box[0], y - box[1]) for x, y in pts]
            # inverse map destination quad -> cell: sample by projective coefficients of a unit square
            dest = Image.new('RGBA', (w, h), (0, 0, 0, 0))
            src = cell.resize((64, 32))
            for yy in range(h):
                for xx in range(w):
                    s, t = _inverse_bilinear((xx + .5, yy + .5), local)
                    if 0 <= s <= 1 and 0 <= t <= 1:
                        dest.putpixel((xx, yy), src.getpixel((min(63, int(s * 64)), min(31, int(t * 32)))))
            image.paste(Image.alpha_composite(Image.new('RGBA', (w, h), (40, 40, 44, 255)), dest).convert('RGB'),
                        box[:2], dest)
    image.save(out)


def _inverse_bilinear(p, quad):
    """(s, t) of point p in quad tl, bl, br, tr (s along top->right, t along top->bottom); Newton steps."""
    tl, bl, br, tr = (np.array(q) for q in quad)
    s = t = .5
    for _ in range(8):
        point = (1 - s) * (1 - t) * tl + s * (1 - t) * tr + s * t * br + (1 - s) * t * bl
        ds = (1 - t) * (tr - tl) + t * (br - bl)
        dt = (1 - s) * (bl - tl) + s * (br - tr)
        jac = np.array([ds, dt]).T
        try:
            step = np.linalg.solve(jac, np.array(p) - point)
        except np.linalg.LinAlgError:
            return -1, -1
        s, t = s + step[0], t + step[1]
    return s, t


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aircraft', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    render(args.aircraft, {}, False, args.out / 'elec-panel-rest.png')
    values = {row[0]: 1 for row in lamps()}
    values.update({f'{PREFIX}cover/{cover}': 1 for cover, _n, _c in COVERS})
    values.update({f'{PREFIX}pb/{cid}_press_ratio': 1 for cid in pressable_ids()})
    render(args.aircraft, values, True, args.out / 'elec-panel-guards-open-pressed-ann-test.png')
    print(f'Wrote {args.out}/elec-panel-rest.png and elec-panel-guards-open-pressed-ann-test.png')


if __name__ == '__main__':
    main()
