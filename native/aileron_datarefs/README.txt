A380X early control-surface visual datarefs — staged native provider

Purpose
  This aircraft plugin registers exactly 22 writable scalar Float refs in
  XPluginStart, before X-Plane resolves datarefs in attached aircraft OBJs:

  a380x/xp12/visual/aileron_l_inboard_ratio
  a380x/xp12/visual/aileron_l_midboard_ratio
  a380x/xp12/visual/aileron_l_outboard_ratio
  a380x/xp12/visual/aileron_r_inboard_ratio
  a380x/xp12/visual/aileron_r_midboard_ratio
  a380x/xp12/visual/aileron_r_outboard_ratio

  In addition to the six legacy names above, a380x/xp12/hydraulics/ owns:
    aileron_l_inboard_ratio, aileron_l_midboard_ratio, aileron_l_outboard_ratio
    aileron_r_inboard_ratio, aileron_r_midboard_ratio, aileron_r_outboard_ratio
    elevator_ratio, rudder_ratio
    spoiler_l_inboard_deg, spoiler_r_inboard_deg
    spoiler_l_inner_deg, spoiler_r_inner_deg
    spoiler_l_middle_deg, spoiler_r_middle_deg
    spoiler_l_outer_deg, spoiler_r_outer_deg

  It stores 22 cached floats, initially zero. Fourteen ratio values clamp to
  [-1, 1]; eight spoiler values clamp to [0, 65] degrees. Nonfinite writes are
  ignored, retaining the last valid value.
  It reads no native datarefs, writes no flight-control datarefs, registers no
  flight loop, and does not compute surface motion. Separate visual drivers
  must find and reuse the complete native-owned set after startup.

  All 22 names are checked for collisions before any registration. Any
  existing ref (even a compatible Float) causes startup to fail without
  taking ownership. Partial registration failure unregisters only handles
  created by this plugin. Stop unregisters those handles; Disable resets the
  cache but leaves registration owned until Stop. Do not load two copies.

Build and offline tests on macOS
  Download the official SDK to external storage. Pass its SDK directory,
  not the parent of that directory:

  python3 native/aileron_datarefs/build.py --sdk /path/to/SDK

  Requires clang, lipo, codesign and MinGW-w64 x86_64 gcc/objdump. Override
  --mingw if installed elsewhere. Only a new output directory is accepted;
  use --output-dir /path/to/new-stage for a subsequent build.

  Default staged outputs:
    native/aileron_datarefs/dist/mac.xpl  (arm64 + x86_64, minimum macOS 11)
    native/aileron_datarefs/dist/win.xpl  (Windows 10+ x86_64, system UCRT)
    native/aileron_datarefs/dist/build-report.json

  The actual C source is linked to SDK mocks using official SDK headers.
  Ten test cases cover early registration before Enable, exact Float and
  writable contract, independent caches, clamp/nonfinite behavior, all 22
  collision positions, all 22 partial-failure positions, stop, restart,
  duplicate Start and Disable. No simulator is launched. The build verifies
  architectures and the exact five plugin exports, records hashes, and checks
  Windows DLL dependencies. Only the host mock test binary is executed.

Intended aircraft packaging (NOT performed by this build script)
  <aircraft>/plugins/A380XAileronDatarefs/mac_x64/A380XAileronDatarefs.xpl
  <aircraft>/plugins/A380XAileronDatarefs/win_x64/A380XAileronDatarefs.xpl

  mac_x64 is X-Plane's SDK 3.0 directory naming convention; the mac.xpl staged
  here includes both Apple Silicon and Intel slices. The Mac binary is
  ad-hoc signed, not notarized. The Windows binary is not Authenticode signed.
  Do not copy the SDK framework/import library into the aircraft.

Integration caution
  A pre-existing Lua-created reference remains a collision even if the Lua
  script stops. Close X-Plane before replacing the provider, then ensure the
  Lua driver binds/reuses these refs rather than registering them. Native
  startup success must be confirmed in X-Plane's log. Compilation and mocks
  do not prove runtime loading, motion correctness or flightworthiness.

Primary references
  https://developer.x-plane.com/article/deferredinitialization/
  https://developer.x-plane.com/sdk/XPLMRegisterDataAccessor/
  https://developer.x-plane.com/sdk/XPLMUnregisterDataAccessor/
  https://developer.x-plane.com/article/building-and-installing-plugins/
  https://developer.x-plane.com/sdk/plugin-sdk-downloads/
  https://learn.microsoft.com/en-us/cpp/windows/universal-crt-deployment
