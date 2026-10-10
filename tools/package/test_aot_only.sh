#!/usr/bin/env bash
# Exercise the compiler, signed manifest and container with mixed component modes.
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-aot-only.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
mkdir -p "$work_dir/source"
cp -R "$pxa_system_dir/apps/tests/wasi/cmake-components-lab" "$work_dir/source/"
"${PYTHON:-python3}" - "$work_dir/source/cmake-components-lab/package.json" <<'PY'
import json, sys
from pathlib import Path
path = Path(sys.argv[1])
metadata = json.loads(path.read_text())
metadata['components'][0]['artifact'] = 'both'
metadata['components'][1]['artifact'] = 'wasm'
path.write_text(json.dumps(metadata))
PY
export PXA_APP_SOURCE_ROOT="$work_dir/source"
export PXA_BUILD_CACHE_DIR="$work_dir/cache"
export PXA_SIGNING_KEY="$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem"
unset PXA_PACKAGE_ARTIFACT_MODE
default_package="$work_dir/default/pxa-cmake-components-lab"
aot_package="$work_dir/aot-only/pxa-cmake-components-lab"
export PXA_PACKAGE_OUTPUT_ROOT="$work_dir/default"
"$script_dir/package_app.sh" cmake-components-lab simulator "$default_package"
test -s "$default_package/artifacts/main.wasm"
test -s "$default_package/artifacts/main.linux-x86_64.aot"
test -s "$default_package/artifacts/responder.wasm"
! test -e "$default_package/artifacts/responder.linux-x86_64.aot"
export PXA_PACKAGE_ARTIFACT_MODE=aot
export PXA_PACKAGE_OUTPUT_ROOT="$work_dir/aot-only"
"$script_dir/package_app.sh" cmake-components-lab simulator "$aot_package"
test -s "$aot_package/artifacts/main.linux-x86_64.aot"
! test -e "$aot_package/artifacts/main.wasm"
test -s "$aot_package/artifacts/responder.wasm"
! test -e "$aot_package/artifacts/responder.linux-x86_64.aot"
cmp "$default_package/artifacts/main.linux-x86_64.aot" \
    "$aot_package/artifacts/main.linux-x86_64.aot"
# The legacy manifest option must agree with the compiler's artifact selection.
engine_abi="$("${PYTHON:-python3}" "$pxa_system_dir/tools/wamr/metadata.py" engine_abi)"
"${PYTHON:-python3}" "$script_dir/build_package_manifest.py" \
    "$work_dir/source/cmake-components-lab/package.json" "$aot_package" \
    "$PXA_SIGNING_KEY" linux-x86_64 "$engine_abi" --aot-only
echo "AOT-only packaging: explicit both stripped, Wasm-only component retained, unchanged AOT and signed manifest passed"
