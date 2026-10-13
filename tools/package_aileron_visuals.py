#!/usr/bin/env python3
"""Apply only the audited 18 aileron-ref tokens and package their Lua provider.

Strict native-baseline byte equality is required; this is not an idempotent
updater or a general aircraft builder. No ACF, texture, cockpit, README, other
wing object, or global FlyWithLua installation is modified. Run with X-Plane
closed and no concurrent writer to these nine targeted package files.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

from aileron_visual_provider import ALL_DATAREFS, BOOTSTRAP_FILES, DATAREFS, FILES, MANIFEST, package_provider, validate_provider
from bind_aileron_visuals import CUSTOM_REFS, EXPECTED_FILES, OLD_REF, rebind_object
from build_development_aircraft import copy_exterior_obj

WINGS = ("a380x_wing1.obj", "a380x_wing2.obj")
WING_ROOT = Path("objects/exterior/wings")
BUILD_MANIFEST = Path("build-manifest.json")
PROVIDER_PATHS = tuple(Path(row[1]) for row in (*FILES, *BOOTSTRAP_FILES)) + (MANIFEST, Path("INSTALL_AILERON_VISUALS.txt"))


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_hash(path: Path) -> str | None:
    return sha(path) if path.is_file() else None


def checked_target(root: Path, relative: Path, required: bool = False) -> Path:
    target = root / relative
    for component in (target, *target.parents):
        if component == root:
            break
        if component.is_symlink():
            raise ValueError(f"Refusing symbolic-link package target: {component}")
        if component != target and component.exists() and not component.is_dir():
            raise ValueError(f"Package target parent is not a directory: {component}")
    if not target.resolve().is_relative_to(root):
        raise ValueError(f"Package target escaped aircraft: {target}")
    if (target.exists() and not target.is_file()) or (required and not target.is_file()):
        raise ValueError(f"Expected regular package file: {target}")
    return target


def atomic_copy(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(prefix=".aileron-copy-", dir=destination.parent)
    temporary = Path(temporary)
    os.close(handle)
    try:
        shutil.copy2(source, temporary)
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def package(candidate_dir: Path, aircraft: Path, textures: Path, project: Path,
            backup_dir: Path, report: Path) -> dict:
    # Resolve the aircraft root (which may itself be the user's installed link),
    # but never traverse linked descendants for a write.
    if backup_dir.is_symlink() or report.is_symlink():
        raise ValueError("Backup and report must not be symbolic links")
    candidate_dir, aircraft, textures, project, backup_dir, report = (
        path.resolve() for path in (candidate_dir, aircraft, textures, project, backup_dir, report))
    for path in (candidate_dir, aircraft, textures, project, backup_dir.parent, report.parent):
        if not path.is_dir():
            raise ValueError(f"Required directory does not exist: {path}")
    if backup_dir.exists() and (not backup_dir.is_dir() or any(backup_dir.iterdir())):
        raise ValueError("Backup directory must be new or empty")
    if report.exists():
        raise ValueError("Report must be a new file")
    for output in (backup_dir, report):
        if any(output.is_relative_to(root) for root in (candidate_dir, aircraft, textures)):
            raise ValueError("Backup/report must be outside candidate, aircraft and texture trees")
    if report.is_relative_to(backup_dir) or backup_dir.is_relative_to(report):
        raise ValueError("Report and backup locations must be separate")
    candidates = {path.name: path for path in candidate_dir.iterdir()
                  if path.suffix.lower() == ".obj" and not path.name.startswith("._")}
    if set(candidates) != EXPECTED_FILES or any(
            not path.is_file() or path.is_symlink() for path in candidates.values()):
        raise ValueError("Candidate directory must contain the exact eleven regular wing OBJ files")

    acf = checked_target(aircraft, Path("A380X-XP12.acf"), required=True)
    manifest_path = checked_target(aircraft, BUILD_MANIFEST, required=True)
    old_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(old_manifest, dict):
        raise ValueError("build-manifest.json must contain an object")
    relatives = tuple(WING_ROOT / name for name in WINGS) + PROVIDER_PATHS + (BUILD_MANIFEST,)
    destinations = {relative: checked_target(aircraft, relative, relative.parent == WING_ROOT)
                    for relative in relatives}
    sources = [project / row[0] for row in (*FILES, *BOOTSTRAP_FILES)]
    if len(DATAREFS) != 6 or set(name.encode() for name in DATAREFS) != CUSTOM_REFS:
        raise ValueError("Provider must declare the exact six audited datarefs")
    if any(not path.is_file() or path.is_symlink() for path in sources):
        raise ValueError("Missing regular required aileron visual driver/core source")
    watched = {path: sha(path) for path in [acf, *sources, *(candidates[name] for name in WINGS)]}
    before = {relative: file_hash(path) for relative, path in destinations.items()}

    # All generated staging is outside the aircraft, on the backup volume.
    with tempfile.TemporaryDirectory(prefix=".aileron-package-", dir=backup_dir.parent) as temporary:
        staged = Path(temporary)
        object_records, texture_records = [], {}
        for name in WINGS:
            relative = WING_ROOT / name
            prepared = staged / relative
            referenced = copy_exterior_obj(candidates[name], prepared, textures, "wings")
            proposed = prepared.read_bytes()
            native = proposed
            for reference in CUSTOM_REFS:
                native = native.replace(reference, OLD_REF)
            # Re-run the original scope/signature/key contract. A swapped panel,
            # extra custom ref, comment edit, or wrong token count cannot pass.
            rebound, proof = rebind_object(native, name)
            if rebound != proposed or proof["header_token_changes"] != 9:
                raise ValueError(f"Candidate does not contain exactly the nine audited tokens: {name}")
            if destinations[relative].read_bytes() != native:
                raise ValueError(f"Installed wing differs from strict native baseline: {name}")
            object_records.append({"path": str(relative), "candidate": str(candidates[name]),
                                   "before_sha256": before[relative], "after_sha256": sha(prepared),
                                   "native_restored_sha256": hashlib.sha256(native).hexdigest(),
                                   "scope_proof": proof})
            for filename in referenced:
                installed_texture = aircraft / "textures/exterior/wings" / filename
                if not installed_texture.is_file():
                    raise ValueError(f"Installed texture is missing: {installed_texture}")
                watched[installed_texture] = sha(installed_texture)
                texture_records[str(installed_texture.relative_to(aircraft))] = watched[installed_texture]
        if sum(record["scope_proof"]["header_token_changes"] for record in object_records) != 18:
            raise ValueError("Expected exactly 18 changed reference tokens")

        provider = package_provider(staged, project)
        refs, errors = validate_provider(staged)
        if errors or refs != set(ALL_DATAREFS):
            raise ValueError("Staged provider failed validation: " + "; ".join(errors))
        staged_files = {path.relative_to(staged) for path in staged.rglob("*") if path.is_file()}
        if staged_files != set(relatives) - {BUILD_MANIFEST}:
            raise ValueError("Provider staging wrote unexpected or missing files")
        proof = {"format": 1, "header_token_changes": 18,
                 "only_aileron_reference_tokens_changed": True, "strict_native_baseline": True,
                 "acf_sha256": watched[acf], "acf_unchanged": True,
                 "textures_unchanged": texture_records, "objects": object_records,
                 "backup_directory": str(backup_dir), "report": str(report),
                 "runtime_execution_verified": False, "global_runtime_installed": False}
        new_manifest = dict(old_manifest)
        new_manifest["aileron_visual_provider"] = provider
        new_manifest["aileron_visual_package_proof"] = proof
        (staged / BUILD_MANIFEST).write_text(json.dumps(new_manifest, indent=2) + "\n", encoding="utf-8")
        expected = {relative: sha(staged / relative) for relative in relatives}

        def unchanged() -> None:
            if any(sha(path) != digest for path, digest in watched.items()):
                raise ValueError("A source, ACF or texture changed during preflight; not deploying")
            if any(file_hash(path) != before[relative] for relative, path in destinations.items()):
                raise ValueError("A targeted package file changed during preflight; not deploying")

        unchanged()
        backup_dir.mkdir(exist_ok=True)
        for relative, digest in before.items():
            if digest is not None:
                backup = backup_dir / relative
                backup.parent.mkdir(parents=True, exist_ok=True)
                with backup.open("xb") as target, destinations[relative].open("rb") as original:
                    shutil.copyfileobj(original, target)
                if sha(backup) != digest:
                    raise ValueError("Backup verification failed; aircraft not modified")
        unchanged()
        try:
            # This helper is itself staged/audited above. It writes only the four
            # Lua files, early-registration binaries, contract and install note;
            # never global FWL.
            package_provider(aircraft, project)
            for name in WINGS:
                relative = WING_ROOT / name
                atomic_copy(staged / relative, destinations[relative])
            atomic_copy(staged / BUILD_MANIFEST, manifest_path)
            if any(sha(path) != expected[relative] for relative, path in destinations.items()):
                raise ValueError("Post-copy hash verification failed")
            if any(sha(path) != digest for path, digest in watched.items()):
                raise ValueError("A source, ACF or texture changed during deployment")
            result = {**proof, "aircraft": str(aircraft), "provider": provider,
                      "files": [{"path": str(relative), "before_sha256": before[relative],
                                 "after_sha256": expected[relative],
                                 "backup": str(backup_dir / relative) if before[relative] else None}
                                for relative in relatives]}
            with report.open("x", encoding="utf-8") as stream:
                json.dump(result, stream, indent=2)
                stream.write("\n")
        except Exception:
            # Recover only this tool's nine explicit targets. Backups remain
            # intact for inspection/manual recovery even if restoration fails.
            for relative, digest in before.items():
                if digest is None:
                    destinations[relative].unlink(missing_ok=True)
                else:
                    atomic_copy(backup_dir / relative, destinations[relative])
            raise
        return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("candidate-dir", "aircraft", "textures", "project", "backup-dir", "report"):
        parser.add_argument("--" + name, type=Path, required=True)
    arguments = parser.parse_args(argv)
    result = package(**vars(arguments))
    print(f"Packaged 18 aileron tokens in two wing objects; ACF/textures unchanged. Backup: {result['backup_directory']}")


if __name__ == "__main__":
    main()
