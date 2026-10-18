#!/usr/bin/env bash
set -euo pipefail
test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
test_build="$(mktemp -d)"
trap 'rm -rf "$test_build"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -DWASM_MEM_EXEC_IN_PSRAM=0 -DWASM_MEM_DUAL_BUS_MIRROR=0 \
  -I"$test_dir/stubs" "$test_dir/memmap_test.c" \
  "$test_dir/../overrides/espidf_memmap.c" -o "$test_build/memmap_test"
"$test_build/memmap_test"
