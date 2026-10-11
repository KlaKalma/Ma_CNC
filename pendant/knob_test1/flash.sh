#!/bin/bash
# flash.sh -- compile et flashe knob_test1.ino (meme logique que pendant/flash.sh,
# avec un build-path SEPARE : le cache d'arduino-cli est indexe sur le chemin.)
set -eo pipefail   # pipefail : sans lui, "arduino-cli compile | tail" renvoie le
                   # code de tail, une erreur de compilation est avalee et
                   # l upload repart sur le binaire PERIME.
BOARD="${1:-unor4wifi}"
PORT="${2:-/dev/ttyACM0}"
SKETCH="$(cd "$(dirname "$0")" && pwd)"
FQBN="arduino:renesas_uno:${BOARD}"
BUILD="/tmp/build-knob_test1-${BOARD}"
export PATH="$HOME/.local/bin:$PATH"

echo "== compilation ${FQBN}"
arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD" "$SKETCH" | tail -2

echo "== upload sur ${PORT}"
if ! arduino-cli upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD" "$SKETCH" 2>&1 | tail -2; then
  echo "-- echec, reset du peripherique USB puis nouvelle tentative"
  DEV=$(lsusb | awk '/2341:/{gsub(":","",$4); printf "/dev/bus/usb/%s/%s", $2, $4; exit}')
  sudo python3 -c "
import fcntl, os, sys
fd = os.open(sys.argv[1], os.O_WRONLY)
fcntl.ioctl(fd, ord('U') << 8 | 20, 0)   # USBDEVFS_RESET
os.close(fd)" "$DEV"
  sleep 4
  arduino-cli upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD" "$SKETCH" | tail -2
fi
echo "== OK"
