#!/usr/bin/env python3
"""Exercise production catalog, SHA and bounded reader with real compiled files.

Synthetic Ogg identification is sufficient here; actual decoder integration is
tested separately by the product backend, not claimed by this byte-reader test.
"""
import subprocess
import sys
import tempfile
from pathlib import Path
from compile_resources import build_index

for marker in (b"OpusHead", b"\x01vorbis"):
    with tempfile.TemporaryDirectory(prefix="pxa-resource-stream-") as name:
        root = Path(name)
        (root / "assets").mkdir()
        (root / "assets/music.ogg").write_bytes(
            b"OggS" + bytes(24) + marker + bytes(range(251)) * 34)
        build_index(root)
        subprocess.run([sys.argv[1], str(root)], check=True)
