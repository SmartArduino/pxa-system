#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
cpp_sdk_dir="$pxa_system_dir/sdk/guest-cpp"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-guest-cpp-test.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

for test_name in core_app ui_wire ui_page ui_controls counter_app task assets game game_service game_loop; do
  "${CXX:-c++}" -std=c++23 -O2 -Wall -Wextra -Werror -Wno-attributes \
    -I"$cpp_sdk_dir/include" \
    "$cpp_sdk_dir/tests/${test_name}_test.cpp" \
    "$cpp_sdk_dir/src/runtime.cpp" \
    -o "$work_dir/$test_name"
  "$work_dir/$test_name"
done

echo "PXA C++ Guest host tests OK"
