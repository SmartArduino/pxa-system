# PXA Core ABI 2.1

Numeric assignments in this document are explanatory. `pxa-core.json` is the
machine-readable source of truth.

## ABI surface

Core 2 Guests import exactly `pxa_call` and `pxa_io` from `pxa.core.v2`.
There is no `pxa_submit` import or Core 0/1 compatibility binding. Packages
must declare major 2; signing and activation reject earlier majors. Only test
packages are migrated from older import majors in the original exploration.
Core 2.0 callback packages retain their transport and execution semantics on a
2.1 Host. The optional main-loop model requires explicit Host engine support.
The Guest exports start, event and stop callbacks as listed in the specification.

`pxa_call(route:u32, token:u64, ptr:u32, len:u32) -> status:i32` borrows the
payload range until return. `route = service | (opcode << 16)`. There is no
encoded request envelope. `ok` means applied or asynchronous work accepted,
not that asynchronous work completed. Input ranges are validated by WAMR.
Services copy any data that must survive the import. The Guest cannot mutate
or grow linear memory while its synchronous import executes.

Any operation that must return data uses a nonzero request token. Completion is a
later Host-to-Guest message with the same service, opcode and request token. This
keeps the Core import read-only and avoids per-service mutable-buffer rules.

Every asynchronous result payload begins with `status:i32`. When status is
`ok`, service-defined result data follows. In Core 2, a failed result ends
after the status; service-defined success data and diagnostic records are
absent. This rule can be extended in a later minor version only through an
explicitly defined common diagnostic record namespace.

`pxa_io` operates on one resource Handle. It returns a nonnegative byte count or
a negative PXA status. Resource operations remain nonblocking except the
explicitly negotiated Core 2.1 event-port wait described in
[the main-loop contract](abi-2.1-main-loop.md). Synchronous operations may consume CPU time. A zero-byte result is valid only when a
service defines end-of-stream for that operation; temporary lack of progress is
reported as `would-block`.

## Message envelope

Host mailboxes and Guest callbacks use the same canonical envelope from
`pxa-core.json`; there is no intermediate request-token mapping or second codec.

```text
offset  size  field
0       2     service (nonzero)
2       2     opcode (nonzero)
4       8     request_token
12      4     payload_len
16      4     flags (zero)
20      N     payload
```

The exact length is `20 + payload_len`; trailing bytes and nonzero flags are
invalid. The control message limit is 4096 bytes (4076 payload bytes).

The current `pxa_call` payload admission limit remains 4076 bytes. This is a
bounded control-plane budget, not a reason to copy or encode an envelope.
Bulk data uses resource I/O with service-specific negotiated capacities. A
larger input limit must be designed independently from completion/event pool
budgets; enlarging every event block would increase all applications' memory.

Invalid payloads return `invalid-argument`; unavailable services/opcodes return
`unsupported`. The adapter validates memory, import capability and lifecycle;
the service owns payload semantics. Optional execution-kernel operations are
implemented by the engine adapter, rather than a second service whitelist.
Internal native 32/64-bit service helpers use an explicit wire
profile, independent from the Guest Core import major; all Core 2 Guests use
64-bit handles. This does not enable an older Guest import namespace.

For game-render 0.6 the optional `borrow-commands` creation flag consumes a
DrawList synchronously inside `pxa_io`: validation and rasterization finish
before return, then display proceeds asynchronously. No Guest pointer is kept.
`would-block` accepts nothing and consumes no frame ID. The Host must support
this mode or reject creation with `unsupported`; it must not silently defer
execution of borrowed bytes. ESP and desktop support it; desktop consumes on
the product loop thread and retains only the rendered Host framebuffer until
presentation. Default queued mode snapshots resources and copies the
commands. Merely pinning memory is insufficient for asynchronous zero-copy:
pinning prevents relocation, not mutation after validation.

## Extensible records

Payloads described as record lists contain consecutive records:

```text
tag:u16 | payload_len:u16 | payload
```

The high bit of `tag` marks a record optional. Receivers reject an unknown
required tag and skip an unknown optional tag. Duplicate and ordering rules are
defined per record list; receivers must not silently choose between duplicate
single-cardinality records.

## Request lifecycle

A request token is scoped to one Component instance, not globally to an App.

```text
Guest                Host
  | request(id=N)      |
  |------------------->|  immediate accepted/rejected status
  |                    |
  | result(id=N)       |  exactly once after acceptance
  |<-------------------|
```

