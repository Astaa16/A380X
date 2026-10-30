"""Run the actual aircraft WebKit helper with isolated fixtures, never X-Plane.

Checks that static MFD pages are captured once and keep a liveness heartbeat,
while the PFD/ND channels continue updating. No test data enters the aircraft.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
from serve_xp_kit_trial import demo, ADIRS

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('staging', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    staging, output = args.staging.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    state = demo()
    state['mode'] = 'native'
    state['words'] = {}
    state['nav'] = {'positionValid': True, 'lat': 51.4775, 'lon': -.4614,
                    'magVar': 0, 'tas': 270, 'groundSpeed': 280, 'zulu': 43200,
                    'windDir': 270, 'windSpeed': 10,
                    'controls': [{'mode': 3, 'range': 3}, {'mode': 3, 'range': 3}], 'route': []}
    for unit in (1, 2, 3):
        state['values'][ADIRS + f'ir{unit}_latitude_deg'] = 51.4775
        state['values'][ADIRS + f'ir{unit}_longitude_deg'] = -.4614
        state['values'][ADIRS + f'adr{unit}_sat_c'] = -5
        state['values'][ADIRS + f'adr{unit}_tat_c'] = 0
    for side in ('captain', 'first_officer'):
        state['values'][f'a380x/xp12/du/{side}_nd'] = 1
    input_file = output / 'isolated-input.json'
    input_file.write_text(json.dumps(state))
    binary = output / 'probe'
    subprocess.run(['clang', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(ROOT / 'native/avionics/test_xpkit_host.c'), '-o', str(binary)], check=True)
    env = dict(os.environ)
    env['A380X_MFD_FIXTURE_LEFT'] = str(staging / 'mfd-previews/test_mfd_page_perf/perf_to_filled.mfd')
    env['A380X_MFD_FIXTURE_RIGHT'] = str(staging / 'mfd-previews/test_mfd_page_fpln/fpln_full.mfd')
    result = subprocess.run([str(binary), str(staging / 'A380XDisplayHost'), str(staging / 'xp-kit'),
        str(input_file), str(output / 'view'),
        '/Users/sava/Library/Application Support/Steam/steamapps/common/X-Plane 12/'],
        env=env, capture_output=True, text=True)
    (output / 'helper-cache-validation.json').write_text(json.dumps({
        'scope': 'Actual native helper with isolated fixture inputs; not a live cockpit/FPS test',
        'passed': result.returncode == 0, 'output': result.stdout, 'errors': result.stderr}, indent=2))
    print(result.stdout, result.stderr, sep='\n')
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
