#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <pxsys_declarative_ui_test>" >&2
  exit 2
fi
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-cpp-declarative.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
signing_key="${PXA_SIGNING_KEY:-$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem}"
artifacts="${PXA_SDK_TEST_ARTIFACT_DIR:-$work_dir/captures}"
mkdir -p "$work_dir/state" "$artifacts"
openssl pkey -in "$signing_key" -pubout -outform DER -out "$work_dir/publisher.der"
PXA_APP_SOURCE_ROOT="$pxa_system_dir/sdk/guest-cpp/examples" \
PXA_SIGNING_KEY="$signing_key" PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
  bash "$script_dir/package_app.sh" declarative-ui simulator "$work_dir/pxa-declarative-ui"
"$1" "$work_dir/pxa-declarative-ui" "$work_dir/publisher.der" "$work_dir/state" "$artifacts"
