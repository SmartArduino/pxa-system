#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
test_apps_root="$pxa_system_dir/apps/tests/wasi"
direct_apps_root="$pxa_system_dir/apps/tests"
wasi_sdk_dir="$("$script_dir/resolve_wasi_sdk.sh")"
wamrc_bin="${WAMRC:-}"

if [[ ! -x "$wasi_sdk_dir/bin/clang" ||
      ! -x "$wasi_sdk_dir/bin/llvm-nm" ]]; then
  echo "WASI SDK 34 must include clang and llvm-nm" >&2
  exit 2
fi
if [[ -z "$wamrc_bin" ]]; then
  wamrc_bin="$($script_dir/build_wamrc.sh)"
fi
if [[ ! -x "$wamrc_bin" ]]; then
  echo "WAMRC must point to an executable wamrc" >&2
  exit 2
fi

work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-cmake-wasi-apps.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

libc_lab="$test_apps_root/wasi-libc-lab"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
  -I"$libc_lab/include" \
  "$pxa_system_dir/sdk/guest-c/tests/wasi_libc_lab_test.c" \
  "$libc_lab/checks/memory_checks.c" \
  "$libc_lab/checks/runner.c" \
  "$libc_lab/checks/string_checks.c" \
  "$libc_lab/presentation/result_text.c" \
  -o "$work_dir/wasi_libc_lab_test"
"$work_dir/wasi_libc_lab_test"

apps=(wasi-libc-lab wasi-system-lab cmake-components-lab)
for app in "${apps[@]}"; do
  PXA_APP_SOURCE_ROOT="$test_apps_root" \
  PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
  PXA_CONTAINER_OUTPUT="$work_dir/pxa-$app.pxa" \
  PXA_SIGNING_KEY="$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem" \
  PXA_BUILD_CACHE_DIR="$work_dir/build-cache" \
  WASI_SDK_DIR="$wasi_sdk_dir" \
  WAMRC="$wamrc_bin" \
    "$script_dir/package_app.sh" "$app" simulator "$work_dir/pxa-$app"
  test -s "$work_dir/pxa-$app/manifest.pxm"
  test -s "$work_dir/pxa-$app/signature.pxs"
  test -s "$work_dir/pxa-$app.pxa"
done

# A libc Guest must share its allocator with WAMR's configuration/event
# buffers. Without these exports both heaps start at the compiled heap base.
"${PYTHON:-python3}" - "$work_dir/build-cache" "$script_dir" <<'PYTHON'
from pathlib import Path
import sys
sys.path.insert(0, sys.argv[2])
from verify_wasm_memory import read_u32_leb
from verify_wasm_core_imports import read_name

artifacts = [p for p in Path(sys.argv[1]).rglob("main.wasm")
             if "wasi-libc-lab-" in str(p)]
assert len(artifacts) == 1, artifacts
data = artifacts[0].read_bytes()
offset = 8
exports = set()
while offset < len(data):
    section = data[offset]
    size, offset = read_u32_leb(data, offset + 1)
    end = offset + size
    if section == 7:
        count, cursor = read_u32_leb(data, offset)
        for _ in range(count):
            name, cursor = read_name(data, cursor, end)
            cursor += 1
            _, cursor = read_u32_leb(data, cursor)
            exports.add(name)
    offset = end
assert {"malloc", "free"} <= exports, exports
PYTHON

find "$work_dir/build-cache" -name '*.obj' -printf '%p %T@\n' | LC_ALL=C sort \
  > "$work_dir/objects-before.txt"
test -s "$work_dir/objects-before.txt"
cp "$work_dir/pxa-cmake-components-lab/artifacts/main.linux-x86_64.aot" \
  "$work_dir/main-before.aot"
PXA_APP_SOURCE_ROOT="$test_apps_root" \
PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
PXA_BUILD_CACHE_DIR="$work_dir/build-cache" \
PXA_SIGNING_KEY="$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem" \
WASI_SDK_DIR="$wasi_sdk_dir" WAMRC="$wamrc_bin" \
  "$script_dir/package_app.sh" cmake-components-lab simulator \
    "$work_dir/pxa-cmake-components-lab"
find "$work_dir/build-cache" -name '*.obj' -printf '%p %T@\n' | LC_ALL=C sort \
  > "$work_dir/objects-after.txt"
