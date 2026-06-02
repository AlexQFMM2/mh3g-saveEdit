#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"
exec ./bin/MH3USaveEditorGUI "$@"
