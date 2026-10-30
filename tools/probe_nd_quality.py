#!/usr/bin/env python3
"""Measure and capture an aircraft-local ND helper with isolated, fixed inputs.

This never starts or controls X-Plane and never writes installed aircraft files.
The helper's direct CPU time excludes WebKit's XPC WebContent/GPU processes.
Snapshot counts and shared-memory bytes are transport measurements, not FPS or
GPU timings. Raw images are real helper pixels; comparison images only model
their 768x1024 display with bilinear sampling. A native cockpit cursor overlay is
not contained in a helper snapshot.
"""
from __future__ import annotations

import argparse
import copy
import ctypes as C
import hashlib
import importlib.util
import json
import math
import mmap
import os
from pathlib import Path
import signal
import struct
import subprocess
import tempfile
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
XP = Path('/Users/sava/Library/Application Support/Steam/steamapps/common/X-Plane 12')
ADIRS = 'a380x/xp12/adirs/'
BASELINE_OANS_RANGES = (.2, .5, 1., 2., 5.)
CLOSE_OANS_RANGES = (.05, .1, .2, .5, 1., 2., 5.)
SCENES = [('arc-10nm', 3, 1, 1.), ('arc-40nm', 3, 3, 1.),
          ('plan-40nm', 4, 3, 1.), ('oans-0.05nm', 4, 0, .05),
          ('oans-0.1nm', 4, 0, .1), ('oans-0.2nm', 4, 0, .2),
          ('oans-1nm', 4, 0, 1.), ('oans-5nm', 4, 0, 5.)]


def oans_ranges(value):
    """Explicit semantic scales keep before/after captures at the same distance."""
    try:
        values = tuple(float(part) for part in value.split(','))
    except ValueError as error:
        raise argparse.ArgumentTypeError('OANS ranges must be comma-separated numbers') from error
    if not values or any(not math.isfinite(x) or x <= 0 for x in values) or any(a >= b for a, b in zip(values, values[1:])):
        raise argparse.ArgumentTypeError('OANS ranges must be positive, finite and strictly increasing')
    return values


def zoom_index(ranges, distance):
    for index, value in enumerate(ranges):
        if math.isclose(value, distance, rel_tol=1e-9, abs_tol=1e-12):
            return index
    raise ValueError(f'OANS scale {distance:g} NM is unsupported by {list(ranges)}')


def package_oans_ranges(directory):
    for filename in ('manifest.json', 'nd-source-manifest.json'):
        path = Path(directory) / filename
        if not path.is_file():
            continue
        manifest = json.loads(path.read_text())
        for key in ('oans_zoom_levels_nm', 'nd_oans_ranges_nm'):
            if key in manifest:
                return oans_ranges(','.join(str(x) for x in manifest[key]))
    return BASELINE_OANS_RANGES


