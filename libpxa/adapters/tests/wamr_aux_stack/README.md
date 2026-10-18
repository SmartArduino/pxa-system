# WAMR auxiliary stack policy regression (Linux/x86_64)

This standalone suite enables the same pthread/thread-manager combination as
ESP-IDF. It uses PXA's production `PxaWamrRuntime.cmake` configuration.

```sh
cmake -S libpxa/adapters/tests/wamr_aux_stack -B build/aux-stack -G Ninja \
  -DPXA_WASI_SDK_DIR=/path/to/wasi-sdk \
  -DPXA_WAMRC=/path/to/wamrc
cmake --build build/aux-stack
ctest --test-dir build/aux-stack --output-on-failure
```

Run these commands from the PXA System repository. Omit `PXA_WAMRC` to run only
the interpreter cases. Release and Debug test builds keep their assertions.
Guest AOT compilation uses O3, size level 0 and explicit bounds checks.

The same 16 KiB module runs a real 4 KiB volatile-stack workload, then checks
its checksum. A second shared-memory fixture creates and joins three child
threads sequentially. Its small fixture allocator verifies zero stack
allocations before spawning and exactly three allocations/frees afterwards;
it is not an application allocator or a memory benchmark.

For the historical control, configure a separate directory with
`-DPXA_TEST_PARTITIONED_STACK=ON`. Both interpreters and AOT must observe only
3,264 bytes available to the main thread and reject the 4 KiB workload with
an auxiliary stack overflow. This expected rejection is a passing control
assertion, not a successful Guest workload. Its three small child routines
still run using their preassigned slices.

The corrected policy keeps all 16 KiB available to the main thread; it also
retains child creation and frees child stacks after join. PXA currently
rejects pthread/WASI-thread imports, but the underlying runtime regression
checks that the policy does not implement the fix by disabling threads.
