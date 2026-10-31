#!/usr/bin/env python3
"""Signed resource builder -> real SDK installer, immutable digest policy."""
import hashlib
import importlib.util
import json
import os
import struct
from pathlib import Path
import subprocess
import sys
import tempfile


def run(*arguments, binding=None, ok=True):
    environment = dict(os.environ)
    if binding is not None:
        environment["PXA_TEST_RESOURCE_MANIFEST"] = binding
    result = subprocess.run(list(map(str, arguments)), env=environment,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert (result.returncode == 0) == ok, result.stderr.decode(errors="replace")


def main():
    verifier, tools = map(Path, sys.argv[1:])
    with tempfile.TemporaryDirectory(prefix="pxa-resource-tool-") as temporary:
        root = Path(temporary)
        key, public = root / "key.pem", root / "key.der"
        run("openssl", "genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", key)
        run("openssl", "pkey", "-in", key, "-pubout", "-outform", "DER", "-out", public)
        package = root / "resources"
        package.mkdir()
        data = package / "pak0.pak"
        # A multi-chunk source; generator inventory must never retain its bytes.
        with data.open("wb") as output:
            for _ in range(256):
                output.write(b"resource-data" * 500)
        meta = root / "package.json"
        metadata = {"format": "pxa-package-source-0.2", "package_type": "resources",
                    "id": "resource-fixture", "version": "1", "release_sequence": 1}
        meta.write_text(json.dumps(metadata))
        archive, descriptor = root / "resources.pxa", root / "binding.json"
        run(sys.executable, tools / "build_resource_package.py", meta, package, key, archive,
            "--descriptor", descriptor)
        assert struct.unpack_from("<HH", archive.read_bytes(), 12) == (2, 12)
        digest = json.loads(descriptor.read_text())["resources"]["manifest_sha256"]
        assert hashlib.sha256((package / "manifest.pxm").read_bytes()).hexdigest() == digest
        storage = root / "storage"
        storage.mkdir()
        run(verifier, public, storage, archive, binding=digest)
        # Different transport encoding preserves logical resource identity.
        stored = root / "stored.pxa"
        run(sys.executable, tools / "build_pxa_container.py", package, key, stored, "--codec", "store")
        run(verifier, public, storage, stored, binding=digest)
        legacy = root / "legacy.pxa"
        run(sys.executable, tools / "build_resource_package.py", meta, package, key, legacy, "--legacy")
        assert struct.unpack_from("<HH", legacy.read_bytes(), 4) == (0, 1)
        assert struct.unpack_from("<HH", legacy.read_bytes(), 12) == (1, 12)
        run(verifier, public, storage, legacy, binding=digest)
        streamed = root / "deflate.pxa"
        run(sys.executable, tools / "build_resource_package.py", meta, package, key, streamed,
            "--streamable", "--codec", "deflate")
        stream_storage = root / "stream-storage"; stream_storage.mkdir()
        run(verifier, public, stream_storage, streamed, binding=digest)
        run(verifier, public, storage, archive, binding="0" * 64, ok=False)
        metadata["version"] = "2"
        meta.write_text(json.dumps(metadata))
        other = root / "other.pxa"
        run(sys.executable, tools / "build_resource_package.py", meta, package, key, other)
        run(verifier, public, storage, other, binding=digest, ok=False)
        metadata["permissions"] = [{"name": "net.client", "required": False}]
        meta.write_text(json.dumps(metadata))
        run(sys.executable, tools / "build_resource_package.py", meta, package, key, other, ok=False)
        # Inspect the inventory API directly with a guarded read_bytes method.
        sys.path.insert(0, str(tools))
        from build_package_manifest import package_files
        original = Path.read_bytes
        def bounded_read(path):
            assert path != data, "whole resource file was read"
            return original(path)
        Path.read_bytes = bounded_read
        try:
            files = package_files(package)
            assert files == [("pak0.pak", data.stat().st_size,
                              hashlib.file_digest(data.open("rb"), "sha256").digest())]
        finally:
            Path.read_bytes = original
    print("signed resources: bounded builder, SDK signature/inventory, immutable hash and codec reuse passed")


if __name__ == "__main__":
    main()
