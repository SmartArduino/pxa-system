# Architecture audit disposition

This document records the result of checking the 2026 libpxa architecture
audit against the implementation. It separates confirmed defects from design
constraints so later work does not treat stale line numbers as current facts.

## Confirmed and addressed

- Runtime coordination, Component lifecycle, service dispatch, Requests,
  Handles and Events now have separate `src/core/` owners and one private
  Runtime layout.
- Service implementations, UI internals and package processing now live under
  explicit `src/services/`, `src/ui/` and `src/package/` boundaries. Manifest,
  inventory and artifact selection are separate translation units.
- All three build frontends consume `cmake/pxaSources.cmake`; new Core
  sources cannot silently disappear from ESP or simulator builds.
- Service-table saturation, hash collisions, Event block boundaries, stale
  tokens, Request completion ordering, Handle/Event generation exhaustion and
  cleanup ordering have focused Host tests.
- UI Registry reserve is rollback-safe when chunk or bucket allocation fails,
  its load-factor arithmetic cannot overflow, and Surface ID allocation checks
  live IDs when the counter wraps.
- Permission allow prompts remain pending after a policy-store failure and can
  be retried. Activation destroys an engine instance while its Core Component
  is still present in the STOPPED state, then removes the Component.
- Backend status normalization, workspace alignment and UTF-8 scalar decoding
  have one internal implementation each. Service-specific validation remains
  explicit.
- A copied `libpxa` tree can use system LZ4 instead of a sibling component.
  Test vectors are local; cross-repository package-tool tests are conditional.
  Installed Core/POSIX/OpenSSL CMake targets are consumable through one package.
- The ESP Host publishes a bounded read-only activation snapshot to UI and
  app-management threads instead of exposing mutable Runtime/service state.
  Clock slots coalesce pending Ticks and reject stale generations; the Net
  backend does not clear or copy large PSRAM buffers while holding a cross-core
  critical section.
- Runtime, WAMR, UI, decoded-image upper bound, activation arena, Audio and Net
  high-water metrics are logged before activation teardown. The watchdog poll
  period is independent from the frame clock and defaults to 50 ms.

## Reports not reproduced

- Little-endian wire helpers use byte shifts and work on either Host byte order.
- `UNSUPPORTED` and `BUSY` are known statuses and are preserved by status
  normalization; only values outside the public status range become INTERNAL.
- Event block size/count and mailbox capacities are caller-configurable rather
  than fixed constants.
- Surface-open event-post failure already closes the backend Surface and removes
  its Core entry.
- Canvas Present transfers display-list ownership only on success; this is now
  explicit in the public callback contract and the UI 0.3 specification.
- Package parsing already enforces sorted Component IDs before activation-plan
  preparation. Lease revocation already retries its PENDING_EVENT state.
- Permission identities/declarations are copied into service workspace, and FS
  directory output is zero-initialized before the backend call.
- Permission authority records are reused from a bounded pool. Their 64-bit IDs
  are intentionally monotonic so releasing a record cannot revive stale
  authority references.

## Deliberate compatibility decisions

- Core, service, UI and package APIs are single-owner-thread and non-reentrant.
  Runtime thread assertions need a new Host callback/config field and are not
  inferred from platform APIs. The WAMR watchdog controls may cross threads only
  through the adapter's configured synchronization callbacks.
- Reusing a 16-bit generation after wrap would revive stale Handles or Event
  tokens, so exhausted slots retire. Wider tokens require an ABI decision.
- `WOULD_BLOCK` remains the record-iterator end sentinel. Adding a new EOF code
  changes the frozen status/wire contract; the header now documents the result.
- UI allocator categories, per-Component FS quotas, slot transaction IDs and
  new error-code distinctions require public configuration or durable-format
  changes. They should be versioned rather than added as private behavior.
- Container fields remain decoded by explicit byte offsets. Mapping wire bytes
  onto a native C struct would introduce padding, alignment and endian hazards.
- The LVGL adapter prefers a configured parent target. Its source-tree fallback
  still discovers LVGL sources for standalone compatibility and is intentionally
  not the production integration path.

ESP-IDF compilation and device execution are separate platform verification
gates; Host tests do not claim to replace them.
