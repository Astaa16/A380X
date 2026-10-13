#!/usr/bin/env python3
"""Inventory Wwise event metadata without extracting or converting audio banks."""
from __future__ import annotations

import argparse
import json
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path


def inventory(paths: list[Path]) -> dict:
    files = []
    all_events: set[str] = set()
    for path in paths:
        root = ET.parse(path).getroot()
        groups: Counter[str] = Counter()
        events = []
        for parent in root.iter():
            for sound in parent.findall("./Sound"):
                groups[parent.tag] += 1
                event = sound.get("WwiseEvent")
                if event:
                    events.append(event)
                    all_events.add(event)
        files.append({
            "path": str(path),
            "groups": dict(sorted(groups.items())),
            "event_references": len(events),
            "unique_events": sorted(set(events)),
        })
    return {
        "metadata_only": True,
        "audio_extracted": False,
        "files": files,
        "total_unique_events": len(all_events),
        "unique_events": sorted(all_events),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sound_xml", type=Path, nargs="+")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--wwnames", type=Path, help="Write unique Wwise event names for wwiser")
    args = parser.parse_args()
    result = inventory(args.sound_xml)
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    if args.wwnames:
        args.wwnames.parent.mkdir(parents=True, exist_ok=True)
        args.wwnames.write_text("\n".join(result["unique_events"]) + "\n", encoding="utf-8")
    print(encoded, end="")


if __name__ == "__main__":
    main()
