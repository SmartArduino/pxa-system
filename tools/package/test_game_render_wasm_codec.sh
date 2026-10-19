#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
system="$(cd "$script_dir/../.." && pwd)"
wasi_dir="${WASI_SDK_DIR:-$("$script_dir/resolve_wasi_sdk.sh")}"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-game-codec.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
cat > "$work_dir/run.cjs" <<'JS'
const {WASI}=require('node:wasi');const fs=require('node:fs');
const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{}});
WebAssembly.instantiate(fs.readFileSync(process.argv[2]),{wasi_snapshot_preview1:wasi.wasiImport}).then(({instance})=>wasi.start(instance));
JS
for packed in 0 1;do
    "$wasi_dir/bin/clang" -std=c11 -O2 -fno-builtin -Wno-ignored-attributes \
        "-DPXA_GAME_RENDER_WASM_PACKED_CODEC=$packed" \
        -I"$system/sdk/guest-c/include" -I"$system/sdk/testing" \
        "$system/sdk/guest-c/tests/pxa_game_spans_test.c" -o "$work_dir/c.wasm"
    node "$work_dir/run.cjs" "$work_dir/c.wasm"
    "$wasi_dir/bin/clang++" -std=c++2c -O2 -fno-builtin -fno-exceptions -fno-rtti \
        "-DPXA_GAME_RENDER_WASM_PACKED_CODEC=$packed" -I"$system/sdk/guest-cpp/include" \
        "$system/sdk/guest-cpp/tests/game_spans_wasm_codec_test.cpp" -o "$work_dir/cpp.wasm"
    node "$work_dir/run.cjs" "$work_dir/cpp.wasm"
done
echo 'PXA WASM GameRender C/C++ portable and packed codecs passed'
