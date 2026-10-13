#!/usr/bin/env python3
"""Rebind six audited A380 aileron scopes without re-exporting any geometry.

Only 18 ANIM_rotate_begin dataref tokens may change, in wing1/wing2. Source
objects remain untouched; output must be a new directory. This establishes
source-structure and byte-preservation contracts, not runtime registration or
aircraft fidelity. Keys remain source-clip ratios (-1..1), not degrees.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re


OLD_REF = b'sim/flightmodel2/controls/roll_ratio'
REF_PREFIX = 'a380x/xp12/visual/aileron_'
PANELS = {(524,): ('inboard', 12, 1), (532,): ('midboard', 13, 2),
          (604, 5472): ('outboard', 14, 3)}
EXPECTED_FILES = frozenset('a380x_' + name + '.obj' for name in (
    'eng_cores_lh', 'eng_cores_rh', 'eng_lh', 'eng_rh', 'fans', 'pylons',
    'wing1', 'wing1_2', 'wing2', 'wing2_2', 'wingfence'))
CUSTOM_REFS = frozenset(f'{REF_PREFIX}{side}_{panel}_ratio'.encode('ascii')
                        for side in ('l', 'r') for panel, _, _ in PANELS.values())
AXES = ((0., 0., -1.), (0., 1., 0.), (1., 0., 0.))


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def finite(words, label):
    try:
        values = tuple(float(word) for word in words)
    except ValueError as error:
        raise ValueError(f'{label}: invalid numeric value') from error
    if not all(math.isfinite(value) for value in values):
        raise ValueError(f'{label}: nonfinite numeric value')
    return values


def rebind_object(data: bytes, name: str):
    """Return patched bytes plus provenance; no filesystem changes."""
    if name not in EXPECTED_FILES:
        raise ValueError(f'Unexpected source OBJ name: {name}')
    lines = data.splitlines(keepends=True)
    scopes, stack, table, draws, edits = [], [], None, [], []
    last_finished_ref = None
    vertices, indices, min_index, max_index, points = 0, 0, 0, -1, None
    for line_index, line in enumerate(lines):
        words = line.split(b'#', 1)[0].split()
        if not words:
            continue
        command, args = words[0], words[1:]
        label = f'{name}:{line_index + 1}'
        if command == b'ANIM_keyframe_loop' and last_finished_ref == OLD_REF:
            raise ValueError(f'{label}: looping roll-ratio tables are outside the source contract')
        if command != b'ANIM_keyframe_loop':
            last_finished_ref = None
        if command.startswith(b'ANIM_') and any(word in CUSTOM_REFS or word.startswith(REF_PREFIX.encode()) for word in args):
            raise ValueError(f'{label}: already or partially rebound aileron references')
        if command.startswith(b'ANIM_') and OLD_REF in args and command != b'ANIM_rotate_begin':
            raise ValueError(f'{label}: unexpected roll-ratio animation form')
        if table and command not in (b'ANIM_' + table['kind'] + b'_key', b'ANIM_' + table['kind'] + b'_end'):
            raise ValueError(f'{label}: malformed animation table scope')
        if command == b'VT':
            if len(args) != 8:
                raise ValueError(f'{label}: malformed vertex')
            finite(args, label)
            vertices += 1
        elif command in (b'IDX', b'IDX10'):
            if len(args) != (1 if command == b'IDX' else 10):
                raise ValueError(f'{label}: malformed indices')
            values = tuple(int(v) for v in args)
            indices += len(values)
            min_index, max_index = min(min_index, *values), max(max_index, *values)
        elif command == b'POINT_COUNTS':
            if points is not None or len(args) != 4:
                raise ValueError(f'{label}: malformed or duplicate POINT_COUNTS')
            points = tuple(int(v) for v in args)
        elif command == b'ANIM_begin':
            if args:
                raise ValueError(f'{label}: malformed ANIM_begin')
            scopes.append({'begin_line': line_index + 1, 'tables': [], 'draws': []})
            stack.append(len(scopes) - 1)
        elif command == b'ANIM_end':
            if args or not stack:
                raise ValueError(f'{label}: unbalanced ANIM_end')
            stack.pop()
        elif command in (b'ANIM_rotate_begin', b'ANIM_trans_begin'):
            kind = b'rotate' if command == b'ANIM_rotate_begin' else b'trans'
            if not stack or len(args) != (4 if kind == b'rotate' else 1):
                raise ValueError(f'{label}: malformed animation header')
            axis = finite(args[:3], label) if kind == b'rotate' else None
            table = {'kind': kind, 'ref': args[-1], 'axis': axis, 'keys': [],
                     'line_index': line_index, 'scope': stack[-1], 'raw': [line]}
        elif command in (b'ANIM_rotate_key', b'ANIM_trans_key'):
            kind = b'rotate' if command == b'ANIM_rotate_key' else b'trans'
            if table is None or table['kind'] != kind or len(args) != (2 if kind == b'rotate' else 4):
                raise ValueError(f'{label}: key outside matching animation table')
            table['keys'].append(finite(args, label))
            table['raw'].append(line)
        elif command in (b'ANIM_rotate_end', b'ANIM_trans_end'):
            kind = b'rotate' if command == b'ANIM_rotate_end' else b'trans'
            if table is None or table['kind'] != kind or args:
                raise ValueError(f'{label}: mismatched animation end')
            table['raw'].append(line)
            if table['ref'] == OLD_REF:
                scopes[table['scope']]['tables'].append(table)
            last_finished_ref = table['ref']
            table = None
        elif command == b'TRIS':
            if len(args) != 2:
                raise ValueError(f'{label}: malformed TRIS')
            start, count = map(int, args)
            if start < 0 or count <= 0 or count % 3:
                raise ValueError(f'{label}: invalid TRIS range')
            draw = {'draw_index': len(draws), 'source_line': line_index + 1,
                    'index_start': start, 'index_count': count, 'triangles': count // 3}
            draws.append(draw)
            for scope in stack:
                scopes[scope]['draws'].append(draw)
    if table is not None or stack:
        raise ValueError(f'{name}: unclosed animation scope/table')
    if (points is None or points[0] != vertices or points[3] != indices
            or min_index < 0 or max_index >= vertices
            or any(d['index_start'] + d['index_count'] > indices for d in draws)):
        raise ValueError(f'{name}: geometry/index count or range contract failed')
    owners = [scope for scope in scopes if scope['tables'] and scope['draws']]
    side = {'a380x_wing1.obj': 'l', 'a380x_wing2.obj': 'r'}.get(name)
    if len(owners) != (3 if side else 0):
        raise ValueError(f'{name}: unexpected number of draw-bearing roll scopes: {len(owners)}')
    signatures = Counter(tuple(sorted(d['triangles'] for d in scope['draws'])) for scope in owners)
    if side and signatures != Counter({signature: 1 for signature in PANELS}):
        raise ValueError(f'{name}: missing, duplicate, or unexpected aileron draw signature')
    records = []
    for scope in owners:
        signature = tuple(sorted(d['triangles'] for d in scope['draws']))
        panel, bone, action = PANELS[signature]
        tables = scope['tables']
        if len(tables) != 3 or tuple(t['axis'] for t in tables) != AXES:
            raise ValueError(f'{name}: expected the three audited Euler-axis rotators')
        reference = f'{REF_PREFIX}{side}_{panel}_ratio'.encode('ascii')
        record = {'panel': panel, 'side': side, 'bone': f'WING_AILERON_{bone}.{side.upper()}',
                  'source_action': f'{side}_aileron{action}_percent_key',
                  'scope_begin_line': scope['begin_line'], 'draw_signature': list(signature),
                  'draws': scope['draws'], 'new_dataref': reference.decode(), 'headers': []}
        for entry in tables:
            keys = entry['keys']
            inputs = [key[0] for key in keys]
            if (len(keys) != 201 or abs(inputs[0] + 1.) > 1e-8 or abs(inputs[-1] - 1.) > 1e-8
                    or any(a >= b for a, b in zip(inputs, inputs[1:]))):
                raise ValueError(f'{name}: expected 201 increasing ratio keys spanning [-1,1]')
            index = entry['line_index']
            tokens = list(re.finditer(rb'\S+', lines[index].split(b'#', 1)[0]))
            token = tokens[-1]
            if token.group() != OLD_REF:
                raise ValueError(f'{name}: dataref replacement token mismatch')
            edits.append((index, token.start(), token.end(), reference))
            record['headers'].append({'line': index + 1, 'axis': list(entry['axis']),
                'key_count': len(keys), 'input_min': inputs[0], 'input_max': inputs[-1],
                'original_table_sha256': sha256(b''.join(entry['raw']))})
        records.append(record)
    if len(edits) != (9 if side else 0) or len({e[0] for e in edits}) != len(edits):
        raise ValueError(f'{name}: unexpected or overlapping header edit count')
    for index, start, end, replacement in edits:
        original = lines[index]
        patched = original[:start] + replacement + original[end:]
        if patched[:start] + OLD_REF + patched[start + len(replacement):] != original:
            raise ValueError('Non-dataref bytes would change')
        lines[index] = patched
    output = b''.join(lines)
    return output, {'object': name, 'source_sha256': sha256(data), 'output_sha256': sha256(output),
        'source_bytes': len(data), 'output_bytes': len(output), 'vertices': vertices, 'indices': indices,
        'triangles': sum(draw['triangles'] for draw in draws), 'header_token_changes': len(edits),
        'all_other_bytes_preserved': True, 'scopes': records}


def bind_directory(source_dir: Path, output_dir: Path, report_path: Path):
    if output_dir.is_symlink() or report_path.is_symlink():
        raise ValueError('Output and report must not be symbolic links')
    source, output, report = source_dir.resolve(), output_dir.resolve(), report_path.resolve()
    if not source.is_dir():
        raise ValueError('Source directory does not exist')
    if output.exists() or output.is_relative_to(source) or source.is_relative_to(output):
        raise ValueError('Output must be a new directory outside the source tree')
    if report.is_relative_to(source) or report == output or report in {output / name for name in EXPECTED_FILES}:
        raise ValueError('Report must not overwrite source or output OBJ content')
    if report.exists():
        raise ValueError('Report must be a new file')
    files = {p.name: p for p in source.iterdir()
             if p.suffix.lower() == '.obj' and not p.name.startswith('._')}
    if set(files) != EXPECTED_FILES or any(not p.is_file() or p.is_symlink() for p in files.values()):
        raise ValueError('Source must contain exactly the eleven audited regular OBJ files')
    records, changed = [], {}
    for name, path in sorted(files.items()):
        patched, record = rebind_object(path.read_bytes(), name)
        records.append(record)
        if record['header_token_changes']:
            changed[name] = patched
    if sum(r['header_token_changes'] for r in records) != 18:
        raise ValueError('Expected exactly 18 header-token changes across two files')
    # Validate everything before reserving the output. Existing files and
    # directories are never overwritten; originals remain the recovery copy.
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir()
    for record in records:
        name = record['object']
        original = files[name].read_bytes()
        if sha256(original) != record['source_sha256']:
            raise ValueError('Source changed during staging; output is incomplete and must not be deployed')
        data = changed.get(name, original)
        with (output / name).open('xb') as stream:
            stream.write(data)
        if sha256((output / name).read_bytes()) != record['output_sha256']:
            raise ValueError('Staged output verification failed; do not deploy')
    if any(sha256(files[r['object']].read_bytes()) != r['source_sha256'] for r in records):
        raise ValueError('Source changed during staging; output must not be deployed')
    result = {'format': 1, 'source_directory': str(source), 'output_directory': str(output),
        'source_untouched': True, 'source_is_recovery_copy': True, 'header_token_changes': 18,
        'source_dataref': OLD_REF.decode(), 'target_value_domain': [-1, 1],
        'scope_contract': {'draw_signatures': [list(signature) for signature in PANELS],
                           'rotation_axes': [list(axis) for axis in AXES],
                           'keys_per_rotator': 201, 'draw_bearing_scopes_per_side': 3},
        'custom_datarefs': sorted(ref.decode() for ref in CUSTOM_REFS),
        'limits': 'Structural/source-scope proof only. Custom datarefs must be registered by a runtime provider; values are source-clip ratios, not degrees. No simulation or flightworthiness validation.',
        'objects': records}
    report.parent.mkdir(parents=True, exist_ok=True)
    with report.open('x') as stream:
        stream.write(json.dumps(result, indent=2) + '\n')
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args(argv)
    result = bind_directory(args.source_dir, args.output_dir, args.report)
    print(f"Rebound 18 header tokens in 2 of 11 OBJs; originals retained at {result['source_directory']}")
    print(f'Report: {args.report}')


if __name__ == '__main__':
    main()
