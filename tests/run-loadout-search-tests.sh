#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${TMPDIR:-/tmp}/mh3g-loadout-search-tests"
mkdir -p "$build_dir"
qmake ROOT="$root" "$root/tests/loadout_search_test.qmake" -o "$build_dir/Makefile"
make -C "$build_dir" -j2
"$build_dir/test_loadout_search" "$root/data/mh3g.sqlite"
