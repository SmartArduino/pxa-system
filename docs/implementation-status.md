# Implementation Status

This page distinguishes implemented contracts from the remaining product
migration. The architecture documents describe the target; this page describes
the current branch.

## Implemented and host-tested

- The portable implementation, libpxa, Guest SDK, specifications and package
  tools now live under one self-contained `pxa-system/` distribution tree.
  Its root supports a normal CMake build, installable `pxsys::*` targets and a
  backend-neutral ESP-IDF component entry. Product-specific `app_pages` and ESP
  service bridges consume it from compatibility components outside the tree.

- `pxa_system_core`: portable C99 application identity and metadata registry,
  lifecycle state machine, runtime-provider broker, Intent resolver, stable
  Intent wire record, foreground task stack, system-role registry, theme
  service, renderer SPI, persistent role host, discoverable versioned
  service/RPC registry, and
  bounded topic event broker. Product sensors and drivers can register
  namespaced interfaces plus feature bits without exposing native handles.
- `pxa_system_native`: `native-static` provider. Native apps implement ordinary
  callbacks and do not include or link the PXA Guest SDK.
- `pxa_system_pxa`: verified libpxa manifest normalization, status translation,
  caller-principal derivation, a storage-independent catalog publish/update/
  unpublish gateway, and a runtime backend SPI for WAMR/other PXA engines.
- `pxa_system_standard`: configurable default composition root. Products may
  use it or compose the same contracts themselves.
- `pxa_system_renderer_headless`: non-LVGL reference renderer used to validate
  the backend-neutral surface contract.
- `pxa_system_renderer_lvgl`: graphical renderer adapter implementing the same
  surface, visibility, transaction and theme SPI. LVGL types remain in its
  optional native-surface extension rather than the portable core.
- `pxa_system_app_pages`: transitional adapter that registers all 15 current
  LVGL pages as native applications and maps Home, Settings, App Manager, and
  Package Installer to standard roles.
- `app_pages` has an optional navigation delegate. With no delegate installed,
  its previous navigation behavior is unchanged.
- Native and PXA callers enter the same navigation, RPC, and topic-event
  contracts through a shared identity-bound client. A cross-runtime test covers
  PXA-to-native RPC and PXA-to-native event delivery, including rejection of a
  forged caller identity. The same test registers a PXA descriptor as the
  Settings role provider, proving roles are runtime-neutral. Policy hooks
  authorize resolved navigation targets, services, event
  publication/subscription, and system-role claims.
- Role launches resolve the current provider and then enter the ordinary
  caller-bound Task/Intent path. Legacy Home, Settings, App Manager and Package
  Installer navigation now uses that operation, so a higher-priority approved
  native or PXA candidate can replace the built-in page without dispatch forks.
- Persistent roles such as status bar, navigation bar and lock screen run in a
  dedicated role host instead of the foreground Back stack. Replacement is
  transactional: an asynchronous candidate must finish starting before the
  previous provider is retired. Both providers use the same Runtime and Intent
  path as ordinary apps.
- Firmware UI startup now creates the standard system and installs the
  `app_pages` bridge after legacy callbacks are ready. Existing pages therefore
  navigate through the common Task/Intent path. Creation failure is logged and
  falls back to the previous navigation implementation.
- The SDL simulator now composes the same standard system, `app_pages` bridge,
  and LVGL renderer. Installed simulator packages are registered under their
  publisher-root/App-ID identity and launch through a `pxa-simulator` runtime
  provider, so native-to-PXA transitions and Back restoration use the common
  task stack. Initial and subsequent system Intents are delivered with the
  same Host-authenticated caller records as ESP. `--pxa-system-self-test`
  covers Home -> Apps -> PXA -> Apps.
- `--automation` now injects logical-coordinate click/swipe/Back, PXA launches,
  status fixtures, arbitrary event/service wire payloads and theme changes in
  process. PNG capture reads the final LVGL framebuffer directly, independent of
  desktop input and screenshot tooling. Example scripts cover Native and PXA
  transitions plus custom-theme system chrome and Toast rendering.
- The standard LVGL chrome now includes a separately trimmable notification
  shade and runtime-selectable button or gesture navigation. Gesture mode maps
  edge Back and bottom-up Home into the same task/runtime contracts used by
  buttons. Simulator automation can hold pointer phases to capture an
  in-progress, theme-colored Back indicator directly from the framebuffer.
- Card recents captures the final transformed application frame on Home-gesture
  release and animates that image directly into its task card. Persistent RGB565
  previews are clipped and downsampled to the release-time bounds, avoiding the
  old full-screen reset frame and reducing retained preview memory.
- Standard chrome and renderer surface containers no longer consume input in
  transparent or decorative regions. A simulator regression launches Arcade,
  delivers a real retained-widget click, verifies guest-internal Back, and then
  verifies system task closure.
- The firmware light/dark preference initializes and updates the portable theme
  service. The ESP PXA bridge observes that same service: new Guests receive
  the effective scheme in their startup UI environment, while active Guests
  receive `PXA_UI_ENVIRONMENT_CHANGED` on the PXA worker.
- The Theme snapshot now supports stable namespaced custom identities and a
  fully overridden semantic palette. The effective light/dark scheme remains
  metadata for accessibility and platform chrome, not a two-theme limit.
