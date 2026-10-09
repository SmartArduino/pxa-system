#!/usr/bin/env python3
"""Canonical release versions and capability metadata; no Guest dependency."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SEMVER = re.compile(
    r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-((?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*)"
    r"(?:\.(?:0|[1-9][0-9]*|[0-9]*[A-Za-z-][0-9A-Za-z-]*))*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?"
)


def release(root: Path = ROOT) -> str:
    value = (root / "VERSION").read_text(encoding="ascii").strip()
    if not SEMVER.fullmatch(value):
        raise ValueError(f"invalid release VERSION: {value!r}")
    return value


def digest(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for part in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(part)
    return sha.hexdigest()


def read_json(root: Path, name: str) -> dict:
    return json.loads((root / name).read_text(encoding="utf-8"))


def profile(root: Path = ROOT) -> dict:
    envelope = read_json(root, "spec/draft/abi-1.0-envelope.json")
    core = envelope["core_version"]
    registry = read_json(root, "spec/draft/pxa-core.json")["services"]
    services = {}
    for item in registry:
        if item.get("state") != "defined" or item["name"] == "core":
            continue
        spec = read_json(root, f"spec/draft/pxa-{item['name']}.json")
        actual = spec["service"]
        for key in ("name", "id", "major", "minor", "patch"):
            if actual[key] != item[key]:
                raise ValueError(f"inconsistent {item['name']} {key}")
        services[item["name"]] = {
            "id": item["id"], "version": [item["major"], item["minor"]],
            "features": {f["name"]: f["bit"] for f in spec.get("features", [])},
        }
    wamr = read_json(root, "config/wamr.json")
    return {
        "schema": "pxa-compatibility-profile-1",
        "pxa_release": release(root),
        "core": [core["major"], core["minor"]],
        "envelope_bytes": envelope["size"],
        "services": services,
        "manifest_format": read_json(root, "spec/draft/pxa-package.json")["protocol_version"],
        "container_format": read_json(root, "spec/draft/pxa-container.json")["protocol_version"],
        "engine_abi": wamr["engine_abi"],
        "host_c_abi": read_json(root, "config/host-abi.json")["soversion"],
    }


def version_headers(root: Path = ROOT) -> dict[Path, str]:
    value = release(root)
    match = SEMVER.fullmatch(value)
    assert match is not None
    major, minor, patch = match.group(1, 2, 3)
    c = (
        "/* Generated from VERSION by tools/release/metadata.py. */\n"
        "#ifndef PXA_VERSION_H\n#define PXA_VERSION_H\n\n"
        f"#define PXA_VERSION_MAJOR {major}\n#define PXA_VERSION_MINOR {minor}\n"
        f"#define PXA_VERSION_PATCH {patch}\n#define PXA_VERSION_STRING \"{value}\"\n"
        f"#define PXA_HOST_C_ABI {profile(root)['host_c_abi']}\n\n#endif\n"
    )
    cpp = (
        "// Generated from VERSION by tools/release/metadata.py.\n#pragma once\n"
        "namespace pxa {\n"
        f"inline constexpr char sdk_version[] = \"{value}\";\n"
        f"inline constexpr unsigned sdk_version_major = {major};\n"
        f"inline constexpr unsigned sdk_version_minor = {minor};\n"
        f"inline constexpr unsigned sdk_version_patch = {patch};\n"
        "}\n"
    )
    return {
        root / "libpxa/include/pxa/version.h": c,
        root / "sdk/guest-c/include/pxa_version.h": c,
        root / "sdk/guest-cpp/include/pxa/version.hpp": cpp,
    }


def check(root: Path = ROOT) -> None:
    for path, expected in version_headers(root).items():
        if not path.is_file() or path.read_text() != expected:
            raise ValueError(f"stale version header: {path.relative_to(root)}")
    text = (root / "idf_component.yml").read_text()
    if f'version: "{release(root)}"' not in text:
        raise ValueError("idf_component.yml version differs from VERSION")
    # Core registry entry describes the internal mailbox, not Guest imports.
    wire = (root / "libpxa/include/pxa/wire.h").read_text()
    core = profile(root)["core"]
    for label, number in zip(("MAJOR", "MINOR"), core):
        if not re.search(rf"PXA_CORE_VERSION_{label}\s+UINT16_C\({number}\)", wire):
            raise ValueError("Host Core wire version differs from v1 specification")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("version", "profile", "check", "generate"))
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    if args.command == "version":
        print(release(args.root))
    elif args.command == "profile":
        print(json.dumps(profile(args.root), indent=2, sort_keys=True))
    elif args.command == "generate":
        for path, content in version_headers(args.root).items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")
        component = args.root / "idf_component.yml"
        component.write_text(re.sub(r'^version:.*$', f'version: "{release(args.root)}"',
                                   component.read_text(), count=1, flags=re.M))
    else:
        check(args.root)
        print("PXA release versions and compatibility metadata OK")


if __name__ == "__main__":
    main()
