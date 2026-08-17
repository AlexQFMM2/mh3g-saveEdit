#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT

g++ -std=c++11 -Wall -Wextra -fPIC \
    $(pkg-config --cflags Qt5Core Qt5Sql) \
    -I"$project_dir/app/MH3U Save Editor/MH3U Save Editor" \
    "$project_dir/tests/test_equipment_validator.cpp" \
    "$project_dir/app/MH3U Save Editor/MH3U Save Editor/game_data_repository.cpp" \
    "$project_dir/app/MH3U Save Editor/MH3U Save Editor/equipment_validator.cpp" \
    $(pkg-config --libs Qt5Core Qt5Sql) \
    -o "$test_dir/test_equipment_validator"

"$test_dir/test_equipment_validator" "$project_dir/data/mh3g.sqlite"
