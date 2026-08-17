#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT

g++ -std=c++11 -Wall -Wextra -fPIC \
    $(pkg-config --cflags Qt5Widgets Qt5Test) \
    -I"$project_dir/app/MH3U Save Editor GUI/MH3U Save Editor GUI" \
    "$project_dir/tests/test_searchable_combo.cpp" \
    $(pkg-config --libs Qt5Widgets Qt5Test) \
    -o "$test_dir/test_searchable_combo"

QT_QPA_PLATFORM=offscreen "$test_dir/test_searchable_combo"
