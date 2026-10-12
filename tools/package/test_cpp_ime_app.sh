#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <pxsys_ime_test> <signed-provider-directory> <publisher-private-key>" >&2
  exit 2
fi
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-cpp-ime.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
openssl pkey -in "$3" -pubout -outform DER -out "$work_dir/publisher.der"
PXA_APP_SOURCE_ROOT="$pxa_system_dir/sdk/guest-cpp/examples" \
PXA_SIGNING_KEY="$3" PXA_PACKAGE_OUTPUT_ROOT="$work_dir" \
  bash "$script_dir/package_app.sh" system-input simulator "$work_dir/pxa-system-input"
mkdir -p "$work_dir/state" "$work_dir/captures"
"$1" "$work_dir/pxa-system-input" "$work_dir/publisher.der" "$2" "$work_dir/state" "$work_dir/captures"
for failure in corrupt-dictionary corrupt-signature missing-dictionary extra-file symlink-dictionary hardlink-dictionary; do
  cp -R "$2" "$work_dir/$failure"
  if [[ "$failure" == corrupt-dictionary ]]; then
    # Same-size corruption must fail SHA256, rather than only a size check.
    python3 - "$work_dir/$failure/assets/dict_pinyin.dat" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
with path.open('r+b') as file:
    byte = file.read(1)
    file.seek(0)
    file.write(bytes([byte[0] ^ 1]))
PY
  elif [[ "$failure" == corrupt-signature ]]; then
    python3 - "$work_dir/$failure/signature.pxs" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
data = bytearray(path.read_bytes()); data[-1] ^= 1; path.write_bytes(data)
PY
  elif [[ "$failure" == extra-file ]]; then
    printf 'unsigned' > "$work_dir/$failure/assets/extra.txt"
  elif [[ "$failure" == symlink-dictionary || "$failure" == hardlink-dictionary ]]; then
    rm "$work_dir/$failure/assets/dict_pinyin.dat"
    if [[ "$failure" == symlink-dictionary ]]; then
      ln -s "$2/assets/dict_pinyin.dat" "$work_dir/$failure/assets/dict_pinyin.dat"
    else
      cp "$2/assets/dict_pinyin.dat" "$work_dir/$failure/dictionary-alias"
      ln "$work_dir/$failure/dictionary-alias" "$work_dir/$failure/assets/dict_pinyin.dat"
      # Keep the second link outside the inventory directory.
      mv "$work_dir/$failure/dictionary-alias" "$work_dir/hardlink-alias"
    fi
  else
    rm "$work_dir/$failure/assets/dict_pinyin.dat"
  fi
  "$1" "$work_dir/pxa-system-input" "$work_dir/publisher.der" \
    "$work_dir/$failure" "$work_dir/state" "$work_dir/captures" --expect-load-failure
done
