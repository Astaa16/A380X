#!/usr/bin/env python3
"""Generate a deterministic SkunkCrafts Updater depot for the aircraft.

The output files live in the aircraft root so a dedicated Git branch can be
served as a static depot.  Only Python's standard library is required.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
import sys
import tempfile
import zlib


PROJECT = Path(__file__).resolve().parents[1]
DEFAULT_PACKAGE = PROJECT / "output/A380X-XP12"
DEFAULT_MODULE = (
    "https://github.com/Sava227/A380X/raw/refs/heads/skunkcrafts/"
    "output/A380X-XP12/"
)
DEFAULT_NAME = "A380X XP12 Port (Development)"
LFS_HEADER = b"version https://git-lfs.github.com/spec/v1\n"

CONTROL_FILES = {
    "skunkcrafts_updater.cfg",
    "skunkcrafts_updater_config.txt",
    "skunkcrafts_updater_whitelist.txt",
    "skunkcrafts_updater_sizeslist.txt",
    "skunkcrafts_updater_blacklist.txt",
    "skunkcrafts_updater_ignore.txt",
    "skunkcrafts_updater_ignorelist.txt",
    "skunkcrafts_updater_oncelist.txt",
    "skunkcrafts_updater_beta.cfg",
}
EXCLUDED_TOP_LEVEL = {"build"}
EXCLUDED_FILES = {
    "large-assets-animation-inventory.json",
    "runtime-smoke-test.json",
    "source-animation-inventory.json",
    "plugins/A380XAvionics/build-report.json",
    "plugins/A380XAvionics/mac_x64/xp-kit-build-report.json",
}
ONCE_FILES = {
    "A380X-XP12_prefs.txt",
    "X-Camera_A380X-XP12.csv",
    "fcu-calibration.ini",
}
GENERATED_FILES = {
    "skunkcrafts_updater.cfg",
    "skunkcrafts_updater_whitelist.txt",
    "skunkcrafts_updater_sizeslist.txt",
    "skunkcrafts_updater_blacklist.txt",
}


class DepotError(RuntimeError):
    """A package cannot safely be published."""


@dataclass(frozen=True)
class ManifestRow:
    path: str
    crc: int
    size: int


def validate_relative(value: str) -> str:
    """Return a safe canonical manifest path or raise DepotError."""
    if not value or value != value.strip() or "|" in value or "\n" in value or "\r" in value:
        raise DepotError(f"unsafe manifest path: {value!r}")
    pure = PurePosixPath(value)
    if pure.is_absolute() or any(part in {"", ".", ".."} for part in pure.parts):
        raise DepotError(f"unsafe manifest path: {value!r}")
    if str(pure) != value or "\\" in value:
        raise DepotError(f"manifest path is not canonical POSIX form: {value!r}")
    return value


def validate_unique_paths(paths: list[str]) -> None:
    """Reject paths that collide on the case-insensitive user filesystems."""
    folded: dict[str, str] = {}
    for value in paths:
        validate_relative(value)
        previous = folded.setdefault(value.casefold(), value)
        if previous != value:
            raise DepotError(f"case-insensitive path collision: {previous!r} and {value!r}")


def excluded(relative: str) -> bool:
    parts = PurePosixPath(relative).parts
    return (
        any(part.startswith(".") for part in parts)
        or parts[0] in EXCLUDED_TOP_LEVEL
        or relative in EXCLUDED_FILES
        or PurePosixPath(relative).name in CONTROL_FILES
    )


def crc32_file(path: Path) -> int:
    value = 0
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            value = zlib.crc32(chunk, value)
    return value & 0xFFFFFFFF


def scan_package(package: Path) -> list[ManifestRow]:
    package = package.resolve()
    if not package.is_dir():
        raise DepotError(f"aircraft package does not exist: {package}")

    candidates: list[tuple[str, Path]] = []
    for path in package.rglob("*"):
        if path.is_symlink():
            raise DepotError(f"symlinks are not allowed in the depot: {path}")
        if not path.is_file():
            continue
        relative = path.relative_to(package).as_posix()
        validate_relative(relative)
        if not excluded(relative):
            candidates.append((relative, path))

    candidates.sort(key=lambda item: item[0])
    validate_unique_paths([relative for relative, _ in candidates])
    rows: list[ManifestRow] = []
    for relative, path in candidates:
        with path.open("rb") as stream:
            if stream.read(len(LFS_HEADER)) == LFS_HEADER:
                raise DepotError(f"Git LFS pointer found instead of payload bytes: {relative}")
        rows.append(
            ManifestRow(
                relative,
                -1 if relative in ONCE_FILES else crc32_file(path),
                path.stat().st_size,
            )
        )
    return rows


def clean_value(label: str, value: str) -> str:
    if not value or value != value.strip() or any(char in value for char in "|\r\n"):
        raise DepotError(f"invalid {label}: {value!r}")
    return value


def config_text(name: str, module: str, version: str) -> str:
    name = clean_value("name", name)
    version = clean_value("version", version)
    module = clean_value("module URL", module)
    if not module.startswith("https://") or not module.endswith("/"):
        raise DepotError("module URL must use HTTPS and end with '/'")
    return "\n".join(
        (
            f"name|{name}",
            f"module|{module}",
            f"version|{version}",
            "locked|false",
            "zone|custom",
            "disabled|false",
            "liveries|false",
            "readme|README.txt",
            "changelog|changelog.txt",
            "",
        )
    )


def read_blacklist(path: Path) -> str:
    if not path.exists():
        return ""
    if path.is_symlink() or not path.is_file():
        raise DepotError(f"blacklist must be a regular file: {path}")
    entries = [line.strip() for line in path.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
    validate_unique_paths(entries)
    forbidden = GENERATED_FILES.intersection(entries)
    if forbidden:
        raise DepotError(f"blacklist cannot delete updater control files: {sorted(forbidden)}")
    return "".join(f"{entry}\n" for entry in entries)


def atomic_write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", newline="\n", dir=path.parent, delete=False
    ) as stream:
        stream.write(text)
        temporary = Path(stream.name)
    temporary.replace(path)


def expected_outputs(
    package: Path, rows: list[ManifestRow], name: str, module: str, version: str
) -> dict[str, str]:
    return {
        "skunkcrafts_updater.cfg": config_text(name, module, version),
        "skunkcrafts_updater_whitelist.txt": "".join(
            f"{row.path}|{row.crc}\n" for row in rows
        ),
        "skunkcrafts_updater_sizeslist.txt": "".join(
            f"{row.path}|{row.size}\n" for row in rows
        ),
        "skunkcrafts_updater_blacklist.txt": read_blacklist(
            package / "skunkcrafts_updater_blacklist.txt"
        ),
    }


def require_release_files(package: Path, version: str) -> None:
    required = (
        "README.txt",
        "changelog.txt",
        "licenses/ATTRIBUTION.md",
        "licenses/GPL-3.0.txt",
        "licenses/mSparks-747-400-CC-BY-NC-SA-4.0.txt",
    )
    missing = [value for value in required if not (package / value).is_file()]
    if missing:
        raise DepotError(f"required distribution files are missing: {', '.join(missing)}")
    changelog = (package / "changelog.txt").read_text(encoding="utf-8-sig").splitlines()
    first = next((line.strip() for line in changelog if line.strip()), "")
    if first != version:
        raise DepotError(f"changelog starts with {first!r}, expected version {version!r}")


def generate(package: Path, name: str, module: str, version: str, check: bool) -> list[ManifestRow]:
    package = package.resolve()
    package_version = package / "VERSION"
    version_text = f"{clean_value('version', version)}\n"
    if check:
        if not package_version.is_file() or package_version.read_text(encoding="utf-8") != version_text:
            raise DepotError(f"{package_version} is missing or stale")
    else:
        atomic_write(package_version, version_text)

    require_release_files(package, version)
    rows = scan_package(package)
    outputs = expected_outputs(package, rows, name, module, version)
    stale: list[str] = []
    for filename, content in outputs.items():
        destination = package / filename
        if check:
            if not destination.is_file() or destination.read_text(encoding="utf-8") != content:
                stale.append(filename)
        else:
            atomic_write(destination, content)
    if stale:
        raise DepotError("generated updater metadata is stale: " + ", ".join(stale))
    return rows


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=DEFAULT_PACKAGE)
    parser.add_argument("--name", default=DEFAULT_NAME)
    parser.add_argument("--module", default=DEFAULT_MODULE)
    parser.add_argument("--version", help="override the repository VERSION file")
    parser.add_argument("--check", action="store_true", help="verify metadata without writing")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        version = args.version or (PROJECT / "VERSION").read_text(encoding="utf-8").strip()
        rows = generate(args.package, args.name, args.module, version, args.check)
    except (DepotError, OSError, UnicodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    size = sum(row.size for row in rows)
    action = "verified" if args.check else "generated"
    print(f"{action} SkunkCrafts depot: {len(rows)} files, {size} bytes, version {version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
