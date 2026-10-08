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
"$cxx" "${flags[@]}" -c "$cpp_sdk_dir/src/game.cpp" -o "$work_dir/game.o"
"$cxx" "${flags[@]}" -c "$cpp_sdk_dir/src/surface.cpp" -o "$work_dir/surface.o"
"${CC:-cc}" -std=c11 -O2 -I"$pxa_system_dir/libpxa/include" \
  -c "$pxa_system_dir/libpxa/src/services/surface/raster.c" \
  -o "$work_dir/raster.o"
"${CC:-cc}" -std=c11 -O2 -I"$pxa_system_dir/libpxa/include" \
  -c "$pxa_system_dir/libpxa/src/core/wire.c" \
  -o "$work_dir/wire.o"

python3 "$cpp_sdk_dir/tools/generate_ipc_contract.py" \
  "$cpp_sdk_dir/examples/ipc-stats/stats.contract.json" \
  "$cpp_sdk_dir/examples/ipc-stats/stats_contract.hpp" --check
python3 "$cpp_sdk_dir/tests/test_generate_ipc_contract.py"

for test_name in features core_app ui_wire ui_page ui_input canvas ui_controls counter_app navigation list task assets storage fs permission audio device sensor net ipc ipc_contract work surface game game_host game_service game_loop; do
  extra_includes=()
  extra_objects=()
  if [[ "$test_name" == game_host ]]; then
    extra_includes=(-I"$pxa_system_dir/libpxa/include")
    extra_objects=("$work_dir/raster.o" "$work_dir/wire.o")
  fi
  "$cxx" "${flags[@]}" \
    "${extra_includes[@]}" \
    "$cpp_sdk_dir/tests/${test_name}_test.cpp" \
    "$work_dir/runtime.o" "$work_dir/net.o" "$work_dir/ipc.o" "$work_dir/work.o" "$work_dir/game.o" "$work_dir/surface.o" \
    "${extra_objects[@]}" \
    -o "$work_dir/$test_name"
  "$work_dir/$test_name"
done

for slots in 3 8 65; do
  "$cxx" "${flags[@]}" -DPXA_COROUTINE_SLOT_COUNT="$slots" \
    "$cpp_sdk_dir/tests/task_pool_test.cpp" -o "$work_dir/task_pool_$slots"
  "$work_dir/task_pool_$slots"
done

echo "PXA C++ Guest host tests OK"
