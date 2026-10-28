#!/usr/bin/env python3
"""Overhead push-button lighting (Airbus dark cockpit), push animation and hotspots.

One source of truth for all 174 animated FBW overhead push buttons:

* reads each button's legends from the FBW cockpit glTF: every legend child (A380_PUSHBUTTON_DECAL)
  samples one cell of the FBW legend atlas, which names its text and colour (FAULT amber, OFF
  white, ON blue, AVAIL green, ...). The atlas grid below was read from the decoded
  A380X_PUSH_TEXT_EMIS texture;
* LINKS ties buttons to X-Plane systems or port state; every other button keeps its own latched
  (or momentary) state in the plugin;
* writes native/avionics/a380x_pb_table.inc (the plugin's button table),
  config/overhead_pushbutton_hotspots.json (for append_cockpit_controls.py, pointing-finger
  cursor) and the patched panel OBJs:
  - caps (and, for buttons the export did not already animate, their legends) are wrapped in
    FBW's own press animation driven by a380x/xp12/pb/push[i];
  - each legend gets ATTR_light_level 0 1 a380x/xp12/pb/upper[i] or lower[i]; everything else in
    the decal OBJ stays at a380x/xp12/pb/dark (0). The decal OBJ gains TEXTURE_LIT.

The OBJs are always rebuilt from the clean originals in --base-dir. Buttons the export already
animates (battery, generators, bleeds, fuel pumps, engine anti-ice, APU, ADIRS) keep that motion.
"""

from __future__ import annotations

import argparse
import math
import json
import re
import struct
from pathlib import Path

from patch_center_right_animations import animation_block, source_targets

# Legend atlas: rows top to bottom, six columns, (upper, lower) per cell.
GRID = [
    [('', 'ON'), ('', 'ON'), ('AVAIL', 'ON'), ('OPEN', 'ON'), ('EVAC', 'ON'), ('OPEN', 'ON')],
    [('CALL', 'ON'), ('FAULT', 'ON'), ('FAULT', 'MAN'), ('FAULT', 'OFF'), ('', 'MAN'), ('', 'OFF')],
    [('FAULT', 'ALTN'), ('FAULT', 'DISC'), ('FAULT', 'OVRD'), ('SMOKE', 'DISCH'), ('SMOKE', 'OFF'), ('SMOKE', '')],
    [('', ''), ('', ''), ('', ''), ('', ''), ('SQUIB', 'DISCH'), ('OPEN', 'FAULT')],
    [('FAULT', ''), ('', 'OFF'), ('', 'OVRD'), ('END', 'ON'), ('FAULT', 'ON'), ('', 'ON')],
    [('', 'ON'), ('', ''), ('', ''), ('', ''), ('', ''), ('', '')],
]
# Lower legend kinds (plugin): 0 none, 1 lit when released (OFF), 2 lit when pushed (ON/MAN/...),
# 3 APU START (lit while the start runs). Upper kinds: 0 none/test only, 1 FAULT source,
# 2 OPEN (follows push), 3 EXT PWR AVAIL, 4 APU AVAIL.
LOWER_KIND = {'': 0, 'OFF': 1, 'ON': 2, 'MAN': 2, 'ALTN': 2, 'OVRD': 2, 'DISC': 2, 'DISCH': 2, 'FAULT': 0}
UPPER_KIND = {'': 0, 'FAULT': 1, 'OPEN': 2, 'AVAIL': 3, 'CALL': 0, 'EVAC': 0, 'SMOKE': 0, 'SQUIB': 0, 'END': 0}

