"""ELEC overhead panel: contract, OBJ patch idempotency, legend cells, cursor audit (synthetic + installed)."""
import json
import re
import tempfile
import unittest
from pathlib import Path

from build_cockpit_controls import ADIRS_ANNUNCIATOR_FIRST_CELL, ELEC_LEGEND_CELLS
from elec_cockpit_panel import (COVER_MARKER, LEGACY_IDS, MARKER, MOTION_MARKER, augment_controls,
                                retarget_legacy, retarget_legacy_manips)
from elec_panel_contract import (COMMANDS, CONTROLS, COVERS, DATAREFS, LEGEND_COLORS, PREFIX, SOUND_FILES,
                                 lamps, press_command, pressable_ids)
from patch_center_right_animations import wrap_geometry
from patch_cockpit_cursors import is_button, patch_config, patch_obj
from patch_efis_animations import vertex_payload

ROOT = Path(__file__).resolve().parents[1]
GLTF = Path('/Volumes/Untitled/A380X-XPlane12/upstream-aircraft-large-files/fbw-a380x/src/base/'
            'flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire/'
            'Part_Interior_Cockpit/model/A380_COCKPIT_LOD00.gltf')
AIRCRAFT = ROOT / 'output/A380X-XP12'
HEADER = ('I\n800\nOBJ\nTEXTURE t.png\nPOINT_COUNTS\t6\t0\t0\t6\n'
          'VT\t0\t0\t0\t0\t1\t0\t0\t0\nVT\t1\t0\t0\t0\t1\t0\t1\t0\nVT\t0\t0\t1\t0\t1\t0\t0\t1\n'
          'VT\t2\t0\t0\t0\t1\t0\t0\t0\nVT\t3\t0\t0\t0\t1\t0\t1\t0\nVT\t2\t0\t1\t0\t1\t0\t0\t1\n'
          'IDX10\t0\t1\t2\t3\t4\t5\n')


class ContractTests(unittest.TestCase):
    def test_native_core_matches_contract_order_and_kinds(self):
        header = (ROOT / 'native/avionics/a380x_elec_panel.h').read_text()
        table = header[header.index('a380x_elec_controls[A380X_ELEC_CONTROL_COUNT]'):]
        ids = re.findall(r'\{"([a-z0-9_]+)", A380X_ELEC_([A-Z_]+),', table)
        kinds = {'LATCH': 'latch', 'EXT': 'ext', 'GUARDED_LATCH': 'guarded_latch',
                 'GUARDED_MOMENTARY': 'guarded_momentary', 'INDICATOR': 'indicator'}
        self.assertEqual([(cid, kinds[kind]) for cid, kind in ids], [(row[0], row[3]) for row in CONTROLS])
        covers = re.search(r'a380x_elec_cover_ids\[A380X_ELEC_COVER_COUNT\] = \{([^}]*)\}', header).group(1)
        self.assertEqual(re.findall(r'"([a-z0-9_]+)"', covers), [row[0] for row in COVERS])

    def test_names_follow_phase2_spec_and_are_unique(self):
        self.assertEqual(len(COMMANDS), len(set(COMMANDS)))
        self.assertEqual(len(DATAREFS), len(set(DATAREFS)))
        for cid in ('bat1', 'gen4', 'bus_tie', 'comm1', 'pax_sys', 'apu_gen_b'):
            self.assertIn(f'{PREFIX}{cid}_toggle', COMMANDS)
            self.assertIn(f'{PREFIX}{cid}_on', COMMANDS)
            self.assertIn(f'{PREFIX}pb/{cid}', DATAREFS)
            self.assertIn(f'{PREFIX}pb/{cid}_press_ratio', DATAREFS)
        for cid in ('ext3', 'drive2', 'rat_man_on'):
            self.assertEqual(press_command(cid), f'{PREFIX}{cid}_press')
        self.assertIn(PREFIX + 'gpu_connect_toggle', COMMANDS)
        self.assertIn(PREFIX + 'ext/connected', DATAREFS)
        self.assertIn(PREFIX + 'lamp/rat_fault_upper', DATAREFS)
        self.assertNotIn(PREFIX + 'rat_fault_press', COMMANDS)          # indicator only
        self.assertEqual(len(lamps()), 45)
        self.assertEqual(SOUND_FILES, tuple(sorted(SOUND_FILES, key=SOUND_FILES.index)))

    def test_lamp_cells_reuse_adirs_fault_off_and_fill_the_free_row(self):
        self.assertEqual(ELEC_LEGEND_CELLS, {'altn': 59, 'disc': 60, 'avail': 61, 'on': 62})
        self.assertEqual(ADIRS_ANNUNCIATOR_FIRST_CELL, 47)
        self.assertEqual(LEGEND_COLORS['fault'], (255, 180, 45, 255))   # same amber as ADIRS FAULT
        self.assertEqual(LEGEND_COLORS['off'], (240, 245, 255, 255))    # same white as ADIRS OFF
        legends = {row[3] for row in lamps()}
        self.assertEqual(legends, {'fault', 'off', 'altn', 'disc', 'avail', 'on'})


