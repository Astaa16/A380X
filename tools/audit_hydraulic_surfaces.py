#!/usr/bin/env python3
"""Independently evaluate installed OBJ geometry at relaxed hydraulic poses."""
from collections import Counter
import argparse
import io
import json
from pathlib import Path
import re
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'blender'))
from obj8_preview import parse_obj8
from bind_hydraulic_surfaces import AILERONS, TAIL, SPOILERS, PREFIX, EXPECTED_FILES


def matrix_program(text):
    result = ['A', '800', 'OBJ', 'VT 0 0 0 0 1 0 0 0', 'VT 1 0 0 0 1 0 0 0',
              'VT 0 1 0 0 1 0 0 0', 'IDX 0', 'IDX 1', 'IDX 2']
    for line in text.splitlines():
        words = line.split('#', 1)[0].split()
        if not words or words[0] in {'VT', 'IDX', 'IDX10', 'POINT_COUNTS', 'A', 'I', '800', 'OBJ'}:
            continue
        result.append('TRIS 0 3' if words[0] == 'TRIS' else line)
    return result


def point(matrix, values, index):
    xyz = values[index * 3:index * 3 + 3]
    return tuple(sum(matrix[row * 4 + col] * xyz[col] for col in range(3)) + matrix[row * 4 + 3]
                 for row in range(3))


def audit(baseline, candidate):
    mapping = {**AILERONS, **TAIL, **SPOILERS}
    inverse = {target: old for old, target in mapping.items()}
    relaxed = {ref: (2 / 3 if '_l_' in ref else -2 / 3) for ref in AILERONS.values()}
    relaxed[PREFIX + 'elevator_ratio'] = -2 / 3
    cases = {'droop': relaxed, 'wind_from_east': {PREFIX + 'rudder_ratio': -2 / 3},
             'wind_from_west': {PREFIX + 'rudder_ratio': 2 / 3}}
    result = {'offline_only': True, 'objects': [], 'surfaces': [], 'unrelated_draw_checks': 0}
    seen = Counter()
    for group, names in EXPECTED_FILES.items():
        for name in sorted(names):
            old_bytes = (baseline / group / name).read_bytes()
            new_bytes = (candidate / group / name).read_bytes()
            # This reverse proof is independent of the forward rebinder.
            def restore(match):
                ref = match[2].decode()
                assert ref in inverse, f'Unrecognized final ref {ref}'
                return match[1] + inverse[ref].encode()
            restored = re.sub(rb'(?m)^([ \t]*ANIM_(?:rotate|trans)_begin[^\r\n#]*?)(a380x/xp12/hydraulics/[a-z_]+)(?=[\s#]|$)', restore, new_bytes)
            assert restored == old_bytes, f'Non-binding change in {group}/{name}'
            new_text = new_bytes.decode()
            obj = parse_obj8(io.StringIO(new_text), source=name)
            program = matrix_program(new_text)
            old_program = matrix_program(old_bytes.decode())
            neutral_old = parse_obj8(old_program)
            assert len(neutral_old.draws) == len(obj.draws)
            for old, new in zip(neutral_old.draws, obj.draws):
                assert old.matrix == new.matrix and old.cull == new.cull and old.alpha_cutoff == new.alpha_cutoff
            for case, values in cases.items():
                posed = parse_obj8(program, values)
                assert len(posed.draws) == len(obj.draws)
                for draw, changed in zip(obj.draws, posed.draws):
                    affected = set(draw.transform_datarefs) & set(values)
                    if not affected:
                        assert draw.matrix == changed.matrix, f'Unrelated draw moved {name}:{draw.draw_index}'
                        result['unrelated_draw_checks'] += 1
                        continue
                    assert len(affected) == 1
                    ref = next(iter(affected))
                    ids = set(obj.indices[draw.start:draw.start + draw.count])
                    points = {i: point(draw.matrix, obj.positions, i) for i in ids}
                    # Exterior ACF attachments are yawed 180 degrees: aft=-Z
                    # locally, aircraft right=-X locally; vertical stays +Y.
                    aft, fore = min(p[2] for p in points.values()), max(p[2] for p in points.values())
                    trailing = [i for i, p in points.items() if p[2] <= aft + .05 * (fore - aft)]
                    delta = [sum(point(changed.matrix, obj.positions, i)[k] - points[i][k]
                                 for i in trailing) / len(trailing) for k in range(3)]
                    record = {'object': f'{group}/{name}', 'draw': draw.draw_index, 'triangles': draw.count // 3,
                              'case': case, 'ref': ref, 'trailing_mean_vertical_delta_m': delta[1],
                              'trailing_mean_aircraft_right_delta_m': -delta[0]}
                    result['surfaces'].append(record)
                    if case == 'droop':
                        assert delta[1] < -0.001, f'Droop went up or did not move: {record}'
                    elif case == 'wind_from_east':
                        assert -delta[0] < -0.001, f'Rudder wind sign wrong: {record}'
                    else:
                        assert -delta[0] > 0.001, f'Rudder wind sign wrong: {record}'
                    seen[ref] += 1
            result['objects'].append({'object': f'{group}/{name}', 'neutral_unchanged': True,
                                      'only_header_tokens_changed': True, 'triangles': len(obj.indices) // 3})
    assert set(seen) == set(AILERONS.values()) | set(TAIL.values()), f'Missing surface geometry: {seen}'
    result['passed'] = True
    result['affected_surface_refs'] = dict(seen)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'candidate', 'report'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.baseline, args.candidate)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(f"Geometry directions passed: {len(result['surfaces'])} affected draws, "
          f"{result['unrelated_draw_checks']} unchanged draw checks")

if __name__ == '__main__':
    main()
