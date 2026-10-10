#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <pxsys_sdk_patterns_test>" >&2
  exit 2
fi
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-cpp-patterns.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
signing_key="${PXA_SIGNING_KEY:-$pxa_system_dir/apps/pxa/.dev-signing/publisher-private.pem}"
artifacts="${PXA_SDK_TEST_ARTIFACT_DIR:-$work_dir/captures}"
mkdir -p "$work_dir/state" "$artifacts"
openssl pkey -in "$signing_key" -pubout -outform DER -out "$work_dir/publisher.der"
PXA_APP_SOURCE_ROOT="$pxa_system_dir/sdk/guest-cpp/examples" \
PXA_SIGNING_KEY="$signing_key" PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
  bash "$script_dir/package_app.sh" sdk-patterns simulator "$work_dir/pxa-sdk-patterns"
"$1" "$work_dir/pxa-sdk-patterns" "$work_dir/publisher.der" "$work_dir/state" "$artifacts"
python3 - "$work_dir/state" <<'PY'
from pathlib import Path
import sys
files=list(Path(sys.argv[1]).glob('private-files/*/transfer.bin'))
assert len(files)==1, files
assert files[0].read_bytes()==b'\x01\x00\x03\x00\x00\x00\x01'
print('Native filesystem: exact portable 7-byte save after three AOT runs OK')
PY
