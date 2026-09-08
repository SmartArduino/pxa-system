# Product Integration

## Composition boundary

`pxsys_standard_system_create()` creates the registries, native runtime, task
manager, role host, theme service, renderer host, service registry and event
broker. It does not select a desktop, settings app, status bar, hardware driver
or storage policy.

A product composition component should:

1. create the standard system with product limits, allocator and policy hooks;
2. bind one renderer provider;
3. register native and installed PXA application descriptors;
4. register approved candidates for standard system roles;
5. register platform and product service providers;
6. start persistent roles and then launch the Home role.

Products that need different ownership or allocation rules may compose the
individual `pxsys::core` objects directly instead of using the standard root.

## Replaceable system UI

Desktop, Settings, status bar, navigation bar, lock screen, permission prompt,
package installer, app manager, file picker and theme provider are role IDs,
not linked singleton implementations. The role registry accepts native or PXA
application identities. Role authorization is a product policy decision.

The backend-neutral reference layout and optional LVGL implementation live
under `ui/reference/`. An ESP product can use the standard component or provide
its own implementation, for example:

```text
components/
  pxa_system/                 Git component or repository checkout
  product_system_profile/     composition and authorization policy
  product_system_ui/          native role providers and renderer assets
  product_device_services/    sensors, storage, audio and board services
```

An override registers a higher-priority authorized role candidate. It does not
replace symbols, patch the core, or introduce a second navigation protocol.
Both native and PXA implementations receive the same system Intent.

## Services and custom hardware

Standard interfaces use the reserved `system.*` namespace. Product extensions
use a reverse-domain identifier such as `com.example.sensor.air-quality` and a
semantic interface version. A provider exposes operations and feature bits
through `pxsys_service_provider_t`; native handles and ESP driver types remain
private to the provider.

Applications discover the interface and version they require. A missing custom
sensor therefore affects only applications that request it. Product policy
authorizes every call using the system-supplied caller identity.

## Themes

The theme service owns an atomic semantic snapshot. Applications and renderers
consume tokens such as background, surface, primary text and accent rather than
hard-coded LVGL colors. Light, dark and namespaced custom themes use the same
contract. A custom snapshot declares a light/dark base only for accessibility
and platform-chrome polarity; its semantic palette and metrics remain fully
independent. Products may replace persistence, theme sources and theme selection
UI while keeping the snapshot ABI.

## ESP-IDF dependency direction

```text
product UI/services -> PXA System contracts
ESP adapters        -> PXA System SPIs + ESP-IDF
PXA runtime         -> PXA System core + libpxa
LVGL renderer       -> renderer SPI + LVGL
PXA System core     -> C99 only
```

The portable component must never include an ESP-IDF, FreeRTOS, LVGL or WAMR
header in a public core header.
