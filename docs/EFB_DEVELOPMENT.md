# Native A380 ISCS window and cockpit tablets

The aircraft-local `A380XAvionics` plugin draws its existing control panel
into the cockpit panel texture and an A380 ISCS floating window. Open the window
from the top-level **ISCS > Open / Close** menu or bind
`a380x/xp12/efb/window_toggle`. The window and both cockpit tablets share the
same native aircraft data and commands. The ISCS window follows the eight-tab
reference layout: Startup/Situations, A/C + Sim Config, Loading/Perfo, Ground
Services, Fault Scenarios, Sounds/Addons, Joystick/Actions, and
Settings/Accounts. Its large cyan ISCS mark replaces the reference brand and
the `1:1` label. The cockpit tablets keep their ten-page flyPad layout and
touch geometry. Each tablet has ten top-tab targets, six
contextual action targets, and one power target, for 34 physical touch regions
in total.

The adapter preserves the pinned FlyByWire page model, A380 mass/fuel/cabin
limits, ten checklist groups, tablet battery timing, and top-of-descent
formula. MSFS SimVars and events are replaced at the boundary by guarded
X-Plane datarefs and commands. It can manage payload and nine-tank fuel,
ground services, all seventeen modeled passenger/cockpit doors, pushback,
radios/transponder, selected
failures, checklists, and parked-aircraft presets.
The tablet Checks page displays each named item and response. Items can be
checked directly on either tablet; the `CHECK ITEM` button
advances to the next incomplete item. With Auto Check enabled, eligible items
follow native seatbelt, parking-brake, beacon, gear, speedbrake, engine, and
battery states. Actions without a direct native observation remain manual.
The Settings unit switch converts displayed altitude, distance, speed, and
mass; the underlying simulator values and 1000 ft descent-target step stay
unchanged.

The window uses a two-column layout with the screenshot's eight top tabs,
deep teal background, cyan controls, and thin dividers. It uses original
text and vector UI primitives; no ToLiss artwork or code is included. The
loading page shows the A380-842 reference limits (510,000 kg MTOW, 395,000 kg
MLW, 373,000 kg MZFW, 323,546 kg maximum fuel). The Ground page controls
the modeled ten main-deck, six upper-deck, and one cockpit door independently.
The six sound sliders use writable X-Plane engine, UI, interior, environment,
ground, and warning-volume datarefs.
Forward, aft and bulk cargo doors now have independent ISCS Ground Services buttons. ALL 20 DOORS includes them; see [CARGO_DOORS.md](CARGO_DOORS.md).
The FBW-derived flight and simulator functions remain underneath.
Both cockpit tablet quads now use upright U/V mapping from their physically
measured corners, and touch regions follow that same projection. Their OBJ8
cursor is a tap-style `button`; commands execute on the press phase only.

This is a native C implementation. The historical FlyWithLua ImGui prototype
remains in source for reference and tests but is not packaged; loading it
alongside the plugin would duplicate the command namespace.

## Pages and commands

The cockpit tablet tabs are Aircraft, Loading, Ground, Perfo, Nav, Radios,
Faults, Checks, States, and Settings. Existing page command names remain compatible:

- `a380x/xp12/efb/page/<page>` for each page name.
- `a380x/xp12/efb/action_1` through `action_6` for contextual buttons.
- `a380x/xp12/efb/power`, `toggle`, and `next_page`.
- `a380x/xp12/efb/window_toggle`, `window_open`, and `window_close`.

The plugin publishes `a380x/xp12/efb/native_available` as a read-only runtime
availability flag. Commands that change loading are permitted only on the
ground with all engines stopped; presets are ground-only.

The Ground page's `ALL DOORS` action covers all 17 modeled openings: ten
main-deck passenger doors, six upper-deck passenger doors, and the cockpit
door. Both the native flyPad and FlyWithLua fallback expose the batch action.

## Honest limitations

The MSFS React/Coherent runtime itself does not execute in X-Plane. Navigraph,
ChartFox, Bing, SimBrief, GSX, Hoppie/online ATC, and account authentication are
not emulated. Where the translated page needs a simulator service, the adapter
uses an explicit X-Plane boundary call and labels unavailable external work.
This does not make X-Plane's stock avionics the PFD/ND implementation: those
four flight displays remain fully owned by the translated custom renderer.

The current flyPad state is session-local. There is no persistent settings or
authentication store, no complete SimBrief route client, and no translated
FBW backend service bus yet.

The ISCS popup is a visual and native-control adaptation of the eight supplied
screens. Startup presets, a session-local payload/fuel snapshot, engine starter
commands, payload, nine-tank fuel, available ground services,
passenger/cockpit doors, and selected X-Plane failures call native commands or
datarefs. CG position, deicing, ADIRU alignment, random-fault
scheduling, external account logins, and a full takeoff calculator lack a
backing model and are labeled as unavailable. Simulator-configuration switches
and startup preferences currently hold session-local UI state; they
do not claim to control unavailable aircraft systems. The X-Plane SDK places
the top-level ISCS menu in the menu bar; the simulator determines its exact
position relative to Developer.

## Verification

`native/avionics/build.py` runs pure C checks for battery charge/discharge,
top-of-descent distance, average descent groundspeed, FMA priorities, and FWS
flight phases before building the plugin. Python panel and cockpit-control
tests verify the 1024x1024 atlas, both tablet quads, and all 34 touch regions.
Geometry tests check both tablet projections and all 34 touch target centers.
The native build verifies command and display compilation, checklist auto
conditions, row touches, unit conversion, and command reuse after an aircraft
reload. A fresh X-Plane runtime check is still needed for final visual
alignment and click feel.

Runtime evidence is recorded per packaged binary in
`build/reports/efb-native-runtime-smoke.json`. Loading and command registration
do not by themselves prove visual alignment or hand-click accuracy; those need
an in-cockpit visual pass.
