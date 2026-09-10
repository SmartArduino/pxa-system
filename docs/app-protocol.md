# Application and Protocol Model

[简体中文](zh-CN/app-protocol.md)

## 1. Application identity

The canonical application identity is the tuple:

```text
(publisher lineage root, application ID)
```

It is shared by native and PXA applications. Runtime prefixes such as
`native:` and `pxa:` MUST NOT appear in the identity namespace. Component,
endpoint, role and private-data ownership derive from this identity.

The wire representation remains structured. A canonical text representation
is used only in configuration, diagnostics and tooling.

Application display name, version, icon, runtime artifact and component IDs are
metadata and do not affect identity.

## 2. Application manifest

Native and installable applications use the same logical manifest model. It
contains:

- identity and display metadata;
- components and their artifact/runtime requirements;
- provided roles, intent filters and service endpoints;
- required service versions and feature bits;
- declared permissions and resource requirements;
- theme and renderer capabilities;
- lifecycle and instance policy.

Native manifests can be generated into firmware tables. PXA manifests remain
signed package content. Both are normalized into the same registry descriptor.

## 3. Lifecycle

An application instance moves through these states:

```text
descriptor discovered
       |
instance allocated -> creating -> ready -> starting -> background
                                               |          |
                                               |          `-> foreground
                                               |
                                               `-> stop requested
foreground <-> background -> stop requested -> stopping
                                                    |
                                                    `-> destroy pending -> destroyed
