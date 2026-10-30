#!/usr/bin/env python3
"""Cockpit cursor audit: pointing-finger ``button`` cursor on every pushbutton / key.

X-Plane OBJ8 manipulator cursors: ``button`` is the pointing index finger, ``hand`` the
grabbing hand, ``rotate_*`` the knob arrows. Pushbuttons, pb-switches, keys and knob
PUSH zones use ``button``; levers, toggle switches, doors, knob PULL zones, the KCCU
trackball and screen pop-up regions keep ``hand``; knobs keep their rotate cursors and the
thrust/reverse levers their drag manipulators (unchanged).

The INSTALLED a380x_cockpit_controls.obj is patched in place (only the cursor word of the
matching ATTR_manip_command lines changes) and config/cockpit_controls.json gets the same
``command_cursor`` so a future regeneration matches. Idempotent.

    python3 tools/patch_cockpit_cursors.py --aircraft output/A380X-XP12 --config config/cockpit_controls.json \
        [--backup <dir>] [--report <json>] [--dry-run]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

CONTROLS_OBJ = 'objects/interior/cockpit-controls/a380x_cockpit_controls.obj'
# Commands of physical pushbuttons / keys (exact names or regular expressions).
BUTTON_PATTERNS = tuple(re.compile(p) for p in (
    r'a380x/xp12/center_right/rto_toggle$',
    r'a380x/xp12/clock/(chr_toggle|chr_reset|date)$',
    r'a380x/xp12/ecam/sd/[a-z_]+$',
    r'a380x/xp12/efis/(captain|first_officer)/[a-z0-9]+_toggle$',
    r'a380x/xp12/efis/(captain|first_officer)/baro_std$',
    r'a380x/xp12/fcu/(ap1|ap2|fd|athr|loc|approach|speed_mach|true_mag|metric|trk_fpa)_toggle$',
    r'a380x/xp12/fcu/(altitude_hold|athr_disconnect)$',
    r'a380x/xp12/fcu/(speed|heading|altitude|vertical_speed)_push$',
    r'a380x/xp12/glareshield/(chrono|takeover|master_caution|master_warning|atc_ack|ptt)_(left|right)$',
    r'a380x/xp12/isis2?/(mode|ls|plus|minus|menu|set_push)$',
    r'a380x/xp12/elec/(?![a-z0-9_]*_cover_)[a-z0-9_]+_(toggle|press)$',
    r'sim/bleed_air/(engine_[1-4]|pack_left|pack_right|apu)_toggle$',
    r'sim/electrical/APU_start$',
    r'sim/fuel/fuel_pump_[1-4]_tog$',
    r'sim/ice/(inlet_eai[0-3]_tog|wing_tai_tog|window_heat_tog)$',
))


def is_button(command: str) -> bool:
    return any(pattern.match(command) for pattern in BUTTON_PATTERNS)


def patch_obj(text: str):
    changed = []
    lines = text.split('\n')
    for i, line in enumerate(lines):
        words = line.split('\t')
        if len(words) >= 4 and words[0].strip() == 'ATTR_manip_command' and words[1] == 'hand' and is_button(words[2]):
            words[1] = 'button'
            lines[i] = '\t'.join(words)
            changed.append(words[2])
    return '\n'.join(lines), changed


def patch_config(items: list[dict]):
    changed = []
    for item in items:
        command = item.get('command')
        if command and item.get('command_cursor', 'hand') == 'hand' and is_button(command):
            item['command_cursor'] = 'button'
            changed.append(item['node'])
    return items, changed


def audit(text: str) -> dict:
    counts: dict[str, int] = {}
    for line in text.split('\n'):
        match = re.match(r'^\s*(ATTR_manip_[a-z_]+)\t([a-z_]+)\t', line)
        if match:
            counts[f'{match.group(1)} {match.group(2)}'] = counts.get(f'{match.group(1)} {match.group(2)}', 0) + 1
    return dict(sorted(counts.items()))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--aircraft', required=True, type=Path)
    parser.add_argument('--config', required=True, type=Path)
    parser.add_argument('--backup', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    if not args.dry_run and args.backup is None:
        parser.error('--backup is required unless --dry-run is used')
    obj_path = args.aircraft / CONTROLS_OBJ
    original = obj_path.read_text()
    patched, obj_changed = patch_obj(original)
    config_text = args.config.read_text()
    items, config_changed = patch_config(json.loads(config_text))
    if not args.dry_run:
        for path, before, after in ((obj_path, original, patched),
                                    (args.config, config_text, json.dumps(items, indent=2, ensure_ascii=False) + '\n')):
            if before == after or (path == args.config and not config_changed):
                continue
            digest = hashlib.sha256(before.encode()).hexdigest()
            backup = args.backup / (digest[:16] + '-' + path.name)
            backup.parent.mkdir(parents=True, exist_ok=True)
            if not backup.exists():
                backup.write_text(before)
            path.write_text(after)
    report = dict(dry_run=args.dry_run, obj_commands_to_button=obj_changed, config_nodes_to_button=config_changed,
                  cursors_before=audit(original), cursors_after=audit(patched))
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(obj_changed=len(obj_changed), config_changed=len(config_changed),
                          after=report['cursors_after'])))


if __name__ == '__main__':
    main()
