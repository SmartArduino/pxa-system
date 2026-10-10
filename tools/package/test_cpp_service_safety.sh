#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <simulator-build-directory> [artifacts-directory]" >&2
  exit 2
fi
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
system_dir="$(cd "$script_dir/../.." && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-service-safety.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
artifacts="${2:-$work_dir/captures}"
mkdir -p "$artifacts"
key="${PXA_SIGNING_KEY:-$system_dir/apps/pxa/.dev-signing/publisher-private.pem}"
openssl pkey -in "$key" -pubout -outform DER -out "$work_dir/publisher.der"
PXA_APP_SOURCE_ROOT="$system_dir/sdk/guest-cpp/examples" \
PXA_SIGNING_KEY="$key" PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
  bash "$script_dir/package_app.sh" service-safety simulator "$work_dir/pxa-service-safety"
python3 "$system_dir/sdk/guest-cpp/examples/service-safety/validation/run_host.py" \
  --simulator-build "$1" --package "$work_dir/pxa-service-safety" \
  --publisher-key "$work_dir/publisher.der" --output "$artifacts"
