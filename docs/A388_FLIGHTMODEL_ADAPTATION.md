# Historical PA A388-842 flight-model adaptation (reverted)

Status: reverted on 20 October 2026 at the user's request. The original
pre-Peter-Hager ACF was restored (SHA256
`1354ee82103ac0b0d1540655c7dbe7334147daa3ebfeb57b1704ace788579e26`).
The user subsequently supplied `A380X-XP12 (3).acf` and `A380xR.afl`/`A380xT.afl`;
the airfoils remain installed byte-for-byte. The supplied ACF (original SHA256
`9f4fb7d1498fdf60d575a80aae4451946b6649a19f3ed5d81645f68baed3162f`)
has one subsequent cockpit alignment correction: `_obja/119/_v10_att_psi_ref`
is restored from 0 to 180 degrees to match the existing cockpit screen objects.
All supplied flight-model properties remain unchanged, without reapplying either
donor variant or the donor trim repair. Corrected active ACF SHA256 is
`3fb21e94b0196b0e3ced463301d7013c6ba2d7b651fc0df8dca4859dc99cffed`.
Cockpit, cabin, flap OBJ files and installed plugins were not modified.
Reverted donor additions/current files are retained for recovery in
`build/reverted-pa-a388-20261020-s7tadft6`; this document describes the withdrawn
experiment, not the installed flight model. Do not reapply it without a new request.

The previously supplied donor was `/Volumes/Untitled/X-Plane 12 Aircraft/PA_A388/A380-842/A388.acf`
(SHA256 `63a2dfeb99a18039945dff20e940c3ceaf54142904b708f340cfc01b927d8d3b`).
Its author field identifies Peter Hager, Copyright 2009–2024, and its description
identifies the A380-842 / Rolls-Royce RB211 Trent 972B-84 variant. It supersedes
the earlier A380-861/GP7270 candidate. This is a local
development adaptation of its **native ACF data**, not a port of its proprietary
FBW, engine-management, fuel-management or avionics plugins. No donor plugin,
cockpit object, panel instrument, license file or activation data is copied.
Do not redistribute the donor-derived ACF/airfoils without appropriate rights.

## What changes

- The invisible native aerodynamic bodies, 35 enabled wing/tail/pylon sections,
  engine definitions, five gear legs and their spring/damping parameters.
- Aerodynamic control-surface definitions and high-lift schedules, native engine
  thrust/spool/SFC parameters, mass, CG, inertia, speed/load limits and ground
  equilibrium cache. The four-engine and nose/wing/body gear-index contracts
  remain unchanged.
- Seven referenced airfoils are copied into the aircraft's `Airfoils` directory.
  Existing files with the same names are backed up before replacement.
- The donor model is translated **+7.663999795 ft vertically**, matching the
  existing body-main contact datum. Its longitudinal nose datum is retained.
  The same translation is applied to all physics positions, CG and fuel arms.
  Aircraft-coordinate wing geometry caches are translated too; local body
  cross-sections are not translated a second time.
- Only the moving gear attachments' Y offsets change, aligning visible neutral
  tire bottoms to the new uncompressed contact envelope. Cockpit/viewpoint,
  fixed nose bay, non-gear objects and all OBJ geometry remain unchanged. This
  is not a loaded-strut or bay-clearance acceptance test.

