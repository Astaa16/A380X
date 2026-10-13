#!/usr/bin/env python3
"""Normalize FBW/MSFS triangle winding before the standard glTF importer.

FBW's MSFS glTF triangles are generally clockwise relative to their stored
outward normals. Standard glTF uses counter-clockwise front faces. Blender's
X-Plane exporter reverses standard glTF faces once more for clockwise OBJ8,
so feeding the MSFS index order directly to it produces inside-out objects.

Call after expanding ASOBO primitive draw ranges and unpacking attributes.
Only build-copy index buffers are written; positions, normals and UVs stay
unchanged. A per-primitive marker makes a repeated call idempotent.
"""

from __future__ import annotations

import mmap
import struct
import sys
from array import array
from pathlib import Path
from typing import Any, Sequence


MARKER = "a380x_xp12_winding"
FORMATS = {5120: "b", 5121: "B", 5122: "h", 5123: "H", 5125: "I", 5126: "f"}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


class _AccessorValues(Sequence):
    """Read vertices lazily instead of retaining millions of Python tuples."""

    def __init__(self, mapped: mmap.mmap, unpack: struct.Struct, offset: int,
                 stride: int, count: int, scalar: bool):
        self.mapped = mapped
        self.unpack = unpack
        self.offset = offset
        self.stride = stride
        self.count = count
        self.scalar = scalar

    def __len__(self):
        return self.count

    def __getitem__(self, index):
        if isinstance(index, slice):
            return [self[i] for i in range(*index.indices(self.count))]
        if index < 0:
            index += self.count
        if not 0 <= index < self.count:
            raise IndexError(index)
        value = self.unpack.unpack_from(self.mapped, self.offset + index * self.stride)
        return value[0] if self.scalar else value


def classify_winding(
    indices: Sequence[int], positions: Sequence[Sequence[float]],
    normals: Sequence[Sequence[float]] | None,
) -> dict[str, Any]:
    """Compare triangle cross products with outward shading normals."""
    if len(indices) % 3:
        raise ValueError("Triangle index count must be divisible by three")
    result = {"triangles": len(indices) // 3, "clockwise": 0,
              "counter_clockwise": 0, "indeterminate": 0}
    for start in range(0, len(indices), 3):
        ids = indices[start:start + 3]
        if any(i < 0 or i >= len(positions) for i in ids):
            raise ValueError(f"Triangle index outside POSITION accessor: {ids}")
        if normals is None:
            result["indeterminate"] += 1
            continue
        if any(i >= len(normals) for i in ids):
            raise ValueError("Triangle index outside NORMAL accessor")
        a, b, c = (positions[i] for i in ids)
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        face_normals = [normals[index] for index in ids]
        normal = [sum(value[i] for value in face_normals) for i in range(3)]
        product = sum(cross[i] * normal[i] for i in range(3))
        magnitude_squared = sum(x * x for x in cross) * sum(x * x for x in normal)
        # Very small or edge-on/contradictory normals cannot identify a face.
        if magnitude_squared < 1e-24 or product * product < magnitude_squared * 0.01:
            result["indeterminate"] += 1
        elif product < 0:
            result["clockwise"] += 1
        else:
            result["counter_clockwise"] += 1
    usable = result["clockwise"] + result["counter_clockwise"]
    result["confidence"] = max(result["clockwise"], result["counter_clockwise"]) / usable if usable else 0.0
    return result


