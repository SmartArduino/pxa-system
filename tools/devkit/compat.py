"""Bounded Manifest inspection after native signature verification."""
from __future__ import annotations

import struct
from pathlib import Path


def records(data: bytes):
    offset = 0
    while offset < len(data):
        if len(data) - offset < 4:
            raise ValueError("truncated record")
        tag, size = struct.unpack_from("<HH", data, offset)
        offset += 4
        if size > len(data) - offset:
            raise ValueError("truncated record payload")
        yield tag & 0x7FFF, data[offset:offset + size]
        offset += size


def singleton(data: bytes) -> dict[int, bytes]:
    result = {}
    for tag, value in records(data):
        if tag in result:
            raise ValueError(f"duplicate singleton record {tag}")
        result[tag] = value
    return result


def manifest_bytes(source: Path) -> bytes:
    with (source / "manifest.pxm" if source.is_dir() else source).open("rb") as stream:
        if source.is_dir():
            value = stream.read(1024 * 1024 + 1)
        else:
            header = stream.read(64)
            if len(header) != 64 or header[:4] != b"PXAC":
                raise ValueError("invalid container header")
            size = struct.unpack_from("<I", header, 16)[0]
            if not 12 <= size <= 1024 * 1024:
                raise ValueError("manifest exceeds inspection bound")
            value = stream.read(size)
    if len(value) > 1024 * 1024 or len(value) < 12 or value[:4] != b"PXAM" or \
            struct.unpack_from("<I", value, 8)[0] != len(value) - 12:
        raise ValueError("invalid manifest length")
    return value


def identity(source: Path) -> str:
    found = [v for tag, v in records(manifest_bytes(source)[12:]) if tag == 2]
    if len(found) != 1:
        raise ValueError("missing or duplicate App identity")
    return found[0].decode("ascii")


def analyze(encoded: bytes, host: dict) -> dict:
    fields = list(records(encoded[12:]))
    core_values = [v for tag, v in fields if tag == 7]
    if len(core_values) != 1 or len(core_values[0]) != 4:
        raise ValueError("missing Core requirement")
    core = struct.unpack("<HH", core_values[0])
    errors = []
    if host["core"][0] != core[0] or host["core"][1] < core[1]:
        errors.append(f"Core {list(core)} required, Host provides {host['core']}")
    available = {s["id"]: s for s in host["services"]}
    selected = {}
    for tag, payload in fields:
        if tag != 16:
            continue
        component_fields = list(records(payload))
        name = next(v.decode("ascii") for t, v in component_fields if t == 1)
        for t, value in component_fields:
            if t != 5:
                continue
            req = singleton(value)
            service_id = struct.unpack("<H", req[1])[0]
            minimum = struct.unpack("<HH", req[2])
            maximum = struct.unpack("<HH", req[3])
            features = struct.unpack("<Q", req[4])[0]
            actual = available.get(service_id)
            if actual is None:
                errors.append(f"{name}: missing service {service_id}")
            elif not (actual["version"][0] == minimum[0] == maximum[0] and
                      minimum[1] <= actual["version"][1] <= maximum[1]):
                errors.append(f"{name}: service {service_id} requires {minimum}..{maximum}, "
                              f"Host provides {actual['version']}")
            elif features & ~actual["features"]:
                errors.append(f"{name}: service {service_id} lacks features 0x{features & ~actual['features']:x}")
        candidates = []
        for t, value in component_fields:
            if t != 4:
                continue
            artifact = singleton(value)
            kind = artifact[1][0]
            required_features = struct.unpack("<Q", artifact[6])[0]
            memory_model = artifact[7][0]
            if memory_model != host["memory_model"] or required_features & ~host.get("artifact_features", 0):
                continue
            if kind == 2 and (artifact[3].decode("ascii") != host["target"] or
                              artifact[4].decode("ascii") != host["engine"] or
                              artifact[5].decode("ascii") != host["engine_abi"]):
                continue
            if kind == 1 and not host.get("wasm", False):
                continue
            candidates.append((kind, required_features.bit_count(), artifact[2].decode("ascii")))
        if not candidates:
            errors.append(f"{name}: no runnable Artifact for {host['target']} / {host['engine_abi']}")
        else:
            selected[name] = sorted(candidates, key=lambda x: (-x[0], -x[1], x[2]))[0][2]
    return {"compatible": not errors, "errors": errors, "artifacts": selected,
            "core_required": list(core), "host_release": host.get("pxa_release"),
            "budget_check": "Host validates runtime quotas during activation"}
