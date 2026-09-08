# PXA System

PXA System is a portable application environment for embedded products. It
defines the application model above an RTOS or desktop host: identity,
lifecycle, discovery, navigation, communication, system services, UI surfaces,
themes, permissions, installation and replaceable system roles.

It is not a kernel and does not replace FreeRTOS, ESP-IDF, Linux or device
drivers. Those environments are integrated through platform and service
provider interfaces.

The name is provisional. The contracts in these documents are intentionally
separated from product naming and from the current ESP implementation.

## Design goals

1. Native and sandboxed PXA applications share one application identity,
   lifecycle, service namespace and communication contract.
2. Native applications do not need the PXA Guest SDK. They use a generated
   native binding over the same contracts used by the PXA wire binding.
3. Neither the system core nor application contracts depend on LVGL, ESP-IDF,
   FreeRTOS, WAMR or a particular display architecture.
4. LVGL is one renderer implementation. Other renderers can implement the same
   UI and compositor SPIs.
5. The desktop, settings, status bar and other system experiences are selected
   by system roles. A product can use the reference implementation, replace it
   with native code, or install an authorized PXA implementation.
6. Standard services can be extended without assigning every vendor a global
   numeric service ID or changing the core library.
7. All public ABI and SPI contracts are versioned and testable with shared
   conformance vectors.

## Document map

- [Architecture](architecture.md) describes layers, dependency rules,
  threading, UI backends and deployment profiles.
- [Application and protocol model](app-protocol.md) defines identity,
  lifecycle, intents, cross-application communication and system roles.
- [PXA binding](pxa-binding.md) defines how Guest requests cross the sandbox
  and thread boundary into those same contracts.
- [Portability and extensions](portability.md) defines platform ports,
  renderer backends, device providers and vendor extensions.
- [Product integration](product-integration.md) defines the composition,
  replaceable system UI and external ESP service boundaries.
- [Migration plan](migration.md) maps the current `app_pages`, `page_manager`
  and PXA integration into incremental implementation milestones.
- [Implementation status](implementation-status.md) records what exists on the
  current branch, its verification, and the remaining ESP product integration.

## Normative language

`MUST`, `MUST NOT`, `SHOULD`, `SHOULD NOT` and `MAY` describe compatibility
requirements. Draft documents can evolve incompatibly until a contract is
declared stable. Stable contracts follow the compatibility policy in the
architecture document.
