#!/bin/bash
# Runs a .3dsx in the Azahar emulator on a virtual X display (no GPU needed).
#   run-azahar.sh <file.3dsx> <seconds> [screenshot.png]
# AZAHAR: path to the azahar binary (Qt frontend). The emulated SD card is
# ~/.local/share/azahar-emu/sdmc.
APP=$1; SECS=${2:-20}; SHOT=$3
AZAHAR=${AZAHAR:-azahar}
export QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1
pkill -x Xvfb >/dev/null 2>&1; sleep 0.5
Xvfb :99 -screen 0 1280x1024x24 >/dev/null 2>&1 &
XPID=$!
sleep 1
export DISPLAY=:99
timeout -s KILL $SECS "$AZAHAR" "$APP" > "${TMPDIR:-/tmp}/azahar-run.log" 2>&1 &
EPID=$!
if [ -n "$SHOT" ]; then sleep $((SECS-3)); import -window root "$SHOT"; fi
wait $EPID
kill $XPID 2>/dev/null
