A380X Trim Panel - X-Plane 12.1+ / macOS Intel and Apple Silicon

Scope
-----
Independent aircraft-local plugin: plugins/A380XTrim/mac_x64/A380XTrim.xpl.
The restored A380XAvionics plugin, renderer assets and panel atlas are unchanged.
No ACF, airfoils, cabin, flaps, exterior, global plugins or Windows binary changes.
This module is simulation software, not real-world flight-control software.

Controls
--------
PITCH TRIM: left click/hold the forward (NOSE DN) half to trim down; the aft
(NOSE UP) half to trim up. Release returns the rocker to its centre position.
RUDDER TRIM: left click/hold the left or right half for NOSE L / NOSE R.
Release spring-centres the knob without undoing the actual rudder trim.
RESET: one click starts a 1 degree/second return to exactly neutral. Manual
rudder trim cancels reset. The reset button depresses and springs back.
The amber pedestal display shows L/R and actual trim degrees, with one decimal.
It is a separate cockpit device, not a web renderer or shared avionics atlas.

Authority and limitations
-------------------------
Manual pitch trim is available on ground or in direct law. Normal/alternate
flight inhibits manual pitch trim. Pitch trim follows the installed ACF's
stabilizer ranges and servo speed; those flight-model parameters are not edited.
The installed ACF gives rudder trim 0.3 of its 30-degree rudder travel (9 degrees).
The degree display uses those actual ACF values, not an assumed 20-degree scale.
The module additionally caps trim at 20 degrees when used with a larger range.

IMPORTANT: the restored port does NOT contain the complete A380 PRIM/SEC
flight-control computers. The added automatic pitch-trim controller is a
conservative, simplified elevator-unloading servo, NOT a complete Airbus law.
It uses actual elevator feedback with a 1-degree deadband and a 1-second filter,
and trims at no more than 0.15 degrees/second clean, 0.25 with flaps extended.
It is enabled only while the existing a380x/xp12/fcu/servo_active reports a real
closed-loop guidance servo controlling pitch, above 100 KIAS / 50 ft AGL,
below/equal 30 degrees bank and
between 0.5 and 1.25 g. The 0.02 feedback gain is an X-Plane adaptation requiring
flight-test tuning, not an Airbus parameter. Normal/alternate here select TRIM
AUTHORITY ONLY; this module does not simulate other control-law protections.
Normal/alternate HAND-FLOWN autotrim remains unimplemented and holds trim.
Simply integrating pilot elevator input without the missing closed-loop PRIM
would cause runaway trim, so this unsafe shortcut is intentionally not enabled.

It yields to native autopilot servos and never writes to elevators, control
surface overrides or joystick inputs. If another plugin owns surface override,
it does not move pitch trim. This also means it will not drive THS under the
optional experimental full-surface FBW override: that controller must integrate
THS itself before its override can safely be used with this panel.

Both actuators use the installed ACF's native electrical bus 0 (18 V minimum).
Trim failures, electric pitch-trim-off, pause, replay and long frame/load stalls
inhibit actuation. Disable/unload releases held inputs without zeroing aircraft
trim. Native keyboard/joystick pitch/rudder-trim commands share the same gated
path and cannot double-integrate. Command handlers pass through on other planes.

Aircraft interface (newly implemented here)
------------------------------------------
Commands: a380x/xp12/trim/pitch_up, pitch_down, rudder_left, rudder_right,
rudder_reset. Each fully-qualified name starts with a380x/xp12/trim/.
Read-only datarefs under that prefix: pitch_switch, rudder_knob, reset_pressed,
pitch_ratio, rudder_ratio, rudder_degrees, powered, device_active, device_bound,
draw_count, manual_pitch_available, auto_pitch_active, rudder_resetting,
computer_active.
Writable control_law: 0 normal, 1 alternate, 2 direct; invalid values ignored.
The optional enabled FBW script's existing fbw/normal_law takes precedence.
There is currently no complete automatic PRIM failure/law-reversion system.
Device: a380x.xp12.trim.rudder, 384 x 136 pixels, lighting channel 23.

