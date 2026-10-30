# ACF files for landing-gear work

This folder keeps the small, editable X-Plane aircraft definitions separate from the generated multi-gigabyte aircraft package.

## Which file should I edit?

For a change to the complete aircraft, edit **[`../output/A380X-XP12/A380X-XP12.acf`](../output/A380X-XP12/A380X-XP12.acf)**. `A380X-XP12-current.acf` is a standalone review snapshot; only the output aircraft's ACF is loaded by the simulator.

The current ACF is based on the user's `A380X-XP12 (3).acf`, installed under the
standard aircraft filename. One cockpit attachment correction restores
`_obja/119/_v10_att_psi_ref` from 0 to 180 degrees, aligning the existing controls
and display geometry with the visible cockpit screens. The standalone review
file matches this corrected active ACF byte-for-byte. All supplied flight-model
properties are unchanged; `A380xR.afl` and `A380xT.afl` remain identical to the
uploads in the aircraft's `Airfoils` directory.

`tools/install_flaps_lever.py` (see [FLAPS_LEVER.md](../docs/FLAPS_LEVER.md)) changes only the slat/flap
schedule of both files identically: the seven detents become FBW's SFCC configurations (flaps 0/0/8/17/17/26/33,
slats 0/20/20/20/23/23/23), FBW travel times, no X-Plane auto-slats or flap extension protection, and
Vfe 263/182 kt; every changed key is listed in that document.

The preceding versions and original installation hashes are recorded in
`output/A380X-XP12/.acf-backups/supplied-acf-airfoils-20261020-84400826/`.
The pre-alignment ACFs and simulator log are backed up in
`output/A380X-XP12/.acf-backups/before-cockpit-alignment-20261020-HmUPs7/`.
The aircraft validator now rejects mismatched cockpit screen/control attachment
transforms. Static validation and regression tests pass; actual display and mouse
behavior still require a fresh simulator load. No OBJ or plugin files were
changed. Neither Peter Hager donor adaptation was reapplied; the
[adaptation notes](../docs/A388_FLIGHTMODEL_ADAPTATION.md) remain historical.

The two other files are comparison baselines:

| File | Purpose |
| --- | --- |
| `acf-before-nosegear-contact-fix-20261005.acf` | State immediately before the nose-wheel contact/attachment correction. Useful for reviewing that focused change. |
| `acf-before-ground-calibration.acf` | Earlier baseline from before the ground-stance calibration work. |

## Known landing-gear problem

The nose-wheel floating/contact defect was improved, but the overall ground stance is not accepted. The last documented parked test still exceeded the one-degree pitch/roll targets, and taxi, takeoff, landing, strut travel, bay clearance, and retraction geometry remain unvalidated.

Read [`../docs/GROUND_TEST_NOTES.md`](../docs/GROUND_TEST_NOTES.md) before changing the ACF. It contains the measured contact geometry, known cached-reload trap, required telemetry, and pass/fail criteria.

After an edit, run:

```sh
python3 tools/ground_gear_diagnostics.py \
  acf/A380X-XP12-current.acf \
  --probe /path/to/fresh-ground-probe.jsonl
```

Then load the edited aircraft in a **fresh X-Plane flight**, confirm that the expected ACF path and gear coordinates were loaded, disable random failures, and collect at least five unpaused seconds after settling. An offline diagnostic alone is not proof that the landing gear is fixed.
