#!/usr/bin/env python3
"""Enforce the reference application's theme and localization boundaries."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


SEMANTIC_APPS = ("hello", "arcade", "lab", "store", "wasi-lab", "weather")
CANVAS_APPS = ("garden-guard", "plane-shooter")
RASTER_SURFACE_APPS = ("maze-evil", "maze-spike")
GAME_RENDER_APPS = ("game-render-bench", "jump-jump-3d",
                    "plane-shooter-raster", "tomb-explorer", "voxel-craft")
REQUIRED_THEME_TOKENS = (
    "PXA_UI_THEME_BACKGROUND",
    "PXA_UI_THEME_TEXT",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    apps = args.root.resolve()

    package_dirs = sorted(path.parent for path in apps.glob("*/package.json"))
    if not package_dirs:
        raise SystemExit(f"no PXA applications found in {apps}")

    for app_dir in package_dirs:
        manifest = json.loads((app_dir / "package.json").read_text(encoding="utf-8"))
        if manifest.get("id") != f"pxa-{app_dir.name}":
            raise SystemExit(f"{app_dir}: package id must match its directory")
        if not (app_dir / "i18n" / "messages.yaml").is_file():
            continue
        for catalog in ("messages.yaml", "zh-CN.yaml"):
            if not (app_dir / "i18n" / catalog).is_file():
                raise SystemExit(f"{app_dir}: missing i18n/{catalog}")
        source = (app_dir / "main.c").read_text(encoding="utf-8")
        for marker in ("pxa_i18n_init_from_start_config", "pxa_i18n_handle_event"):
            if marker not in source:
                raise SystemExit(f"{app_dir}: missing locale lifecycle marker {marker}")

    for app_name in SEMANTIC_APPS:
        if not (apps / app_name).is_dir():
            continue
        source = (apps / app_name / "main.c").read_text(encoding="utf-8")
        for token in REQUIRED_THEME_TOKENS:
            if token not in source:
                raise SystemExit(f"{app_name}: missing semantic theme token {token}")

    for app_name in CANVAS_APPS:
        if not (apps / app_name).is_dir():
            continue
        source = (apps / app_name / "main.c").read_text(encoding="utf-8")
        if "pxa_canvas_" not in source:
            raise SystemExit(f"{app_name}: expected an app-owned Canvas surface")

    for app_name in RASTER_SURFACE_APPS:
        if not (apps / app_name).is_dir():
            continue
        source = (apps / app_name / "main.c").read_text(encoding="utf-8")
        if "pxa_surface_" not in source:
            raise SystemExit(f"{app_name}: expected an app-owned raster Surface")

    for app_name in GAME_RENDER_APPS:
        if not (apps / app_name).is_dir():
            continue
        source = (apps / app_name / "main.c").read_text(encoding="utf-8")
        if "pxa_game_render_" not in source:
            raise SystemExit(f"{app_name}: expected an app-owned GameRender context")

    print("PXA reference UI theme and locale boundaries OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
