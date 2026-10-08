# Nintendo 3DS port (work in progress)

An experimental port of the [Free Pascal version](https://github.com/pakompom/SpaceRangersHD_FPC)
of the recovered source to the New Nintendo 3DS (homebrew, `.3dsx`).

This directory does not change the matching Delphi source in `source/`. It holds:

| Path | Contents |
| --- | --- |
| `patches/fpc_sr.patch` | Changes to the pinned [FPC fork](https://github.com/pakompom/fpc_sr) for the new `arm-ctr` target |
| `fpc-overlay/` | New FPC files: target info (`i_ctr`), linker (`t_ctr`) and the 3DS RTL (`rtl/ctr`) |
| `patches/SpaceRangersHD_FPC.patch` | Changes to the pinned FPC port of the game |
| `platform/` | 3DS platform units; they take precedence over the FPC port's `platform/` |
| `tests/` | On-device tests |
| `tools/` | Build scripts |

## Toolchain

The port adds an `arm-ctr` target to Free Pascal:

- ARM11 MPCore (ARMv6K) with VFPv2, AAPCS-VFP (`eabihf`) calling convention.
- Linking is done by the devkitARM gcc driver with `3dsx.specs`; the Pascal
  main program is the C `main()` called by libctru's crt0.
- The RTL uses newlib and libctru through a small C layer (`rtl/ctr/fpcctr.c`):
  files (`sdmc:/`, `romfs:/`), libctru threads (threadvars via TLS, worker
  threads can run on the New 3DS extra core), recursive locks and light events.
- The FPC ARM code generator crashed when a `Double` parameter was passed on
  the stack after the VFP argument registers were used up; the patch fixes it
  and aligns such arguments as required by AAPCS.

## Platform layer

The FPC port draws through an IDirect3DDevice9 implemented on SDL_Renderer.
`native/sdlctr` implements the part of SDL2 it uses on the 3DS, so the
port's Pascal platform units stay unchanged:

- Rendering: citro3d, with texture render targets, batched triangles,
  clipping, alpha blending and readback. Render targets must be in VRAM
  (two 3 MiB banks), so the 1024x768 game screen uses RGB565.
- Presentation: one screen shows the whole game screen scaled down with a
  frame marking the zoomed region, the other a zoomed region that follows
  the cursor.
- Input, audio (ndsp; needs `sdmc:/3ds/dspfirm.cdc`), software keyboard,
  message boxes, mutexes and condition variables.

`native/vorbisfile` provides the libvorbisfile calls of the game on Tremor.
OKGF, the game's C graphics library, is built from its pinned source.

### Controls

| Input | Action |
| --- | --- |
| Touch screen | Left click / drag at the touched point |
| L + touch | Move the cursor without clicking |
| R + touch | Right click |
| Circle pad | Move the cursor |
| A / B | Left / right mouse button |
| L + circle pad | Mouse wheel |
| C-stick | Pan the zoomed view (New 3DS) |
| ZL / ZR | Zoom out / in (New 3DS) |
| D-pad | Arrow keys |
| X | Software keyboard |
| Y | Enter |
| START | Escape |
| SELECT | Swap the screens |

## Installing

Copy `Rangers.3dsx` to `sdmc:/3ds/` and the contents of the installed game
folder to `sdmc:/3ds/SpaceRangersHD/` (so that `install.txt` is directly in
it). Saves and logs go to `sdmc:/3ds/SpaceRangersHD/user/`; the port's own
logs are `ctr.log` and `sdl.log` in the data folder. Without data the game
shows where it expects it. Planetary battles (MatrixGame) and AVI videos are
not available.

### Downscaled data

The HD images are drawn 1:1 in the game's 1024x768 space, which the 3DS
shows at 400x240 (or zoomed in), and they do not fit its memory beyond the
main menu. `tools/convert-data.sh` makes a set of the image packages at half
size (or a quarter with shift 2):

```sh
port3ds/tools/convert-data.sh <game>/DATA DATA-half      # shift 1, default
port3ds/tools/convert-data.sh <game>/DATA DATA-quarter 2
```

It builds the converter (`tools/assetconv`, needs a host C compiler and
zlib) on first use and takes about a minute; the half-size set is about
800 MiB instead of 1.7 GiB. Copy the output folder to
`sdmc:/3ds/SpaceRangersHD/` and write its name in
`sdmc:/3ds/SpaceRangersHD/assets.txt` (for example `DATA-half`); packages
missing from it are read from `DATA`, and deleting `assets.txt` goes back to
the original data. `ctr.log` lists the packages taken from the set.

Converted images keep their logical size, so layouts and clicks are
unchanged; the cost is detail when zooming in. Converted: `.gi` images of at
least 64x64 pixels, `.gai` animations, and playback animations (the menu
ships, the government officials) with their first image. Not converted:
`.hai` ship sprites, JPEG/PNG pictures, rotating (format 4) animations.

## Building

Requirements: devkitARM with libctru and the 3DS portlibs (the
`devkitpro/devkitarm` Docker image has everything), Free Pascal 3.2.2 for the
host, git, make.

```sh
export DEVKITPRO=/opt/devkitpro
FPC_BOOTSTRAP=/path/to/ppcx64 port3ds/tools/build.sh
```

Steps: `compiler rtl native game rtltest` (default: all), and `assetconv`
(the host converter, see Downscaled data). Outputs go to
`port3ds/.local/`:

- `rtltest/rtltest.3dsx` - RTL self-test; writes `sdmc:/rtltest.log` and ends
  with `RESULT: PASS`. It covers exceptions, strings and code pages, floating
  point, the Pascal/C calling convention (AAPCS-VFP), files, the heap, threads,
  threadvars, critical sections and events.

`tools/run-azahar.sh` runs a `.3dsx` in the [Azahar](https://github.com/azahar-emu/azahar)
emulator on a virtual display. The RTL self-test passes there.
- `build/Rangers.3dsx` - the game. `GAME_FLAGS=-Cn` compiles the game units
  without linking.

## Status

- [x] `arm-ctr` compiler target and RTL (system, sysutils, classes, threads,
      math, zlib, fcl-image) and a Unicode string manager (`ctrwstring`: UTF-8,
      CP1251, CP1252, CP866, Latin/Cyrillic case mapping)
- [x] RTL self-test passes in the emulator
- [x] All game units compile for the 3DS
- [x] Platform layer: SDL2 subset on citro3d, ndsp and hid; the video
      self-test passes in the emulator
- [x] Linked `Rangers.3dsx`; with the game data it reaches the main menu in
      the emulator (Azahar, New 3DS mode) after about 2.5 minutes
- [x] Large animations (.gai above 1 MiB, up to 62 MiB in the HD version)
      are read frame by frame, keeping only the frame shown on the GPU;
      images above the 1024-pixel GPU limit (the 4200x1600 menu panorama)
      are drawn from 512-pixel tiles created when they come into view. The main menu runs with its ship animations and
      background, but slowly (each animation frame is decoded and uploaded
      on the fly) and close to the memory limit.
- [x] Downscaled data sets (`tools/convert-data.sh`, `assets.txt`); with
      the half-size set the game gets through the new game screen and the
      galaxy generation in the emulator, but the heap is then at 82 of 87
      MiB (game data, not images) and entering space runs out of memory.
- [ ] Measure speed and memory on a New 3DS
- [ ] Interface legibility on the small screens (layout work)

Memory notes: the platform layer reports the real free memory as the
texture budget, drops the CPU copy of a texture after uploading it (the GPU
copy is read back if the game locks it again), keeps textures above the GPU
limit at their downscaled size, lets the Pascal heap spill into spare linear
memory, and caps worker thread stacks at 1 MiB. The resource cache keeps
what was used in the last 8 seconds even above its 8 MiB budget, and the
package layer remembers block offsets so that frames can be read from the
middle of a compressed entry. Creating `trace_open.txt` next to the game
data logs every file open and every heap block of 1 MiB or more to
`open.log`; `frame_dump.txt` makes sdlctr save every 20th game frame, at
the full 1024x768, as `frameNNN.bmp`. `input_script.txt` plays scripted
input for tests, one action per line: `<seconds> click|rclick|move <x> <y>`
in game coordinates or `<seconds> key <SDL scancode>`.

Game data is not included; it comes from an installed copy of the game.
