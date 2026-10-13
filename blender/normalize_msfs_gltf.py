#!/usr/bin/env python3
"""Normalize an FBW/MSFS glTF so Blender's standard importer can read it.

Microsoft Flight Simulator stores texture sources in the non-standard
``MSFT_texture_dds`` extension and uses Windows path separators. Blender's
glTF importer intentionally rejects that required extension. This tool makes
a build copy, promotes each extension texture source to standard glTF, and
copies the referenced buffers/images beside the normalized document.

``--geometry-only`` drops texture references entirely. It is useful for
proving the mesh/animation pipeline before texture conversion is available.
The original FlyByWire checkout is never modified.
"""

from __future__ import annotations

import argparse
import json
import shutil
import struct
from pathlib import Path, PurePosixPath
from typing import Any

from normalize_msfs_winding import normalize_msfs_winding
from repair_fbw_tail_bindshape import protected_tail_joint_accessors, repair_fbw_tail_bindshape
from repair_fbw_wing_joints import repair_fbw_wing_joints


MSFS_TEXTURE_EXTENSION = "MSFT_texture_dds"
MATERIAL_TEXTURE_FIELDS = (
    "normalTexture",
    "occlusionTexture",
    "emissiveTexture",
)
PBR_TEXTURE_FIELDS = (
    "baseColorTexture",
    "metallicRoughnessTexture",
)
COMPONENT_COUNTS = {
    "SCALAR": 1,
    "VEC2": 2,
    "VEC3": 3,
    "VEC4": 4,
}


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="Source FBW/MSFS .gltf file")
    parser.add_argument("output", type=Path, help="Destination normalized .gltf file")
    parser.add_argument(
        "--asset-root",
        type=Path,
        help="Root searched by filename when an MSFS-relative image path is missing",
    )
    parser.add_argument(
        "--geometry-only",
        action="store_true",
        help="Remove image and material texture references",
    )
    parser.add_argument(
        "--repair-fbw-tail-bindshape", action="store_true",
        help="EXPERIMENTAL: source-specific A380 horizontal-tail bind-shape correction; rebuild/native verification required",
    )
    parser.add_argument("--repair-fbw-wing-joints", action="store_true",
                        help="Decode the two audited optimized wing skins' one-based joint palette")
    return parser.parse_args()


def normalized_uri(uri: str) -> str:
    return uri.replace("\\", "/")


def is_embedded_uri(uri: str) -> bool:
    return uri.startswith("data:")


def remove_extension_name(document: dict[str, Any], key: str) -> None:
    for field in ("extensionsUsed", "extensionsRequired"):
        values = document.get(field)
        if not isinstance(values, list):
            continue
        document[field] = [value for value in values if value != key]
        if not document[field]:
            del document[field]


def promote_msfs_texture_sources(document: dict[str, Any]) -> int:
    promoted = 0
    for texture in document.get("textures", []):
        extensions = texture.get("extensions")
        if not isinstance(extensions, dict):
            continue
        msfs_texture = extensions.pop(MSFS_TEXTURE_EXTENSION, None)
        if isinstance(msfs_texture, dict) and "source" in msfs_texture:
            texture["source"] = msfs_texture["source"]
            promoted += 1
        if not extensions:
            texture.pop("extensions", None)
    remove_extension_name(document, MSFS_TEXTURE_EXTENSION)
    return promoted


def strip_texture_references(document: dict[str, Any]) -> None:
    for material in document.get("materials", []):
        for field in MATERIAL_TEXTURE_FIELDS:
            material.pop(field, None)
        pbr = material.get("pbrMetallicRoughness")
        if isinstance(pbr, dict):
            for field in PBR_TEXTURE_FIELDS:
                pbr.pop(field, None)
    for field in ("textures", "images", "samplers"):
        document.pop(field, None)


def build_asset_index(asset_root: Path | None) -> dict[str, list[Path]]:
    if asset_root is None:
        return {}
    if not asset_root.is_dir():
        raise FileNotFoundError(f"Asset root does not exist: {asset_root}")
    index: dict[str, list[Path]] = {}
    for path in asset_root.rglob("*"):
        if path.is_file() and not path.name.startswith("._"):
            index.setdefault(path.name.casefold(), []).append(path)
    return index


