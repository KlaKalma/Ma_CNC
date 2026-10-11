#!/bin/bash
set -eo pipefail   # pipefail : sans lui, "arduino-cli compile | tail" renvoie le
                   # code de tail, une erreur de compilation est avalee et
                   # l upload repart sur le binaire PERIME.
BOARD="${1:-unor4wifi}"; PORT="${2:-/dev/ttyACM0}"
SKETCH="$(cd "$(dirname "$0")" && pwd)"
FQBN="arduino:renesas_uno:${BOARD}"; BUILD="/tmp/build-knob-${BOARD}"
export PATH="$HOME/.local/bin:$PATH"
echo "== compilation ${FQBN}"
arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD" "$SKETCH" | tail -2
echo "== upload sur ${PORT}"
arduino-cli upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD" "$SKETCH" | tail -2
echo "== OK"
