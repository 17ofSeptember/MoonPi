#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
prefix="${1:-$PWD/release/moonpi}"
if [ ! -f frontend/dist/index.html ]; then
  printf '%s\n' 'Build frontend/dist on a development machine and copy it here first.' >&2
  exit 1
fi
sh scripts/build-pi.sh
cmake --install build-pi --prefix "$prefix"
printf 'Installed simulation package at %s\n' "$prefix"
printf '%s\n' 'No OS configuration or service installation was performed.'
