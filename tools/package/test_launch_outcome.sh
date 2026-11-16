#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 2 ]]; then
  echo "Usage: $0 <desktop-simulator> <package-installer>" >&2
  exit 2
fi
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
system_dir="$(cd "$script_dir/../.." && pwd)"
artifact_root="${PXA_TEST_ARTIFACT_ROOT:-$system_dir/../../local/artifacts/launch-outcome}"
mkdir -p "$artifact_root"
work="$(mktemp -d "$artifact_root/launch-XXXXXX")"
key="$work/publisher.pem"
trap 'rm -f "$key"' EXIT
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$key"
openssl pkey -in "$key" -pubout -outform DER -out "$work/publisher.der"
for fail in 0 1; do
  trial="$work/$fail"
  mkdir -p "$trial/state"
  PXA_SIGNING_KEY="$key" PXA_APP_DEFINES="PXA_TEST_START_FAIL=$fail" \
    PXA_APP_SOURCE_ROOT="$system_dir/simulator/desktop/tests/apps" \
    PXA_PACKAGE_OUTPUT_ROOT="$trial" \
    bash "$script_dir/package_app.sh" launch-outcome simulator \
      "$trial/pxa-launch-outcome" >"$trial/build.log" 2>&1 || {
        cat "$trial/build.log" >&2; exit 1;
      }
  "$2" --storage-root "$trial/state" --publisher-key "$work/publisher.der" \
    --source "$trial/pxa-launch-outcome.pxa" \
    --expected-id pxa-launch-outcome >"$trial/install.log" 2>&1 || {
      cat "$trial/install.log" >&2; exit 1;
    }
  status=0
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 12s "$1" \
    --installed-packages-root "$trial/state/packages" \
    --state-root "$trial/state" --publisher-key "$work/publisher.der" \
    --launch pxa-launch-outcome --duration-ms 400 \
    >"$trial/run.log" 2>&1 || status=$?
  if [[ "$status" -ne "$fail" ]] || \
      { [[ "$fail" -eq 1 ]] && ! grep -Fq 'main component start status=-3' "$trial/run.log"; }; then
    cat "$trial/run.log" >&2
    echo "Expected launch exit $fail, got $status" >&2
    exit 1
  fi
done
echo 'Signed Guest launch: successful start exits0; rejected start exits1'
