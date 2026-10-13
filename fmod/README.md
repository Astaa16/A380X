# A380X FMOD integration source

This directory contains the X-Plane integration and local authoring source for
the future A380X FMOD bank. It does **not** contain a compiled bank, so it is
intentionally not copied into the development aircraft by default.

- `project/A380X-XP12.fspro` is the fresh, uniquely identified Laminar X-Plane
  12 starter project. Its standard bus GUIDs must not be changed.
- `A380X-XP12.snd` is generated from `../config/fmod_event_map.json`.
- `fbw-sound-inventory.json` inventories upstream Wwise event metadata only. It
  does not extract audio from the FBW `.PCK` files.
- `fbw-engine-source-manifest.json` records the RTPC-based selection of 22 FBW
  engine event graphs and 90 decoded media sources, including hashes and loop
  points. `fbw-engine-samples.csv` is the compact audition index.
- `project/Assets/FBW_Trent900/*.wav` contains the 90 loop-tagged local
  authoring sources. The numeric filename is the original Wwise media ID.
- The initial bank contract has four FMOD events and expands them to eleven
  spatial X-Plane attachments: four engines, wind, flaps, and five tires.

The Wwise event aliases were stripped from the optimized bank, so the numeric
event clusters are not presented as exact front/rear/start labels. Rebuild and
audition the N1/N2 layers in FMOD Studio using the manifest as provenance. Do
not distribute the staged WAVs or a bank containing them until sound-specific
rights have been confirmed.

Regenerate the SND file with the installed X-Plane dataref catalog:

```sh
python3 tools/build_fmod_snd.py \
  config/fmod_event_map.json \
  fmod/A380X-XP12.snd \
  --datarefs "/path/to/X-Plane 12/Resources/plugins/DataRefs.txt"
```

Open the project with FMOD Studio 2.02.x after installing Studio. See
`../docs/FMOD_DEVELOPMENT.md` for the authoring and packaging workflow.
