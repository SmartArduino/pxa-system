# PXA System PXA Binding

This component is the sandbox-side binding between verified libpxa packages
and PXA System. It normalizes package management identity and metadata into the
same application registry used by native applications and translates status
codes at the gateway boundary.

`pxsys_pxa_catalog_publish()` is the storage-independent commit point for a
verified install or upgrade. It adds a new identity or atomically replaces its
metadata when no live task, subscription, or other registry reference holds
the old version. `pxsys_pxa_catalog_unpublish()` removes the same canonical
identity. Storage adapters remain responsible for durable package transactions
and invoke these functions only after commit.

`pxsys_pxa_client` derives a component principal from a verified manifest and
then delegates Intent, RPC and topic operations to the same core bound-client
implementation used by `pxsys_native_client`. Runtime gateways decode Guest
wire messages and call this API; they never trust a caller identity supplied by
Guest memory.

It does not own installation policy, WAMR, rendering, or task navigation. ESP
products provide those through the package store, runtime provider, renderer,
and portable core respectively.
