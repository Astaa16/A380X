"""Small source-coverage tests for the detailed passenger-cabin selection."""

import unittest

from prepare_cabin_interior import CABIN_MATERIAL_ROLES, material_coverage


class CabinInteriorTests(unittest.TestCase):
    def make_document(self):
        materials = [{"name": name} for name in CABIN_MATERIAL_ROLES]
        meshes = []
        nodes = []
        accessors = []
        for index, name in enumerate(CABIN_MATERIAL_ROLES):
            accessors.append({"count": 6})
            meshes.append({
                "primitives": [{
                    "attributes": {},
                    "indices": index,
                    "material": index,
                    "mode": 4,
                }],
            })
            nodes.append({"name": name, "mesh": index})
        return {
            "materials": materials,
            "meshes": meshes,
            "nodes": nodes,
            "accessors": accessors,
        }

    def test_selection_includes_requested_cabin_features(self):
        self.assertIn("A380X_AFT_STAIRS", CABIN_MATERIAL_ROLES)
        self.assertIn("A380X_CABIN_STAIRS_FWD", CABIN_MATERIAL_ROLES)
        self.assertIn("A380X_CABIN_FLOOR", CABIN_MATERIAL_ROLES)
        self.assertIn("A380X_UPPER_DECK_FLOOR", CABIN_MATERIAL_ROLES)
        self.assertIn("A380X_MAIN_DECK_CARPET", CABIN_MATERIAL_ROLES)
        self.assertIn("A380X_MAIN_DECK_ASSETS", CABIN_MATERIAL_ROLES)
        self.assertIn("A380X_PAX_DOORS", CABIN_MATERIAL_ROLES)

    def test_every_selected_material_requires_visible_triangles(self):
        coverage = material_coverage(self.make_document())
        self.assertEqual(set(coverage), set(CABIN_MATERIAL_ROLES))
        self.assertTrue(all(item["triangles"] == 2 for item in coverage.values()))

    def test_missing_or_transparent_material_is_rejected(self):
        missing = self.make_document()
        missing["materials"].pop()
        with self.assertRaisesRegex(ValueError, "materials missing"):
            material_coverage(missing)

        transparent = self.make_document()
        transparent["materials"][0]["alphaMode"] = "BLEND"
        with self.assertRaisesRegex(ValueError, "not visibly opaque"):
            material_coverage(transparent)


if __name__ == "__main__":
    unittest.main()
