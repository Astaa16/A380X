# A380X EWD FADEC numeric subset

Imported from FlyByWire `aircraft` revision
`2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2` under GPL-3.0.
`manifest.json` records source paths, original hashes and local changes.

`Table1502_A380X.hpp` and `Fadec.cpp` are unchanged. `ThrustLimits_A380X.hpp`
only drops an unused logging include. `EngineRatios.hpp` drops the MSFS header
and replaces its `FLOAT64` alias with `double`. `Fadec.h` exposes the two
unchanged numerical helpers without the simulator module interface.

The C ABI in `../../a380x_ewd_fadec.{h,cpp}` calls these exact tables and
equations, and adapts the static parts of
`EngineControl_A380X::generateIdleParameters` and `updateThrustLimits`:

- Ambient-temperature correction and Table1502 idle N1.
- TO/GA/CLB/MCT altitude and temperature tables, bleed derates, and the
  original Mach and altitude transitions between limits.
- The EWD's extra 0.6 N1 maximum compensation when either pack is active.
- Reverse N1 at the pinned `FlyByWireInterface` default of 0.813 times TOGA.

Upstream treats packs as a boolean indicating either pack is on, even though
the port API accepts a count. The function rejects nonfinite inputs and the
idle table's invalid CN3 domain before its upstream array search.

This function returns the non-FLEX limits. It has no FLEX-temperature latch,
FLEX-to-CLB transition state, engine actuation, fuel/EGT generation, or complete
FADEC network. At high altitude a latched FLEX-to-CLB transition can affect
the upstream CLB value used in the TOGA transition; that state is outside this
stateless EWD boundary. Numeric correspondence is with the pinned simulation
source and is not a claim of certified aircraft engine performance.

Compile `../../a380x_ewd_fadec.cpp` and `Fadec.cpp` as C++17. The exposed header
can be included by C or C++ callers.
