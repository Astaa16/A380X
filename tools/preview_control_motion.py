#!/usr/bin/env python3
"""Offline proof of cockpit control motion in the INSTALLED OBJs (no X-Plane).

Evaluates the OBJ8 animation tree (blender/obj8_preview.py, Laminar semantics) at rest and with
control datarefs driven, then
* measures, per dataref (combination), the displacement of every draw it moves: mm along the
  main-panel normal (negative = into the panel) and the rotation seen from the seat (+ = clockwise);
* renders a contact sheet: zoomed side views (rest grey, actuated orange) for pushbuttons and
  shafts, and seat views of the endless knobs coloured by their rest-pose angle, so a turn shows.

    python3 tools/preview_control_motion.py --objects output/A380X-XP12/objects/interior/cockpit-panels \
        --out docs/control-anim-previews --report /tmp/motion.json
"""
from __future__ import annotations

import argparse
import colorsys
import json
import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from obj8_preview import parse_obj8  # noqa: E402

NORMAL = np.array([0.0, 0.3328, -0.943])        # main-panel normal toward the crew
NORMAL /= np.linalg.norm(NORMAL)
RIGHT = np.array([-1.0, 0.0, 0.0])               # crew's right (source +X is crew-left)
UP = np.cross(NORMAL, RIGHT)
UP /= np.linalg.norm(UP)
if UP[1] < 0:
    UP = -UP
FILES = ('panel_a380_cockpit_mip_knobs_02_4k.obj', 'panel_a380_cockpit_mip_knob01_4k.obj',
         'decal_a380x_cockpit_decal_emis.obj', 'panel_a380_cockpit_pedestal_knobs02_4k.obj')
SIDE_TILES = (('ISIS 1 MODE', 'isis/mode_pressed'), ('ISIS 2 LS', 'isis2/ls_pressed'),
              ('ISIS 1 SET/SEL push', 'isis/set_sel_axial'), ('EFIS CAPT LS', 'efis/captain/ls_pressed'),
              ('EFIS CAPT CSTR', 'efis/captain/cstr_pressed'), ('EFIS F/O VV', 'efis/first_officer/vv_pressed'),
              ('EFIS CAPT BARO push STD', 'efis/captain/baro_knob_axial'))
KNOB_TILES = (('ISIS 1 SET/SEL +90 deg', 'isis/set_sel_rotation'), ('ISIS 2 SET/SEL +90 deg', 'isis2/set_sel_rotation'))


def state_values(state):
    values = {}
    if state == 'pressed':
        for prefix in ('isis', 'isis2'):
            for key in ('mode', 'ls', 'plus', 'minus', 'menu'):
                values[f'a380x/xp12/{prefix}/{key}_pressed'] = 1
            values[f'a380x/xp12/{prefix}/set_sel_axial'] = 1
            values[f'a380x/xp12/{prefix}/set_sel_rotation'] = 90
        for side in ('captain', 'first_officer'):
            for key in ('cstr', 'wpt', 'vord', 'ndb', 'arpt', 'vor1', 'vor2', 'terr', 'traf', 'wx', 'taxi', 'ls', 'vv'):
                values[f'a380x/xp12/efis/{side}/{key}_pressed'] = 1
            values[f'a380x/xp12/efis/{side}/baro_knob_axial'] = 1
        for key in ('ap1', 'ap2', 'fd', 'athr', 'loc', 'alt_button', 'approach', 'speed_mach', 'true_mag',
                    'trk_fpa', 'metric'):
            values[f'a380x/xp12/fcu/{key}_pressed'] = 1
        for knob in ('speed', 'heading', 'altitude', 'vertical_speed'):
            values[f'a380x/xp12/fcu/{knob}_knob_rotation'] = 90
            values[f'a380x/xp12/fcu/{knob}_knob_axial'] = 1
        values['a380x/xp12/ecam/sd/brightness'] = 1
        values['a380x/xp12/ecam/sd/ewd_brightness'] = 1
    elif state == 'pulled':
        for side in ('captain', 'first_officer'):
            values[f'a380x/xp12/efis/{side}/baro_knob_axial'] = -1
    return values


def evaluate(path, values):
    with path.open() as stream:
        obj = parse_obj8(stream, values=values, source=str(path))
    positions = np.frombuffer(obj.positions, dtype=np.float64).reshape(-1, 3)
    indices = np.frombuffer(obj.indices, dtype=np.uint32)
    draws = []
    for draw in obj.draws:
        matrix = np.array(draw.matrix).reshape(4, 4)
        tri = indices[draw.start:draw.start + draw.count].reshape(-1, 3)
        refs = tuple(r.replace('a380x/xp12/', '') for r in draw.transform_datarefs if r.startswith('a380x/'))
        draws.append((draw.draw_index, refs, positions[tri] @ matrix[:3, :3].T + matrix[:3, 3]))
    return draws


def measure(rest, moved):
    by_index = {index: world for index, _, world in rest}
    result = {}
    for index, refs, world in moved:
        base = by_index.get(index)
        if base is None or not refs or not np.any(np.abs(world - base) > 1e-7):
            continue
        a, b = base.reshape(-1, 3), world.reshape(-1, 3)
        ca, cb = a.mean(axis=0), b.mean(axis=0)
        pa = np.stack(((a - ca) @ RIGHT, (a - ca) @ UP), axis=1)
        pb = np.stack(((b - cb) @ RIGHT, (b - cb) @ UP), axis=1)
        h = pa.T @ pb
        angle = -math.degrees(math.atan2(h[0, 1] - h[1, 0], h[0, 0] + h[1, 1]))
        row = result.setdefault(' + '.join(refs), dict(draws=0, triangles=0, normal_mm=set(), clockwise_deg=set()))
        row['draws'] += 1
        row['triangles'] += len(base)
        row['normal_mm'].add(round(float(np.mean((b - a) @ NORMAL)) * 1000, 2))
        row['clockwise_deg'].add(round(angle + 0.0, 1))
    return result


