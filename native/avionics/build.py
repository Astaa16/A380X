#!/usr/bin/env python3
"""Build and test the aircraft-local FBW-derived avionics plugin.

The build uses the official X-Plane SDK and produces a universal macOS plugin
plus a Windows x86-64 plugin.  It never launches or modifies X-Plane.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess


HERE = Path(__file__).resolve().parent
PLUGIN = HERE / "a380x_avionics.c"
CORE = HERE / "a380x_avionics_core.c"
CORE_HEADER = HERE / "a380x_avionics_core.h"
ADIRS = HERE / "a380x_adirs.c"
ADIRS_TEST = HERE / "test_adirs.c"
ADIRS_ADAPTER_TEST = HERE / "test_adirs_adapter.c"
ADIRS_WINDOW_TEST = HERE / "test_adirs_window.c"
EFB = HERE / "a380x_efb.c"
EFB_HEADER = HERE / "a380x_efb.h"
TEST = HERE / "test_avionics_core.c"
GUIDANCE = HERE / "a380x_guidance.c"
GUIDANCE_TEST = HERE / "test_guidance.c"
PFD_TEST = HERE / "test_pfd_render.c"
EFB_TEST = HERE / "test_efb_render.c"
ADAPTER_TEST = HERE / "test_fcu_adapter.c"
CONTROL_LIFECYCLE_TEST = HERE / "test_control_lifecycle.c"
CLOCK_RENDER_TEST = HERE / "test_clock_render.c"
EFIS_TEST = HERE / "test_efis.c"
EFIS_ND_TEST = HERE / "test_efis_nd.c"
EFIS_AIRPORT_TEST = HERE / "test_efis_airport.c"
EWD_DATA_TEST = HERE / "test_ewd_data.c"
EWD_RENDER_TEST = HERE / "test_ewd_render.c"
EWD_FONT = HERE / "fonts/FBW-Display-EIS-A380.ttf"
EWD_FONT_HEADER = HERE / "a380x_ewd_font.h"
EWD_FONT_GENERATOR = HERE.parent.parent / "tools/build_ewd_font.py"
EWD_FONT_SHA256 = "85a25cece2241ab8939b11869dce762c43f2b65fc4a47d8ec9038688fa2085c4"
EWD_FADEC_SOURCES = [HERE / "a380x_ewd_fadec.cpp", HERE / "vendor/ewd_fadec/Fadec.cpp"]
CXX_SOURCES = [*EWD_FADEC_SOURCES, HERE / "a380x_fbw_laws.cpp", *[
    HERE / "vendor/fbw" / name for name in
    ("A380FgOuterLoops.cpp", "rt_modd.cpp", "look1_binlxpw.cpp")]]
# Native MFD + FMS (docs/mfd-spec/09-port-integration.md section 1.1). XPLM-free C11 core; the
# X-Plane boundary is a380x_mfd_adapter.inc + a380x_fms_adapter.inc (+ a380x_mfd_systems_host.inc).
MFD_CORE_SOURCES = [HERE / name for name in (
    "a380x_mfd_draw.c", "a380x_mfd_ui.c", "a380x_mfd.c", "a380x_mfd_formats.c", "a380x_fms_messages.c",
    "a380x_fms_speeds.c", "a380x_fms.c", "a380x_navdata.c", "a380x_fms_fpln.c", "a380x_mfd_pages.c")]
MFD_PAGE_MODULES = ("data", "init", "fuel", "sec", "perf", "fpln", "fpln_rev", "position", "surv",
                    "atccom", "fcubkup")
MFD_ADAPTER_TEST = HERE / "test_mfd_adapter.c"
MFD_TEST_DATA = HERE / "test_data"
MFD_FONT_HEADER = HERE / "a380x_mfd_font.h"
MFD_FONT_GENERATOR = HERE.parent.parent / "tools/build_mfd_font.py"
# (test source, kind): "core" = pure C unit test, "navdata" = gets the fixture directory,
# "render" = writes PPM previews into its own directory. Optional page tests run when present.
MFD_TESTS = (("test_mfd_formats", "core"), ("test_fms_core", "core"), ("test_navdata", "navdata"),
             ("test_mfd_ui", "core"), ("test_mfd_render", "render"))
MFD_OPTIONAL_TESTS = (("test_mfd_pages_a", "render"), ("test_mfd_page_perf", "render"),
                      ("test_mfd_page_fpln", "render"), ("test_mfd_pages_h", "render"))
EXTRA_INPUTS = [ADIRS, ADIRS_TEST, ADIRS_ADAPTER_TEST, ADIRS_WINDOW_TEST, GUIDANCE, GUIDANCE_TEST, PFD_TEST, EFB_TEST, ADAPTER_TEST, CONTROL_LIFECYCLE_TEST, CLOCK_RENDER_TEST, EFIS_TEST, EFIS_ND_TEST, EFIS_AIRPORT_TEST,
    EWD_DATA_TEST, EWD_RENDER_TEST,
    *CXX_SOURCES, *[p for p in HERE.glob("*.h") if not p.name.startswith("._")],
    *[p for p in HERE.glob("*.inc") if not p.name.startswith("._")],
    *MFD_CORE_SOURCES, MFD_ADAPTER_TEST,
    *[HERE / f"{name}.c" for name, _ in MFD_TESTS],
    *[p for p in MFD_TEST_DATA.rglob("*") if p.is_file() and not p.name.startswith("._")],
    *HERE.glob("sounds/*.pcm"),
    *[p for directory in ("vendor/fbw", "vendor/ewd_fadec", "fonts")
      for p in (HERE / directory).glob("*") if p.is_file() and not p.name.startswith("._")]]
EXPORTS = {"XPluginStart", "XPluginStop", "XPluginEnable", "XPluginDisable",
           "XPluginReceiveMessage"}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=HERE / "dist")
    parser.add_argument("--datarefs", type=Path, help="Optional installed simulator DataRefs.txt for raw-input contract verification")
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--mingw", default="/opt/homebrew/bin/x86_64-w64-mingw32-gcc")
    parser.add_argument("--allow-missing-pages", action="store_true",
                        help="Incremental integration build: link only the MFD page modules that exist")
    parser.add_argument("--skip-tests", default="",
                        help="Comma-separated optional MFD page tests to leave out (incremental builds only)")
    parser.add_argument("--skip-pages", default="",
                        help="Comma-separated MFD page modules to leave out (incremental builds only)")
    args = parser.parse_args()
    if platform.system() != "Darwin":
        parser.error("This build orchestrator requires macOS for the universal Mac slice")
    sdk, output = args.sdk.resolve(), args.output_dir.absolute()
    if output.exists() or output.is_symlink():
        parser.error("Output directory already exists; select a fresh staging path")
    skipped_pages = {name for name in args.skip_pages.split(",") if name}
    if skipped_pages and not args.allow_missing_pages:
        parser.error("--skip-pages requires --allow-missing-pages")
    mfd_pages = [name for name in MFD_PAGE_MODULES
                 if (HERE / f"a380x_mfd_page_{name}.c").is_file() and name not in skipped_pages]
    missing_pages = [name for name in MFD_PAGE_MODULES if name not in mfd_pages]
    if missing_pages and not args.allow_missing_pages:
        parser.error(f"Missing MFD page modules: {missing_pages} (use --allow-missing-pages for an incremental build)")
    mfd_page_sources = [HERE / f"a380x_mfd_page_{name}.c" for name in mfd_pages]
    mfd_page_flags = [f"-DA380X_MFD_HAVE_PAGE_{name.upper()}=1" for name in mfd_pages if name != "data"]
    MFD_SOURCES = [*MFD_CORE_SOURCES, *mfd_page_sources]
    skipped_tests = {name for name in args.skip_tests.split(",") if name}
    if skipped_tests and not args.allow_missing_pages:
        parser.error("--skip-tests requires --allow-missing-pages")
    mfd_optional_tests = [(name, kind) for name, kind in MFD_OPTIONAL_TESTS
                          if (HERE / f"{name}.c").is_file() and name not in skipped_tests]
    headers = sdk / "CHeaders/XPLM"
    framework = sdk / "Libraries/Mac/XPLM.framework/XPLM"
    import_library = sdk / "Libraries/Win/XPLM_64.lib"
    header_names = ("XPLMDataAccess.h", "XPLMDefs.h", "XPLMDisplay.h",
                    "XPLMGraphics.h", "XPLMPlugin.h", "XPLMProcessing.h",
                    "XPLMUtilities.h", "XPLMMenus.h", "XPLMSound.h", "XPLMNavigation.h", "XPLMScenery.h")
    inputs = [headers / name for name in header_names]
    for path in [*inputs, framework, import_library, PLUGIN, CORE, CORE_HEADER, EFB, EFB_HEADER, TEST,
                 EWD_FONT, EWD_FONT_HEADER, EWD_FONT_GENERATOR, MFD_FONT_HEADER, MFD_FONT_GENERATOR, *MFD_SOURCES, *EXTRA_INPUTS]:
        if not path.is_file():
            parser.error(f"Missing build input: {path}")
    clang = shutil.which(args.clang)
    mingw = shutil.which(args.mingw)
    objdump = shutil.which(args.mingw.removesuffix("gcc") + "objdump")
    if not all((clang, mingw, objdump, shutil.which("codesign"), shutil.which("lipo"))):
        parser.error("Missing clang, MinGW gcc/objdump, codesign, or lipo")
    vendor_manifest = json.loads((HERE / "vendor/fbw/manifest.json").read_text())
    for row in vendor_manifest["files"]:
        if digest(HERE / "vendor/fbw" / row["filename"]) != row["sha256"]:
            parser.error(f"Imported FBW source changed: {row['filename']}")
    ewd_fadec_manifest = json.loads((HERE / "vendor/ewd_fadec/manifest.json").read_text())
    if ewd_fadec_manifest["revision"] != vendor_manifest["revision"]:
        parser.error("EWD FADEC and FBW guidance source revisions disagree")
    for row in [*ewd_fadec_manifest["files"], *ewd_fadec_manifest.get("local_files", [])]:
        if "filename" in row and digest(HERE / "vendor/ewd_fadec" / row["filename"]) != row["sha256"]:
            parser.error(f"Imported EWD FADEC source changed: {row['filename']}")
    if digest(EWD_FONT) != EWD_FONT_SHA256:
        parser.error("Imported FBW EIS font differs from the pinned source")
    recorded_font_hash = re.search(r"Font SHA-256: ([0-9a-f]{64})", EWD_FONT_HEADER.read_text())
    if not recorded_font_hash or recorded_font_hash.group(1) != EWD_FONT_SHA256:
        parser.error("Embedded EWD font atlas does not identify the pinned source font")
    output.mkdir(parents=True)
    commands: list[dict[str, object]] = []

    def run(command: list[str]) -> str:
        completed = subprocess.run(command, text=True, capture_output=True)
        commands.append({"argv": command, "stdout": completed.stdout,
                         "stderr": completed.stderr, "returncode": completed.returncode})
        if completed.returncode:
            raise RuntimeError(f"Command failed ({completed.returncode}): {command!r}\n"
                               f"{completed.stdout}{completed.stderr}")
        return completed.stdout

    warnings = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror"]
    test_binary = output / "test_avionics_core"
    run([clang, *warnings, str(CORE), str(TEST), "-lm", "-o", str(test_binary)])
    test_result = run([str(test_binary)]).strip()

    adirs_test_binary = output / "test_adirs"
    run([clang, *warnings, str(ADIRS), str(ADIRS_TEST), "-lm", "-o", str(adirs_test_binary)])
    adirs_result = run([str(adirs_test_binary)]).strip()
    adirs_adapter_binary = output / "test_adirs_adapter"
    adirs_window_binary = output / "test_adirs_window"
    for source, binary in ((ADIRS_ADAPTER_TEST, adirs_adapter_binary), (ADIRS_WINDOW_TEST, adirs_window_binary)):
        run([clang, *warnings, "-Wno-deprecated-declarations", "-DAPL=1", "-DXPLM200=1", "-DXPLM300=1", "-DXPLM301=1", "-I", str(headers),
             str(source), str(ADIRS), "-lm", "-o", str(binary)])
    adirs_adapter_result = run([str(adirs_adapter_binary)]).strip()
    adirs_window_result = run([str(adirs_window_binary)]).strip()
    adirs_contracts = run([str(adirs_adapter_binary), "--contracts"]).splitlines()

    def compile_laws(compiler, flags, suffix):
        objects = []
        for source in CXX_SOURCES:
            obj = output / (source.stem + suffix + ".o")
            run([compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-fvisibility=hidden", *flags, "-c", str(source), "-o", str(obj)])
            objects.append(str(obj))
        return objects

    mac_laws = compile_laws(clang, ["-arch", "arm64", "-arch", "x86_64",
                                    "-mmacosx-version-min=11.0"], "_mac")
    mac_ewd_fadec = [obj for source, obj in zip(CXX_SOURCES, mac_laws) if source in EWD_FADEC_SOURCES]
    ewd_data_binary = output / "test_ewd_data"
    run([clang, *warnings, "-DAPL=1", "-DXPLM200=1", "-I", str(headers),
         str(EWD_DATA_TEST), *mac_ewd_fadec, "-lc++", "-lm", "-o", str(ewd_data_binary)])
    ewd_data_result = run([str(ewd_data_binary)]).strip()
    ewd_render_binary = output / "test_ewd_render"
    run([clang, *warnings, str(EWD_RENDER_TEST), "-lm", "-o", str(ewd_render_binary)])
    ewd_previews = [output / f"ewd-{state}.svg" for state in ("inactive", "live", "warning")]
    ewd_render_result = run([str(ewd_render_binary), *map(str, ewd_previews)]).strip()
    font_data_uri = "data:font/ttf;base64," + base64.b64encode(EWD_FONT.read_bytes()).decode("ascii")
    for preview in ewd_previews:
        svg = preview.read_text()
        if svg.count("EWD_FONT_DATA_URI") != 1:
            raise RuntimeError(f"EWD preview missing its font placeholder: {preview}")
        preview.write_text(svg.replace("EWD_FONT_DATA_URI", font_data_uri))
    guidance_test_binary = output / "test_guidance"
    run([clang, *warnings, str(CORE), str(GUIDANCE), str(GUIDANCE_TEST),
         *mac_laws, "-lc++", "-lm", "-o", str(guidance_test_binary)])
    guidance_result = run([str(guidance_test_binary)]).strip()
    pfd_test_binary = output / "test_pfd_render"
    run([clang, *warnings, str(CORE), str(PFD_TEST), "-lm", "-o", str(pfd_test_binary)])
    pfd_result = run([str(pfd_test_binary), str(output / "pfd-climb.svg"),
                      str(output / "pfd-approach.svg"), str(output / "fcu-selected.svg"),
                      str(output / "fcu-managed.svg"), str(output / "efis-cp.svg"), str(output / "efis-nd.svg")]).strip()
    for preview in (output / "pfd-climb.svg", output / "pfd-approach.svg", output / "nd-fbw-arc.svg",
                    output / "nd-fbw-rose.svg", output / "nd-fbw-plan.svg"):
        preview.write_text(preview.read_text().replace("PFD_FONT_DATA_URI",font_data_uri))
    efb_test_binary = output / "test_efb_render"
    run([clang, *warnings, "-Wno-deprecated-declarations", "-DAPL=1", "-I", str(headers),
         str(EFB_TEST), str(CORE), "-lm", "-o", str(efb_test_binary)])
    efb_result = run([str(efb_test_binary), str(output)]).strip()
    adapter_test_binary = output / "test_fcu_adapter"
    run([clang, *warnings, "-DAPL=1", "-DXPLM200=1", "-DXPLM300=1", "-I", str(headers), str(ADAPTER_TEST), str(GUIDANCE), str(CORE),
         *mac_laws, "-lc++", "-lm", "-o", str(adapter_test_binary)])
    adapter_result = run([str(adapter_test_binary)]).strip()
    control_lifecycle_binary = output / "test_control_lifecycle"
    run([clang, *warnings, "-DAPL=1", "-DXPLM200=1", "-I", str(headers),
         str(CONTROL_LIFECYCLE_TEST), "-lm", "-o", str(control_lifecycle_binary)])
    control_lifecycle_result = run([str(control_lifecycle_binary)]).strip()
    clock_render_binary = output / "test_clock_render"
    run([clang, *warnings, str(CLOCK_RENDER_TEST), "-o", str(clock_render_binary)])
    clock_render_result = run([str(clock_render_binary), str(output / "clock.svg")]).strip()
    efis_test_binary = output / "test_efis"
    run([clang, *warnings, "-DAPL=1", "-DXPLM200=1", "-DXPLM300=1", "-I", str(headers),
         str(EFIS_TEST), "-lm", "-o", str(efis_test_binary)])
    efis_result = run([str(efis_test_binary)]).strip()
    efis_contracts = run([str(efis_test_binary), "--contracts"]).splitlines()
    nd_test_binary = output / "test_efis_nd"
    run([clang, *warnings, str(EFIS_ND_TEST), "-lm", "-o", str(nd_test_binary)])
    nd_result = run([str(nd_test_binary)]).strip()
    airport_test_binary = output / "test_efis_airport"
    run([clang, *warnings, str(EFIS_AIRPORT_TEST), "-lm", "-o", str(airport_test_binary)])
    airport_result = run([str(airport_test_binary)]).strip()
    contracts = run([str(adapter_test_binary), "--contracts"]).splitlines()

    # ---- MFD / FMS core: objects once (host arch, no XPLM include path = proves XPLM-free) ----
    mfd_objects = []
    mfd_object_dir = output / "mfd-objects"
    mfd_object_dir.mkdir()
    for source in MFD_SOURCES:
        obj = mfd_object_dir / (source.stem + ".o")
        run([clang, *warnings, *mfd_page_flags, "-c", str(source), "-o", str(obj)])
        mfd_objects.append(str(obj))
    for source in MFD_SOURCES:   # gcc-only warnings must not first appear at the Windows link
        run([mingw, *warnings, *mfd_page_flags, "-c", str(source), "-o", str(mfd_object_dir / (source.stem + "_win.o"))])
    mfd_previews_root = output / "mfd-previews"
    mfd_test_results = {}
    mfd_previews = []
    for name, kind in [*MFD_TESTS, *mfd_optional_tests]:
        binary = output / name
        run([clang, *warnings, *mfd_page_flags, str(HERE / f"{name}.c"), *mfd_objects, "-lm", "-o", str(binary)])
        argv = [str(binary)]
        if kind == "navdata":
            argv.append(str(MFD_TEST_DATA / "navdata"))
        elif kind == "render":
            preview_dir = mfd_previews_root / name
            preview_dir.mkdir(parents=True)
            argv.append(str(preview_dir))
        lines = run(argv).strip().splitlines()
        mfd_test_results[name] = lines[-1] if lines else ""
        if kind == "render":
            mfd_previews.extend({"file": f"{name}/{p.name}", "sha256": digest(p)}
                                for p in sorted(preview_dir.iterdir()) if p.is_file() and not p.name.startswith("._"))
    mfd_adapter_binary = output / "test_mfd_adapter"
    run([clang, *warnings, "-Wno-deprecated-declarations", "-DAPL=1", "-DXPLM200=1", "-DXPLM210=1",
         "-DXPLM300=1", "-DXPLM301=1", "-DXPLM400=1", "-DXPLM410=1", *mfd_page_flags, "-I", str(headers),
         str(MFD_ADAPTER_TEST), *mfd_objects, "-lm", "-o", str(mfd_adapter_binary)])
    mfd_adapter_result = run([str(mfd_adapter_binary)]).strip()
    mfd_contracts = run([str(mfd_adapter_binary), "--contracts"]).splitlines()
    fms_adapter_source = "".join((HERE / name).read_text() for name in ("a380x_fms_adapter.inc", "a380x_mfd_systems_host.inc"))
    fms_raw = set(re.findall(r'FMSA_RAW\(\s*"(sim/[^"\n]+)"', fms_adapter_source))
    if not fms_raw:
        raise RuntimeError("No FMS raw-input contracts were discovered")
    fms_dataref_names = {"a380x/xp12/fms/" + name for name in
                         re.findall(r'(?<!define )FMSR(?:_I)?\(\s*([a-z0-9_]+)\s*\)', fms_adapter_source)}
    fms_dataref_names |= {"a380x/xp12/fms/" + name for name in
                          re.findall(r'FMSR_(?:AS|CTL)\(\s*"([a-z0-9_]+)"', fms_adapter_source)}
    surv_dataref_names = {"a380x/xp12/surv/" + name for name in
                          re.findall(r'(?<!define )SURVR\(\s*"([a-z0-9_]+)"', fms_adapter_source)}
    center_right_raw = {
        name
        for filename in ("a380x_clock.inc", "a380x_center_right.inc")
        for name in re.findall(r'XPLMFindDataRef\(\s*"(sim/[^"\n]+)"', (HERE / filename).read_text())
    }
    ewd_input_contracts = re.findall(r'EWD_INPUT\(\s*"([^"\n]+)"', (HERE / "a380x_ewd_data.inc").read_text())
    ewd_raw = {name for name in ewd_input_contracts if name.startswith("sim/")}
    if not ewd_raw:
        raise RuntimeError("No EWD raw-input contracts were discovered")
    raw_inputs = sorted({line[2:] for line in [*contracts, *efis_contracts, *adirs_contracts] if line.startswith("R ")}
                        | center_right_raw | ewd_raw | fms_raw)
    if args.datarefs:
        raw_catalog = {line.split()[0] for line in args.datarefs.read_text().splitlines() if line.strip()}
        missing_raw = set(raw_inputs) - raw_catalog
        if missing_raw:
            raise RuntimeError(f"Missing installed raw inputs: {sorted(missing_raw)}")
    windows_laws = compile_laws(mingw, [], "_win")

    # Keep package/manipulator declarations identical to the native registry.
    import sys
    sys.path.insert(0, str(HERE.parent.parent / "tools"))
    from avionics_provider import (KCCU_COMMANDS, KCCU_DATAREFS, MFD_COMMANDS, MFD_DATAREFS, FMS_DATAREFS,
                                   MFD_DEVICES, SURV_DATAREFS)
    from avionics_provider import ADIRS_COMMANDS, ADIRS_DATAREFS, CENTER_RIGHT_COMMANDS, CENTER_RIGHT_DATAREFS, CLOCK_COMMANDS, CLOCK_DATAREFS, COCKPIT_DOOR_COMMANDS, COCKPIT_DOOR_DATAREFS, EFIS_COMMANDS, EFIS_DATAREFS, DU_COMMANDS, DU_DATAREFS, FCU_COMMANDS, FCU_DATAREFS, PFD_DATAREFS
    if ({line[2:] for line in adirs_contracts if line.startswith("C ")} | {"a380x/xp12/adirs/window_toggle"} != set(ADIRS_COMMANDS)
            or {line[2:] for line in adirs_contracts if line.startswith("D ")} != set(ADIRS_DATAREFS)):
        raise RuntimeError("ADIRS registry disagrees with cockpit provider contract")
    adapter = (HERE / "a380x_fcu_adapter.inc").read_text()
    command_names = {"a380x/xp12/fcu/" + name for name in re.findall(r'FCUC\("([^"\n]+)"', adapter)}
    dataref_names = {"a380x/xp12/fcu/" + name for name in re.findall(r'FCUR\("([^"\n]+)"', adapter)}
    registered_commands = {line[2:] for line in contracts if line.startswith("C ")}
    registered_datarefs = {line[2:] for line in contracts if line.startswith("D ")}
    if (len(FCU_COMMANDS) != len(set(FCU_COMMANDS)) or len(FCU_DATAREFS) != len(set(FCU_DATAREFS))
            or command_names != set(FCU_COMMANDS) or dataref_names != set(FCU_DATAREFS)
            or registered_commands != command_names or registered_datarefs != dataref_names):
        raise RuntimeError("FCU native registry disagrees with cockpit provider contract")
    if ({line[2:] for line in contracts if line.startswith("P ")} != set(PFD_DATAREFS)
            or len(PFD_DATAREFS) != len(set(PFD_DATAREFS))):
        raise RuntimeError("PFD native registry disagrees with cockpit provider contract")
    if ({line[2:] for line in efis_contracts if line.startswith("C ")} != set(EFIS_COMMANDS)
            or {line[2:] for line in efis_contracts if line.startswith("D ")} != set(EFIS_DATAREFS)):
        raise RuntimeError("EFIS native registry disagrees with cockpit provider contract")
    clock = (HERE / "a380x_clock.inc").read_text()
    clock_commands = {"a380x/xp12/clock/" + name
                      for name in re.findall(r'CLOCK_CMD\("([^"\n]+)"', clock)}
    clock_datarefs = {"a380x/xp12/clock/" + name
                      for name in re.findall(r'CLOCK_REF\("([^"\n]+)"', clock)}
    if (len(CLOCK_COMMANDS) != len(set(CLOCK_COMMANDS))
            or len(CLOCK_DATAREFS) != len(set(CLOCK_DATAREFS))
            or clock_commands != set(CLOCK_COMMANDS)
            or clock_datarefs != set(CLOCK_DATAREFS)):
        raise RuntimeError("Clock native registry disagrees with cockpit provider contract")
    center_right = (HERE / "a380x_center_right.inc").read_text()
    center_right_commands = {"a380x/xp12/center_right/" + name
        for name in re.findall(r'CR_CMD\("([^"\n]+)"', center_right)}
    center_right_datarefs = {"a380x/xp12/center_right/" + name
        for name in re.findall(r'CR_REF\("([^"\n]+)"', center_right)}
    if (center_right_commands != set(CENTER_RIGHT_COMMANDS)
            or center_right_datarefs != set(CENTER_RIGHT_DATAREFS)):
        raise RuntimeError("Center-right native registry disagrees with cockpit provider contract")
    cockpit_door = (HERE / "a380x_cockpit_door.inc").read_text()
    for command in COCKPIT_DOOR_COMMANDS:
        if cockpit_door.count('XPLMCreateCommand("' + command + '"') != 1:
            raise RuntimeError("Cockpit-door native command registry disagrees with provider contract")
    for dataref in COCKPIT_DOOR_DATAREFS:
        if cockpit_door.count('XPLMRegisterDataAccessor(\n        "' + dataref + '"') != 1:
            raise RuntimeError("Cockpit-door native dataref registry disagrees with provider contract")
    du_source = (HERE / "a380x_du.inc").read_text()
    du_channels = re.findall(r'\{"(captain_[a-z_]+|first_officer_[a-z_]+)", 1\.0f', du_source)
    du_commands = {f"a380x/xp12/du/{channel}_{direction}"
                   for channel in du_channels for direction in ("up", "down")}
    du_datarefs = {f"a380x/xp12/du/{channel}" for channel in du_channels}
    if (len(du_channels) != 8 or len(du_channels) != len(set(du_channels))
            or du_commands != set(DU_COMMANDS) or du_datarefs != set(DU_DATAREFS)):
        raise RuntimeError("DU native registry disagrees with cockpit provider contract")

    if ({line[2:] for line in mfd_contracts if line.startswith("C ")} != set(KCCU_COMMANDS) | set(MFD_COMMANDS)
            or {line[2:] for line in mfd_contracts if line.startswith("D ")} != set(KCCU_DATAREFS) | set(MFD_DATAREFS)
            or {line[2:] for line in mfd_contracts if line.startswith("V ")} != {device for device, _ in MFD_DEVICES}
            or fms_dataref_names != set(FMS_DATAREFS) or len(FMS_DATAREFS) != len(set(FMS_DATAREFS))
            or surv_dataref_names != set(SURV_DATAREFS)):
        raise RuntimeError("MFD native registry disagrees with cockpit provider contract")

    common = [*warnings, "-Wno-deprecated-declarations", "-fvisibility=hidden",
              "-DXPLM200=1", "-DXPLM210=1", "-DXPLM300=1", "-DXPLM400=1", "-DXPLM410=1",
              *mfd_page_flags, "-I", str(headers)]
    mac = output / "mac.xpl"
    run([clang, *common, "-DAPL=1", "-arch", "arm64", "-arch", "x86_64",
         "-mmacosx-version-min=11.0", "-dynamiclib", str(PLUGIN), str(CORE), str(EFB), str(GUIDANCE), str(ADIRS),
         *map(str, MFD_SOURCES), *mac_laws, "-lc++",
         "-F", str(sdk / "Libraries/Mac"), "-framework", "XPLM",
         "-framework", "OpenGL", "-Wl,-install_name,A380XAvionics.xpl",
         "-o", str(mac)])
    run(["codesign", "--force", "--sign", "-", str(mac)])
    run(["codesign", "--verify", "--strict", str(mac)])
    architectures = run(["lipo", "-archs", str(mac)]).split()
    if set(architectures) != {"arm64", "x86_64"}:
        raise RuntimeError(f"Unexpected macOS architectures: {architectures}")
    for arch in architectures:
        symbols = run(["nm", "-arch", arch, "-gjU", str(mac)]).split()
        if set(symbol.removeprefix("_") for symbol in symbols) != EXPORTS:
            raise RuntimeError(f"Unexpected exported macOS symbols: {symbols}")

    windows = output / "win.xpl"
    run([mingw, *common, "-DIBM=1", "-shared", str(PLUGIN), str(CORE), str(EFB), str(GUIDANCE), str(ADIRS),
         *map(str, MFD_SOURCES), *windows_laws, "-static", "-lstdc++", "-lwinpthread",
         str(import_library), "-lopengl32", "-static-libgcc", "-lm",
         "-Wl,--exclude-all-symbols", "-o", str(windows)])
    windows_headers = run([objdump, "-p", str(windows)])
    windows_format = run([objdump, "-f", str(windows)])
    if "pei-x86-64" not in windows_format:
        raise RuntimeError("Windows plugin is not PE x86-64")
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
    allowed = {"xplm_64.dll", "opengl32.dll", "kernel32.dll", "msvcrt.dll",
               "api-ms-win-crt-heap-l1-1-0.dll",
               "api-ms-win-crt-convert-l1-1-0.dll",
               "api-ms-win-crt-environment-l1-1-0.dll",
               "api-ms-win-crt-private-l1-1-0.dll",
               "api-ms-win-crt-runtime-l1-1-0.dll",
               "api-ms-win-crt-stdio-l1-1-0.dll",
               "api-ms-win-crt-string-l1-1-0.dll",
               "api-ms-win-crt-math-l1-1-0.dll",
               "api-ms-win-crt-time-l1-1-0.dll",
               # MFD core (2026-10-07): qsort (navdata CIFP sort) and localeconv (locale-independent
               # number formats). Same Universal CRT (ucrtbase.dll) as the API sets above; present on
               # Windows 10+ and with the UCRT redistributable. No new runtime library.
               "api-ms-win-crt-utility-l1-1-0.dll",
               "api-ms-win-crt-locale-l1-1-0.dll"}
    if not {name.lower() for name in dlls} <= allowed:
        raise RuntimeError(f"Unexpected Windows dependencies: {dlls}")

    report = {
        "format": 1,
        "source_revision": "2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2",
        "logic_sources": [
            "fbw-a380x/src/systems/instruments/src/PFD/FMADefinitions.ts",
            "fbw-a380x/src/wasm/fbw_a380/src/model/A380FgOuterLoops.cpp",
            "fbw-a380x/src/wasm/fbw_a380/src/model/A380FcuComputer.cpp",
            "fbw-a380x/src/wasm/fbw_a380/src/model/A380PrimComputerFg.cpp",
            "fbw-a380x/src/systems/instruments/src/PFD/PFD.tsx",
            "fbw-a380x/src/systems/instruments/src/PFD/PFDUtils.tsx",
            "fbw-a380x/src/systems/instruments/src/PFD/AttitudeIndicatorHorizon.tsx",
            "fbw-a380x/src/systems/instruments/src/PFD/AttitudeIndicatorFixed.tsx",
            "fbw-a380x/src/systems/instruments/src/PFD/SpeedIndicator.tsx",
            "fbw-a380x/src/systems/instruments/src/PFD/AltitudeIndicator.tsx",
            "fbw-a380x/src/systems/instruments/src/PFD/HeadingIndicator.tsx",
            "fbw-a380x/src/systems/instruments/src/PFD/VerticalSpeedIndicator.tsx",
            "fbw-a380x/src/systems/instruments/src/PFD/LowerArea.tsx",
            "src/systems/systems-host/CpiomC/FlightWarningSystem/FwsFlightPhases.ts",
            "fbw-a32nx/src/systems/instruments/src/EFB/Service/TODCalculator.ts",
            "fbw-a380x/src/systems/instruments/src/EFB/Assets/Checklists/checklists.json5",
            "fbw-common/src/systems/instruments/src/EFB/Efb.tsx",
            "fbw-common/src/systems/instruments/src/EFB/Assets/Theme.css",
            "fbw-common/src/systems/instruments/src/EFB/ToolBar/ToolBar.tsx",
            "fbw-common/src/systems/instruments/src/EFB/Dashboard/Dashboard.tsx",
            "fbw-a380x/src/systems/instruments/src/EWD/EWD.tsx",
            "fbw-a380x/src/systems/instruments/src/EWD/style.scss",
            "fbw-a380x/src/systems/instruments/src/EWD/elements/EngineGauge.tsx",
            "fbw-a380x/src/systems/instruments/src/EWD/elements/ThrustGauge.tsx",
            "fbw-a380x/src/systems/instruments/src/EWD/elements/N1.tsx",
            "fbw-a380x/src/systems/instruments/src/EWD/elements/EGT.tsx",
            "fbw-a380x/src/systems/instruments/src/EWD/elements/WdMemos.tsx",
            "fbw-a380x/src/systems/instruments/src/MsfsAvionicsCommon/definitions.scss",
            "fbw-a380x/src/systems/systems-host/CpiomC/FlightWarningSystem/FwsMemos.ts",
            "fbw-a380x/src/wasm/fadec_a380x/src/Fadec/EngineControl_A380X.cpp",
            "fbw-a380x/src/systems/instruments/src/MFD/MFD.tsx",
            "fbw-a380x/src/systems/instruments/src/MFD/FMC/FlightManagementComputer.ts",
            "fbw-a380x/src/systems/instruments/src/MFD/FMC/FmcAircraftInterface.ts",
            "fbw-a380x/src/systems/instruments/src/MFD/pages/FMS",
            "fbw-a380x/src/systems/instruments/src/MFD/pages/common",
            "fbw-a380x/src/systems/instruments/src/MFD/shared",
            "fbw-common/src/systems/fmgc/src/flightplanning",
        ],
        "stock_xplane_efis_modes_consumed": True,
        "pfd_design_reference": {
            "product": "FlyByWire A380X",
            "scope": "open-source PFD coordinate geometry, symbology, and display algorithms translated to native OpenGL",
            "source_revision": "2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2",
            "runtime_dependency": False,
            "imported_files": [],
        },
        "proprietary_reference": {
            "product": "FlightFactor A350",
            "scope": "observation only; not an implementation source",
            "runtime_dependency": False,
            "imported_files": [],
        },
        "display_targets": [
            "captain_pfd", "first_officer_pfd", "captain_nd", "first_officer_nd",
            "ewd", "captain_efb", "first_officer_efb", "fcu", "captain_efis", "first_officer_efis",
            "captain_mfd", "first_officer_mfd",
        ],
        "xplm_api": "XPLM410",
        "minimum_xplane": "12.1.0",
        "mock_test_result": test_result,
        "adirs_test_result": adirs_result,
        "adirs_adapter_test_result": adirs_adapter_result,
        "adirs_window_test_result": adirs_window_result,
        "adirs_commands": list(ADIRS_COMMANDS), "adirs_datarefs": list(ADIRS_DATAREFS),
        "guidance_test_result": guidance_result,
        "pfd_render_test_result": pfd_result,
        "efb_render_test_result": efb_result,
        "fcu_adapter_test_result": adapter_result,
        "control_lifecycle_test_result": control_lifecycle_result,
        "clock_render_test_result": clock_render_result,
        "efis_test_result": efis_result,
        "efis_nd_test_result": nd_result,
        "efis_airport_test_result": airport_result,
        "ewd_data_test_result": ewd_data_result,
        "ewd_render_test_result": ewd_render_result,
        "ewd_input_contracts": ewd_input_contracts,
        "ewd_render_previews": [{"file": path.name, "sha256": digest(path)} for path in ewd_previews],
        "ewd_font": {
            "upstream_path": "fbw-a380x/src/fonts/FBW-Display-EIS-A380.ttf",
            "source_revision": vendor_manifest["revision"],
            "sha256": digest(EWD_FONT),
            "embedded_atlas": {"file": EWD_FONT_HEADER.name, "sha256": digest(EWD_FONT_HEADER)},
            "generator": {"file": "tools/build_ewd_font.py", "sha256": digest(EWD_FONT_GENERATOR)},
        },
        "efis_commands": list(EFIS_COMMANDS), "efis_datarefs": list(EFIS_DATAREFS),
        "mfd_ui_test_result": mfd_test_results["test_mfd_ui"],
        "mfd_formats_test_result": mfd_test_results["test_mfd_formats"],
        "fms_core_test_result": mfd_test_results["test_fms_core"],
        "navdata_test_result": mfd_test_results["test_navdata"],
        "mfd_render_test_result": mfd_test_results["test_mfd_render"],
        "mfd_page_test_results": {name: mfd_test_results[name] for name, _ in mfd_optional_tests},
        "mfd_adapter_test_result": mfd_adapter_result,
        "mfd_render_previews": mfd_previews,
        "mfd_page_modules": mfd_pages,
        "mfd_missing_page_modules": missing_pages,
        "mfd_font": {
            "embedded_atlas": {"file": MFD_FONT_HEADER.name, "sha256": digest(MFD_FONT_HEADER)},
            "generator": {"file": "tools/build_mfd_font.py", "sha256": digest(MFD_FONT_GENERATOR)},
            "source_sha256": re.findall(r"SHA-256: ([0-9a-f]{64})", MFD_FONT_HEADER.read_text()[:8192]),
        },
        "kccu_commands": list(KCCU_COMMANDS), "kccu_datarefs": list(KCCU_DATAREFS),
        "mfd_commands": list(MFD_COMMANDS), "mfd_datarefs": list(MFD_DATAREFS),
        "fms_datarefs": list(FMS_DATAREFS), "surv_datarefs": list(SURV_DATAREFS),
        "cockpit_devices": [{"id": device, "name": name, "width": 768, "height": 1024}
                            for device, name in MFD_DEVICES],
        "raw_inputs": raw_inputs,
        "installed_datarefs_verified": str(args.datarefs) if args.datarefs else None,
        "fcu_commands": list(FCU_COMMANDS), "fcu_datarefs": list(FCU_DATAREFS),
        "pfd_datarefs": list(PFD_DATAREFS),
        "imported_fbw_laws": json.loads((HERE / "vendor/fbw/manifest.json").read_text()),
        "imported_ewd_fadec": ewd_fadec_manifest,
        "simulator_tested": False,
        "sdk_root": str(sdk),
        "sdk_inputs": [{"path": str(path.relative_to(sdk)), "sha256": digest(path)}
                       for path in [*inputs, framework, import_library]],
        "sources": [{"name": str(path.resolve().relative_to(HERE)), "sha256": digest(path)}
                    for path in (PLUGIN, CORE, CORE_HEADER, EFB, EFB_HEADER, TEST, Path(__file__), *EXTRA_INPUTS,
                                 *mfd_page_sources, *[HERE / f"{name}.c" for name, _ in mfd_optional_tests])],
        "binaries": [
            {"file": mac.name, "sha256": digest(mac), "bytes": mac.stat().st_size,
             "architectures": architectures},
            {"file": windows.name, "sha256": digest(windows), "bytes": windows.stat().st_size,
             "architectures": ["x86_64"], "dependencies": dlls},
        ],
        "commands": commands,
    }
    (output / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(test_result)
    print(f"Staged A380X avionics plugin: {output}")


if __name__ == "__main__":
    main()
