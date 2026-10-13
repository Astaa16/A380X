#!/usr/bin/env python3
"""Preserve the installed cockpit fix in the canonical rebuild inputs.

Dry-run is the default. --apply copies only the six patched cockpit objects,
their textures, and installed MIP03 (if present), then updates their manifest
entries. Geometry, animations, and all unrelated variants remain unchanged.
Transactional backups use patch_cockpit_materials.py's rollback format.
"""
from __future__ import annotations

import argparse
import copy
import json
import os
from pathlib import Path

from patch_cockpit_materials import (
    ASSET_DIR, OBJ_DIR, PROJECT, TARGETS, atomic_write, inspect_obj,
    install, texture_references,
)

BUILD_OBJECTS = Path("xplane-obj/cockpit-panels-textured")
BUILD_TEXTURES = Path("textures/cockpit-panels")
PANEL_MANIFEST = Path("manifests/cockpit-panel-exports.json")
DECAL_MANIFEST = Path("manifests/cockpit-decal-exports.json")
MIP03 = ("panel_a380_cockpit_mip03_4k.obj", "A380_COCKPIT_MIP03_4k", "mip")
TEXTURE_KEYS = {
    "TEXTURE": "texture",
    "TEXTURE_LIT": "texture_lit",
    "TEXTURE_MAP normal": "texture_map_normal",
    "TEXTURE_MAP material_gloss": "texture_map_material_gloss",
}


def load_manifest(path: Path) -> dict:
    document = json.loads(path.read_text())
    if not isinstance(document, dict) or not isinstance(document.get("exports"), list):
        raise ValueError(f"Expected an exports manifest: {path}")
    names = [entry["name"] for entry in document["exports"]]
    if len(names) != len(set(names)):
        raise ValueError(f"Duplicate export names: {path}")
    return document


def find_variant(document: dict, filename: str, material: str) -> dict | None:
    matches = [entry for entry in document["exports"]
               if entry["name"] == Path(filename).stem
               or material in entry.get("materials", [])]
    if len(matches) > 1:
        raise ValueError(f"Duplicate or conflicting material variants: {material}")
    if not matches:
        return None
    entry = matches[0]
    if entry["name"] != Path(filename).stem or entry.get("materials") != [material]:
        raise ValueError(f"Expected exactly one named material in variant: {filename}")
    return entry


def rewrite_texture_paths(text: str, replacements: dict[str, str]) -> str:
    """Rewrite only existing texture headers, retaining all other lines exactly."""
    result, seen = [], set()
    for line in text.splitlines(keepends=True):
        references = list(texture_references(line))
        if not references:
            result.append(line)
            continue
        directive, _old_path = references[0]
        if directive not in replacements or directive in seen:
            raise ValueError(f"Unexpected or repeated texture directive: {directive}")
        seen.add(directive)
        newline = "\r\n" if line.endswith("\r\n") else "\n" if line.endswith("\n") else ""
        indent = line[:len(line) - len(line.lstrip())]
        result.append(f"{indent}{directive} {replacements[directive]}{newline}")
    if seen != replacements.keys():
        raise ValueError("Not every texture reference was rewritten")
    return "".join(result)


