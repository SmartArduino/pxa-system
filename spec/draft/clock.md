# PXA Clock Draft 0.3

Clock v1 exposes a bounded periodic wake-up for foreground Components.
`set-period` uses a `u16` millisecond period: zero cancels the timer and values
16 through 1000 arm or update it. Other values are invalid.

This is the current protocol range, not a hardware timer precision claim.
The C SDK exposes `PXA_CLOCK_MIN_PERIOD_MS` / `PXA_CLOCK_MAX_PERIOD_MS` and
returns `PXA_STATUS_INVALID_ARGUMENT` without sending a command when the
period is outside that range. Applications must check configuration errors.
An accepted period requests wake-ups; load and event coalescing may delay
delivery. Lower-period extensions require explicit capability negotiation.

Each `tick` carries a little-endian `u64` monotonic timestamp in microseconds.
Its epoch is unspecified. Tick events are coalescible and do not represent a
count of elapsed periods; Components calculate elapsed time from timestamps.

`now` accepts a nonzero request ID and an empty payload. The Host replies with
`now-result` using the same request ID; its payload is an `i32` status followed
by a little-endian `u64` monotonic timestamp. The response is asynchronous so
the Clock service remains consistent with the rest of the Guest event model.

The timer is a Component-owned resource. The Host revokes it automatically
before `pxa_app_stop`, because Core imports are forbidden during that
callback. Background scheduling and durable alarms belong to a separate
service and are not implied by this API.

## Core 1 preview binding

The `pxa.core.v1` binding keeps `set-period` as a one-way command with envelope
token zero and the same two-byte payload. `now` uses a nonzero 64-bit request
token and returns a reserved completion with opcode `now` and payload
`status:i32` followed by `timestamp_us:u64` on success. A failed completion
contains only status. Tick events retain opcode `0x8001`, envelope token zero
and their eight-byte timestamp. The v0 `now-result` opcode remains specific to
Core 0; it is not a second completion path in Core 1.

`pxa_clock.h` provides no-allocation builders and typed completion/tick
parsers. The Host reserves completion capacity before reading the clock so a
successful `now` request cannot lose its result under mailbox pressure.

## Wall time and the effective system time zone

Clock 0.2 adds `wall-time` (opcode 3) for Core 1/2. Its request has a nonzero
token and an empty payload. The reserved completion contains status followed
by exactly 24 bytes on success:

| Offset | Type | Meaning |
|---|---|---|
| 0 | i64 | UTC microseconds since 1970-01-01, excluding leap seconds |
| 8 | u64 | Host monotonic microseconds sampled with the wall clock |
| 16 | i32 | Local time minus UTC, in seconds; includes daylight saving |
| 20 | u32 | Bit 0: daylight saving active; other bits zero |

The offset is for this sampled instant, including non-hour offsets and DST.
Local time is UTC plus the offset. This snapshot does not provide an IANA
identifier, historical/future zone rules, or a claim that network time is
synchronized. Applications must not reuse its offset to convert arbitrary
dates. The reference POSIX adapter reads the Host's effective local time zone;
it never changes the process `TZ` or loads a database into Guest memory.

The Host reserves completion capacity before sampling. The operation does not
change periodic ticks or application scheduling. Applications can refresh on
foreground and periodically while displaying wall time, rather than polling
each frame. Older Hosts reject the opcode or a package requirement for Clock
0.2; there is no silent assumption of a particular local offset.

## Device timing capabilities (0.3)

Core 1/2 Hosts advertising the `timing` feature (bit 0) implement `limits`
(opcode 4, empty request) and `configure` (opcode 5, `period_us:u32`). Both
require a nonzero token and a reserved completion with the same opcode/token.
A package using these operations requires Clock >= 0.3 and the timing feature,
or queries Core capabilities and retains a legacy fallback on older Hosts.

`limits` returns status followed by exactly 20 bytes: `min_period_us:u32`,
`max_period_us:u32`, `period_step_us:u32`, `dispatch_quantum_us:u32`, `flags:u32`.
Minimum, step and quantum are nonzero; maximum is at least minimum. Valid
nonzero periods lie within the range and are multiples of the step (not an
offset from the minimum). Flags bit 0 means ticks are coalesced; bit 1 means
foreground-only scheduling. Unknown flag bits may be ignored. The dispatch
quantum describes scheduling granularity, not a maximum delivery delay or a
hard real-time guarantee. It is independent of the accepted period step.

`configure` returns status and exactly `accepted_period_us:u32` on success.
Zero cancels; a nonzero accepted value must equal the request. Unsupported
values complete with INVALID_ARGUMENT, preserving the previous timer. A
backend failure also preserves it. Malformed envelopes fail synchronously.
There is no silent rounding. Legacy `set-period` retains 0 / 16..1000ms.
Both operations replace the same Component-owned timer; they do not create
multiple subscriptions. Reconfiguration invalidates queued ticks. Cancelling,
pausing or destroying a Component discards stale ticks; resume rebases its
next deadline. There is no backlog replay. Delayed ticks carry the current
monotonic time, so applications use elapsed time rather than counting ticks.

The ESP Host uses a serialized, one-shot timer for the nearest runnable
subscription, separate from existing pointer/system maintenance. Pending and
paused subscriptions cause no Clock timer wake-ups. Event consumption rearms
the deadline; at most one tick per Component is queued while Guest runs. The
reference GameKit range is 1000..60000000us, step 1us, dispatch quantum 1us.
The SDL product simulator reports quantum 1000us; its platform scheduling and
Guest workload can add delay. A microsecond wire format alone never implies
microsecond callback precision.
