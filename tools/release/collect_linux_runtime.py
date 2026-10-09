#!/usr/bin/env python3
"""Run inside the baseline Debian/Ubuntu builder, never against the build host."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("binaries", nargs="+", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for folder in ("bin", "lib", "licenses"):
        (args.output / folder).mkdir(exist_ok=True)
    openssl = Path(shutil.which("openssl"))
    shutil.copy2(openssl, args.output / "bin/openssl")
    excluded = re.compile(r"(?:ld-linux.*|lib(?:c|m|dl|pthread|rt|resolv|util)\.so(?:\..*)?)$")
    files = {}
    packages = subprocess.check_output(["dpkg-query", "-W", "-f=${binary:Package}\t${source:Package}\t${source:Version}\n"], text=True)
    package_sources = [line.split("\t") for line in packages.splitlines()]
    for binary in [openssl, *args.binaries]:
        result = subprocess.run(["ldd", str(binary)], check=True, capture_output=True, text=True)
        for line in result.stdout.splitlines():
            if "not found" in line:
                raise ValueError(f"unresolved dependency: {binary}: {line}")
            match = re.search(r"(?:=> )?(/[^ ]+)", line)
            if not match:
                continue
            path = Path(match[1])
            if excluded.fullmatch(path.name):
                continue
            output = args.output / "lib" / path.name
            if output.exists() and output.read_bytes() != path.read_bytes():
                raise ValueError("conflicting private library: " + path.name)
            shutil.copy2(path.resolve(), output)
            try:
                owner = subprocess.check_output(["dpkg-query", "-S", str(path.resolve())], text=True).split(": ")[0]
            except subprocess.CalledProcessError:
                owner = subprocess.check_output(["dpkg-query", "-S", str(path)], text=True).split(": ")[0]
            package = owner.split(":")[0]
            copyright_path = Path("/usr/share/doc") / package / "copyright"
            if not copyright_path.is_file():
                # Minimal builder images may strip runtime documentation. An
                # installed sibling from the same source/version carries the
                # identical upstream and Debian notices (e.g. liblz4-dev).
                source = subprocess.check_output(["dpkg-query", "-W", "-f=${source:Package}\t${source:Version}", owner], text=True).split("\t")
                candidates = [Path("/usr/share/doc") / name.split(":")[0] / "copyright"
                              for name, source_name, source_version in package_sources
                              if [source_name, source_version] == source]
                copyright_path = next((p for p in candidates if p.is_file()), copyright_path)
            if not copyright_path.is_file():
                raise ValueError("missing dependency license: " + package + "; install matching package documentation")
            shutil.copy2(copyright_path, args.output / "licenses" / (package + ".txt"))
            files[path.name] = {"package": owner,
                               "version": subprocess.check_output(["dpkg-query", "-W", "-f=${Version}", owner], text=True),
                               "copyright_source": str(copyright_path),
                               "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}
    shutil.copy2("/usr/share/doc/openssl/copyright", args.output / "licenses/openssl.txt")
    (args.output / "runtime.json").write_text(json.dumps({
        "os": Path("/etc/os-release").read_text(),
        "glibc": subprocess.check_output(["getconf", "GNU_LIBC_VERSION"], text=True).strip(),
        "libraries": files}, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
