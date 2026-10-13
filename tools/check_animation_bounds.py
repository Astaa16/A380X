#!/usr/bin/env python3
"""Check exported control geometry at a given pose without loading Blender.

Bounds are in the OBJ's native X-Plane coordinates, before aircraft attachment
offsets. This catches displaced animated parts that ordinary index validation
cannot detect. It is a geometry regression check, not a flight-model test.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from obj8_preview import load_obj8


def animation_bounds(preview, dataref):
    lo, hi = [math.inf] * 3, [-math.inf] * 3
    triangles, selected_draws = 0, 0
    for draw in preview.draws:
        if dataref not in draw.transform_datarefs:
            continue
        selected_draws += 1
        triangles += draw.count // 3
        # Bound memory by one draw call; do not build the entire posed mesh.
        for index in set(preview.indices[draw.start:draw.start + draw.count]):
            position = preview.positions[index * 3:index * 3 + 3]
            for axis in range(3):
                value = sum(draw.matrix[axis * 4 + k] * position[k] for k in range(3)) + draw.matrix[axis * 4 + 3]
                if not math.isfinite(value):
                    raise ValueError('Non-finite animated vertex')
                lo[axis], hi[axis] = min(lo[axis], value), max(hi[axis], value)
    if not triangles:
        raise ValueError(f'No visible triangles transformed by {dataref}')
    return {'minimum': lo, 'maximum': hi, 'triangles': triangles, 'draw_calls': selected_draws}


def bounds_errors(bounds, minimum, maximum, tolerance=1e-4):
    errors = []
    if any(not math.isfinite(value) for value in (*minimum, *maximum, tolerance)) or tolerance < 0:
        raise ValueError('Bounds and nonnegative tolerance must be finite')
    for axis, label in enumerate('xyz'):
        if minimum[axis] > maximum[axis]:
            raise ValueError(f'Invalid {label} interval')
        if bounds['minimum'][axis] < minimum[axis] - tolerance:
            errors.append(f'{label} minimum {bounds["minimum"][axis]:.6f} below {minimum[axis]:.6f}')
        if bounds['maximum'][axis] > maximum[axis] + tolerance:
            errors.append(f'{label} maximum {bounds["maximum"][axis]:.6f} above {maximum[axis]:.6f}')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('objects', type=Path, nargs='+')
    parser.add_argument('--filter-dataref', required=True)
    parser.add_argument('--value', type=float, default=0)
    parser.add_argument('--minimum', type=float, nargs=3, required=True, metavar=('X', 'Y', 'Z'))
    parser.add_argument('--maximum', type=float, nargs=3, required=True, metavar=('X', 'Y', 'Z'))
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if not math.isfinite(args.value):
        parser.error('--value must be finite')
    results = []
    for path in args.objects:
        try:
            preview = load_obj8(path, values={args.filter_dataref: args.value})
            bounds = animation_bounds(preview, args.filter_dataref)
            errors = bounds_errors(bounds, args.minimum, args.maximum)
            results.append({'object': str(path), **bounds, 'errors': errors})
            del preview
        except ValueError as error:
            results.append({'object': str(path), 'errors': [str(error)]})
    report = {'dataref': args.filter_dataref, 'value': args.value,
              'expected_minimum': args.minimum, 'expected_maximum': args.maximum,
              'objects': results, 'passed': all(not row['errors'] for row in results)}
    text = json.dumps(report, indent=2)
    print(text)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text + '\n')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
