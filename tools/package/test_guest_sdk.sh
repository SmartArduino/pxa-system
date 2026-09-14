#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pxa_system_dir="$(cd "$script_dir/../.." && pwd)"
cc_bin="${CC:-cc}"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/pxa-guest-sdk-test.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT

flags=(-std=c11 -O2 -Wall -Wextra -Werror -Wno-attributes
       -I"$pxa_system_dir/sdk/guest-c/include"
       -I"$pxa_system_dir/apps/pxa/common")

generate_catalog() {
  local app_id="$1"
  local catalog_dir="$pxa_system_dir/apps/pxa/$app_id/i18n"
  local output_dir="$work_dir/generated/$app_id"
  local locale_catalogs=()

  mkdir -p "$output_dir"
  while IFS= read -r catalog; do
    locale_catalogs+=("$catalog")
  done < <(find "$catalog_dir" -maxdepth 1 -type f -name '*.yaml' \
           ! -name 'messages.yaml' -print | LC_ALL=C sort)
  "${PYTHON:-python3}" "$pxa_system_dir/tools/i18n/compile_catalog.py" \
    "$catalog_dir/messages.yaml" "${locale_catalogs[@]}" \
    --output "$output_dir/pxa_app_messages.h"
}

for localized_app in arcade garden-guard lab plane-shooter wasi-lab weather; do
  generate_catalog "$localized_app"
done

"${PYTHON:-python3}" \
  "$pxa_system_dir/tools/apps/check_ui.py" \
  "$pxa_system_dir/apps/pxa"

"${PYTHON:-python3}" \
  "$pxa_system_dir/tools/package/test_ui_vectors.py" \
  "$pxa_system_dir/spec/draft/golden/ui-vectors.json"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_net_test.c" \
  -o "$work_dir/pxa_net_test"
"$work_dir/pxa_net_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_device_test.c" \
  -o "$work_dir/pxa_device_test"
"$work_dir/pxa_device_test"

"${PYTHON:-python3}" - "$pxa_system_dir" <<'PYTHON'
import importlib.util
import pathlib
import re
import struct
import sys

pxa_system_dir = pathlib.Path(sys.argv[1])
tool_path = pxa_system_dir / "tools/package/build_package_manifest.py"
spec = importlib.util.spec_from_file_location("pxa_package_manifest", tool_path)
manifest_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest_tool)

invalid = []
for assets_dir in (pxa_system_dir / "apps/pxa").glob("*/assets"):
    for path in assets_dir.rglob("*"):
        if path.is_file():
            relative = path.relative_to(assets_dir.parent).as_posix()
            if manifest_tool.PACKAGE_PATH.fullmatch(relative) is None:
                invalid.append(relative)
if invalid:
    raise SystemExit("invalid PXA package asset path: " + ", ".join(sorted(invalid)))

expected_pngs = {
    "apps/pxa/arcade/assets/brick-breaker/icon.png": (96, 96),
    "apps/pxa/arcade/assets/flappy-bird/bird.png": (34, 21),
    "apps/pxa/arcade/assets/flappy-bird/icon.png": (96, 96),
    "apps/pxa/arcade/assets/jump-jump/icon.png": (96, 96),
    "apps/pxa/arcade/assets/jumping/icon.png": (96, 96),
    "apps/pxa/arcade/assets/minesweeper-music/icon.png": (96, 96),
    "apps/pxa/arcade/assets/plane-shooter/player-plane.png": (38, 28),
    "apps/pxa/arcade/assets/plane-shooter/enemy-plane.png": (28, 20),
    "apps/pxa/arcade/assets/plane-shooter/icon.png": (96, 96),
    "apps/pxa/arcade/assets/tetris/icon.png": (96, 96),
    "apps/pxa/garden-guard/assets/actors.png": (352, 112),
    "apps/pxa/garden-guard/assets/sun.png": (22, 22),
    "apps/pxa/garden-guard/assets/pea-shooter.png": (30, 30),
    "apps/pxa/garden-guard/assets/sunflower.png": (30, 30),
    "apps/pxa/garden-guard/assets/wall-nut.png": (30, 30),
    "apps/pxa/garden-guard/assets/cherry-bomb.png": (30, 30),
    "apps/pxa/garden-guard/assets/repeater.png": (30, 30),
    "apps/pxa/garden-guard/assets/ice-shooter.png": (30, 30),
    "apps/pxa/garden-guard/assets/zombie.png": (30, 30),
    "apps/pxa/garden-guard/assets/cone-zombie.png": (30, 30),
    "apps/pxa/garden-guard/assets/bucket-zombie.png": (30, 30),
    "apps/pxa/garden-guard/assets/mower.png": (30, 30),
    "apps/pxa/garden-guard/assets/shovel.png": (24, 24),
}
for relative, expected_size in expected_pngs.items():
    data = (pxa_system_dir / relative).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n" or len(data) < 24:
        raise SystemExit(f"invalid PNG asset: {relative}")
    actual_size = struct.unpack(">II", data[16:24])
    if actual_size != expected_size:
        raise SystemExit(
            f"unexpected PNG dimensions for {relative}: "
            f"{actual_size[0]}x{actual_size[1]}"
        )