def _project(points, axes, centre, scale, size):
    return [(size / 2 + (p - centre) @ axes[0] * scale, size / 2 - (p - centre) @ axes[1] * scale) for p in points]


def side_tile(rest, moved, ref, label, size=360, half_mm=9.0):
    """Side view (crew's right): the panel face is vertical, the crew is to the left."""
    mine = [i for i, refs, _ in moved if ref in refs]
    chosen = [w for i, _, w in rest if i in mine]
    centre = np.concatenate([w.reshape(-1, 3) for w in chosen]).mean(axis=0)
    axes = (NORMAL, UP)
    scale = size / (2 * half_mm / 1000)
    image = Image.new('RGBA', (size, size), (22, 24, 28, 255))
    for source, colour in ((rest, (160, 166, 176, 150)), (moved, (240, 140, 40, 150))):
        layer = Image.new('RGBA', (size, size), (0, 0, 0, 0))
        draw = ImageDraw.Draw(layer)
        for index, _, world in source:
            if index not in mine:
                continue
            for tri in world:
                draw.polygon(_project(tri, axes, centre, scale, size), fill=colour)
        image = Image.alpha_composite(image, layer)
    draw = ImageDraw.Draw(image)
    draw.text((8, 6), label, fill=(255, 255, 255, 255))
    draw.text((8, size - 18), 'panel <-  grey rest / orange actuated  -> crew', fill=(200, 200, 200, 255))
    draw.line([(size - 60, size - 30), (size - 60 + 1 * scale / 1000, size - 30)], fill=(255, 255, 0, 255), width=3)
    draw.text((size - 60, size - 46), '1 mm', fill=(255, 255, 0, 255))
    return image


def knob_tile(rest, moved, ref, label, size=360, half_mm=13.0):
    mine = [i for i, refs, _ in moved if ref in refs]
    rest_tris = [w for i, _, w in rest if i in mine]
    centre = np.concatenate([w.reshape(-1, 3) for w in rest_tris]).mean(axis=0)
    axes = (RIGHT, UP)
    scale = size / (2 * half_mm / 1000)
    tiles = []
    for source in (rest, moved):
        image = Image.new('RGB', (size, size), (22, 24, 28))
        draw = ImageDraw.Draw(image)
        rest_by_index = {i: w for i, _, w in rest}
        items = []
        for index, _, world in source:
            if index not in mine:
                continue
            for tri, rest_tri in zip(world, rest_by_index[index]):
                c = rest_tri.mean(axis=0) - centre
                hue = (math.atan2(c @ UP, c @ RIGHT) / (2 * math.pi)) % 1.0
                items.append((float(tri.mean(axis=0) @ NORMAL), tri, hue))
        for _, tri, hue in sorted(items, key=lambda item: item[0]):
            r, g, b = colorsys.hsv_to_rgb(hue, 0.85, 0.95)
            draw.polygon(_project(tri, axes, centre, scale, size), fill=(int(r * 255), int(g * 255), int(b * 255)))
        tiles.append(image)
    sheet = Image.new('RGB', (size * 2 + 6, size), (60, 60, 60))
    sheet.paste(tiles[0], (0, 0)); sheet.paste(tiles[1], (size + 6, 0))
    ImageDraw.Draw(sheet).text((8, 6), label + ' (seat view; colour = rest-pose angle)', fill=(255, 255, 255))
    return sheet


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--objects', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    report, rest_all, moved_all, pulled_all = {}, [], [], []
    for number, name in enumerate(FILES):
        path = args.objects / name
        rest = evaluate(path, {})
        offset = number * 10_000_000
        rest_all += [(offset + i, r, w) for i, r, w in rest]
        for state, bucket in (('pressed', moved_all), ('pulled', pulled_all)):
            moved = evaluate(path, state_values(state))
            bucket += [(offset + i, r, w) for i, r, w in moved]
            for ref, row in measure(rest, moved).items():
                merged = report.setdefault(state, {}).setdefault(ref, dict(draws=0, triangles=0, normal_mm=set(), clockwise_deg=set()))
                merged['draws'] += row['draws']; merged['triangles'] += row['triangles']
                merged['normal_mm'] |= row['normal_mm']; merged['clockwise_deg'] |= row['clockwise_deg']
    tiles = [side_tile(rest_all, moved_all, ref, label) for label, ref in SIDE_TILES]
    tiles.append(side_tile(rest_all, pulled_all, 'efis/captain/baro_knob_axial', 'EFIS CAPT BARO pull QNH'))
    sheet = Image.new('RGB', (4 * 366, 2 * 366), (60, 60, 60))
    for n, tile in enumerate(tiles):
        sheet.paste(tile.convert('RGB'), ((n % 4) * 366, (n // 4) * 366))
    sheet.save(args.out / 'press-travel-side-views.png')
    knobs = [knob_tile(rest_all, moved_all, ref, label) for label, ref in KNOB_TILES]
    sheet = Image.new('RGB', (knobs[0].width, sum(k.height + 6 for k in knobs)), (60, 60, 60))
    for n, tile in enumerate(knobs):
        sheet.paste(tile, (0, n * (tile.height + 6)))
    sheet.save(args.out / 'isis-knob-rotation.png')
    for rows in report.values():
        for row in rows.values():
            row['normal_mm'] = sorted(row['normal_mm']); row['clockwise_deg'] = sorted(row['clockwise_deg'])
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({state: len(rows) for state, rows in report.items()}))


if __name__ == '__main__':
    main()