- Theme snapshots now also carry six backend-neutral typography roles from
  12 px caption text through 28 px display text. The reference LVGL UI accepts
  per-role font faces, while constrained products retain one/two-font fallback.
- The standard composition owns a canonical BCP 47 locale service and an
  overridable, priority-ordered resource catalog. The reference UI ships
  English and Simplified Chinese resources under `system.ui`; simulator startup
  and automation can change locale without restarting the system.
- The product `app_pages` compatibility bridge now receives the complete
  semantic palette, so custom themes apply to Native pages, Toasts and the edge
  Back indicator instead of being reduced to light/dark metadata.
- The ESP PXA adapter mirrors installed packages into the common registry,
  launches them through the common task manager, and reconciles asynchronous
  PXA start/exit notifications with the shared lifecycle.
- PXA Guest SDK and Host service `17` now route encoded `PXIN` navigation
  requests across the worker/UI-thread boundary into the same Intent resolver
  used by native applications. Caller identity is derived from the verified
  manifest and active component, not from Guest payload.
- Initial and subsequent Intents are delivered into a running PXA Guest as
  `PXA_SYSTEM_INTENT_EVENT`. The payload contains the unchanged `PXIN` record
  plus an optional Host-authenticated caller principal; failure to enqueue the
  initial event fails the launch instead of silently dropping it.
- The same gateway now supports PXA-to-native RPC and topic publication, topic
  subscribe/unsubscribe with native-to-PXA reliable event delivery, and PXA
  service endpoint registration with native-to-PXA calls and asynchronous PXA
  completion. Inbound service calls include the system-supplied native caller
  principal. Endpoint, subscription and pending-call cleanup follows the PXA
  app lifecycle.
- Runtime providers can append an asynchronous `request_stop` callback without
  breaking the original provider prefix. PXA instances remain in `STOPPING`
  until the worker reports the actual exit; task-stack and registry ownership
  are retained until that acknowledgement.
- The standard system now owns a renderer host rather than a concrete UI
  backend. A renderer can be bound or replaced while no surfaces are alive;
  surface lifecycle, transactions and atomic theme snapshots pass through the
  common host. The headless backend integration test uses this path.
- The ESP package store and POSIX installer now key packages, locks, owner
  records, private data and disabled policy by
  `<publisher-root-hex>~<app-id>`. Canonical external identity remains
  `<publisher-root-hex>:<app-id>`. Short App IDs are accepted only when lookup
  is unambiguous. Startup verifies every installed package before cataloging
  it and migrates verified legacy App-ID-only packages, owned private data,
  declared permission grants and disabled state.
- Inbox approval is bound atomically to the verified publisher root and App ID
  inside the installer, before any destination mutation. Replacing a staged
  source between preview and install cannot install a different identity.
- Most legacy pages now consume semantic theme tokens. Remaining fixed colors
  are content/status colors such as camera black, QR contrast, file-type icons
  and destructive/success indicators rather than light-theme structure.

All portable targets compile as C99 with warnings treated as errors. Their host
tests also pass under AddressSanitizer and UndefinedBehaviorSanitizer. The
legacy page adapter passes a strict syntax check against the repository's real
LVGL and `app_pages` headers.

The consolidated top-level CMake build currently runs 36 host tests, including
the backend-neutral simulator smoke test. The product SDL simulator separately
passes `--pxa-system-self-test` against the same relocated libraries.

## Remaining product integration

The PXA host facade is attached for catalog, launch, lifecycle, navigation,
RPC, bidirectional topic events and Intent delivery. Remaining product work is:

- move each legacy page's widget construction from its backend-specific native
  adapter to portable UI transactions where cross-renderer portability is
  required. Direct LVGL native apps remain a supported compatibility mode;
- product policy for publisher roots, role authorization and service access.
- product-specific selection and persistence UI for assigning installed apps
  to privileged system roles. The registry and role host deliberately require
  an authorized product decision rather than trusting a package claim.

The existing PXA ESP host still has one physical active-package execution slot.
The system Runtime and role host support concurrent instances, and the ESP
bridge tracks their logical lifecycles, but two simultaneously visible PXA
apps (for example a PXA status bar over a PXA foreground app) require a future
multi-instance WAMR Host. Native and PXA combinations already use the same role
and foreground orchestration contracts.

The simulator host currently has the same single-active-PXA limitation. A PXA
task is stopped when backgrounded behind a native task and restarted if it is
foregrounded again; the logical task and shared application identity remain in
the system stack. The remaining PXA System service gateway operations (RPC,
topics, and service endpoints) are implemented by the ESP bridge and remain to
be attached to the simulator's libpxa service registry.

## Compatibility status

All new contracts are draft `0.x`. `struct_size` is present for ABI growth, but
source and wire compatibility are not promised until the first conformance
suite and ESP integration are complete. Runtime type is already excluded from
application identity and must remain so.

## Destruction order

Applications and pending service calls must stop before destroying a system.
The standard composition root enforces the important preconditions and tears
down in this order: persistent role instances, tasks, roles, Intent filters,
runtime broker, native
runtime, service registry, event broker, renderer host, theme service, then
application registry. The ESP PXA bridge also rolls provider unregistration
back if its runtime cannot be destroyed, so a failed shutdown remains usable
and can be retried.
