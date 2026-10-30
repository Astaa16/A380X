"""Run with: python3 -m unittest discover -s tools -p test_patch_cockpit_materials.py"""
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image

import patch_cockpit_materials as p


OBJ = """I
800
OBJ
TEXTURE old.png
POINT_COUNTS 3 0 0 3
VT 0 0 0 0 1 0 0 0
VT 1 0 0 0 1 0 1 0
VT 0 0 1 0 1 0 0 1
IDX 0
IDX 1
IDX 2
ANIM_begin
ANIM_rotate 0 1 0 0 10 0 1 test/knob
ATTR_shiny_rat 0.5
ATTR_poly_os 1
ATTR_blend
TRIS 0 3
ANIM_end
"""
TEXTURES = {"TEXTURE": "new.png", "TEXTURE_LIT": "new_LIT.png",
            "TEXTURE_MAP normal": "normal.png", "TEXTURE_MAP material_gloss": "gloss.png"}


class PatchTests(unittest.TestCase):
    def test_fcu_refresh_retains_authored_material_detail_and_unit_master(self):
        class Source:
            def load(self, spec):
                return Image.new('RGBA',(8,8),(80,64,128,255))
        mat={'name':'A380_COCKPIT_FCU_4k','pbrMetallicRoughness':{
            'baseColorTexture':{'index':0},'metallicRoughnessTexture':{'index':1}}}
        self.assertEqual(next(kind for _,name,kind in p.TARGETS if name==mat['name']),'fcu')
        image=p.make_materials(Source(),mat,'fcu')['material_gloss']
        self.assertEqual(image.size,(8,8))
        self.assertEqual(image.getpixel((0,0)),(128,191,0,255))
        self.assertIn('ATTR_shiny_rat 1.0\n',p.patch_obj(OBJ,TEXTURES,'fcu',None))

    def test_mapping_animation_and_repeated_patch(self):
        for kind in ("labels", "mip", "matte"):
            with self.subTest(kind=kind):
                result = p.patch_obj(OBJ, TEXTURES, kind, "test/light" if kind == "labels" else None)
                self.assertEqual(p.inspect_obj(OBJ)["geometry_animation_sha256"],
                                 p.inspect_obj(result)["geometry_animation_sha256"])
                self.assertEqual(result, p.patch_obj(result, TEXTURES, kind, "test/light" if kind == "labels" else None))
                self.assertEqual(result.count("NORMAL_METALNESS"), 1)
                if kind == "labels":
                    self.assertIn("ATTR_poly_os 0\n", result)
                    self.assertNotIn("ATTR_poly_os 1\n", result)

    def test_rejects_screen_regions_and_corrupt_geometry(self):
        for invalid in (OBJ.replace("TRIS", "ATTR_cockpit_region 0\nTRIS"),
                        OBJ.replace("IDX 2", "IDX 4"), OBJ.replace("ANIM_end", "")):
            with self.assertRaises(ValueError):
                p.patch_obj(invalid, TEXTURES, "labels", None)

    def test_apply_noop_and_restore(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "panel.obj").write_bytes(b"original")
            plan = {Path("panel.obj"): b"updated", Path("textures/atlas.png"): b"asset"}
            manifest = p.install(root, plan)
            self.assertIsNotNone(manifest)
            self.assertIsNone(p.install(root, plan))
            p.restore(manifest)
            self.assertEqual((root / "panel.obj").read_bytes(), b"original")
            self.assertFalse((root / "textures/atlas.png").exists())

    def test_restore_refuses_user_changes_before_any_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest = p.install(root, {Path("a"): b"one", Path("b"): b"two"})
            (root / "b").write_bytes(b"user edit")
            with self.assertRaises(ValueError):
                p.restore(manifest)
            self.assertEqual((root / "a").read_bytes(), b"one")

    def test_partial_install_rolls_back(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "a").write_bytes(b"original")
            writer = p.atomic_write
            def fail_one(path, data):
                if path == root / "b":
                    raise OSError("simulated write error")
                writer(path, data)
            with patch.object(p, "atomic_write", side_effect=fail_one):
                with self.assertRaises(OSError):
                    p.install(root, {Path("a"): b"changed", Path("b"): b"new"})
            self.assertEqual((root / "a").read_bytes(), b"original")
            self.assertFalse((root / "b").exists())

    def test_source_paint_alpha_lit_and_pbr_conversion(self):
        class Source:
            def load(self, spec):
                return {0: Image.new("RGBA", (4, 4), (50, 60, 70, 128)),
                        1: Image.new("RGBA", (4, 4), (255, 64, 0, 255))}[spec["index"]]
        mat = {"name": "test", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
               "metallicRoughnessTexture": {"index": 1}}, "emissiveTexture": {"index": 0}}
        matte = p.make_materials(Source(), mat, "matte")
        self.assertEqual(matte["albedo"].getpixel((0, 0)), (50, 60, 70, 128))
        self.assertEqual(matte["material_gloss"].getpixel((0, 0)), (0, 20, 0, 255))
        mip = p.make_materials(Source(), mat, "mip")
        self.assertEqual(mip["material_gloss"].getpixel((0, 0)), (10, 191, 0, 255))
        labels = p.make_materials(Source(), mat, "labels")
        self.assertEqual(labels["lit"].mode, "RGB")
        self.assertEqual(labels["lit"].getpixel((0, 0)), (25, 30, 35))


if __name__ == "__main__":
    unittest.main()