# node: (pushed source dataref, array index or -1, mode, fault dataref or '')
# modes: 'nonzero' pushed = value != 0; 'zero' pushed = value == 0; 'two' pushed = value == 2.
LINKS = {
    'PUSH_OVHD_ELEC_BAT1': ('sim/cockpit2/electrical/battery_on', 0, 'nonzero', ''),
    'PUSH_OVHD_ELEC_BAT2': ('sim/cockpit2/electrical/battery_on', 1, 'nonzero', ''),
    **{f'PUSH_OVHD_ELEC_GEN{n}': ('sim/cockpit2/electrical/generator_on', n - 1, 'nonzero', '') for n in range(1, 5)},
    'PUSH_OVHD_ELEC_APUGEN': ('sim/cockpit2/electrical/APU_generator_on', -1, 'nonzero', ''),
    'PUSH_OVHD_ELEC_APUGEN2': ('sim/cockpit2/electrical/APU_generator_on', -1, 'nonzero', ''),
    **{f'PUSH_OVHD_ELEC_EXTPWR{s}': ('sim/cockpit/electrical/gpu_on', -1, 'nonzero', '') for s in ('', '2', '3', '4')},
    'PUSH_OVHD_ELEC_BUSTIE': ('sim/cockpit2/electrical/cross_tie', -1, 'nonzero', ''),
    'PUSH_OVHD_APU_MASTERSW': ('sim/cockpit2/electrical/APU_starter_switch', -1, 'nonzero', ''),
    'PUSH_OVHD_APU_START': ('sim/cockpit2/electrical/APU_starter_switch', -1, 'two', ''),
    **{f'PUSH_OVHD_AIRCOND_ENG{n}BLEED': ('sim/cockpit2/bleedair/actuators/engine_bleed_sov', n - 1, 'nonzero', '')
       for n in range(1, 5)},
    'PUSH_OVHD_AIRCOND_APUBLEED': ('sim/cockpit2/bleedair/actuators/apu_bleed', -1, 'nonzero', ''),
    'PUSH_OVHD_AIRCOND_PACK1': ('sim/cockpit2/bleedair/actuators/pack_left', -1, 'nonzero', ''),
    'PUSH_OVHD_AIRCOND_PACK2': ('sim/cockpit2/bleedair/actuators/pack_right', -1, 'nonzero', ''),
    **{f'PUSH_OVHD_ANTIICE_ENG{n}': ('sim/cockpit2/ice/ice_inlet_heat_on_per_engine', n - 1, 'nonzero', '')
       for n in range(1, 5)},
    'PUSH_OVHD_ANTIICE_WING': ('sim/cockpit2/ice/ice_surface_hot_bleed_air_on', -1, 'nonzero', ''),
    'PUSH_OVHD_PROBESWINDOW': ('sim/cockpit2/ice/ice_window_heat_on', -1, 'nonzero', ''),
    **{f'PUSH_OVHD_FUEL_FEEDTK{n}_MAIN': ('sim/cockpit2/engine/actuators/fuel_pump_on', n - 1, 'nonzero', '')
       for n in range(1, 5)},
    **{f'PUSH_OVHD_ENGMANSTART_{n}': ('sim/flightmodel2/engines/starter_is_running', n - 1, 'nonzero', '')
       for n in range(1, 5)},
    **{f'PUSH_OVHD_ADIRS_IR{n}': (f'a380x/xp12/adirs/ir{n}_off', -1, 'zero', f'a380x/xp12/adirs/ir{n}_fault')
       for n in range(1, 4)},
    **{f'PUSH_OVHD_ADIRS_{n}': (f'a380x/xp12/adirs/adr{n}_off', -1, 'zero', f'a380x/xp12/adirs/adr{n}_fault')
       for n in range(1, 4)},
}
EXCLUDE = {'PUSH_OVHD_AIRCOND_APUBLEEDBASE'}   # static base of APU BLEED, not a button

PB = 'a380x/xp12/pb/'
CAP_FILES = ('panel_a380_cockpit_ovhd_knobs_4k.obj', 'panel_a380_cockpit_ovhd_knobs_fire_4k.obj',
             'decal_a380_pushbutton_decal.obj')
DECAL_FILE = 'decal_a380_pushbutton_decal.obj'
LIT_TEXTURE = '../../../textures/interior/cockpit-panels/a380_pushbutton_decal_lit.png'
MARKER = '# A380X PUSHBUTTON '
# FBW's ENG 2 BLEED upper legend samples an atlas cell its siblings do not; the panel export uses FAULT/OFF.
LEGEND_CELL_FIX = {'PUSH_OVHD_AIRCOND_ENG2BLEED_SEQ1': (1, 3, 'upper')}


