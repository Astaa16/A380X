# SkunkCrafts Updater feed

The A380X XP12 port publishes an **experimental development feed** compatible
with SkunkCrafts Updater 4.x and the established 3.x depot protocol. It is not
a stable flight-validated release.

## User installation

Install the aircraft in `X-Plane 12/Aircraft/A380X-XP12`. The package must
contain `skunkcrafts_updater.cfg`. Close X-Plane, open SkunkCrafts Updater,
scan the simulator folder, select **A380X XP12 Port (Development)**, then use
Check/Repair.

Bundled livery updates are disabled by default. Users may opt into them in the
updater. Extra liveries and any other unlisted files are never deleted.

The development module is:

```text
https://github.com/Sava227/A380X/raw/refs/heads/skunkcrafts/output/A380X-XP12/
```

Do not replace that URL with `raw.githubusercontent.com`: Git LFS assets would
be served as pointer text instead of aircraft data. The `github.com/.../raw/`
endpoint redirects ordinary Git blobs and LFS objects to their correct byte
stores.

## Managed-file policy

`tools/build_skunkcrafts_depot.py` generates sorted, reproducible CRC32 and
size manifests from the hydrated package bytes. It rejects symlinks, unsafe or
case-colliding paths, and Git LFS pointer files.

The feed uses CRC `-1` (install if absent, then never overwrite) for:

- `A380X-XP12_prefs.txt`
- `fcu-calibration.ini`
- `X-Camera_A380X-XP12.csv`

Hidden files, AppleDouble metadata, backup directories, `build/`, runtime
smoke results, native build reports, and source animation inventories are not
published. The local updater configuration and updater control files are not
members of the payload manifest. Deleted or renamed runtime files must be
added manually to `skunkcrafts_updater_blacklist.txt` for at least one release.

## Generate and validate

From the repository root:

```sh
python3 tools/build_skunkcrafts_depot.py
python3 tools/build_skunkcrafts_depot.py --check
python3 -m unittest tools/test_build_skunkcrafts_depot.py
```

The version comes from the repository `VERSION` file. The first non-empty line
of the package `changelog.txt` must match it. Bump both for every publication.
The generator creates the matching package `VERSION` and these depot files:

- `skunkcrafts_updater.cfg`
- `skunkcrafts_updater_whitelist.txt`
- `skunkcrafts_updater_sizeslist.txt`
- `skunkcrafts_updater_blacklist.txt`

The configuration is both the local discovery file and the remote
configuration expected by current SkunkCrafts clients. The similarly named
`skunkcrafts_updater_config.txt` is only an `xp_depot` authoring input and is
not required by this static feed.

## Publish safely

Publish an exact source-and-package snapshot to the dedicated `skunkcrafts`
branch without switching or staging the working checkout. Uploading Git LFS
objects happens before the branch ref changes, so clients do not see a commit
whose large payload is absent. After the push:

1. GET the remote configuration and both lists; do not rely on `HEAD` alone.
2. GET representative ordinary and LFS payload files and verify size and CRC.
3. Run the official SkunkCrafts CLI against a temporary X-Plane tree.
4. Check the real installation without starting an update, then copy the local
   configuration into it only after the remote depot passes.

For a non-atomic web/CDN upload, first publish the configuration with
`locked|true`; upload payload files, blacklist, sizes list, and whitelist;
purge metadata caches; finally upload `locked|false`. Serve every byte without
image optimization, transcoding, or minification. SkunkCrafts requests the
configuration with a unique query and version-keys list files, but a CDN that
ignores query strings must be purged explicitly.

GitHub is suitable only for this small development audience. GitHub LFS's
included bandwidth is roughly a few full repairs of this multi-gigabyte plane
per month. Before a public release, move the same expanded depot to static
object storage/CDN, keep metadata TTLs short, verify remote GET bytes, and keep
the `module` URL stable. CRC32 detects transfer errors; release archives should
also carry a published SHA-256 checksum.

## Compatibility and source references

- SkunkCrafts Updater v4 release and downloads:
  <https://forums.x-plane.org/forums/topic/352063-20261004-skunkcrafts-updater-v4xy-is-available/>
- Official v4 user guide:
  <https://1228098873.rsc.cdn77.org/updater/USER_GUIDE.html>
- Open-source 4.x client and publisher protocol:
  <https://github.com/lzamouth/sku_client>

The macOS display helper needs execute permission. SkunkCrafts downloads into
a regular temporary file and does not preserve POSIX mode, so the aircraft
plugin validates the expected helper as a non-symlink regular file and restores
execute permission immediately before spawning it.
