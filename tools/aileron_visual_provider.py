"""Exact, auditable control-surface provider contract (not a namespace exemption)."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil

PROVIDER_ID = "a380x-aileron-dance-v1"
MANIFEST = Path("animation-providers/aileron-dance-v1.json")
KEYS = tuple(f"{side}_{panel}_ratio" for side in ("l", "r")
             for panel in ("inboard", "midboard", "outboard"))
DATAREFS = tuple("a380x/xp12/visual/aileron_" + key for key in KEYS)
HYDRAULIC_DATAREFS = tuple("a380x/xp12/hydraulics/" + name for name in (
    *("aileron_" + key for key in KEYS), "elevator_ratio", "rudder_ratio",
    *(f"spoiler_{side}_{panel}_deg" for panel in ("inboard", "inner", "middle", "outer")
      for side in ("l", "r")),
))
ALL_DATAREFS = DATAREFS + HYDRAULIC_DATAREFS
DATAREF_CONTRACT = tuple(
    {"name": name, "type": "Float", "minimum": 0 if name.endswith("_deg") else -1,
     "maximum": 65 if name.endswith("_deg") else 1}
    for name in ALL_DATAREFS
)
# Project source, packaged source, global FlyWithLua destination.
FILES = (
    ("FlyWithLua/Scripts/A380X_XP12_Ailerons.lua",
     "plugins/FlyWithLua/Scripts (required)/A380X_XP12_Ailerons.lua",
     "Scripts/A380X_XP12_Ailerons.lua"),
    ("FlyWithLua/Modules/a380x_aileron_dance.lua",
     "plugins/FlyWithLua/Modules/a380x_aileron_dance.lua",
     "Modules/a380x_aileron_dance.lua"),
    ("FlyWithLua/Modules/a380x_hydraulic_surfaces.lua",
     "plugins/FlyWithLua/Modules/a380x_hydraulic_surfaces.lua",
     "Modules/a380x_hydraulic_surfaces.lua"),
    ("FlyWithLua/Scripts/A380X_XP12_Hydraulics.lua",
     "plugins/FlyWithLua/Scripts (required)/A380X_XP12_Hydraulics.lua",
     "Scripts/A380X_XP12_Hydraulics.lua"),
)
BOOTSTRAP_FILES = (
    ("native/aileron_datarefs/dist/mac.xpl", "plugins/A380XAileronDatarefs/mac_x64/A380XAileronDatarefs.xpl"),
    ("native/aileron_datarefs/dist/win.xpl", "plugins/A380XAileronDatarefs/win_x64/A380XAileronDatarefs.xpl"),
)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def package_provider(root: Path, project: Path) -> dict:
    sources = [project / source for source, _, _ in FILES]
    bootstrap_sources = [project / source for source, _ in BOOTSTRAP_FILES]
    if not all(path.is_file() for path in sources + bootstrap_sources):
        raise ValueError("Missing required aileron/hydraulic visual driver/core or early-registration plugin")
    files = []
    for source, (_, packaged, installed) in zip(sources, FILES):
        target = root / packaged
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        files.append({"path": packaged, "install_path": installed, "sha256": digest(target)})
    bootstrap = []
    for source, (_, packaged) in zip(bootstrap_sources, BOOTSTRAP_FILES):
        target = root / packaged
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        bootstrap.append({"path": packaged, "sha256": digest(target)})
    document = {"format": 1, "provider": PROVIDER_ID, "runtime": "FlyWithLua NG 2.8.12 or later",
                "visual_only": True, "requires_global_installation": True,
                "datarefs": [dict(row) for row in DATAREF_CONTRACT], "files": files,
                "bootstrap": {"registration": "XPluginStart", "files": bootstrap}}
    (root / MANIFEST).parent.mkdir(parents=True, exist_ok=True)
    (root / MANIFEST).write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    (root / "INSTALL_AILERON_VISUALS.txt").write_text(
        "A380X aileron and hydraulic surface visual animation dependency\n\n"
        "Install FlyWithLua NG (tested API: 2.8.12), then, with X-Plane closed:\n"
        "1. Copy plugins/FlyWithLua/Modules/a380x_aileron_dance.lua and\n"
        "   plugins/FlyWithLua/Modules/a380x_hydraulic_surfaces.lua to\n"
        "   X-Plane 12/Resources/plugins/FlyWithLua/Modules/.\n"
        "2. Copy plugins/FlyWithLua/Scripts (required)/A380X_XP12_Ailerons.lua and\n"
        "   plugins/FlyWithLua/Scripts (required)/A380X_XP12_Hydraulics.lua to\n"
        "   X-Plane 12/Resources/plugins/FlyWithLua/Scripts/.\n"
        "3. Keep the included plugins/A380XAileronDatarefs folder in this aircraft.\n"
        "   It registers all 22 animation datarefs before X-Plane loads the objects.\n"
        "4. Fully restart X-Plane and load this A380X-XP12 aircraft.\n\n"
        "The same Lua files support Mac and Windows. Do not install or enable the\n"
        "optional experimental FBW control-law script for these animations.\n"
        "Only custom visual values are written; flight physics are unchanged.\n"
        "The six legacy and eight hydraulic ratios use [-1, 1]; eight hydraulic\n"
        "spoiler angles use [0, 65] degrees. Both drivers and modules are required.\n"
        "Without the drivers these surfaces cannot animate normally; a stopped Lua\n"
        "runtime may leave the last visual pose until the aircraft is reloaded.\n"
        "Restore the saved native-animation build if FlyWithLua is unavailable.\n"
        "Back up existing files before upgrading; do not overwrite unrelated scripts.\n"
        "These scripts passed offline Lua 5.1/5.4 tests, not a simulator flight test.\n",
        encoding="utf-8")
    return document


def validate_provider(root: Path) -> tuple[set[str], list[str]]:
    manifest = root / MANIFEST
    if not manifest.exists():
        return set(), []
    try:
        document = json.loads(manifest.read_text(encoding="utf-8"))
        if (document.get("format") != 1 or document.get("provider") != PROVIDER_ID
                or document.get("visual_only") is not True
                or document.get("requires_global_installation") is not True):
            raise ValueError("unsupported provider identity/contract")
        if document.get("datarefs") != list(DATAREF_CONTRACT):
            raise ValueError("expected the exact 22 scalar visual datarefs and their ranges")
        rows = document.get("files")
        if not isinstance(rows, list) or len(rows) != len(FILES):
            raise ValueError("expected both visual drivers and both core modules")
        for row, (_, packaged, installed) in zip(rows, FILES):
            if row.get("path") != packaged or row.get("install_path") != installed:
                raise ValueError("unexpected provider file path")
            path = root / packaged
            if (not path.resolve().is_relative_to(root.resolve()) or not path.is_file()
                    or digest(path) != row.get("sha256")):
                raise ValueError(f"missing, escaped or changed provider file: {packaged}")
        bootstrap = document.get("bootstrap", {})
        rows = bootstrap.get("files")
        if bootstrap.get("registration") != "XPluginStart" or not isinstance(rows, list) or len(rows) != 2:
            raise ValueError("missing early-registration aircraft plugin contract")
        for row, (_, packaged) in zip(rows, BOOTSTRAP_FILES):
            path = root / packaged
            if (row.get("path") != packaged or not path.resolve().is_relative_to(root.resolve())
                    or not path.is_file() or digest(path) != row.get("sha256")):
                raise ValueError(f"missing, escaped or changed bootstrap file: {packaged}")
        # The runtime test exercises registration; this manifest is a static
        # dependency proof only, never evidence that Lua loaded in X-Plane.
        return set(ALL_DATAREFS), []
    except (OSError, ValueError, TypeError, AttributeError) as error:
        return set(), [f"Invalid aileron animation provider: {error}"]


def verify_installed(root: Path, flywithlua: Path) -> dict:
    refs, errors = validate_provider(root)
    if errors or not refs:
        raise ValueError("; ".join(errors) or "Aileron provider is not packaged")
    files = []
    for _, packaged, installed in FILES:
        target = flywithlua / installed
        if not target.is_file() or digest(target) != digest(root / packaged):
            raise ValueError(f"Aileron runtime not installed or stale: {target}")
        files.append({"path": str(target), "sha256": digest(target)})
    return {"provider": PROVIDER_ID, "files_match_package": True,
            "runtime_execution_verified": False, "files": files}
