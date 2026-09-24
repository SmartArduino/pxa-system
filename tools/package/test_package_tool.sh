#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
app_source_root="$(realpath -m -- "${PXA_APP_SOURCE_ROOT:-$pxa_system_dir/../../local/pxa-apps}")"
if [[ ! -f "$app_source_root/arcade/package.json" ]]; then
  echo "PXA_APP_SOURCE_ROOT must point to the separate pxa-apps checkout" >&2
  exit 2
fi
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-package-tool-test.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
"${PYTHON:-python3}" "$script_dir/test_verify_wasm_memory.py"
engine_abi="$("${PYTHON:-python3}" "$pxa_system_dir/tools/wamr/metadata.py" engine_abi)"
package_dir="$work_dir/package"
mkdir -p "$package_dir/artifacts" "$package_dir/assets/flappy-bird" \
  "$work_dir/i18n" "$work_dir/generated/arcade" \
  "$work_dir/generated/weather"

"${PYTHON:-python3}" "$pxa_system_dir/tools/i18n/compile_catalog.py" \
  "$app_source_root/arcade/i18n/messages.yaml" \
  "$app_source_root/arcade/i18n/zh-CN.yaml" \
  --output "$work_dir/generated/arcade/pxa_app_messages.h"
"${PYTHON:-python3}" "$pxa_system_dir/tools/i18n/compile_catalog.py" \
  "$app_source_root/weather/i18n/messages.yaml" \
  "$app_source_root/weather/i18n/zh-CN.yaml" \
  --output "$work_dir/generated/weather/pxa_app_messages.h"

clang --target=wasm32-unknown-unknown -O2 -fno-builtin -nostdlib \
  -I"$pxa_system_dir/sdk/guest-c/include" \
  -I"$app_source_root/common" \
  -I"$work_dir/generated/arcade" \
  -Wl,--no-entry \
  -Wl,--allow-undefined-file="$pxa_system_dir/sdk/guest-c/pxa-imports.txt" \
  -Wl,--export=pxa_app_on_event -Wl,--export=pxa_app_stop \
  -DPXA_ARCADE_STANDALONE_TEST \
  "$app_source_root/arcade/modules/tetris.c" \
  -o "$package_dir/artifacts/main.wasm"

# Clang recognizes Weather's bounded string loop as strlen at -O2. The direct
# builder must keep it self-contained instead of creating an ambient env import.
clang --target=wasm32-unknown-unknown -O2 -fno-builtin -nostdlib \
  -I"$pxa_system_dir/sdk/guest-c/include" \
  -I"$app_source_root/common" \
  -I"$work_dir/generated/weather" \
  -Wl,--no-entry \
  -Wl,--allow-undefined-file="$pxa_system_dir/sdk/guest-c/pxa-imports.txt" \
  -Wl,--export=pxa_app_on_event -Wl,--export=pxa_app_stop \
  "$app_source_root/weather/main.c" \
  -o "$work_dir/weather-import-regression.wasm"

# The manifest layer inventories opaque Artifact bytes. WAMR format validation
# belongs to ComponentEngine, so a copy is sufficient for this package test.
cp "$package_dir/artifacts/main.wasm" \
   "$package_dir/artifacts/main.linux-x86_64.aot"
cp "$package_dir/artifacts/main.wasm" \
   "$package_dir/artifacts/main.esp32-s3.aot"
cp "$package_dir/artifacts/main.wasm" \
   "$package_dir/artifacts/responder.wasm"
cp "$app_source_root/arcade/assets/flappy-bird/icon.png" \
   "$package_dir/assets/flappy-bird/icon.png"
cp "$app_source_root/garden-guard/assets/SOURCES.md" \
   "$package_dir/assets/SOURCES.md"
cp "$app_source_root/arcade/i18n/messages.yaml" \
   "$app_source_root/arcade/i18n/zh-CN.yaml" \
   "$work_dir/i18n/"

