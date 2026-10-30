"""Ground-check MFD render transport and physical KCCU routing, not a flight test.

Does not enter/replace a flight plan. Restores DU brightness. Leaves captain
on F-PLN and first officer on PERF for inspection; the page changes are explicit.
"""
import argparse
import base64
import ctypes as C
import json
import mmap
from pathlib import Path
import struct
from check_xp_kit_cockpit import Check, eventually, png
from xp_kit_transport import Shared, VERSION


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shared-file', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    with args.shared_file.open('rb') as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as memory:
        assert len(memory) == C.sizeof(Shared)
        assert struct.unpack_from('II', memory) == (0x58333830, VERSION)
        check = Check(memory, args.output)
        saved = None

        def health(name):
            return check.read('a380x/xp12/xpkit/' + name)

        def mfd(side):
            for _ in range(30):
                start = Shared.mfd.offset + C.sizeof(Shared().mfd[0]) * side
                before = struct.unpack_from('I', memory, start)[0]
                if not before or before & 1:
                    continue
                frame = Shared.from_buffer_copy(memory).mfd[side]
                if before == struct.unpack_from('I', memory, start)[0]:
                    return frame
            raise AssertionError('No stable MFD page list')

        def uri(side):
            return bytes(mfd(side).uri).decode()

        def select(side, key, expected):
            check.activate(f'a380x/xp12/kccu/{"first_officer" if side else "captain"}/{key}')
            eventually(lambda: uri(side) == expected)

        def brightness(target):
            current = check.read('a380x/xp12/du/captain_mfd') / 100
            for _ in range(abs(round((target - current) * 20))):
                check.activate('a380x/xp12/du/captain_mfd_' + ('up' if target > current else 'down'))
            eventually(lambda: abs(check.read('a380x/xp12/du/captain_mfd') / 100 - target) < .001)

        def colored(side):
            sequence, pixels = check.frame(side + 4)
            return sequence, pixels, sum(max(pixels[i:i + 3]) > 30 for i in range(0, len(pixels), 4))

        try:
            path = base64.b64decode(check.read('sim/aircraft/view/acf_relative_path')).decode().rstrip('\0')
            assert path == check.proof['aircraft'], path
            assert check.read('sim/flightmodel/failures/onground_any') == 1
            assert check.read('sim/flightmodel/position/groundspeed') < .5
            assert check.read('sim/flightmodel2/misc/has_crashed') == 0
            eventually(lambda: all(health(n) == 1 for n in ('captain_active', 'first_officer_active',
                'captain_nd_active', 'first_officer_nd_active', 'captain_mfd_active', 'first_officer_mfd_active')))
            assert all(check.snapshot()[0].ready)
            check.record('Exact stationary project aircraft; all six native view channels healthy')
            check.activate('sim/view/3d_cockpit_cmnd_look')
            initial = [health(side + '_mfd_presented_frames') for side in ('captain', 'first_officer')]
            for side in range(2):
                key='init' if uri(side)=='fms/active/perf' else 'perf'
                select(side,key,'fms/active/'+key)
            eventually(lambda: all(health(side + '_mfd_presented_frames') > initial[n]
                                  for n, side in enumerate(('captain', 'first_officer'))))
            check.record('Both full-resolution MFD textures uploaded to actual cockpit-device draw callbacks')
            officer_original = uri(1)
            for key, route in (('init', 'fms/active/init'), ('perf', 'fms/active/perf'),
                    ('fpln', 'fms/active/f-pln/top'), ('navaid', 'fms/position/navaids'),
                    ('secindex', 'fms/sec/index'), ('surv', 'surv/controls'), ('atccom', 'atccom/connect')):
                select(0, key, route)
                assert uri(1) == officer_original
                assert not mfd(0).list.overflowed
                assert mfd(0).list.cmd_count > 20
            check.record('Seven physical captain KCCU page keys route independently; no page-list overflow')
            select(0, 'fpln', 'fms/active/f-pln/top')
            select(1, 'perf', 'fms/active/perf')
            for side, name in enumerate(('captain-fpln', 'first-officer-perf')):
                eventually(lambda: colored(side)[2] > 2000)
                sequence, pixels, count = colored(side)
                png(args.output / (name + '.png'), pixels, 768, 1024)
                check.record('Native full-resolution MFD pixel capture', side=side, sequence=sequence, lit_pixels=count)
            saved = check.read('a380x/xp12/du/captain_mfd') / 100
            brightness(0)
            eventually(lambda: not mfd(0).powered)
            eventually(lambda: colored(0)[2] < 100)
            assert colored(1)[2] > 2000
            brightness(saved)
            eventually(lambda: colored(0)[2] > 2000)
            check.record('Captain MFD blanks independently; FO remains lit; brightness restored')
            assert health('errors') == 0
            check.proof['passed'] = True
        except Exception as error:
            check.proof['passed'] = False
            check.proof['errors'].append(repr(error))
            raise
        finally:
            try:
                if saved is not None:
                    brightness(saved)
                check.proof['controls_restored'] = True
            except Exception as error:
                check.proof['passed'] = False
                check.proof['errors'].append('brightness restoration: ' + repr(error))
            check.proof['scope'] = 'Live ground transport/KCCU/power checks; field editing covered by native offline page tests'
            check.proof['navigation_left_for_review'] = ['fms/active/f-pln/top', 'fms/active/perf']
            check.save()
            print(json.dumps(check.proof, indent=2))


if __name__ == '__main__':
    main()
