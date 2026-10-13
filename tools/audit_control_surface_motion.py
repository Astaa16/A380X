#!/usr/bin/env python3
"""Measure OBJ8 motion offline; diagnostic only, not a physical pass/fail test.

Uses obj8_preview's OBJ8 evaluator without Blender, textures, or triangle baking.
Coordinates remain in the OBJ's own meter-based coordinate system; aircraft OBJ
attachment transforms, simulator failures, skinning fidelity, and aerodynamics
are not evaluated. Every animation dataref needs an explicit supplied value.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from obj8_preview import parse_obj8


class ExplicitValues(dict):
    """Prevent preview's convenient zero/gear-down defaults in strict audits."""
    def __bool__(self):
        return True

    def __contains__(self, key):
        if not super().__contains__(key):
            raise ValueError(f'Missing explicit dataref value: {key}; provide --set {key}=VALUE')
        return True


def finite(value, label):
    number = float(value)
    if not math.isfinite(number):
        raise ValueError(f'{label} must be finite')
    return number


def validated_lines(path, metadata):
    """Stream source validation, content hash, and every TRIS identity."""
    digest = hashlib.sha256()
    transform_refs, scopes = (), []
    with path.open('rb') as stream:
        for line_number, raw in enumerate(stream, 1):
            digest.update(raw)
            line = raw.decode('utf-8')
            words = line.split('#', 1)[0].split()
            if words:
                command, args = words[0], words[1:]
                numbers = []
                if command in ('VT', 'ANIM_trans_key', 'ANIM_rotate_key', 'ANIM_keyframe_loop',
                               'ATTR_LOD', 'ATTR_no_blend', 'GLOBAL_no_blend'):
                    numbers = args
                elif command == 'ANIM_rotate_begin':
                    numbers = args[:3]
                elif command in ('ANIM_hide', 'ANIM_show'):
                    numbers = args[:2]
                elif command in ('ANIM_rotate', 'ANIM_trans'):
                    base = 5 if command == 'ANIM_rotate' else 6
                    numbers = args[:base if len(args) == base else base + 2]
                for number in numbers:
                    finite(number, f'{path}:{line_number}: {command} numeric value')
                if command == 'ANIM_begin':
                    scopes.append(transform_refs)
                elif command == 'ANIM_end' and scopes:
                    transform_refs = scopes.pop()
                elif command == 'ATTR_LOD':
                    transform_refs = ()
                name = None
                if command in ('ANIM_rotate_begin', 'ANIM_trans_begin') and args:
                    name = args[-1]
                elif command in ('ANIM_rotate', 'ANIM_trans'):
                    base = 5 if command == 'ANIM_rotate' else 6
                    if len(args) == base + 3:
                        name = args[-1]
                if name and name not in transform_refs:
                    transform_refs += (name,)
                if command == 'TRIS':
                    start, count = map(int, args)
                    metadata['draws'].append({
                        'draw_index': len(metadata['draws']), 'source_line': line_number,
                        'index_start': start, 'index_count': count,
                        'transform_datarefs': list(transform_refs),
                    })
            yield line
    metadata['sha256'] = digest.hexdigest()


def load_sample(path, values, lod_distance):
    metadata = {'draws': []}
    obj = parse_obj8(validated_lines(path, metadata), ExplicitValues(values),
                     lod_distance=lod_distance, source=str(path))
    for draw in obj.draws:
        for number in draw.matrix:
            finite(number, f'{path}: TRIS {draw.draw_index} transformed matrix')
    return obj, metadata


def point(matrix, positions, index):
    offset = index * 3
    x, y, z = positions[offset:offset + 3]
    result = tuple(matrix[row * 4] * x + matrix[row * 4 + 1] * y +
                   matrix[row * 4 + 2] * z + matrix[row * 4 + 3] for row in range(3))
    if not all(math.isfinite(number) for number in result):
        raise ValueError(f'Nonfinite transformed vertex {index}')
    return result


