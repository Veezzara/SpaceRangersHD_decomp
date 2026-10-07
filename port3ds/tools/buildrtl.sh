#!/bin/bash
# Builds the Free Pascal RTL and packages for the Nintendo 3DS (arm-ctr).
# Usage: buildrtl.sh <fpc-source-dir> <output-dir>
set -e
FPC_SRC=$(realpath "$1")
OUT=$(realpath -m "$2")
PPC="$FPC_SRC/compiler/ppcarm"
: "${DEVKITPRO:=/opt/devkitpro}"
CC="$DEVKITPRO/devkitARM/bin/arm-none-eabi-gcc"
mkdir -p "$OUT"
RTL="$FPC_SRC/rtl"
PKG="$FPC_SRC/packages"

"$CC" -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O2 -Wall -D__3DS__ \
  -I"$DEVKITPRO/libctru/include" -c "$RTL/ctr/fpcctr.c" -o "$OUT/fpcctr.o"

COMMON=(-n -Tctr -XParm-none-eabi- -O2 -g -gl "-FU$OUT" "-Fl$DEVKITPRO/libctru/lib"
  "-Fi$RTL/ctr" "-Fi$RTL/inc" "-Fi$RTL/arm" "-Fi$RTL/objpas" "-Fi$RTL/objpas/sysutils"
  "-Fi$RTL/objpas/classes" "-Fu$RTL/ctr" "-Fu$RTL/inc" "-Fu$RTL/arm" "-Fu$RTL/objpas"
  "-Fu$RTL/charmaps" "-Fi$RTL/charmaps")

# system unit
"$PPC" "${COMMON[@]}" -Us -Sg "$RTL/ctr/system.pp"
# the remaining RTL units compile in the modes they select themselves
for unit in "$RTL/objpas/objpas.pp" "$RTL/objpas/sysconst.pp" "$RTL/objpas/rtlconsts.pp" \
            "$RTL/ctr/sysutils.pp" "$RTL/objpas/types.pp" "$RTL/objpas/typinfo.pp" \
            "$RTL/inc/sortbase.pp" "$RTL/arm/intrinsics.pp" "$RTL/ctr/classes.pp" \
            "$RTL/objpas/math.pp" "$RTL/objpas/fgl.pp" "$RTL/inc/ctypes.pp" \
            "$RTL/inc/strings.pp" "$RTL/inc/charset.pp" "$RTL/objpas/unicodedata.pas" \
            "$RTL/ctr/ctrwstring.pp" \
            "$RTL/objpas/character.pas" "$RTL/inc/getopts.pp" \
            "$RTL/inc/lineinfo.pp" "$RTL/inc/lnfodwrf.pp" "$RTL/inc/heaptrc.pp"; do
  "$PPC" "${COMMON[@]}" "$unit"
done

PKGFLAGS=("-Fu$PKG/rtl-objpas/src/inc" "-Fi$PKG/rtl-objpas/src/inc" "-Fi$PKG/rtl-objpas/src/common"
  "-Fu$PKG/fcl-base/src" "-Fi$PKG/fcl-base/src" "-Fu$PKG/hash/src" "-Fu$PKG/paszlib/src"
  "-Fi$PKG/paszlib/src" "-Fu$PKG/pasjpeg/src" "-Fi$PKG/pasjpeg/src" "-Fu$PKG/fcl-image/src" "-Fi$PKG/fcl-image/src")
for unit in "$PKG/rtl-objpas/src/inc/strutils.pp" "$PKG/rtl-objpas/src/inc/dateutils.pp" \
            "$PKG/rtl-objpas/src/inc/varutils.pp" "$PKG/rtl-objpas/src/inc/variants.pp" \
            "$PKG/fcl-base/src/syncobjs.pp" "$PKG/fcl-base/src/contnrs.pp" \
            "$PKG/fcl-base/src/uriparser.pp" "$PKG/paszlib/src/zbase.pas" \
            "$PKG/paszlib/src/zinflate.pas" "$PKG/paszlib/src/zdeflate.pas" \
            "$PKG/fcl-image/src/fpimage.pp" "$PKG/fcl-image/src/fpwritejpeg.pas"; do
  "$PPC" "${COMMON[@]}" "${PKGFLAGS[@]}" "$unit"
done
echo "RTL built in $OUT"
