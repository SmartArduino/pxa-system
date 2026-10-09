#!/usr/bin/env bash
# Run in Ubuntu 22.04 with native SDL/media, Clang 14/lld 14 and CMake/Ninja.
set -euo pipefail
if [[ $# != 4 ]]; then
  echo "Usage: $0 WORKSPACE OUTPUT LLVM_SOURCE LVGL_SOURCE" >&2
  exit 2
fi
pxa_workspace="$(realpath -- "$1")"
pxa_output="$(realpath -m -- "$2")"
pxa_llvm="$(realpath -- "$3")"
pxa_lvgl="$(realpath -- "$4")"
pxa_source="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pxa_jobs="${PXA_RELEASE_JOBS:-8}"
[[ "$(getconf GNU_LIBC_VERSION)" == 'glibc 2.35' ]] || {
  echo 'Release binaries must be built against the glibc 2.35 baseline' >&2; exit 1;
}
[[ "$pxa_output/" != "$pxa_source/"* && "$pxa_output/" != "$pxa_llvm/"* ]] || {
  echo 'Build output must be outside source directories' >&2; exit 1;
}
python3 "$pxa_source/tools/release/metadata.py" check
pxa_expected_llvm="$(python3 "$pxa_source/tools/wamr/metadata.py" llvm.commit)"
pxa_expected_wamr="$(python3 "$pxa_source/tools/wamr/metadata.py" commit)"
[[ "$(git -C "$pxa_llvm" rev-parse HEAD)" == "$pxa_expected_llvm" ]]
[[ "$(git -C "$pxa_source/wamr" rev-parse HEAD)" == "$pxa_expected_wamr" ]]
mkdir -p "$pxa_output"
cmake -S "$pxa_source/simulator/desktop" -B "$pxa_output/simulator" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DPXSYS_LVGL_SOURCE_DIR="$pxa_lvgl" \
  -DPXSYS_DESKTOP_TEXT_FONT= -DPXSYS_BUILD_TESTS=OFF
cmake --build "$pxa_output/simulator" --parallel "$pxa_jobs"
PXA_SIMULATOR_FONT="$pxa_workspace/factory/base/system/fonts/noto_sans_cjk_common.ttf" \
  ctest --test-dir "$pxa_output/simulator" --output-on-failure
cmake -S "$pxa_llvm/llvm" -B "$pxa_output/llvm" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-14 -DCMAKE_CXX_COMPILER=clang++-14 \
  -DLLVM_USE_LINKER=lld-14 '-DLLVM_TARGETS_TO_BUILD=RISCV;X86' \
  -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=Xtensa -DLLVM_ENABLE_PROJECTS= \
  -DLLVM_INCLUDE_TOOLS=OFF -DLLVM_INCLUDE_UTILS=OFF -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_ZSTD=ON
cmake --build "$pxa_output/llvm" --parallel "$pxa_jobs"
cmake -S "$pxa_source/wamr/wamr-compiler" -B "$pxa_output/wamrc" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-14 -DCMAKE_CXX_COMPILER=clang++-14 \
  -DWAMR_BUILD_WITH_CUSTOM_LLVM=1 -DWAMR_BUILD_TARGET=X86_64 \
  -DLLVM_DIR="$pxa_output/llvm/lib/cmake/llvm"
cmake --build "$pxa_output/wamrc" --parallel "$pxa_jobs"
python3 "$pxa_source/tools/release/collect_linux_runtime.py" \
  --output "$pxa_output/host-runtime" "$pxa_output/wamrc/wamrc" \
  "$pxa_output/simulator/pxsys_desktop_simulator" \
  "$pxa_output/simulator/pxsys_product_simulator" \
  "$pxa_output/simulator/pxsys_package_installer"
