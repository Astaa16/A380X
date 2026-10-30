"""Protect authored FCU artwork/geometry while correcting technical PBR data."""
import math
import unittest

from PIL import Image
from prepare_fcu_materials import (F0_FLOOR, convert_comp, material_references,
                                   patch_master_gloss, protected_object_bytes)


OBJECT = '''I
800
OBJ
TEXTURE ../../../textures/interior/cockpit-panels/a380_cockpit_fcu_4k_albedo.png
TEXTURE_MAP normal ../../../textures/interior/cockpit-panels/a380_cockpit_fcu_4k_normal.png
TEXTURE_MAP material_gloss ../../../textures/interior/cockpit-panels/a380_cockpit_fcu_4k_material_gloss.png
NORMAL_METALNESS
POINT_COUNTS 3 0 0 3
VT .1 2 3 0 1 0 .25 .75
VT .2 2 3 0 1 0 .50 .75
VT .3 2 3 0 1 0 .75 .75
IDX 0
IDX 1
IDX 2
ATTR_shiny_rat 0.05 # retain this material comment
ANIM_begin
ANIM_trans 0 0 0 0 -.003 0 0 1 a380x/xp12/fcu/ap1_pressed
ATTR_manip_command button a380x/xp12/fcu/ap1_toggle Autopilot 1
TRIS 0 3
ANIM_end
'''


class FcuMaterialConversionTests(unittest.TestCase):
    def test_authored_roughness_and_metal_mask_with_dielectric_floor(self):
        image = Image.new('RGBA', (3, 1))
        image.putdata([(100, 44, 0, 0), (200, 181, 255, 31), (33, 128, 50, 255)])
        result = convert_comp(image)
        self.assertEqual(result.size, image.size)
        self.assertEqual(list(result.getdata()), [(F0_FLOOR, 211, 0, 255), (255, 74, 0, 255), (50, 127, 0, 255)])
        self.assertEqual(list(image.getdata()), [(100, 44, 0, 0), (200, 181, 255, 31), (33, 128, 50, 255)])

    def test_factors_are_linear_and_invalid_factors_rejected(self):
        image = Image.new('RGBA', (1, 1), (99, 128, 255, 99))
        self.assertEqual(convert_comp(image, .5, .5).getpixel((0, 0)), (128, 191, 0, 255))
        for value in (-.1, 1.1, math.nan, math.inf):
            with self.assertRaises(ValueError):
                convert_comp(image, value, 1)
            with self.assertRaises(ValueError):
                convert_comp(image, 1, value)


class FcuObjectPreservationTests(unittest.TestCase):
    def test_master_gloss_only_preserves_uv_animation_and_clicks(self):
        after = patch_master_gloss(OBJECT)
        self.assertEqual(after, OBJECT.replace('ATTR_shiny_rat 0.05', 'ATTR_shiny_rat 1.0'))
        self.assertEqual(protected_object_bytes(OBJECT), protected_object_bytes(after))
        self.assertEqual(material_references(OBJECT), material_references(after))
        self.assertEqual(patch_master_gloss(after), after)

    def test_crlf_and_existing_global_master_preserved(self):
        before = OBJECT.replace('NORMAL_METALNESS\n', 'NORMAL_METALNESS\nGLOBAL_specular .05\n').replace('\n', '\r\n')
        after = patch_master_gloss(before)
        self.assertEqual(after.count('\r\n'), before.count('\r\n'))
        self.assertIn('GLOBAL_specular 1.0\r\n', after)
        self.assertEqual(protected_object_bytes(before), protected_object_bytes(after))

    def test_ambiguous_or_incompatible_material_scopes_rejected(self):
        for before in (OBJECT + 'ATTR_shiny_rat .2\n', OBJECT + 'ATTR_reset\n',
                       OBJECT.replace('NORMAL_METALNESS\n', ''),
                       OBJECT.replace('ATTR_shiny_rat 0.05', 'ATTR_shiny_rat nan')):
            with self.assertRaises(ValueError):
                patch_master_gloss(before)
        with self.assertRaises(ValueError):
            material_references(OBJECT + 'TEXTURE_NORMAL legacy.png\n')


if __name__ == '__main__':
    unittest.main()
