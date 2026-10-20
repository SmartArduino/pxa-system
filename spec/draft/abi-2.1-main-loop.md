# Core 2.1 exclusive main-loop execution

This optional mode retains the native call stack on the existing engine owner
thread. It adds neither a Guest thread nor a Host event mailbox or framebuffer.
It does not establish device FPS or original audio fidelity.

## Admission and startup

A package declares `min_sdk`, `target_sdk`, and `compile_sdk` at least `[2, 1]`,
with exactly one `main` UI component containing `"execution_model":"main-loop"`.
The signed component flag is bit 1 (value 2). Pinned memory remains independent.
Manifest parsing rejects main-loop declarations with an older Core, a Job, or
multiple components. A Host activation profile must advertise
`PXA_EXECUTION_MODEL_MAIN_LOOP`, obtained from its configured engine. Merely
bumping a version or advertising a service cannot enable this model.

The WAMR adapter requires a configured native `cooperate` callback and monotonic
clock. The imports remain `pxa.core.v2.pxa_call` and `pxa.core.v2.pxa_io`.
Start/event/stop exports are unchanged. The Host publishes activation before
dispatching the first main-loop event: that event may not return until stop.
Core 2.0 callbacks retain their original path and never acquire an event port.

## Event port

Core opcode 5 opens one instance-owned port. The empty request uses a nonzero
token and returns the ordinary completion `status:i32, handle:u64`. Completion
capacity is reserved before allocating the handle. A duplicate open is rejected.
Wait is allowed only inside a running event callback, not start or stop.

Operation `0x201` on this typed handle takes an event buffer of 20..4096 bytes.
The first eight request bytes hold `timeout_us:u64-le`, at most 60000000.
The Host runs a native maintenance checkpoint, even if an event is already
ready. Success replaces the buffer with one unmodified standard envelope and
returns its byte size. The Guest dispatches this event through ordinary C/C++
calls; the Host does not recursively call the event export.

Zero timeout returns WOULD_BLOCK if no event is ready; a positive elapsed timeout
returns TIMED_OUT. An oversized head returns RESOURCE_LIMIT without consuming
the event or modifying caller bytes. The SDK has already written the timeout
request; callers that need arbitrary events should provide the 4096-byte limit.
Wrong-owner and stale handles fail. No Guest pointer survives the import, and
memory.grow between waits requires a fresh address translation as usual.

## Owner pump and stop

Native maintenance may process input, timers, permissions and I/O completions.
It must not enter any Guest or destroy/replace the active runtime. A lifecycle
or package mutation remains queued; the pump returns CANCELLED. WAMR unwinds
the current call and reports `cooperative_cancelled`. The Host must stop that
instance after return, then process the retained command. Its lost call stack
cannot be resumed. Cancellation is a terminal VM-call outcome, not a recoverable
status returned to the Guest loop. Out-of-bounds/traps remain ordinary faults.

The ESP reference mode gives the system BACK action to the Host: it stops the
exclusive game. In-game menus use application pointer/controller events.
Background/lock lifecycle messages may pause the retained game loop; starting
another package ends exclusive ownership. Guest jobs and another Guest-based
IME/provider cannot execute while this exclusive model owns the engine.

Waiting excludes native maintenance time from the call deadline. A completed
checkpoint renews a still-valid execution slice; it never erases an already
expired deadline or substitutes for an operator decision. Explicit termination
wins over deadline restoration, including termination during a native wait.
Wake notifications must be latched across the queue check and sleep. The Host
must also yield periodically during zero-time checkpoints so IDLE/native workers
are not starved by a CPU-only load loop.

## Evidence required before product enablement

The adapter tests exercise interpreted Wasm and x86 AOT: queue-head preservation,
memory.grow, wrong-owner/stale handles, timeouts, overdue operator extension,
external termination, cancellation, traps and disabled-backend/old-Core gates.
They also reject nested Guest entry during native maintenance and check complete
request/handle/event cleanup. Package tests cover execution-model admission.
ESP integration still needs a signed device probe for foreground, input,
background, stop/replacement and heap/stack recovery before game measurements.
