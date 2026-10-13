#!/usr/bin/env python3
"""Stage audited exterior hydraulic bindings; change only OBJ8 header ref tokens.

The current packaged exterior is the input, including its later tail repairs.
All twenty exterior objects must match the expected animation table contracts.
No geometry, transforms, keyframes, trim refs, or cockpit files are rewritten.
Output must be a new directory; the originals remain a recovery copy.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re

PREFIX = 'a380x/xp12/hydraulics/'
AILERONS = {f'a380x/xp12/visual/aileron_{side}_{panel}_ratio':
            f'{PREFIX}aileron_{side}_{panel}_ratio'
            for side in ('l', 'r') for panel in ('inboard', 'midboard', 'outboard')}
TAIL = {'sim/joystick/yoke_pitch_ratio': PREFIX + 'elevator_ratio',
        'sim/flightmodel2/controls/heading_ratio': PREFIX + 'rudder_ratio'}
SPOILERS = {
    f'sim/flightmodel2/wing/{native}[{index}]': f'{PREFIX}spoiler_{side}_{panel}_deg'
    for native, index, side, panel in (
        ('speedbrake1_deg', 0, 'l', 'inboard'), ('speedbrake1_deg', 1, 'r', 'inboard'),
        ('spoiler1_deg', 0, 'l', 'inner'), ('spoiler1_deg', 1, 'r', 'inner'),
        ('spoiler1_deg', 2, 'l', 'middle'), ('spoiler1_deg', 3, 'r', 'middle'),
        ('spoiler2_deg', 4, 'l', 'outer'), ('spoiler2_deg', 5, 'r', 'outer'))}
EXPECTED_FILES = {
    'wings': frozenset('a380x_' + name + '.obj' for name in (
        'eng_cores_lh', 'eng_cores_rh', 'eng_lh', 'eng_rh', 'fans', 'pylons',
        'wing1', 'wing1_2', 'wing2', 'wing2_2', 'wingfence')),
    'fuselage': frozenset('a380x_' + name + '.obj' for name in (
        'accessories', 'fuse1', 'fuse2', 'fuse3', 'fuse4', 'fuse5', 'lg_bay',
        'lg_bay_main', 'pax_windows')),
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def expected_tables(group, name):
    """Count (ref, command, key count, first input, last input) contracts."""
    expected = Counter()
    # Current engine export strips all unrelated flight-control branches.
    # Preserve those independent cleanup changes; only the wing skins need refs.
    if group == 'wings' and name in {'a380x_eng_cores_lh.obj', 'a380x_eng_cores_rh.obj',
                                      'a380x_eng_lh.obj', 'a380x_eng_rh.obj', 'a380x_fans.obj'}:
        return expected
    if group == 'fuselage':
        for ref, count, keys in zip(TAIL, (10, 6), (21, 7)):
            expected[ref, 'ANIM_rotate_begin', keys, -1., 1.] = count
    else:
        for ref, target in SPOILERS.items():
            inboard = target.endswith('_inboard_deg')
            expected[ref, 'ANIM_rotate_begin', 25 if inboard else 34, -50., 100.] = (
                8 if inboard or target.endswith('_middle_deg') else
                2 if target.endswith('_inner_deg') else 6)
            if inboard:
                expected[ref, 'ANIM_trans_begin', 25, -50., 100.] = 5
        side = {'a380x_wing1.obj': 'l', 'a380x_wing2.obj': 'r'}.get(name)
        for ref in AILERONS:
            if side and f'aileron_{side}_' in ref:
                expected[ref, 'ANIM_rotate_begin', 201, -1., 1.] = 3
    return expected


def rebind_object(data: bytes, group: str, name: str):
    if group not in EXPECTED_FILES or name not in EXPECTED_FILES[group]:
        raise ValueError(f'Unexpected exterior object: {group}/{name}')
    mapping = TAIL if group == 'fuselage' else dict(SPOILERS, **AILERONS)
    lines = data.splitlines(keepends=True)
    observed, headers = Counter(), []
    stack, table = [], None
    points, vertices, indices, draws = None, 0, 0, []
    minimum_index, maximum_index = 0, -1
    previous_ref = None
    for index, raw in enumerate(lines):
        body = raw.split(b'#', 1)[0]
        tokens = list(re.finditer(rb'\S+', body))
        if not tokens:
            continue
        words = [token.group().decode('ascii') for token in tokens]
        command, args = words[0], words[1:]
        label = f'{group}/{name}:{index + 1}'
        if command == 'ANIM_keyframe_loop' and previous_ref in mapping:
            raise ValueError(f'{label}: hydraulic table must not loop')
        if command != 'ANIM_keyframe_loop':
            previous_ref = None
        if command.startswith('ANIM_'):
            if any(arg.startswith(PREFIX) for arg in args):
                raise ValueError(f'{label}: already or partially hydraulically bound')
            if any(arg in mapping for arg in args) and command not in ('ANIM_rotate_begin', 'ANIM_trans_begin'):
                raise ValueError(f'{label}: unexpected hydraulic animation form')
        if table and command not in (table['kind'] + '_key', table['kind'] + '_end'):
            raise ValueError(f'{label}: malformed animation table')
        if command == 'ANIM_begin':
            if args:
                raise ValueError(f'{label}: malformed scope')
            stack.append(index + 1)
        elif command == 'ANIM_end':
            if args or not stack:
                raise ValueError(f'{label}: unbalanced scope')
            stack.pop()
        elif command in ('ANIM_rotate_begin', 'ANIM_trans_begin'):
            if not stack or len(args) != (4 if command == 'ANIM_rotate_begin' else 1):
                raise ValueError(f'{label}: malformed animation header')
            if any(not math.isfinite(float(arg)) for arg in args[:-1]):
                raise ValueError(f'{label}: nonfinite axis')
            table = {'kind': command.removesuffix('_begin'), 'command': command,
                     'ref': args[-1], 'index': index, 'token': tokens[-1], 'keys': [],
                     'scope_begin_line': stack[-1]}
        elif command in ('ANIM_rotate_key', 'ANIM_trans_key'):
            if not table or len(args) != (2 if command == 'ANIM_rotate_key' else 4):
                raise ValueError(f'{label}: malformed animation key')
            numbers = tuple(float(arg) for arg in args)
            if not all(math.isfinite(number) for number in numbers):
                raise ValueError(f'{label}: nonfinite key')
            table['keys'].append(numbers)
        elif command in ('ANIM_rotate_end', 'ANIM_trans_end'):
            if not table or args:
                raise ValueError(f'{label}: malformed animation end')
            if table['ref'] in mapping:
                keys = table['keys']
                if not keys or any(a[0] >= b[0] for a, b in zip(keys, keys[1:])):
                    raise ValueError(f'{label}: unordered or empty hydraulic table')
                observed[table['ref'], table['command'], len(keys), keys[0][0], keys[-1][0]] += 1
                headers.append(table)
            previous_ref = table['ref']
            table = None
        elif command == 'POINT_COUNTS':
            if points is not None or len(args) != 4:
                raise ValueError(f'{label}: invalid POINT_COUNTS')
            points = tuple(map(int, args))
        elif command == 'VT':
            if len(args) != 8 or not all(math.isfinite(float(arg)) for arg in args):
                raise ValueError(f'{label}: invalid vertex')
            vertices += 1
        elif command in ('IDX', 'IDX10'):
            if len(args) != (1 if command == 'IDX' else 10):
                raise ValueError(f'{label}: invalid index line')
            values = tuple(map(int, args))
            minimum_index, maximum_index = min(minimum_index, *values), max(maximum_index, *values)
            indices += len(values)
        elif command == 'TRIS':
            if len(args) != 2:
                raise ValueError(f'{label}: invalid TRIS')
            draws.append(tuple(map(int, args)))
    if table or stack:
        raise ValueError(f'{group}/{name}: unclosed animation table/scope')
    if (not points or points[0] != vertices or points[3] != indices or minimum_index < 0
            or maximum_index >= vertices or any(start < 0 or count <= 0 or count % 3
                or start + count > indices for start, count in draws)):
        raise ValueError(f'{group}/{name}: geometry/index contract failed')
    if observed != expected_tables(group, name):
        raise ValueError(f'{group}/{name}: hydraulic table contract mismatch: {observed}')
    edits = []
    for header in headers:
        index, token, ref = header['index'], header['token'], header['ref']
        target = mapping[ref].encode('ascii')
        original = lines[index]
        lines[index] = original[:token.start()] + target + original[token.end():]
        if lines[index][:token.start()] + ref.encode('ascii') + lines[index][token.start() + len(target):] != original:
            raise ValueError('Non-header bytes would change')
        edits.append({'line': index + 1, 'old_dataref': ref, 'new_dataref': target.decode(),
                      'scope_begin_line': header['scope_begin_line'], 'key_count': len(header['keys'])})
    output = b''.join(lines)
    return output, {'object': f'{group}/{name}', 'source_sha256': sha256(data),
                    'output_sha256': sha256(output), 'source_bytes': len(data),
                    'output_bytes': len(output), 'header_token_changes': len(edits),
                    'all_other_bytes_preserved': True, 'headers': edits}


def bind_directory(source_dir: Path, output_dir: Path, report_path: Path):
    if output_dir.is_symlink() or report_path.is_symlink():
        raise ValueError('Output/report must not be symbolic links')
    source, output, report = source_dir.resolve(), output_dir.resolve(), report_path.resolve()
    if not source.is_dir():
        raise ValueError('Source directory does not exist')
    if output.exists() or output.is_relative_to(source) or source.is_relative_to(output):
        raise ValueError('Output must be a new directory outside the source tree')
    if report.exists() or report.is_relative_to(source) or report == output:
        raise ValueError('Report must be new and outside source')
    files = {}
    for group, expected in EXPECTED_FILES.items():
        folder = source / group
        found = {p.name: p for p in folder.glob('*.obj') if not p.name.startswith('._')}
        if set(found) != expected or folder.is_symlink() or any(p.is_symlink() or not p.is_file() for p in found.values()):
            raise ValueError(f'Expected exact audited regular {group} OBJ set')
        files.update({(group, name): path for name, path in found.items()})
    if report in {output / group / name for group, name in files}:
        raise ValueError('Report must not overwrite an output object')
    # Validate all source objects before reserving the new candidate directory.
    records = []
    for (group, name), path in sorted(files.items()):
        _, record = rebind_object(path.read_bytes(), group, name)
        records.append(record)
    output.mkdir(parents=True)
    for record in records:
        relative = Path(record['object'])
        original = (source / relative).read_bytes()
        if sha256(original) != record['source_sha256']:
            raise ValueError('Source changed during staging; do not deploy incomplete candidate')
        patched, _ = rebind_object(original, relative.parts[0], relative.name)
        target = output / relative
        target.parent.mkdir(exist_ok=True)
        with target.open('xb') as stream:
            stream.write(patched)
        if sha256(target.read_bytes()) != record['output_sha256']:
            raise ValueError('Staged object verification failed')
    if any(sha256((source / record['object']).read_bytes()) != record['source_sha256'] for record in records):
        raise ValueError('Source changed during staging; do not deploy candidate')
    result = {'format': 1, 'source_directory': str(source), 'output_directory': str(output),
              'source_untouched': True, 'header_token_changes': sum(r['header_token_changes'] for r in records),
              'custom_datarefs': sorted([*AILERONS.values(), *TAIL.values(), *SPOILERS.values()]),
              'objects': records}
    report.parent.mkdir(parents=True, exist_ok=True)
    with report.open('x') as stream:
        stream.write(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, required=True, help='Exterior root containing wings/ and fuselage/')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    result = bind_directory(args.source_dir, args.output_dir, args.report)
    print(f"Staged {len(result['objects'])} objects; changed {result['header_token_changes']} header tokens only")
    print(f'Report: {args.report}')


if __name__ == '__main__':
    main()
