#!/usr/bin/env python3
"""Derive the smaller SDK and simulator assets from an accepted DevKit."""
from __future__ import annotations

import argparse
import gzip
import json
import os
import shutil
import sys
import tarfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "devkit"))
from pxa import inventory_matches
import metadata


def archive(root: Path, epoch: int) -> Path:
    result = Path(str(root) + ".tar.gz")
    def normalize(info):
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        info.mtime = epoch
        return info
    with result.open("wb") as file, gzip.GzipFile(fileobj=file, mode="wb", filename="", mtime=epoch) as compressed:
        with tarfile.open(fileobj=compressed, mode="w") as package:
            package.add(root, arcname=root.name, filter=normalize)
    Path(str(result) + ".sha256").write_text(metadata.digest(result) + "  " + result.name + "\n")
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devkit", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    kit = args.devkit.resolve()
    document = json.loads((kit / "distribution.json").read_text())
    if document.get("schema") != "pxa-distribution-1" or document.get("source_dirty") or document.get("workspace_dirty"):
        raise ValueError("component assets require a clean-source DevKit")
    parents = {}
    for item in document["files"]:
        if not inventory_matches(kit, item, parents):
            raise ValueError("DevKit inventory mismatch: " + item["path"])
    args.output.mkdir(parents=True, exist_ok=False)
    version = document["release"]
    selections = {
        "sdk": (f"pxa-sdk-{version}", ("sdk/", "config/", "spec/", "libpxa/", "tools/package/", "tools/i18n/", "tools/wamr/", "tools/release/metadata.py")),
        "simulator": (f"pxa-simulator-{version}-linux-x86_64", ("runtime/lib/", "runtime/bin/pxsys_", "share/", "bin/pxa-simulator", "bin/pxa-desktop", "bin/pxa-installer")),
    }
    for component, (name, prefixes) in selections.items():
        root = args.output / name
        root.mkdir()
        selected = [item for item in document["files"] if item["path"].startswith(
            (*prefixes, "licenses/", "docs/", "LICENSE", "VERSION", "idf_component.yml"))]
        for item in selected:
            source = kit / item["path"]
            target = root / item["path"]
            target.parent.mkdir(parents=True, exist_ok=True)
            if "link" in item:
                target.symlink_to(item["link"])
            else:
                shutil.copy2(source, target)
        readme = ("# PXA C/C++ SDK\n\nGuest headers, runtime sources, CMake and packaging tools. "
                  "The matching DevKit supplies the pinned WASI SDK, wamrc and Python tools. "
                  "Set WAMRC, WASI_SDK_DIR and PYTHON when using the packager directly. "
                  "For the supported offline workflow, use bin/pxa from the full DevKit.\n"
                  if component == "sdk" else
                  "# PXA Linux x86_64 simulator\n\nRequires glibc 2.35+, Bash and a display server. "
                  "Run bin/pxa-simulator --package APP.pxa --publisher-key publisher.der "
                  "--state-root /writable/app-state. bin/pxa-desktop provides the standard system demo. "
                  "Headless tests use SDL_VIDEODRIVER=dummy and SDL_AUDIODRIVER=dummy. "
                  "For project build/run, PXADB and profile selection use the matching full DevKit.\n")
        (root / "README.md").write_text(readme + "\nSee docs/DEVKIT.zh-CN.md and docs/VERSIONING.zh-CN.md.\n")
        bom = json.loads((kit / "sbom.cdx.json").read_text())
        wanted = {"PXA"} if component == "sdk" else {"PXA", "WAMR", "LVGL", "Noto Sans CJK Chinese subset"}
        bom["components"] = [item for item in bom["components"] if item["name"] in wanted or
                             (component == "simulator" and item["name"].startswith("lib"))]
        (root / "sbom.cdx.json").write_text(json.dumps(bom, indent=2, sort_keys=True) + "\n")
        files = selected + [{"path": generated, "bytes": (root / generated).stat().st_size,
                             "sha256": metadata.digest(root / generated)}
                            for generated in ("README.md", "sbom.cdx.json")]
        (root / "component.json").write_text(json.dumps({
            "schema": "pxa-component-distribution-1", "component": component,
            "release": version, "source_commit": document["source_commit"],
            "parent_distribution_sha256": metadata.digest(kit / "distribution.json"),
            "files": files}, indent=2, sort_keys=True) + "\n")
        print(archive(root, document["source_date_epoch"]))


if __name__ == "__main__":
    main()
