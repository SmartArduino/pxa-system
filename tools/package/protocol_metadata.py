#!/usr/bin/env python3
"""Load package-facing service metadata from the normative JSON specs."""

from __future__ import annotations

import json
from pathlib import Path


SPEC_ROOT = Path(__file__).resolve().parents[2] / "spec/draft"


def read_spec(name: str) -> dict[str, object]:
    path = SPEC_ROOT / f"pxa-{name}.json"
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError(f"{path} must contain an object")
    return value


def load_services() -> tuple[dict[str, int], dict[int, tuple[int, int]]]:
    core = read_spec("core")
    entries = core.get("services")
    if not isinstance(entries, list):
        raise RuntimeError("pxa-core.json has no service registry")
    identifiers: dict[str, int] = {}
    versions: dict[int, tuple[int, int]] = {}
    for entry in entries:
        if not isinstance(entry, dict) or entry.get("state") != "defined":
            continue
        name = entry.get("name")
        service_id = entry.get("id")
        major = entry.get("major")
        minor = entry.get("minor")
        if not (isinstance(name, str) and isinstance(service_id, int) and
                isinstance(major, int) and isinstance(minor, int)):
            raise RuntimeError("invalid defined service in pxa-core.json")
        if name != "core":
            service = read_spec(name).get("service")
            expected = {"name": name, "id": service_id, "major": major,
                        "minor": minor, "patch": entry.get("patch")}
            if service != expected:
                raise RuntimeError(f"Core and {name} service metadata differ")
        identifiers[name] = service_id
        versions[service_id] = (major, minor)
    return identifiers, versions


def load_wasi_features() -> dict[str, int]:
    features = read_spec("wasi").get("features")
    if not isinstance(features, list):
        raise RuntimeError("pxa-wasi.json has no feature registry")
    result: dict[str, int] = {}
    for feature in features:
        if not isinstance(feature, dict):
            raise RuntimeError("invalid WASI feature metadata")
        name = feature.get("name")
        bit = feature.get("bit")
        if not isinstance(name, str) or not isinstance(bit, int) or not 0 <= bit < 64:
            raise RuntimeError("invalid WASI feature metadata")
        result[name] = 1 << bit
    return result


SERVICE_IDS, SERVICE_VERSIONS = load_services()
DECLARABLE_SERVICE_IDS = {
    name: value for name, value in SERVICE_IDS.items() if name != "wasi"
}
WASI_FEATURES = load_wasi_features()
