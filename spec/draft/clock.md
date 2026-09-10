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
