#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <pxsys_app_matrix_test>" >&2
  exit 2
fi
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
system_dir="$(cd "$script_dir/../.." && pwd)"
artifact_root="$(realpath -m -- "${PXA_TEST_ARTIFACT_ROOT:-$system_dir/../../local/artifacts/embedded-main-loop}")"
artifact_dir="$artifact_root/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$artifact_dir"
work_dir="$(mktemp -d "$artifact_dir/run-XXXXXX")"
mkdir -p "$work_dir/state"
trap 'rm -f "$work_dir/publisher-private.pem"' EXIT
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -out "$work_dir/publisher-private.pem"
openssl pkey -in "$work_dir/publisher-private.pem" -pubout -outform DER -out "$work_dir/publisher.der"
PXA_SIGNING_KEY="$work_dir/publisher-private.pem" \
PXA_APP_SOURCE_ROOT="$system_dir/simulator/desktop/tests/apps" \
PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
  bash "$script_dir/package_app.sh" main-loop-smoke simulator "$work_dir/pxa-main-loop-smoke" \
  >"$work_dir/build.log" 2>&1 || { cat "$work_dir/build.log" >&2; exit 1; }
PXA_MATRIX_SEND_BACK=1 "$1" "$work_dir/pxa-main-loop-smoke" "$work_dir/publisher.der" "$work_dir/state" \
  480 480 305 1 "$work_dir/frame" >"$work_dir/run.log" 2>&1 || {
    cat "$work_dir/run.log" >&2; exit 1;
  }
"${PYTHON:-python3}" - "$work_dir/run.log" <<'PY'
import sys
from pathlib import Path
log = Path(sys.argv[1]).read_text()
offset = 0
for marker in ("Main loop: entered", "Main loop: foreground", "Main loop: background",
               "Main loop: foreground", "Main loop: back",
               "Signed app: startup, background/resume and shutdown passed"):
    position = log.find(marker, offset)
    assert position >= 0, (marker, log)
    offset = position + len(marker)
PY
echo "Embedded main loop: startup, lifecycle, Back, native pump and cancellation passed"
