# Architecture

## 1. Scope

PXA System owns the user-space application environment:

- application discovery, identity and metadata;
- lifecycle and foreground task management;
- intents, RPC, events and content exchange;
- system role resolution;
- window, surface, input and focus policy;
- system service discovery and authorization;
- theme and UI environment distribution;
- integration of native and PXA execution engines.

The host operating environment owns scheduling primitives, clocks, persistent
storage, cryptography, displays, input devices, networking, audio and hardware
drivers. PXA System accesses them only through SPIs.

## 2. Layering

```text
Applications
  |- reference system applications
  |- product native applications
  `- installable PXA applications
           |
Bindings generated from the System Contracts IDL
  |- System Native API (typed, in-process)
  `- PXA wire binding (validated, sandbox boundary)
           |
Portable System Core
  |- application registry and lifecycle
  |- task, intent and message brokers
  |- role and service registries
  `- policy and capability contexts
           |
Runtime providers             System providers
  |- native runtime             |- services and drivers
  `- libpxa/WAMR runtime        |- compositor
                                `- theme provider
           |
Platform and renderer SPIs
  |- ESP-IDF / FreeRTOS
  |- POSIX / Linux
  |- LVGL / Qt / Skia / SDL
  `- test and headless backends
```

Dependencies MUST point down this diagram. The portable core MUST NOT include
headers from a runtime, renderer or platform implementation.

## 3. Contract, binding and implementation

A system feature has one normative contract. The contract defines its stable
identity, versions, operations, events, payload fields, state transitions,
authorization rules and error semantics.

Bindings expose that contract in different environments:

- The native binding uses typed C or C++ functions and can pass validated
  in-process values without serializing them.
- The PXA binding serializes the same contract into the PXA control/event wire
  format and validates all data crossing the sandbox boundary.

Bindings MUST NOT invent behavior that is absent from the contract. Both
bindings enter the same broker and service implementation. An optimized native
path MAY avoid byte encoding, but it MUST preserve validation, caller identity,
authorization, cancellation and ordering semantics.

IDL sources are authoritative. Public headers, wire codecs, validators,
documentation and golden vectors SHOULD be generated from them.

## 4. Core ownership and threading

The portable core is an explicitly created object; it is not a global
singleton. Configuration supplies capacities, allocation functions and platform
callbacks.

Core mutation is single-owner-thread and non-reentrant. Calls from other
threads are posted through a platform queue and drained by the owner. Providers
performing asynchronous work return a request token and post completion to the
owner thread. The core never assumes pthreads, FreeRTOS queues or a particular
event loop.

Large and high-rate data, including camera frames, audio and framebuffers, MUST
use streams, leases or handles. It MUST NOT be copied through the ordinary
event queue.

## 5. Runtime providers

A runtime provider turns an application artifact into one or more component
instances. It implements instantiate, start, event delivery, stop and destroy.

The first standard providers are:

- `native-static`: resolves a build-time descriptor to native function
  pointers. Native applications use the System Native API and are not required
  to link the PXA Guest SDK.
- `wamr-wasm` and `wamr-aot`: use libpxa and WAMR, with PXA wire services exposed
  through the system gateway.

Runtime type is not part of application identity. Changing an artifact from
native to AOT MUST NOT change its data, permissions, intent address or role
ownership.

Arbitrary downloadable native machine code is outside the initial security
model. Installable third-party applications use a sandboxed runtime unless a
future platform defines a secure native loader.

## 6. UI and compositor architecture

The system core has no widget API and no LVGL types. Applications present one
or more logical surfaces to the compositor. Surface roles include application,
dialog, overlay, system bar and external/raw content.

Portable UI applications use the backend-neutral UI contract. A renderer
implements transactions, resources, layout primitives, input translation and
surface attachment. The existing PXA UI 0.3 protocol is the starting point for
this contract.

A native application has three UI choices:

1. use the generated portable native UI binding;
2. request a backend-specific native surface, such as an LVGL root, and declare
   that renderer requirement in its manifest;
3. submit raw pixel surfaces for games, video or camera content.

Only the first option is renderer-portable. Backend-specific applications still
participate in the common lifecycle, window, intent, IPC and theme contracts.

The compositor owns z-order, focus, safe insets, system-bar allocation and
input routing. Renderers own backend objects. Applications never receive
another application's renderer objects.

## 7. Themes

Theme is a standard system service and UI-environment record. It includes:

- configured mode: system, light, dark or custom;
- effective color scheme: light or dark;
- contrast mode and accessibility overrides;
- semantic color, typography, spacing, radius and motion tokens;
- accent and product theme identity;
- a monotonically increasing generation.

Portable UI uses semantic tokens instead of backend colors. Theme changes are
published atomically to visible surfaces. Canvas and raw-surface applications
can query a resolved palette and redraw when the generation changes.

Products MAY replace the theme provider. A renderer MUST provide a safe
fallback for unknown tokens. New stable tokens can be appended but existing
token meaning cannot be changed incompatibly.

A custom theme has a stable namespaced identity and still declares an effective
light/dark scheme for accessibility and platform chrome. It may replace every
semantic token; the effective scheme is not a restriction to two palettes.
Products can supply the initial snapshot or publish later snapshots through the
same Theme service, without modifying the reference UI or a renderer backend.

## 8. Replaceable system roles

System behavior is selected through roles rather than linked to fixed pages.
Initial roles include home, settings, status bar, navigation bar, lock screen,
permission prompt, package installer, application manager and file picker.

Role providers are ordinary application components with additional trust and
liveness policy. A role can be provided by a reference native application, a
product native application or an authorized PXA package. The core treats these
implementations uniformly.

Foreground roles such as home and settings normally enter the Task Manager.
Persistent roles such as status bar, navigation bar and lock screen are owned
by the Role Host and do not alter the Back stack. Both use the same Role
Registry, Runtime, Intent wire and navigation policy. During replacement the
Role Host keeps the old provider until the new provider completes startup, and
tracks asynchronous retirement separately.

Products configure role candidates and precedence. Only identities authorized
by platform policy can claim security-sensitive roles. An immutable minimal
recovery shell remains available if replaceable role providers fail.

## 9. Public compatibility boundaries

The project publishes three kinds of interface:

- App ABI: application-visible contracts with the strongest compatibility
  guarantees.
- Provider SPI: runtime, renderer, service and platform integration contracts.
- Internal API: implementation details with no compatibility guarantee.

ABI and SPI structures begin with `struct_size`. Protocols negotiate a version
range and optional feature bits. A major version changes incompatible semantics;
a minor version adds backward-compatible operations or fields; a patch version
clarifies behavior without changing the encoded contract.

No product configuration, pointer value, RTOS handle or renderer object is part
of an application ABI.
