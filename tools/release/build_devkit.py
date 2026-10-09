#!/usr/bin/env python3
"""Assemble a relocatable, offline Linux DevKit from verified build inputs."""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

import metadata


def copy_tree(source: Path, destination: Path) -> None:
    shutil.copytree(source, destination, symlinks=True,
                    ignore=shutil.ignore_patterns("__pycache__", ".git", ".pxa", "build", "*.pyc"))


def executable(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)
    path.chmod(0o755)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", required=True, type=Path)
    parser.add_argument("--simulator-dir", required=True, type=Path)
    parser.add_argument("--host-runtime", required=True, type=Path,
                        help="baseline runtime containing bin/, lib/, licenses/")
    parser.add_argument("--wamrc", required=True, type=Path)
    parser.add_argument("--wasi-sdk", required=True, type=Path)
    parser.add_argument("--python-root", required=True, type=Path)
    parser.add_argument("--python-site", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--licenses", required=True, type=Path,
                        help="collected pinned Python/toolchain/LVGL/font license notices")
    parser.add_argument("--allow-dirty", action="store_true", help="development builds only")
    parser.add_argument("--minimum-glibc", default="2.35")
    args = parser.parse_args()
    root = metadata.ROOT
    metadata.check(root)
    source_dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True).strip())
    workspace_dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=args.workspace, text=True).strip())
    dirty = source_dirty or workspace_dirty
    if dirty and not args.allow_dirty:
        raise SystemExit("commit release inputs first (or use --allow-dirty for local validation)")
    version = metadata.release(root)
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", subprocess.check_output(
        ["git", "show", "-s", "--format=%ct", "HEAD"], cwd=root, text=True).strip()))
    destination = args.output.resolve()
    if destination.exists():
        raise SystemExit("output must be a new directory")
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".devkit-", dir=destination.parent) as temp:
        stage = Path(temp) / "stage"
        stage.mkdir()
        shutil.copy2(root / "LICENSE", stage / "LICENSE")
        copy_tree(args.licenses, stage / "licenses/third-party")
        (stage / "docs").mkdir()
        for name in ("DEVKIT.zh-CN.md", "VERSIONING.zh-CN.md", "BUILDING_RELEASE.zh-CN.md"):
            shutil.copy2(root / "docs" / name, stage / "docs" / name)
        shutil.copy2(root / "docs/DEVKIT.zh-CN.md", stage / "README.md")
        for name in ("guest-c", "guest-cpp", "cmake"):
            copy_tree(root / "sdk" / name, stage / "sdk" / name)
        copy_tree(root / "config", stage / "config")
        # Protocol/compiler inputs are part of the published SDK contract.
        copy_tree(root / "spec/draft", stage / "spec/draft")
        for name in ("package", "i18n", "wamr", "release", "devkit"):
            copy_tree(root / "tools" / name, stage / "tools" / name)
        for unwanted in ("build_wamrc.sh", "test_guest_cpp.sh", "test_guest_sdk.sh"):
            (stage / "tools/package" / unwanted).unlink(missing_ok=True)
        shutil.copy2(root / "VERSION", stage / "VERSION")
        (stage / "idf_component.yml").write_text(f'version: "{version}"\n')
        # Metadata check also verifies the Host/Core version, for SDK diagnostics.
        for name in ("version.h", "wire.h"):
            target = stage / "libpxa/include/pxa" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(root / "libpxa/include/pxa" / name, target)
        shutil.copy2(args.workspace / "tools/pxadb/pxadb.py", stage / "tools/devkit/pxadb.py")
        shutil.copy2(args.workspace / "tools/simulator_pxadb.py", stage / "tools/devkit/simulator_pxadb.py")
        copy_tree(args.python_root, stage / "runtime/python")
        copy_tree(args.python_site, stage / "runtime/python-site")
        copy_tree(args.host_runtime / "lib", stage / "runtime/lib")
        copy_tree(args.host_runtime / "licenses", stage / "licenses/host-runtime")
        shutil.copy2(args.host_runtime / "runtime.json", stage / "licenses/host-runtime/runtime.json")
        shutil.copy2(root / "config/devkit-linux-x86_64.json", stage / "licenses/third-party/build-inputs.json")
        (stage / "runtime/bin").mkdir()
        for name in ("pxsys_desktop_simulator", "pxsys_product_simulator", "pxsys_package_installer"):
            shutil.copy2(args.simulator_dir / name, stage / "runtime/bin" / name)
        shutil.copy2(args.wamrc, stage / "runtime/bin/wamrc")
        shutil.copy2(args.host_runtime / "bin/openssl", stage / "runtime/bin/openssl")
        copy_tree(args.wasi_sdk, stage / "toolchains/wasi-sdk")
        font = stage / "share/pxa/fonts/system.ttf"
        font.parent.mkdir(parents=True)
        shutil.copy2(args.workspace / "factory/base/system/fonts/noto_sans_cjk_common.ttf", font)
        profiles = stage / "share/pxa/profiles"
        profiles.mkdir(parents=True)
        import tomllib
        for path in sorted((args.workspace / "simulator/profiles").glob("*.toml")):
            source = tomllib.loads(path.read_text())["desktop"]
            names = {"width", "height", "density_dpi", "safe_insets", "corner_radius", "locale"}
            value = {key.replace("_", "-"): item for key, item in source.items() if key in names}
            (profiles / (path.stem + ".json")).write_text(json.dumps(value, indent=2) + "\n")
        # Minimal templates use the SDK without a Host checkout.
        for language, example in (("c", root / "sdk/guest-c/examples/ui-button"),
                                  ("cpp", root / "sdk/guest-cpp/examples/counter")):
            template = stage / "sdk/templates" / language
            template.mkdir(parents=True)
            for path in example.iterdir():
                if path.is_file() and path.suffix in (".c", ".cpp", ".json", ".txt"):
                    shutil.copy2(path, template / path.name)
            if language == "cpp":
                (template / "CMakeLists.txt").write_text(
                    "cmake_minimum_required(VERSION 3.20)\nproject(pxa_app CXX)\n"
                    "find_package(PxaGuest CONFIG REQUIRED)\n"
                    "pxa_add_app(pxa_main COMPONENT_ID main SOURCES main.cpp)\n")
        prefix = '''#!/usr/bin/env bash
set -euo pipefail
pxa_prefix="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
'''
        for name, script in (("pxa", "pxa.py"), ("pxadb", "pxadb.py")):
            executable(stage / "bin" / name, prefix +
                'export PYTHONPATH="$pxa_prefix/runtime/python-site"\n'
                'export PYTHONDONTWRITEBYTECODE=1\n'
                'export SSL_CERT_FILE="${SSL_CERT_FILE:-$pxa_prefix/runtime/python/lib/python3.13/site-packages/pip/_vendor/certifi/cacert.pem}"\n'
                'export LD_LIBRARY_PATH="$pxa_prefix/runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
                f'exec "$pxa_prefix/runtime/python/bin/python3" "$pxa_prefix/tools/devkit/{script}" "$@"\n')
        for name, target in (("wamrc", "wamrc"), ("openssl", "openssl"),
                             ("pxa-simulator", "pxsys_product_simulator"),
                             ("pxa-desktop", "pxsys_desktop_simulator"),
                             ("pxa-installer", "pxsys_package_installer")):
            executable(stage / "bin" / name, prefix +
                'export LD_LIBRARY_PATH="$pxa_prefix/runtime/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\n'
                'export PXA_SIMULATOR_FONT="$pxa_prefix/share/pxa/fonts/system.ttf"\n'
                f'exec "$pxa_prefix/runtime/bin/{target}" "$@"\n')
        for name, target in (("cmake", "cmake/data/bin/cmake"), ("ninja", "bin/ninja")):
            executable(stage / "bin" / name, prefix +
                f'exec "$pxa_prefix/runtime/python-site/{target}" "$@"\n')
        inputs = metadata.read_json(root, "config/devkit-linux-x86_64.json")
        wamr = metadata.read_json(root, "config/wamr.json")
        components = [
            {"type": "framework", "name": "PXA", "version": version},
            {"type": "library", "name": "WAMR", "version": wamr["commit"]},
            {"type": "library", "name": "Espressif LLVM", "version": wamr["llvm"]["commit"]},
            {"type": "application", "name": "WASI SDK", "version": inputs["wasi_sdk"]["version"]},
            {"type": "application", "name": "CPython", "version": inputs["python"]["version"]},
        ]
        from importlib.metadata import distributions
        installed = {d.metadata["Name"].lower(): d for d in distributions(path=[str(args.python_site)])}
        for name, expected in inputs["python_packages"].items():
            package = installed[name.lower()]
            if package.version != expected:
                raise ValueError("unpinned Python package: " + name)
            components.append({"type": "library", "name": name, "version": package.version,
                               "purl": f"pkg:pypi/{name.lower()}@{package.version}"})
        runtime = json.loads((args.host_runtime / "runtime.json").read_text())
        for name, item in sorted(runtime["libraries"].items()):
            components.append({"type": "library", "name": name, "version": item["version"],
                               "hashes": [{"alg": "SHA-256", "content": item["sha256"]}],
                               "properties": [{"name": "pxa:deb-package", "value": item["package"]}]})
        components.extend([
            {"type": "library", "name": "LVGL", "properties": [
                {"name": "pxa:workspace-lock-sha256", "value": metadata.digest(args.workspace / "firmware/dependencies.lock.pai-touch")}]},
            {"type": "file", "name": "Noto Sans CJK Chinese subset", "hashes": [
                {"alg": "SHA-256", "content": metadata.digest(font)}], "licenses": [{"license": {"id": "OFL-1.1"}}]},
        ])
        (stage / "sbom.cdx.json").write_text(json.dumps({"bomFormat": "CycloneDX", "specVersion": "1.6",
            "version": 1, "components": components}, indent=2, sort_keys=True) + "\n")
        # A distribution inventory also detects accidental source/secret inclusion.
        inventory = []
        for path in sorted(stage.rglob("*")):
            relative = str(path.relative_to(stage))
            if path.is_symlink():
                if not path.resolve().is_relative_to(stage):
                    raise ValueError(f"non-relocatable symlink: {relative}")
                inventory.append({"path": relative, "link": os.readlink(path)})
                continue
            if not path.is_file():
                continue
            is_public_certificate = (path.suffix == ".pem" and
                b"-----BEGIN CERTIFICATE-----" in path.read_bytes() and
                b"PRIVATE KEY" not in path.read_bytes())
            if (path.suffix in (".pem", ".key") and not is_public_certificate) or ".dev-signing" in relative:
                raise ValueError(f"signing key must not ship in DevKit: {relative}")
            inventory.append({"path": relative, "bytes": path.stat().st_size,
                              "sha256": metadata.digest(path)})
        toolchain_hash = hashlib.sha256(json.dumps(
            [item for item in inventory if item["path"].startswith("toolchains/") or
             item["path"] == "runtime/bin/wamrc"], sort_keys=True).encode()).hexdigest()
        source = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
        document = {"schema": "pxa-distribution-1", "release": version,
                    "source_date_epoch": epoch,
                    "source_commit": source, "host_platform": "linux-x86_64",
                    "source_dirty": source_dirty, "workspace_dirty": workspace_dirty,
                    "workspace_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=args.workspace, text=True).strip(),
                    "wamr": metadata.read_json(root, "config/wamr.json"),
                    "minimum_glibc": args.minimum_glibc, "toolchain_sha256": toolchain_hash,
                    "compatibility": metadata.profile(), "files": inventory}
        (stage / "distribution.json").write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")
        stage.rename(destination)
    archive = Path(str(destination) + ".tar.gz")
    def normalized(info):
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        info.mtime = epoch
        return info
    with archive.open("wb") as stream, gzip.GzipFile(fileobj=stream, mode="wb", filename="", mtime=epoch, compresslevel=6) as compressed:
        with tarfile.open(fileobj=compressed, mode="w") as package:
            package.add(destination, arcname=destination.name, filter=normalized)
    (archive.parent / (archive.name + ".sha256")).write_text(
        metadata.digest(archive) + "  " + archive.name + "\n")
    print(archive)


if __name__ == "__main__":
    main()
