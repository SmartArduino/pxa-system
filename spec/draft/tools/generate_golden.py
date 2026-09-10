#!/usr/bin/env python3
"""Generate the normative PXA Core and service wire vectors."""

from __future__ import annotations

import json
import pathlib
import struct
import sys


ROOT = pathlib.Path(__file__).resolve().parent.parent
OUTPUT = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "golden/core-vectors.json"


def u8(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}B", *values)


def u16(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}H", *values)


def u32(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}I", *values)


def u64(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}Q", *values)


def i16(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}h", *values)


def i32(*values: int) -> bytes:
    return struct.pack(f"<{len(values)}i", *values)


def record(tag: int, payload: bytes) -> bytes:
    return struct.pack("<HH", tag, len(payload)) + payload


def records(items: list[tuple[int, bytes]]) -> bytes:
    return b"".join(record(tag, payload) for tag, payload in items)


def message(service: int, opcode: int, request_id: int, payload: bytes) -> bytes:
    return struct.pack("<HHII", service, opcode, request_id, len(payload)) + payload


def message_vector(name: str, service: int, opcode: int, request_id: int,
                   payload: bytes) -> dict[str, object]:
    return {
        "name": name,
        "kind": "message",
        "message": {
            "service": service,
            "opcode": opcode,
            "request_id": request_id,
            "payload_hex": payload.hex(),
        },
        "wire_hex": message(service, opcode, request_id, payload).hex(),
    }


def records_vector(name: str, items: list[tuple[int, bytes]]) -> dict[str, object]:
    return {
        "name": name,
        "kind": "records",
        "records": [
            {"tag": tag, "payload_hex": payload.hex()} for tag, payload in items
        ],
        "wire_hex": records(items).hex(),
    }


