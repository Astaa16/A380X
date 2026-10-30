import json
import tempfile
import unittest
import struct
import zlib
from pathlib import Path

from build_development_aircraft import (
    MFD_SCREENS_ATTACHMENT,
    build_replacements,
    copy_exterior_obj,
    interior_sources,
    merge_refreshed_paths,
    mfd_screens_texture,
    set_value,
    write_empty_2d_panel,
)
from build_cockpit_controls import (MFD_SCREEN_TEXTURE, MFD_TOUCH_LIFT, build_mfd_screens_obj,
                                    mfd_device_quads, mfd_screen_black_png)
from avionics_provider import (
    AVIONICS_DATAREFS, DISPLAY_TARGETS, FCU_COMMANDS, FCU_DATAREFS, FILES as AVIONICS_FILES,
    MANIFEST as AVIONICS_MANIFEST, SOURCE_REVISION, package_avionics_provider,
)
from validate_aircraft import cockpit_device_quads, validate, validate_native_panel

MFD_CAPT, MFD_FO = 'a380x.xp12.mfd.captain', 'a380x.xp12.mfd.first_officer'
MFD_TOUCH_BLOCK = (
    'ATTR_draw_disable\n'
    f'ATTR_cockpit_device\t{MFD_CAPT}\t1\t14\t1\nATTR_manip_device\tarrow\t{MFD_CAPT}\tCAPT MFD\n'
    'TRIS\t66\t6\nATTR_manip_none\nATTR_no_cockpit\n'
    f'ATTR_cockpit_device\t{MFD_FO}\t1\t15\t1\nATTR_manip_device\tarrow\t{MFD_FO}\tF/O MFD\n'
    'TRIS\t72\t6\nATTR_manip_none\nATTR_no_cockpit\n'
    'ATTR_draw_enable\n'
)


