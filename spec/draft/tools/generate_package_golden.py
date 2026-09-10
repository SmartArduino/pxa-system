#!/usr/bin/env python3
"""Generate the normative PXA package manifest and signature vectors."""

from __future__ import annotations

import hashlib
import json
import pathlib
import struct
import sys


ROOT = pathlib.Path(__file__).resolve().parent.parent
OUTPUT = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "golden/package-vectors.json"


def record(tag: int, payload: bytes) -> bytes:
    return struct.pack("<HH", tag, len(payload)) + payload


def records(items: list[tuple[int, bytes]]) -> bytes:
    return b"".join(record(tag, payload) for tag, payload in items)


def artifact(kind: int, path: str, features: int, memory: int,
             target: str | None = None, engine: str | None = None,
             engine_abi: str | None = None) -> bytes:
    items = [(1, struct.pack("<B", kind)), (2, path.encode())]
    if target:
        items.append((3, target.encode()))
    if engine:
        items.append((4, engine.encode()))
    if engine_abi:
        items.append((5, engine_abi.encode()))
    items.extend(((6, struct.pack("<Q", features)),
                  (7, struct.pack("<B", memory))))
    return records(items)


def service(service_id: int, minimum: tuple[int, int],
            maximum: tuple[int, int], features: int = 0) -> bytes:
    return records([
        (1, struct.pack("<H", service_id)),
        (2, struct.pack("<HH", *minimum)),
        (3, struct.pack("<HH", *maximum)),
        (4, struct.pack("<Q", features)),
    ])


def component(component_id: str, kind: int,
              artifacts: list[dict[str, object]],
              services: list[dict[str, object]]) -> bytes:
    items: list[tuple[int, bytes]] = [
        (1, component_id.encode()), (2, struct.pack("<B", kind))
    ]
    for item in sorted(artifacts, key=lambda value: str(value["path"])):
        items.append((4, artifact(**item)))
    for item in sorted(services, key=lambda value: int(value["service_id"])):
        items.append((5, service(**item)))
    return records(items)


def file_entry(path: str, content: bytes) -> bytes:
    return records([
        (1, path.encode()),
        (2, struct.pack("<Q", len(content))),
        (3, hashlib.sha256(content).digest()),
    ])


def permission(name: str, required: bool, scope: bytes | None = None) -> bytes:
    items = [(1, name.encode()), (2, struct.pack("<B", int(required)))]
    if scope is not None:
        items.append((3, scope))
    return records(items)


def build_document() -> dict[str, object]:
    publisher_key_id = bytes(range(1, 33))
    payloads = {
        "artifacts/main.esp32p4.aot": b"aot-p4-main\0",
        "artifacts/main.esp32s3.aot": b"aot-s3-main\0",
        "artifacts/main.wasm": b"\0asm-portable-main",
        "artifacts/sync.wasm": b"\0asm-portable-sync",
        "assets/icon.png": b"PNG-golden-icon",
    }
    main_artifacts = [
        {"kind": 2, "path": "artifacts/main.esp32p4.aot", "target": "esp32-p4",
         "engine": "wamr", "engine_abi": "wamr-2.4.0-aot-v1-pxa0",
         "features": 9, "memory": 1},
        {"kind": 2, "path": "artifacts/main.esp32s3.aot", "target": "esp32-s3",
         "engine": "wamr", "engine_abi": "wamr-2.4.0-aot-v1-pxa0",
         "features": 1, "memory": 1},
        {"kind": 1, "path": "artifacts/main.wasm", "features": 1, "memory": 1},
    ]
    sync_artifacts = [
        {"kind": 1, "path": "artifacts/sync.wasm", "features": 0, "memory": 1}
    ]
    top = [
        (1, publisher_key_id), (2, b"com.example.reader"), (3, b"0.3.0"),
        (4, b"Reader"), (5, b"Golden multi-artifact package"),
        (6, b"assets/icon.png"), (7, struct.pack("<HH", 0, 1)),
        (8, struct.pack("<HH", 0, 3)),
        (16, component("main", 1, main_artifacts, [
            {"service_id": 2, "minimum": (0, 1), "maximum": (0, 1)},
            {"service_id": 3, "minimum": (0, 3), "maximum": (0, 3),
             "features": 1},
        ])),
        (16, component("sync", 2, sync_artifacts, [
            {"service_id": 7, "minimum": (0, 1), "maximum": (0, 1)}
        ])),
    ]
    for path, content in sorted(payloads.items()):
        top.append((17, file_entry(path, content)))
    top.append((18, permission("fs.private", True)))
    net_scope = records([(1, b"api.example.com"),
                         (2, struct.pack("<H", 443))])
    top.append((18, permission("net.client", False, net_scope)))

    body = records(top)
    manifest = b"PXAM" + struct.pack("<HHI", 0, 1, len(body)) + body
    signature_bytes = bytes(0xA0 + index % 16 for index in range(64))
    signature = (b"PXAS" + struct.pack("<HH", 1, 1) + publisher_key_id +
                 struct.pack("<HH", 64, 0) + signature_bytes)
    signed_message = b"PXA-PACKAGE-MANIFEST\0" + manifest
    return {
        "schema": "pxa-package-golden-0.1",
        "generated_by": "tools/generate_package_golden.py",
        "manifest_hex": manifest.hex(),
        "signature_hex": signature.hex(),
        "signature_message_hex": signed_message.hex(),
        "identity": {
            "publisher_key_id_hex": publisher_key_id.hex(),
            "app_id": "com.example.reader",
            "private_data_key": "com.example.reader",
        },
        "inventory": [
            {"path": path, "size": len(content),
             "sha256_hex": hashlib.sha256(content).hexdigest()}
            for path, content in sorted(payloads.items())
        ],
    }


def main() -> int:
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(build_document(), indent=2) + "\n",
                      encoding="utf-8")
    print(f"Wrote {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
