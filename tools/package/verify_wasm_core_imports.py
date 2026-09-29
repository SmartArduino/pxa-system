#!/usr/bin/env python3
"""Check Wasm imports against the Core and WASI capabilities being signed."""

import argparse
import json
from pathlib import Path

from verify_wasm_memory import WASM_HEADER, WasmMemoryError, read_u32_leb


def read_name(data: bytes, offset: int, end: int) -> tuple[str, int]:
    size, offset = read_u32_leb(data, offset)
    if size > end - offset:
        raise WasmMemoryError("truncated WebAssembly import name")
    try:
        value = data[offset:offset + size].decode("utf-8")
    except UnicodeDecodeError as error:
        raise WasmMemoryError("invalid UTF-8 WebAssembly import name") from error
    return value, offset + size


def skip_limits(data: bytes, offset: int) -> int:
    flags, offset = read_u32_leb(data, offset)
    if flags & ~0x03:
        raise WasmMemoryError("unsupported WebAssembly import limits")
    _, offset = read_u32_leb(data, offset)
    if flags & 1:
        _, offset = read_u32_leb(data, offset)
    return offset


def import_entries(data: bytes) -> list[tuple[str, str]]:
    if not data.startswith(WASM_HEADER):
        raise WasmMemoryError("invalid WebAssembly header")
    offset = len(WASM_HEADER)
    imports = []
    while offset < len(data):
        section_id = data[offset]
        offset += 1
        section_size, offset = read_u32_leb(data, offset)
        end = offset + section_size
        if end > len(data):
            raise WasmMemoryError("WebAssembly section extends past end of file")
        if section_id == 2:
            count, cursor = read_u32_leb(data, offset)
            for _ in range(count):
                module, cursor = read_name(data, cursor, end)
                field, cursor = read_name(data, cursor, end)
                if cursor >= end:
                    raise WasmMemoryError("truncated WebAssembly import kind")
                kind = data[cursor]
                cursor += 1
                if kind == 0 or kind == 4:  # function or exception tag
                    _, cursor = read_u32_leb(data, cursor)
                    if kind == 4:
                        _, cursor = read_u32_leb(data, cursor)
                elif kind == 1:  # table
                    if cursor >= end:
                        raise WasmMemoryError("truncated WebAssembly table type")
                    cursor = skip_limits(data, cursor + 1)
                elif kind == 2:  # memory
                    cursor = skip_limits(data, cursor)
                elif kind == 3:  # global
                    cursor += 2
                else:
                    raise WasmMemoryError("unknown WebAssembly import kind")
                if cursor > end:
                    raise WasmMemoryError("WebAssembly import extends past section")
                imports.append((module, field))
            if cursor != end:
                raise WasmMemoryError("trailing WebAssembly import data")
        offset = end
    return imports


def import_modules(data: bytes) -> list[str]:
    return [module for module, _ in import_entries(data)]


def verify_core_imports(data: bytes, core_major: int) -> None:
    if core_major != 1:
        raise WasmMemoryError("unsupported PXA Core major")
    expected = f"pxa.core.v{core_major}"
    for module in import_modules(data):
        if module.startswith("pxa.core.") and module != expected:
            raise WasmMemoryError(
                f"Core {core_major} package imports {module}; expected {expected}")


def wasi_feature_for_import(name: str) -> set[str]:
    # Keep these classes aligned with wasi_import_feature() in WAMR. Preview 1
    # selects the clock at call time, so one clock import needs both grants.
    if name in ("fd_close", "fd_seek", "fd_write"):
        return set()
    if name in ("fd_read", "fd_fdstat_get"):
        return {"stdio"}
    if name in ("clock_res_get", "clock_time_get"):
        return {"monotonic-clock", "wall-clock"}
    if name == "random_get":
        return {"random"}
    if name in ("args_get", "args_sizes_get"):
        return {"arguments"}
    if name in ("environ_get", "environ_sizes_get"):
        return {"environment"}
    if name.startswith("path_") or name in (
        "fd_readdir", "fd_prestat_get", "fd_prestat_dir_name", "fd_tell",
        "fd_sync", "fd_datasync", "fd_advise", "fd_allocate",
        "fd_fdstat_set_flags", "fd_filestat_get", "fd_filestat_set_size",
        "fd_filestat_set_times",
    ):
        return {"private-fs"}
    raise WasmMemoryError(f"unsupported WASI import {name}")


def verify_wasi_imports(data: bytes, features: set[str] | None) -> None:
    for module, field in import_entries(data):
        if module != "wasi_snapshot_preview1":
            continue
        if features is None:
            raise WasmMemoryError(f"WASI import {field} without declaration")
        missing = wasi_feature_for_import(field) - features
        if missing:
            raise WasmMemoryError(
                f"WASI import {field} requires features: {', '.join(sorted(missing))}")


def component_wasi_features(path: Path, component_id: str) -> set[str] | None:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    for component in manifest.get("components", []):
        if component.get("id") == component_id:
            wasi = component.get("wasi")
            return None if wasi is None else set(wasi.get("features", []))
    raise WasmMemoryError(f"component {component_id} missing from package source")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("wasm", type=Path)
    parser.add_argument("--core-major", type=int, required=True)
    parser.add_argument("--manifest-source", type=Path)
    parser.add_argument("--component-id")
    args = parser.parse_args()
    try:
        data = args.wasm.read_bytes()
        verify_core_imports(data, args.core_major)
        if args.manifest_source is not None:
            if not args.component_id:
                raise WasmMemoryError("--component-id is required with --manifest-source")
            verify_wasi_imports(
                data, component_wasi_features(args.manifest_source, args.component_id))
    except (OSError, ValueError, WasmMemoryError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
