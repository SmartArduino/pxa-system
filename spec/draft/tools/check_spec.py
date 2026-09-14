#!/usr/bin/env python3
"""Validate the consolidated PXA draft and its checked-in golden vectors."""

from __future__ import annotations

import json
import pathlib
import struct


ROOT = pathlib.Path(__file__).resolve().parent.parent
GOLDEN = ROOT / "golden"


class SpecError(RuntimeError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SpecError(message)


def load_json(path: pathlib.Path) -> dict[str, object]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SpecError(f"cannot parse {path.relative_to(ROOT)}: {error}") from error
    require(isinstance(value, dict), f"{path.name} must contain a JSON object")
    return value


def version_of(service: dict[str, object]) -> str:
    return ".".join(str(service[key]) for key in ("major", "minor", "patch"))


def unique(items: list[dict[str, object]], field: str, label: str) -> None:
    values = [item[field] for item in items]
    require(len(values) == len(set(values)), f"duplicate {label}")


def validate_named_registries(document: dict[str, object], label: str) -> None:
    for section, value in document.items():
        if not isinstance(value, list) or not value or not all(isinstance(item, dict) for item in value):
            continue
        items = value
        if all("name" in item for item in items):
            unique(items, "name", f"{label} {section} name")
        for numeric in ("id", "value", "bit", "opcode"):
            if all(numeric in item for item in items):
                unique(items, numeric, f"{label} {section} {numeric}")
                break


def parse_hex(value: object, label: str) -> bytes:
    require(isinstance(value, str), f"{label} must be a hexadecimal string")
    try:
        return bytes.fromhex(value)
    except ValueError as error:
        raise SpecError(f"{label} contains invalid hexadecimal data") from error


def encode_records(items: list[dict[str, object]]) -> bytes:
    output = bytearray()
    for item in items:
        tag = item["tag"]
        require(isinstance(tag, int) and 0 <= tag <= 0xFFFF,
                "golden record tag is out of range")
        payload = parse_hex(item["payload_hex"], "golden record payload")
        require(len(payload) <= 0xFFFF, "golden record payload is too large")
        output += struct.pack("<HH", tag, len(payload)) + payload
    return bytes(output)


def validate_core_golden(document: dict[str, object]) -> int:
    require(document.get("schema") == "pxa-core-golden-0.1",
            "Core golden schema mismatch")
    require(document.get("generated_by") == "tools/generate_golden.py",
            "Core golden generator metadata is stale")
    vectors = document.get("vectors")
    require(isinstance(vectors, list), "Core golden vectors must be an array")
    names = [item["name"] for item in vectors]
    require(len(names) == len(set(names)), "duplicate Core golden vector name")
    for vector in vectors:
        kind = vector.get("kind")
        if kind == "message":
            item = vector["message"]
            payload = parse_hex(item["payload_hex"], f"{vector['name']} payload")
            encoded = struct.pack(
                "<HHII", item["service"], item["opcode"],
                item["request_id"], len(payload)
            ) + payload
            require(len(encoded) >= 12, f"{vector['name']} envelope is truncated")
            service, opcode, request_id, payload_size = struct.unpack_from("<HHII", encoded)
            require(payload_size == len(encoded) - 12,
                    f"{vector['name']} envelope length mismatch")
            require([service, opcode, request_id] ==
                    [item["service"], item["opcode"], item["request_id"]],
                    f"{vector['name']} does not round-trip")
        elif kind == "records":
            encoded = encode_records(vector["records"])
        else:
            raise SpecError(f"unknown Core golden vector kind {kind!r}")
        require(encoded == parse_hex(vector["wire_hex"], f"{vector['name']} wire"),
                f"Core golden vector {vector['name']} is stale")
    required = {
        "clock.set-period", "clock.tick",
        "fs.open", "fs.read-directory", "storage.set", "storage.get",
        "ipc.call", "ipc.request", "ipc.result", "permission.acquire",
        "permission.revoked", "audio.open-session", "audio.commit-graph",
        "work.enqueue", "work.cancel", "work.complete", "device.get-mac",
    }
    require(not required.difference(names),
            f"missing Core golden vectors: {sorted(required.difference(names))}")
    return len(vectors)


def validate_package_golden(document: dict[str, object], package: dict[str, object]) -> int:
    require(document.get("schema") == "pxa-package-golden-0.1",
            "Package golden schema mismatch")
    require(document.get("generated_by") == "tools/generate_package_golden.py",
            "Package golden generator metadata is stale")
    manifest = parse_hex(document["manifest_hex"], "Package golden manifest")
    signature = parse_hex(document["signature_hex"], "Package golden signature")
    signed = parse_hex(document["signature_message_hex"], "Package signed message")
    require(manifest[:4] == b"PXAM" and len(manifest) >= 12,
            "Package golden manifest header mismatch")
    major, minor, body_size = struct.unpack_from("<HHI", manifest, 4)
    require((major, minor) == (0, 1), "Package golden manifest version mismatch")
    require(len(manifest) == 12 + body_size, "Package golden manifest length mismatch")
    require(len(manifest) <= package["manifest"]["max_size"],
            "Package golden manifest exceeds the specified limit")
    require(signature[:4] == b"PXAS" and len(signature) == 108,
            "Package golden signature layout mismatch")
    require(struct.unpack_from("<H", signature, 4)[0] == 1,
            "Package golden signature version mismatch")
    require(signed == b"PXA-PACKAGE-MANIFEST\0" + manifest,
            "Package golden signed message mismatch")
    inventory = document.get("inventory")
    require(isinstance(inventory, list), "Package golden inventory must be an array")
    unique(inventory, "path", "Package golden inventory path")
    return len(inventory)


def validate_specs() -> tuple[int, int]:
    paths = sorted(ROOT.glob("pxa-*.json"))
    require(len(paths) == 17, "the consolidated draft must contain 17 machine specifications")
    require(not list(ROOT.glob("pxa-*.yaml")),
            "machine specifications must use the .json extension")
    specs = {path.stem.removeprefix("pxa-"): load_json(path) for path in paths}
    require(set(specs) == {
        "audio", "clock", "container", "core", "device", "fs", "ipc",
        "net", "package", "permission", "sensor", "storage", "surface", "ui",
        "wasi", "window", "work",
    }, "machine specification inventory mismatch")

    core = specs["core"]
    require(core.get("schema") == "pxa-core-spec-0.1", "Core schema mismatch")
    require(core.get("status") == "draft", "Core must remain marked draft")
    require(core.get("protocol_version") == version_of(core["abi"]),
            "Core protocol version is stale")
    abi = core["abi"]
    require(abi["encoded"] == (abi["major"] << 16 | abi["minor"]),
            "Core encoded ABI version mismatch")
    require(abi["byte_order"] == "little-endian", "Core byte order mismatch")
    envelope = core["envelope"]
    require(envelope["size"] == 12, "Core envelope size mismatch")
    require([(field["name"], field["type"], field["offset"])
             for field in envelope["fields"]] == [
                 ("service", "u16", 0), ("opcode", "u16", 2),
                 ("request_id", "u32", 4), ("payload_len", "u32", 8),
             ], "Core envelope layout mismatch")
    validate_named_registries(core, "Core")

    expected_services = {
        "window": (2, "0.1.0"), "ui": (3, "0.3.0"),
        "clock": (4, "0.1.0"), "fs": (5, "0.1.0"),
        "storage": (6, "0.1.0"), "ipc": (7, "0.1.0"),
        "sensor": (8, "0.1.0"), "net": (9, "0.2.0"),
        "audio": (10, "0.5.0"), "permission": (11, "0.1.0"),
        "work": (13, "0.1.0"), "wasi": (14, "0.1.0"),
        "device": (15, "0.1.0"), "surface": (16, "0.2.0"),
    }
    core_services = {item["name"]: item for item in core["services"]}
    for name, (service_id, version) in expected_services.items():
        document = specs[name]
        service = document["service"]
        require(document.get("status") == "draft", f"{name} must remain marked draft")
        require(document.get("protocol_version") == version,
                f"{name} protocol version metadata mismatch")
        require(version_of(service) == version and service["id"] == service_id,
                f"{name} service declaration mismatch")
        require(document.get("schema") == f"pxa-{name}-spec-{version.rsplit('.', 1)[0]}",
                f"{name} schema version mismatch")
        core_service = core_services[name]
        require(core_service.get("state") == "defined" and
                core_service["id"] == service_id and
                version_of(core_service) == version,
                f"Core and {name} service versions differ")
        validate_named_registries(document, name)

    package = specs["package"]
    container = specs["container"]
    require(package.get("protocol_version") == "0.6.0",
            "Package manifest protocol version mismatch")
    require(container.get("protocol_version") == "0.1.0",
            "Container protocol version mismatch")
    require(package["manifest"] == {
        "magic": "PXAM", "major": 0, "minor": 6, "patch": 0,
        "header_size": 12, "max_size": 16384,
    }, "Package manifest constants mismatch")
    require(container["header"]["magic"] == "PXAC" and
            container["header"]["size"] == 64,
            "Container header constants mismatch")
    validate_named_registries(package, "Package")
    validate_named_registries(container, "Container")

    ui = specs["ui"]
    require(len(ui["opcodes"]) == 15, "UI opcode inventory mismatch")
    require(len(ui["commands"]) == 5, "UI command inventory mismatch")
    require({feature["name"] for feature in ui["features"]} >=
            {"canvas", "virtual-list", "controller-input", "canvas-stream-io"},
            "UI required feature registry is incomplete")

    core_count = validate_core_golden(load_json(GOLDEN / "core-vectors.json"))
    package_count = validate_package_golden(
        load_json(GOLDEN / "package-vectors.json"), package)
    ui_golden = load_json(GOLDEN / "ui-vectors.json")
    require(ui_golden.get("schema") == "pxa-ui-golden-0.3",
            "UI golden schema mismatch")
    return core_count, package_count


def main() -> int:
    try:
        core_count, package_count = validate_specs()
    except (KeyError, TypeError, SpecError) as error:
        print(f"PXA spec error: {error}")
        return 1
    print(f"PXA consolidated draft OK ({core_count} Core vectors, "
          f"{package_count} package payloads)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