Authored animations
-------------------
Uses the pinned FBW SWITCH_TRIM_PITCH, KNOB_RUDDERTRIM (including both children)
and PUSH_TRIM_RESET clips. Pitch preserves pedestal.xml's INVERT_ANIM=1.
Vertex/index/UV/material payload of the visible pedestal OBJ stays unchanged;
only trim draw commands receive animations. Existing click/screen objects get
append-only hit boxes and the original rounded eight-corner RTPI screen face.

Build and installation
----------------------
From the xplane-port project, with a fresh staging directory each time:
python3 native/trim/build.py --sdk build/sdk/SDK --output build/trim-native-NEW
PYTHONPATH=tools python3 -m unittest tools/test_trim_cockpit_panel.py
python3 tools/trim_cockpit_panel.py --aircraft <installed-aircraft-folder>
  --source <pinned-A380_COCKPIT_LOD00.gltf> --stage build/trim-geometry-NEW
python3 tools/install_trim_panel.py --aircraft <installed-aircraft-folder>
  --native-stage build/trim-native-NEW --geometry-stage build/trim-geometry-NEW
The installer requires X-Plane closed, validates all inputs, makes a full APFS
recovery copy OUTSIDE Aircraft/, and verifies all unrelated files byte-identical.

In-simulator checks (NOT YET PERFORMED)
--------------------------------------
1. Load the local Aircraft/A380X-XP12/A380X-XP12.acf with electrical power on.
2. At rest, hold NOSE UP then NOSE DN. Verify rocker direction, spring return,
   native elevator_trim change and actual stabilizer movement outside the plane.
3. Hold rudder L then R. Confirm correct knob rotation, L/R degree indication,
   retained trim after release and actual rudder response with airflow.
4. Press/release RESET once. Verify smooth return, exact L 0.0 and button travel.
   During another reset, apply L/R to verify manual input cancels reset.
5. Repeat through assigned native keyboard/joystick trim commands; check rates
   do not change when multiple sources or command-continue events occur.
6. Test clicks outside the panel. Verify existing MFD, PFD, FCU and other controls.
7. Pause/replay, power loss, native trim actuator failures, trim-servo-off and
   reload: verify no hidden movement, stale held input or new Log.txt errors.
8. In controlled flight above 100 KIAS / 50 ft, normal/alternate authority:
   manual pitch must be inhibited. With the existing custom guidance servo
   active, verify auto trim moves in the unloading
   direction and stabilizes rather than running away. Repeat in both directions,
   with flaps and without, different masses/CG, bank and g freeze, AP on/off.
9. Set trim/control_law=2 for an isolated DIRECT-authority test: manual pitch
   available, automatic pitch inhibited. Restore 0 after the test.
10. Load a different aircraft and verify its native trim is unaffected.
Until the flight checks pass, treat automatic trim as development software.

Undo
----
Quit X-Plane. The installer prints a recovery directory under the simulator's
Output/aircraft-restore-backups/. Its aircraft/ holds the complete pre-trim
aircraft; trim-install-report.json lists every changed/new file. Preserve the
current aircraft and restore that saved aircraft folder as a complete set.

References
----------
https://developer.x-plane.com/article/stabilizer-trim-and-servo/
https://developer.x-plane.com/article/types-of-flight-control-trim/
https://developer.x-plane.com/sdk/XPLMCreateAvionicsEx/
https://docs.flybywiresim.com/aircraft/a380x/a380x-api/a380x-flight-deck-api/
Pinned FBW 2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2:
model/A380PitchNormalLaw.cpp (trim freeze and rate schedules),
model/A380SecComputer.cpp (rudder manual/reset arbitration),
model/behaviour/pedestal/pedestal.xml (pitch switch inversion),
A380_COCKPIT_LOD00.gltf (all mechanical motion and screen geometry).
