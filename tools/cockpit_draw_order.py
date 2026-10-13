"""Preserve cockpit decal draw order without changing attachment properties.

X-Plane polygon-offset overlays must follow their base geometry. A filename
sort puts ``decal_`` before ``panel_``, which reverses that dependency.
"""

from __future__ import annotations

import re
from dataclasses import dataclass


CURRENT_COCKPIT_DECALS = (
    "decal_a380_cockpit_sticker_8k.obj",
    "decal_a380_pushbutton_decal.obj",
    "decal_a380x_cockpit_decal_emis.obj",
    "decal_a380x_cockpit_decal_lovis.obj",
    "decal_decal_black_noemis.obj",
    "decal_new_decal_noemis.obj",
)

_RECORD = re.compile(rb"^(P _obja/)(\d+)(/([^ \t\r\n]+)[ \t]+[^\r\n]*)(\r?\n)?$")
_COUNT = re.compile(rb"^P _obja/count[ \t]+(\d+)(?:\r?\n)?$")


def _panel_filename(path: str) -> str | None:
    value = path.strip().replace("\\", "/")
    if value.startswith("objects/"):
        value = value[len("objects/"):]
    prefix = "interior/cockpit-panels/"
    if not value.startswith(prefix):
        return None
    filename = value[len(prefix):]
    return filename if "/" not in filename else None


def is_cockpit_panel_decal(path: str) -> bool:
    """Classify current and future decal exports in the cockpit-panel group."""
    filename = _panel_filename(path)
    return bool(filename and filename.startswith("decal_") and filename.endswith(".obj"))


def order_cockpit_attachments(paths: list[str]) -> list[str]:
    """Stable partition; preserve duplicates and order within both groups."""
    return sorted(paths, key=is_cockpit_panel_decal)


def cockpit_decal_order_errors(paths: list[str]) -> list[str]:
    """Report decals followed by panel geometry; native controls are excluded."""
    last_panel = next((path for path in reversed(paths)
                       if _panel_filename(path) and not is_cockpit_panel_decal(path)), None)
    if last_panel is None:
        return []
    last_index = max(index for index, path in enumerate(paths)
                     if _panel_filename(path) and not is_cockpit_panel_decal(path))
    return [f"Cockpit decal {path} precedes base panel {last_panel}"
            for path in paths[:last_index] if is_cockpit_panel_decal(path)]


@dataclass
class _Attachment:
    index: int
    lines: list[bytes]
    fields: set[bytes]
    path: str | None = None

    def render(self, index: int) -> bytes:
        # The prefix/index alone changes; all field values and line endings
        # travel together, including fields unknown to this tool.
        result = []
        for line in self.lines:
            record = _RECORD.fullmatch(line)
            assert record is not None
            result.append(record[1] + str(index).encode("ascii") + record[3] + (record[5] or b""))
        return b"".join(result)


def _parse(data: bytes) -> tuple[list[bytes | _Attachment], list[_Attachment]]:
    tokens: list[bytes | _Attachment] = []
    attachments: list[_Attachment] = []
    counts: list[int] = []
    for line in data.splitlines(keepends=True):
        record = _RECORD.fullmatch(line)
        if record:
            index = int(record[2])
            if tokens and isinstance(tokens[-1], _Attachment) and tokens[-1].index == index:
                block = tokens[-1]
            else:
                if index != len(attachments):
                    raise ValueError("ACF attachments must be complete, contiguous blocks numbered from zero")
                block = _Attachment(index, [], set())
                tokens.append(block)
                attachments.append(block)
            field = record[4]
            if field in block.fields:
                raise ValueError(f"Duplicate attachment field at index {index}: {field.decode('latin1')}")
            block.fields.add(field)
            block.lines.append(line)
            if field == b"_v10_att_file_stl":
                value = record[3].split(None, 1)
                if len(value) != 2 or not value[1].strip():
                    raise ValueError(f"Empty attachment path at index {index}")
                block.path = value[1].decode("latin1")
        else:
            count = _COUNT.fullmatch(line)
            if count:
                counts.append(int(count[1]))
            elif line.startswith(b"P _obja/"):
                raise ValueError(f"Unsupported attachment record: {line!r}")
            tokens.append(line)
    if len(counts) != 1 or counts[0] != len(attachments):
        raise ValueError("ACF must have one attachment count matching its complete record blocks")
    if any(block.path is None for block in attachments):
        raise ValueError("Every ACF attachment requires one _v10_att_file_stl field")
    return tokens, attachments


def reorder_acf_attachments(data: bytes) -> bytes:
    """Return an ACF with stable decal-last ordering and byte-preserved fields.

    Complete numbered attachment blocks are moved between their original
    slots and renumbered. Every nonattachment segment (including count,
    comments, encoding, and line endings) stays byte-identical. Malformed,
    repeated, or incomplete record blocks are rejected before any output.
    This function does no filesystem I/O.
    """
    tokens, blocks = _parse(data)
    ordered = sorted(blocks, key=lambda block: is_cockpit_panel_decal(block.path or ""))
    if all(old is new for old, new in zip(blocks, ordered)):
        return data
    output: list[bytes] = []
    index = 0
    for token in tokens:
        if isinstance(token, _Attachment):
            output.append(ordered[index].render(index))
            index += 1
        else:
            output.append(token)
    result = b"".join(output)
    new_tokens, new_blocks = _parse(result)
    if b"".join(t for t in tokens if isinstance(t, bytes)) != b"".join(
            t for t in new_tokens if isinstance(t, bytes)):
        raise AssertionError("Reordering changed nonattachment bytes")
    if [block.render(0) for block in ordered] != [block.render(0) for block in new_blocks]:
        raise AssertionError("Reordering changed attachment fields")
    return result
