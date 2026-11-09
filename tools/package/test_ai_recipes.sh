#!/usr/bin/env bash
set -euo pipefail
pxa_recipe_system="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
pxa_recipe_work="$(mktemp -d "${TMPDIR:-/tmp}/pxa-ai-recipes.XXXXXX")"
trap 'rm -rf "$pxa_recipe_work"' EXIT
python3 - "$pxa_recipe_system" "$pxa_recipe_work" <<'PY'
from pathlib import Path
import sys
root, output = map(Path, sys.argv[1:])
source = (root / 'ai/templates/recipes/timer/main.cpp').read_text()
source = source.replace('@PXA_MINUTES@', '25').replace('@PXA_BREAK_MINUTES@', '5').replace('@PXA_TITLE@', '"番茄钟"')
(output / 'recipe_timer.hpp').write_text(source)
PY
"${CXX:-clang++}" -std=c++2c -O2 -fno-exceptions -fno-rtti -Wall -Wextra -Werror \
    -Wno-attributes -DPXA_RECIPE_TIMER_SECONDS=6 \
    -I"$pxa_recipe_system/sdk/guest-cpp/include" -I"$pxa_recipe_work" \
    "$pxa_recipe_system/ai/tests/recipe_timer_test.cpp" \
    "$pxa_recipe_system/sdk/guest-cpp/src/runtime.cpp" -o "$pxa_recipe_work/timer"
"$pxa_recipe_work/timer"
