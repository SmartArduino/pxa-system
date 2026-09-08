#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd "$script_dir/.." && pwd)"
build_dir="${PXSYS_BUILD_DIR:-$source_dir/build}"

cmake -S "$source_dir" -B "$build_dir" "$@"
if [[ -n "${PXSYS_BUILD_JOBS:-}" ]]; then
  cmake --build "$build_dir" --parallel "$PXSYS_BUILD_JOBS"
else
  cmake --build "$build_dir" --parallel
fi