def legend_inventory(gltf: Path):
    document = json.loads(gltf.read_text(encoding='utf-8'))
    blob = (gltf.parent / document['buffers'][0]['uri']).read_bytes()
    nodes = document['nodes']
    names = {node.get('name'): i for i, node in enumerate(nodes)}
    animations = {a['name'] for a in document['animations']}

    def uv_centre(index):   # MSFS VertexVersion 2 stores TEXCOORD_0 as two float16
        us, vs = [], []
        for primitive in document['meshes'][nodes[index]['mesh']]['primitives']:
            accessor = document['accessors'][primitive['attributes']['TEXCOORD_0']]
            view = document['bufferViews'][accessor['bufferView']]
            stride = view.get('byteStride', 4)
            base = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
            for k in range(accessor['count']):
                u, v = struct.unpack_from('<ee', blob, base + k * stride)
                us.append(u)
                vs.append(v)
        return (min(us) + max(us)) / 2, (min(vs) + max(vs)) / 2, max(vs) - min(vs)

    buttons = {}
    for name, index in names.items():
        if (not name or not re.match(r'^PUSH_OVHD_', name) or re.search(r'_SEQ[12]$|\.\d{3}$', name)
                or 'mesh' not in nodes[index] or name in EXCLUDE or name not in animations):
            continue
        legends = []

        def walk(j):
            for child in nodes[j].get('children', []):
                node = nodes[child]
                if 'mesh' in node and any(document['materials'][p['material']]['name'] == 'A380_PUSHBUTTON_DECAL'
                                          for p in document['meshes'][node['mesh']]['primitives']):
                    u, v, height = uv_centre(child)
                    row, col = int(v / 0.125), int(u / 0.125)
                    upper, lower = GRID[row][col] if row < 6 and col < 6 else ('', '')
                    half = 'lower' if height > 0.09 or (v % 0.125) >= 0.0625 else 'upper'
                    if node['name'] in LEGEND_CELL_FIX:
                        row, col, half = LEGEND_CELL_FIX[node['name']]
                        upper, lower = GRID[row][col]
                    legends.append({'node': node['name'], 'half': half, 'cell': [row, col],
                                    'text': lower if half == 'lower' else upper})
                walk(child)
        walk(index)
        buttons[name] = legends
    return buttons


def button_table(buttons):
    table = []
    for i, name in enumerate(sorted(buttons)):
        legends = buttons[name]
        lower = next((l['text'] for l in legends if l['half'] == 'lower'), '')
        upper = next((l['text'] for l in legends if l['half'] == 'upper'), '')
        lower_kind = 3 if name == 'PUSH_OVHD_APU_START' else LOWER_KIND.get(lower, 0)
        upper_kind = UPPER_KIND.get(upper, 0)
        if upper == 'AVAIL':
            upper_kind = 4 if name == 'PUSH_OVHD_APU_START' else 3
        source, index, mode, fault = LINKS.get(name, ('', -1, 'state', ''))
        momentary = not legends
        # Dark cockpit: an OFF-legend button rests pushed in (normal, dark); every other button
        # rests released.
        initial = 1 if lower_kind == 1 else 0
        table.append(dict(index=i, node=name, upper=upper, lower=lower, upper_kind=upper_kind,
                          lower_kind=lower_kind, source=source, source_index=index, mode=mode,
                          fault=fault, momentary=momentary, initial=initial, legends=legends))
    return table


def write_c_table(table, path: Path):
    modes = {'state': 0, 'nonzero': 1, 'zero': 2, 'two': 3}
    lines = ['/* Generated by tools/build_overhead_pushbuttons.py - do not edit. */',
             f'#define PB_COUNT {len(table)}',
             'static const PbButton pb_buttons[PB_COUNT] = {']
    for b in table:
        lines.append(f'    {{"{b["node"]}", "{b["source"]}", {b["source_index"]}, {modes[b["mode"]]}, '
                     f'"{b["fault"]}", {b["upper_kind"]}, {b["lower_kind"]}, {int(b["momentary"])}, '
                     f'{b["initial"]}}}, /* {b["index"]}: {b["upper"] or "-"} / {b["lower"] or "-"} */')
    lines.append('};')
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def hotspot_config(table, mapped_path: Path, extra_path: Path):
    mapped = {c['node']: c for c in json.loads(mapped_path.read_text(encoding='utf-8'))}
    extra = {c['node']: c for c in json.loads(extra_path.read_text(encoding='utf-8'))}
    out = []
    for b in table:
        node, label = b['node'], b['node'].replace('PUSH_OVHD_', '').replace('_', ' ')
        legend = ' / '.join(t for t in (b['upper'], b['lower']) if t)
        if node in mapped and mapped[node].get('command'):
            out.append({'node': node, 'kind': 'command', 'command': mapped[node]['command'], 'cursor': 'button',
                        'label': mapped[node]['label']})
        elif node in mapped and mapped[node].get('toggle_values'):
            out.append({'node': node, 'kind': 'toggle', 'dataref': mapped[node]['dataref'],
                        'values': list(reversed(mapped[node]['toggle_values'])), 'cursor': 'button',
                        'label': mapped[node]['label']})
        elif node in extra and extra[node]['kind'] in ('command', 'toggle') and not extra[node].get('dataref', '').startswith('a380x/xp12/ovhd/'):
            out.append(dict(extra[node], cursor='button'))
        elif b['momentary']:
            out.append({'node': node, 'kind': 'push', 'dataref': f'{PB}state[{b["index"]}]', 'cursor': 'button',
                        'label': f'{label} - push'})
        else:
            out.append({'node': node, 'kind': 'toggle', 'dataref': f'{PB}state[{b["index"]}]', 'values': [1, 0],
                        'cursor': 'button', 'label': f'{label}' + (f' ({legend})' if legend else '')})
    return out


