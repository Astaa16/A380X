#!/usr/bin/env python3
"""Inventory and decode the FlyByWire A380X Wwise sound set for the X-Plane port.

Reads the three FBW packages (``FBW_A320_NEO_{1,2,3}.PC.PCK``) read-only, parses
every bank object (see ``sound_wwise_hirc.py``), recovers event names exactly by
hash, resolves each event to its media, containers, loop settings, RTPC curves,
switches and states, cross-references the A380X ``sound.xml`` files, cockpit
behaviours and the FWS TypeScript, and decodes the media to PCM16 WAV with
vgmstream (decode only: no trimming, resampling, mixing or level change; the
Wwise loop region is kept in the WAV ``smpl`` chunk).

    python3 tools/sound_fbw_stage.py \
        --upstream ../upstream-aircraft \
        --large-files ../upstream-aircraft-large-files \
        --out build/fbw-sounds-stage \
        --inventory fmod/fbw-a380x-sound-inventory.json \
        --decode a380

``--decode a380`` (default) decodes the media reachable from events that the
A380X actually posts; ``all`` also decodes the A32NX-only media that share the
packages; ``none`` writes the inventory only.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sound_fbw_names import (  # noqa: E402
    A32NX_RTPC_NAMES, A32NX_SOUND_XML_EVENTS, A32NX_SOUND_XML_URL, CONVENTION_NUMBER_RANGE, CONVENTION_PREFIXES,
    CONVENTION_SUFFIXES, GENERIC_NAMES, classify, variants,
)
from sound_wwise_hirc import (  # noqa: E402
    FNV_BASIS, MSFS_EVENT_PREFIX, MSFS_EVENT_STATE, Bank, ancestors, fnv1_32, fnv1_unwind, load_package,
    msfs_event_id, read_media,
)

SIMOBJECT = "fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X"
SOUND_XML = {
    "common/sound/sound.xml": f"{SIMOBJECT}/common/sound/sound.xml",
    "cockpit/sound/sound.xml": f"{SIMOBJECT}/attachments/flybywire/Part_Interior_Cockpit/sound/sound.xml",
}
BEHAVIOUR_DIR = f"{SIMOBJECT}/attachments/flybywire/Part_Interior_Cockpit/model"
TS_SOURCES = [
    "fbw-a380x/src/systems/systems-host/CpiomC/FlightWarningSystem/FwsSoundManager.ts",
    "fbw-a380x/src/systems/systems-host/Misc/LegacySoundManager.ts",
    "fbw-common/src/systems/datalink/atc/src/DigitalOutputs.ts",
    "fbw-a380x/src/systems/systems-host/Misc/tcas/lib/TcasConstants.ts",
]
PACKAGE_GLOB = "FBW_A320_NEO_*.PC.*"
SOUND_DIR = f"{SIMOBJECT}/common/sound"
NAME_SOURCE_RANK = ["a380x_sound_xml", "a380x_behaviour", "a380x_typescript", "a32nx_sound_xml",
                    "fbw_repo_identifier", "naming_variant", "naming_convention"]
CONFIDENCE = {"a380x_sound_xml": "declared", "a380x_behaviour": "declared", "a380x_typescript": "declared",
              "a32nx_sound_xml": "declared", "fbw_repo_identifier": "repo_identifier",
              "naming_variant": "convention_family"}
A380_SOURCES = {"a380x_sound_xml", "a380x_behaviour", "a380x_typescript"}
LICENSE = {
    "fbw_repository": "flybywiresim/aircraft (A380X sources, snapshot rev 2baa2b35 per config/aircraft_reference.json): "
                      "LICENSE file is the GNU GPL v3; both A380X sound.xml files carry SPDX GPL-3.0 headers.",
    "fbw_readme_license_section": (
        "README 'License' (same text in flybywiresim large-files README): original source code is GPLv3, "
        "original 3D assets are CC BY-NC 4.0, MSFS-derived assets fall under Microsoft's Game Content Usage Rules, "
        "and distribution packages are licensed per those three classes. Audio is not named in any class."),
    "status": "UNCONFIRMED for redistribution: the Wwise packages are binary build products with no sound-specific "
              "licence, author credit or source session in the repository. Treat the decoded audio as local "
              "development material until FlyByWire confirms the audio licence in writing.",
    "msfs_base_package": "Events that exist only in the MSFS 'Asobo_A320_NEO' main package (e.g. button_switch_generic, "
                         "fmc_push_button_on, wind_sound, rain_sound, aural_5ft) are Asobo/Microsoft content that is not "
                         "in the FBW repository and must not be ported.",
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


# --------------------------------------------------------------------------- upstream references
def sound_xml_references(upstream: Path) -> list[dict]:
    refs = []
    for label, relative in SOUND_XML.items():
        path = upstream / relative
        root = ET.parse(path).getroot()
        for group in root:
            for sound in group.iter("Sound"):
                name = sound.attrib.get("WwiseEvent")
                if not name:
                    continue
                entry = {"file": label, "group": group.tag, "event": name, "attributes": dict(sound.attrib)}
                entry["rtpcs"] = [dict(rtpc.attrib) for rtpc in sound.findall("WwiseRTPC")]
                ranges = [dict(item.attrib) for item in sound.findall("Range")]
                if ranges:
                    entry["range"] = ranges
                requires = []
                for item in sound.findall("Requires"):
                    requirement = dict(item.attrib)
                    requirement["range"] = [dict(r.attrib) for r in item.findall("Range")]
                    requires.append(requirement)
                if requires:
                    entry["requires"] = requires
                refs.append(entry)
        # The cockpit XML comments document pb1on/pb1off (latching) vs mpb1on/mpb1off (momentary).
    return refs


def behaviour_references(upstream: Path) -> dict[str, list[str]]:
    usage: dict[str, list[str]] = defaultdict(list)
    for path in sorted((upstream / BEHAVIOUR_DIR).rglob("*.xml")):
        text = path.read_text(encoding="utf-8", errors="replace")
        for tag, value in re.findall(r"<(WWISE_EVENT[A-Z0-9_]*)>\s*([^<#\s][^<]*?)\s*</", text):
            usage[value].append(f"{path.relative_to(upstream / BEHAVIOUR_DIR)}:{tag}")
    return usage


def typescript_references(upstream: Path) -> dict[str, list[str]]:
    usage: dict[str, list[str]] = defaultdict(list)
    for relative in TS_SOURCES:
        path = upstream / relative
        text = path.read_text(encoding="utf-8")
        for value in re.findall(r"wwiseEventName:\s*'([^']+)'", text):
            usage[value].append(f"{relative}:wwiseEventName")
        for value in re.findall(r"name:\s*'([^']+)'", text):
            usage[value].append(f"{relative}:LegacySound")
        for value in re.findall(r"PLAY_INSTRUMENT_SOUND',\s*'([^']+)'", text):
            usage[value].append(f"{relative}:PLAY_INSTRUMENT_SOUND")
    return usage


def fws_aurals(upstream: Path) -> dict[str, dict]:
    """FwsAuralsList entries: key -> {localVarName|wwiseEventName, priority, type, length, ...}."""
    text = (upstream / TS_SOURCES[0]).read_text(encoding="utf-8")
    body = text.split("export const FwsAuralsList", 1)[1].split("\n};", 1)[0]
    result = {}
    for key, block in re.findall(r"\n  (\w+): \{(.*?)\n  \}", body, re.S):
        item = {}
        for field_name, value in re.findall(r"(\w+):\s*([^,\n]+),", block):
            value = value.strip().strip("'")
            item[field_name] = value
        result[key] = item
    return result


def repo_identifiers(upstream: Path) -> set[str]:
    tokens: set[str] = set()
    skip_suffix = (".png", ".jpg", ".jpeg", ".dds", ".ktx2", ".bin", ".glb", ".ttf", ".otf", ".woff",
                   ".woff2", ".pck", ".wasm", ".ico", ".webp", ".zip", ".gz", ".pdf", ".svg", ".gltf")
    for top in ("fbw-a380x", "fbw-common"):
        for directory, dirs, files in os.walk(upstream / top):
            dirs[:] = [d for d in dirs if d not in ("node_modules", ".git", "dist", "out")]
            for name in files:
                if name.lower().endswith(skip_suffix):
                    continue
                path = Path(directory) / name
                try:
                    if path.stat().st_size > 8_000_000:
                        continue
                    text = path.read_text(encoding="utf-8", errors="ignore")
                except OSError:
                    continue
                for token in re.findall(r"[A-Za-z0-9_.\-]{2,64}", text):
                    tokens.add(token)
                    tokens.update(part for part in re.split(r"[.\-]", token) if len(part) >= 2)
                # SimVar names with spaces -> SIMVAR_ game-parameter convention
                for simvar in re.findall(r"['\"]([A-Z][A-Z0-9 ]{3,60}?)(?::\d+)?['\"]", text):
                    tokens.add("SIMVAR_" + simvar.strip().replace(" ", "_"))
    return tokens


# --------------------------------------------------------------------------- name recovery
def recover_event_names(event_ids: set[int], sources: dict[str, set[str]]) -> dict[int, dict]:
    names: dict[int, dict] = {}
    for source in NAME_SOURCE_RANK:
        for candidate in sorted(sources.get(source, ())):
            event_id = msfs_event_id(candidate)
            if event_id not in event_ids:
                continue
            entry = names.setdefault(event_id, {"name": candidate, "name_source": source, "aliases": []})
            if entry["name"].lower() != candidate.lower() and candidate not in entry["aliases"]:
                entry["aliases"].append(candidate)
            if entry["name_source"] != source and source not in entry.setdefault("also_in", []):
                entry["also_in"].append(source)
    return names


def convention_names(unnamed: set[int], stems: set[str]) -> dict[int, dict]:
    """Exact FNV matches of PREFIX + stem + SUFFIX for events that no declared name explains.

    A match is a corroborated family when another event matches the same name shape (digit runs
    and a trailing L/R side letter abstracted, e.g. ``#_380`` or ``flyover<side>``); an isolated
    match is kept but flagged, because this search tests ~1e8 combinations per event (about a 2 %
    chance per event of an accidental 32-bit match).
    """
    stems = {s.lower() for s in stems} | {str(n) for n in CONVENTION_NUMBER_RANGE}
    forward: dict[int, tuple[str, str]] = {}
    for prefix in CONVENTION_PREFIXES:
        state = fnv1_32(prefix, MSFS_EVENT_STATE)
        for stem in stems:
            forward.setdefault(fnv1_32(stem, state), (prefix, stem))
    matches: dict[int, list[tuple[str, str, str]]] = defaultdict(list)
    for event_id in unnamed:
        for suffix in CONVENTION_SUFFIXES:
            hit = forward.get(fnv1_unwind(event_id, suffix))
            if hit:
                matches[event_id].append((hit[0], hit[1], suffix))

    def family_key(name: str) -> str:
        return re.sub(r"[lr]$", "<side>", re.sub(r"\d+", "#", name))

    families: dict[str, set[int]] = defaultdict(set)
    for event_id, hits in matches.items():
        for prefix, stem, suffix in hits:
            families[family_key(prefix + stem + suffix)].add(event_id)
    result = {}
    for event_id, hits in matches.items():
        names = sorted({p + s + x for p, s, x in hits})
        family = any(len(families[family_key(name)]) > 1 for name in names)
        result[event_id] = {"name": names[0], "name_source": "naming_convention",
                            "confidence": "convention_family" if family else "convention_isolated_low_confidence",
                            "aliases": names[1:]}
    return result


def recover_plain_names(ids: set[int], candidates: set[str]) -> dict[int, str]:
    result: dict[int, str] = {}
    for candidate in sorted(candidates, key=lambda c: (len(c), c)):
        value = fnv1_32(candidate)
        if value in ids and value not in result:
            result[value] = candidate
    return result


# --------------------------------------------------------------------------- graph resolution
ADDITIVE_PROPS = ("Volume", "Pitch", "LPF", "HPF", "MakeUpGain", "BusVolume", "OutputBusVolume")


def describe_tree(objects: dict[int, dict], node_id: int, depth: int = 0) -> dict:
    node = objects.get(node_id)
    if node is None:
        return {"id": node_id, "type": "external_or_missing"}
    item: dict = {"id": node_id, "type": node["type"]}
    if node["type"] == "Sound":
        item["media_id"] = node["source_id"]
        return item
    if node["type"] == "RanSeqCntr":
        item.update({"mode": node["mode"], "random_mode": node["random_mode"], "continuous": node["continuous"],
                     "loop_count": node["loop_count"], "transition": node["transition_mode"],
                     "transition_time_s": node["transition_time"][0], "avoid_repeat": node["avoid_repeat"],
                     "playlist_weights": [p["weight"] for p in node["playlist"]]})
    elif node["type"] == "SwitchCntr":
        item.update({"group_type": node["group_type"], "group_id": node["group_id"],
                     "default_switch": node["default_switch"], "switches": node["switches"]})
    elif node["type"] == "LayerCntr":
        item["layers"] = [{"crossfade_rtpc_id": layer["crossfade_rtpc_id"],
                           "associations": len(layer["associations"])} for layer in node["layers"]]
    if depth < 8:
        item["children"] = [describe_tree(objects, child, depth + 1) for child in node.get("children", [])]
    return item


def collect_leaves(objects: dict[int, dict], node_id: int, path: list[dict], out: list[dict]) -> None:
    node = objects.get(node_id)
    if node is None:
        out.append({"missing_node": node_id, "path": path})
        return
    if node["type"] == "Sound":
        out.append({"sound_id": node_id, "media_id": node["source_id"], "path": path})
        return
    for child in node.get("children", []):
        step: dict = {"id": node_id, "type": node["type"]}
        if node["type"] == "SwitchCntr":
            step["switch_group"] = node["group_id"]
            step["selected_by"] = [s["switch_id"] for s in node["switches"] if child in s["nodes"]]
            step["is_default"] = any(s["switch_id"] == node["default_switch"] and child in s["nodes"]
                                     for s in node["switches"])
        elif node["type"] == "LayerCntr":
            for layer in node["layers"]:
                for assoc in layer["associations"]:
                    if assoc["child"] == child:
                        step["crossfade_rtpc_id"] = layer["crossfade_rtpc_id"]
                        step["crossfade_curve"] = assoc["curve"]
        elif node["type"] == "RanSeqCntr":
            step.update({"mode": node["mode"], "continuous": node["continuous"], "loop_count": node["loop_count"]})
            weights = [p["weight"] for p in node["playlist"] if p["id"] == child]
            if weights:
                step["weight"] = weights[0]
        collect_leaves(objects, child, path + [step], out)


def effective_params(objects: dict[int, dict], sound_id: int, names: dict) -> dict:
    chain = ancestors(objects, sound_id)
    sound = chain[0]
    props: dict[str, float] = {}
    ranged: dict[str, list[float]] = {}
    rtpcs, states, effects = [], [], []
    attenuation = bus = None
    for depth, node in enumerate(chain):
        for fx in node.get("fx", []):
            plugin = objects.get(fx["fx_id"], {})
            effects.append({"on_id": node["id"], "slot": fx["index"], "fx_id": fx["fx_id"],
                            "plugin": plugin.get("plugin_hex"), "fx_object": plugin.get("type")})
        for key, value in node.get("props", {}).items():
            if key in ADDITIVE_PROPS:
                props[key] = round(props.get(key, 0.0) + value, 4)
            elif key not in props:
                props[key] = value
        for key, (low, high) in node.get("ranged_props", {}).items():
            if key not in ranged:
                ranged[key] = [low, high]
        for curve in node.get("rtpcs", []):
            rtpcs.append({**curve, "rtpc": names["rtpc"].get(curve["rtpc_id"]), "on": node["type"],
                          "on_id": node["id"], "depth": depth})
        for group in node.get("state_groups", []):
            states.append({"group_id": group["group_id"], "group": names["plain"].get(group["group_id"]),
                           "states": [{"state": names["plain"].get(s["state_id"], s["state_id"]),
                                       "props": objects.get(s["state_instance_id"], {}).get("props", {})}
                                      for s in group["states"]], "on_id": node["id"]})
        if attenuation is None and "AttenuationID" in node.get("props", {}):
            attenuation = node["props"]["AttenuationID"]
        if bus is None and node.get("override_bus_id"):
            bus = node["override_bus_id"]
    loop = sound.get("props", {}).get("Loop")
    att = objects.get(attenuation) if attenuation else None
    attenuation_summary = None
    if att:
        dry = att["curves"].get("VolumeDry")
        attenuation_summary = {"id": attenuation, "max_distance_m": dry["points"][-1]["x"] if dry else None,
                               "volume_dry": dry, "lowpass": att["curves"].get("LowPassFilter"),
                               "cone": att.get("cone"), "rtpcs": att.get("rtpcs")}
    return {
        "looping": loop is not None and loop != 1, "loop_count": loop if loop is not None else 1,
        "infinite_loop": loop == 0, "props": props, "ranged_props": ranged, "rtpcs": rtpcs, "states": states,
        "attenuation": attenuation_summary, "output_bus_id": bus, "effects": effects,
        "output_bus": names["plain"].get(bus) if bus else None,
        "stream_type": sound["stream_type"], "codec": sound["codec"],
        "positioning": next((n["positioning"] for n in chain if n.get("positioning", {}).get("override_parent")),
                            None),
    }


# --------------------------------------------------------------------------- media
def probe_and_decode(jobs: list[dict], work: Path, vgmstream: str, workers: int) -> None:
    """jobs: {package, bank, media_id, decode_to|None}; fills metadata in place."""
    work.mkdir(parents=True, exist_ok=True)

    def run(job: dict) -> None:
        wem = read_media(job["package"], job["bank"], job["media_id"])
        job["wem_bytes"] = len(wem)
        job["wem_sha256"] = sha256_bytes(wem)
        source = work / f"{job['media_id']}.wem"
        source.write_bytes(wem)
        try:
            meta = json.loads(subprocess.run([vgmstream, "-m", "-I", str(source)], check=True,
                                             capture_output=True, text=True).stdout.strip().splitlines()[-1])
            rate, frames = meta["sampleRate"], meta["numberOfSamples"]
            loop = meta.get("loopingInfo")
            job["format"] = {"encoding": meta.get("encoding"), "sample_rate": rate, "channels": meta["channels"],
                             "frames": frames, "duration_s": round(frames / rate, 4),
                             "bitrate": meta.get("bitrate")}
            job["loop"] = ({"start_frame": loop["start"], "end_frame": loop["end"],
                            "start_s": round(loop["start"] / rate, 4), "end_s": round(loop["end"] / rate, 4)}
                           if loop else None)
            if job.get("decode_to"):
                target: Path = job["decode_to"]
                if not target.is_file() or target.stat().st_size <= 44:
                    subprocess.run([vgmstream, "-i", "-L", "-o", str(target), str(source)], check=True,
                                   capture_output=True, text=True)
                job["wav_bytes"] = target.stat().st_size
                job["wav_sha256"] = sha256_file(target)
                expected = 44 + frames * meta["channels"] * 2
                if job["wav_bytes"] < expected:
                    raise RuntimeError(f"short decode for {job['media_id']}")
        finally:
            source.unlink(missing_ok=True)

    with ThreadPoolExecutor(max_workers=workers) as pool:
        list(pool.map(run, jobs))


# --------------------------------------------------------------------------- main
def stage(args: argparse.Namespace) -> dict:
    upstream, large = args.upstream.resolve(), args.large_files.resolve()
    packages = sorted((large / SOUND_DIR).glob(PACKAGE_GLOB))
    if len(packages) != 3:
        raise SystemExit(f"expected 3 FBW packages in {large / SOUND_DIR}, found {len(packages)}")
    banks: list[tuple[Path, Bank]] = [(path, load_package(path)) for path in packages]

    xml_refs = sound_xml_references(upstream)
    behaviour = behaviour_references(upstream)
    typescript = typescript_references(upstream)
    aurals = fws_aurals(upstream)
    tokens = repo_identifiers(upstream)

    a380_xml = {ref["event"] for ref in xml_refs}
    sources = {
        "a380x_sound_xml": a380_xml,
        "a380x_behaviour": set(behaviour),
        "a380x_typescript": set(typescript),
        "a32nx_sound_xml": set(A32NX_SOUND_XML_EVENTS),
        "fbw_repo_identifier": tokens,
        "naming_variant": {v for base in a380_xml | set(A32NX_SOUND_XML_EVENTS) for v in variants(base)},
    }
    event_ids = {obj_id for _, bank in banks for obj_id, obj in bank.objects.items() if obj["type"] == "Event"}
    event_names = recover_event_names(event_ids, sources)
    for named in event_names.values():
        named["confidence"] = CONFIDENCE[named["name_source"]]
    event_names.update(convention_names(event_ids - set(event_names), tokens | set(A32NX_SOUND_XML_EVENTS)))

    plain_ids: set[int] = set()
    for _, bank in banks:
        for obj in bank.objects.values():
            for curve in obj.get("rtpcs", []):
                plain_ids.add(curve["rtpc_id"])
            for layer in obj.get("layers", []):
                plain_ids.add(layer["crossfade_rtpc_id"])
                for curve in layer["rtpcs"]:
                    plain_ids.add(curve["rtpc_id"])
            if obj["type"] == "SwitchCntr":
                plain_ids.update([obj["group_id"], obj["default_switch"]] + [s["switch_id"] for s in obj["switches"]])
            for group in obj.get("state_groups", []):
                plain_ids.add(group["group_id"])
                plain_ids.update(s["state_id"] for s in group["states"])
            if obj.get("override_bus_id"):
                plain_ids.add(obj["override_bus_id"])
            if obj["type"] == "Action":
                for key in ("state_group_id", "state_id", "switch_group_id", "switch_state_id"):
                    if key in obj:
                        plain_ids.add(obj[key])
    rtpc_candidates = set(A32NX_RTPC_NAMES) | set(GENERIC_NAMES) | tokens
    rtpc_candidates |= {r.get("RTPCName", "") for ref in xml_refs for r in ref["rtpcs"]}
    for ref in xml_refs:
        for rtpc in ref["rtpcs"]:
            var = rtpc.get("LocalVar") or rtpc.get("SimVar") or ""
            stem = ("LOCALVAR_" if rtpc.get("LocalVar") else "SIMVAR_") + var.split(":")[0].replace(" ", "_")
            rtpc_candidates |= {stem, stem + "_DERIVED"}
    plain = recover_plain_names(plain_ids, {c for c in rtpc_candidates if c})
    names = {"rtpc": plain, "plain": plain}

    # ---- events
    xml_by_event: dict[str, list[dict]] = defaultdict(list)
    for ref in xml_refs:
        xml_by_event[ref["event"].lower()].append(ref)
    aural_by_event: dict[str, list[dict]] = defaultdict(list)
    for key, item in aurals.items():
        if "wwiseEventName" in item:
            aural_by_event[item["wwiseEventName"].lower()].append({"key": key, **item})
    aural_by_lvar: dict[str, list[dict]] = defaultdict(list)
    for key, item in aurals.items():
        if "localVarName" in item:
            aural_by_lvar[item["localVarName"]].append({"key": key, **item})

    events = []
    media_jobs: dict[int, dict] = {}
    for path, bank in banks:
        for obj_id in bank.order:
            obj = bank.objects[obj_id]
            if obj["type"] != "Event":
                continue
            named = event_names.get(obj_id)
            name = named["name"] if named else None
            category, sub = classify(name)
            lowered = (name or "").lower()
            refs = xml_by_event.get(lowered, [])
            behaviour_refs = sorted(set(behaviour.get(name, []) + [u for k, v in behaviour.items()
                                                                      if k.lower() == lowered and k != name
                                                                      for u in v])) if name else []
            ts_refs = typescript.get(name, []) if name else []
            fws = list(aural_by_event.get(lowered, []))
            for ref in refs:
                lvar = ref["attributes"].get("LocalVar")
                if lvar:
                    fws.extend(aural_by_lvar.get(lvar, []))
            actions = []
            leaves: list[dict] = []
            for action_id in obj["actions"]:
                action = bank.objects.get(action_id, {"action": "missing", "target_id": None})
                entry = {"id": action_id, "action": action["action"], "target_id": action.get("target_id")}
                for key in ("props", "fade_curve", "state_group_id", "state_id", "switch_group_id",
                            "switch_state_id", "bank_id"):
                    if action.get(key) not in (None, {}, []):
                        entry[key] = action[key]
                if action["action"] == "Play":
                    entry["tree"] = describe_tree(bank.objects, action["target_id"])
                    collect_leaves(bank.objects, action["target_id"], [], leaves)
                actions.append(entry)
            for leaf in leaves:
                if "sound_id" not in leaf:
                    continue
                leaf.update(effective_params(bank.objects, leaf["sound_id"], names))
                for step in leaf["path"]:
                    if "switch_group" in step:
                        step["switch_group_name"] = plain.get(step["switch_group"])
                        step["selected_by_names"] = [plain.get(s, s) for s in step["selected_by"]]
                    if "crossfade_rtpc_id" in step:
                        step["crossfade_rtpc"] = plain.get(step["crossfade_rtpc_id"])
                media_id = leaf["media_id"]
                leaf["media_in_bank"] = media_id in bank.media
                if media_id in bank.media and media_id not in media_jobs:
                    media_jobs[media_id] = {"media_id": media_id, "package": path, "bank": bank,
                                            "package_name": path.name, "events": set(), "a380": False}
                if media_id in media_jobs:
                    media_jobs[media_id]["events"].add(obj_id)
            name_source = named["name_source"] if named else None
            all_sources = ({name_source} | set(named.get("also_in", []))) if named else set()
            used_by_a380 = bool(all_sources & A380_SOURCES)
            for leaf in leaves:
                if used_by_a380 and leaf.get("media_id") in media_jobs:
                    media_jobs[leaf["media_id"]]["a380"] = True
            in_a32nx = bool(named) and any(n.lower() == lowered for n in A32NX_SOUND_XML_EVENTS)
            events.append({
                "event_id": obj_id, "bank": path.name, "bank_id": bank.bank_id, "hirc_index": obj["hirc_index"],
                "name": name, "name_source": name_source, "name_also_in": sorted(all_sources - {name_source}),
                "name_aliases": named.get("aliases", []) if named else [],
                "name_match": f"fnv1_32('{MSFS_EVENT_PREFIX}' + name) == event_id" if named else None,
                "name_confidence": named["confidence"] if named else None,
                "category": category, "subcategory": sub, "used_by_a380x": used_by_a380,
                "a380_specific": used_by_a380 and not in_a32nx, "shared_with_a32nx": in_a32nx,
                "sound_xml": refs, "behaviour": behaviour_refs, "typescript": ts_refs, "fws_aural": fws,
                "actions": actions, "leaves": leaves,
            })

    # ---- media metadata + decode
    out = args.out.resolve()
    wav_dir = out / "wav"
    jobs = []
    for media_id, job in sorted(media_jobs.items()):
        decode = args.decode == "all" or (args.decode == "a380" and job["a380"])
        job["decode_to"] = wav_dir / f"{media_id}.wav" if decode else None
        jobs.append(job)
    if any(job["decode_to"] for job in jobs):
        wav_dir.mkdir(parents=True, exist_ok=True)
    probe_and_decode(jobs, out / ".work", args.vgmstream, args.workers)
    shutil.rmtree(out / ".work", ignore_errors=True)
    # media present in banks but not reachable from any event
    reachable = set(media_jobs)
    orphan = sorted({(m, p.name) for p, b in banks for m in b.media if m not in reachable})

    media_index = {}
    for job in jobs:
        media_index[job["media_id"]] = {
            "media_id": job["media_id"], "package": job["package_name"], "wem_bytes": job["wem_bytes"],
            "wem_sha256": job["wem_sha256"], **job.get("format", {}), "loop": job.get("loop"),
            "event_ids": sorted(job["events"]), "used_by_a380x": job["a380"],
            "wav": f"wav/{job['media_id']}.wav" if job["decode_to"] else None,
            "wav_bytes": job.get("wav_bytes"), "wav_sha256": job.get("wav_sha256"),
        }
    for event in events:
        for leaf in event["leaves"]:
            media = media_index.get(leaf.get("media_id"))
            if media:
                leaf["duration_s"] = media.get("duration_s")
                leaf["media_loop"] = media.get("loop")
                leaf["channels"] = media.get("channels")
                leaf["sample_rate"] = media.get("sample_rate")

    # ---- XML events not present in the FBW banks
    present = {e["name"].lower() for e in events if e["name"]}
    missing_xml = sorted({ref["event"] for ref in xml_refs if ref["event"].lower() not in present})
    missing_behaviour = sorted({n for n in behaviour if n.lower() not in present})
    missing_ts = sorted({n for n in typescript if n.lower() not in present})

    unique = {e["event_id"]: e for e in events}
    summary = {
        "packages": len(banks), "bank_objects": sum(len(b.objects) for _, b in banks),
        "event_objects": len(events), "unique_event_ids": len(unique),
        "named_unique_events": sum(1 for e in unique.values() if e["name"]),
        "unnamed_unique_events": sum(1 for e in unique.values() if not e["name"]),
        "names_by_source": {s: sum(1 for e in unique.values() if e["name_source"] == s) for s in NAME_SOURCE_RANK},
        "names_by_confidence": dict(sorted(_count(e["name_confidence"] or "unnamed" for e in unique.values()))),
        "a380x_used_events": sum(1 for e in unique.values() if e["used_by_a380x"]),
        "a380x_specific_events": sum(1 for e in unique.values() if e["a380_specific"]),
        "events_by_category_a380x": dict(sorted(_count(e["category"] for e in unique.values() if e["used_by_a380x"]))),
        "events_by_category_all": dict(sorted(_count(e["category"] for e in unique.values()))),
        "media_total": sum(len(b.media) for _, b in banks), "media_reachable": len(media_index),
        "media_unreachable": len(orphan),
        "media_a380x": sum(1 for m in media_index.values() if m["used_by_a380x"]),
        "wav_files": sum(1 for m in media_index.values() if m["wav"]),
        "wav_bytes": sum(m["wav_bytes"] or 0 for m in media_index.values()),
        "looping_media_with_markers": sum(1 for m in media_index.values() if m.get("loop")),
        "rtpc_switch_state_ids": len(plain_ids), "rtpc_switch_state_names_recovered": len(plain),
        "sound_xml_events_not_in_fbw_banks": len(missing_xml),
    }
    return {
        "schema_version": 1,
        "generator": "tools/sound_fbw_stage.py",
        "hash_convention": {
            "event": f"fnv1_32('{MSFS_EVENT_PREFIX}' + lower(WwiseEvent)); MSFS posts Play_<MainPackage>_<event>, "
                     "MainPackage = Asobo_A320_NEO in both A380X sound.xml files",
            "game_parameter_switch_state_bus": "fnv1_32(lower(name)) from offset basis 2166136261",
            "fnv1_basis": FNV_BASIS,
        },
        "inputs": {
            "packages": [{"file": str(p.relative_to(large)), "sha256": b.package_sha256, "bank_id": b.bank_id,
                          "bank_version": b.version, "objects": len(b.objects), "media": len(b.media)}
                         for p, b in banks],
            "upstream_sound_xml": list(SOUND_XML.values()), "behaviour_dir": BEHAVIOUR_DIR,
            "typescript": TS_SOURCES, "a32nx_sound_xml": A32NX_SOUND_XML_URL,
        },
        "decode": {"tool": args.vgmstream, "options": "-i -L (ignore loop repetition, write smpl loop chunk)",
                   "format": "RIFF WAVE PCM16 little-endian, source rate and channel count, no editing",
                   "selection": args.decode},
        "license": LICENSE,
        "summary": summary,
        "rtpc_switch_state_names": {str(k): v for k, v in sorted(plain.items())},
        "unresolved_rtpc_switch_state_ids": sorted(plain_ids - set(plain)),
        "sound_xml_events_not_in_fbw_banks": missing_xml,
        "behaviour_events_not_in_fbw_banks": missing_behaviour,
        "typescript_events_not_in_fbw_banks": missing_ts,
        "fws_aurals": aurals,
        "events": events,
        "media": [media_index[k] for k in sorted(media_index)],
        "unreachable_media": [{"media_id": m, "package": p} for m, p in orphan],
    }


def _count(values) -> list[tuple[str, int]]:
    counts: dict[str, int] = defaultdict(int)
    for value in values:
        counts[value] += 1
    return list(counts.items())


def compact_inventory(manifest: dict) -> dict:
    """Small, reviewable inventory for fmod/: one row per unique event id."""
    rows = {}
    for event in manifest["events"]:
        key = event["event_id"]
        if key in rows:
            rows[key]["banks"].append(event["bank"])
            continue
        leaves = [leaf for leaf in event["leaves"] if "sound_id" in leaf]
        rtpcs = sorted({f"{c['rtpc'] or c['rtpc_id']}->{c['param']}" for leaf in leaves for c in leaf["rtpcs"]})
        switches = sorted({f"{s.get('switch_group_name') or s['switch_group']}={'/'.join(map(str, s.get('selected_by_names', [])))}"
                           for leaf in leaves for s in leaf["path"] if "switch_group" in s})
        crossfades = sorted({str(s.get("crossfade_rtpc") or s["crossfade_rtpc_id"])
                             for leaf in leaves for s in leaf["path"] if "crossfade_rtpc_id" in s})
        containers = sorted({f"{s['type']}:{s.get('mode', '')}{'/continuous' if s.get('continuous') else ''}"
                             for leaf in leaves for s in leaf["path"]})
        xml = [{k: v for k, v in {
            "file": r["file"], "group": r["group"], "node": r["attributes"].get("NodeName"),
            "viewpoint": r["attributes"].get("ViewPoint"), "continuous": r["attributes"].get("Continuous"),
            "trigger": r["attributes"].get("LocalVar") or r["attributes"].get("SimVar"),
            "index": r["attributes"].get("Index"), "range": r.get("range"),
            "requires": [{"var": q.get("LocalVar") or q.get("SimVar"), "range": q.get("range")}
                         for q in r.get("requires", [])] or None,
            "fade_out": r["attributes"].get("FadeOutTime"), "cone_heading": r["attributes"].get("ConeHeading"),
            "rtpc_bindings": [f"{(q.get('LocalVar') or q.get('SimVar'))}{':' + q['Index'] if q.get('Index') else ''}"
                              f"{' (derived)' if q.get('Derived') == 'true' else ''}->{q.get('RTPCName')}"
                              for q in r["rtpcs"]] or None,
        }.items() if v not in (None, [], "")} for r in event["sound_xml"]]
        rows[key] = {
            "event_id": key, "name": event["name"], "name_source": event["name_source"],
            "name_confidence": event["name_confidence"],
            "category": event["category"], "subcategory": event["subcategory"],
            "used_by_a380x": event["used_by_a380x"], "a380_specific": event["a380_specific"],
            "shared_with_a32nx": event["shared_with_a32nx"], "banks": [event["bank"]],
            "actions": sorted({a["action"] for a in event["actions"]}),
            "media": [{"id": leaf["media_id"], "dur_s": leaf.get("duration_s"), "ch": leaf.get("channels"),
                       "hz": leaf.get("sample_rate"), "loop": bool(leaf["looping"]),
                       "loop_s": [leaf["media_loop"]["start_s"], leaf["media_loop"]["end_s"]]
                       if leaf.get("media_loop") else None} for leaf in leaves],
            "containers": containers, "rtpc_curves": rtpcs, "switches": switches, "crossfade_rtpcs": crossfades,
            "sound_xml": xml, "behaviour_uses": len(event["behaviour"]), "fws_aural": event["fws_aural"] or None,
        }
    return {
        "schema_version": 1,
        "generator": "tools/sound_fbw_stage.py (compact view of build/fbw-sounds-stage/manifest.json)",
        "hash_convention": manifest["hash_convention"], "license": manifest["license"],
        "summary": manifest["summary"],
        "sound_xml_events_not_in_fbw_banks": manifest["sound_xml_events_not_in_fbw_banks"],
        "events": sorted(rows.values(), key=lambda r: (r["category"], r["subcategory"], (r["name"] or "~").lower(),
                                                        r["event_id"])),
    }


def write_csv(manifest: dict, out: Path) -> None:
    with (out / "events.csv").open("w", newline="", encoding="utf-8") as target:
        writer = csv.writer(target)
        writer.writerow(["event_id", "name", "name_source", "category", "subcategory", "used_by_a380x",
                         "a380_specific", "bank", "media_ids", "max_duration_s", "looping", "rtpcs"])
        for event in manifest["events"]:
            leaves = [leaf for leaf in event["leaves"] if "sound_id" in leaf]
            writer.writerow([event["event_id"], event["name"] or "", event["name_source"] or "", event["category"],
                             event["subcategory"], int(event["used_by_a380x"]), int(event["a380_specific"]),
                             event["bank"], " ".join(str(leaf["media_id"]) for leaf in leaves),
                             max((leaf.get("duration_s") or 0 for leaf in leaves), default=0),
                             int(any(leaf["looping"] for leaf in leaves)),
                             " ".join(sorted({str(c["rtpc"] or c["rtpc_id"]) for leaf in leaves for c in leaf["rtpcs"]}))])
    with (out / "media.csv").open("w", newline="", encoding="utf-8") as target:
        writer = csv.writer(target)
        writer.writerow(["media_id", "package", "wav", "sample_rate", "channels", "duration_s", "loop_start_s",
                         "loop_end_s", "used_by_a380x", "event_ids", "wem_sha256", "wav_sha256"])
        for media in manifest["media"]:
            loop = media.get("loop") or {}
            writer.writerow([media["media_id"], media["package"], media["wav"] or "", media.get("sample_rate"),
                             media.get("channels"), media.get("duration_s"), loop.get("start_s", ""),
                             loop.get("end_s", ""), int(media["used_by_a380x"]),
                             " ".join(map(str, media["event_ids"])), media["wem_sha256"], media["wav_sha256"] or ""])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--upstream", type=Path, required=True)
    parser.add_argument("--large-files", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, help="write the compact inventory JSON here")
    parser.add_argument("--decode", choices=("a380", "all", "none"), default="a380")
    parser.add_argument("--vgmstream", default=shutil.which("vgmstream-cli") or "vgmstream-cli")
    parser.add_argument("--workers", type=int, default=6)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    manifest = stage(args)
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=1) + "\n", encoding="utf-8")
    write_csv(manifest, args.out)
    if args.inventory:
        args.inventory.parent.mkdir(parents=True, exist_ok=True)
        args.inventory.write_text(json.dumps(compact_inventory(manifest), indent=1) + "\n", encoding="utf-8")
    print(json.dumps(manifest["summary"], indent=2))


if __name__ == "__main__":
    main()