def extend_bounds(bounds, xyz):
    if bounds is None:
        return [list(xyz), list(xyz)]
    for axis in range(3):
        bounds[0][axis] = min(bounds[0][axis], xyz[axis])
        bounds[1][axis] = max(bounds[1][axis], xyz[axis])
    return bounds


def summarize_sample(obj, neutral, catalog):
    """Visit unique source vertices per draw; never expand triangle arrays.

    Peak working data is two compact parser results and a set for ONE draw.
    A vertex reused in another draw is counted again because its matrix can
    differ. RMS and centroids are vertex-weighted, not area/index-weighted.
    """
    baseline = {draw.draw_index: draw for draw in neutral.draws}
    current = {draw.draw_index: draw for draw in obj.draws}
    results, bounds = [], None
    sum_squared, max_distance, compared_vertices = 0., 0., 0
    visible_vertices = 0
    for identity in catalog:
        index = identity['draw_index']
        draw, reference = current.get(index), baseline.get(index)
        record = dict(identity, visible=draw is not None, neutral_visible=reference is not None)
        if draw is None:
            record.update(bounds_m=None, centroid_m=None, unique_vertex_count=0,
                          matrix_obj8=None,
                          max_displacement_m=None, rms_displacement_m=None,
                          centroid_displacement_m=None)
            results.append(record)
            continue
        if reference is not None and (draw.start, draw.count) != (reference.start, reference.count):
            raise ValueError(f'Draw identity {index} changed index range')
        seen = set()
        local_bounds, sums, delta_sums = None, [0., 0., 0.], [0., 0., 0.]
        squared, maximum = 0., 0.
        for slot in range(draw.start, draw.start + draw.count):
            vertex = obj.indices[slot]
            if vertex in seen:
                continue
            seen.add(vertex)
            xyz = point(draw.matrix, obj.positions, vertex)
            local_bounds = extend_bounds(local_bounds, xyz)
            for axis in range(3):
                sums[axis] += xyz[axis]
            if reference is not None:
                original = point(reference.matrix, neutral.positions, vertex)
                delta = tuple(a - b for a, b in zip(xyz, original))
                distance_squared = sum(number * number for number in delta)
                finite(distance_squared, f'Draw {index} squared displacement')
                squared += distance_squared
                maximum = max(maximum, distance_squared)
                for axis in range(3):
                    delta_sums[axis] += delta[axis]
        count = len(seen)
        if local_bounds is not None:
            bounds = extend_bounds(bounds, local_bounds[0])
            bounds = extend_bounds(bounds, local_bounds[1])
        visible_vertices += count
        record.update(
            bounds_m=local_bounds,
            centroid_m=[finite(number / count, 'Centroid') for number in sums] if count else None,
            unique_vertex_count=count, transform_datarefs=list(draw.transform_datarefs),
            matrix_obj8=list(draw.matrix),
            max_displacement_m=math.sqrt(maximum) if reference is not None and count else None,
            rms_displacement_m=math.sqrt(finite(squared, 'Squared displacement sum') / count)
            if reference is not None and count else None,
            centroid_displacement_m=[finite(number / count, 'Centroid displacement') for number in delta_sums]
            if reference is not None and count else None,
        )
        if reference is not None:
            sum_squared += squared
            max_distance = max(max_distance, maximum)
            compared_vertices += count
        results.append(record)
    return {
        'bounds_m': bounds, 'visible_draw_count': len(current),
        'draw_vertex_count': visible_vertices, 'compared_draw_vertex_count': compared_vertices,
        'max_displacement_m': math.sqrt(max_distance) if compared_vertices else None,
        'rms_displacement_m': math.sqrt(finite(sum_squared, 'Total squared displacement') / compared_vertices)
        if compared_vertices else None,
        'appeared_draws': sorted(current.keys() - baseline.keys()),
        'disappeared_draws': sorted(baseline.keys() - current.keys()),
        'draws': results,
    }


