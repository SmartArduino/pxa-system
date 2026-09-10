# PXA Binding

[简体中文](zh-CN/pxa-binding.md)

## 1. Service envelope

The PXA binding reserves service ID `17` for the PXA System gateway. Version
`0.1` defines these Guest requests:

| Opcode | Operation | Payload |
| ---: | --- | --- |
| 1 | Intent start | canonical `PXIN` record |
| 2 | invoke a system service | ordered service records |
| 3 | publish a topic event | ordered topic records |
| 4 | subscribe to a topic | topic and minimum version records |
| 5 | unsubscribe from a topic | topic and version records |
| 6 | register a PXA service endpoint | interface, version and features |
| 7 | unregister a PXA service endpoint | interface and major version |
| 8 | complete an inbound service call | call ID, status and result bytes |

Request IDs MUST be nonzero. The gateway does not introduce a runtime-specific
target or identity prefix. Service interfaces and topics are the same textual
identities accepted by the native binding and common registries.

Completion uses the normal libpxa request event. The first four payload bytes
are the signed little-endian PXA status emitted by `pxa_request_complete`.

The Host emits reliable unsolicited events on the same service:

| Opcode | Event | Payload |
| ---: | --- | --- |
| `0x8001` | subscribed topic event | ordered topic records |
| `0x8002` | inbound service request | service records plus call and caller identity |
| `0x8003` | initial or subsequent Intent | canonical `PXIN` plus caller identity |

An inbound service request carries a gateway call ID distinct from the Guest's
control request IDs. Its caller publisher root, App ID and component ID are
added by the Host. The Guest returns that call ID with opcode `8`; it never
supplies or rewrites caller identity.

### Ordered record schemas

All integers are little-endian. Tags with the PXA optional bit may be skipped
by an older decoder; mandatory unknown tags reject the message. Records remain
ordered by raw tag.

Service invocation uses:

| Tag | Requirement | Value |
| ---: | --- | --- |
| 1 | mandatory | UTF-8 interface identity |
| 2 | mandatory | major `u16`, minor `u16` |
| 3 | mandatory | operation `u32` |
| 4 | optional | flags `u32` |
| 5 | optional | operation payload bytes |

Topic publish, subscribe, delivery and unsubscribe use:

| Tag | Requirement | Value |
| ---: | --- | --- |
| 1 | mandatory | UTF-8 topic identity |
| 2 | mandatory | major `u16`, minor `u16` |
| 3 | mandatory | event `u32`; zero for subscribe/unsubscribe |
| 4 | optional | sequence `u64` |
| 5 | optional | event payload bytes |

Service registration uses tags 1 and 2 from service invocation plus optional
tag 3 containing feature bits as `u64`. Completion uses mandatory tag 1 call
ID `u64`, mandatory tag 2 signed PXA status `i32`, and optional tag 3 result
bytes.

The Host extends inbound service-request events with optional tag 6 call ID
`u64`, tag 7 caller publisher root (32 bytes), tag 8 caller App ID, and tag 9
caller component ID. They are required by the current Guest helper even though
the optional bit permits future compatible envelope evolution.

Intent events use mandatory tag 1 for the unchanged canonical `PXIN` record.
When a caller exists, optional tags 2, 3 and 4 carry its publisher root, App ID
and component ID as one all-or-none principal. These fields are injected by
the Host. Failure to queue the initial Intent causes the Runtime launch to
fail; later single-instance launches deliver another Intent event.

Control and event payloads are bounded by the libpxa control-message limit.
The ESP bridge additionally uses configured bounds for subscriptions, exported
services and pending calls. Exhaustion returns `resource-limit`; it never grows
an unbounded queue.

## 2. Caller identity

The Guest does not send caller identity. The ESP Host derives publisher root
and App ID from the verified active manifest and resolves the current libpxa
component handle back to a declared component ID. The gateway copies this
principal beside the request before crossing threads.

The component handle is an opaque transport correlation token. It is never
part of the public App namespace and cannot be used as an identity.

## 3. Threading

Guest control calls execute on the PXA worker. A gateway request is begun on
that worker, copied into a bounded owner-thread message, and handled by the
same task, service, or event broker used by native callers. Completion is
copied into a PXA command and finalized on the PXA worker.

No portable system object is mutated from the PXA worker, and no libpxa
runtime object is mutated from the UI owner thread. Requests whose App or
component has stopped before completion are discarded by libpxa ownership and
generation checks.

## 4. Evolution

Future opcodes for theme observation and content handles MUST reuse this
transport and caller binding. They MUST NOT add a second PXA-only service
namespace. New opcode payloads use ordered PXA records for their outer fields
and retain the canonical payload of the underlying system contract.

Theme is already part of the standard PXA UI environment rather than a
system-gateway opcode. `color_scheme` is `PXA_UI_COLOR_SCHEME_LIGHT` or
`PXA_UI_COLOR_SCHEME_DARK`. The Host includes the effective value in startup
configuration and posts `PXA_UI_ENVIRONMENT_CHANGED` after an atomic system
theme update. Guests must redraw semantic-token and Canvas content in response.

Topic subscriptions and exported PXA service endpoints are owned by the active
component. Stop cancels its outstanding inbound calls and unregisters its
subscriptions and endpoints before the runtime instance is destroyed. Late or
forged completions are rejected by call ID and the Host-derived component
principal.
