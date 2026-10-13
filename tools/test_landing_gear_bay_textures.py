#!/usr/bin/env python3
"""Pure-Python guards for the landing-gear-bay material integration."""
import copy
from pathlib import Path
import tempfile
import unittest

PROJECT = Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0, str(PROJECT / "tools"))

from prepare_landing_gear_bay_textures import (
    BAY_MATERIALS,
    coverage,
    merge_exports,
    normalize_additions,
)


def source_fixture():
    return {
        "materials": [
            {
                "name": BAY_MATERIALS[0],
                "normalTexture": {"index": 0},
                "pbrMetallicRoughness": {
                    "baseColorTexture": {"index": 2},
                    "metallicRoughnessTexture": {"index": 1},
                },
            },
            {"name": BAY_MATERIALS[1]},
        ],
        "nodes": [
            {"name": "main-bay", "mesh": 0},
            {"name": "plain-bay", "mesh": 1},
        ],
        "meshes": [
            {"primitives": [{"material": 0, "indices": 0,
                              "extras": {"ASOBO_primitive": {"PrimitiveCount": 12}}}]},
            {"primitives": [{"material": 1, "indices": 1}]},
        ],
        "accessors": [{"count": 999}, {"count": 18}],
    }


def prepared(texture_dir: Path):
    return {
        "exports": [
            {
                "name": "generated-main",
                "materials": [BAY_MATERIALS[0]],
                "alpha_mode": "OPAQUE",
                "texture": str(texture_dir / "image-002.png"),
                "texture_map_normal": str(texture_dir / "image-000.png"),
                "texture_map_material_gloss": str(texture_dir / "bay-gloss.png"),
            },
            {
                "name": "generated-plain",
                "materials": [BAY_MATERIALS[1]],
                "alpha_mode": "OPAQUE",
                "texture": str(texture_dir / "plain.png"),
            },
        ],
    }


class LandingGearBayTextureTests(unittest.TestCase):
    def test_coverage_counts_draw_ranges_and_index_fallback(self):
        result = coverage(source_fixture())
        self.assertEqual(result[BAY_MATERIALS[0]]["triangles"], 12)
        self.assertEqual(result[BAY_MATERIALS[1]]["triangles"], 6)
        self.assertTrue(result[BAY_MATERIALS[0]]["has_albedo"])
        self.assertFalse(result[BAY_MATERIALS[1]]["has_albedo"])

    def test_missing_geometry_and_main_pbr_maps_are_rejected(self):
        for mutate in (
            lambda document: document["nodes"].pop(),
            lambda document: document["materials"][0].pop("normalTexture"),
            lambda document: document["materials"][1].update(alphaMode="BLEND"),
        ):
            document = source_fixture()
            mutate(document)
            with self.assertRaises(ValueError):
                coverage(document)

    def test_normalization_uses_stable_names_and_relative_texture_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = root / "manifests/fuselage-exports.json"
            texture_dir = root / "textures/fuselage"
            normalized = normalize_additions(prepared(texture_dir), manifest, texture_dir)
            self.assertEqual(
                [entry["name"] for entry in normalized["exports"]],
                ["a380x_lg_bay_main", "a380x_lg_bay"],
            )
            self.assertEqual(normalized["exports"][0]["texture"], "../textures/fuselage/image-002.png")

    def test_merge_is_idempotent_and_preserves_existing_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            additions = normalize_additions(
                prepared(root / "textures/fuselage"),
                root / "manifests/fuselage-exports.json",
                root / "textures/fuselage",
            )
            existing = {
                "exports": [{"name": "a380x_fuse1", "materials": ["A380X_FUSE1"]}],
                "metadata": {"keep": True},
            }
            original = copy.deepcopy(existing)
            merged = merge_exports(existing, additions)
            self.assertEqual(existing, original)
            self.assertEqual(merged["metadata"], existing["metadata"])
            self.assertEqual(len(merged["exports"]), 3)
            self.assertEqual(merge_exports(merged, additions), merged)

    def test_collisions_and_changed_existing_variant_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            additions = normalize_additions(
                prepared(root / "textures/fuselage"),
                root / "manifests/fuselage-exports.json",
                root / "textures/fuselage",
            )
            collision = {"exports": [{"name": "a380x_lg_bay", "materials": ["OTHER"]}]}
            with self.assertRaisesRegex(ValueError, "collides"):
                merge_exports(collision, additions)
            changed = copy.deepcopy(additions)
            changed["exports"][0]["texture"] = "wrong.png"
            with self.assertRaisesRegex(ValueError, "differs"):
                merge_exports(changed, additions)


if __name__ == "__main__":
    unittest.main()
