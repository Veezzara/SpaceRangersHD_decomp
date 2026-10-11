# Working on the 3DS port (port3ds/)

Read `HANDOFF.md` in this folder first: it has the current state, open
problems and what was already ruled out. `README.md` describes the port for
users.

## Ground rules

- Talk to the user in Russian.
- Never commit game data (packages, converted packages, frame dumps,
  screenshots of game art): it is copyrighted and private.
- Do not open pull requests unless asked.
- The emulator (Azahar) does not reproduce the console's graphics problems.
  A rendering fix counts as verified only with a console screenshot or
  frame dump, never with the emulator alone.
- Before changing code for a reported symptom, first narrow it down with
  the cheapest test (logs, frame dump, screenshot), and say which hypothesis
  the change rests on.

## How the code is laid out

- `fpc-overlay/` - the arm-ctr FPC target and RTL (`rtl/ctr/fpcctr.c` is the
  C side: files, memory, threads).
- `native/sdlctr/` - SDL2 subset on citro3d/ndsp/hid (video, events, audio).
- `platform/` - `Ctru.pas` (logging, memory stats) and other port units.
- `patches/SpaceRangersHD_FPC.patch` - all changes to the game sources
  (github.com/pakompom/SpaceRangersHD_FPC at the revision pinned in
  `tools/build.sh`). Port changes are under `{$IFDEF CTR}`.
- `tools/assetconv/` - host converter that downscales image packages;
  `tools/convert-data.sh` runs it.

## Building

```sh
export DEVKITPRO=/opt/devkitpro
FPC_BOOTSTRAP=/path/to/ppcx64 port3ds/tools/build.sh          # everything
port3ds/tools/build.sh native game                             # after changes
```

Outputs go to `port3ds/.local/` (override with `WORK=`): `build/Rangers.3dsx`
and `build/Rangers.elf` (symbols, for crash dumps).

Editing game code: the `game` step resets `$WORK/game` to the pinned
revision and applies the patch, so edits made there are lost unless the
patch is regenerated first:

```sh
# edit files in $WORK/game, then:
git -C "$WORK/game" diff > port3ds/patches/SpaceRangersHD_FPC.patch
port3ds/tools/build.sh game
```

## Running and debugging on the console

- Data lives on the SD card in `sdmc:/3ds/SpaceRangersHD/` (the user's own
  copy). `assets.txt` there names the downscaled package folder
  (`DATA-half`); without it the original packages are used.
- `3dslink -s Rangers.3dsx` sends and starts a build when Homebrew Launcher
  waits for it (Y); `-s` streams the game's stdout to the terminal.
- Files in the game folder that switch on debug aids:
  - `frame_dump.txt` - every 20th game frame saved as `frameNNN.bmp`
    (the 1024x768 game image before it is shown on the 3DS screens);
  - `trace_open.txt` - file opens and heap blocks of 1 MiB or more in
    `open.log`;
  - `input_script.txt` - scripted input: `<seconds> click|rclick|move x y`
    or `<seconds> key <SDL scancode>`, in game coordinates.
- Logs: `ctr.log` (memory, files and textures every 2 s), `sdl.log`,
  `user/########.log` (the game's own log, CP1251).
- Luma crash dumps (`sdmc:/luma/dumps/arm11/*.dmp`): header is 40 bytes
  (magic DEADC0DE DEADCAFE, version, processor, exception type, sizes of the
  register, code and stack dumps), then r0-r12, sp, lr, pc, cpsr, dfsr,
  ifsr, far...; symbolize pc, lr and stack words in 0x100000-0x800000 with
  `arm-none-eabi-addr2line -f -e Rangers.elf` (the ELF must be from the same
  build).
- Rosalina (L+Down+Select): GDB stub for attaching `arm-none-eabi-gdb` over
  Wi-Fi, and screenshots of the real screens.
