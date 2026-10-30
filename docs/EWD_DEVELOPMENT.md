# Engine Warning Display port

The A380X X-Plane port renders the center EWD on the existing authored
`SCREEN_DU_EWD` face. Its logical canvas is 768 x 1024, mapped to panel pixels
`(1024, 0)` through `(1792, 1024)` in the 2048 x 2048 cockpit atlas. The display
keeps the source aspect ratio and full logical resolution. The mesh and atlas
mapping are defined by `tools/avionics_panel_layout.py`; the native draw entry
is `draw_ewd(&ewd, 1024, 0)`.

## Source and appearance

The reference is FlyByWire `aircraft` revision
`2baa2b35eadaf4c78e172ce41bbe6b40b4aeafb2`, particularly
`fbw-a380x/src/systems/instruments/src/EWD/` and the shared EIS gauge/styles.
This is a port of the open-source simulation display; it does not establish
complete Airbus system or certified engine-performance fidelity.

`native/avionics/a380x_ewd_render.inc` uses the source engine-column positions,
THR/N1/EGT arcs and readout placement, 379 px upper-area separator, two-column
memo spacing, and semantic colors. The background is black; normal values
are green `#00ff00`, units cyan `#00ffff`, labels white `#ffffff`, unavailable
values amber `#e68000`, and over-limit red `#ff0000`. The original
`FBW-Display-EIS-A380.ttf` is embedded as an alpha atlas, so runtime text does
not depend on the host's installed fonts. `tools/build_ewd_font.py` generates
the atlas and records the source font hash.

The upper area has THR %, N1 % and EGT degrees Celsius for four engines.
Inactive engines show the reference amber `XX` presentation. Active engines
show moving thrust/EGT needles and numeric values; N1 is normally numeric,
with the analog N1 presentation used when THR normalization is unavailable.
Available-thrust shading, AVAIL and the two inner-engine REV indications
follow available inputs. Source-style cyan throttle donuts and green
autothrust demand arcs require real N1 target producers.

N2/N3, fuel flow, oil and vibration are separate SD ENGINE-page parameters in
the reference. That page remains unported and those parameters are not added
to the EWD layout.

## Warning display (lower EWD) renderer

`native/avionics/a380x_ewd_wd.inc` draws the lower area from an `EwdWdView` (`a380x_ewd_wd.h`)
that the native FWS fills each frame; when no view is supplied the legacy memo/warning lists
are drawn. It translates the pinned FBW components:

* `FormattedFwcText.tsx`: escape codes `\x1b<1m`..`<7m` (yellow, red, green, amber, cyan,
  magenta, white), `\x1b4m` underline, `\x1b'm` frame, `\x1bm` reset, `\r` = new 30 px line,
  24 px text with 1.75 px letter spacing and decorations on FBW's 16 px letter grid. FBW quirks
  are kept: a decoration is emitted only when its segment is closed by an escape or `\r`, and
  only the Amber/Green/White/Red line classes have a stroke.
* `WdLimitations` + `WdMemos`: LIMITATIONS / ALL PHASES / APPR & LDG headings, two columns of
  `\r`-joined messages, light-grey divider, then the memo columns.
* `WdAbstractChecklistComponent` / `EclLine`: 18 lines of 34 px, 24 px checkbox column (■/□ drawn
  as squares; the embedded EIS font has no box glyphs), 26 px text truncated to 39 characters,
  3 px cyan selection box open on the right, the white end-marker bracket that frames a
  procedure (top on its first line, bottom on its last), separation lines, green tables, LAND
  ASAP/ANSA right aligned, centred/underlined sub-headlines, green overflow arrows; line colours
  follow the `style.scss` cascade (two-class rules in order, then `.ChecklistItem.Checked` /
  `.AbnormalItem.Checked`, then the `!important` grey of inactive and completed lines).
* STS area: FAILURE PENDING, STS (or STS & DEFRD PROC) and ADV boxes.