def resolve_uri_file(
    source_document: Path,
    uri: str,
    asset_index: dict[str, list[Path]],
) -> Path:
    relative_uri = normalized_uri(uri)
    direct = (source_document.parent / relative_uri).resolve()
    if direct.is_file():
        return direct

    filename = PurePosixPath(relative_uri).name
    candidates = asset_index.get(filename.casefold(), [])
    if not candidates:
        raise FileNotFoundError(
            f"Could not resolve {uri!r} from {source_document}; "
            "pass --asset-root to search the full FBW asset tree"
        )
    if len(candidates) == 1:
        return candidates[0]

    # Shared A380 textures normally live under common/texture. Prefer that
    # deterministic location when duplicate filenames exist.
    common = [
        candidate
        for candidate in candidates
        if "/common/texture/" in candidate.as_posix().casefold()
    ]
    if len(common) == 1:
        return common[0]
    formatted = "\n  ".join(str(candidate) for candidate in candidates)
    raise RuntimeError(f"Ambiguous asset {filename!r}; candidates:\n  {formatted}")


def copy_uri_assets(
    document: dict[str, Any],
    source_document: Path,
    output_document: Path,
    asset_index: dict[str, list[Path]],
) -> tuple[int, int]:
    copied_buffers = 0
    copied_images = 0

    for index, buffer in enumerate(document.get("buffers", [])):
        uri = buffer.get("uri")
        if not isinstance(uri, str) or is_embedded_uri(uri):
            continue
        source = resolve_uri_file(source_document, uri, asset_index)
        suffix = source.suffix or ".bin"
        destination = output_document.parent / "buffers" / f"buffer-{index:03d}{suffix}"
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        buffer["uri"] = destination.relative_to(output_document.parent).as_posix()
        copied_buffers += 1

    for index, image in enumerate(document.get("images", [])):
        uri = image.get("uri")
        if not isinstance(uri, str) or is_embedded_uri(uri):
            continue
        source = resolve_uri_file(source_document, uri, asset_index)
        suffixes = "".join(source.suffixes) or source.suffix or ".image"
        destination = output_document.parent / "textures" / f"image-{index:03d}{suffixes}"
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        image["uri"] = destination.relative_to(output_document.parent).as_posix()
        copied_images += 1

    return copied_buffers, copied_images


def accessor_semantics(document: dict[str, Any]) -> dict[int, set[str]]:
    semantics: dict[int, set[str]] = {}
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            for semantic, accessor_index in primitive.get("attributes", {}).items():
                semantics.setdefault(accessor_index, set()).add(semantic)
    return semantics