garden_source = (
    pxa_system_dir / "apps/pxa/garden-guard/main.c"
).read_text(encoding="utf-8")
garden_runtime_pngs = set(re.findall(r'"(assets/[^"\\]+\.png)"', garden_source))
expected_garden_runtime_pngs = {"assets/actors.png", "assets/sun.png"}
if garden_runtime_pngs != expected_garden_runtime_pngs:
    raise SystemExit(
        "garden-guard must use only its actor atlas and sun sprite at runtime: "
        + ", ".join(sorted(garden_runtime_pngs))
    )

arcade_source = (pxa_system_dir / "apps/pxa/arcade/main.c").read_text(encoding="utf-8")
expected_arcade_menu_icons = {
    "assets/brick-breaker/icon.png",
    "assets/flappy-bird/icon.png",
    "assets/jump-jump/icon.png",
    "assets/jumping/icon.png",
    "assets/minesweeper-music/icon.png",
    "assets/tetris/icon.png",
}
arcade_menu_icons = set(re.findall(r'"(assets/[^"\\]+/icon\.png)"', arcade_source))
if arcade_menu_icons != expected_arcade_menu_icons:
    raise SystemExit(
        "arcade menu must use the matching asset icon for every game: "
        + ", ".join(sorted(arcade_menu_icons))
    )
PYTHON

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_canvas_test.c" \
  -o "$work_dir/pxa_canvas_test"
"$work_dir/pxa_canvas_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_ui_builder_test.c" \
  -o "$work_dir/pxa_ui_builder_test"
"$work_dir/pxa_ui_builder_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_fs_test.c" \
  -o "$work_dir/pxa_fs_test"
"$work_dir/pxa_fs_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_storage_test.c" \
  -o "$work_dir/pxa_storage_test"
"$work_dir/pxa_storage_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_ipc_test.c" \
  -o "$work_dir/pxa_ipc_test"
"$work_dir/pxa_ipc_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_lease_test.c" \
  -o "$work_dir/pxa_lease_test"
"$work_dir/pxa_lease_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_sensor_test.c" \
  -o "$work_dir/pxa_sensor_test"
"$work_dir/pxa_sensor_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_permission_test.c" \
  -o "$work_dir/pxa_permission_test"
"$work_dir/pxa_permission_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_work_test.c" \
  -o "$work_dir/pxa_work_test"
"$work_dir/pxa_work_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_audio_test.c" \
  -o "$work_dir/pxa_audio_test"
"$work_dir/pxa_audio_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_surface_test.c" \
  -o "$work_dir/pxa_surface_test"
"$work_dir/pxa_surface_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_mapped_surface_test.c" \
  -o "$work_dir/pxa_mapped_surface_test"
"$work_dir/pxa_mapped_surface_test"

"$cc_bin" "${flags[@]}" \
  -I"$pxa_system_dir/apps/pxa/maze-evil" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_maze_audio_test.c" \
  "$pxa_system_dir/apps/pxa/maze-evil/audio.c" \
  -o "$work_dir/pxa_maze_audio_test"
"$work_dir/pxa_maze_audio_test"

"$cc_bin" "${flags[@]}" -Wno-unused-function \
  -I"$pxa_system_dir/apps/pxa/maze-evil" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_maze_render_test.c" \
  "$pxa_system_dir/apps/pxa/maze-evil/raster.c" \
  "$pxa_system_dir/apps/pxa/maze-evil/palette.c" \
  "$pxa_system_dir/apps/pxa/maze-evil/font.c" \
  "$pxa_system_dir/apps/pxa/maze-evil/assets.c" \
  "$pxa_system_dir/apps/pxa/maze-evil/sprites.c" \
  "$pxa_system_dir/apps/pxa/maze-evil/raycast.c" \
  "$pxa_system_dir/apps/pxa/maze-evil/world.c" \
  "$pxa_system_dir/apps/pxa/maze-evil/render.c" \
  -lm \
  -o "$work_dir/pxa_maze_render_test"
"$work_dir/pxa_maze_render_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_raster_test.c" \
  -o "$work_dir/pxa_raster_test"
"$work_dir/pxa_raster_test"

"$cc_bin" "${flags[@]}" \
  -I"$pxa_system_dir/apps/pxa/voxel-craft" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_voxel_surface_ownership_test.c" \
  -o "$work_dir/pxa_voxel_surface_ownership_test"
"$work_dir/pxa_voxel_surface_ownership_test"

"$cc_bin" "${flags[@]}" \
  -I"$pxa_system_dir/apps/pxa/voxel-craft" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_voxel_quality_controller_test.c" \
  -o "$work_dir/pxa_voxel_quality_controller_test"
