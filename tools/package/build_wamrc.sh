#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
readonly wamr_ref="main"
readonly wamr_commit="cd0497e26a7355973948f1fd903b049003cf0e85"
readonly llvm_repo="https://github.com/espressif/llvm-project.git"
readonly llvm_ref="xtensa_release_18.1.2"
readonly llvm_commit="344b928e67bfc1d07cb150609c55548e86a9bf19"
wamr_source_dir="${PXA_WAMR_SOURCE_DIR:-${PXA_WAMR_COMPILER_DIR:-$pxa_system_dir/wamr}}"
cache_dir="${PXA_WAMR_CACHE_DIR:-$pxa_system_dir/.pxa}"
build_dir="${PXA_WAMR_BUILD_DIR:-$cache_dir/wamrc-build-$wamr_commit-llvm-$llvm_commit}"
llvm_dir="${PXA_WAMR_LLVM_DIR:-$cache_dir/llvm-$llvm_commit}"
wamrc_bin="$build_dir/wamrc-2.4.3"
lock_file="${PXA_WAMR_LOCK_FILE:-$cache_dir/wamrc-$wamr_commit-llvm-$llvm_commit.lock}"
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

for command in git cmake ninja flock "$host_cc" "$host_cxx" lld; do
  require_command "$command"
done

case "$host_arch" in
  x86_64|amd64) host_wamr_target="X86_64" ;;
  *)
    say "Automatic wamrc bootstrap currently supports an x86_64 host, not $host_arch"
    exit 1
    ;;
esac

mkdir -p "$(dirname "$lock_file")"
exec 9>"$lock_file"
flock 9

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

if [[ -e "$wamr_source_dir" ]] \
    && ! git -C "$wamr_source_dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  say "PXA_WAMR_SOURCE_DIR is not a WAMR checkout: $wamr_source_dir"
  exit 1
fi

if ! git -C "$wamr_source_dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  say "WAMR source checkout is missing: $wamr_source_dir"
  say "Initialize pxa-system/wamr with: git submodule update --init pxa-system/wamr"
  exit 1
fi

actual_commit="$(git -C "$wamr_source_dir" rev-parse HEAD)"
if [[ "$actual_commit" != "$wamr_commit" ]]; then
  say "WAMR checkout is $actual_commit; expected $wamr_commit from $wamr_ref"
  exit 1
fi

llvm_build_dir="$llvm_dir/build"
llvm_config="$llvm_build_dir/lib/cmake/llvm/LLVMConfig.cmake"
llvm_required_libraries=(
  "$llvm_build_dir/lib/libLLVMCore.a"
  "$llvm_build_dir/lib/libLLVMAArch64CodeGen.a"
  "$llvm_build_dir/lib/libLLVMARMCodeGen.a"
  "$llvm_build_dir/lib/libLLVMMipsCodeGen.a"
  "$llvm_build_dir/lib/libLLVMRISCVCodeGen.a"
  "$llvm_build_dir/lib/libLLVMX86CodeGen.a"
  "$llvm_build_dir/lib/libLLVMXtensaCodeGen.a"
)

llvm_build_is_complete() {
  local library

  [[ -f "$llvm_config" ]] || return 1
  for library in "${llvm_required_libraries[@]}"; do
    [[ -f "$library" ]] || return 1
  done
}

if [[ -e "$llvm_dir" && ! -d "$llvm_dir/.git" ]]; then
  say "LLVM compiler source directory is not a checkout: $llvm_dir"
  exit 1
fi

if [[ ! -d "$llvm_dir/.git" ]]; then
  say "Cloning Espressif LLVM $llvm_ref at $llvm_commit"
  git clone --depth 1 "$llvm_repo" "$llvm_dir" >&2
  git -C "$llvm_dir" fetch --depth 1 origin "$llvm_commit" >&2
  git -C "$llvm_dir" checkout --detach FETCH_HEAD >&2
fi

actual_llvm_commit="$(git -C "$llvm_dir" rev-parse HEAD)"
if [[ "$actual_llvm_commit" != "$llvm_commit" ]]; then
  say "LLVM checkout is $actual_llvm_commit; expected $llvm_commit for WAMR $wamr_commit"
  exit 1
fi

if ! llvm_build_is_complete; then
  # WAMR's build_llvm.py repackages LLVM and drops static libraries referenced
  # by LLVMConfig.cmake. Keep the raw CMake output so wamrc can link all targets.
  say "Building the LLVM AOT backends for the x86_64 host wamrc; this is a one-time, lengthy host build"
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

if ! llvm_build_is_complete; then
  say "LLVM build completed without all configured AOT backends required by wamrc"
  exit 1
fi

say "Building x86_64 host wamrc"
cmake -S "$wamr_source_dir/wamr-compiler" -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR="$llvm_build_dir/lib/cmake/llvm" \
  -DWAMR_BUILD_TARGET="$host_wamr_target" >&2
cmake --build "$build_dir" --parallel "$build_jobs" >&2

if [[ ! -x "$wamrc_bin" ]]; then
  say "wamrc build completed without producing $wamrc_bin"
  exit 1
fi

printf '%s\n' "$wamrc_bin"
