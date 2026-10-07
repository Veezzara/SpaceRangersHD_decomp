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

## Building

Requirements: devkitARM with libctru and the 3DS portlibs (the
`devkitpro/devkitarm` Docker image has everything), Free Pascal 3.2.2 for the
host, git, make.

```sh
export DEVKITPRO=/opt/devkitpro
FPC_BOOTSTRAP=/path/to/ppcx64 GAME_FLAGS=-Cn port3ds/tools/build.sh
```

Outputs go to `port3ds/.local/`:

- `rtltest/rtltest.3dsx` - RTL self-test; writes `sdmc:/rtltest.log` and ends
  with `RESULT: PASS`. It covers exceptions, strings and code pages, floating
  point, the Pascal/C calling convention (AAPCS-VFP), files, the heap, threads,
  threadvars, critical sections and events.

`tools/run-azahar.sh` runs a `.3dsx` in the [Azahar](https://github.com/azahar-emu/azahar)
emulator on a virtual display. The RTL self-test passes there.
- `build/` - the game. `GAME_FLAGS=-Cn` compiles all game units without
  linking; linking needs the 3DS platform layer (in progress).

## Status

- [x] `arm-ctr` compiler target and RTL (system, sysutils, classes, threads,
      math, zlib, fcl-image) and a Unicode string manager (`ctrwstring`: UTF-8,
      CP1251, CP1252, CP866, Latin/Cyrillic case mapping)
- [x] RTL self-test passes in the emulator
- [x] All game units compile for the 3DS
- [ ] Platform layer: rendering (citro3d), input, audio, events
- [ ] Linked `.3dsx`, game start-up with data from the SD card

Game data is not included; it comes from an installed copy of the game.
