#!/usr/bin/env python3
"""Winding fixtures: source buffers remain unchanged and reruns never flip twice."""
import importlib.util
import json
import struct
import tempfile
import unittest
from pathlib import Path

MODULE = Path(__file__).resolve().parents[1] / "blender" / "normalize_msfs_winding.py"
spec = importlib.util.spec_from_file_location("winding", MODULE)
winding = importlib.util.module_from_spec(spec)
spec.loader.exec_module(winding)


class WindingTests(unittest.TestCase):
    def make_interleaved_document(self, root, indices=None, count=3, normals=True):
        # POSITION starts after a four-byte sentinel; NORMAL follows in stride.
        values = ((0, 0, 0), (1, 0, 0), (0, 1, 0))
        raw = b"TEST" + b"".join(struct.pack("<6f", *values[i % 3], 0, 0, 1) for i in range(count))
        views = [{"buffer": 0, "byteOffset": 4, "byteLength": count * 24, "byteStride": 24}]
        accessors = [{"bufferView": 0, "componentType": 5126, "count": count, "type": "VEC3"},
                     {"bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": count, "type": "VEC3"}]
        primitive = {"attributes": {"POSITION": 0}}
        if normals:
            primitive["attributes"]["NORMAL"] = 1
        if indices is not None:
            views.append({"buffer": 0, "byteOffset": len(raw), "byteLength": 4 * len(indices)})
            accessors.append({"bufferView": 1, "componentType": 5125, "count": len(indices), "type": "SCALAR"})
            primitive["indices"] = 2
            raw += struct.pack(f"<{len(indices)}I", *indices)
        (root / "source.bin").write_bytes(raw)
        return {"buffers": [{"uri": "source.bin", "byteLength": len(raw)}],
                "bufferViews": views, "accessors": accessors,
                "meshes": [{"primitives": [primitive]}]}

    def test_clockwise_and_counter_clockwise(self):
        points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        normals = [(0, 0, 1)] * 3
        self.assertEqual(winding.classify_winding([0, 2, 1], points, normals)["clockwise"], 1)
        self.assertEqual(winding.classify_winding([0, 1, 2], points, normals)["counter_clockwise"], 1)
        with self.assertRaises(ValueError):
            winding.classify_winding([0, 1, 3], points, normals)
        with self.assertRaises(ValueError):
            winding.classify_winding([0, 1], points, normals)

    def test_interleaved_attributes_and_chunked_indices(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = self.make_interleaved_document(root, [0, 2, 1] * 8193)
            report = winding.normalize_msfs_winding(document, root / "fixture.gltf")
            self.assertEqual(report["flipped_primitives"], 1)
            self.assertEqual(report["primitives"][0]["clockwise"], 8193)
            data = (root / document["buffers"][-1]["uri"]).read_bytes()
            self.assertEqual(data, struct.pack("<3I", 0, 1, 2) * 8193)

    def test_missing_normals_explicitly_assumes_msfs(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = self.make_interleaved_document(root, [0, 2, 1], normals=False)
            report = winding.normalize_msfs_winding(document, root / "fixture.gltf")
            self.assertEqual(report["assumed_clockwise_primitives"], 1)
            self.assertEqual(report["flipped_primitives"], 1)

    def test_nonindexed_ccw_remains_nonindexed(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = self.make_interleaved_document(root)
            report = winding.normalize_msfs_winding(document, root / "fixture.gltf")
            self.assertEqual(report["flipped_primitives"], 0)
            self.assertNotIn("indices", document["meshes"][0]["primitives"][0])
            self.assertEqual(len(document["buffers"]), 1)

    def test_reused_accessor_is_only_written_once(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = self.make_interleaved_document(root, [0, 2, 1])
            primitive = document["meshes"][0]["primitives"][0]
            document["meshes"][0]["primitives"].append(json.loads(json.dumps(primitive)))
            report = winding.normalize_msfs_winding(document, root / "fixture.gltf")
            self.assertEqual(report["flipped_primitives"], 2)
            self.assertEqual(report["new_index_accessors"], 1)
            self.assertEqual(document["meshes"][0]["primitives"][0]["indices"], document["meshes"][0]["primitives"][1]["indices"])

    def test_only_triangles_are_processed(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = self.make_interleaved_document(root, [0, 2, 1])
            document["meshes"][0]["primitives"][0]["mode"] = 5
            report = winding.normalize_msfs_winding(document, root / "fixture.gltf")
            self.assertEqual(report["processed_primitives"], 0)
            self.assertEqual(len(document["buffers"]), 1)

    def test_empty_primitive_needs_no_buffer(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            document = self.make_interleaved_document(root, [], count=0)
            report = winding.normalize_msfs_winding(document, root / "fixture.gltf")
            self.assertEqual(report["flipped_primitives"], 0)
            self.assertEqual(report["assumed_clockwise_primitives"], 0)
            self.assertEqual(len(document["buffers"]), 1)

    def test_build_buffer_and_idempotence(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            raw = struct.pack("<18f6H", 0, 0, 0, 1, 0, 0, 0, 1, 0,
                              0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 2, 1, 0, 1, 2)
            (root / "source.bin").write_bytes(raw)
            document = {"buffers": [{"uri": "source.bin", "byteLength": len(raw)}],
                        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36},
                                        {"buffer": 0, "byteOffset": 36, "byteLength": 36},
                                        {"buffer": 0, "byteOffset": 72, "byteLength": 12}],
                        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
                                      {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
                                      {"bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR"},
                                      {"bufferView": 2, "byteOffset": 6, "componentType": 5123, "count": 3, "type": "SCALAR"}],
                        "meshes": [{"primitives": [{"indices": 2, "attributes": {"POSITION": 0, "NORMAL": 1}},
                                                    {"indices": 3, "attributes": {"POSITION": 0, "NORMAL": 1}}]}]}
            report = winding.normalize_msfs_winding(document, root / "fixture.gltf")
            self.assertEqual(report["flipped_primitives"], 1)
            self.assertEqual(report["assumed_clockwise_primitives"], 0)
            self.assertEqual((root / "source.bin").read_bytes(), raw)
            corrected = root / document["buffers"][-1]["uri"]
            self.assertEqual(struct.unpack("<3I", corrected.read_bytes()), (0, 1, 2))
            snapshot = json.dumps(document, sort_keys=True)
            self.assertEqual(winding.normalize_msfs_winding(document, root / "fixture.gltf")["processed_primitives"], 0)
            self.assertEqual(snapshot, json.dumps(document, sort_keys=True))


if __name__ == "__main__":
    unittest.main()