The donor ratios sum to 1.001000011; they are normalized to exactly one while
retaining their relative proportions. Donor tank capacity, names and empty/full
arms are retained. Its plugin-managed storage/trim/feeder roles are adapted to
native **normal** tanks so the fuel is not stranded without the donor's transfer
plugin. This is a deliberately simplified native fuel setup, not an A380 FQMS.
[Laminar's fuel-system documentation](https://developer.x-plane.com/article/the-x-plane-fuel-system/)
explains why trim tanks cannot automatically feed engines.

Native donor limits are 595,248 lb empty, 1,267,657.6 lb maximum gross and
559,936.5 lb fuel. All four donor engine definitions specify 78,822 lbf rated
thrust; their native N1/N2 curves, 8.710000038 bypass ratio, 3,500 rpm reference
fan speed and 1.600000024 s compressor spool constant replace the previous
GP7270 values. Existing avionics/EFB reference limits have not been rewritten
and are not proof of complete Trent system/performance parity.

## What stays unchanged

The existing native panel sections, display-device registrations, cockpit
controls and MFD touch quads, pilot/copilot eyes, aircraft identity, electrical
equipment/buses and all installed plugin binaries/assets stay intact. The
Windows rollback remains in force. No new Windows helper is installed.

The developed cockpit, cabin, wing and flap OBJ geometry, textures and embedded
animations are preserved byte-for-byte, not replaced with the donor's models.
The -842 and preceding -861 donor have identical native flap/slat schedules, so
this donor switch also retains the current native flap/slat settings. The report
explicitly checks this, attachment preservation, and all 578 protected assets;
`protected-assets.json` retains their hashes for an independent recheck.

The validator now accepts **enabled** wing slots above 11. It continues rejecting
serialized disabled wings, which can reach the XP12 solver without initialized
airfoils. This replaces the scaffold-specific numerical slot cutoff, not the
safety requirement. The adapter removes disabled donor wing records outright.

## Pitch-trim load conflict corrected

The first adaptation retained the host's `acf/_ptch_acft_up_trim_rat=1` and
`acf/_ptch_acft_dn_trim_rat=1` while importing the donor's active stabilizer
incidence-trim assignments on wings 8/9. X-Plane rejected this combination at
load time with the warning that trim moved both the stabilizer and an elevator
tab. The earlier structural validation did not check that semantic relationship.

Both elevator-tab ranges now match the donor at **zero**, retaining the donor's
10-degree up / 2-degree down stabilizer ranges, stabilizer assignments, trim
mode and electric trim servo. Only those two ACF values changed in this repair;
the cockpit, cabin, flaps and installed plugins are not rewritten.
This follows [Laminar's XP12 trim configuration requirements](https://developer.x-plane.com/article/types-of-flight-control-trim/).

The offline adapter now imports the pitch-trim ranges/mode and flying-stabilizer
configuration together. Both candidate validation and whole-aircraft validation
reject simultaneous moving-stabilizer and elevator-tab trim. Regression tests
reproduce the original warning condition and accept the corrected configuration.
The pre-fix ACF/mirror and simulator log are backed up in
`.flightmodel-backups/before-trim-fix-20261020-LRpd8i/`. The corrected load still
requires a fresh simulator check; the prior failed log is not post-fix evidence.

## Reproduce and undo

The adapter is an offline build tool, defaulting to candidate-only output:

```sh
python3 tools/adapt_a388_flightmodel.py \
  --donor '/Volumes/Untitled/X-Plane 12 Aircraft/PA_A388/A380-842/A388.acf' \
  --output build/a388-new-candidate
```

With X-Plane and Plane Maker closed, add `--apply` to back up and install the
ACF, standalone `acf/A380X-XP12-current.acf` copy and required airfoils. Use a fresh
output directory each time. The report records exact hashes, gear offsets and
the backup path. `restore-map.json` in `.flightmodel-backups/before-pa-a388-*`
maps every replaced file to its original; entries with no backup were new files.
Restore the mapped originals and remove only those new airfoils to undo.
The current -842 report is `build/pa-a388-842-flightmodel-installed-20261020/adaptation-report.json`.
The original pre-adaptation aircraft (before either donor) remains recoverable
from `.flightmodel-backups/before-pa-a388-04csrkms/restore-map.json`.

Full scaffold regeneration by `build_development_aircraft.py` still constructs
the old physics. Do not use it to overwrite this adapted ACF; reapply this
offline adaptation after any such regeneration. Laminar permits offline block
transfers but stresses checking the result in Plane Maker:
[text ACF editing guidance](https://developer.x-plane.com/article/guidelines-for-working-with-text-based-acf-file-formats/).

## Acceptance still required

Automated adapter tests, provider checks, file hashes and static geometry checks
are not a flight test. Start a **fresh X-Plane process** after applying the ACF;
in-process reloads have previously retained gear caches in this project.

1. Check `Log.txt` for missing airfoils, loading errors and plugin errors. Confirm
   the existing PFD/ND/MFD/SD displays and mouse interactions still work.
2. With random failures explicitly disabled, load a moderate valid mass and CG
   on a level runway. Verify all five contacts, positive nose force, sensible
   loaded struts, no collapse/crash and settled pitch/roll within one degree.
   `ground_gear_diagnostics.py` and `audit_gear_mesh_alignment.py --strict` are
   useful diagnostics; neither proves contact loads or handling.
3. Check taxi steering, toe brakes, parking-brake release, and all gear retraction
   endpoints/bay clearances. The static tire envelope excludes truck pitch and
   actual tire/suspension compression.
4. Test slat/flap detents, ailerons/elevators/rudder, stabilizer trim, speedbrakes,
   all engines and reverse. Existing FBW/guidance laws and slat annunciations
   were not recalibrated to this donor model.
5. Separately test acceleration, rotation, climb, cruise, trim/stability, stalls,
   fuel consumption/depletion and landing at several masses and CG positions.
   Do not claim the donor's published performance or plugin behavior transfers
   automatically to this different model/system implementation.

No native X-Plane flight/handling pass has been recorded for this adaptation.