def normalize_msfs_winding(document: dict[str, Any], output_document: Path) -> dict[str, Any]:
    """Convert MSFS clockwise primitives to standard glTF counter-clockwise.

    A decisive existing CCW majority is left untouched. In an ambiguous case
    (less than 80% agreement or no normals), use the known MSFS clockwise
    convention and report that assumption explicitly. Callers can inspect
    ``assumed_clockwise_primitives`` before exporting unfamiliar source assets.
    """
    output_document = Path(output_document)
    root = output_document.parent.resolve()
    maps: dict[int, mmap.mmap] = {}
    files = []
    cache: dict[int, _AccessorValues] = {}
    writer = None
    new_buffer_index = None
    reports = []
    converted: dict[tuple[int | None, int, int | None], int] = {}
    buffer_path = root / "buffers" / f"{output_document.stem}.winding-{len(document.get('buffers', [])):03d}.bin"

    def accessor_values(index: int) -> _AccessorValues:
        if index in cache:
            return cache[index]
        accessor = document["accessors"][index]
        if "sparse" in accessor or "bufferView" not in accessor:
            raise ValueError("Expand sparse/implicit accessors before winding normalization")
        view = document["bufferViews"][accessor["bufferView"]]
        buffer_index = view["buffer"]
        if buffer_index not in maps:
            uri = document["buffers"][buffer_index]["uri"]
            source = (root / uri.replace("\\", "/")).resolve()
            if not source.is_relative_to(root):
                raise ValueError("Winding normalization requires buffers copied inside the build directory")
            stream = source.open("rb")
            files.append(stream)
            maps[buffer_index] = mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ)
        component = FORMATS[accessor["componentType"]]
        width = WIDTHS[accessor["type"]]
        unpack = struct.Struct("<" + component * width)
        offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        stride = view.get("byteStride", unpack.size)
        count = accessor["count"]
        if stride < unpack.size or offset < 0 or (count and offset + (count - 1) * stride + unpack.size > len(maps[buffer_index])):
            raise ValueError(f"Accessor {index} exceeds its source buffer or has invalid stride")
        cache[index] = _AccessorValues(maps[buffer_index], unpack, offset, stride, count, width == 1)
        return cache[index]

    try:
        for mesh_index, mesh in enumerate(document.get("meshes", [])):
            for primitive_index, primitive in enumerate(mesh.get("primitives", [])):
                if primitive.get("mode", 4) != 4:
                    continue
                if primitive.get("extras", {}).get(MARKER, {}).get("output") == "counter_clockwise":
                    continue
                position_index = primitive["attributes"]["POSITION"]
                normal_index = primitive["attributes"].get("NORMAL")
                old_index = primitive.get("indices")
                positions = accessor_values(position_index)
                indices = accessor_values(old_index) if old_index is not None else range(len(positions))
                normals = accessor_values(normal_index) if normal_index is not None else None
                report = classify_winding(indices, positions, normals)
                assumed = bool(report["triangles"]) and report["confidence"] < 0.8
                flip = assumed or report["clockwise"] > report["counter_clockwise"]
                report.update(mesh=mesh_index, primitive=primitive_index, flipped=flip,
                              assumed_msfs_clockwise=assumed, output="counter_clockwise")
                primitive.setdefault("extras", {})[MARKER] = dict(report)
                reports.append(report)
                if not flip:
                    continue
                cache_key = (old_index, position_index, normal_index)
                if cache_key in converted:
                    primitive["indices"] = converted[cache_key]
                    continue
                if writer is None:
                    existing = {(root / b["uri"].replace("\\", "/")).resolve() for b in document.get("buffers", []) if "uri" in b}
                    if buffer_path.resolve() in existing:
                        raise ValueError("Refusing to overwrite a buffer already referenced by the input document")
                    buffer_path.parent.mkdir(parents=True, exist_ok=True)
                    writer = buffer_path.open("wb")
                    new_buffer_index = len(document.setdefault("buffers", []))
                    document["buffers"].append({"uri": buffer_path.relative_to(root).as_posix(), "byteLength": 0})
                offset = writer.tell()
                # Keep additional memory bounded even for a full cockpit mesh.
                # glTF index components are little-endian, unlike native array.
                for chunk_start in range(0, len(indices), 3 * 8192):
                    chunk_end = min(chunk_start + 3 * 8192, len(indices))
                    corrected = array("I", (index for start in range(chunk_start, chunk_end, 3)
                                           for index in (indices[start], indices[start + 2], indices[start + 1])))
                    if sys.byteorder != "little":
                        corrected.byteswap()
                    writer.write(corrected.tobytes())
                view_index = len(document.setdefault("bufferViews", []))
                document["bufferViews"].append({"buffer": new_buffer_index, "byteOffset": offset,
                                              "byteLength": len(indices) * 4, "target": 34963})
                accessor_index = len(document.setdefault("accessors", []))
                document["accessors"].append({"bufferView": view_index, "byteOffset": 0,
                                             "componentType": 5125, "count": len(indices), "type": "SCALAR"})
                primitive["indices"] = accessor_index
                converted[cache_key] = accessor_index
        if writer is not None:
            document["buffers"][new_buffer_index]["byteLength"] = writer.tell()
    finally:
        if writer is not None:
            writer.close()
        for mapped in maps.values():
            mapped.close()
        for stream in files:
            stream.close()
    return {"processed_primitives": len(reports),
            "flipped_primitives": sum(report["flipped"] for report in reports),
            "assumed_clockwise_primitives": sum(report["assumed_msfs_clockwise"] for report in reports),
            "new_index_accessors": len(converted), "primitives": reports}
