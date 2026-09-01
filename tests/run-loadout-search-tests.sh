#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${TMPDIR:-/tmp}/mh3g-loadout-search-tests"
mkdir -p "$build_dir"
qmake_bin="${QMAKE:-qmake}"
if ! command -v "$qmake_bin" >/dev/null 2>&1; then qmake_bin="qmake-qt5"; fi
if ! command -v "$qmake_bin" >/dev/null 2>&1; then
    echo "qmake/qmake-qt5 not found" >&2
    exit 127
fi
"$qmake_bin" ROOT="$root" "$root/tests/loadout_search_test.qmake" -o "$build_dir/Makefile"
make_bin="make"
if ! command -v "$make_bin" >/dev/null 2>&1; then make_bin="mingw32-make"; fi
if ! command -v "$make_bin" >/dev/null 2>&1; then
    echo "make/mingw32-make not found" >&2
    exit 127
fi
"$make_bin" -C "$build_dir" -j2
test_bin="$build_dir/test_loadout_search"
if [ ! -x "$test_bin" ] && [ -x "$test_bin.exe" ]; then test_bin="$test_bin.exe"; fi
if [ ! -x "$test_bin" ]; then
    echo "loadout search test binary not found" >&2
    exit 127
fi
"$test_bin" "$root/data/mh3g.sqlite"
