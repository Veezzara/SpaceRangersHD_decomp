#!/bin/bash
# Converts the image packages of a Space Rangers HD installation for the 3DS.
#
#   port3ds/tools/convert-data.sh <game DATA folder> <output folder> [shift]
#
# shift 1 (default) stores images at half size, 2 at a quarter. Copy the
# output folder next to DATA on the SD card (for example as DATA-half) and
# write its name in assets.txt in the game folder; packages missing from it
# are read from DATA. See port3ds/README.md.
set -euo pipefail

HERE=$(cd "$(dirname "$0")/.." && pwd)
WORK=${WORK:-$HERE/.local}
TOOL=${ASSETCONV:-$WORK/assetconv/assetconv}
SRC=${1:?usage: convert-data.sh <DATA folder> <output folder> [shift]}
OUT=${2:?usage: convert-data.sh <DATA folder> <output folder> [shift]}
SHIFT=${3:-1}
[ -x "$TOOL" ] || "$HERE/tools/build.sh" assetconv
mkdir -p "$OUT"
# The packages with images; the others (sound, music, quests, planetary
# battles) have nothing to convert.
for name in mainmenu forms common items locations background ships WSE arcade; do
  pkg=$(find "$SRC" -maxdepth 1 -iname "$name.pkg" | head -1)
  [ -n "$pkg" ] || { echo "skipping $name.pkg (not found)"; continue; }
  "$TOOL" -s "$SHIFT" -j "$(nproc 2>/dev/null || echo 4)" "$pkg" "$OUT/$(basename "$pkg")"
done
