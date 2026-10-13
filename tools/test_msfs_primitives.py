#!/usr/bin/env python3
"""Regression tests for shared-index MSFS material draw calls.

Tiny synthetic geometry makes the original corruption measurable: two quads
share one index accessor but use separate draw ranges and vertex bases.
"""
from __future__ import annotations

import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "blender"))
from normalize_msfs_gltf import normalize_asobo_primitive_ranges


class PrimitiveRangeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="asobo-primitive-test-", dir=os.environ.get("A380X_TEST_TEMP_DIR"))
        self.root = Path(self.temp.name)
        self.output = self.root / "model.gltf"
        self.original = struct.pack("<12H", 0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3)
        (self.root / "mesh.bin").write_bytes(self.original)
        self.document = {
            "buffers": [{"uri": "mesh.bin", "byteLength": len(self.original)}],
            "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": len(self.original)}],
            "accessors": [{"bufferView": 0, "componentType": 5123, "count": 12, "type": "SCALAR"}, {"count": 8}, {"count": 8}],
            "meshes": [{"primitives": [
                {"attributes": {"POSITION": 1, "TEXCOORD_0": 2}, "indices": 0, "material": 0, "extras": {"keep": "metadata", "ASOBO_primitive": {"PrimitiveCount": 2, "VertexCount": 4}}},
                {"attributes": {"POSITION": 1, "TEXCOORD_0": 2}, "indices": 0, "material": 1, "extras": {"ASOBO_primitive": {"StartIndex": 6, "BaseVertexIndex": 4, "PrimitiveCount": 2, "VertexCount": 4}}},
            ]}],
        }

    def tearDown(self) -> None:
        self.temp.cleanup()

    def decoded(self, primitive: dict) -> tuple[int, ...]:
        accessor = self.document["accessors"][primitive["indices"]]
        view = self.document["bufferViews"][accessor["bufferView"]]
        binary = (self.root / "mesh.bin").read_bytes()
        return struct.unpack_from(f"<{accessor['count']}I", binary, view["byteOffset"])

    def test_material_ranges_and_base_vertex_are_expanded_once(self) -> None:
        stats = normalize_asobo_primitive_ranges(self.document, self.output)
        first, second = self.document["meshes"][0]["primitives"]
        self.assertEqual(self.decoded(first), (0, 1, 2, 0, 2, 3))
        self.assertEqual(self.decoded(second), (4, 5, 6, 4, 6, 7))
        self.assertEqual(first["attributes"], second["attributes"])
        self.assertEqual(first["extras"], {"keep": "metadata"})
        self.assertNotIn("extras", second)
        self.assertEqual(stats["asobo_source_index_references"], 24)
        self.assertEqual(stats["asobo_draw_index_references"], 12)
        self.assertEqual((self.root / "mesh.bin").read_bytes()[:24], self.original)
        before = (self.root / "mesh.bin").read_bytes()
        second_pass = normalize_asobo_primitive_ranges(self.document, self.output)
        self.assertEqual(second_pass["asobo_primitives_processed"], 0)
        self.assertEqual((self.root / "mesh.bin").read_bytes(), before)

    def assert_bad_draw(self, mutate, match: str) -> None:
        mutate(self.document)
        with self.assertRaisesRegex(ValueError, match):
            normalize_asobo_primitive_ranges(self.document, self.output)
        self.assertEqual((self.root / "mesh.bin").read_bytes(), self.original)

    def test_start_index_range_cannot_escape_shared_accessor(self) -> None:
        self.assert_bad_draw(lambda d: d["meshes"][0]["primitives"][1]["extras"]["ASOBO_primitive"].update(StartIndex=9), "draw range")

    def test_base_vertex_is_checked_against_position(self) -> None:
        self.assert_bad_draw(lambda d: d["meshes"][0]["primitives"][1]["extras"]["ASOBO_primitive"].update(BaseVertexIndex=5), "POSITION accessor")

    def test_every_attribute_has_bounds_checked(self) -> None:
        self.assert_bad_draw(lambda d: d["accessors"][2].update(count=7), "TEXCOORD_0 accessor")

    def test_local_vertex_count_is_respected(self) -> None:
        self.assert_bad_draw(lambda d: d["meshes"][0]["primitives"][1]["extras"]["ASOBO_primitive"].update(VertexCount=3), "VertexCount")

    def test_declared_buffer_view_limits_are_respected(self) -> None:
        self.assert_bad_draw(lambda d: d["bufferViews"][0].update(byteLength=22), "buffer bounds")

    def test_malformed_offset_fails_closed(self) -> None:
        self.assert_bad_draw(lambda d: d["meshes"][0]["primitives"][1]["extras"]["ASOBO_primitive"].update(StartIndex=-1), "non-negative")

    def test_only_indexed_triangle_draw_calls_are_accepted(self) -> None:
        self.assert_bad_draw(lambda d: d["meshes"][0]["primitives"][0].update(mode=1), "TRIANGLES")


if __name__ == "__main__":
    unittest.main()
