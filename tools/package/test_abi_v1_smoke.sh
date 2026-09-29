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
publisher_id="$(sha256sum "$publisher_key" | cut -d ' ' -f1)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-abi-v1-smoke.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

PXA_APP_SOURCE_ROOT="$app_root" PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
  bash "$script_dir/package_app.sh" abi-v1-smoke simulator \
  "$work_dir/pxa-abi-v1-smoke" >"$work_dir/build.log" 2>&1 || {
    cat "$work_dir/build.log" >&2
    exit 1
  }

set +e
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 6s "$simulator" \
  --package "$work_dir/pxa-abi-v1-smoke" \
  --publisher-key "$publisher_key" \
  --pxadb-control-socket "$work_dir/control.sock" \
  >"$work_dir/run.log" 2>&1
run_status=$?
set -e

if [[ "$run_status" -ne 0 && "$run_status" -ne 124 ]] || \
   ! grep -Fq 'ABI v1 signed Guest reached Host' "$work_dir/run.log" || \
   ! grep -Fq 'ABI v1 17 token completions and reuse OK' "$work_dir/run.log" || \
   ! grep -Fq 'ABI v1 GameRender 64-bit Handle IO and stale rejection OK' \
       "$work_dir/run.log" || \
   ! grep -Fq 'ABI v1 Window snapshot and cross-service token OK' \
      "$work_dir/run.log" || \
   ! grep -Fq 'ABI v1 Window configure and toast OK' \
      "$work_dir/run.log" || \
   ! grep -Fq 'ABI v1 Device MAC scoped native Handle and stale rejection OK' \
       "$work_dir/run.log" || \
   ! grep -Fq 'ABI v1 Storage max value roundtrip and lifecycle OK' \
       "$work_dir/run.log" || \
   ! grep -Fq 'ABI v1 FS native Handle and file lifecycle OK' \
       "$work_dir/run.log" || \
   ! grep -Fq 'ABI v1 Clock reserved completion OK' \
       "$work_dir/run.log" || \
   ! grep -Fq 'ABI v1 UI theme completion OK' \
       "$work_dir/run.log" || \
   [[ ! -d "$work_dir/pxa-abi-v1-smoke.state/app-data/$publisher_id-pxa-abi-v1-smoke" ]] || \
   [[ ! -d "$work_dir/pxa-abi-v1-smoke.state/private-files/$publisher_id-pxa-abi-v1-smoke" ]]; then
  cat "$work_dir/build.log" >&2
  cat "$work_dir/run.log" >&2
  echo "ABI v1 signed Guest pressure test failed (simulator status $run_status)" >&2
  exit 1
fi
echo 'ABI v1 signed Guest pressure test OK'
