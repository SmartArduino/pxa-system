#!/usr/bin/env python3

from verify_wasm_core_imports import (
    import_modules, verify_core_imports, verify_wasi_imports,
)
from verify_wasm_memory import WASM_HEADER, WasmMemoryError


def leb(value: int) -> bytes:
    output = bytearray()
    while True:
        byte = value & 0x7f
        value >>= 7
        output.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(output)


def name(value: str) -> bytes:
    encoded = value.encode()
    return leb(len(encoded)) + encoded


def wasm(*imports: tuple[str, str, int, bytes]) -> bytes:
    entries = b"".join(name(module) + name(field) + bytes([kind]) + desc
                       for module, field, kind, desc in imports)
    section = leb(len(imports)) + entries
    return WASM_HEADER + bytes([2]) + leb(len(section)) + section


def rejected(data: bytes, major: int) -> None:
    try:
        verify_core_imports(data, major)
    except WasmMemoryError:
        return
    raise AssertionError("expected Core import rejection")


v0 = wasm(("pxa.core.v0", "pxa_control", 0, leb(0)))
v1 = wasm(("pxa.core.v1", "pxa_submit", 0, leb(0)),
          ("wasi_snapshot_preview1", "random_get", 0, leb(1)))
assert import_modules(v0) == ["pxa.core.v0"]
assert import_modules(v1) == ["pxa.core.v1", "wasi_snapshot_preview1"]
rejected(v0, 0)
verify_core_imports(v1, 1)
rejected(v0, 1)
rejected(v1, 0)
rejected(wasm(("pxa.core.v2", "pxa_submit", 0, leb(0))), 1)
rejected(wasm(("pxa.core.v1extra", "pxa_submit", 0, leb(0))), 1)
verify_core_imports(wasm(("env", "memory", 2, leb(1) + leb(2) + leb(3))), 1)
rejected(WASM_HEADER + bytes([2, 4, 1, 1, 65]), 1)
verify_wasi_imports(v1, {"random"})
clock = wasm(("wasi_snapshot_preview1", "clock_time_get", 0, leb(0)))
verify_wasi_imports(clock, {"monotonic-clock", "wall-clock"})
for declared in (None, set(), {"monotonic-clock"}, {"wall-clock"}):
    try:
        verify_wasi_imports(clock, declared)
    except WasmMemoryError:
        pass
    else:
        raise AssertionError("expected WASI clock capability rejection")
try:
    verify_wasi_imports(v1, set())
except WasmMemoryError:
    pass
else:
    raise AssertionError("expected WASI random capability rejection")
print("PXA Wasm Core import verification OK")
