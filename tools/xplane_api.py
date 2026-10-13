#!/usr/bin/env python3
"""Small local X-Plane Web API helper for repeatable development tests."""
import argparse
from http.client import RemoteDisconnected
import json
import sys
from urllib.parse import urlencode
from urllib.request import Request, urlopen


def request(path, data=None, method=None, timeout=10):
    body = None if data is None else json.dumps(data).encode()
    req = Request('http://127.0.0.1:8086/api/v3/' + path, data=body,
                  headers={'Content-Type': 'application/json'}, method=method)
    with urlopen(req, timeout=timeout) as response:
        content = response.read()
    return json.loads(content) if content else None


def identifier(kind, name):
    result = request(kind + '?' + urlencode({'filter[name]': name}))
    matches = [item for item in result['data'] if item['name'] == name]
    if len(matches) != 1:
        raise ValueError(f'Expected one {kind} named {name}: {matches}')
    return matches[0]['id']


def disable_random_failures():
    """Verify the native switch; MTBF=0 alone was unreliable in a test session."""
    path = f'datarefs/{identifier("datarefs", "sim/operation/failures/enable_random_failures")}/value'
    request(path, {'data': 0}, 'PATCH')
    state = request(path)
    if state.get('data') != 0:
        raise RuntimeError(f'Random failures remain enabled: {state}')
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('read', 'write', 'command', 'ground-start', 'disable-random-failures'))
    parser.add_argument('name', nargs='?')
    parser.add_argument('--value', help='JSON dataref value for write')
    args = parser.parse_args()
    if args.operation == 'read':
        result = request(f'datarefs/{identifier("datarefs", args.name)}/value')
    elif args.operation == 'write':
        result = request(f'datarefs/{identifier("datarefs", args.name)}/value',
                         {'data': json.loads(args.value)}, 'PATCH')
    elif args.operation == 'command':
        result = request(f'command/{identifier("commands", args.name)}/activate', {'duration': 0}, 'POST')
    elif args.operation == 'disable-random-failures':
        result = disable_random_failures()
    else:
        try:
            result = request('flight', {'data': {
                'aircraft': {'path': 'Aircraft/A380X-XP12/A380X-XP12.acf'},
                'runway_start': {'airport_id': 'UAAA', 'runway': '05R'},
                'engine_status': {'all_engines': {'running': False}},
                # A nonzero MTBF avoids the observed zero-MTBF/session-state discrepancy.
                # Explicitly disable and verify the actual random-failure switch below.
                'failures': {'fix_everything': True, 'mean_time_between_failures_in_hours': 5000},
                'weather': {'definition': 'vfr_few_clouds', 'vertical_speed_in_thermal_in_feet_per_minute': 0,
                            'wave_height_in_meters': 0, 'wave_direction_in_degrees': 0,
                            'terrain_state': 'dry', 'variation_across_region_percentage': 0,
                            'evolution_over_time_enum': 'static'},
                'time_enum': 'day'}}, 'POST', timeout=240)
        except RemoteDisconnected as error:
            # The sim can accept the POST and load a flight before closing its
            # response. Never repeat this non-idempotent initialization request.
            # Disabling failures is still useful, but does not confirm which
            # aircraft/flight is now loaded (it may be the previous flight).
            result = {
                'status': 'initialization_unconfirmed',
                'aircraft_confirmed': False,
                'error': 'RemoteDisconnected',
                'detail': str(error),
                'post_retried': False,
            }
            print('Flight initialization is unconfirmed: the connection closed '
                  'without a response. No POST retry; attempting to disable and '
                  'verify random failures in the current simulator session.', file=sys.stderr)
        result = {'flight': result, 'random_failures': disable_random_failures()}
    print(json.dumps(result))


if __name__ == '__main__':
    main()
