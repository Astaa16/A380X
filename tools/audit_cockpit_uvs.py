#!/usr/bin/env python3
"""Read-only comparison of the authored FBW cockpit UVs with exported OBJ8 UVs.

ASOBO stores TEXCOORD_0 as IEEE binary16 in accessors marked componentType
5122. This audit recognizes that encoding only on ASOBO primitives. Ordinary
float32 accessors are also supported. OBJ UVs are converted back to the source
encoding after reversing Blender's V flip, avoiding decimal print noise.

This verifies complete UV-set preservation, not vertex-to-position assignment
or visual alignment. It never changes geometry, textures, or source assets.
"""

from __future__ import annotations

import argparse
import base64
import json
import math
import struct
from pathlib import Path
from urllib.parse import unquote


OBJ_FOLDER = "objects/interior/cockpit-panels/"
DEFAULT_TARGETS = {
    "A380_COCKPIT_MIP01_8k": OBJ_FOLDER + "panel_a380_cockpit_mip01_8k.obj",
    "A380_COCKPIT_MIP02_4k": OBJ_FOLDER + "panel_a380_cockpit_mip02_4k.obj",
    "A380_COCKPIT_MIP04_4k": OBJ_FOLDER + "panel_a380_cockpit_mip04_4k.obj",
    "A380_COCKPIT_GLARESHIELD_4k": OBJ_FOLDER + "panel_a380_cockpit_glareshield_4k.obj",
    "A380_COCKPIT_FCU_4k": OBJ_FOLDER + "panel_a380_cockpit_fcu_4k.obj",
    "A380X_COCKPIT_DECAL_EMIS": OBJ_FOLDER + "decal_a380x_cockpit_decal_emis.obj",
}


def _read_buffer(source: Path, record: dict) -> bytes:
    uri = record.get("uri", "")
    if uri.startswith("data:"):
        header, payload = uri.split(",", 1)
        if not header.endswith(";base64"):
            raise ValueError("Only base64 data buffers are supported")
        content = base64.b64decode(payload, validate=True)
    else:
        if not uri:
            raise ValueError("An external or data buffer URI is required")
        path = source.parent / unquote(uri).replace("\\", "/")
        if path.is_file():
            content = path.read_bytes()
        else:
            parts = sorted(path.parent.glob(path.name + ".part[0-9][0-9]"))
            if not parts:
                raise FileNotFoundError(f"Missing buffer and split parts: {path}")
            numbers = [int(part.name[-2:]) for part in parts]
            if numbers != list(range(numbers[0], numbers[0] + len(numbers))):
                raise ValueError(f"Noncontiguous split buffer parts: {path}")
            content = b"".join(part.read_bytes() for part in parts)
    if len(content) != record["byteLength"]:
        raise ValueError(
            f"Buffer size mismatch for {uri}: {len(content)} != {record['byteLength']}"
        )
    return content


def _source_uvs(document: dict, source: Path, material: int, buffers: dict) -> dict:
    encodings: dict[str, set[bytes]] = {}
    accessor_ids = set()
    primitive_count = 0
    vertex_count = 0
    for node in document.get("nodes", []):
        if "mesh" not in node:
            continue
        for primitive in document["meshes"][node["mesh"]].get("primitives", []):
            if primitive.get("material") != material:
                continue
            primitive_count += 1
            accessor_id = primitive.get("attributes", {}).get("TEXCOORD_0")
            if accessor_id is None:
                raise ValueError("A selected source primitive has no TEXCOORD_0")
            if accessor_id in accessor_ids:
                continue
            accessor_ids.add(accessor_id)
            accessor = document["accessors"][accessor_id]
            if accessor.get("type") != "VEC2" or "sparse" in accessor:
                raise ValueError("UV accessor must be a dense VEC2")
            component_type = accessor["componentType"]
            if component_type == 5122 and "ASOBO_primitive" in primitive.get("extras", {}):
                encoding = "e"
            elif component_type == 5126:
                encoding = "f"
            else:
                raise ValueError(f"Unsupported UV encoding: componentType {component_type}")
            if accessor.get("normalized"):
                raise ValueError("Normalized UV accessors require a separate decoder")
            view = document["bufferViews"][accessor["bufferView"]]
            buffer_id = view["buffer"]
            if buffer_id not in buffers:
                buffers[buffer_id] = _read_buffer(source, document["buffers"][buffer_id])
            content = buffers[buffer_id]
            size = struct.calcsize("<2" + encoding)
            stride = view.get("byteStride", size)
            offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
            count = accessor["count"]
            end = offset + (count - 1) * stride + size if count else offset
            view_end = view.get("byteOffset", 0) + view["byteLength"]
            if stride < size or count < 0 or offset < 0 or end > min(view_end, len(content)):
                raise ValueError("UV accessor extends beyond its buffer view")
            values = encodings.setdefault(encoding, set())
            for index in range(count):
                u, v = struct.unpack_from("<2" + encoding, content, offset + index * stride)
                if not math.isfinite(u) or not math.isfinite(v):
                    raise ValueError("Source contains a nonfinite UV")
                # Canonicalize negative zero without losing source precision.
                values.add(struct.pack("<2" + encoding, u + 0.0, v + 0.0))
            vertex_count += count
    if not primitive_count:
        raise ValueError("Source material is not used by any mesh-bearing node")
    return {"encodings": encodings, "vertex_count": vertex_count,
            "primitive_instances": primitive_count, "accessor_count": len(accessor_ids)}


