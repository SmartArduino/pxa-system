# PXA Component Lifecycle Draft 0.1

## State machine

```text
created -> starting -> running -> stop-requested -> stopping -> stopped
                \-> stopped                    \-------------> stopped
```

- `created`: the Artifact is instantiated but Guest code has not run.
- `starting`: `pxa_app_start` is the active callback.
- `running`: the Component may receive serialized events.
- `stop-requested`: shutdown is latched, but an active callback is allowed to
  return. No new event callback starts.
- `stopping`: `pxa_app_stop` is the active callback.
- `stopped`: requests, events, Handles and leases have been invalidated.

The Host never invokes two callbacks concurrently and never re-enters Guest
code from an import. A stop request made during a callback is deferred until
that callback returns. Faults that make further Guest execution unsafe may
abort directly to `stopped` without calling `pxa_app_stop`.

## Import legality

`pxa_control` and `pxa_io` are legal only while `pxa_app_start` or
`pxa_app_on_event` is active. They are illegal from `pxa_app_stop`, because
stop is a bounded notification rather than a cleanup phase in which new work
can be acquired. The Host owns resource cleanup regardless of whether the stop
callback returns normally.

A successful start moves the Component to `running`. Any negative start result
moves it directly to `stopped` and closes resources acquired during start.
A negative event result requests stop with reason `fault`. Event results above
`handled` are protocol errors.

## Cancellation and commit points

Each asynchronous service operation defines a commit point:

- Before commit, cancellation wins and the only result is `cancelled`.
- After commit, cancellation is acknowledged but cannot rewrite history. The
  operation delivers its normal success or failure result.
- Result publication wins if it was retained by Core before cancellation.

The commit point is where externally durable or irreversible effects become
observable, such as a filesystem rename, transmitted packet, IPC acceptance or
permission prompt decision. A service must call the Core commit primitive at
that boundary; it must not infer commit from queueing work.

Core retains a completed result until it can enter the reliable mailbox. The
service transfers result ownership once completion returns `ok` and must never
retry that completion. Retained results preserve completion order.

## Package and multi-Component activation

Package installation and Component activation are separate transactions. An
installed Package is not executable until the Host has validated the Package's
Core and Service requirements and selected one compatible Artifact for the
requested Component. Selection completes before the engine opens or validates
the Artifact.

Each Component is an independent runtime instance with its own lifecycle,
linear memory, request namespace, Handles and event mailbox. `.wasm` and `.aot`
are alternative implementations of that same Component identity; they do not
change its permissions, IPC identity or private-data authority. Components
never call or link each other through engine-private symbols. They communicate
through PXA IPC using the same rules as Components from different Apps.

Activation is on demand rather than an all-Package start operation:

- a UI Component activates when the App gains a foreground Window;
- a service Component activates when a declared endpoint receives work or a
  background policy explicitly starts it;
- a job Component activates for one scheduled or externally triggered run.

Draft 0.1 permits at most one live instance of one Component ID per installed
App session. Multiple different Components may run concurrently. A Host first
prepares a deterministic activation plan for every Component, but instantiates
only the requested entry. A failed Component start tears down that instance and
does not implicitly stop unrelated running Components.

For one activation the ordering is:

1. validate Core and Service requirements;
2. select the deterministic Artifact;
3. apply permissions and resource limits for the Component;
4. validate and instantiate the Artifact without running Guest code;
5. register the Core instance in `created` state;
6. invoke `pxa_app_start` under the normal lifecycle rules.

Steps 1 through 4 must not make the Component externally addressable. Any
failure after engine instantiation destroys the engine instance and removes the
Core instance. Deactivation requests stop, invokes the bounded stop callback
when legal, performs Host-owned resource cleanup and finally destroys the
engine instance.

## Permission revocation

Runtime resources and requests may carry a nonzero opaque authority ID assigned
by the permission broker. Revoking an authority atomically invalidates its
Handles and cancels its uncommitted requests. Committed requests finish under
the cancellation rule above, but cannot acquire new resources using the
revoked grant. Authority IDs are Host-internal and never replace manifest
permission names or public Handle values.
