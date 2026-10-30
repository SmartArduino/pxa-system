# Haptics service 0.1

Haptics is standard service 23. It controls a single foreground actuator through
an authority-bound Core v1 64-bit resource (type 16). C clients include
`pxa_haptics.h`; C++ clients use `ctx.haptics()`. Hosts without an actuator return
zero capability flags and zero maximum duration, and reject Open as Unsupported.

All integers are little endian. Requests use the Core envelope with a nonzero
request token. Every completion starts with an i32 status. Failed completions
contain only that status; successful bodies follow the table below.

| Opcode | Request | Successful body |
|---|---|---|
| 1 Capabilities | empty | flags u32, max-duration-ms u32 |
| 2 Open | permission u64 | actuator u64 |
| 3 Vibrate | actuator u64, duration-ms u32, amplitude u16, reserved u16=0 | empty |
| 4 Stop | actuator u64 | empty |

Capability bit 0 is vibration; bit 1 is amplitude control and requires bit 0.
Unknown bits are reserved. Amplitude is 1–1000; a binary actuator accepts 1000
and returns Unsupported for other strengths. Duration must be nonzero and no
greater than the advertised maximum. Stop cancels immediately. A successful
Vibrate replaces the current pulse and acknowledges acceptance, not completion
of its physical motion. Timing is enforced by the backend independently of Guest
execution. Capability flags are device properties, not service feature bits.

Open requires permission `haptics.play` scoped to `device.default`, foreground
ownership and an unused actuator. Core handle closure, permission revocation,
component stop and foreground loss close the actuator and stop its output. A
closed actuator must be reopened; stale and foreign handles cannot control it.
The Host reserves the reliable completion before starting physical output. A
backend error stops the output and returns a known failure status.

C++ example (inside a foreground coroutine):

```cpp
auto caps = co_await ctx.haptics().capabilities();
if (!caps || !caps->supported()) co_return pxa::Result<void>{};
auto permission = co_await ctx.permissions().acquire("haptics.play", "device.default");
if (!permission) co_return std::unexpected(permission.error());
auto actuator = co_await ctx.haptics().open(*permission);
if (!actuator) co_return std::unexpected(actuator.error());
co_await actuator->vibrate(100);
// Retain permission and actuator while the effect should remain active.
```

The backend must stop before its deadline even if the runtime stops making
progress. Board-specific strength, waveforms and calibration are not part of
version 0.1. ESP-MOSAICO advertises binary vibration with a 1000 ms maximum.
