"""Observe real cockpit MFD clicks through read-only X-Plane datarefs.

This tool sends no commands, mouse events, keys or flight-plan writes. Use the
3D cockpit (close MFD popups), open header menus and select their items on both
seats. It records physical callback counts separately from page/KCCU changes;
a successful render or a KCCU shortcut alone cannot pass the touch checks.
"""
import argparse
import base64
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import time
from xplane_api import identifier, request
from urllib.parse import urlencode

SIDES = ('captain', 'first_officer')
FIELDS = ('device_bound', 'touch_down_count', 'touch_up_count', 'cursor_x', 'cursor_y',
          'menu_open', 'events_dropped', 'active_page', 'last_down_x', 'last_down_y',
          'last_up_x', 'last_up_y', 'press_target', 'click_target', 'click_count', 'menu_open_count')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, default=120)
    args = parser.parse_args()
    if not 10 <= args.seconds <= 600:
        parser.error('--seconds must be between 10 and 600')
    names = [f'a380x/xp12/mfd/{side}/{field}' for side in SIDES for field in FIELDS]
    ids = {row['name']: row['id'] for row in request('datarefs?' + urlencode(
        [('filter[name]', name) for name in names]))['data']}
    missing = set(names) - ids.keys()
    if missing:
        raise RuntimeError('Native click-tracing build is not loaded: ' + ', '.join(sorted(missing)))
    pool = ThreadPoolExecutor(max_workers=8)

    def read(name):
        if name not in ids:
            ids[name] = identifier('datarefs', name)
        return request(f'datarefs/{ids[name]}/value', timeout=3)['data']

    def text(name):
        return base64.b64decode(read(name)).decode().rstrip('\0')

    def snapshot():
        # Parallel reads avoid serial, frame-paced HTTP round trips while the
        # user is testing cursor smoothness. Persistent menu counters catch
        # even a menu that opens and closes between observations.
        values = dict(pool.map(lambda name: (name, text(name) if name.endswith('/active_page')
                                            else read(name)), names))
        return [{key: values[f'a380x/xp12/mfd/{side}/{key}'] for key in FIELDS} for side in SIDES]

    aircraft = text('sim/aircraft/view/acf_relative_path')
    assert aircraft == 'Aircraft/A380X-XP12/A380X-XP12.acf', aircraft
    assert read('sim/flightmodel/failures/onground_any') == 1
    assert read('sim/flightmodel/position/groundspeed') < .5
    previous = snapshot()
    assert all(side['device_bound'] for side in previous), 'Both MFD devices must be bound'
    proof = {'scope': 'Read-only observation of real XPLM touchscreen callbacks and native page state; '
                       'keep popups closed to test the 3D cockpit, not popup windows',
             'aircraft': aircraft, 'initial': previous, 'events': [], 'passed': False}
    menus, pages = [0, 0], [set(), set()]
    last_click = [-1e9, -1e9]
    origin = time.monotonic()
    print('Observing actual MFD input. In the 3D cockpit, open and select header menus on BOTH seats.', flush=True)
    try:
        while time.monotonic() - origin < args.seconds:
            current = snapshot()
            for side, (before, after) in enumerate(zip(previous, current)):
                assert after['device_bound'], f'{SIDES[side]} MFD lost its device binding'
                assert after['events_dropped'] == 0, f'{SIDES[side]} MFD input queue overflowed'
                if after['touch_up_count'] > before['touch_up_count']:
                    last_click[side] = time.monotonic()
                clicked = time.monotonic() - last_click[side] < 1.0
                if clicked:
                    menus[side] += int(after['menu_open_count'] - before['menu_open_count'])
                if clicked and before['active_page'] != after['active_page']:
                    pages[side].add(after['active_page'])
                if after != before:
                    event = {'seconds': round(time.monotonic() - origin, 2), 'side': SIDES[side], **after}
                    proof['events'].append(event)
                    if after['menu_open'] != before['menu_open'] or after['active_page'] != before['active_page']:
                        print(json.dumps(event), flush=True)
            previous = current
            if all(menus[s] >= 2 and len(pages[s]) >= 2 for s in range(2)):
                proof['passed'] = True
                break
            time.sleep(.15)
        proof['final'] = previous
        proof['menus_opened'] = dict(zip(SIDES, menus))
        proof['pages_selected'] = {side: sorted(pages[n]) for n, side in enumerate(SIDES)}
        if not proof['passed']:
            proof['limitation'] = 'Did not observe two touchscreen menu openings and two page changes on each seat'
    finally:
        pool.shutdown()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps({key: value for key, value in proof.items() if key != 'events'}, indent=2))
    raise SystemExit(0 if proof['passed'] else 1)


if __name__ == '__main__':
    main()
