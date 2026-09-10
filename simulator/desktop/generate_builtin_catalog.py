#!/usr/bin/env python3
"""Generate the desktop simulator's built-in catalog from PXA manifests."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--apps", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    entries: list[dict[str, str]] = []
    for manifest_path in sorted(args.apps.glob("*/package.json")):
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        entry = {
            "id": manifest["id"],
            "name": manifest["name"],
            "version": manifest["version"],
            "description": manifest.get("description", "PXA reference application"),
        }
        for key, value in entry.items():
            if not isinstance(value, str) or not value:
                raise ValueError(f"{manifest_path}: {key} must be a non-empty string")
        entries.append(entry)
    if not entries:
        raise ValueError(f"no package.json files found below {args.apps}")

    lines = [
        "/* Generated from apps/pxa package manifests. Do not edit. */",
        "static const pxsys_desktop_builtin_app_t k_builtin_apps[] = {",
    ]
    for entry in entries:
        lines.append(
            "    {%s, %s, %s, %s},"
            % tuple(c_string(entry[key]) for key in ("id", "name", "version", "description"))
        )
    lines.extend(["};", ""])
    output = "\n".join(lines)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != output:
        args.output.write_text(output, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
