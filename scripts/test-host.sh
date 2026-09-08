#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd "$script_dir/.." && pwd)"
build_dir="${PXSYS_BUILD_DIR:-$source_dir/build}"

"$script_dir/build-host.sh" "$@"
ctest --test-dir "$build_dir" --output-on-failure
