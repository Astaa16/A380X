#!/usr/bin/env python3
"""Early live frame-period evidence, before camera/setup or ND health waits.

This samples the existing stationary session and injects only the same diagnostic
cursor datarefs as check_nd_quality_live.py. It does not test physical OS input.
"""
from __future__ import annotations
import argparse
import ctypes as C
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import math
import mmap
from pathlib import Path
import statistics
import struct
import time

from check_nd_quality_live import CAMERA, Live, current_helper, screenshot
from probe_nd_quality import ROOT, load_transport, package_oans_ranges


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--duration', type=float, default=10)
    parser.add_argument('--capture', action='store_true', help='After timing, record the actual camera, ND textures and native full-scene screenshot')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    pid, path, command, parent = current_helper()
    marker = 'A380XDisplayHost'
    executable = Path(command[:command.index(marker) + len(marker)])
    aircraft = executable.parents[3] / 'A380X-XP12.acf'
    version = struct.unpack('II', path.open('rb').read(8))
    if version[0] != 0x58333830 or version[1] not in (4, 5):
        raise RuntimeError(f'Unsupported live transport {version}')
    schema = ROOT / ('build/nd-quality-20261020/before/baseline-source/xp_kit_transport.py'
                     if version[1] == 4 else 'tools/xp_kit_transport.py')
    module = load_transport(schema)
    report = dict(scope='Actual global X-Plane frame-period samples in the current stationary live session; cursor writes use aircraft diagnostic datarefs, not physical OS mouse input',
                  limitations='Unmatched camera/scenery/plugins or concurrent system load prevent a controlled performance comparison. This early sampler does not wait for ND health or move view/selectors.',
                  helper_pid=pid, simulator_pid=parent, shared_file=str(path), transport_version=version[1],
                  aircraft_file=str(aircraft), aircraft_sha256=hashlib.sha256(aircraft.read_bytes()).hexdigest(),
                  helper_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                  nd_html_sha256=hashlib.sha256((executable.parent / 'xp-kit/nd.html').read_bytes()).hexdigest(),
                  oans_zoom_ranges_nm=list(package_oans_ranges(executable.parent / 'xp-kit')),
                  measurements=[], passed=False, controls_restored=False)
    cursor_names = ['a380x/xp12/nd/captain/cursor_' + key for key in ('x', 'y')]
    period_name = 'sim/operation/misc/frame_rate_period'
    saved = []

    def save():
        (args.output / 'early-live-frame-periods.json').write_text(json.dumps(report, indent=2))

    with path.open('rb') as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as memory:
        if len(memory) != C.sizeof(module.Shared):
            raise RuntimeError('Live shared memory size differs from saved schema')
        live = Live(memory, module)
        try:
            state = live.state()
            if not state.get('ok') or state.get('mode') != 'native':
                raise RuntimeError('No current native aircraft state')
            age = live.counters()['state_age_seconds']
            if age > 1:
                raise RuntimeError(f'Native aircraft state is stale ({age:g}s)')
            values = state['values']
            if values.get('sim/flightmodel/failures/onground_any') != 1 or state['nav'].get('groundSpeed', 999) >= 1:
                raise RuntimeError('Early timing requires a stationary aircraft on the ground')
            report['initial_state'] = state
            report['initial_counters'] = live.counters()
            saved = [live.read(name) for name in cursor_names]
            for name in (period_name, *cursor_names):
                live.data_id(name)
            with ThreadPoolExecutor(max_workers=3) as pool:
                for moving in (False, True):
                    row = dict(input='diagnostic-cursor-writes' if moving else 'idle', before=live.counters(), samples=[])
                    report['measurements'].append(row)
                    start, tick = time.monotonic(), 0
                    try:
                        while time.monotonic() - start < args.duration:
                            tick_start = time.monotonic()
                            tasks = [pool.submit(live.read, period_name)]
                            if moving:
                                tasks.extend([pool.submit(live.write, cursor_names[0], 384 + 170 * math.sin(tick * .13)),
                                              pool.submit(live.write, cursor_names[1], 440 + 130 * math.cos(tick * .17))])
                            period = tasks[0].result()
                            for task in tasks[1:]:
                                task.result()
                            if not isinstance(period, (float, int)) or not 0 < period < 5:
                                raise RuntimeError(f'Invalid live frame-period sample: {period!r}')
                            row['samples'].append(dict(elapsed=time.monotonic()-start, period_seconds=period))
                            tick += 1
                            time.sleep(max(0, .05-(time.monotonic()-tick_start)))
                    finally:
                        elapsed = time.monotonic()-start
                        periods = [s['period_seconds'] for s in row['samples']]
                        row.update(elapsed_seconds=elapsed, sample_count=len(periods), samples_per_second=len(periods)/elapsed,
                                   injected_cursor_positions=tick if moving else 0, after=live.counters())
                        row['nd_frame_deltas'] = [b-a for a,b in zip(row['before']['frames'],row['after']['frames'])]
                        if periods:
                            row.update(mean_frame_period_seconds=statistics.mean(periods), median_frame_period_seconds=statistics.median(periods),
                                       p95_frame_period_seconds=sorted(periods)[min(len(periods)-1,math.ceil(.95*len(periods))-1)],
                                       fps_from_mean_sampled_frame_period=1/statistics.mean(periods))
                        save()
            if args.capture:
                report['camera_after_timing'] = {}
                for name in CAMERA:
                    try:
                        report['camera_after_timing'][name] = live.read(name)
                    except Exception as error:
                        report.setdefault('camera_errors', {})[name] = repr(error)
                report['captures'] = []
                for side in (0, 1):
                    try:
                        report['captures'].append(live.capture(side+2, args.output / f'as-found-side-{side}-live-native.png'))
                    except Exception as error:
                        report['captures'].append(dict(side=side, error=repr(error)))
                report['screenshot'] = screenshot(live, args.output, 'as-found')
            report['passed'] = True
        except BaseException as error:
            report['error'] = repr(error)
            raise
        finally:
            errors = []
            for name,value in zip(cursor_names,saved):
                try:
                    live.write(name,value)
                except Exception as error:
                    errors.append(repr(error))
            report['controls_restored'] = not errors
            report['restoration_errors'] = errors
            save()
            print(json.dumps({k:v for k,v in report.items() if k not in ('initial_state','measurements')},indent=2))


if __name__ == '__main__':
    main()
