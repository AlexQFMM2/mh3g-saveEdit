#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${TMPDIR:-/tmp}/mh3u-se-model-tests"
mkdir -p "$build_dir"

g++ -std=c++11 -Wall -Wextra -fPIC \
  -I"$root_dir/app/MH3U Save Editor GUI/MH3U Save Editor GUI" \
  "$root_dir/tests/test_mh3g_models.cpp" \
  "$root_dir/app/MH3U Save Editor GUI/MH3U Save Editor GUI/mh3g_model.cpp" \
  "$root_dir/app/MH3U Save Editor GUI/MH3U Save Editor GUI/game_resource_manager.cpp" \
  $(pkg-config --cflags --libs Qt5Core Qt5Gui) \
  -o "$build_dir/test_mh3g_models"

if [[ $# -gt 0 ]]; then
  XDG_DATA_HOME="$build_dir/local-data" "$build_dir/test_mh3g_models" "$@"
else
  "$build_dir/test_mh3g_models"
fi