The ECL soft keys are off by default in FBW (`CONFIG_A380X_SHOW_ECL_SOFTKEYS = 0`) and are not
drawn. Upper area additions: A.FLOOR (A/THR mode A.FLOOR or TOGA LK), THR LK (FMA A3 thrust lock;
also hides the rating like `n1LimitHidden`), and the FWS engine attention getters, which replace
the native start/fire approximation when the FWS supplies them.

## Thrust rating (N1 limit) producer

While no `a380x/xp12/engine/thrust_limit_type` producer is registered, `a380x_ewd_data.inc`
selects the rating like FBW's `A380FadecComputer.cpp` does for engine 1 (the EWD shows
`fadecOutputs[0]`): lever 1 on the A380 TLA scale (idle 0, CL 25, FLX/MCT 35, TOGA 45 degrees,
reverse negative; inside the aircraft-local detent calibration a lever is exactly in its
detent), FLEX when the FMS FLEX temperature is above TAT
(`sim/cockpit2/temperature/outside_air_LE_temp_degc`), FBW's FLEX latch (set on the ground in the
FLX/MCT detent, cleared at CL or TOGA), and FBW's table: on the ground with the engine running
TOGA or FLX (MREV in reverse); otherwise TOGA above 35 degrees, FLX/MCT above 25, CLB from 0, MREV
below. Limits come from the vendored FADEC equations (CLB, MCT, TOGA, reverse). Port assumption:
the FLX N1 is the TOGA limit computed at the FLEX temperature, never above TOGA, because the
vendored equations have no FLEX table.

## Engine inputs and THR normalization

`a380x_ewd_data.inc` reads typed raw X-Plane inputs. N1 comes from
`sim/cockpit2/engine/indicators/N1_percent`; EGT comes from
`sim/flightmodel2/engines/EGT_deg_cel`, whose unit is always Celsius.
Combustion, starter, engine-mode selector, FADEC switch and reverser deployment
drive the visible states. A fire annunciator alone does not blank a display;
the optional true fire-pushbutton input can do so.

`a380x_ewd_fadec.cpp` imports the pinned A380X Table1502 idle model,
TO/GA/CLB/MCT thrust-limit tables, bleed corrections, and Mach/altitude limit
transitions. Source hashes, alterations to remove MSFS-only dependencies,
and GPL-3.0 licensing are recorded in
`native/avionics/vendor/ewd_fadec/manifest.json` and its README/LICENSE.
Inputs are ambient temperature, ambient pressure, pressure altitude, Mach,
pack state, engine anti-ice and wing anti-ice. The display maximum includes
the source EWD's 0.6 N1 compensation when either pack is on.

Forward THR uses the source N1-to-THR equation, including a 4.2% idle offset
for a running engine. Reverse uses the source default N1 limit of TOGA x
0.813. The native boundary uses a positive reverse N1 magnitude to resolve
the sign mismatch between the pinned source producer and display. No thrust
in newtons, throttle-travel ratio, or invented static N1 limit is used to
estimate indicated THR.

The numerical bridge is stateless and returns non-FLEX limits. It does not
reproduce the upstream FLEX-temperature latch or FLEX-to-CLB transition;
the latter can affect the high-altitude TOGA blend. It does not command the
engines or supply the complete FADEC network. Missing, nonfinite or invalid
inputs produce unavailable indications rather than a live zero.

## Supported lower-area indications

The adapter supplies live `ENG n FIRE`, gear-not-down/gear-lock alerts,
EGT-over-limit cautions and the simulator's N1/N2-over-limit annunciation.
Red warnings precede amber cautions, and alerts replace the memo area while
present. Caution thresholds use the loaded aircraft's limits or annunciator
inputs. These are a bounded native alert presentation, not complete sensed
ECAM procedures.