class SyntheticPatchTests(unittest.TestCase):
    def test_legacy_latched_block_becomes_momentary_only_at_the_elec_pivot(self):
        block = ('\tANIM_begin\n\tANIM_trans\t0.13286\t3.4753101\t32.23251\t0.13286\t3.4753101\t32.23251\n'
                 '\tANIM_trans_begin\tsim/cockpit2/electrical/battery_on[0]\n\tANIM_trans_key\t0\t0\t0\t0\n'
                 '\tANIM_trans_key\t1\t0\t-0.0015\t0\n\tANIM_trans_end\n\tTRIS\t0\t3\n\tANIM_end\n')
        far = block.replace('0.13286', '0.5')
        origins = {cid: (0.1329, 3.4757, 32.2328) for cid in LEGACY_IDS}
        text, changed = retarget_legacy(HEADER + block + far, origins)
        self.assertEqual(changed, ['bat1'])
        self.assertIn(f'ANIM_trans_begin\t{PREFIX}pb/bat1_press_ratio', text)
        self.assertIn('ANIM_trans_begin\tsim/cockpit2/electrical/battery_on[0]', text)   # unrelated pivot
        self.assertEqual(retarget_legacy(text, origins), (text, []))

    def test_press_and_cover_wrap_preserve_payload_and_rerun(self):
        obj = HEADER + 'TRIS\t0\t6\n'
        button = dict(name='PUSH_OVHD_ELEC_BUSTIE', material='test', low=(0, 0, 0), high=(1, 0, 1), count=3,
                      points=None, kind='translation', dataref=PREFIX + 'pb/bus_tie_press_ratio', values=[0, 1],
                      poses=[(0, 0, 0), (0, .00136, .00063)], pivot=(0, 0, 0))
        cover = dict(name='LOCK_OVHD_ELEC_IDG1', material='test', low=(2, 0, 0), high=(3, 0, 1), count=3,
                     points=None, kind='rotation', dataref=PREFIX + 'cover/drive1', values=[0, 1],
                     axis=(1, 0, 0), poses=[0, 80.86], pivot=(2, 0, 0))
        text, meshes, _ = wrap_geometry(obj, 'panel_test.obj', [button], MOTION_MARKER)
        text, covers, _ = wrap_geometry(text, 'panel_test.obj', [cover], COVER_MARKER)
        self.assertEqual(vertex_payload(obj), vertex_payload(text))
        self.assertEqual((len(meshes), len(covers)), (1, 1))
        self.assertIn(f'ANIM_rotate_begin\t1\t0\t0\t{PREFIX}cover/drive1', text)
        self.assertIn('ANIM_rotate_key\t1\t80.86', text)
        again, *_ = wrap_geometry(text, 'panel_test.obj', [button], MOTION_MARKER)
        self.assertEqual(again, text)

    def test_legacy_click_meshes_get_native_commands_and_button_cursor(self):
        text = ('ATTR_manip_command\thand\tsim/electrical/battery_1_toggle\tBAT 1 - native battery toggle\n'
                'ATTR_manip_command\thand\tsim/electrical/generator_4_toggle\tGEN 4 - native generator toggle\n')
        patched, changed = retarget_legacy_manips(text)
        self.assertEqual(changed, ['bat1', 'gen4'])
        self.assertIn(f'ATTR_manip_command\tbutton\t{PREFIX}bat1_toggle\tBAT 1 - AUTO / OFF', patched)
        self.assertEqual(retarget_legacy_manips(patched), (patched, []))

    def test_cursor_audit_classification(self):
        for command in ('a380x/xp12/fcu/ap1_toggle', 'a380x/xp12/ecam/sd/engine', 'a380x/xp12/isis2/menu',
                        'sim/ice/window_heat_tog', 'a380x/xp12/elec/drive1_press', 'a380x/xp12/elec/bat1_toggle',
                        'a380x/xp12/fcu/heading_push', 'a380x/xp12/glareshield/master_warning_left'):
            self.assertTrue(is_button(command), command)
        for command in ('a380x/xp12/center_right/gear_toggle', 'a380x/xp12/fcu/heading_pull',
                        'sim/lights/beacon_lights_toggle', 'sim/flight_controls/door_toggle_17',
                        'a380x/xp12/mfd/captain/popup', 'a380x/xp12/elec/drive1_cover_toggle',
                        'a380x/xp12/fcu/altitude_step_toggle', 'a380x/xp12/center_right/antiskid_toggle'):
            self.assertFalse(is_button(command), command)
        text = ('ATTR_manip_command\thand\ta380x/xp12/fcu/fd_toggle\tFD\n'
                'ATTR_manip_command\thand\tsim/lights/nav_lights_toggle\tNAV\n')
        patched, changed = patch_obj(text)
        self.assertEqual(changed, ['a380x/xp12/fcu/fd_toggle'])
        self.assertEqual(patch_obj(patched), (patched, []))
        items, nodes = patch_config([{'node': 'PUSH_FCU_FD', 'command': 'a380x/xp12/fcu/fd_toggle', 'label': 'FD'}])
        self.assertEqual((nodes, items[0]['command_cursor']), (['PUSH_FCU_FD'], 'button'))


