#!/bin/bash
set -eo pipefail   # pipefail : sans lui, "arduino-cli compile | tail" renvoie le
                   # code de tail, une erreur de compilation est avalee et
                   # l upload repart sur le binaire PERIME.
BOARD="${1:-unor4wifi}"; PORT="${2:-/dev/ttyACM0}"
SKETCH="$(cd "$(dirname "$0")" && pwd)"
FQBN="arduino:renesas_uno:${BOARD}"; BUILD="/tmp/build-knoblab-${BOARD}"
export PATH="$HOME/.local/bin:$PATH"
echo "== compilation ${FQBN}"
arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD" "$SKETCH" | tail -2
# USB natif (2341:006d) : la demande 1200 bps du coeur ne redemarre pas la
# carte. On lui demande d'aller elle-meme en bootloader ('boot'), ce qui rend
# l'USB a l'ESP32 (2341:1002), puis on flashe par le chemin habituel.
if lsusb -d 2341:006d >/dev/null 2>&1; then
  echo "== USB natif : commande boot"
  python3 -c "
import serial,time
s=serial.Serial('${PORT}',2000000,timeout=0.2); s.write(b'stream 0\nmode 0\n'); time.sleep(0.1); s.write(b'boot\n'); time.sleep(0.2); s.close()" || true
  # L'aiguillage passe de l'USB du RA4M1 a celui de l'ESP32 SANS coupure
  # visible : le PC garde l'ancien peripherique (mesure). On force la
  # reenumeration en desactivant/reactivant le port du Pi.
  sleep 0.5
  P=/sys/bus/usb/devices/1-0:1.0/usb1-port1/disable
  echo 1 | sudo -n tee $P >/dev/null; sleep 1.5; echo 0 | sudo -n tee $P >/dev/null
  for i in $(seq 1 50); do lsusb -d 2341:1002 >/dev/null 2>&1 && break; sleep 0.2; done
  if ! lsusb -d 2341:1002 >/dev/null 2>&1; then
    # SURTOUT ne pas lancer l'upload : sur le port natif, arduino-cli ferait
    # le reset 1200 bps, qui FIGE la carte (double appui RESET obligatoire).
    echo "!! la carte n'est pas passee en bootloader : upload annule."
    echo "!! double appui rapide sur RESET (LED en respiration), puis relancer."
    exit 1
  fi
  sleep 1.5
fi
echo "== upload sur ${PORT}"
arduino-cli upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD" "$SKETCH" | tail -2
echo "== OK"
