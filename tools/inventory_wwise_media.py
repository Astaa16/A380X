#!/usr/bin/env python3
"""Probe extracted Wwise media with vgmstream and write a searchable inventory."""
from __future__ import annotations

import argparse
import json
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path


def media_entry(path: Path, root: Path, metadata: dict) -> dict:
    sample_rate = metadata["sampleRate"]
    samples = metadata["numberOfSamples"]
    loop = metadata.get("loopingInfo")
    return {
        "id": int(path.stem),
        "path": str(path.relative_to(root)),
        "bytes": path.stat().st_size,
        "sample_rate": sample_rate,
        "channels": metadata["channels"],
        "duration_seconds": samples / sample_rate,
        "looped": loop is not None,
        "loop_start_seconds": loop["start"] / sample_rate if loop else None,
        "loop_end_seconds": loop["end"] / sample_rate if loop else None,
        "encoding": metadata["encoding"],
        "bitrate": metadata.get("bitrate"),
    }


def probe_batch(paths: list[Path], root: Path, vgmstream: str) -> list[dict]:
    completed = subprocess.run(
        [vgmstream, "-m", "-I", *(str(path) for path in paths)],
        check=True,
        capture_output=True,
        text=True,
    )
    metadata = [json.loads(line) for line in completed.stdout.splitlines() if line.strip()]
    if len(metadata) != len(paths):
        raise RuntimeError(f"vgmstream returned {len(metadata)} records for {len(paths)} files")
    return [media_entry(path, root, info) for path, info in zip(paths, metadata)]


def inventory(root: Path, vgmstream: str, workers: int) -> dict:
    media = sorted(root.glob("FBW_A320_NEO_*.PC/media/*.wem"))
    batches = [media[offset:offset + 128] for offset in range(0, len(media), 128)]
    with ThreadPoolExecutor(max_workers=workers) as executor:
        groups = list(executor.map(lambda paths: probe_batch(paths, root, vgmstream), batches))
    entries = [entry for group in groups for entry in group]
    return {
        "root": str(root),
        "probe": vgmstream,
        "entries": entries,
        "summary": {
            "count": len(entries),
            "looped": sum(entry["looped"] for entry in entries),
            "bytes": sum(entry["bytes"] for entry in entries),
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--vgmstream", default="vgmstream-cli")
    parser.add_argument("--workers", type=int, default=8)
    args = parser.parse_args()
    result = inventory(args.root, args.vgmstream, args.workers)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result["summary"], indent=2))


if __name__ == "__main__":
    main()