@unittest.skipUnless(GLTF.is_file() and (AIRCRAFT / 'objects').is_dir(), 'pinned FBW cockpit glTF / aircraft not present')
class InstalledAircraftTests(unittest.TestCase):
    def test_installed_objects_carry_the_complete_panel_once(self):
        controls = (AIRCRAFT / 'objects/interior/cockpit-controls/a380x_cockpit_controls.obj').read_text()
        self.assertEqual(controls.count(MARKER), 1)
        for cid in pressable_ids():
            self.assertEqual(controls.count(f'\t{press_command(cid)}\t'), 1, cid)
        for cover, _node, _caption in COVERS:
            self.assertIn(f'ATTR_manip_command\thand\t{PREFIX}{cover}_cover_toggle\t', controls)
        for dataref, *_rest in lamps():
            self.assertEqual(controls.count(f'ATTR_light_level\t0\t1\t{dataref}\n'), 1, dataref)
        self.assertNotIn('sim/electrical/battery_1_toggle', controls)
        self.assertEqual(augment_controls(controls, GLTF)[0], controls)   # rerun: no change
        panel = (AIRCRAFT / 'objects/interior/cockpit-panels/panel_a380_cockpit_ovhd_knobs_4k.obj').read_text()
        self.assertEqual(panel.count(COVER_MARKER), 6)
        self.assertNotIn('sim/cockpit2/electrical/battery_on[0]', panel)
        # previously patched FCU/EFIS/ISIS/PFD/ND/SD device blocks are preserved
        for marker in ('# A380X FCU SHARED DISPLAYS V1', '# A380X NATIVE ND DEVICE QUADS V1', '# A380X NATIVE ECAM SD V2',
                       '# A380X NATIVE PFD DEVICE QUADS V1', '# A380X NATIVE ISIS 1 V1', '# A380X NATIVE ISIS 2 V1',
                       '# A380X ECAM ENG SELECTION V1', '# ADIRS ANNUNCIATOR'):
            self.assertIn(marker, controls)


if __name__ == '__main__':
    unittest.main()
