# PXA Permission Draft 0.1

Permission v1 is service 11. It separates a signed Package declaration from a
persisted policy decision and a Component-local runtime authority. A Guest can
check or acquire a declared permission; it cannot grant itself access or cause
a policy UI to appear during a callback.

## Declaration and policy

The signed Package Manifest declares a dotted permission name, `required` flag
and optional canonical scope bytes. Identity is the signed
`(publisher-key-id, app-id)` tuple. A Host discards persisted grants that are
not exactly present in the current signed Manifest, including scope bytes.
There is no prefix, wildcard, Unicode normalization or implied scope match.

All declarations default to deny. A Host policy may persist allow or deny. A
required declaration must be allowed before activation; a preferred declaration
does not block activation. The service never exposes policy storage, a Host
path, user identity or implementation-specific grant identifier to Guest code.

## Guest requests

`check` and `acquire` use `name` and optional `scope` records with nonzero
request IDs. Every result begins with Core `status:i32`.

- successful `check` appends `decision:u8`;
- successful `acquire` appends a Component-owned permission Handle;
- denied `acquire` has only `status=denied`.

A service that creates network, sensor, audio, IPC or Secrets resources binds
those resources and any pending operation to the authority represented by the
permission Handle. The Handle itself is not transferable to another Component
or App; a service resolves it to its Core authority before opening the resource.

## Revocation

When policy changes from allow to deny, the Host first revokes all authorities
issued for the exact declaration. Core then closes authority-bound Handles and
cancels uncommitted authority-bound requests. The Host posts reliable `revoked`
best-effort notification to affected running Components. Delivery failure never
delays revocation, so Guests must treat every later operation as independently
authorized.

Revocation does not implicitly stop an App. A Host may separately stop a
Component with `permission-revoked` when its product policy considers the
permission essential.
