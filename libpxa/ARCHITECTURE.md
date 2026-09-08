# libpxa architecture

`libpxa` is a platform-neutral, single-owner-thread C99 library. Public headers
describe the stable Host ABI and wire contract; internal file organization is
free to evolve without changing that contract.

## Dependency direction

Dependencies point inward in this order:

1. `include/pxa/` defines public values, wire types and opaque APIs.
2. `src/common/` provides private, stateless implementation utilities.
3. `src/core/` owns Runtime coordination and bounded Core state.
4. `src/services/`, `src/ui/` and `src/package/` own their domain state.
5. service implementations depend on the public Runtime API, not Runtime
   internals.
6. `adapters/` implement backend tables and may depend on platform libraries.

Core code must not include adapter headers. Service modules must not inspect the
opaque Runtime representation. Cross-module state changes go through an owner
API instead of sharing writable structs.

## Source layout

```text
include/pxa/                 stable public API only
src/
|-- common/                  checked math, bytes/UTF-8, status normalization
|-- core/                    Runtime, Component, Request, Handle, Event, wire
|-- services/<name>/         one owner directory per Host service
|-- ui/                      UI service, memory, registry and transactions
`-- package/                 manifest, inventory, artifact and activation flow
adapters/                    optional platform backends
tests/                       public API and focused internal regression tests
```

Private headers remain under `src/` and are never installed. Public headers do
not include them.

## Current ownership

| State | Owner | Access path |
| --- | --- | --- |
| Runtime limits, workspace and init/deinit | `core/runtime.c` | Public Runtime API |
| Component lifecycle and authority policy | `core/component.c` | `core/runtime_internal.h` |
| Request slots, lookup and completion queues | `core/request.c` | `core/request_internal.h` |
| Resource Handles and deferred close state | `core/handle.c` | `core/handle_internal.h` |
| Event slots, blocks and Component mailboxes | `core/event.c` | `core/event_internal.h` |
| Registered service operations and dispatch | `core/service_registry.c` | Private registry API |
| UI service, memory, registry and transactions | `ui/` | `ui/ui_internal.h` |
| Manifest, inventory and artifact selection | `package/` | Public package APIs |
| Filesystem, storage, sensor, scheduler, network and audio state | `services/<name>/` | Backend operation tables |

`src/common/checked_math.h`, `status_internal.h`, `bytes.c` and
`bytes_internal.h` are the single implementations of checked workspace layout,
backend-status normalization, byte comparison and UTF-8 scalar validation.
Service-specific policy remains at the call site.

`core/runtime_internal.h` is the only definition of the opaque Runtime layout.
It is private to Core: services, UI, package code and adapters must use public
Runtime APIs rather than including it.

Core, service, UI and package APIs remain single-owner-thread and non-reentrant.
The WAMR adapter deadline controls are the narrow exception: a watchdog may
inspect or terminate the active guest call from another thread only when the
Host configures the adapter's paired synchronization callbacks. No Runtime or
service call is made from that watchdog thread.

## Build source ownership

`cmake/pxaSources.cmake` is the single source list for the standalone
library, ESP component and desktop simulator. Platform adapters remain listed
by their platform target.

## Change rules

- Do not expose private layouts or private headers through installed includes.
- Keep workspace sizing and initialization driven by the same checked layout.
- Preserve the Runtime Core's fixed-capacity behavior: it performs no allocation
  after initialization. Services with an explicit Host allocator keep their
  documented allocation policy.
- Keep callbacks outside partially updated state; cleanup establishes the final
  Core state before notifying resources or services.
- Treat handle/token widths and wire fields as compatibility decisions, not
  internal refactors.
- Generation exhaustion retires a Handle/Event slot instead of making an old
  token valid again. Changing that availability-versus-safety tradeoff requires
  a wider public token or an ABI revision.