metadata="$work_dir/package.json"
sed '/^}/i\\  ,"build": {"system": "direct", "linear_memory": {"maximum_bytes": 65536, "pinned": true}}\n  ,"services": ["fs", {"name": "net", "min_version": [0, 1], "max_version": [0, 4]}, {"name": "ui", "features": ["canvas"]}]\n  ,"components": [{"id": "main", "kind": "ui", "wasi": {"version": "preview1", "libc": "wasi-libc", "features": ["monotonic-clock", "stdio"]}}, {"id": "responder", "kind": "service", "artifact": "wasm", "services": ["ipc"]}]\n  ,"permissions": [{"name": "net.client", "required": false, "scope": "api.example"}]\n  ,"ipc_endpoints": [{"name": "demo.echo", "component": "responder"}]' \
  "$app_source_root/arcade/package.json" > "$metadata"

"${PYTHON:-python3}" "$script_dir/build_package_manifest.py" \
  "$metadata" "$package_dir" \
  "$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem" \
  linux-x86_64 "$engine_abi"

invalid_metadata="$work_dir/package-invalid.json"
sed 's/"services": \["fs", {"name": "net", "min_version": \[0, 1\], "max_version": \[0, 4\]}, {"name": "ui", "features": \["canvas"\]}\]/"services": [5]/' "$metadata" > "$invalid_metadata"
if "${PYTHON:-python3}" "$script_dir/build_package_manifest.py" \
  "$invalid_metadata" "$package_dir" \
  "$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem" \
  linux-x86_64 "$engine_abi" >/dev/null 2>&1; then
  echo "numeric service IDs must be rejected" >&2
  exit 1
fi

invalid_core_metadata="$work_dir/package-invalid-core.json"
sed 's/"services": \["fs", {"name": "net", "min_version": \[0, 1\], "max_version": \[0, 4\]}, {"name": "ui", "features": \["canvas"\]}\]/"services": ["core"]/' \
  "$metadata" > "$invalid_core_metadata"
if "${PYTHON:-python3}" "$script_dir/build_package_manifest.py" \
  "$invalid_core_metadata" "$package_dir" \
  "$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem" \
  linux-x86_64 "$engine_abi" >/dev/null 2>&1; then
  echo "Core must be declared through min_sdk, not services" >&2
  exit 1
fi

invalid_wasi_metadata="$work_dir/package-invalid-wasi.json"
sed 's/"monotonic-clock", "stdio"/"private-fs", "unknown"/' \
  "$metadata" > "$invalid_wasi_metadata"
if "${PYTHON:-python3}" "$script_dir/build_package_manifest.py" \
  "$invalid_wasi_metadata" "$package_dir" \
  "$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem" \
  linux-x86_64 "$engine_abi" >/dev/null 2>&1; then
  echo "unknown WASI features must be rejected" >&2
  exit 1
fi

openssl_flags=($(pkg-config --cflags --libs openssl))
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -Wpedantic \
  -DPXSYS_WAMR_ENGINE_ABI=\"$engine_abi\" \
  -I"$pxa_system_dir/libpxa/include" \
  -I"$pxa_system_dir/libpxa/src" \
  -I"$pxa_system_dir/libpxa/adapters/include" \
  "$pxa_system_dir/libpxa/src/common/bytes.c" \
  "$pxa_system_dir/libpxa/src/core/wire.c" \
  "$pxa_system_dir/libpxa/src/package/manifest.c" \
  "$pxa_system_dir/libpxa/src/package/manifest_decode.c" \
  "$pxa_system_dir/libpxa/src/package/manifest_security.c" \
  "$pxa_system_dir/libpxa/src/package/inventory.c" \
  "$pxa_system_dir/libpxa/src/package/artifact_select.c" \
  "$pxa_system_dir/libpxa/adapters/openssl/pxa_openssl.c" \
  "$pxa_system_dir/libpxa/adapters/tests/package_tool_test.c" \
  "${openssl_flags[@]}" -o "$work_dir/package_tool_test"

"$work_dir/package_tool_test" "$package_dir" \
  "$pxa_system_dir/apps/pxa/.dev-signing/publisher-public.der"
echo "PXA Package generator verification OK"
