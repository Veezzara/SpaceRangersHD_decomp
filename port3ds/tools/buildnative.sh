#!/bin/bash
# Builds the native libraries of the 3DS port:
#   libSDL2.a        - SDL2 subset on citro3d/ndsp (native/sdlctr)
#   libokgf.a        - the game's graphics library (OKGF)
#   libvorbisfile.a  - vorbisfile API on Tremor (native/vorbisfile)
# Usage: buildnative.sh <okgf-source> <out-dir>
set -e
HERE=$(cd "$(dirname "$0")/.." && pwd)
OKGF=$(realpath "$1"); OUT=$(realpath -m "$2")
: "${DEVKITPRO:=/opt/devkitpro}"
export PATH="$DEVKITPRO/devkitARM/bin:$DEVKITPRO/tools/bin:$PATH"
CFLAGS=(-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O2 -g -Wall -D__3DS__
  -I"$DEVKITPRO/libctru/include" -I"$DEVKITPRO/portlibs/3ds/include")
mkdir -p "$OUT/obj/sdl" "$OUT/obj/okgf" "$OUT/obj/vorbis"

# SDL2 subset
picasso -o "$OUT/obj/sdl/render.shbin" "$HERE/native/sdlctr/render.v.pica"
(cd "$OUT/obj/sdl" && bin2s render.shbin > render_shbin.s)
printf '#pragma once\n#include <stdint.h>\nextern const uint8_t render_shbin[];\nextern const uint32_t render_shbin_size;\n' > "$OUT/obj/sdl/render_shbin.h"
arm-none-eabi-gcc -c "$OUT/obj/sdl/render_shbin.s" -o "$OUT/obj/sdl/render_shbin.o"
for f in "$HERE"/native/sdlctr/*.c; do
  arm-none-eabi-gcc "${CFLAGS[@]}" -I"$OUT/obj/sdl" -c "$f" -o "$OUT/obj/sdl/$(basename "${f%.c}").o"
done
rm -f "$OUT/libSDL2.a"
arm-none-eabi-ar rcs "$OUT/libSDL2.a" "$OUT"/obj/sdl/*.o

# OKGF
for f in math/precision cpu pixels copy lines alpha_buffers rle delta indexed lighting rotation \
         rotation_build planet planet_build shapes gradients rescale bmp_read image_write png_read \
         jpeg_read psd_read image_read platform; do
  arm-none-eabi-gcc "${CFLAGS[@]}" -std=c11 -fno-fast-math -ffp-contract=off \
    -DOKGF_GAME_RELEASE=OKGF_GAME_SRHD -DOKGF_NATIVE_MATH=1 -I"$OKGF/include" -I"$OKGF/src" \
    -c "$OKGF/src/$f.c" -o "$OUT/obj/okgf/$(basename "$f").o"
done
rm -f "$OUT/libokgf.a"
arm-none-eabi-ar rcs "$OUT/libokgf.a" "$OUT"/obj/okgf/*.o

# vorbisfile on Tremor: rename Tremor's ov_* so the shim can provide them
SYMS=()
for sym in $(arm-none-eabi-nm "$DEVKITPRO/portlibs/3ds/lib/libvorbisidec.a" | awk '$2=="T" && $3 ~ /^ov_/ {print $3}' | sort -u); do
  SYMS+=(--redefine-sym "$sym=tremor_$sym")
done
arm-none-eabi-objcopy "${SYMS[@]}" "$DEVKITPRO/portlibs/3ds/lib/libvorbisidec.a" "$OUT/obj/vorbis/libtremor.a"
arm-none-eabi-gcc "${CFLAGS[@]}" -c "$HERE/native/vorbisfile/vorbisfile_tremor.c" -o "$OUT/obj/vorbis/vorbisfile_tremor.o"
rm -f "$OUT/libvorbisfile.a"
cp "$OUT/obj/vorbis/libtremor.a" "$OUT/libvorbisfile.a"
arm-none-eabi-ar rs "$OUT/libvorbisfile.a" "$OUT/obj/vorbis/vorbisfile_tremor.o"
# the Pascal bindings also name these libraries; Tremor contains everything
arm-none-eabi-ar rcs "$OUT/libvorbis.a"
arm-none-eabi-ar rcs "$OUT/libvorbisenc.a"
echo "native libraries in $OUT"
