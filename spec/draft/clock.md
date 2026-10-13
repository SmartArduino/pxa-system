# PXA Clock Draft 0.2

Clock v1 exposes a bounded periodic wake-up for foreground Components.
`set-period` uses a `u16` millisecond period: zero cancels the timer and values
16 through 1000 arm or update it. Other values are invalid.

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
