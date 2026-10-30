# Interactive MFD browser review

Open **http://127.0.0.1:8383/**. This preview is intentionally isolated from
X-Plane. It does not use the simulator API, installed plugin/shared memory,
aircraft preferences or live flight plan. Nothing from this preview is installed.

Both screens use the actual native page/input/FMS core and the xp-kit drawing
backend. The simulator-independent plan hooks are extracted, with exact markers,
from `a380x_fms_adapter.inc` during the preview build; no aircraft source is edited.
Clicks and keyboard events go to the real widget controller, not image hotspots
that merely swap screenshots. Navigation data is read from local X-Plane files.
Aircraft position/weight/power are explicit example inputs; unknown simulated
systems show dashes/NOT AVAIL rather than pretending to be connected.

Use the **FMS 1/FMS 2** arrow for the system menu and **ACTIVE**, **POSITION**,
**SEC INDEX**, **DATA** for their page menus. Click a cyan field, type with the
keyboard and press Enter; Esc cancels. Paste is supported while editing. Both
seats share entered FMS values but navigate independently. The outside quick keys
match the physical KCCU page keys. Display-off switches and reset affect only
this in-memory preview. Example data is labelled; it is not a flight briefing.

```sh
python3 tools/build_xp_kit_mfd_preview.py
python3 tools/serve_xp_kit_mfd_trial.py --port 8383
python3 tools/test_xp_kit_mfd_preview.py
```

The server binds only to loopback, validates bounded input batches, requires the
matching Origin for writes and serves only the explicit preview/view assets.
Its test state is lost when it exits. Existing ND/OANS review on port 8382 is
left running and unchanged. Wait for user review before changing the simulator.
