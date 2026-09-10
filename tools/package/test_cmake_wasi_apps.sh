#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
test_apps_root="$pxa_system_dir/apps/tests/wasi"
wasi_sdk_dir="${WASI_SDK_DIR:-}"
wamrc_bin="${WAMRC:-}"

if [[ ! -x "$wasi_sdk_dir/bin/clang" ||
      ! -x "$wasi_sdk_dir/bin/llvm-nm" ]]; then
  echo "WASI_SDK_DIR must point to a complete WASI SDK" >&2
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

apps=(wasi-libc-lab wasi-system-lab cmake-components-lab wasi-undeclared-random)
for app in "${apps[@]}"; do
  PXA_APP_SOURCE_ROOT="$test_apps_root" \
  PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
  PXA_CONTAINER_OUTPUT="$work_dir/pxa-$app.pxa" \
  PXA_SIGNING_KEY="$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem" \
  WASI_SDK_DIR="$wasi_sdk_dir" \
  WAMRC="$wamrc_bin" \
    "$script_dir/package_app.sh" "$app" simulator "$work_dir/pxa-$app"
  test -s "$work_dir/pxa-$app/manifest.pxm"
  test -s "$work_dir/pxa-$app/signature.pxs"
  test -s "$work_dir/pxa-$app.pxa"
done

test -s "$work_dir/pxa-wasi-libc-lab/artifacts/main.linux-x86_64.aot"
test -s "$work_dir/pxa-wasi-system-lab/artifacts/main.linux-x86_64.aot"
test -s "$work_dir/pxa-cmake-components-lab/artifacts/main.linux-x86_64.aot"
test -s "$work_dir/pxa-cmake-components-lab/artifacts/responder.linux-x86_64.aot"
test -s "$work_dir/pxa-wasi-undeclared-random/artifacts/main.linux-x86_64.aot"
! test -e "$work_dir/pxa-wasi-libc-lab/artifacts/main.wasm"
! test -e "$work_dir/pxa-wasi-system-lab/artifacts/main.wasm"
! test -e "$work_dir/pxa-cmake-components-lab/artifacts/main.wasm"
! test -e "$work_dir/pxa-cmake-components-lab/artifacts/responder.wasm"
! test -e "$work_dir/pxa-wasi-undeclared-random/artifacts/main.wasm"

echo "PXA CMake/WASI test Apps OK"
