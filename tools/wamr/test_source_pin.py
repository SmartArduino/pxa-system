#!/usr/bin/env python3
"""Verify that PXA consumes the WAMR commit declared in its metadata."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
METADATA = ROOT / "config" / "wamr.json"
WAMR = ROOT / "wamr"


def revision(path: pathlib.Path) -> str | None:
    result = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(result.stderr.strip() or f"cannot read WAMR revision in {path}", file=sys.stderr)
        return None
    return result.stdout.strip()


def main() -> int:
    metadata = json.loads(METADATA.read_text(encoding="utf-8"))
    expected = metadata["commit"]
    actual = revision(WAMR)
    if actual is None:
        return 1
    if actual != expected:
        print(
            f"PXA WAMR is {actual}; expected {expected}",
            file=sys.stderr,
        )
        return 1
    print(f"PXA WAMR source pin verified: {expected}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
