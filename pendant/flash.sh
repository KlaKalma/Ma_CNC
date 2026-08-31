#!/bin/bash
# flash.sh -- compile et flashe pendant.ino sur la carte du pendant.
#
#   ./pendant/flash.sh              (defaut : UNO R4 WiFi sur /dev/ttyACM0)
#   ./pendant/flash.sh minima /dev/ttyACM1
#
# Pourquoi ce script plutot qu'un appel direct a arduino-cli :
#   - le cache de build d'arduino-cli est indexe sur le CHEMIN du sketch, pas
#     sur le FQBN. Compiler pour minima puis uploader en unor4wifi flashe le
#     binaire minima sans aucun avertissement, et la carte reste muette.
#     -> --build-path / --input-dir explicites, un repertoire par carte.
#   - le reset logiciel a 1200 bauds echoue parfois ("No device found on
#     ttyACM0"). Un reset du peripherique USB (ioctl USBDEVFS_RESET) le
#     debloque, la ou le bouton RESET de la carte ne suffit pas.

set -e

BOARD="${1:-unor4wifi}"
PORT="${2:-/dev/ttyACM0}"
SKETCH="$(cd "$(dirname "$0")" && pwd)"
FQBN="arduino:renesas_uno:${BOARD}"
BUILD="/tmp/build-pendant-${BOARD}"

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
