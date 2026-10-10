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

python3 "$pxa_system_dir/tools/i18n/compile_catalog.py" \
  "$cpp_sdk_dir/tests/i18n/messages.yaml" "$cpp_sdk_dir"/tests/i18n/{en-GB,zh-CN,zh-Hant,ru,ar}.yaml \
  --language cpp --output "$work_dir/pxa_i18n_test_messages.hpp"
python3 "$pxa_system_dir/tools/i18n/compile_catalog.py" \
  "$cpp_sdk_dir/examples/i18n/i18n/messages.yaml" "$cpp_sdk_dir"/examples/i18n/i18n/{zh-CN,zh-Hant,ru}.yaml \
  --language cpp --output "$work_dir/pxa_app_messages.hpp"
python3 "$pxa_system_dir/tools/i18n/test_compile_catalog.py"


for test_name in features binary events startup locale i18n i18n_app core_app ui_wire ui_page ui_input ui_display canvas ui_controls ui_canvas game_painter game_painter_replay game_arcade counter_app navigation list task assets storage storage_value storage_ownership fs permission permission_ownership audio device sensor net ipc ipc_contract work surface game game_pacing game_host game_service game_loop game_utils; do
  extra_includes=(-I"$work_dir")
  extra_objects=()
  if [[ "$test_name" == game_host || "$test_name" == game_painter_replay || "$test_name" == game_arcade ]]; then
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

"$cxx" "${flags[@]}" -I"$work_dir" -DPXA_I18N_DYNAMIC_TEXT=1 \
  "$cpp_sdk_dir/tests/i18n_app_test.cpp" \
  "$work_dir/runtime.o" "$work_dir/net.o" "$work_dir/ipc.o" "$work_dir/work.o" "$work_dir/game.o" "$work_dir/surface.o" \
  -o "$work_dir/i18n_dynamic_app"
"$work_dir/i18n_dynamic_app"

for slots in 3 8 65; do
  "$cxx" "${flags[@]}" -DPXA_COROUTINE_SLOT_COUNT="$slots" \
    "$cpp_sdk_dir/tests/task_pool_test.cpp" -o "$work_dir/task_pool_$slots"
  "$work_dir/task_pool_$slots"
done

echo "PXA C++ Guest host tests OK"
