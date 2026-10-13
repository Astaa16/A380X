#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
blender_bin=${BLENDER_BIN:-/Applications/Blender.app/Contents/MacOS/Blender}
input_path=${1:-"$script_dir/../upstream-aircraft-large-files/fbw-a380x"}
output_path=${2:-"$script_dir/output/A380X-XP12/A380X-converted.blend"}
report_path=${3:-"$script_dir/output/A380X-XP12/animation-report.json"}

if [ ! -x "$blender_bin" ]; then
    echo "Blender was not found at $blender_bin. Set BLENDER_BIN to the Blender executable." >&2
    exit 2
fi

"$blender_bin" --background --python-exit-code 1 \
    --python "$script_dir/blender/convert_gltf_animations.py" -- \
    --input "$input_path" \
    --mapping "$script_dir/config/animation_datarefs.json" \
    --output "$output_path" \
    --report "$report_path"
