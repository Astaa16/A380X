#!/usr/bin/env python3
"""Extract Wwise AKPK packages without modifying the source PCK files."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


def read_u32(data: bytes, offset: int, endian: str) -> tuple[int, int]:
    if offset + 4 > len(data):
        raise ValueError("Unexpected end of PCK header")
    return struct.unpack_from(endian + "I", data, offset)[0], offset + 4


def parse_table(
    data: bytes,
    offset: int,
    section_size: int,
    endian: str,
    kind: str,
) -> tuple[list[dict], int]:
    end = offset + section_size
    if end > len(data):
        raise ValueError(f"{kind} table extends beyond the PCK file")
    if section_size == 0:
        return [], offset
    count, cursor = read_u32(data, offset, endian)
    if count == 0:
        return [], end
    payload_size = section_size - 4
    if payload_size <= 0 or payload_size % count:
        raise ValueError(f"Malformed {kind} table size")
    entry_size = payload_size // count
    if entry_size not in (20, 24):
        raise ValueError(f"Unsupported {kind} entry size {entry_size}")
    entries = []
    for _ in range(count):
        if kind == "external" and entry_size == 24:
            file_id = struct.unpack_from(endian + "Q", data, cursor)[0]
            cursor += 8
        else:
            file_id, cursor = read_u32(data, cursor, endian)
        block_size, cursor = read_u32(data, cursor, endian)
        if entry_size == 24 and kind != "external":
            size = struct.unpack_from(endian + "Q", data, cursor)[0]
            cursor += 8
        else:
            size, cursor = read_u32(data, cursor, endian)
        start_block, cursor = read_u32(data, cursor, endian)
        language_id, cursor = read_u32(data, cursor, endian)
        file_offset = start_block * block_size if block_size else start_block
        if file_offset + size > len(data):
            raise ValueError(f"{kind} entry {file_id} extends beyond the PCK file")
        entries.append({
            "id": file_id,
            "offset": file_offset,
            "size": size,
            "language_id": language_id,
            "kind": kind,
        })
    if cursor != end:
        raise ValueError(f"Malformed {kind} table length")
    return entries, end


def parse_pck(data: bytes) -> dict:
    if data[:4] != b"AKPK":
        raise ValueError("Not a Wwise AKPK package")
    little_flag = struct.unpack_from("<I", data, 8)[0]
    if little_flag == 1:
        endian = "<"
    elif little_flag == 0x01000000:
        endian = ">"
    else:
        raise ValueError("Cannot determine PCK byte order")
    header_size, cursor = read_u32(data, 4, endian)
    _flag, cursor = read_u32(data, cursor, endian)
    language_size, cursor = read_u32(data, cursor, endian)
    bank_size, cursor = read_u32(data, cursor, endian)
    sound_size, cursor = read_u32(data, cursor, endian)
    external_size = 0
    if language_size + bank_size + sound_size + 16 < header_size:
        external_size, cursor = read_u32(data, cursor, endian)
    language_start = cursor
    cursor = language_start + language_size
    if cursor > len(data):
        raise ValueError("Language table extends beyond the PCK file")
    banks, cursor = parse_table(data, cursor, bank_size, endian, "bank")
    sounds, cursor = parse_table(data, cursor, sound_size, endian, "sound")
    externals, cursor = parse_table(data, cursor, external_size, endian, "external")
    return {
        "endianness": "little" if endian == "<" else "big",
        "header_size": header_size,
        "language_table_offset": language_start,
        "entries": banks + sounds + externals,
    }


def extract_pck(source: Path, output: Path) -> dict:
    data = source.read_bytes()
    parsed = parse_pck(data)
    output.mkdir(parents=True, exist_ok=True)
    written = []
    for entry in parsed["entries"]:
        suffix = ".bnk" if entry["kind"] == "bank" else ".wem"
        destination = output / f'{entry["id"]}{suffix}'
        payload = data[entry["offset"]:entry["offset"] + entry["size"]]
        destination.write_bytes(payload)
        written.append({
            **entry,
            "path": destination.name,
            "sha256": hashlib.sha256(payload).hexdigest(),
        })
    return {
        "source": str(source),
        "source_size": len(data),
        "source_sha256": hashlib.sha256(data).hexdigest(),
        "source_untouched": True,
        "endianness": parsed["endianness"],
        "header_size": parsed["header_size"],
        "entries": written,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    result = extract_pck(args.source, args.output)
    encoded = json.dumps(result, indent=2) + "\n"
    if args.manifest:
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(encoded, encoding="utf-8")
    print(json.dumps({
        "source": result["source"],
        "output": str(args.output),
        "entries": len(result["entries"]),
        "bytes": sum(entry["size"] for entry in result["entries"]),
    }, indent=2))


if __name__ == "__main__":
    main()
