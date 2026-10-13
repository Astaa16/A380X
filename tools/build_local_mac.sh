#!/bin/sh
# Local external-drive development build. Optional extra flags include --acf-only.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=/Volumes/MacGames/A380X-XPlane12-build
reference_dir=/Volumes/MacGames/A380X-XPlane12-git/open-source-747-reference
cockpit_source="$project_dir/../upstream-aircraft-large-files/fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire/Part_Interior_Cockpit/model/A380_COCKPIT_LOD00.gltf"
cabin_source="$project_dir/../upstream-aircraft-large-files/fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire/Part_Interior_Cabin/model/A380_CABIN_LOD00.gltf"
fuselage_source="$project_dir/../upstream-aircraft-large-files/fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire/Part_Exterior_Fuselage/model/A380_FUSELAGE.gltf"
controls_object="$build_dir/xplane-obj/cockpit-controls/a380x_cockpit_controls.obj"
# Visible MFD cockpit-device screens; mfd_screen_black.png is written beside it.
mfd_screens_object="$build_dir/xplane-obj/mfd-screens/a380x_mfd_screens.obj"
fcu_panel_assets="$build_dir/xplane-fcu-panel"
commands_file="$HOME/Library/Application Support/Steam/steamapps/common/X-Plane 12/Resources/plugins/Commands.txt"
datarefs_file="$HOME/Library/Application Support/Steam/steamapps/common/X-Plane 12/Resources/plugins/DataRefs.txt"
python3 "$project_dir/tools/build_fcu_panel.py" --output "$fcu_panel_assets"
if [ -f "$commands_file" ] && [ -f "$datarefs_file" ]; then
  python3 "$project_dir/tools/build_cockpit_controls.py" "$cockpit_source" \
    --config "$project_dir/config/cockpit_controls.json" \
    --output "$controls_object" \
    --mfd-screens-output "$mfd_screens_object" \
    --report "$build_dir/reports/cockpit-controls.json" \
    --commands "$commands_file" \
    --datarefs "$datarefs_file" \
    --additional-source "$cabin_source" "$project_dir/config/cabin_door_controls.json" \
    --additional-source "$fuselage_source" "$project_dir/config/upper_deck_door_controls.json" \
    --fcu-overlay
else
  python3 "$project_dir/tools/build_cockpit_controls.py" "$cockpit_source" \
    --config "$project_dir/config/cockpit_controls.json" \
    --output "$controls_object" \
    --mfd-screens-output "$mfd_screens_object" \
    --report "$build_dir/reports/cockpit-controls.json" \
    --additional-source "$cabin_source" "$project_dir/config/cabin_door_controls.json" \
    --additional-source "$fuselage_source" "$project_dir/config/upper_deck_door_controls.json" \
    --fcu-overlay
fi
python3 "$project_dir/tools/build_development_aircraft.py" \
  --template "$reference_dir/747-400.acf" \
  --reference-license "$reference_dir/LICENSE" \
  --airfoils "$reference_dir/airfoils" \
  --body-obj "$build_dir/xplane-obj/gear-body-lh-body/a380x_gear_body_lh.obj" \
  --tires-obj "$build_dir/xplane-obj/gear-body-lh-tires/a380x_gear_body_lh_tires.obj" \
  --textures "$build_dir/textures/gear-body-lh" \
  --fuselage-objects "$build_dir/xplane-obj/fuselage-corrected" \
  --fuselage-textures "$build_dir/textures/fuselage" \
  --wing-objects "$build_dir/xplane-obj/wings-corrected" \
  --wing-textures "$build_dir/textures/wings" \
  --complete-gear-build "$build_dir" \
  --cockpit-objects "$build_dir/xplane-obj/cockpit-textured" \
  --cockpit-textures "$build_dir/textures/cockpit-lod01" \
  --cabin-objects "$build_dir/xplane-obj/cabin-textured" \
  --cabin-textures "$build_dir/textures/cabin-lod00" \
  --cabin-seat-objects "$build_dir/xplane-obj/cabin-seats-textured" \
  --cabin-seat-textures "$build_dir/textures/cabin-seats" \
  --cockpit-panel-objects "$build_dir/xplane-obj/cockpit-panels-textured" \
  --cockpit-panel-textures "$build_dir/textures/cockpit-panels" \
  --cockpit-controls-object "$controls_object" \
  --mfd-screens-object "$mfd_screens_object" \
  --fcu-panel-assets "$fcu_panel_assets" \
  --lua "$project_dir/FlyWithLua/Scripts/A380X_XP12_FBW.lua" \
  --output "$project_dir/output/A380X-XP12" "$@"

# Apply engine materials from source after wing packaging, including when the
# cached exports live on a full build image. Focused cockpit/ACF refreshes skip
# this pass; full builds and explicit wing refreshes retain the smoother finish.
engine_material_refresh=full
next_refresh_group=no
has_refresh_groups=no
engine_acf_only=no
for argument in "$@"; do
  if [ "$next_refresh_group" = yes ]; then
    has_refresh_groups=yes
    [ "$argument" != wings ] || engine_material_refresh=wings
    next_refresh_group=no
  fi
  case "$argument" in
    --refresh-group) next_refresh_group=yes ;;
    --refresh-group=wings) has_refresh_groups=yes; engine_material_refresh=wings ;;
    --refresh-group=*) has_refresh_groups=yes ;;
    --acf-only) engine_acf_only=yes ;;
  esac
done
if [ "$engine_acf_only" = no ] && { [ "$has_refresh_groups" = no ] || [ "$engine_material_refresh" = wings ]; }; then
  python3 "$project_dir/tools/prepare_engine_materials.py" \
    --source "$project_dir/../upstream-aircraft-large-files/fbw-a380x/src/base/flybywire-aircraft-a380-842/SimObjects/AirPlanes/FlyByWire_A380X/attachments/flybywire/Part_Exterior_Wings/model/A380_WINGS.gltf" \
    --build-dir "$build_dir" --aircraft "$project_dir/output/A380X-XP12" --package-only \
    --backup "$project_dir/build/engine-material-backups" \
    --report "$project_dir/output/A380X-XP12/build/engine-materials.json"
fi
