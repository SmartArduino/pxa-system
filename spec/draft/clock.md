# PXA Clock Draft 0.1

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
