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
  wamr/                        upstream WAMR runtime submodule
  libpxa/                     platform-neutral PXA Host library
  system/core/                identity, lifecycle, Intent, RPC, roles, themes
  system/runtimes/native/     native-static application runtime
  system/runtimes/pxa/        libpxa-backed application runtime binding
  system/standard/            standard composition root
  ui/reference/               reference shell and standard Native App boundary
  ui/renderers/               backend-neutral renderer implementations
  ui/backends/lvgl/           optional LVGL-only helpers, including page_manager
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

Clone this tree with its runtime and UI dependencies:

```sh
git clone --recurse-submodules <pxa-system-repository>
```

The checked-in WAMR revision follows its upstream `main` branch and currently
uses WAMR 2.4.3. Updating that submodule is an ABI-changing operation for AOT
packages: update the declared engine ABI and regenerate packages with the
bundled `tools/package/build_wamrc.sh` flow before deployment.

`build_wamrc.sh` uses `wamr/` as its only WAMR source checkout. It stores the
host LLVM checkout, the out-of-tree `wamrc` build and compiler cache under
`pxa-system/.pxa/`; those generated files can be relocated with
`PXA_WAMR_CACHE_DIR`. Its Xtensa backend is pinned to Espressif LLVM 18.1.2
commit `344b928e67bfc1d07cb150609c55548e86a9bf19` from
`xtensa_release_18.1.2`. The `wamrc` cache key includes both the WAMR and LLVM
commits so switching toolchains cannot reuse an incompatible compiler binary.
The host compiler retains the AArch64, ARM, Mips, RISC-V, X86 and Xtensa
backends, so the same toolchain can generate AOT artifacts for ESP32-P4 and
other supported devices as targets are added to the package flow. ESP32-P4
uses `--target=riscv32 --target-abi=ilp32f --cpu=esp32p4`; the explicit ABI is
required because WAMR otherwise defaults RISC-V 32-bit output to `ilp32d`.

CMake/WASI Apps additionally need wasi-sdk 29.0. Packaging first honors
`PXA_WASI_SDK_DIR`, `WASI_SDK_DIR` and `WASI_SDK_PATH`, then `/opt/wasi-sdk`,
and finally the cached SDK under `pxa-system/.pxa`. If none exists, it downloads
the pinned, checksum-verified SDK to that cache. Set
`PXA_WASI_SDK_AUTO_DOWNLOAD=0` to prohibit downloads in an offline build.
Set `PXA_WASI_SDK_CACHE_DIR` to relocate the cache.

## Standalone build

```sh
cmake -S pxa-system -B /tmp/pxa-system-build
cmake --build /tmp/pxa-system-build
ctest --test-dir /tmp/pxa-system-build --output-on-failure
```

The default build includes libpxa with the bundled WAMR adapter, native and
PXA runtime bindings, the standard composition, headless renderer, tests and
the portable simulator. It needs OpenSSL and `liblz4` development packages for
the host installer adapter.
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

For PXA execution, add `wamr/` to `EXTRA_COMPONENT_DIRS` and require `wamr`.
The ESP32-S3 WAMR compatibility setup lives under `platforms/esp-idf`; product
components call it rather than referring to a Component Manager installation.
`ui/backends/lvgl/page_manager` is the canonical LVGL page stack; the parent
firmware keeps `components/page_manager` only as a compatibility component.

See [Product integration](docs/product-integration.md) for system UI, service
and driver overrides.
See [Display profiles and standard UI](docs/display-and-system-ui.md) for
responsive layout, shape simulation, role replacement, and UI backend ports.

## Compatibility policy

All contracts are currently draft `0.x`. Public structures carry `struct_size`
and wire formats use explicit versions, but compatibility is not promised
until the conformance suite and first external product port are complete.
