#!/usr/bin/env python3
"""Upgrade the known installed visual runtime only, with verified backups.

Run only with X-Plane fully closed. No OBJ, ACF, native controls, preferences,
other scripts or textures are touched. Existing Lua files must match the old
provider manifest before any writes; this is not a general overwrite installer.
"""
import argparse
import json
from pathlib import Path
import shutil
import tempfile

from aileron_visual_provider import (ALL_DATAREFS, BOOTSTRAP_FILES, DATAREF_CONTRACT, DATAREFS, FILES, MANIFEST,
    digest, package_provider, validate_provider, verify_installed)
from package_aileron_visuals import atomic_copy, checked_target


def upgrade(project, aircraft, xplane, backup, previous_runtime_report=None):
    project, aircraft, xplane, backup = (p.resolve() for p in (project, aircraft, xplane, backup))
    if backup.exists():
        raise ValueError("Use a new backup directory")
    if not backup.parent.is_dir() or backup.is_relative_to(aircraft) or backup.is_relative_to(xplane):
        raise ValueError("Backup must be on an existing external parent, outside aircraft and simulator")
    plugin = xplane / "Resources/plugins/FlyWithLua"
    old = json.loads((aircraft / MANIFEST).read_text())
    old_refs = old.get("datarefs")
    if old_refs == list(DATAREF_CONTRACT[:len(DATAREFS)]):
        old_files = FILES[:2]
    elif old_refs == list(DATAREF_CONTRACT):
        old_files = FILES
    else:
        raise ValueError("Old provider is not an exact supported scalar-ref contract")
    previous_runtime = {}
    if previous_runtime_report is not None:
        recorded = json.loads(previous_runtime_report.read_text())
        expected_paths = {str(plugin / installed) for _, _, installed in old_files}
        rows = recorded.get("files", [])
        if (recorded.get("provider") != "a380x-aileron-dance-v1"
                or recorded.get("files_match_package") is not True
                or len(rows) != len(old_files)
                or not all(isinstance(row.get("path"), str) for row in rows)
                or {str(Path(row["path"]).resolve()) for row in rows} != expected_paths):
            raise ValueError("Previous runtime report must identify exactly this simulator's old provider files")
        previous_runtime = {str(Path(row["path"]).resolve()): row["sha256"] for row in rows}
    if len(old["files"]) != len(old_files):
        raise ValueError("Old manifest must name exactly the expected visual Lua files")
    for row, (_, packaged, installed) in zip(old["files"], old_files):
        if row["path"] != packaged or row["install_path"] != installed:
            raise ValueError("Unexpected old provider paths")
        for path in (checked_target(aircraft, Path(packaged), True),
                     checked_target(plugin, Path(installed), True)):
            expected_hash = previous_runtime.get(str(path), row["sha256"])
            if digest(path) != expected_hash:
                raise ValueError(f"Preserving independently changed runtime file: {path}")
    for _, packaged, installed in FILES[len(old_files):]:
        for path in (aircraft / packaged, plugin / installed):
            if path.exists() or path.is_symlink():
                raise ValueError(f"Preserving independently changed runtime file: {path}")
    for _, packaged in BOOTSTRAP_FILES:
        if (aircraft / packaged).exists():
            raise ValueError("Bootstrap already exists; this migration will not replace it")
    build_path = checked_target(aircraft, Path("build-manifest.json"), True)
    build = json.loads(build_path.read_text())
    if not isinstance(build, dict):
        raise ValueError("Invalid build manifest")
    with tempfile.TemporaryDirectory(prefix=".aileron-upgrade-", dir=backup.parent) as temp:
        stage = Path(temp)
        provider = package_provider(stage, project)
        refs, errors = validate_provider(stage)
        if errors or refs != set(ALL_DATAREFS):
            raise ValueError(str(errors))
        build["aileron_visual_provider"] = provider
        build["aileron_runtime_repair"] = {
            "date": "2026-10-04", "early_registration": "XPluginStart",
            "timing_dataref": "sim/operation/misc/frame_rate_period",
            "runtime_execution_verified": False, "backup": str(backup)}
        (stage / "build-manifest.json").write_text(json.dumps(build, indent=2) + "\n")
        mappings = [(source, checked_target(aircraft, source.relative_to(stage)))
                    for source in stage.rglob("*") if source.is_file()]
        mappings += [(stage / packaged, checked_target(plugin, Path(installed)))
                     for _, packaged, installed in FILES]
        before = {target: digest(target) if target.is_file() else None for _, target in mappings}
        acf = aircraft / "A380X-XP12.acf"
        acf_hash = digest(acf)
        backup.mkdir()
        saved = {}
        for _, target in mappings:
            rel = (Path("aircraft") / target.relative_to(aircraft) if target.is_relative_to(aircraft)
                   else Path("FlyWithLua") / target.relative_to(plugin))
            saved[target] = backup / rel
            if before[target] is not None:
                saved[target].parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(target, saved[target])
                if digest(saved[target]) != before[target]:
                    raise ValueError("Backup verification failed")
        if any((digest(path) if path.is_file() else None) != value for path, value in before.items()):
            raise ValueError("Runtime files changed during backup; not upgrading")
        try:
            for source, target in mappings:
                atomic_copy(source, target)
            if any(digest(source) != digest(target) for source, target in mappings):
                raise ValueError("Upgrade copy verification failed")
            if digest(acf) != acf_hash:
                raise ValueError("ACF changed during upgrade")
            result = verify_installed(aircraft, plugin)
            result.update({"acf_unchanged": True, "acf_sha256": acf_hash,
                           "backup": str(backup), "files_updated": [str(p) for _, p in mappings]})
            (backup / "upgrade-report.json").write_text(json.dumps(result, indent=2) + "\n")
        except Exception:
            for target, previous in before.items():
                if previous is None:
                    target.unlink(missing_ok=True)
                else:
                    atomic_copy(saved[target], target)
            raise
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("project", "aircraft", "xplane", "backup"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--previous-runtime-report", type=Path,
                        help="Verified prior install report when packaged and active Lua revisions differ")
    result = upgrade(**vars(parser.parse_args()))
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
