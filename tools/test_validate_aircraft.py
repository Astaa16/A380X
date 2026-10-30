"""Regression checks for the supplied saved ACF's serialization."""
import unittest
from cockpit_draw_order import cockpit_decal_order_errors
from validate_aircraft import (acf_attachments, native_panel_declaration,
                              cockpit_attachment_alignment_errors,
                              MFD_SCREENS_ATTACHMENT, MFD_SCREENS_ANCHOR)


class SavedAcfTests(unittest.TestCase):
    def test_numeric_attachment_order_survives_lexical_property_order(self):
        text='\n'.join(f'P _obja/{index}/_v10_att_file_stl {path}' for index,path in (
            (0,'exterior/fuselage.obj'),
            (10,'interior/cockpit-panels/decal_labels.obj'),
            (2,'interior/cockpit-panels/panel_base.obj')))
        rows=acf_attachments(text)
        self.assertEqual([int(index) for index,_ in rows],[0,2,10])
        self.assertEqual(cockpit_decal_order_errors([path for _,path in rows]),[])

    def test_saved_empty_hidden_native_group_needs_no_generator_comment(self):
        for name in ('AVIONIC','AVIONICS'):
            self.assertTrue(native_panel_declaration(f'GROUP A380 NATIVE {name}\n  LOCKED\n  HIDE\nEND_GROUP\n'))

    def test_missing_flags_unrelated_groups_and_added_instruments_are_rejected(self):
        empty='GROUP A380 NATIVE AVIONIC\n  LOCKED\n  HIDE\nEND_GROUP\n'
        for value in ('',empty.replace('HIDE',''),empty.replace('A380 NATIVE AVIONIC','OTHER'),
                      empty+'gen_LED\n',empty.replace('HIDE','HIDE\n  gen_LED')):
            self.assertFalse(native_panel_declaration(value))

    def test_original_generator_marker_remains_supported(self):
        self.assertTrue(native_panel_declaration('# A380X FBW AVIONICS PLUGIN OWNS PFD, ND, EWD, FCU AND EFB REGIONS\n'))


class CockpitAttachmentTests(unittest.TestCase):
    controls = 'interior/cockpit-controls/controls.obj'

    def fixture(self, controls_yaw='180'):
        return (f'P _obja/0/_v10_att_file_stl {self.controls}\n'
                f'P _obja/0/_v10_att_psi_ref {controls_yaw}\n'
                f'P _obja/1/_v10_att_file_stl {MFD_SCREENS_ATTACHMENT}\n'
                'P _obja/1/_v10_att_psi_ref 180\n')

    def test_current_acf_regression_rotated_controls_is_rejected(self):
        errors = cockpit_attachment_alignment_errors(self.fixture('0'), self.controls)
        self.assertTrue(any('_v10_att_psi_ref' in e and 'misaligned' in e for e in errors))

    def test_matching_attachment_and_wrapped_angles_are_valid(self):
        for yaw in ('180', '-180', '540'):
            self.assertEqual(cockpit_attachment_alignment_errors(self.fixture(yaw), self.controls), [])

    def test_translation_and_parent_mismatches_are_rejected(self):
        for suffix in ('_v10_att_z_acf_prt_ref', '_v10_att_body', '_v10_att_gear', '_v10_att_wing'):
            text = self.fixture() + f'P _obja/0/{suffix} 3\n'
            self.assertTrue(cockpit_attachment_alignment_errors(text, self.controls), suffix)

    def test_visible_backing_catches_both_screen_and_controls_rotated(self):
        text = self.fixture('0').replace('_obja/1/_v10_att_psi_ref 180', '_obja/1/_v10_att_psi_ref 0')
        text += (f'P _obja/2/_v10_att_file_stl {MFD_SCREENS_ANCHOR}\n'
                 'P _obja/2/_v10_att_psi_ref 180\n')
        errors = cockpit_attachment_alignment_errors(text, self.controls)
        self.assertEqual(len(errors), 2)

    def test_missing_screen_is_left_to_existing_attachment_validation(self):
        self.assertEqual(cockpit_attachment_alignment_errors(
            f'P _obja/0/_v10_att_file_stl {self.controls}\n', self.controls), [])

    def test_invalid_transform_rejected(self):
        self.assertTrue(cockpit_attachment_alignment_errors(self.fixture('nan'), self.controls))


if __name__=='__main__':unittest.main()
