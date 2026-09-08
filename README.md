# PXA System

PXA System is a portable application system for embedded products. This tree
is intentionally self-contained so it can become its own Git repository or be
embedded directly in a product repository.

The system gives native and PXA applications the same identity, lifecycle,
Intent, RPC, event, role and service contracts. Native applications link the
native C binding and do not use the PXA Guest SDK. LVGL is an optional renderer,
not a dependency of the system core.

## Repository layout

```text
pxa-system/
  CMakeLists.txt              standalone CMake and ESP component entry
  cmake/                      shared source manifests and package helpers
  libpxa/                     platform-neutral PXA Host library
  system/core/                identity, lifecycle, Intent, RPC, roles, themes
  system/runtimes/native/     native-static application runtime
  system/runtimes/pxa/        libpxa-backed application runtime binding
  system/standard/            standard composition root
  ui/reference/               reference shell and built-in app boundary
  ui/renderers/headless/      conformance and test renderer
  ui/renderers/lvgl/          optional LVGL renderer
  services/                   standard and vendor service provider boundary
  sdk/guest-c/                PXA Guest C SDK
  sdk/cmake/                  WASI toolchain and PXA App CMake helpers
  tools/package/              manifest, signing and package tools
  scripts/                    host build, test and SDK release scripts
  platforms/esp-idf/          ESP-IDF integration helpers
  simulator/                  backend-neutral smoke simulator
  examples/                   installed SDK and integration examples
  spec/                       wire/service specifications and golden vectors
  docs/                       architecture, protocol and porting documents
```

Product-specific hardware services and UI implementations are not part of the
portable core. A product registers them through service, renderer and system
role provider interfaces. The current firmware's `app_pages` and ESP PXA host
bridges are examples of such external product adapters.

## Standalone build

```sh
cmake -S pxa-system -B /tmp/pxa-system-build
cmake --build /tmp/pxa-system-build
ctest --test-dir /tmp/pxa-system-build --output-on-failure
```

The default build includes libpxa, native and PXA runtime bindings, the
standard composition, headless renderer, tests and the portable simulator.
Enable the LVGL renderer with `-DPXSYS_BUILD_RENDERER_LVGL=ON` after providing
an `lvgl` CMake target.

`scripts/test-host.sh` provides the same default build and test flow.
`scripts/package-sdk.sh` creates an installable SDK archive containing libpxa,
PXA System headers, libraries and CMake package metadata.

## ESP-IDF component

Place this directory at `components/pxa_system`, use it through a Git component,
or add its parent to `EXTRA_COMPONENT_DIRS`. Its root `CMakeLists.txt` detects
ESP-IDF and registers the portable foundation as one component named after the
directory. The component deliberately does not require LVGL.

For the optional LVGL renderer, add
`platforms/esp-idf/components` to `EXTRA_COMPONENT_DIRS` and require
`pxa_system_renderer_lvgl`. A product may instead provide another renderer.

See [Product integration](docs/product-integration.md) for system UI, service
and driver overrides.
See [Display profiles and standard UI](docs/display-and-system-ui.md) for
responsive layout, shape simulation, role replacement, and UI backend ports.

## Compatibility policy

All contracts are currently draft `0.x`. Public structures carry `struct_size`
and wire formats use explicit versions, but compatibility is not promised
until the conformance suite and first external product port are complete.
