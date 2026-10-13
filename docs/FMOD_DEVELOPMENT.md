# FMOD development

## Current state

The X-Plane attachment layer, a fresh Laminar X-Plane 12 starter project, and a
local FMOD-ready copy of the FBW Trent engine media are ready. No FMOD bank is
shipped because this machine does not currently have FMOD Studio installed.

The three upstream Wwise `.PCK` packages were parsed without modifying them.
The engine selection follows references from Wwise events that contain the FBW
N1, N2, derived-N1, or reverser RTPC IDs. That identified 22 parameter-driven
event graphs and 90 unique media files in `FBW_A320_NEO_3.PC.PCK`. The selected
files are decoded once, not rendered through the Wwise mix:

| Staged engine source | Count |
|---|---:|
| WAV files | 90 |
| Files retaining loop markers | 84 |
| Stereo / mono | 88 / 2 |
| 44.1 kHz / 48 kHz | 70 / 20 |
| PCM payload | 108,292,528 bytes |

The loop-tagged files are in `fmod/project/Assets/FBW_Trent900`. Their original
media IDs, Wwise event clusters, RTPCs, loop points, source/output hashes, and
provenance are in `fmod/fbw-engine-source-manifest.json`; the compact audition
index is `fmod/fbw-engine-samples.csv`.

The optimized bank does not retain a recoverable mapping from its numeric event
IDs to the 24 `WwiseEvent` aliases in FBW's `sound.xml`. The 22 RTPC-driven
graphs are positively identified as engine material, but should not be labelled
as a particular `Engine_1_Front`, `Engine_2_Rear`, and so on merely from their
numeric IDs. The two `ac1`/`ac2` N2-threshold cockpit cues have no engine RTPC
and are deliberately excluded until their bank objects can be resolved.

The initial integration deliberately stays small and testable:

| FMOD event | X-Plane instances | Trigger | Intended bus |
|---|---:|---|---|
| `event:/A380X/Engines/Trent972` | 4 | actual N1 | `Exterior Processed/Aircraft`, or `Exterior Unprocessed` if the event contains separate interior recordings |
| `event:/A380X/Airframe/Wind` | 1 | indicated airspeed | `Interior` |
| `event:/A380X/HighLift/Flaps` | 1 | actual flap motion | `Exterior Processed/Aircraft` |
| `event:/A380X/Gear/Tire` | 5 | actual tire rotation | `Exterior Processed/Aircraft` |

This is not yet a complete airliner sound set. APU, packs, hydraulics, cockpit
switches, warnings, touchdown, precipitation, doors, brakes, reversers, and
interior/exterior mixing remain later slices.

## Authoring requirements

1. Install FMOD Studio 2.02.x for X-Plane 12.
2. Open `fmod/project/A380X-XP12.fspro`. It was freshly generated for this
   aircraft by Laminar and has a unique master-bank GUID plus the required
   standard bus GUIDs; do not reuse it for another aircraft or recreate those
   buses.
3. Keep the starter project's 5.1 desktop output and standard bus hierarchy.
4. Add the four event paths listed above to the Master Bank.
5. Import the WAVs from `project/Assets/FBW_Trent900`, audition them, and use
   `fbw-engine-source-manifest.json` to keep layers from the same Wwise event
   graph together while rebuilding the N1/N2 crossfades.
6. Preserve the sample loop regions. The WAV `smpl` chunks and manifest hold
   the original loop points; they do not reproduce Wwise gain, pitch, filter,
   state, or randomization curves automatically.
7. Keep the staged audio local until the sound-specific redistribution rights
   have been confirmed. Presence in the FBW checkout and successful decoding
   are not, by themselves, permission to publish the recordings or a bank that
   embeds them.

The engine and tire events are reused across their X-Plane attachments with
`PARAM_DREF_IDX`. In FMOD Studio 2.02, use these wildcard parameter names:

```text
sim.flightmodel2.engines.N1_percent[#]
sim.flightmodel2.engines.N2_percent[#]
sim.flightmodel2.engines.engine_rotation_speed_rad_sec[#]
sim.flightmodel2.gear.tire_rotation_speed_rad_sec[#]
```

The X-Plane `.snd` file continues to use slash-form datarefs. The generator
expands each attachment's exact index for start and end conditions.

Prefer actual-result datarefs over cockpit controls. For example, the flap
event is driven by the rate of change of
`sim/flightmodel2/controls/flap1_deploy_ratio`, so a commanded but jammed flap
does not make a false movement sound.

## Rebuild the FBW engine source selection

The extraction tools preserve numeric Wwise IDs so the selection is
reproducible. After extracting the source PCK and its internal BNK media and
generating TXTP graphs with `wwiser`, stage only the engine-linked samples:

```sh
python3 tools/stage_fbw_engine_audio.py \
  --txtp-dir "/path/to/FBW_A320_NEO_3.PC/txtp" \
  --inventory "/path/to/media-inventory.json" \
  --media-root "/path/to/fbw-wwise" \
  --sound-xml "/path/to/FBW/common/sound/sound.xml" \
  --decode-dir fmod/project/Assets/FBW_Trent900 \
  --output fmod/fbw-engine-source-manifest.json \
  --csv fmod/fbw-engine-samples.csv
```

The staging tool decodes each WEM once with `vgmstream-cli -i -L`, retaining
the source loop in the WAV `smpl` chunk. `inventory_wwise_media.py` uses
metadata-only mode and does not create decoded sidecars.

## Generate and validate the attachment file

From `xplane-port`:

```sh
python3 tools/build_fmod_snd.py \
  config/fmod_event_map.json \
  fmod/A380X-XP12.snd \
  --datarefs "/path/to/X-Plane 12/Resources/plugins/DataRefs.txt"

python3 tools/test_fmod.py
```

The current event map produces eleven `BEGIN_SOUND_ATTACHMENT` blocks. Edit the
JSON map rather than hand-editing the generated SND file.

## Build and package the bank

Build the FMOD project for Desktop. The exported directory must contain at
least:

```text
Master Bank.bank
GUIDs.txt
```

Other emitted `.bank` files, including the strings bank, are copied as well.
Package them with the aircraft only through the guarded build arguments:

```sh
./tools/build_local_mac.sh \
  --fmod-snd "$PWD/fmod/A380X-XP12.snd" \
  --fmod-bank-dir "/path/to/FMOD/project/Build/Desktop"
```

The build refuses a missing master bank, missing GUID file, wrong SND filename,
or an attached event absent from `GUIDs.txt`. `validate_aircraft.py` applies the
same completeness checks to any package that contains an `fmod` directory.

## Native acceptance for the first bank

- Start and stop each engine independently; confirm sound follows the correct
  engine and fully stops below the end threshold.
- Check all four engines from exterior views and from both flight-deck seats.
- Move the flaps, then stop them mid-travel; audio must follow actual movement.
- Taxi slowly and confirm each tire event stops after wheel rotation stops.
- Check that the A380 used as AI traffic plays exterior engine/tire sound but
  does not leak cockpit-only wind audio.
- Review `Log.txt` for missing bank, GUID, event, dataref, and FMOD errors.
- Recheck CPU/voice use before adding always-running snapshots or cockpit loops.

Only after this slice passes should the bank add interior sound spaces,
insulation snapshots, touchdown transients, systems, and cockpit controls.