def normalize_asobo_primitive_ranges(
    document: dict[str, Any], output_document: Path
) -> dict[str, int]:
    """Expand MSFS draw calls into independent, standard glTF index accessors.

    ASOBO stores several material draw calls in one index/vertex accessor and
    supplies StartIndex, PrimitiveCount and BaseVertexIndex in primitive extras.
    A standard glTF importer ignores those extras, drawing the complete index
    buffer once per material and applying indices to the wrong vertex range.
    Keep each fully declared attribute accessor and add BaseVertexIndex to the
    selected indices. Every selected index is checked against EVERY attribute.
    """
    stats = {"asobo_primitives_processed": 0, "asobo_range_accessors_created": 0,
             "asobo_base_vertex_primitives": 0, "asobo_source_index_references": 0,
             "asobo_draw_index_references": 0}
    buffers: dict[int, bytearray] = {}
    paths: dict[int, Path] = {}
    changed_buffers: set[int] = set()
    scalar_formats = {5121: "B", 5123: "H", 5125: "I"}

    def nonnegative_integer(value: Any, name: str) -> int:
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            raise ValueError(f"{name} must be a non-negative integer, got {value!r}")
        return value

    for mesh_index, mesh in enumerate(document.get("meshes", [])):
        for primitive_index, primitive in enumerate(mesh.get("primitives", [])):
            extras = primitive.get("extras", {})
            draw = extras.get("ASOBO_primitive")
            if not isinstance(draw, dict):
                continue
            label = f"mesh {mesh_index} primitive {primitive_index}"
            if primitive.get("mode", 4) != 4 or "indices" not in primitive:
                raise ValueError(f"{label}: ASOBO draw range requires indexed TRIANGLES")
            if "PrimitiveCount" not in draw:
                raise ValueError(f"{label}: ASOBO draw range has no PrimitiveCount")
            start = nonnegative_integer(draw.get("StartIndex", 0), f"{label} StartIndex")
            base = nonnegative_integer(draw.get("BaseVertexIndex", 0), f"{label} BaseVertexIndex")
            count = 3 * nonnegative_integer(draw["PrimitiveCount"], f"{label} PrimitiveCount")
            vertex_count = draw.get("VertexCount")
            if vertex_count is not None:
                vertex_count = nonnegative_integer(vertex_count, f"{label} VertexCount")

            accessor = document["accessors"][primitive["indices"]]
            component = accessor.get("componentType")
            if accessor.get("type") != "SCALAR" or component not in scalar_formats or accessor.get("normalized") or "sparse" in accessor:
                raise ValueError(f"{label}: unsupported ASOBO index accessor")
            declared_count = nonnegative_integer(accessor["count"], f"{label} index count")
            if start + count > declared_count:
                raise ValueError(f"{label}: draw range {start}:{start + count} exceeds {declared_count} indices")
            view = document["bufferViews"][accessor["bufferView"]]
            buffer_index = view.get("buffer", 0)
            buffer_info = document["buffers"][buffer_index]
            uri = buffer_info.get("uri")
            if not isinstance(uri, str) or is_embedded_uri(uri):
                raise ValueError(f"{label}: ASOBO draw range requires an external buffer")
            if buffer_index not in buffers:
                paths[buffer_index] = output_document.parent / normalized_uri(uri)
                buffers[buffer_index] = bytearray(paths[buffer_index].read_bytes())
            binary = buffers[buffer_index]
            component_size = struct.calcsize(scalar_formats[component])
            stride = nonnegative_integer(view.get("byteStride", component_size), f"{label} index stride")
            if stride < component_size or stride % component_size:
                raise ValueError(f"{label}: invalid index stride {stride}")
            accessor_offset = nonnegative_integer(accessor.get("byteOffset", 0), f"{label} accessor byteOffset")
            view_offset = nonnegative_integer(view.get("byteOffset", 0), f"{label} view byteOffset")
            view_length = nonnegative_integer(view["byteLength"], f"{label} view byteLength")
            accessor_end = accessor_offset + ((declared_count - 1) * stride + component_size if declared_count else 0)
            if accessor_end > view_length or view_offset + view_length > len(binary) or view_offset + view_length > buffer_info["byteLength"]:
                raise ValueError(f"{label}: index accessor or bufferView exceeds its declared buffer bounds")
            fmt = "<" + scalar_formats[component]
            indices = [struct.unpack_from(fmt, binary, view_offset + accessor_offset + (start + i) * stride)[0] for i in range(count)]
            if indices and vertex_count is not None and max(indices) >= vertex_count:
                raise ValueError(f"{label}: local index {max(indices)} exceeds VertexCount {vertex_count}")
            adjusted = [index + base for index in indices]
            attributes = primitive.get("attributes", {})
            if "POSITION" not in attributes:
                raise ValueError(f"{label}: draw range has no POSITION accessor")
            for semantic, attribute_index in attributes.items():
                attribute_count = nonnegative_integer(document["accessors"][attribute_index]["count"], f"{label} {semantic} count")
                if adjusted and max(adjusted) >= attribute_count:
                    raise ValueError(f"{label}: adjusted index {max(adjusted)} exceeds {semantic} accessor count {attribute_count}")
            if adjusted and max(adjusted) > 0xFFFFFFFF:
                raise ValueError(f"{label}: adjusted index cannot fit glTF unsigned integer storage")

            if start or base or count != declared_count:
                while len(binary) % 4:
                    binary.append(0)
                offset = len(binary)
                binary.extend(struct.pack(f"<{len(adjusted)}I", *adjusted))
                new_view = len(document["bufferViews"])
                document["bufferViews"].append({"buffer": buffer_index, "byteOffset": offset, "byteLength": 4 * len(adjusted), "target": 34963})
                new_accessor = {"bufferView": new_view, "byteOffset": 0, "componentType": 5125, "count": len(adjusted), "type": "SCALAR", "name": f"A380X_XP12_draw_range_{mesh_index}_{primitive_index}"}
                if adjusted:
                    new_accessor.update(min=[min(adjusted)], max=[max(adjusted)])
                primitive["indices"] = len(document["accessors"])
                document["accessors"].append(new_accessor)
                buffer_info["byteLength"] = len(binary)
                changed_buffers.add(buffer_index)
                stats["asobo_range_accessors_created"] += 1
            stats["asobo_primitives_processed"] += 1
            stats["asobo_base_vertex_primitives"] += bool(base)
            stats["asobo_source_index_references"] += declared_count
            stats["asobo_draw_index_references"] += count
            del extras["ASOBO_primitive"]
            if not extras:
                primitive.pop("extras", None)

    # Do not change any build-copy buffer on disk until every draw range passes.
    for buffer_index in changed_buffers:
        paths[buffer_index].write_bytes(buffers[buffer_index])
    return stats


