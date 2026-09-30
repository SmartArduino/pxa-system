#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
cpp_sdk_dir="$pxa_system_dir/sdk/guest-cpp"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-guest-cpp-test.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

cxx="${CXX:-clang++}"
flags=(-std=c++2c -O2 -fno-exceptions -fno-rtti
       -Wall -Wextra -Werror -Wno-attributes -I"$cpp_sdk_dir/include")
"$cxx" "${flags[@]}" -c "$cpp_sdk_dir/src/runtime.cpp" -o "$work_dir/runtime.o"
"$cxx" "${flags[@]}" -c "$cpp_sdk_dir/src/net.cpp" -o "$work_dir/net.o"
"$cxx" "${flags[@]}" -c "$cpp_sdk_dir/src/ipc.cpp" -o "$work_dir/ipc.o"
"$cxx" "${flags[@]}" -c "$cpp_sdk_dir/src/work.cpp" -o "$work_dir/work.o"
"$cxx" "${flags[@]}" -c "$cpp_sdk_dir/src/surface.cpp" -o "$work_dir/surface.o"

python3 "$cpp_sdk_dir/tools/generate_ipc_contract.py" \
  "$cpp_sdk_dir/examples/ipc-stats/stats.contract.json" \
  "$cpp_sdk_dir/examples/ipc-stats/stats_contract.hpp" --check
python3 "$cpp_sdk_dir/tests/test_generate_ipc_contract.py"

for test_name in features core_app ui_wire ui_page ui_controls counter_app navigation list task assets storage fs permission audio device sensor net ipc ipc_contract work surface game game_service game_loop; do
  "$cxx" "${flags[@]}" \
    "$cpp_sdk_dir/tests/${test_name}_test.cpp" \
    "$work_dir/runtime.o" "$work_dir/net.o" "$work_dir/ipc.o" "$work_dir/work.o" "$work_dir/surface.o" \
    -o "$work_dir/$test_name"
  "$work_dir/$test_name"
done

echo "PXA C++ Guest host tests OK"
