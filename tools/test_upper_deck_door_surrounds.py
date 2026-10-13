"""Regression coverage for the authored upper-deck doorway surround subset."""
from __future__ import annotations

import unittest
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_upper_deck_door_surrounds import (
    EXPECTED_TRIANGLES,
    MATERIAL,
    SELECTED_NODES,
    source_coverage,
)


def fixture() -> dict:
    index_counts = (4452, 2196, 714, 6, 219, 2160, 16032)
    return {
        "materials": [{"name": MATERIAL}],
        "accessors": [{"count": count} for count in index_counts],
        "meshes": [
            {"primitives": [{"material": 0, "indices": index, "mode": 4}]}
            for index in range(len(index_counts))
        ],
        "nodes": [
            {"name": name, "mesh": index} for index, name in enumerate(SELECTED_NODES)
        ],
    }


class UpperDeckDoorSurroundTests(unittest.TestCase):
    def test_exact_authored_frame_coverage_is_accepted(self):
        coverage = source_coverage(fixture())
        self.assertEqual(coverage["triangles"], EXPECTED_TRIANGLES)
        self.assertEqual([item["name"] for item in coverage["nodes"]], list(SELECTED_NODES))

    def test_unrelated_fallback_material_nodes_are_not_part_of_selection(self):
        document = fixture()
        document["nodes"].append({"name": "BAR.001", "mesh": 0})
        document["nodes"].append({"name": "PAX_DOOR_U1L.004_BACKUP", "mesh": 0})
        coverage = source_coverage(document)
        self.assertNotIn("BAR.001", [item["name"] for item in coverage["nodes"]])
        self.assertNotIn("PAX_DOOR_U1L.004_BACKUP", [item["name"] for item in coverage["nodes"]])


if __name__ == "__main__":
    unittest.main()
