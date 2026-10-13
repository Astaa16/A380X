import tempfile
import unittest
from pathlib import Path

from PIL import Image

from build_fcu_panel import (
    EFB_PANEL_RECT,
    EFIS_BARO_PANEL_RECTS,
    EFIS_UPPER_PANEL_RECTS,
    EFIS_LOWER_PANEL_RECTS,
    EWD_PANEL_RECT,
    FCU_PANEL_RECT,
    FCU_SCREEN_HEIGHT,
    ND_PANEL_RECTS,
    PANEL_SIZE,
    PFD_PANEL_RECTS,
    build_label_atlas,
    build_panel_background,
    build_panel_definition,
)

from build_cockpit_controls import (FCU_ANNUNCIATORS, FCU_ANNUNCIATOR_FIRST_CELL,
                                    FCU_LABEL_COLUMNS, FCU_LABEL_ROWS)


class FcuPanelTests(unittest.TestCase):
    def test_ewd_uses_full_fbw_canvas_without_moving_other_displays(self):
        self.assertEqual(PANEL_SIZE, (2048, 2048))
        self.assertEqual(EWD_PANEL_RECT, (1024, 0, 1792, 1024))
        self.assertEqual(FCU_PANEL_RECT, (0, 472, 1024, 512))
        self.assertEqual(PFD_PANEL_RECTS, ((0, 32, 320, 448), (704, 32, 1024, 448)))
        self.assertEqual(ND_PANEL_RECTS, ((328, 230, 508, 464), (516, 230, 696, 464)))
        self.assertEqual(EFB_PANEL_RECT, (152, 516, 872, 1016))

    def test_live_display_regions_are_isolated_and_inside_the_panel(self):
        rects = (*PFD_PANEL_RECTS, *ND_PANEL_RECTS, EWD_PANEL_RECT, EFB_PANEL_RECT,
                 *EFIS_BARO_PANEL_RECTS,
                 *(rect for side in EFIS_UPPER_PANEL_RECTS for rect in side),
                 *(rect for side in EFIS_LOWER_PANEL_RECTS for rect in side),
                 FCU_PANEL_RECT)
        for left, bottom, right, top in rects:
            self.assertLessEqual(0, left)
            self.assertLessEqual(0, bottom)
            self.assertLess(right, PANEL_SIZE[0] + 1)
            self.assertLess(top, PANEL_SIZE[1] + 1)
            self.assertLess(left, right)
            self.assertLess(bottom, top)
        for index, first in enumerate(rects):
            for second in rects[index + 1:]:
                horizontal_gap = first[2] <= second[0] or second[2] <= first[0]
                vertical_gap = first[3] <= second[1] or second[3] <= first[1]
                self.assertTrue(horizontal_gap or vertical_gap)

    def test_generated_panel_has_an_unpowered_fcu_render_bed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build_panel_background(root)
            build_label_atlas(root)
            with Image.open(root / '-PANELS-/Panel.png') as image:
                self.assertEqual(image.size, PANEL_SIZE)
                for rect in (*PFD_PANEL_RECTS, *ND_PANEL_RECTS, EWD_PANEL_RECT, EFB_PANEL_RECT):
                    left, bottom, _right, top = rect
                    self.assertEqual(image.getpixel((left + 10, PANEL_SIZE[1] - top + 10))[3], 255)
                self.assertEqual(image.getpixel((1, 100))[3], 0)
                self.assertEqual(image.getpixel((10, PANEL_SIZE[1] - 530)), (1, 3, 6, 255))
                self.assertEqual(image.getpixel((10, PANEL_SIZE[1] - 500)), (1, 3, 6, 255))
                self.assertEqual(image.getpixel((990, PANEL_SIZE[1] - 480)), (1, 3, 6, 255))
                # The FCU must stop at x=1024 rather than filling the new atlas width.
                self.assertEqual(image.getpixel((1900, PANEL_SIZE[1] - 480))[3], 0)
            with Image.open(root / 'fcu_labels.png') as image:
                self.assertEqual(image.size, (1024, 512))
            self.assertFalse((root / 'generic/pfd').exists())
            self.assertFalse((root / 'generic/fcu').exists())

    def test_annunciator_cells_are_green_transparent_and_identical_when_lit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build_label_atlas(root)
            with Image.open(root / 'fcu_labels.png') as day, Image.open(root / 'fcu_labels_LIT.png') as night:
                width, height = day.width // FCU_LABEL_COLUMNS, day.height // FCU_LABEL_ROWS
                for index in range(len(FCU_ANNUNCIATORS)):
                    cell = FCU_ANNUNCIATOR_FIRST_CELL + index
                    column, row = cell % FCU_LABEL_COLUMNS, cell // FCU_LABEL_COLUMNS
                    box = (column * width, row * height, (column + 1) * width, (row + 1) * height)
                    face = day.crop(box)
                    self.assertEqual(face.tobytes(), night.crop(box).tobytes())
                    pixels = list(face.getdata())
                    self.assertTrue(any(g > 200 and r < 120 and b < 140 and a == 255 for r, g, b, a in pixels))
                    self.assertEqual(face.getpixel((0, 0))[3], 0)
                    self.assertEqual(face.getpixel((width - 1, height - 1))[3], 0)

    def test_panel_definition_reserves_displays_for_fbw_plugin(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build_panel_definition(root)
            panel = (root / 'panel_3d.txt').read_text()
            self.assertIn('A380X FBW AVIONICS PLUGIN OWNS PFD, ND, EWD, FCU AND EFB REGIONS', panel)
            for forbidden in (
                'EFIS_horizon', 'EFIS_airspeed', 'EFIS_altitude', 'EFIS_vvi_vert',
                'EFIS_hsi_fullround', 'EFIS_map_s_GA',
                'sim/cockpit2/autopilot/', 'gen_LED',
            ):
                self.assertNotIn(forbidden, panel)
            self.assertNotIn('GROUP A380 PFD', panel)
            self.assertNotIn('GROUP A380 ND', panel)


if __name__ == '__main__':
    unittest.main()
