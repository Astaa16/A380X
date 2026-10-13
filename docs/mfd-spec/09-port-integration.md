# MFD spec 09: port integration (WP-J build, plugin, cockpit OBJ, KCCU)

Status: normative for the **integration mechanics** of WP-J (spec 12 §8). It was written against the tree on
2026-10-06/07. Line numbers refer to that state.

Precedence:

* Spec 12 owns the module partition.
* Spec 08 (`08-xplane-cockpit-device.md`) owns the device behaviour. This covers device IDs, OBJ attribute values,
  the visible-object/touch-quad split, `drawOnDemand`, `XPLMFindSymbol` linking, the keyboard policy, brightness,
  popups and the fallbacks.
* This file adopts those decisions and specifies every file, list, test and contract the integrator changes.
* Spec 09b holds the measured geometry.

**Name conflict, resolved.** Spec 07 §1.4 talks about `a380x_fms_core.c` and `a380x_fms_adapter.inc`. Spec 12 §3
names `a380x_fms.c` and `a380x_mfd_adapter.inc`. Spec 12 wins.

* Use `a380x_fms.c/.h` for the FMS core.
* The XPLM boundary may be split into `a380x_mfd_adapter.inc` (devices, KCCU, keyboard, popups) and
  `a380x_fms_adapter.inc` (FMS inputs and outputs, spec 07 §9-10). Both are WP-J files, included by
  `a380x_avionics.c`.
* Update spec 12's table when the split is made.

Spec 07's line references are now a few lines off:

* `configured_v1 = 145.0` is at `a380x_avionics.c:285`.
* The `read_guidance_inputs` memset is at `a380x_fcu_adapter.inc:157`.

---

## 0. Verified environment facts

| Item | Value |
|---|---|
| Port root | `/Volumes/Savs' drive/A380X-XPlane12/xplane-port`. This is not a git repository. |
| Packaged aircraft | `output/A380X-XP12` |
| Installation into X-Plane | **A symlink, not a copy.** `~/Library/Application Support/Steam/steamapps/common/X-Plane 12/Aircraft/A380X-XP12 → …/xplane-port/output/A380X-XP12`. Every write into `output/` is live. |
| Plugin as loaded | `Aircraft/A380X-XP12/plugins/A380XAvionics/mac_x64/A380XAvionics.xpl`, signature `org.a380x.xp12.fbw-avionics.v1` (`Log.txt` lines 423-424). Also `win_x64/A380XAvionics.xpl`, `sounds/`, `build-report.json`. |
| X-Plane | 12.4.3-r2 (build 124311). The log header says "Intel 64-bit"; `Launch_X-Plane_12_Rosetta.command` exists. The host is an Apple M1 Pro. Both slices of the universal plugin matter. |
| SDK | `/Volumes/MacGames/A380X-XPlane12-build/sdk/XPSDK430/SDK` (SDK 4.3.0). The avionics device API sits behind `#if defined(XPLM410)` inside `#if defined(XPLM400)` (`XPLMDisplay.h:277-992`). `XPLMCreateAvionicsEx`, `XPLMDestroyAvionics`, `XPLMIsAvionicsBound`, … are exported by `Libraries/Mac/XPLM.framework/XPLM` (36 avionics symbols) and `Libraries/Win/XPLM_64.lib`. |
| Toolchain | Apple clang 21.0.0; `/opt/homebrew/bin/x86_64-w64-mingw32-gcc` 15.2.0 with objdump; Python 3.13 with PIL 11.3 and fontTools 4.63. **No FontForge.** |
| Compile probe | A file using `XPLMCreateAvionics_t` compiles with `-std=c11 -O2 -Wall -Wextra -Werror -DXPLM200..XPLM400 -DXPLM410=1` on both clang (`-DAPL=1`) and MinGW (`-DIBM=1`). Without `-DXPLM410` it fails: "unknown type name 'XPLMCreateAvionics_t'". |
| Disk: `Savs' drive` | exFAT, **128 KiB allocation block**, 1.7 GB free. Every file also gets a `._` AppleDouble companion, so each new file costs at least 256 KiB. |
| Disk: `MacGames` | APFS, 4 KiB blocks, about 208-218 MB free. Holds the build directory (15 GB, more than 70 old staging folders of 3-5 MB each), the SDK, Blender and the 747 reference template. |
| Build directory | Hard-coded in `tools/build_local_mac.sh:5` as `/Volumes/MacGames/A380X-XPlane12-build`. |

---

## 1. Integration checklist

### 1.1 New native files and where each is compiled

| Files (spec 12 §3) | Plugin link, Mac and Windows | Test compile | Report |
|---|---|---|---|
| Core `.c` files: `a380x_mfd_draw.c`, `a380x_mfd_ui.c`, `a380x_mfd.c`, `a380x_mfd_formats.c`, `a380x_fms_messages.c`, `a380x_fms_speeds.c`, `a380x_fms.c`, `a380x_navdata.c`, `a380x_fms_fpln.c`, `a380x_mfd_page_{init,fuel,data,sec,perf,fpln,fpln_rev,position,surv,atccom,fcubkup}.c` (20 files) | Yes. New list `MFD_SOURCES` in `build.py`. | Yes, in the pure-core tests with `*warnings` and **without** `-I headers`. That proves they are XPLM-free. | `sources[]` |
| Public headers `a380x_mfd*.h`, `a380x_fms*.h`, `a380x_navdata.h` | – (included) | header smoke test | auto: `HERE.glob("*.h")` |
| `a380x_mfd_adapter.inc` (+ optional `a380x_fms_adapter.inc`) | via `#include` in `a380x_avionics.c` | `test_mfd_adapter.c` (mock XPLM) | auto: `HERE.glob("*.inc")` |
| `a380x_mfd_render.inc` (GL executor) | via `#include` in `a380x_avionics.c` | recommended: a GL-capture test in the style of `test_ewd_render.c`, so `-Werror` problems surface before the plugin link | auto |
| `a380x_mfd_font.h` (generated), `tools/build_mfd_font.py`, font sources in `fonts/` | via the render `.inc` | – | `mfd_font` block |
| Tests: `test_mfd_ui.c`, `test_mfd_formats.c`, `test_fms_core.c`, `test_navdata.c` (+ `test_data/`), `test_mfd_render.c`, `test_mfd_adapter.c`, `test_mfd_headers.c` | – | compile, then run | `*_test_result`, previews, contracts |

### 1.2 `native/avionics/a380x_avionics.c`

1. **XPLM410.**
   * Prefer the build flag (§1.3 g). The file defines only `XPLM301` itself (line 32).
   * Start `a380x_mfd_adapter.inc` with a guard:
     `#if !defined(XPLM400) || !defined(XPLM410)` / `#error "MFD adapter needs -DXPLM400=1 -DXPLM410=1"` / `#endif`.
2. **Headers.** After line 46 (`#include "a380x_pfd_data.h"`), add `#include "a380x_mfd.h"` and the FMS headers the
   adapter needs.
3. **Adapter include.**
   * Put `#include "a380x_mfd_adapter.inc"` (and `a380x_fms_adapter.inc`) after line 253
     (`#include "a380x_ewd_data.inc"`) and before `flight_loop`.
   * At that point `refs`, `guidance`, `guidance_inputs`, `pfd_aircraft`, `du_channels`/`DU_MFD`, `plugin_enabled`,
     `find_required()` and `failed()` are all visible.
   * Forward-declare the device draw callbacks there. They are defined after the renderer include (step 5).
4. **`flight_loop`** (lines 255-301), following spec 07 §1.4:
   * a. After `adirs_update(dt);` (line 278), call `fms_update(dt, powered)`. This runs the 10 Hz core with the
     previous frame's guidance state.
   * b. Line 285: replace `phase_inputs.configured_v1 = 145.0;` with the FMS V1 (0 = none). This also needs the
     `a380x_flight_phase_update` change "V1 ≤ 0 → never above V1" in `a380x_avionics_core.c` and a case in
     `test_avionics_core.c`.
   * c. `update_guidance(dt)` (line 289) is unchanged. The copy of managed targets lives in
     `read_guidance_inputs()` after the memset at `a380x_fcu_adapter.inc:157`.
   * d. After `cockpit_door_update();` (line 298), call `mfd_update(dt, powered)`. It:
     * drains the queued pointer and key events;
     * polls `XPLMIsCursorOverAvionics` for hover and leave;
     * derives DU on/off from `du_channels[side*DU_CHANNELS_PER_SIDE + DU_MFD].level` and power;
     * runs both sides' UI frames into display lists;
     * publishes `active_page`;
     * calls `XPLMAvionicsNeedsDrawing` only when a list changed (spec 08 §10.1).
   * Do not register a second flight loop.
