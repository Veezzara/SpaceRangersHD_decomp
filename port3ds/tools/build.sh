#!/bin/bash
# Builds Space Rangers HD for the Nintendo 3DS from scratch.
#
#   port3ds/tools/build.sh [step...]
#
# Steps (default: all): compiler rtl native game rtltest
#
# Requirements:
#   - devkitARM + libctru + 3ds portlibs (DEVKITPRO, default /opt/devkitpro;
#     the devkitpro/devkitarm Docker image contains everything)
#   - Free Pascal 3.2.2 for the host (FPC_BOOTSTRAP, default: fpc/ppcx64 in PATH)
#   - git, make, python3
#
# Everything is built under port3ds/.local (WORK to override).
set -euo pipefail

HERE=$(cd "$(dirname "$0")/.." && pwd)
WORK=${WORK:-$HERE/.local}
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"

# Pinned upstream revisions.
FPC_REPO=https://github.com/pakompom/fpc_sr.git
FPC_REV=3a1c9cfae7f7a2bb17079b2989f562dbc5728b01
GAME_REPO=https://github.com/pakompom/SpaceRangersHD_FPC.git
GAME_REV=5f491a841cbd11d2a9a6861a822a08ffa64d15f2
OKGF_REPO=https://github.com/pakompom/okgf.git
OKGF_REV=c01aa7a168a6f1772541501074b7bba1b96550ef

FPC_SRC=$WORK/fpc
RTL_OUT=$WORK/rtl
GAME_SRC=$WORK/game
OKGF_SRC=$WORK/okgf
GAME_OUT=$WORK/build
NATIVE_OUT=$WORK/native

log() { printf '\033[1m==> %s\033[0m\n' "$*"; }

checkout() { # repo rev dir
  local repo=$1 rev=$2 dir=$3
  if [ ! -d "$dir/.git" ]; then
    git init -q "$dir"
    git -C "$dir" remote add origin "$repo"
  fi
  if [ "$(git -C "$dir" rev-parse -q --verify HEAD 2>/dev/null)" != "$rev" ]; then
    git -C "$dir" fetch -q --depth 1 origin "$rev"
    git -C "$dir" checkout -q -f FETCH_HEAD
  fi
  git -C "$dir" reset -q --hard "$rev"
  git -C "$dir" clean -q -fdx
}

host_fpc() {
  if [ -n "${FPC_BOOTSTRAP:-}" ]; then echo "$FPC_BOOTSTRAP"; return; fi
  command -v ppcx64 || command -v fpc || { echo "Free Pascal 3.2.2 not found (set FPC_BOOTSTRAP)" >&2; exit 1; }
}

step_compiler() {
  log "Preparing FPC sources ($FPC_REV)"
  mkdir -p "$WORK"
  checkout "$FPC_REPO" "$FPC_REV" "$FPC_SRC"
  git -C "$FPC_SRC" apply "$HERE/patches/fpc_sr.patch"
  cp -r "$HERE/fpc-overlay/." "$FPC_SRC/"
  log "Building the arm-ctr cross compiler"
  make -C "$FPC_SRC/compiler" arm PP="$(host_fpc)" OPT="-O2" > "$WORK/compiler.log" 2>&1 ||
    { tail -30 "$WORK/compiler.log"; exit 1; }
}

step_rtl() {
  log "Building the 3DS RTL"
  # Refresh the overlay so RTL edits do not need a compiler rebuild.
  cp -r "$HERE/fpc-overlay/rtl/." "$FPC_SRC/rtl/"
  rm -rf "$RTL_OUT"
  "$HERE/tools/buildrtl.sh" "$FPC_SRC" "$RTL_OUT" > "$WORK/rtl.log" 2>&1 ||
    { grep -E "Error|Fatal" "$WORK/rtl.log" | head -30; exit 1; }
}

step_native() {
  log "Building native libraries (SDL2 subset, OKGF, vorbisfile)"
  checkout "$OKGF_REPO" "$OKGF_REV" "$OKGF_SRC"
  "$HERE/tools/buildnative.sh" "$OKGF_SRC" "$NATIVE_OUT" > "$WORK/native.log" 2>&1 ||
    { grep -iE "error" "$WORK/native.log" | head -30; exit 1; }
}

step_game() {
  log "Preparing game sources ($GAME_REV)"
  checkout "$GAME_REPO" "$GAME_REV" "$GAME_SRC"
  git -C "$GAME_SRC" apply "$HERE/patches/SpaceRangersHD_FPC.patch"
  log "Compiling the game for arm-ctr"
  "$HERE/tools/buildgame.sh" "$FPC_SRC" "$RTL_OUT" "$GAME_SRC" "$GAME_OUT" "-Fl$NATIVE_OUT" ${GAME_FLAGS:-} > "$WORK/game.log" 2>&1 ||
    { grep -E "Error|Fatal|undefined reference" "$WORK/game.log" | head -40; exit 1; }
  if [[ " ${GAME_FLAGS:-} " != *" -Cn "* ]] && [ -f "$GAME_OUT/Rangers.elf" ]; then
    smdhtool --create "Space Rangers HD" "Unofficial 3DS port" "SpaceRangersHD_decomp" \
      "$HERE/meta/icon.png" "$GAME_OUT/Rangers.smdh"
    3dsxtool "$GAME_OUT/Rangers.elf" "$GAME_OUT/Rangers.3dsx" --smdh="$GAME_OUT/Rangers.smdh"
    log "Built $GAME_OUT/Rangers.3dsx"
  else
    log "Game units compiled (link skipped)"
  fi
}

step_rtltest() {
  log "Building the RTL self-test"
  mkdir -p "$WORK/rtltest"
  arm-none-eabi-gcc -march=armv6k -mtune=mpcore -mfloat-abi=hard -O2 -c "$HERE/tests/rtltest/abitest.c" -o "$WORK/rtltest/abitest.o"
  "$FPC_SRC/compiler/ppcarm" -n -Tctr -XParm-none-eabi- -O2 -g "-Fu$RTL_OUT" "-Fl$DEVKITPRO/libctru/lib" \
    "-FU$WORK/rtltest" "-FE$WORK/rtltest" "-Fo$WORK/rtltest" "$HERE/tests/rtltest/rtltest.pp" > "$WORK/rtltest.log" 2>&1 ||
    { grep -E "Error|Fatal" "$WORK/rtltest.log"; exit 1; }
  3dsxtool "$WORK/rtltest/rtltest.elf" "$WORK/rtltest/rtltest.3dsx"
  log "Built $WORK/rtltest/rtltest.3dsx"
}

steps=("$@")
[ ${#steps[@]} -eq 0 ] && steps=(compiler rtl native game rtltest)
for s in "${steps[@]}"; do "step_$s"; done
