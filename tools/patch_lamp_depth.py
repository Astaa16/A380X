#!/usr/bin/env python3
"""Make the cockpit legend lamps visible over the FBW pushbutton decals (in-sim finding 2026-10-22).

X-Plane draws the aircraft objects in ACF order. The lit legend quads live in
a380x_cockpit_controls.obj (_obja 119) and were alpha-blended; blended geometry does not
write depth, so the opaque legend decal object decal_a380_pushbutton_decal.obj (_obja 121,
0.4 mm behind every quad, ATTR_poly_os 1) painted over them: the lamp datarefs were 1.0 in the
running sim, ANN LT TEST lit nothing. Lamps backed by objects drawn earlier (FCU AP1/AP2/FD/
A/THR on mip_knob01) were visible. Fix:

* a380x_cockpit_controls.obj - every lamp quad whose backing surface is that decal (all ELEC,
  overhead and ADIRS legends, glareshield/gear annunciators) is drawn alpha-tested opaque
  (ATTR_no_blend 0.5 ... ATTR_blend), so it writes depth and the later decal is rejected
  behind its glyphs. The FCU lamps that already work are left blended.
* decal_a380_pushbutton_decal.obj - the APU MASTER SW legend decal was not animated (the cap
  and the lamps travel 1.1 mm in when latched), so the ON lamp sank behind it. Both decal
  faces now replay their lamp's travel (same dataref and keys).

Patches the INSTALLED objects in place (nothing regenerated), backs up by content hash, and is
idempotent: re-running is a no-op.

    python3 tools/patch_lamp_depth.py --aircraft output/A380X-XP12 [--backup <dir>] [--dry-run]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

CONTROLS_OBJ = 'objects/interior/cockpit-controls/a380x_cockpit_controls.obj'
DECAL_OBJ = 'objects/interior/cockpit-panels/decal_a380_pushbutton_decal.obj'
NO_BLEND = 'ATTR_no_blend\t0.5'
BLEND = 'ATTR_blend'
MARKER = '# A380X LAMP DEPTH V1'
DECAL_MARKER = '# A380X APU MASTER DECAL MOTION V1'
# Lamp comment prefixes always backed by the pushbutton decal (overhead panels).
OVERHEAD_PREFIXES = ('# ELEC LAMP lamp/', '# OVHD LAMP lamp/', '# ADIRS ANNUNCIATOR ')
# FCU-section annunciators backed by the decal (the others sit on mip_knob01 / mip03).
FCU_DECAL_NODES = (
    'PUSH_GLARESHIELD_', 'GEAR_WING_', 'GEAR_BODY_', 'PUSH_AUTOBKR_LDGGEAR_', 'PUSH_RTO_ARM_')
# APU MASTER SW decal faces (identity transform) and the lamp whose travel each follows.
APU_DECAL_TRIS = (('TRIS 714 6', 'apu_master_upper'), ('TRIS 720 6', 'apu_master_lower'))


def _target(line: str) -> bool:
    if line.startswith(OVERHEAD_PREFIXES):
        return True
    return line.startswith('# FCU ANNUNCIATOR ') and \
        line.removeprefix('# FCU ANNUNCIATOR ').startswith(FCU_DECAL_NODES)


def patch_controls(text: str) -> tuple[str, int]:
    """Wrap every target lamp block in ATTR_no_blend / ATTR_blend. Returns (text, blocks)."""
    if MARKER in text:
        return text, 0
    lines = text.split('\n')
    out, count, i = [], 0, 0
    while i < len(lines):
        line = lines[i]
        out.append(line)
        i += 1
        if not _target(line):
            continue
        if i < len(lines) and lines[i] == NO_BLEND:
            continue   # generated opaque (elec_cockpit_panel.py / ovhd_cockpit_lamps.py)
        out.append(NO_BLEND)
        count += 1
        depth, opened = 0, False
        while i < len(lines):
            row = lines[i]
            out.append(row)
            i += 1
            words = row.split()
            if words[:1] == ['ANIM_begin']:
                depth += 1
                opened = True
            elif words[:1] == ['ANIM_end']:
                depth -= 1
                if opened and depth == 0:
                    break
        else:
            raise ValueError(f'unterminated lamp block: {line}')
        if i < len(lines) and lines[i] == 'ATTR_light_level_reset':
            out.append(lines[i])
            i += 1
        out.append(BLEND)
    while out and out[-1] == '':
        out.pop()
    out += ['', MARKER, '']
    return '\n'.join(out), count


def lamp_motion(controls_text: str, lamp: str):
    """(dataref, [ANIM_trans_key rows]) of '# OVHD LAMP lamp/<lamp>'."""
    lines = controls_text.split('\n')
    start = lines.index('# OVHD LAMP lamp/' + lamp)
    ref, keys = None, []
    for row in lines[start + 1:]:
        words = row.split()
        if words[:1] == ['ANIM_trans_begin']:
            ref = words[1]
        elif words[:1] == ['ANIM_trans_key'] and ref:
            keys.append(row.strip())
        elif words[:1] == ['ANIM_trans_end'] or row == 'ATTR_light_level_reset':
            break
    if not ref or len(keys) < 2:
        raise ValueError(f'no motion for lamp {lamp}')
    return ref, keys


def patch_decal(text: str, controls_text: str) -> tuple[str, int]:
    if DECAL_MARKER in text:
        return text, 0
    lines = text.split('\n')
    count = 0
    for tris, lamp in APU_DECAL_TRIS:
        rows = [n for n, row in enumerate(lines) if row.split() == tris.split()]
        if len(rows) != 1:
            raise ValueError(f'{tris} not unique in the decal object')
        n = rows[0]
        indent = lines[n][:len(lines[n]) - len(lines[n].lstrip())]
        ref, keys = lamp_motion(controls_text, lamp)
        lines[n:n + 1] = [f'{indent}{DECAL_MARKER} {lamp}', f'{indent}ANIM_begin',
                          f'{indent}ANIM_trans_begin\t{ref}', *(indent + k for k in keys),
                          f'{indent}ANIM_trans_end', lines[n], f'{indent}ANIM_end']
        count += 1
    return '\n'.join(lines), count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--aircraft', type=Path, required=True)
    parser.add_argument('--backup', type=Path)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    controls_path, decal_path = args.aircraft / CONTROLS_OBJ, args.aircraft / DECAL_OBJ
    controls, decal = controls_path.read_text(), decal_path.read_text()
    new_controls, lamps = patch_controls(controls)
    new_decal, faces = patch_decal(decal, controls)
    report = {'opaque_lamp_blocks': lamps, 'apu_master_decal_faces': faces}
    if not args.dry_run:
        for path, old, new in ((controls_path, controls, new_controls), (decal_path, decal, new_decal)):
            if new == old:
                continue
            if args.backup:
                args.backup.mkdir(parents=True, exist_ok=True)
                digest = hashlib.sha256(old.encode()).hexdigest()[:16]
                (args.backup / f'{path.name}.{digest}').write_text(old)
            tmp = path.with_name('.' + path.name + '.tmp')
            tmp.write_text(new)
            tmp.replace(path)
    print(json.dumps(report))
    return 0


if __name__ == '__main__':
    sys.exit(main())
