# PXA CMake/WASI Test Apps

- `wasi-libc-lab`: runnable UI test for string, parsing, memory allocation,
  memory movement and formatting routines split across directory modules.
- `wasi-system-lab`: positive UI test for signed monotonic clock, wall clock
  and device-random WASI capabilities.
- `cmake-components-lab`: runnable UI and service Components linked as two
  independent artifacts and connected through a signed IPC endpoint.
- `wasi-undeclared-random`: negative App. Packaging rejects `random_get`
  because the source manifest omits the required `random` feature.

All C/WASI examples use the pinned WASI SDK 34. Its libc imports
`clock_time_get` here, so the source manifests declare both clock features.
The same regression script also compares serial and parallel direct C builds
of `../direct-parallel` byte for byte.

These Apps are intentionally outside `apps/pxa`; they are not included in
factory assets. See `../../README.md` for the package layout.
Run all packaging checks with:

```sh
WASI_SDK_DIR=/opt/wasi-sdk-34.0 WAMRC=/path/to/wamrc \
  tools/package/test_cmake_wasi_apps.sh
```

To check AOT-only packaging (including explicit `artifact: "both"` and a
Wasm-only service in the same signed package):

```sh
tools/package/test_aot_only.sh
```

This check builds a temporary copy of `cmake-components-lab`, verifies that
the AOT is unchanged, strips its Wasm fallback and retains the service's Wasm.
