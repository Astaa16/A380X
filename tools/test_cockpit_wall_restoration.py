#!/usr/bin/env python3
"""Small pure-Python coverage/manifest guards; no Blender or real assets."""
import copy
import unittest

from prepare_cockpit_wall_restoration import WALL_MATERIALS, coverage, merge_exports


def fixture():
    return {"materials": [{"name": name} for name in WALL_MATERIALS],
            "nodes": [{"mesh": i, "name": f"wall-{i}"} for i in range(6)],
            "meshes": [{"primitives": [{"material": i, "indices": 0, "extras": {"ASOBO_primitive": {"PrimitiveCount": i + 1}}}]} for i in range(6)],
            "accessors": [{"count": 900}]}


def prepared():
    return {"exports": [{"name": "panel_" + name.lower(), "materials": [name], "alpha_mode": "OPAQUE"} for name in WALL_MATERIALS]}


class WallRestorationTests(unittest.TestCase):
    def test_coverage_uses_draw_counts_not_shared_index_accessor(self):
        result = coverage(fixture())
        self.assertEqual(sum(item["triangles"] for item in result.values()), 21)

    def test_coverage_counts_instances(self):
        document = fixture()
        document["nodes"].append({"mesh": 5, "name": "other-side"})
        self.assertEqual(coverage(document)[WALL_MATERIALS[5]]["triangles"], 12)

    def test_missing_and_invisible_materials_rejected(self):
        for mutate in (lambda d: d["materials"].pop(),
                       lambda d: d["materials"][0].update(alphaMode="BLEND"),
                       lambda d: d["materials"][0].update(extensions={"ASOBO_material_invisible": {}}),
                       lambda d: d["nodes"].pop()):
            document = fixture()
            mutate(document)
            with self.assertRaises(ValueError):
                coverage(document)

    def test_existing_entries_and_metadata_preserved_and_idempotent(self):
        existing = {"exports": [{"name": "old", "materials": ["OLD"], "custom": [1, 2]}], "metadata": {"keep": True}}
        original = copy.deepcopy(existing)
        merged = merge_exports(existing, prepared())
        self.assertEqual(existing, original)
        self.assertEqual(merged["exports"][0], existing["exports"][0])
        self.assertEqual(merged["metadata"], existing["metadata"])
        self.assertEqual(len(merged["exports"]), 7)
        self.assertEqual(merge_exports(merged, prepared()), merged)

    def test_name_collision_rejected(self):
        existing = {"exports": [{"name": prepared()["exports"][0]["name"], "materials": ["OLD"]}]}
        with self.assertRaisesRegex(ValueError, "collides"):
            merge_exports(existing, prepared())

    def test_changed_existing_wall_rejected(self):
        existing = prepared()
        existing["exports"][0]["alpha_mode"] = "BLEND"
        with self.assertRaisesRegex(ValueError, "differs"):
            merge_exports(existing, prepared())

    def test_incomplete_preparation_rejected(self):
        partial = prepared()
        partial["exports"].pop()
        with self.assertRaisesRegex(ValueError, "exactly the six"):
            merge_exports({"exports": []}, partial)


if __name__ == "__main__":
    unittest.main()