def patch_objs(gltf: Path, table, base_dir: Path, out_dir: Path):
    motions = tuple((b['node'], b['node'], f'pb/push[{b["index"]}]', 1) for b in table)
    targets = source_targets(gltf, motions=motions, include_child_points=True)
    by_name = {t['name']: t for t in targets}
    legends = []
    for b in table:
        for legend in b['legends']:
            target = by_name[legend['node']]
            centre = tuple(sum(p[a] for p in target['points']) / len(target['points']) for a in range(3))
            legends.append(dict(button=b['node'], index=b['index'], half=legend['half'], cell=tuple(legend['cell']),
                                centre=centre, target=target))
    report, sources, cap_animated = {}, {}, set()
    for filename in CAP_FILES:
        sources[filename] = (base_dir / filename).read_text(encoding='utf-8')
        if MARKER in sources[filename]:
            raise ValueError(f'{filename} in the base directory is already patched')
        if filename != DECAL_FILE:
            cap_animated |= animated_caps(sources[filename], [t for t in targets if is_cap(t, filename)])
    # A button is left to the export's motion when either its cap or a legend already sits in an animation.
    text, decal_report, pre_animated = patch_decal(sources[DECAL_FILE], legends, light_only=cap_animated)
    (out_dir / DECAL_FILE).write_text(text, encoding='utf-8')
    report[DECAL_FILE] = decal_report
    for filename in CAP_FILES:
        if filename == DECAL_FILE:
            continue
        text = sources[filename]
        caps = [t for t in targets if is_cap(t, filename) and t['animation'] not in pre_animated]
        text, counts = patch_caps(text, filename, caps)
        (out_dir / filename).write_text(text, encoding='utf-8')
        report[filename] = counts
    return report, sorted(pre_animated)


def is_cap(target, filename):
    # Usually the button node itself; PA's node is its legend and the cap is the PUSH_OVHD_PA.001 child.
    return filename.endswith(target['material'] + '.obj') and not re.search(r'_SEQ[12]$', target['name'])


def obj_geometry(lines):
    vertices = [tuple(map(float, l.split()[1:9])) for l in lines if l.startswith('VT')]
    indices = [int(v) for l in lines if l.split()[:1] in (['IDX'], ['IDX10']) for v in l.split()[1:]]
    return vertices, indices


def rotate(axis, degrees, p):
    n = math.sqrt(sum(a * a for a in axis))
    k = [a / n for a in axis]
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    d = sum(k[i] * p[i] for i in range(3))
    x = (k[1] * p[2] - k[2] * p[1], k[2] * p[0] - k[0] * p[2], k[0] * p[1] - k[1] * p[0])
    return [p[i] * c + x[i] * s + k[i] * d * (1 - c) for i in range(3)]


def world(ops, p):
    """Place a point by the enclosing animations at their first keyframe (the export's rest pose)."""
    p = list(p)
    for op in reversed(ops):
        p = [p[i] + op[1][i] for i in range(3)] if op[0] == 't' else rotate(op[1], op[2], p)
    return p


def tris_lines(lines):
    """Yield (line number, line, animation depth, rest-pose ops) for every TRIS line."""
    stack, pending = [], None
    for n, line in enumerate(lines):
        fields = line.split()
        head = fields[0] if fields else ''
        if head == 'ANIM_begin':
            stack.append([])
        elif head == 'ANIM_end':
            stack.pop()
        elif head == 'ANIM_trans':
            stack[-1].append(('t', list(map(float, fields[1:4]))))
        elif head == 'ANIM_rotate':
            stack[-1].append(('r', list(map(float, fields[1:4])), float(fields[4])))
        elif head == 'ANIM_trans_begin':
            pending = ('t',)
        elif head == 'ANIM_rotate_begin':
            pending = ('r', list(map(float, fields[1:4])))
        elif head == 'ANIM_trans_key' and pending:
            stack[-1].append(('t', list(map(float, fields[2:5]))))
            pending = None
        elif head == 'ANIM_rotate_key' and pending:
            stack[-1].append(('r', pending[1], float(fields[2])))
            pending = None
        elif head == 'TRIS':
            yield n, line, len(stack), [op for level in stack for op in level]