def audit_object(path, dataref, values, neutral_value=0., fixed_values=None, lod_distance=0.):
    path = Path(path).resolve()
    neutral_value = finite(neutral_value, 'Neutral value')
    lod_distance = finite(lod_distance, 'LOD distance')
    if lod_distance < 0:
        raise ValueError('LOD distance must be nonnegative')
    values = [finite(value, 'Sample value') for value in values]
    if not values or not dataref:
        raise ValueError('A nonempty dataref and at least one sample value are required')
    fixed = {name: finite(value, f'Dataref {name}') for name, value in (fixed_values or {}).items()}
    if dataref in fixed:
        raise ValueError('Selected --dataref must not also be supplied with --set')
    neutral, metadata = load_sample(path, dict(fixed, **{dataref: neutral_value}), lod_distance)
    if dataref not in neutral.datarefs:
        raise ValueError(f'{path}: Selected dataref is absent: {dataref}')
    report = {
        'object': str(path), 'sha256': metadata['sha256'],
        'source_vertex_count': len(neutral.positions) // 3, 'index_count': len(neutral.indices),
        'source_draw_count': len(metadata['draws']), 'neutral_datarefs': neutral.datarefs,
        'neutral': dict(value=neutral_value, **summarize_sample(neutral, neutral, metadata['draws'])),
        'samples': [],
    }
    for value in values:
        if value == neutral_value:
            summary = report['neutral'].copy()
        else:
            sampled, sample_metadata = load_sample(path, dict(fixed, **{dataref: value}), lod_distance)
            if sample_metadata != metadata:
                raise ValueError(f'{path}: Object changed while auditing; retry against stable source files')
            summary = dict(value=value, **summarize_sample(sampled, neutral, metadata['draws']))
            del sampled
        report['samples'].append(summary)
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('objects', type=Path, nargs='+')
    parser.add_argument('--dataref', required=True)
    parser.add_argument('--values', type=float, nargs='+', required=True)
    parser.add_argument('--neutral', type=float, default=0.)
    parser.add_argument('--set', dest='fixed', action='append', default=[], metavar='DATAREF=VALUE',
                        help='Explicit value for each other animation dataref; repeat as needed')
    parser.add_argument('--lod-distance', type=float, default=0.)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        fixed = {}
        for assignment in args.fixed:
            name, value = assignment.rsplit('=', 1)
            if not name or name in fixed:
                raise ValueError(f'Empty or duplicate --set dataref: {name}')
            fixed[name] = finite(value, f'Dataref {name}')
        if args.report.resolve() in {path.resolve() for path in args.objects}:
            raise ValueError('Report must not overwrite an input object')
        report = {
            'schema_version': 1, 'diagnostic_only': True, 'dataref': args.dataref,
            'coordinate_system': 'OBJ8 object-local coordinates, meters; no aircraft attachment transform',
            'metric_basis': 'Unique source vertices per draw; repeat across draws; RMS not area-weighted. '
                            'Displacement compares only draws visible in both neutral and sample.',
            'limitations': 'No physical pass/fail thresholds, simulator state, skinning validation, or aerodynamic evaluation.',
            'lod_distance_m': args.lod_distance,
            'objects': [audit_object(path, args.dataref, args.values, args.neutral, fixed, args.lod_distance)
                        for path in args.objects],
        }
        serialized = json.dumps(report, indent=2, allow_nan=False) + '\n'
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(serialized)
    except (ValueError, OSError) as error:
        parser.exit(2, f'Motion audit failed: {error}\n')
    for item in report['objects']:
        motions = ', '.join(f"{sample['value']:g}: " +
                            (f"{sample['max_displacement_m']:.6g} m max"
                             if sample['max_displacement_m'] is not None else 'no comparable draws')
                            for sample in item['samples'])
        print(f"{Path(item['object']).name}: {item['source_draw_count']} draws; {motions}")
    print(f'Diagnostic only; report: {args.report}')


if __name__ == '__main__':
    main()
