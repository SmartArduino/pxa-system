# PXA Sensor Draft 0.1

Sensor v1 is service 8. It exposes Host-described semantic sensors, not I2C
buses, vendor registers or board-specific device names. A descriptor has a
stable-for-one-activation numeric ID, a canonical semantic ID, unit, dimension
count and accepted sampling-period range. IDs are discovered with `list`; Apps
must not persist them across activation. Unit values are nonzero Draft registry
values. Core treats them as descriptor metadata so later registry entries do not
require a Core ABI or implementation change; Apps that do not recognize a unit
must not infer its scale.

## Permission and subscription

`list` requires no permission because it returns only semantic capability
metadata. `subscribe` requires a Component-owned Permission Handle for the
exact declaration `sensor.read` scoped to the descriptor semantic ID. The Host
binds the resulting Sensor Handle to the same Core authority, so permission
revocation atomically closes the subscription and prevents later samples.

The request has `sensor-id:u16`, `period-ms:u32` and `permission-handle:u32`
records in ascending tag order. Its successful result appends one Sensor
Handle. Closing that Handle stops delivery. The requested period must be within
the descriptor range; a missing descriptor returns `not-found`.

## Samples and backpressure

`sample` is a coalescible Host event. Its record list contains subscription
Handle, monotonic microsecond timestamp, sample count and packed signed
little-endian `i32` values. Values are ordered by time then descriptor
dimension. Draft v1 emits one sample per event; the count field reserves the
same format for bounded batching without changing the event ABI.

The Guest interprets scale and dimensions from the descriptor. A temperature
value with unit `milli-celsius`, for example, is signed degrees Celsius times
1000; an illuminance value with unit `milli-lux` is lux times 1000. Sample loss
by coalescing is permitted. Results, revocations and all
permission state remain reliable under the Core mailbox rules.
