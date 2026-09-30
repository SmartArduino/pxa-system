# PXA CMake/WASI Test Apps

- `wasi-libc-lab`: runnable UI test for string, parsing, memory allocation,
  memory movement and formatting routines split across directory modules.
- `wasi-system-lab`: positive UI test for signed monotonic clock, wall clock
  and device-random WASI capabilities.
- `cmake-components-lab`: runnable UI and service Components linked as two
  independent artifacts and connected through a signed IPC endpoint.
- `wasi-undeclared-random`: negative App. Packaging rejects `random_get`
  because the source manifest omits the required `random` feature.

These Apps are intentionally outside `apps/pxa`; they are not included in
factory assets. See `../../README.md` for the package layout.
Run all packaging checks with:

```sh
WASI_SDK_DIR=/opt/wasi-sdk-29.0 WAMRC=/path/to/wamrc \
  tools/package/test_cmake_wasi_apps.sh
```
