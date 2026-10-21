#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
system="$(cd "$script_dir/../.." && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-guest-sdk-test.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
flags=(-std=c11 -O2 -Wall -Wextra -Werror -Wno-attributes
       -I"$system/sdk/guest-c/include" -I"$system/sdk/testing")
# SDK checks are independent of the product app checkout. Gameplay/Canvas and
# audio regressions live in pxa-apps/tests/native and common/cpp/tests.
for unit in headers net store_installer canvas ui_builder fs lease sensor work audio surface raster game_spans log core clock ui ui_wire_transport i18n service_parity; do
    "${CC:-cc}" "${flags[@]}" "$system/sdk/guest-c/tests/pxa_${unit}_test.c" \
        -o "$work_dir/$unit"
    "$work_dir/$unit"
done
"${CC:-cc}" "${flags[@]}" -c "$system/sdk/guest-c/examples/ui-button/main.c" \
    -o "$work_dir/ui_button_example.o"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-attributes \
    -I"$system/sdk/guest-c/include" -I"$system/sdk/testing" \
    -c "$system/sdk/guest-c/tests/pxa_cpp_test.cpp" -o "$work_dir/pxa_cpp_test.o"
python3 "$system/tools/package/test_ui_vectors.py" "$system/spec/draft/golden/ui-vectors.json"
echo "PXA Guest wire SDK tests OK"
