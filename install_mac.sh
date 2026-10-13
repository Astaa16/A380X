#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
xplane_dir=${XPLANE_DIR:-"$HOME/Library/Application Support/Steam/steamapps/common/X-Plane 12"}
scripts_dir="$xplane_dir/Resources/plugins/FlyWithLua/Scripts"

if [ ! -d "$scripts_dir" ]; then
    echo "FlyWithLua was not found under $xplane_dir. Set XPLANE_DIR to the X-Plane 12 folder." >&2
    exit 2
fi

for filename in A380X_XP12_FBW.lua; do
    destination="$scripts_dir/$filename"
    ln -sfn "$script_dir/FlyWithLua/Scripts/$filename" "$destination"
    echo "Linked $destination"
done
echo "PFD/ND/flyPad avionics are aircraft-local native plugins; legacy Lua display/FCU adapters are not installed."