Rules:

- Zero is never an asynchronous request token.
- The Guest cannot reuse N until its completion has been taken from the mailbox.
  Pending work and queued completions use one shared request table. There is no
  adapter-level 16-slot limit: capacity follows the configured Runtime budget.
  Defaults remain 16 per Component; larger budgets are explicitly configured.
- Immediate rejection creates no pending request and produces no result.
- Accepted work finishes with a normal result or `cancelled`.
- A cancel command names the target request token in its payload. Cancellation is
  idempotent but may race with normal completion; only one final result is
  delivered.
- Component termination cancels requests without requiring delivery to the
  terminated instance.

## Handle lifecycle

Guest Handles are opaque nonzero `u64` values. A Host uses a slot generation or an
equivalent mechanism so a stale value cannot refer to a later resource. Every
Handle has an owning Component, type, permission scope and lifetime.

Closing a valid Handle succeeds exactly once. Reusing that value after close,
or presenting a Handle owned by another Component, returns `not-found`. This is
intentional: treating a stale value as success would hide lifetime bugs and
make authority mistakes harder to diagnose. A higher-level service operation
may separately define an idempotent release command when its application model
requires one.

Component termination closes every owned Handle. Handles are not serialized to
files or IPC messages as transferable authority; a service must explicitly
create a new Handle for another Component.

Host readiness notifications are edge-coalesced hints. After receiving
`handle-ready`, the Guest calls `pxa_io` until it returns `would-block`. The
Guest must tolerate spurious readiness and a Handle becoming closed before it
uses the notification.

## Event mailbox and backpressure

Each Component has a bounded Host-to-Guest mailbox. Events are classified by
the service definition as either reliable or coalescible:

- A coalescible event has a nonzero key scoped to one Component mailbox. A new
  event with the same key replaces the queued event in place.
- Coalescible events cannot consume the mailbox capacity reserved for reliable
  events.
- A reliable event is never silently replaced or dropped. If the mailbox has
  no capacity, its producer receives `busy` and retains responsibility for
  retrying or retaining equivalent out-of-band state.
- An asynchronous request remains pending until its final reliable result has
  been admitted to the mailbox. Mailbox pressure therefore cannot turn an
  accepted request into a lost completion.
- Before accepting an asynchronous request, Core reserves a result event slot
  and enough event blocks for that operation's maximum completion payload.
  Resource exhaustion rejects the request immediately, before backend work or
  an externally visible effect. A completed result may wait behind a full
  mailbox while retaining its reserved storage. If a backend exceeds its
  declared result bound, Core delivers a `limit-exceeded` completion using
  that reservation instead of leaving the accepted request pending.

The exact capacity and reliable reserve are runtime limits reported in startup
configuration. They are not ABI constants.

## Startup configuration

`pxa_app_start` receives a record list. Required singleton records identify
the Component instance, App and Component. Repeated records describe supported
service versions, granted capabilities and final resource limits.

The startup configuration reports actual Host grants. Manifest requirements are
installation or activation constraints; they are not a substitute for runtime
capability checks.

Record 12 (`system-environment`) contains the initial system configuration for
the Component. Its nested records use the same payload as the System service
`configuration-changed` event: canonical BCP 47 locale is required record 1,
and text direction is optional record 2. A UI App applies this record before
its first render; later changes arrive through the event. This avoids exposing
the App's source-language fallback while an asynchronous configuration event is
still queued.

## Event callback result

`pxa_app_on_event` returns:

- `0` when an event is not handled;
- `1` when it is handled;
- a negative PXA status on Guest failure.

Only services that define a default Host action inspect handled/unhandled. For
example, an unhandled Window back event closes the current UI Component. Most
notifications ignore a positive handled result.

## Core leases

`acquire-lease` is asynchronous and uses a record-list payload. A successful
result contains a lease Handle. Closing the Handle releases the lease. The Host
can revoke a lease and emits `lease-revoked` before or while invalidating the
Handle, subject to event delivery capacity.

For `pxa.core.v2`, acquire uses the 64-bit request token. The success
result's tag 4 contains a native 64-bit Lease Handle. The token-zero
`lease-revoked` notification contains that Handle followed by `reason:i32`,
for a 12-byte payload. Close and expiry validate the full Handle generation.
`pxa_lease.h` builds the bounded request and parses both responses.

A lease permits continued scheduling; it does not itself grant access to audio,
network or sensors. Those remain separately permissioned service resources.
