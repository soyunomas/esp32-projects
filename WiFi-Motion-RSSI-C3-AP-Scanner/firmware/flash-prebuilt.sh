#!/usr/bin/env bash
set -euo pipefail

firmware_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
port="${PORT:-/dev/ttyACM0}"
baud="${BAUD:-460800}"

python3 -m esptool --chip esp32c3 --port "${port}" --baud "${baud}" \
    --before default-reset --after hard-reset write-flash \
    --flash-mode dio --flash-freq 80m --flash-size 4MB \
    0x0 "${firmware_dir}/bootloader.bin" \
    0x8000 "${firmware_dir}/partition-table.bin" \
    0x10000 "${firmware_dir}/wifi-ap-scan-probe-c3.bin"
