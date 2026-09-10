#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$script_dir/../../.." && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-package-tool-test.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
package_dir="$work_dir/package"
mkdir -p "$package_dir/artifacts" "$package_dir/assets/flappy-bird" \
  "$work_dir/i18n" "$work_dir/generated/arcade" \
  "$work_dir/generated/weather"

"${PYTHON:-python3}" "$project_dir/pxa-system/tools/i18n/compile_catalog.py" \
  "$project_dir/apps/pxa/arcade/i18n/messages.yaml" \
  "$project_dir/apps/pxa/arcade/i18n/zh-CN.yaml" \
  --output "$work_dir/generated/arcade/pxa_app_messages.h"
"${PYTHON:-python3}" "$project_dir/pxa-system/tools/i18n/compile_catalog.py" \
  "$project_dir/apps/pxa/weather/i18n/messages.yaml" \
  "$project_dir/apps/pxa/weather/i18n/zh-CN.yaml" \
  --output "$work_dir/generated/weather/pxa_app_messages.h"

clang --target=wasm32-unknown-unknown -O2 -fno-builtin -nostdlib \
  -I"$project_dir/pxa-system/sdk/guest-c/include" \
  -I"$project_dir/apps/pxa/common" \
  -I"$work_dir/generated/arcade" \
  -Wl,--no-entry \
  -Wl,--allow-undefined-file="$project_dir/pxa-system/sdk/guest-c/pxa-imports.txt" \
  -Wl,--export=pxa_app_on_event -Wl,--export=pxa_app_stop \
  -DPXA_ARCADE_STANDALONE_TEST \
  "$project_dir/apps/pxa/arcade/modules/tetris.c" \
  -o "$package_dir/artifacts/main.wasm"

# Clang recognizes Weather's bounded string loop as strlen at -O2. The direct
# builder must keep it self-contained instead of creating an ambient env import.
clang --target=wasm32-unknown-unknown -O2 -fno-builtin -nostdlib \
  -I"$project_dir/pxa-system/sdk/guest-c/include" \
  -I"$project_dir/apps/pxa/common" \
  -I"$work_dir/generated/weather" \
  -Wl,--no-entry \
  -Wl,--allow-undefined-file="$project_dir/pxa-system/sdk/guest-c/pxa-imports.txt" \
  -Wl,--export=pxa_app_on_event -Wl,--export=pxa_app_stop \
  "$project_dir/apps/pxa/weather/main.c" \
  -o "$work_dir/weather-import-regression.wasm"

# The manifest layer inventories opaque Artifact bytes. WAMR format validation
# belongs to ComponentEngine, so a copy is sufficient for this package test.
cp "$package_dir/artifacts/main.wasm" \
   "$package_dir/artifacts/main.linux-x86_64.aot"
cp "$package_dir/artifacts/main.wasm" \
   "$package_dir/artifacts/main.esp32-s3.aot"
cp "$package_dir/artifacts/main.wasm" \
   "$package_dir/artifacts/responder.wasm"
cp "$project_dir/apps/pxa/arcade/assets/flappy-bird/icon.png" \
   "$package_dir/assets/flappy-bird/icon.png"
cp "$project_dir/apps/pxa/garden-guard/assets/SOURCES.md" \
   "$package_dir/assets/SOURCES.md"
cp "$project_dir/apps/pxa/arcade/i18n/messages.yaml" \
   "$project_dir/apps/pxa/arcade/i18n/zh-CN.yaml" \
   "$work_dir/i18n/"

metadata="$work_dir/package.json"
sed '/^}/i\\  ,"services": ["fs","net"]\n  ,"components": [{"id": "main", "kind": "ui", "wasi": {"version": "preview1", "libc": "wasi-libc", "features": ["monotonic-clock", "stdio"]}}, {"id": "responder", "kind": "service", "artifact": "wasm", "services": ["ipc"]}]\n  ,"permissions": [{"name": "net.client", "required": false, "scope": "api.example"}]\n  ,"ipc_endpoints": [{"name": "demo.echo", "component": "responder"}]' \
  "$project_dir/apps/pxa/arcade/package.json" > "$metadata"

"${PYTHON:-python3}" "$script_dir/build_package_manifest.py" \
  "$metadata" "$package_dir" \
  "$project_dir/apps/pxa/.dev-signing/publisher-private.pem" \
  linux-x86_64 wamr-2.4.3-aot-v1-pxa-core-1

invalid_metadata="$work_dir/package-invalid.json"
sed 's/"services": \["fs","net"\]/"services": [5]/' "$metadata" > "$invalid_metadata"
if "${PYTHON:-python3}" "$script_dir/build_package_manifest.py" \
  "$invalid_metadata" "$package_dir" \
  "$project_dir/apps/pxa/.dev-signing/publisher-private.pem" \
  linux-x86_64 wamr-2.4.3-aot-v1-pxa-core-1 >/dev/null 2>&1; then
  echo "numeric service IDs must be rejected" >&2
  exit 1
fi

invalid_wasi_metadata="$work_dir/package-invalid-wasi.json"
sed 's/"monotonic-clock", "stdio"/"private-fs", "unknown"/' \
  "$metadata" > "$invalid_wasi_metadata"
if "${PYTHON:-python3}" "$script_dir/build_package_manifest.py" \
  "$invalid_wasi_metadata" "$package_dir" \
  "$project_dir/apps/pxa/.dev-signing/publisher-private.pem" \
  linux-x86_64 wamr-2.4.3-aot-v1-pxa-core-1 >/dev/null 2>&1; then
  echo "unknown WASI features must be rejected" >&2
  exit 1
fi

openssl_flags=($(pkg-config --cflags --libs openssl))
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -Wpedantic \
  -I"$project_dir/pxa-system/libpxa/include" \
  -I"$project_dir/pxa-system/libpxa/src" \
  -I"$project_dir/pxa-system/libpxa/adapters/include" \
  "$project_dir/pxa-system/libpxa/src/common/bytes.c" \
  "$project_dir/pxa-system/libpxa/src/core/wire.c" \
  "$project_dir/pxa-system/libpxa/src/package/manifest.c" \
  "$project_dir/pxa-system/libpxa/src/package/manifest_decode.c" \
  "$project_dir/pxa-system/libpxa/src/package/manifest_security.c" \
  "$project_dir/pxa-system/libpxa/src/package/inventory.c" \
  "$project_dir/pxa-system/libpxa/src/package/artifact_select.c" \
  "$project_dir/pxa-system/libpxa/adapters/openssl/pxa_openssl.c" \
  "$project_dir/components/pxa/tests/package_tool_test.c" \
  "${openssl_flags[@]}" -o "$work_dir/package_tool_test"

"$work_dir/package_tool_test" "$package_dir" \
  "$project_dir/apps/pxa/.dev-signing/publisher-public.der"
echo "PXA Package generator verification OK"
