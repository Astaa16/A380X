# Cockpit feedback pass (November 2026)

Motion, click targets and lamps for FBW-authored controls that were static in the port. One contract
(`tools/cockpit_feedback_contract.py`) drives the geometry patcher (`tools/cockpit_feedback.py`) and the
native module (`native/avionics/a380x_cockpit_feedback.inc`, table generated into
`a380x_cockpit_feedback_table.h`, checked by `native/avionics/build.py`).

| Group | Controls | Motion | Click | Lamps / function |
| --- | --- | --- | --- | --- |
| SWITCHING | ATT HDG, AIR DATA, FMS | FBW clip, 3 detents | rotary knob, `a380x/xp12/switching/<knob>_left/_right` | ATT HDG / AIR DATA switch the captain or F/O IR / ADR to unit 3 (native ADIRS); FMS drives `a380x/xp12/fms/switching_knob` |
| ECAM control panel | 28 keys (ENG keeps its own prototype) | 0.6 mm press on the existing `ecam/sd/*` commands | yes | green bar on the displayed SD page key; CLR lit while an ECAM warning is waiting |
| SURV panel | XPDR/TCAS SYS 1/2, WXR/TAWS SYS 1/2, ABV, BLW, TA ONLY, G/S MODE | FBW press clip | yes, `a380x/xp12/surv/<key>` | write the shared SURV datarefs (also used by the MFD SURV page); green bars follow them, TA ONLY "ON", G/S MODE "OFF" |
| KCCU | 63 key caps per side | FBW clip, follows the existing `kccu/<side>/<key>` commands | existing targets | key travel only |
| ENG MASTER 1-4 | number decal, FIRE / FAULT faces | follow the lever (`mixture_ratio[n]`) | existing toggle | the decals no longer float at the OFF pose |
| Glareshield side | CONSOLE & FLOOR LT (OFF / DIM / BRT), OIT SIDE (FLT OPS / NSS AVNCS) | FBW clip | up/down switch | state datarefs only (`lighting/<side>_console_floor`, `oit/<side>_side`) |

Datarefs: `<key>_pressed` / `press_ratio` 0..1 for key travel; selectors and switches are writable ints.
Press handlers are registered in front of the owning module's handler and return 1, so the original
action still runs. Lamps are dark without DC power.

Geometry notes

* Only TRIS ranges that were not animated before are wrapped; VT / IDX / UV payloads are unchanged.
* The exported legend decals sit 2 mm out along their face normal (earlier legend repair); legends are
  matched at either position.
* Lamp faces in the emissive decal (SURV bars) get their own `ATTR_light_level` and are hidden while dark.
  Lamp faces in decals without a LIT texture (ECAM bars, TA ONLY, G/S MODE) get lit quads in the controls
  object (fcu_labels atlas cells), drawn alpha-tested so they write depth.
* The KCCU KBD / CCD switches and the WXR VD AZIM knobs are not animated yet: their FBW position mapping
  needs the behaviour XML to confirm.

Run (X-Plane closed): `python3 tools/cockpit_feedback.py --aircraft "<X-Plane>/Aircraft/A380X-XP12"`.
Re-running is a no-op. Not yet tested in the simulator.
