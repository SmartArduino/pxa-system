# pxa_system_core

Portable C99 foundation for PXA System. It provides canonical application
identity, registry and lifecycle, runtime providers, Intent resolution and wire
encoding, task history, replaceable system roles, semantic light/dark themes,
a renderer SPI and provider host, an extensible versioned service registry,
and a bounded topic event broker. The renderer host owns backend selection,
surface accounting and theme propagation without exposing backend objects.

It deliberately has no dependency on ESP-IDF, FreeRTOS, LVGL, WAMR or libpxa.
The host supplies allocation functions and selects registry limits.

For standalone host tests:

```sh
cmake -S . -B /tmp/pxsys-core-build \
  -DPXSYS_BUILD_LIBPXA=OFF -DPXSYS_BUILD_PXA_RUNTIME=OFF \
  -DPXSYS_BUILD_SIMULATOR=OFF
cmake --build /tmp/pxsys-core-build
ctest --test-dir /tmp/pxsys-core-build --output-on-failure
```

See [`docs/`](../../docs/README.md) for the architecture and migration plan.
