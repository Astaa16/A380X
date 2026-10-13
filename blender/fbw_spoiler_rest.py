"""Prove five audited FBW flap tracks are redundant node-rest transforms.

Read-only, pure Python. Call before omitting these exact targets from
``l_flap_percent_key`` ownership; never generalize the returned names to other
clips. glTF float accessors and STEP/LINEAR constant tracks are supported;
sparse accessors, cubic tangents, matrix nodes, and extra target channels fail
closed. Reference: https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
"""
from __future__ import annotations

from contextlib import ExitStack
import math
import os
from pathlib import Path
import struct
from urllib.parse import unquote, urlsplit


TARGETS = frozenset(f"WING_MFTF_{number}.L" for number in (3, 5, 7, 9, 11))
CLIP = "l_flap_percent_key"
TRANSLATION_TOLERANCE = 2e-6
QUATERNION_TOLERANCE = 2e-7
PALETTE_MARKER = "a380x_xp12_wing_joint_palette"


def _integer(value, label, minimum=0):
    if type(value) is not int or value < minimum:
        raise ValueError(f"{label}: expected integer >= {minimum}")
    return value


def _entry(items, index, label):
    index = _integer(index, label)
    if not isinstance(items, list) or index >= len(items) or not isinstance(items[index], dict):
        raise ValueError(f"{label}: invalid table index")
    return items[index]


def _vector(value, width, label):
    if (not isinstance(value, (list, tuple)) or len(value) != width
            or any(type(v) not in (float, int) or not math.isfinite(v) for v in value)):
        raise ValueError(f"{label}: invalid finite vector")
    return tuple(value)


class _FloatReader:
    def __init__(self, document, root, stack):
        self.document, self.root, self.stack = document, root, stack
        self.buffers = {}

    def read(self, index, kind):
        doc = self.document
        accessor = _entry(doc.get("accessors"), index, "accessor")
        if (accessor.get("componentType") != 5126 or accessor.get("type") != kind
                or accessor.get("normalized", False) is not False or "sparse" in accessor):
            raise ValueError("Rest proof requires unnormalized, nonsparse float accessors")
        count = _integer(accessor.get("count"), "accessor count", 2)
        view = _entry(doc.get("bufferViews"), accessor.get("bufferView"), "bufferView")
        buffer_index = _integer(view.get("buffer"), "buffer")
        info = _entry(doc.get("buffers"), buffer_index, "buffer")
        declared = _integer(info.get("byteLength"), "buffer byteLength")
        if buffer_index not in self.buffers:
            uri = info.get("uri")
            if not isinstance(uri, str) or not uri:
                raise ValueError("Rest proof requires external local buffers")
            parts = urlsplit(uri.replace("\\", "/"))
            if parts.scheme or parts.netloc or parts.query or parts.fragment:
                raise ValueError("Rest proof requires external local buffer paths")
            relative = Path(unquote(parts.path))
            path = (self.root / relative).resolve()
            if relative.is_absolute() or not path.is_relative_to(self.root):
                raise ValueError("Rest-proof buffer escapes glTF directory")
            try:
                stream = self.stack.enter_context(path.open("rb"))
            except OSError as error:
                raise ValueError(f"Cannot read rest-proof buffer: {uri}") from error
            self.buffers[buffer_index] = stream, os.fstat(stream.fileno()).st_size
        stream, actual = self.buffers[buffer_index]
        width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[kind]
        unpack = struct.Struct("<" + "f" * width)
        start = _integer(view.get("byteOffset", 0), "view byteOffset")
        length = _integer(view.get("byteLength"), "view byteLength")
        offset = _integer(accessor.get("byteOffset", 0), "accessor byteOffset")
        stride = _integer(view.get("byteStride", unpack.size), "view byteStride", unpack.size)
        if (start % 4 or offset % 4 or stride % 4 or start + length > min(actual, declared)
                or offset + (count - 1) * stride + unpack.size > length):
            raise ValueError("Rest-proof accessor exceeds buffer bounds or alignment")
        rows = []
        for row in range(count):
            stream.seek(start + offset + row * stride)
            raw = stream.read(unpack.size)
            if len(raw) != unpack.size:
                raise ValueError("Rest-proof buffer changed or is truncated")
            values = unpack.unpack(raw)
            if not all(math.isfinite(v) for v in values):
                raise ValueError("Rest-proof accessor contains nonfinite values")
            rows.append(values)
        return rows


def verify_fbw_spoiler_rest(document: dict, source_path: Path) -> set[str]:
    """Return all five names only after every rest-pose proof succeeds.

    The document and external buffers are never changed. Values must be exactly
    constant across stored samples, then component-wise equal to node rest within
    the stated tolerances. Quaternion signs are deliberately not normalized or
    flipped: a different representation fails this source-scoped proof.
    """
    marker = document.get("extras", {}).get(PALETTE_MARKER, {})
    if not isinstance(marker, dict) or marker.get("status") != "decoded":
        raise ValueError("Rest proof requires the corrected wing joint palette")
    nodes = document.get("nodes", [])
    selected = {}
    for name in TARGETS:
        matches = [(i, n) for i, n in enumerate(nodes) if n.get("name") == name]
        if len(matches) != 1:
            raise ValueError(f"Rest proof requires one unique target: {name}")
        selected[name] = matches[0]
    clips = [a for a in document.get("animations", []) if a.get("name") == CLIP]
    if len(clips) != 1:
        raise ValueError(f"Rest proof requires one unique {CLIP} clip")
    clip = clips[0]
    with ExitStack() as stack:
        reader = _FloatReader(document, Path(source_path).resolve().parent, stack)
        for name, (index, node) in selected.items():
            if "matrix" in node:
                raise ValueError(f"{name}: matrix-node rest proof is unsupported")
            channels = [c for c in clip.get("channels", []) if c.get("target", {}).get("node") == index]
            if (len(channels) != 2
                    or {c["target"].get("path") for c in channels} != {"translation", "rotation"}):
                raise ValueError(f"{name}: expected only one translation and one rotation channel")
            for channel in channels:
                path = channel["target"]["path"]
                width, default, tolerance = ((3, (0., 0., 0.), TRANSLATION_TOLERANCE)
                    if path == "translation" else (4, (0., 0., 0., 1.), QUATERNION_TOLERANCE))
                rest = _vector(node.get(path, default), width, f"{name} rest {path}")
                if path == "rotation" and abs(sum(v*v for v in rest) - 1.) > 1e-5:
                    raise ValueError(f"{name}: rest quaternion is not unit length")
                sampler = _entry(clip.get("samplers"), channel.get("sampler"), "sampler")
                if sampler.get("interpolation", "LINEAR") not in ("STEP", "LINEAR"):
                    raise ValueError(f"{name}: unsupported interpolation for constant rest proof")
                times = [row[0] for row in reader.read(sampler.get("input"), "SCALAR")]
                values = reader.read(sampler.get("output"), f"VEC{width}")
                if (len(times) != len(values) or times[0] < 0
                        or any(a >= b for a, b in zip(times, times[1:]))):
                    raise ValueError(f"{name}: mismatched or non-increasing animation samples")
                if any(row != values[0] for row in values[1:]):
                    raise ValueError(f"{name}: {path} samples are not exactly constant")
                if any(abs(value - expected) > tolerance for value, expected in zip(values[0], rest)):
                    raise ValueError(f"{name}: constant {path} is not node rest")
    return set(TARGETS)