5. **Renderer.**
   * Add `#include "a380x_mfd_render.inc"` to the renderer block (lines 303-308).
   * Define the screen and bezel draw callbacks after it.
   * `draw_panel` (322-365) is **not** touched. The MFD never draws into the 2048² panel texture.
   * The MFD font atlas is created lazily in the device callback, as `ewd_font_draw` does (`a380x_ewd_font.inc:31-46`).
6. **`XPluginStart`** (404-472):
   * Add `if (!mfd_init()) { … }` after the `a380x_efb_init()` block (437-452) and before
     `XPLMRegisterDrawCallback` (453).
     * Its failure path calls `a380x_efb_shutdown()`, then everything the EFB failure path already unwinds, in the
       same order.
     * The draw-callback failure path (453-468) gains `mfd_shutdown();` as its first call.
   * `mfd_init()` registers commands, datarefs, the menu, devices (through the `XPLMFindSymbol` table, spec 08 §8.3)
     and the key sniffer.
   * A device that cannot be created is **not** fatal: it drops to the spec 08 §10.6 F0 fallback. A command or
     dataref name collision **is** fatal (`return 0`), as for every other module.
   * Do **not** rename the plugin name `A380X FBW Avionics + flyPad` (line 410). It is a string contract in
     `avionics_provider.py:208` and `build_development_aircraft.py:1038`.
7. **`XPluginStop`** (474-493):
   * Call `mfd_shutdown();` right after `XPLMUnregisterDrawCallback` (478), because it was initialised after the EFB.
     It destroys the devices, unregisters the sniffer, command handlers and datarefs, destroys the menu and frees
     navdata.
   * Call `mfd_font_shutdown();` next to `ewd_font_shutdown();` (480).
8. **`XPluginEnable`** (495-499): call `mfd_devices_enable()` (spec 08 §10.2).
9. **`XPluginDisable`** (501-519): call `mfd_devices_disable()` first. It releases keyboard capture and focus, ends a
   held KCCU `click`, and hides the cursor.
10. **`XPluginReceiveMessage`** (521-535): on `XPLM_MSG_PLANE_LOADED` for the user aircraft, call
    `mfd_plane_loaded()`. Both sides return to `fms/data/status`, focus and menus are cleared, and the FMS is reset as
    spec 07 decides. This mirrors the existing ADIRS/guidance reset.

### 1.3 `native/avionics/build.py`

| Where | Change |
|---|---|
| a. Constants (20-56) | Add `MFD_SOURCES` (the 20 `.c` files of §1.1), the test paths, `MFD_TEST_DATA = HERE / "test_data"`, the font paths, `MFD_FONT_GENERATOR` and the pinned SHA-256s. Extend `EXTRA_INPUTS` with the tests and `*[p for p in MFD_TEST_DATA.rglob("*") if p.is_file() and not p.name.startswith("._")]`. **Always filter `._` files.** `HERE.glob("*.h")` already picks up AppleDouble `._*.h` files on exFAT: the current report lists `._a380x_avionics_core.h` and others. |
| b. `header_names` (81-83) | Add only the XPLM headers the adapter really includes. `XPLMDisplay.h`, `XPLMMenus.h`, `XPLMNavigation.h`, `XPLMUtilities.h` and `XPLMPlugin.h` are already listed and hashed into `sdk_inputs`. |
| c. Input existence check (85-88) | Add `*MFD_SOURCES`, the fonts and the generator. Until every WP has landed, either add sources to the list as they arrive or commit stubs. The check fails with "Missing build input". |
| d. Font pins (104-108 pattern) | Two families (spec 12 §2). `MFD_FONT_ECAM` is the already pinned `fonts/FBW-Display-EIS-A380.ttf` (`EWD_FONT_SHA256`); reuse it. `MFD_FONT_SZ` exists upstream **only** as `fbw-a380x/src/fonts/FBW-Display-EIS-A380-SlashedZero.sfd` (FontForge source; there is no TTF in the pinned tree, and no FontForge here). Pin the `.sfd` hash, or the derived glyph source, and record `Font SHA-256:` lines in `a380x_mfd_font.h` as `a380x_ewd_font.h` does. |
| e. Tests (122-200) | Pure core: `run([clang, *warnings, *core_srcs, str(TEST), "-lm", "-o", bin])` for `test_mfd_ui`, `test_mfd_formats`, `test_fms_core` and `test_navdata` (pass `str(MFD_TEST_DATA)` as `argv[1]`). Render: `test_mfd_render` writes SVG previews to `output`, with the font placeholder replaced as at 158-163. Adapter: `run([clang, *warnings, "-Wno-deprecated-declarations", "-DAPL=1", "-DXPLM200=1", "-DXPLM210=1", "-DXPLM300=1", "-DXPLM301=1", "-DXPLM400=1", "-DXPLM410=1", "-I", str(headers), str(MFD_ADAPTER_TEST), *map(str, MFD_SOURCES), "-lm", "-o", bin])`, then run it and run it with `--contracts`. Header smoke test: compile only. Optional but cheap: `x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -c <file> -o /dev/null` per core file, because tests otherwise run on clang only and gcc-only warnings first show at the Windows link. |
| f. Contracts (221-274) | Import `KCCU_COMMANDS`, `KCCU_DATAREFS`, `MFD_COMMANDS`, `MFD_DATAREFS`, `FMS_DATAREFS` and `MFD_DEVICES`. Compare the adapter test's `C` lines with `set(KCCU_COMMANDS) \| set(MFD_COMMANDS)`, its `D` lines with the three dataref tuples, and new `V <deviceID>` lines with `MFD_DEVICES`. On mismatch raise "MFD native registry disagrees with cockpit provider contract". Add its `R` lines to `raw_inputs` (211-212) so `--datarefs` checks them against DataRefs.txt. The DU check (267-274) needs no change: it requires exactly 8 channels, and the MFD reuses `captain_mfd`/`first_officer_mfd`. |
| g. `common` (276-277) | Add `"-DXPLM410=1"`. It is safe: verified on both compilers, and it only exposes extra struct members and APIs. Do **not** add `XPLM420`/`XPLM430`. |
| h. Link lines | Mac (279-283) and Windows (295-297): add `*map(str, MFD_SOURCES)` after `str(ADIRS)`. Export checks (289-292, 302-312) stay as they are. New non-static functions are hidden by `-fvisibility=hidden` and `--exclude-all-symbols`. |
| i. DLL allow-list (315-324) | Unchanged. If a new `api-ms-win-crt-*` import appears (for example from directory enumeration or locale calls in navdata), fix the code, not the list. |
| j. Report (328-426) | Append `"captain_mfd", "first_officer_mfd"` to `display_targets` (377-380), in this order, at the end. Add `mfd_ui_test_result`, `mfd_formats_test_result`, `fms_core_test_result`, `navdata_test_result`, `mfd_render_test_result`, `mfd_adapter_test_result`, `mfd_render_previews` (`[{file, sha256}]`) and `mfd_font` (`{upstream_path, source_revision, sha256, embedded_atlas, generator}`). Add `kccu_commands`, `kccu_datarefs`, `mfd_commands`, `mfd_datarefs`, `fms_datarefs` and `cockpit_devices` (`[{id, name, width: 768, height: 1024}]`). Add `"xplm_api": "XPLM410"`, `"minimum_xplane": "12.1.0"` and the MFD FBW files in `logic_sources`. Add `*MFD_SOURCES` to `sources` (417-418). |
| k. Unchanged | `mock_test_result` must stay `a380x avionics + flyPad core tests passed`; `avionics_provider.py:186, 254` checks it. Keep every test's stdout to **one line**: `run()` stores the stdout of every command in `build-report.json`, already 404 KB, which is packaged. |

