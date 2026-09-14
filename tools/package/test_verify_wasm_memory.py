#!/usr/bin/env python3

import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("verify_wasm_memory.py")
SPEC = importlib.util.spec_from_file_location("verify_wasm_memory", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def u32_leb(value: int) -> bytes:
    encoded = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            byte |= 0x80
        encoded.append(byte)
        if not value:
            return bytes(encoded)


def module_with_memory(minimum: int, maximum: int | None) -> bytes:
    limits = u32_leb(1 if maximum is not None else 0) + u32_leb(minimum)
    if maximum is not None:
        limits += u32_leb(maximum)
    payload = u32_leb(1) + limits
    return MODULE.WASM_HEADER + bytes([5]) + u32_leb(len(payload)) + payload


class VerifyWasmMemoryTest(unittest.TestCase):
    def verify(self, data: bytes, maximum_bytes: int):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "app.wasm"
            path.write_bytes(data)
            return MODULE.verify_maximum(path, maximum_bytes)

    def test_accepts_matching_maximum(self):
        self.assertEqual(self.verify(module_with_memory(19, 32), 2 * 1024 * 1024),
                         (19, 32))

    def test_rejects_missing_maximum(self):
        with self.assertRaisesRegex(MODULE.WasmMemoryError, "no declared maximum"):
            self.verify(module_with_memory(19, None), 2 * 1024 * 1024)

    def test_rejects_mismatched_maximum(self):
        with self.assertRaisesRegex(MODULE.WasmMemoryError, "expected 32"):
            self.verify(module_with_memory(19, 31), 2 * 1024 * 1024)

    def test_rejects_truncated_section(self):
        with self.assertRaisesRegex(MODULE.WasmMemoryError, "past end"):
            self.verify(MODULE.WASM_HEADER + b"\x05\x04\x01", 2 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()
