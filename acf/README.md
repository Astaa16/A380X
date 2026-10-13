# ACF files for landing-gear work

This folder keeps the small, editable X-Plane aircraft definitions separate from the generated multi-gigabyte aircraft package.

## Which file should I edit?

Edit **`A380X-XP12-current.acf`**. It is copied from `output/A380X-XP12/A380X-XP12.acf` and is the current development definition.

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