"$work_dir/pxa_voxel_quality_controller_test"

"$cc_bin" "${flags[@]}" \
  -I"$pxa_system_dir/apps/pxa/voxel-craft" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_voxel_raster_pipeline_test.c" \
  "$pxa_system_dir/apps/pxa/voxel-craft/voxel_raster.c" \
  -o "$work_dir/pxa_voxel_raster_pipeline_test"
"$work_dir/pxa_voxel_raster_pipeline_test"

"$cc_bin" "${flags[@]}" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_game_sfx_test.c" \
  -o "$work_dir/pxa_game_sfx_test"
"$work_dir/pxa_game_sfx_test"

for app_source in \
  apps/pxa/arcade/modules/flappy_bird.c \
  apps/pxa/garden-guard/main.c \
  apps/pxa/arcade/modules/jumping.c \
  apps/pxa/arcade/modules/jump_jump.c \
  apps/pxa/arcade/modules/plane_shooter.c \
  apps/pxa/arcade/modules/brick_breaker.c \
  apps/pxa/arcade/modules/tetris.c; do
  app_flags=()
  app_id="${app_source#apps/pxa/}"
  app_id="${app_id%%/*}"
  [[ "$app_source" == apps/pxa/arcade/* ]] && app_flags=(-DPXA_ARCADE_STANDALONE_TEST)
  "$cc_bin" "${flags[@]}" "${app_flags[@]}" \
    -I"$work_dir/generated/$app_id" \
    "$pxa_system_dir/sdk/guest-c/tests/pxa_canvas_smoke_test.c" \
    "$pxa_system_dir/$app_source" \
    -o "$work_dir/pxa_canvas_smoke_test"
  "$work_dir/pxa_canvas_smoke_test"
done

for app_source in \
  apps/pxa/arcade/modules/jumping.c \
  apps/pxa/arcade/modules/plane_shooter.c \
  apps/pxa/arcade/modules/brick_breaker.c; do
  "$cc_bin" "${flags[@]}" -DPXA_ARCADE_STANDALONE_TEST \
    -I"$work_dir/generated/arcade" \
    "$pxa_system_dir/sdk/guest-c/tests/pxa_pointer_frame_pacing_test.c" \
    "$pxa_system_dir/$app_source" \
    -o "$work_dir/pxa_pointer_frame_pacing_test"
  "$work_dir/pxa_pointer_frame_pacing_test"
done

"$cc_bin" "${flags[@]}" \
  -DPXA_ARCADE_STANDALONE_TEST \
  -I"$work_dir/generated/arcade" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_tetris_controls_test.c" \
  "$pxa_system_dir/apps/pxa/arcade/modules/tetris.c" \
  -o "$work_dir/pxa_tetris_controls_test"
"$work_dir/pxa_tetris_controls_test"

for app_id in arcade weather lab wasi-lab plane-shooter; do
  "$cc_bin" "${flags[@]}" -I"$work_dir/generated/$app_id" -fsyntax-only \
    "$pxa_system_dir/apps/pxa/$app_id/main.c"
done
for lab_module in "$pxa_system_dir"/apps/pxa/lab/modules/*.c; do
  "$cc_bin" "${flags[@]}" -I"$work_dir/generated/lab" -fsyntax-only "$lab_module"
done
"$cc_bin" "${flags[@]}" -I"$work_dir/generated/lab" -fsyntax-only \
  "$pxa_system_dir/apps/pxa/lab/responder.c"
"$cc_bin" "${flags[@]}" -I"$work_dir/generated/lab" -fsyntax-only \
  "$pxa_system_dir/apps/pxa/lab/worker.c"

for app_id in maze-spike maze-evil; do
  for app_source in "$pxa_system_dir"/apps/pxa/"$app_id"/*.c; do
    "$cc_bin" "${flags[@]}" -I"$pxa_system_dir/apps/pxa/$app_id" \
      -fsyntax-only "$app_source"
  done
done

"$cc_bin" "${flags[@]}" -DPXA_ARCADE_STANDALONE_TEST \
  -I"$work_dir/generated/arcade" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_jump_jump_test.c" \
  -o "$work_dir/pxa_jump_jump_test"
"$work_dir/pxa_jump_jump_test"

"$cc_bin" "${flags[@]}" \
  -I"$work_dir/generated/garden-guard" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_garden_guard_test.c" \
  -o "$work_dir/pxa_garden_guard_test"
"$work_dir/pxa_garden_guard_test"

"$cc_bin" "${flags[@]}" -DGARDEN_DEBUG_UNLOCK_ALL=1 \
  -I"$work_dir/generated/garden-guard" \
  "$pxa_system_dir/sdk/guest-c/tests/pxa_garden_guard_test.c" \
  -o "$work_dir/pxa_garden_guard_debug_test"
"$work_dir/pxa_garden_guard_debug_test"

echo "PXA Guest SDK and App tests OK"
