#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${IDF_PATH:?Source the ESP-IDF export.sh first}"
idf.py set-target esp32c3
idf.py -D SDKCONFIG_DEFAULTS=sdkconfig.defaults build