```

Failed creation or start follows explicit rollback. Stop has a reason such as
normal, replaced, policy, resource pressure, permission revoked, fault or
shutdown. A runtime provider MUST release component resources even after a
partial start.

Task and application are distinct. A singleton application can own one task;
a document-oriented application can own several instances. The task manager
owns foreground order and back history. An application's internal routes are
private to that instance.

## 4. Intents and navigation

Applications MUST navigate through the Intent service rather than calling a
target implementation. An intent contains:

```text
explicit target identity (optional)
action
URI and MIME type (optional)
flags and launch policy
versioned argument records
correlation ID for a result (optional)
```

The resolver matches an explicit target or manifest intent filters, checks
visibility and permissions, activates the target when necessary and delivers
the intent as a lifecycle event.

Required operations include resolve, start, deliver-to-existing-instance,
finish and finish-with-result. Back handling proceeds through system modal,
application dialog, application handler, private route history, task history
and finally the home role.

Native-to-PXA, PXA-to-native and same-runtime navigation MUST traverse the same
resolver and produce the same observable lifecycle sequence.

After resolution and before any lifecycle or foreground state changes, the
navigation policy receives the system-bound caller principal, operation,
original Intent and resolved target descriptor. A denial therefore has no
partial navigation side effects. A null caller denotes a trusted
system-originated request, not an anonymous application.

### 4.1 Intent wire record draft 1

The portable implementation uses one big-endian record for both native and PXA
delivery. The fixed 40-byte header is:

| Offset | Type | Field |
| ---: | --- | --- |
| 0 | `u32` | magic `PXIN` |
| 4 | `u16` | version (`1`) |
| 6 | `u16` | header bytes (`40`) |
| 8 | `u32` | flags |
| 12 | `u64` | correlation ID |
| 20 | `u32` | target App ID bytes, zero when implicit |
| 24 | `u32` | action bytes |
| 28 | `u32` | URI bytes |
| 32 | `u32` | MIME bytes |
| 36 | `u32` | argument bytes |

When a target is present, the body begins with its 32-byte publisher lineage
root and App ID. Action, URI, MIME and opaque versioned arguments follow. There
is no runtime discriminator in the record.

PXA delivery wraps this unchanged record in the System service's Intent event.
The outer ordered records may also carry the Host-authenticated caller
publisher root, App ID and component ID. Native delivery carries the same
principal beside the same encoded Intent; applications never supply it.

## 5. Cross-application communication

The system offers four communication forms:

- Intent: activation and user-facing navigation.
- RPC endpoint: bounded request and response.
- Topic event: permissioned publish and subscribe.
- Content URI or stream: bulk data with explicit lifetime and authority.

Applications MUST NOT exchange pointers, renderer objects, driver handles or
raw platform paths. Capability handles are non-transferable unless a service
explicitly defines a delegation operation.

An endpoint identity combines provider application identity, component and a
declared endpoint name. Public endpoint metadata includes a stable interface
identity, semantic version, visibility and required permission.

The native binding can submit typed values directly. The PXA gateway converts
wire records into the same internal message. Both paths carry an unforgeable
caller application/component identity supplied by the system.

Caller identity is metadata beside the encoded payload, not a caller-writable
field inside it. A native binding invokes `pxsys_task_manager_start_as()` using
its bound component principal; the PXA gateway derives the same principal from
its active component context. The convenience `pxsys_task_manager_start()`
entry point is reserved for trusted system-originated navigation.

## 6. Service namespace

Published standard services retain reviewed numeric wire IDs for compact PXA
messages. Extensible interfaces use a globally unique textual identity derived
from the publisher namespace and interface name. A service registry negotiates
that identity and version into a process-local handle.

This avoids permanent global numeric allocations for product extensions while
keeping existing PXA services compatible.

Standard service behavior and vendor extension behavior use the same request,
completion, event, cancellation and resource-lifetime rules.

## 7. System roles

Role identity is stable and implementation-independent. Initial role names are:

```text
system.role.home
system.role.settings
system.role.status-bar
system.role.navigation-bar
system.role.lock-screen
system.role.permission-prompt
system.role.package-installer
system.role.app-manager
system.role.file-picker
system.role.theme-provider
system.role.settings.sound
system.role.settings.network
system.role.settings.bluetooth
system.role.settings.alarm
system.role.file-manager
system.role.device-info
```

A product profile maps each role to ordered provider candidates. Selection
considers trust, compatibility, enabled state, health and explicit product or
user policy. Role assignment changes are transactional and fall back to the
last healthy provider or recovery implementation.

Settings launches these roles through the same Intent path used by every other
application. A provider may therefore be a firmware-built Native application
or an installed PXA application. The legacy `app_pages` bridge publishes its
sound/display, network, Bluetooth audio, alarm, file manager and device-info
pages as low-priority candidates; a product can replace any one of them without
forking the standard Settings UI.

## 8. Theme contract

The Theme service exposes configured mode, effective light/dark scheme,
contrast, accent, token set and generation. Applications can follow the system
or request a per-window light/dark preference; they cannot change global theme
without permission.

The base semantic token set covers backgrounds, surfaces, primary/secondary
text, borders, accent and on-accent text, error/warning/success, scrim,
typography, spacing, radius and motion. Resources can provide light, dark and
high-contrast variants through metadata rather than filename conventions.

Theme and UI-environment changes use reliable coalescing by generation. A
renderer applies the new environment atomically across system and application
surfaces to avoid mixed-theme frames.

## 9. Errors and asynchronous work

All contracts share a stable error vocabulary: invalid argument, unsupported,
not found, denied, busy, timeout, cancelled, resource limit, unavailable,
conflict and internal fault.

Asynchronous requests have a commit point. Cancellation before commit prevents
the operation; cancellation after commit reports the committed result. Provider
completion is exactly once. Late completion for a stopped component is dropped
after releasing transferred resources.

## 10. Installation and catalog commit

Package verification, durable storage and application visibility are separate
transactions. The platform installer first verifies signature, publisher
lineage, manifest compatibility, permissions and artifacts, then commits the
package store, and only then publishes the verified manifest to the common app
registry. A failed registry publish leaves the durable package installed but
not launchable and must be retried during catalog reconciliation.

Install, upgrade and uninstall use the same canonical identity as native apps.
An upgrade replaces registry metadata atomically only when the old descriptor
has no live references. Otherwise it returns `busy`; policy may stop the app
and retry, or defer activation until reboot. Runtime type can change during an
upgrade without changing identity or private-data ownership.