Supported memos are APU AVAIL/APU BLEED, SEAT BELTS, PARK BRK ON,
ELEC EXT PWR, ENG A-ICE and WING A-ICE. The adapter uses source wording and
the applicable implemented phase/color rules. APU availability currently
uses native APU-running and N1 >= 95%; external-power status uses the native
power-on annunciator. T.O/LDG checklist replacement and its memo suppression
are not yet implemented. When no supported alert or memo is active, the
lower area is empty.

## Optional aircraft-local producers

The EWD consumes these datarefs if another aircraft-local component registers
them. It does not register, write, or claim that these producers exist.
Missing registrations are checked again approximately once per second.
Array indices 0..3 correspond to engines 1..4.

All names in the following table have prefix `a380x/xp12/engine/`:

| Suffix | XPLM type | Contract |
|---|---|---|
| `thrust_limit_idle_n1` | Float | Idle N1 %, finite 0..120 |
| `thrust_limit_max_n1` | Float | Display maximum N1 %, including pack compensation; greater than idle |
| `thrust_limit_reverse_n1` | Float | Positive reverse N1 limit %, greater than idle |
| `commanded_n1` | FloatArray | Four actual commanded N1 targets, finite 0..150 |
| `throttle_position_n1` | FloatArray | Four N1 targets corresponding to lever position, finite 0..150 |
| `thrust_limit_n1` | Float | Selected rating N1 %, finite 0..150 |
| `thrust_limit_type` | Int | 1 CLB, 2 MCT, 3 FLX, 4 TOGA, 5 MREV; 0 means no rating |
| `a380x/xp12/fms/to_flex_temp_c` (native FMS, replaces `engine/flex_temperature_c`) | Float | FLEX °C with the FBW encoding (0 none, 0.1 = FLEX 0 °C); displayed only with a valid FLX rating and source temperature condition |
| `autothrust_status` | Int | 0 inactive; 1 or 2 active under the source display convention |
| `fire_pushbutton` | IntArray | Four true fire-pushbutton states, each 0 or 1 |

The numerical fallback is used only when neither external idle nor maximum
reference is bound. A partial or invalid external reference pair is not
silently replaced. A valid external reverse limit overrides the numerical
reverse limit. Rating mode/percentage, FLEX, throttle donuts, autothrust
transient cues and IDLE require their corresponding valid producer inputs;
the current guidance lever command does not establish an N1 rating. The
rating field therefore remains `XX` when its producer is absent.

Gear alerts consume the existing integer boolean contracts
`a380x/xp12/center_right/gear_not_down_warning`,
`gear_not_locked_down_warning` and `gear_not_locked_up_warning` under the
same `a380x/xp12/center_right/` prefix.

## Remaining differences

Full FWS fault arbitration, suppression, ECAM limitations/status, normal and
abnormal electronic checklists, deferred procedures, STS/ADV/failure-pending
boxes, A.FLOOR/THR LK, and the source IDLE pulse are not implemented. Engine
attention brackets use the supported native start/fire/EGT states rather
than full FWS attention-getter outputs. The port displays actual EGT
excursions; it does not copy the pinned source's provisional 850/900-degree
readout trimming and inconsistent throttle-mode thresholds. A successful
render test does not certify these missing systems.

## Verification

The native build verifies the imported FADEC/font hashes, checks discovered
raw EWD paths against the supplied installed DataRefs catalog, and runs the
production adapter and renderer against mocks. It produces
`ewd-inactive.svg`, `ewd-live.svg` and `ewd-warning.svg` with the actual font
embedded. The FADEC bridge was also compared against independently compiled
upstream equations over 31,104 input cases, agreeing within 1e-12; invalid
input/domain rejection was checked separately.

In-simulator acceptance remains required: check the full screen at cockpit
zoom levels, start/shut down each engine, vary power, deploy reverse on
engines 2/3, operate the supported memo switches, and exercise supported
failure inputs. Confirm independent needles, missing-data indications,
warning/memo replacement and panel power-off behavior. Check optional target
cues only with a valid producer connected.
