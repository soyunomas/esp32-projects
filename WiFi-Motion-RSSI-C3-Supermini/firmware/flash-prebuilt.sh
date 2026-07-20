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
  echo "esptool is missing. Install it with: python3 -m pip install --user esptool" >&2
  exit 1
fi

"${FLASHER[@]}" --chip esp32c3 --port "$PORT" --baud "$BAUD" \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-freq 80m --flash-size 4MB \
  0x0 "$FIRMWARE_DIR/bootloader.bin" \
  0x8000 "$FIRMWARE_DIR/partition-table.bin" \
  0x10000 "$FIRMWARE_DIR/wifi-motion-rssi-c3.bin"

