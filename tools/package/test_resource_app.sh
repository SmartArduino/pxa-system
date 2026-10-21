#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Usage: $0 <resource-app-test> [exit-loading|exit-reading|exit-sound-loading|storage-steady|storage-stall|storage-exit]" >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
system_dir="$(cd "$script_dir/../.." && pwd)"
runner="$(realpath -- "$1")"
shift
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-resource-app.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
signing_key="$work_dir/publisher-private.pem"
publisher_key="$work_dir/publisher-public.der"
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$signing_key"
openssl pkey -in "$signing_key" -pubout -outform DER -out "$publisher_key"

PXA_SIGNING_KEY="$signing_key" \
PXA_APP_SOURCE_ROOT="$system_dir/simulator/desktop/tests/apps" \
PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
  bash "$script_dir/package_app.sh" resource-scenes simulator \
  "$work_dir/pxa-resource-scenes" >"$work_dir/build.log" 2>&1 || {
    cat "$work_dir/build.log" >&2
    exit 1
  }

"$runner" "$work_dir/pxa-resource-scenes" "$publisher_key" "$@"
