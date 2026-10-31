# PXA Browser Host (experimental)

Runs standard PXA Core 2 WebAssembly applications in the browser VM. A separate
Emscripten module builds the existing libpxa Core, platform services and
LVGL renderer. The Guest C and C++ SDKs and wire ABI are unchanged. This is an
application host, not the complete device firmware, launcher or installer.

## Build and test

Prerequisites: Python 3, CMake, Emscripten 4.0.6 (`emcmake` on PATH), a WASI SDK
(the PXA 0.7.0 DevKit includes one), and Node 24+ (JSPI) for integration tests. LVGL is
pinned to 9.6.0. Supply `--lvgl` to reuse an existing checkout offline.

```sh
python3 simulator/browser/build.py \
  --build-dir local/artifacts/browser-build --out local/artifacts/browser-bundle \
  --wasi-sdk /path/to/pxa-devkit/toolchains/wasi-sdk
PXA_BROWSER_BUNDLE="$PWD/local/artifacts/browser-bundle" \
  node --test simulator/browser/*.test.mjs
```

`--host-build` reuses a separately compiled Host (useful with Emscripten in a
container). It must contain the Host files and toolchain license copies emitted by CMake; also supply
`--lvgl` for the matching license. Build outputs stay outside the source tree.
The bundle includes a C Canvas game, a native-input/persistence/audio notes app,
the unchanged C counter template and the unchanged C++ Hello template. The last is an SDK interoperability test fixture. An allocator-free main-loop
fixture exercises suspension, lifecycle, input and cancellation.

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
- Require exported `memory` and PXA callback entry points. Allocator-free SDK
  builds work by preserving LLVM's `__stack_pointer` name. The loader exposes
  that global and reserves a callback event on the Guest stack; function bodies
  are unchanged. Otherwise `malloc`/`free` exports are required. Memory and table
  maxima are bounded before instantiation (512 MiB, 65,536 table entries).
  Chip-specific AOT artifacts cannot run here; retain the original Wasm.
- Core 2.1 main-loop components use WebAssembly JSPI, detected before startup.
  Event-port waits suspend the Guest while Host timers, graphics and input keep
  running. No Guest callback is entered on top of a suspended callback. Initial
  foreground and focus changes follow the same lifecycle protocol as desktop.
  Unsupported browsers show an explicit error; callback applications do not
  require JSPI. Pause retains queued events; cooperative stop unwinds a pending
  wait before invoking the stop callback.
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
  and 256 MiB. Writes complete against MEMFS; persistence is debounced 150 ms and
  flushed on pause/cooperative stop. A tab crash/forced close can lose unflushed
  writes. Storage errors are visible; unsupported browsers explicitly report
  volatile mode. App IDs must be assigned by the trusted loader, never Guest code.
- Verified package data mounts under private `/package`; app resource packs
  mount under `/media`. Assets use the shared catalog, image loaders, read queue
  and cache, including resident PCM, PXR, PNG, JPEG and GIF decoding. Limits are
  4096 files and 256 MiB per load. Only the chosen primary Wasm entry runs; optional
  secondary providers/scripts are not implicitly started. File imports use a
  native picker, publish complete files atomically, preserve existing names and
  persist user files separately from bundled media. Stop cancels in-flight reads.
- Audio mixes/resamples in the Host Worker; an AudioWorklet consumes a bounded
  60 ms PCM queue. Start/resume requires a browser user gesture. Output uses the
  system default audio device. Page mute controls this app only: browsers do not
  expose OS master-volume control. PCM/tone/effects and borrowed PCM/PSG
  computation are implemented. Resident PCM sounds use the shared cache and mixer. Packaged Ogg music uses
  browser media decoding and the same AudioContext, with PXA playback events.
  Optional mono 16 kHz raw Opus capture uses AudioWorklet and WebCodecs after an
  explicit microphone click; unsupported browsers return UNSUPPORTED.
- Network declarations come from the verified package. Standalone demos use
  Fetch/CORS with omitted cookies. The store offers an authenticated same-origin
  HTTP relay and an outbound TCP/TLS WebSocket relay. Both validate authority,
  reject private/multicast/mixed DNS results, pin the resolved public address and
  verify TLS hostnames. Only public ports 80/443 are exposed; no LAN, raw UDP or
  arbitrary-port access. Store cookies/CSRF are never sent upstream. Ring buffers,
  quotas, timeouts and receiver credit bound each connection; streaming uploads
  are capped at 2 MiB. Browser link-state queries do not probe a remote server.
- Surface supports copied RGB565 and premultiplied ARGB8888 buffers with LVGL
  composition. GameRender reuses the portable rasterizer, uploads, bindings and
  compute kernels. Mapped Surface registers a pinned Host shadow and copies only
  the submitted bank across the two Wasm memories. This is not zero-copy.
  Reliable RELEASED events retain bank ownership until accepted; memory.grow
  refreshes views. Direct hardware scanout is unavailable. Surface scaling follows the current Core contract (same-size
  layers). RGB565 frames are expanded into a reusable presentation buffer on
  submission, so pixel edges are exact and UI redraws do not repeat scaling.
  RGB565 Surface presentation follows the desktop 1×/2×/4× integer
  fit and centering contract; GameRender preserves aspect ratio within the display.
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
and normal service errors. Assets, Lease and an empty physical Sensor inventory
are registered. Hardware MAC access, Work, PXA IME-provider registration and
StoreInstaller are unavailable. Native text entry replaces the device keyboard.
The store serves only authenticated, reviewed public releases, verifies both
package signatures and inventories, and checks per-file SHA-256 in the page.
A primary UI entry may run without automatically starting auxiliary scripts or
system input-method providers. Resource generations must match the signed code
reference, app identity and publisher lineage. Required unimplemented services
remain gated; an entry passing startup is not a complete gameplay guarantee.
Private storage derives from publisher lineage plus app ID, independent of slug
or release version. Uploading arbitrary archives in the browser is unsupported.
WASI exposes basic clocks, randomness, arguments and stdout/stderr; filesystem
access uses the PXA FS service, not WASI preopened directories.

## Distribution

The Host follows the [system license policy](../../docs/zh-CN/system-license-policy.md).
Retired engine-specific span kernels are not linked; LVGL's optional FrogFS and
ThorVG implementations are excluded from compilation. Browser bundles retain
PXA, LVGL, font and toolchain licenses plus `THIRD_PARTY_NOTICES.md`. Application modules have
independent licenses. The repository's top-level MIT text does not replace
individual third-party file licenses.

## AI development boundary

For app/game tasks, keep SDK, ABI and board sources read-only. Use existing
services; check this capability list before promising browser preview. For
browser platform work, extend this Host adapter and test both C/C++ startup and
input behavior. Do not replace real services with success-returning mocks.
Browser frame counters measure changed frames completed by the renderer, not
physical display refresh or device throughput. Report scheduling and processing
cost separately; verify actual device performance with the normal simulator and
hardware tools.

## Resource delivery in Browser Host 0.7

Verified resource descriptors may reference `Uint8Array` views into one downloaded
bundle. The runtime preserves their offsets and mounts those views in private MEMFS
without copying the entire bundle for each file. Deduplicate transferable buffers
before sending them to the Worker. Guest package files remain read-only; imported
media and saved data retain their separate persistence lifecycle.
