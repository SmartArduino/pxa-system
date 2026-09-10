#!/usr/bin/env python3
"""Read the centrally pinned PXA WAMR toolchain metadata."""

from __future__ import annotations

import argparse
import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_METADATA = ROOT / "config/wamr.json"


def load(path: pathlib.Path = DEFAULT_METADATA) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def lookup(document: dict[str, object], key: str) -> object:
    value: object = document
    for part in key.split("."):
        if not isinstance(value, dict) or part not in value:
            raise KeyError(key)
        value = value[part]
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("key", help="dotted metadata key, for example llvm.commit")
    parser.add_argument("--metadata", type=pathlib.Path, default=DEFAULT_METADATA)
    arguments = parser.parse_args()
    value = lookup(load(arguments.metadata), arguments.key)
    if isinstance(value, (dict, list)):
        print(json.dumps(value, separators=(",", ":")))
    else:
        print(value)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
