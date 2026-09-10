#!/usr/bin/env python3
import json
import pathlib
import struct
import sys


def main() -> int:
    path = (pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else
            pathlib.Path(__file__).resolve().parent /
            "data/ui-vectors.json")
    document = json.loads(path.read_text(encoding="utf-8"))
    assert document["schema"] == "pxa-ui-golden-0.3"
    vectors = {item["name"]: item for item in document["vectors"]}
    expected = {
        "begin-replace-primary-surface": (
            1,
            struct.pack("<IIIIBBH", 1, 7, 1, 0, 3, 1, 0),
        ),
        "fragment-create-root": (
            2,
            struct.pack("<I", 7)
            + struct.pack("<BBH", 1, 0, 16)
            + struct.pack("<IIIBBH", 1, 0, 0, 1, 0, 0),
        ),
        "commit-transaction-seven": (3, struct.pack("<I", 7)),
        "normal-resource-pressure": (0x8003, b"\x00\x00\x00\x00"),
        "controller-zero-state": (
            0x8001,
            struct.pack("<IIIHHQBBHI", 1, 1, 7, 10, 2, 1_000_000,
                        0, 1, 0, 0x18),
        ),
        "write-rgb565-bitmap": (
            8,
            struct.pack("<III", 1, 5, 9)
            + struct.pack("<BBHiiIII", 10, 0, 28, -1, 3, 2, 2, 4)
            + bytes.fromhex("00f8e0071f00ffff"),
        ),
    }
    assert set(vectors) == set(expected)
    for name, (opcode, payload) in expected.items():
        assert vectors[name]["opcode"] == opcode
        assert bytes.fromhex(vectors[name]["payload_hex"]) == payload

    invalid = {item["name"]: bytes.fromhex(item["payload_hex"])
               for item in document["invalid"]}
    assert invalid["reserved-begin-byte"][18] != 0
    assert struct.unpack("<I", invalid["zero-transaction"])[0] == 0
    truncated = invalid["truncated-command"]
    assert len(truncated) < 8 + struct.unpack_from("<H", truncated, 6)[0]
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
