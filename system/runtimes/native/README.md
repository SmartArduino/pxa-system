# pxa_system_native

Native-static application runtime provider for PXA System. Native applications
register ordinary C callbacks and do not depend on the PXA Guest SDK or WAMR.
The adapter presents those applications through the common `pxsys_runtime`
lifecycle and message interface.

`pxsys_native_client` is the native System API binding. It binds an app and
component identity once, then injects that principal into Intent and service
calls. Native app code cannot replace the caller field supplied to a request.
The same client binds topic publish/subscribe operations; subscriptions must be
removed before destroying the client.
