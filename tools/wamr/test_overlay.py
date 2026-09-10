#!/usr/bin/env python3
"""Verify the pinned WAMR patches and ESP-IDF override wiring."""

from __future__ import annotations

import importlib.util
import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "wamr"
SERIES = ROOT / "platforms/esp-idf/wamr/patches/series"
OVERRIDE = ROOT / "platforms/esp-idf/wamr/overrides/espidf_memmap.c"
INTEGRATION = ROOT / "platforms/esp-idf/cmake/pxsys_wamr.cmake"


def load_overlay_module():
    path = pathlib.Path(__file__).with_name("prepare_overlay.py")
    spec = importlib.util.spec_from_file_location("pxsys_prepare_overlay", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load prepare_overlay.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def tracked_status() -> str:
    return subprocess.run(
        ["git", "-C", str(SOURCE), "status", "--porcelain",
         "--untracked-files=no"],
        check=True, text=True, capture_output=True,
    ).stdout


def main() -> int:
    overlay = load_overlay_module()
    expected: set[pathlib.PurePosixPath] = set()
    for name in SERIES.read_text(encoding="utf-8").splitlines():
        name = name.strip()
        if name and not name.startswith("#"):
            for file_patch in overlay.parse_patch(SERIES.parent / name):
                expected.add(file_patch.path)

    before = tracked_status()
    with tempfile.TemporaryDirectory(prefix="pxsys-wamr-overlay-") as temporary:
        written = overlay.prepare(
            SOURCE, pathlib.Path(temporary), ROOT / "config/wamr.json", SERIES
        )
        actual = {
            pathlib.PurePosixPath(path.relative_to(temporary).as_posix())
            for path in written
        }
        if actual != expected:
            raise RuntimeError(f"overlay mismatch: expected {expected}, got {actual}")
        if any(not path.is_file() or path.stat().st_size == 0 for path in written):
            raise RuntimeError("overlay contains an empty or missing file")

    if tracked_status() != before:
        raise RuntimeError("WAMR submodule changed while verifying patches")
    if not OVERRIDE.is_file() or OVERRIDE.stat().st_size == 0:
        raise RuntimeError("ESP-IDF memory-map override is missing")
    wiring = INTEGRATION.read_text(encoding="utf-8")
    for required in ("prepare_overlay.py", "patches/series",
                     "overrides/espidf_memmap.c", "pxsys_replace_wamr_source",
                     "CMAKE_CONFIGURE_DEPENDS"):
        if required not in wiring:
            raise RuntimeError(f"WAMR CMake integration is missing {required}")
    print(f"WAMR overlay verified ({len(expected)} patched files + 1 override)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