def animated_caps(text, caps, tolerance=0.001):
    """Caps whose geometry the export already placed inside an animation (matched at rest pose)."""
    lines = text.splitlines()
    vertices, indices = obj_geometry(lines)
    found = set()
    for _, line, depth, ops in tris_lines(lines):
        if depth == 0:
            continue
        start, count = map(int, line.split()[1:3])
        ids = set(indices[start:start + count])
        points = [world(ops, vertices[i][:3]) for i in ids]
        for target in caps:
            if target['name'] in found:
                continue
            if all(all(target['low'][a] - tolerance <= p[a] <= target['high'][a] + tolerance for a in range(3))
                   for p in points) and len(points) >= 3:
                found.add(target['name'])
    return found


def patch_decal(text, legends, light_only=(), radius=0.012):
    """Each legend is one quad. Match quads to FBW legends by atlas cell, half and nearest centre."""
    lines = text.splitlines(keepends=True)
    vertices, indices = obj_geometry(lines)
    by_cell = {}
    for legend in legends:
        by_cell.setdefault((legend['cell'], legend['half']), []).append(legend)
    pre_animated = set(light_only)
    plans, used = {}, {}
    for n, line, depth, ops in tris_lines(lines):
        fields = line.split()
        start, count = int(fields[1]), int(fields[2])
        runs = []
        for quad in range(start, start + count, 6):
            ids = sorted(set(indices[quad:quad + 6]))
            uv = [vertices[i][6:8] for i in ids]
            u = (min(x for x, _ in uv) + max(x for x, _ in uv)) / 2
            v = 1 - (min(y for _, y in uv) + max(y for _, y in uv)) / 2   # OBJ v is flipped
            cell, half = (int(v / 0.125), int(u / 0.125)), ('upper' if v % 0.125 < 0.0625 else 'lower')
            centre = world(ops, (sum(vertices[i][a] for i in ids) / len(ids) for a in range(3)))
            best = None
            for legend in by_cell.get((cell, half), ()):
                distance = sum((centre[a] - legend['centre'][a]) ** 2 for a in range(3)) ** .5
                if distance <= radius and (best is None or distance < best[0]):
                    best = (distance, legend)
            match = best[1] if best else None
            if match is not None:
                # The export sometimes duplicates a legend quad (ENG 2 BLEED); light every copy.
                used.setdefault(match['target']['name'], n)
                if depth > 0:
                    pre_animated.add(match['button'])
            key = id(match) if match else None
            if runs and runs[-1][0] == key:
                runs[-1][2] += 6
            else:
                runs.append([key, quad, 6, match])
        if any(r[3] for r in runs):
            plans[n] = (runs, depth)
    missing = [l['target']['name'] for l in legends if l['target']['name'] not in used]
    if missing:
        raise ValueError(f'{len(missing)} FBW legends not found in the decal OBJ: {missing[:8]}')
    out = []
    for n, line in enumerate(lines):
        if n not in plans:
            out.append(line)
            continue
        runs, depth = plans[n]
        indent = line[:len(line) - len(line.lstrip())]
        for _, run_start, run_count, legend in runs:
            if legend is None:
                out.append(f'{indent}TRIS\t{run_start}\t{run_count}\n')
                continue
            body = [f"ATTR_light_level\t0\t1\t{PB}{legend['half']}[{legend['index']}]",
                    f'TRIS\t{run_start}\t{run_count}', f'ATTR_light_level\t0\t1\t{PB}dark']
            if depth == 0 and legend['button'] not in pre_animated:
                block = animation_block(legend['target'], run_start, run_count, MARKER)
                at = block.index(f'TRIS\t{run_start}\t{run_count}')
                block = block[:at] + body + block[at + 1:]
            else:
                block = [f"{MARKER}{legend['target']['name']} (light only)"] + body
            out.extend(indent + row + '\n' for row in block)
    texture_at = next(i for i, l in enumerate(out) if l.startswith(('TEXTURE\t', 'TEXTURE ')))
    out.insert(texture_at + 1, f'TEXTURE_LIT {LIT_TEXTURE}\n')
    first = next(i for i, l in enumerate(out) if l.lstrip().startswith(('TRIS', 'ANIM_begin', 'ATTR_', MARKER)))
    out.insert(first, f'ATTR_light_level\t0\t1\t{PB}dark\n')
    return ''.join(out), {'legends_lit': len(used), 'pre_animated_buttons': len(pre_animated)}, pre_animated


