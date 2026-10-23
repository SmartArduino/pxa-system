#!/usr/bin/env python3
"""Enforce the reference application's theme and localization boundaries."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import yaml


SEMANTIC_APPS = ("hello", "arcade", "store", "weather")
CANVAS_APPS = ("garden-guard", "plane-shooter")
RASTER_SURFACE_APPS = ("maze-evil",)
GAME_RENDER_APPS = ("jump-jump-3d", "voxel-craft")
REQUIRED_THEME_TOKENS = (
    "PXA_UI_THEME_BACKGROUND",
    "PXA_UI_THEME_TEXT",
)


def entry_source(app: Path) -> str:
    directories = [app]
    cmake = app / "CMakeLists.txt"
    if cmake.is_file():
        # Shared frontends conventionally keep main.c beside the included
        # CMake module. Resolve literal source-relative includes without
        # configuring the project or assuming an emulator directory name.
        for include in re.findall(r'include\(\s*"?([^\s"\)]+)', cmake.read_text(encoding="utf-8")):
            for variable in ("CMAKE_CURRENT_SOURCE_DIR", "CMAKE_CURRENT_LIST_DIR"):
                include = include.replace("${" + variable + "}", str(app))
            if "$" not in include:
                module = (app / include).resolve()
                if module.is_file():
                    directories.append(module.parent)
    for directory in directories:
        for name in ("main.cpp", "main.c"):
            source = directory / name
            if source.is_file():
                return source.read_text(encoding="utf-8")
    raise SystemExit(f"{app}: missing application entry source for locale lifecycle checks")


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
        app_id = manifest.get("id")
        if not isinstance(app_id, str) or not re.fullmatch(r"[a-z][a-z0-9._-]{0,63}", app_id):
            raise SystemExit(f"{app_dir}: invalid canonical package id")
        if not (app_dir / "i18n" / "messages.yaml").is_file():
            continue
        for catalog in ("messages.yaml", "zh-CN.yaml"):
            if not (app_dir / "i18n" / catalog).is_file():
                raise SystemExit(f"{app_dir}: missing i18n/{catalog}")
        source_catalog = yaml.safe_load((app_dir / "i18n" / "messages.yaml").read_text(encoding="utf-8"))
        if not isinstance(source_catalog, dict) or not isinstance(source_catalog.get("messages", {}), dict):
            raise SystemExit(f"{app_dir}: invalid source message catalog")
        # A metadata-only catalog localizes the installed App name in the
        # Host. Engine ports do not need a Guest translation lifecycle for it.
        if not source_catalog.get("messages"):
            continue
        source = entry_source(app_dir)
        if "pxa_i18n_init_from_start_config" not in source or not any(
            marker in source for marker in ("pxa_i18n_handle_event",)
        ):
            raise SystemExit(f"{app_dir}: missing locale lifecycle handling")

    for app_name in SEMANTIC_APPS:
        if not (apps / app_name).is_dir():
            continue
        source = entry_source(apps / app_name)
        for token in REQUIRED_THEME_TOKENS:
            if token not in source:
                raise SystemExit(f"{app_name}: missing semantic theme token {token}")

    for app_name in CANVAS_APPS:
        if not (apps / app_name).is_dir():
            continue
        source = entry_source(apps / app_name)
        if "pxa_canvas_" not in source:
            raise SystemExit(f"{app_name}: expected an app-owned Canvas surface")

    for app_name in RASTER_SURFACE_APPS:
        if not (apps / app_name).is_dir():
            continue
        source = entry_source(apps / app_name)
        if not any(name in source for name in ("pxa_surface_",)):
            raise SystemExit(f"{app_name}: expected an app-owned raster Surface")

    for app_name in GAME_RENDER_APPS:
        if not (apps / app_name).is_dir():
            continue
        source = entry_source(apps / app_name)
        if not any(name in source for name in ("pxa_game_render_", "pxa::game::Renderer")):
            raise SystemExit(f"{app_name}: expected an app-owned GameRender context")

    print("PXA reference UI theme and locale boundaries OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
