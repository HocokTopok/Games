#!/bin/sh
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
task_build=${1:-"$task_root/build"}
task_icons="$task_root/assets/icons"
task_iconset="$task_build/TankWars.iconset"
mkdir -p "$task_icons" "$task_iconset"
cmake --build "$task_build" --target tankwars_icon_tool --parallel 8
"$task_build/tankwars_icon_tool" "$task_root/assets" "$task_icons/TankWars.png"
for task_size in 16 32 128 256 512; do
    sips -z "$task_size" "$task_size" "$task_icons/TankWars.png" --out "$task_iconset/icon_${task_size}x${task_size}.png" >/dev/null
    task_double=$((task_size * 2))
    sips -z "$task_double" "$task_double" "$task_icons/TankWars.png" --out "$task_iconset/icon_${task_size}x${task_size}@2x.png" >/dev/null
done
iconutil -c icns "$task_iconset" -o "$task_icons/TankWars.icns"
