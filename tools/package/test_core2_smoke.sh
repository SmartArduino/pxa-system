#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <product-simulator>" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
system_dir="$(cd "$script_dir/../.." && pwd)"
app_root="$system_dir/simulator/desktop/tests/apps"
simulator="$(realpath -- "$1")"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-core2-smoke.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
signing_key="$work_dir/publisher-private.pem"
publisher_key="$work_dir/publisher-public.der"
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$signing_key"
openssl pkey -in "$signing_key" -pubout -outform DER -out "$publisher_key"
publisher_id="$(sha256sum "$publisher_key" | cut -d ' ' -f1)"

PXA_SIGNING_KEY="$signing_key" \
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
   ! grep -Fq 'Core 2 signed Guest reached Host' "$work_dir/run.log" || \
   ! grep -Fq 'Core 2 17 token completions and reuse OK' "$work_dir/run.log" || \
   ! grep -Fq 'Core 2 GameRender 64-bit Handle IO and stale rejection OK' \
       "$work_dir/run.log" || \
   ! grep -Fq 'Core 2 Window snapshot and cross-service token OK' \
      "$work_dir/run.log" || \
   ! grep -Fq 'Core 2 Window configure and toast OK' \
      "$work_dir/run.log" || \
   ! grep -Fq 'Core 2 Device MAC scoped native Handle and stale rejection OK' \
       "$work_dir/run.log" || \
   ! grep -Fq 'Core 2 Storage max value roundtrip and lifecycle OK' \
       "$work_dir/run.log" || \
   ! grep -Fq 'Core 2 FS native Handle and file lifecycle OK' \
       "$work_dir/run.log" || \
   ! grep -Fq 'Core 2 Clock reserved completion OK' \
       "$work_dir/run.log" || \
   ! grep -Fq 'Core 2 UI theme completion OK' \
       "$work_dir/run.log" || \
   [[ ! -d "$work_dir/pxa-abi-v1-smoke.state/app-data/$publisher_id-pxa-abi-v1-smoke" ]] || \
   [[ ! -d "$work_dir/pxa-abi-v1-smoke.state/private-files/$publisher_id-pxa-abi-v1-smoke" ]]; then
  cat "$work_dir/build.log" >&2
  cat "$work_dir/run.log" >&2
  echo "Core 2 signed Guest pressure test failed (simulator status $run_status)" >&2
  exit 1
fi
echo 'Core 2 signed Guest pressure test OK'
