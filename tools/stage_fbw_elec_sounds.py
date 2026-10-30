#!/usr/bin/env python3
"""Stage the FBW cockpit click samples used by the native ELEC overhead panel.

The FlyByWire A380X ships its cockpit interaction sounds in the Wwise packages
``FBW_A320_NEO_{1,2,3}.PC.PCK`` (common/sound).  Those banks are *optimized*: the
event names referenced by the cockpit ``sound.xml`` (``pb1on``, ``pb1off``,
``mpb1on``, ``firepbguard`` ...) are not stored and their FNV-1 short IDs do not
occur in the banks (verified against every identifier in the FBW repository), so
an event cannot be selected by name.  The samples below were therefore selected
from the bank graph (Event -> Play action -> random container -> Sound -> media)
and their acoustic profile:

* ``pb_press`` / ``pb_release`` - the two sibling two-variant containers whose
  every variant is a ~80-95 ms broadband double transient (cap travel + latch),
  the signature of an Airbus pushbutton-switch press and its return.
* ``guard`` - a two-variant container of single-impact ~0.2 s plastic snaps used
  for the guard opening (variant A) and closing (variant B).

No sample is synthesized or edited: each WEM is decoded with vgmstream, down-mixed
to mono when stereo, and written as raw little-endian PCM16 at the source rate.

    python3 tools/stage_fbw_elec_sounds.py \
        --sound-dir <upstream-aircraft-large-files>/fbw-a380x/.../FlyByWire_A380X/common/sound \
        --out native/avionics/sounds
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract_wwise_bnk_media import parse_bnk_media  # noqa: E402
from extract_wwise_pck import parse_pck  # noqa: E402

PACKAGE = "FBW_A320_NEO_1.PC.PCK"
SAMPLE_RATE = 44100
# output file -> (Wwise event ID, random-container ID, media ID, role)
SELECTION = {
    "elec_pb_press_a.pcm": (3703968208, 248233517, 136777599, "pushbutton press (cap travel + latch), variant A"),
    "elec_pb_press_b.pcm": (3703968208, 248233517, 632899204, "pushbutton press (cap travel + latch), variant B"),
    "elec_pb_release_a.pcm": (1674450798, 492115419, 578881204, "pushbutton release, variant A"),
    "elec_pb_release_b.pcm": (1674450798, 492115419, 1000562486, "pushbutton release, variant B"),
    "elec_guard_open.pcm": (2073564337, 897668303, 897566525, "guard cover snap, variant A (open)"),
    "elec_guard_close.pcm": (2073564337, 897668303, 976977767, "guard cover snap, variant B (close)"),
}
LICENSE_NOTE = (
    "Extracted from the FlyByWire Simulations A380X package (GPL-3.0 repository). "
    "The FlyByWire repository does not carry a separate sound-asset licence; confirm "
    "redistribution rights for the audio with FlyByWire before public distribution "
    "(see README / fmod/README.md)."
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def hirc_objects(bank: bytes) -> dict[int, tuple[int, bytes]]:
    cursor, objects = 0, {}
    while cursor + 8 <= len(bank):
        tag = bank[cursor:cursor + 4]
        size = struct.unpack_from("<I", bank, cursor + 4)[0]
        if tag == b"HIRC":
            body = bank[cursor + 8:cursor + 8 + size]
            offset = 4
            while offset < len(body):
                kind = body[offset]
                length, object_id = struct.unpack_from("<II", body, offset + 1)
                objects[object_id] = (kind, body[offset + 9:offset + 5 + length])
                offset += 5 + length
        cursor += 8 + size
    return objects


def verify_graph(objects: dict[int, tuple[int, bytes]], event: int, container: int, media: int) -> None:
    """Event -> Play(container) and Sound(media) whose direct parent is the container."""
    kind, payload = objects.get(event, (None, b""))
    if kind != 4:
        raise ValueError(f"Wwise event {event} is missing")
    actions = [struct.unpack_from("<I", payload, 1 + 4 * i)[0] for i in range(payload[0])]
    targets = set()
    for action in actions:
        action_kind, action_payload = objects.get(action, (None, b""))
        if action_kind == 3:
            action_type, target = struct.unpack_from("<HI", action_payload, 0)
            if action_type == 0x0403:
                targets.add(target)
    if container not in targets or objects.get(container, (None,))[0] != 5:
        raise ValueError(f"Wwise event {event} does not play random container {container}")
    for kind, payload in objects.values():
        if kind != 2:
            continue
        source = struct.unpack_from("<I", payload, 5)[0]
        if source != media:
            continue
        fx = payload[15]
        cursor = 16 + (1 + 7 * fx if fx else 0) + 1  # NodeInitialFx, bOverrideAttachmentParams
        _bus, parent = struct.unpack_from("<II", payload, cursor)
        if parent != container:
            raise ValueError(f"Media {media} is not a child of container {container}")
        return
    raise ValueError(f"No Wwise sound object plays media {media}")


def decode(wem: bytes, vgmstream: str, work: Path) -> tuple[bytes, dict]:
    source, target = work / "in.wem", work / "out.wav"
    source.write_bytes(wem)
    subprocess.run([vgmstream, "-o", str(target), str(source)], check=True, capture_output=True)
    with wave.open(str(target)) as decoded:
        channels, rate, width = decoded.getnchannels(), decoded.getframerate(), decoded.getsampwidth()
        frames = decoded.readframes(decoded.getnframes())
    if width != 2 or rate != SAMPLE_RATE or channels not in (1, 2):
        raise ValueError(f"Unexpected decoded format: {channels} ch, {rate} Hz, {8 * width} bit")
    samples = struct.unpack(f"<{len(frames) // 2}h", frames)
    if channels == 2:
        samples = tuple(int(round((samples[i] + samples[i + 1]) / 2)) for i in range(0, len(samples), 2))
    peak = max(abs(value) for value in samples) or 1
    import math
    return struct.pack(f"<{len(samples)}h", *samples), {
        "source_channels": channels, "sample_rate": rate, "frames": len(samples),
        "duration_seconds": round(len(samples) / rate, 4), "peak_dbfs": round(20 * math.log10(peak / 32768), 2)}


def stage(sound_dir: Path, out: Path, vgmstream: str) -> dict:
    package = sound_dir / PACKAGE
    data = package.read_bytes()
    entries = parse_pck(data)["entries"]
    banks = [entry for entry in entries if entry["kind"] == "bank"]
    if len(banks) != 1:
        raise ValueError(f"{PACKAGE}: expected exactly one sound bank")
    bank = data[banks[0]["offset"]:banks[0]["offset"] + banks[0]["size"]]
    media = {row["id"]: bank[row["offset"]:row["offset"] + row["size"]] for row in parse_bnk_media(bank)}
    objects = hirc_objects(bank)
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        for name, (event, container, media_id, role) in SELECTION.items():
            verify_graph(objects, event, container, media_id)
            wem = media[media_id]
            pcm, info = decode(wem, vgmstream, work)
            (out / name).write_bytes(pcm)
            rows.append({"file": name, "role": role, "wwise_event_id": event,
                         "wwise_random_container_id": container, "wwise_media_id": media_id,
                         "wem_sha256": sha256(wem), "pcm_sha256": sha256(pcm),
                         "format": "raw PCM16 little-endian mono", **info})
    return {"package": PACKAGE, "package_sha256": sha256(data), "bank_id": banks[0]["id"],
            "event_names_available": False, "license": LICENSE_NOTE, "files": rows}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sound-dir", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--vgmstream", default=shutil.which("vgmstream-cli") or "vgmstream-cli")
    args = parser.parse_args()
    manifest = stage(args.sound_dir, args.out, args.vgmstream)
    (args.out / "elec_sounds_provenance.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"files": [row["file"] for row in manifest["files"]], "out": str(args.out)}))


if __name__ == "__main__":
    main()