def append_float_accessor(
    document: dict[str, Any],
    accessor_index: int,
    output_document: Path,
    binary_buffers: dict[int, bytearray],
    buffer_paths: dict[int, Path],
) -> None:
    accessor = document["accessors"][accessor_index]
    source_view = document["bufferViews"][accessor["bufferView"]]
    buffer_index = source_view.get("buffer", 0)
    buffer_info = document["buffers"][buffer_index]
    buffer_uri = buffer_info.get("uri")
    if not isinstance(buffer_uri, str) or is_embedded_uri(buffer_uri):
        raise RuntimeError("ASOBO half-float conversion requires an external buffer URI")

    buffer_path = output_document.parent / normalized_uri(buffer_uri)
    binary = binary_buffers.get(buffer_index)
    if binary is None:
        binary = bytearray(buffer_path.read_bytes())
        binary_buffers[buffer_index] = binary
        buffer_paths[buffer_index] = buffer_path
    component_count = COMPONENT_COUNTS[accessor["type"]]
    source_offset = source_view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    source_stride = source_view.get("byteStride", component_count * 2)
    converted = bytearray()
    for item_index in range(accessor["count"]):
        item_offset = source_offset + item_index * source_stride
        values = struct.unpack_from(f"<{component_count}e", binary, item_offset)
        converted.extend(struct.pack(f"<{component_count}f", *values))

    while len(binary) % 4:
        binary.append(0)
    converted_offset = len(binary)
    binary.extend(converted)
    buffer_info["byteLength"] = len(binary)

    document["bufferViews"].append(
        {
            "buffer": buffer_index,
            "byteOffset": converted_offset,
            "byteLength": len(converted),
            "target": 34962,
            "name": f"A380X_XP12_float_accessor_{accessor_index}",
        }
    )
    accessor["bufferView"] = len(document["bufferViews"]) - 1
    accessor["byteOffset"] = 0
    accessor["componentType"] = 5126
    accessor.pop("normalized", None)


def append_scalar_weight_accessor(
    document: dict[str, Any],
    accessor_index: int,
    output_document: Path,
    binary_buffers: dict[int, bytearray],
    buffer_paths: dict[int, Path],
) -> None:
    """Expand a rigid scalar WEIGHTS_0 value to glTF's required VEC4."""
    accessor = document["accessors"][accessor_index]
    source_view = document["bufferViews"][accessor["bufferView"]]
    buffer_index = source_view.get("buffer", 0)
    buffer_info = document["buffers"][buffer_index]
    buffer_uri = buffer_info.get("uri")
    if not isinstance(buffer_uri, str) or is_embedded_uri(buffer_uri):
        raise RuntimeError("Scalar skin-weight conversion requires an external buffer URI")

    buffer_path = output_document.parent / normalized_uri(buffer_uri)
    binary = binary_buffers.get(buffer_index)
    if binary is None:
        binary = bytearray(buffer_path.read_bytes())
        binary_buffers[buffer_index] = binary
        buffer_paths[buffer_index] = buffer_path

    source_offset = source_view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    source_stride = source_view.get("byteStride", 4)
    converted = bytearray()
    for item_index in range(accessor["count"]):
        weight = struct.unpack_from("<f", binary, source_offset + item_index * source_stride)[0]
        converted.extend(struct.pack("<4f", weight, 0.0, 0.0, 0.0))

    while len(binary) % 4:
        binary.append(0)
    converted_offset = len(binary)
    binary.extend(converted)
    buffer_info["byteLength"] = len(binary)
    document["bufferViews"].append(
        {
            "buffer": buffer_index,
            "byteOffset": converted_offset,
            "byteLength": len(converted),
            "target": 34962,
            "name": f"A380X_XP12_vec4_weights_accessor_{accessor_index}",
        }
    )
    accessor["bufferView"] = len(document["bufferViews"]) - 1
    accessor["byteOffset"] = 0
    accessor["componentType"] = 5126
    accessor["type"] = "VEC4"
    accessor.pop("normalized", None)


