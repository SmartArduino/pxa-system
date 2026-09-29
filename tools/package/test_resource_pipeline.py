#!/usr/bin/env python3
"""Cross-language compiler/Host integration using a temporary real filesystem."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from compile_resources import compile_resources

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    app, package = root / "app", root / "package"
    app.mkdir()
    package.mkdir()
    (app / "pixels").write_bytes(bytes([1, 2, 3, 0]))
    (app / "palette.json").write_text(json.dumps([0, 0xf800, 0x07e0, 0x001f] + [0] * 252))
    (app / "resources.json").write_text(json.dumps({"assets": [
        {"path": "assets/t.pxr", "kind": "index8", "source": "pixels", "width": 2, "height": 2},
        {"path": "assets/p.pxr", "kind": "palette", "source": "palette.json"},
    ]}))
    compile_resources(app, package)
    subprocess.run([sys.argv[1], str(package)], check=True, timeout=30)
