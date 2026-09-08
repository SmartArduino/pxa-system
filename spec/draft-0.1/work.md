# PXA Work Draft 0.1

Work service 13, version 0.1.0, runs bounded background work in a declared `job`
Component. It follows the same model as Android WorkManager: an enqueue delay is
the earliest eligible start time, not a timer guarantee and not the worker's
lifetime. Apps use Work for deferrable persistence, synchronization, uploads and
maintenance. A Clock timer remains the right API for callbacks in a running UI.

## Enqueue

`enqueue` (opcode 1) takes increasing records: worker Component ID (tag 1),
initial delay in milliseconds (tag 2), execution hint in milliseconds (tag 3),
optional opaque input (tag 5), retry delay (tag 6), and maximum attempts (tag
7). A zero execution hint requests the Host default. The result is Core status,
then on success a nonzero Work ID (tag 4) and the granted execution time (tag
8).

The Host stores at most eight queued items per installed App. Initial delay is
0 through 7 days. Execution defaults to 10 seconds and is capped at 60 seconds.
Input is at most 24 bytes; larger state belongs in Storage. Maximum attempts is
1 through 5. A retryable Work request uses a retry delay from 1 second through
7 days.

## Worker lifetime

At activation the worker receives immutable start records: Work ID (Core config
tag 7), one-based attempt number (tag 9), monotonic deadline in milliseconds
(tag 10), and optional input (tag 11). Its lifetime begins when activation
starts. It ends when the worker calls `complete` (opcode 3) with the same Work
ID and one of `success`, `retry`, or `failure` (result tag 9).

`success` and `failure` are terminal. `retry` queues the same Work ID after its
retry delay and increments the attempt. When the attempt limit is reached,
`retry` becomes terminal failure. A worker that reaches its deadline receives a
reliable `stop-requested` event (opcode `0x8001`) carrying Work ID and deadline.
It has 500 ms to return a completion; after that the Host stops it and applies
retry policy. Host scheduling checks may add a small amount of latency.

Delivery is at least once. Work must therefore use its Work ID or application
state to make side effects idempotent.

## Cancel

`cancel` (opcode 2) takes one Work ID record (tag 4). Queued Work is removed
atomically. Running Work is stopped after the current Guest callback returns
and is not retried. An unknown or already terminal ID returns `not-found`.

## Time and host scope

All times use the Host monotonic clock. Pending Work remains eligible after the
foreground UI exits while that App is the resident Package. Starting another
Package ends the resident scope. The current ESP and simulator Hosts do not
promise reboot recovery because no trusted wall clock is part of Work 0.1;
they must not reinterpret a deadline from an older monotonic epoch.

Calendar schedules, periodic Work, network or charging constraints, progress
observation, notifications, and concurrent background execution from multiple
Packages are outside version 0.1.
