#!/usr/bin/env python3
"""Convert FBW's block-compressed KTX2 textures to PNG and/or DDS.

The FBW build assets use unsupercompressed BC3, BC5 and BC7 payloads inside
KTX2 containers. Pillow can decode those formats from DDS, so this tool wraps
the existing mip payloads in a standards-compliant DX10 DDS header without
loss, then optionally decodes mip level 0 to PNG.
"""

from __future__ import annotations

import argparse
import io
import struct
from pathlib import Path

from PIL import Image


KTX2_IDENTIFIER = b"\xabKTX 20\xbb\r\n\x1a\n"
VK_TO_DXGI = {
    131: 71,   # BC1 RGB UNORM -> BC1 UNORM
    132: 72,   # BC1 RGB SRGB -> BC1 SRGB
    133: 71,   # BC1 RGBA UNORM -> BC1 UNORM
    134: 72,   # BC1 RGBA SRGB -> BC1 SRGB
    135: 74,   # BC2 UNORM
    136: 75,   # BC2 SRGB
    137: 77,   # BC3 UNORM
    138: 78,   # BC3 SRGB
    139: 80,   # BC4 UNORM
    140: 81,   # BC4 SNORM
    141: 83,   # BC5 UNORM
    142: 84,   # BC5 SNORM
    145: 98,   # BC7 UNORM
    146: 99,   # BC7 SRGB
}


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path, help="A .ktx2 file or directory")
    parser.add_argument("output", type=Path, help="Output directory")
    parser.add_argument(
        "--write-dds",
        action="store_true",
        help="Also keep a lossless DDS wrapper containing all original mip levels",
    )
    parser.add_argument(
        "--stem",
        action="append",
        default=[],
        help="Convert only this output stem (repeatable, for example image-011)",
    )
    return parser.parse_args()


def source_files(path: Path) -> list[Path]:
    if path.is_file():
        return [path]
    files = sorted(
        candidate
        for candidate in path.rglob("*")
        if candidate.is_file() and candidate.suffix.casefold() == ".ktx2"
    )
    if not files:
        raise FileNotFoundError(f"No KTX2 files found under {path}")
    return files


def output_stem(path: Path) -> str:
    name = path.name
    while Path(name).suffix.casefold() in {".ktx2", ".png", ".dds"}:
        name = Path(name).stem
    return name


def dds_header(
    width: int,
    height: int,
    mip_count: int,
    level_zero_size: int,
    dxgi_format: int,
) -> bytes:
    ddsd_caps = 0x1
    ddsd_height = 0x2
    ddsd_width = 0x4
    ddsd_pixel_format = 0x1000
    ddsd_mipmap_count = 0x20000
    ddsd_linear_size = 0x80000
    flags = ddsd_caps | ddsd_height | ddsd_width | ddsd_pixel_format | ddsd_linear_size
    if mip_count > 1:
        flags |= ddsd_mipmap_count

    ddpf_fourcc = 0x4
    fourcc_dx10 = struct.unpack("<I", b"DX10")[0]
    dds_caps_texture = 0x1000
    dds_caps_complex = 0x8
    dds_caps_mipmap = 0x400000
    caps = dds_caps_texture
    if mip_count > 1:
        caps |= dds_caps_complex | dds_caps_mipmap

    header_values = [
        124,
        flags,
        height,
        width,
        level_zero_size,
        0,
        mip_count,
        *([0] * 11),
        32,
        ddpf_fourcc,
        fourcc_dx10,
        0,
        0,
        0,
        0,
        0,
        caps,
        0,
        0,
        0,
        0,
    ]
    if len(header_values) != 31:
        raise AssertionError("DDS header must contain 31 uint32 values")
    # DDS_DIMENSION_TEXTURE2D, misc flag, array size, alpha-mode unknown.
    dx10_values = (dxgi_format, 3, 0, 1, 0)
    return b"DDS " + struct.pack("<31I", *header_values) + struct.pack("<5I", *dx10_values)


def ktx2_to_dds(path: Path) -> tuple[bytes, dict[str, int]]:
    payload = path.read_bytes()
    if payload[:12] != KTX2_IDENTIFIER:
        raise ValueError(f"Not a KTX2 file: {path}")

    (
        vk_format,
        _type_size,
        width,
        height,
        depth,
        layer_count,
        face_count,
        level_count,
        supercompression,
    ) = struct.unpack_from("<9I", payload, 12)
    if vk_format not in VK_TO_DXGI:
        raise ValueError(f"Unsupported Vulkan format {vk_format} in {path}")
    if depth not in (0, 1) or layer_count not in (0, 1) or face_count != 1:
        raise ValueError("Only non-array 2D textures are supported")
    if supercompression != 0:
        raise ValueError("Supercompressed KTX2 input must be transcoded before DDS wrapping")
    level_count = level_count or 1

    levels: list[tuple[int, int, int]] = []
    for level_index in range(level_count):
        levels.append(struct.unpack_from("<3Q", payload, 80 + level_index * 24))
    for offset, byte_length, _uncompressed_length in levels:
        if offset + byte_length > len(payload):
            raise ValueError(f"KTX2 level exceeds file size in {path}")

    header = dds_header(width, height, level_count, levels[0][1], VK_TO_DXGI[vk_format])
    # DDS stores the largest/base level first, followed by decreasing mip levels.
    dds_payload = b"".join(payload[offset : offset + byte_length] for offset, byte_length, _ in levels)
    return header + dds_payload, {
        "vk_format": vk_format,
        "dxgi_format": VK_TO_DXGI[vk_format],
        "width": width,
        "height": height,
        "mip_count": level_count,
    }


def main() -> None:
    arguments = parse_arguments()
    arguments.output.mkdir(parents=True, exist_ok=True)
    selected_stems = set(arguments.stem)
    sources = [
        source
        for source in source_files(arguments.input)
        if not selected_stems or output_stem(source) in selected_stems
    ]
    if selected_stems:
        found_stems = {output_stem(source) for source in sources}
        missing = selected_stems - found_stems
        if missing:
            raise FileNotFoundError(f"Requested KTX2 stems were not found: {sorted(missing)}")
    for source in sources:
        dds, information = ktx2_to_dds(source)
        stem = output_stem(source)
        dds_path = arguments.output / f"{stem}.dds"
        png_path = arguments.output / f"{stem}.png"
        if arguments.write_dds:
            dds_path.write_bytes(dds)
        with Image.open(io.BytesIO(dds)) as image:
            image.load()
            image.save(png_path, format="PNG", optimize=True)
        print(
            f"{source.name}: {information['width']}x{information['height']} "
            f"VK={information['vk_format']} DXGI={information['dxgi_format']} "
            f"mips={information['mip_count']} -> {png_path.name}"
        )


if __name__ == "__main__":
    main()
