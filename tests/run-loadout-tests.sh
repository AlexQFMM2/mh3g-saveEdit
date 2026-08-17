#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${TMPDIR:-/tmp}/mh3g-loadout-tests"
mkdir -p "$build_dir"

g++ -std=c++11 -fPIC \
  -I"$root/app/MH3U Save Editor/MH3U Save Editor" \
  "$root/tests/test_loadout.cpp" \
  "$root/app/MH3U Save Editor/MH3U Save Editor/game_data_repository.cpp" \
  "$root/app/MH3U Save Editor/MH3U Save Editor/equipment_validator.cpp" \
  "$root/app/MH3U Save Editor/MH3U Save Editor/loadout.cpp" \
  "$root/app/MH3U Save Editor/MH3U Save Editor/mh3u_se.cpp" \
  $(pkg-config --cflags --libs Qt5Core Qt5Sql) \
  -o "$build_dir/test_loadout"

"$build_dir/test_loadout" "$root/data/mh3g.sqlite" "$@"
