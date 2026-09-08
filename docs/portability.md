# Portability and Extensions

## 1. Platform SPI

The portable system core receives platform operations through configuration.
The minimum SPI covers:

- allocation and release with explicit alignment and memory class hints;
- monotonic time and wall-clock availability;
- owner-thread wakeup and cross-thread command posting;
- logging and diagnostic sinks;
- persistent system configuration transactions;
- random bytes and cryptographic verification hooks.

Optional providers expose filesystems, networking, audio, sensors, input,
display power and other product facilities. The core MUST compile without an
optional provider.

Platform callbacks are synchronous unless their contract explicitly returns an
asynchronous request. They do not call back into the core reentrantly.

## 2. Renderer SPI

A renderer implements backend objects for logical UI surfaces. Its SPI covers:

- renderer capabilities and environment discovery;
- create, attach, detach and destroy surface;
- begin, apply, commit and cancel atomic UI transactions;
- resource acquire/release;
- Canvas or display-list presentation;
- backend input capture and normalized event delivery;
- resolved theme-token installation;
- snapshot and diagnostics where supported.

`pxsys_renderer_host` owns the selected provider, copies its stable identity,
tracks live surfaces and rejects replacement while backend resources are
alive. `pxsys_standard_system` creates this host even when no graphical backend
is selected, so products can bind LVGL or another renderer after platform
initialization. Theme snapshots are delivered once at bind and after every
system theme update. A provider may reject the initial snapshot while binding,
but once bound it MUST accept every structurally valid snapshot. It must use
safe fallback tokens for backend limitations rather than leaving the system and
renderer on different generations.

The implemented reference backends are headless and LVGL. The LVGL adapter
also exposes an explicitly negotiated native root for legacy native apps. A second
graphical backend should be implemented before the UI SPI is declared stable,
because a single graphical implementation cannot prove that an interface is
backend-neutral.

## 3. Backend-specific native UI

A renderer MAY publish a native extension interface. An application using it
declares an exact renderer requirement and receives only a root owned by its
surface. It cannot access the compositor root or another application's objects.

Backend-native UI remains interoperable with all applications through common
system contracts, but it is not portable to a different renderer. Products
must be able to reject an incompatible application during resolution rather
than failing after launch.

## 4. Device and sensor providers

A device provider registers descriptors and operations with the service
registry. Registration includes interface identity, version range, features,
permission mapping, concurrency rules, cancellation behavior and cleanup.

Custom sensors that fit the standard sensor model use a vendor-namespaced
semantic descriptor with unit, dimensions and sample periods. They do not need
a new service protocol.

Capabilities that do not fit a standard model use a vendor interface such as:

```text
com.example.device.radar-presence
com.example.actuator.desk-motor
```

The registry negotiates this stable identity to a local handle. Native clients
use a generated binding; PXA clients use the extension-service wire gateway.
Both reach the same provider and authorization policy.

Providers can be implemented by platform driver adapters, native service
components or PXA service components. The consumer cannot distinguish them
except through declared features and performance characteristics.

## 5. Product profiles and overrides

A product profile selects:

- platform, renderer and runtime providers;
- capacities and memory budgets;
- trusted publishers and role-claim policy;
- role provider candidates;
- standard and vendor services;
- default theme and accessibility policy;
- preinstalled applications;
- recovery implementations.

Reference defaults live outside the core. Products override them through data
and provider registration, not by forking central dispatch code.

Configuration is validated before activation. Missing mandatory roles or
incompatible provider versions cause a deterministic recovery-mode boot.

## 6. Testing requirements

Every stable contract needs:

- schema validation and golden wire vectors;
- identical native-binding and PXA-binding behavior tests;
- lifecycle, cancellation and fault-injection tests;
- authorization tests with caller identity spoof attempts;
- bounded-resource and memory-pressure tests;
- provider cleanup tests;
- simulator coverage with no ESP headers;
- UI transaction tests against headless and graphical renderers;
- light, dark and theme-transition snapshots.

ESP integration tests supplement these tests; they are not the only proof of
core correctness or portability.
