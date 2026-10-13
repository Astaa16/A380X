#!/usr/bin/env python3
"""Compile/test a staged, never-installed plugin using an external official SDK.

Requires macOS clang, lipo, codesign, and MinGW-w64 for the Windows cross-build.
The output directory must not already exist; no package or simulator is touched.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess


HERE = Path(__file__).resolve().parent
PLUGIN = HERE / "aileron_datarefs.c"
TEST = HERE / "test_aileron_datarefs.c"
EXPORTS = {"XPluginStart", "XPluginStop", "XPluginEnable", "XPluginDisable",
           "XPluginReceiveMessage"}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True,
                        help="Official SDK root containing CHeaders and Libraries")
    parser.add_argument("--output-dir", type=Path, default=HERE / "dist")
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--mingw", default="/opt/homebrew/bin/x86_64-w64-mingw32-gcc")
    args = parser.parse_args()
    if platform.system() != "Darwin":
        parser.error("This build orchestrator requires a macOS host for universal Mac output")
    sdk, output = args.sdk.resolve(), args.output_dir.absolute()
    if output.exists() or output.is_symlink():
        parser.error("Output directory already exists; select a new staging directory")
    headers = sdk / "CHeaders/XPLM"
    framework = sdk / "Libraries/Mac/XPLM.framework/XPLM"
    import_library = sdk / "Libraries/Win/XPLM_64.lib"
    needed = [headers / name for name in
              ("XPLMDataAccess.h", "XPLMDefs.h", "XPLMPlugin.h", "XPLMUtilities.h")]
    for path in [*needed, framework, import_library]:
        if not path.is_file():
            parser.error(f"Missing official SDK input: {path}")
    mingw = shutil.which(args.mingw)
    clang = shutil.which(args.clang)
    objdump = shutil.which(args.mingw.removesuffix("gcc") + "objdump")
    if not all((mingw, clang, objdump, shutil.which("codesign"), shutil.which("lipo"))):
        parser.error("Missing clang, MinGW gcc/objdump, codesign, or lipo")

    output.mkdir(parents=True, exist_ok=False)
    commands: list[dict] = []

    def run(command: list[str]) -> str:
        completed = subprocess.run(command, text=True, capture_output=True)
        commands.append({"argv": command, "stdout": completed.stdout,
                         "stderr": completed.stderr})
        if completed.returncode:
            raise RuntimeError(f"Command failed ({completed.returncode}): {command!r}\n"
                               f"{completed.stdout}{completed.stderr}")
        return completed.stdout

    # Define only API generations needed for aircraft/fat-plugin packaging;
    # none of the new SDK 4.3 flight-init APIs are used or imported.
    common = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
              "-fvisibility=hidden", "-DXPLM200=1", "-DXPLM210=1", "-DXPLM300=1",
              "-I", str(headers)]
    test_binary = output / "test_aileron_datarefs"
    run([clang, *common, "-DAPL=1", str(PLUGIN), str(TEST), "-o", str(test_binary)])
    test_result = run([str(test_binary)]).strip()

    mac = output / "mac.xpl"
    run([clang, *common, "-DAPL=1", "-arch", "arm64", "-arch", "x86_64",
         "-mmacosx-version-min=11.0", "-dynamiclib", str(PLUGIN),
         "-F", str(sdk / "Libraries/Mac"), "-framework", "XPLM",
         "-Wl,-install_name,A380XAileronDatarefs.xpl", "-o", str(mac)])
    # Ad-hoc sign both slices for local Apple Silicon loading. Not notarization.
    run(["codesign", "--force", "--sign", "-", str(mac)])
    run(["codesign", "--verify", "--strict", str(mac)])
    architectures = run(["lipo", "-archs", str(mac)]).split()
    if set(architectures) != {"arm64", "x86_64"}:
        raise RuntimeError(f"Unexpected macOS architectures: {architectures}")
    for arch in architectures:
        symbols = run(["nm", "-arch", arch, "-gjU", str(mac)]).split()
        if set(symbol.removeprefix("_") for symbol in symbols) != EXPORTS:
            raise RuntimeError(f"Unexpected exported macOS symbols: {symbols}")
    mac_libraries = run(["otool", "-L", str(mac)])

    windows = output / "win.xpl"
    run([mingw, *common, "-DIBM=1", "-shared", str(PLUGIN), str(import_library),
         "-static-libgcc", "-Wl,--exclude-all-symbols", "-o", str(windows)])
    windows_headers = run([objdump, "-p", str(windows)])
    windows_format = run([objdump, "-f", str(windows)])
    if "pei-x86-64" not in windows_format:
        raise RuntimeError("Windows plugin is not PE x86-64")
    # The PE name-pointer table is an exact export list (ignore other tables).
    table = windows_headers.split("[Ordinal/Name Pointer] Table", 1)
    if len(table) != 2:
        raise RuntimeError("Cannot locate Windows export names")
    names = set()
    for line in table[1].splitlines():
        if line.strip().startswith("["):
            names.add(line.split()[-1])
        elif names and line.strip():
            break
    if names != EXPORTS:
        raise RuntimeError(f"Unexpected Windows exports: {sorted(names)}")
    dlls = [line.split(":", 1)[1].strip() for line in windows_headers.splitlines()
            if "DLL Name:" in line]
    # Homebrew MinGW targets the Windows Universal CRT (part of Windows 10+).
    # No libgcc, libstdc++, winpthread, or other third-party DLL is permitted.
    allowed_dlls = {"xplm_64.dll", "kernel32.dll", "msvcrt.dll",
                    "api-ms-win-crt-heap-l1-1-0.dll",
                    "api-ms-win-crt-private-l1-1-0.dll",
                    "api-ms-win-crt-runtime-l1-1-0.dll",
                    "api-ms-win-crt-stdio-l1-1-0.dll",
                    "api-ms-win-crt-string-l1-1-0.dll"}
    if not {name.lower() for name in dlls} <= allowed_dlls:
        raise RuntimeError(f"Unexpected external Windows runtime dependencies: {dlls}")

    report = {
        "format": 1, "staged_only": True, "simulator_tested": False,
        "visual_only": True, "mock_test_result": test_result,
        "registration_phase": "XPluginStart", "dataref_count": 22,
        "type": "Float", "writable": True,
        "ranges": [{"suffix": "_ratio", "count": 14, "minimum": -1, "maximum": 1},
                   {"suffix": "_deg", "count": 8, "minimum": 0, "maximum": 65}],
        "nonfinite_write_policy": "ignore; retain last finite cached value",
        "sdk_source": "https://developer.x-plane.com/sdk/plugin-sdk-downloads/",
        "sdk_root": str(sdk),
        "sdk_inputs": [{"path": str(path.relative_to(sdk)), "sha256": digest(path)}
                       for path in [*needed, framework, import_library]],
        "sources": [{"name": path.name, "sha256": digest(path)}
                    for path in (PLUGIN, TEST, Path(__file__))],
        "binaries": [
            {"file": mac.name, "sha256": digest(mac), "bytes": mac.stat().st_size,
             "architectures": architectures, "signing": "ad-hoc, verified; not notarized",
             "dependencies": mac_libraries},
            {"file": windows.name, "sha256": digest(windows),
             "bytes": windows.stat().st_size, "architectures": ["x86_64"],
             "dependencies": dlls, "minimum_os": "Windows 10 (Universal CRT)",
             "runtime_tested": False}],
        "commands": commands,
    }
    report_path = output / "build-report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(test_result)
    print(f"Staged Mac universal + Windows x64 plugin: {output}")
    print(f"Build evidence: {report_path}")


if __name__ == "__main__":
    main()
