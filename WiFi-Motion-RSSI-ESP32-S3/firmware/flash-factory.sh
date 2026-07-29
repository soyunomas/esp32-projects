#!/usr/bin/env bash
set -euo pipefail

FIRMWARE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PORT="${PORT:-/dev/ttyACM0}"
BAUD="${BAUD:-460800}"

if command -v esptool >/dev/null 2>&1; then
  FLASHER=(esptool)
elif command -v esptool.py >/dev/null 2>&1; then
  FLASHER=(esptool.py)
elif python3 -c 'import esptool' >/dev/null 2>&1; then
  FLASHER=(python3 -m esptool)
else
  echo "Install esptool with: python3 -m pip install --user esptool" >&2
  exit 1
fi

echo "WARNING: this factory image clears all saved runtime settings."
"${FLASHER[@]}" --chip esp32s3 --port "$PORT" --baud "$BAUD" \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-freq 80m --flash-size 4MB \
  0x0 "$FIRMWARE_DIR/wifi-motion-rssi-s3-complete.bin"
