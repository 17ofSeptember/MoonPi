#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
cmake -S . -B build-pi -DCMAKE_BUILD_TYPE=Release
cmake --build build-pi --parallel 2
ctest --test-dir build-pi --output-on-failure
printf '%s\n' 'Moon Pi built. Simulation is the default; see docs/LINUX_GPIO.md for opt-in Pi 3 B+ GPIO.'
printf '%s\n' './build-pi/moonpi --simulation --web frontend/dist'
