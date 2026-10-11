# Handoff: state of the 3DS port (2026-10-11)

Goal: Space Rangers HD on a New 3DS. Branch `claude/relaxed-maxwell-2cc048`.
The user tests on a real New 3DS (Luma, Homebrew Launcher started from
Download Play) and works on Arch Linux.

## What works

- RTL, sdlctr, linking, packages, sound; in the emulator the game reaches
  the main menu, the new game screen and the galaxy generation.
- Memory work: frame-by-frame .gai animations, downscaled storage for
  textures above 1024 px, lazy 512 px tiles for such images, cache budget
  with an 8 s retention, CPU texture copies dropped after upload.
- Downscaled data sets (`tools/assetconv`, `convert-data.sh`, `assets.txt`):
  converted images keep their logical size (GI header Version 0x5343xxxx and
  Gap38, GAI Gap28 "SC"), the engine keeps their pixels and textures at the
  stored size. Half size: 1.7 GiB of image packages -> ~690 MiB; main menu
  heap ~60 MiB.
- On the console with DATA-half the main menu and the new game screen run
  and the interface is drawn correctly.

## Open problems (console only; the emulator does not show them)

1. **Backgrounds not drawn.** Full-screen backgrounds (main menu panorama,
   new game screen background) do not appear; the game redraws only changed
   areas, so older screens stay visible underneath (golden streaks, ship
   trails, menu remnants). ctr.log shows no failed allocations, enough
   linear memory and no texture errors in sdl.log, so the textures are
   created. Unverified hypothesis: opaque images, which become RGB565
   textures, are not drawn on hardware (everything with alpha, RGBA8, is).
   Next step: `frame_dump.txt` on the console plus a Rosalina screenshot of
   the same screen, to tell whether the background is missing in the game
   image (drawing) or only on the screens (presentation in sdlctr). Then,
   if it is drawing, a test pattern with RGB565 and RGBA8 textures.
2. **Hang after "New game" -> galaxy creation**, about 2 minutes in. The
   whole process stops (even the 2 s ctr.log monitor), with 64 MiB heap,
   20 MiB linear free, no failed allocations. Not memory. Next step: attach
   GDB through Rosalina and dump all thread stacks.
3. **Unused memory.** The console gives a 124 MiB application region
   (`app region` in ctr.log; 96 MiB in the emulator) but the heap capacity
   stays 87 MiB in both, so ~28 MiB are unused. Check how libctru splits
   heap and linear memory (`__ctru_heap_size`, `__system_allocateHeaps`).
4. In the emulator the heap reaches 82 of 87 MiB after the galaxy
   generation and allocations fail when entering space; nobody has profiled
   what holds it (game data, not images).

## Lessons / do not repeat

- Commit 889c368 (tiles) was made on a wrong diagnosis: the console
  artifacts were taken for blur from an 8x downscale, while the background
  is actually not drawn at all. The tiles work and improve quality, but did
  not fix the artifacts and later caused a crash (fixed in 22237cc).
- An earlier "out of memory" explanation of the artifacts was refuted by
  the console logs.
- Images and textures that fail to load are skipped silently (7db9ee9),
  which hides failures; add logging when touching that code.

## Proposed next work

1. Device loop for a local agent: mirror ctr.log to stdout (for
   `3dslink -s`), optionally send frame dumps and logs to the PC over the
   network, and a `tools/device-run.sh` that sends, starts and collects.
2. Problems 1 and 2 above, verified on the console.
3. Give the heap the unused memory; profile what fills it; consider a .cia
   build for more memory.
4. Later: speed on hardware (turn processing), layout for the small
   screens, planetary battles (MatrixGame.dll) are not ported.
