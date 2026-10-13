#!/usr/bin/env python3
"""Record native spoiler sign evidence from shipped OBJ8 geometry, offline.

Input aircraft are read-only. Descending key tables in shipped assets are
reordered only in memory so the strict preview evaluator can read them.
This evaluates the authored geometry, not the running X-Plane flight model.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from normalize_obj_animation_keys import normalize_animation_stream
from obj8_preview import parse_obj8


REFERENCE_PATTERN = re.compile(r'sim/flightmodel2/wing/(?:spoiler|speedbrake)[12]_deg\[\d+\]')
SAMPLE_VALUES = (0., 10., -10.)


def transformed_points(positions, vertex_ids, matrix):
    return [tuple(sum(matrix[row * 4 + k] * positions[vertex * 3 + k] for k in range(3))
                  + matrix[row * 4 + 3] for row in range(3))
            for vertex in vertex_ids]


def summarize_points(points):
    return {
        'bounds_min_xyz_m': [min(point[axis] for point in points) for axis in range(3)],
        'bounds_max_xyz_m': [max(point[axis] for point in points) for axis in range(3)],
        'mean_xyz_m': [statistics.mean(point[axis] for point in points) for axis in range(3)],
    }


def audit(path):
    original = path.read_bytes()
    original_text = original.decode('utf-8-sig')
    normalized = io.StringIO()
    key_statistics = normalize_animation_stream(io.StringIO(original_text), normalized)
    lines = normalized.getvalue().splitlines()
    refs = sorted(set(REFERENCE_PATTERN.findall(original_text)))
    if not refs:
        raise ValueError(f'No native spoiler/speedbrake animation references: {path}')
    states = {value: parse_obj8(lines, values={ref: value for ref in refs}, source=str(path))
              for value in SAMPLE_VALUES}
    draw_maps = {value: {draw.draw_index: draw for draw in obj.draws}
                 for value, obj in states.items()}
    neutral = states[0.]
    records = []
    for draw in neutral.draws:
        controlled_by = [ref for ref in draw.transform_datarefs if ref in refs]
        if not controlled_by:
            continue
        vertex_ids = sorted(set(neutral.indices[draw.start:draw.start + draw.count]))
        sampled = {}
        for value in SAMPLE_VALUES:
            moved = draw_maps[value][draw.draw_index]
            points = transformed_points(neutral.positions, vertex_ids, moved.matrix)
            sampled[str(value)] = summarize_points(points)
        positive_delta = sampled['10.0']['mean_xyz_m'][1] - sampled['0.0']['mean_xyz_m'][1]
        negative_delta = sampled['-10.0']['mean_xyz_m'][1] - sampled['0.0']['mean_xyz_m'][1]
        records.append({
            'native_datarefs': controlled_by,
            'all_transform_datarefs': list(draw.transform_datarefs),
            'draw_index': draw.draw_index,
            'normalized_source_line': draw.source_line,
            'tris_index_offset': draw.start,
            'tris_index_count': draw.count,
            'unique_vertices': len(vertex_ids),
            'samples_native_degrees': sampled,
            'positive10_mean_y_delta_m': positive_delta,
            'negative10_mean_y_delta_m': negative_delta,
            'positive_deployment_raises_geometry': positive_delta > 1e-6 and negative_delta < -1e-6,
        })
    if not records:
        raise ValueError(f'No visible triangles controlled by the native references: {path}')
    if path.read_bytes() != original:
        raise RuntimeError(f'Input changed during audit: {path}')
    return {
        'source': str(path.resolve()),
        'source_sha256': hashlib.sha256(original).hexdigest(),
        'source_bytes': len(original),
        'source_unchanged': True,
        'key_table_normalization_in_memory_only': key_statistics,
        'native_datarefs': refs,
        'controlled_draws': records,
        'summary': {
            'controlled_draw_count': len(records),
            'positive_raises_draw_count': sum(r['positive_deployment_raises_geometry'] for r in records),
            'all_controlled_draws_raise_for_positive_degrees': all(r['positive_deployment_raises_geometry'] for r in records),
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('objects', nargs='+', type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    report_path = args.report.resolve()
    if report_path in {path.resolve() for path in args.objects}:
        raise ValueError('Report must not overwrite an input object')
    report = {
        'purpose': 'Offline sign verification from shipped Laminar aircraft animation and triangle geometry',
        'coordinates': 'X-Plane OBJ8 coordinates; +Y is up; metres',
        'method': 'Evaluate each controlled TRIS at native dataref 0, +10, -10 degrees; compare identical vertex sets',
        'defaults': 'All audited spoiler/speedbrake refs receive the sample value; other refs zero except preview gear defaults',
        'limitations': [
            'Authored native-aircraft geometry evidence, not a live simulator dataref measurement.',
            'Other control inputs remain neutral; no source asset is modified.',
            'A380 uses fewer native physical channels than its sixteen visual spoiler panels.',
            'Panel associations are a visual approximation, not an exact FBW flight-control law.',
        ],
        'sources': [audit(path) for path in args.objects],
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'report': str(report_path), 'sources': [
        {'source': source['source'], **source['summary']} for source in report['sources']
    ]}, indent=2))


if __name__ == '__main__':
    main()