def load_transport(path):
    spec = importlib.util.spec_from_file_location('nd_quality_transport', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fixture():
    # Stored unchanged in each report so before/after runs are reproducible.
    from serve_xp_kit_trial import demo
    state = demo()
    state.update(mode='native', words={})
    state['values']['sim/flightmodel/failures/onground_any'] = 1
    lat, lon = 43.348289, 77.021213
    for n in (1, 2, 3):
        for field, value in [('latitude_deg', lat), ('longitude_deg', lon),
                             ('heading_deg', 55), ('track_deg', 55),
                             ('ground_speed_knots', 8)]:
            state['values'][ADIRS + f'ir{n}_{field}'] = value
        state['values'][ADIRS + f'adr{n}_sat_c'] = 12
        state['values'][ADIRS + f'adr{n}_tat_c'] = 12
    for side in ('captain', 'first_officer'):
        state['values'][f'a380x/xp12/du/{side}_nd'] = 1
    route = []
    previous = [lat, lon]
    for i, (ident, a, b) in enumerate([('KUGOS', 43.45, 77.15),
                                      ('ALMAT', 43.60, 77.36),
                                      ('RENAT', 43.85, 77.65)]):
        route.append(dict(ident=ident, lat=a, lon=b, active=i == 0, start=previous))
        previous = [a, b]
    state['nav'] = dict(lat=lat, lon=lon, positionValid=True, magVar=0,
                        tas=8, groundSpeed=8, windDir=285, windSpeed=10, zulu=32000,
                        selectedHeading=55, selectedAltitude=12000,
                        controls=[dict(mode=3, range=3, zoom=2, option=2, powered=1)
                                  for _ in (0, 1)],
                        airports=[dict(idarpt='UAAA', iata='ALA', name='Almaty Intl',
                                       coordinates=dict(lat=43.355555556, lon=77.043888889), elev=679.4)],
                        airport='UAAA', route=route, to=route[0], radios=[],
                        symbols=[dict(databaseId='fixture-vor', ident='AAA',
                                      location=dict(lat=43.43, long=76.96), type=2 | 256)],
                        pointers=[dict(x=384, y=440, inside=0, button=0, events=[])
                                  for _ in (0, 1)])
    return state


def png(path, rgba, width, height):
    def chunk(kind, data):
        return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', width, height, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def view(shared, channel):
    """Handle frozen v4 and the new separated ND storage without hardcoding offsets."""
    if channel in (2, 3) and hasattr(shared, 'nd_pixels'):
        return shared.nd_pixels[channel - 2], 768, 1024
    if channel < 4:
        data = shared.pixels[channel]
        width = 768 if len(data) == 768 * 1024 * 4 else 384
        return data, width, width * 4 // 3
    return shared.mfd_pixels[channel - 4], 768, 1024


def stable_frame(shared, channel):
    for _ in range(50):
        before = shared.frame_seq[channel]
        if not before or before & 1:
            time.sleep(.005)
            continue
        data, width, height = view(shared, channel)
        pixels = bytes(data)
        if shared.frame_seq[channel] == before:
            return before // 2, pixels, width, height
    raise RuntimeError('Could not read an unchanged ND frame sequence')


def cpu_seconds(pid):
    result = subprocess.run(['ps', '-p', str(pid), '-o', 'time='], capture_output=True, text=True)
    if result.returncode:
        return None
    fields = result.stdout.strip().split(':')
    return sum(float(part) * 60 ** i for i, part in enumerate(reversed(fields)))


def snapshot_stats(shared):
    stats = dict(frames=[int(shared.frame_seq[i]) // 2 for i in (2, 3)],
                 nd_status=[list(shared.nd_status[i]) for i in (0, 1)],
                 errors=int(shared.errors))
    if hasattr(shared, 'nd_profile'):
        stats['profile'] = [{name: int(getattr(shared.nd_profile[i], name))
                             for name, *_ in shared.nd_profile[i]._fields_} for i in (0, 1)]
    return stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('staging', type=Path, help='Directory containing A380XDisplayHost and xp-kit/')
    parser.add_argument('--transport', type=Path, default=ROOT / 'tools/xp_kit_transport.py')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', type=Path)
    parser.add_argument('--measure-seconds', type=float, default=20)
    parser.add_argument('--scenes-only', action='store_true')
    parser.add_argument('--extended', action='store_true', help='Also measure moving navigation and OANS idle/cursor')
    parser.add_argument('--scenes', default=','.join(x[0] for x in SCENES))
    parser.add_argument('--oans-zoom-ranges', type=oans_ranges,
                        help='Actual OANS range list in NM; default reads package manifest, or uses baseline .2,.5,1,2,5')
    args = parser.parse_args()
    staging, output = args.staging.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    transport = load_transport(args.transport)
    ranges = args.oans_zoom_ranges or package_oans_ranges(staging / 'xp-kit')
    one_nm = zoom_index(ranges, 1.)
    Shared = transport.Shared
    state = json.loads(args.fixture.read_text()) if args.fixture else fixture()
    for control in state['nav']['controls']:
        control['zoom'] = one_nm
    original_state = copy.deepcopy(state)
    (output / 'fixture.json').write_text(json.dumps(original_state, indent=2))
    report = dict(scope='Actual aircraft-local WebKit helper with isolated fixed inputs; not X-Plane screenshots or cockpit FPS',
                  exclusions=['No simulator launched', 'No cockpit GL uploads or native cursor overlay exercised',
                              'Direct helper process CPU excludes WebKit XPC WebContent/GPU processes'],
                  staging=str(staging), transport=str(args.transport.resolve()),
                  transport_version=transport.VERSION, shared_memory_bytes=C.sizeof(Shared),
                  helper_sha256=hashlib.sha256((staging / 'A380XDisplayHost').read_bytes()).hexdigest(),
                  nd_html_sha256=hashlib.sha256((staging / 'xp-kit/nd.html').read_bytes()).hexdigest(),
                  input_rate_hz=20, oans_zoom_ranges_nm=list(ranges), measurements=[], captures=[], skipped_scenes=[], passed=False)
    descriptor, name = tempfile.mkstemp(prefix='a380x-nd-quality-', dir='/tmp')
    os.ftruncate(descriptor, C.sizeof(Shared))
    memory = mmap.mmap(descriptor, C.sizeof(Shared))
    os.close(descriptor)
    shared = Shared.from_buffer(memory)
    shared.magic, shared.version = 0x58333830, transport.VERSION
    sequence = 0
    event_sequence = 0

    def send():
        nonlocal sequence
        payload = json.dumps(state, separators=(',', ':'), allow_nan=False).encode()
        if len(payload) >= transport.CAP:
            raise ValueError('Fixture exceeds shared state capacity')
        sequence += 2
        shared.state_seq = sequence - 1
        shared.state = payload
        shared.state_size = len(payload)
        # Python's monotonic() is CLOCK_UPTIME_RAW on macOS. The C transport
        # uses CLOCK_MONOTONIC, which has a different origin after system sleep.
        shared.state_time = time.clock_gettime(time.CLOCK_MONOTONIC)
        shared.state_seq = sequence

    # Other channels remain representative static native pages, identical before/after.
    previews = ROOT / 'build/mfd-screen-entry-20261019/mfd-previews'
    for i, path in enumerate([previews / 'test_mfd_page_perf/perf_to_filled.mfd',
                              previews / 'test_mfd_page_fpln/fpln_full.mfd']):
        if path.is_file():
            data = path.read_bytes()
            if len(data) != C.sizeof(shared.mfd[i]):
                raise ValueError(f'MFD fixture schema changed: {path}')
            C.memmove(C.addressof(shared.mfd[i]), data, len(data))
    sd = b'{"powered":true,"page":6,"more":false,"vars":{}}'
    shared.sd.json, shared.sd.size, shared.sd.sequence = sd, len(sd), 2
    send()
    stderr_file = (output / 'helper-stderr.log').open('w')
    stdout_file = (output / 'helper-stdout.log').open('w')
    proc = subprocess.Popen([str(staging / 'A380XDisplayHost'), name,
                             str(staging / 'xp-kit'), str(XP) + '/'],
                            stdout=stdout_file, stderr=stderr_file)

    def ensure_alive():
        if proc.poll() is not None:
            raise RuntimeError(f'Helper exited with {proc.returncode}; see helper-stderr.log')

    def pump(seconds, moving=False, clock_running=True, nav_motion=False):
        nonlocal event_sequence
        start = time.monotonic()
        tick = 0
        while time.monotonic() - start < seconds:
            ensure_alive()
            if clock_running:
                state['values']['sim/time/total_flight_time_sec'] += .05
                state['nav']['zulu'] += .05
            if nav_motion:
                elapsed = tick / 20
                heading = 55 + 12 * math.sin(elapsed / 4)
                state['nav']['lat'] = original_state['nav']['lat'] + .002 * math.sin(elapsed / 3)
                state['nav']['lon'] = original_state['nav']['lon'] + .003 * math.sin(elapsed / 3)
                for n in (1, 2, 3):
                    for field, value in [('heading_deg', heading), ('track_deg', heading),
                                         ('latitude_deg', state['nav']['lat']), ('longitude_deg', state['nav']['lon'])]:
                        state['values'][ADIRS + f'ir{n}_{field}'] = value
            if moving:
                event_sequence += 1
                # The same motion is sent at 20 Hz rather than flooding JavaScript.
                for side, pointer in enumerate(state['nav']['pointers']):
                    x = 384 + 180 * math.sin(tick * .13 + side)
                    y = 440 + 130 * math.cos(tick * .17 + side)
                    pointer.update(x=x, y=y, inside=1, button=0,
                                   events=[[event_sequence, 0, x, y, 0]])
            send()
            tick += 1
            time.sleep(max(0, start + tick / 20 - time.monotonic()))
        return tick

    def wait_for(predicate, timeout=35):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if predicate():
                return
            pump(.1)
        raise RuntimeError(f'ND state did not settle; {snapshot_stats(shared)}')

    def measure(label, moving, nav_motion=False):
        saved_state = copy.deepcopy(state)
        state['values']['sim/time/paused'] = int(not nav_motion)
        for pointer in state['nav']['pointers']:
            pointer.update(inside=int(moving), events=[])
        pump(1, moving=moving, clock_running=nav_motion, nav_motion=nav_motion)
        before_frames = [stable_frame(shared, c) for c in (2, 3)]
        before, cpu_before, start = snapshot_stats(shared), cpu_seconds(proc.pid), time.monotonic()
        ticks = pump(args.measure_seconds, moving=moving, clock_running=nav_motion, nav_motion=nav_motion)
        elapsed, cpu_after, after = time.monotonic() - start, cpu_seconds(proc.pid), snapshot_stats(shared)
        delta = [b - a for a, b in zip(before['frames'], after['frames'])]
        bytes_per_frame = [len(view(shared, c)[0]) for c in (2, 3)]
        row = dict(name=label, elapsed_seconds=elapsed, input_updates=ticks,
                   simulation_clock_frozen=not nav_motion, navigation_motion=nav_motion,
                   cursor_samples_per_side=ticks if moving else 0, nd_frame_deltas=delta,
                   nd_snapshots_per_second=[x / elapsed for x in delta],
                   nd_shared_pixel_bytes=sum(n * b for n, b in zip(delta, bytes_per_frame)),
                   before=before, after=after)
        after_frames = [stable_frame(shared, c) for c in (2, 3)]
        row['retained_pixels_unchanged'] = [a[1] == b[1] for a, b in zip(before_frames, after_frames)]
        row['pixels_sha256_before'] = [hashlib.sha256(f[1]).hexdigest() for f in before_frames]
        row['pixels_sha256_after'] = [hashlib.sha256(f[1]).hexdigest() for f in after_frames]
        for side, (_, pixels, width, height) in enumerate(after_frames):
            png(output / f'{label}-side-{side}-native-{width}x{height}.png', pixels, width, height)
        if cpu_before is not None and cpu_after is not None:
            row.update(helper_direct_cpu_seconds=cpu_after - cpu_before,
                       helper_direct_cpu_percent_one_core=(cpu_after - cpu_before) / elapsed * 100)
        if 'profile' in before:
            row['profile_delta'] = [{k: after['profile'][s][k] - before['profile'][s][k]
                                     for k in before['profile'][s]} for s in (0, 1)]
        report['measurements'].append(row)
        state.clear()
        state.update(saved_state)
        (output / 'native-helper-report.json').write_text(json.dumps(report, indent=2))
        print(json.dumps(row), flush=True)

    try:
        wait_for(lambda: all(shared.ready[i] and shared.frame_seq[i] for i in range(4)))
        pump(3)
        wait_for(lambda: all(list(shared.nd_status[s])[:3] ==
                             [state['nav']['controls'][s][key] for key in ('mode', 'range', 'zoom')]
                             for s in (0, 1)))
        if not args.scenes_only:
            measure('arc40-idle', False)
            measure('arc40-cursor20hz', True)
            if args.extended:
                measure('arc40-navigation20hz', False, nav_motion=True)
                for control in state['nav']['controls']:
                    control.update(mode=4, range=0, zoom=one_nm)
                pump(3)
                wait_for(lambda: all(list(shared.nd_status[s])[:3] == [4, 0, one_nm]
                                     and shared.nd_status[s][3] > 20 for s in (0, 1)))
                measure('oans1-idle', False)
                measure('oans1-cursor20hz', True)
        requested = set(args.scenes.split(','))
        for label, mode, distance, scale_nm in SCENES:
            if label not in requested:
                continue
            try:
                zoom = zoom_index(ranges, scale_nm)
            except ValueError:
                report['skipped_scenes'].append(dict(scene=label, reason='OANS scale unsupported by this installed range list'))
                continue
            for control in state['nav']['controls']:
                control.update(mode=mode, range=distance, zoom=zoom)
            for pointer in state['nav']['pointers']:
                pointer.update(inside=0, button=0, events=[])
            send()
            pump(2.5)
            wait_for(lambda: all(list(shared.nd_status[s])[:3] == [mode, distance, zoom]
                                 for s in (0, 1)))
            if distance == 0:
                wait_for(lambda: all(shared.nd_status[s][3] > 20 for s in (0, 1)))
            pump(1.5)
            seq, pixels, width, height = stable_frame(shared, 2)
            raw = output / f'{label}-native-{width}x{height}.png'
            png(raw, pixels, width, height)
            comparison = output / f'{label}-display-768x1024.png'
            if (width, height) == (768, 1024):
                comparison.write_bytes(raw.read_bytes())
            else:
                from PIL import Image
                image = Image.frombytes('RGBA', (width, height), pixels)
                image.resize((768, 1024), Image.Resampling.BILINEAR).save(comparison)
            capture = dict(scene=label, frame=seq, native_dimensions=[width, height],
                           oans_scale_nm=scale_nm if distance == 0 else None, oans_zoom_index=zoom,
                           raw=str(raw), comparison=str(comparison),
                           comparison_processing='No resize' if width == 768 else 'Bilinear 2x scale models former GL_LINEAR presentation; not a new simulator capture',
                           nd_status=[list(shared.nd_status[i]) for i in (0, 1)],
                           native_nonblack_pixels=sum(any(pixels[i:i+3]) for i in range(0, len(pixels), 4)))
            if capture['native_nonblack_pixels'] < 2000:
                raise RuntimeError(f'ND capture is unexpectedly blank: {capture}')
            report['captures'].append(capture)
            print(json.dumps(capture), flush=True)
        if shared.errors:
            raise RuntimeError(f'Helper errors: {shared.errors}, {bytes(shared.error)!r}')
        report['passed'] = True
    except BaseException as error:
        report['error'] = repr(error)
        raise
    finally:
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            proc.wait(timeout=15)
        report['final'] = snapshot_stats(shared)
        report['helper_exit_status'] = proc.returncode
        (output / 'native-helper-report.json').write_text(json.dumps(report, indent=2))
        stdout_file.close()
        stderr_file.close()
        del shared
        memory.close()
        Path(name).unlink(missing_ok=True)


if __name__ == '__main__':
    main()
