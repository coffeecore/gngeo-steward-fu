#!/bin/sh
set -eu

EMU_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROM="$1"
ROM_DIR="$(dirname "$ROM")"

export SDL_NOMOUSE=1

export HOME="/mnt/SDCARD/Saves/gngeo"
export GNGEO_STATE_DIR="$HOME/states"

mkdir -p "$GNGEO_STATE_DIR"
mkdir -p "/mnt/SDCARD/.minui/logs"

export LD_LIBRARY_PATH="$EMU_DIR/lib:/mnt/SDCARD/System/lib:/usr/trimui/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if [ -d "$EMU_DIR/lib/ts" ]; then
    export TSLIB_PLUGINDIR="$EMU_DIR/lib/ts"
fi

needs-swap

exec "$EMU_DIR/gngeo" \
    -i "$ROM_DIR" \
    "$ROM" \
    > "/mnt/SDCARD/.minui/logs/Neo Geo.txt" 2>&1
