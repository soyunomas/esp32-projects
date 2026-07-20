#!/usr/bin/env bash
set -euo pipefail

IDF_VERSION="${IDF_VERSION:-v6.0.2}"
IDF_ROOT="${IDF_ROOT:-$HOME/esp/esp-idf-${IDF_VERSION}}"

if [[ ! -d "$IDF_ROOT/.git" ]]; then
  mkdir -p "$(dirname "$IDF_ROOT")"
  git clone --branch "$IDF_VERSION" --recursive \
    https://github.com/espressif/esp-idf.git "$IDF_ROOT"
fi

"$IDF_ROOT/install.sh" esp32c3
printf '\nRun:\n  source %q\n' "$IDF_ROOT/export.sh"