class DevelopmentBuildTests(unittest.TestCase):
    @staticmethod
    def write_png(path: Path, width: int, height: int) -> None:
        def chunk(kind, data):
            return (struct.pack('>I', len(data)) + kind + data
                    + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(
            b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IEND', b'')
        )

    def make_native_panel_package(self, root: Path) -> tuple[str, list[str]]:
        self.write_png(root / 'cockpit_3d/-PANELS-/Panel.png', 2048, 2048)
        project = root / 'provider-source'
        for source, _packaged in AVIONICS_FILES[:-1]:
            path = project / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'native plugin fixture')
        report = project / AVIONICS_FILES[-1][0]
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps({
            'format': 1,
            'source_revision': SOURCE_REVISION,
            'logic_sources': ['FMADefinitions.ts', 'FwsFlightPhases.ts'],
            'stock_xplane_efis_modes_consumed': True,
            'display_targets': DISPLAY_TARGETS,
            'mock_test_result': 'a380x avionics + flyPad core tests passed',
        }))
        package_avionics_provider(root, project)
        groups = [
            '# A380X FBW AVIONICS PLUGIN OWNS PFD, ND, EWD, FCU AND EFB REGIONS',
            '# Stock X-Plane EFIS, map, FD, VNAV and AP mode instruments are intentionally absent.',
            'GROUP A380 FCU',
            '  LOCKED',
            'END_GROUP',
        ]
        acf_text = (
            'P acf/_ICAO A388\n'
            'P _obja/0/_obj_flags 2061\n'
            'P _obja/0/_v10_att_file_stl interior/cockpit-controls/controls.obj\n'
            'P _obja/1/_obj_flags 13\n'
            f'P _obja/1/_v10_att_file_stl {MFD_SCREENS_ATTACHMENT}\n'
            'PANEL_3D_BEGIN\n' + '\n'.join(groups) + '\nPANEL_3D_END\n'
        )
        # Visible MFD device screens exactly as build_cockpit_controls writes them.
        screens = root / 'objects' / MFD_SCREENS_ATTACHMENT
        screens.parent.mkdir(parents=True)
        screens.write_text(build_mfd_screens_obj())
        texture = root / 'textures/interior/cockpit-panels' / MFD_SCREEN_TEXTURE
        texture.parent.mkdir(parents=True)
        texture.write_bytes(mfd_screen_black_png())
        obj = root / 'objects/interior/cockpit-controls/controls.obj'
        obj.parent.mkdir(parents=True)
        uv = (
            (0.0, 448 / 2048), (320 / 2048, 448 / 2048),
            (320 / 2048, 32 / 2048), (0.0, 32 / 2048),
            (704 / 2048, 448 / 2048), (1024 / 2048, 448 / 2048),
            (1024 / 2048, 32 / 2048), (704 / 2048, 32 / 2048),
            (328 / 2048, 464 / 2048), (508 / 2048, 464 / 2048),
            (508 / 2048, 230 / 2048), (328 / 2048, 230 / 2048),
            (516 / 2048, 464 / 2048), (696 / 2048, 464 / 2048),
            (696 / 2048, 230 / 2048), (516 / 2048, 230 / 2048),
            (152 / 2048, 1016 / 2048), (872 / 2048, 1016 / 2048),
            (872 / 2048, 516 / 2048), (152 / 2048, 516 / 2048),
            (152 / 2048, 1016 / 2048), (872 / 2048, 1016 / 2048),
            (872 / 2048, 516 / 2048), (152 / 2048, 516 / 2048),
        )
        uv += tuple(point for i in range(2) for point in (
            ((1024+(i+1)*512)/2048, 1088/2048), ((1024+i*512)/2048, 1088/2048),
            ((1024+i*512)/2048, 1216/2048), ((1024+(i+1)*512)/2048, 1216/2048),
        ))
        # Retain the historical unused slots so EWD/MFD fixture IDs stay fixed.
        uv += ((0.0, 0.0),) * 8
        uv += ((1024 / 2048, 1024 / 2048), (1792 / 2048, 1024 / 2048),
               (1792 / 2048, 0.0), (1024 / 2048, 0.0))
        # Two MFD device touch quads, BL/TL/TR/BR with the full device UVs.
        uv += ((0.0, 0.0), (0.0, 1.0), (1.0, 1.0), (1.0, 0.0)) * 2
        indices = (
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7,
            8, 9, 10, 8, 10, 11,
            12, 13, 14, 12, 14, 15,
            16, 17, 18, 16, 18, 19,
            20, 21, 22, 20, 22, 23,
        )
        indices += tuple(vertex for base in range(24, 40, 4)
                         for vertex in (base, base + 1, base + 2, base, base + 2, base + 3))
        indices += (40, 41, 42, 40, 42, 43)
        indices += (44, 45, 46, 44, 46, 47, 48, 49, 50, 48, 50, 51)
        touch_vertices = {44+side*4+corner: point
                          for side, _device, points in mfd_device_quads(MFD_TOUCH_LIFT)
                          for corner, point in enumerate(points)}
        obj.write_text(
            'I\n800\nOBJ\n# A380X FLYPAD ORIENTATION: UV0 TOUCH0 A380_CONTROL_UI_V3\n'
            'COCKPIT_REGION 0 0 2048 2048\n'
            'POINT_COUNTS 52 0 0 78\n'
            + ''.join(
                'VT\t'+'\t'.join(f'{value:.9g}' for value in touch_vertices.get(index, (index,0,0,0,0,1,u,v)))+'\n'
                for index, (u, v) in enumerate(uv)
            )
            + ''.join(f'IDX\t{index}\n' for index in indices)
            + 'ATTR_cockpit_region 0\nTRIS 0 6\nTRIS 6 6\nTRIS 12 6\nTRIS 18 6\n'
            + 'TRIS 24 6\nTRIS 30 6\n'
            + 'TRIS 36 6\nTRIS 42 6\n'
            + 'TRIS 60 6\n'
            + 'ATTR_no_cockpit\n'
            + MFD_TOUCH_BLOCK
        )
        return acf_text, ['interior/cockpit-controls/controls.obj', MFD_SCREENS_ATTACHMENT]

    def test_custom_panel_is_transparent_rgba_png(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_empty_2d_panel(root)
            png = (root / 'cockpit/-PANELS-/Panel.png').read_bytes()
            self.assertEqual(png[:8], b'\x89PNG\r\n\x1a\n')
            self.assertEqual(struct.unpack('>2I5B', png[16:29]), (1024, 1024, 8, 6, 0, 0, 0))
            offset, compressed = 8, b''
            while offset < len(png):
                length = struct.unpack('>I', png[offset:offset + 4])[0]
                kind = png[offset + 4:offset + 8]
                data = png[offset + 8:offset + 8 + length]
                if kind == b'IDAT':
                    compressed += data
                offset += length + 12
            self.assertEqual(zlib.decompress(compressed), b'\0' * (1024 * (1 + 1024 * 4)))

    def test_source_cg_and_initial_ground_height(self):
        values = build_replacements()
        self.assertAlmostEqual(float(values['acf/_cgY']), 2.8)
        self.assertAlmostEqual(float(values['acf/_cgZ']), 99.108)
        self.assertAlmostEqual(float(values['acf/_h_eqlbm']), 20.75)
        self.assertAlmostEqual(float(values['acf/_the_eqlbm']), 0.0)
        self.assertEqual(values['acf/_use_cus_gear_defs'], '0')

    def test_airliner_parking_brake_uses_hydraulic_valve(self):
        values = build_replacements()
        self.assertEqual(values['acf/_hyd_park_brake_trap'], '1')
        self.assertEqual(values['acf/_hyd_park_brake_toe'], '0')

    def test_a380_gear_layout_uses_six_wheel_body_bogies(self):
        values = build_replacements()
        self.assertEqual([values[f'_gear/{index}/_gear_type'] for index in range(5)],
                         ['3', '5', '5', '6', '6'])
        for index in range(5):
            self.assertAlmostEqual(float(values[f'_gear/{index}/_eagle_claw_deg']), 0.0)
            self.assertAlmostEqual(float(values[f'_gear/{index}/_latE']), 0.0)
            self.assertAlmostEqual(float(values[f'_gear/{index}/_lonE']), 0.0)

    def test_ground_contact_calibration_offsets_measured_tire_sag(self):
        values = build_replacements()
        contacts = [float(values[f'_gear/{index}/_gear_y'])
                    - float(values[f'_gear/{index}/_leg_len'])
                    - float(values[f'_gear/{index}/_tire_radius'])
                    for index in range(5)]
        self.assertAlmostEqual(contacts[0], -17.95)
        self.assertAlmostEqual(contacts[1], -15.63)
        self.assertAlmostEqual(contacts[2], -15.63)
        self.assertAlmostEqual(contacts[3], -16.17)
        self.assertAlmostEqual(contacts[4], -16.17)
        # The old stationary probe measured 0.133 m average wing-tire sag
        # and 0.297 m body-tire sag. The calibrated body gear should no
        # longer impose the earlier ~2-degree pitch from that difference.
        ft_to_m = 0.3048
        loaded_wing = contacts[1] * ft_to_m + 0.133
        loaded_body = contacts[3] * ft_to_m + 0.297
        self.assertLess(abs(loaded_body - loaded_wing), 0.04)

    def test_integer_serialization(self):
        values = {}
        set_value(values, 'flag', 1)
        set_value(values, 'number', 1.25)
        self.assertEqual(values, {'flag': '1', 'number': '1.250000000'})

    def test_portable_interior_texture_headers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'panel.png').touch()
            (root / 'normal.png').touch()
            source = root / 'source.obj'
            source.write_text('TEXTURE /build/textures/panel.png\nTEXTURE_MAP normal /build/normal.png\n')
            destination = root / 'package' / 'part.obj'
            refs = copy_exterior_obj(source, destination, root, 'cockpit', 'interior')
            self.assertEqual(refs, {'panel.png', 'normal.png'})
            self.assertIn('../../../textures/interior/cockpit/panel.png', destination.read_text())

    def test_missing_texture_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'part.obj'
            source.write_text('TEXTURE missing.png\n')
            with self.assertRaises(FileNotFoundError):
                copy_exterior_obj(source, root / 'copy.obj', root, 'cockpit', 'interior')

    def test_focused_refresh_ignores_unselected_missing_interior_group(self):
        missing = Path('/definitely/not/present/a380-cabin-export')
        self.assertEqual(interior_sources('cabin', missing, ['cockpit-controls']), [])

    def test_focused_refresh_replaces_only_selected_attachment_group(self):
        existing = [
            'interior/cockpit/shell.obj',
            'interior/cabin/cabin-old.obj',
            'interior/cabin-seats/seats.obj',
        ]
        merged = merge_refreshed_paths(
            existing,
            {'cabin': ['interior/cabin/cabin-new.obj']},
            'interior',
        )
        self.assertEqual(merged, [
            'interior/cockpit/shell.obj',
            'interior/cabin/cabin-new.obj',
            'interior/cabin-seats/seats.obj',
        ])

    def test_validator_accepts_packaged_bilateral_native_pfds(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            acf_text, attachments = self.make_native_panel_package(root)
            errors, report = validate_native_panel(root, acf_text, attachments)
            self.assertEqual(errors, [])
            self.assertEqual(report['panel_size'], [2048, 2048])
            self.assertEqual(report['pfd_groups'], ['captain', 'first_officer'])
            self.assertEqual(report['nd_groups'], ['captain', 'first_officer'])
            self.assertEqual(report['cockpit_region_pfd_quads'], 2)
            self.assertEqual(report['cockpit_region_nd_quads'], 2)
            self.assertEqual(report['cockpit_region_ewd_quads'], 1)
            self.assertEqual(report['cockpit_region_efb_quads'], 2)
            self.assertEqual(report['efb_orientation'], 'UV0 TOUCH0 A380_CONTROL_UI_V3')
            self.assertEqual(report['cockpit_device_quads'], {MFD_CAPT: 1, MFD_FO: 1})
            self.assertEqual(report['cockpit_device_touch_quads'], {MFD_CAPT: 1, MFD_FO: 1})

    def test_validator_rejects_wrong_panel_size_and_missing_fo_quad(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            acf_text, attachments = self.make_native_panel_package(root)
            self.write_png(root / 'cockpit_3d/-PANELS-/Panel.png', 1024, 512)
            controls = root / 'objects/interior/cockpit-controls/controls.obj'
            controls.write_text(controls.read_text().replace('TRIS 6 6\n', ''))
            errors, report = validate_native_panel(root, acf_text, attachments)
            self.assertTrue(any('2048x2048' in error for error in errors))
            self.assertTrue(any('captain and first-officer' in error for error in errors))
            self.assertEqual(report['cockpit_region_pfd_quads'], 1)

    def test_validator_rejects_bad_indices(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'objects').mkdir()
            (root / 'A380X-XP12.acf').write_text('P _obja/0/_v10_att_file_stl bad.obj\n')
            (root / 'objects/bad.obj').write_text(
                'I\n800\nOBJ\nPOINT_COUNTS 1 0 0 3\nVT 0 0 0 0 1 0 0 0\nIDX 0\nIDX 9\nIDX 0\nTRIS 0 3\n')
            result = validate(root)
            self.assertFalse(result['passed'])
            self.assertTrue(any('vertex index' in e for e in result['errors']))

    def test_validator_rejects_short_idx10_record(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'objects').mkdir()
            (root / 'A380X-XP12.acf').write_text('P _obja/0/_v10_att_file_stl bad.obj\n')
            (root / 'objects/bad.obj').write_text(
                'I\n800\nOBJ\nPOINT_COUNTS 3 0 0 3\n'
                'VT 0 0 0 0 0 1 0 0\nVT 1 0 0 0 0 1 0 0\nVT 0 1 0 0 0 1 0 0\n'
                'IDX10 0 2 1\nTRIS 0 3\n'
            )
            errors = validate(root)['errors']
            self.assertTrue(any('malformed IDX10 record' in error for error in errors))

    def test_validator_accepts_static_transform_without_dataref(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'objects').mkdir()
            (root / 'A380X-XP12.acf').write_text('P _obja/0/_v10_att_file_stl empty.obj\n')
            (root / 'objects/empty.obj').write_text(
                'I\n800\nOBJ\nPOINT_COUNTS 0 0 0 0\nANIM_begin\nANIM_rotate 0 1 0 20 20 0 0\nANIM_end\n')
            (root / 'datarefs.txt').write_text('sim/test float n\n')
            self.assertTrue(validate(root, root / 'datarefs.txt')['passed'])

    def test_validator_rejects_reversed_winding(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'objects').mkdir()
            (root / 'A380X-XP12.acf').write_text('P _obja/0/_v10_att_file_stl face.obj\n')
            obj = root / 'objects/face.obj'
            header = ('I\n800\nOBJ\nPOINT_COUNTS 3 0 0 300\n'
                      'VT 0 0 0 0 0 1 0 0\nVT 1 0 0 0 0 1 0 0\nVT 0 1 0 0 0 1 0 0\n')
            obj.write_text(header + 'IDX 0\nIDX 1\nIDX 2\n' * 100 + 'TRIS 0 300\n')
            self.assertTrue(any('backwards' in error for error in validate(root)['errors']))
            obj.write_text(header + 'IDX 0\nIDX 2\nIDX 1\n' * 100 + 'TRIS 0 300\n')
            self.assertTrue(validate(root)['passed'])

    def test_validator_rejects_descending_animation_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'objects').mkdir()
            (root / 'A380X-XP12.acf').write_text('P _obja/0/_v10_att_file_stl trim.obj\n')
            obj = root / 'objects/trim.obj'
            header = 'I\n800\nOBJ\nPOINT_COUNTS 0 0 0 0\nANIM_begin\nANIM_rotate_begin 1 0 0 sim/test\n'
            end = 'ANIM_rotate_end\nANIM_end\n'
            obj.write_text(header + 'ANIM_rotate_key 1 20\nANIM_rotate_key -1 -20\n' + end)
            self.assertTrue(any('low-to-high' in error for error in validate(root)['errors']))
            obj.write_text(header + 'ANIM_rotate_key -1 -20\nANIM_rotate_key 1 20\n' + end)
            self.assertTrue(validate(root)['passed'])

    def test_validator_requires_cockpit_flag_and_known_command_for_manipulator(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'objects').mkdir()
            acf = root / 'A380X-XP12.acf'
            acf.write_text(
                'P _obja/0/_obj_flags 13\n'
                'P _obja/0/_v10_att_file_stl controls.obj\n'
            )
            (root / 'objects/controls.obj').write_text(
                'I\n800\nOBJ\nPOINT_COUNTS 3 0 0 3\n'
                'VT 0 0 0 0 0 1 0 0\nVT 1 0 0 0 0 1 0 0\nVT 0 1 0 0 0 1 0 0\n'
                'IDX 0\nIDX 2\nIDX 1\nATTR_draw_disable\n'
                'ATTR_manip_command hand sim/test/button TEST BUTTON\nTRIS 0 3\n'
            )
            commands = root / 'Commands.txt'
            commands.write_text('sim/test/other Other command\n')
            errors = validate(root, commands=commands)['errors']
            self.assertTrue(any('cockpit-object flag' in error for error in errors))
            self.assertTrue(any('not in native catalog' in error for error in errors))
            acf.write_text(
                'P _obja/0/_obj_flags 2061\n'
                'P _obja/0/_v10_att_file_stl controls.obj\n'
            )
            commands.write_text('sim/test/button Test button\n')
            self.assertTrue(validate(root, commands=commands)['passed'])

    def test_validator_checks_drag_axis_manipulator_dataref(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'objects').mkdir()
            (root / 'A380X-XP12.acf').write_text(
                'P _obja/0/_obj_flags 2061\n'
                'P _obja/0/_v10_att_file_stl controls.obj\n'
            )
            (root / 'objects/controls.obj').write_text(
                'I\n800\nOBJ\nPOINT_COUNTS 3 0 0 3\n'
                'VT 0 0 0 0 0 1 0 0\nVT 1 0 0 0 0 1 0 0\nVT 0 1 0 0 0 1 0 0\n'
                'IDX 0\nIDX 2\nIDX 1\nATTR_draw_disable\n'
                'ATTR_manip_drag_axis hand 0 0 -0.2 0 1 sim/test/throttle[0] THROTTLE\nTRIS 0 3\n'
            )
            datarefs = root / 'DataRefs.txt'
            datarefs.write_text('sim/test/other float y ratio\n')
            errors = validate(root, datarefs=datarefs)['errors']
            self.assertTrue(any('manipulator dataref not in native catalog' in error for error in errors))
            datarefs.write_text('sim/test/throttle float[4] y ratio\n')
            self.assertTrue(validate(root, datarefs=datarefs)['passed'])

    def test_validator_checks_both_command_knob_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'objects').mkdir()
            (root / 'A380X-XP12.acf').write_text(
                'P _obja/0/_obj_flags 2061\n'
                'P _obja/0/_v10_att_file_stl controls.obj\n'
            )
            (root / 'objects/controls.obj').write_text(
                'I\n800\nOBJ\nPOINT_COUNTS 3 0 0 3\n'
                'VT 0 0 0 0 0 1 0 0\nVT 1 0 0 0 0 1 0 0\nVT 0 1 0 0 0 1 0 0\n'
                'IDX 0\nIDX 2\nIDX 1\nATTR_draw_disable\n'
                'ATTR_manip_command_knob rotate_medium sim/test/up sim/test/down TEST KNOB\nTRIS 0 3\n'
            )
            commands = root / 'Commands.txt'
            commands.write_text('sim/test/up Up command\n')
            errors = validate(root, commands=commands)['errors']
            self.assertTrue(any('sim/test/down' in error for error in errors))
            commands.write_text('sim/test/up Up command\nsim/test/down Down command\n')
            self.assertTrue(validate(root, commands=commands)['passed'])

    def test_native_fcu_commands_and_annunciator_datarefs_require_verified_provider(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            acf, paths = self.make_native_panel_package(root)
            (root / 'A380X-XP12.acf').write_text(acf)
            obj = root / 'objects' / paths[0]
            obj.write_text(obj.read_text() + (
                'ATTR_draw_disable\n'
                f'ATTR_manip_command_knob rotate_medium {FCU_COMMANDS[0]} {FCU_COMMANDS[1]} SPEED\n'
                'TRIS 0 6\nATTR_manip_none\nATTR_draw_enable\n'
                'ANIM_begin\nANIM_show 1 1 a380x/xp12/fcu/ap1_active\nTRIS 0 6\nANIM_end\n'
            ))
            catalog = root / 'empty-catalog.txt'
            catalog.write_text('')
            result = validate(root, datarefs=catalog, commands=catalog)
            self.assertEqual(result['errors'], [])
            manifest_path = root / AVIONICS_MANIFEST
            manifest = json.loads(manifest_path.read_text())
            self.assertEqual(tuple(manifest['datarefs']), AVIONICS_DATAREFS)
            manifest['commands'].remove('a380x/xp12/fcu/speed_up')
            manifest_path.write_text(json.dumps(manifest))
            result = validate(root, datarefs=catalog, commands=catalog)
            self.assertTrue(any('no packaged provider' in error and 'speed_up' in error
                                for error in result['errors']))
            self.assertTrue(any('verified provider' in error and 'ap1_active' in error
                                for error in result['errors']))

    def test_native_fcu_rejects_stock_instruments_and_stock_control_routes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            acf, paths = self.make_native_panel_package(root)
            errors, report = validate_native_panel(root, acf, paths)
            self.assertEqual(errors, [])
            self.assertEqual(report['cockpit_region_fcu_quads'], 2)
            errors, _report = validate_native_panel(
                root, acf.replace('END_GROUP', 'gen_LED Speed\nDATAREF sim/cockpit2/autopilot/airspeed_dial_kts\nEND_GROUP'), paths)
            self.assertTrue(any('forbidden stock' in error for error in errors))
            obj = root / 'objects' / paths[0]
            obj.write_text(obj.read_text() + 'ATTR_manip_command hand sim/autopilot/servos_toggle AP\n')
            errors, _report = validate_native_panel(root, acf, paths)
            self.assertTrue(any('stock autopilot route' in error for error in errors))

    def test_native_fcu_requires_both_shared_display_regions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            acf, paths = self.make_native_panel_package(root)
            obj = root / 'objects' / paths[0]
            obj.write_text(obj.read_text().replace('TRIS 42 6\n', ''))
            errors, report = validate_native_panel(root, acf, paths)
            self.assertEqual(report['cockpit_region_fcu_quads'], 1)
            self.assertTrue(any('two shared native FCU' in error for error in errors))


    def write_package_acf(self, root: Path) -> tuple[str, list[str]]:
        acf, paths = self.make_native_panel_package(root)
        (root / 'A380X-XP12.acf').write_text(acf)
        return acf, paths

    def test_validator_accepts_mfd_device_screens_and_touch_quads(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_package_acf(root)
            catalog = root / 'empty-catalog.txt'
            catalog.write_text('')
            result = validate(root, datarefs=catalog, commands=catalog)
            self.assertEqual(result['errors'], [])
            objects = {item['path']: item for item in result['objects']}
            self.assertEqual(objects['interior/cockpit-controls/controls.obj']['manipulator_devices'],
                             [MFD_CAPT, MFD_FO])
            self.assertEqual(objects[MFD_SCREENS_ATTACHMENT]['manipulator_devices'], [])
            self.assertEqual(objects[MFD_SCREENS_ATTACHMENT]['textures'],
                             ['../../../textures/interior/cockpit-panels/' + MFD_SCREEN_TEXTURE])
            self.assertEqual(result['native_panel']['cockpit_device_touch_quads'], {MFD_CAPT: 1, MFD_FO: 1})

    def test_cockpit_device_quads_track_device_and_manipulator_scope(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _acf, paths = self.make_native_panel_package(root)
            controls = (root / 'objects' / paths[0]).read_text()
            full = [(0.0, 0.0, 1.0, 1.0)]
            self.assertEqual(cockpit_device_quads(controls, True), {MFD_CAPT: full, MFD_FO: full})
            # Cockpit-region panel quads never count as device quads.
            self.assertEqual(set(cockpit_device_quads(controls)), {MFD_CAPT, MFD_FO})
            screens = (root / 'objects' / paths[1]).read_text()
            self.assertEqual(cockpit_device_quads(screens), {MFD_CAPT: full, MFD_FO: full})
            self.assertEqual(cockpit_device_quads(screens, True), {})

    def test_validator_rejects_missing_fo_device_touch_quad(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            acf, paths = self.make_native_panel_package(root)
            obj = root / 'objects' / paths[0]
            obj.write_text(obj.read_text().replace(
                f'ATTR_cockpit_device\t{MFD_FO}\t1\t15\t1\nATTR_manip_device\tarrow\t{MFD_FO}\tF/O MFD\n'
                'TRIS\t72\t6\nATTR_manip_none\nATTR_no_cockpit\n', ''))
            errors, report = validate_native_panel(root, acf, paths)
            self.assertEqual(report['cockpit_device_touch_quads'], {MFD_CAPT: 1, MFD_FO: 0})
            self.assertTrue(any('touch quad for MFD device ' + MFD_FO in error and 'found 0' in error
                                for error in errors), errors)

    def test_validator_rejects_mismatched_mfd_device_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            acf, paths = self.write_package_acf(root)
            obj = root / 'objects' / paths[0]
            obj.write_text(obj.read_text().replace(MFD_FO, 'a380x.xp12.mfd.fo'))
            catalog = root / 'empty-catalog.txt'
            catalog.write_text('')
            errors = validate(root, datarefs=catalog, commands=catalog)['errors']
            self.assertTrue(any('touch quad for MFD device ' + MFD_FO in error for error in errors), errors)
            self.assertTrue(any('unexpected cockpit devices: a380x.xp12.mfd.fo' in error for error in errors))
            self.assertTrue(any('no packaged provider device: a380x.xp12.mfd.fo' in error for error in errors))
            # The visible object must use the same IDs, bus, channel and auto values.
            obj.write_text(obj.read_text().replace('a380x.xp12.mfd.fo', MFD_FO))
            screens = root / 'objects' / paths[1]
            screens.write_text(screens.read_text().replace(f'{MFD_FO}\t1\t15\t1', f'{MFD_FO}\t1\t14\t1'))
            errors, _report = validate_native_panel(root, acf, paths)
            self.assertTrue(any('bus/channel/auto' in error for error in errors), errors)
            # The provider manifest must publish exactly the same device IDs.
            screens.write_text(build_mfd_screens_obj())
            manifest_path = root / AVIONICS_MANIFEST
            manifest = json.loads(manifest_path.read_text())
            manifest['cockpit_devices'] = [MFD_CAPT]
            manifest_path.write_text(json.dumps(manifest))
            errors, _report = validate_native_panel(root, acf, paths)
            self.assertTrue(any('MFD cockpit devices' in error for error in errors), errors)

    def test_validator_rejects_device_manipulator_without_device_tag(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            acf, paths = self.write_package_acf(root)
            obj = root / 'objects' / paths[0]
            obj.write_text(obj.read_text().replace(f'ATTR_cockpit_device\t{MFD_CAPT}\t1\t14\t1\n', ''))
            errors = validate(root)['errors']
            self.assertTrue(any(f'ATTR_manip_device {MFD_CAPT} is not preceded by a matching '
                                'ATTR_cockpit_device' in error for error in errors), errors)
            self.assertTrue(any('touch quad for MFD device ' + MFD_CAPT in error for error in errors))
            obj.write_text(obj.read_text().replace(f'ATTR_cockpit_device\t{MFD_FO}\t1\t15\t1',
                                                   f'ATTR_cockpit_device\t{MFD_FO}\t1\t15'))
            self.assertTrue(any('malformed ATTR_cockpit_device' in error for error in validate(root)['errors']))

    def test_validator_requires_visible_mfd_screen_attachment(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            acf, paths = self.make_native_panel_package(root)
            errors, report = validate_native_panel(root, acf, paths[:1])
            self.assertTrue(any('no visible MFD screen attachment' in error
                                and 'cockpit-controls refresh' in error for error in errors), errors)
            self.assertEqual(report['cockpit_device_quads'], {})
            anchored = ['interior/cockpit-panels/screen_powered_off_display_faces.obj']
            errors, _report = validate_native_panel(root, acf, [paths[0], paths[1], *anchored])
            self.assertTrue(any('must be attached after' in error for error in errors), errors)
            (root / 'objects' / paths[1]).write_text(
                build_mfd_screens_obj().replace('ATTR_poly_os\t0', 'ATTR_manip_none\nATTR_poly_os\t0'))
            errors, _report = validate_native_panel(root, acf, paths)
            self.assertTrue(any('must not carry manipulators' in error for error in errors), errors)

    def test_mfd_screen_object_preflight_requires_its_package_texture(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            obj = root / 'a380x_mfd_screens.obj'
            obj.write_text(build_mfd_screens_obj())
            with self.assertRaises(FileNotFoundError):
                mfd_screens_texture(obj)
            (root / MFD_SCREEN_TEXTURE).write_bytes(mfd_screen_black_png())
            self.assertEqual(mfd_screens_texture(obj), root / MFD_SCREEN_TEXTURE)
            obj.write_text(build_mfd_screens_obj().replace('textures/interior/cockpit-panels/',
                                                           'textures/interior/cockpit-controls/'))
            with self.assertRaisesRegex(ValueError, 'one TEXTURE'):
                mfd_screens_texture(obj)
            renamed = root / 'screens.obj'
            renamed.write_text(build_mfd_screens_obj())
            with self.assertRaisesRegex(ValueError, 'must be named'):
                mfd_screens_texture(renamed)


if __name__ == '__main__':
    unittest.main()
