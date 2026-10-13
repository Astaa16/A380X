"""Pure-Python motion diagnostics fixtures; never contact the simulator."""
import importlib.util
from io import StringIO
import json
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('motion_audit', Path(__file__).with_name('audit_control_surface_motion.py'))
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)

GEOMETRY = '''I
800
OBJ
VT 0 0 0 0 0 1 0 0
VT 1 0 0 0 0 1 1 0
VT 1 1 0 0 0 1 1 1
VT 0 1 0 0 0 1 0 1
IDX 0
IDX 1
IDX 2
IDX 0
IDX 2
IDX 3
'''


class AuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / 'motion.obj'

    def write(self, animation, geometry=GEOMETRY):
        self.path.write_text(geometry + animation)
        return self.path

    def test_translation_bounds_centroid_displacements_and_no_triangle_bake(self):
        path = self.write('''ANIM_begin
ANIM_trans 0 0 0 2 0 0 0 1 test/roll
TRIS 0 6
ANIM_end
''')
        with patch('obj8_preview.PreviewObject.bake', side_effect=AssertionError('Triangle expansion forbidden')):
            report = audit.audit_object(path, 'test/roll', [-1, 0, 1])
        self.assertEqual(report['neutral']['bounds_m'], [[0, 0, 0], [1, 1, 0]])
        self.assertEqual(report['neutral']['draws'][0]['centroid_m'], [.5, .5, 0])
        self.assertEqual(report['source_vertex_count'], 4)
        self.assertEqual(report['index_count'], 6)
        self.assertEqual(report['neutral']['draw_vertex_count'], 4)
        for sample in (report['samples'][0], report['samples'][2]):
            self.assertEqual(sample['max_displacement_m'], 2)
            self.assertEqual(sample['rms_displacement_m'], 2)
            self.assertEqual(sample['draws'][0]['transform_datarefs'], ['test/roll'])
        self.assertEqual(report['samples'][2]['bounds_m'], [[2, 0, 0], [3, 1, 0]])
        self.assertEqual(report['samples'][2]['draws'][0]['centroid_displacement_m'], [2, 0, 0])
        self.assertEqual(report['samples'][1]['rms_displacement_m'], 0)
        self.assertEqual(len(report['sha256']), 64)

    def test_nested_rotation_about_translated_pivot(self):
        path = self.write('''ANIM_begin
ANIM_trans 2 0 0 2 0 0
ANIM_begin
ANIM_rotate 0 0 1 0 90 0 1 test/roll
TRIS 0 6
ANIM_end
ANIM_end
''')
        sample = audit.audit_object(path, 'test/roll', [1])['samples'][0]
        for actual, expected in zip(sample['draws'][0]['centroid_m'], [1.5, .5, 0]):
            self.assertAlmostEqual(actual, expected)
        self.assertAlmostEqual(sample['max_displacement_m'], 2)
        self.assertAlmostEqual(sample['rms_displacement_m'], math.sqrt(2))

    def test_duplicate_ranges_keep_source_identity_across_visibility_changes(self):
        path = self.write('''ANIM_begin
ANIM_trans 0 0 0 0 1 0 0 1 test/roll
ANIM_hide 0 0 test/roll
TRIS 0 3
ANIM_end
ANIM_begin
ANIM_trans 0 0 0 0 2 0 0 1 test/roll
TRIS 0 3
ANIM_end
''')
        report = audit.audit_object(path, 'test/roll', [1])
        first, second = report['neutral']['draws']
        self.assertEqual([first['draw_index'], second['draw_index']], [0, 1])
        self.assertFalse(first['visible'])
        self.assertEqual(first['transform_datarefs'], ['test/roll'])
        self.assertTrue(second['visible'])
        self.assertNotEqual(first['source_line'], second['source_line'])
        sample = report['samples'][0]
        self.assertEqual(sample['appeared_draws'], [0])
        self.assertEqual(sample['draws'][1]['draw_index'], 1)
        self.assertEqual(sample['draws'][1]['max_displacement_m'], 2)
        self.assertIsNone(sample['draws'][0]['max_displacement_m'])
        self.assertEqual(sample['compared_draw_vertex_count'], 3)

    def test_disappeared_draw_has_no_false_zero_displacement(self):
        path = self.write('ANIM_begin\nANIM_hide 1 1 test/roll\nTRIS 0 3\nANIM_end\n')
        sample = audit.audit_object(path, 'test/roll', [1])['samples'][0]
        self.assertEqual(sample['disappeared_draws'], [0])
        self.assertIsNone(sample['bounds_m'])
        self.assertIsNone(sample['max_displacement_m'])
        self.assertIsNone(sample['draws'][0]['centroid_m'])

    def test_lod_skips_do_not_renumber_later_draws(self):
        path = self.write('''ATTR_LOD 10 100
TRIS 0 3
ATTR_LOD 0 10
ANIM_begin
ANIM_trans 0 0 0 1 0 0 0 1 test/roll
TRIS 0 6
ANIM_end
''')
        neutral = audit.audit_object(path, 'test/roll', [0])['neutral']
        self.assertFalse(neutral['draws'][0]['visible'])
        self.assertEqual(neutral['draws'][1]['draw_index'], 1)
        self.assertTrue(neutral['draws'][1]['visible'])

    def test_missing_selected_ref_fails(self):
        with self.assertRaisesRegex(ValueError, 'Selected dataref is absent'):
            audit.audit_object(self.write('TRIS 0 3\n'), 'test/absent', [0])

    def test_secondary_refs_must_be_explicit_even_gear_down(self):
        path = self.write('''ANIM_begin
ANIM_rotate 0 0 1 0 20 0 1 sim/flightmodel2/gear/deploy_ratio[1]
ANIM_trans 0 0 0 1 0 0 0 1 test/roll
TRIS 0 3
ANIM_end
''')
        with self.assertRaisesRegex(ValueError, 'Missing explicit dataref value'):
            audit.audit_object(path, 'test/roll', [1])
        report = audit.audit_object(path, 'test/roll', [1], fixed_values={'sim/flightmodel2/gear/deploy_ratio[1]': 1})
        self.assertEqual(report['neutral_datarefs']['sim/flightmodel2/gear/deploy_ratio[1]'], 1)
        self.assertEqual(len(report['samples'][0]['draws'][0]['transform_datarefs']), 2)

    def test_nonfinite_inputs_rejected(self):
        path = self.write('ANIM_begin\nANIM_trans 0 0 0 1 0 0 0 1 test/roll\nTRIS 0 3\nANIM_end\n')
        for kwargs in ({'values': [float('nan')]}, {'neutral_value': float('inf')},
                       {'fixed_values': {'unused': float('-inf')}}, {'lod_distance': float('nan')}):
            with self.subTest(kwargs=kwargs), self.assertRaisesRegex(ValueError, 'finite'):
                audit.audit_object(path, 'test/roll', **({'values': [1]} | kwargs))

    def test_nonfinite_unused_vertex_normals_and_keys_rejected(self):
        animation = 'ANIM_begin\nANIM_trans 0 0 0 1 0 0 0 1 test/roll\nTRIS 0 3\nANIM_end\n'
        for component in ('VT nan 0 0 0 0 1 0 0\n', 'VT 0 0 0 inf 0 1 0 0\n'):
            with self.subTest(component=component), self.assertRaisesRegex(ValueError, 'finite'):
                audit.audit_object(self.write(animation, GEOMETRY + component), 'test/roll', [0])
        path = self.write('ANIM_begin\nANIM_rotate_begin 0 0 1 test/roll\nANIM_rotate_key 0 0\n'
                          'ANIM_rotate_key 1 10\nANIM_rotate_key 2 inf\nANIM_rotate_end\nTRIS 0 3\nANIM_end\n')
        with self.assertRaisesRegex(ValueError, 'finite'):
            audit.audit_object(path, 'test/roll', [0])

    def test_changed_source_fails_instead_of_matching_unrelated_vertices(self):
        path = self.write('ANIM_begin\nANIM_trans 0 0 0 1 0 0 0 1 test/roll\nTRIS 0 3\nANIM_end\n')
        original = audit.load_sample
        calls = 0

        def changing(*args):
            nonlocal calls
            calls += 1
            obj, metadata = original(*args)
            if calls == 2:
                metadata['sha256'] = 'changed'
            return obj, metadata

        with patch.object(audit, 'load_sample', side_effect=changing), self.assertRaisesRegex(ValueError, 'changed while auditing'):
            audit.audit_object(path, 'test/roll', [1])

    def test_cli_json_diagnostic_label_and_nonzero_neutral(self):
        path = self.write('ANIM_begin\nANIM_trans 0 0 0 2 0 0 0 1 test/roll\nTRIS 0 6\nANIM_end\n')
        target = self.root / 'report.json'
        with patch.object(audit.sys, 'stdout', new_callable=StringIO):
            audit.main([str(path), '--dataref', 'test/roll', '--values', '-1', '0', '1',
                        '--neutral', '1', '--report', str(target)])
        report = json.loads(target.read_text())
        self.assertTrue(report['diagnostic_only'])
        self.assertEqual(report['objects'][0]['samples'][0]['max_displacement_m'], 4)
        self.assertEqual(report['objects'][0]['samples'][2]['max_displacement_m'], 0)

    def test_cli_failure_writes_no_report_and_does_not_overwrite_input(self):
        path = self.write('TRIS 0 3\n')
        original = path.read_text()
        for target in (path, self.root / 'invalid.json'):
            with self.subTest(target=target), patch.object(audit.sys, 'stderr', new_callable=StringIO), self.assertRaises(SystemExit) as error:
                audit.main([str(path), '--dataref', 'missing', '--values', '0', '--report', str(target)])
            self.assertEqual(error.exception.code, 2)
        self.assertEqual(path.read_text(), original)
        self.assertFalse((self.root / 'invalid.json').exists())


if __name__ == '__main__':
    unittest.main()
