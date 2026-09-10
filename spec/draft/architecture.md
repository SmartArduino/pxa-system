# PXA Architecture Draft 0.1

## Platform model

```text
Package
|- identity, publisher, permissions and private-data ownership
|- components
|  |- ui: one foreground UI component
|  |- service: event-activated background components
|  `- job: bounded scheduled work
|- artifacts
|  |- portable WebAssembly
|  `- target and engine-specific AOT variants
`- immutable assets
```

An App is the security, signing, permission, installation and private-data
boundary. A Component is an independently instantiated execution boundary with
its own linear memory and lifecycle. An Artifact is one executable form of one
Component.

Components never communicate through shared Wasm memory or direct exported
calls. Intra-App and inter-App communication both use the IPC broker. A Host may
optimize the transport without changing the observable IPC semantics.

## Stable and replaceable layers

The Core ABI defines only lifecycle execution, control messages, non-blocking
IO, handles, asynchronous request semantics, capabilities and resource
lifetime. Window, UI, Clock, FS, Storage, IPC, Sensor, Network, Audio,
Permission and Secrets are independently versioned services.

WAMR AOT compatibility is an Artifact concern. LVGL is a UI adapter. LittleFS
is a storage backend. None of them are part of the public PXA vocabulary.

## Execution model

- A Component is single-threaded from the Guest's perspective.
- The Host never re-enters a Component while a Guest callback or import is
  active.
- Events are serialized per Component. Ordering between different Components
  is not defined unless an individual service defines it.
- Guest callbacks have a Host-enforced deadline. Deadline expiry terminates the
  Component and invalidates its resources.
- Control calls carry small bounded messages. Bulk and streaming data uses
  non-blocking handle IO.
- Resource limits are negotiated at startup. A manifest can distinguish hard
  requirements from preferences.

## Background execution

Background Components are activation-based, not permanent daemons. Apps submit
deferrable tasks through Work; the Host activates a declared worker, grants a
bounded deadline and stops it after completion or timeout. Specialized
foreground, audio, network and sensor integrations may use lower-level
resource leases internally, but application Work never manages a lease.

State that must survive deactivation belongs in the App's private data. A
service or worker Component must tolerate termination and later activation.

The precise callback states, import legality and cancellation commit rule are
defined by `lifecycle.md`.

## Artifact selection

Each Component may provide portable Wasm and multiple AOT Artifacts. The Host:

1. selects an AOT Artifact only when target, engine, engine ABI, Wasm feature
   set and memory model all match;
2. otherwise selects a compatible portable Wasm Artifact;
3. rejects Component activation if neither is usable.

Engine multi-module linking is not a PXA contract. Libraries may be statically
linked into an Artifact. Independently running modules communicate through IPC.

## Capability and permission model

Access follows four separate checks:

```text
Host capability
  -> manifest declaration
  -> user or system-policy grant
  -> scoped runtime handle
```

A capability says that a Host implements a feature. A permission declaration
says that an App may request it. A grant records the current policy decision. A
handle represents actual access with a specific scope and lifetime.

Revocation immediately invalidates affected handles and cancels related
requests. Permissions are bound to publisher identity plus App ID, not to an
untrusted package string alone.

## UI ownership

Window controls the App surface's relationship to system UI: metrics, insets,
edge-to-edge layout, system-bar visibility and system navigation. App bars,
titles and action buttons are regular declarative UI.

Canvas is a UI node with a retained, length-delimited display list and logical
input coordinates. It is not a game-specific service and does not expose LVGL
drawing objects.

## Realtime audio boundary

Audio rendering and capture run in a Host-managed realtime graph. Audio threads
never execute Wasm. Guest Components configure graph transactions and exchange
PCM or encoded packets through bounded streams. The Host owns mixing,
resampling, device routing, focus, ducking and realtime deadlines.

## Package identity and private data

A Package contains a signed manifest and file hashes. App identity is the
publisher identity plus App ID. `/data` is shared by Components in that App,
retained across upgrades and isolated from other Apps. `/cache`, `/temp`,
read-only package assets, user-granted files and protected Secrets have separate
lifetimes and policies.

## Draft-to-1.0 rule

Core 1.0 is frozen only after at least two Host implementations, portable Wasm
and AOT conformance, malformed-input fuzzing, request/handle lifecycle tests,
resource-pressure tests, and one real service extension that required no Core
ABI change.
