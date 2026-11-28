#!/usr/bin/env bash
set -euo pipefail

test_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
root=$(cd "$test_dir/../../../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
python3 "$root/tools/wamr/prepare_overlay.py" --source "$root/wamr" --output "$build/overlay"
clang --target=riscv32-linux-gnu -march=rv32imafc -mabi=ilp32f \
    -O2 -ffreestanding -fno-stack-protector -fno-pie -nostdlib -static -fuse-ld=lld -Wl,-e,_start \
    "$test_dir/riscv_native_test.c" "$build/overlay/core/iwasm/common/arch/invokeNative_riscv.S" \
    -o "$build/riscv_native_test"
qemu=$(command -v qemu-riscv32 || command -v qemu-riscv32-static)
"$qemu" "$build/riscv_native_test"
echo "RV32 native stack alignment, register arguments, and spill arguments passed"
