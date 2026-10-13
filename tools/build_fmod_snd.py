#!/usr/bin/env python3
"""Generate the X-Plane SND attachment file from a reviewable event map."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


OPERATORS = {"<", "<=", "==", "!=", ">=", ">"}
PARTS = {"engine", "tire", "cockpit"}
DATAREF_PREFIXES = ("DELTA=", "ABS_DELTA=")


def bare_dataref(value: str) -> str:
    for prefix in DATAREF_PREFIXES:
        if value.startswith(prefix):
            value = value[len(prefix):]
            break
    return re.sub(r"\[\d+\]$", "", value)


def read_dataref_catalog(path: Path) -> set[str]:
    return {
        line.split("\t", 1)[0]
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines()
        if line.startswith("sim/")
    }


def load_event_map(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema_version") != 1:
        raise ValueError("Unsupported FMOD event-map schema_version")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", data.get("acf_stem", "")):
        raise ValueError("acf_stem must be a filename stem")
    events = data.get("events")
    if not isinstance(events, list) or not events:
        raise ValueError("FMOD event map must contain at least one event")
    return data


def validate_condition(condition: object, event_name: str) -> tuple[str, str, int | float]:
    if not isinstance(condition, list) or len(condition) != 3:
        raise ValueError(f"{event_name}: conditions must be [dataref, operator, number]")
    dataref, operator, value = condition
    if not isinstance(dataref, str) or not dataref:
        raise ValueError(f"{event_name}: condition dataref must be a string")
    if operator not in OPERATORS:
        raise ValueError(f"{event_name}: unsupported operator {operator!r}")
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{event_name}: condition value must be numeric")
    return dataref, operator, value


def render_snd(event_map: dict, known_datarefs: set[str] | None = None) -> str:
    lines = ["A", "1000", "ACF_SOUNDS", ""]
    seen_attachments: set[tuple[str, str, int]] = set()
    for event in event_map["events"]:
        name = event.get("event")
        if not isinstance(name, str) or not name.startswith("event:/"):
            raise ValueError("Every FMOD event name must start with event:/")
        part = event.get("part")
        if part not in PARTS:
            raise ValueError(f"{name}: part must be one of {sorted(PARTS)}")
        indices = event.get("indices")
        if not isinstance(indices, list) or not indices or any(
            isinstance(index, bool) or not isinstance(index, int) or index < 0 for index in indices
        ):
            raise ValueError(f"{name}: indices must be a non-empty list of non-negative integers")
        if len(indices) != len(set(indices)):
            raise ValueError(f"{name}: duplicate attachment indices")
        starts = [validate_condition(item, name) for item in event.get("start_conditions", [])]
        ends = [validate_condition(item, name) for item in event.get("end_conditions", [])]
        if not starts:
            raise ValueError(f"{name}: at least one start condition is required")
        for index in indices:
            identity = (name, part, index)
            if identity in seen_attachments:
                raise ValueError(f"Duplicate FMOD attachment {identity}")
            seen_attachments.add(identity)
            lines.extend([
                "BEGIN_SOUND_ATTACHMENT",
                f"EVENT_NAME {name}",
                f"VEH_PART {part} {index}",
            ])
            if event.get("parameter_dataref_index"):
                lines.append(f"PARAM_DREF_IDX {index}")
            if event.get("allowed_for_ai"):
                lines.append("EVENT_ALLOWED_FOR_AI")
            for directive, conditions in (("EVENT_START_COND", starts), ("EVENT_END_COND", ends)):
                for dataref, operator, value in conditions:
                    expanded = dataref.format(index=index)
                    if "{" in expanded or "}" in expanded:
                        raise ValueError(f"{name}: unsupported condition placeholder in {dataref!r}")
                    if known_datarefs is not None and bare_dataref(expanded) not in known_datarefs:
                        raise ValueError(f"{name}: unknown X-Plane dataref {expanded!r}")
                    lines.append(f"{directive} {expanded} {operator} {value:g}")
            lines.extend(["END_SOUND_ATTACHMENT", ""])
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("event_map", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--datarefs", type=Path, help="Optional X-Plane Resources/plugins/DataRefs.txt")
    args = parser.parse_args()
    catalog = read_dataref_catalog(args.datarefs) if args.datarefs else None
    rendered = render_snd(load_event_map(args.event_map), catalog)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8")
    print(json.dumps({"output": str(args.output), "attachments": rendered.count("BEGIN_SOUND_ATTACHMENT")}, indent=2))


if __name__ == "__main__":
    main()
