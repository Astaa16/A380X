#!/usr/bin/env python3
"""Identify and optionally decode the FBW Wwise engine media for FMOD authoring."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import subprocess
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path


RTPCS = {
    895650072: "SIMVAR_TURB_ENG_REVERSE_NOZZLE_PERCENT",
    2508093411: "LOCALVAR_A32NX_INTERIOR_ENG_VOLUME",
    2750389324: "SIMVAR_TURB_ENG_N1",
    2750389327: "SIMVAR_TURB_ENG_N2",
    2919001772: "SIMVAR_AIRSPEED_TRUE",
    3763572042: "SIMVAR_PLANE_ALT_ABOVE_GROUND",
    3898595254: "SIMVAR_TURB_ENG_N1_DERIVED",
}
ENGINE_CONTROL_IDS = {895650072, 2750389324, 2750389327, 3898595254}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_txtp(path: Path) -> dict | None:
    text = path.read_text(encoding="utf-8", errors="replace")
    event = re.search(r"^#\s+CAkEvent\[(\d+)\]\s+(\d+)", text, re.MULTILINE)
    if not event:
        return None
    rtpc_ids = {
        int(value)
        for value in re.findall(r"^# - RTPCID: \{(\d+)=", text, re.MULTILINE)
    }
    source_ids = {
        int(value)
        for value in re.findall(r"^#\s+Source\s+(\d+)", text, re.MULTILINE)
    }
    return {
        "hirc_index": int(event.group(1)),
        "event_id": int(event.group(2)),
        "rtpc_ids": rtpc_ids,
        "source_ids": source_ids,
    }


def classify_event(rtpc_ids: set[int]) -> str:
    has_spool_pair = 2750389327 in rtpc_ids or 3898595254 in rtpc_ids
    has_reverse = 895650072 in rtpc_ids
    if has_spool_pair:
        base = "multi_spool_layer"
    else:
        base = "n1_spatial_layer"
    return f"{base}_with_reverse" if has_reverse else base


def collect_events(txtp_dir: Path) -> list[dict]:
    merged: dict[int, dict] = {}
    for path in sorted(txtp_dir.glob("*.txtp")):
        parsed = parse_txtp(path)
        if not parsed or not (parsed["rtpc_ids"] & ENGINE_CONTROL_IDS):
            continue
        event = merged.setdefault(
            parsed["event_id"],
            {
                "hirc_index": parsed["hirc_index"],
                "event_id": parsed["event_id"],
                "txtp_variants": [],
                "rtpc_ids": set(),
                "source_ids": set(),
            },
        )
        event["txtp_variants"].append(path.name)
        event["rtpc_ids"].update(parsed["rtpc_ids"])
        event["source_ids"].update(parsed["source_ids"])

    result = []
    for event in sorted(merged.values(), key=lambda item: item["hirc_index"]):
        rtpc_ids = event.pop("rtpc_ids")
        source_ids = event.pop("source_ids")
        event["classification"] = classify_event(rtpc_ids)
        event["rtpcs"] = [
            {"id": value, "name": RTPCS.get(value, "unresolved")}
            for value in sorted(rtpc_ids)
        ]
        event["source_ids"] = sorted(source_ids)
        result.append(event)
    return result


def expected_engine_events(sound_xml: Path | None) -> list[dict]:
    if sound_xml is None:
        return []
    result = []
    root = ET.parse(sound_xml).getroot()
    for sound in root.iter("Sound"):
        name = sound.attrib.get("WwiseEvent", "")
        if not (name.startswith("Engine_") or name in {"ac1", "ac2"}):
            continue
        result.append({
            "name": name,
            "rtpcs": [
                rtpc.attrib["RTPCName"]
                for rtpc in sound.iter("WwiseRTPC")
                if "RTPCName" in rtpc.attrib
            ],
        })
    return result


def verify_staged(samples: list[dict], vgmstream: str) -> None:
    staged = [sample for sample in samples if "staged_path" in sample]
    if not staged:
        return
    completed = subprocess.run(
        [vgmstream, "-m", "-I", *(sample["staged_path"] for sample in staged)],
        check=True,
        capture_output=True,
        text=True,
    )
    metadata = [json.loads(line) for line in completed.stdout.splitlines() if line.strip()]
    if len(metadata) != len(staged):
        raise RuntimeError(f"vgmstream verified {len(metadata)} of {len(staged)} WAV files")
    for sample, actual in zip(staged, metadata):
        expected_samples = round(sample["duration_seconds"] * sample["sample_rate"])
        actual_loop = actual.get("loopingInfo")
        if actual["sampleRate"] != sample["sample_rate"]:
            raise RuntimeError(f"sample-rate mismatch in {sample['staged_path']}")
        if actual["channels"] != sample["channels"]:
            raise RuntimeError(f"channel-count mismatch in {sample['staged_path']}")
        if abs(actual["numberOfSamples"] - expected_samples) > 1:
            raise RuntimeError(f"duration mismatch in {sample['staged_path']}")
        if (actual_loop is not None) != sample["looped"]:
            raise RuntimeError(f"loop-marker mismatch in {sample['staged_path']}")
        sample["staged_verified"] = True


def stage(
    txtp_dir: Path,
    inventory_path: Path,
    media_root: Path,
    output_dir: Path | None,
    sound_xml: Path | None,
    vgmstream: str,
    force: bool,
) -> dict:
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    indexed = {int(entry["id"]): entry for entry in inventory["entries"]}
    events = collect_events(txtp_dir)
    event_ids_by_source: dict[int, list[int]] = defaultdict(list)
    for event in events:
        for source_id in event["source_ids"]:
            event_ids_by_source[source_id].append(event["event_id"])

    samples = []
    for source_id in sorted(event_ids_by_source):
        if source_id not in indexed:
            raise ValueError(f"source {source_id} is missing from {inventory_path}")
        metadata = indexed[source_id]
        source = media_root / metadata["path"]
        if not source.is_file():
            raise FileNotFoundError(source)
        item = {
            **metadata,
            "sha256": sha256(source),
            "event_ids": sorted(event_ids_by_source[source_id]),
        }
        if output_dir is not None:
            output_dir.mkdir(parents=True, exist_ok=True)
            destination = output_dir / f"{source_id}.wav"
            if force or not destination.is_file():
                subprocess.run(
                    [vgmstream, "-i", "-L", "-o", str(destination), str(source)],
                    check=True,
                    capture_output=True,
                    text=True,
                )
            if destination.stat().st_size <= 44:
                raise RuntimeError(f"decoded WAV is empty: {destination}")
            item["staged_path"] = str(destination)
            item["staged_bytes"] = destination.stat().st_size
            item["staged_sha256"] = sha256(destination)
        samples.append(item)

    verify_staged(samples, vgmstream)

    return {
        "schema_version": 1,
        "selection": {
            "method": "Wwise TXTP graph contains an FBW engine-control RTPC",
            "engine_control_rtpc_ids": sorted(ENGINE_CONTROL_IDS),
            "event_name_mapping": "unresolved; optimized bank does not retain the XML WwiseEvent aliases",
            "excluded": "ac1 and ac2 threshold cues have no engine RTPC and are not selected",
        },
        "sources": {
            "txtp_dir": str(txtp_dir),
            "media_inventory": str(inventory_path),
            "media_root": str(media_root),
            "sound_xml": str(sound_xml) if sound_xml else None,
        },
        "expected_fbw_engine_events": expected_engine_events(sound_xml),
        "events": events,
        "samples": samples,
        "summary": {
            "events": len(events),
            "samples": len(samples),
            "looped_samples": sum(bool(item["looped"]) for item in samples),
            "encoded_bytes": sum(item["bytes"] for item in samples),
            "staged_bytes": sum(item.get("staged_bytes", 0) for item in samples),
            "staged_samples": sum("staged_path" in item for item in samples),
            "verified_staged_samples": sum(bool(item.get("staged_verified")) for item in samples),
        },
        "rights_notice": (
            "FBW-provided compiled audio staged for local development. "
            "Confirm sound-specific redistribution rights before packaging or publishing it."
        ),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--txtp-dir", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    parser.add_argument("--media-root", type=Path, required=True)
    parser.add_argument("--sound-xml", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="JSON selection manifest")
    parser.add_argument("--csv", type=Path, help="optional compact sample index")
    parser.add_argument("--decode-dir", type=Path, help="decode selected WEM files to loop-tagged WAV")
    parser.add_argument("--vgmstream", default="vgmstream-cli")
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    result = stage(
        args.txtp_dir,
        args.inventory,
        args.media_root,
        args.decode_dir,
        args.sound_xml,
        args.vgmstream,
        args.force,
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        fields = [
            "id", "staged_path", "duration_seconds", "sample_rate", "channels",
            "looped", "loop_start_seconds", "loop_end_seconds", "event_ids", "sha256",
        ]
        with args.csv.open("w", newline="", encoding="utf-8") as target:
            writer = csv.DictWriter(target, fieldnames=fields)
            writer.writeheader()
            for sample in result["samples"]:
                row = {key: sample.get(key) for key in fields}
                row["event_ids"] = " ".join(map(str, sample["event_ids"]))
                writer.writerow(row)
    print(json.dumps(result["summary"], indent=2))


if __name__ == "__main__":
    main()
