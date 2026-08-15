#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${TMPDIR:-/tmp}/mh3u-se-encyclopedia-tests"
mkdir -p "$build_dir"

g++ -std=c++11 -Wall -Wextra -fPIC \
  -I"$root_dir/app/MH3U Save Editor GUI/MH3U Save Editor GUI" \
  "$root_dir/tests/test_encyclopedia_data.cpp" \
  "$root_dir/app/MH3U Save Editor GUI/MH3U Save Editor GUI/encyclopedia_data.cpp" \
  $(pkg-config --cflags --libs Qt5Core Qt5Sql) \
  -o "$build_dir/test_encyclopedia_data"

cd "$root_dir"
"$build_dir/test_encyclopedia_data"
