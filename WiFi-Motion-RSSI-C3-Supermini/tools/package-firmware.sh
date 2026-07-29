#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
: "${IDF_PATH:?Source the ESP-IDF export.sh first}"

TARGET="esp32c3"
APP_BUILD_NAME="wifi_motion_rssi_c3"
APP_FILE_NAME="wifi-motion-rssi-c3"
FIRMWARE_DIR="firmware"

mkdir -p "$FIRMWARE_DIR"
cp "build/bootloader/bootloader.bin" "$FIRMWARE_DIR/bootloader.bin"
cp "build/partition_table/partition-table.bin" \
  "$FIRMWARE_DIR/partition-table.bin"
cp "build/${APP_BUILD_NAME}.bin" "$FIRMWARE_DIR/${APP_FILE_NAME}.bin"

esptool --chip "$TARGET" merge-bin \
  -o "$FIRMWARE_DIR/${APP_FILE_NAME}-complete.bin" \
  --flash-mode dio --flash-freq 80m --flash-size 4MB \
  0x0 "$FIRMWARE_DIR/bootloader.bin" \
  0x8000 "$FIRMWARE_DIR/partition-table.bin" \
  0x10000 "$FIRMWARE_DIR/${APP_FILE_NAME}.bin"

(
  cd "$FIRMWARE_DIR"
  sha256sum \
    bootloader.bin \
    partition-table.bin \
    "${APP_FILE_NAME}.bin" \
    "${APP_FILE_NAME}-complete.bin" > SHA256SUMS
)

APP_SIZE="$(stat -c %s "$FIRMWARE_DIR/${APP_FILE_NAME}.bin")"
COMPLETE_SIZE="$(stat -c %s "$FIRMWARE_DIR/${APP_FILE_NAME}-complete.bin")"
IDF_VERSION="$(idf.py --version)"
IDF_VERSION="${IDF_VERSION#ESP-IDF }"

{
  echo "Project: WiFi Motion RSSI for ESP32-C3 SuperMini"
  echo "Build date: $(date -u +%F)"
  echo "Target: $TARGET"
  echo "Flash size: 4 MB"
  echo "Flash mode: DIO"
  echo "Flash frequency: 80 MHz"
  echo "ESP-IDF: $IDF_VERSION"
  echo "Application size: $APP_SIZE bytes"
  echo "Combined image size: $COMPLETE_SIZE bytes"
  echo
  echo "Separate image offsets:"
  echo "  0x000000  bootloader.bin"
  echo "  0x008000  partition-table.bin"
  echo "  0x010000  ${APP_FILE_NAME}.bin"
  echo
  echo "Runtime configuration is stored in NVS at 0x009000."
  echo "The separate-image flash procedure preserves NVS."
  echo "The combined factory image overwrites NVS with erased bytes."
} > "$FIRMWARE_DIR/BUILD-INFO.txt"

echo "Firmware package updated in $FIRMWARE_DIR/"
