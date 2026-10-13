#!/usr/bin/env python3
"""Install the aileron/hydraulic visual drivers on Mac/Windows; never enable FBW.

Different pre-existing target content is refused, not overwritten. Upgrade by
explicitly moving the previous provider files to a backup first.
"""
import argparse
import json
from pathlib import Path
import shutil

from aileron_visual_provider import FILES, digest, validate_provider, verify_installed


def install(root: Path, xplane: Path) -> dict:
    refs, errors = validate_provider(root)
    if errors or not refs:
        raise ValueError("; ".join(errors) or "Package lacks required aileron visual provider")
    plugin = xplane / "Resources/plugins/FlyWithLua"
    if not (plugin / "Scripts").is_dir() or not (plugin / "Modules").is_dir():
        raise ValueError("Install FlyWithLua NG first; its Scripts and Modules directories are required")
    for _, packaged, installed in FILES:
        target = plugin / installed
        if target.is_symlink() or (target.exists() and
                (not target.is_file() or digest(target) != digest(root / packaged))):
            raise ValueError(f"Refusing to overwrite an existing different provider: {target}")
    for _, packaged, installed in FILES:
        target = plugin / installed
        if not target.exists():
            shutil.copy2(root / packaged, target)
    return verify_installed(root, plugin)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--aircraft", type=Path, required=True)
    parser.add_argument("--xplane", type=Path, required=True)
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    result = (verify_installed(args.aircraft, args.xplane / "Resources/plugins/FlyWithLua")
              if args.verify_only else install(args.aircraft, args.xplane))
    encoded = json.dumps(result, indent=2) + "\n"
    if args.report:
        args.report.write_text(encoded, encoding="utf-8")
    print(encoded)


if __name__ == "__main__":
    main()