cmp "$work_dir/objects-before.txt" "$work_dir/objects-after.txt"
cmp "$work_dir/main-before.aot" \
  "$work_dir/pxa-cmake-components-lab/artifacts/main.linux-x86_64.aot"

PXA_APP_SOURCE_ROOT="$test_apps_root" \
PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
PXA_BUILD_CACHE_DIR="$work_dir/build-cache" \
PXA_APP_DEFINES=PXA_TEST_CACHE_CHANGE=1 \
PXA_SIGNING_KEY="$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem" \
WASI_SDK_DIR="$wasi_sdk_dir" WAMRC="$wamrc_bin" \
  "$script_dir/package_app.sh" cmake-components-lab simulator \
    "$work_dir/pxa-cmake-components-lab"
find "$work_dir/build-cache" -name '*.obj' -printf '%p %T@\n' | LC_ALL=C sort \
  > "$work_dir/objects-changed.txt"
if cmp -s "$work_dir/objects-after.txt" "$work_dir/objects-changed.txt"; then
  echo "Changing PXA_APP_DEFINES did not rebuild cached objects" >&2
  exit 1
fi

test -s "$work_dir/pxa-wasi-libc-lab/artifacts/main.linux-x86_64.aot"
test -s "$work_dir/pxa-wasi-system-lab/artifacts/main.linux-x86_64.aot"
test -s "$work_dir/pxa-cmake-components-lab/artifacts/main.linux-x86_64.aot"
test -s "$work_dir/pxa-cmake-components-lab/artifacts/responder.linux-x86_64.aot"
! test -e "$work_dir/pxa-wasi-libc-lab/artifacts/main.wasm"
! test -e "$work_dir/pxa-wasi-system-lab/artifacts/main.wasm"
! test -e "$work_dir/pxa-cmake-components-lab/artifacts/main.wasm"
! test -e "$work_dir/pxa-cmake-components-lab/artifacts/responder.wasm"

if PXA_APP_SOURCE_ROOT="$test_apps_root" \
   PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
   PXA_SIGNING_KEY="$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem" \
   WASI_SDK_DIR="$wasi_sdk_dir" WAMRC="$wamrc_bin" \
   "$script_dir/package_app.sh" wasi-undeclared-random simulator \
     "$work_dir/pxa-wasi-undeclared-random" \
     >"$work_dir/undeclared-random.log" 2>&1; then
  echo "Undeclared WASI random import unexpectedly passed packaging" >&2
  exit 1
fi
grep -q "random_get requires features: random" \
    "$work_dir/undeclared-random.log"
! test -e "$work_dir/pxa-wasi-undeclared-random.pxa"

for jobs in 1 4; do
  PXA_APP_SOURCE_ROOT="$direct_apps_root" \
  PXA_PACKAGE_OUTPUT_ROOT="$work_dir/jobs-$jobs" \
  PXA_SIGNING_KEY="$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem" \
  PXA_BUILD_JOBS="$jobs" WASI_SDK_DIR="$wasi_sdk_dir" WAMRC="$wamrc_bin" \
    "$script_dir/package_app.sh" direct-parallel simulator \
      "$work_dir/jobs-$jobs/pxa-direct-parallel"
done
cmp "$work_dir/jobs-1/pxa-direct-parallel/artifacts/main.wasm" \
    "$work_dir/jobs-4/pxa-direct-parallel/artifacts/main.wasm"
cmp "$work_dir/jobs-1/pxa-direct-parallel/artifacts/main.linux-x86_64.aot" \
    "$work_dir/jobs-4/pxa-direct-parallel/artifacts/main.linux-x86_64.aot"
if PXA_APP_SOURCE_ROOT="$direct_apps_root" \
   PXA_PACKAGE_OUTPUT_ROOT="$work_dir/aot-failure" \
   PXA_SIGNING_KEY="$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem" \
   PXA_BUILD_JOBS=4 WASI_SDK_DIR="$wasi_sdk_dir" WAMRC=/bin/false \
   "$script_dir/package_app.sh" direct-parallel simulator \
     "$work_dir/aot-failure/pxa-direct-parallel" \
     >"$work_dir/aot-failure.log" 2>&1; then
  echo "Failed AoT job unexpectedly produced a package" >&2
  exit 1
fi
grep -q "PXA build failed: AOT main" "$work_dir/aot-failure.log"
! test -e "$work_dir/aot-failure/pxa-direct-parallel.pxa"

echo "PXA CMake/WASI test Apps OK"