def build_vectors() -> list[dict[str, object]]:
    vectors: list[dict[str, object]] = []
    add = vectors.append

    add(message_vector("core.close-handle", 1, 2, 0, u32(0x01020304)))
    add(message_vector("core.handle-ready", 1, 0x8001, 0,
                       u32(0x11223344, 0x00000003)))
    lease = [(1, u16(2)), (2, u32(30_000)),
             (0x8007, b"ignored-by-draft-host")]
    add(records_vector("core.acquire-lease.records", lease))
    add(message_vector("core.acquire-lease", 1, 3, 42, records(lease)))
    add(message_vector("core.acquire-lease.result", 1, 3, 42,
                       i32(0) + record(4, u32(0xA0010001))))

    config = [
        (1, u64(0x0102030405060708)),
        (2, b"com.example.player"),
        (3, b"main"),
        (4, u16(1, 0, 1, 0)),
        (4, u16(3, 0, 3, 0)),
        (6, u16(3, 1) + u64(64)),
        (7, u32(0x00010001)),
    ]
    add(records_vector("core.start-config.records", config))

    window_config = [(1, u8(1)), (2, u8(2)), (4, u8(2)),
                     (6, u32(0x102030FF))]
    add(records_vector("window.configure.records", window_config))
    add(message_vector("window.configure", 2, 1, 0, records(window_config)))
    window_snapshot = [
        (1, u64(1)), (2, u32(320, 240)), (3, u32(640, 480)),
        (4, u32(2, 1)), (5, u32(0, 24, 0, 12)),
        (6, u32(0, 20, 0, 10)), (7, u8(2)), (8, u8(1)),
    ]
    snapshot = records(window_snapshot)
    add(records_vector("window.snapshot.records", window_snapshot))
    add(message_vector("window.metrics-changed", 2, 0x8001, 0, snapshot))
    add(message_vector("window.get-snapshot.result", 2, 2, 51,
                       i32(0) + snapshot))
    add(message_vector("window.back-requested", 2, 0x8002, 0, b""))

    add(message_vector("clock.set-period", 4, 1, 0, u16(33)))
    add(message_vector("clock.tick", 4, 0x8001, 0, u64(1_234_567)))

    fs_open = [(1, b"notes/today.txt"), (3, u32(1 | 2 | 4))]
    add(records_vector("fs.open.records", fs_open))
    add(message_vector("fs.open", 5, 1, 71, records(fs_open)))
    add(message_vector("fs.open.result", 5, 1, 71,
                       i32(0) + u32(0x00010001)))
    add(message_vector("fs.read-directory", 5, 7, 72, u32(0x00010002)))

    storage = [(1, b"counter.total"), (2, u32(7))]
    add(records_vector("storage.set.records", storage))
    add(message_vector("storage.set", 6, 2, 73, records(storage)))
    add(message_vector("storage.get", 6, 1, 74,
                       record(1, b"counter.total")))
    add(message_vector("storage.get.result", 6, 1, 74,
                       i32(0) + record(2, u32(7))))

    ipc = [(1, b"example.echo"), (2, b"hi")]
    add(records_vector("ipc.call.records", ipc))
    add(message_vector("ipc.call", 7, 1, 75, records(ipc)))
    add(message_vector("ipc.call.result", 7, 1, 75, i32(0) + u32(91)))
    add(message_vector("ipc.request", 7, 0x8001, 91, records(ipc)))
    add(message_vector("ipc.result", 7, 0x8002, 91,
                       i32(0) + record(3, b"ok")))

    sensor_subscribe = [(1, u16(1)), (2, u32(500)),
                        (3, u32(0x00010003))]
    add(records_vector("sensor.subscribe.records", sensor_subscribe))
    add(message_vector("sensor.subscribe", 8, 2, 76,
                       records(sensor_subscribe)))
    descriptor = records([
        (1, u16(1)), (2, b"ambient.temperature"), (3, u16(1)),
        (4, u8(1)), (5, u32(100)), (6, u32(10_000)),
    ])
    add(message_vector("sensor.list.result", 8, 1, 76,
                       i32(0) + record(1, descriptor)))
    sample = records([
        (4, u32(0x00010004)), (2, u64(1_234_567)),
        (3, u16(1)), (4, i32(21_500)),
    ])
    add(message_vector("sensor.sample", 8, 0x8001, 0, sample))

    net_fetch = [
        (1, b"https://example.test/hello"), (2, u16(1)),
        (3, u32(0x00010003)), (4, u32(64)),
    ]
    add(records_vector("net.fetch.records", net_fetch))
    add(message_vector("net.fetch", 9, 1, 77, records(net_fetch)))
    add(message_vector("net.fetch.result", 9, 1, 77,
                       i32(0) + record(5, u16(200)) +
                       record(6, b"text/plain") +
                       record(7, u32(0x00010004))))
    header = record(1, b"content-type") + record(2, b"application/json")
    net_http = [
        (1, b"https://example.test?mode=post"), (2, u16(3)),
        (3, u32(0x00010003)), (4, u32(1024)), (8, u32(2500)),
        (9, header), (10, b"{}"), (11, b"etag"),
    ]
    add(records_vector("net.http-request.records", net_http))
    add(message_vector("net.http-request", 9, 2, 78, records(net_http)))
    response_header = record(1, b"etag") + record(2, b'"test"')
    add(message_vector("net.http-request.result", 9, 2, 78,
                       i32(0) + record(5, u16(200)) +
                       record(6, b"application/json") +
                       record(7, u32(0x00010004)) +
                       record(9, response_header) + record(12, u64(11)) +
                       record(13, u32(3))))

    add(message_vector("audio.open-session", 10, 1, 81,
                       record(1, u32(0x00010002)) + record(2, u16(1))))
    audio_graph = (record(3, u32(0x00010003)) + record(2, i16(-256)) +
                   record(3, struct.pack("<HhH", 1500, 256, 256)) +
                   record(4, u16(1)))
    add(message_vector("audio.commit-graph", 10, 2, 82, audio_graph))

    work = [
        (1, b"sync.job"), (2, u32(1000)), (3, u32(5000)), (5, b"abc"),
        (6, u32(2000)), (7, u8(3)),
    ]
    add(records_vector("work.enqueue.records", work))
    add(message_vector("work.enqueue", 13, 1, 79, records(work)))
    add(message_vector("work.enqueue.result", 13, 1, 79,
                       i32(0) + record(4, u32(0x00010001)) +
                       record(8, u32(5000))))
    work_cancel = record(4, u32(0x00010001))
    add(message_vector("work.cancel", 13, 2, 80, work_cancel))
    add(message_vector("work.complete", 13, 3, 81,
                       work_cancel + record(9, u8(1))))

    permission = [(1, b"net.client"), (2, b"api")]
    add(records_vector("permission.acquire.records", permission))
    add(message_vector("permission.acquire", 11, 2, 81,
                       records(permission)))
    add(message_vector("permission.acquire.result", 11, 2, 81,
                       i32(0) + u32(0x00010003)))
    add(message_vector("permission.revoked", 11, 0x8001, 0,
                       records(permission)))

    device = [(1, u16(1)), (2, u32(0xA0010002))]
    add(records_vector("device.get-mac.records", device))
    add(message_vector("device.get-mac", 15, 1, 82, records(device)))
    add(message_vector("device.get-mac.result", 15, 1, 82,
                       i32(0) + record(1, u16(1)) +
                       record(2, bytes((0x24, 0x6F, 0x28, 0x70, 0x14, 0x01))) +
                       record(3, u32(1))))
    return vectors


def main() -> int:
    document = {
        "schema": "pxa-core-golden-0.1",
        "generated_by": "tools/generate_golden.py",
        "vectors": build_vectors(),
    }
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
