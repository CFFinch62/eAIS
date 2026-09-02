#!/usr/bin/env sh
# Host-side AIS decoder tests. g++ only - no hardware, no PlatformIO.
set -e
cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/eais_test"
g++ -std=c++20 -Wall -Wextra -Isrc -Itest/stubs \
    test/ais/*.cpp src/ais/*.cpp src/nmea/*.cpp -o "$OUT"
exec "$OUT"
