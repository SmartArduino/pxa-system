#!/usr/bin/env python3
"""Build a signed, architecture-independent immutable PXA resource package."""

import argparse
import hashlib
import json
from pathlib import Path

from build_package_manifest import PackageError, main as build_manifest, require
from build_pxa_container import build as build_container


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("metadata", type=Path)
    parser.add_argument("package_directory", type=Path)
    parser.add_argument("private_key", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--codec", choices=("store", "lz4"), default="lz4")
    parser.add_argument("--descriptor", type=Path,
                        help="write the manifest binding to use in the application package")
    args = parser.parse_args()
    metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
    require(metadata.get("format") == "pxa-package-source-0.2" and
            metadata.get("package_type") == "resources",
            "resource metadata requires source format 0.2 and package_type resources")
    build_manifest([str(args.metadata), str(args.package_directory), str(args.private_key),
                    "resources", "none"])
    build_container(args.package_directory.resolve(), str(args.private_key),
                    args.output.resolve(), int(args.codec == "lz4"))
    if args.descriptor:
        digest = hashlib.sha256((args.package_directory / "manifest.pxm").read_bytes()).hexdigest()
        args.descriptor.parent.mkdir(parents=True, exist_ok=True)
        args.descriptor.write_text(json.dumps({"resources": {"manifest_sha256": digest}},
                                             indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, PackageError) as error:
        raise SystemExit(f"error: {error}") from error
