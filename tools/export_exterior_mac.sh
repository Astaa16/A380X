#!/bin/sh
# Re-export exterior materials/animations using the pinned external toolchain.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=/Volumes/MacGames/A380X-XPlane12-build
asset_root="/Volumes/Savs' drive/A380X-XPlane12/upstream-aircraft-large-files"
attachments="$asset_root/fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire"
export BLENDER_USER_CONFIG=/Volumes/MacGames/A380X-XPlane12-toolchain/blender-user/config
export BLENDER_USER_SCRIPTS=/Volumes/MacGames/A380X-XPlane12-toolchain/blender-user/scripts
export TMPDIR="$build_dir/tmp"
blender_bin=/Volumes/MacGames/A380X-XPlane12-toolchain/Blender.app/Contents/MacOS/Blender
python3 "$project_dir/tools/prepare_landing_gear_bay_textures.py" \
  --source "$attachments/Part_Exterior_Fuselage/model/A380_FUSELAGE.gltf" \
  --asset-root "$asset_root" \
  --build-dir "$build_dir"
python3 "$project_dir/tools/prepare_engine_materials.py" \
  --source "$attachments/Part_Exterior_Wings/model/A380_WINGS.gltf" \
  --build-dir "$build_dir" --prepare-only \
  --backup "$project_dir/build/engine-material-backups" \
  --report "$build_dir/reports/engine-material-preparation.json"
for group in fuselage wings; do
  case "$group" in
    fuselage) source_name=A380_FUSELAGE.gltf; source_part=Part_Exterior_Fuselage; set -- --repair-fbw-tail-bindshape ;;
    wings) source_name=A380_WINGS.gltf; source_part=Part_Exterior_Wings; set -- --repair-fbw-wing-joints ;;
  esac
  # Always rebuild from upstream, so packed draw-range and winding repairs
  # are applied exactly once instead of reusing stale normalized buffers.
  python3 "$project_dir/blender/prepare_gltf_subset.py" \
    "$attachments/$source_part/model/$source_name" \
    "$build_dir/normalized/$group/$source_name" \
    --manifest "$build_dir/manifests/$group-exports.json" "$@"
  # Wings need a common evaluated neutral skin pose. Tail panels retain their
  # corrected bind-shape geometry and only consolidate same-family skin weights.
  if [ "$group" = wings ]; then
    set -- --bake-static-skin-pose --rigid-fbw-flap-panels --rigid-fbw-spoiler-panels
  else
    set -- --rigid-fbw-tail-panels
  fi
  "$blender_bin" --background --threads 2 --python-exit-code 1 --python "$project_dir/blender/convert_gltf_animations.py" -- \
    --input "$build_dir/normalized/$group/$source_name" \
    --mapping "$project_dir/config/animation_datarefs.json" \
    --output "$build_dir/blend/$group-corrected.blend" \
    --report "$build_dir/reports/$group-corrected.json" \
    --export-manifest "$build_dir/manifests/$group-exports.json" \
    --export-dir "$build_dir/xplane-obj/$group-corrected" "$@"
done
python3 "$project_dir/tools/build_gear_assets.py" \
  --asset-root "$asset_root" \
  --build-dir "$build_dir" --blender "$blender_bin" --reuse-materials