### 1.4 `tools/avionics_provider.py`

```python
KCCU_SIDES = ("captain", "first_officer")
KCCU_KEYS = (*"abcdefghijklmnopqrstuvwxyz", *"0123456789", "dot", "plusminus", "ent", "backspace",
             "sp", "slash", "esc", "esc2", "up", "down", "left", "right", "dir", "perf", "init",
             "navaid", "mailbox", "fpln", "dest", "secindex", "surv", "atccom", "nd", "kbd",
             "clrinfo", "rewind", "forward")                     # spec 01 section 12.5, 63 keys
KCCU_ACTIONS = ("wheel_up", "wheel_down", "click", "kbd_switch_toggle", "ccd_switch_toggle")  # [PORT] section 4.3
KCCU_COMMANDS = tuple(f"a380x/xp12/kccu/{s}/{k}" for s in KCCU_SIDES for k in (*KCCU_KEYS, *KCCU_ACTIONS))
KCCU_DATAREFS = tuple(f"a380x/xp12/kccu/{s}/{n}" for s in KCCU_SIDES for n in ("kbd_on", "ccd_on"))
MFD_COMMANDS = tuple(f"a380x/xp12/mfd/{s}/{n}" for s in KCCU_SIDES for n in ("popup", "popout"))  # spec 08 section 10.1
MFD_DATAREFS = (*(f"a380x/xp12/mfd/{s}/{n}" for s in KCCU_SIDES for n in ("active_page", "keyboard_captured")),
                "a380x/xp12/mfd/pc_keyboard_capture")                                          # spec 01 section 12.5, spec 08 section 10.5
FMS_DATAREFS = tuple("a380x/xp12/fms/" + n for n in (...))  # exactly the list of spec 07 section 10.3
MFD_DEVICES = (("a380x.xp12.mfd.captain", "A380X MFD CAPT"), ("a380x.xp12.mfd.first_officer", "A380X MFD F/O"))
```

* Append `"captain_mfd", "first_officer_mfd"` to `DISPLAY_TARGETS` (22-25). The tuple must be byte-identical to
  `build.py`'s list; `avionics_provider.py:185, 233` compares tuples.
* Extend `AVIONICS_COMMANDS` (165-167) with `*KCCU_COMMANDS, *MFD_COMMANDS`.
* Extend `AVIONICS_DATAREFS` (168-170) with `*KCCU_DATAREFS, *MFD_DATAREFS, *FMS_DATAREFS`.
* Manifest (202-216): add `"cockpit_devices": [id for id, _ in MFD_DEVICES]`. Change `draw_target` to
  `"2048x2048 aircraft cockpit panel texture + two 768x1024 plugin cockpit devices"`. Add `"kccu_targets": 136`.
  Keep `physical_touch_targets: 34` (flyPad).
* `validate_avionics_provider` (223-258): reject a manifest whose `cockpit_devices` differs.
* Consequence: the currently packaged manifest **fails validation** until the cockpit-controls refresh has repackaged
  the new provider. Land §1.3-§1.9 and the refresh together.

### 1.5 `tools/avionics_panel_layout.py`

* Add the spec 09b §2.1 constants: `MFD_SCREEN_SURFACES`, `MFD_SCREEN_CORNERS` (BL, TL, TR, BR) and `MFD_SCREEN_SIZE`.
* Add the spec 08 values:

  ```python
  MFD_DEVICE_IDS = ("a380x.xp12.mfd.captain", "a380x.xp12.mfd.first_officer")
  MFD_DEVICE_BUS = 1                 # bitfield: bus 0
  MFD_LIGHTING_CHANNELS = (14, 15)   # instrument_brightness_ratio custom slots
  MFD_AUTO_ADJUST = 1
  ```

* The EWD/PFD panel rectangles are unaffected.

### 1.6 `tools/build_cockpit_controls.py`

See §3. In summary:

* new imports (24, 26-53);
* KCCU/MFD command-prefix checks (after 489);
* MFD touch quads in the draw-disabled block (1046-1082);
* the visible `a380x_mfd_screens.obj` writer (spec 08 §2.5 a);
* report fields (1148-1219).

### 1.7 `config/cockpit_controls.json`

* Add 136 entries (§4).
* Relabel `KNOB_EFIS_CS_MFD` (line 193) to `CAPT MFD DU - brightness and off`.
* Relabel `KNOB_EFIS_FO_MFD` (line 225) to `F/O MFD DU - brightness and off`.
* Commands and animations are unchanged (`a380x/xp12/du/<side>_mfd_*` already exist).

### 1.8 Tests in `tools/`

* **`test_cockpit_controls.py`.** Its exact-count assertions must change in the same patch.
  * Line 128 (`report['triangles'] == 151`): add the MFD touch quads, +4, giving 155.
  * Lines 139-151 (overlay vertex count): add `+ 2  # MFD touch quads`. Append the MFD quads **after** every existing
    overlay item, so no existing offset changes.
  * Line 515 (`len(controls) == 140`): 276 with the plan of §4.
  * New tests:
    * MFD surfaces equal the 09b values (like 35-39);
    * the touch block emits `ATTR_cockpit_device <id> 1 <ch> 1` → `ATTR_manip_device` → `TRIS … 6` →
      `ATTR_manip_none` → `ATTR_no_cockpit` inside `ATTR_draw_disable`;
    * UVs: BL (0,0), TL (0,1), TR (1,1), BR (1,0), with physical up equal to texture up (like 224-226);
    * clockwise winding;
    * every `PUSH_KCCU{L,R}_<KEY>` maps to its own side and the configured `a380x/xp12/kccu/` set equals
      `set(KCCU_COMMANDS)` (like 583-602);
    * a misspelled `a380x/xp12/kccu/…` command is rejected (like 604-608);
    * the visible MFD object exists with the same IDs, bus, channels and corners.
* **`test_fcu_adapter_contract.py`.** Add a KCCU twin. Either regex the adapter's registration table or rely on the
  build.py contract, and assert that config usage equals `KCCU_COMMANDS ∪ {popup}`, with `popout` assignable only.
* **`test_development_build.py`.**
  * `make_native_panel_package` (36-115) must gain the two MFD touch quads, the visible MFD object and its ACF
    attachment once `validate_native_panel` requires them.
  * Add negative tests: a missing F/O device quad, mismatched device IDs, and `ATTR_manip_device` without a preceding
    `ATTR_cockpit_device`.
* **`test_cockpit_draw_order.py`.** Cover the new `a380x_mfd_screens.obj` attachment (spec 08 §10.3 step 4).

### 1.9 `tools/validate_aircraft.py`

* Add `cockpit_device_quads(obj) -> dict[id, list[uv_bounds]]`, analogous to `cockpit_region_quad_bounds` (62-97).
  It tracks `ATTR_cockpit_device`, `ATTR_manip_device` and `ATTR_no_cockpit`.
* In `validate_native_panel` (100-242), for an A388 require:
  * exactly one 6-index draw per ID in `MFD_DEVICE_IDS`, with UV bounds (0, 0, 1, 1), in the visible MFD object;
  * the same in the cockpit-controls object under `ATTR_manip_device`;
  * IDs equal to the provider's `cockpit_devices`;
  * report keys `cockpit_device_quads` and `cockpit_device_touch_quads`.
* Per-object loop (393-412): parse `ATTR_manip_device` (`len(words) >= 3`; cursor, device, tooltip) into
  `manipulator_devices`.
  * Add it to the cockpit-flag rule at 464 (`if (manipulator_commands or manipulator_refs or manipulator_devices)`).
  * Error when a device is not in the provider's `cockpit_devices`.
  * Check that `ATTR_cockpit_device` has 5 words.
* KCCU commands need no new validator code. Non-`sim/` manipulator commands must already be in `packaged_commands`
  (466-471), which §1.4 provides.

### 1.10 `tools/build_development_aircraft.py`

* Attach `interior/cockpit-panels/a380x_mfd_screens.obj` immediately after `screen_powered_off_display_faces.obj`.
  Use `_obj_flags 13` and the same transform: psi 180, z 115.108 (spec 08 §10.3).
  * The file must reach the package in a cockpit-controls refresh. Either place it under
    `objects/interior/cockpit-controls/` next to the controls OBJ, or extend the refresh so it copies the file.
    Otherwise a focused refresh drops or misses it. `existing_attachment_paths()` (755-) treats
    `interior/cockpit-controls/` specially.
