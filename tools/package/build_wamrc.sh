#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$script_dir/../../.." && pwd)"
readonly wamr_repo="https://github.com/espressif/wasm-micro-runtime.git"
readonly wamr_ref="esp_based_on_v2.4.0"
readonly wamr_commit="8f806e0f2c02a768d2c35044f2964f769612d9bc"
readonly llvm_repo="https://github.com/espressif/llvm-project.git"
readonly llvm_ref="xtensa_release_18.1.2"
readonly llvm_commit="344b928e67bfc1d07cb150609c55548e86a9bf19"
source_dir="${PXA_WAMR_COMPILER_DIR:-$project_dir/.pxa/wamr-$wamr_commit}"
build_dir="${PXA_WAMR_BUILD_DIR:-$source_dir/wamr-compiler/build}"
wamrc_bin="$build_dir/wamrc-2.4.0"
build_jobs="${PXA_WAMR_JOBS:-}"
host_arch="$(uname -m)"
host_cc="${PXA_WAMR_CC:-clang}"
host_cxx="${PXA_WAMR_CXX:-clang++}"
# LLVM 18's SmallVector header relied on an indirect <cstdint> include that
# is no longer provided by current Linux standard-library headers.
llvm_cxx_flags="-include cstdint"

say() {
  printf '%s\n' "$*" >&2
}

require_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    say "Missing required command: $1"
    exit 1
  fi
}

for command in git cmake ninja "$host_cc" "$host_cxx" lld; do
  require_command "$command"
done

case "$host_arch" in
  x86_64|amd64) host_wamr_target="X86_64" ;;
  *)
    say "Automatic wamrc bootstrap currently supports an x86_64 host, not $host_arch"
    exit 1
    ;;
esac

if [[ -z "$build_jobs" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    build_jobs="$(nproc)"
    if (( build_jobs > 1 )); then
      ((build_jobs -= 1))
    fi
  else
    build_jobs=1
  fi
fi

if [[ -x "$wamrc_bin" ]]; then
  printf '%s\n' "$wamrc_bin"
  exit 0
fi

if [[ -e "$source_dir" && ! -d "$source_dir/.git" ]]; then
  say "PXA_WAMR_COMPILER_DIR is not a WAMR checkout: $source_dir"
  exit 1
fi

if [[ ! -d "$source_dir/.git" ]]; then
  mkdir -p "$(dirname "$source_dir")"
  say "Cloning WAMR $wamr_ref at $wamr_commit"
  git clone --depth 1 --branch "$wamr_ref" "$wamr_repo" "$source_dir" >&2
fi

actual_commit="$(git -C "$source_dir" rev-parse HEAD)"
if [[ "$actual_commit" != "$wamr_commit" ]]; then
  say "WAMR checkout is $actual_commit; expected $wamr_commit for the ESP component"
  exit 1
fi

llvm_dir="$source_dir/core/deps/llvm"
llvm_build_dir="$llvm_dir/build"
llvm_core_library="$llvm_build_dir/lib/libLLVMCore.a"
llvm_xtensa_library="$llvm_build_dir/lib/libLLVMXtensaCodeGen.a"
llvm_tablegen_library="$llvm_build_dir/lib/libLLVMTableGenGlobalISel.a"

if [[ -e "$llvm_dir" && ! -d "$llvm_dir/.git" ]]; then
  say "LLVM compiler source directory is not a checkout: $llvm_dir"
  exit 1
fi

if [[ ! -d "$llvm_dir/.git" ]]; then
  say "Cloning Espressif LLVM $llvm_ref at $llvm_commit"
  git clone --depth 1 --branch "$llvm_ref" "$llvm_repo" "$llvm_dir" >&2
fi

actual_llvm_commit="$(git -C "$llvm_dir" rev-parse HEAD)"
if [[ "$actual_llvm_commit" != "$llvm_commit" ]]; then
  say "LLVM checkout is $actual_llvm_commit; expected $llvm_commit for WAMR $wamr_commit"
  exit 1
fi

if [[ ! -f "$llvm_core_library" || ! -f "$llvm_xtensa_library" || ! -f "$llvm_tablegen_library" ]]; then
  # WAMR's build_llvm.py repackages LLVM and drops static libraries referenced
  # by LLVMConfig.cmake. Keep the raw CMake output so wamrc can link all targets.
  rm -rf "$llvm_build_dir"
  say "Building the Xtensa LLVM backend for the x86_64 host wamrc; this is a one-time, lengthy host build"
  CC="$host_cc" CXX="$host_cxx" cmake -S "$llvm_dir/llvm" -B "$llvm_build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE:STRING=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DLLVM_APPEND_VC_REV:BOOL=ON \
    -DLLVM_BUILD_EXAMPLES:BOOL=OFF \
    -DLLVM_BUILD_LLVM_DYLIB:BOOL=OFF \
    -DLLVM_ENABLE_BINDINGS:BOOL=OFF \
    -DLLVM_ENABLE_IDE:BOOL=OFF \
    -DLLVM_ENABLE_LIBEDIT=OFF \
    -DLLVM_ENABLE_TERMINFO:BOOL=OFF \
    -DLLVM_ENABLE_ZLIB:BOOL=ON \
    -DLLVM_INCLUDE_BENCHMARKS:BOOL=OFF \
    -DLLVM_INCLUDE_DOCS:BOOL=OFF \
    -DLLVM_INCLUDE_EXAMPLES:BOOL=OFF \
    -DLLVM_INCLUDE_UTILS:BOOL=OFF \
    -DLLVM_INCLUDE_TESTS:BOOL=OFF \
    -DLLVM_OPTIMIZED_TABLEGEN:BOOL=ON \
    -DLLVM_CCACHE_BUILD:BOOL=ON \
    -DLLVM_USE_LINKER:STRING=lld \
    -DLLVM_ENABLE_LIBXML2:BOOL=OFF \
    -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD:STRING=Xtensa \
    '-DLLVM_TARGETS_TO_BUILD:STRING=AArch64;ARM;Mips;RISCV;X86' \
    -DLLVM_INCLUDE_TOOLS:BOOL=OFF \
    "-DCMAKE_CXX_FLAGS:STRING=$llvm_cxx_flags" >&2
  cmake --build "$llvm_build_dir" --parallel "$build_jobs" >&2
fi

if [[ ! -f "$llvm_core_library" || ! -f "$llvm_xtensa_library" || ! -f "$llvm_tablegen_library" ]]; then
  say "LLVM build completed without all static libraries required by wamrc"
  exit 1
fi

say "Building x86_64 host wamrc"
cmake -S "$source_dir/wamr-compiler" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release -DWAMR_BUILD_TARGET="$host_wamr_target" >&2
cmake --build "$build_dir" --parallel "$build_jobs" >&2

if [[ ! -x "$wamrc_bin" ]]; then
  say "wamrc build completed without producing $wamrc_bin"
  exit 1
fi

printf '%s\n' "$wamrc_bin"
