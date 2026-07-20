#!/usr/bin/env bash
set -euo pipefail

IDF_REF="${IDF_REF:-12f36a021f511cd4de41d3fffff146c5336ac1e7}"
IDF_ROOT="${IDF_ROOT:-$HOME/esp/esp-idf-12f36a021f}"

if [[ ! -d "$IDF_ROOT/.git" ]]; then
  mkdir -p "$(dirname "$IDF_ROOT")"
  git clone --filter=blob:none --no-checkout \
    https://github.com/espressif/esp-idf.git "$IDF_ROOT"
fi

git -C "$IDF_ROOT" fetch --depth 1 origin "$IDF_REF"
git -C "$IDF_ROOT" checkout --detach FETCH_HEAD
git -C "$IDF_ROOT" submodule update --init --recursive --depth 1

"$IDF_ROOT/install.sh" esp32c3
printf '\nRun:\n  source %q\n' "$IDF_ROOT/export.sh"