* Report `native_3d_panel` (1031-1054): add `"mfd_sides": ["captain", "first_officer"]` and `"cockpit_devices"`.
* README text (1203-1253): "the remaining system screens stay powered-off geometry" becomes "the SD and the other
  system screens …"; mention the MFDs and KCCUs.
* Provider packaging is already invoked on a `cockpit-controls` refresh (1189-1192). No refresh-group logic changes.

### 1.11 Existing native files changed for the FMS (spec 07 §10.2)

| File | Change | Test that must follow |
|---|---|---|
| `a380x_avionics_core.c/.h` | V1 ≤ 0 means never above V1 | `test_avionics_core.c` |
| `a380x_fcu_adapter.inc` | copy FMS managed targets after line 157 | `test_fcu_adapter.c` includes this `.inc` (line 131): stub the FMS outputs there |
| `a380x_pfd_inputs.inc` / `a380x_pfd_data.h` | V1/VR from the FMS, independent of `envelope_valid`; add `v2_knots` | `test_fcu_adapter.c` (includes `pfd_inputs.inc` through `fcu_adapter.inc:18`), `test_pfd_render.c` |
| `a380x_guidance.h` | managed Mach fields | `test_guidance.c`, `test_fcu_adapter.c` |
| `a380x_ewd_data.inc` | FLEX from `a380x/xp12/fms/to_flex_temp_c` (the `EWD_INPUT` list is regex-scanned by `build.py:207`) | `test_ewd_data.c` |
| `a380x_efis_nd.inc` | route from the native plan (`efis_nd_read_route`, 70-85) | `test_efis_nd.c` |

### 1.12 Docs

* `native/avionics/README.md`: MFD paragraph and the X-Plane ≥ 12.1 requirement.
* `docs/COCKPIT_CONTROLS.md` (67-80 says "MFD rendering has not been ported"): add a KCCU table.
* `docs/AVIONICS_DEVELOPMENT.md`: data ownership list.
* Top-level `README.md`: interaction count.
* Optionally a new `docs/MFD_DEVELOPMENT.md`.

### 1.13 Acceptance sequence

```sh
# 1  build into a fresh staging dir on the APFS build volume (cheap blocks)
python3 native/avionics/build.py --sdk /Volumes/MacGames/A380X-XPlane12-build/sdk/XPSDK430/SDK \
  --output-dir /Volumes/MacGames/A380X-XPlane12-build/avionics-mfd-YYYYMMDD-a \
  --datarefs "$HOME/Library/Application Support/Steam/steamapps/common/X-Plane 12/Resources/plugins/DataRefs.txt"
# 2  promote: copy mac.xpl, win.xpl, build-report.json into native/avionics/dist/ (what avionics_provider.FILES needs)
# 3  QUIT X-Plane (section 5.3), then the focused refresh (section 5.2)
./tools/build_local_mac.sh --refresh-group cockpit-controls
# 4  static validation + tool tests
python3 tools/validate_aircraft.py output/A380X-XP12 --datarefs ".../DataRefs.txt" --commands ".../Commands.txt"
(cd tools && python3 -m unittest test_cockpit_controls test_fcu_adapter_contract test_development_build test_cockpit_draw_order)
# 5  start X-Plane, load the A380, run the spec 08 section 10.7 checks on the real aircraft
```

---

## 2. Conventions

### 2.1 Naming

| Kind | Rule | Examples |
|---|---|---|
| Files | `a380x_<area>[_<part>].{c,h,inc}`; tests `test_<area>.c`; tools `tools/<verb>_<noun>.py` | `a380x_mfd_page_perf.c`, `test_mfd_adapter.c` |
| C types / enums | `A380X…` for core API structs (`A380XFms`), `Mfd…` for MFD types (`MfdKey`, `MfdRect`), enum values in `MFD_…` / `A380X_…` capitals | `MFD_KEY_ENT`, `MFD_ERR_FORMAT_ERROR` |
| Static functions and objects in `.inc` | Prefix with the module: `mfd_`, `kccu_`, `fms_`. **Mandatory.** `a380x_avionics.c` is a unity build that already defines `Color`, `WHITE`, `GREEN`, `CYAN`, `AMBER`, `RED`, `YELLOW`, `GREY`, `BLACK`, `Side`, `Inputs`, `refs`, `published`, `guidance`, `failed()`, `find_required()`, `ARRAY_LEN`, `PANEL_W` and every static of 15 included `.inc` files. | `mfd_screen_draw`, `kccu_command` |
| Commands | `a380x/xp12/<module>/<side>/<action>`, all lower-case; side is `captain` or `first_officer`; act on `xplm_CommandBegin` | `a380x/xp12/kccu/captain/dir` |
| Datarefs | `a380x/xp12/<module>/<side>/<field>`; read-only unless stated | `a380x/xp12/mfd/captain/active_page` |
| Device IDs | spec 08: `a380x.xp12.mfd.<side>` (no spaces, at most 64 characters) | `a380x.xp12.mfd.first_officer` |
| Hover labels | `<SEAT> <UNIT> - <legend or action>`, where SEAT is `CAPT` or `F/O` (the DU knobs use `F/O`), on one line with no tabs or newlines (`build_cockpit_controls.py:407-409`) | `CAPT KCCU - DIR (F-PLN direct to)` |

### 2.2 C style and flags

* Every file starts with the `/* SPDX-License-Identifier: GPL-3.0-or-later` header and an intent comment.
* Headers have `#ifndef A380X_…_H` include guards and `extern "C"` blocks, as `a380x_mfd_formats.h` does.
* Use C11 only, compiled with `-std=c11 -O2 -Wall -Wextra -Werror` on clang (universal, `-mmacosx-version-min=11.0`)
  **and** MinGW gcc 15. Plugin builds add `-Wno-deprecated-declarations -fvisibility=hidden -DXPLM200 -DXPLM210
  -DXPLM300 -DXPLM400 (+ -DXPLM410)`.
* Write `(void)param;` for unused parameters. Define `M_PI` locally if needed (`a380x_avionics.c:48-50`). Size
  `snprintf` targets generously (at least 96-160 bytes, as the existing name buffers are); gcc's
  `-Wformat-truncation` at `-O2` is a `-Werror` hazard.
* The core is XPLM-free and GL-free. It does no heap allocation per frame and uses fixed capacities with a sticky
  `overflowed` flag (spec 12 §1, §4). Only navdata loading may allocate, and it frees everything in shutdown.
* Avoid POSIX-only APIs in the core. A probe showed `strdup` and `strcasecmp` compile on this MinGW, but directory
  enumeration and locale functions pull in new CRT DLLs (§1.3 i). Use ASCII helpers.

### 2.3 XPLM patterns to copy

* **Datarefs.** Call `XPLMFindDataRef(name)` first. If it exists, it is a collision: shut down and `return 0`. Then
  `XPLMRegisterDataAccessor` with the module object as refcon (`a380x_du.inc:62-66`, `a380x_avionics.c:378-402`).
  Unregister every handle in shutdown and set it to NULL.
* **Commands.** Commands outlive the plugin, so use `XPLMFindCommand` before `XPLMCreateCommand`
  (`a380x_adirs_window.inc:345-350`). Register handlers with `before = 1` and a per-entry refcon. Unregister with the
  identical signature. Guard with `if (!plugin_enabled || phase != xplm_CommandBegin) return 1;` (`a380x_du.inc:27-30`).
* **Init and shutdown.** `init` returns 0 and cleans up after itself. `shutdown` is idempotent. `XPluginStart` unwinds
  explicitly in reverse order.
* **Windows and menus.** The fallback window follows `a380x_efb_window.inc:150-194`: `XPLMCreateWindowEx` with
  `decorateAsFloatingWindow` (needs `XPLM301`) and a top-level `XPLMCreateMenu(name, NULL, …)` with "Open / Close".
  Keyboard focus is handled with `XPLMTakeKeyboardFocus` and released in `losing_focus`, on ESC and in shutdown
  (`a380x_adirs_window.inc:19-25, 232-262`).