def build_plan(aircraft: Path, build: Path, mip03_manifest: Path) -> tuple[dict[Path, bytes], dict]:
    aircraft, build = aircraft.resolve(), build.resolve()
    if aircraft == build or aircraft.is_relative_to(build) or build.is_relative_to(aircraft):
        raise ValueError("Aircraft and canonical build must be separate directories")
    manifests = {relative: load_manifest(build / relative)
                 for relative in (PANEL_MANIFEST, DECAL_MANIFEST)}
    originals = copy.deepcopy(manifests)
    targets = list(TARGETS)
    mip_installed = (aircraft / OBJ_DIR / MIP03[0]).is_file()
    if mip_installed:
        staged = load_manifest(mip03_manifest)
        mip_variant = find_variant(staged, *MIP03[:2])
        if mip_variant is None:
            raise ValueError("Staged manifest does not contain the MIP03 export")
        existing = find_variant(manifests[PANEL_MANIFEST], *MIP03[:2])
        if existing is None:
            manifests[PANEL_MANIFEST]["exports"].append(copy.deepcopy(mip_variant))
        else:
            existing.clear()
            existing.update(copy.deepcopy(mip_variant))
        targets.append(MIP03)

    plan: dict[Path, bytes] = {}
    report = {"aircraft": str(aircraft), "build_directory": str(build),
              "mip03_installed": mip_installed, "objects": [], "warnings": []}
    if not mip_installed:
        report["warnings"].append("MIP03 is not installed yet; only the existing six objects will be persisted.")
    for filename, material, kind in targets:
        source_object = aircraft / OBJ_DIR / filename
        text = source_object.read_text()
        before = inspect_obj(text)
        target_object = BUILD_OBJECTS / filename
        manifest_path = DECAL_MANIFEST if kind == "labels" else PANEL_MANIFEST
        variant = find_variant(manifests[manifest_path], filename, material)
        if variant is None:
            raise ValueError(f"Existing canonical manifest entry is missing: {material}")
        references = list(texture_references(text))
        directives = [key for key, _value in references]
        if len(directives) != len(set(directives)) or not {"TEXTURE", "TEXTURE_MAP normal", "TEXTURE_MAP material_gloss"}.issubset(directives):
            raise ValueError(f"Missing or repeated PBR texture references: {filename}")
        replacements, manifest_textures = {}, {}
        for directive, value in references:
            if directive not in TEXTURE_KEYS or not value:
                raise ValueError(f"Unsupported or blank texture reference in {filename}: {directive}")
            source_texture = (source_object.parent / value).resolve()
            if not source_texture.is_relative_to(aircraft) or not source_texture.is_file():
                raise ValueError(f"Missing or external installed texture: {source_texture}")
            if (filename != MIP03[0]
                    and not source_texture.is_relative_to((aircraft / ASSET_DIR).resolve())):
                raise ValueError(f"Run the cockpit material patch first: {source_texture}")
            destination = BUILD_TEXTURES / source_texture.name
            data = source_texture.read_bytes()
            if destination in plan and plan[destination] != data:
                raise ValueError(f"Conflicting texture basenames: {source_texture.name}")
            plan[destination] = data
            replacements[directive] = Path(os.path.relpath(build / destination, (build / target_object).parent)).as_posix()
            manifest_textures[TEXTURE_KEYS[directive]] = str(build / destination)
        copied = rewrite_texture_paths(text, replacements)
        after = inspect_obj(copied)
        if before["geometry_animation_sha256"] != after["geometry_animation_sha256"]:
            raise AssertionError(f"Geometry or animation changed while copying {filename}")
        # Remove stale texture slots, then use only the installed OBJ's references.
        for key in TEXTURE_KEYS.values():
            variant.pop(key, None)
        variant.update(manifest_textures)
        variant["normal_metalness"] = True
        variant["specular"] = .05 if kind in {"labels", "matte"} else 1.0
        if "texture_lit" in variant:
            variant.pop("emissive_source_pending", None)
        if kind == "labels":
            if "texture_lit" not in variant:
                raise ValueError("Installed white-label OBJ is missing its LIT atlas")
            variant.update(alpha_mode="MASK", alpha_cutoff=.15, polygon_offset=0,
                           lit_level_dataref="a380x/xp12/lighting/integral",
                           decal_offset_m=.002, decal_cutout_ratio=.15)
        plan[target_object] = copied.encode()
        report["objects"].append({"object": str(target_object), "material": material,
                                  "geometry_animation_sha256": after["geometry_animation_sha256"],
                                  "uv_sha256": after["uv_sha256"],
                                  "vertices": after["vertices"], "triangles": after["triangles_drawn"],
                                  "textures": manifest_textures})

    target_names = {Path(item[0]).stem for item in targets}
    for relative, manifest in manifests.items():
        old_unrelated = [entry for entry in originals[relative]["exports"] if entry["name"] not in target_names]
        new_unrelated = [entry for entry in manifest["exports"] if entry["name"] not in target_names]
        if old_unrelated != new_unrelated:
            raise AssertionError(f"Unrelated manifest variants changed: {relative}")
        plan[relative] = (json.dumps(manifest, indent=2) + "\n").encode()
    for relative in plan:
        if not (build / relative).resolve().is_relative_to(build):
            raise ValueError(f"Build destination escapes canonical directory: {relative}")
    report["changed_files"] = [str(path) for path, data in plan.items()
                               if not (build / path).is_file() or (build / path).read_bytes() != data]
    report["planned_files"] = len(plan)
    report["unrelated_manifest_variants_preserved"] = True
    report["decal_copy_offset_applied"] = False
    return plan, report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--aircraft", type=Path, default=PROJECT / "output/A380X-XP12")
    parser.add_argument("--build-dir", type=Path, default=Path("/Volumes/MacGames/A380X-XPlane12-build"))
    parser.add_argument("--mip03-manifest", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    plan, report = build_plan(args.aircraft, args.build_dir, args.mip03_manifest)
    report["mode"] = "apply" if args.apply else "dry-run"
    if args.apply:
        backup = install(args.build_dir.resolve(), plan)
        report["backup_manifest"] = str(backup) if backup else None
    if args.report:
        atomic_write(args.report.resolve(), (json.dumps(report, indent=2) + "\n").encode())
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError) as error:
        raise SystemExit(f"Cockpit persistence aborted: {error}")
