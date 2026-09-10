# PXA Core Wire Draft 0.1

Numeric assignments in this document are explanatory. `pxa-core.json` is the
machine-readable source of truth.

## ABI surface

The Guest imports `pxa_control` and `pxa_io` from `pxa.core.v0`. The Guest
exports its API version, start, event and stop callbacks as listed in the
machine-readable specification.

`pxa_control` receives a read-only Guest-memory range containing exactly one
message. Its return value is an immediate status: `ok` means that the command
was applied synchronously or an asynchronous request was accepted. It does not
mean that asynchronous work completed.

Any operation that must return data uses a nonzero request ID. Completion is a
later Host-to-Guest message with the same service, opcode and request ID. This
keeps the Core import read-only and avoids per-service mutable-buffer rules.

Every asynchronous result payload begins with `status:i32`. When status is
`ok`, service-defined result data follows. In Draft 0.1, a failed result ends
after the status; service-defined success data and diagnostic records are
absent. This rule can be extended in a later minor version only through an
explicitly defined common diagnostic record namespace.

`pxa_io` operates on one resource Handle. It returns a nonnegative byte count or
a negative PXA status. It never blocks. A zero-byte result is valid only when a
service defines end-of-stream for that operation; temporary lack of progress is
reported as `would-block`.

## Message envelope

Every control request, result and event uses a 12-byte header:

```text
offset  size  field
0       2     service
2       2     opcode
4       4     request_id
8       4     payload_len
12      N     payload
```

The message length must equal `12 + payload_len`; trailing data is invalid. A
Core implementation must reject control messages larger than the negotiated
limit, which is at most 4096 bytes in Draft 0.1.

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

A request ID is scoped to one Component instance, not globally to an App.

```text
Guest                Host
  | request(id=N)      |
  |------------------->|  immediate accepted/rejected status
  |                    |
  | result(id=N)       |  exactly once after acceptance
  |<-------------------|
```

Rules:

- Zero is never an asynchronous request ID.
- The Guest cannot reuse N while N is pending.
- Immediate rejection creates no pending request and produces no result.
- Accepted work finishes with a normal result or `cancelled`.
- A cancel command names the target request ID in its payload. Cancellation is
  idempotent but may race with normal completion; only one final result is
  delivered.
- Component termination cancels requests without requiring delivery to the
  terminated instance.

## Handle lifecycle

Handles are opaque nonzero `u32` values. A Host uses a slot generation or an
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

A lease permits continued scheduling; it does not itself grant access to audio,
network or sensors. Those remain separately permissioned service resources.
