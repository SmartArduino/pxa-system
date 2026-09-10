#!/usr/bin/env python3
"""Regenerate PXA golden vectors in a temporary directory and compare them."""

from __future__ import annotations

import pathlib
import subprocess
import sys
import tempfile


TOOLS = pathlib.Path(__file__).resolve().parent
ROOT = TOOLS.parent


def run(*arguments: str) -> None:
    subprocess.run([sys.executable, *arguments], check=True)


def main() -> int:
    run(str(TOOLS / "check_spec.py"))
    with tempfile.TemporaryDirectory(prefix="pxa-spec-test-") as directory:
        work = pathlib.Path(directory)
        generated_core = work / "core-vectors.json"
        generated_package = work / "package-vectors.json"
        run(str(TOOLS / "generate_golden.py"), str(generated_core))
        run(str(TOOLS / "generate_package_golden.py"), str(generated_package))
        if generated_core.read_bytes() != (ROOT / "golden/core-vectors.json").read_bytes():
            raise RuntimeError("Core golden vectors are stale")
        if generated_package.read_bytes() != (ROOT / "golden/package-vectors.json").read_bytes():
            raise RuntimeError("Package golden vectors are stale")
    print("PXA spec generation is reproducible")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
