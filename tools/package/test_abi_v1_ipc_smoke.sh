#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <product-simulator>" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
system_dir="$(cd "$script_dir/../.." && pwd)"
app_root="$(realpath -m -- "${PXA_APP_SOURCE_ROOT:-$system_dir/../../local/pxa-apps}")"
simulator="$(realpath -- "$1")"
publisher_key="$app_root/.dev-signing/publisher-public.der"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-abi-v1-ipc.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

PXA_APP_SOURCE_ROOT="$app_root" PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
  bash "$script_dir/package_app.sh" abi-v1-ipc-smoke simulator \
  "$work_dir/pxa-abi-v1-ipc-smoke" >"$work_dir/build.log" 2>&1 || {
    cat "$work_dir/build.log" >&2
    exit 1
  }

set +e
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 6s "$simulator" \
  --package "$work_dir/pxa-abi-v1-ipc-smoke" \
  --publisher-key "$publisher_key" \
  --pxadb-control-socket "$work_dir/control.sock" \
  >"$work_dir/run.log" 2>&1
run_status=$?
set -e

if [[ "$run_status" -ne 0 && "$run_status" -ne 124 ]] || \
   ! grep -Fq 'ABI v1 IPC two-component exchange OK' "$work_dir/run.log"; then
  cat "$work_dir/build.log" >&2
  cat "$work_dir/run.log" >&2
  echo "ABI v1 signed IPC smoke test failed (simulator status $run_status)" >&2
  exit 1
fi
echo 'ABI v1 signed IPC smoke test OK'
