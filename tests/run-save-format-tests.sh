#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <3ds-user-file> <wiiu-user-file>" >&2
    exit 2
fi

project_dir="$(cd "$(dirname "$0")/.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT

g++ -std=c++11 -Wall -Wextra -fPIC \
    $(pkg-config --cflags Qt5Core) \
    -I"$project_dir/app/MH3U Save Editor/MH3U Save Editor" \
    -I"$project_dir/app/MH3U Save Editor GUI/MH3U Save Editor GUI" \
    "$project_dir/tests/test_save_formats.cpp" \
    "$project_dir/app/MH3U Save Editor/MH3U Save Editor/mh3u_se.cpp" \
    "$project_dir/app/MH3U Save Editor/MH3U Save Editor/mh3u_transfer.cpp" \
    "$project_dir/app/MH3U Save Editor GUI/MH3U Save Editor GUI/save_action_bridge.cpp" \
    $(pkg-config --libs Qt5Core) \
    -o "$test_dir/test_save_formats"

"$test_dir/test_save_formats" "$1" "$2"
