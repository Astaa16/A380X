#!/usr/bin/env python3
"""Extract ID-named WEM media from an embedded Wwise sound bank."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


def parse_bnk_media(data: bytes) -> list[dict]:
    cursor = 0
    index_payload = None
    media_offset = None
    media_size = None
    while cursor + 8 <= len(data):
        tag = data[cursor:cursor + 4]
        size = struct.unpack_from("<I", data, cursor + 4)[0]
        payload = cursor + 8
        end = payload + size
        if end > len(data):
            raise ValueError(f"Wwise bank chunk {tag!r} extends beyond the file")
        if tag == b"DIDX":
            index_payload = data[payload:end]
        elif tag == b"DATA":
            media_offset, media_size = payload, size
        cursor = end
    if cursor != len(data):
        raise ValueError("Trailing or truncated bytes in Wwise bank")
    if index_payload is None or media_offset is None or media_size is None:
        raise ValueError("Wwise bank does not contain both DIDX and DATA chunks")
    if len(index_payload) % 12:
        raise ValueError("Malformed Wwise DIDX chunk")
    entries = []
    for offset in range(0, len(index_payload), 12):
        media_id, relative_offset, size = struct.unpack_from("<III", index_payload, offset)
        if relative_offset + size > media_size:
            raise ValueError(f"Wwise media {media_id} extends beyond DATA")
        entries.append({"id": media_id, "offset": media_offset + relative_offset, "size": size})
    return entries


def extract_bnk_media(source: Path, output: Path) -> dict:
    data = source.read_bytes()
    entries = parse_bnk_media(data)
    output.mkdir(parents=True, exist_ok=True)
    written = []
    for entry in entries:
        destination = output / f'{entry["id"]}.wem'
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
        "entries": written,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    result = extract_bnk_media(args.source, args.output)
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
