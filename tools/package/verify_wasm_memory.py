#!/usr/bin/env python3
"""Verify the declared maximum of a direct-build WebAssembly memory."""

import argparse
from pathlib import Path


WASM_HEADER = b"\0asm\x01\0\0\0"
WASM_PAGE_BYTES = 65536


class WasmMemoryError(ValueError):
    pass


def read_u32_leb(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    for shift in range(0, 35, 7):
        if offset >= len(data):
            raise WasmMemoryError("truncated unsigned LEB128")
        byte = data[offset]
        offset += 1
        if shift == 28 and byte > 0x0F:
            raise WasmMemoryError("u32 LEB128 overflow")
        value |= (byte & 0x7F) << shift
        if byte & 0x80 == 0:
            return value, offset
    raise WasmMemoryError("unterminated unsigned LEB128")


def defined_memory_limits(data: bytes) -> tuple[int, int | None]:
    if not data.startswith(WASM_HEADER):
        raise WasmMemoryError("invalid WebAssembly header")
    offset = len(WASM_HEADER)
    while offset < len(data):
        section_id = data[offset]
        offset += 1
        section_size, offset = read_u32_leb(data, offset)
        section_end = offset + section_size
        if section_end > len(data):
            raise WasmMemoryError("section extends past end of file")
        if section_id != 5:
            offset = section_end
            continue
        count, cursor = read_u32_leb(data, offset)
        if count != 1:
            raise WasmMemoryError("direct PXA components must define one memory")
        flags, cursor = read_u32_leb(data, cursor)
        if flags & ~0x07:
            raise WasmMemoryError("unsupported WebAssembly memory flags")
        minimum, cursor = read_u32_leb(data, cursor)
        maximum = None
        if flags & 0x01:
            maximum, cursor = read_u32_leb(data, cursor)
        if cursor != section_end:
            raise WasmMemoryError("unexpected data in WebAssembly memory section")
        return minimum, maximum
    raise WasmMemoryError("WebAssembly module has no defined memory")


def verify_maximum(path: Path, maximum_bytes: int) -> tuple[int, int]:
    if (maximum_bytes < WASM_PAGE_BYTES or
            maximum_bytes % WASM_PAGE_BYTES != 0):
        raise WasmMemoryError("maximum bytes must be a WebAssembly page multiple")
    minimum, maximum = defined_memory_limits(path.read_bytes())
    expected_pages = maximum_bytes // WASM_PAGE_BYTES
    if maximum is None:
        raise WasmMemoryError("WebAssembly memory has no declared maximum")
    if maximum != expected_pages:
        raise WasmMemoryError(
            f"WebAssembly memory maximum is {maximum} pages, expected {expected_pages}")
    if minimum > maximum:
        raise WasmMemoryError("WebAssembly memory minimum exceeds maximum")
    return minimum, maximum


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("wasm", type=Path)
    parser.add_argument("--maximum-bytes", type=int, required=True)
    args = parser.parse_args()
    try:
        minimum, maximum = verify_maximum(args.wasm, args.maximum_bytes)
    except (OSError, WasmMemoryError) as error:
        parser.error(str(error))
    print(f"Verified pinned linear memory: min={minimum} max={maximum} pages")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
