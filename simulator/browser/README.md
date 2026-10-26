# PXA Browser Host (experimental)

Runs standard PXA Core 2 WebAssembly applications in the browser VM. A separate
Emscripten module builds the existing libpxa Core, UI/Window/Clock services and
LVGL renderer. The Guest C and C++ SDKs and wire ABI are unchanged. This is an
application host, not the complete device firmware, launcher or installer.

## Build and test

Prerequisites: Python 3, CMake, Emscripten 4.0.6 (`emcmake` on PATH), a WASI SDK
(the PXA 0.5.0 DevKit includes one), and Node 22+ for integration tests. LVGL is
pinned to 9.6.0. Supply `--lvgl` to reuse an existing checkout offline.

```sh
python3 simulator/browser/build.py \
  --build-dir local/artifacts/browser-build --out local/artifacts/browser-bundle \
  --wasi-sdk /path/to/pxa-devkit/toolchains/wasi-sdk
PXA_BROWSER_BUNDLE="$PWD/local/artifacts/browser-bundle" \
  node --test simulator/browser/runtime.test.mjs
```

`--host-build` reuses a separately compiled Host (useful with Emscripten in a
container). It must contain `pxa-browser.mjs` and `pxa-browser.wasm`; also supply
`--lvgl` for the matching license. Build outputs stay outside the source tree.
The bundle includes a C Canvas game, the unchanged C counter template and the
unchanged C++ Hello template. The last is an SDK interoperability test fixture.

Serve over HTTPS or localhost. The website accepts `PXA_BROWSER_BUNDLE` during
its build and provides the controller UI. Never open modules with `file://`.
Allow `script-src 'self' 'wasm-unsafe-eval'; worker-src 'self'` in CSP. No
`unsafe-eval`, SharedArrayBuffer, cross-origin isolation or browser extension is
required. Runtime downloads are lazy, after the user starts a demo.

## Runtime contract

- Each app runs in a fresh Worker with its own Guest and Host linear memory.
  Only the two Core imports and an explicit WASI function allowlist are exposed.
  Guest code cannot import DOM, JavaScript evaluation, fetch, or filesystem APIs.
- Copy validated import payloads only, reusing scratch space. Core control
  messages are bounded by the existing wire limit; I/O is capped at 4 MiB.
  Drain at most 128 events per turn. Never reenter a Guest during an import.
- Require exported `memory`, `malloc`, `free` and PXA callback entry points.
  Default SDK components provide these; `NO_ALLOCATOR` builds are not supported.
  Chip-specific AOT artifacts cannot run here; retain the original Wasm.
- Supply the real display environment as the standard startup configuration.
  The initial font/locale bundle is Latin/en-US; CJK fonts and IME remain future
  Host work. Physical resolution is 320×240 in the demo; CSS scales with aspect
  ratio preserved. The Host accepts bounded sizes 64–1024 for future profiles.
- LVGL paints XRGB8888; the Worker converts BGRA bytes to Canvas RGBA only on
  changed frames. OffscreenCanvas keeps presentation in the Worker, with a
  transferable-buffer fallback. The Worker owns the 60 Hz
  clock with anchored deadlines, without a per-frame main-thread round trip.
  Only statistics (once per second) and user input cross to the page. The
  fallback permits one frame in flight. The page pauses on hidden tabs,
  releases held inputs, and terminates a stalled Worker.
- Standard digital gamepads and keyboard map to PXA controller bits. The initial
  controller target is primary surface root node 1 (used by included demos).
  Applications with other roots need a Host routing extension; pointer input
  follows the native LVGL hit test. Canvas pointer input currently has one contact.
- `stop` terminates the Worker and frees both memories. There is no persistent
  state to flush in this version. A future storage adapter must add a bounded
  cooperative shutdown/flush before termination, with a watchdog fallback.

Supported services: UI (including Canvas commands/RGB565 streams), Window,
Clock (period, timing limits/configuration, monotonic time). Unsupported imports
or services fail explicitly. No Surface/Game Render borrowed-memory operations,
audio, durable storage, package installation, external files, sensors or network.
Only curated demo modules are offered publicly; this is not a universal `.pxa`
loader. Do not advertise unimplemented services to an application.

## AI development boundary

For app/game tasks, keep SDK, ABI and board sources read-only. Use existing
services; check this capability list before promising browser preview. For
browser platform work, extend this Host adapter and test both C/C++ startup and
input behavior. Do not replace real services with success-returning mocks.
Browser frame counters measure changed frames completed by the renderer, not
physical display refresh or device throughput. Report scheduling and processing
cost separately; verify actual device performance with the normal simulator and
hardware tools.