def sanitize_joint_indices(
    document: dict[str, Any],
    output_document: Path,
    binary_buffers: dict[int, bytearray],
    buffer_paths: dict[int, Path],
    protected_joint_accessors: set[int] | None = None,
) -> int:
    """Clamp invalid ASOBO joint indices to the skin's neutral/last joint.

    Some FBW exterior primitives contain a joint index equal to the number of
    joints in the attached skin. Blender evaluates every VEC4 lane even when
    its corresponding weight is zero, so a single such value aborts import.
    """
    mesh_skin_sizes: dict[int, list[int]] = {}
    for node in document.get("nodes", []):
        mesh_index = node.get("mesh")
        skin_index = node.get("skin")
        if not isinstance(mesh_index, int) or not isinstance(skin_index, int):
            continue
        joints = document.get("skins", [])[skin_index].get("joints", [])
        if joints:
            mesh_skin_sizes.setdefault(mesh_index, []).append(len(joints))

    accessor_limits: dict[int, int] = {}
    for mesh_index, mesh in enumerate(document.get("meshes", [])):
        sizes = mesh_skin_sizes.get(mesh_index)
        if not sizes:
            continue
        joint_limit = min(sizes)
        for primitive in mesh.get("primitives", []):
            accessor_index = primitive.get("attributes", {}).get("JOINTS_0")
            if isinstance(accessor_index, int):
                previous = accessor_limits.get(accessor_index, joint_limit)
                accessor_limits[accessor_index] = min(previous, joint_limit)

    component_formats = {5121: "B", 5123: "H"}
    repaired = 0
    for accessor_index, joint_limit in accessor_limits.items():
        if accessor_index in (protected_joint_accessors or ()):
            continue
        accessor = document["accessors"][accessor_index]
        if accessor.get("type") != "VEC4":
            continue
        component_format = component_formats.get(accessor.get("componentType"))
        if component_format is None:
            continue
        source_view = document["bufferViews"][accessor["bufferView"]]
        buffer_index = source_view.get("buffer", 0)
        buffer_info = document["buffers"][buffer_index]
        buffer_uri = buffer_info.get("uri")
        if not isinstance(buffer_uri, str) or is_embedded_uri(buffer_uri):
            raise RuntimeError("Joint-index repair requires an external buffer URI")
        buffer_path = output_document.parent / normalized_uri(buffer_uri)
        binary = binary_buffers.get(buffer_index)
        if binary is None:
            binary = bytearray(buffer_path.read_bytes())
            binary_buffers[buffer_index] = binary
            buffer_paths[buffer_index] = buffer_path

        value_format = f"<4{component_format}"
        value_size = struct.calcsize(value_format)
        source_offset = source_view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        source_stride = source_view.get("byteStride", value_size)
        replacement = max(0, joint_limit - 1)
        for item_index in range(accessor["count"]):
            item_offset = source_offset + item_index * source_stride
            values = list(struct.unpack_from(value_format, binary, item_offset))
            corrected = [value if value < joint_limit else replacement for value in values]
            if corrected != values:
                struct.pack_into(value_format, binary, item_offset, *corrected)
                repaired += sum(a != b for a, b in zip(values, corrected))
    return repaired


