import unittest
import errno
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch
from PIL import Image
from prepare_engine_materials import material_map, normal_map, patch_object, save_bytes


class EngineMaterialsTest(unittest.TestCase):
    def test_low_space_failure_keeps_original_and_backup(self):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / 'normal.png'
            target.write_bytes(b'original texture')
            writer = Path.write_bytes
            def limited_write(path, data):
                if path.name.endswith('.engine-tmp'):
                    raise OSError(errno.ENOSPC, 'full build image')
                return writer(path, data)
            with patch.object(Path, 'write_bytes', limited_write):
                with self.assertRaises(OSError):
                    save_bytes(target, b'smaller', root / 'backups', [])
            self.assertEqual(target.read_bytes(), b'original texture')
            backups = list((root / 'backups').iterdir())
            self.assertEqual(len(backups), 1)
            self.assertEqual(backups[0].read_bytes(), b'original texture')

    def test_normal_convention_and_bounded_painted_surface_relief(self):
        image = Image.new('RGB', (3, 1))
        image.putdata([(0, 255, 128), (128, 127, 128), (255, 0, 128)])
        self.assertEqual(list(normal_map(image).getdata()),
                         [(0, 0, 128), (128, 128, 128), (255, 255, 128)])
        self.assertEqual(list(normal_map(image, .3).getdata()),
                         [(89, 89, 128), (128, 128, 128), (166, 166, 128)])
        with self.assertRaises(ValueError):
            normal_map(image, 2)

    def test_dielectric_floor_and_independent_roughness(self):
        image = Image.new('RGBA', (3, 1))
        image.putdata([(99, 0, 0, 10), (45, 128, 128, 0), (0, 255, 255, 255)])
        self.assertEqual(list(material_map(image).getdata()),
                         [(10, 255, 0, 255), (128, 127, 0, 255), (255, 0, 0, 255)])

    def test_material_patch_preserves_motion_and_geometry(self):
        text = ('I\n800\nOBJ\nTEXTURE paint.png\nTEXTURE_MAP normal\tnormal.png\n'
                'TEXTURE_MAP material_gloss old.png\nVT\t1\t2\t3\nIDX 0\n'
                'ANIM_begin\nANIM_rotate 0 0 1 0 360 0 360 fan\n'
                '\tATTR_shiny_rat\t0.5\nTRIS 0 3\nANIM_end\n')
        changed = patch_object(text, '../gloss.png')
        self.assertIn('NORMAL_METALNESS\nGLOBAL_specular 1\n', changed)
        self.assertIn('\tATTR_shiny_rat\t1\n', changed)
        self.assertEqual(changed, patch_object(changed, '../gloss.png'))
        keep = lambda t: [x for x in t.splitlines() if x.startswith(('VT', 'IDX', 'ANIM', 'TRIS', 'TEXTURE paint'))]
        self.assertEqual(keep(text), keep(changed))


if __name__ == '__main__':
    unittest.main()
