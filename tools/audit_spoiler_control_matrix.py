#!/usr/bin/env python3
"""Offline native-spoiler matrix guards; not an aerodynamic/flightworthiness test.

Each OBJ is first fully parsed and validated. Repeated matrix-only samples then
replace geometry with one dummy triangle per TRIS (preserving draw ordinal and
every state/animation command). Equivalence to full parsing is checked at zero
and all-native-controls=50 before trusting the compact evaluator.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path

from audit_control_surface_motion import ExplicitValues, load_sample
from obj8_preview import parse_obj8


PREFIX = 'sim/flightmodel2/wing/'
NATIVE = tuple(PREFIX + name for name in (
    'speedbrake1_deg[0]', 'speedbrake1_deg[1]',
    'spoiler1_deg[0]', 'spoiler1_deg[1]', 'spoiler1_deg[2]', 'spoiler1_deg[3]',
    'spoiler2_deg[4]', 'spoiler2_deg[5]'))
RIGHT_ROLL = (NATIVE[3], NATIVE[5], NATIVE[7])
FLAP = 'sim/flightmodel2/controls/flap1_deploy_ratio'
BASE = {ref: 0. for ref in NATIVE} | {
    'sim/flightmodel2/controls/roll_ratio': 0., FLAP: 0.,
    'sim/flightmodel2/controls/slat1_deploy_ratio': 0.,
    'sim/flightmodel2/gear/deploy_ratio[1]': 1.,
    'sim/flightmodel2/gear/deploy_ratio[2]': 1.,
}
VALUES = (-1000., -50., -25., 0., 25., 50., 75., 100., 1000.)


def compact_commands(path):
    commands = ['VT 0 0 0 0 1 0 0 0', 'VT 1 0 0 0 1 0 1 0',
                'VT 0 0 1 0 1 0 0 1', 'IDX 0', 'IDX 1', 'IDX 2']
    with path.open() as stream:
        for line in stream:
            words = line.split('#', 1)[0].split()
            if not words or words[0] in ('VT', 'IDX', 'IDX10', 'POINT_COUNTS'):
                continue
            commands.append('TRIS 0 3' if words[0] == 'TRIS' else line)
    return commands


def matrices(obj):
    result = {}
    for draw in obj.draws:
        if not all(math.isfinite(v) for v in draw.matrix):
            raise ValueError('Nonfinite animation matrix')
        result[draw.draw_index] = draw.matrix
    return result


def same(first, second, tolerance=1e-10):
    return first.keys() == second.keys() and all(
        max(abs(a-b) for a, b in zip(first[index], second[index])) <= tolerance
        for index in first)


def changed(first, second):
    if first.keys() != second.keys():
        raise ValueError('Unexpected draw visibility change')
    return [index for index in first if not same({index: first[index]}, {index: second[index]})]


def expected_ref(side, number):
    right = side == 'R'
    return NATIVE[(1 if right else 0) if number < 3 else
                  (3 if right else 2) if number == 3 else
                  (5 if right else 4) if number < 7 else (7 if right else 6)]


def audit_object(path):
    full, metadata = load_sample(path, BASE, 0.)
    commands = compact_commands(path)
    def sample(values):
        return matrices(parse_obj8(commands, ExplicitValues(values), source=str(path)))
    neutral = sample(BASE)
    if not same(neutral, matrices(full)):
        raise ValueError('Compact evaluator differs from full neutral parse')
    full_values = BASE | {ref: 50. for ref in NATIVE}
    all_full = sample(full_values)
    checked, checked_metadata = load_sample(path, full_values, 0.)
    if checked_metadata != metadata or not same(all_full, matrices(checked)):
        raise ValueError('Compact evaluator differs from full deployed parse or source changed')
    refs = {d.draw_index: set(d.transform_datarefs) for d in full.draws}
    native_draws = {i for i, values in refs.items() if values.intersection(NATIVE)}
    side_refs = set(NATIVE[::2] if path.stem in ('a380x_wing1', 'a380x_wing1_2') else NATIVE[1::2])
    opposite_side = [i for i in native_draws if refs[i].intersection(NATIVE) - side_refs]
    records = []
    for ref in NATIVE:
        deployed = sample(BASE | {ref: 50.})
        entries = []
        for value in VALUES:
            current = sample(BASE | {ref: value})
            moved = changed(neutral, current)
            entries.append({'value': value, 'changed_draws': moved,
                'changed_triangles': sum(metadata['draws'][i]['index_count']//3 for i in moved),
                'matches_neutral': same(current, neutral), 'matches_50': same(current, deployed),
                'unlinked_changed_draws': [i for i in moved if ref not in refs[i]]})
        records.append({'dataref': ref, 'samples': entries,
            'negative_clamp_ok': all(v['matches_neutral'] for v in entries if v['value'] <= 0),
            'upper_clamp_ok': all(v['matches_50'] for v in entries if v['value'] >= 50),
            'isolated_to_linked_draws': all(not v['unlinked_changed_draws'] for v in entries)})
    right = sample(BASE | {ref: 50. for ref in RIGHT_ROLL})
    right_moved = changed(neutral, right)
    combined = sample(full_values | {FLAP: 1.})
    mixed = [i for i in native_draws if len(refs[i].intersection(NATIVE)) > 1]
    wrong_side = [i for i in right_moved if not refs[i].intersection(RIGHT_ROLL)]
    flap_changed = [i for i in native_draws if not same({i: all_full[i]}, {i: combined[i]})]
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != metadata['sha256']:
        raise ValueError('Source changed during matrix audit')
    return {'object': str(path), 'sha256': digest, 'dataref_values': BASE,
        'draw_count': len(full.draws), 'draw_catalog': metadata['draws'],
        'old_speedbrake_ratio_present': 'sim/flightmodel2/controls/speedbrake_ratio' in full.datarefs,
        'compact_evaluator_matches_full_at_neutral_and_all_50': True,
        'groups': records, 'mixed_native_group_draws': mixed,
        'opposite_side_native_draws': opposite_side,
        'right_roll_only_changed_draws': right_moved,
        'right_roll_only_unexpected_draws': wrong_side,
        'flap_full_changes_spoiler_draw_matrices': flap_changed}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--export-report', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    objects = []
    for suffix in ('1', '1_2', '2', '2_2'):
        result = audit_object(args.directory / f'a380x_wing{suffix}.obj')
        objects.append(result)
        print(f"{Path(result['object']).name}: {result['draw_count']} draws; matrix audit complete", flush=True)
    export = json.loads(args.export_report.read_text())['sources'][0]
    panels = export['rigid_spoiler_panels']['panels']
    partitions = {p['object']: p for p in export['skin_partitions']}
    targets = export['converted_targets']
    panel_records = []
    for panel in panels:
        ref = expected_ref(panel['side'], panel['number'])
        animation = [t for t in targets if t['target'] == panel['representative_bone'] and t['status'] == 'animated']
        partition = partitions[panel['object']]
        moving = sum(p['polygons'] for p in partition['parts'] if p['bone'] == panel['representative_bone'])
        fixed = sum(p['polygons'] for p in partition['parts'] if p['bone'] != panel['representative_bone'])
        panel_records.append({'object': panel['object'], 'side': panel['side'], 'number': panel['number'],
            'expected_ref': ref, 'representative_bone': panel['representative_bone'],
            'moving_triangles': moving, 'fixed_triangles': fixed,
            'counts_match': moving == panel['moving_polygons'] and fixed == panel['fixed_polygons'],
            'mirrored_native_mapping_ok': len(animation) == 1 and animation[0]['dataref'] == ref})
    # Names do not survive OBJ export. Check the required multiset of
    # (material OBJ, native ref, triangle count) signatures without pretending
    # that equal-sized draws establish source-vertex identity.
    required, available, signature_records = Counter(), Counter(), []
    for panel in panels:
        filename = 'a380x_' + panel['material'].removeprefix('A380_EXTERIOR_').lower() + '.obj'
        key = (filename, expected_ref(panel['side'], panel['number']), panel['moving_polygons'])
        required[key] += 1
    for obj in objects:
        name = Path(obj['object']).name
        for group in obj['groups']:
            ref = group['dataref']
            moved = next(sample['changed_draws'] for sample in group['samples'] if sample['value'] == 50.)
            for index in moved:
                draw = obj['draw_catalog'][index]
                available[(name, ref, draw['index_count']//3)] += 1
    for key, count in sorted(required.items()):
        signature_records.append({'object': key[0], 'dataref': key[1], 'triangles_per_draw': key[2],
                                  'required_draws': count, 'matching_moving_draws': available[key]})
    rest_records = []
    for number in (3, 5, 7, 9, 11):
        name = f'WING_MFTF_{number}.L'
        entries = [t for t in targets if t['target'] == name]
        rest_records.append({'target': name, 'entries': entries,
            'rest_clip_omitted_and_spoiler_animated':
                len(entries) == 2 and any(t['action'] == 'l_flap_percent_key' and t['status'] == 'redundant_rest_pose_omitted' for t in entries)
                and any(t['action'] == 'l_spoiler1_key' and t['status'] == 'animated' and t['dataref'] == NATIVE[0] for t in entries)})
    checks = {
        'all_matrix_clamps_pass': all(g['negative_clamp_ok'] and g['upper_clamp_ok'] for o in objects for g in o['groups']),
        'group_isolation_pass': all(g['isolated_to_linked_draws'] for o in objects for g in o['groups']),
        'no_old_speedbrake_ratio': all(not o['old_speedbrake_ratio_present'] for o in objects),
        'no_additive_native_groups': all(not o['mixed_native_group_draws'] for o in objects),
        'no_opposite_side_native_draws': all(not o['opposite_side_native_draws'] for o in objects),
        'right_roll_only_isolation_pass': all(not o['right_roll_only_unexpected_draws'] for o in objects),
        'combined_flap_keeps_spoiler_matrices': all(not o['flap_full_changes_spoiler_draw_matrices'] for o in objects),
        'panel_counts_match': all(p['counts_match'] for p in panel_records),
        'moving_fixed_totals_match': sum(p['moving_triangles'] for p in panel_records) == 45030 and sum(p['fixed_triangles'] for p in panel_records) == 46,
        'sixteen_panels': len({(p['side'], p['number']) for p in panel_records}) == 16,
        'mirrored_panel_refs_match': all(p['mirrored_native_mapping_ok'] for p in panel_records),
        'panel_signatures_present_in_moving_obj_draws': all(available[key] >= count for key, count in required.items()),
        'five_left_mftf_correctly_owned': all(p['rest_clip_omitted_and_spoiler_animated'] for p in rest_records),
    }
    report = {'diagnostic_only': True, 'limits': 'Matrix/control routing and exporter-count evidence only. Does not establish source-skin accuracy, collisions, native runtime scheduling or flightworthiness.',
        'checks': checks, 'all_checks_pass': all(checks.values()), 'objects': objects,
        'source_panels': panel_records, 'panel_draw_signature_crosscheck': signature_records,
        'left_mftf': rest_records}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(checks, indent=2), flush=True)
    print(f'Report: {args.report}', flush=True)
    return 0 if report['all_checks_pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