* **GL in callbacks.**
  * Save and restore `GL_CULL_FACE` (`draw_panel`, lines 330-334, 363).
  * Use `XPLMSetGraphicsState` and `XPLMBindTexture2d`.
  * Device callbacks must `glClear` themselves, because X-Plane does not clear device screens.

### 2.4 Test harness

* **Mock-XPLM tests** define the XPLM functions they need in the test file and `#include` the production `.inc`.
  Patterns:
  * `test_control_lifecycle.c:13-149`: `MockRef`/`MockCommand` arrays. Commands survive handler removal, and
    `XPLMCommandOnce` runs Begin and End.
  * `test_adirs_window.c:56-118`: window, menu and GL stubs with asserts on GL state.
  * `test_ewd_render.c`: captures GL calls and writes SVG.
* **Reload test.** Run init, exercise, shutdown three times. Assert that the command count is stable, that no handler
  is left registered, and that every `a380x/` dataref is unregistered (`test_efis.c:286-301`,
  `test_control_lifecycle.c:test_reload`).
* **Contracts.** `<test> --contracts` prints `C <command>`, `D <a380x dataref>` and `R <sim dataref>` lines
  (`test_efis.c:275-285`, `test_adirs_adapter.c:276-283`). The MFD adapter test adds `V <deviceID>`.
* **`test_mfd_adapter.c`** must mock `XPLMFindSymbol`, `XPLMCreateAvionicsEx`, `XPLMDestroyAvionics`,
  `XPLMIsCursorOverAvionics`, `XPLMAvionicsNeedsDrawing`, popup/focus calls and `XPLMRegisterKeySniffer`. It checks:
  * the struct (`structSize == sizeof`, 768 × 1024, IDs, callbacks non-NULL);
  * the texel → canvas conversion at four corners;
  * that a KCCU command on one side never reaches the other side;
  * that a DU-off side ignores input;
  * keyboard routing;
  * the fallback when a symbol is missing;
  * reload cleanup.
* **Output.** One summary line on stdout; failures are `assert`s.

---

## 3. Extending `build_cockpit_controls.py`

### 3.1 How the writer works today

1. `load_controls()` (349-410) validates each JSON entry. It accepts exactly one of `command`, `dataref`
   (+`axis`/`toggle_values`, `sim/` only), or `command_up`/`command_down` (+optional `command_push`/`command_pull`).
   It also checks cursor names, rest rotations and single-line labels.
2. `build_obj()` checks `sim/` commands against `Commands.txt` and every aircraft-local prefix against its provider
   tuple (462-489). It checks datarefs against `DataRefs.txt` (490-498).
3. For each control it transforms the named node's mesh to source coordinates and appends vertices and indices.
   Winding is forced clockwise against the authored normals (619-632). It records
   `draws.append((source, control, start, count, tris))`. Push/pull knobs become four coplanar zones (636-673).
4. With `--fcu-overlay` (always passed by `build_local_mac.sh`) it appends textured overlay quads through `add_quad`
   (683-709) or explicit corners (`add_efb_surface`, 839-861), recorded in `overlay_draws`. The order is: FCU windows
   → EFIS → clock → PFD → ND → EWD → EFB + 34 touch quads → ND PLAN touch → lamps → ADIRS lamps → selector markers.
5. It emits the OBJ (1018-1146) in this order:
   * header: `I`/`800`/`OBJ`, the flyPad orientation comment, `TEXTURE`/`TEXTURE_LIT fcu_labels`,
     `COCKPIT_REGION 0 0 2048 2048`, `POINT_COUNTS`;
   * all `VT`, then `IDX10`/`IDX`;
   * **`ATTR_draw_disable`**, then every manipulator draw: `[ANIM_begin + ANIM_hide …]` → `ATTR_manip_<kind> …` →
     `TRIS` (or an animation block) → `ATTR_manip_none` → `[ANIM_end]`;
   * **`ATTR_draw_enable`**;
   * screens: `ATTR_cockpit_region 0` + `ATTR_poly_os 2`, all screen `TRIS`, then `ATTR_no_cockpit` +
     `ATTR_poly_os 0`;
   * lamps (`ATTR_poly_os 2`, `ATTR_light_level …`, `ANIM_hide` gates, `ATTR_light_level_reset`, `ATTR_poly_os 0`);
   * ADIRS lamps;
   * EFIS selector markers.

**Attribute reset rules** used by the file, which the MFD block must follow:

* Every `ATTR_manip_*` is closed by `ATTR_manip_none`.
* Every `ATTR_cockpit_region` and `ATTR_cockpit_device` is closed by `ATTR_no_cockpit`, which restores the object
  texture and drops panel-click manipulation.
* `ATTR_poly_os n` is closed by `ATTR_poly_os 0`.
* `ATTR_light_level` is closed by `ATTR_light_level_reset`.
* `ANIM_begin` is closed by `ANIM_end`.
* `ATTR_draw_disable` is closed by `ATTR_draw_enable`.
* Never let a cockpit texture state run into the next block.

**Draw order across objects.**

* Attachment 117 `screen_powered_off_display_faces.obj` (opaque dark faces, including both MFDs; see 09b §4) comes
  before 118 `a380x_cockpit_controls.obj`, which comes before the 119-124 decals.
* `tools/cockpit_draw_order.py` only moves `cockpit-panels/decal_*` after the non-decal panel objects and explicitly
  ignores the controls object. Overlays rely on attachment order plus `ATTR_poly_os 2` and a 2 mm lift along the
  normal.
* Spec 08's new visible `a380x_mfd_screens.obj` goes directly after 117. It is a cockpit-panels non-decal object, so
  `cockpit_decal_order_errors()` accepts it.

### 3.2 Manipulators available, and hover labels

| Config shape | Emitted | Notes |
|---|---|---|
| `command` (+`command_cursor` ∈ hand/button/arrow) | `ATTR_manip_command <cursor> <cmd> <label>` | push button: Begin on press, End on release |
| `command_up` + `command_down` (+`cursor` ∈ rotate_small/medium/large/up_down) | `ATTR_manip_command_knob …` | X-Plane drives it by drag **and mouse wheel** (one command per wheel click); `switch_axis: "up_down"` gives `ATTR_manip_command_switch_up_down` |
| … + `command_push`/`command_pull` | three/four coplanar zones; push and pull become `command` zones labelled `label - PUSH/PULL` | FCU knobs only |
| `dataref` (`sim/` only) + `toggle_values` | `ATTR_manip_toggle <cursor> <v1> <v2> <ref> <label>` | `a380x/` datarefs are rejected (line 390). Use commands for aircraft-local state. |
| `dataref` (`sim/`) + `axis`, `value_min/max` | `ATTR_manip_drag_axis hand dx dy dz v1 v2 <ref> <label>` | throttles |
| `visible_dataref` / `visible_value` | wraps the target in `ANIM_hide` so it is clickable only at that value | ND PLAN chevrons |
| `rest_rotation` | builds the hit mesh in a rest pose | door switch |

* **Not emitted today:** `ATTR_manip_wheel` (a wheel add-on for single-dataref manipulators), `axis_knob`,
  `drag_xy`, `noop`, `command_knob2`, `command_switch_up_down2`, `device`.
* **Hover labels.** The JSON `label` becomes the last field of the manipulator line, which is X-Plane's mouse-hover
  tooltip (`docs/COCKPIT_CONTROLS.md:9-10`).
* **Animated hit meshes.** These come from `control_motions` (center-right gear/autobrake and ADIRS only, 512-522).
  KCCU keys stay static (§4.5).

### 3.3 MFD device quads (spec 08 placement)

Code, appended at the **end** of the `if fcu_overlay:` geometry section after the selector loop (1016), so no
existing overlay offset or test index changes:

```python
from avionics_panel_layout import (MFD_SCREEN_CORNERS, MFD_DEVICE_IDS, MFD_DEVICE_BUS,
                                   MFD_LIGHTING_CHANNELS, MFD_AUTO_ADJUST)
device_touch_draws = []
for side, (corners, device) in enumerate(zip(MFD_SCREEN_CORNERS, MFD_DEVICE_IDS)):
    normal = surface_normal(corners)              # cross(BR-BL, TL-BL): points at the crew
    start, base = len(indices), len(vertices)
    for point, uv in zip(corners, ((0.0, 0.0), (0.0, 1.0), (1.0, 1.0), (1.0, 0.0))):   # BL TL TR BR
        raised = tuple(point[a] + normal[a] * 0.001 for a in range(3))               # spec 08: 1 mm
        vertices.append((*raised, *normal, *uv))
    indices.extend((base, base + 1, base + 2, base, base + 2, base + 3))             # add_efb_surface order
    device_touch_draws.append((side, device, start))
```

* Do **not** call `add_quad()` with its defaults for the MFD. Its face basis is the main-panel 20° tilt; the MFD is
  54° (09b §2.3).
* Use the explicit seat-view corners, as above, or pass `face_normal`/`face_vertical` derived from the 6-tuple with
  `uv = (1, 0, 0, 1)` (09b §3.4).
* Both forms give UV (0,0) at the pilot's bottom-left and clockwise front faces.

Emission, **inside the draw-disabled block**: after the `for … in draws:` loop (1047-1081) and before
`lines.append('ATTR_draw_enable')` (1082):

```python
seat = ('CAPT', 'F/O')
for side, device, start in device_touch_draws:
    lines.extend([
        f'ATTR_cockpit_device\t{device}\t{MFD_DEVICE_BUS}\t{MFD_LIGHTING_CHANNELS[side]}\t{MFD_AUTO_ADJUST}',
        f'ATTR_manip_device\tarrow\t{device}\t{seat[side]} MFD',
        f'TRIS\t{start}\t6',
        'ATTR_manip_none',
        'ATTR_no_cockpit',
    ])
```

Resulting OBJ fragment (CAPT):

```
ATTR_cockpit_device	a380x.xp12.mfd.captain	1	14	1
ATTR_manip_device	arrow	a380x.xp12.mfd.captain	CAPT MFD
TRIS	<start>	6
ATTR_manip_none
ATTR_no_cockpit
```

Rules:

* **Order.** The order within a block is device tag → device manipulator → `TRIS` → `ATTR_manip_none` →
  `ATTR_no_cockpit` (spec 08 §2.5). The `ATTR_manip_device` triangles must have the same shape and the same UVs as
  the device screen, and must carry the device tag. That is why the touch quad reuses the visible corners and UVs.
* **Never hide the touch quads.** No `visible_dataref`/`ANIM_hide`: hidden device manipulators may still eat clicks
  (spec 08 R4). Gate input in the plugin.
* **Visible object.** The visible quads are written to the separate object `a380x_mfd_screens.obj` exactly as in
  spec 08 §2.5 (a):
  * `TEXTURE` is an opaque black 8 × 8 PNG, with `GLOBAL_cockpit_lit`;
  * the 2 mm lift and `ATTR_poly_os 2` sit over the dark face;
  * the same ID, bus, channel and auto values;
  * `ATTR_no_cockpit`, then `ATTR_poly_os 0`.
  * Generate it from the same `MFD_SCREEN_CORNERS`, in this builder (a second `--mfd-screens-output` path) or a
    sibling, so the visible and touch geometry can never drift.
* **No per-widget hit regions.** Unlike the flyPad's 34 fixed touch quads, the MFD needs none. `ATTR_manip_device`
  forwards press, drag, release, wheel and hover to the plugin, and the UI core hit-tests (spec 12 §5).
* **Report additions** (1148-1219): `'mfd_device_touch_quads': 2`, `'mfd_devices': list(MFD_DEVICE_IDS)` and
  `'kccu_targets': <count>`. Optionally add a signature comment next to the flyPad one at line 1022, for example
  `# A380X MFD DEVICES: UV0=BL DEVICE_Y_UP V1`. If you do, the validator checks it.
* **Prefix validation**, next to 486-489:

  ```python
  unregistered_kccu = sorted(c for c in configured_commands
      if c.startswith(('a380x/xp12/kccu/', 'a380x/xp12/mfd/')) and c not in (*KCCU_COMMANDS, *MFD_COMMANDS))
  if unregistered_kccu:
      raise ValueError('Commands absent from native KCCU/MFD provider: ' + ', '.join(unregistered_kccu))
  ```

### 3.4 KCCU click targets

KCCU click targets need **no writer change**. They are ordinary `command` and `command_knob` controls on named nodes
of the main cockpit glTF. Add them to `config/cockpit_controls.json` (§4). The dry run in §4.4 proves the unmodified
`build_obj()` accepts all 136.

---

## 4. KCCU click-target plan

### 4.1 Commands and datarefs

| Name | Type | Behaviour in the adapter |
|---|---|---|
| `a380x/xp12/kccu/<side>/<key>` for the 63 keys of spec 01 §12.5 | command | On Begin: `mfd_key(core, side, MFD_KEY_…, ch)`. Same side only (spec 01 quirk 1). Ignored while that side's DU is not On (quirk 17). |
| `…/<side>/wheel_up`, `…/wheel_down` **[PORT]** | command | `mfd_pointer_wheel(side, ±1)` at the current MFD cursor position. |
| `…/<side>/click` **[PORT]** | command | Begin = primary button down and End = up, at the current cursor position, only while the cursor is shown. Lets a trackball or hardware button work. |
| `…/<side>/kbd_switch_toggle`, `…/ccd_switch_toggle` **[PORT]** | command | Flip `kbd_on` / `ccd_on`. `kbd_on` feeds spec 08 §10.5 mode 2. `ccd_on` is state only, because FBW has no consumer. |
| `a380x/xp12/kccu/<side>/kbd_on`, `ccd_on` | int, writable | spec 01 §12.5. Default 1 (ON). |
| `a380x/xp12/mfd/<side>/popup`, `popout` | command | spec 08 §6, §10.1 |
| `a380x/xp12/mfd/<side>/active_page` | byte[64] | spec 01 §12.5 |

Totals: `KCCU_COMMANDS` = 2 × (63 + 5) = 136, and `MFD_COMMANDS` = 4. The OBJ uses 138 distinct commands
(`popout` is assignable only).

### 4.2 Legend keys: node → command → label

For every `<KEY>` below:

* CAPT: `PUSH_KCCUL_<KEY>` → `a380x/xp12/kccu/captain/<key>` with label `CAPT KCCU - <legend>`;
* F/O: `PUSH_KCCUR_<KEY>` → `a380x/xp12/kccu/first_officer/<key>` with label `F/O KCCU - <legend>`.

All use `"command_cursor": "button"`. `<key>` is the lower-case node suffix.

| `<KEY>` (→ `<key>`) | Legend in label | MFD action (spec 01 §10.4, §12.3) |
|---|---|---|
| `A`…`Z`, `0`…`9` | the character | character input to the focused field |
| `DOT` | `. (decimal point)` | `.` |
| `PLUSMINUS` | `+/- (sign)` | sign toggle |
| `ENT` | `ENT (enter)` | validate field |
| `BACKSPACE` | `BACKSPACE` | backspace |
| `SP` | `SP (space)` | space |
| `SLASH` | `/ (slash)` | `/` |
| `ESC` | `ESC (keyboard escape)` | cancel edit [PORT] |
| `ESC2` | `ESC (cursor-control escape)` | cancel edit [PORT] |
| `UP` `DOWN` `LEFT` `RIGHT` | `cursor UP` … | none in fields (FBW "unsupported") |
| `DIR` | `DIR (F-PLN direct to)` | `fms/active/f-pln-direct-to` |
| `PERF` | `PERF` | `fms/active/perf` |
| `INIT` | `INIT` | `fms/active/init` |
| `NAVAID` | `NAVAID (position navaids)` | `fms/position/navaids` |
| `FPLN` | `F-PLN` | `fms/active/f-pln/top` |
| `DEST` | `DEST` | `fms/active/f-pln/dest` |
| `SECINDEX` | `SEC INDEX` | `fms/sec/index` |
| `SURV` | `SURV` | `surv/controls` |
| `ATCCOM` | `ATC COM` | `atccom/connect` |
| `CLRINFO` | `CLR INFO (clear latest message)` | clear latest FMS message |
| `MAILBOX` | `MAILBOX (no MFD function)` | none [FBW-NA] |
| `ND` | `ND (no MFD function)` | none |
| `KBD` | `KBD (no MFD function)` | none |
| `REWIND` | `REWIND (INOP: cursor to left display)` | none |
| `FORWARD` | `FORWARD (INOP: cursor to right display)` | none |

