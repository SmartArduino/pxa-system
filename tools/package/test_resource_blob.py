#!/usr/bin/env python3
"""Exercise authenticated partial reads against the compiler's real files."""
import subprocess
import sys
import tempfile
from pathlib import Path
from compile_resources import build_index, make_pxr

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    assets = root / "assets"
    assets.mkdir()
    (assets / "map.bin").write_bytes(bytes(i % 251 for i in range(8193)))
    (assets / "zero.bin").write_bytes(b"")
    (assets / "t.pxr").write_bytes(make_pxr(1, 1, 1, b"\1"))
    build_index(root)
    subprocess.run([sys.argv[1], str(root)], check=True, timeout=30)
