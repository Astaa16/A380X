#!/usr/bin/env python3
"""Ground-check the installed native PFD integration; restore test controls.

The API is used only by this verification tool, not by the aircraft renderer.
Moves FCU speed one detent, blanks the captain DU, briefly suspends our helper
to exercise stale-frame fallback, then restores all of those states.
"""
import argparse
import base64
import ctypes as C
import json
import mmap
import os
from pathlib import Path
import signal
import struct
import subprocess
import time
import zlib
from xplane_api import identifier, request

from xp_kit_transport import Shared, W, H, CAP, VERSION, view_buffer


def eventually(check, timeout=10):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        result = check()
        if result:
            return result
        time.sleep(.1)
    raise AssertionError('Timed out: ' + str(check))


def png(path, rgba, width=W, height=H):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


class Check:
    def __init__(self, memory, destination):
        self.memory, self.destination = memory, destination
        self.ids = {}
        self.proof = {'format': 1, 'aircraft': 'Aircraft/A380X-XP12/A380X-XP12.acf',
                      'checks': [], 'errors': [], 'controls_restored': False}

    def read(self, name):
        if name not in self.ids:
            self.ids[name] = identifier('datarefs', name)
        return request(f'datarefs/{self.ids[name]}/value')['data']

    def activate(self, name):
        request(f'command/{identifier("commands", name)}/activate',
                {'duration': 0}, 'POST')

    def command(self, suffix):
        self.activate('a380x/xp12/fcu/' + suffix)

    def brightness(self, level):
        # The SDK Web API chooses the registered integer DU accessor (percent),
        # while the in-process adapter uses the float accessor (0..1).
        current = self.snapshot()[1]['values']['a380x/xp12/du/captain_pfd']
        steps = round((level - current) * 20)
        for _ in range(abs(steps)):
            self.activate('a380x/xp12/du/captain_pfd_' + ('up' if steps > 0 else 'down'))
        eventually(lambda: abs(self.snapshot()[1]['values']['a380x/xp12/du/captain_pfd'] - level) < .001)

    def snapshot(self):
        for _ in range(30):
            s = Shared.from_buffer_copy(self.memory)
            if not s.state_seq or s.state_seq & 1:
                continue
            if s.state_seq == struct.unpack_from('I', self.memory, Shared.state_seq.offset)[0]:
                return s, json.loads(bytes(s.state))
        raise AssertionError('No stable native state')

    def frame(self, side):
        s, _ = self.snapshot()
        for _ in range(30):
            before = struct.unpack_from('I', self.memory, Shared.frame_seq.offset + 4 * side)[0]
            if before & 1:
                continue
            start, length = view_buffer(side)
            data = bytes(self.memory[start:start + length])
            if before == struct.unpack_from('I', self.memory, Shared.frame_seq.offset + 4 * side)[0]:
                return before, data
        raise AssertionError('No stable native frame')

    def active(self):
        return all(self.read('a380x/xp12/xpkit/' + name) == 1
                   for name in ('helper_running', 'captain_active', 'first_officer_active'))

    def record(self, name, **details):
        self.proof['checks'].append({'name': name, 'passed': True, **details})
        self.save()

    def save(self):
        self.destination.mkdir(parents=True, exist_ok=True)
        (self.destination / 'native-validation.json').write_text(json.dumps(self.proof, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shared-file', required=True, type=Path)
    parser.add_argument('--helper-pid', required=True, type=int)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    command = subprocess.check_output(['ps', '-p', str(args.helper_pid), '-o', 'command='], text=True)
    assert '/Aircraft/A380X-XP12/plugins/A380XAvionics/mac_x64/A380XDisplayHost ' in command
    assert str(args.shared_file) in command
    with args.shared_file.open('rb') as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as memory:
        assert len(memory) == C.sizeof(Shared)
        assert struct.unpack_from('II', memory) == (0x58333830, VERSION)
        check = Check(memory, args.output)
        du = 'a380x/xp12/du/captain_pfd'
        paused = 'sim/time/paused'
        saved_du = saved_pause = None
        moved_speed = stopped = False
        try:
            path = base64.b64decode(check.read('sim/aircraft/view/acf_relative_path')).decode().rstrip('\0')
            assert path == check.proof['aircraft'], path
            assert check.read('sim/flightmodel/failures/onground_any') == 1
            assert check.read('sim/flightmodel/position/groundspeed') < .5
            assert check.read('sim/flightmodel2/misc/has_crashed') == 0
            eventually(check.active)
            assert check.read('a380x/xp12/xpkit/errors') == 0
            check.record('Correct stationary project aircraft; both native library channels active')
            request(f'command/{identifier("commands", "sim/view/3d_cockpit_cmnd_look")}/activate',
                    {'duration': 0}, 'POST')
            presented = [check.read('a380x/xp12/xpkit/' + side + '_presented_frames')
                         for side in ('captain', 'first_officer')]
            eventually(lambda: all(check.read('a380x/xp12/xpkit/' + side + '_presented_frames') > presented[i] + 5
                                   for i, side in enumerate(('captain', 'first_officer'))))
            check.record('X-Plane panel draw uploads both library textures in actual 3D cockpit view',
                         initial_presented_frames=presented)
            saved_du = check.snapshot()[1]['values'][du]
            saved_pause = check.read(paused)
            check.activate('sim/operation/pause_on')
            initial = [check.frame(side)[0] for side in range(2)]
            eventually(lambda: all(check.frame(side)[0] > initial[side] + 10 for side in range(2)))
            for side, name in enumerate(('captain', 'first-officer')):
                seq, data = check.frame(side)
                colored = sum(max(data[i:i + 3]) > 30 for i in range(0, len(data), 4))
                assert colored > 2000
                png(args.output / (name + '-native.png'), data)
            state, values = check.snapshot()
            assert values['mode'] == 'native' and values['ok']
            assert state.errors == 0, bytes(state.error)
            check.record('Both rendered PFDs keep updating while simulator is paused', initial_sequences=initial,
                         current_sequences=list(state.frame_seq))
            original_speed = check.read('a380x/xp12/fcu/selected_speed')
            assert check.read('a380x/xp12/fcu/mach_mode') == 0
            assert 100 <= original_speed < 398
            check.command('speed_up'); moved_speed = True
            eventually(lambda: check.read('a380x/xp12/fcu/selected_speed') == original_speed + 1)
            eventually(lambda: all(check.snapshot()[1]['words'][f'L:A32NX_PRIM_{unit}_SELECTED_AIRSPEED']
                                   == [original_speed + 1, True] for unit in (1, 2, 3)))
            check.command('speed_down'); moved_speed = False
            eventually(lambda: check.read('a380x/xp12/fcu/selected_speed') == original_speed)
            check.record('Physical FCU speed commands reach library ARINC selections; selection restored',
                         original_knots=original_speed)
            check.brightness(0)
            eventually(lambda: check.snapshot()[1]['values'][du] == 0)
            def captain_dark():
                _, pixels = check.frame(0)
                return sum(max(pixels[i:i + 3]) > 30 for i in range(0, len(pixels), 4)) < 100
            eventually(captain_dark)
            _, officer = check.frame(1)
            assert sum(max(officer[i:i + 3]) > 30 for i in range(0, len(officer), 4)) > 2000
            check.brightness(saved_du)
            eventually(lambda: not captain_dark())
            check.record('Captain DU blanks independently; FO remains lit; brightness restored')
            os.kill(args.helper_pid, signal.SIGSTOP); stopped = True
            eventually(lambda: all(check.read('a380x/xp12/xpkit/' + name) == 0
                                   for name in ('captain_active', 'first_officer_active')))
            os.kill(args.helper_pid, signal.SIGCONT); stopped = False
            eventually(check.active)
            check.record('Stale helper frames deactivate both library channels; renderer recovery is automatic')
            assert check.read('a380x/xp12/xpkit/errors') == 0
            check.proof['passed'] = True
        except Exception as error:
            check.proof['passed'] = False
            check.proof['errors'].append(repr(error))
            raise
        finally:
            if stopped:
                os.kill(args.helper_pid, signal.SIGCONT)
            if moved_speed:
                check.command('speed_down')
            restoration_errors = []
            for restore in (lambda: check.brightness(saved_du) if saved_du is not None else None,
                            lambda: check.activate('sim/operation/pause_on' if saved_pause else 'sim/operation/pause_off')
                            if saved_pause is not None else None):
                try:
                    restore()
                except Exception as error:
                    restoration_errors.append(repr(error))
            check.proof['controls_restored'] = not restoration_errors
            check.proof['errors'].extend(restoration_errors)
            if restoration_errors:
                check.proof['passed'] = False
            check.save()
            print(json.dumps(check.proof, indent=2))


if __name__ == '__main__':
    main()
