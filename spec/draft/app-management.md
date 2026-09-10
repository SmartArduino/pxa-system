# PXA App Management Draft 0.1

App management is a Host policy surface. It is not part of the Guest Core ABI.
The Host keeps independent ownership domains for product-owned built-in
sources, user staging inboxes, verified executable slots, enable policy and
identity-scoped private data.

An App is launchable only when a verified `current` slot exists and its full
publisher/App identity is enabled. Catalog listing and activation both recheck
this rule.

| Operation | Built-in App | User App | Private data |
| --- | --- | --- | --- |
| install/update | boot sync or explicit | explicit | retained |
| disable/enable | allowed | allowed | retained |
| uninstall code | denied | allowed | retained |
| clear data | allowed when stopped | allowed when stopped | removed |

Uninstall holds the identity lock and removes every executable transaction
slot. It does not remove policy, a signed source, private data, cache or
secrets. The product UI confirms uninstall and clear-data operations.

An App with an active Component cannot be disabled, uninstalled or have data
cleared. A future service scheduler may add an explicit stop-and-wait
management transaction; Draft 0.1 does not race management with running Wasm.

## ESP32 storage

Zuowei Pai Touch uses one `assets` LittleFS partition. Product-owned built-in
sources live below `/assets/system/pxa/builtin`. Mutable App state has a single
root `/assets/pxa-state`, with fixed `packages/` transaction slots, user
`inbox/`, and identity-scoped `data/` ownership domains. Enable policy is
stored in NVS.

The system-assets updater replaces only `/assets/system` through its
`/assets/tmp` and `/assets/.previous` transaction directories, so a normal
online assets update retains `/assets/pxa-state`. Flashing or formatting the
whole assets partition clears both system assets and App state. Capacity is
shared dynamically; the updater must account for App state when reserving room
for a temporary system release.
