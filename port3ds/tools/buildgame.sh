#!/bin/bash
# Compiles the Space Rangers HD FPC port for the Nintendo 3DS.
# Usage: buildgame.sh <fpc-source-dir> <rtl-units-dir> <fpc-port-dir> <out-dir> [extra fpc args]
set -e
FPC_SRC=$(realpath "$1"); RTL=$(realpath "$2"); GAME=$(realpath "$3"); OUT=$(realpath -m "$4"); shift 4
HERE=$(dirname "$(realpath "$0")")/..
: "${DEVKITPRO:=/opt/devkitpro}"
PPC="$FPC_SRC/compiler/ppcarm"
mkdir -p "$OUT/units"
DIRS=()
for d in $(find "$GAME/source" -type d | sort); do DIRS+=("-Fu$d"); done
"$PPC" -n -Tctr -XParm-none-eabi- -Mdelphi -FcUTF8 -O2 -OoNOORDERFIELDS -OoNOFASTMATH -g -gl \
  "-Fu$RTL" "-Fl$DEVKITPRO/libctru/lib" "-Fl$DEVKITPRO/portlibs/3ds/lib" "-Fl$OUT" \
  "-Fu$HERE/platform" "-Fi$HERE/platform" \
  "-Fu$GAME/platform" \
  "-Fu$FPC_SRC/packages/oggvorbis/src" "-Fu$FPC_SRC/packages/fcl-image/src" "-Fu$FPC_SRC/packages/pasjpeg/src" \
  "-Fi$GAME/source" "${DIRS[@]}" "-FU$OUT/units" "-FE$OUT" "$@" "$GAME/source/Rangers.dpr"
