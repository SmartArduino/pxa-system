#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_dir="$(cd "$script_dir/.." && pwd)"
build_dir="${PXSYS_BUILD_DIR:-$source_dir/build}"
output_dir="${PXSYS_SDK_OUTPUT_DIR:-$source_dir/dist}"
version="$(tr -d '\r\n' < "$source_dir/VERSION")"
stage_dir="$build_dir/sdk-stage/pxa-system-$version"
archive="$output_dir/pxa-host-sdk-$version.tar.gz"

"$script_dir/build-host.sh" -DPXSYS_BUILD_TESTS=OFF \
  -DPXSYS_BUILD_SIMULATOR=OFF "$@"
cmake --install "$build_dir" --prefix "$stage_dir"
cmake -E make_directory "$output_dir"
cmake -E chdir "$build_dir/sdk-stage" cmake -E tar czf "$archive" \
  --format=gnutar "pxa-system-$version"
printf 'Created %s\n' "$archive"
