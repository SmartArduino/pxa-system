#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
libpxa_dir="$(cd "$script_dir/.." && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/libpxa-bench.XXXXXX")"
cc_bin="${CC:-cc}"
run_count="${PXA_BENCH_RUNS:-1}"
c_flags=(-std=c99 -O3 -DNDEBUG -Wall -Wextra -Werror -Wpedantic
         -I"$libpxa_dir/include" -I"$libpxa_dir/src")
core_sources=(
  "$libpxa_dir/src/common/bytes.c"
  "$libpxa_dir/src/core/wire.c"
  "$libpxa_dir/src/core/runtime.c"
  "$libpxa_dir/src/core/component.c"
  "$libpxa_dir/src/core/request.c"
  "$libpxa_dir/src/core/handle.c"
  "$libpxa_dir/src/core/event.c"
  "$libpxa_dir/src/core/service_registry.c"
)

trap 'rm -rf "$work_dir"' EXIT

"$cc_bin" "${c_flags[@]}" "$script_dir/runtime_bench.c" \
  "${core_sources[@]}" -o "$work_dir/native_c"

for ((run = 1; run <= run_count; ++run)); do
  echo "run $run"
  "$work_dir/native_c"
done