### 4.3 Non-legend KCCU controls

| Node (both sides) | Config | Label |
|---|---|---|
| `PUSH_KCCU{L,R}_WHEEL` | `"command": null, "command_up": "…/wheel_up", "command_down": "…/wheel_down", "cursor": "rotate_small"` | `CAPT KCCU - scroll wheel` |
| `PUSH_KCCU{L,R}_LBUTTON` | `"command": "…/click", "command_cursor": "button"` | `CAPT KCCU - cursor select (click at MFD cursor)` |
| `PUSH_KCCU{L,R}_BALL` | `"command": "a380x/xp12/mfd/<side>/popup", "command_cursor": "hand"` | `CAPT KCCU trackball - MFD pop-up (point at the MFD to move the cursor)` |
| `PUSH_KCCU{L,R}_KBD_ON_OFF` | `"command": "…/kbd_switch_toggle", "command_cursor": "button"` | `CAPT KCCU KBD switch - ON / OFF` |
| `PUSH_KCCU{L,R}_CCD_ON_OFF` | `"command": "…/ccd_switch_toggle", "command_cursor": "button"` | `CAPT KCCU CCD switch - ON / OFF` |
| `PUSH_KCCU{L,R}_BLANK1…6` | **not mapped** (no FBW event) | – |
| `PUSH_KCCU{L,R}_*_SEQ1`, `KCCU`, `KCCU_COVER_BALL`, `PUSH_KCCUL_WHEEL.001` | **never mapped** (legend decals, body, empty helper) | – |

Notes:

* **BALL.** A 2-D trackball cannot be expressed with the current manipulator set. Pointing at the MFD with the mouse
  is the trackball. A click on the ball opens that side's popup; drop this if the team prefers the ball inert.
* **Later option.** `ATTR_manip_drag_xy` onto two writable `ball_x`/`ball_y` datarefs would need a new writer kind
  and relaxing the `sim/`-only dataref rule (line 390).
* **`ATTR_manip_noop` is not available** in the writer. Leaving the blanks unmapped is the existing convention for
  inert controls.
* **Lever animation (optional, later).** The glTF clips `PUSH_KCCU{L,R}_{KBD,CCD}_ON_OFF_ANIM` rotate the levers
  between about −23° and +15°. Map `kbd_on`/`ccd_on` in `config/animation_datarefs.json` and re-export the pedestal
  knobs with `tools/patch_efis_animations.py`-style patching. That requires the datarefs in `AVIONICS_DATAREFS`
  (§1.4), because the validator checks animation datarefs against the provider. Key-press travel (1.15 mm) is not
  worth 126 datarefs.

### 4.4 Config JSON, generator and dry run

Example entries (same schema as the existing file):

```json
{"node": "PUSH_KCCUL_DIR", "command": "a380x/xp12/kccu/captain/dir", "command_cursor": "button",
 "label": "CAPT KCCU - DIR (F-PLN direct to)"},
{"node": "PUSH_KCCUR_ENT", "command": "a380x/xp12/kccu/first_officer/ent", "command_cursor": "button",
 "label": "F/O KCCU - ENT (enter)"},
{"node": "PUSH_KCCUL_WHEEL", "command": null, "command_up": "a380x/xp12/kccu/captain/wheel_up",
 "command_down": "a380x/xp12/kccu/captain/wheel_down", "cursor": "rotate_small",
 "label": "CAPT KCCU - scroll wheel"},
{"node": "PUSH_KCCUR_BALL", "command": "a380x/xp12/mfd/first_officer/popup", "command_cursor": "hand",
 "label": "F/O KCCU trackball - MFD pop-up (point at the MFD to move the cursor)"}
```

Generator and dry run, from the throwaway `/private/tmp/a380x-geometry/kccu_plan.py`. It imports the unmodified
`build_cockpit_controls` read-only.

```python
import json, sys
sys.dont_write_bytecode = True
sys.path.insert(0, "/Volumes/Savs' drive/A380X-XPlane12/xplane-port/tools")
import build_cockpit_controls as bcc
from pathlib import Path
SOURCE = Path(".../Part_Interior_Cockpit/model/A380_COCKPIT_LOD00.gltf")   # full path in spec 09b
LEGEND = {**{str(d): str(d) for d in range(10)}, **{chr(c): chr(c) for c in range(65, 91)},
    'DOT': '. (decimal point)', 'PLUSMINUS': '+/- (sign)', 'ENT': 'ENT (enter)', 'BACKSPACE': 'BACKSPACE',
    'SP': 'SP (space)', 'SLASH': '/ (slash)', 'ESC': 'ESC (keyboard escape)', 'ESC2': 'ESC (cursor-control escape)',
    'UP': 'cursor UP', 'DOWN': 'cursor DOWN', 'LEFT': 'cursor LEFT', 'RIGHT': 'cursor RIGHT',
    'DIR': 'DIR (F-PLN direct to)', 'PERF': 'PERF', 'INIT': 'INIT', 'NAVAID': 'NAVAID (position navaids)',
    'MAILBOX': 'MAILBOX (no MFD function)', 'FPLN': 'F-PLN', 'DEST': 'DEST', 'SECINDEX': 'SEC INDEX',
    'SURV': 'SURV', 'ATCCOM': 'ATC COM', 'ND': 'ND (no MFD function)', 'KBD': 'KBD (no MFD function)',
    'CLRINFO': 'CLR INFO (clear latest message)', 'REWIND': 'REWIND (INOP: cursor to left display)',
    'FORWARD': 'FORWARD (INOP: cursor to right display)'}
KEYS = [*map(str, range(10)), *[chr(c) for c in range(65, 91)], 'DOT', 'PLUSMINUS', 'ENT', 'BACKSPACE', 'SP',
        'SLASH', 'ESC', 'ESC2', 'UP', 'DOWN', 'LEFT', 'RIGHT', 'DIR', 'PERF', 'INIT', 'NAVAID', 'MAILBOX',
        'FPLN', 'DEST', 'SECINDEX', 'SURV', 'ATCCOM', 'ND', 'KBD', 'CLRINFO', 'REWIND', 'FORWARD']
rows = []
for letter, side, seat in (('L', 'captain', 'CAPT'), ('R', 'first_officer', 'F/O')):
    base = f'a380x/xp12/kccu/{side}/'
    rows += [{'node': f'PUSH_KCCU{letter}_{k}', 'command': base + k.lower(), 'command_cursor': 'button',
              'label': f'{seat} KCCU - {LEGEND[k]}'} for k in KEYS]
    rows += [
        {'node': f'PUSH_KCCU{letter}_WHEEL', 'command': None, 'command_up': base + 'wheel_up',
         'command_down': base + 'wheel_down', 'cursor': 'rotate_small', 'label': f'{seat} KCCU - scroll wheel'},
        {'node': f'PUSH_KCCU{letter}_LBUTTON', 'command': base + 'click', 'command_cursor': 'button',
         'label': f'{seat} KCCU - cursor select (click at MFD cursor)'},
        {'node': f'PUSH_KCCU{letter}_BALL', 'command': f'a380x/xp12/mfd/{side}/popup', 'command_cursor': 'hand',
         'label': f'{seat} KCCU trackball - MFD pop-up (point at the MFD to move the cursor)'},
        {'node': f'PUSH_KCCU{letter}_KBD_ON_OFF', 'command': base + 'kbd_switch_toggle',
         'command_cursor': 'button', 'label': f'{seat} KCCU KBD switch - ON / OFF'},
        {'node': f'PUSH_KCCU{letter}_CCD_ON_OFF', 'command': base + 'ccd_switch_toggle',
         'command_cursor': 'button', 'label': f'{seat} KCCU CCD switch - ON / OFF'}]
Path('kccu_controls_proposed.json').write_text(json.dumps(rows, indent=2) + '\n')
controls = bcc.load_controls(Path('kccu_controls_proposed.json'))
encoded, report = bcc.build_obj(SOURCE, controls, known_commands=set())
print(len(controls), report['triangles'], report['vertices'], len(encoded.encode()))
```

