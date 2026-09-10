# ESP-IDF Integration

[简体中文](README.zh-CN.md)

The repository root is the backend-neutral ESP component. Optional adapters
that require extra ESP components live below `components/` so a product opts
into those dependencies explicitly through `EXTRA_COMPONENT_DIRS`. Add the
bundled `../../wamr` directory as an ESP-IDF component when enabling PXA
execution; its component name is `wamr`.

Board services, package storage and a concrete system UI should be separate
product components. WAMR configuration and ESP32-S3 compatibility fixes belong
to this platform layer. Products may replace reference providers by normal
registration and policy, without overriding or weak-linking core symbols.

The WAMR compatibility layer applies the explicit patch list in
`wamr/patches/series` to a build-directory overlay. It maps WAMR's POSIX
abstractions to ESP-IDF's `poll`, `ioctl`, `struct pollfd` and `struct
timespec` APIs. The full project-owned executable-memory implementation lives
in `wamr/overrides`. The WAMR submodule stays unmodified throughout configure
and build; `tools/wamr/prepare_overlay.py` verifies both its pinned commit and
clean status.

The layer exports ESP libgcc's 32-bit division and remainder helpers through
WAMR's Xtensa relocation table. This supports AOT artifacts that emit calls to
these helpers instead of native division instructions.

ESP-IDF uses Xtensa's windowed ABI, so ESP32-S3 AOT packages must use the same
ABI when calling through WAMR's native bridge. The AOT toolchain is pinned to
Espressif LLVM 18.1.2 because LLVM 22 generated invalid return targets after
deep AOT-to-native callbacks in the current dynamically mapped instruction
window. The package tool selects `--cpu=esp32s3` explicitly to preserve its
windowed ABI while enabling the ESP32-S3 instruction model.

The board disables FreeRTOS trace support, so WAMR cannot discover the native
thread stack boundary. Packages disable generated native-stack checks and rely
on the host's 12 KiB runtime stack. Memory bounds checks remain enabled, and
`--opt-level=3` keeps the performance optimization pipeline enabled. Regenerate
and deploy the packages after changing these code-generation options.
