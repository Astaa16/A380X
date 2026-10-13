#!/bin/sh
# Rebuild the detailed LOD00 passenger cabin on the external toolchain volume.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=/Volumes/MacGames/A380X-XPlane12-build
asset_root="/Volumes/Savs' drive/A380X-XPlane12/upstream-aircraft-large-files"
attachments="$asset_root/fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire"
aircraft_root="$asset_root/fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X"
source="$attachments/Part_Interior_Cabin/model/A380_CABIN_LOD00.gltf"
blender_bin=/Volumes/MacGames/A380X-XPlane12-toolchain/Blender.app/Contents/MacOS/Blender
blender_user_dir=/Volumes/MacGames/A380X-XPlane12-toolchain/blender-user
export TMPDIR="$build_dir/tmp"

python3 "$project_dir/tools/prepare_cabin_interior.py" "$source" \
  --asset-root "$asset_root" \
  --build-dir "$build_dir" \
  --reuse-decoded

python3 "$project_dir/tools/prepare_upper_deck_door_surrounds.py" "$source" \
  --manifest "$build_dir/manifests/upper_deck_door_surround_exports.json" \
  --report "$build_dir/reports/upper-deck-door-surrounds.json"

python3 "$project_dir/tools/rebuild_interior_geometry.py" \
  --aircraft-root "$aircraft_root" \
  --build-dir "$build_dir" \
  --blender "$blender_bin" \
  --blender-user-dir "$blender_user_dir" \
  --only cabin-textured \
  --only cabin-upper-door-surrounds
