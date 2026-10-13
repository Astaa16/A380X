import json
import math
import tempfile
import unittest
from pathlib import Path

from avionics_provider import ADIRS_COMMANDS, ADIRS_DATAREFS
from build_cockpit_controls import (
    ADIRS_ANNUNCIATORS, ADIRS_ANNUNCIATOR_FIRST_CELL, FCU_LABEL_COLUMNS, FCU_LABEL_ROWS,
    adirs_lamp_face, local_matrix, load_controls, transform_point,
)
from build_fcu_panel import build_label_atlas
from patch_adirs_animations import ADIRS_MOTIONS, MARKER
from patch_center_right_animations import wrap_geometry
from patch_efis_animations import vertex_payload


class AdirsPanelTests(unittest.TestCase):
    def test_three_detented_selectors_and_six_independent_pushbuttons_have_native_contracts(self):
        project = Path(__file__).resolve().parents[1]
        controls = [c for c in load_controls(project / 'config/cockpit_controls.json')
                    if '_ADIRS_' in c.node]
        self.assertEqual(len(controls), 9)
        for control in controls:
            for command in (control.command, control.command_up, control.command_down):
                if command:
                    self.assertIn(command, ADIRS_COMMANDS)
        rules = json.loads((project / 'config/animation_datarefs.json').read_text())['mappings']
        for animation, node, ref, maximum in ADIRS_MOTIONS:
            self.assertIn('a380x/xp12/' + ref, ADIRS_DATAREFS)
            rule = next(r for r in rules if r['pattern'] == '(?i)' + animation)
            self.assertEqual(rule['dataref'], 'a380x/xp12/' + ref)
            self.assertEqual(rule['value_max'], maximum)
            self.assertEqual(maximum, 2 if node.startswith('KNOB') else 1)

    def test_lamps_are_white_off_amber_fault_and_fit_atlas(self):
        self.assertEqual(len(ADIRS_ANNUNCIATORS), 12)
        self.assertLessEqual(ADIRS_ANNUNCIATOR_FIRST_CELL + len(ADIRS_ANNUNCIATORS),
                             FCU_LABEL_COLUMNS * FCU_LABEL_ROWS)
        from PIL import Image
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            build_label_atlas(output)
            with Image.open(output / 'fcu_labels_LIT.png') as atlas:
                for i, (label, node, ref, color) in enumerate(ADIRS_ANNUNCIATORS,
                                                           ADIRS_ANNUNCIATOR_FIRST_CELL):
                    self.assertIn(ref, ADIRS_DATAREFS)
                    self.assertTrue(node.endswith('_SEQ1' if label == 'FAULT' else '_SEQ2'))
                    self.assertEqual(color, (255, 180, 45, 255) if label == 'FAULT'
                                     else (240, 245, 255, 255))
                    cell = atlas.crop(((i % 8) * 128, (i // 8) * 64,
                                       (i % 8 + 1) * 128, (i // 8 + 1) * 64))
                    self.assertIn(color, set(cell.getdata()))

    def test_overhead_lamp_preserves_source_tilt_roll_and_text_direction(self):
        positions = [(-.007, 0, -.0035), (.007, 0, -.0035),
                     (.007, 0, .0035), (-.007, 0, .0035)]
        matrix = local_matrix(dict(translation=(.28, 3.56, 32.01),
            rotation=(.0139639536, -.2139027715, .9745970964, .0648935735)))
        corners, normal = adirs_lamp_face(positions, matrix)
        self.assertAlmostEqual(sum(v * v for v in normal), 1)
        self.assertLess(normal[1], -.8)  # face points down at crew, not forward
        local_corners = [positions[3], positions[2], positions[1], positions[0]]
        for actual, local in zip(corners, local_corners):
            expected = transform_point(matrix, local)
            for axis in range(3):
                self.assertAlmostEqual(actual[axis] - expected[axis], .0004 * normal[axis])

    def test_patch_moves_button_and_legend_without_touching_texture_payload(self):
        obj = ('TEXTURE overhead.png\nPOINT_COUNTS 6 0 0 6\n'
               'VT\t0\t0\t0\t0\t1\t0\t0\t0\nVT\t1\t0\t0\t0\t1\t0\t1\t0\n'
               'VT\t0\t0\t1\t0\t1\t0\t0\t1\nVT\t2\t0\t0\t0\t1\t0\t0\t0\n'
               'VT\t3\t0\t0\t0\t1\t0\t1\t0\nVT\t2\t0\t1\t0\t1\t0\t0\t1\n'
               'IDX10\t0\t1\t2\t3\t4\t5\nTRIS\t0\t6\n')
        button = dict(name='PUSH_OVHD_ADIRS_IR1', material='test', low=(0, 0, 0),
            high=(1, 0, 1), count=3, points=None, kind='translation',
            dataref='a380x/xp12/adirs/ir1_pressed', values=[0, 1],
            poses=[(0, 0, 0), (.0002, .00134, .00062)])
        legend = dict(button, name='PUSH_OVHD_ADIRS_IR1_SEQ2', low=(2, 0, 0), high=(3, 0, 1))
        patched, meshes, _ = wrap_geometry(obj, 'panel_test.obj', [button, legend], MARKER)
        self.assertEqual(vertex_payload(obj), vertex_payload(patched))
        self.assertEqual(len(meshes), 2)
        self.assertEqual(patched.count('ANIM_trans_begin\ta380x/xp12/adirs/ir1_pressed'), 2)
        self.assertEqual(wrap_geometry(patched, 'panel_test.obj', [button, legend], MARKER)[0], patched)


if __name__ == '__main__':
    unittest.main()
