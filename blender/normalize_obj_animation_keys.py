"""Normalize OBJ8 key-table order without changing animation or geometry.

XPlane2Blender emits transform keys in source frame order. Inverted mappings
can therefore create descending dataref values, but OBJ8 requires ascending
tables. Reorder entire key rows, never axes, transforms, or geometry commands.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import tempfile
from pathlib import Path
from typing import Iterable, TextIO


def normalize_animation_stream(source: Iterable[str], target: TextIO) -> dict[str, int]:
    stats = {'tables': 0, 'reordered_tables': 0, 'equal_duplicate_keys_removed': 0}
    kind, entries, leading, original = None, [], [], []
    for line_number, line in enumerate(source, 1):
        words = line.split('#', 1)[0].split()
        command = words[0] if words else ''
        if kind is None:
            if command in ('ANIM_rotate_begin', 'ANIM_trans_begin'):
                kind = command[5:-6]
                if len(words) != (5 if kind == 'rotate' else 2):
                    raise ValueError(f'Line {line_number}: malformed animation table header')
                entries, leading, original = [], [], []
                stats['tables'] += 1
            elif command in ('ANIM_rotate_key', 'ANIM_trans_key', 'ANIM_rotate_end', 'ANIM_trans_end'):
                raise ValueError(f'Line {line_number}: animation key/end outside a table')
            target.write(line)
            continue

        if command == f'ANIM_{kind}_end':
            if len(words) != 1 or not entries:
                raise ValueError(f'Line {line_number}: malformed or empty animation table')
            ordered = sorted(entries, key=lambda entry: entry['key'])
            unique = []
            for entry in ordered:
                if unique and entry['key'] == unique[-1]['key']:
                    if entry['transform'] != unique[-1]['transform']:
                        raise ValueError(f'Line {line_number}: conflicting duplicate animation key {entry["key"]}')
                    stats['equal_duplicate_keys_removed'] += 1
                    # The redundant row is removed, but no attached comment is
                    # lost. Keep any inline comment as an ordinary comment.
                    unique[-1]['tail'].extend(entry['leading'])
                    if '#' in entry['line']:
                        comment = entry['line'].split('#', 1)[1]
                        indent = entry['line'][:len(entry['line']) - len(entry['line'].lstrip())]
                        unique[-1]['tail'].append(indent + '#' + comment)
                    continue
                unique.append(entry)
            content = ''.join(''.join(entry['leading']) + entry['line'] + ''.join(entry['tail']) for entry in unique) + ''.join(leading)
            stats['reordered_tables'] += content != ''.join(original)
            target.write(content)
            target.write(line)
            kind = None
            continue

        original.append(line)
        if not command:
            leading.append(line)
        elif command == f'ANIM_{kind}_key':
            if len(words) != (3 if kind == 'rotate' else 5):
                raise ValueError(f'Line {line_number}: malformed animation key')
            try:
                values = tuple(float(value) for value in words[1:])
            except ValueError as error:
                raise ValueError(f'Line {line_number}: nonnumeric animation key') from error
            if not all(math.isfinite(value) for value in values):
                raise ValueError(f'Line {line_number}: nonfinite animation key')
            entries.append({'key': values[0], 'transform': values[1:], 'leading': leading,
                            'line': line, 'tail': []})
            leading = []
        else:
            raise ValueError(f'Line {line_number}: unexpected {command} in {kind} animation table')
    if kind is not None:
        raise ValueError(f'Unclosed {kind} animation table')
    return stats


def normalize_obj_animation_keys(path: Path) -> dict[str, int]:
    """Atomically rewrite only if needed; malformed input is never replaced."""
    path = Path(path)
    temporary = None
    try:
        with path.open(encoding='utf-8', newline='') as source:
            with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='',
                                             dir=path.parent, prefix=f'.{path.name}.keys-', delete=False) as target:
                temporary = Path(target.name)
                stats = normalize_animation_stream(source, target)
        if stats['reordered_tables']:
            os.chmod(temporary, path.stat().st_mode & 0o777)
            os.replace(temporary, path)
            temporary = None
        return stats
    finally:
        if temporary is not None:
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('objects', nargs='+', type=Path)
    args = parser.parse_args()
    print(json.dumps({str(path): normalize_obj_animation_keys(path) for path in args.objects}, indent=2))


if __name__ == '__main__':
    main()
