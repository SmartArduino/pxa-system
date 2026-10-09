"""Explicit, checksum-verified DevKit installation. Never replaces an SDK in use."""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import tarfile
import tempfile
import urllib.request
from pathlib import Path


def download(url: str, destination: Path) -> None:
    if not url.startswith("https://"):
        raise ValueError("downloads require HTTPS")
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "pxa-devkit"}), timeout=60) as response, destination.open("wb") as stream:
        shutil.copyfileobj(response, stream, 1024 * 1024)


def unpack(archive: Path, checksum: str, destination: Path) -> Path:
    if destination.exists():
        raise ValueError("SDK installation destination already exists")
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != checksum.lower():
        raise ValueError("DevKit archive checksum mismatch")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".install-", dir=destination.parent) as temp:
        stage = Path(temp)
        with tarfile.open(archive, "r:gz") as package:
            members = package.getmembers()
            if len(members) > 200000 or sum(m.size for m in members) > 8 * 1024**3:
                raise ValueError("DevKit archive exceeds installation bounds")
            # Python's data filter rejects traversal, devices and escaping links.
            package.extractall(stage, filter="data")
        roots = list(stage.iterdir())
        if len(roots) != 1 or not roots[0].is_dir():
            raise ValueError("DevKit archive must contain one directory")
        root = roots[0]
        distribution = json.loads((root / "distribution.json").read_text())
        if distribution.get("schema") != "pxa-distribution-1":
            raise ValueError("unsupported distribution schema")
        for item in distribution["files"]:
            path = root / item["path"]
            if not path.resolve().is_relative_to(root):
                raise ValueError("invalid distribution inventory path")
            if "link" in item:
                if not path.is_symlink() or os.readlink(path) != item["link"]:
                    raise ValueError("DevKit inventory link mismatch: " + item["path"])
                continue
            if path.is_symlink() or not path.is_file():
                raise ValueError("unexpected inventory symlink")
            with path.open("rb") as stream:
                actual = hashlib.file_digest(stream, "sha256").hexdigest()
            if actual != item["sha256"]:
                raise ValueError("DevKit inventory mismatch: " + item["path"])
        if not (root / "bin/pxa").is_file():
            raise ValueError("DevKit entry point missing")
        root.rename(destination)
    return destination


def install(args, user_root: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="pxa-download-") as temp:
        if args.archive:
            if not args.sha256:
                raise ValueError("offline install requires --sha256")
            archive, checksum = args.archive.resolve(), args.sha256
            name = archive.name.removesuffix(".tar.gz")
        else:
            if not args.version:
                raise ValueError("specify --version or --archive with --sha256")
            from metadata import SEMVER
            if not SEMVER.fullmatch(args.version):
                raise ValueError("invalid SDK release version")
            name = f"pxa-devkit-{args.version}-linux-x86_64"
            base = f"https://github.com/SmartArduino/pxa-system/releases/download/v{args.version}/"
            archive = Path(temp) / (name + ".tar.gz")
            checksum_file = Path(temp) / "checksum"
            download(base + archive.name + ".sha256", checksum_file)
            checksum = checksum_file.read_text().split()[0]
            download(base + archive.name, archive)
        destination = args.destination or user_root / "sdks" / name
        result = unpack(archive, checksum, destination.resolve())
        print(f"Installed {result}; use {result / 'bin/pxa'}")