def patch_caps(text, filename, caps):
    """Wrap cap triangles (exact FBW vertex match) in the button's own FBW press animation."""
    lines = text.splitlines(keepends=True)
    vertices, indices = obj_geometry(lines)
    tolerance = .000012
    selected = []
    for target in caps:
        grid = {}
        for p in target['points']:
            grid.setdefault(tuple(round(v / tolerance) for v in p), []).append(p)
        chosen = set()
        for i, vertex in enumerate(vertices):
            p = vertex[:3]
            if not all(target['low'][a] - tolerance <= p[a] <= target['high'][a] + tolerance for a in range(3)):
                continue
            key = tuple(round(v / tolerance) for v in p)
            near = (q for dx in (-1, 0, 1) for dy in (-1, 0, 1) for dz in (-1, 0, 1)
                    for q in grid.get((key[0] + dx, key[1] + dy, key[2] + dz), ()))
            if any(max(abs(a - b) for a, b in zip(p, q)) <= tolerance for q in near):
                chosen.add(i)
        selected.append(chosen)
    counts = [0] * len(caps)
    out, depth = [], 0
    for line in lines:
        fields = line.split()
        if fields[:1] == ['ANIM_begin']:
            depth += 1
        elif fields[:1] == ['ANIM_end']:
            depth -= 1
        if fields[:1] != ['TRIS']:
            out.append(line)
            continue
        start, count = int(fields[1]), int(fields[2])
        runs = []
        for position in range(start, start + count, 3):
            tri = indices[position:position + 3]
            matches = [k for k, chosen in enumerate(selected) if all(v in chosen for v in tri)]
            if len(matches) > 1:
                raise ValueError(f'{filename}: ambiguous cap geometry at {position}')
            match = matches[0] if matches and depth == 0 else -1
            if runs and runs[-1][0] == match:
                runs[-1][2] += 3
            else:
                runs.append([match, position, 3])
        if len(runs) == 1 and runs[0][0] < 0:
            out.append(line)
            continue
        indent = line[:len(line) - len(line.lstrip())]
        for match, run_start, run_count in runs:
            if match < 0:
                out.append(f'{indent}TRIS\t{run_start}\t{run_count}\n')
            else:
                counts[match] += run_count
                out.extend(indent + row + '\n' for row in animation_block(caps[match], run_start, run_count, MARKER))
    for target, count in zip(caps, counts):
        if count != target['count']:
            raise ValueError(f'{filename}: cap {target["name"]} expected {target["count"]} indices, found {count}')
    return ''.join(out), {'caps_animated': len(caps)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('gltf', type=Path)
    parser.add_argument('--base-dir', type=Path, required=True, help='clean original cockpit-panels OBJs')
    parser.add_argument('--out-dir', type=Path, required=True)
    parser.add_argument('--table', type=Path, required=True)
    parser.add_argument('--hotspots', type=Path, required=True)
    parser.add_argument('--mapped', type=Path, required=True, help='config/cockpit_controls.json')
    parser.add_argument('--extra', type=Path, required=True, help='config/overhead_extra_controls.json')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    buttons = legend_inventory(args.gltf)
    table = button_table(buttons)
    write_c_table(table, args.table)
    args.hotspots.write_text(json.dumps(hotspot_config(table, args.mapped, args.extra), indent=1) + '\n',
                             encoding='utf-8')
    args.out_dir.mkdir(parents=True, exist_ok=True)
    counts, pre_animated = patch_objs(args.gltf, table, args.base_dir, args.out_dir)
    summary = {'buttons': len(table), 'pre_animated': pre_animated,
               'legends': sum(len(b['legends']) for b in table), 'files': counts,
               'table': [{k: b[k] for k in ('index', 'node', 'upper', 'lower', 'source', 'mode', 'momentary', 'initial')}
                         for b in table]}
    if args.report:
        args.report.write_text(json.dumps(summary, indent=1) + '\n', encoding='utf-8')
    print(f'{len(table)} buttons, {summary["legends"]} legends, {len(pre_animated)} already animated by the export')


if __name__ == '__main__':
    main()