def _obj_uvs(path: Path) -> list[tuple[float, float]]:
    values = []
    for number, line in enumerate(path.read_text().splitlines(), 1):
        fields = line.split()
        if not fields or fields[0] != "VT":
            continue
        if len(fields) != 9:
            raise ValueError(f"Malformed OBJ VT at line {number}")
        u, v = map(float, fields[-2:])
        if not math.isfinite(u) or not math.isfinite(v):
            raise ValueError(f"Nonfinite OBJ UV at line {number}")
        values.append((u, v))
    if not values:
        raise ValueError("OBJ contains no VT records")
    return values


def audit_cockpit_uvs(source_gltf: Path | str, aircraft_root: Path | str,
                      targets: dict[str, str] | None = None) -> dict:
    """Return a JSON-compatible UV audit; neither successful nor failed audits write files.

    ``targets`` maps exact glTF material names to aircraft-relative OBJ paths.
    Missing files/materials and accessor failures become per-material errors.
    Invalid source JSON or an unreadable source file raises the original error.
    """
    source = Path(source_gltf).resolve()
    root = Path(aircraft_root).resolve()
    document = json.loads(source.read_text())
    buffers: dict[int, bytes] = {}
    results = []
    for name, relative in (DEFAULT_TARGETS if targets is None else targets).items():
        path = root / relative
        result = {"material": name, "obj": str(path), "ok": False}
        try:
            matches = [i for i, item in enumerate(document.get("materials", []))
                       if item.get("name") == name]
            if len(matches) != 1:
                raise ValueError(f"Expected one exact material-name match, found {len(matches)}")
            source_values = _source_uvs(document, source, matches[0], buffers)
            obj_values = _obj_uvs(path)
            encoded = source_values["encodings"]
            if len(encoded) != 1:
                raise ValueError("Mixed UV precisions within one material are not supported")
            encoding, expected = next(iter(encoded.items()))
            actual = {struct.pack("<2" + encoding, u + 0.0, (1.0 - v) + 0.0)
                      for u, v in obj_values}
            unexpected, missing = actual - expected, expected - actual
            result.update({
                "material_index": matches[0],
                "source_encoding": "ASOBO_binary16" if encoding == "e" else "float32",
                "source_vertex_count": source_values["vertex_count"],
                "source_primitive_instances": source_values["primitive_instances"],
                "source_accessor_count": source_values["accessor_count"],
                "obj_vertex_count": len(obj_values),
                "source_unique_uv_count": len(expected),
                "obj_unique_uv_count": len(actual),
                "unexpected_obj_uv_count": len(unexpected),
                "missing_source_uv_count": len(missing),
                "unexpected_uv_examples": [list(struct.unpack("<2" + encoding, value))
                                           for value in sorted(unexpected)[:5]],
                "missing_uv_examples": [list(struct.unpack("<2" + encoding, value))
                                        for value in sorted(missing)[:5]],
                "ok": not unexpected and not missing,
            })
        except (OSError, ValueError, KeyError, IndexError, OverflowError, struct.error) as error:
            result["error"] = str(error)
        results.append(result)
    return {
        "source": str(source), "aircraft_root": str(root),
        "ok": bool(results) and all(result["ok"] for result in results),
        "checked_material_count": len(results), "materials": results,
        "method": "Compare complete UV sets after reversing OBJ V flip and rounding to source precision.",
        "limitation": "Does not verify vertex-to-position assignment, decal depth, texture loading, or in-simulator visual alignment.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Authored FBW cockpit glTF")
    parser.add_argument("--aircraft-root", type=Path, required=True, help="Packaged X-Plane aircraft directory")
    args = parser.parse_args()
    report = audit_cockpit_uvs(args.source, args.aircraft_root)
    print(json.dumps(report, indent=2))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