def normalize_optimized_vertex_attributes(
    document: dict[str, Any],
    output_document: Path,
    *, protected_joint_accessors: set[int] | None = None,
) -> dict[str, int]:
    """Translate ASOBO's packed vertex declarations to standard glTF.

    The optimized format stores UVs and 16-bit colors as IEEE binary16 values
    while declaring integer component types. Normals use four signed bytes even
    though glTF requires a three-component NORMAL. Reusing the original
    interleaved stride is safe for the byte-packed attributes; half values are
    expanded into new float32 buffer views.
    """
    statistics = {
        "half_float_accessors": 0,
        "normal_accessors": 0,
        "tangent_accessors": 0,
        "byte_color_accessors": 0,
        "scalar_weight_accessors": 0,
        "sanitized_joint_indices": 0,
    }
    # A large ASOBO model can have hundreds of packed UV/color accessors in
    # one buffer. Keep each buffer in memory and flush it once after all of its
    # converted accessors have been appended. Re-reading and rewriting the
    # whole buffer per accessor is especially expensive on external media.
    binary_buffers: dict[int, bytearray] = {}
    buffer_paths: dict[int, Path] = {}
    for accessor_index, semantic_names in accessor_semantics(document).items():
        accessor = document["accessors"][accessor_index]

        if "NORMAL" in semantic_names and accessor.get("componentType") == 5120:
            accessor["type"] = "VEC3"
            accessor["normalized"] = True
            statistics["normal_accessors"] += 1

        if "TANGENT" in semantic_names and accessor.get("componentType") == 5120:
            accessor["normalized"] = True
            statistics["tangent_accessors"] += 1

        if any(name.startswith("TEXCOORD_") for name in semantic_names) and accessor.get(
            "componentType"
        ) == 5122:
            append_float_accessor(
                document,
                accessor_index,
                output_document,
                binary_buffers,
                buffer_paths,
            )
            statistics["half_float_accessors"] += 1

        if "COLOR_0" in semantic_names and accessor.get("componentType") == 5123:
            append_float_accessor(
                document,
                accessor_index,
                output_document,
                binary_buffers,
                buffer_paths,
            )
            statistics["half_float_accessors"] += 1
        elif "COLOR_0" in semantic_names and accessor.get("componentType") == 5120:
            # ASOBO writes unsigned RGBA bytes but labels the storage as signed.
            accessor["componentType"] = 5121
            accessor["normalized"] = True
            statistics["byte_color_accessors"] += 1

        if (
            "WEIGHTS_0" in semantic_names
            and accessor.get("type") == "SCALAR"
            and accessor.get("componentType") == 5126
        ):
            append_scalar_weight_accessor(
                document,
                accessor_index,
                output_document,
                binary_buffers,
                buffer_paths,
            )
            statistics["scalar_weight_accessors"] += 1

    statistics["sanitized_joint_indices"] = sanitize_joint_indices(
        document,
        output_document,
        binary_buffers,
        buffer_paths,
        protected_joint_accessors,
    )

    for buffer_index, binary in binary_buffers.items():
        buffer_paths[buffer_index].write_bytes(binary)

    remove_extension_name(document, "ASOBO_asset_optimized")
    return statistics


def main() -> None:
    arguments = parse_arguments()
    source = arguments.input.resolve()
    output = arguments.output.resolve()
    if source.suffix.casefold() != ".gltf":
        raise ValueError("Only JSON .gltf input is supported; .glb requires no MSFT path rewrite")
    if source == output:
        raise ValueError("Refusing to overwrite the source glTF")

    document = json.loads(source.read_text(encoding="utf-8-sig"))
    promoted = promote_msfs_texture_sources(document)
    output.parent.mkdir(parents=True, exist_ok=True)

    if arguments.geometry_only:
        strip_texture_references(document)
        asset_index: dict[str, list[Path]] = {}
    else:
        asset_index = build_asset_index(arguments.asset_root)

    copied_buffers, copied_images = copy_uri_assets(
        document,
        source,
        output,
        asset_index,
    )
    wing_joints = repair_fbw_wing_joints(document, output) if arguments.repair_fbw_wing_joints else {"status": "disabled"}
    draw_statistics = normalize_asobo_primitive_ranges(document, output)
    optimized_statistics = normalize_optimized_vertex_attributes(
        document, output, protected_joint_accessors=protected_tail_joint_accessors(document)
        if arguments.repair_fbw_tail_bindshape else None)
    tail_bindshape = repair_fbw_tail_bindshape(document, output) if arguments.repair_fbw_tail_bindshape else {"status": "disabled"}
    winding = normalize_msfs_winding(document, output)
    winding_statistics = {key: value for key, value in winding.items() if key != "primitives"}
    document.setdefault("extras", {})["a380x_xp12_normalization"] = {
        "source": str(source),
        "geometry_only": arguments.geometry_only,
        "promoted_msfs_textures": promoted,
        **draw_statistics,
        **optimized_statistics,
        "winding": winding_statistics,
        "experimental_tail_bindshape": tail_bindshape,
        "wing_joint_palette": wing_joints,
    }
    output.write_text(json.dumps(document, indent=2), encoding="utf-8")
    print(
        json.dumps(
            {
                "output": str(output),
                "promoted_msfs_textures": promoted,
                "copied_buffers": copied_buffers,
                "copied_images": copied_images,
                "geometry_only": arguments.geometry_only,
                **draw_statistics,
                **optimized_statistics,
                "winding": winding_statistics,
                "experimental_tail_bindshape": tail_bindshape,
                "wing_joint_palette": wing_joints,
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
