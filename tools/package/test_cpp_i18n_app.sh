#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <pxsys_i18n_app_test>" >&2
  exit 2
fi
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-cpp-i18n.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
signing_key="${PXA_SIGNING_KEY:-$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem}"
openssl pkey -in "$signing_key" -pubout -outform DER -out "$work_dir/publisher.der"
PXA_APP_SOURCE_ROOT="$pxa_system_dir/sdk/guest-cpp/examples" \
PXA_SIGNING_KEY="$signing_key" PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
  bash "$script_dir/package_app.sh" i18n simulator "$work_dir/pxa-i18n"
"$1" "$work_dir/pxa-i18n" "$work_dir/publisher.der"
