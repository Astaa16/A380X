#!/usr/bin/env python3
"""Summarize recorded telemetry, without mistaking stale packets for a test pass."""
import argparse
import json
from pathlib import Path


def summarize(path):
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    fresh = [row for row in rows if row.get('packet_age') is not None and row['packet_age'] < 1
             and row.get('sim/time/paused') == 0
             and row.get('sim/flightmodel2/misc/has_crashed') is not None]
    disabled = [row for row in fresh if row.get('sim/operation/failures/enable_random_failures') == 0]
    def span(name):
        values = [row[name] for row in disabled if row.get(name) is not None]
        return [min(values), max(values)] if values else None
    flight_time = span('sim/time/total_flight_time_sec')
    seconds = flight_time[1] - flight_time[0] if flight_time else 0
    largest_gap = max((b['t'] - a['t'] for a, b in zip(disabled, disabled[1:])), default=0)
    failures = sorted({name for row in disabled for name, value in row.items()
                       if name.startswith('sim/operation/failures/rel_') and value not in (None, 0)})
    crash = any(row['sim/flightmodel2/misc/has_crashed'] != 0 for row in disabled)
    return {
        'source': str(path.resolve()), 'scope': 'ground-only telemetry; not a flight or visual acceptance test',
        'fresh_unpaused_samples': len(fresh), 'verified_random_failures_disabled_samples': len(disabled),
        'flight_time_range_sec': flight_time, 'observed_span_sec': seconds,
        'largest_observation_gap_sec': largest_gap,
        'sample_interval_limit_sec': 1, 'aircraft_crash_detected': crash,
        'nonzero_recorded_failure_datarefs': failures,
        'normal_g_range': span('sim/flightmodel/forces/g_nrml'),
        'pitch_deg_range': span('sim/flightmodel/position/theta'),
        'gear_contact_ranges': [span(f'sim/flightmodel2/gear/on_ground[{i}]') for i in range(5)],
        'sustained_ground_check': len(disabled) >= 50 and seconds >= 60 and largest_gap < 2 and not crash and not failures,
        'limitations': ['Aircraft crash dataref does not detect a native application crash.',
                       'Nose-wheel loading, taxi, takeoff, landing, and performance require separate tests.',
                       'Data gaps are excluded; the observed span is not proof of uninterrupted coverage.']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = summarize(args.input)
    encoded = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end='')


if __name__ == '__main__':
    main()
