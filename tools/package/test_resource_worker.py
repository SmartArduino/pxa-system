#!/usr/bin/env python3
"""Generate a 20-texture file workload for the real asynchronous Host worker."""
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
    assets = []
    for i in range(20):
        (app / f"t{i:02}.bin").write_bytes(bytes([i + 1]) * 65536)
        assets.append({"kind": "index8", "source": f"t{i:02}.bin",
                       "path": f"assets/t{i:02}.pxr", "width": 256, "height": 256})
    (app / "palette.json").write_text(json.dumps([i * 251 for i in range(256)]))
    assets.append({"kind": "palette", "source": "palette.json", "path": "assets/p.pxr"})
    (package / "assets").mkdir()
    (package / "assets/zsound.pcm").write_bytes(bytes([255])*160)
    (package / "assets/zmap.bin").write_bytes(bytes(i % 251 for i in range(8193)))
    from PIL import Image
    for suffix, encoding, color in [("bgra","bgra8888",(17,43,89,123)),("rgb","rgb565",(255,128,0,255))]:
        Image.new("RGBA",(320,16),color).save(app / (suffix+".png"))
        assets.append({"kind":"image","source":suffix+".png","path":"assets/zzui-"+suffix+".pxr","encoding":encoding})
    (app / "resources.json").write_text(json.dumps({"assets": assets}))
    if "--music" in sys.argv[2:]:
        (package / "assets/zzmusic.ogg").write_bytes(b"OggSOpusHead" + bytes(i % 251 for i in range(9001 - len(b"OggSOpusHead"))))
    compile_resources(app, package)
    subprocess.run([sys.argv[1], str(package)], check=True, timeout=90)
