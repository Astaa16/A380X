#!/usr/bin/env python3
"""Matched live ND channel captures and sampled frame periods on the stationary A380X.

Cursor stress uses writable aircraft diagnostic datarefs, not a physical mouse.
No UI automation, aircraft loading, electrical changes or flight-plan writes.
"""
from __future__ import annotations
import argparse
import base64
from concurrent.futures import ThreadPoolExecutor
import ctypes as C
import hashlib
import json
import math
import mmap
from pathlib import Path
import shutil
import statistics
import struct
import subprocess
import time

from probe_nd_quality import ROOT, XP, load_transport, png, package_oans_ranges, zoom_index
from xplane_api import identifier, request

AIRCRAFT = 'Aircraft/A380X-XP12/A380X-XP12.acf'
PAUSE = 'sim/time/paused'
PERIOD = 'sim/operation/misc/frame_rate_period'
CAMERA = ['sim/graphics/view/' + key for key in
          ('view_type', 'pilots_head_x', 'pilots_head_y', 'pilots_head_z',
           'pilots_head_psi', 'pilots_head_the', 'pilots_head_phi', 'field_of_view_deg',
           'window_width', 'window_height')]
READONLY_CAMERA = {'sim/graphics/view/' + key for key in ('view_type', 'window_width', 'window_height')}


def current_helper():
    """Only accept a live helper parented by the owner of the local simulator API."""
    api = subprocess.run(['lsof', '-nP', '-t', '-iTCP:8086', '-sTCP:LISTEN'], capture_output=True, text=True)
    owners = {int(x) for x in api.stdout.split()}
    pids = subprocess.run(['pgrep', '-x', 'A380XDisplayHost'], capture_output=True, text=True)
    matches = []
    for pid in pids.stdout.split():
        command = subprocess.check_output(['ps', '-p', pid, '-o', 'args='], text=True).strip()
        parent = int(subprocess.check_output(['ps', '-p', pid, '-o', 'ppid='], text=True))
        if owners and parent not in owners:
            continue
        if '/A380X-XP12/plugins/A380XAvionics/mac_x64/A380XDisplayHost ' not in command:
            continue
        words = command.split()
        paths = [Path(x) for x in words if x.startswith(('/tmp/a380x-cockpit-', '/private/tmp/a380x-cockpit-'))]
        if len(paths) == 1 and paths[0].is_file():
            matches.append((int(pid), paths[0], command, parent))
    if len(matches) != 1:
        raise RuntimeError(f'Expected one live project helper attached to API owner {owners}; found {matches}')
    return matches[0]


