#!/usr/bin/env python3
"""Offline, backed-up A388 physics adaptation; never import donor avionics/plugins.

This is a development preprocessor for the supplied XP12 ACF, not an aircraft
runtime updater or a general ACF schema. See docs/A388_FLIGHTMODEL_ADAPTATION.md.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import sys
import tempfile
from ground_gear_diagnostics import extended_gear_contact as extended_contact

ROOT = Path(__file__).resolve().parents[1]
FT_M = 0.3048
GROUPS = {'_wing', '_body', '_engn', '_gear', '_sbrk'}
# Intentionally excludes electrical/hydraulic equipment, instrument ranges,
# viewpoints, native AP/artificial-stability settings and every plugin route.
FAMILIES = re.compile(
    r'^_(?:[VGJ][A-Za-z0-9_]*|Mmo|cg[A-Za-z_]*|cg_in_mac|'
    r'ailn[12]_.*|elev[12]_.*|rudd[12]_.*|sbrk[12]_.*|spoi[12]_.*|yawbr_.*|'
    r'ailn_(?:def_time|tab|trim_time)|elev_(?:def_time|tab|trim_time|with_flap_rat)|'
    r'rudd_(?:def_time|tab|trim_time|with_ailn_rat|with_fail)|'
    r'flap[12]?_.*|flaps_.*|slat[12]?_.*|stab_(?:trim_.*|with_flap)|'
    r'ptch_acft_(?:up_trim_rat|dn_trim_rat|trim_mode)|'
    r'using_new_v12_flying_stab_trim|trim_elev_elec_EQ|'
    r'speedbrake_(?:ext_time|ret_time|size)|sbrk_(?:EQ|on_td_EQ)|'
    r'spoi_.*|wing_(?:damp_rat|frac_fuel|frac_mass|mid_dihed_per_g)|'
    r'jet_.*|SFC_.*|n[12]_.*|idle_rat|throt_.*|throttle_drives_.*|thrust_.*|'
    r'bypass_ratio|crit_alt_mtr|engine_friction_ratio|engine_pwr_rat_from_crit_alt|'
    r'engine_rev_time_sec|face_jet|fan_rpm_for_n1_100|ff_rat_idle_JET|'
    r'inlet_.*|spool_time_compressor_jet|lock_n[12]|min_n[12].*|'
    r'fuel_intro_time_jet|reverser_area|rev_thrust_EQ|revt_on_td_EQ|'
    r'starter_(?:max_rpm_rat|torque_rat)|'
    r'm_(?:empty|max|fuel_max_tot)|tank_(?:name|rat|rat_def|xyz|xyz_full)|'
    r'brake_co|roll_co|truck_(?:lat_sep|lon_sep)|ster_hi_speed_use|'
    r'use_cus_gear_(?:defs|damping)|h_eqlbm|the_eqlbm|'
    r'stall_warn_aoa|average_mac_acf|num_engn|num_prop)$'
)
GEARS = ('gear-nose', 'gear-wing-lh', 'gear-wing-rh', 'gear-body-lh', 'gear-body-rh')


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def properties(data: bytes) -> dict[str, str]:
    text = data.decode('latin1')
    if text.splitlines()[:3] != ['I', '1200 Version', 'ACF']:
        raise ValueError('Expected the supplied text-format XP12 ACF')
    rows = re.findall(r'^P ([^ ]+) (.*)$', text, re.M)
    result = dict(rows)
    if len(rows) != len(result):
        raise ValueError('Duplicate ACF property keys')
    if text.count('PROPERTIES_BEGIN\n') != 1 or text.count('PROPERTIES_END\n') != 1:
        raise ValueError('Ambiguous ACF property section')
    return result


def selected(key: str) -> bool:
    root, _, leaf = key.partition('/')
    return root in GROUPS or root == 'acf' and bool(FAMILIES.fullmatch(leaf.split('/')[0]))


def merge(base: bytes, donor: bytes, dy: float) -> tuple[bytes, dict]:
    a, b = properties(base), properties(donor)
    for p in (a, b):
        if p.get('acf/_num_engn') != '4':
            raise ValueError('Both aircraft must retain four engines')
        if [int(p[f'_gear/{i}/_gear_type']) for i in range(5)] != [3, 5, 5, 6, 6]:
            raise ValueError('Gear order must be nose, L/R wing, L/R body')
    if not math.isfinite(dy) or abs(dy) > 15:
        raise ValueError('Invalid vertical datum translation')
    for key, value in b.items():
        if re.fullmatch(r'_sbrk/\d+/_type', key) and value != '0':
            raise ValueError('This supplied donor must use wing-integrated speedbrakes')

    # Keep only enabled wing/body slots. Disabled non-zero wing records can
    # enter the XP12 finite-wing solver with uninitialized airfoil pointers.
    active = {group: {key.split('/')[1] for key, value in b.items()
                      if key.startswith(group + '/') and key.endswith('/_part_specs_eq')
                      and value == '1'} for group in ('_wing', '_body')}
    replacement = {}
    for key, value in b.items():
        if not selected(key):
            continue
        fields = key.split('/')
        if fields[0] in active and len(fields) > 2 and fields[1] not in active[fields[0]]:
            continue
        if key.endswith('/_part_y') or re.fullmatch(r'_gear/\d+/_gear_y', key):
            value = f'{float(value) + dy:.9f}'
        elif re.fullmatch(r'_wing/\d+/_geo_xyz/\d+,\d+,1', key):
            # Wing caches are already in aircraft coordinates; body geometry
            # is LOCAL to its _part_xyz and must not be translated twice.
            value = f'{float(value) + dy:.9f}'
        elif key == 'acf/_cgY' or re.fullmatch(r'acf/_tank_xyz(?:_full)?/\d+,1', key):
            value = f'{float(value) + dy:.9f}'
        elif key.endswith('/_part_specs_invis'):
            value = '1'
        replacement[key] = value
    for key, value in a.items():
        if re.fullmatch(r'_engn/\d+/_engn_health_var', key):
            replacement.setdefault(key, value)  # modern XP12 field absent from donor

    # The donor's storage tanks are plugin-managed TRIM tanks. Import their
    # capacity/arms, but use native NORMAL roles so no commercial transfer
    # plugin is needed to make that fuel available. This is NOT A380 FQMS.
    count = int(b['acf/_tank_rat/count'])
    if count != 9:
        raise ValueError('Expected nine tanks for the existing native loading UI')
    ratios = [float(b[f'acf/_tank_rat/{i}']) for i in range(count)]
    if not all(math.isfinite(r) and r > 0 for r in ratios) or abs(sum(ratios) - 1) > .01:
        raise ValueError('Invalid donor fuel capacity ratios')
    ratios = [round(r / sum(ratios), 9) for r in ratios]
    ratios[-1] = round(1 - sum(ratios[:-1]), 9)
    for i, ratio in enumerate(ratios):
        replacement[f'acf/_tank_rat/{i}'] = f'{ratio:.9f}'
        replacement[f'acf/_tank_role/{i}'] = '0'
        replacement[f'acf/_tank_part_en/{i}'] = '-1'
    replacement['acf/_tank_role/count'] = replacement['acf/_tank_part_en/count'] = str(count)

    # Avoid importing the donor's incompatible native-panel or plugin owners.
    text = base.decode('latin1')
    prefix, tail = text.split('PROPERTIES_BEGIN\n', 1)
    body, suffix = tail.split('PROPERTIES_END\n', 1)
    kept = []
    for line in body.splitlines(keepends=True):
        match = re.match(r'P ([^ ]+) ', line)
        if match and (selected(match[1]) or match[1] in replacement):
            continue
        kept.append(line)
    result = (prefix + 'PROPERTIES_BEGIN\n' + ''.join(kept) +
              ''.join(f'P {k} {v}\n' for k, v in sorted(replacement.items())) +
              'PROPERTIES_END\n' + suffix).encode('latin1')
    c = properties(result)
    for key, value in a.items():
        if not selected(key) and key not in replacement and c.get(key) != value:
            raise AssertionError(f'Protected integration property changed: {key}')
    if result.split(b'PROPERTIES_END\n', 1)[1] != base.split(b'PROPERTIES_END\n', 1)[1]:
        raise AssertionError('Panel sections changed')
    return result, {'vertical_datum_shift_ft': dy, 'active_wing_slots': sorted(map(int, active['_wing'])),
                    'active_body_slots': sorted(map(int, active['_body'])),
                    'imported_property_count': len(replacement), 'fuel_ratios_original_sum': sum(float(b[f'acf/_tank_rat/{i}']) for i in range(9)),
                    'fuel_roles': 'native normal (no donor transfer plugin)',
                    'fuel_ratios_normalized': ratios}


def align_tires(data: bytes, aircraft: Path) -> tuple[bytes, list[dict]]:
    """Move moving-gear attachment Y only; preserve cockpit and fixed nose bay."""
    sys.path.insert(0, str(ROOT / 'blender'))
    from obj8_preview import parse_obj8
    p = properties(data)
    updates, report = {}, []
    for i, group in enumerate(GEARS):
        suffix = f'exterior/{group}/{group.replace("-", "_")}_a380_exterior_tyres.obj'
        attachment = [k.removesuffix('/_v10_att_file_stl') for k, v in p.items()
                      if k.endswith('/_v10_att_file_stl') and v == suffix]
        if len(attachment) != 1:
            raise ValueError(f'Expected one existing tire attachment: {suffix}')
        attachment = attachment[0]
        for name in ('phi', 'the'):
            if abs(float(p[f'{attachment}/_v10_att_{name}_ref'])) > 1e-6:
                raise ValueError('Tire tilt requires a full attachment transform')
        mesh = parse_obj8((aircraft / 'objects' / suffix).read_text().splitlines(),
                         values={f'sim/flightmodel2/gear/deploy_ratio[{i}]': 1.0})
        vertices, *_ = mesh.bake()
        visual_y = min(v[2] for v in vertices) / FT_M + float(p[f'{attachment}/_v10_att_y_acf_prt_ref'])
        contact_y = extended_contact(p, i)[1]
        change = contact_y - visual_y
        if abs(change) > 4:
            raise ValueError(f'Unreasonable moving-gear placement change: {group}, {change} ft')
        moved = []
        for key, value in p.items():
            if key.endswith('/_v10_att_file_stl') and value.startswith(f'exterior/{group}/') and not value.endswith('_lg_bay.obj'):
                ykey = key.removesuffix('/_v10_att_file_stl') + '/_v10_att_y_acf_prt_ref'
                updates[ykey] = f'{float(p[ykey]) + change:.9f}'
                moved.append(value)
        report.append({'gear': group, 'neutral_physics_contact_ft': list(extended_contact(p, i)),
                       'visual_bottom_before_ft': visual_y, 'attachment_shift_y_ft': change,
                       'visual_bottom_after_ft': visual_y + change, 'moved_attachments': moved,
                       'note': 'Uncompressed static envelope only; loaded suspension and retraction require X-Plane testing.'})
    text = data.decode('latin1')
    for key, value in updates.items():
        text, count = re.subn(rf'^P {re.escape(key)} .*$', f'P {key} {value}', text, flags=re.M)
        assert count == 1
    return text.encode('latin1'), report


def protected_files(aircraft: Path) -> dict[str, str]:
    result = {}
    for directory in ('plugins', 'objects', 'textures', 'avionics-providers', 'cockpit_3d'):
        for p in (aircraft / directory).rglob('*'):
            if p.is_file() and not p.name.startswith('._'):
                result[p.relative_to(aircraft).as_posix()] = sha(p)
    return result


def adapt(donor_path: Path, aircraft: Path, output: Path, apply: bool = False) -> dict:
    donor_path, aircraft, output = donor_path.resolve(), aircraft.resolve(), output.resolve()
    current = aircraft / 'A380X-XP12.acf'
    if donor_path == current or output.exists():
        raise ValueError('Use a separate donor and a fresh report directory')
    base, donor = current.read_bytes(), donor_path.read_bytes()
    a, b = properties(base), properties(donor)
    for p in (a, b):
        if abs(float(p['_body/0/_part_z'])) > 1e-6 or abs(float(p['_body/0/_part_x'])) > 1e-6:
            raise ValueError('This adaptation expects both fuselages referenced at the nose')
    dy = sum(extended_contact(a, i)[1] - extended_contact(b, i)[1] for i in (3, 4)) / 2
    result, report = merge(base, donor, dy)
    result, report['gear_visual_alignment'] = align_tires(result, aircraft)
    c = properties(result)
    names = sorted({v for k, v in c.items() if '_afl_file_' in k and k.startswith('_wing/')})
    airfoils = {}
    for name in names:
        if Path(name).name != name or '\\' in name:
            raise ValueError('Unexpected donor airfoil path')
        source = donor_path.parent / 'Airfoils' / name
        if not source.is_file():
            raise ValueError(f'Missing essential donor airfoil: {source}')
        if source.read_bytes().splitlines()[:1] != [b'I']:
            raise ValueError(f'Invalid supplied airfoil header: {source}')
        airfoils[name] = source

    from validate_aircraft import aerodynamic_wing_errors, trim_configuration_errors, validate_native_panel, acf_attachments
    errors = aerodynamic_wing_errors(result.decode('latin1')) + trim_configuration_errors(result.decode('latin1'))
    native_errors, _ = validate_native_panel(aircraft, result.decode('latin1'),
                                           [p for _, p in acf_attachments(result.decode('latin1'))])
    if errors or native_errors:
        raise ValueError(f'Candidate configuration failed: {errors + native_errors}')
    output.mkdir(parents=True)
    candidate = output / 'A380X-XP12.acf'
    candidate.write_bytes(result)
    report.update({'donor': str(donor_path), 'donor_sha256': sha(donor_path),
                   'donor_author': b.get('acf/_author'), 'aircraft': str(aircraft),
                   'before_acf_sha256': hashlib.sha256(base).hexdigest(), 'adapted_acf_sha256': sha(candidate),
                   'airfoils': {name: sha(source) for name, source in airfoils.items()},
                   'panels_preserved_byte_for_byte': True,
                   'cockpit_controls_attachment_preserved': all(c[k] == v for k, v in a.items()
                       if k.startswith('_obja/') and 'cockpit-controls' in a.get(k.split('/_')[0] + '/_v10_att_file_stl', '')),
                   'flap_slat_parameters_unchanged': all(c.get(k) == v for k, v in a.items()
                       if re.match(r'^acf/_(?:flap|flaps|slat)', k)),
                   'all_object_attachments_unchanged': {k: v for k, v in a.items() if k.startswith('_obja/')} ==
                       {k: v for k, v in c.items() if k.startswith('_obja/')},
                   'donor_plugins_copied': False, 'xplane_flight_tested': False,
                   'limits_lb': {k: c['acf/' + k] for k in ('_m_empty', '_m_max', '_m_fuel_max_tot')},
                   'limitations': ['Native ACF data only; proprietary donor FBW/FQMS behavior is not imported.',
                                   'Existing avionics and EFB reference limits are unchanged; donor systems are not ported.',
                                   'Ground loading, taxi, takeoff, handling, fuel burn and landing need native tests.',
                                   'Keep donor-derived ACF/airfoils local; no redistribution rights were supplied.'],
                   'applied': False})
    if apply:
        if any(subprocess_running(name) for name in ('X-Plane', 'Plane Maker')):
            raise RuntimeError('Close X-Plane and Plane Maker before applying')
        preserved = protected_files(aircraft)
        backup_parent = aircraft / '.flightmodel-backups'
        backup_parent.mkdir(exist_ok=True)
        backup = Path(tempfile.mkdtemp(prefix='before-pa-a388-', dir=backup_parent))
        mirror = ROOT / 'acf/A380X-XP12-current.acf'
        targets = [(candidate, current), (candidate, mirror)] + [
            (source, aircraft / 'Airfoils' / name) for name, source in airfoils.items()]
        saved = []
        for index, (source, target) in enumerate(targets):
            previous = backup / 'files' / str(index) if target.exists() else None
            if previous:
                previous.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(target, previous)
            saved.append({'target': str(target), 'backup': str(previous) if previous else None,
                          'previous_sha256': sha(previous) if previous else None})
        (backup / 'restore-map.json').write_text(json.dumps(saved, indent=2) + '\n')
        from install_windows_avionics import copy_atomic
        try:
            if sha(current) != report['before_acf_sha256'] or sha(donor_path) != report['donor_sha256']:
                raise RuntimeError('An input changed during preparation')
            for source, target in targets:
                copy_atomic(source, target)
                assert sha(source) == sha(target)
            assert protected_files(aircraft) == preserved, 'A protected aircraft file changed'
        except BaseException:
            for row in saved:
                target = Path(row['target'])
                if row['backup']:
                    copy_atomic(row['backup'], target)
                elif target.exists():
                    target.unlink()
            raise
        (output / 'protected-assets.json').write_text(json.dumps(preserved, indent=2) + '\n')
        report.update({'applied': True, 'backup': str(backup), 'protected_files_unchanged': len(preserved),
                       'protected_asset_hashes': str(output / 'protected-assets.json'),
                       'cockpit_cabin_flap_assets_unchanged': True})
    (output / 'adaptation-report.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def subprocess_running(name: str) -> bool:
    import subprocess
    result = subprocess.run(['pgrep', '-x', name], capture_output=True)
    if result.returncode not in (0, 1):
        raise RuntimeError('Could not check simulator process state')
    return result.returncode == 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--donor', required=True, type=Path)
    parser.add_argument('--aircraft', default=ROOT / 'output/A380X-XP12', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    report = adapt(args.donor, args.aircraft, args.output, args.apply)
    print(json.dumps(report, indent=2))
