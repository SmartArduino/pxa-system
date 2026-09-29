#!/usr/bin/env python3
"""Run the native backend with temporary copies and an authenticated-index fixture.

The C test supplies the fixture manifest; signed AOT tests cover real authority.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from compile_resources import build_index

with tempfile.TemporaryDirectory(prefix="pxa-audio-backend-") as name:
    root = Path(name)
    shutil.copytree(Path(sys.argv[2]) / "assets", root / "assets")
    (root / "assets/bad.ogg").write_bytes(b"OggS" + bytes(24) + b"\x01vorbis")
    build_index(root)
    subprocess.run([sys.argv[1], str(root)], check=True)
