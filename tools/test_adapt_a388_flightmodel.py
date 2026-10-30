import unittest
from adapt_a388_flightmodel import merge, properties, selected, extended_contact
from validate_aircraft import aerodynamic_wing_errors, trim_configuration_errors


def fixture(donor=False):
    p = {'acf/_num_engn': '4', 'acf/_cgY': '-7' if donor else '3',
         'acf/_cgZ': '102' if donor else '99', 'acf/_m_empty': '595248' if donor else '610000',
         'acf/_tank_rat/count': '9', 'acf/_name': 'Foreign' if donor else 'Native A380',
         'acf/_num_batteries': '1' if donor else '4', 'acf/_FBWtheP': '2' if donor else '.4',
         'acf/_ptch_acft_up_trim_rat': '0' if donor else '1',
         'acf/_ptch_acft_dn_trim_rat': '0' if donor else '1',
         'acf/_ptch_acft_trim_mode': '0', 'acf/_using_new_v12_flying_stab_trim': '1',
         'acf/_trim_elev_elec_EQ': '1', 'acf/_stab_trim_up': '10', 'acf/_stab_trim_dn': '2',
         '_obja/0/_v10_att_file_stl': 'foreign.obj' if donor else 'cockpit-controls.obj',
         '_obja/0/_v10_att_psi_ref': '0' if donor else '180',
         '_wing/13/_part_specs_eq': '1' if donor else '0',
         '_wing/13/_inc_trim/0': '1' if donor else '0',
         '_wing/13/_part_y': '-8', '_wing/13/_geo_xyz/0,0,1': '-8',
         '_wing/13/_geo_nrm/0,0,1': '1', '_wing/13/_semilen_SEG': '20',
         '_body/0/_part_specs_eq': '1', '_body/0/_part_y': '0',
         '_body/0/_geo_xyz/0,0,1': '-5',
         '_engn/0/_part_y': '-11', '_engn/0/_engn_health_var': '1',
         'acf/_tank_xyz/0,1': '-9', 'acf/_tank_xyz/0,2': '100',
         'acf/_h_eqlbm': '15.38', 'acf/_flap_detents': '5', 'acf/_flap1_dn/5': '33'}
    for i, kind in enumerate((3, 5, 5, 6, 6)):
        for suffix, value in {'gear_type': kind, 'gear_x': i, 'gear_y': -10, 'gear_z': 100,
                              'leg_len': 10, 'tire_radius': 2, 'latE': 0, 'lonE': 0}.items():
            p[f'_gear/{i}/_{suffix}'] = str(value)
    for i in range(9):
        p[f'acf/_tank_rat/{i}'] = str(.1112 if donor else 1/9)
        p[f'acf/_tank_role/{i}'] = '3' if donor else '0'
        p[f'acf/_tank_part_en/{i}'] = '-1'
    if donor:
        p.pop('_engn/0/_engn_health_var')
    return ('I\n1200 Version\nACF\n\nPROPERTIES_BEGIN\n' +
            ''.join(f'P {k} {v}\n' for k, v in p.items()) +
            'PROPERTIES_END\nPANEL_2D_BEGIN\nPANEL_2D_END\nPANEL_3D_BEGIN\n' +
            ('gen_LED\n' if donor else 'GROUP A380 NATIVE AVIONIC\n  LOCKED\n  HIDE\nEND_GROUP\n') +
            'PANEL_3D_END\n').encode()


