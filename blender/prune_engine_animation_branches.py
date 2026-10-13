"""Remove draw-free, animation-only branches from the five engine OBJ variants.

Material filtering leaves the wing armature's unrelated animations in each
engine export. OBJ8 ANIM_begin/end scope transformations and visibility, so an
animation-only branch with no draws cannot affect any other branch. Attribute,
light, unknown and all other directives are retained, including their ancestors:
their state may persist beyond ANIM_end.
"""
from __future__ import annotations

from pathlib import Path


ENGINE_VARIANTS = frozenset({
    "a380x_eng_lh", "a380x_eng_rh", "a380x_eng_cores_lh",
    "a380x_eng_cores_rh", "a380x_fans",
})
SCOPED_ANIMATION_COMMANDS = frozenset({
    "ANIM_trans", "ANIM_rotate", "ANIM_hide", "ANIM_show",
    "ANIM_trans_begin", "ANIM_trans_key", "ANIM_trans_end",
    "ANIM_rotate_begin", "ANIM_rotate_key", "ANIM_rotate_end",
    "ANIM_keyframe_loop",
})


def prune_text(text: str, variant: str) -> tuple[str, dict]:
    """Prune only complete safe branches, preserving retained bytes verbatim."""
    report = {"empty_animation_branches_removed": 0, "animation_lines_removed": 0}
    if variant not in ENGINE_VARIANTS:
        return text, report
    lines = text.splitlines(keepends=True)
    stack, removable = [], []
    for index, line in enumerate(lines):
        words = line.split("#", 1)[0].split()
        if not words:
            continue
        command = words[0]
        if command == "ANIM_begin":
            if len(words) != 1:
                raise ValueError("Unexpected arguments after ANIM_begin")
            stack.append([index, True])
        elif command == "ANIM_end":
            if not stack or len(words) != 1:
                raise ValueError("Unmatched or invalid ANIM_end")
            start, safe = stack.pop()
            if safe:
                removable.append((start, index + 1))
        elif command not in SCOPED_ANIMATION_COMMANDS:
            # ANIM_end does not restore material/draw attributes. Never discard
            # a persistent directive, an unknown command or a drawable subtree.
            for branch in stack:
                branch[1] = False
    if stack:
        raise ValueError("Unclosed ANIM_begin")

    ranges = []
    for start, end in sorted(removable, key=lambda pair: (pair[0], -pair[1])):
        if not ranges or start >= ranges[-1][1]:
            ranges.append((start, end))
    if not ranges:
        return text, report
    kept, cursor = [], 0
    for start, end in ranges:
        kept.extend(lines[cursor:start])
        cursor = end
    kept.extend(lines[cursor:])
    report["empty_animation_branches_removed"] = len(ranges)
    report["animation_lines_removed"] = sum(end - start for start, end in ranges)
    return "".join(kept), report


def prune_engine_animation_branches(path: Path, variant: str | None = None) -> dict:
    original = path.read_text()
    pruned, report = prune_text(original, variant or path.stem)
    if pruned != original:
        path.write_text(pruned)
    return report