Result:

* 136 controls (134 `command` + 2 `command_knob`), 138 distinct commands;
* **34,062 triangles, 27,527 vertices, 2,727,258 bytes** of OBJ;
* every node found, every mesh indexed triangles, every label valid.

The controls OBJ grows from 10.49 MB to about 13.2 MB, and the controls report by about 60 KB.

### 4.5 Hit-mesh choice

* The keys' own meshes (180-252 tris each) are the visible key caps. Clicks therefore follow the caps, the convention
  of this builder.
* If click-test cost or file size becomes a concern, a later writer option could emit only each key's top face
  (2 tris).
* KCCU keys are static in `panel_a380_cockpit_pedestal_knobs01_4k.obj`, so no `control_motions` are needed.

### 4.6 Pull-out keyboards (`PUSH_KBD_CPT_*`, `PUSH_KBD_FO_*`): recommendation

**Do not map them to the MFD scratch entry in this feature.** Reasons:

1. **They belong to the OIT/OIS** (onboard information terminal), not the FMS. The KCCU already has a full A-Z/0-9
   keyboard on the same side, and PC typing is covered by spec 08 §10.5 capture.
2. **Geometry.**
   * The 88 captain key nodes are children of the animated sliding-table chain
     `Cube.034 → Circle.017 → Cube.041 → KEYBOARD_LH` (09b §6).
   * Their glTF rest pose is **stowed** under the main panel. Static click meshes would sit where the keyboard is
     hidden, and the stowed/deployed state is not simulated.
   * Making them clickable would need animated hit meshes driven by a table dataref.
3. **Asymmetry.** There are **no `PUSH_KBD_FO_*` nodes**. The F/O keyboard (`KEYBOARD_LH.001`) has no individually
   named keys, so a symmetric mapping is impossible without new geometry.
4. **Cost.** They would add 17,789 tris and 19,662 vertices (about 2 MB) for the captain side alone.

Revisit with the OIT port. If it is ever wanted earlier, map only the alphanumerics, `ENTER`, `BACKSPACE`, `ESC`,
`SPACE`, `SLASH`, `FULLSTOP` and `MINUS`/`PLUS` to the same `a380x/xp12/kccu/captain/<key>` commands. Gate them with
`visible_dataref` on a table-deployed dataref, and leave the F/O side unmapped.

---

## 5. Risks

### 5.1 Disk space

* **`Savs' drive`.** It is exFAT with 128 KiB clusters and only 1.7 GB free. Every new file costs at least 256 KiB
  with its `._` companion.
  * Roughly 30 new sources and tests cost about 8 MB before content.
  * `native/avionics` already occupies 50 MB, of which `dist/` is 18 MB.
  * Keep staging builds **off** this volume (§1.13 builds on MacGames). Promote only `mac.xpl`, `win.xpl` and
    `build-report.json`.
  * Do not commit large SVG preview sets into `dist/`: one MFD page per SVG times about 40 pages is about 10 MB of
    clusters. Keep previews in the staging directory and record their hashes in the report.
  * The controls OBJ grows by about 2.7 MB in the package. The refresh also rewrites the 2.27 MB ACF, the manifest and
    the README.
  * `build/` (3.7 GB of historical builds and backups) is the obvious reclaim target, but only with the user's
    consent.
  * exFAT is case-insensitive, so no fixture names may differ only in case.
* **`MacGames`.** It has about 208-218 MB free.
  * A staging build is about 4-5 MB. The controls OBJ (10.5 → about 13.2 MB) and the reports are rewritten in place.
  * Never start a Blender re-export as part of this feature. The dark MFD faces already exist, so no re-export is
    needed.
  * Clean old `avionics-*`/`iscs-*` staging folders only with consent.

### 5.2 Focused rebuild path

`./tools/build_local_mac.sh --refresh-group cockpit-controls` runs three steps:

1. `build_fcu_panel.py` into `$build_dir/xplane-fcu-panel`.
2. `build_cockpit_controls.py`. It writes `$build_dir/xplane-obj/cockpit-controls/a380x_cockpit_controls.obj` and
   validates `sim/` names against `Resources/plugins/Commands.txt` and `DataRefs.txt` (both present).
3. `build_development_aircraft.py … --refresh-group cockpit-controls`.

Preconditions:

* the package `output/A380X-XP12/A380X-XP12.acf` exists;
* the MacGames volume and `/Volumes/MacGames/A380X-XPlane12-git/open-source-747-reference` (template, license,
  airfoils) are mounted, because the script always passes them;
* the cockpit glTF is on the SD volume;
* `native/avionics/dist/{mac.xpl,win.xpl,build-report.json}` are promoted, because packaging validates the report
  (`avionics_provider.py:181-187`).

What the refresh does:

* It refreshes only the controls OBJ (rmtree + copy), `cockpit_3d/` and the avionics provider (plugin binaries,
  sounds, report, manifest). It also rewrites the ACF, `build-manifest.json`, `README.txt` and the Lua script.
* Unselected groups are preserved from the existing ACF.
* The new `a380x_mfd_screens.obj` must be part of what this refresh copies and attaches (§1.10). Otherwise the device
  shows only through the touch quad: the touch quad is draw-disabled and the visible screen stays dark.

Afterwards always run `validate_aircraft.py` and the tool unit tests.

### 5.3 Replacing a loaded plugin binary

* `package_avionics_provider()` writes each file to a temp name, then `os.replace` (`avionics_provider.py:192-200`).
  That protects an APFS inode, but the package lives on **exFAT**, where open-file unlink/rename semantics are weaker.
  Windows refuses outright.
* macOS also may not unmap a C++/libc++-linked bundle on aircraft reload, so the old code keeps running.
* A crash that skipped `XPluginStop` leaves device IDs registered, and `XPLMCreateAvionicsEx` then fails. This is
  handled by the F0 fallback, but it is confusing.
* **Rule: quit X-Plane before the refresh, then start it fresh.** The install is a symlink, so any write is live
  immediately.

### 5.4 X-Plane version and API

* **Version.** The device API is XPLM410 (X-Plane ≥ 12.1.0), and `ATTR_manip_device` is new in 12.1.0. The installed
  sim is 12.4.3-r2.
* **Linking.** Spec 08 §8.3 resolves the avionics functions through `XPLMFindSymbol`, so the binary has no hard
  imports and still loads on 12.0 with the F0 fallback. If someone links them directly instead, the whole avionics
  plugin (PFD/ND/EWD/EFB as well) fails to load on X-Plane < 12.1.
* **Defines.** Keep `-DXPLM410` and do not define `XPLM420`/`XPLM430`.
* **Rosetta.** Rosetta use means the x86_64 slice is exercised. Both slices are already enforced (`build.py:286-288`).
* **Re-check on 12.4.4.** The 12.4.4 SDK 440 beta changes panel graphics. Re-check the GL device callbacks then
  (spec 08 R11).

### 5.5 Other integration risks

* **Exact-count tests and tuple contracts.** Triangles 151, 140 controls, the overlay vertex sum, `DISPLAY_TARGETS`
  and `AVIONICS_COMMANDS` must all change in one atomic patch with the provider repackage, or validation stays red.
* **Unity-build name collisions** (§2.1): prefix every static.
* **Missing inputs.** `build.py` fails early on any listed but missing source (§1.3 c). Integrate work packages as
  they land.
* **SlashedZero font.** It exists only as `.sfd` upstream (§1.3 d), and no FontForge is installed. WP-I needs an
  SFD-outline reader or another documented derivation, and `build.py` must pin it.
* **Key sniffer.** Consume keys only while capture is active, and never with Ctrl, Alt or Cmd held (spec 08 §5.2,
  R5). Otherwise typing "P" pauses the sim.
* **Behaviour to verify in the sim** (spec 08 §10.7):
  * device binding;
  * corner mapping, where mirrored U means wrong corners;
  * the brightness law with channels 14/15;
  * hover on 12.4.3;
  * the OS cursor hidden over the screen;
  * that tooltips on the touch quad are not intrusive;
  * frame cost with `drawOnDemand = 1`.
