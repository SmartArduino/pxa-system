#!/usr/bin/env python3
"""Emit a reproducible build-provenance and file-inventory sidecar for PXA."""

import hashlib
import json
import os
import struct
import subprocess
import sys
from pathlib import Path


def records(data: bytes):
    offset = 0
    while offset < len(data):
        if len(data) - offset < 4:
            raise ValueError("truncated manifest record")
        tag, size = struct.unpack_from("<HH", data, offset)
        offset += 4
        if size > len(data) - offset:
            raise ValueError("truncated manifest record payload")
        yield tag & 0x7FFF, data[offset:offset + size]
        offset += size


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def version(command: list[str]) -> str:
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, check=False,
                                text=True, timeout=5)
    except (OSError, subprocess.TimeoutExpired):
        return "unavailable"
    return result.stdout.strip().splitlines()[0][:300] if result.stdout else "unavailable"


def sdk(value: bytes) -> str:
    if len(value) != 4:
        return "invalid"
    major, minor = struct.unpack("<HH", value)
    return f"{major}.{minor}"


def main(argv: list[str]) -> None:
    if len(argv) != 6:
        raise SystemExit(
            "usage: build_pxa_provenance.py <package-dir> <pxa> <package.json> "
            "<target> <engine-abi> <output>"
        )
    package_dir, pxa_path, metadata_path, target, engine_abi, output_path = map(Path, argv)
    i18n_dir = metadata_path.resolve().parent / "i18n"
    i18n_inputs = [
        {
            "path": str(path.relative_to(metadata_path.resolve().parent)),
            "sha256": sha256(path),
        }
        for path in sorted(i18n_dir.glob("*.yaml"))
    ] if i18n_dir.is_dir() else []
    manifest_path = package_dir / "manifest.pxm"
    manifest = manifest_path.read_bytes()
    if len(manifest) < 12 or manifest[:4] != b"PXAM":
        raise SystemExit("invalid manifest.pxm")
    major, minor, body_size = struct.unpack_from("<HHI", manifest, 4)
    if body_size != len(manifest) - 12:
        raise SystemExit("invalid manifest.pxm length")
    fields = {}
    files = []
    for tag, value in records(manifest[12:]):
        if tag <= 13:
            fields[tag] = value
        elif tag == 17:
            file_fields = dict(records(value))
            path = file_fields.get(1, b"").decode("ascii")
            size = file_fields.get(2, b"")
            digest = file_fields.get(3, b"")
            if len(size) != 8 or len(digest) != 32:
                raise SystemExit("invalid signed file inventory")
            files.append({
                "path": path,
                "size": struct.unpack("<Q", size)[0],
                "sha256": digest.hex(),
            })
    provenance = {
        "schema": "pxa-provenance-1",
        "artifact": {
            "path": pxa_path.name,
            "sha256": sha256(pxa_path),
            "size": pxa_path.stat().st_size,
            "manifest_sha256": hashlib.sha256(manifest).hexdigest(),
        },
        "package": {
            "app_id": fields.get(2, b"").decode("ascii"),
            "release_sequence": struct.unpack("<Q", fields.get(9, b"\0" * 8))[0],
            "publisher_key_id": fields.get(1, b"").hex(),
            "manifest_format": f"{major}.{minor}",
            "min_sdk": sdk(fields.get(7, b"")),
            "target_sdk": sdk(fields.get(8, b"")),
            "target": str(target),
            "engine_abi": str(engine_abi),
        },
        "inputs": {
            "package_json_sha256": sha256(metadata_path),
            "i18n_catalogs": i18n_inputs,
            "compile_sdk": json.loads(metadata_path.read_text(encoding="utf-8")).get(
                "compile_sdk", "not-declared"
            ),
            "files": sorted(files, key=lambda item: item["path"]),
        },
        "tools": {
            "clang": version([os.getenv("CLANG", "clang"), "--version"]),
            "wamrc": version([os.getenv("WAMRC", "wamrc"), "--version"]),
            "wasi_sdk_dir": os.getenv("WASI_SDK_DIR", ""),
            "python": sys.version.split()[0],
        },
    }
    destination = output_path.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(
        json.dumps(provenance, ensure_ascii=True, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main(sys.argv[1:])