class Live:
    def __init__(self, memory, module):
        self.memory, self.module, self.Shared, self.ids = memory, module, module.Shared, {}

    def data_id(self, name):
        if name not in self.ids:
            self.ids[name] = identifier('datarefs', name)
        return self.ids[name]

    def read(self, name):
        return request(f'datarefs/{self.data_id(name)}/value')['data']

    def write(self, name, value):
        return request(f'datarefs/{self.data_id(name)}/value', {'data': value}, 'PATCH')

    def command(self, name):
        return request(f'command/{identifier("commands", name)}/activate', {'duration': 0}, 'POST')

    def state(self):
        for _ in range(50):
            before = struct.unpack_from('I', self.memory, self.Shared.state_seq.offset)[0]
            size = struct.unpack_from('I', self.memory, self.Shared.state_size.offset)[0]
            if not before or before & 1 or not 0 < size < self.module.CAP:
                continue
            start = self.Shared.state.offset
            data = self.memory[start:start + size]
            if before == struct.unpack_from('I', self.memory, self.Shared.state_seq.offset)[0]:
                return json.loads(data)
        raise RuntimeError('No stable live aircraft state')

    def counters(self):
        result = dict(frames=list(struct.unpack_from('7I', self.memory, self.Shared.frame_seq.offset))[2:4],
                      status=[list(struct.unpack_from('8I', self.memory, self.Shared.nd_status.offset + i * 32))
                              for i in (0, 1)],
                      errors=struct.unpack_from('I', self.memory, self.Shared.errors.offset)[0])
        result['frames'] = [x // 2 for x in result['frames']]
        now = time.clock_gettime(time.CLOCK_MONOTONIC)
        times = struct.unpack_from('7d', self.memory, self.Shared.frame_time.offset)
        result['frame_ages_seconds'] = [now - times[i] for i in (2, 3)]
        result['state_age_seconds'] = now - struct.unpack_from('d', self.memory, self.Shared.state_time.offset)[0]
        result['ready'] = list(struct.unpack_from('7I', self.memory, self.Shared.ready.offset))
        if hasattr(self.Shared, 'nd_profile'):
            Profile = self.module.NdProfile
            result['profile'] = []
            for i in (0, 1):
                p = Profile.from_buffer_copy(self.memory, self.Shared.nd_profile.offset + i * C.sizeof(Profile))
                result['profile'].append({key: int(getattr(p, key)) for key, *_ in Profile._fields_})
        return result

    def capture(self, channel, path):
        width, height = (384, 512) if self.module.VERSION == 4 else (768, 1024)
        field, index = ('pixels', channel) if self.module.VERSION == 4 else ('nd_pixels', channel - 2)
        length = width * height * 4
        offset = getattr(self.Shared, field).offset + index * length
        for _ in range(50):
            before = struct.unpack_from('I', self.memory, self.Shared.frame_seq.offset + 4 * channel)[0]
            if not before or before & 1:
                time.sleep(.005)
                continue
            pixels = self.memory[offset:offset + length]
            if before == struct.unpack_from('I', self.memory, self.Shared.frame_seq.offset + 4 * channel)[0]:
                age = self.counters()['frame_ages_seconds'][channel - 2]
                if age > 1.5:
                    raise RuntimeError(f'ND channel {channel} pixels are stale ({age:.3f}s), not a fresh live capture')
                colored = sum(max(pixels[i:i+3]) > 30 for i in range(0, len(pixels), 4))
                if colored < 2000:
                    raise RuntimeError(f'Live ND channel {channel} appears blank ({colored} foreground pixels)')
                png(path, pixels, width, height)
                return dict(path=str(path), sequence=before // 2, dimensions=[width, height], foreground_pixels=colored,
                            age_seconds=age,
                            scope='Actual live aircraft ND helper texture; excludes native cursor overlay and cockpit compositing')
        raise RuntimeError('Could not copy a stable live ND channel frame')

    def select(self, side, field, target):
        seat = 'first_officer' if side else 'captain'
        current = self.state()['nav']['controls'][side][field]
        for _ in range(abs(target - current)):
            if field == 'zoom':
                action = 'zoom_out' if target > current else 'zoom_in'
                self.command(f'a380x/xp12/nd/{seat}/{action}')
            else:
                action = field + ('_up' if target > current else '_down')
                self.command(f'a380x/xp12/efis/{seat}/{action}')
        wait_for(lambda: self.state()['nav']['controls'][side][field] == target)


def wait_for(predicate, timeout=35):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(.1)
    raise RuntimeError('Timed out waiting for live ND selectors/channel health')


def screenshot(live, output, label):
    directory = XP / 'Output/screenshots'
    before = {p.resolve() for p in directory.glob('*')} if directory.is_dir() else set()
    try:
        live.command('sim/operation/screenshot')
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            candidates = [p for p in directory.glob('*') if p.resolve() not in before and p.suffix.lower() in ('.png', '.jpg', '.jpeg')]
            if candidates:
                source = max(candidates, key=lambda p: p.stat().st_mtime)
                time.sleep(.5)
                destination = output / (label + '-cockpit' + source.suffix)
                shutil.copy2(source, destination)
                return dict(path=str(destination), simulator_source=str(source), scope='Native X-Plane full-scene screenshot command; unchanged camera')
            time.sleep(.2)
        raise RuntimeError('Native screenshot command created no file in Output/screenshots')
    except Exception as error:
        return dict(error=repr(error), scope='No screenshot invented or substituted')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase', choices=('before', 'after'), required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--duration', type=float, default=10)
    parser.add_argument('--oans-zoom-index', type=int,
                        help='Explicit override of the 1 NM index; normally read from the installed package manifest')
    parser.add_argument('--close-zoom-captures', action='store_true',
                        help='Also capture the available .05, .1 and .2 NM OANS views after paired FPS phases')
    parser.add_argument('--pause', choices=('keep', 'on', 'off'), default='keep')
    parser.add_argument('--camera', type=Path, help='Explicitly apply camera values recorded in a prior live report')
    parser.add_argument('--refresh-cockpit', action='store_true',
                        help='Re-enter the already-selected 3-D cockpit view, preserving its camera values')
    parser.add_argument('--no-screenshot', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    proof = dict(phase=args.phase, scope='Live stationary A380X ND channels and sampled simulator frame periods; diagnostic cursor injection is not physical mouse testing',
                 conditions='Other plugins/scenery/CPU/GPU workload may affect global frame-period samples; compare matching recorded camera, selectors, pause, location and hardware',
                 measurements=[], captures=[], screenshots=[], passed=False, controls_restored=False)
    pid, shared_file, command, parent = current_helper()
    proof.update(helper_pid=pid, helper_parent_pid=parent, helper_command=command, shared_file=str(shared_file))
    marker = 'A380XDisplayHost'
    executable = Path(command[:command.index(marker) + len(marker)])
    proof['helper_executable'] = str(executable)
    aircraft_path = executable.parents[3] / 'A380X-XP12.acf'
    proof['aircraft_file'] = str(aircraft_path)
    proof['aircraft_sha256'] = hashlib.sha256(aircraft_path.read_bytes()).hexdigest()
    proof['helper_sha256'] = hashlib.sha256(executable.read_bytes()).hexdigest()
    html = executable.parent / 'xp-kit/nd.html'
    ranges = package_oans_ranges(executable.parent / 'xp-kit')
    one_nm = zoom_index(ranges, 1.) if args.oans_zoom_index is None else args.oans_zoom_index
    if not 0 <= one_nm < len(ranges) or not math.isclose(ranges[one_nm], 1.):
        raise RuntimeError('Requested paired OANS index must represent exactly 1 NM')
    proof['oans_zoom_ranges_nm'] = list(ranges)
    proof['paired_oans_zoom_index'] = one_nm
    if html.is_file():
        proof['nd_html_sha256'] = hashlib.sha256(html.read_bytes()).hexdigest()
    for key, process in [('helper_architecture', pid), ('simulator_architecture', parent)]:
        arch = subprocess.run(['ps', '-p', str(process), '-o', 'arch='], capture_output=True, text=True)
        proof[key] = arch.stdout.strip() if arch.returncode == 0 else 'unavailable'
    version = struct.unpack('II', shared_file.open('rb').read(8))
    if version[0] != 0x58333830 or version[1] not in (4, 5):
        raise RuntimeError(f'Unsupported live transport {version}')
    path = ROOT / ('build/nd-quality-20261020/before/baseline-source/xp_kit_transport.py' if version[1] == 4 else 'tools/xp_kit_transport.py')
    module = load_transport(path)
    if module.VERSION != version[1]:
        raise RuntimeError('Saved transport schema does not match live helper')
    proof['transport_version'] = module.VERSION
    saved, camera_changes = {}, {}
    with shared_file.open('rb') as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as memory:
        if len(memory) != C.sizeof(module.Shared):
            raise RuntimeError('Live shared memory size does not match transport')
        live = Live(memory, module)
        try:
            aircraft = live.read('sim/aircraft/view/acf_relative_path')
            aircraft = base64.b64decode(aircraft).decode().rstrip('\0') if isinstance(aircraft, str) else bytes(aircraft).decode().rstrip('\0')
            if aircraft != AIRCRAFT:
                raise RuntimeError(f'Wrong aircraft: {aircraft}')
            if live.read('sim/flightmodel/failures/onground_any') != 1 or live.read('sim/flightmodel/position/groundspeed') >= .5:
                raise RuntimeError('Live ND check requires a stationary aircraft on the ground')
            if live.read('sim/flightmodel2/misc/has_crashed'):
                raise RuntimeError('Aircraft reports a crash; no test controls changed')
            state = live.state()
            if not state.get('ok') or state.get('mode') != 'native':
                raise RuntimeError('No valid live native aircraft input')
            values = state.get('values', {})
            bus = values.get('sim/cockpit2/electrical/bus_volts')
            if not isinstance(bus, list) or not bus or max(bus) < 18:
                raise RuntimeError('Live electrical-bus data is missing or unpowered; no test controls changed')
            if not all(c.get('powered') for c in state.get('nav', {}).get('controls', [])):
                raise RuntimeError('Both EFIS/ND channels must already be powered')
            if not all(values.get(f'a380x/xp12/du/{seat}_nd', 0) > 0 for seat in ('captain', 'first_officer')):
                raise RuntimeError('Both ND brightness channels must already be powered; electrical states will not be changed')
            saved['controls'] = [dict(c) for c in state['nav']['controls']]
            saved['pause'] = live.read(PAUSE)
            camera = {}
            for name in CAMERA:
                try:
                    camera[name] = live.read(name)
                except Exception:
                    pass
            saved['camera'] = camera
            proof['initial'] = dict(aircraft=aircraft, nav=state['nav'], camera=camera, paused=saved['pause'], electrical_bus=values.get('sim/cockpit2/electrical/bus_volts'))
            health_keys = ('captain_nd_active', 'first_officer_nd_active', 'captain_nd_bound', 'first_officer_nd_bound')
            proof['initial_health'] = {key: live.read('a380x/xp12/xpkit/' + key) for key in health_keys}
            if args.refresh_cockpit:
                if camera.get('sim/graphics/view/view_type') != 1026:
                    raise RuntimeError('Cockpit refresh requires the existing 3-D cockpit view (1026), so its view type remains restorable')
                camera_changes.update({name: value for name, value in camera.items() if name not in READONLY_CAMERA})
                live.command('sim/view/3d_cockpit_cmnd_look')
                proof['cockpit_refresh_command'] = 'sim/view/3d_cockpit_cmnd_look'
            if args.camera:
                requested = json.loads(args.camera.read_text())
                requested = requested.get('initial', requested).get('camera', requested)
                for name in READONLY_CAMERA:
                    if name in requested and requested[name] != camera.get(name):
                        raise RuntimeError(f'Current view or render dimensions differ from baseline: {name}; match them before paired capture')
                for name, value in requested.items():
                    if name not in camera or name in READONLY_CAMERA:
                        continue
                    camera_changes[name] = camera[name]
                    live.write(name, value)
            if args.pause != 'keep':
                live.command('sim/operation/pause_' + args.pause)
            proof['capture_camera'] = {name: live.read(name) for name in camera}
            proof['capture_paused'] = live.read(PAUSE)
            wait_for(lambda: all(live.read('a380x/xp12/xpkit/' + key) == 1 for key in health_keys))
            cursor_names = ['a380x/xp12/nd/captain/cursor_' + c for c in ('x', 'y')]
            saved['cursor'] = [live.read(n) for n in cursor_names]
            for name in [PERIOD, *cursor_names]:
                live.data_id(name)
            with ThreadPoolExecutor(max_workers=3) as pool:
                for label, mode, distance, zoom in [('arc40', 3, 3, one_nm), ('oans1', 4, 0, one_nm)]:
                    for side in (0, 1):
                        for field, target in [('mode', mode), ('range', distance), ('zoom', zoom)]:
                            live.select(side, field, target)
                    wait_for(lambda: all(row[:3] == [mode, distance, zoom] and (distance != 0 or row[3] > 20)
                                         for row in live.counters()['status']))
                    time.sleep(2)
                    for side, seat in enumerate(('captain', 'first-officer')):
                        proof['captures'].append(live.capture(side + 2, output / f'{label}-{seat}-live-native.png'))
                    if not args.no_screenshot:
                        proof['screenshots'].append(screenshot(live, output, label))
                    for moving in (False, True):
                        begin, samples, tick = live.counters(), [], 0
                        start = time.monotonic()
                        while time.monotonic() - start < args.duration:
                            tick_start = time.monotonic()
                            tasks = [pool.submit(live.read, PERIOD)]
                            if moving:
                                tasks.extend([pool.submit(live.write, cursor_names[0], 384 + 170 * math.sin(tick * .13)),
                                              pool.submit(live.write, cursor_names[1], 440 + 130 * math.cos(tick * .17))])
                            period = tasks[0].result()
                            for task in tasks[1:]:
                                task.result()
                            if not isinstance(period, (int, float)) or not 0 < period < 5:
                                raise RuntimeError(f'Invalid sampled frame period: {period}')
                            samples.append(dict(elapsed=time.monotonic() - start, period_seconds=period))
                            tick += 1
                            # Do not flood the simulator with catch-up writes if
                            # the API needs longer than one nominal 50ms tick.
                            time.sleep(max(0, .05 - (time.monotonic() - tick_start)))
                        elapsed, end = time.monotonic() - start, live.counters()
                        periods = [r['period_seconds'] for r in samples]
                        row = dict(scene=label, input='diagnostic-cursor-writes' if moving else 'idle', elapsed_seconds=elapsed,
                                   sample_count=len(samples), samples_per_second=len(samples) / elapsed,
                                   injected_cursor_positions=tick if moving else 0,
                                   mean_frame_period_seconds=statistics.mean(periods), median_frame_period_seconds=statistics.median(periods),
                                   p95_frame_period_seconds=sorted(periods)[min(len(periods)-1, math.ceil(.95 * len(periods))-1)],
                                   fps_from_mean_sampled_frame_period=1 / statistics.mean(periods),
                                   nd_frame_deltas=[b-a for a, b in zip(begin['frames'], end['frames'])], before=begin, after=end, samples=samples)
                        if 'profile' in begin:
                            row['profile_delta'] = [{key: end['profile'][s][key] - begin['profile'][s][key] for key in begin['profile'][s]} for s in (0, 1)]
                        proof['measurements'].append(row)
                        (output / 'live-nd-quality.json').write_text(json.dumps(proof, indent=2))
                if args.close_zoom_captures:
                    for scale in (.05, .1, .2):
                        if scale not in ranges:
                            continue
                        zoom = zoom_index(ranges, scale)
                        label = f'oans{scale:g}nm'
                        for side in (0, 1):
                            live.select(side, 'mode', 4)
                            live.select(side, 'range', 0)
                            live.select(side, 'zoom', zoom)
                        wait_for(lambda: all(row[:3] == [4, 0, zoom] and row[3] > 20 for row in live.counters()['status']))
                        time.sleep(2)
                        for side, seat in enumerate(('captain', 'first-officer')):
                            capture = live.capture(side + 2, output / f'{label}-{seat}-live-native.png')
                            capture.update(oans_scale_nm=scale, oans_zoom_index=zoom)
                            proof['captures'].append(capture)
                        if not args.no_screenshot:
                            proof['screenshots'].append(screenshot(live, output, label))
            proof['passed'] = live.counters()['errors'] == 0
            if not proof['passed']:
                raise RuntimeError('Native helper reports errors')
        except BaseException as error:
            proof['error'] = repr(error)
            raise
        finally:
            failures = []
            if saved:
                for side, controls in enumerate(saved.get('controls', [])):
                    for field in ('mode', 'range', 'zoom'):
                        try:
                            live.select(side, field, controls[field])
                        except Exception as error:
                            failures.append(f'{side}/{field}: {error!r}')
                for name, value in camera_changes.items():
                    try:
                        live.write(name, value)
                    except Exception as error:
                        failures.append(f'{name}: {error!r}')
                if 'cursor' in saved:
                    for name, value in zip(cursor_names, saved['cursor']):
                        try:
                            live.write(name, value)
                        except Exception as error:
                            failures.append(f'{name}: {error!r}')
                if 'pause' in saved:
                    try:
                        live.command('sim/operation/pause_' + ('on' if saved['pause'] else 'off'))
                    except Exception as error:
                        failures.append('pause: ' + repr(error))
            proof['controls_restored'] = bool(saved) and not failures
            proof['restoration_errors'] = failures
            proof['final_counters'] = live.counters()
            if failures:
                proof['passed'] = False
            (output / 'live-nd-quality.json').write_text(json.dumps(proof, indent=2))
            print(json.dumps({k: v for k, v in proof.items() if k not in ('measurements', 'initial')}, indent=2))


if __name__ == '__main__':
    main()