class AdaptTests(unittest.TestCase):
    def setUp(self):
        self.base, self.donor = fixture(), fixture(True)
        self.result, self.report = merge(self.base, self.donor, 7.5)
        self.p = properties(self.result)

    def test_panels_and_cockpit_not_imported(self):
        self.assertEqual(self.result.split(b'PROPERTIES_END\n')[1], self.base.split(b'PROPERTIES_END\n')[1])
        self.assertEqual(self.p['_obja/0/_v10_att_file_stl'], 'cockpit-controls.obj')
        self.assertEqual(self.p['_obja/0/_v10_att_psi_ref'], '180')

    def test_identity_power_and_fbw_ownership_preserved(self):
        self.assertEqual(self.p['acf/_name'], 'Native A380')
        self.assertEqual(self.p['acf/_num_batteries'], '4')
        self.assertEqual(self.p['acf/_FBWtheP'], '.4')

    def test_consistent_vertical_translation_not_normals_or_local_body_mesh(self):
        self.assertEqual(float(self.p['acf/_cgY']), .5)
        self.assertEqual(float(self.p['_wing/13/_part_y']), -.5)
        self.assertEqual(float(self.p['_wing/13/_geo_xyz/0,0,1']), -.5)
        self.assertEqual(self.p['_wing/13/_geo_nrm/0,0,1'], '1')
        self.assertEqual(float(self.p['_body/0/_part_y']), 7.5)
        self.assertEqual(self.p['_body/0/_geo_xyz/0,0,1'], '-5')
        self.assertEqual(float(self.p['_engn/0/_part_y']), -3.5)
        self.assertEqual(float(self.p['acf/_tank_xyz/0,1']), -1.5)
        self.assertEqual(self.p['acf/_tank_xyz/0,2'], '100')

    def test_mass_cg_highlift_and_modern_health_fields(self):
        self.assertEqual(self.p['acf/_m_empty'], '595248')
        self.assertEqual(self.p['acf/_cgZ'], '102')
        self.assertEqual(self.p['acf/_h_eqlbm'], '15.38')
        self.assertEqual(self.p['acf/_flap1_dn/5'], '33')
        self.assertEqual(self.p['_engn/0/_engn_health_var'], '1')

    def test_fuel_is_native_and_normalized(self):
        self.assertAlmostEqual(sum(float(self.p[f'acf/_tank_rat/{i}']) for i in range(9)), 1, places=9)
        self.assertTrue(all(self.p[f'acf/_tank_role/{i}'] == '0' for i in range(9)))

    def test_extended_enabled_wings_accepted_and_old_disabled_slots_rejected(self):
        self.assertEqual(aerodynamic_wing_errors(self.result.decode()), [])
        self.assertTrue(aerodynamic_wing_errors(self.base.decode()))

    def test_tilted_leg_contact_envelope(self):
        p = properties(self.donor)
        p['_gear/0/_lonE'] = '-2'
        x, y, z = extended_contact(p, 0)
        self.assertAlmostEqual(y, -21.9939082702)
        self.assertAlmostEqual(z, 99.651005033)

    def test_reject_wrong_gear_mapping_and_duplicate_keys(self):
        with self.assertRaises(ValueError):
            merge(self.base, self.donor.replace(b'_gear/3/_gear_type 6', b'_gear/3/_gear_type 5'), 7)
        with self.assertRaises(ValueError):
            properties(self.base.replace(b'PROPERTIES_END', b'P acf/_num_engn 4\nPROPERTIES_END'))

    def test_no_avionics_or_system_equipment_in_physics_allowlist(self):
        for key in ('acf/_battery_watt_hr_max', 'acf/_pe_xyz/1', 'acf/_esys_bus_bit_add/rel_efis_1',
                    'acf/_AShi_ptch_rat', 'acf/_FBWtheP', '_obja/0/_obj_flags'):
            self.assertFalse(selected(key), key)

    def test_repeat_import_is_deterministic(self):
        again, _ = merge(self.result, self.donor, 7.5)
        self.assertEqual(again, self.result)

    def test_disabled_donor_wings_removed_not_activated(self):
        donor = self.donor.replace(b'PROPERTIES_END',
            b'P _wing/55/_part_specs_eq 0\nP _wing/55/_semilen_SEG 20\nPROPERTIES_END')
        result, _ = merge(self.base, donor, 7.5)
        self.assertNotIn('_wing/55/_semilen_SEG', properties(result))
        self.assertEqual(aerodynamic_wing_errors(result.decode()), [])

    def test_invalid_ratio_and_nonfinite_datum_rejected(self):
        for dy in (float('nan'), float('inf'), 100):
            with self.assertRaises(ValueError): merge(self.base, self.donor, dy)
        with self.assertRaises(ValueError):
            merge(self.base, self.donor.replace(b'acf/_tank_rat/0 0.1112',
                                               b'acf/_tank_rat/0 -1'), 7.5)

    def test_flying_stabilizer_and_elevator_trim_import_together(self):
        self.assertEqual(self.p['acf/_ptch_acft_up_trim_rat'], '0')
        self.assertEqual(self.p['acf/_ptch_acft_dn_trim_rat'], '0')
        self.assertEqual(self.p['acf/_stab_trim_up'], '10')
        self.assertEqual(self.p['_wing/13/_inc_trim/0'], '1')
        self.assertEqual(trim_configuration_errors(self.result.decode()), [])

    def test_load_warning_from_screenshot_is_rejected(self):
        for direction in ('up', 'dn'):
            bad = self.result.decode().replace(f'acf/_ptch_acft_{direction}_trim_rat 0',
                                               f'acf/_ptch_acft_{direction}_trim_rat 1')
            errors = trim_configuration_errors(bad)
            self.assertTrue(any('BOTH' in error for error in errors), errors)

    def test_elevator_trim_without_flying_stabilizer_remains_allowed(self):
        fixed = self.result.decode().replace('_wing/13/_inc_trim/0 1', '_wing/13/_inc_trim/0 0')
        fixed = fixed.replace('acf/_ptch_acft_up_trim_rat 0', 'acf/_ptch_acft_up_trim_rat 1')
        self.assertEqual(trim_configuration_errors(fixed), [])

    def test_invalid_trim_numbers_rejected(self):
        bad = self.result.decode().replace('acf/_ptch_acft_up_trim_rat 0', 'acf/_ptch_acft_up_trim_rat nan')
        self.assertTrue(trim_configuration_errors(bad))


if __name__ == '__main__':
    unittest.main()
