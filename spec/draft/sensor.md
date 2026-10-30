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

The request has `sensor-id:u16`, `period-ms:u32` and `permission-handle:u64`
records in ascending tag order. Its successful result appends one raw `u64`
Sensor Handle after the status, without a record header. Closing that Handle stops delivery. The requested period must be within
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

The unit registry also assigns `nano-tesla` value 5 for magnetic field vectors:
1000 represents 1 microtesla. Accelerometers use unit 2 (micrometres/s²),
gyroscopes unit 3 (microradians/s). Dimensions follow the descriptor's axis
order; three-axis samples are ordered X, Y, Z. Canonical motion semantics use
the device frame below; other vector semantics document their own frame.
`micro-volts` (6) and `micro-amperes` (7) represent electrical measurements.
Battery current is signed; the board documents its charge/discharge polarity.
A battery temperature channel uses the existing `milli-celsius` (1).

## Device axes for semantic motion

`motion.acceleration` and `motion.angular-velocity` describe the device, not
the sensor chip's mounting. With an integrated display in its native
orientation, +X points toward the top edge, +Y toward the left edge and +Z
out of the screen. These axes form a right-handed frame. Devices without a
display document equivalent housing reference edges. The reference remains
fixed when an App changes its view or rotates its measurement interface.

The Host's board provider converts chip axes into this frame before publishing
either channel. Acceleration and angular velocity use the same proper rotation;
changing a single sign to compensate one App's display is not a mounting
rotation. Units, timestamps and vector lengths are preserved by this conversion.
Angular velocity follows the right-hand rule: rotation viewed from the +Z
screen side is positive when counterclockwise. A stationary device with its
screen facing upward reports approximately +9,806,650 micrometres/s² on Z;
screen-down reports the negative value. Stationary angular velocity is near zero.

UI pixel Y increasing downward is a presentation convention and does not
change this physical frame. Apps may project to screen coordinates or apply a
user-selected measurement rotation, without chip- or board-specific remapping.

## Core 1.0 binding

Under `pxa.core.v1`, Subscribe request tag 3 holds a native 64-bit Permission
Handle and its successful completion carries a native 64-bit Sensor Handle.
The coalescible Sample event's first record carries that same 64-bit Sensor
Handle. The remaining descriptor and sample fields retain their widths.
The Host rejects four-byte Permission Handles for v1 Components and validates
the full generation when closing a subscription. Sample coalescing uses the
full-width Handle, so reusing a low slot index does not merge two generations.
The descriptor nesting uses tags 1 through 6. Sample records are tag 4
(subscription Handle), tag 2 (timestamp), tag 3 (count = 1), then tag 4
(one to three signed values). The current Host emits one sample per event,
not eight; a Guest must not assume batching is implemented.
The Guest uses `pxa_sensor.h` to build requests and parse borrowed
descriptor views and typed sample values.
