# PXA Browser Host (experimental)

Runs standard PXA Core 2 WebAssembly applications in the browser VM. A separate
Emscripten module builds the existing libpxa Core, platform services and
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
  node --test simulator/browser/*.test.mjs
```

`--host-build` reuses a separately compiled Host (useful with Emscripten in a
container). It must contain `pxa-browser.mjs` and `pxa-browser.wasm`; also supply
`--lvgl` for the matching license. Build outputs stay outside the source tree.
The bundle includes a C Canvas game, a native-input/persistence/audio notes app,
the unchanged C counter template and the unchanged C++ Hello template. The last is an SDK interoperability test fixture.

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
- Supply the real display environment as standard startup configuration. Browser
  system fonts supply CJK glyphs through a bounded alpha-glyph cache; glyph
  metrics use the browser alphabetic baseline. Without OffscreenCanvas, use the
  bundled Latin fonts. The demo canvas is 320×240 and CSS preserves aspect ratio;
  the Host accepts display sizes 64–1024. Surface dimensions follow their ABI
  fields and allocation budget, not the display dimensions.
- Text fields open a trusted DOM input/textarea. Native composition, clipboard
  paste and mobile keyboards work without a PXA candidate keyboard. UTF-8 byte
  limits and Unicode cursor positions are preserved. Focus sessions reject stale
  replies; password fields are cleared on close and never logged.
- LVGL paints XRGB8888. The Worker converts BGRA to RGBA only on changed frames.
  OffscreenCanvas presents without a page round trip; the fallback allows one
  transferred frame in flight. The Worker owns the 60 Hz clock. Only input,
  native-service notifications and once-per-second statistics reach the page.
- Standard gamepads and keyboard map to digital controller bits. Routing finds
  the subscribed root, regardless of its ID. Escape sends Window Back. Volume
  keys retain their OS action; a PXA key event is also sent if the browser exposes
  the key and the application subscribes. Pointer input uses LVGL hit testing and
  currently supports one contact. Hidden tabs pause and release held buttons.
- Each app identity has an IndexedDB-backed private filesystem and an exclusive
  Web Lock to prevent concurrent writers. KV and filesystem quotas are 256 KiB
  and 8 MiB. Writes complete against MEMFS; persistence is debounced 150 ms and
  flushed on pause/cooperative stop. A tab crash/forced close can lose unflushed
  writes. Storage errors are visible; unsupported browsers explicitly report
  volatile mode. App IDs must be assigned by the trusted loader, never Guest code.
- Audio mixes/resamples in the Host Worker; an AudioWorklet consumes a bounded
  60 ms PCM queue. Start/resume requires a browser user gesture. Output uses the
  system default audio device. Page mute controls this app only: browsers do not
  expose OS master-volume control. PCM/tone/effects and borrowed PCM/PSG/GB-PSG
  computation are implemented. Asset-based music/sound playback is not yet wired.
- Network requests use Fetch with exact-origin PXA permissions, CORS, omitted
  cookies and rejected redirects. The loader supplies approved `networkOrigins`;
  default is none. CSP must separately permit each origin. Legacy Fetch/HTTP
  requests work with bounded bodies; HTTP_OPEN is enabled when request streaming
  is available. Its rings are 512–65536 bytes, independent of transfer size.
  Streaming uploads also depend on browser/server protocol support. Cancellation,
  timeouts and quota failures propagate; no CORS bypass proxy is provided.
- Surface supports copied RGB565 and premultiplied ARGB8888 buffers with LVGL
  composition. GameRender reuses the portable rasterizer, uploads, bindings and
  compute kernels. Guest-mapped persistent buffers and direct hardware scanout
  are unavailable. Surface scaling follows the current Core contract (same-size
  layers); GameRender fits its output within the display with aspect preserved.
- Borrowed compute operations use a reusable sparse memory mirror, preserving
  original offsets and aliasing for the existing C validators. Only referenced
  ranges are copied, and only validated output ranges are written back. Mirror
  reservation is capped at 64 MiB, transfer at 4 MiB per call. This avoids copying
  the whole Guest heap but is not zero-copy and increases peak Host memory.
- Stop runs the Guest stop callback, releases resources and flushes storage before
  terminating. A three-second watchdog can force termination; the page reports
  that persistence was not confirmed. No background execution survives tab close.

`capabilities.json` is the machine-readable support inventory distributed with
this Host. Registered services are UI, Window, Clock, Storage, FS, Audio, Net,
Surface, GameRender, Device runtime information, Log, Permission and an isolated
IPC broker. Not every optional operation is available; consult the inventory
and normal service errors. Hardware MAC access is unavailable. Assets, Sensor,
Lease, Work, PXA IME-provider registration and StoreInstaller are not wired.
Native text entry replaces the device input method; it is not a fake IME provider.
Only curated Wasm modules are loaded publicly, not arbitrary `.pxa` archives.
WASI exposes basic clocks, randomness, arguments and stdout/stderr; filesystem
access uses the PXA FS service, not WASI preopened directories.

## Distribution

GameRender links GPL-2.0-or-later span kernels derived from Quake and
recrof/Quake-ESP32S3; retain their source notices and the bundled GPL license.
Browser releases include the corresponding full source archive and build scripts,
in addition to PXA/LVGL/font notices. The repository's top-level MIT text does
not replace individual third-party file licenses.

## AI development boundary

For app/game tasks, keep SDK, ABI and board sources read-only. Use existing
services; check this capability list before promising browser preview. For
browser platform work, extend this Host adapter and test both C/C++ startup and
input behavior. Do not replace real services with success-returning mocks.
Browser frame counters measure changed frames completed by the renderer, not
physical display refresh or device throughput. Report scheduling and processing
cost separately; verify actual device performance with the normal simulator and
hardware tools.
