#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${IDF_PATH:?Source the ESP-IDF export.sh first}"
PORT="${PORT:-/dev/ttyACM0}"
idf.py -p "$PORT" flash monitor
